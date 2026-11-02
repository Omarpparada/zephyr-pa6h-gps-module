# Zephyr Module for Adafruit PA6H

A Zephyr driver for the Adafruit Ultimate GPS Breakout v3 (PA6H). Its Devicetree binding selects an NMEA output profile. The driver receives raw sentences over UART and validates checksums.

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
│       └── pa6h.c                         # UART reception and sentence queue
└── samples/pa6h_uart/
	├── app.overlay                        # PA6H on USART1 at 9600 baud
	├── prj.conf                           # Sample configuration
	├── CMakeLists.txt                     # Configures the Zephyr application
	└── src/main.c                         # Prints received sentences
```

## Wiring

The sample overlay uses `&usart1`. Connect both UART directions:

- PA6H TX to USART1 RX (PB7 / Arduino D0).
- USART1 TX (PB6 / Arduino D1) to PA6H RX.
- PA6H GND to board GND.
- Power according to the breakout board's electrical requirements (5V).

The TX connection is required to send the selected PMTK314 command to the PA6H. Set the desired output using the `nmea-output` property in the PA6H Devicetree node. Check your board documentation to confirm that USART1 is enabled, its pins match your wiring, and it is not shared with the serial console. If you use another UART, change the node in the overlay. The configured baud rate is 9600 and must match the receiver.

Available `nmea-output` values are `GLL`, `RMC`, `VTG`, `GGA`, `GSA`, `GSV`, `RMC_GGA`, `RMC_GGA_GSA`, `ALL`, and `OFF`. For example:

```dts
pa6h: pa6h {
	compatible = "adafruit,pa6h";
	current-speed = <9600>;
	nmea-output = "GGA";
	status = "okay";
};
```

The driver currently intends to send the selection command after detecting the PA6H startup sentence `$PMTK010,001*2E`.

## Integration

The sample adds this repository to `EXTRA_ZEPHYR_MODULES` before calling `find_package(Zephyr)`. Zephyr reads `module.yml`, loads the binding, and processes the overlay node with `compatible = "adafruit,pa6h"`.

The driver activation flow is:

```text
app.overlay: compatible = "adafruit,pa6h"
  └─ binding: generates DT_HAS_ADAFRUIT_PA6H_ENABLED
	   └─ Kconfig: CONFIG_PA6H depends on the node and SERIAL/UART_INTERRUPT_DRIVEN
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
struct pa6h_nmea_sentence sentence;

if (device_is_ready(gps) && pa6h_read_sentence(gps, &sentence) == 0) {
	printk("%s\n", sentence.data);
}
```

The function is non-blocking. It returns `0` when it provides a checksum-valid sentence and `-EAGAIN` when no complete sentence is queued yet. `sentence.length` contains the number of received bytes.

The driver uses UART interrupts to collect bytes and separates sentences at `LF`; it also removes `CR`. `pa6h_read_sentence()` discards sentences with an invalid checksum, but does not filter by sentence type. The selected PMTK314 profile is expected to control which NMEA sentence types the receiver transmits.

## Current Limits

- Each sentence can contain up to 127 bytes; the remaining buffer byte is reserved for `\0`.
- An overlong line is discarded in its entirety.
- The queue holds up to 8 sentences. If it is full, the oldest sentence is discarded.
- The driver validates the NMEA checksum but does not parse fields or filter sentence types in software.
