#!/usr/bin/env python3
"""Run the gameplay ROM-vs-native parity gate.

This script is the repeatable workflow for movement/collision work:

1. Use a BizHawk player-runtime trace as the ROM teacher.
2. Run the native viewer in teacher-forced mode, where every logic tick starts
   from ROM state and advances one native step.
3. Run the native viewer in open replay mode to catch accumulated divergence.
4. Emit a compact report and fail only when the checked budgets regress.

The budgets are deliberately explicit.  They document the current known state
of the port while giving future fixes a ratchet: lower the budgets when a bug
is fixed, and CI/local runs will keep it fixed.
"""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


DEFAULT_EXE = Path("build/viewer/Release/sonic-pocket-viewer.exe")
DEFAULT_ROM_TRACE = Path("out/player-runtime-trace.csv")
DEFAULT_TEACHER_TRACE = Path("out/native-teacher-trace.csv")
DEFAULT_REPLAY_TRACE = Path("out/native-replay-trace.csv")
DEFAULT_REPORT = Path("out/gameplay-parity-report.json")

NUMERIC_DELTA_FIELDS = (
    "dx_raw",
    "dy_raw",
    "dground_speed",
    "dx_velocity",
    "dy_velocity",
)

COMPARE_FIELDS = (
    "x_raw_16_8",
    "y_raw_16_8",
    "ground_speed_s8_8",
    "x_velocity_s8_8",
    "y_velocity_s8_8",
    "surface_angle",
)


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def parse_int(value: str) -> int:
    return int(value, 0)


def resolve_rom_trace(path: Path) -> Path:
    if path.name == "player-runtime-trace.csv":
        marker = path.with_name("player-runtime-trace-latest.txt")
        if marker.is_file():
            candidate = Path(marker.read_text(encoding="utf-8").strip())
            if candidate.is_file():
                return candidate
    return path


def run(command: list[str]) -> None:
    print("+ " + " ".join(command))
    subprocess.run(command, check=True)


@dataclass(frozen=True)
class TeacherSummary:
    samples: int
    numeric_bad: dict[str, int]
    surface_angle_bad: int
    grounded_bad: int
    first_bad_rows: list[dict[str, str]]

    @property
    def total_numeric_bad(self) -> int:
        return sum(self.numeric_bad.values())


def summarize_teacher(path: Path, first_bad_limit: int) -> TeacherSummary:
    rows = load_rows(path)
    numeric_bad = {
        field: sum(1 for row in rows if parse_int(row[field]) != 0)
        for field in NUMERIC_DELTA_FIELDS
    }
    surface_angle_bad = sum(
        1
        for row in rows
        if row["expected_surface_angle"].lower() != row["actual_surface_angle"].lower()
    )
    grounded_bad = sum(
        1 for row in rows if row["expected_grounded"] != row["actual_grounded"]
    )
    first_bad_rows: list[dict[str, str]] = []
    for row in rows:
        bad = any(parse_int(row[field]) != 0 for field in NUMERIC_DELTA_FIELDS)
        bad = bad or row["expected_surface_angle"].lower() != row["actual_surface_angle"].lower()
        bad = bad or row["expected_grounded"] != row["actual_grounded"]
        if bad:
            first_bad_rows.append(row)
            if len(first_bad_rows) >= first_bad_limit:
                break
    return TeacherSummary(
        samples=len(rows),
        numeric_bad=numeric_bad,
        surface_angle_bad=surface_angle_bad,
        grounded_bad=grounded_bad,
        first_bad_rows=first_bad_rows,
    )


def row_value(row: dict[str, str], key: str) -> int:
    value = row[key].strip()
    if value.lower().startswith("0x"):
        return int(value, 16)
    return int(float(value))


