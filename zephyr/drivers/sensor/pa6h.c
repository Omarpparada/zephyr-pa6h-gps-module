#define DT_DRV_COMPAT adafruit_pa6h

#include <pa6h.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(pa6h, CONFIG_SENSOR_LOG_LEVEL);

#define PA6H_NMEA_QUEUE_DEPTH 8

struct pa6h_config
{
    const struct device *uart;
    struct uart_config uart_config;
    const char *nmea_output_command;
};

struct pa6h_data
{
    struct k_msgq *sentence_queue;
    const struct pa6h_config *config;
    char line[PA6H_NMEA_SENTENCE_MAX];
    size_t line_len;
    bool line_overflow;
};

/**
 * Checks if a NMEA sentence is valid using its checksum.
 *
 * @param line The NMEA sentence to check.
 * @param len The length of the NMEA sentence.
 * @return true if the sentence is valid, false otherwise.
 */
static bool pa6h_check_sentence(const char *line, size_t len)
{
    // Check that the sentence starts with '$'
    if (line[0] != '$')
    {
        LOG_DBG("Invalid NMEA sentence: does not start with '$'");
        return false;
    }

    // Find the asterisk indicating the start of the checksum
    const char *checksum_at = strchr(line, '*');
    // If there is no asterisk or the checksum is incomplete, the sentence is invalid
    if (!checksum_at || checksum_at + 3 > line + len)
    {
        LOG_DBG("Invalid NMEA sentence: missing or incomplete checksum");
        return false;
    }

    // Calculate checksum (XOR of all characters between '$' and '*')
    uint8_t checksum = 0;
    for (const char *p = line + 1; p < checksum_at; p++)
    {
        checksum ^= (uint8_t)(*p);
    }

    // Parse checksum from sentence
    uint8_t sentence_checksum;
    if (sscanf(checksum_at + 1, "%2hhX", &sentence_checksum) != 1)
    {
        LOG_DBG("Invalid NMEA sentence: failed to parse checksum");
        return false;
    }

    return checksum == sentence_checksum;
}

/**
 * UART interrupt service routine for the PA6H module.
 *
 * This ISR reads incoming data from the UART, assembles NMEA sentences,
 * and places complete sentences into the message queue.
 *
 * @param uart The UART device.
 * @param user_data Pointer to the PA6H data structure.
 */
static void pa6h_uart_isr(const struct device *uart, void *user_data)
{
    struct pa6h_data *data = user_data;
    uint8_t buffer[16];

    uart_irq_update(uart);
    while (uart_irq_rx_ready(uart))
    {
        // Read data from the UART into the buffer (16 bytes max)
        int count = uart_fifo_read(uart, buffer, sizeof(buffer));
        // If no data was read, exit the loop
        if (count <= 0)
        {
            break;
        }
        // Process each byte read
        for (int index = 0; index < count; index++)
        {
            char value = (char)buffer[index];
            // Check for newline character indicating end of NMEA sentence
            if (value == '\n')
            {
                // If the line is not overflowing and has content, process it as a complete sentence
                if (!data->line_overflow && data->line_len > 0)
                {
                    // Null-terminate the line to form a complete NMEA sentence
                    data->line[data->line_len] = '\0';
                    struct pa6h_nmea_sentence sentence = {
                        .length = data->line_len,
                    };
                    // Copy the line into the sentence data buffer
                    memcpy(sentence.data, data->line, data->line_len + 1);
                    // If full, discard the oldest sentence to make room.
                    if (k_msgq_put(data->sentence_queue, &sentence, K_NO_WAIT) != 0)
                    {
                        struct pa6h_nmea_sentence oldest;

                        (void)k_msgq_get(data->sentence_queue, &oldest, K_NO_WAIT);
                        (void)k_msgq_put(data->sentence_queue, &sentence, K_NO_WAIT);
                    }
                }
                // Reset the line buffer for the next sentence
                data->line_len = 0;
                data->line_overflow = false;
            }
            // If the character is not a carriage return and the line is not overflowing, add it to the line buffer
            else if (value != '\r' && !data->line_overflow)
            {
                if (data->line_len < sizeof(data->line) - 1)
                {
                    // Add the character to the line buffer
                    data->line[data->line_len++] = value;
                }
                else
                {
                    // Mark the line as overflowing if the buffer is full
                    data->line_overflow = true;
                }
            }
        }
    }
}

int pa6h_read_sentence(const struct device *dev, struct pa6h_nmea_sentence *sentence)
{
    struct pa6h_data *data;
    int err;
    // Validate input parameters
    if (dev == NULL || sentence == NULL)
    {
        LOG_ERR("Invalid input parameters");
        return -EINVAL;
    }

    data = dev->data;
    // Keep reading sentences until a valid one is found
    do
    {
        err = k_msgq_get(data->sentence_queue, sentence, K_NO_WAIT);
        if (err < 0)
        {
            return err == -ENOMSG ? -EAGAIN : err;
        }
    } while (!pa6h_check_sentence(sentence->data, sentence->length));

    return 0;
}

void pa6h_send_command(const struct device *dev, const char *command)
{
    const struct pa6h_config *config = dev->config;
    while(*command != '\0')
    {
        uart_poll_out(config->uart, *command);
        command++;
    }
}

static int pa6h_init(const struct device *dev)
{
    const struct pa6h_config *config = dev->config;
    struct pa6h_data *data = dev->data;
    int err;

    if (!device_is_ready(config->uart))
    {
        LOG_ERR("UART device not ready");
        return -ENODEV;
    }

    data->config = config;

    err = uart_configure(config->uart, &config->uart_config);
    if (err < 0)
    {
        LOG_ERR("Failed to configure UART");
        return err;
    }
    // Configure the UART interrupt with the PA6H ISR and user data
    err = uart_irq_callback_user_data_set(config->uart, pa6h_uart_isr, data);
    if (err < 0)
    {
        LOG_ERR("Failed to set UART IRQ callback");
        return err;
    }
    // Enable UART RX interrupt to start receiving data from the PA6H module
    uart_irq_rx_enable(config->uart);

    return 0;
}

#define PA6H_INIT(inst)                                                         \
    K_MSGQ_DEFINE(pa6h_sentence_queue_##inst,                                   \
                  sizeof(struct pa6h_nmea_sentence), PA6H_NMEA_QUEUE_DEPTH, 4); \
    static struct pa6h_data pa6h_data_##inst = {                                \
        .sentence_queue = &pa6h_sentence_queue_##inst,                          \
    };                                                                          \
    static const struct pa6h_config pa6h_config_##inst = {                      \
        .uart = DEVICE_DT_GET(DT_PARENT(DT_DRV_INST(inst))),                    \
        .uart_config = {                                                        \
            .baudrate = DT_INST_PROP(inst, current_speed),                      \
            .parity = UART_CFG_PARITY_NONE,                                     \
            .stop_bits = UART_CFG_STOP_BITS_1,                                  \
            .data_bits = UART_CFG_DATA_BITS_8,                                  \
            .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,                               \
        },                                                                      \
    };                                                                          \
    DEVICE_DT_INST_DEFINE(inst, pa6h_init, NULL, &pa6h_data_##inst,             \
                          &pa6h_config_##inst, POST_KERNEL,                     \
                          CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(PA6H_INIT)