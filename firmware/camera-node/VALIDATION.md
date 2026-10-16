# Camera firmware checkpoint — 2026-10-17

This is an implementation checkpoint, not hardware acceptance.

| Check | Observed result |
|---|---|
| `pio run -d firmware/camera-node -e front` | PASS; static RAM 29,788 / 327,680 bytes; flash 453,649 / 3,145,728 bytes |
| Front build configuration | Default empty SSID; no private `local_config.h`. Configured networking path still needs an explicit nonempty-SSID build |
| Rear firmware build | Not yet run |
| Updated main controller build | Not yet run after DHCP-start change |
| Native camera protocol checks | PASS: freshness boundaries, multipart metadata, HTTP routing, partial writes, stalled writes and absolute send deadline |
| Python probe parser tests | 5 / 5 PASS |
| Main-controller portable core | 51 / 51 PASS; rider-input and configuration-guard tests also passed |
| Physical ESP32-CAM verification | Not performed; no device flashed |
| Concurrent video / clock-sync / controller timing | Implementation present; needs physical acceptance |

PlatformIO 6.1.18, Espressif32 platform 6.10.0 and Arduino framework
3.20017.241212 were used for the front build. A package mirror returned bad
checksums; PlatformIO rejected those downloads and successfully installed from
another mirror. Checksum verification was not disabled.

The native tests exercise the actual portable stream helpers, not ESP32 camera
DMA, task scheduling, Wi-Fi or power behavior. Python tests validate the probe's
parser, not the physical camera output. The main controller's only code change
in this milestone moves DHCP allocation to `.20`, avoiding fixed camera `.2`
and `.3` addresses. No Android source was changed.

Before merging to main: build front AND rear with an ignored local configuration
using a nonempty test SSID, build the changed main-controller profiles, inspect
warnings and source, and update this record with actual results. Follow the
physical acceptance checklist in README.md after flashing real boards.
