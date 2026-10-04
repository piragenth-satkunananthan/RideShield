# Semi-Final Technical Progress

## Problem
Motorcycle riders can face delayed emergency response after a crash, limited warning of road hazards such as potholes and wet surfaces, and reduced awareness of vehicles approaching from behind.

## Proposed solution
RideShield separates the system into a safety-critical ESP32-S3 node and independent camera perception nodes. The safety node performs crash-event sensing, rider cancellation, emergency communication and logging. Vision nodes provide pothole, wet-road and rear-vehicle information without being allowed to block the safety core.

## Current design decisions
- Main controller: ESP32-S3
- Crash sensing: MPU6050 only
- Crash output: event severity index, not medical injury severity
- Front perception: camera-based pothole + wet-road detection
- Rear perception: camera-based rear-vehicle awareness
- Helmet worn: two-signal verification (head presence + strap)
- Emergency: GPS + LTE/SMS
- Evidence: microSD black-box/event logging

## Current progress status
Use this table as the repository source of truth and update it only with real evidence.

| Subsystem | Status | Evidence to add |
|---|---|---|
| System architecture | Defined | Architecture document |
| Main safety firmware | In integration / to be committed | source code + serial output |
| MPU6050 crash POC | To be evidenced | controlled test output |
| Helmet-worn logic | Design selected | sensor test / serial output |
| GPS | Integration stage | location output |
| LTE/SMS | Integration stage | successful SMS evidence |
| microSD logging | Integration stage | saved log file |
| Pothole model | Software POC under development | detection screenshot + metrics |
| Wet-road model | Dataset/model stage | dry/wet classification evidence |
| Rear camera | Later-stage subsystem | vehicle detection evidence |
| OP0062 | Secondary | eye-open/closed test |
| MQ-3 | Secondary | pre-ride screening test |

## Semi-final proof target
The strongest demo is one complete chain:

```text
changed physical input
→ ESP32-S3 reads sensors
→ crash state changes
→ warning/countdown
→ cancel OR confirm
→ event is logged
→ location/alert path demonstrated
```

The vision proof should separately show pothole and wet-road model outputs.

## Three required progress screenshots
1. **Embedded progress:** ESP32-S3 + MPU6050/other integrated hardware with real serial output.
2. **Pothole progress:** real model inference with pothole bounding box/confidence.
3. **Wet-road or system-integration progress:** classifier output, GPS/LTE result, or another genuine implementation result.

Do not use architecture-only screenshots as all three progress images; at least some screenshots should prove implementation.
