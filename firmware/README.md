# Firmware

`main-controller/` is the NodeMCU-32S firmware under hardware bring-up. Its
default build reads a real MPU6050 and runs the portable RideShield core, phone
gateway, journal and recording logic. Live SMS, optional peripherals and
actuator outputs stay disabled until their power and wiring are verified.

`dev_version/rideshield-core/` remains a separate simulation and test project.
See `main-controller/README.md` to start the hardware bring-up. The old
ESP32-S3 Wokwi demo remains in `RideShield_simulation/` for reference.

Project layout:

```text
firmware/
├── main-controller/     # ESP32 NodeMCU-32S project
├── front-vision-node/   # planned
└── rear-vision-node/    # planned
```

Bring up the remaining sensors and outputs incrementally after the MPU6050
test. Confirm each module's wiring and power requirements before enabling its
interface in `main-controller/include/board_config.h`.
