# RideShield WebSocket v2 (RSW2) Protocol Extension Proposal

## 1. Executive Summary & Problem Statement

The existing **RSW1 protocol** (`dev_version/rideshield-core/lib/RideShieldCore/src/phone.*`) is a tightly constrained, 64-byte binary protocol designed specifically for lightweight microcontrollers (ESP32-S3).

### Strict Schema Validation in RSW1:
The firmware controller performs byte-level boundary checks on every incoming packet:
```cpp
// phone.cpp line 7:
if(!b || n!=64 || std::memcmp(b,"RSW1",4) || b[4]<1 || b[4]>4 || b[5]>2 ||
   (b[6]&0xf0) || (b[7]&0xf8) || b[28]>3 || b[29]>100 || b[30] || b[31])
    return false;
```

Crucially:
- **Reserved Bytes (30..31)**: Must be zero.
- **Hazards Bitmask (byte 7)**: Only bits 0 (pothole), 1 (wet road), and 2 (rear vehicle) are valid (`hazards & 0xF8 == 0`).
- **Front Camera Hazards**: Strictly prohibits rear-vehicle hazard (`hazards & 4 == 0`) and requires `zone == UNKNOWN (0)`.
- **Rear Camera Hazards**: Strictly prohibits pothole or wet road hazards (`hazards & 3 == 0`).
- **No Floating-Point Distance / Tracks**: There are **zero** payload bytes allocated for tracking identifiers, distance in meters, closing speeds, or time-to-collision (TTC).

**Attempting to encode unsupported classes, tracking IDs, or distance measurements into RSW1 packets will immediately trigger `Reject::Schema` (code 8) or cause the controller to drop the connection.**

Furthermore, as noted in `PHONE_PROTOCOL.md` (line 94):
> *"Rear-vehicle is a general advisory. Camera image position alone is not a bike-relative blind spot, especially on a moving helmet. No numeric distance, closing speed or time-to-collision is carried by this version."*

---

## 2. Proposed RSW2 Specification

To support advanced rider assistance features—such as multi-target tracking, numeric ranging, time-to-collision estimation, and calibrated blind-spot monitoring—we propose **RSW2**.

### 2.1 Backward-Compatible Handshake Negotiation

The controller signals protocol support in the existing JSON `welcome` frame:
```json
{
  "type": "welcome",
  "version": 2,
  "supported_protocols": ["RSW1", "RSW2"],
  "session": 12847592
}
```
If the client elects to use RSW2, it issues its binary `AUTH` packet with magic `RSW2`. If the client only supports RSW1, it issues `RSW1`.

---

### 2.2 RSW2 128-Byte Binary Packet Layout

All multi-byte integers are encoded in **Little-Endian** format.

| Offset | Type | Field Name | Description |
|---|---|---|---|
| 0 | 4 bytes | Magic | ASCII `RSW2` (`0x52, 0x53, 0x57, 0x32`) |
| 4 | uint8 | `packet_type` | `1=AUTH`, `2=SYNC`, `3=HEARTBEAT`, `4=OBSERVATION`, `5=TRACK_UPDATE` |
| 5 | uint8 | `source` | `0=NONE`, `1=FRONT`, `2=REAR`, `3=SIDE_LEFT`, `4=SIDE_RIGHT` |
| 6 | uint8 | `flags` | bit 0: usable, bit 1: model-valid, bit 2: geometry-calibrated, bit 3: clock-synced, bit 4: helmet-yaw-compensated |
| 7 | uint8 | `hazard_type` | `0=NONE`, `1=POTHOLE`, `2=WET_ROAD`, `3=OIL_SLICK`, `4=VEHICLE`, `5=DEBRIS`, `6=PEDESTRIAN` |
| 8 | uint32 | `session` | Nonzero controller session ID |
| 12 | uint32 | `sequence` | Strictly increasing monotonic sequence counter |
| 16 | uint64 | `phone_ms` | Phone monotonic milliseconds (`SystemClock.elapsedRealtime()`) |
| 24 | uint32 | `auxiliary` | Sync ID (for SYNC) or Camera Clock Uncertainty in ms (for OBS) |
| 28 | uint8 | `zone` | `0=UNKNOWN`, `1=LEFT_BLIND_SPOT`, `2=DIRECT_REAR`, `3=RIGHT_BLIND_SPOT` |
| 29 | uint8 | `confidence` | Perception detection confidence (0..100) |
| 30 | uint16 | `target_track_id` | Unique identifier for persistent Kalman/ByteTrack tracklet |
| 32 | uint16 | `distance_cm` | Estimated distance to obstacle/vehicle in centimeters ($0..65535\text{ cm} \approx 655\text{ m}$) |
| 34 | int16 | `closing_speed_cms` | Relative range-rate in cm/s (positive = approaching, negative = departing) |
| 36 | uint16 | `time_to_collision_ms`| Estimated Time-To-Collision in ms ($0..65535\text{ ms}$) |
| 38 | uint8 | `road_friction_score` | Estimated tyre-road friction index (0..100; lower = slicker) |
| 39 | uint8 | `severity_level` | Advisory severity: `0=INFO`, `1=CAUTION`, `2=WARNING`, `3=CRITICAL` |
| 40 | uint16 | `bbox_x1_norm` | Normalized bounding box $x_1 \times 10000$ ($0..10000$) |
| 42 | uint16 | `bbox_y1_norm` | Normalized bounding box $y_1 \times 10000$ ($0..10000$) |
| 44 | uint16 | `bbox_x2_norm` | Normalized bounding box $x_2 \times 10000$ ($0..10000$) |
| 46 | uint16 | `bbox_y2_norm` | Normalized bounding box $y_2 \times 10000$ ($0..10000$) |
| 48 | float32 | `helmet_yaw_deg` | Relative helmet yaw angle at frame capture time (from IMU fusion) |
| 52 | 12 bytes | `reserved` | Reserved for future expansion, set to 0 |
| 64 | 32 bytes | `auth_token` | Pre-shared authentication token (used only in AUTH; 0 in others) |
| 96 | 32 bytes | `payload_hash` | Optional HMAC-SHA256 signature for cryptographic verification |

---

## 3. Advantages of RSW2 Over RSW1

1. **Persistent Target Tracking**:
   Allows the ESP32 safety core to track approaching vehicles across frames, eliminating spurious flickering warnings and enabling multi-frame velocity tracking.
2. **True Time-to-Collision (TTC)**:
   Enables graduated alert haptics (gentle pulse when vehicle is 4s away, urgent buzzer when vehicle closing speed indicates collision within 1.5s).
3. **Helmet Motion Decoupling**:
   Carries `helmet_yaw_deg` so the safety core can distinguish whether a vehicle is in the true bike blind spot or if the rider simply turned their head.
4. **Surface Friction Estimation**:
   Transitions wet-road detection from binary RGB classification to a friction coefficient estimation ($0.00..1.00$).

---

## 4. Implementation Schedule

- **Phase 1 (Current)**: Enforce exact RSW1 protocol. Keep phone pipeline strictly aligned with existing firmware.
- **Phase 2 (Firmware v2.0)**: Introduce dual-protocol support in ESP32-S3 `PhoneGate` (detect magic `RSW1` vs `RSW2`).
- **Phase 3 (App v2.0)**: Activate ONNX vehicle tracking and distance estimation models using the RSW2 layout.
