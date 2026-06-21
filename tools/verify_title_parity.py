#!/usr/bin/env python3
"""Verify title-screen frames against a BizHawk teacher capture.

This is intentionally stricter than the exploratory comparison tool: it checks
known frame-to-frame mappings and fails when a supposedly ROM-verified title
frame drifts.

The title path is expected to be ROM-derived/native by default.  Older builds
could import BizHawk screenshots into out/title/intro and mark that folder with
teacher_capture.txt; this verifier still understands that fallback, but it
reports the mode explicitly so a screenshot-backed title cannot masquerade as a
reconstructed renderer.
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
DEFAULT_TEACHER_CAPTURE_INTRO_START = 460
DEFAULT_NATIVE_INTRO_START = 336

WAIT_ON_TEACHER_FRAMES = (1327, 1349, 1365, 1401, 1365, 1349)
WAIT_OFF_TEACHER_FRAMES = (1385, 1329, 1333, 1337, 1333, 1329)
NATIVE_INTRO_SAMPLES = (0, 22, 32, 42, 52, 62, 72, 82, 90, 102, 110, 116, 119, 128)


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
    parser.add_argument(
        "--intro-start",
        type=int,
        default=None,
        help=(
            "first teacher frame for intro checks; defaults to 336 for native "
            "30Hz title frames or 460 for teacher-copied screenshot frames"
        ),
    )
    parser.add_argument(
        "--intro-samples",
        type=int,
        nargs="*",
        default=None,
        help="generated intro frame indices to verify against teacher frames",
    )
    parser.add_argument("--sample-step", type=int, default=1)
    parser.add_argument(
        "--max-error",
        type=float,
        default=0.004,
        help=(
            "maximum normalized RGB error.  The native path stays below this "
            "while still catching frame drift, bad palettes, and screenshot "
            "fallback regressions."
        ),
    )
    parser.add_argument(
        "--require-native",
        action="store_true",
        default=True,
        help="fail if out/title/intro is screenshot-backed instead of ROM-derived native output",
    )
    parser.add_argument(
        "--allow-teacher-capture",
        action="store_false",
        dest="require_native",
        help="allow the old teacher_capture.txt screenshot fallback",
    )
    args = parser.parse_args()

    teacher = args.teacher or latest_teacher(args.teacher_root)
    if not teacher.is_dir():
        raise SystemExit(f"teacher capture does not exist: {teacher}")

    intro_marker = args.title_root / "intro" / "teacher_capture.txt"
    intro_is_teacher_capture = intro_marker.is_file()
    if args.require_native and intro_is_teacher_capture:
        raise SystemExit(
            f"title intro is screenshot-backed ({intro_marker}); run tools/extract_title.py "
            "to regenerate ROM-derived native frames"
        )

    if args.intro_start is None:
        intro_start = (
            DEFAULT_TEACHER_CAPTURE_INTRO_START
            if intro_is_teacher_capture
            else DEFAULT_NATIVE_INTRO_START
        )
    else:
        intro_start = args.intro_start

    if args.intro_samples is None:
        intro_samples = (
            [0, 130, 141, 155, 169, 171, 240, 440, 867, 1339]
            if intro_is_teacher_capture
            else list(NATIVE_INTRO_SAMPLES)
        )
    else:
        intro_samples = args.intro_samples

    checks: list[tuple[Path, Path]] = []
    for generated_index in intro_samples:
        teacher_index = (
            intro_start + generated_index
            if intro_is_teacher_capture
            else intro_start + generated_index * 2
        )
        checks.append(
            (
                teacher / f"frame_{teacher_index:05d}.png",
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
    print(f"Intro mode: {'teacher-capture fallback' if intro_is_teacher_capture else 'native ROM-derived'}")
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
