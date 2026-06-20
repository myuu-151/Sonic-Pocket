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
        tile_id = entry & 0x01FF
        palette_slot = (entry >> palette_shift) & 0x0F
        flip_y = (entry & 0x4000) != 0
        flip_x = (entry & 0x8000) != 0
        tile = tile_cache.get(tile_id)
        if tile is None:
            tile = decode_tile(tile_data, tile_id)
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


def render_variant(
    output: Path,
    tile_data: bytes,
    map1: bytes,
    map2: bytes,
    palettes: list[list[tuple[int, int, int]]],
    *,
    skip_header: int,
    palette_shift: int,
) -> Path:
    plane2, plane2_mask = render_tilemap(
        tile_data,
        map2,
        palettes,
        width_tiles=TITLE_WIDTH_TILES,
        height_tiles=TITLE_HEIGHT_TILES,
        skip_header=skip_header,
        palette_shift=palette_shift,
        transparent_zero=False,
    )
    plane1, plane1_mask = render_tilemap(
        tile_data,
        map1,
        palettes,
        width_tiles=TITLE_WIDTH_TILES,
        height_tiles=TITLE_HEIGHT_TILES,
        skip_header=skip_header,
        palette_shift=palette_shift,
        transparent_zero=True,
    )
    backdrop = solid_rgb(TITLE_WIDTH, TITLE_HEIGHT, palettes[0][0])
    composite = composite_rgb(composite_rgb(backdrop, plane2, plane2_mask), plane1, plane1_mask)
    path = output / f"title_skip{skip_header}_pal{palette_shift}.png"
    write_png(path, TITLE_WIDTH, TITLE_HEIGHT, composite)
    return path


def render_layers(
    output: Path,
    tile_data: bytes,
    map1: bytes,
    map2: bytes,
    prompt_map: bytes,
    palettes: list[list[tuple[int, int, int]]],
    *,
    skip_header: int,
    palette_shift: int,
) -> dict[str, str]:
    plane2, plane2_mask = render_tilemap(
        tile_data,
        map2,
        palettes,
        width_tiles=TITLE_WIDTH_TILES,
        height_tiles=TITLE_HEIGHT_TILES,
        skip_header=skip_header,
        palette_shift=palette_shift,
        transparent_zero=False,
    )
    plane1, plane1_mask = render_tilemap(
        tile_data,
        map1,
        palettes,
        width_tiles=TITLE_WIDTH_TILES,
        height_tiles=TITLE_HEIGHT_TILES,
        skip_header=skip_header,
        palette_shift=palette_shift,
        transparent_zero=True,
    )
    prompt, prompt_mask = render_tilemap(
        tile_data,
        prompt_map,
        palettes,
        width_tiles=PROMPT_WIDTH_TILES,
        height_tiles=PROMPT_HEIGHT_TILES,
        skip_header=skip_header,
        palette_shift=palette_shift,
        transparent_zero=True,
    )

    backdrop = solid_rgb(TITLE_WIDTH, TITLE_HEIGHT, palettes[0][0])
    composite = composite_rgb(composite_rgb(backdrop, plane2, plane2_mask), plane1, plane1_mask)
    title_path = output / "title.png"
    plane2_path = output / "plane2.png"
    plane1_path = output / "plane1.png"
    prompt_path = output / "press_prompt.png"
    prompt_preview_path = output / "title_with_prompt.png"
    write_png(plane2_path, TITLE_WIDTH, TITLE_HEIGHT, plane2)
    write_png_rgba(plane1_path, TITLE_WIDTH, TITLE_HEIGHT, rgba_from_rgb_mask(plane1, plane1_mask))
    write_png(title_path, TITLE_WIDTH, TITLE_HEIGHT, composite)
    write_png_rgba(
        prompt_path,
        PROMPT_WIDTH_TILES * 8,
        PROMPT_HEIGHT_TILES * 8,
        rgba_from_rgb_mask(prompt, prompt_mask),
    )

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
    write_png(prompt_preview_path, TITLE_WIDTH, TITLE_HEIGHT, bytes(preview))

    return {
        "title": str(title_path),
        "plane2": str(plane2_path),
        "plane1": str(plane1_path),
        "press_prompt": str(prompt_path),
        "title_with_prompt": str(prompt_preview_path),
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
    map1 = (art / "data_08F5C8.map").read_bytes()
    map2 = (art / "data_08F2CC.map").read_bytes()
    prompt_map = (art / "data_08F8C4.map").read_bytes()
    palette_collection = rom[
        PALETTE_COLLECTION_OFFSET : PALETTE_COLLECTION_OFFSET + PALETTE_COLLECTION_SIZE
    ]
    palette_ids = [word & 0xFF for word in TITLE_PALETTE_WORDS]
    palettes = build_palettes(palette_collection, palette_ids)

    args.output.mkdir(parents=True, exist_ok=True)
    variants = [(4, 9)]
    if args.all_variants:
        variants = [(skip, shift) for skip in (0, 2, 4) for shift in (8, 9, 10)]

    layer_paths = render_layers(
        args.output,
        tile_data,
        map1,
        map2,
        prompt_map,
        palettes,
        skip_header=4,
        palette_shift=9,
    )

    rendered = [
        str(render_variant(
            args.output,
            tile_data,
            map1,
            map2,
            palettes,
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
            "palette_words": [f"0x{word:04X}" for word in TITLE_PALETTE_WORDS],
            "palette_ids": palette_ids,
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
