#!/usr/bin/env python3
"""Compare generated title frames against BizHawk teacher screenshots.

The goal is to stop judging title/intro parity by eye.  Feed this a folder of
BizHawk screenshots captured from the ROM and it reports the nearest generated
frame plus pixel-error metrics.  It intentionally uses only the Python standard
library so it can run in the repo without installing Pillow.
"""

from __future__ import annotations

import argparse
import csv
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Image:
    width: int
    height: int
    rgba: bytes


def read_png(path: Path) -> Image:
    data = path.read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError(f"{path} is not a PNG")
    offset = 8
    width = height = color_type = None
    compressed = bytearray()
    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError(f"{path} has a truncated PNG chunk")
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        kind = data[offset + 4 : offset + 8]
        payload = data[offset + 8 : offset + 8 + length]
        offset += 12 + length
        if kind == b"IHDR":
            width, height, bit_depth, color_type, compression, filter_method, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
            if bit_depth != 8 or compression != 0 or filter_method != 0 or interlace != 0:
                raise ValueError(f"{path} uses unsupported PNG settings")
            if color_type not in (2, 6):
                raise ValueError(f"{path} uses unsupported PNG colour type {color_type}")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
    if width is None or height is None or color_type is None:
        raise ValueError(f"{path} is missing IHDR")
    channels = 4 if color_type == 6 else 3
    raw = zlib.decompress(bytes(compressed))
    stride = width * channels
    rows: list[bytearray] = []
    cursor = 0
    for _ in range(height):
        filter_type = raw[cursor]
        cursor += 1
        row = bytearray(raw[cursor : cursor + stride])
        cursor += stride
        previous = rows[-1] if rows else bytearray(stride)
        recon = bytearray(stride)
        for index, value in enumerate(row):
            left = recon[index - channels] if index >= channels else 0
            up = previous[index]
            up_left = previous[index - channels] if index >= channels else 0
            if filter_type == 0:
                predictor = 0
            elif filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = up
            elif filter_type == 3:
                predictor = (left + up) // 2
            elif filter_type == 4:
                pa = abs(up - up_left)
                pb = abs(left - up_left)
                pc = abs(left + up - 2 * up_left)
                predictor = left if pa <= pb and pa <= pc else up if pb <= pc else up_left
            else:
                raise ValueError(f"{path} has unsupported PNG filter {filter_type}")
            recon[index] = (value + predictor) & 0xFF
        rows.append(recon)
    rgba = bytearray(width * height * 4)
    for y, row in enumerate(rows):
        for x in range(width):
            source = x * channels
            destination = (y * width + x) * 4
            rgba[destination : destination + 3] = row[source : source + 3]
            rgba[destination + 3] = row[source + 3] if channels == 4 else 255
    return Image(width, height, bytes(rgba))


def resize_nearest(image: Image, width: int, height: int) -> Image:
    if image.width == width and image.height == height:
        return image
    output = bytearray(width * height * 4)
    for y in range(height):
        source_y = y * image.height // height
        for x in range(width):
            source_x = x * image.width // width
            output[(y * width + x) * 4 : (y * width + x) * 4 + 4] = image.rgba[
                (source_y * image.width + source_x) * 4 : (source_y * image.width + source_x) * 4 + 4
            ]
    return Image(width, height, bytes(output))


def diff_score(a: Image, b: Image, sample_step: int) -> tuple[float, float, int]:
    if (a.width, a.height) != (b.width, b.height):
        b = resize_nearest(b, a.width, a.height)
    absolute = 0
    max_channel = 0
    samples = 0
    for y in range(0, a.height, sample_step):
        row = y * a.width * 4
        for x in range(0, a.width, sample_step):
            index = row + x * 4
        # Ignore alpha for screenshots; compare visible RGB only.
            for channel in range(3):
                delta = abs(a.rgba[index + channel] - b.rgba[index + channel])
                absolute += delta
                max_channel = max(max_channel, delta)
            samples += 1
    mean_abs = absolute / (samples * 3)
    normalized = mean_abs / 255.0
    return mean_abs, normalized, max_channel


def pngs(directory: Path) -> list[Path]:
    return sorted(directory.glob("*.png"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("teacher", type=Path, help="BizHawk screenshot directory")
    parser.add_argument(
        "--generated",
        type=Path,
        default=Path("out/title/intro"),
        help="generated title intro frame directory",
    )
    parser.add_argument("--output", type=Path, default=Path("out/title/title-frame-compare.csv"))
    parser.add_argument("--teacher-start", type=int, default=0, help="first teacher PNG index to compare")
    parser.add_argument("--limit", type=int, default=0, help="limit teacher frames; 0 = all")
    parser.add_argument(
        "--sample-step",
        type=int,
        default=2,
        help="compare every Nth pixel horizontally and vertically; 1 = full frame",
    )
    args = parser.parse_args()

    teacher_paths = pngs(args.teacher)
    generated_paths = pngs(args.generated)
    if args.teacher_start > 0:
        teacher_paths = teacher_paths[args.teacher_start :]
    if args.limit > 0:
        teacher_paths = teacher_paths[: args.limit]
    if not teacher_paths:
        raise SystemExit(f"no teacher PNGs found in {args.teacher}")
    if not generated_paths:
        raise SystemExit(f"no generated PNGs found in {args.generated}")

    generated = [(path, read_png(path)) for path in generated_paths]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, str]] = []
    for teacher_index, teacher_path in enumerate(teacher_paths):
        teacher = read_png(teacher_path)
        best: tuple[float, float, int, Path] | None = None
        for generated_path, generated_image in generated:
            mean_abs, normalized, max_channel = diff_score(
                teacher, generated_image, max(1, args.sample_step)
            )
            if best is None or normalized < best[1]:
                best = (mean_abs, normalized, max_channel, generated_path)
        assert best is not None
        rows.append(
            {
                "teacher_index": str(teacher_index),
                "teacher": str(teacher_path),
                "best_generated": str(best[3]),
                "mean_abs_rgb": f"{best[0]:.4f}",
                "normalized_error": f"{best[1]:.6f}",
                "max_channel_delta": str(best[2]),
            }
        )

    with args.output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    worst = max(rows, key=lambda row: float(row["normalized_error"]))
    best_row = min(rows, key=lambda row: float(row["normalized_error"]))
    print(f"Compared {len(rows)} teacher frame(s) against {len(generated)} generated frame(s)")
    print(f"Best:  teacher {best_row['teacher_index']} -> {best_row['best_generated']} error={best_row['normalized_error']}")
    print(f"Worst: teacher {worst['teacher_index']} -> {worst['best_generated']} error={worst['normalized_error']}")
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
