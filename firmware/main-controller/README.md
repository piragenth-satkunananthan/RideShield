# RideShield main controller — NodeMCU-32S

Targets the original ESP32, not ESP32-S3. Camera firmware is separate. The
hardware safety library remains compatible with the simulation in
`dev_version/rideshield-core`; tests below exercise the hardware copy directly.

## Build and upload

From this directory:

```sh
pio run -e nodemcu32s_core
pio run -e nodemcu32s_core -t upload
pio device monitor -b 115200
```

`nodemcu32s_core` enables IMU, ToF, IR, strap, MQ input, outputs, GPS, SD and
LTE status queries. Live SMS is disabled. `nodemcu32s_bringup` enables just
IMU/ToF/IR plus buttons and networking. Both use real sensor readings, not
injected impacts. `nodemcu32s_compile_all` mirrors the full dry-run profile.

## Wiring

All modules need common ground. GPIO names below are ESP32 directions.

| Module / function | GPIO |
|---|---|
| IMU SDA / SCL | 21 / 22, address 0x68 (0x69 configurable) |
| VL53L0X SDA / SCL | 21 / 22, address 0x29 |
| KY-033 OUT | 36 |
| Strap reed switch | 39 to GND; external 10 kΩ pull-up to 3V3 |
| MQ-3 analog | 35 through a divider keeping ADC voltage below 3.3 V |
| Cancel / SOS | 32 / 33, each button to GND |
| Buzzer driver | 25 |
| Left / right vibration drivers | 26 / 4 |
| LED through resistor | 27 |
| GPS module TX / RX | 16 / 17 (9600 baud) |
| LTE module TX / RX | 34 / 13 (115200 baud) |
| SD SCK / MOSI / MISO / CS | 18 / 23 / 19 / 14 |

Use each breakout's rated supply and suitable logic-level conversion. GPIO36
and 39 have no internal pull-up. Motors and buzzer need appropriate drivers;
the modem and MQ heater need suitable power, not GPIO power. LTE power-key
control is not assigned: the board must already power up its modem. An optional
ADXL375 is not implemented.

## Access point and phone

- Wi-Fi: `RideShield-XXXX-CORE` (XXXX is derived from the device ID).
- Default password: `rideshield-bench-only`.
- Browser dashboard: **http://192.168.4.1/**; live JSON: `/api/status`.
- Existing authenticated RSW1 phone WebSocket: port **81**.
- Local AP only; it does not provide Internet routing.

The dashboard is read-only and shows readings, calibration, warnings, location,
SD/modem state and timing. All AP clients can read it, so replace the example
password before use outside the bench. Private settings go in ignored
`include/local_config.h`:

```cpp
#pragma once
#define RS_WIFI_PASSWORD "your-private-wifi-password"
#define RS_PHONE_TOKEN "0123456789abcdef0123456789abcdef"
// #define RS_SMS_NUMBER "+<country code and subscriber number>"
```

The token must be exactly 32 ASCII characters and match the phone app.
Configure a real recipient, then explicitly build `nodemcu32s_live_sms` to
permit SMS. Use an agreed test recipient; messages remain labelled TEST.
`SUBMITTED` means the modem accepted submission, not confirmed delivery.

## Calibration through USB Serial

Type `help` for commands. `status` prints the current readings.

1. Position the ToF inside the helmet, aimed toward where the head will be.
   Hold a steady worn position for at least a second; send `headcal worn`.
   Remove the helmet, wait for stable valid readings, send `headcal empty`.
   The empty reading must be at least 30 mm farther away. Both measurements
   must be valid 10–2000 mm; no-return/out-of-range is **unknown**, not absent.
   If the empty helmet gives no valid return, change mounting or leave head
   detection uncalibrated. Uncalibrated/unknown presence does not suppress an
   incident. Calibration requires five readings with at most 20 mm spread.
2. KY-033 is a reflective line sensor. First verify two distinct levels using
   test surfaces, then capture the intended states with `ircal open` and
   `ircal closed`. It reports a prolonged calibrated signal after two seconds
   while a head is present. This is not validated eye-closure detection; do
   not position an unassessed emitter near an eye to perform this calibration.
3. MQ screening is disabled until `mqlimit N`, where N is 100–3000 mV at the
   ESP32 ADC after the divider. Establish a threshold from bench measurements;
   no universal threshold or blood-alcohol conversion is provided. The runtime
   warm-up gate is three minutes; follow the module manufacturer's initial
   conditioning requirements separately. A high requires two continuous
   seconds above threshold, with 50 mV release hysteresis.
4. Wait for `Calibration saved`. Values persist across restart with CRC
   validation. `calclear` resets all calibrations. A save failure is reported;
   repeat the command after correcting the storage problem.

The strap signal is debounced. An open strap warns but does not disable crash
or IR monitoring. Inputs ready is diagnostic only, not permission to ride.

## Controller behavior

The existing crash core retains its candidate/verification/countdown/incident
states, 15-second cancel window, deliberate one-second Cancel hold and
three-second SOS hold. SOS bypasses wear checks. Missing IMU, storage, network
or persistence does not cancel an existing incident. Emergency and countdown
outputs take priority over system and rider warnings.

The MPU driver accepts WHO_AM_I 0x71 and 0x70 (MPU-6500-class accel/gyro in the
user's module). It reads accelerometer and gyro at 200 Hz; no magnetometer is
used. ToF runs continuous ranging, polled without waiting for a measurement;
the controller task owns I2C. Check `loop_max_us` and `gaps` on real hardware
under simultaneous Wi-Fi/SD/modem load before relying on timing.

Separate tasks handle networking/GPS, modem UART, NVS and SD. The SD stores
`events-BOOT.rsl`, `motion-INCIDENT.rsl`, and one-Hz `sensors-BOOT.csv` files.
After an SD failure it attempts remount every five seconds; missed records are
not recovered. With no PSRAM, the motion buffer targets about one second before
and six seconds after the trigger; allocation/truncation is reported. The
experimental event index is a bounded motion-signal summary, not injury severity.

`ack` acknowledges the local incident; it does not stop SMS retries. `close`
only closes after submission or disabled SMS. Never generate real impacts to
test this prototype; use the existing simulation and controlled button tests.

## Verification

From the repository root:

```sh
python firmware/main-controller/tools/test_native.py
```

This runs the 51 portable-core tests against the hardware library, plus rider
calibration/freshness/debounce/timing/priority checks, with undefined-behavior
sanitization. Compilation and native tests cannot verify wiring, output drivers,
actual sampling latency, modem registration/delivery, or wearable performance.
