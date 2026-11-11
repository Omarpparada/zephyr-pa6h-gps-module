# Zephyr Module for Adafruit PA6H

A Zephyr driver for the Adafruit Ultimate GPS Breakout v3 (PA6H). The driver receives raw NMEA sentences over UART, validates their checksums, and hands them to the application. Applications can configure the receiver with PMTK commands through the public API.

## Repository Structure

```text
zephyr-pa6h-gps-module/
├── include/pa6h.h                         # Public API for setting up the receiver and reading sentences
├── zephyr/
│   ├── module.yml                         # Registers the repository as a Zephyr module
│   ├── Kconfig                            # Kconfig entry point
│   ├── dts/bindings/sensor/adafruit,pa6h.yaml
│   └── drivers/sensor/
│       ├── Kconfig                        # CONFIG_PA6H
│       ├── CMakeLists.txt                 # Builds the driver and exposes include/
│       └── pa6h.c                         # UART reception (ring buffer) and sentence parsing
└── samples/pa6h_uart/
	├── boards/nucleo_wl55jc.overlay       # PA6H on USART1 at 9600 baud
	├── prj.conf                           # Sample configuration
	├── CMakeLists.txt                     # Configures the Zephyr application
	├── Makefile                           # Build, flash and monitor shortcuts
	└── src/main.c                         # Prints GGA sentences and reconfigures the GPS if needed
```

## Wiring

The sample overlay (`boards/nucleo_wl55jc.overlay`) uses `&usart1`. Connect both UART directions:

- PA6H TX to USART1 RX (PB7 / Arduino D0).
- USART1 TX (PB6 / Arduino D1) to PA6H RX.
- PA6H GND to board GND.
- Power according to the breakout board's electrical requirements (5V).

The TX connection is required to send PMTK commands to the PA6H. Check your board documentation to confirm that USART1 is enabled, its pins match your wiring, and it is not shared with the serial console. If you use another UART, change the node in the overlay. The configured baud rate is 9600 and must match the receiver.

```dts
pa6h: pa6h {
	compatible = "adafruit,pa6h";
	status = "okay";
};
```

## Integration

The sample adds this repository to `EXTRA_ZEPHYR_MODULES` before calling `find_package(Zephyr)`. Zephyr reads `module.yml`, loads the binding, and processes the overlay node with `compatible = "adafruit,pa6h"`.

The driver activation flow is:

```text
overlay: compatible = "adafruit,pa6h"
  └─ binding: generates DT_HAS_ADAFRUIT_PA6H_ENABLED
	   └─ Kconfig: CONFIG_PA6H depends on the node and SERIAL/UART_INTERRUPT_DRIVEN, and selects RING_BUFFER
			└─ CMake: builds pa6h.c when CONFIG_PA6H is enabled
```

## Building

From a Zephyr workspace with `ZEPHYR_BASE` configured and `west` available:

```sh
west build -b <board> samples/pa6h_uart
west flash
```

Replace `<board>` with the board you are using. The sample overlay requires that the board define a `usart1` node; otherwise, adapt the overlay to a UART available on your board.

## Reading NMEA Sentences

The public interface is in `include/pa6h.h`. The application gets the device by its compatible and calls `pa6h_read_sentence()`:

```c
const struct device *gps = DEVICE_DT_GET_ONE(adafruit_pa6h);
pa6h_nmea_sentence sentence;

if (device_is_ready(gps) && pa6h_read_sentence(gps, &sentence, K_MSEC(500)) == 0) {
	printk("%s\n", sentence);
}
```

`pa6h_nmea_sentence` is a `char` array of `PA6H_NMEA_SENTENCE_MAX` (128) bytes. On success it holds a null-terminated sentence without `CR`/`LF`.

`pa6h_read_sentence()` blocks for up to `timeout` in total, even if the sentence arrives in several UART chunks. It returns `0` when it provides a checksum-valid sentence and `-EAGAIN` if none arrives before the timeout (`K_NO_WAIT` polls once). Sentences with an invalid checksum are discarded silently, and the function does not filter by sentence type.

### Reception design

