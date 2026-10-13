# Validation report — reconstructed core, 2026-10-13

These results were rerun against this package's reconstructed source. Earlier
workspace build results are not used as evidence for this version.

| Check | Result |
|---|---|
| Native production core, g++ C++17 / warnings as errors | 51/51 pass |
| Same tests under AddressSanitizer + UndefinedBehaviorSanitizer | 51/51 pass |
| Python protocol and log-decoder tests | 5/5 pass |
| Executable demonstration | 4/4 scenarios pass |
| Compile-time configuration guards | 3/3 expected outcomes |
| ESP32-S3 BENCH firmware | Compiled and linked successfully |
| ESP32-S3 HARDWARE firmware | Not confirmed; final build results unavailable |
| All-peripheral integration profile, SMS disabled | Not confirmed; final build results unavailable |
| Physical board, sensors, phone app, cellular network | Not tested |

Native tests compile the actual portable production modules, not duplicated
mock implementations of their logic. Cases cover candidate support/rejection,
clipping, invalid IMU samples, missing sensors, button bounce/holds, cancellation,
manual SOS, 64-bit uptime, retry identity, stale location, full log queues, corrupt
journals, reboot recovery, stale/replayed phone packets, camera/heartbeat clock
separation, alert priority, recorder bounds and modem transaction outcomes.

The deterministic parser stress case processes 100,000 random bounded buffers and
modem characters. This is limited fuzz coverage, not an exhaustive security audit.
LeakSanitizer is disabled (`ASAN_OPTIONS=detect_leaks=0`) because of the execution
environment; address and undefined-behaviour checking remain enabled. No heap-leak
claim is made.

The modem tests feed synthetic AT transcripts only. They do not transmit SMS.
The phone sender is synthetic and has not been connected to a flashed board here.
No claims are made about real crash sensitivity, false-positive rates, model
accuracy, detection range, physical latency, power integrity or recipient delivery.

Raw outputs are in `docs/results/`. Reproduce using the commands in README.md.

## ESP32 compiler result and interrupted validation

The BENCH profile successfully generated firmware.elf and firmware.bin with the
pinned toolchain. Observed build output: static RAM 47,708 / 327,680 bytes;
flash 776,889 / 6,553,600 bytes. These are linker figures, not runtime heap/stack
or PSRAM measurements.

The subsequent hardware/all-peripheral build result could not be collected:
automatic approval review failed because the session usage limit was reached.
Workspace maintenance then removed the build directory and raw compiler log.
The source package and host-test logs were preserved. The BENCH result above was
observed before that interruption; the two other profiles are not claimed passed.
No firmware binary is distributed in this source package.

To complete those integration checks locally:

```sh
pio run -e esp32s3_hardware -e esp32s3_compile_all
```
