#ifndef PA6H_H_
#define PA6H_H_

#include <zephyr/device.h>
#include <stddef.h>
#include <stdint.h>

#define PA6H_NMEA_SENTENCE_MAX 128

struct pa6h_nmea_sentence {
	size_t length;
	char data[PA6H_NMEA_SENTENCE_MAX];
};

/* Non-blocking; returns -EAGAIN when no complete NMEA sentence is queued. */
int pa6h_read_sentence(const struct device *dev,
		       struct pa6h_nmea_sentence *sentence);

#endif /* PA6H_H_ */