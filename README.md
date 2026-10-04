# RideShield

**IoTrix 2.0 – Track A: Embedded IoT System**

RideShield is a smart motorcycle-helmet safety platform that combines an independent embedded safety core with camera-based road and rear-traffic perception.

> **Semi-final engineering principle:** perception features are auxiliary. Crash detection, logging and emergency communication must continue even if a camera or AI subsystem fails.

## Current architecture

### 1. Main safety node — ESP32-S3

Responsibilities:
- MPU6050 motion and crash-event sensing
- helmet-worn verification using head-presence sensing + chin-strap state
- crash state machine and rider cancel countdown
- GPS location and pre-event speed
- LTE/SMS emergency alert
- microSD black-box logging
- buzzer / vibration / LED warnings

### 2. Front vision node — ESP32-S3 + camera

Responsibilities:
- pothole detection
- wet-road classification / wet-road risk estimation
- image evidence logging
- later: road-geometry estimation

The camera **does not directly measure tyre-road friction**. The current design estimates visible wet-road risk.

### 3. Rear vision node — ESP32-S3 + camera

Responsibilities:
- rear-vehicle detection
- tracking / approach awareness
- warning metadata to the main controller

Because the camera is helmet-mounted, head movement changes its geometry. This is treated as a known limitation.

## Safety-core flow

```text
MPU6050 + GPS + helmet-worn state
              |
              v
       Crash state machine
              |
      possible crash event
              |
      verification / stillness
              |
      warning + countdown
         /          \
     CANCEL       NO CANCEL
       |              |
    log event     severity index
                      |
                 GPS location
                      |
                   LTE/SMS
                      |
                 microSD log
```

## Crash severity

RideShield reports a **Crash Event Severity Index** and classifies events as:

- Low-energy crash event
- Moderate-energy crash event
- High-energy crash event

This is **not a medical injury assessment**. The MPU6050 accelerometer is limited to ±16 g, so the system does not claim true high-g impact magnitude after saturation.

## Semi-final scope

The semi-final prioritizes proof over feature count.

### Core proof target
- sensor acquisition on ESP32-S3
- MPU6050-based crash-event logic
- helmet-worn input logic
- warning + cancel state
- GPS/LTE/microSD integration where available
- changed-input physical demonstration

### Vision proof target
- front-camera pothole detection
- wet-road classification
- model output shown with real or recorded road imagery

### Later / secondary integration
- rear vehicle approach awareness
- OP0062 prolonged-eye-closure warning
- MQ-3 pre-ride alcohol-vapour screening
- road-risk fusion and experimental grade estimation

## Current repository status

This repository currently contains the project documentation and architecture. Firmware, model code, screenshots and measured test results should be committed as they become available.

Older PDF reports in the repository may describe previous design choices such as ADXL375 or LD2451 radar. **The architecture documented in this README is the current RideShield design.**

## Repository structure

```text
RideShield/
├── README.md
├── docs/
│   ├── ARCHITECTURE.md
│   ├── SEMIFINAL_PROGRESS.md
│   └── TEST_PLAN.md
├── firmware/
│   └── README.md
├── vision/
│   └── README.md
├── results/
│   └── README.md
└── historical proposal/report PDFs
```

## Key technical limitations

- MPU6050 saturates above ±16 g.
- GPS is suitable for location and pre-event speed, not collision impulse measurement.
- Helmet-mounted cameras move with the rider's head.
- RGB vision estimates wetness; it does not directly measure friction.
- LTE coverage and high current peaks are system risks.
- Local Sri Lankan road data is needed to improve computer-vision generalization.
- Rain, spray, fogging and lens contamination can reduce camera reliability.

## Team

South Eastern University of Sri Lanka  
Faculty of Technology – Department of Information and Communication Technology

Project lead: Piragenth

## Semi-final

For the semi-final, judges should be able to see:
1. the complete system architecture,
2. real hardware/firmware progress,
3. a changed-input physical test,
4. front-vision proof-of-concept,
5. basic measured validation,
6. known limitations and the plan for the final.
