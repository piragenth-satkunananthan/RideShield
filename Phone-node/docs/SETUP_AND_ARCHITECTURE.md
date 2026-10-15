# RideShield Android App: Architecture and Setup Guide

## 1. System Overview & Safety Boundary

RideShield is an intelligent motorcycle helmet system that integrates sensor telemetry, crash detection, automated emergency communication, and computer vision perception.

### Critical Safety Invariant:
```text
+---------------------------------------------------------------------------------+
|                    MAIN SAFETY CORE (NodeMCU ESP-32S)                           |
|                    Wi-Fi SoftAP Host: 192.168.4.1:81                            |
|                                                                                 |
|  [MPU6050 IMU]   --> Crash State Machine (Monitor/Degraded/Verify/Countdown)    |
|  [Head Sensor]   --> Helmet-Worn Presence Detection                             |
|  [Emergency]     --> LTE Modem / Direct Emergency SMS                           |
|  [BlackBox]      --> MicroSD Pre-trigger & Incident Acceleration Recording      |
|  [Haptics]       --> Buzzers, Vibration Motors, Warning LEDs                    |
+---------------------------------------------------------------------------------+
                                       ^
                                       | RSW1 Protocol (64-byte binary over WebSocket)
                                       v
+---------------------------------------------------------------------------------+
|                               ANDROID PHONE NODE                                |
|                                                                                 |
|  [Wi-Fi AP]      --> Joined to NodeMCU ESP-32S SoftAP (192.168.4.1:81)           |
|  [Front Stream]  --> Ingests timestamped MJPEG from Front Camera (192.168.4.2)  |
|  [Rear Stream]   --> Ingests timestamped MJPEG from Rear Camera (192.168.4.3)   |
|  [YOLO Model]    --> On-device pothole inference (best.onnx via ONNX Runtime)   |
|  [Bounded Queue] --> Drops stale frames; guarantees <= 500ms freshness envelope |
|  [HUD Display]   --> Live previews, bounding boxes, latencies, telemetry cards  |
|                                                                                 |
|  NOTE: The phone node NEVER triggers crashes, SOS messages, SMS alerts, or      |
|        direct buzzer outputs. It acts strictly as an external perception        |
|        producer and HUD telemetry monitor.                                      |
+---------------------------------------------------------------------------------+
```

### NodeMCU ESP-32S Hardware Specifics (Main Safety Core):
- **SoC Architecture**: Dual-Core Xtensa 32-bit LX6 @ 240 MHz (ESP-WROOM-32 module).
- **Wi-Fi SoftAP Configuration**:
  - The NodeMCU ESP-32S acts as the central Wi-Fi Access Point (`RideShield-XXXX-BENCH` or `CORE`).
  - IP Address: `192.168.4.1`, WebSocket Server Port: `81`.
  - SoftAP Station Capacity: 4 concurrent devices (1 Phone + 1 Front Camera + 1 Rear Camera).
- **Memory Model**:
  - 520 KB internal SRAM, 4 MB external SPI Flash (no external PSRAM).
  - The firmware in `src/main.cpp` automatically activates the internal SRAM ring-buffer fallback (`preCount=200`, `eventCount=1402`, `postMs=6000`), ensuring complete blackbox recording stability without external PSRAM.
- **Recommended NodeMCU ESP-32S Pinout**:
  - **I2C Bus (MPU6050 & VL53L0X)**: SDA = `GPIO 21`, SCL = `GPIO 22`
  - **Hardware SPI (MicroSD Blackbox)**: SCK = `GPIO 18`, MISO = `GPIO 19`, MOSI = `GPIO 23`, CS = `GPIO 5`
  - **UART2 (SIM800L LTE Modem)**: RX = `GPIO 16`, TX = `GPIO 17`
  - **UART1 (NEO-6M GPS Module)**: RX = `GPIO 32`, TX = `GPIO 33`
  - **Switches & Inputs**: Cancel = `GPIO 25`, SOS Touch = `GPIO 26`, Strap Reed Switch = `GPIO 27`
  - **Alert Actuators**: Buzzer = `GPIO 4`, Vibration Motor = `GPIO 14`, Warning LEDs = `GPIO 13`
  - *Caution*: Do NOT use `GPIO 6..11` on NodeMCU ESP-32S (reserved for integrated SPI flash).

