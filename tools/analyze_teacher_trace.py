#!/usr/bin/env python3
"""Summarize teacher-forced runtime mismatches.

Teacher forcing resets native state from each ROM trace row, runs exactly one
native logic step, then compares against the next ROM row.  That prevents one
early divergence from contaminating the rest of the trace and turns the port
work into clusters of independent routine errors.
"""

from __future__ import annotations

import argparse
import csv
from collections import Counter, defaultdict
from pathlib import Path


NUMERIC_FIELDS = [
    "dx_raw",
    "dy_raw",
    "dground_speed",
    "dx_velocity",
    "dy_velocity",
]


def as_int(value: str) -> int:
    return int(value, 0)


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def print_field_summary(rows: list[dict[str, str]]) -> None:
    print(f"samples: {len(rows)}")
    for field in NUMERIC_FIELDS:
        counts = Counter(as_int(row[field]) for row in rows)
        bad = sum(count for value, count in counts.items() if value != 0)
        top = ", ".join(f"{value}:{count}" for value, count in counts.most_common(8))
        print(f"{field}: bad={bad} top=[{top}]")

    angle_bad = sum(
        1
        for row in rows
        if row["expected_surface_angle"].lower() != row["actual_surface_angle"].lower()
    )
    grounded_bad = sum(
        1 for row in rows if row["expected_grounded"] != row["actual_grounded"]
    )
    print(f"surface_angle: bad={angle_bad}")
    print(f"grounded: bad={grounded_bad}")


def y_mismatch_signature(row: dict[str, str]) -> tuple[str, ...]:
    return (
        row["expected_surface_angle"].lower(),
        row["actual_surface_angle"].lower(),
        row["movement"],
        row["floor_hit_delta_y"],
        row["floor_hit_response"],
        row["floor_hit_local_x"],
        row["floor_hit_local_y"],
        row["floor_hit_collision_type"],
        row["dy_raw"],
    )


def print_y_clusters(rows: list[dict[str, str]], limit: int) -> None:
    mismatches = [row for row in rows if as_int(row["dy_raw"]) != 0]
    clusters: defaultdict[tuple[str, ...], list[dict[str, str]]] = defaultdict(list)
    for row in mismatches:
        clusters[y_mismatch_signature(row)].append(row)

    print()
    print(f"y mismatch rows: {len(mismatches)}")
    print("top y mismatch clusters:")
    for signature, members in sorted(
        clusters.items(), key=lambda item: len(item[1]), reverse=True
    )[:limit]:
        first = members[0]
        print(
            f"count={len(members)} sig={signature} "
            f"first_row={first['row']} first_frame={first['frame']}"
        )

    print()
    print("first y mismatches:")
    for row in mismatches[:limit]:
        print(
            "row={row} frame={frame} dy={dy_raw} "
            "angle={expected_surface_angle}->{actual_surface_angle} "
            "move={movement} gs={expected_ground_speed_s8_8}->{actual_ground_speed_s8_8} "
            "vy={expected_y_velocity_s8_8}->{actual_y_velocity_s8_8} "
            "walk_dy={walk_delta_y} floor=(dy:{floor_hit_delta_y},"
            "resp:{floor_hit_response},x:{floor_hit_local_x},"
            "y:{floor_hit_local_y},type:{floor_hit_collision_type})".format(**row)
        )


def print_next_actions(rows: list[dict[str, str]]) -> None:
    y_bad = [row for row in rows if as_int(row["dy_raw"]) != 0]
    x_bad = [row for row in rows if as_int(row["dx_raw"]) != 0]
    speed_bad = [row for row in rows if as_int(row["dground_speed"]) != 0]
    velocity_bad = [
        row
        for row in rows
        if as_int(row["dx_velocity"]) != 0 or as_int(row["dy_velocity"]) != 0
    ]

    print()
    print("repair priority:")
    if y_bad:
        print(
            "- Floor Y correction is the active target. Port/check "
            "sub_39BC22 + BGCollChk4 before touching camera/sprites."
        )
    if x_bad or speed_bad or velocity_bad:
        print(
            "- Movement integration has isolated failures too, but fewer than "
            "Y correction; fix after floor correction."
        )
    if not any((y_bad, x_bad, speed_bad, velocity_bad)):
        print("- Teacher-forced trace is clean for numeric movement fields.")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Analyze native teacher-forced trace mismatch clusters."
    )
    parser.add_argument(
        "trace",
        nargs="?",
        type=Path,
        default=Path("out/native-teacher-trace.csv"),
    )
    parser.add_argument("--limit", type=int, default=20)
    args = parser.parse_args()

    rows = load_rows(args.trace)
    print_field_summary(rows)
    print_y_clusters(rows, args.limit)
    print_next_actions(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
