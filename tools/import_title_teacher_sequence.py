#!/usr/bin/env python3
"""Import a measured BizHawk title sequence as the viewer's title intro.

This is a parity fallback, not the final decomp renderer.  It lets the PC
viewer play the exact captured ROM title sequence while the ROM-derived
extractor catches up phase by phase.  The capture is produced from the user's
own ROM by scripts/bizhawk-title-capture.lua.
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


def latest_teacher(root: Path) -> Path:
    latest = root / "latest.txt"
    if latest.is_file():
        target = Path(latest.read_text(encoding="utf-8").strip())
        if target.is_dir():
            return target
    candidates = sorted((p for p in root.iterdir() if p.is_dir()), key=lambda p: p.stat().st_mtime)
    if not candidates:
        raise SystemExit(f"no BizHawk title captures found in {root}")
    return candidates[-1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--teacher-root",
        type=Path,
        default=Path("out/title-teacher"),
        help="directory containing timestamped BizHawk title captures",
    )
    parser.add_argument(
        "--teacher",
        type=Path,
        default=None,
        help="specific BizHawk title capture directory",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("out/title/intro"),
        help="viewer intro frame output directory",
    )
    parser.add_argument(
        "--start",
        type=int,
        default=460,
        help="first captured ROM frame to import",
    )
    parser.add_argument(
        "--end",
        type=int,
        default=-1,
        help="last captured ROM frame to import, inclusive; -1 imports through the final captured frame",
    )
    args = parser.parse_args()

    teacher = args.teacher or latest_teacher(args.teacher_root)
    if not teacher.is_dir():
        raise SystemExit(f"teacher capture does not exist: {teacher}")

    args.output.mkdir(parents=True, exist_ok=True)
    for stale in args.output.glob("frame_*.png"):
        stale.unlink()

    written = 0
    end = args.end
    if end < 0:
        frames = sorted(teacher.glob("frame_*.png"))
        if not frames:
            raise SystemExit(f"no teacher PNGs found in {teacher}")
        end = max(int(frame.stem.split("_")[1]) for frame in frames)

    for frame in range(args.start, end + 1):
        source = teacher / f"frame_{frame:05d}.png"
        if not source.is_file():
            raise SystemExit(f"missing teacher frame: {source}")
        destination = args.output / f"frame_{written:04d}.png"
        shutil.copyfile(source, destination)
        written += 1

    (args.output / "teacher_capture.txt").write_text(
        f"source={teacher}\nstart={args.start}\nend={end}\nframes={written}\n",
        encoding="utf-8",
    )
    print(f"Imported {written} title teacher frame(s) from {teacher} to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
