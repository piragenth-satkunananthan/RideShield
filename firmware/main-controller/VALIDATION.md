# Core controller validation — 2026-10-15

## Implemented

NodeMCU-32S hardware profile; MPU accel/gyro; VL53L0X continuous ranging and
calibrated head presence; strap debounce; calibrated KY-033 signal timing;
MQ threshold screening and warm-up; priority buzzer/haptic/LED outputs; GPS;
LTE health queries and existing SMS transport; persistent calibration and
incident journal; SD sensor/event/motion logs; local Wi-Fi AP, read-only
browser dashboard and existing authenticated phone WebSocket.

No camera firmware was changed. The portable crash/phone/SMS core was retained.
The latest upstream phone commit was preserved.

## Checks passed

| Check | Result |
|---|---|
| `pio run -e nodemcu32s_core` | PASS; flash 956,017 / 1,310,720 bytes (72.9%) |
| `pio run -e nodemcu32s_bringup` | PASS; flash 891,997 / 1,310,720 bytes (68.1%) |
| Hardware-copy core native suite | 51 / 51 PASS, undefined-behavior sanitizer enabled |
| Rider-input suite | PASS: calibration, freshness, head hysteresis, strap debounce, IR timing, MQ warm-up/threshold timing and warning priority |
| Hardware configuration guards | PASS: default/full pin map accepted; SMS without recipient and SMS in bench mode rejected |
| Dashboard | JavaScript syntax and JSON-to-display field mapping PASS |
| Patch whitespace | PASS |

Full-profile static RAM is 50,316 bytes (15.4%). This excludes runtime heap,
Wi-Fi allocations, task stacks and dynamically allocated black-box buffers;
confirm runtime free heap and timing on the board.

Review corrected stale ToF history after a data gap and MQ timing that could
otherwise count noncontinuous above-threshold time. Head freshness is updated
before crash decisions. Invalid ToF data remains unknown, never inferred absent.

The live-SMS profile was not built with a real recipient or physically tested.
All hardware images built here disable live SMS. No device was flashed and no
SMS was sent during these checks.

## Required physical verification

1. Confirm the documented pin map, voltage levels, external strap pull-up and
   output drivers against the actual modules before uploading the full profile.
2. Calibrate ToF worn/empty states and confirm invalid/no-return is UNKNOWN.
   Check IR using test surfaces; this is not a validated eye-closure detector.
   Establish an MQ threshold from bench measurements after conditioning.
3. Verify AP/dashboard/phone connection, button holds and all output channels.
4. Check `gaps`, `loop_max_us`, free heap and logs under simultaneous sensor,
   Wi-Fi, SD and modem activity. Compilation does not establish real-time timing.
5. Verify GPS outdoors, SD records/remount behavior, persistent calibration and
   incident recovery after restart.
6. Only after configuring an agreed test recipient, build the live-SMS profile
   and verify modem/SIM/network behavior and actual receipt. SUBMITTED alone is
   not proof of delivery. Avoid real impact testing of a wearable helmet.

See [README.md](README.md) for build, wiring, calibration and AP instructions.
