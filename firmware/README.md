# Firmware

Place embedded source code here as it becomes available.

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
