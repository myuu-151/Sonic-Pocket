#!/usr/bin/env python3
"""Derive floor-correction targets from teacher-forced trace output.

The viewer reports the native one-step result for every ROM trace row.  For
floor Y mismatches, this script converts the final ROM-space error back into
the ground-correction delta that the collision routine should have applied.

It does not patch code.  It creates a small evidence table for porting
sub_39BC22/BGCollChk4 without guessing in the live viewer.
"""

from __future__ import annotations

import argparse
import csv
from collections import Counter, defaultdict
from pathlib import Path


FIXED_ONE = 0x100


def as_int(value: str) -> int:
    return int(value, 0)


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def effective_ground_delta(row: dict[str, str]) -> int | None:
    walk_delta = as_int(row["walk_delta_y"])
    if walk_delta % FIXED_ONE != 0:
        return None
    return -walk_delta // FIXED_ONE


def desired_ground_delta(row: dict[str, str]) -> int | None:
    current = effective_ground_delta(row)
    if current is None:
        return None
    dy_raw = as_int(row["dy_raw"])
    if dy_raw % FIXED_ONE != 0:
        return None
    # actual_rom_y = expected_rom_y + dy_raw.
    # view_y is inverted, so the native final view position must move by
    # +dy_raw.  Since view_y -= ground_delta * 0x100, the desired delta is:
    return current - (dy_raw // FIXED_ONE)


def signature(row: dict[str, str]) -> tuple[str, ...]:
    return (
        row["expected_surface_angle"].lower(),
        row["actual_surface_angle"].lower(),
        row["movement"],
        row["floor_hit_delta_y"],
        row["floor_hit_response"],
        row["floor_hit_local_x"],
        row["floor_hit_collision_type"],
        row["expected_y_velocity_s8_8"],
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build floor correction target table from teacher trace."
    )
    parser.add_argument(
        "trace",
        nargs="?",
        type=Path,
        default=Path("out/native-teacher-trace.csv"),
    )
    parser.add_argument("--limit", type=int, default=50)
    args = parser.parse_args()

    rows = load_rows(args.trace)
    mismatches = [row for row in rows if as_int(row["dy_raw"]) != 0]

    print(f"teacher trace: {args.trace}")
    print(f"floor y mismatches: {len(mismatches)}")
    print()
    print(
        "row,frame,angle,movement,vy,floor_delta,floor_response,"
        "floor_local_x,type,current_delta,desired_delta,dy_pixels"
    )

    rule_counter: Counter[tuple[str, ...]] = Counter()
    rule_examples: defaultdict[tuple[str, ...], list[dict[str, str]]] = defaultdict(list)

    for row in mismatches[: args.limit]:
        current = effective_ground_delta(row)
        desired = desired_ground_delta(row)
        dy_pixels = as_int(row["dy_raw"]) // FIXED_ONE
        print(
            f"{row['row']},{row['frame']},"
            f"{row['expected_surface_angle'].lower()},"
            f"{row['movement']},"
            f"{row['expected_y_velocity_s8_8']},"
            f"{row['floor_hit_delta_y']},"
            f"{row['floor_hit_response']},"
            f"{row['floor_hit_local_x']},"
            f"{row['floor_hit_collision_type']},"
            f"{current},{desired},{dy_pixels}"
        )
        key = (
            row["expected_surface_angle"].lower(),
            row["movement"],
            row["floor_hit_delta_y"],
            row["floor_hit_response"],
            row["floor_hit_collision_type"],
            str(desired),
        )
        rule_counter[key] += 1
        rule_examples[key].append(row)

    print()
    print("candidate rule clusters:")
    for key, count in rule_counter.most_common(args.limit):
        first = rule_examples[key][0]
        print(
            f"count={count} key={key} first=row {first['row']} frame {first['frame']} "
            f"local_x={first['floor_hit_local_x']} vy={first['expected_y_velocity_s8_8']}"
        )

    print()
    print("next action:")
    print(
        "- Port/check BGCollChk4 until current_delta equals desired_delta for "
        "these rows without special-casing level coordinates."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