def first_replay_mismatch(
    rom_trace: Path,
    native_trace: Path,
) -> dict[str, object]:
    rom_rows = load_rows(rom_trace)
    native_rows = load_rows(native_trace)
    limit = min(len(rom_rows), len(native_rows))
    for index in range(limit):
        rom = rom_rows[index]
        native = native_rows[index]
        for field in COMPARE_FIELDS:
            if field not in rom or field not in native:
                continue
            expected = row_value(rom, field)
            actual = row_value(native, field)
            if expected != actual:
                return {
                    "matched_rows": index,
                    "field": field,
                    "frame": row_value(rom, "frame") if "frame" in rom else index,
                    "expected": expected,
                    "actual": actual,
                    "rom": {key: rom.get(key, "") for key in COMPARE_FIELDS},
                    "native": {key: native.get(key, "") for key in COMPARE_FIELDS},
                }
    return {
        "matched_rows": limit,
        "field": "",
        "frame": "",
        "expected": "",
        "actual": "",
        "rom_length": len(rom_rows),
        "native_length": len(native_rows),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    parser.add_argument("--rom-trace", type=Path, default=DEFAULT_ROM_TRACE)
    parser.add_argument("--teacher-out", type=Path, default=DEFAULT_TEACHER_TRACE)
    parser.add_argument("--replay-out", type=Path, default=DEFAULT_REPLAY_TRACE)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--first-bad-limit", type=int, default=8)
    parser.add_argument(
        "--max-teacher-numeric-bad",
        type=int,
        default=0,
        help="fail if any teacher-forced numeric movement field diverges",
    )
    parser.add_argument("--max-teacher-y-bad", type=int, default=0)
    parser.add_argument("--max-teacher-angle-bad", type=int, default=0)
    parser.add_argument("--max-teacher-grounded-bad", type=int, default=0)
    parser.add_argument(
        "--min-replay-matched-rows",
        type=int,
        default=1036,
        help="fail if open replay does not match the full current ROM trace",
    )
    args = parser.parse_args()

    rom_trace = resolve_rom_trace(args.rom_trace)
    if not args.exe.is_file():
        raise SystemExit(f"viewer exe not found: {args.exe}")
    if not rom_trace.is_file():
        raise SystemExit(f"ROM player trace not found: {rom_trace}")

    args.teacher_out.parent.mkdir(parents=True, exist_ok=True)
    run([str(args.exe), "--teacher-trace", str(rom_trace), "--teacher-out", str(args.teacher_out)])
    run([str(args.exe), "--replay-trace", str(rom_trace), "--trace-out", str(args.replay_out)])

    teacher = summarize_teacher(args.teacher_out, args.first_bad_limit)
    replay = first_replay_mismatch(rom_trace, args.replay_out)

    report = {
        "rom_trace": str(rom_trace),
        "teacher_trace": str(args.teacher_out),
        "replay_trace": str(args.replay_out),
        "teacher": {
            "samples": teacher.samples,
            "numeric_bad": teacher.numeric_bad,
            "total_numeric_bad": teacher.total_numeric_bad,
            "surface_angle_bad": teacher.surface_angle_bad,
            "grounded_bad": teacher.grounded_bad,
            "first_bad_rows": teacher.first_bad_rows,
        },
        "replay": replay,
        "budgets": {
            "max_teacher_numeric_bad": args.max_teacher_numeric_bad,
            "max_teacher_y_bad": args.max_teacher_y_bad,
            "max_teacher_angle_bad": args.max_teacher_angle_bad,
            "max_teacher_grounded_bad": args.max_teacher_grounded_bad,
            "min_replay_matched_rows": args.min_replay_matched_rows,
        },
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2), encoding="utf-8")

    print()
    print(f"ROM trace: {rom_trace}")
    print(f"Teacher samples: {teacher.samples}")
    print(f"Teacher numeric bad: {teacher.numeric_bad} total={teacher.total_numeric_bad}")
    print(
        "Teacher angle/grounded bad: "
        f"{teacher.surface_angle_bad}/{teacher.grounded_bad}"
    )
    print(
        "Replay matched rows: "
        f"{replay['matched_rows']} first_field={replay.get('field')}"
    )
    if teacher.first_bad_rows:
        first = teacher.first_bad_rows[0]
        print(
            "First teacher mismatch: "
            f"row={first.get('row')} frame={first.get('frame')} "
            f"dx={first.get('dx_raw')} dy={first.get('dy_raw')} "
            f"dg={first.get('dground_speed')} "
            f"dvx={first.get('dx_velocity')} dvy={first.get('dy_velocity')}"
        )
    print(f"Report: {args.report}")

    failures: list[str] = []
    if teacher.total_numeric_bad > args.max_teacher_numeric_bad:
        failures.append(
            f"teacher numeric bad {teacher.total_numeric_bad} > {args.max_teacher_numeric_bad}"
        )
    if teacher.numeric_bad["dy_raw"] > args.max_teacher_y_bad:
        failures.append(
            f"teacher y bad {teacher.numeric_bad['dy_raw']} > {args.max_teacher_y_bad}"
        )
    if teacher.surface_angle_bad > args.max_teacher_angle_bad:
        failures.append(
            f"teacher angle bad {teacher.surface_angle_bad} > {args.max_teacher_angle_bad}"
        )
    if teacher.grounded_bad > args.max_teacher_grounded_bad:
        failures.append(
            f"teacher grounded bad {teacher.grounded_bad} > {args.max_teacher_grounded_bad}"
        )
    if int(replay["matched_rows"]) < args.min_replay_matched_rows:
        failures.append(
            f"replay matched {replay['matched_rows']} < {args.min_replay_matched_rows}"
        )

    if failures:
        for failure in failures:
            print("FAIL " + failure)
        return 1
    print("Gameplay parity gate passed within current budgets.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