---

## 2. Android Studio Project Structure

```text
Phone-node/
├── app/
│   ├── build.gradle.kts                   # Module build configuration (CompileSdk 34, MinSdk 26)
│   ├── proguard-rules.pro                 # Obfuscation & reflection preservation rules
│   └── src/
│       ├── main/
│       │   ├── AndroidManifest.xml        # Cleartext network permissions & hardware declarations
│       │   ├── assets/models/best.onnx    # Exported YOLOv12s pothole model weights (35 MB)
│       │   ├── java/com/rideshield/phone/
│       │   │   ├── RideShieldApp.kt       # Application initialization
│       │   │   ├── protocol/              # Exact RSW1 wire protocol implementation
│       │   │   │   ├── RswConstants.kt    # Wire constants matching phone.h & PHONE_PROTOCOL.md
│       │   │   │   ├── RswPacket.kt       # 64-byte packet model & factory methods
│       │   │   │   ├── RswCodec.kt        # Little-endian binary encoder & validator
│       │   │   │   ├── RswMessages.kt     # JSON controller message parsers
│       │   │   │   └── RswClockSynchronizer.kt # Clock offset & freshness bound validator
│       │   │   ├── network/               # WebSocket client & Wi-Fi management
│       │   │   │   ├── RswWebSocketClient.kt # Handshake, auth, 1Hz heartbeat, observation dispatch
│       │   │   │   ├── RswClientState.kt  # StateFlow telemetry data classes
│       │   │   │   └── WifiApManager.kt   # Wi-Fi process socket binding
│       │   │   ├── camera/                # Camera streaming & clock synchronization
│       │   │   │   ├── CameraFrame.kt     # Timestamped JPEG frame container
│       │   │   │   ├── BoundedFrameQueue.kt # Capacity 1 drop-oldest frame queue
│       │   │   │   ├── CameraClockFilter.kt # Microsecond-to-millisecond clock estimator
│       │   │   │   ├── CameraQualityMonitor.kt # FPS, stream age, sequence gaps, sharpness
│       │   │   │   ├── CameraStreamClient.kt # Multipart MJPEG client & stream parser
│       │   │   │   └── MockCameraSource.kt# Synthetic frame generator for bench testing
│       │   │   ├── vision/                # On-device computer vision pipelines
│       │   │   │   ├── VisionContracts.kt # Model interfaces & bounding box structures
│       │   │   │   ├── PotholeDetector.kt # Pothole detection contract
│       │   │   │   ├── OnnxPotholeDetector.kt # ONNX Runtime YOLOv12 inference engine
│       │   │   │   ├── MockPotholeDetector.kt # Fallback simulation detector
│       │   │   │   ├── WetRoadClassifier.kt # Wet-road classification contract
│       │   │   │   ├── UnavailableWetRoadClassifier.kt # Reports UNAVAILABLE; zero invented detections
│       │   │   │   ├── RearVehicleDetectorTracker.kt # Rear vehicle detection contract
│       │   │   │   ├── UnavailableRearVehicleDetector.kt # Reports UNAVAILABLE; zero invented detections
│       │   │   │   └── VisionPipelineManager.kt # Worker orchestrator & observation dispatcher
│       │   │   ├── safety/
│       │   │   │   └── SafetyBoundaryGuard.kt # Enforces safety architectural boundaries
│       │   │   └── ui/                    # Modern Jetpack Compose & Material 3 HUD
│       │   │       ├── MainActivity.kt    # Permission request & lifecycle host
│       │   │       ├── MainViewModel.kt   # Reactive state management
│       │   │       ├── theme/             # Dark HUD styling (Color, Type, Theme)
│       │   │       ├── components/        # Reusable UI cards & dialogs
│       │   │       └── screens/DashboardScreen.kt # Complete HUD dashboard
│       │   └── res/                       # Icons, colors, themes, cleartext security config
│       └── test/java/com/rideshield/phone/ # Unit & integration test suites
│           ├── protocol/RswPacketEncodingTest.kt # Wire encoding & golden vector verification
│           ├── network/StaleFrameRejectionTest.kt# Freshness envelope & stale drops
│           ├── network/DisconnectReconnectTest.kt # Session reset & handshake lifecycle
│           ├── vision/UnavailableModelsTest.kt   # Verifies UNAVAILABLE status & zero fake hazards
│           └── camera/                           # Bounded queue & clock filter tests
├── docs/
│   ├── CAMERA_STREAM_CONTRACT.md          # ESP32-S3 Camera firmware interface contract
│   ├── MODEL_CONVERSION_GUIDE.md          # PyTorch best.pt to ONNX export guide
│   ├── RSW2_PROTOCOL_PROPOSAL.md          # Versioned extension for distance & tracking
│   └── SETUP_AND_ARCHITECTURE.md          # This setup document
└── scripts/
    ├── convert_best_pt.py                 # Automated model export script
    └── test_rsw1_against_phone_sim.py     # Protocol verification harness
```

