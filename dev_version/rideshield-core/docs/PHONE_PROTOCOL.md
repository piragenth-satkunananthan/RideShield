# Phone → main-controller protocol v1

The main board is a Wi-Fi AP at the usual Arduino AP address 192.168.4.1 and hosts
an unencrypted WebSocket server on port 81. It authenticates one producer using a
32-byte pre-shared token, with a new random session on each connection. WPA2 protects
the radio association; WebSocket itself is not TLS. This is a local prototype
protocol, not an internet-facing endpoint. It carries observations, never SOS,
cancel, SMS-recipient changes or direct GPIO commands.

## Connection sequence

1. Connect; receive JSON `{"type":"welcome","version":1,"session":...}`.
2. Within 5 s send binary AUTH, sequence 1, returned session and configured token.
3. Receive JSON `{"type":"sync","id":...,"controller_ms":...}`.
4. Immediately return binary SYNC with `auxiliary=id` and the phone's monotonic
   clock in milliseconds. Continue global sequence numbering: 2, 3, 4…
5. Send HEARTBEAT every second; sync again on each request (normally every 10 s).
6. Send OBSERVATION for each processed distinct frame using its **original capture
   timestamp expressed in the phone's monotonic time domain**. Do not stamp the
   end of inference or resend a cached frame with a new timestamp.
7. Reconnect with a fresh session before sequence wrap. A session may use both
   front and rear observations; all packet types share one strictly increasing
   sequence counter.

The controller accepts only unfragmented 64-byte binary application packets and
ignores WebSocket ping/pong. Invalid packets, duplicate connections, authentication
timeout and >50 binary messages per second disconnect the client. Text and
fragmented application messages are not accepted. The pinned WebSockets library
can allocate up to its 15 KiB ESP32 transport frame limit before application
validation; application length enforcement is not a 64-byte transport allocation
limit. There are at most two library clients and one selected producer.

## Exact 64-byte little-endian binary layout

| Offset | Type | Field |
|---|---|---|
| 0 | 4 bytes | ASCII `RSW1` |
| 4 | uint8 | type: AUTH=1, SYNC=2, HEARTBEAT=3, OBSERVATION=4 |
| 5 | uint8 | source: NONE=0, FRONT=1, REAR=2 |
| 6 | uint8 | flags: usable=1, model-valid=2, geometry-valid=4, capture-clock-known=8 |
| 7 | uint8 | hazards: pothole=1, wet=2, rear-vehicle=4 |
| 8 | uint32 | session (nonzero) |
| 12 | uint32 | sequence (nonzero, no wrap) |
| 16 | uint64 | phone monotonic milliseconds; max 2^52 |
| 24 | uint32 | SYNC request ID; OBSERVATION camera-clock uncertainty in ms |
| 28 | uint8 | zone: unknown=0, left=1, centre=2, right=3 |
| 29 | uint8 | confidence 0–100 |
| 30 | uint16 | reserved, zero |
| 32 | 32 bytes | token for AUTH; all zero for other packets |

Control packets must have source/flags/hazards/zone/confidence zero. AUTH and
HEARTBEAT auxiliary are zero. Front observations allow pothole/wet only, zone
unknown. Rear observations allow rear-vehicle only. Camera uncertainty is 0–10000
ms, though a large value makes a result unusable under the freshness bound.

Python struct: `<4sBBBBIIQIBBH32s`. `tools/phone_sim.py` supplies a matching encoder.
The shared C++/Python golden vector tests prevent accidental wire-layout drift.

## Freshness and confirmation

For a sync sent at controller t0 and answered at t1 with phone timestamp p,
offset = midpoint(t0,t1) − p. Uncertainty is ceil(RTT/2)+1 ms. RTT above 400 ms is
rejected. Sync expires after 30 s; uncertainty grows at 200 ppm between syncs.
Observation uncertainty adds the camera→phone mapping uncertainty. The conservative
upper bound on frame age must be ≤500 ms. Far-future timestamps are rejected;
timestamps slightly ahead within the uncertainty interval do not extend the
validity beyond that bound.

Observations require usable, model-valid and capture-clock-known flags plus
confidence ≥60. Potholes/rear vehicles need two distinct frames within 500 ms;
wet road needs three within 1000 ms. Windows slide so sustained detections remain
confirmed. Confidence thresholds/persistence are prototype settings, not measured
model accuracy. A clear observation clears that hazard's persistence count.
Heartbeat freshness is 3 s and does not refresh camera data. Observation validity
ends at capture time +500 ms minus uncertainty, not reception time +500 ms.

A camera on a separate MCU has a different clock. The future phone application
must estimate that mapping and bound its error, or leave capture-clock-known
unset. Merely receiving a frame now does not establish when it was captured.

## Status / errors

Controller-to-phone JSON includes `type=status`, profile, state, send, warning,
front_valid and rear_valid. `type=rejected` codes map to:
1 session, 2 sequence, 3 clock, 4 offline, 5 future, 6 stale, 7 timestamp,
8 schema. Rejected accepted-format packets consume their sequence number.

`front_valid`/`rear_valid` mean a usable recent observation, not necessarily a
hazard. Serial `status` also shows IMU health, phone/clock health, event drops,
sample gaps, rejected messages and missed recording captures.

## What rear-vehicle means here

Rear-vehicle is a general advisory. Camera image position alone is not a
bike-relative blind spot, especially on a moving helmet. No numeric distance,
closing speed or time-to-collision is carried by this version. Do not label the
simulator as validated blind-spot detection. The future vision pipeline must first
establish camera calibration, orientation and tracking (and a validated ranging
method if distance is required).
