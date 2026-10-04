# System Architecture

## Design principle
The main safety controller and computer-vision nodes are separated so that camera/AI failure cannot disable crash detection or emergency communication.

```text
                       FRONT VISION NODE
                    ESP32-S3 + Camera + PSRAM
                              |
                  pothole / wet-road analysis
                              |
                         metadata only
                              |
                              v
+----------------------------------------------------------------+
|                     MAIN SAFETY NODE                            |
|                        ESP32-S3                                 |
|                                                                |
| MPU6050 ---------> crash state machine                          |
| head sensor + strap -> helmet-worn state                        |
| GPS -------------> location / speed / time                      |
| cancel button ----> rider response                              |
|                                                                |
|              +-----------------------------+                   |
|              | central event / safety logic|                   |
|              +-----------------------------+                   |
|                    |        |        |                          |
|                    v        v        v                          |
|                 microSD   warnings    LTE/SMS                   |
|                 blackbox  haptic      emergency                 |
+----------------------------------------------------------------+
                              ^
                              |
                         metadata only
                              |
                    REAR VISION NODE
                    ESP32-S3 + Camera
                              |
                     vehicle detection
                     approach awareness
```

## Interfaces
Recommended allocation:
- I2C: MPU6050, optional ToF head-presence sensor
- UART: GPS
- UART: LTE modem
- SPI/SDMMC: microSD
- GPIO: strap, cancel button, buzzer, vibration, LEDs
- ESP-NOW or another lightweight link: compact vision metadata

## Fault isolation
- front camera fails -> safety core continues
- rear camera fails -> safety core continues
- AI inference fails -> safety core continues
- microSD fails -> crash detection and alert path continue
- LTE unavailable -> event remains locally recorded and communication can retry
- GPS fix unavailable -> retain last valid fix/timestamp rather than blocking crash detection

## Known architecture conflicts
1. MPU6050 ±16 g saturation: severity is an event index, not true high-g measurement.
2. Head movement alters camera geometry: perception confidence must account for it.
3. RGB wet-road detection does not directly measure tyre-road friction.
4. LTE can draw high transient current and must not brown out the safety MCU.
5. Camera processing must stay outside the critical crash-loop timing path.
