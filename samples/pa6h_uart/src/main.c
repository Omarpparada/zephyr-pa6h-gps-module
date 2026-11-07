#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <pa6h.h>

static void configure_gps(const struct device *gps)
{
	// GGA sentences only, at 1 Hz
	pa6h_send_command(gps, PMTK_SET_NMEA_OUTPUT_GGA);
	pa6h_send_command(gps, PMTK_SET_NMEA_UPDATE_1HZ);
}

int main(void)
{
	const struct device *gps = DEVICE_DT_GET_ONE(adafruit_pa6h);
	pa6h_nmea_sentence sentence;

	if (!device_is_ready(gps)) {
		printk("PA6H device not ready\n");
		return 0;
	}
	// Allow the GPS module to initialize before sending commands
	k_sleep(K_SECONDS(1));
	configure_gps(gps);

	printk("PA6H ready; forwarding raw NMEA sentences\n");
	while (true) {
		int err = pa6h_read_sentence(gps, &sentence, K_MSEC(1200));
		if (err == 0) {
			if (strncmp(sentence + 3, "GGA", 3) == 0) {
				printk("%s\n", sentence);
			} else if (strncmp(sentence, "$PMTK", 5) != 0) {
				// A power cycle makes the module fall back to its default output
				printk("Unexpected sentence, reconfiguring PA6H\n");
				configure_gps(gps);
			}
		} else if (err != -EAGAIN) {
			printk("Could not read NMEA sentence: %d\n", err);
		}
	}
}