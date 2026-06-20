#!/usr/bin/env python3
"""Render the Sonic Pocket Adventure title screen assets from the disassembly."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

SCRIPT_PATH = Path(__file__.replace("\\", "/")).resolve()
sys.path.insert(0, str(SCRIPT_PATH.parent))
sys.path.insert(0, str(Path.cwd() / "tools"))

from extract_level import (
    DEFAULT_REFERENCE,
    PALETTE_COLLECTION_OFFSET,
    PALETTE_COLLECTION_SIZE,
    SPRITE_TILES_OFFSET,
    SPRITE_TILES_SIZE,
    background_color,
    build_palettes,
    composite_rgb,
    decode_tile,
    sha256,
    solid_rgb,
    unpack_words,
    write_png,
    write_png_rgba,
)
import rominfo


TITLE_WIDTH_TILES = 20
TITLE_HEIGHT_TILES = 19
TITLE_WIDTH = TITLE_WIDTH_TILES * 8
TITLE_HEIGHT = TITLE_HEIGHT_TILES * 8
PROMPT_WIDTH_TILES = 20
PROMPT_HEIGHT_TILES = 2
PROMPT_DEST_X = 0
PROMPT_DEST_Y = 17 * 8
TITLE_BACKDROP_PALETTE = 0x24F
TITLE_SONIC_X = 0x50
TITLE_SONIC_Y = 0x4C
TITLE_FACE_PALETTE = 0x25C
TITLE_BODY_PALETTE = 0x25B
TITLE_FACE_FRAMES = [
    ("0536_92A6.spr", 4),
    ("0537_92D4.spr", 2),
    ("0538_9302.spr", 2),
    ("0539_9330.spr", 4),
    ("0538_9302.spr", 2),
    ("0537_92D4.spr", 2),
]
TITLE_BODY_FRAMES = [
    ("053A_935E.spr", 4),
    ("053B_93B8.spr", 2),
    ("053C_9412.spr", 2),
    ("053D_946C.spr", 4),
    ("053C_9412.spr", 2),
    ("053B_93B8.spr", 2),
]
TITLE_PALETTE_WORDS = [
    0x8250,
    0x8651,
    0x8A58,
    0x8E5A,
    0x925E,
    0x4252,
    0x4653,
    0x4A54,
    0x4E55,
    0x5256,
    0x5657,
    0x5A59,
    0x5E5D,
    0x625F,
    0xC24F,
    0xC403,
]


def title_palette_banks(
    palette_collection: bytes,
) -> tuple[list[list[tuple[int, int, int]]], list[list[tuple[int, int, int]]], list[int]]:
    palette_ids = [word & 0x03FF for word in TITLE_PALETTE_WORDS]
    decoded = build_palettes(palette_collection, palette_ids)
    empty = [(0, 0, 0)] * 4
    plane1 = [empty for _ in range(16)]
    plane2 = [empty for _ in range(16)]
    for word, palette in zip(TITLE_PALETTE_WORDS, decoded):
        slot = (word >> 10) & 0x3F
        slot_index = slot & 0x0F
        bank = slot & 0x30
        if bank == 0x10:
            plane1[slot_index] = palette
        elif bank == 0x20:
            plane2[slot_index] = palette
        elif bank == 0x30:
            # Title prompt/window data is copied into scroll plane 1.
            plane1[slot_index] = palette
    return plane1, plane2, palette_ids


def decode_title_tile(tile_data: bytes, tile_id: int) -> tuple[tuple[int, ...], ...]:
    """Decode a title tile.

    TitleScr_Tiles is copied with CopyTileBlk, whose first word is a pattern
    count. The stage extractor's decode_tile reads raw tile data from byte 0,
    so using it here shifts every title tile by the two-byte count header.
    """
    if len(tile_data) < 2:
        raise ValueError("title tile data is missing its tile count")
    tile_count = int.from_bytes(tile_data[:2], "little")
    if tile_id >= tile_count:
        raise ValueError(f"title tile {tile_id} outside tile count {tile_count}")
    return decode_tile(tile_data[2:], tile_id)


def render_tilemap(
    tile_data: bytes,
    map_data: bytes,
    palettes: list[list[tuple[int, int, int]]],
    *,
    width_tiles: int,
    height_tiles: int,
    skip_header: int,
    palette_shift: int,
    transparent_zero: bool,
) -> tuple[bytes, bytes]:
    entries = unpack_words(map_data[skip_header:])
    expected = width_tiles * height_tiles
    if len(entries) < expected:
        raise ValueError(f"title map has {len(entries)} entries, expected {expected}")
    entries = entries[:expected]

    width = width_tiles * 8
    height = height_tiles * 8
    image = bytearray(width * height * 3)
    mask = bytearray(width * height)
    tile_cache: dict[int, tuple[tuple[int, ...], ...]] = {}

    for index, entry in enumerate(entries):
        # CopyTileMapBlk skips the 4-byte title map header and copies these
        # words straight into NGPC scroll map memory.
        tile_id = entry & 0x01FF
        palette_slot = (entry >> palette_shift) & 0x0F
        flip_y = (entry & 0x4000) != 0
        flip_x = (entry & 0x8000) != 0
        tile = tile_cache.get(tile_id)
        if tile is None:
            tile = decode_title_tile(tile_data, tile_id)
            tile_cache[tile_id] = tile
        dst_x = (index % width_tiles) * 8
        dst_y = (index // width_tiles) * 8
        palette = palettes[palette_slot]
        for y in range(8):
            source_y = 7 - y if flip_y else y
            for x in range(8):
                source_x = 7 - x if flip_x else x
                color_index = tile[source_y][source_x]
                if transparent_zero and color_index == 0:
                    continue
                pixel = ((dst_y + y) * width + dst_x + x) * 3
                image[pixel : pixel + 3] = bytes(palette[color_index])
                mask[(dst_y + y) * width + dst_x + x] = 255
    return bytes(image), bytes(mask)


def rgba_from_rgb_mask(rgb: bytes, mask: bytes) -> bytes:
    rgba = bytearray((len(rgb) // 3) * 4)
    for pixel in range(len(mask)):
        rgb_offset = pixel * 3
        rgba_offset = pixel * 4
        rgba[rgba_offset : rgba_offset + 3] = rgb[rgb_offset : rgb_offset + 3]
        rgba[rgba_offset + 3] = mask[pixel]
    return bytes(rgba)


def render_title_sprite_canvas(
    sprite_tiles: bytes,
    sprite_data: bytes,
    palette: list[tuple[int, int, int]],
    *,
    origin_x: int,
    origin_y: int,
) -> bytes:
    canvas = bytearray(TITLE_WIDTH * TITLE_HEIGHT * 4)
    tile_count = int.from_bytes(sprite_data[:2], "little")
    offset = 2
    tiles_read = 0

    def draw_tile(raw_tile_word: int, signed_x: int, signed_y: int) -> None:
        tile = decode_tile(sprite_tiles, raw_tile_word)
        destination_x = origin_x + signed_x
        destination_y = origin_y + signed_y
        for y, row in enumerate(tile):
            target_y = destination_y + y
            if target_y < 0 or target_y >= TITLE_HEIGHT:
                continue
            for x, color_index in enumerate(row):
                if color_index == 0:
                    continue
                target_x = destination_x + x
                if target_x < 0 or target_x >= TITLE_WIDTH:
                    continue
                destination = (target_y * TITLE_WIDTH + target_x) * 4
                canvas[destination : destination + 3] = bytes(palette[color_index])
                canvas[destination + 3] = 255

    while tiles_read < tile_count:
        if offset + 4 > len(sprite_data):
            raise ValueError("title sprite definition is truncated")
        tile_word = int.from_bytes(sprite_data[offset : offset + 2], "little")
        if tile_word == 0xFFFF:
            if offset + 8 > len(sprite_data):
                raise ValueError("title sprite vertical run is truncated")
            vertical_count = int.from_bytes(sprite_data[offset + 2 : offset + 4], "little")
            first_tile = int.from_bytes(sprite_data[offset + 4 : offset + 6], "little")
            signed_x = int.from_bytes(sprite_data[offset + 6 : offset + 7], "little", signed=True)
            signed_y = int.from_bytes(sprite_data[offset + 7 : offset + 8], "little", signed=True)
            for tile_offset in range(vertical_count):
                draw_tile(first_tile + tile_offset, signed_x, signed_y + tile_offset * 8)
            tiles_read += vertical_count
            offset += 8
            continue
        signed_x = int.from_bytes(sprite_data[offset + 2 : offset + 3], "little", signed=True)
        signed_y = int.from_bytes(sprite_data[offset + 3 : offset + 4], "little", signed=True)
        draw_tile(tile_word, signed_x, signed_y)
        tiles_read += 1
        offset += 4
    return bytes(canvas)


def alpha_composite_rgba(bottom: bytes, top: bytes) -> bytes:
    if len(bottom) != len(top):
        raise ValueError("RGBA layers have different sizes")
    output = bytearray(bottom)
    for index in range(0, len(top), 4):
        if top[index + 3] == 0:
            continue
        output[index : index + 4] = top[index : index + 4]
    return bytes(output)


def render_variant(
    output: Path,
    tile_data: bytes,
    map1: bytes,
    map2: bytes,
    plane1_palettes: list[list[tuple[int, int, int]]],
    plane2_palettes: list[list[tuple[int, int, int]]],
    background: tuple[int, int, int],
    *,
    skip_header: int,
    palette_shift: int,
) -> Path:
    plane2, plane2_mask = render_tilemap(
        tile_data,
        map2,
        plane2_palettes,
        width_tiles=TITLE_WIDTH_TILES,
        height_tiles=TITLE_HEIGHT_TILES,
        skip_header=skip_header,
        palette_shift=palette_shift,
        transparent_zero=False,
    )
    plane1, plane1_mask = render_tilemap(
        tile_data,
        map1,
        plane1_palettes,
        width_tiles=TITLE_WIDTH_TILES,
        height_tiles=TITLE_HEIGHT_TILES,
        skip_header=skip_header,
        palette_shift=palette_shift,
        transparent_zero=True,
    )
    backdrop = solid_rgb(TITLE_WIDTH, TITLE_HEIGHT, background)
    plane2_composited = composite_rgb(backdrop, plane2, plane2_mask)
    composite = composite_rgb(plane2_composited, plane1, plane1_mask)
    path = output / f"title_skip{skip_header}_pal{palette_shift}.png"
    write_png(path, TITLE_WIDTH, TITLE_HEIGHT, composite)
    return path


def render_layers(
    output: Path,
    tile_data: bytes,
    sprite_tiles: bytes,
    map1: bytes,
    map2: bytes,
    prompt_map: bytes,
    sprite_directory: Path,
    palette_collection: bytes,
    plane1_palettes: list[list[tuple[int, int, int]]],
    plane2_palettes: list[list[tuple[int, int, int]]],
    background: tuple[int, int, int],
    *,
    skip_header: int,
    palette_shift: int,
) -> dict[str, str]:
    plane2, plane2_mask = render_tilemap(
        tile_data,
        map2,
        plane2_palettes,
        width_tiles=TITLE_WIDTH_TILES,
        height_tiles=TITLE_HEIGHT_TILES,
        skip_header=skip_header,
        palette_shift=palette_shift,
        transparent_zero=False,
    )
    plane1, plane1_mask = render_tilemap(
        tile_data,
        map1,
        plane1_palettes,
        width_tiles=TITLE_WIDTH_TILES,
        height_tiles=TITLE_HEIGHT_TILES,
        skip_header=skip_header,
        palette_shift=palette_shift,
        transparent_zero=True,
    )
    prompt, prompt_mask = render_tilemap(
        tile_data,
        prompt_map,
        plane1_palettes,
        width_tiles=PROMPT_WIDTH_TILES,
        height_tiles=PROMPT_HEIGHT_TILES,
        skip_header=skip_header,
        palette_shift=palette_shift,
        transparent_zero=True,
    )

    backdrop = solid_rgb(TITLE_WIDTH, TITLE_HEIGHT, background)
    plane2_composited = composite_rgb(backdrop, plane2, plane2_mask)
    composite = composite_rgb(plane2_composited, plane1, plane1_mask)
    title_path = output / "title.png"
    plane2_path = output / "plane2.png"
    plane1_path = output / "plane1.png"
    prompt_path = output / "press_prompt.png"
    prompt_preview_path = output / "title_with_prompt.png"
    sonic_frame_paths: list[str] = []
    write_png(plane2_path, TITLE_WIDTH, TITLE_HEIGHT, plane2_composited)
    write_png_rgba(plane1_path, TITLE_WIDTH, TITLE_HEIGHT, rgba_from_rgb_mask(plane1, plane1_mask))
    write_png(title_path, TITLE_WIDTH, TITLE_HEIGHT, composite)
    write_png_rgba(
        prompt_path,
        PROMPT_WIDTH_TILES * 8,
        PROMPT_HEIGHT_TILES * 8,
        rgba_from_rgb_mask(prompt, prompt_mask),
    )

    face_palette = build_palettes(palette_collection, [TITLE_FACE_PALETTE])[0]
    body_palette = build_palettes(palette_collection, [TITLE_BODY_PALETTE])[0]
    for index, ((face_name, delay), (body_name, body_delay)) in enumerate(
        zip(TITLE_FACE_FRAMES, TITLE_BODY_FRAMES)
    ):
        if delay != body_delay:
            raise ValueError("title Sonic face/body animation timing mismatch")
        body = render_title_sprite_canvas(
            sprite_tiles,
            (sprite_directory / body_name).read_bytes(),
            body_palette,
            origin_x=TITLE_SONIC_X,
            origin_y=TITLE_SONIC_Y,
        )
        face = render_title_sprite_canvas(
            sprite_tiles,
            (sprite_directory / face_name).read_bytes(),
            face_palette,
            origin_x=TITLE_SONIC_X,
            origin_y=TITLE_SONIC_Y,
        )
        frame = alpha_composite_rgba(body, face)
        frame_path = output / f"title_sonic_{index}.png"
        write_png_rgba(frame_path, TITLE_WIDTH, TITLE_HEIGHT, frame)
        sonic_frame_paths.append(str(frame_path))

    preview = bytearray(composite)
    for y in range(PROMPT_HEIGHT_TILES * 8):
        for x in range(PROMPT_WIDTH_TILES * 8):
            source = y * PROMPT_WIDTH_TILES * 8 + x
            if prompt_mask[source] == 0:
                continue
            dst_x = PROMPT_DEST_X + x
            dst_y = PROMPT_DEST_Y + y
            if 0 <= dst_x < TITLE_WIDTH and 0 <= dst_y < TITLE_HEIGHT:
                preview[(dst_y * TITLE_WIDTH + dst_x) * 3 : (dst_y * TITLE_WIDTH + dst_x) * 3 + 3] = (
                    prompt[source * 3 : source * 3 + 3]
                )
    preview_rgba = bytearray(TITLE_WIDTH * TITLE_HEIGHT * 4)
    for pixel in range(TITLE_WIDTH * TITLE_HEIGHT):
        preview_rgba[pixel * 4 : pixel * 4 + 3] = preview[pixel * 3 : pixel * 3 + 3]
        preview_rgba[pixel * 4 + 3] = 255
    if sonic_frame_paths:
        preview_rgba = bytearray(
            alpha_composite_rgba(
                bytes(preview_rgba),
                render_title_sprite_canvas(
                    sprite_tiles,
                    (sprite_directory / TITLE_BODY_FRAMES[0][0]).read_bytes(),
                    body_palette,
                    origin_x=TITLE_SONIC_X,
                    origin_y=TITLE_SONIC_Y,
                ),
            )
        )
        preview_rgba = bytearray(
            alpha_composite_rgba(
                bytes(preview_rgba),
                render_title_sprite_canvas(
                    sprite_tiles,
                    (sprite_directory / TITLE_FACE_FRAMES[0][0]).read_bytes(),
                    face_palette,
                    origin_x=TITLE_SONIC_X,
                    origin_y=TITLE_SONIC_Y,
                ),
            )
        )
    write_png_rgba(prompt_preview_path, TITLE_WIDTH, TITLE_HEIGHT, bytes(preview_rgba))

    return {
        "title": str(title_path),
        "plane2": str(plane2_path),
        "plane1": str(plane1_path),
        "press_prompt": str(prompt_path),
        "title_with_prompt": str(prompt_preview_path),
        "sonic_frames": sonic_frame_paths,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", nargs="?", type=Path, default=None)
    parser.add_argument("--reference", type=Path, default=DEFAULT_REFERENCE)
    parser.add_argument("--output", type=Path, default=Path("out/title"))
    parser.add_argument("--all-variants", action="store_true")
    args = parser.parse_args()

    rom_path = args.rom or rominfo.discover_rom(rominfo.DEFAULT_ROM_DIRECTORY)
    rom = rom_path.read_bytes()
    info = rominfo.inspect_rom(rom_path)
    manifest = json.loads(rominfo.DEFAULT_MANIFEST.read_text(encoding="utf-8"))
    mismatches = rominfo.compare_manifest(info, manifest)
    if mismatches:
        raise SystemExit(f"unsupported ROM: {mismatches}")

    art = args.reference / "art"
    tile_data = (art / "data_08BD98.til").read_bytes()
    sprite_tiles = rom[SPRITE_TILES_OFFSET : SPRITE_TILES_OFFSET + SPRITE_TILES_SIZE]
    map1 = (art / "data_08F5C8.map").read_bytes()
    map2 = (art / "data_08F2CC.map").read_bytes()
    prompt_map = (art / "data_08F8C4.map").read_bytes()
    sprite_directory = args.reference / "sprites"
    palette_collection = rom[
        PALETTE_COLLECTION_OFFSET : PALETTE_COLLECTION_OFFSET + PALETTE_COLLECTION_SIZE
    ]
    # SetPaletteList stores the lower 10 bits as the palette collection ID.
    # The upper bits select the plane-palette object slot.
    plane1_palettes, plane2_palettes, palette_ids = title_palette_banks(palette_collection)
    background = background_color(palette_collection, TITLE_BACKDROP_PALETTE)

    args.output.mkdir(parents=True, exist_ok=True)
    variants = [(4, 9)]
    if args.all_variants:
        variants = [(skip, shift) for skip in (0, 2, 4) for shift in (8, 9, 10)]

    layer_paths = render_layers(
        args.output,
        tile_data,
        sprite_tiles,
        map1,
        map2,
        prompt_map,
        sprite_directory,
        palette_collection,
        plane1_palettes,
        plane2_palettes,
        background,
        skip_header=4,
        palette_shift=9,
    )

    rendered = [
        str(render_variant(
            args.output,
            tile_data,
            map1,
            map2,
            plane1_palettes,
            plane2_palettes,
            background,
            skip_header=skip,
            palette_shift=shift,
        ))
        for skip, shift in variants
    ]
    title_path = args.output / "title.png"
    if not args.all_variants:
        rendered.append(str(title_path))
    else:
        rendered.append(str(title_path))

    manifest = {
        "rom": {
            "path": str(rom_path),
            "sha256": info["hashes"]["sha256"],
            "title": info["header"]["title"],
        },
        "source": {
            "tiles": "TitleScr_Tiles / art/data_08BD98.til",
            "map1": "TitleScr_TMap1 / art/data_08F5C8.map",
            "map2": "TitleScr_TMap2 / art/data_08F2CC.map",
            "prompt": "TMap_28F8C4 / art/data_08F8C4.map",
            "sonic_sprites": "Spr_0536..Spr_053D / sprites/*.spr",
            "palette_words": [f"0x{word:04X}" for word in TITLE_PALETTE_WORDS],
            "palette_ids": palette_ids,
            "background_palette": f"0x{TITLE_BACKDROP_PALETTE:03X}",
        },
        "layers": {
            **layer_paths,
            "press_prompt_destination": {"x": PROMPT_DEST_X, "y": PROMPT_DEST_Y},
            "press_prompt_blink_frames": 10,
        },
        "rendered": rendered,
        "sha256": {
            "tiles": sha256(tile_data),
            "map1": sha256(map1),
            "map2": sha256(map2),
            "prompt": sha256(prompt_map),
        },
    }
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    for path in rendered:
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
