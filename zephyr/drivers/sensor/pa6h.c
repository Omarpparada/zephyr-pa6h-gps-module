#define DT_DRV_COMPAT adafruit_pa6h

#include <pa6h.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(pa6h, CONFIG_SENSOR_LOG_LEVEL);

#define PA6H_RX_BUFFER_SIZE 256

struct pa6h_config
{
    const struct device *uart;
    struct uart_config uart_config;
};

struct pa6h_data
{
    const struct pa6h_config *config;
    struct ring_buf rx_buffer;
    struct k_sem rx_sem;
};

/**
 * Checks if a NMEA sentence is valid using its checksum.
 *
 * @param line The NMEA sentence to check.
 * @return true if the sentence is valid, false otherwise.
 */
static bool pa6h_check_sentence(const char *line)
{
    // Check that there is a sentence and it starts with '$'
    if (line == NULL || line[0] != '$')
    {
        LOG_DBG("No sentence or does not start with '$'");
        return false;
    }
    // Check that the sentence has only a '$'
    if (strchr(line + 1, '$') != NULL)
    {
        LOG_DBG("Invalid NMEA sentence: contains multiple '$' characters");
        return false;
    }
    size_t len = strlen(line);

    // Find the asterisk indicating the start of the checksum
    const char *checksum_at = strchr(line, '*');
    // If there is no asterisk or the checksum is incomplete, the sentence is invalid
    if (!checksum_at || len - (size_t)(checksum_at - line) != 3)
    {
        LOG_DBG("Invalid NMEA sentence: missing or incomplete checksum");
        return false;
    }

    // Parse checksum from sentence
    uint8_t sentence_checksum;
    if (hex2bin(checksum_at + 1, 2, &sentence_checksum, sizeof(sentence_checksum)) != 1)
    {
        LOG_DBG("Invalid NMEA sentence: failed to parse checksum");
        return false;
    }

    // Calculate checksum (XOR of all characters between '$' and '*')
    uint8_t checksum = 0;
    for (const char *p = line + 1; p < checksum_at; p++)
    {
        checksum ^= (uint8_t)(*p);
    }

    return checksum == sentence_checksum;
}

/**
 * UART interrupt service routine for the PA6H module.
 *
 * Only drains the UART FIFO into the ring buffer and wakes the reader once a
 * complete line ('\n') has arrived, or when bytes are dropped so it can drain.
 * Parsing and validation happen in pa6h_read_sentence().
 *
 * @param uart The UART device.
 * @param user_data Pointer to the PA6H data structure.
 */
static void pa6h_uart_isr(const struct device *uart, void *user_data)
{
    struct pa6h_data *data = user_data;
    uint8_t buffer[16];
    bool wake = false;

    if (!uart_irq_update(uart))
        return;
    while (uart_irq_rx_ready(uart))
    {
        int count = uart_fifo_read(uart, buffer, sizeof(buffer));
        if (count <= 0)
            break;
        // If overflow -> drop bytes
        if (ring_buf_put(&data->rx_buffer, buffer, count) < (uint32_t)count)
            wake = true;
        if (memchr(buffer, '\n', count) != NULL)
            wake = true;
    }
    if (wake)
        k_sem_give(&data->rx_sem);
}

int pa6h_read_sentence(const struct device *dev, pa6h_nmea_sentence *sentence, k_timeout_t timeout)
{
    
    // Validate input parameters
    if (dev == NULL || sentence == NULL)
    {
        LOG_ERR("Invalid input parameters");
        return -EINVAL;
    }
    
    struct pa6h_data *data = dev->data;
    k_timepoint_t end = sys_timepoint_calc(timeout);
    char *line = *sentence;

    while (true)
    {
        // Peek without consuming: a partial sentence stays in the ring buffer
        size_t available = ring_buf_peek(&data->rx_buffer, (uint8_t *)line, sizeof(pa6h_nmea_sentence) - 1);
        // Search for a newline character
        char *newline = memchr(line, '\n', available);

        if (newline)
        {
            // Calculate the length of the line up to the newline character
            size_t len = (size_t)(newline - line);
            // Consume the line including the '\n' char from the buffer
            (void)ring_buf_get(&data->rx_buffer, NULL, len + 1);
            // Remove the '\n' char from the length if present
            if (len > 0 && line[len - 1] == '\r')
                len--;
            // Terminate the line
            line[len] = '\0';
            if (pa6h_check_sentence(line))
                return 0;
            continue;
        }

        // If there is not a '\n' in the line, discard it from the buffer
        if (available == sizeof(pa6h_nmea_sentence) - 1)
        {
            (void)ring_buf_get(&data->rx_buffer, NULL, available);
            continue;
        }

        // Wait for the ISR to deliver more bytes. If the timeout expires, return -EAGAIN
        if (k_sem_take(&data->rx_sem, sys_timepoint_timeout(end)) < 0)
            return -EAGAIN;
    }
}

void pa6h_send_command(const struct device *dev, const char *command)
{
    const struct pa6h_config *config = dev->config;
    while (*command != '\0')
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

#define PA6H_INIT(inst)                                               \
    static uint8_t pa6h_rx_storage_##inst[PA6H_RX_BUFFER_SIZE];       \
    static struct pa6h_data pa6h_data_##inst = {                      \
        .rx_buffer = RING_BUF_INIT(pa6h_rx_storage_##inst,            \
                                   sizeof(pa6h_rx_storage_##inst)),   \
        .rx_sem = Z_SEM_INITIALIZER(pa6h_data_##inst.rx_sem, 0, 1),   \
    };                                                                \
    static const struct pa6h_config pa6h_config_##inst = {            \
        .uart = DEVICE_DT_GET(DT_PARENT(DT_DRV_INST(inst))),          \
        .uart_config = {                                              \
            .baudrate = DT_N_S_soc_S_serial_40013800_P_current_speed, \
            .parity = UART_CFG_PARITY_NONE,                           \
            .stop_bits = UART_CFG_STOP_BITS_1,                        \
            .data_bits = UART_CFG_DATA_BITS_8,                        \
            .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,                     \
        },                                                            \
    };                                                                \
    DEVICE_DT_INST_DEFINE(inst, pa6h_init, NULL, &pa6h_data_##inst,   \
                          &pa6h_config_##inst, POST_KERNEL,           \
                          CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(PA6H_INIT)