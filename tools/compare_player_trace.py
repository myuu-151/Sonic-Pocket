#!/usr/bin/env python3
"""Replay BizHawk player traces against the native movement model.

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


def row_value(row: dict[str, str], key: str) -> int:
    if key == "buttons_current" or key == "state":
        return int(row[key], 16)
    return int(row[key])


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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "trace",
        type=Path,
        nargs="?",
        default=Path("out/player-runtime-trace.csv"),
    )
    args = parser.parse_args()

    rows = load_rows(args.trace)
    for cadence in (1, 2, 3, 4):
        for phase in range(cadence):
            matched, message = compare(rows, cadence, phase)
            print(f"{matched:5d} rows: {message}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
