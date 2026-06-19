#!/usr/bin/env python3
"""Extract and render a Sonic Pocket Adventure level from a verified ROM."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

import rominfo


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = PROJECT_ROOT / "out" / "nsi1"
DEFAULT_REFERENCE = (
    PROJECT_ROOT
    / "SonicPocketAdventure_disasm+tools"
    / "disassembly"
)
PALETTE_COLLECTION_OFFSET = 0x03C748
PALETTE_COLLECTION_SIZE = 0x1800


@dataclass(frozen=True)
class Segment:
    filename: str
    offset: int
    size: int
    reference: str


@dataclass(frozen=True)
class LevelSpec:
    key: str
    name: str
    width_blocks: int
    height_blocks: int
    start_x: int
    start_y: int
    rings: int
    background_palette: int
    segments: dict[str, Segment]


NSI1 = LevelSpec(
    key="nsi1",
    name="Neo South Island Act 1",
    width_blocks=200,
    height_blocks=31,
    start_x=0x70,
    start_y=0x1A8,
    rings=333,
    background_palette=2,
    segments={
        "tiles": Segment("tiles.bin", 0x091D4C, 0x1C00, "level/1-1_TileData.bin"),
        "collision": Segment("collision.bin", 0x09394C, 0x0380, "level/1-1_Coll.bin"),
        "plane2": Segment("plane2.bin", 0x093CCC, 0x3070, "level/1-1_BlkPlane2.bin"),
        "plane1": Segment("plane1.bin", 0x096D3C, 0x3070, "level/1-1_BlkPlane1.bin"),
        "blocks": Segment("blocks.bin", 0x099DAC, 0x97A0, "level/1-1_BlkTileMap.bin"),
        "palette1": Segment(
            "palette1.bin", 0x0A354C, 0x22, "level/1-1_PalsPlane1.bin"
        ),
        "palette2": Segment(
            "palette2.bin", 0x0A356E, 0x22, "level/1-1_PalsPlane2.bin"
        ),
        "objects": Segment("objects.bin", 0x0A35B2, 0x05B8, "level/1-1_Objects.bin"),
    },
)

LEVELS = {NSI1.key: NSI1}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def unpack_words(data: bytes) -> list[int]:
    if len(data) % 2:
        raise ValueError("word data has an odd byte count")
    return list(struct.unpack(f"<{len(data) // 2}H", data))


def decode_ngpc_color(word: int) -> tuple[int, int, int]:
    """Decode an NGPC 12-bit RGB color into 8-bit channels."""
    return (
        (word & 0x000F) * 17,
        ((word >> 4) & 0x000F) * 17,
        ((word >> 8) & 0x000F) * 17,
    )


def decode_tile(tile_data: bytes, tile_id: int) -> tuple[tuple[int, ...], ...]:
    start = tile_id * 16
    raw = tile_data[start : start + 16]
    if len(raw) != 16:
        raise ValueError(f"tile {tile_id} lies outside the tile data")

    rows: list[tuple[int, ...]] = []
    for row in range(8):
        packed = int.from_bytes(raw[row * 2 : row * 2 + 2], "little")
        rows.append(tuple((packed >> (14 - pixel * 2)) & 3 for pixel in range(8)))
    return tuple(rows)


def palette_ids(data: bytes) -> list[int]:
    values = unpack_words(data)
    if values and values[-1] == 0xFFFF:
        values.pop()
    if len(values) != 16:
        raise ValueError(f"expected 16 palette slots, found {len(values)}")
    return values


def build_palettes(collection: bytes, ids: Iterable[int]) -> list[list[tuple[int, int, int]]]:
    palettes: list[list[tuple[int, int, int]]] = []
    for palette_id in ids:
        start = palette_id * 6
        raw = collection[start : start + 6]
        if len(raw) != 6:
            raise ValueError(f"palette {palette_id} lies outside the palette collection")
        palettes.append(
            [(0, 0, 0)]
            + [decode_ngpc_color(value) for value in unpack_words(raw)]
        )
    return palettes


def background_color(collection: bytes, palette_id: int) -> tuple[int, int, int]:
    start = palette_id * 6
    raw = collection[start : start + 2]
    if len(raw) != 2:
        raise ValueError(f"background palette {palette_id} lies outside the collection")
    return decode_ngpc_color(int.from_bytes(raw, "little"))


def render_block(
    block_id: int,
    block_words: list[int],
    tile_data: bytes,
    palettes: list[list[tuple[int, int, int]]],
    tile_cache: dict[int, tuple[tuple[int, ...], ...]],
) -> tuple[bytes, bytes]:
    start = block_id * 16
    entries = block_words[start : start + 16]
    if len(entries) != 16:
        raise ValueError(f"block {block_id} lies outside the block table")

    image = bytearray(32 * 32 * 3)
    opaque = bytearray(32 * 32)
    for position, entry in enumerate(entries):
        tile_id = entry & 0x01FF
        palette_slot = (entry >> 9) & 0x0F
        flip_y = bool(entry & 0x4000)
        flip_x = bool(entry & 0x8000)
        tile = tile_cache.get(tile_id)
        if tile is None:
            tile = decode_tile(tile_data, tile_id)
            tile_cache[tile_id] = tile
        destination_x = (position % 4) * 8
        destination_y = (position // 4) * 8

        for y in range(8):
            source_y = 7 - y if flip_y else y
            for x in range(8):
                source_x = 7 - x if flip_x else x
                color = palettes[palette_slot][tile[source_y][source_x]]
                pixel = ((destination_y + y) * 32 + destination_x + x) * 3
                image[pixel : pixel + 3] = bytes(color)
                opaque[(destination_y + y) * 32 + destination_x + x] = (
                    255 if tile[source_y][source_x] else 0
                )
    return bytes(image), bytes(opaque)


def render_plane(
    layout_data: bytes,
    block_data: bytes,
    tile_data: bytes,
    palettes: list[list[tuple[int, int, int]]],
    width_blocks: int,
    height_blocks: int,
) -> tuple[int, int, bytes, bytes]:
    layout = unpack_words(layout_data)
    expected = width_blocks * height_blocks
    if len(layout) != expected:
        raise ValueError(f"expected {expected} layout entries, found {len(layout)}")

    block_words = unpack_words(block_data)
    tile_cache: dict[int, tuple[tuple[int, ...], ...]] = {}
    block_cache: dict[int, tuple[bytes, bytes]] = {}
    width = width_blocks * 32
    height = height_blocks * 32
    image = bytearray(width * height * 3)
    opaque = bytearray(width * height)

    for index, block_id in enumerate(layout):
        rendered_block = block_cache.get(block_id)
        if rendered_block is None:
            rendered_block = render_block(
                block_id, block_words, tile_data, palettes, tile_cache
            )
            block_cache[block_id] = rendered_block
        block, block_opaque = rendered_block
        block_x = (index % width_blocks) * 32
        block_y = (index // width_blocks) * 32
        for row in range(32):
            source = row * 32 * 3
            destination = ((block_y + row) * width + block_x) * 3
            image[destination : destination + 32 * 3] = block[source : source + 32 * 3]
            mask_source = row * 32
            mask_destination = (block_y + row) * width + block_x
            opaque[mask_destination : mask_destination + 32] = block_opaque[
                mask_source : mask_source + 32
            ]
    return width, height, bytes(image), bytes(opaque)


def composite_rgb(background: bytes, foreground: bytes, foreground_mask: bytes) -> bytes:
    if len(background) != len(foreground) or len(background) != len(foreground_mask) * 3:
        raise ValueError("composite layer dimensions do not match")
    result = bytearray(background)
    for pixel, is_opaque in enumerate(foreground_mask):
        if is_opaque:
            offset = pixel * 3
            result[offset : offset + 3] = foreground[offset : offset + 3]
    return bytes(result)


def solid_rgb(width: int, height: int, color: tuple[int, int, int]) -> bytes:
    return bytes(color) * (width * height)


def collision_color(value: int) -> tuple[int, int, int]:
    if value == 0:
        return (0, 0, 0)
    hue = (value * 47) % 255
    return (255, 48 + hue // 3, hue)


def render_collision(
    layout_data: bytes,
    block_data: bytes,
    collision_data: bytes,
    width_blocks: int,
    height_blocks: int,
) -> tuple[int, int, bytes]:
    layout = unpack_words(layout_data)
    block_words = unpack_words(block_data)
    path1 = collision_data[: len(collision_data) // 2]
    width = width_blocks * 32
    height = height_blocks * 32
    image = bytearray(width * height * 3)

    for index, block_id in enumerate(layout):
        entries = block_words[block_id * 16 : block_id * 16 + 16]
        block_x = (index % width_blocks) * 32
        block_y = (index // width_blocks) * 32
        for position, entry in enumerate(entries):
            tile_id = entry & 0x01FF
            color = collision_color(path1[tile_id])
            tile_x = block_x + (position % 4) * 8
            tile_y = block_y + (position // 4) * 8
            row_pixels = bytes(color) * 8
            for row in range(8):
                destination = ((tile_y + row) * width + tile_x) * 3
                image[destination : destination + 24] = row_pixels
    return width, height, bytes(image)


def parse_objects(data: bytes) -> dict[str, Any]:
    if len(data) < 2:
        raise ValueError("object data is missing its screen-grid header")
    screens_x, screens_y = data[0], data[1]
    pointer_count = screens_x * screens_y
    table_end = 2 + pointer_count * 2
    if table_end > len(data):
        raise ValueError("object screen pointer table is truncated")

    pointers = unpack_words(data[2:table_end])
    screens: list[dict[str, Any]] = []
    objects: list[dict[str, Any]] = []
    parsed_chunks: dict[int, tuple[int, list[dict[str, Any]]]] = {}

    for screen_index, pointer in enumerate(pointers):
        screen_x = screen_index % screens_x
        screen_y = screen_index // screens_x
        screen: dict[str, Any] = {
            "x": screen_x,
            "y": screen_y,
            "pointer": pointer,
            "objects": [],
        }
        if pointer:
            if pointer not in parsed_chunks:
                cursor = 2 + pointer
                if cursor + 2 > len(data):
                    raise ValueError(f"object chunk 0x{pointer:04x} is outside the file")
                slot = data[cursor]
                count = data[cursor + 1]
                cursor += 2
                records: list[dict[str, Any]] = []
                for record_index in range(count):
                    raw = data[cursor : cursor + 10]
                    if len(raw) != 10:
                        raise ValueError(f"object chunk 0x{pointer:04x} is truncated")
                    records.append(
                        {
                            "record": record_index,
                            "id": raw[0],
                            "difficulty": raw[1],
                            "x": int.from_bytes(raw[2:4], "little"),
                            "y": int.from_bytes(raw[4:6], "little"),
                            "parameters": list(raw[6:10]),
                        }
                    )
                    cursor += 10
                parsed_chunks[pointer] = (slot, records)

            slot, records = parsed_chunks[pointer]
            screen["slot"] = slot
            for record in records:
                item = {**record, "screen_x": screen_x, "screen_y": screen_y, "slot": slot}
                screen["objects"].append(record)
                objects.append(item)
        screens.append(screen)

    return {
        "screen_grid": {"width": screens_x, "height": screens_y},
        "object_count": len(objects),
        "objects": objects,
        "screens": screens,
    }


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    )


def write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    expected = width * height * 3
    if len(rgb) != expected:
        raise ValueError(f"expected {expected} RGB bytes, found {len(rgb)}")
    stride = width * 3
    scanlines = b"".join(
        b"\0" + rgb[row * stride : (row + 1) * stride] for row in range(height)
    )
    contents = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(scanlines, 9))
        + png_chunk(b"IEND", b"")
    )
    path.write_bytes(contents)


def validate_reference(
    segments: dict[str, bytes], spec: LevelSpec, reference: Path | None
) -> dict[str, Any]:
    if reference is None or not reference.is_dir():
        return {"available": False, "all_match": None, "segments": {}}

    results: dict[str, Any] = {}
    for name, segment in spec.segments.items():
        reference_path = reference / segment.reference
        if not reference_path.is_file():
            results[name] = {"available": False, "match": None}
            continue
        expected = reference_path.read_bytes()
        results[name] = {
            "available": True,
            "match": expected == segments[name],
            "reference_sha256": sha256(expected),
        }
    available = [result for result in results.values() if result["available"]]
    return {
        "available": bool(available),
        "all_match": bool(available) and all(result["match"] for result in available),
        "segments": results,
    }


def extract(
    rom_path: Path,
    output: Path,
    spec: LevelSpec = NSI1,
    reference: Path | None = None,
) -> dict[str, Any]:
    manifest = json.loads(rominfo.DEFAULT_MANIFEST.read_text(encoding="utf-8"))
    info = rominfo.inspect_rom(rom_path)
    mismatches = rominfo.compare_manifest(info, manifest)
    if mismatches:
        raise ValueError("ROM verification failed: " + "; ".join(mismatches))

    rom = rom_path.read_bytes()
    output.mkdir(parents=True, exist_ok=True)
    data_directory = output / "data"
    data_directory.mkdir(exist_ok=True)

    segments: dict[str, bytes] = {}
    segment_manifest: dict[str, Any] = {}
    for name, segment in spec.segments.items():
        data = rom[segment.offset : segment.offset + segment.size]
        if len(data) != segment.size:
            raise ValueError(f"{name} extends beyond the end of the ROM")
        segments[name] = data
        (data_directory / segment.filename).write_bytes(data)
        segment_manifest[name] = {
            "rom_offset": f"0x{segment.offset:06x}",
            "size": segment.size,
            "sha256": sha256(data),
            "output": f"data/{segment.filename}",
        }

    palette_collection = rom[
        PALETTE_COLLECTION_OFFSET : PALETTE_COLLECTION_OFFSET + PALETTE_COLLECTION_SIZE
    ]
    (data_directory / "palette_collection.bin").write_bytes(palette_collection)
    palettes1_ids = palette_ids(segments["palette1"])
    palettes2_ids = palette_ids(segments["palette2"])
    palettes1 = build_palettes(palette_collection, palettes1_ids)
    palettes2 = build_palettes(palette_collection, palettes2_ids)

    width, height, plane1, plane1_mask = render_plane(
        segments["plane1"],
        segments["blocks"],
        segments["tiles"],
        palettes1,
        spec.width_blocks,
        spec.height_blocks,
    )
    _, _, plane2, plane2_mask = render_plane(
        segments["plane2"],
        segments["blocks"],
        segments["tiles"],
        palettes2,
        spec.width_blocks,
        spec.height_blocks,
    )
    _, _, collision = render_collision(
        segments["plane1"],
        segments["blocks"],
        segments["collision"],
        spec.width_blocks,
        spec.height_blocks,
    )
    backdrop = solid_rgb(
        width, height, background_color(palette_collection, spec.background_palette)
    )
    plane2_composited = composite_rgb(backdrop, plane2, plane2_mask)
    write_png(output / "plane1.png", width, height, plane1)
    write_png(output / "plane2.png", width, height, plane2_composited)
    write_png(
        output / "stage.png",
        width,
        height,
        composite_rgb(plane2_composited, plane1, plane1_mask),
    )
    write_png(output / "collision.png", width, height, collision)

    objects = parse_objects(segments["objects"])
    (output / "objects.json").write_text(
        json.dumps(objects, indent=2) + "\n", encoding="utf-8"
    )

    result = {
        "format": 1,
        "level": {
            "key": spec.key,
            "name": spec.name,
            "size_blocks": [spec.width_blocks, spec.height_blocks],
            "size_pixels": [width, height],
            "start": {"x": spec.start_x, "y": spec.start_y},
            "ring_count": spec.rings,
            "background_palette": spec.background_palette,
        },
        "source_rom": {
            "path": str(rom_path.resolve()),
            "size": info["size"],
            "hashes": info["hashes"],
            "verified": True,
        },
        "segments": segment_manifest,
        "palettes": {"plane1": palettes1_ids, "plane2": palettes2_ids},
        "objects": {
            "count": objects["object_count"],
            "screen_grid": objects["screen_grid"],
            "output": "objects.json",
        },
        "images": {
            "composited_stage": "stage.png",
            "plane1": "plane1.png",
            "plane2": "plane2.png",
            "collision_path1": "collision.png",
        },
        "reference_validation": validate_reference(segments, spec, reference),
    }
    (output / "manifest.json").write_text(
        json.dumps(result, indent=2) + "\n", encoding="utf-8"
    )
    return result


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "rom",
        nargs="?",
        type=Path,
        help="ROM path; defaults to the sole image in the local Rom directory",
    )
    parser.add_argument("--level", choices=LEVELS, default="nsi1")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--reference",
        type=Path,
        default=DEFAULT_REFERENCE if DEFAULT_REFERENCE.is_dir() else None,
        help="optional ValleyBell disassembly directory for byte validation",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        rom_path = args.rom or rominfo.discover_rom(rominfo.DEFAULT_ROM_DIRECTORY)
        result = extract(
            rom_path=rom_path,
            output=args.output,
            spec=LEVELS[args.level],
            reference=args.reference,
        )
    except (FileNotFoundError, OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    validation = result["reference_validation"]
    reference_status = (
        "PASS"
        if validation["all_match"] is True
        else "FAIL"
        if validation["all_match"] is False
        else "not available"
    )
    print(f"Extracted {result['level']['name']} to {args.output.resolve()}")
    print(
        f"Rendered {result['level']['size_pixels'][0]}x"
        f"{result['level']['size_pixels'][1]} plane and collision images"
    )
    print(f"Exported {result['objects']['count']} object placements")
    print(f"Reference binary validation: {reference_status}")
    return 0 if validation["all_match"] is not False else 1


if __name__ == "__main__":
    raise SystemExit(main())
