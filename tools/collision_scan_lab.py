#!/usr/bin/env python3
"""Compare BGCollChk4 scan variants against teacher-forced mismatch rows.

This is an evidence tool, not gameplay code.  It feeds the exact floor-probe
inputs recorded by teacher trace into small ROM-coordinate collision scanners.
The goal is to find which low-level scan semantics explain the remaining
floor-Y mismatches before replacing live viewer code.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path


STAGE_HEIGHT = 992
FIXED_ONE = 0x100
COLLISION_DEF_SIZE = 130
LEVEL_BLOCKS_WIDE = 200
STAGE_WIDTH = 6400


def s8(value: int) -> int:
    value &= 0xFF
    return value - 0x100 if value & 0x80 else value


def u16le(data: bytes, offset: int) -> int:
    if offset + 1 >= len(data):
        return 0
    return data[offset] | (data[offset + 1] << 8)


def as_int(value: str) -> int:
    return int(value, 0)


@dataclass(frozen=True)
class Sample:
    response: int
    angle: int
    collision_type: int
    local_x: int
    local_y: int


@dataclass(frozen=True)
class Hit:
    hit: bool = False
    delta_y: int = 0
    angle: int = 0
    response: int = 0
    collision_type: int = 0
    local_x: int = 0
    local_y: int = 0


class CollisionData:
    def __init__(self, data_dir: Path) -> None:
        self.plane2 = (data_dir / "data" / "plane2.bin").read_bytes()
        self.blocks = (data_dir / "data" / "blocks.bin").read_bytes()
        self.collision = (data_dir / "data" / "collision.bin").read_bytes()
        self.defs = (data_dir / "collision-defs.bin").read_bytes()

    def vertical_response(self, x: int, rom_y: int, *, rom_x_column: bool) -> Sample | None:
        view_y = STAGE_HEIGHT - 1 - rom_y
        if x < 0 or x >= STAGE_WIDTH or view_y < 0 or view_y >= STAGE_HEIGHT:
            return None

        block_x = x // 32
        block_y = view_y // 32
        block_id = u16le(self.plane2, (block_y * LEVEL_BLOCKS_WIDE + block_x) * 2)

        tile_in_block_x = (x & 31) // 8
        tile_in_block_y = (view_y & 31) // 8
        tile_position = tile_in_block_y * 4 + tile_in_block_x
        tile_entry = u16le(self.blocks, (block_id * 16 + tile_position) * 2)
        tile_id = tile_entry & 0x01FF
        collision_type = u16le(self.collision, tile_id * 2)
        if collision_type == 0xFFFF:
            collision_type = 0

        flip_x = (tile_entry & 0x8000) != 0
        def_offset = (collision_type * 2 + int(flip_x)) * COLLISION_DEF_SIZE
        if def_offset + COLLISION_DEF_SIZE > len(self.defs):
            return None

        local_x = x & 7
        # Top-down stage local Y equals 7 - (ROM bottom-up Y & 7), because this
        # stage height is 0 mod 8.  That matches GetCollDataPtr's row.
        local_y = view_y & 7
        column = local_x if rom_x_column else 7 - local_x
        table_index = local_y * 8 + column
        return Sample(
            response=s8(self.defs[def_offset + 2 + 64 + table_index]),
            angle=s8(self.defs[def_offset + 1]),
            collision_type=collision_type,
            local_x=local_x,
            local_y=local_y,
        )


def bg_coll_chk4(
    collision: CollisionData,
    x: int,
    rom_y: int,
    scan_length: int,
    *,
    rom_x_column: bool,
    skip_7f: bool,
) -> Hit:
    offset = 0
    remaining = scan_length
    probe_y = rom_y
    while remaining > 0:
        sample = collision.vertical_response(x, probe_y, rom_x_column=rom_x_column)
        if sample is not None:
            if sample.response == 0x7F or sample.response == -0x81:
                # s8(0x7f) remains 127, so the second form is just defensive.
                if skip_7f:
                    skip = sample.local_y + 1
                    remaining -= skip
                    if remaining <= 0:
                        return Hit()
                    probe_y += skip
                    offset += skip
                    continue
            elif sample.response > 0:
                return Hit(
                    True,
                    sample.response + offset,
                    sample.angle,
                    sample.response,
                    sample.collision_type,
                    sample.local_x,
                    sample.local_y,
                )
        remaining -= 1
        probe_y += 1
        offset += 1
    return Hit()


def choose_floor_pair(
    collision: CollisionData,
    center_x: int,
    rom_y: int,
    radius_x: int,
    scan_length: int,
    *,
    rom_x_column: bool,
    skip_7f: bool,
) -> Hit:
    right = bg_coll_chk4(
        collision,
        center_x + radius_x,
        rom_y,
        scan_length,
        rom_x_column=rom_x_column,
        skip_7f=skip_7f,
    )
    left = bg_coll_chk4(
        collision,
        center_x - radius_x,
        rom_y,
        scan_length,
        rom_x_column=rom_x_column,
        skip_7f=skip_7f,
    )
    if not left.hit:
        return right
    if not right.hit:
        return left
    return left if left.delta_y >= right.delta_y else right


def desired_delta(row: dict[str, str]) -> int:
    return as_int(row["floor_applied_delta_y"]) - (as_int(row["dy_raw"]) // FIXED_ONE)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=Path("out/nsi1"))
    parser.add_argument("--trace", type=Path, default=Path("out/native-teacher-trace.csv"))
    parser.add_argument("--limit", type=int, default=40)
    args = parser.parse_args()

    collision = CollisionData(args.data)
    with args.trace.open(newline="") as handle:
        rows = list(csv.DictReader(handle))

    variants = [
        ("current_index_step", False, False),
        ("current_index_skip7f", False, True),
        ("rom_index_step", True, False),
        ("rom_index_skip7f", True, True),
    ]

    mismatches = [row for row in rows if as_int(row["dy_raw"]) != 0]
    print(f"mismatches={len(mismatches)}")
    print(
        "row,frame,desired,applied,current_hit,"
        + ",".join(name for name, _, _ in variants)
    )
    improved_counts = {name: 0 for name, _, _ in variants}
    exact_counts = {name: 0 for name, _, _ in variants}

    for row in mismatches[: args.limit]:
        center_x = as_int(row["floor_probe_center_x"])
        rom_y = as_int(row["floor_probe_rom_y"])
        radius_x = 7
        # Teacher mismatches all come from the walking floor path, whose live
        # code currently expands byte_668F into this scan length.
        scan_length = max(9, min(0x20, abs(as_int(row["walk_delta_y"]) // FIXED_ONE) + 9))
        want = desired_delta(row)
        applied = as_int(row["floor_applied_delta_y"])
        current_hit = (
            f"{row['floor_hit_delta_y']}/"
            f"{row['floor_hit_response']}/"
            f"{row['floor_hit_local_x']}/"
            f"{row['floor_hit_collision_type']}"
        )
        values: list[str] = []
        for name, rom_x_column, skip_7f in variants:
            hit = choose_floor_pair(
                collision,
                center_x,
                rom_y,
                radius_x,
                scan_length,
                rom_x_column=rom_x_column,
                skip_7f=skip_7f,
            )
            text = (
                f"{hit.delta_y}/{hit.response}/{hit.local_x}/{hit.collision_type}"
                if hit.hit
                else "miss"
            )
            values.append(text)
            if hit.hit and abs(hit.delta_y - want) < abs(applied - want):
                improved_counts[name] += 1
            if hit.hit and hit.delta_y == want:
                exact_counts[name] += 1
        print(
            f"{row['row']},{row['frame']},{want},{applied},{current_hit},"
            + ",".join(values)
        )

    print()
    print("variant_score:")
    for name, _, _ in variants:
        print(f"{name}: exact={exact_counts[name]} closer_raw={improved_counts[name]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
