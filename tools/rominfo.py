#!/usr/bin/env python3
"""Inspect and verify a Neo Geo Pocket cartridge image."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import sys
from pathlib import Path
from typing import Any, BinaryIO


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = PROJECT_ROOT / "config" / "rom.json"
DEFAULT_ROM_DIRECTORY = PROJECT_ROOT / "Rom"
ROM_EXTENSIONS = {".bin", ".ngc", ".ngp", ".npc"}
HEADER_SIZE = 0x30


def _decode_ascii(value: bytes) -> str:
    return value.split(b"\0", 1)[0].decode("ascii", errors="replace").rstrip()


def parse_header(header: bytes) -> dict[str, Any]:
    """Parse the 0x30-byte Neo Geo Pocket cartridge header."""
    if len(header) < HEADER_SIZE:
        raise ValueError(
            f"ROM is too small for an NGP header: {len(header)} < {HEADER_SIZE} bytes"
        )

    return {
        "copyright": _decode_ascii(header[0x00:0x1C]),
        "entry_point": f"0x{int.from_bytes(header[0x1C:0x20], 'little'):08x}",
        "catalog": f"0x{int.from_bytes(header[0x20:0x22], 'little'):04x}",
        "subcatalog": f"0x{header[0x22]:02x}",
        "mode": f"0x{header[0x23]:02x}",
        "title": _decode_ascii(header[0x24:0x30]),
    }


def hash_stream(stream: BinaryIO) -> tuple[int, dict[str, str], bytes]:
    """Hash a stream and retain only the cartridge header."""
    digesters = {
        "md5": hashlib.md5(usedforsecurity=False),
        "sha1": hashlib.sha1(usedforsecurity=False),
        "sha256": hashlib.sha256(),
    }
    crc32 = 0
    size = 0
    header = bytearray()

    while chunk := stream.read(1024 * 1024):
        if len(header) < HEADER_SIZE:
            needed = HEADER_SIZE - len(header)
            header.extend(chunk[:needed])
        size += len(chunk)
        crc32 = binascii.crc32(chunk, crc32)
        for digester in digesters.values():
            digester.update(chunk)

    hashes = {name: digester.hexdigest() for name, digester in digesters.items()}
    hashes["crc32"] = f"{crc32 & 0xFFFFFFFF:08x}"
    return size, hashes, bytes(header)


def inspect_rom(path: Path) -> dict[str, Any]:
    with path.open("rb") as stream:
        size, hashes, header = hash_stream(stream)
    return {
        "path": str(path.resolve()),
        "size": size,
        "hashes": hashes,
        "header": parse_header(header),
    }


def discover_rom(directory: Path) -> Path:
    candidates = sorted(
        path
        for path in directory.iterdir()
        if path.is_file() and path.suffix.lower() in ROM_EXTENSIONS
    ) if directory.is_dir() else []

    if not candidates:
        raise FileNotFoundError(f"no ROM image found in {directory}")
    if len(candidates) != 1:
        names = ", ".join(path.name for path in candidates)
        raise ValueError(f"expected one ROM image in {directory}, found: {names}")
    return candidates[0]


def compare_manifest(info: dict[str, Any], manifest: dict[str, Any]) -> list[str]:
    mismatches: list[str] = []
    if info["size"] != manifest["size"]:
        mismatches.append(f"size: expected {manifest['size']}, got {info['size']}")

    for group in ("hashes", "header"):
        for key, expected in manifest[group].items():
            actual = info[group].get(key)
            if isinstance(actual, str) and isinstance(expected, str):
                matches = actual.casefold() == expected.casefold()
            else:
                matches = actual == expected
            if not matches:
                mismatches.append(f"{group}.{key}: expected {expected}, got {actual}")
    return mismatches


def format_report(info: dict[str, Any], game: str, mismatches: list[str]) -> str:
    hashes = info["hashes"]
    header = info["header"]
    lines = [
        f"ROM: {info['path']}",
        f"Game: {game}",
        f"Size: {info['size']} bytes",
        f"CRC32: {hashes['crc32']}",
        f"MD5: {hashes['md5']}",
        f"SHA-1: {hashes['sha1']}",
        f"SHA-256: {hashes['sha256']}",
        f"Title: {header['title']}",
        f"Entry point: {header['entry_point']}",
        f"Catalog: {header['catalog']} / {header['subcatalog']}",
        f"Mode: {header['mode']}",
        "Verification: " + ("PASS" if not mismatches else "FAIL"),
    ]
    lines.extend(f"  - {mismatch}" for mismatch in mismatches)
    return "\n".join(lines)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "rom",
        nargs="?",
        type=Path,
        help="ROM path; defaults to the sole image in the local Rom directory",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST,
        help="reference manifest to verify against",
    )
    parser.add_argument("--json", action="store_true", help="print machine-readable JSON")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        rom_path = args.rom or discover_rom(DEFAULT_ROM_DIRECTORY)
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        info = inspect_rom(rom_path)
        mismatches = compare_manifest(info, manifest)
    except (FileNotFoundError, OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    if args.json:
        result = {
            "game": manifest["game"],
            **info,
            "matches_manifest": not mismatches,
            "mismatches": mismatches,
        }
        print(json.dumps(result, indent=2))
    else:
        print(format_report(info, manifest["game"], mismatches))
    return 0 if not mismatches else 1


if __name__ == "__main__":
    raise SystemExit(main())
