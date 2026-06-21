#!/usr/bin/env python3
"""Summarize BizHawk title object traces for title-screen porting.

The Lua trace records very wide rows.  This tool condenses it into the frames
where the ROM's title object/sprite composition changes, which are the frames
that should drive `tools/extract_title.py`.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path


DEFAULT_LATEST = Path("out/title-object-trace-latest.txt")


@dataclass(frozen=True)
class Row:
    trace_frame: int
    emu_frame: int
    object_count: int
    sprite_count: int
    active_objects: str
    copied_sprites: str

    @classmethod
    def from_csv(cls, row: dict[str, str]) -> "Row":
        return cls(
            trace_frame=int(row["trace_frame"]),
            emu_frame=int(row["emu_frame"]),
            object_count=int(row["active_object_count"]),
            sprite_count=int(row["sprite_tile_count"]),
            active_objects=row["active_objects"],
            copied_sprites=row["copied_sprites"],
        )

    @property
    def signature(self) -> tuple[int, int, str, str]:
        return (
            self.object_count,
            self.sprite_count,
            self.active_objects,
            self.copied_sprites,
        )


def load_rows(path: Path) -> list[Row]:
    with path.open(newline="", encoding="utf-8") as handle:
        return [Row.from_csv(row) for row in csv.DictReader(handle)]


def latest_trace(marker: Path) -> Path:
    if not marker.is_file():
        raise SystemExit(f"latest trace marker not found: {marker}")
    path = Path(marker.read_text(encoding="utf-8").strip())
    if not path.is_file():
        raise SystemExit(f"latest trace does not exist: {path}")
    return path


def shorten(value: str, limit: int) -> str:
    if len(value) <= limit:
        return value
    return value[: limit - 3] + "..."


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", nargs="?", type=Path, help="title object trace CSV")
    parser.add_argument("--latest", type=Path, default=DEFAULT_LATEST)
    parser.add_argument("--start", type=int, default=0, help="first trace frame to inspect")
    parser.add_argument("--end", type=int, default=-1, help="last trace frame to inspect; -1 = end")
    parser.add_argument(
        "--object-width",
        type=int,
        default=180,
        help="maximum active-object text width in the summary",
    )
    parser.add_argument(
        "--sprite-width",
        type=int,
        default=220,
        help="maximum copied-sprite text width in the summary",
    )
    args = parser.parse_args()

    trace = args.trace or latest_trace(args.latest)
    rows = load_rows(trace)
    if not rows:
        raise SystemExit(f"trace has no rows: {trace}")

    selected = [
        row
        for row in rows
        if row.trace_frame >= args.start and (args.end < 0 or row.trace_frame <= args.end)
    ]
    if not selected:
        raise SystemExit("no rows in requested frame range")

    print(f"Trace: {trace}")
    print(f"Rows: {len(rows)}  selected: {selected[0].trace_frame}..{selected[-1].trace_frame}")
    print()
    print("Change frames:")

    previous_signature: tuple[int, int, str, str] | None = None
    change_count = 0
    for row in selected:
        if row.signature == previous_signature:
            continue
        previous_signature = row.signature
        change_count += 1
        print(
            f"{row.trace_frame:5d} emu={row.emu_frame:8d} "
            f"obj={row.object_count:2d} spr={row.sprite_count:2d}"
        )
        print(f"      objects: {shorten(row.active_objects, args.object_width)}")
        print(f"      sprites: {shorten(row.copied_sprites, args.sprite_width)}")
    print()
    print(f"{change_count} composition change(s) in selected range.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
