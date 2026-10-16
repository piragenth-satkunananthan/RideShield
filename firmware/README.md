# Firmware

`main-controller/` is the NodeMCU-32S firmware under hardware bring-up. Its
default `nodemcu32s_core` profile includes sensor, output, GPS, LTE and SD
integration. The smaller `nodemcu32s_bringup` profile disables optional
peripherals and actuator outputs. Live SMS is disabled in both profiles.

`camera-node/` contains shared **ESP32-CAM + OV2640** firmware with `front`
and `rear` build environments. It uses the AI-Thinker pinout and PSRAM.
See its [setup and flashing guide](camera-node/README.md). Update the main
controller first so its DHCP range does not overlap the camera addresses.

`dev_version/rideshield-core/` remains a separate simulation and test project.
See `main-controller/README.md` to start the hardware bring-up. The old
ESP32-S3 Wokwi demo remains in `RideShield_simulation/` for reference.

Project layout:

```text
firmware/
├── main-controller/     # ESP32 NodeMCU-32S project
└── camera-node/         # shared ESP32-CAM project: front / rear profiles
```

Bring up the remaining sensors and outputs incrementally after the MPU-9250
test. Confirm each module's wiring and power requirements before enabling its
interface in `main-controller/include/board_config.h`.
