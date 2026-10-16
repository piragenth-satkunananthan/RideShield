# RideShield Phone Node: Native Android HUD & Perception Application

Native Kotlin Android application for the **RideShield Smart Helmet**.
This application functions as the rider's Heads-Up Display (HUD) and perception producer, connecting directly to the helmet's main ESP32 NodeMCU-32S microcontroller over Wi-Fi and WebSockets.

See [Controller integration](docs/CONTROLLER_INTEGRATION.md) for the current hardware connection flow, live sensor telemetry, connection tests and remaining device checks. Hardware mode is the default; camera previews are explicit and synthetic frames stay on the phone.

---

## Key Capabilities

1. **Exact RSW1 Protocol Client**:
   - Implements the strict 64-byte little-endian binary protocol (`dev_version/rideshield-core/lib/RideShieldCore/src/phone.*`).
   - Handles the complete connection lifecycle: Welcome parsing, 32-byte token AUTH, clock synchronization, 1 Hz heartbeats, and fresh front/rear observation dispatch.
   - Enforces sequence numbering, rejection code handling, rate limiting (<40 pkts/s), and clock freshness bounds ($\le 500\text{ ms}$).

2. **On-Device Pothole Inference (`best.pt` $\to$ `best.onnx`)**:
   - Executes custom-trained YOLOv12 model on incoming camera frames via ONNX Runtime Mobile.
   - Real-time bounding box detection, confidence scoring, letterbox preprocessing, and Non-Maximum Suppression (NMS).
   - Generates confirmed hazard flags for the helmet controller when confidence $\ge 60\%$.

3. **Strict Unavailable Model Policy**:
   - Dedicated interfaces for `WetRoadClassifier` and `RearVehicleDetectorTracker`.
   - When model weights are unavailable, the app reports status **`UNAVAILABLE`** and strictly produces **zero invented detections**.

4. **Dual Camera Stream Engine**:
   - Ingests timestamped JPEG streams from separate Front (`192.168.4.2:8080`) and Rear (`192.168.4.3:8080`) ESP32-S3 camera nodes.
   - Bounded newest-frame queues (capacity 1, drop-oldest) to guarantee zero stale frame latency.
   - Estimates camera-to-phone clock offset and passes uncertainty ($U_{cam}$) in the RSW1 packet.
   - Real-time quality diagnostics: stream age (ms), sequence gaps, frame drops, decode latency, and sharpness.

5. **Safety Boundary Isolation**:
   - Crash detection, SOS emergency SMS dispatch, blackbox recording, and buzzer/vibration alerts remain autonomous on the ESP32 NodeMCU-32S microcontroller.
   - The phone application operates strictly as an external perception sensor and telemetry monitor.

6. **RSW2 Versioned Protocol Proposal**:
   - Comprehensive proposal documenting why RSW1 cannot transmit tracking IDs, numeric distance, or closing speed without breaking controller schema validation, and proposing the 128-byte `RSW2` layout.

---

## Documentation

- [Setup & Architecture Guide](docs/SETUP_AND_ARCHITECTURE.md)
- [ESP32-S3 Camera Stream Interface Contract](docs/CAMERA_STREAM_CONTRACT.md)
- [PyTorch `best.pt` Model Conversion Guide](docs/MODEL_CONVERSION_GUIDE.md)
- [RSW2 Protocol Extension Proposal](docs/RSW2_PROTOCOL_PROPOSAL.md)

---

## Quick Start

### 1. Build & Run in Android Studio
1. Open this directory (`/home/piragenth/Documents/rideshield/Phone-node`) in Android Studio (Iguana / Jellyfish / Koala or newer).
2. Allow Gradle to sync.
3. Build and launch on an emulator or Android device:
   ```bash
   ./gradlew assembleDebug
   ```

### 2. Run Comprehensive Unit Tests
```bash
./gradlew test
```

### 3. Verify RSW1 Wire Encoding Against Python Simulator
```bash
python3 scripts/test_rsw1_against_phone_sim.py
```
