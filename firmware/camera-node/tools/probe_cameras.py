#!/usr/bin/env python3
"""Run on a laptop joined to the controller AP. No third-party dependencies."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import http.client
import json
import time


def read_exact(stream, length):
    data = bytearray()
    while len(data) < length:
        part = stream.read(length - len(data))
        if not part:
            raise ValueError("Truncated JPEG")
        data.extend(part)
    return bytes(data)


def read_frame(stream, expected_node):
    line = stream.readline(128)
    # The CRLF after the previous JPEG belongs to the multipart delimiter.
    if line == b"\r\n":
        line = stream.readline(128)
    if line != b"--frame\r\n":
        raise ValueError("Missing multipart boundary")
    headers = {}
    total = 0
    while True:
        line = stream.readline(512)
        total += len(line)
        if not line or total > 4096 or not line.endswith(b"\r\n"):
            raise ValueError("Malformed multipart headers")
        if line == b"\r\n":
            break
        key, value = line.decode("ascii").split(":", 1)
        key = key.lower()
        if key in headers:
            raise ValueError("Duplicate multipart header")
        headers[key] = value.strip()
    if headers.get("content-type") != "image/jpeg" or headers.get("x-camera-node") != expected_node:
        raise ValueError("Wrong format or camera role")
    length = int(headers["content-length"])
    if not 4 <= length <= 262144:
        raise ValueError("Invalid JPEG length")
    jpeg = read_exact(stream, length)
    if not jpeg.startswith(b"\xff\xd8") or not jpeg.endswith(b"\xff\xd9"):
        raise ValueError("Invalid JPEG markers")
    index = int(headers["x-frame-index"])
    timestamp = int(headers["x-capture-timestamp-us"])
    if index < 1 or timestamp < 1:
        raise ValueError("Invalid timestamp/index")
    return headers, index, timestamp, len(jpeg)


def get_json(host, path):
    conn = http.client.HTTPConnection(host, 8080, timeout=3)
    try:
        start = time.monotonic_ns() / 1000
        conn.request("GET", path)
        response = conn.getresponse()
        if response.status != 200:
            raise ValueError(f"{path}: HTTP {response.status}")
        body = response.read(4097)
        end = time.monotonic_ns() / 1000
        if len(body) > 4096:
            raise ValueError("Oversized JSON")
        return json.loads(body), start, end
    finally:
        conn.close()


def probe(host, role, frames):
    state, _, _ = get_json(host, "/status")
    if state.get("node") != role or state.get("camera_ready") is not True:
        raise ValueError(f"{role}: camera not ready or wrong role: {state}")
    conn = http.client.HTTPConnection(host, 8080, timeout=3)
    try:
        conn.request("GET", "/stream")
        response = conn.getresponse()
        if response.status != 200:
            raise ValueError(f"{role}: stream HTTP {response.status}; close browser/other viewer")
        if "multipart/x-mixed-replace" not in response.getheader("Content-Type", ""):
            raise ValueError("Stream is not MJPEG")
        # Must answer a second socket while the long-lived stream is open.
        sync, t0, t1 = get_json(host, "/sync")
        if sync.get("node") != role or sync.get("boot_id") != state["boot_id"]:
            raise ValueError("Node changed/rebooted during probe")
        if t1 - t0 > 300000:
            raise ValueError("Clock-sync RTT exceeds 300 ms")
        offset = (t0 + t1) / 2 - sync["camera_us"]
        previous_index = previous_timestamp = 0
        ages = []
        start = time.monotonic()
        for _ in range(frames):
            h, index, stamp, _ = read_frame(response, role)
            if index <= previous_index or stamp <= previous_timestamp or h["x-boot-id"] != sync["boot_id"]:
                raise ValueError("Non-monotonic frame metadata or camera reboot")
            previous_index, previous_timestamp = index, stamp
            now = time.monotonic_ns() / 1000
            age_ms = (now - (stamp + offset)) / 1000
            ages.append(age_ms)
        elapsed = time.monotonic() - start
        return {"node": role, "frames": frames, "received_fps": round(frames / elapsed, 2),
                "sync_rtt_ms": round((t1-t0)/1000, 1), "max_estimated_age_ms": round(max(ages), 1),
                "metadata": "PASS", "sync_during_stream": "PASS",
                "note": "Age estimate excludes exposure uncertainty; measure before enabling perception."}
    finally:
        conn.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--front", default="192.168.4.2")
    parser.add_argument("--rear", default="192.168.4.3")
    parser.add_argument("--frames", type=int, default=30)
    args = parser.parse_args()
    if not 2 <= args.frames <= 10000:
        parser.error("--frames must be 2..10000")
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures = [pool.submit(probe, args.front, "FRONT", args.frames),
                   pool.submit(probe, args.rear, "REAR", args.frames)]
        for future in futures:
            print(json.dumps(future.result(), indent=2))


if __name__ == "__main__":
    main()
