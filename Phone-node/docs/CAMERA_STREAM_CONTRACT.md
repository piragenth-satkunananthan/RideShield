# RideShield Camera Stream Interface Contract (v1.0)

This document specifies the communication and streaming contract between the separate **Front and Rear Camera Nodes** and the **RideShield Android Phone Application**, operating on the **NodeMCU ESP-32S** helmet SoftAP.

---

## 1. Network Topology

```text
 +-----------------------------------------------------------------+
 |              NodeMCU ESP-32S Helmet SoftAP Host                |
 |              SSID: RideShield-XXXX-BENCH / CORE                 |
 |              Gateway IP: 192.168.4.1                            |
 +-----------------------------------------------------------------+
         |                           |                      |
         v                           v                      v
+--------------------+     +-------------------+  +------------------+
| Main Safety Core   |     | Front Camera Node |  | Rear Camera Node |
| NodeMCU ESP-32S    |     | ESP32-CAM/S3-CAM  |  | ESP32-CAM/S3-CAM |
| 192.168.4.1:81     |     | 192.168.4.2:8080  |  | 192.168.4.3:8080 |
+--------------------+     +-------------------+  +------------------+
         ^                           ^                      ^
         | WebSocket (RSW1)          | MJPEG + Metadata     | MJPEG + Metadata
         +---------------------------+----------------------+
                                     |
                          +--------------------+
                          | Android Smartphone |
                          | (Pothole / HUD)    |
                          | IP: 192.168.4.X    |
                          +--------------------+
```

- **SSID**: `RideShield-XXXX-BENCH` or `RideShield-XXXX-CORE`
- **Password**: `rideshield-bench-only`
- **Subnet**: `192.168.4.0/24`
- **Node IP Allocations**:
  - Main Controller AP: `192.168.4.1` (WebSocket port 81)
  - Front Camera Node: `192.168.4.2` (HTTP Stream port 8080)
  - Rear Camera Node: `192.168.4.3` (HTTP Stream port 8080)
  - Phone Client: DHCP assigned (`192.168.4.4` ... `192.168.4.10`)

---

## 2. Stream Transport Protocols

The camera nodes support two streaming options:
1. **HTTP Multipart MJPEG with Custom Metadata Headers** (Default, highly reliable and compatible)
2. **Binary Frame WebSocket / UDP** (Low overhead alternative)

### 2.1 HTTP Multipart MJPEG Specification

- **Endpoint**: `GET http://<camera_ip>:8080/stream`
- **MIME Type**: `multipart/x-mixed-replace; boundary=frame`
- **Frame Rate**: Target 15–20 FPS
- **Resolution**: 640x480 (VGA) or 800x600 (SVGA)
- **JPEG Quality**: 75–85 (OV2640 JPEG compression engine)

#### Per-Frame Multipart Header Format:
Each frame part must include the standard HTTP content headers followed by custom RideShield metadata headers:

```http
--frame
Content-Type: image/jpeg
Content-Length: <jpeg_byte_length>
X-Camera-Node: FRONT
X-Frame-Index: <uint64_strictly_increasing_sequence>
X-Capture-Timestamp-Us: <uint64_esp_timer_get_time_microseconds>
X-Exposure-Us: <uint32_exposure_duration_microseconds>
X-Sensor-Gain: <uint16_sensor_gain_factor>

<JPEG binary bytes>
\r\n
```

#### Field Definitions:
| Header | Type | Description |
|---|---|---|
| `X-Camera-Node` | String | `FRONT` or `REAR` |
| `X-Frame-Index` | uint64 | Strictly increasing frame counter (starts at 1). Gaps indicate dropped capture frames on camera MCU. |
| `X-Capture-Timestamp-Us` | uint64 | Monotonic hardware timestamp from ESP32 `esp_timer_get_time()` in microseconds at the moment exposure completed. |
| `X-Exposure-Us` | uint32 | Sensor exposure duration in microseconds (used for motion blur compensation). |
| `X-Sensor-Gain` | uint16 | Sensor analog/digital gain (used for low-light detection). |

---

## 3. Camera-to-Phone Clock Synchronization

Because camera MCUs and smartphones run on independent oscillators, the phone must map camera hardware timestamps to its monotonic time domain (`SystemClock.elapsedRealtime()`).

### 3.1 Two-Way Synchronization Endpoint:
Camera firmware implements a low-overhead sync ping endpoint:

- **Endpoint**: `GET http://<camera_ip>:8080/sync`
- **Response Format**: JSON
  ```json
  {
    "node": "FRONT",
    "camera_us": 12485923048,
    "uptime_ms": 12485923
  }
  ```

### 3.2 Clock Mapping Algorithm on Phone:
1. Phone sends request at phone time $T_0$.
2. Camera responds with $T_{cam}$ (microsecond timer).
3. Phone receives response at phone time $T_1$.
4. Round-Trip Time: $RTT = T_1 - T_0$. If $RTT > 300\text{ ms}$, reject sample.
5. Estimated Offset:
   $$\text{Offset}_{cam \to phone} = \left(T_0 + \frac{RTT}{2}\right) - \frac{T_{cam}}{1000}$$
6. Uncertainty Bound:
   $$U_{cam} = \left\lceil \frac{RTT}{2} \right\rceil + 2\text{ ms}$$
7. When a frame arrives with timestamp $T_{cam}^{frame}$:
   $$\text{CapturePhoneMs} = \frac{T_{cam}^{frame}}{1000} + \text{Offset}_{cam \to phone}$$
8. The uncertainty $U_{cam}$ is passed into the RSW1 packet's `auxiliary` field (in milliseconds).

---

## 4. Backpressure and Dropping Policy on ESP32-S3

To prevent stale frame accumulation in camera firmware:
1. **DMA Double-Buffering**: Use 2 frame buffers in PSRAM.
2. **Drop-Oldest on Network Stall**: If the TCP socket write blocks or the previous frame is still being flushed over Wi-Fi, the camera firmware **must discard the buffered frame** and grab the newest sensor frame.
3. **No Unbounded Memory Buffers**: Never buffer more than 1 pending frame in firmware.

---

## 5. Sample ESP32-S3 Firmware Implementation (Arduino / ESP-IDF)

```cpp
#include "esp_camera.h"
#include "esp_timer.h"
#include <WiFi.h>
#include <WebServer.h>

WebServer server(8080);
static uint64_t frameIndex = 0;

void handleSync() {
    uint64_t nowUs = esp_timer_get_time();
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"node\":\"FRONT\",\"camera_us\":%llu,\"uptime_ms\":%llu}", 
             (unsigned long long)nowUs, (unsigned long long)(nowUs / 1000));
    server.send(200, "application/json", buf);
}

void handleStream() {
    WiFiClient client = server.client();
    client.print("HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n");

    while (client.connected()) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) { delay(10); continue; }

        uint64_t captureUs = esp_timer_get_time();
        ++frameIndex;

        char header[256];
        int headerLen = snprintf(header, sizeof(header),
            "--frame\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %u\r\n"
            "X-Camera-Node: FRONT\r\n"
            "X-Frame-Index: %llu\r\n"
            "X-Capture-Timestamp-Us: %llu\r\n\r\n",
            fb->len, (unsigned long long)frameIndex, (unsigned long long)captureUs);

        client.write((const uint8_t*)header, headerLen);
        client.write(fb->buf, fb->len);
        client.write("\r\n", 2);

        esp_camera_fb_return(fb);
        delay(50); // ~20 FPS
    }
}

void setupCameraServer() {
    server.on("/stream", HTTP_GET, handleStream);
    server.on("/sync", HTTP_GET, handleSync);
    server.begin();
}
```
