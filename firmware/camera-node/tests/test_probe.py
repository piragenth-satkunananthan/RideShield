import io
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from probe_cameras import read_frame


def frame(node="FRONT", size=4):
    return (f"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: {size}\r\n"
            f"X-Camera-Node: {node}\r\nX-Frame-Index: 1\r\n"
            "X-Capture-Timestamp-Us: 1234\r\nX-Boot-Id: 00000001\r\n\r\n").encode() + b"\xff\xd8\xff\xd9\r\n"


class ProbeTest(unittest.TestCase):
    def test_consecutive_frames_and_crlf(self):
        stream = io.BytesIO(frame() + frame())
        for _ in range(2):
            self.assertEqual(read_frame(stream, "FRONT")[1:], (1, 1234, 4))

    def test_wrong_camera(self):
        with self.assertRaises(ValueError):
            read_frame(io.BytesIO(frame("REAR")), "FRONT")

    def test_truncated_frame(self):
        with self.assertRaises(ValueError):
            read_frame(io.BytesIO(frame()[:-4]), "FRONT")

    def test_unbounded_length(self):
        with self.assertRaises(ValueError):
            read_frame(io.BytesIO(frame(size=999999999)), "FRONT")

    def test_invalid_jpeg(self):
        with self.assertRaises(ValueError):
            read_frame(io.BytesIO(frame().replace(b"\xff\xd9", b"xx")), "FRONT")


if __name__ == "__main__":
    unittest.main()
