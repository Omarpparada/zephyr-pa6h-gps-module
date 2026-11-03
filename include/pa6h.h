#ifndef PA6H_H_
#define PA6H_H_

#include <zephyr/device.h>
#include <stddef.h>
#include <stdint.h>

#define PA6H_NMEA_SENTENCE_MAX 128

/**
 * Commands to configure the PA6H module to output specific NMEA sentences.
 * References:
 * https://cdn-shop.adafruit.com/datasheets/PMTK_A08.pdf
 * https://github.com/adafruit/Adafruit_GPS/blob/307428f53931791799f169d07fdbf9c4c478fc15/src/Adafruit_PMTK.h
 */
#define PMTK_SET_NMEA_OUTPUT_GLL "$PMTK314,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*29\r\n"         // turn on only the GPGLL sentence
#define PMTK_SET_NMEA_OUTPUT_RMC "$PMTK314,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*29\r\n"         // turn on only the GPRMC sentence
#define PMTK_SET_NMEA_OUTPUT_VTG "$PMTK314,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*29\r\n"         // turn on only the GPVTG
#define PMTK_SET_NMEA_OUTPUT_GGA "$PMTK314,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*29\r\n"         // turn on just the GPGGA
#define PMTK_SET_NMEA_OUTPUT_GSA "$PMTK314,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0*29\r\n"         // turn on just the GPGSA
#define PMTK_SET_NMEA_OUTPUT_GSV "$PMTK314,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0*29\r\n"         // turn on just the GPGSV
#define PMTK_SET_NMEA_OUTPUT_RMC_GGA "$PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*28\r\n"     // turn on GPRMC and GPGGA
#define PMTK_SET_NMEA_OUTPUT_RMC_GGA_GSA "$PMTK314,0,1,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0*29\r\n" // turn on GPRMC, GPGGA and GPGSA
#define PMTK_SET_NMEA_OUTPUT_ALL "$PMTK314,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0*28\r\n"         // turn on ALL THE DATA
#define PMTK_SET_NMEA_OUTPUT_OFF "$PMTK314,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*28\r\n"         // turn off output
#define PMTK_SET_NMEA_UPDATE_1HZ "$PMTK220,1000*1F\r\n" // set NMEA output rate to 1 Hz
#define PMTK_SET_NMEA_UPDATE_2HZ "$PMTK220,500*2B\r\n"  // set NMEA output rate to 2 Hz
#define PMTK_SET_NMEA_UPDATE_5HZ "$PMTK220,200*2C\r\n"  // set NMEA output rate to 5 Hz
#define PMTK_SET_NMEA_UPDATE_10HZ "$PMTK220,100*2F\r\n" // set NMEA output rate to 10 Hz

struct pa6h_nmea_sentence {
	size_t length;
	char data[PA6H_NMEA_SENTENCE_MAX];
};

/* Non-blocking; returns -EAGAIN when no complete NMEA sentence is queued. */
int pa6h_read_sentence(const struct device *dev,
		       struct pa6h_nmea_sentence *sentence);
void pa6h_send_command(const struct device *dev, const char *command);

#endif /* PA6H_H_ */