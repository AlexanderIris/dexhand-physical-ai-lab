import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import dexlog_decode as dec  # noqa: E402


class DexLogDecodeTest(unittest.TestCase):
    def test_crc_known_vector(self):
        self.assertEqual(dec.crc16_ccitt_false(b"123456789"), 0x29B1)

    def test_control_payload_roundtrip(self):
        joint = struct.pack("<iiiiHH", 100, 90, 10, 250, 3700, 0)
        payload = struct.pack("<4B I H H I", 1, 1, 0, 1, 7, 1000, 88, 0) + joint
        parsed = dec.parse_control_payload(payload)
        self.assertEqual(parsed["cycle"], 7)
        self.assertEqual(parsed["joint_count"], 1)
        self.assertEqual(parsed["joints"][0]["current_ma"], 250)
        self.assertEqual(parsed["joints"][0]["temperature_cdeg"], 3700)

    def test_frame_file_iterator_and_summary(self):
        control_joint = struct.pack("<iiiiHH", 10, 9, 1, 20, 3650, 0)
        control_payload = struct.pack("<4B I H H I", 1, 1, 0, 1, 0, 1000, 50, 0) + control_joint
        frames = [
            dec.build_frame(0x01, 0, 100, 100, b'{"schema":"test"}'),
            dec.build_frame(0x10, 1, 200, 200, control_payload),
        ]
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "sample.dexlog"
            path.write_bytes(b"".join(frames))
            read = list(dec.read_frames(path))
            self.assertEqual(len(read), 2)
            summary, materialized = dec.summarize(iter(read))
            summary.path = str(path)
            self.assertEqual(summary.control_count, 1)
            self.assertEqual(summary.crc_errors, 0)
            self.assertEqual(materialized[1].decoded["cycle"], 0)


if __name__ == "__main__":
    unittest.main()
