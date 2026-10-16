# Camera stream protocol — implemented firmware

Transport: HTTP/1.1 on port 8080, multipart MJPEG at `GET /stream`.
No WebSocket/UDP video transport is implemented. Response MIME:
`multipart/x-mixed-replace; boundary=frame`. The response is connection-delimited,
not HTTP chunked. Each JPEG part has its own exact byte length:

```http
--frame
Content-Type: image/jpeg
Content-Length: 12345
X-Camera-Node: FRONT
X-Boot-Id: 1234abcd
X-Frame-Index: 1
X-Capture-Timestamp-Us: 12345678
X-Timestamp-Source: driver-first-dma

<12345 JPEG bytes>
```

All line endings are CRLF. After the JPEG there is one CRLF, then the next
`--frame` boundary. Read exactly Content-Length bytes, consume the delimiter
and parse each part's headers. A closed connection before the declared JPEG
length is a dropped frame: discard it and reconnect. Do not fabricate missing
timestamps, pad truncated JPEGs or reuse a prior frame's metadata.

- Node is `FRONT` or `REAR`; addresses are `.2`/`.3` respectively.
- Boot ID is an opaque eight-digit hexadecimal marker, not authentication.
- Frame index is a 64-bit increasing sequence of **acquired driver buffers**,
  continuous across stream reconnections within a boot. It resets on reboot.
  Gaps include firmware freshness drops; driver-internal discarded frames are
  not counted, so this is not a complete sensor-frame loss metric.
- Timestamp comes directly from `camera_fb_t.timestamp`: microseconds since
  boot at the **first DMA buffer of the frame**. It is not socket-send time,
  exposure start/end time or a shared wall clock. It must be mapped to the
  consumer's monotonic clock before observation freshness checks.
- Actual exposure duration and physical sensor gain are unavailable through
  this implementation. `X-Exposure-Us` and `X-Sensor-Gain` are omitted, not
  invented. Consumers must treat them as unknown.
- Configuration uses OV2640's JPEG-quality parameter (lower number means
  higher quality/larger frames), not a 0–100 quality percentage. Default: 16.

`GET /sync` stays available while video streams:

```json
{"node":"FRONT","boot_id":"1234abcd","camera_us":12345678,"uptime_ms":12345}
```

Record consumer monotonic times immediately before request and after receipt.
The request midpoint minus `camera_us` estimates the clock offset; RTT/2 is a
provisional timing uncertainty, not an exposure or network asymmetry guarantee.
Refresh periodically; reject high RTT and reset on reboot/reconnection as
appropriate. This firmware does not call NTP or set wall-clock time.

`GET /status` reports initialization state (`camera_ready`, `camera_error`),
current stream ownership, configured geometry/quality/FPS target, RSSI, uptime,
free heap/PSRAM and counters. `delivered` counts frames accepted into the socket,
not acknowledged/decoded by the consumer. Capture errors and send timeouts are
separate counters. Counters other than frame index are unsigned 32-bit.

Only one stream consumer is allowed per node. HTTP 409 means already occupied;
503 means unavailable hardware/worker. `/sync` and `/status` remain separate
short requests. Request headers are limited to 1023 bytes and 300 ms; do not
send browser extensions' large cookies/headers to these endpoints.

The pre-send frame-age limit and send budget bound firmware waiting; they do
not guarantee end-to-end image age, delivery, detection accuracy or real-time
safety performance. Exposure, radio contention and consumer buffers remain
measurement and integration work.
