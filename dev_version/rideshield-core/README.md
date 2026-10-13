**Wokwi users: start with [WOKWI_START_HERE.md](WOKWI_START_HERE.md).**

# RideShield Core v0.1.0

A testable prototype core for **ESP32-S3 N16R8**. The main board owns alerts,
crash verification, the cancellation countdown, manual SOS and SMS retry state.
The phone supplies front-road and rear-vehicle observations over Wi-Fi.

**Start with BENCH.** It uses synthetic motion and disables physical outputs and
real SMS. This release is software-tested; it has not been flashed or exercised
on your hardware. Detection thresholds are demonstration settings, not validated
crash or road-safety thresholds.

## What is implemented

- Portable C++ crash/SOS state machine; 200 Hz MPU6050 FIFO adapter.
- Debounced SOS and cancellation buttons; prioritized, nonblocking alert patterns.
- Authenticated phone WebSocket session; sequence, clock, frame-age and persistence checks.
- GPS location freshness and unavailable-location handling.
- AT SMS transaction state machine, bounded timeouts, retry/backoff and stable incident IDs.
- Asynchronous NVS recovery journal and optional SD event/motion recording.
- Host fault tests, a runnable demonstration, synthetic phone sender and log decoder.

The camera nodes and real phone AI application are **not implemented in this core
release**. The sender is a test tool. It does not detect potholes, wet roads,
vehicles or distance. See [scope and limitations](docs/SCOPE.md).

## 1. Run the core on your computer

Requires Python 3 and a C++17 compiler (`g++`). From this extracted folder:

```sh
python3 tools/test_native.py
python3 tools/run_demo.py
python3 -m unittest discover -s tests -p 'test_*.py' -v
python3 tools/test_native.py --sanitize
python3 tools/test_board_guards.py
```

The demo exercises impact cancellation, an uncancelled event, manual SOS with a
missing IMU, and rear-observation expiry. It sends no SMS and needs no hardware.
On Arch Linux, install missing host prerequisites with `sudo pacman -S --needed
base-devel python`.

## 2. Build and flash the bench firmware

Use a Python virtual environment; the board package downloads on the first build:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install platformio==6.2.0
pio run -e esp32s3_bench
pio run -e esp32s3_bench -t upload
pio device monitor -b 115200
```

The target assumes a DevKitC-style ESP32-S3 with **16 MB flash / 8 MB octal
PSRAM**. Verify the exact module and development-board pinout before connecting
anything. Upload mode/port selection may differ by board.

Start with only USB. Type `help` or `status` in the serial monitor. Send each
command as a separate newline-terminated line:

| Test | Commands / action | Expected result |
|---|---|---|
| Cancel an impact | `impact`, then `cancel down`; wait >1 s; `cancel up` | COUNTDOWN → MONITOR, no SMS job |
| Uncancelled impact | `impact`; wait 15 s | INCIDENT, `SMS DISABLED` preview |
| Finish simulated event | `ack`, then `close` | Local alarm acknowledged; return to monitoring after disabled/submitted result |
| Manual SOS | `imu off`, `sos down`; wait >3 s; `sos up` | INCIDENT even without IMU |
| Restore normal bench input | `imu on`, `ack`, `close` | Monitoring resumes |
| Known absent helmet handling | `head absent`, then `impact` | Handling event recorded; no automatic countdown |

`candidate` injects two moderate samples. Subsequent simulated stillness confirms
that candidate; it is not a road-data replay. `gps 6.9 79.8` injects an example fix.
`head unknown` restores conservative automatic handling. Hardware firmware accepts
only `help`, `status`, `ack`, and `close`; simulated sensor commands are excluded.

## 3. Exercise phone → main-board alerts

Join the `RideShield-xxxx-BENCH` Wi-Fi network using password
`rideshield-bench-only`. On the joined computer (acting as the phone test producer):

```sh
python -m pip install -r tools/requirements.txt
python tools/phone_sim.py --scenario pothole
python tools/phone_sim.py --scenario wet
python tools/phone_sim.py --scenario rear
python tools/phone_sim.py --scenario stale
```

The controller reports accepted alert state over WebSocket and `status` over
serial. Fresh repeated observations activate advisories; `stale` must not.
Physical motors and buzzer remain disabled by default. Stop the sender: camera
results expire within their remaining 500 ms age budget, independently of the
3 s heartbeat timeout. Two detection frames confirm pothole/rear; three confirm wet.

The AP password and application token are public bench credentials. Change both
before shared-field testing. Copy `include/local_config.example.h` to
`include/local_config.h`; the latter is excluded from the package and Git.
Set `RIDESHIELD_PHONE_TOKEN` for the simulator to match your 32-byte ASCII token.
Protocol details are in [PHONE_PROTOCOL.md](docs/PHONE_PROTOCOL.md).

## 4. Bring up the hardware incrementally

Read [WIRING.md](docs/WIRING.md), then use `pio run -e esp32s3_hardware`.
Start with MPU6050 and two buttons. Confirm sensor loss/recovery and button timing.
Add driver-connected outputs, then GPS, SD and LTE one at a time using the optional
configuration flags. Real SMS requires an explicit hardware-profile configuration
and recipient; keep `RS_ALLOW_SMS=0` throughout initial bring-up.

Do not flash `esp32s3_compile_all` as a shortcut: it is an integration compile
profile that enables proposed peripheral pins. It still disables SMS.

## Project map

| Location | Responsibility |
|---|---|
| `lib/RideShieldCore/src/core.*` | State transitions, thresholds, buttons, warnings, SMS jobs |
| `lib/RideShieldCore/src/phone.*` | Wire schema, clock/freshness gate, observation persistence |
| `lib/RideShieldCore/src/sms_modem.*` | AT command and response transaction |
| `lib/RideShieldCore/src/blackbox.*` | Fixed-buffer pre/post-event recording |
| `lib/RideShieldCore/src/codec.*` | Explicit binary layout and CRC32 |
| `include/mpu6050.h` | Register configuration and FIFO polling |
| `src/main.cpp` | FreeRTOS tasks, queues, GPIO, Wi-Fi, NVS, SD, UART integration |
| `tests/`, `tools/` | Host tests, demo, phone simulator and SD decoder |
| `docs/TEST_REPORT.md` | Actual validation results and limits |

Full architecture: [ARCHITECTURE.md](docs/ARCHITECTURE.md). Decode an SD log with
`python3 tools/read_log.py path/to/file.rsl`.