The UART interrupt handler only drains the UART FIFO into a 256-byte ring buffer and gives a semaphore when a line ending (`LF`) arrives, or when the buffer overflows, so the reader wakes once per sentence instead of once per byte. Line assembly, `CR`/`LF` removal and checksum validation run in the thread that calls `pa6h_read_sentence()`, so the interrupt stays short. A partial sentence remains in the ring buffer until its `LF` arrives.

Only one thread should call `pa6h_read_sentence()` per device; the driver does not lock the ring buffer for several readers.

## Sending PMTK Commands

PMTK commands configure the GPS over UART.

The public API is declared in `include/pa6h.h`. `pa6h_send_command()` transmits the supplied string exactly as provided: it does not calculate the checksum or append a line ending. Some commands are already declared in `include/pa6h.h`:

Select which NMEA sentence types the receiver outputs with a `PMTK314` command:

| Macro | Output |
| --- | --- |
| `PMTK_SET_NMEA_OUTPUT_GGA` | GGA only |
| `PMTK_SET_NMEA_OUTPUT_RMC` | RMC only |
| `PMTK_SET_NMEA_OUTPUT_GLL` | GLL only |
| `PMTK_SET_NMEA_OUTPUT_VTG` | VTG only |
| `PMTK_SET_NMEA_OUTPUT_GSA` | GSA only |
| `PMTK_SET_NMEA_OUTPUT_GSV` | GSV only |
| `PMTK_SET_NMEA_OUTPUT_RMC_GGA` | RMC and GGA |
| `PMTK_SET_NMEA_OUTPUT_RMC_GGA_GSA` | RMC, GGA, and GSA |
| `PMTK_SET_NMEA_OUTPUT_ALL` | Standard sentence set |
| `PMTK_SET_NMEA_OUTPUT_OFF` | Disable sentence output |

Set the NMEA transmission interval with a `PMTK220` macro:

| Macro | Interval |
| --- | --- |
| `PMTK_SET_NMEA_UPDATE_1HZ` | 1 second (1 Hz) |
| `PMTK_SET_NMEA_UPDATE_2HZ` | 500 ms (2 Hz) |
| `PMTK_SET_NMEA_UPDATE_5HZ` | 200 ms (5 Hz) |
| `PMTK_SET_NMEA_UPDATE_10HZ` | 100 ms (10 Hz) |

Example: request GGA only at 1 Hz after obtaining the PA6H device:

```c
pa6h_send_command(gps, PMTK_SET_NMEA_OUTPUT_GGA);
pa6h_send_command(gps, PMTK_SET_NMEA_UPDATE_1HZ);
```

The receiver acknowledges a recognized command with a sentence such as `$PMTK001,314,3*36`; command `314` identifies the sentence-output configuration and result `3` indicates success. The driver does not currently wait for or verify the acknowledgement automatically.

### Recovering from a power cycle

The sample keeps only GGA sentences. If the GPS loses power while running, it falls back to its default output (RMC, GGA, GSA, ...). `src/main.c` detects this: any sentence that is neither GGA nor a `$PMTK` acknowledgement triggers `configure_gps()`, which sends the two commands above again.

`$PMTK` sentences are ignored because they are the receiver's replies to those commands. Non-GGA sentences may still arrive for a moment after the commands are sent, so a reconfiguration can be repeated a few times before the receiver settles.

## Current Limits

- A line, including its `CR` and `LF`, must fit in 127 bytes. A longer line without `LF` has its first 127 bytes discarded; the tail then fails the checksum and is dropped too.
- The ring buffer holds 256 bytes (`PA6H_RX_BUFFER_SIZE`). If the application does not read fast enough, new bytes are dropped and the checksum rejects the damaged sentence.
- Only one reader thread per device is supported.
- The driver validates the NMEA checksum but does not parse fields or filter sentence types in software.

## References
https://nmeachecksum.eqth.net/
https://cdn-shop.adafruit.com/datasheets/PMTK_A11.pdf
https://cdn-shop.adafruit.com/datasheets/PMTK_A08.pdf
https://github.com/adafruit/Adafruit_GPS/blob/307428f53931791799f169d07fdbf9c4c478fc15/src/Adafruit_PMTK.h

