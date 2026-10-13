# RideShield: Wokwi core test

Use **Wokwi for VS Code** with PlatformIO for this multi-file C++17 project.
This is a separate simulation harness using the original `Core` implementation.
It does not run the production FreeRTOS/peripheral integration in `main.cpp`.
The original bench/hardware profiles remain available.

## Start

1. Extract this ZIP. Open the folder containing `platformio.ini` in VS Code.
2. Install the **PlatformIO IDE** and **Wokwi Simulator** extensions. Complete
   Wokwi's license/account setup if the extension prompts for it.
3. In a PlatformIO terminal, build:

   ```sh
   pio run -e esp32s3_wokwi
   ```

4. Press **F1 → Wokwi: Start Simulator**.
5. Open the simulation serial monitor. Newline must be enabled. Type `help`.

Do not use the upload command: Wokwi loads the local build files. This bundle
contains source/configuration, not a precompiled ESP32 binary. First build may
need to download the pinned ESP32 toolchain.

## Circuit

| Part | GPIO | Meaning |
|---|---|---|
| Orange IMPACT button | 4 → GND when pressed | Inject one strong synthetic impact |
| Green CANCEL button | 21 → GND | Hold >1 s during countdown |
| Red SOS button | 14 → GND | Hold >3 s |
| Red LED | 39 through 330 ohm resistor | Active buzzer output indicator |
| Yellow LED | 40 through 330 ohm resistor | Vibration output indicator |
| Green LED | 41 through 330 ohm resistor | Status output |

The red LED replaces an active buzzer for a visible timing test. The yellow LED
replaces the motor/driver. There is no audible buzzer or physical vibration.
The board is configured as ESP32-S3, 16 MB flash, 8 MB octal PSRAM, USB serial.
`wokwi-flash.json` loads the built bootloader, partition table and application.

## Run these tests

Start with all buttons released. Use simulation time, which may run slower than
wall-clock time. Stop/restart the simulation to reset the harness completely.

| Test | Action | Expected |
|---|---|---|
| Normal | Let the simulation run | MONITOR; status LED heartbeat |
| Impact | Click IMPACT once | COUNTDOWN; red/yellow output pulses |
| Cancellation | During countdown, hold CANCEL for about 1.2 s, then release | Cancelled event; return to MONITOR |
| Uncancelled event | Click IMPACT; wait 15 simulated seconds | INCIDENT, emergency pulses, SMS PREVIEW ONLY, then SmsDisabled |
| Acknowledge/close | After preview, type `ack`, then `close` | Alarm acknowledged, then MONITOR |
| Manual SOS | Hold SOS about 3.2 s, then release | Immediate INCIDENT after hold; no crash countdown |
| Missing sensor | Type `imu off`, then hold SOS | DEGRADED still permits manual SOS |
| Restore | Type `imu on`, `ack`, `close` | Monitoring resumes |
| Handling | On a fresh run, type `head absent`, then click IMPACT | Handling event, no countdown |
| Road/rear advisory | On a fresh run, type `pothole`, `wet`, or `rear` | Synthetic advisory for ~5 s, yellow LED pulse |

Serial alternatives: `impact`, `candidate`, `cancel down`, `cancel up`, `sos down`,
`sos up`, `head present`, `head unknown`, `gps`, `clear`, `status`.
`candidate` injects two moderate frames; subsequent synthetic stillness can confirm
it. `gps` injects the example coordinates 6.9,79.8. `clear` clears advisories only;
it cannot cancel an emergency.

## What this validates

This harness exercises the same crash/SOS state machine, button timing, warning
priority and SMS-job formatting as the core. Motion input is synthetic gravity
at roughly 200 Hz plus injected impacts. Road/rear commands inject already-accepted
Vision values directly: they do **not** validate PhoneGate, Wi-Fi or AI models.

Not included in this harness: MPU6050 register/FIFO driver, live phone protocol,
GPS UART, LTE AT commands, real SMS delivery, SD/NVS recovery, recording, production
task scheduling, AI detection, distance estimation or calibrated blind-spot zones.
Those need the separate native tests and subsequent integration/hardware tests.

## Verification status

- Three host smoke checks of `src/wokwi_main.cpp`: PASS (impact GPIO output,
  cancel hold, SOS preview-only SMS).
- JSON/TOML configuration parsed successfully.
- Original core: 51 passing native tests; original BENCH firmware previously
  compiled successfully. See `docs/TEST_REPORT.md` for the original test evidence.
- **This new Wokwi profile has not been cross-compiled or run in Wokwi here.**
  Host checks do not validate simulator compatibility or ESP32 timing.

Reproduce the new host smoke checks with:

```sh
python3 tools/test_wokwi_harness.py
```

If the serial monitor is blank, check `serialInterface: USB_SERIAL_JTAG` in the
board attributes and retain the provided USB build flags. If firmware is missing,
run the exact `esp32s3_wokwi` build command above. Do not point Wokwi at a different
profile's firmware.

## Documentation used

- https://docs.wokwi.com/vscode/platformio
- https://docs.wokwi.com/vscode/getting-started
- https://docs.wokwi.com/guides/esp32
- https://docs.wokwi.com/vscode/project-config

For browser-only Wokwi, this PlatformIO folder cannot be pasted as one Arduino
sketch. Use this VS Code route to keep the pinned compiler options and multi-file
core unchanged.
