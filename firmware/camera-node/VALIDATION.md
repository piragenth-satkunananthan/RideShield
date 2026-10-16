# Camera firmware checkpoint — 2026-10-17

This is an implementation checkpoint, not hardware acceptance.

| Check | Observed result |
|---|---|
| `pio run -d firmware/camera-node -e front` (ignored nonempty-SSID `local_config.h`) | PASS; static RAM 49,396 / 327,680 bytes (15.1%); flash 864,585 / 3,145,728 bytes (27.5%) |
| `pio run -d firmware/camera-node -e rear` (same ignored config) | PASS; static RAM 49,396 / 327,680 bytes; flash 864,569 / 3,145,728 bytes |
| Strict warnings on camera sources | PASS; `-Wall -Wextra -Wshadow -Wconversion` produced no warning from `src/` or `include/` |
| `pio run -d firmware/main-controller -e nodemcu32s_core` | PASS; static RAM 50,316 / 327,680 bytes (15.4%); flash 956,841 / 1,310,720 bytes (73.0%) |
| `pio run -d firmware/main-controller -e nodemcu32s_bringup` | PASS; static RAM 49,544; flash 892,653 / 1,310,720 bytes (68.1%) |
| `pio run -d firmware/main-controller -e nodemcu32s_compile_all` | PASS; static RAM 50,316; flash 956,841 |
| Native camera protocol checks (`tools/test_native.py`) | PASS: freshness boundaries, multipart metadata, HTTP routing, partial writes, stalled writes and absolute send deadline |
| Python probe parser tests | 5 / 5 PASS |
| Main-controller portable core | 51 / 51 PASS; rider-input and configuration-guard tests also passed |
| Physical ESP32-CAM verification | Not performed; no device flashed |
| Concurrent video / clock-sync / controller timing | Implementation present; needs physical acceptance |

Both camera roles were built with a nonempty test SSID in an ignored
`firmware/camera-node/include/local_config.h`, so the Wi-Fi, lwIP and streaming
paths are linked rather than dead-code-eliminated. The earlier checkpoint's
front figure (453,649 bytes flash / 29,788 bytes RAM) was an EMPTY-SSID build
artifact; the configured nonempty-SSID image is roughly twice that size.

PlatformIO 6.2.0, Espressif32 platform 6.10.0 and Arduino framework
3.20017.241212 were used. A package mirror previously returned bad checksums;
PlatformIO rejected those downloads and installed from another mirror.
Checksum verification was not disabled.

The native tests exercise the actual portable stream helpers, not ESP32 camera
DMA, task scheduling, Wi-Fi or power behavior. Python tests validate the probe's
parser, not the physical camera output. The main controller's only code change
in this milestone moves DHCP allocation to `.20`, avoiding fixed camera `.2`
and `.3` addresses. No Android source was changed.

## Framework behavior confirmed against the shipped binary/source

- `camera_fb_t.timestamp` is time since boot in `esp_timer_get_time()`
  microseconds, set when the driver starts the frame (confirmed by
  disassembling `cam_start_frame` in `libesp32-camera.a`). `fresh()` and the
  `MAX_FRAME_AGE_MS` comparison therefore use the same clock the firmware reads.
- `WiFiClient` is a `std::shared_ptr<WiFiClientSocketHandle>`: copying one
  shares the socket, and the socket closes only when the last reference drops.
  Handing the accepted client from `serve()` to the stream worker is safe.
- `WiFiServer::available()` accepts a pending connection nonblocking.
- `WiFi.softAPConfig(..., 4th argument)` sets the DHCP lease start to
  `192.168.4.20` (pool `.20`–`.30`) in arduino-esp32 2.0.17.

## Still outstanding

Physical flash/upload of the controller and both cameras; confirmation of the
AI-Thinker pin map and PSRAM presence on the actual boards; measured FPS, sync
round-trip, estimated frame age, memory and reconnect behavior under video load;
and main-controller IMU gaps / loop time while streaming. Follow the physical
acceptance checklist in README.md after flashing real boards.