---

## 3. Setup and Build Instructions

### Prerequisites:
- **Android Studio**: Iguana (2023.2.1), Jellyfish (2023.3.1), Koala (2024.1.1), or newer.
- **JDK**: Java 17 (recommended default in Android Studio).
- **Android SDK**: API Level 34 installed.

### Step 1: Open Project in Android Studio
1. Launch Android Studio.
2. Select **Open** and choose the directory: `/home/piragenth/Documents/rideshield/Phone-node`.
3. Allow Gradle to sync dependencies (AndroidX Compose, OkHttp, ONNX Runtime Android).

### Step 2: Build the Application
- From the menu, select **Build > Make Project** (or press `Ctrl+F9`).
- Or from terminal:
  ```bash
  ./gradlew assembleDebug
  ```
  The compiled APK will be located at `app/build/outputs/apk/debug/app-debug.apk`.

### Step 3: Run Unit Tests
To execute the comprehensive protocol and safety tests:
```bash
./gradlew test
```

---

## 4. Testing & Running the App

### Option A: Out-of-the-Box Bench Simulator Mode (No Hardware Required)
1. Launch the app on an Android Emulator or connected physical phone.
2. By default, **Bench Simulator Mode** is active.
3. The app generates real-time synthetic camera frames with perspective road lanes and simulated potholes at 10 FPS.
4. You can observe:
   - Live camera preview with detected pothole bounding boxes.
   - Pothole inference latency (~40–70 ms).
   - Stream quality metrics (FPS, stream age, sequence gaps, queue drops).
   - Wet-road and Rear-vehicle pipelines explicitly flagged as **UNAVAILABLE** with zero invented detections.

### Option B: Connecting to Physical ESP32-S3 Helmet Hardware
1. Power on the main ESP32-S3 safety board running `src/main.cpp`.
2. Connect your Android phone to the ESP32 Wi-Fi SoftAP:
   - **SSID**: `RideShield-XXXX-BENCH` (or `CORE`)
   - **Password**: `rideshield-bench-only`
3. In the RideShield Android app, tap the **Settings** icon (top right):
   - **WebSocket URL**: `ws://192.168.4.1:81`
   - **Token**: `bench-token-change-before-use-01` (32 bytes)
   - Toggle **Bench Simulator Mode** to **OFF** (to ingest from live camera nodes).
   - Tap **Apply**.
4. Tap the **Power** icon in the top app bar to initiate connection.
5. The app executes the RSW1 handshake:
   - Receives `{"type":"welcome","session":...}`
   - Sends binary `AUTH` with sequence `1`
   - Receives `{"type":"sync",...}` and immediately replies with binary `SYNC` (sequence `2`)
   - Starts 1 Hz `HEARTBEAT` loop (sequence `3, 4, ...`)
   - Dispatches fresh `OBSERVATION` packets for processed frames.
6. The HUD updates in real-time with controller telemetry:
   - State (`Monitor`, `Verify`, `Countdown`, `Incident`)
   - Active Warnings (`None`, `Pothole`, `WetRoad`, `Rear`, `Emergency`)
   - `front_valid` and `rear_valid` flags confirmed by controller
   - Measured Clock Offset and Uncertainty ($\pm\text{ms}$).
