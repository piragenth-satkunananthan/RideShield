# Phone–controller integration

This milestone connects the Android app to the original ESP32 NodeMCU-32S main
controller. It does not implement camera firmware, wet-road inference or rear
vehicle inference. Continuous background riding operation is still pending.

## Connect to the hardware

1. Upload the main-controller `nodemcu32s_core` profile.
2. In Android Wi-Fi settings join `RideShield-XXXX-CORE`. The bench password is
   `rideshield-bench-only`; use your private password if you changed firmware
   settings. Keep the connection when Android reports no Internet.
3. Open the app. Settings default to `ws://192.168.4.1:81` and the firmware's
   32-character bench token. Set the same private token on both sides if changed.
4. Tap Connect. The app routes its traffic over Wi-Fi, authenticates, responds
   to clock sync, and sends one heartbeat per second. Sensor telemetry is polled
   from `http://192.168.4.1/api/status` once per second after the connection is active.
5. Confirm changing IMU/ToF/IR readings, wear state, MQ screening, fresh GPS,
   SD/modem state, live-SMS setting, countdown, sample gaps and free heap.
   Sensor calibration remains through the controller's USB serial commands.

The HTTP endpoint uses the WebSocket hostname and port 80 (443 for wss). Custom
controller addresses must also be allowed by Android's network security config.
The shipped config permits the helmet's 192.168.4.x addresses and local emulator
addresses. Wi-Fi selection is manual; the app does not discover or auto-join an SSID.

## Connection behavior

- A successful socket send is not treated as authentication. The controller's
  sync request confirms that AUTH was accepted. Active status follows controller
  status messages.
- Handshake timeout: 6 seconds. Missing status: 3 seconds. Reconnect uses a
  1–15 second backoff. Manual disconnect cancels pending reconnects.
- Old socket callbacks cannot overwrite a newer session. Session reset clears
  controller state and observation validity. HTTP failure clears sensor readings.
- Session changes and packet sends are serialized, so concurrent front/rear
  producers cannot send sequence numbers out of order. Observation traffic
  reserves packet capacity for control messages and heartbeats.
- Missing/expired sync, future/stale captures and invalid uncertainty block
  observations. The controller remains the authority for acceptance; the phone's
  clock offset/uncertainty is a provisional estimate, not a measured RTT.
- Connection settings are saved in app-private preferences, with app backup
  disabled. Tokens must be exactly 32 printable ASCII characters; no padding or
  Unicode substitution is performed.

## Camera previews and model behavior

Hardware mode is the default. Camera previews start only when explicitly
requested. The simulation toggle is a local preview: synthetic frames are
marked and cannot become outgoing hardware observations, even during a mode
switch. Failed pothole-model initialization reports unavailable instead of
falling back to invented detections. The bundled ONNX asset loader returns the
loaded bytes correctly. Model quality and real camera timing still need evaluation.

## Build and checks

Use JDK 17, Android SDK 34 and the included Gradle 8.7 wrapper:

```sh
./gradlew testDebugUnitTest assembleDebug
```

Configure your own Android SDK path in `local.properties`. The debug APK is at
`app/build/outputs/apk/debug/app-debug.apk` after a successful build.

New tests cover auth confirmation, late callbacks, manual disconnect, status
expiry/reconnect, concurrent packet ordering, invalid connection settings,
expired clocks, telemetry parsing and stale-reading removal. Existing packet
encoding tests retain the RSW1 golden vector.

## Physical acceptance still required

Keep the app foregrounded for this milestone. Test with mobile data enabled,
rotate the phone, switch off/restart the controller, interrupt Wi-Fi, reconnect,
and verify stale readings clear and the new session starts correctly. Confirm
calibrated sensors and logging on the board. No physical link, model accuracy,
background lifecycle or SMS delivery is established by a software build.
