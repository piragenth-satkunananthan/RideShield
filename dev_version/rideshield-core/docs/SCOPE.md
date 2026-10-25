# Scope and remaining integration work

This release implements the core first, as requested. All required full-project
features remain in scope for the broader project, but the unavailable architecture
image and earlier documents cannot be treated as implemented specifications.
The code uses the visible decision: two camera streams → phone AI → main ESP32-S3
alerts, with onboard crash/SOS logic independent of the phone.

## Implemented core vs remaining work

| Area | Delivered now | Still required |
|---|---|---|
| Main ESP32-S3 | Firmware, task separation, alert state machine | Flash/run and verify exact board/pin map |
| Crash/SOS | IMU adapter, candidate verification, cancel, SOS, uncertainty | Real mounting/calibration, non-crash datasets, controlled testing |
| Front road detection | Freshness-gated pothole/wet observation input | Camera firmware, trained/validated models, phone inference |
| Rear warning | Freshness-gated rear-vehicle advisory | Camera calibration, tracking, helmet orientation, validated blind-spot zones |
| Distance behind | No fabricated distance estimate | Choose and validate range sensor or calibrated vision method |
| Phone | Binary protocol and synthetic sender | Real Android/iOS application, camera clock mapping and stream transport |
| Outputs | Proposed active-high GPIO drivers and alert patterns | Exact motors/buzzer/driver circuits, perceptibility and power measurements |
| GPS | Optional NMEA adapter and freshness-labelled SMS location | Exact receiver baud/wiring and acquisition tests |
| LTE/SMS | Generic nonblocking AT engine and retries | Exact module, power/PWRKEY, UART levels, operator setup, prompt recovery, live tests |
| Logging | CRC records, RAM capture, async SD/NVS | Power-cut, card-removal and wear testing; optional export UI |
| Wear/strap | Conservative Unknown; portable presence input | Sensor selection, debounce/calibration and hardware adapter |
| Other helmet features | Not inferred/implemented | Eye closure, MQ-3B, orientation, photo evidence, HUD, cloud, battery management if required by the full project spec |

Real SMS is labelled TEST. No emergency services are contacted. There is no
medical injury/severity classifier, engine interlock, OTA updater, cloud service,
or delivery-report confirmation in this package.

## Boundaries to validate on the bench

- The 200 Hz FIFO implementation reconstructs sample times; it is not a sensor
  timestamp facility. FIFO overflow/backlog raises a fault instead of silently
  treating a large burst as current data.
- Per-transaction I2C timeouts and task priorities are bounded design choices;
  hardware timing, USB serial blocking and power/EMI behaviour remain unmeasured.
- The core can survive loss of phone/SD/GPS logically; the physical system still
  depends on its supply, wiring and MCU staying powered.
- Plain WebSocket + local WPA2 + a token is prototype access control. It is not
  internet security or cryptographic protection of logs.
- A single capture buffer cannot retain overlapping long incidents. Missed captures
  are counted. Captures are written after the post-event interval, so power loss
  before completion loses them.
- NVS is asynchronous; loss/duplication around a power cut remains possible.
- Submitted SMS is a network handoff, not proof that a contact read or received it.
- Phone absence is visible in status and removes vision advisories; it does not
  prevent crash/SOS operation. Vision absence is not interpreted as a clear road.

The next physical milestone is USB-only BENCH firmware, then MPU6050 + the two
buttons. The next software milestone is one real phone model feeding timestamped
observations through the existing protocol, after camera capture times are known.
