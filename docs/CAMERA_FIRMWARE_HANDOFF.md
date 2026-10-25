# RideShield firmware handoff — 2026-10-17

## Scope and source of truth

Repository: https://github.com/piragenth-satkunananthan/RideShield

Continue from branch **`camera-nodes`**, not only `main`. Fetch current remote
state and inspect local changes first. Preserve newer user work. The user
explicitly assigns the main controller and front/rear camera firmware to this
agent; **Android app development is outside scope**. Reading its interface to
identify compatibility issues is useful, but leave its source unchanged.

Hardware confirmed by user:

- Main: original **ESP32 NodeMCU-32S**, not ESP32-S3.
- IMU: MPU-9250-labelled module, reported WHO_AM_I `0x70`; firmware supports
  MPU-6500-class accelerometer/gyro readings.
- KY-033 IR (not OP0062), VL53L0X ToF. User reported these sensors working.
- Front AND rear: **ESP32-CAM with OV2640**. The firmware assumes the common
  AI-Thinker pin map and PSRAM; board variant has not been independently verified.
- GPS, LTE/SMS, SD, strap/buttons and actuators have firmware integration, but
  complete physical validation is still outstanding. Live SMS stays disabled.

`firmware/main-controller/` is the real safety controller.
`dev_version/rideshield-core/` is simulation/testing only.
`firmware/camera-node/` is the new shared camera project (`front` / `rear`).

## Read first

1. Any applicable AGENTS.md.
2. `firmware/camera-node/README.md`, `STREAM_PROTOCOL.md`, `VALIDATION.md`.
3. `firmware/camera-node/src/main.cpp` and its include files.
4. `firmware/main-controller/README.md`, `VALIDATION.md`, `platformio.ini`.
5. `docs/PROJECT_PROGRESS.md`.

Older proposal/architecture documents can be stale: do not restore ESP32-S3 as
the main board, MPU6050 or OP0062 based on those documents.

## Implemented on camera-nodes

- Pinned PlatformIO Espressif32 6.10.0 / Arduino ESP32 2.0.17 project.
- VGA JPEG capture, two PSRAM driver buffers, latest-frame grab mode, 10 FPS
  default target per camera. Actual FPS has not been measured.
- Exact configured AP SSID/password; Wi-Fi reconnect; static front `.2`, rear `.3`.
- Port 8080 `/stream`, `/sync`, `/status`, browser preview `/`.
- Stream task separate from control requests; one stream consumer per node.
- Monotonic driver timestamp, frame sequence, role and boot ID in each part.
- Pre-send age rejection; nonblocking partial-write handling with one 250 ms
  deadline per whole frame; close stream after a partial/failed send.
- Flash LED remains off; no inference, fake detections or invented exposure/gain.
- Main controller DHCP starts at `192.168.4.20` to reserve cameras `.2` / `.3`.
- Host protocol tests, Python probe parser tests and a two-camera physical probe.
- Setup, upload, protocol, progress and handoff documentation.

Important semantics: `camera_fb_t.timestamp` is time since boot at the first
DMA buffer, NOT measured exposure completion. Frame index counts buffers
acquired by firmware, not every sensor exposure. Send success means accepted
by the socket, not consumer receipt. End-to-end freshness remains unverified.

## Verification actually completed

- Front AND rear both built with a nonempty test SSID in an ignored
  `local_config.h`, so Wi-Fi/lwIP/streaming paths are linked (not
  dead-code-eliminated): front flash 864,585 / static RAM 49,396; rear flash
  864,569 / static RAM 49,396. This supersedes the earlier front 453,649 /
  29,788 figure, which was an EMPTY-SSID build artifact.
- Camera sources compile clean under `-Wall -Wextra -Wshadow -Wconversion`
  (no warning from `src/` or `include/`; only framework `WString.h` noise).
- Changed controller profiles rebuilt: `nodemcu32s_core` 956,841 flash /
  50,316 RAM; `nodemcu32s_bringup` 892,653 / 49,544; `nodemcu32s_compile_all`
  956,841 / 50,316 — all PASS.
- Native stream helper suite passed; five Python probe parser tests passed.
- Main-controller native suite: 51/51 core tests, rider-input suite and board
  configuration guards passed.
- Framework behavior confirmed against the shipped camera library: frame
  `timestamp` is since-boot `esp_timer_get_time()` microseconds; `WiFiClient`
  shares a refcounted socket; `WiFiServer::available()` is nonblocking;
  `softAPConfig` 4th argument sets the `.20` DHCP start.
- Code review found no socket-ownership, concurrency, reconnect, buffer-lifetime
  or error-recovery defects requiring a fix.
- No physical camera/board was flashed or tested here.

Do not claim the camera subsystem, real-time behavior or road safety is verified.

## Next actions

Steps 1–5 below are DONE as of the 2026-10-17 re-check (see "Verification
actually completed"). Remaining work is the physical steps 6–7; no board has
been flashed or measured yet.

1. Fetch `camera-nodes` and `main`, inspect status and preserve changes. Resume
   the checkpoint rather than rewriting working code.
2. Review firmware for socket ownership, task concurrency, reconnect behavior,
   buffer lifetime, timestamp semantics, request limits and error recovery.
3. Create ignored `firmware/camera-node/include/local_config.h` with a clearly
   labelled nonempty test SSID/password for compile checks; do not commit it.
   Real upload configuration must use the user's exact controller SSID.
4. Run:

   ```sh
   pio run -d firmware/camera-node -e front -e rear
   pio run -d firmware/main-controller -e nodemcu32s_core -e nodemcu32s_bringup
   python firmware/camera-node/tools/test_native.py
   python -m unittest discover -s firmware/camera-node/tests -p 'test_*.py' -v
   python firmware/main-controller/tools/test_native.py
   ```

5. Fix actual failures, record exact results in camera VALIDATION.md, commit and
   push. Merge/publish tested work to main while preserving newer upstream work.
6. Guide hardware upload in order: updated main controller, front camera, rear
   camera. Renew the phone/laptop DHCP lease to remove old `.2`/`.3` leases.
7. Test browser previews first, then close viewers and run
   `python firmware/camera-node/tools/probe_cameras.py --frames 100` from a laptop
   on the AP. Measure both streams, sync RTT, estimated age, memory, reconnect
   behavior and main-controller IMU gaps/loop time under video load.

## Phone integration limitation — report, do not edit

The phone reader inspected at this checkpoint does not correctly skip the CRLF
between multipart JPEGs before parsing the next headers. It also needs actual
`/sync` requests, reboot-aware clock reset and truncated-JPEG handling. Hand
these findings to the app owner. Do not distort valid camera framing to work
around an invalid consumer or claim end-to-end phone inference is ready.

## Previous environment notes

The prior workspace reset after long pauses. GitHub commits are the durable
checkpoint; local tools/caches/logs may disappear. Build tool was a scratch venv
at `/workspace/scratch/7f7ad9cb9940/pio-venv/bin/pio` (PlatformIO 6.1.18).
Use normal PlatformIO elsewhere. A mirror checksum mismatch was resolved by
PlatformIO's verified alternate mirror; never disable integrity checks.

Continue autonomously with firmware work and concise progress updates. Ask only
when missing hardware information materially prevents correct implementation.
