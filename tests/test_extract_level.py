import struct
import tempfile
import unittest
from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import extract_level


class ExtractLevelTests(unittest.TestCase):
    def test_decode_tile_reads_ngpc_two_bit_pixels(self):
        tile = struct.pack("<8H", 0x1B00, 0, 0, 0, 0, 0, 0, 0)
        pixels = extract_level.decode_tile(tile, 0)
        self.assertEqual(pixels[0], (0, 1, 2, 3, 0, 0, 0, 0))

    def test_palette_ids_removes_sentinel(self):
        data = struct.pack("<17H", *range(16), 0xFFFF)
        self.assertEqual(extract_level.palette_ids(data), list(range(16)))

    def test_decode_ngpc_color_converts_rgb_nibbles(self):
        self.assertEqual(extract_level.decode_ngpc_color(0x0A3F), (255, 51, 170))

    def test_build_palettes_uses_three_color_six_byte_lines(self):
        collection = struct.pack("<6H", 0x001, 0x020, 0x300, 0x004, 0x050, 0x600)
        palette = extract_level.build_palettes(collection, [1])[0]
        self.assertEqual(
            palette,
            [(0, 0, 0), (68, 0, 0), (0, 85, 0), (0, 0, 102)],
        )

    def test_composite_rgb_uses_nonzero_foreground_mask(self):
        background = bytes([1, 2, 3, 4, 5, 6])
        foreground = bytes([7, 8, 9, 10, 11, 12])
        self.assertEqual(
            extract_level.composite_rgb(background, foreground, bytes([0, 255])),
            bytes([1, 2, 3, 10, 11, 12]),
        )

    def test_parse_objects_resolves_relative_chunk_pointers(self):
        data = bytearray([1, 1])
        data.extend(struct.pack("<H", 2))
        data.extend([3, 1, 7, 2])
        data.extend(struct.pack("<HH", 0x1234, 0x5678))
        data.extend([9, 8, 7, 6])
        result = extract_level.parse_objects(bytes(data))
        self.assertEqual(result["object_count"], 1)
        self.assertEqual(
            result["objects"][0],
            {
                "record": 0,
                "id": 7,
                "difficulty": 2,
                "x": 0x1234,
                "y": 0x5678,
                "parameters": [9, 8, 7, 6],
                "screen_x": 0,
                "screen_y": 0,
                "slot": 3,
            },
        )

    def test_write_png_emits_valid_dimensions(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.png"
            extract_level.write_png(path, 2, 1, bytes([255, 0, 0, 0, 255, 0]))
            data = path.read_bytes()
        self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(struct.unpack(">II", data[16:24]), (2, 1))

    def test_reference_validation_detects_exact_match(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reference_path = root / "level" / "1-1_TileData.bin"
            reference_path.parent.mkdir()
            reference_path.write_bytes(b"abc")
            spec = extract_level.LevelSpec(
                key="test",
                name="Test",
                width_blocks=1,
                height_blocks=1,
                start_x=0,
                start_y=0,
                rings=0,
                background_palette=0,
                segments={
                    "tiles": extract_level.Segment(
                        "tiles.bin", 0, 3, "level/1-1_TileData.bin"
                    )
                },
            )
            result = extract_level.validate_reference({"tiles": b"abc"}, spec, root)
        self.assertTrue(result["all_match"])


if __name__ == "__main__":
    unittest.main()
