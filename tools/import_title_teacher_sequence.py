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


WAIT_ON_TEACHER_FRAMES = (1327, 1349, 1365, 1401, 1365, 1349)
WAIT_OFF_TEACHER_FRAMES = (1385, 1329, 1333, 1337, 1333, 1329)
HANDOFF_TEACHER_FRAMES = tuple(range(593, 629))
STATE_TEACHER_FRAMES = {
    "frame_0000_press_off.png": 1385,
    "frame_0001_press_a.png": 1327,
}


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
    parser.add_argument(
        "--no-wait",
        action="store_true",
        help="only import intro frames; leave generated wait/title-state frames untouched",
    )
    parser.add_argument(
        "--only-handoff",
        action="store_true",
        help="only import the ROM-verified intro-to-title handoff frames",
    )
    args = parser.parse_args()

    teacher = args.teacher or latest_teacher(args.teacher_root)
    if not teacher.is_dir():
        raise SystemExit(f"teacher capture does not exist: {teacher}")

    written = 0
    if not args.only_handoff:
        args.output.mkdir(parents=True, exist_ok=True)
        for stale in args.output.glob("frame_*.png"):
            stale.unlink()

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

    if not args.no_wait or args.only_handoff:
        title_root = args.output.parent
        if not args.only_handoff:
            wait_imports = {
                title_root / "wait_on": WAIT_ON_TEACHER_FRAMES,
                title_root / "wait_off": WAIT_OFF_TEACHER_FRAMES,
            }
            for directory, frame_numbers in wait_imports.items():
                directory.mkdir(parents=True, exist_ok=True)
                for stale in directory.glob("frame_*.png"):
                    stale.unlink()
                for index, frame in enumerate(frame_numbers):
                    source = teacher / f"frame_{frame:05d}.png"
                    if not source.is_file():
                        raise SystemExit(f"missing teacher wait frame: {source}")
                    shutil.copyfile(source, directory / f"frame_{index:04d}.png")
                (directory / "teacher_capture.txt").write_text(
                    f"source={teacher}\nframes={','.join(str(frame) for frame in frame_numbers)}\n",
                    encoding="utf-8",
                )

        handoff = title_root / "handoff"
        handoff.mkdir(parents=True, exist_ok=True)
        for stale in handoff.glob("frame_*.png"):
            stale.unlink()
        for index, frame in enumerate(HANDOFF_TEACHER_FRAMES):
            source = teacher / f"frame_{frame:05d}.png"
            if not source.is_file():
                raise SystemExit(f"missing teacher handoff frame: {source}")
            shutil.copyfile(source, handoff / f"frame_{index:04d}.png")
        (handoff / "teacher_capture.txt").write_text(
            f"source={teacher}\nframes={','.join(str(frame) for frame in HANDOFF_TEACHER_FRAMES)}\n",
            encoding="utf-8",
        )

        if not args.only_handoff:
            states = title_root / "states"
            states.mkdir(parents=True, exist_ok=True)
            for name, frame in STATE_TEACHER_FRAMES.items():
                source = teacher / f"frame_{frame:05d}.png"
                if not source.is_file():
                    raise SystemExit(f"missing teacher state frame: {source}")
                shutil.copyfile(source, states / name)

    if not args.only_handoff:
        print(f"Imported {written} title teacher frame(s) from {teacher} to {args.output}")
    if not args.no_wait or args.only_handoff:
        print("Imported ROM-verified title handoff/wait frames to out/title")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
