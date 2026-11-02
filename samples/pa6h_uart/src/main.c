#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <pa6h.h>

int main(void)
{
	const struct device *gps = DEVICE_DT_GET_ONE(adafruit_pa6h);
	struct pa6h_nmea_sentence sentence;

	if (!device_is_ready(gps)) {
		printk("PA6H device not ready\n");
		return 0;
	}

	printk("PA6H ready; forwarding raw NMEA sentences\n");
	while (true) {
		int err = pa6h_read_sentence(gps, &sentence);

		if (err == 0) {
			printk("%s\n", sentence.data);
		} else if (err != -EAGAIN) {
			printk("Could not read NMEA sentence: %d\n", err);
		} else {
			k_sleep(K_MSEC(10));
		}
	}

	return 0;
}