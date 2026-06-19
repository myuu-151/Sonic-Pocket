#!/usr/bin/env python3
"""Extract and render a Sonic Pocket Adventure level from a verified ROM."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
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
SPRITE_TILES_OFFSET = 0x00CB34
SPRITE_TILES_SIZE = 0x2AA40
SONIC_CANVAS_WIDTH = 64
SONIC_CANVAS_HEIGHT = 64
SONIC_CANVAS_ORIGIN_X = 32
SONIC_CANVAS_ORIGIN_Y = 40
SONIC_ANIMATIONS = {
    # PAniScr_398737: standing blink/idle loop.
    "idle": ((0x0009, 60), (0x000A, 3), (0x000B, 20)),
    # PAniScr_398CD7 / asLoc_398CE7: fast ground cycle.
    "run": tuple((sprite_id, 2) for sprite_id in range(0x0001, 0x0009)),
    # PAniScr_398CD1: airborne/spin player sprite.
    "jump": ((0x0055, 1),),
    "fall": ((0x0055, 1),),
}


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
    collision_types = unpack_words(collision_data)
    width = width_blocks * 32
    height = height_blocks * 32
    image = bytearray(width * height * 3)

    for index, block_id in enumerate(layout):
        entries = block_words[block_id * 16 : block_id * 16 + 16]
        block_x = (index % width_blocks) * 32
        block_y = (index // width_blocks) * 32
        for position, entry in enumerate(entries):
            tile_id = entry & 0x01FF
            collision_type = collision_types[tile_id]
            color = collision_color(0 if collision_type == 0xFFFF else collision_type)
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


def write_png_rgba(path: Path, width: int, height: int, rgba: bytes) -> None:
    expected = width * height * 4
    if len(rgba) != expected:
        raise ValueError(f"expected {expected} RGBA bytes, found {len(rgba)}")
    stride = width * 4
    scanlines = b"".join(
        b"\0" + rgba[row * stride : (row + 1) * stride] for row in range(height)
    )
    contents = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(scanlines, 9))
        + png_chunk(b"IEND", b"")
    )
    path.write_bytes(contents)


def render_sprite(
    sprite_tiles: bytes,
    palette_collection: bytes,
    layers: list[tuple[bytes, int]],
) -> tuple[int, int, int, int, bytes]:
    entries: list[tuple[int, int, int, int]] = []
    for sprite_data, palette_id in layers:
        if len(sprite_data) < 2:
            raise ValueError("sprite definition is truncated")
        count = sprite_data[0]
        if 2 + count * 4 > len(sprite_data):
            raise ValueError("sprite tile list is truncated")
        for index in range(count):
            offset = 2 + index * 4
            tile_id = int.from_bytes(sprite_data[offset : offset + 2], "little")
            if tile_id == 0xFFFF:
                continue
            x = int.from_bytes(
                sprite_data[offset + 2 : offset + 3], "little", signed=True
            )
            y = int.from_bytes(
                sprite_data[offset + 3 : offset + 4], "little", signed=True
            )
            entries.append((palette_id, tile_id, x, y))

    min_x = min(x for _, _, x, _ in entries)
    min_y = min(y for _, _, _, y in entries)
    max_x = max(x + 8 for _, _, x, _ in entries)
    max_y = max(y + 8 for _, _, _, y in entries)
    width = max_x - min_x
    height = max_y - min_y
    image = bytearray(width * height * 4)
    palettes = {
        palette_id: build_palettes(palette_collection, [palette_id])[0]
        for palette_id, _, _, _ in entries
    }

    for palette_id, tile_id, tile_x, tile_y in entries:
        tile = decode_tile(sprite_tiles, tile_id)
        for y, row in enumerate(tile):
            for x, color_index in enumerate(row):
                if color_index == 0:
                    continue
                destination_x = tile_x - min_x + x
                destination_y = tile_y - min_y + y
                destination = (destination_y * width + destination_x) * 4
                image[destination : destination + 3] = bytes(
                    palettes[palette_id][color_index]
                )
                image[destination + 3] = 255
    return width, height, -min_x, -min_y, bytes(image)


def place_sprite_on_canvas(
    width: int, height: int, origin_x: int, origin_y: int, rgba: bytes
) -> bytes:
    canvas = bytearray(SONIC_CANVAS_WIDTH * SONIC_CANVAS_HEIGHT * 4)
    offset_x = SONIC_CANVAS_ORIGIN_X - origin_x
    offset_y = SONIC_CANVAS_ORIGIN_Y - origin_y
    for source_y in range(height):
        destination_y = offset_y + source_y
        if destination_y < 0 or destination_y >= SONIC_CANVAS_HEIGHT:
            continue
        for source_x in range(width):
            destination_x = offset_x + source_x
            if destination_x < 0 or destination_x >= SONIC_CANVAS_WIDTH:
                continue
            source = (source_y * width + source_x) * 4
            if rgba[source + 3] == 0:
                continue
            destination = (destination_y * SONIC_CANVAS_WIDTH + destination_x) * 4
            canvas[destination : destination + 4] = rgba[source : source + 4]
    return bytes(canvas)


def parse_player_sprite_list(reference_directory: Path) -> list[tuple[int, int]]:
    spa_asm = reference_directory / "spa.asm"
    if not spa_asm.is_file():
        raise FileNotFoundError(f"missing disassembly source: {spa_asm}")

    rows: list[tuple[int, int]] = []
    collecting = False
    sprite_pattern = re.compile(r"Spr_[0-9A-Fa-f]+_([0-9A-Fa-f]+)")
    for line in spa_asm.read_text(encoding="utf-8", errors="replace").splitlines():
        if "Player_SprList:" in line:
            collecting = True
        if not collecting:
            continue

        matches = [int(match, 16) for match in sprite_pattern.findall(line)]
        if len(matches) >= 2:
            rows.append((matches[0], matches[1]))
            continue
        if rows and line and not line[0].isspace() and not line.startswith(";"):
            break

    if len(rows) <= 0x55:
        raise ValueError(
            f"Player_SprList only yielded {len(rows)} rows; expected at least 0x56"
        )
    return rows


def sprite_definition_size(sprite_data: bytes, offset: int) -> int:
    if offset + 2 > len(sprite_data):
        raise ValueError(f"sprite definition 0x{offset:04X} is outside sprite data")
    count = sprite_data[offset]
    size = 2 + count * 4
    if offset + size > len(sprite_data):
        raise ValueError(f"sprite definition 0x{offset:04X} is truncated")
    return size


def render_player_sprite(
    rom: bytes,
    sprite_tiles: bytes,
    palette_collection: bytes,
    player_sprite_list: list[tuple[int, int]],
    sprite_id: int,
) -> tuple[int, int, bytes]:
    layer0_offset, layer1_offset = player_sprite_list[sprite_id]
    layer0_size = sprite_definition_size(rom, layer0_offset)
    layer1_size = sprite_definition_size(rom, layer1_offset)
    width, height, origin_x, origin_y, rgba = render_sprite(
        sprite_tiles,
        palette_collection,
        [
            (rom[layer0_offset : layer0_offset + layer0_size], 0),
            (rom[layer1_offset : layer1_offset + layer1_size], 7),
        ],
    )
    return (
        SONIC_CANVAS_ORIGIN_X,
        SONIC_CANVAS_ORIGIN_Y,
        place_sprite_on_canvas(width, height, origin_x, origin_y, rgba),
    )


def collision_surface(collision_type: int, x: int) -> int | None:
    if collision_type in (1, 22):
        return 0
    if collision_type == 2:
        return 7 - x
    if collision_type == 3:
        return 7 - x // 2
    if collision_type == 4:
        return max(0, 3 - x // 2)
    if 5 <= collision_type <= 8:
        segment = collision_type - 5
        return 7 - (segment * 8 + x) // 4
    if collision_type == 9:
        return max(0, 7 - x * 2)
    if collision_type == 10:
        return 7 if x < 4 else max(0, 7 - (x - 4) * 2)
    if collision_type == 12:
        return x
    if collision_type == 13:
        return 7 - (7 - x) // 2
    if collision_type == 14:
        return max(0, 3 - (7 - x) // 2)
    if 15 <= collision_type <= 18:
        segment = 18 - collision_type
        return 7 - (segment * 8 + (7 - x)) // 4
    if collision_type == 19:
        return max(0, 7 - (7 - x) * 2)
    if collision_type == 20:
        return 7 if x > 3 else max(0, 7 - (3 - x) * 2)
    if collision_type == 25:
        return 4
    return None


def collision_tile_mask(collision_type: int) -> list[list[bool]]:
    pixels = [[False] * 8 for _ in range(8)]
    if collision_type in (21, 24, 26):
        return [[True] * 8 for _ in range(8)]
    if collision_type in (11, 23):
        return pixels
    for x in range(8):
        surface = collision_surface(collision_type, x)
        if surface is not None:
            for y in range(max(0, surface), 8):
                pixels[y][x] = True
    return pixels


def build_collision_mask(
    layout_data: bytes,
    block_data: bytes,
    collision_data: bytes,
    width_blocks: int,
    height_blocks: int,
) -> bytes:
    layout = unpack_words(layout_data)
    block_words = unpack_words(block_data)
    collision_types = unpack_words(collision_data)
    width = width_blocks * 32
    height = height_blocks * 32
    mask = bytearray(width * height)
    tile_masks: dict[int, list[list[bool]]] = {}

    for index, block_id in enumerate(layout):
        entries = block_words[block_id * 16 : block_id * 16 + 16]
        block_x = (index % width_blocks) * 32
        block_y = (index // width_blocks) * 32
        for position, entry in enumerate(entries):
            tile_id = entry & 0x01FF
            collision_type = collision_types[tile_id]
            if collision_type == 0xFFFF:
                collision_type = 0
            tile_mask = tile_masks.setdefault(
                collision_type, collision_tile_mask(collision_type)
            )
            flip_y = bool(entry & 0x4000)
            flip_x = bool(entry & 0x8000)
            tile_x = block_x + (position % 4) * 8
            tile_y = block_y + (position // 4) * 8
            for y in range(8):
                source_y = 7 - y if flip_y else y
                for x in range(8):
                    source_x = 7 - x if flip_x else x
                    if tile_mask[source_y][source_x]:
                        mask[(tile_y + y) * width + tile_x + x] = 255
    return bytes(mask)


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
        segments["plane2"],
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
    collision_mask = build_collision_mask(
        segments["plane2"],
        segments["blocks"],
        segments["collision"],
        spec.width_blocks,
        spec.height_blocks,
    )
    (output / "collision-mask.bin").write_bytes(collision_mask)

    sprite_tiles = rom[
        SPRITE_TILES_OFFSET : SPRITE_TILES_OFFSET + SPRITE_TILES_SIZE
    ]
    player_sprite_list = parse_player_sprite_list(reference or DEFAULT_REFERENCE)
    sonic_directory = output / "sonic"
    sonic_directory.mkdir(exist_ok=True)
    sonic_frames: list[dict[str, Any]] = []
    sonic_animations: dict[str, list[dict[str, Any]]] = {}
    rendered_sprite_ids: dict[int, str] = {}

    for animation_name, frames in SONIC_ANIMATIONS.items():
        sonic_animations[animation_name] = []
        for frame_index, (sprite_id, duration) in enumerate(frames):
            frame_filename = f"{animation_name}_{frame_index:02d}.png"
            if sprite_id in rendered_sprite_ids:
                source_name = rendered_sprite_ids[sprite_id]
                source_bytes = (sonic_directory / source_name).read_bytes()
                (sonic_directory / frame_filename).write_bytes(source_bytes)
            else:
                frame_origin_x, frame_origin_y, frame_rgba = render_player_sprite(
                    rom,
                    sprite_tiles,
                    palette_collection,
                    player_sprite_list,
                    sprite_id,
                )
                write_png_rgba(
                    sonic_directory / frame_filename,
                    SONIC_CANVAS_WIDTH,
                    SONIC_CANVAS_HEIGHT,
                    frame_rgba,
                )
                rendered_sprite_ids[sprite_id] = frame_filename

            legacy_name = {
                ("idle", 0): "idle.png",
                ("run", 0): "step0.png",
                ("run", 1): "step1.png",
                ("run", 2): "step2.png",
                ("jump", 0): "jump.png",
            }.get((animation_name, frame_index))
            if legacy_name is not None:
                (sonic_directory / legacy_name).write_bytes(
                    (sonic_directory / frame_filename).read_bytes()
                )
            if animation_name == "idle" and frame_index == 0:
                (output / "sonic-idle.png").write_bytes(
                    (sonic_directory / frame_filename).read_bytes()
                )

            sonic_animations[animation_name].append(
                {
                    "sprite_id": sprite_id,
                    "duration_frames": duration,
                    "output": f"sonic/{frame_filename}",
                    "size": [SONIC_CANVAS_WIDTH, SONIC_CANVAS_HEIGHT],
                    "origin": [SONIC_CANVAS_ORIGIN_X, SONIC_CANVAS_ORIGIN_Y],
                }
            )
            sonic_frames.append(
                {
                    "name": f"{animation_name}_{frame_index:02d}",
                    "sprite_id": sprite_id,
                    "duration_frames": duration,
                    "output": f"sonic/{frame_filename}",
                    "size": [SONIC_CANVAS_WIDTH, SONIC_CANVAS_HEIGHT],
                    "origin": [SONIC_CANVAS_ORIGIN_X, SONIC_CANVAS_ORIGIN_Y],
                }
            )

    (sonic_directory / "animations.json").write_text(
        json.dumps(sonic_animations, indent=2) + "\n", encoding="utf-8"
    )

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
            "sonic_idle": "sonic-idle.png",
        },
        "sonic_animations": {
            "output": "sonic/animations.json",
            "canvas": [SONIC_CANVAS_WIDTH, SONIC_CANVAS_HEIGHT],
            "origin": [SONIC_CANVAS_ORIGIN_X, SONIC_CANVAS_ORIGIN_Y],
            "states": sonic_animations,
        },
        "sonic_frames": sonic_frames,
        "collision_mask": {
            "output": "collision-mask.bin",
            "size": [width, height],
            "solid_bytes": sum(1 for value in collision_mask if value),
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
