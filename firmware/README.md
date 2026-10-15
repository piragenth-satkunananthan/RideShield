# Firmware

`main-controller/` is the NodeMCU-32S firmware under hardware bring-up. Its
default build reads a real MPU6050 and runs the portable RideShield core, phone
gateway, journal and recording logic. Live SMS, optional peripherals and
actuator outputs stay disabled until their power and wiring are verified.

The root `main.cpp` is an older Wokwi semi-final demo, not the NodeMCU-32S
entry point. `dev_version/rideshield-core/` remains a separate simulation and
test project. See `main-controller/README.md` to start the hardware bring-up.

Suggested structure:

```text
firmware/
├── main-safety-controller/
├── front-vision-node/
└── rear-vision-node/
```

For the semi-final, prioritize the **main safety controller**:
- MPU6050 acquisition
- crash state machine
- helmet-worn logic
- cancel countdown
- GPS
- LTE/SMS
- microSD logging
- warning outputs

Each subfolder should include wiring/pin notes and a short run/test description.
