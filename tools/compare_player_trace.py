#!/usr/bin/env python3
"""Replay or compare BizHawk player traces against native movement output.

This is intentionally small and strict: it exists to stop tuning by feel.
The first mismatch is the next bug to explain with either trace evidence or
the original disassembly.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path


BUTTON_LEFT = 0x04
BUTTON_RIGHT = 0x08
BUTTON_JUMP = 0x10


@dataclass
class Player:
    x_raw: int
    y_raw: int
    ground_speed: int
    velocity_x: int
    velocity_y: int
    grounded: bool = True
    walking_active: bool = False


def sign_extend_16(value: int) -> int:
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def approach(value: int, target: int, amount: int) -> int:
    if value < target:
        return min(value + amount, target)
    return max(value - amount, target)


def update_player(player: Player, buttons: int) -> None:
    """Mirror the current native viewer flat-ground movement model."""

    movement = 0
    if buttons & BUTTON_RIGHT:
        movement += 1
    if buttons & BUTTON_LEFT:
        movement -= 1
    jump_pressed = False

    ground_acceleration = 0x20
    ground_friction = 0x20
    ground_max_speed = 0x800
    air_acceleration = 0x10
    air_max_x_speed = 0x800
    gravity = 0x80
    fall_max_speed = 0xF00
    jump_impulse = 0x900
    jump_release_limit = 0x400

    if player.grounded:
        if movement != 0 and not player.walking_active and player.ground_speed == 0:
            player.walking_active = True
            player.velocity_x = 0
            player.velocity_y = 0
        elif movement > 0:
            player.walking_active = True
            player.ground_speed = min(
                player.ground_speed + ground_acceleration, ground_max_speed
            )
        elif movement < 0:
            player.walking_active = True
            player.ground_speed = max(
                player.ground_speed - ground_acceleration, -ground_max_speed
            )
        else:
            player.ground_speed = approach(player.ground_speed, 0, ground_friction)
            if abs(player.ground_speed) < 0x100:
                player.ground_speed = 0
                player.walking_active = False
        player.velocity_x = player.ground_speed
        player.velocity_y = 0
    else:
        if movement > 0:
            player.velocity_x = min(
                player.velocity_x + air_acceleration, air_max_x_speed
            )
        elif movement < 0:
            player.velocity_x = max(
                player.velocity_x - air_acceleration, -air_max_x_speed
            )

    if jump_pressed and player.grounded:
        player.velocity_y = -jump_impulse
        player.velocity_x = player.ground_speed
        player.grounded = False

    if (
        not player.grounded
        and not (buttons & BUTTON_JUMP)
        and player.velocity_y < -jump_release_limit
    ):
        player.velocity_y = -jump_release_limit

    if not player.grounded:
        player.velocity_y = min(player.velocity_y + gravity, fall_max_speed)

    player.x_raw += player.velocity_x
    player.y_raw += player.velocity_y


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def resolve_trace_path(path: Path) -> Path:
    """Use the timestamped BizHawk trace when the default path is requested."""

    if path.name != "player-runtime-trace.csv":
        return path

    marker = path.with_name("player-runtime-trace-latest.txt")
    if marker.exists():
        candidate = Path(marker.read_text(encoding="utf-8").strip())
        if candidate.exists():
            return candidate

    timestamped = sorted(
        path.parent.glob("player-runtime-trace-*.csv"),
        key=lambda item: item.stat().st_mtime,
        reverse=True,
    )
    if timestamped:
        return timestamped[0]

    return path


def row_value(row: dict[str, str], key: str) -> int:
    raw = row[key].strip()
    if raw.lower().startswith("0x"):
        return int(raw, 16)
    return int(float(raw))


def compare(rows: list[dict[str, str]], cadence: int, phase: int) -> tuple[int, str]:
    first = rows[0]
    player = Player(
        x_raw=row_value(first, "x_raw_16_8"),
        y_raw=row_value(first, "y_raw_16_8"),
        ground_speed=row_value(first, "ground_speed_s8_8"),
        velocity_x=row_value(first, "x_velocity_s8_8"),
        velocity_y=row_value(first, "y_velocity_s8_8"),
        grounded=(row_value(first, "state") != 0x0039AAF7),
    )

    for index, row in enumerate(rows[1:], start=1):
        if (index - phase) % cadence == 0:
            update_player(player, row_value(row, "buttons_current"))

        expected = {
            "x_raw_16_8": row_value(row, "x_raw_16_8"),
            "ground_speed_s8_8": row_value(row, "ground_speed_s8_8"),
            "x_velocity_s8_8": row_value(row, "x_velocity_s8_8"),
            "y_velocity_s8_8": row_value(row, "y_velocity_s8_8"),
        }
        actual = {
            "x_raw_16_8": player.x_raw,
            "ground_speed_s8_8": player.ground_speed,
            "x_velocity_s8_8": player.velocity_x,
            "y_velocity_s8_8": player.velocity_y,
        }
        for key, expected_value in expected.items():
            if actual[key] != expected_value:
                frame = row["frame"]
                return index, (
                    f"frame {frame} row {index}: {key} expected "
                    f"{expected_value}, got {actual[key]} "
                    f"(cadence={cadence}, phase={phase}, "
                    f"buttons={row['buttons_current']})"
                )
    return len(rows), f"matched {len(rows)} rows"


def compare_native_trace(
    rom_rows: list[dict[str, str]],
    native_rows: list[dict[str, str]],
) -> tuple[int, str]:
    keys = [
        "x_raw_16_8",
        "y_raw_16_8",
        "ground_speed_s8_8",
        "x_velocity_s8_8",
        "y_velocity_s8_8",
        "surface_angle",
    ]
    limit = min(len(rom_rows), len(native_rows))
    for index in range(limit):
        rom_row = rom_rows[index]
        native_row = native_rows[index]
        for key in keys:
            if key not in rom_row or key not in native_row:
                continue
            expected = row_value(rom_row, key)
            actual = row_value(native_row, key)
            if expected != actual:
                frame = rom_row.get("frame", str(index))
                return index, (
                    f"frame {frame} row {index}: {key} expected "
                    f"{expected}, got {actual}; "
                    f"rom={{"
                    f"x={rom_row.get('x_raw_16_8')}, "
                    f"y={rom_row.get('y_raw_16_8')}, "
                    f"g={rom_row.get('ground_speed_s8_8')}, "
                    f"vx={rom_row.get('x_velocity_s8_8')}, "
                    f"vy={rom_row.get('y_velocity_s8_8')}, "
                    f"angle={rom_row.get('surface_angle')}"
                    f"}} native={{"
                    f"x={native_row.get('x_raw_16_8')}, "
                    f"y={native_row.get('y_raw_16_8')}, "
                    f"g={native_row.get('ground_speed_s8_8')}, "
                    f"vx={native_row.get('x_velocity_s8_8')}, "
                    f"vy={native_row.get('y_velocity_s8_8')}, "
                    f"angle={native_row.get('surface_angle')}"
                    f"}}"
                )
    if len(rom_rows) != len(native_rows):
        return limit, (
            f"matched {limit} shared rows, length differs: "
            f"rom={len(rom_rows)} native={len(native_rows)}"
        )
    return limit, f"matched {limit} rows"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "trace",
        type=Path,
        nargs="?",
        default=Path("out/player-runtime-trace.csv"),
    )
    parser.add_argument(
        "native_trace",
        type=Path,
        nargs="?",
        help="Optional native trace CSV produced by sonic-pocket-viewer --replay-trace.",
    )
    args = parser.parse_args()

    args.trace = resolve_trace_path(args.trace)
    print(f"ROM trace: {args.trace}")
    rows = load_rows(args.trace)
    if args.native_trace is not None:
        native_rows = load_rows(args.native_trace)
        matched, message = compare_native_trace(rows, native_rows)
        print(f"{matched:5d} rows: {message}")
        return 0 if matched == min(len(rows), len(native_rows)) else 1

    for cadence in (1, 2, 3, 4):
        for phase in range(cadence):
            matched, message = compare(rows, cadence, phase)
            print(f"{matched:5d} rows: {message}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
