# Front and rear ESP32-CAM firmware

One PlatformIO project, two roles, for **AI-Thinker-style ESP32-CAM boards with
OV2640 and PSRAM**. This is the original ESP32 camera board, not ESP32-S3.
The AI-Thinker pin map is an explicit assumption: verify it against a different
board variant before uploading. The main safety board remains NodeMCU-32S.

## What it does

- Joins the main controller's exact configured Wi-Fi SSID; reconnects after loss.
- Captures VGA (640×480) JPEGs; defaults to a **10 FPS target per camera**.
  Actual FPS/latency depends on exposure, scene complexity and shared Wi-Fi.
- Keeps two driver buffers in PSRAM with `CAMERA_GRAB_LATEST` and holds at most
  one frame for transmission. It does not make additional JPEG copies/queues.
- Rejects frames older than 250 ms before sending. A nonblocking socket writer
  gives each complete multipart frame a 250 ms send budget. A failed/partial
  frame closes the connection; the consumer must reconnect.
- Keeps `/sync` and `/status` available from a separate task while streaming.
  Accepts one stream consumer per camera; a second receives HTTP 409.
- Leaves the flash LED off. Missing PSRAM, an unexpected sensor or failed camera
  initialization disables streaming and reports the error on serial/status.

The nodes capture and transmit images. They do **not** run YOLO, classify wet
roads, estimate vehicle distance or send safety alerts. Those outputs need
separately validated perception; the main controller owns safety decisions.

## Network and endpoints

| Device | Address |
|---|---|
| Main NodeMCU-32S AP | `192.168.4.1` |
| Front camera (`front` build) | `192.168.4.2:8080` |
| Rear camera (`rear` build) | `192.168.4.3:8080` |
| Phone / laptop | DHCP starting at `192.168.4.20` |

**Upload the updated main-controller firmware first.** Its DHCP pool now starts
at `.20`, reserving `.2` and `.3` for cameras. After updating, forget/rejoin the
AP or renew your laptop's lease; do not keep an old `.2`/`.3` client lease.
The AP permits four stations: two cameras, phone and test laptop.

| GET path on port 8080 | Result |
|---|---|
| `/` | Simple browser preview |
| `/stream` | Multipart MJPEG with frame metadata |
| `/sync` | Node, boot ID, monotonic `camera_us`, uptime |
| `/status` | Camera health, Wi-Fi RSSI, stream counters, memory |

Close the browser preview before opening another stream consumer. The browser
may need a refresh after a stream timeout or controller restart. These are
local HTTP endpoints; access is limited by membership of the helmet Wi-Fi,
not per-request authentication. Use your configured private AP password.

## Configure and upload

1. From the repository root, update the main controller:

   ```sh
   pio run -d firmware/main-controller -e nodemcu32s_core -t upload
   ```

2. Copy `firmware/camera-node/include/local_config.example.h` to
   `firmware/camera-node/include/local_config.h`. This file is git-ignored.
   Set `RS_CAMERA_SSID` to the exact SSID printed by the main controller and
   `RS_CAMERA_PASSWORD` to the matching password. The example SSID is only an
   example. Without a configured SSID, the firmware prints a configuration
   message and does not join a network.

3. Connect one camera at a time. With an ESP32-CAM-MB programmer, follow its
   boot/reset procedure. For a USB–UART adapter, use **3.3 V UART logic**:

   | Adapter / supply | ESP32-CAM |
   |---|---|
   | Stable regulated 5 V supply | `5V` |
   | Ground | `GND` |
   | Adapter TX, 3.3 V logic | `U0R` / GPIO3 |
   | Adapter RX | `U0T` / GPIO1 |
   | Ground, only for flashing | GPIO0 |

   For manual flashing, connect GPIO0 to GND and reset/power-cycle before upload.
   After upload remove the GPIO0–GND link and reset again to run the camera.
   Use a supply sized for both camera boards and the controller; do not power
   the cameras from the NodeMCU's 3.3 V pin or assume a USB–UART 3.3 V output can
   supply them. Share ground and avoid competing power sources. Brownout
   detection stays enabled; fix the supply if brownout resets occur.

4. Flash the front and rear roles to different boards:

   ```sh
   pio run -d firmware/camera-node -e front -t upload --upload-port /dev/ttyUSB0
   pio run -d firmware/camera-node -e rear  -t upload --upload-port /dev/ttyUSB0
   pio device monitor --port /dev/ttyUSB0 -b 115200
   ```

   Replace the serial port with yours. Label each board FRONT or REAR. Do not
   run two boards with the same role: that creates a static IP conflict.

5. Join the main controller AP from your laptop/phone. Open
   `http://192.168.4.2:8080/`, then `http://192.168.4.3:8080/`.
   Serial should report `CAMERA OV2640 ready` and the corresponding camera URL.

## Firmware interface

The authoritative implemented interface is [STREAM_PROTOCOL.md](STREAM_PROTOCOL.md).
It corrects the earlier design sketch's exposure timestamp, JPEG-quality and
address-allocation assumptions. No Android source is changed in this milestone.

Consumer integration still needs work: the existing phone reader must skip the
CRLF between multipart parts, retain the next part's headers, handle truncated
JPEGs, call `/sync` and reset its clock mapping when a camera reboots. The
camera's driver timestamp is **not** a measured exposure-completion timestamp.
Use the browser/probe for camera acceptance before trusting phone inference.

## Tests and physical acceptance

Software checks:

```sh
pio run -d firmware/camera-node -e front -e rear
python firmware/camera-node/tools/test_native.py
python -m unittest discover -s firmware/camera-node/tests -p 'test_*.py' -v
```

After flashing, close other viewers, connect a laptop to the AP and run:

```sh
python firmware/camera-node/tools/probe_cameras.py --frames 100
```

The probe opens both streams concurrently, checks JPEG framing, role/boot ID,
monotonic timestamps and indices, and requests `/sync` while streaming. It
prints received FPS, sync RTT and estimated frame age. JPEG marker checks do
not establish image quality, and the age estimate excludes exposure uncertainty.

Before integration sign-off:

1. Confirm each board's PSRAM, sensor identity, focus, orientation and stable power.
2. Stream both cameras for at least ten minutes; record FPS, frame ages, RSSI,
   free heap, capture/send errors and visible blur in daylight and lower light.
3. Check `/sync` and `/status` during video. Confirm a second viewer receives 409.
4. Restart the controller, interrupt Wi-Fi and unplug/reconnect each camera;
   confirm reconnection and correct role/IP without stale image reuse.
5. Monitor the **main controller** simultaneously: IMU gaps, maximum loop time,
   heap, buttons and alerts must remain acceptable under the two video streams.
6. Verify a slow/disconnected viewer releases its stream and a fresh viewer can
   connect. If bandwidth is poor, reduce `RS_CAMERA_FPS` to 5 or increase the
   JPEG-quality number modestly, then repeat the measurement.

No physical camera, Wi-Fi throughput or end-to-end perception result is implied
by compilation or host tests. See [VALIDATION.md](VALIDATION.md) for recorded checks.

## Driver references

- [Espressif AI-Thinker pin map](https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/ESP32/examples/Camera/CameraWebServer/camera_pins.h)
- [Espressif camera driver and timestamp definition](https://github.com/espressif/esp32-camera/blob/master/driver/include/esp_camera.h)
- [Pinned Arduino Wi-Fi AP configuration](https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/WiFi/src/WiFiAP.cpp)
