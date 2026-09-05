import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from inspect_mission_sdk import Pack, SECTIONS, inspect_shard


def empty_pack():
    data = bytearray(848)
    data[:8] = b"SRSDKP01"
    struct.pack_into("<IIQ", data, 8, 37, 848, len(data))
    struct.pack_into("<I", data, 152, 43)
    data[24:56] = hashlib.sha256(b"").digest()
    for index, stride in SECTIONS.values():
        struct.pack_into("<QII", data, 160 + index * 16, 848, 0, stride)
    return data


class InspectionTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "fixture.pack"

    def open(self, data):
        self.path.write_bytes(data)
        return Pack(self.path)

    def test_absent_scenario_does_not_match_another(self):
        pack = self.open(empty_pack())
        try:
            with self.assertRaisesRegex(ValueError, "resolve exactly once"):
                pack.inspect(0x80800011)
        finally:
            pack.close()

    def test_version_and_truncation(self):
        data = empty_pack()
        struct.pack_into("<I", data, 8, 36)
        with self.assertRaisesRegex(ValueError, "unsupported"):
            self.open(data)
        with self.assertRaises(ValueError):
            self.open(empty_pack()[:-1])

    def test_payload_corruption(self):
        data = empty_pack() + b"payload"
        struct.pack_into("<Q", data, 16, len(data))
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.open(data)

    def test_section_outside_file(self):
        data = empty_pack()
        struct.pack_into("<QII", data, 160 + 2 * 16, 848, 1, 48)
        with self.assertRaisesRegex(ValueError, "bounds"):
            self.open(data)

    def test_wrong_row_stride(self):
        data = empty_pack()
        struct.pack_into("<I", data, 160 + 2 * 16 + 12, 44)
        with self.assertRaisesRegex(ValueError, "stride"):
            self.open(data)

    def test_shard_identity_and_payload(self):
        data = bytearray(664)
        data[:8] = b"SRGWSHRD"
        struct.pack_into("<IIQII", data, 8, 13, 664, len(data), 0x80800011, 35)
        data[72:104] = hashlib.sha256(b"").digest()
        self.path.write_bytes(data)
        result = inspect_shard(self.path, 0x80800011)
        self.assertEqual(sum(result["authored_squad_context_counts"].values()), 0)
        with self.assertRaisesRegex(ValueError, "mismatched"):
            inspect_shard(self.path, 0x80800012)
        data[72] ^= 1
        self.path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "checksum"):
            inspect_shard(self.path, 0x80800011)


if __name__ == "__main__":
    unittest.main()
