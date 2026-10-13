# Proposed bench wiring

This is a software pin proposal for a DevKitC-style ESP32-S3 N16R8, not confirmation
of your actual board. Check exposed pins and voltage compatibility first. Buttons
are active-low to GND, with internal pull-ups. All peripheral grounds must share a
reference. ESP32 GPIO is 3.3 V logic.

| Module / connection | ESP32-S3 GPIO | Default |
|---|---|---|
| MPU6050 SDA / SCL | 8 / 9 | Used by hardware profile, address 0x68 |
| Cancel button / SOS button | 21 / 14 | INPUT_PULLUP |
| Active buzzer driver input | 39 | Disabled |
| Vibration motor driver input | 40 | Disabled |
| Second motor driver | Unassigned (-1) | Disabled |
| Status LED through resistor | 41 | Disabled |
| GPS module TX → MCU RX | 18 | Disabled, 9600 baud |
| GPS module RX ← MCU TX | 17 | Disabled |
| LTE module TX → MCU RX | 16 | Disabled, 115200 baud |
| LTE module RX ← MCU TX | 15 | Disabled |
| SD SCK / MOSI / MISO / CS | 12 / 11 / 13 / 10 | Disabled, SPI 4 MHz |

Use transistor/MOSFET drivers sized for the loads, flyback protection for motors,
and appropriate power supplies. Do not power motors or LTE from a GPIO. A modem
breakout may require level conversion and a separate supply capable of its current
peaks; a raw module and a development board can have different interfaces. Consult
the exact module documentation. The buzzer implementation expects an active buzzer;
it does not generate an audio-frequency signal for a passive transducer.

USB pins 19/20, flash/PSRAM-related or unexposed 22–37, and strapping pins
0/3/45/46 are deliberately excluded by this proposal's compile-time checks.
A different board may need a different verified map. No helmet drilling or
structural changes are needed for these bench tests.

## Bring-up order and acceptance checks

1. USB-only BENCH profile: all four host-demo scenarios; serial commands; no outputs.
2. Hardware profile, MPU6050 + buttons: normal IMU status, loss when unplugged,
   recovery when reconnected, SOS with sensor absent; no impact testing on a person.
3. Add driver-connected outputs: verify polarity, immediate countdown pattern,
   cancellation and continuous-running thermal/current behaviour.
4. Add GPS: valid fix vs missing fix, then let the fix age and confirm OLD LOC.
5. Add SD: decode CRC records; remove card during logging; confirm alerts continue.
6. Add the exact LTE board with SMS disabled first: validate power, voltage levels,
   baud, command mode, SIM readiness, registration and supported AT commands.
7. Only then configure a consenting test recipient and enable real TEST SMS in
   the hardware profile. Measure actual network outcomes and power behaviour.

Software flags go in `include/local_config.h`; proposed pins are in
`include/board_config.h`. Keep credentials and recipient out of shared packages.

## Dependencies pinned for reproducibility

- PlatformIO Core 6.2.0; Espressif32 platform 6.10.0; Arduino ESP32 2.0.17.
- Links2004 WebSockets 2.6.1; TinyGPSPlus 1.0.3.
- Python websocket-client 1.8.0 for the synthetic sender only.

Source references used for the adapters/build configuration:

- https://docs.platformio.org/en/latest/boards/espressif32/esp32-s3-devkitc-1.html
- https://github.com/platformio/platform-espressif32/releases/tag/v6.10.0
- https://github.com/Links2004/arduinoWebSockets
- https://github.com/mikalhart/TinyGPSPlus
- https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map.pdf
- https://en.simcom.com/product/A7670X.html
