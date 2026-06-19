from __future__ import annotations

import hashlib
import io
import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import rominfo  # noqa: E402


class RomInfoTests(unittest.TestCase):
    def test_parse_header(self) -> None:
        header = bytearray(rominfo.HEADER_SIZE)
        header[0x00:0x1C] = b"COPYRIGHT BY SNK CORPORATION"
        header[0x1C:0x20] = (0x00200040).to_bytes(4, "little")
        header[0x20:0x22] = (0x0059).to_bytes(2, "little")
        header[0x22] = 0x05
        header[0x23] = 0x10
        header[0x24:0x30] = b"SONIC POCKET"

        self.assertEqual(
            rominfo.parse_header(bytes(header)),
            {
                "copyright": "COPYRIGHT BY SNK CORPORATION",
                "entry_point": "0x00200040",
                "catalog": "0x0059",
                "subcatalog": "0x05",
                "mode": "0x10",
                "title": "SONIC POCKET",
            },
        )

    def test_parse_header_rejects_short_input(self) -> None:
        with self.assertRaises(ValueError):
            rominfo.parse_header(b"short")

    def test_hash_stream(self) -> None:
        payload = bytes(range(rominfo.HEADER_SIZE))
        size, hashes, header = rominfo.hash_stream(io.BytesIO(payload))

        self.assertEqual(size, len(payload))
        self.assertEqual(header, payload)
        self.assertEqual(hashes["sha256"], hashlib.sha256(payload).hexdigest())
        self.assertEqual(hashes["crc32"], "05202171")

    def test_manifest_mismatch_is_reported(self) -> None:
        info = {
            "size": 4,
            "hashes": {"sha1": "aaaa"},
            "header": {"title": "TEST"},
        }
        manifest = {
            "size": 5,
            "hashes": {"sha1": "bbbb"},
            "header": {"title": "TEST"},
        }
        mismatches = rominfo.compare_manifest(info, manifest)

        self.assertEqual(len(mismatches), 2)
        self.assertTrue(mismatches[0].startswith("size:"))
        self.assertTrue(mismatches[1].startswith("hashes.sha1:"))


if __name__ == "__main__":
    unittest.main()
