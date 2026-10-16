# Semi-Final Technical Progress

## Problem
Motorcycle riders can face delayed emergency response after a crash, limited warning of road hazards such as potholes and wet surfaces, and reduced awareness of vehicles approaching from behind.

## Proposed solution
RideShield separates the ESP32 NodeMCU-32S safety controller from camera streams and phone-based perception. The controller performs crash-event sensing, rider cancellation, emergency communication and logging. The Android phone connects over the controller's Wi-Fi access point for telemetry and auxiliary perception.

## Current design decisions
- Main controller: ESP32 NodeMCU-32S (original ESP32, not ESP32-S3)
- Motion sensing: MPU-9250-labelled module; MPU-6500 identity `0x70` supported for accelerometer/gyroscope readings
- Crash output: event severity index, not medical injury severity
- Front perception: camera-based pothole + wet-road detection
- Rear perception: camera-based rear-vehicle awareness
- Helmet worn: two-signal verification (head presence + strap)
- Head presence: VL53L0X time-of-flight sensor; IR input: KY-033
- Emergency: GPS + LTE/SMS
- Evidence: microSD black-box/event logging

## Current progress status
Use this table as the repository source of truth and update it only with real evidence.

| Subsystem | Status | Evidence / remaining proof |
|---|---|---|
| Main safety firmware | Implemented; full and bring-up profiles compiled | [Controller validation](../firmware/main-controller/VALIDATION.md); physical fault and incident scenarios remain |
| IMU / ToF / KY-033 | Integrated; user reports connected sensors working | Record calibrated readings and controlled bench results |
| Helmet-worn logic | Implemented | Verify head-presence calibration, strap input and sensor-failure behavior |
| Wi-Fi AP / browser dashboard | Implemented | Local dashboard and `/api/status`; board startup previously showed AP at `192.168.4.1` |
| Android controller integration | Implemented; build/test results in integration guide | [Phone integration](../Phone-node/docs/CONTROLLER_INTEGRATION.md); physical phone-to-board acceptance remains |
| GPS | Firmware integration present | Fresh outdoor fix and loss-of-fix evidence still needed |
| LTE/SMS | Firmware integration present; live SMS disabled by default | Registered modem and authorized test delivery still needed |
| microSD logging | Firmware integration present | Read back event files from physical card and test recovery |
| Pothole model | Android inference implementation present | Real camera timing, accuracy and latency measurements remain |
| Wet-road / rear-vehicle models | Unavailable in the Android app | Trained models and validated outputs still needed |
| Front/rear camera firmware | Implemented for AI-Thinker ESP32-CAM + OV2640 | [Camera validation](../firmware/camera-node/VALIDATION.md); physical streaming, shared Wi-Fi load and perception integration remain |
| MQ-3 | Firmware screening input present | Calibrated bench screening evidence needed; not a blood-alcohol measurement |

Software builds and unit tests do not establish road readiness. The phone currently
requires foreground operation; reliable background riding operation remains work.

## Semi-final proof target
The strongest demo is one complete chain:

```text
changed physical input
→ NodeMCU-32S reads sensors
→ crash state changes
→ warning/countdown
→ cancel OR confirm
→ event is logged
→ location/alert path demonstrated
```

The vision proof should separately show pothole and wet-road model outputs.

## Three required progress screenshots
1. **Embedded progress:** NodeMCU-32S + IMU/ToF/KY-033 with real serial output and phone telemetry.
2. **Pothole progress:** real model inference with pothole bounding box/confidence.
3. **Wet-road or system-integration progress:** classifier output, GPS/LTE result, or another genuine implementation result.

Do not use architecture-only screenshots as all three progress images; at least some screenshots should prove implementation.
