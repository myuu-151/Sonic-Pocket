#!/usr/bin/env python3
"""Verify title-screen frames against a BizHawk teacher capture.

This is intentionally stricter than the exploratory comparison tool: it checks
known frame-to-frame mappings and fails when a supposedly ROM-verified title
frame drifts.  The current imported intro uses a direct teacher-frame copy, so
these checks also catch accidental re-extraction/regeneration that stomps the
teacher-backed assets.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

SCRIPT_PATH = Path(__file__.replace("\\", "/")).resolve()
sys.path.insert(0, str(SCRIPT_PATH.parent))

from compare_title_frames import diff_score, read_png


DEFAULT_TEACHER_ROOT = Path("out/title-teacher")
DEFAULT_TITLE_ROOT = Path("out/title")
DEFAULT_INTRO_START = 460

WAIT_ON_TEACHER_FRAMES = (1327, 1349, 1365, 1401, 1365, 1349)
WAIT_OFF_TEACHER_FRAMES = (1385, 1329, 1333, 1337, 1333, 1329)


def latest_teacher(root: Path) -> Path:
    latest = root / "latest.txt"
    if latest.is_file():
        target = Path(latest.read_text(encoding="utf-8").strip())
        if target.is_dir():
            return target
    candidates = sorted((path for path in root.iterdir() if path.is_dir()), key=lambda path: path.stat().st_mtime)
    if not candidates:
        raise SystemExit(f"no BizHawk title captures found in {root}")
    return candidates[-1]


def check_pair(
    teacher_path: Path,
    generated_path: Path,
    *,
    sample_step: int,
    max_error: float,
) -> tuple[bool, str]:
    if not teacher_path.is_file():
        return False, f"missing teacher frame: {teacher_path}"
    if not generated_path.is_file():
        return False, f"missing generated frame: {generated_path}"

    teacher = read_png(teacher_path)
    generated = read_png(generated_path)
    mean_abs, normalized, max_channel = diff_score(teacher, generated, sample_step)
    ok = normalized <= max_error
    return ok, (
        f"{generated_path} <= {teacher_path.name}: "
        f"norm={normalized:.8f} mean={mean_abs:.4f} max={max_channel}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--teacher-root", type=Path, default=DEFAULT_TEACHER_ROOT)
    parser.add_argument("--teacher", type=Path, default=None)
    parser.add_argument("--title-root", type=Path, default=DEFAULT_TITLE_ROOT)
    parser.add_argument("--intro-start", type=int, default=DEFAULT_INTRO_START)
    parser.add_argument(
        "--intro-samples",
        type=int,
        nargs="*",
        default=[0, 130, 141, 155, 169, 171, 240, 440, 867, 1339],
        help="generated intro frame indices to verify against teacher frames",
    )
    parser.add_argument("--sample-step", type=int, default=1)
    parser.add_argument("--max-error", type=float, default=0.0)
    args = parser.parse_args()

    teacher = args.teacher or latest_teacher(args.teacher_root)
    if not teacher.is_dir():
        raise SystemExit(f"teacher capture does not exist: {teacher}")

    checks: list[tuple[Path, Path]] = []
    for generated_index in args.intro_samples:
        checks.append(
            (
                teacher / f"frame_{args.intro_start + generated_index:05d}.png",
                args.title_root / "intro" / f"frame_{generated_index:04d}.png",
            )
        )

    for index, teacher_frame in enumerate(WAIT_ON_TEACHER_FRAMES):
        checks.append(
            (
                teacher / f"frame_{teacher_frame:05d}.png",
                args.title_root / "wait_on" / f"frame_{index:04d}.png",
            )
        )
    for index, teacher_frame in enumerate(WAIT_OFF_TEACHER_FRAMES):
        checks.append(
            (
                teacher / f"frame_{teacher_frame:05d}.png",
                args.title_root / "wait_off" / f"frame_{index:04d}.png",
            )
        )

    failures = 0
    print(f"Teacher: {teacher}")
    print(f"Title root: {args.title_root}")
    for teacher_path, generated_path in checks:
        ok, message = check_pair(
            teacher_path,
            generated_path,
            sample_step=max(1, args.sample_step),
            max_error=args.max_error,
        )
        print(("OK  " if ok else "FAIL ") + message)
        if not ok:
            failures += 1

    if failures:
        print(f"{failures} title parity check(s) failed.")
        return 1
    print(f"All {len(checks)} title parity check(s) passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
