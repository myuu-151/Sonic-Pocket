from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import recomp_seeds  # noqa: E402

# A synthetic Macroassembler AS listing in the same shape as the real one:
# IDA puts an arrow glyph (0x18/0x19) before each xref kind letter, and long
# instructions wrap their bytes onto a continuation line.
LISTING = (
    "(1)  940/  200040 :                     EntryPoint:\t\t\t\t; CODE XREF: Reset+1Ej\n"
    "(1)  941/  200040 : C2 1F 00 20 FF      \t\tCP\t(off_20001C+3), 0FFh\n"
    "(1)  975/  2000A0 :                     VBlankInt:\t\t\t\t; DATA XREF: ROM:00200262\x18o\n"
    "(1)  976/  2000A0 : 3C                  \t\tPUSH\tXIX\n"
    "(1)  977/  2000A1 :                     loc_2000A1:\t\t\t\t; CODE XREF: VBlankInt+8\x19j\n"
    "(1)  978/  2000A1 : E7 3C 03 3E 78 23   \t\tLD\tXHL3, SegaSound\n"
    "                    00 \n"
    "(1)  979/  2000A8 :                     ; =============== S U B R O U T I N E ==========\n"
    "(1)  980/  2000A8 :                     Helper:\n"
    "(1)  981/  2000A8 : 0E                  \t\tRET\n"
    "(1)  982/  2000A9 :                     word_2000A9:\t\t\t\t; DATA XREF: Helper\x18o\n"
    "(1)  983/  2000A9 : 34 12               \t\tDW 1234h\n"
)


class RecompSeedsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.listing = Path(self.directory.name) / "Main.lst"
        self.listing.write_text(LISTING, encoding="latin-1")

    def tearDown(self) -> None:
        self.directory.cleanup()

    def test_instruction_sizes_include_wrapped_bytes(self) -> None:
        _, instructions = recomp_seeds.parse(self.listing)
        self.assertEqual(instructions[0x2000A1][0], 7)
        self.assertEqual(instructions[0x2000A0], (1, "PUSH XIX"))
        self.assertNotIn(0x2000A9, instructions)  # data, not code

    def test_xref_kinds_after_arrow_glyphs(self) -> None:
        labels, _ = recomp_seeds.parse(self.listing)
        self.assertEqual(labels["VBlankInt"], (0x2000A0, "o"))
        self.assertEqual(labels["loc_2000A1"], (0x2000A1, "j"))
        self.assertEqual(labels["Helper"], (0x2000A8, "s"))

    def test_seeds_skip_jump_targets_and_data(self) -> None:
        functions = Path(self.directory.name) / "functions.txt"
        instructions = Path(self.directory.name) / "instructions.txt"
        argv = sys.argv
        sys.argv = ["recomp_seeds.py", str(self.listing), "--functions", str(functions),
                    "--instructions", str(instructions)]
        try:
            recomp_seeds.main()
        finally:
            sys.argv = argv
        seeds = [line.split() for line in functions.read_text().splitlines() if not line.startswith("#")]
        self.assertEqual(seeds, [["200040", "EntryPoint"], ["2000A0", "VBlankInt"], ["2000A8", "Helper"]])


if __name__ == "__main__":
    unittest.main()
