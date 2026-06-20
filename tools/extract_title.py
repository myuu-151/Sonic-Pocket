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
TITLE_INTRO_BACKDROP_PALETTE = 0x003
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
TITLE_MENU_PALETTE = 0x25F
TITLE_MENU_X = 0x28
TITLE_MENU_NO_CONTINUE_START_Y = 0x85
TITLE_MENU_GO_TO_ROOM_Y = 0x8F
TITLE_PRESS_A_X = 0x20
TITLE_PRESS_A_Y = 0x88
TITLE_MENU_CURSOR_SPRITE = "0200_2F5E.spr"
TITLE_PRESS_A_SPRITE = "0532_920E.spr"
TITLE_MENU_START_SPRITE = "0533_9248.spr"
TITLE_MENU_CONTINUE_SPRITE = "0534_925E.spr"
TITLE_MENU_GO_TO_ROOM_SPRITE = "0535_9280.spr"
INTRO_PARTICLE_FRAMES = [
    "000B_02D8.spr",
    "000C_02DE.spr",
    "000D_02E8.spr",
    "000E_02F2.spr",
    "000F_02FC.spr",
    "0010_0306.spr",
]
INTRO_PARTICLE_OBJECTS = [
    {"x": 0x28, "y": -0x2E, "target_y": -0x2E + 0x50},
    {"x": 0x52, "y": -0x3F, "target_y": -0x3F + 0x50},
    {"x": 0x86, "y": -0x3C, "target_y": -0x3C + 0x50},
]
INTRO_LOGO_FACE_PALETTE = 0x25C
INTRO_LOGO_SHADE_PALETTE = 0x25D
INTRO_LOGO_BODY_PALETTE = 0x25B
INTRO_LOGO_FACE_SEQUENCE = [
    ("0719_C45C.spr", 1),
    ("071A_C462.spr", 3),
    ("071B_C468.spr", 3),
    ("071C_C46E.spr", 3),
    ("071D_C478.spr", 3),
    ("071E_C48E.spr", 3),
    ("071F_C4B4.spr", 8),
    ("0720_C4E2.spr", 4),
    ("0721_C508.spr", 2),
]
INTRO_LOGO_SHADE_SEQUENCE = [
    ("0713_C3F8.spr", 1),
    ("0714_C3FE.spr", 3),
    ("0715_C414.spr", 3),
    ("0716_C436.spr", 3),
    ("0717_C440.spr", 3),
    ("0718_C44E.spr", 3),
]
INTRO_LOGO_BODY_SEQUENCE = [
    ("0722_C52E.spr", 2),
    ("0723_C538.spr", 3),
    ("0724_C54A.spr", 3),
    ("0725_C564.spr", 3),
    ("0726_C582.spr", 8),
    ("0727_C5D8.spr", 4),
    ("0728_C626.spr", 2),
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
TITLE_ANIM_PALETTE_WORDS = [
    0x4003,
    0x4403,
    0x4803,
    0x4C03,
    0x5003,
    0x5403,
    0x5A59,
    0x5E5D,
    0x625F,
    0x8003,
    0x8403,
    0x8A58,
    0x8E5A,
    0x925E,
    0xC003,
    0xC403,
    0x4252,
    0x4653,
    0x4A54,
    0x4E55,
    0x5256,
    0x5657,
    0x8250,
    0x8651,
    0xC24F,
]


def title_palette_banks(
    palette_collection: bytes,
    palette_words: list[int] | None = None,
) -> tuple[list[list[tuple[int, int, int]]], list[list[tuple[int, int, int]]], list[int]]:
    if palette_words is None:
        palette_words = TITLE_PALETTE_WORDS
    palette_ids = [word & 0x03FF for word in palette_words]
    decoded = build_palettes(palette_collection, palette_ids)
    empty = [(0, 0, 0)] * 4
    plane1 = [empty for _ in range(16)]
    plane2 = [empty for _ in range(16)]
    for word, palette in zip(palette_words, decoded):
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


def decode_counted_tiles(tile_data: bytes) -> list[tuple[tuple[int, ...], ...]]:
    if len(tile_data) < 2:
        raise ValueError("tile data is missing its count header")
    tile_count = int.from_bytes(tile_data[:2], "little")
    payload = tile_data[2:]
    if len(payload) < tile_count * 16:
        raise ValueError("tile data is shorter than its count header")
    return [decode_tile(payload, tile_id) for tile_id in range(tile_count)]


def copy_tiles_to_patterns(
    patterns: list[tuple[tuple[int, ...], ...] | None],
    tile_data: bytes,
    base_tile: int,
) -> None:
    for index, tile in enumerate(decode_counted_tiles(tile_data)):
        destination = base_tile + index
        if destination >= len(patterns):
            raise ValueError(f"pattern destination {destination} outside table")
        patterns[destination] = tile


def copy_tilemap_to_scroll(
    scroll: list[int],
    map_data: bytes,
    *,
    destination_bytes: int,
    width_tiles: int,
    height_tiles: int,
) -> None:
    source_width = map_data[0]
    entries = unpack_words(map_data[4:])
    destination = destination_bytes // 2
    for row in range(height_tiles):
        for column in range(width_tiles):
            source_index = row * source_width + column
            if source_index >= len(entries):
                raise ValueError("tilemap source is truncated")
            destination_index = ((destination // 32 + row) & 31) * 32 + (
                (destination + column) & 31
            )
            scroll[destination_index] = entries[source_index]


def render_scroll_map(
    scroll: list[int],
    patterns: list[tuple[tuple[int, ...], ...] | None],
    palettes: list[list[tuple[int, int, int]]],
    *,
    camera_x: int = 0,
    camera_y: int = 0,
) -> tuple[bytes, bytes]:
    image = bytearray(TITLE_WIDTH * TITLE_HEIGHT * 3)
    mask = bytearray(TITLE_WIDTH * TITLE_HEIGHT)
    blank_tile = tuple(tuple(0 for _ in range(8)) for _ in range(8))
    for screen_y in range(TITLE_HEIGHT):
        source_y = screen_y - camera_y
        if source_y < 0:
            continue
        row = (source_y // 8) & 31
        tile_y = source_y & 7
        for screen_x in range(TITLE_WIDTH):
            source_x = screen_x - camera_x
            if source_x < 0:
                continue
            column = (source_x // 8) & 31
            tile_x = source_x & 7
            entry = scroll[row * 32 + column]
            tile_id = entry & 0x01FF
            palette_slot = (entry >> 9) & 0x0F
            flip_y = (entry & 0x4000) != 0
            flip_x = (entry & 0x8000) != 0
            tile = patterns[tile_id]
            if tile is None:
                tile = blank_tile
            palette = palettes[palette_slot]
            source_tile_y = 7 - tile_y if flip_y else tile_y
            source_tile_x = 7 - tile_x if flip_x else tile_x
            color_index = tile[source_tile_y][source_tile_x]
            if color_index == 0:
                continue
            destination = screen_y * TITLE_WIDTH + screen_x
            image[destination * 3 : destination * 3 + 3] = bytes(palette[color_index])
            mask[destination] = 255
    return bytes(image), bytes(mask)


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


def rgba_from_rgb(rgb: bytes) -> bytes:
    rgba = bytearray((len(rgb) // 3) * 4)
    for pixel in range(len(rgb) // 3):
        rgba[pixel * 4 : pixel * 4 + 3] = rgb[pixel * 3 : pixel * 3 + 3]
        rgba[pixel * 4 + 3] = 255
    return bytes(rgba)


def expand_timed_sequence(sequence: list[tuple[str, int]]) -> list[str]:
    expanded: list[str] = []
    for name, duration in sequence:
        expanded.extend([name] * duration)
    return expanded


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
        transparent_zero=True,
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
    press_off_path = output / "press_a_button_off.png"
    press_a_path = output / "press_a_button.png"
    menu_path = output / "menu_options.png"
    prompt_preview_path = output / "title_with_prompt.png"
    press_preview_path = output / "title_press_a.png"
    press_off_preview_path = output / "title_press_off.png"
    menu_preview_path = output / "title_menu.png"
    states_dir = output / "states"
    states_dir.mkdir(parents=True, exist_ok=True)
    for stale_state in states_dir.glob("*.png"):
        stale_state.unlink()
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
    menu_palette = build_palettes(palette_collection, [TITLE_MENU_PALETTE])[0]
    prompt_layer = bytearray(TITLE_WIDTH * TITLE_HEIGHT * 4)
    for y in range(PROMPT_HEIGHT_TILES * 8):
        for x in range(PROMPT_WIDTH_TILES * 8):
            source = y * PROMPT_WIDTH_TILES * 8 + x
            if prompt_mask[source] == 0:
                continue
            dst_x = PROMPT_DEST_X + x
            dst_y = PROMPT_DEST_Y + y
            if 0 <= dst_x < TITLE_WIDTH and 0 <= dst_y < TITLE_HEIGHT:
                src_rgb = source * 3
                dst_rgba = (dst_y * TITLE_WIDTH + dst_x) * 4
                prompt_layer[dst_rgba : dst_rgba + 3] = prompt[src_rgb : src_rgb + 3]
                prompt_layer[dst_rgba + 3] = 255
    press_a_sprite = render_title_sprite_canvas(
        sprite_tiles,
        (sprite_directory / TITLE_PRESS_A_SPRITE).read_bytes(),
        menu_palette,
        origin_x=TITLE_PRESS_A_X,
        origin_y=TITLE_PRESS_A_Y,
    )
    press_a = alpha_composite_rgba(bytes(prompt_layer), press_a_sprite)
    press_off = bytes(prompt_layer)
    write_png_rgba(press_off_path, TITLE_WIDTH, TITLE_HEIGHT, press_off)
    write_png_rgba(press_a_path, TITLE_WIDTH, TITLE_HEIGHT, press_a)
    menu = bytearray(TITLE_WIDTH * TITLE_HEIGHT * 4)
    for sprite_name, y in (
        (TITLE_MENU_CURSOR_SPRITE, TITLE_MENU_NO_CONTINUE_START_Y),
        (TITLE_MENU_START_SPRITE, TITLE_MENU_NO_CONTINUE_START_Y),
        (TITLE_MENU_GO_TO_ROOM_SPRITE, TITLE_MENU_GO_TO_ROOM_Y),
    ):
        menu = bytearray(
            alpha_composite_rgba(
                bytes(menu),
                render_title_sprite_canvas(
                    sprite_tiles,
                    (sprite_directory / sprite_name).read_bytes(),
                    menu_palette,
                    origin_x=TITLE_MENU_X,
                    origin_y=y,
                ),
            )
        )
    write_png_rgba(menu_path, TITLE_WIDTH, TITLE_HEIGHT, bytes(menu))
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

    press_preview_rgba = bytearray(alpha_composite_rgba(rgba_from_rgb(composite), press_a))
    press_off_preview_rgba = bytearray(alpha_composite_rgba(rgba_from_rgb(composite), press_off))
    menu_preview_rgba = bytearray(alpha_composite_rgba(rgba_from_rgb(composite), bytes(menu)))
    if sonic_frame_paths:
        press_preview_rgba = bytearray(
            alpha_composite_rgba(
                bytes(press_preview_rgba),
                render_title_sprite_canvas(
                    sprite_tiles,
                    (sprite_directory / TITLE_BODY_FRAMES[0][0]).read_bytes(),
                    body_palette,
                    origin_x=TITLE_SONIC_X,
                    origin_y=TITLE_SONIC_Y,
                ),
            )
        )
        press_off_preview_rgba = bytearray(
            alpha_composite_rgba(
                bytes(press_off_preview_rgba),
                render_title_sprite_canvas(
                    sprite_tiles,
                    (sprite_directory / TITLE_BODY_FRAMES[0][0]).read_bytes(),
                    body_palette,
                    origin_x=TITLE_SONIC_X,
                    origin_y=TITLE_SONIC_Y,
                ),
            )
        )
        press_off_preview_rgba = bytearray(
            alpha_composite_rgba(
                bytes(press_off_preview_rgba),
                render_title_sprite_canvas(
                    sprite_tiles,
                    (sprite_directory / TITLE_FACE_FRAMES[0][0]).read_bytes(),
                    face_palette,
                    origin_x=TITLE_SONIC_X,
                    origin_y=TITLE_SONIC_Y,
                ),
            )
        )
        press_preview_rgba = bytearray(
            alpha_composite_rgba(
                bytes(press_preview_rgba),
                render_title_sprite_canvas(
                    sprite_tiles,
                    (sprite_directory / TITLE_FACE_FRAMES[0][0]).read_bytes(),
                    face_palette,
                    origin_x=TITLE_SONIC_X,
                    origin_y=TITLE_SONIC_Y,
                ),
            )
        )
        menu_preview_rgba = bytearray(
            alpha_composite_rgba(
                bytes(menu_preview_rgba),
                render_title_sprite_canvas(
                    sprite_tiles,
                    (sprite_directory / TITLE_BODY_FRAMES[0][0]).read_bytes(),
                    body_palette,
                    origin_x=TITLE_SONIC_X,
                    origin_y=TITLE_SONIC_Y,
                ),
            )
        )
        menu_preview_rgba = bytearray(
            alpha_composite_rgba(
                bytes(menu_preview_rgba),
                render_title_sprite_canvas(
                    sprite_tiles,
                    (sprite_directory / TITLE_FACE_FRAMES[0][0]).read_bytes(),
                    face_palette,
                    origin_x=TITLE_SONIC_X,
                    origin_y=TITLE_SONIC_Y,
                ),
            )
        )
    write_png_rgba(press_preview_path, TITLE_WIDTH, TITLE_HEIGHT, bytes(press_preview_rgba))
    write_png_rgba(press_off_preview_path, TITLE_WIDTH, TITLE_HEIGHT, bytes(press_off_preview_rgba))
    write_png_rgba(menu_preview_path, TITLE_WIDTH, TITLE_HEIGHT, bytes(menu_preview_rgba))
    write_png_rgba(prompt_preview_path, TITLE_WIDTH, TITLE_HEIGHT, bytes(menu_preview_rgba))
    state_press_off_path = states_dir / "frame_0000_press_off.png"
    state_press_path = states_dir / "frame_0001_press_a.png"
    state_menu_path = states_dir / "frame_0002_menu.png"
    write_png_rgba(state_press_off_path, TITLE_WIDTH, TITLE_HEIGHT, bytes(press_off_preview_rgba))
    write_png_rgba(state_press_path, TITLE_WIDTH, TITLE_HEIGHT, bytes(press_preview_rgba))
    write_png_rgba(state_menu_path, TITLE_WIDTH, TITLE_HEIGHT, bytes(menu_preview_rgba))

    return {
        "title": str(title_path),
        "plane2": str(plane2_path),
        "plane1": str(plane1_path),
        "press_prompt": str(prompt_path),
        "press_a_button_off": str(press_off_path),
        "press_a_button": str(press_a_path),
        "menu_options": str(menu_path),
        "title_with_prompt": str(prompt_preview_path),
        "title_press_a": str(press_preview_path),
        "title_press_off": str(press_off_preview_path),
        "title_menu": str(menu_preview_path),
        "state_frames": [str(state_press_off_path), str(state_press_path), str(state_menu_path)],
        "sonic_frames": sonic_frame_paths,
    }


def render_intro_frames(
    output: Path,
    sprite_directory: Path,
    art_directory: Path,
    title_tile_data: bytes,
    title_map1: bytes,
    title_map2: bytes,
    sprite_tiles: bytes,
    palette_collection: bytes,
    plane1_palettes: list[list[tuple[int, int, int]]],
    plane2_palettes: list[list[tuple[int, int, int]]],
    title_plane1_palettes: list[list[tuple[int, int, int]]],
    title_plane2_palettes: list[list[tuple[int, int, int]]],
    background: tuple[int, int, int],
) -> list[str]:
    intro_dir = output / "intro"
    intro_dir.mkdir(parents=True, exist_ok=True)
    for stale_frame in intro_dir.glob("frame_*.png"):
        stale_frame.unlink()
    teacher_marker = intro_dir / "teacher_capture.txt"
    if teacher_marker.exists():
        teacher_marker.unlink()

    intro_background = background_color(palette_collection, TITLE_INTRO_BACKDROP_PALETTE)
    particle_palette = build_palettes(palette_collection, [0x01B])[0]
    logo_face_palette = build_palettes(palette_collection, [INTRO_LOGO_FACE_PALETTE])[0]
    logo_shade_palette = build_palettes(palette_collection, [INTRO_LOGO_SHADE_PALETTE])[0]
    logo_body_palette = build_palettes(palette_collection, [INTRO_LOGO_BODY_PALETTE])[0]
    title_face_palette = build_palettes(palette_collection, [TITLE_FACE_PALETTE])[0]
    title_body_palette = build_palettes(palette_collection, [TITLE_BODY_PALETTE])[0]
    face_sequence = expand_timed_sequence(INTRO_LOGO_FACE_SEQUENCE)
    shade_sequence = expand_timed_sequence(INTRO_LOGO_SHADE_SEQUENCE)
    body_sequence = expand_timed_sequence(INTRO_LOGO_BODY_SEQUENCE)

    def interpolate_palette(
        source: list[tuple[int, int, int]],
        target: list[tuple[int, int, int]],
        amount: float,
    ) -> list[tuple[int, int, int]]:
        return [
            tuple(round(a + (b - a) * amount) for a, b in zip(source_color, target_color))
            for source_color, target_color in zip(source, target)
        ]

    def render_title_sonic_overlay(face_index: int = 0, body_index: int = 0) -> bytes:
        body_name = TITLE_BODY_FRAMES[body_index % len(TITLE_BODY_FRAMES)][0]
        face_name = TITLE_FACE_FRAMES[face_index % len(TITLE_FACE_FRAMES)][0]
        body = render_title_sprite_canvas(
            sprite_tiles,
            (sprite_directory / body_name).read_bytes(),
            title_body_palette,
            origin_x=TITLE_SONIC_X,
            origin_y=TITLE_SONIC_Y,
        )
        face = render_title_sprite_canvas(
            sprite_tiles,
            (sprite_directory / face_name).read_bytes(),
            title_face_palette,
            origin_x=TITLE_SONIC_X,
            origin_y=TITLE_SONIC_Y,
        )
        return alpha_composite_rgba(body, face)

    def render_intro_logo_background() -> bytes:
        plane2, plane2_mask = render_scroll_map(
            scroll2, patterns, plane2_palettes, camera_y=0
        )
        plane1, plane1_mask = render_scroll_map(
            scroll1, patterns, plane1_palettes, camera_y=0
        )
        rgb = composite_rgb(
            composite_rgb(solid_rgb(TITLE_WIDTH, TITLE_HEIGHT, intro_background), plane2, plane2_mask),
            plane1,
            plane1_mask,
        )
        return rgba_from_rgb(rgb)

    def render_faded_title(amount: float) -> bytes:
        faded_plane1 = [
            interpolate_palette(plane1_palettes[index], title_plane1_palettes[index], amount)
            for index in range(16)
        ]
        faded_plane2 = [
            interpolate_palette(plane2_palettes[index], title_plane2_palettes[index], amount)
            for index in range(16)
        ]
        faded_background = tuple(
            round(a + (b - a) * amount) for a, b in zip(intro_background, background)
        )
        plane2, plane2_mask = render_tilemap(
            title_tile_data,
            title_map2,
            faded_plane2,
            width_tiles=TITLE_WIDTH_TILES,
            height_tiles=TITLE_HEIGHT_TILES,
            skip_header=4,
            palette_shift=9,
            transparent_zero=True,
        )
        plane1, plane1_mask = render_tilemap(
            title_tile_data,
            title_map1,
            faded_plane1,
            width_tiles=TITLE_WIDTH_TILES,
            height_tiles=TITLE_HEIGHT_TILES,
            skip_header=4,
            palette_shift=9,
            transparent_zero=True,
        )
        rgb = composite_rgb(
            composite_rgb(solid_rgb(TITLE_WIDTH, TITLE_HEIGHT, faded_background), plane2, plane2_mask),
            plane1,
            plane1_mask,
        )
        return alpha_composite_rgba(rgba_from_rgb(rgb), render_title_sonic_overlay())

    patterns: list[tuple[tuple[int, ...], ...] | None] = [None] * 512
    scroll1 = [0] * (32 * 32)
    scroll2 = [0] * (32 * 32)
    copy_tiles_to_patterns(patterns, (art_directory / "data_08A3FA.til").read_bytes(), 0)
    copy_tilemap_to_scroll(
        scroll1,
        (art_directory / "data_08EC58.map").read_bytes(),
        destination_bytes=0,
        width_tiles=20,
        height_tiles=19,
    )
    copy_tilemap_to_scroll(
        scroll2,
        (art_directory / "data_08E95C.map").read_bytes(),
        destination_bytes=0,
        width_tiles=20,
        height_tiles=19,
    )
    copy_tiles_to_patterns(patterns, (art_directory / "data_08D75A.til").read_bytes(), 0x83)

    plane1_patch_maps = [
        "data_08FAD4.map",
        "data_08FE4C.map",
        "data_0901C4.map",
        "data_09053C.map",
        "data_0908B4.map",
        "data_090D6C.map",
    ]
    plane2_patch_maps = [
        "data_08F918.map",
        "data_08FC90.map",
        "data_090008.map",
        "data_090380.map",
        "data_0906F8.map",
        "data_090A70.map",
    ]
    logo_tile_patches = [
        ("data_08AC2C.til", 0),
        ("data_08AEBE.til", 0x32),
        ("data_08B1A0.til", 0),
        ("data_08B462.til", 0x32),
        ("data_08B704.til", 0),
        ("data_08BA06.til", 0x32),
    ]
    logo_map_patches = [
        "data_08EF54.map",
        "data_08EFE8.map",
        "data_08F07C.map",
        "data_08F110.map",
        "data_08F1A4.map",
        "data_08F238.map",
    ]

    rendered: list[str] = []
    frame_index = 0
    reveal_patch_index = 0
    logo_patch_index = 0
    logo_started_at: int | None = None
    logo_body_started_at: int | None = None
    camera_scroll_frames = 80
    pre_reveal_wait_frames = 0x1E
    reveal_patch_start = camera_scroll_frames + pre_reveal_wait_frames
    logo_start = reveal_patch_start + len(plane1_patch_maps)
    logo_patch_interval = 3
    # The ROM leaves the animated intro after generated frame 128 in the
    # current teacher capture and hands over to the final title-screen
    # tilemaps/fade state. Keeping later generated intro frames on screen makes
    # Sonic look corrupted because those frames are not the active ROM
    # composition anymore.
    max_frames = logo_start + 13
    for frame in range(max_frames):
        if reveal_patch_start <= frame < logo_start and reveal_patch_index < len(plane1_patch_maps):
            destination_bytes = 0 if reveal_patch_index == 5 else 0x200
            patch_height = 19 if reveal_patch_index == 5 else 11
            copy_tilemap_to_scroll(
                scroll1,
                (art_directory / plane1_patch_maps[reveal_patch_index]).read_bytes(),
                destination_bytes=destination_bytes,
                width_tiles=20,
                height_tiles=patch_height,
            )
            copy_tilemap_to_scroll(
                scroll2,
                (art_directory / plane2_patch_maps[reveal_patch_index]).read_bytes(),
                destination_bytes=destination_bytes,
                width_tiles=20,
                height_tiles=patch_height,
            )
            reveal_patch_index += 1
        if frame == logo_start:
            copy_tiles_to_patterns(
                patterns,
                (art_directory / logo_tile_patches[0][0]).read_bytes(),
                logo_tile_patches[0][1],
            )
            copy_tilemap_to_scroll(
                scroll1,
                (art_directory / logo_map_patches[0]).read_bytes(),
                destination_bytes=0x0A,
                width_tiles=9,
                height_tiles=8,
            )
            logo_started_at = frame
            logo_patch_index = 1
        elif (
            logo_started_at is not None
            and logo_patch_index < len(logo_map_patches)
            and (frame - logo_started_at) % logo_patch_interval == 0
        ):
            copy_tiles_to_patterns(
                patterns,
                (art_directory / logo_tile_patches[logo_patch_index][0]).read_bytes(),
                logo_tile_patches[logo_patch_index][1],
            )
            copy_tilemap_to_scroll(
                scroll1,
                (art_directory / logo_map_patches[logo_patch_index]).read_bytes(),
                destination_bytes=0x0A,
                width_tiles=9,
                height_tiles=8,
            )
            if logo_patch_index == 2:
                logo_body_started_at = frame
            logo_patch_index += 1

        camera_y = min(0, -0x50 + frame) if frame < camera_scroll_frames else 0
        plane2, plane2_mask = render_scroll_map(
            scroll2, patterns, plane2_palettes, camera_y=camera_y
        )
        plane1, plane1_mask = render_scroll_map(
            scroll1, patterns, plane1_palettes, camera_y=camera_y
        )
        rgb = composite_rgb(
            composite_rgb(solid_rgb(TITLE_WIDTH, TITLE_HEIGHT, intro_background), plane2, plane2_mask),
            plane1,
            plane1_mask,
        )
        rgba = rgba_from_rgb(rgb)

        if frame < reveal_patch_start:
            for obj_index, obj in enumerate(INTRO_PARTICLE_OBJECTS):
                y = min(obj["target_y"], obj["y"] + frame)
                particle_name = INTRO_PARTICLE_FRAMES[
                    (frame // 2 + obj_index) % len(INTRO_PARTICLE_FRAMES)
                ]
                rgba = alpha_composite_rgba(
                    rgba,
                    render_title_sprite_canvas(
                        sprite_tiles,
                        (sprite_directory / particle_name).read_bytes(),
                        particle_palette,
                        origin_x=obj["x"],
                        origin_y=y,
                    ),
                )

        if logo_started_at is not None:
            logo_frame = frame - logo_started_at
            if 0 <= logo_frame < len(shade_sequence):
                rgba = alpha_composite_rgba(
                    rgba,
                    render_title_sprite_canvas(
                        sprite_tiles,
                        (sprite_directory / shade_sequence[logo_frame]).read_bytes(),
                        logo_shade_palette,
                        origin_x=TITLE_SONIC_X,
                        origin_y=TITLE_SONIC_Y,
                    ),
                )
            if 0 <= logo_frame < len(face_sequence):
                rgba = alpha_composite_rgba(
                    rgba,
                    render_title_sprite_canvas(
                        sprite_tiles,
                        (sprite_directory / face_sequence[logo_frame]).read_bytes(),
                        logo_face_palette,
                        origin_x=TITLE_SONIC_X,
                        origin_y=TITLE_SONIC_Y,
                    ),
                )
            if logo_body_started_at is not None:
                body_frame = frame - logo_body_started_at
            else:
                body_frame = -1
            if 0 <= body_frame < len(body_sequence):
                rgba = alpha_composite_rgba(
                    rgba,
                    render_title_sprite_canvas(
                        sprite_tiles,
                        (sprite_directory / body_sequence[body_frame]).read_bytes(),
                        logo_body_palette,
                        origin_x=TITLE_SONIC_X,
                        origin_y=TITLE_SONIC_Y,
                    ),
                )

        frame_path = intro_dir / f"frame_{frame_index:04d}.png"
        write_png_rgba(frame_path, TITLE_WIDTH, TITLE_HEIGHT, rgba)
        rendered.append(str(frame_path))
        frame_index += 1

    # After the logo reveal, the ROM swaps from the intro Sonic animation to
    # the normal title Sonic sprites before the final title tilemaps finish
    # fading in. Keeping this as its own phase avoids holding the curled intro
    # Sonic over the title logo during the handoff.
    for transition_frame in range(12):
        rgba = alpha_composite_rgba(
            render_intro_logo_background(),
            render_title_sonic_overlay(face_index=3, body_index=0),
        )
        frame_path = intro_dir / f"frame_{frame_index:04d}.png"
        write_png_rgba(frame_path, TITLE_WIDTH, TITLE_HEIGHT, rgba)
        rendered.append(str(frame_path))
        frame_index += 1

    # loc_3E7CF9 loads the final title tilemaps and starts palette-object
    # fades from the intro palette bank into TitleScr_PalLst. The hardware
    # fades per colour component; linear RGB interpolation is close enough to
    # remove the corrupted handoff while the exact palette-object stepping is
    # still being mapped.
    for fade_frame in range(31):
        amount = min(1.0, (fade_frame + 3) / 30.0)
        frame_path = intro_dir / f"frame_{frame_index:04d}.png"
        write_png_rgba(frame_path, TITLE_WIDTH, TITLE_HEIGHT, render_faded_title(amount))
        rendered.append(str(frame_path))
        frame_index += 1

    return rendered


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
    anim_plane1_palettes, anim_plane2_palettes, anim_palette_ids = title_palette_banks(
        palette_collection, TITLE_ANIM_PALETTE_WORDS
    )
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
        anim_plane1_palettes,
        anim_plane2_palettes,
        background,
        skip_header=4,
        palette_shift=9,
    )
    intro_frames = render_intro_frames(
        args.output,
        sprite_directory,
        art,
        tile_data,
        map1,
        map2,
        sprite_tiles,
        palette_collection,
        anim_plane1_palettes,
        anim_plane2_palettes,
        plane1_palettes,
        plane2_palettes,
        background,
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
            "animation_palette_ids": anim_palette_ids,
            "background_palette": f"0x{TITLE_BACKDROP_PALETTE:03X}",
        },
        "layers": {
            **layer_paths,
            "intro_frames": intro_frames,
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
