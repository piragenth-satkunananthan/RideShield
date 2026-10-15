# RideShield main controller: NodeMCU-32S

Open this directory as a PlatformIO project. It targets the **original ESP32**,
not ESP32-S3. The portable core is copied from
`dev_version/rideshield-core/lib/RideShieldCore/src/`; synchronize fixes to both
copies until the projects share a single library source.

## First hardware bring-up

1. With nothing attached, power the NodeMCU-32S over USB and run
   `pio run -e nodemcu32s_bringup`. Upload with
   `pio run -e nodemcu32s_bringup -t upload`, then open the 115200-baud monitor.
2. Disconnect power. Check the **exact** MPU-9250 breakout's voltage rating. For
   a verified 3.3 V-compatible module, connect VCC to 3V3, GND to GND, SDA to
   GPIO21 and SCL to GPIO22. The default address is `0x68` (AD0 low); if your
   breakout has AD0 high, set `board::MPU_ADDRESS` to `0x69` in
   `include/board_config.h`. Leave other modules disconnected.
3. Power over USB again. Type `status` and press Enter in the serial monitor.
   The boot line reports the available black-box buffer. Gently tilt and rotate
   the module; confirm `IMU=1` and changing readings. A still sensor should
   read about 1 g of total acceleration. `imu_id=0x71` identifies the expected
   MPU-9250; `imu_id=0x70` indicates MPU-6500-class motion silicon and is
   supported for accelerometer/gyroscope readings. This ID alone cannot prove
   whether a magnetometer is present or working. If it reports `imu_id=0x00`,
   check power, wiring, and I2C address.
4. The controller hosts a bench Wi-Fi access point with the RSW1 WebSocket
   gateway on port 81. Its default password and 32-byte token are placeholders;
   put private replacements in ignored `include/local_config.h` before field use.

## Connected-sensor diagnostics

With the MPU module already working, a VL53L0X breakout may share SDA (GPIO21)
and SCL (GPIO22), with common ground and a supply rated for that breakout.
The KY-033 `S`/`OUT` signal may connect to GPIO36, `-` to GND and `+` to a
verified 3.3 V supply. Inspect the labels on your own module before powering.
After upload, wait a second and type `status` twice: `TOF_ACK=1` means a device
acknowledged I2C address `0x29`; `IR_RAW=0/1` shows the digital pin level.
Try changing the KY-033's view of a test surface and check the raw level.
`TOF_ACK` does **not** verify distance measurements; `IR_RAW` does **not** verify
eye closure. Neither diagnostic currently changes the crash state machine.

The default build uses `RS_BENCH=0` and reads accelerometer and gyroscope
samples from either ID above; a magnetometer is not used. The firmware does not
inject synthetic impacts. It prints `SMS DISABLED` for an incident and never sends a
live SMS. The phone app and camera boards are separate work. Do not test by
dropping a wearable helmet or simulating a real collision.

## Provisional pin map

| Function | GPIO | Default |
|---|---:|---|
| MPU-9250 SDA / SCL | 21 / 22 | Enabled |
| VL53L0X SDA / SCL | 21 / 22 | Address probe only |
| KY-033 signal | 36 | Raw digital input only |
| Cancel / SOS buttons | 32 / 33 | Pull-up inputs |
| Buzzer / left haptic driver / LED | 25 / 26 / 27 | Disabled |
| GNSS RX / TX (ESP32 directions) | 16 / 17 | Disabled |
| LTE RX / TX (ESP32 directions) | 34 / 13 | Disabled |
| SD SCK / MOSI / MISO / CS | 18 / 23 / 19 / 14 | Disabled |

The layout in `include/board_config.h` is provisional. GPIO34 is input-only,
so it is only assigned UART RX. Check the exact board pinout, every breakout's
logic levels and the supply current before enabling modules. Motors, the modem
and the MQ heater need suitable power and drivers; they cannot draw power from
a GPIO. The `nodemcu32s_compile_all` profile verifies source compilation with
all optional interfaces and SMS disabled; it is not a wiring plan for flashing.

The usual NodeMCU-32S has no PSRAM. The smaller internal-RAM capture buffer
targets roughly one second before and six seconds after an event, subject to
actual available heap. The serial boot line reports allocation or failure.
