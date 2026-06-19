# Getting started

## Reference data

Put a legally obtained cartridge dump in `Rom/`. The directory is ignored by
Git. Verify it before doing any analysis:

```powershell
python tools/rominfo.py
```

The command must end with `Verification: PASS`. The accepted hashes and header
values live in `config/rom.json`, making every address in this project relative
to one immutable reference image.

Run the tooling tests with:

```powershell
python -m unittest discover -s tests -v
```

## Extract the first stage

The first ROM-driven project milestone extracts Neo South Island Act 1:

```powershell
python tools/extract_level.py
```

The command verifies the ROM before reading it, then writes these generated
files under the ignored `out/nsi1/` directory:

- `stage.png`: the two hardware planes composited into a full-stage reference
- `plane1.png` and `plane2.png`: separate 6400x992-pixel hardware planes
- `collision.png`: color-coded path-1 collision tile values
- `collision-mask.bin`: byte-per-pixel native prototype collision mask
- `sonic-idle.png` and `sonic/*.png`: Sonic's ROM-extracted two-palette
  prototype animation frames
- `objects.json`: the object screen table and decoded 10-byte placements
- `manifest.json`: ROM hashes, offsets, palette IDs, and output metadata
- `data/`: exact raw level segments extracted from the user's ROM

If the ignored ValleyBell reference bundle is present in its standard local
directory, every extracted segment is also compared byte-for-byte with the
reference binaries. No ROM data or generated assets are committed.

## Static analysis

The preferred long-term environment is
[Ghidra](https://github.com/NationalSecurityAgency/ghidra) with these extensions:

- [TLCS-900/H processor module](https://github.com/nevesnunes/ghidra-tlcs900h)
- [Neo Geo Pocket loader](https://github.com/nevesnunes/ghidra-neogeopocket-loader)

The loader applies the cartridge mapping and labels known BIOS functions and
I/O ports. The processor module provides both disassembly and developing p-code
semantics, which is considerably more useful than a flat assembly listing.

The pinned Windows toolchain is recorded in `config/toolchain.json`. A local
portable installation belongs under the ignored `Tools/` directory and can be
started with:

```powershell
.\scripts\launch-ghidra.ps1
```

The NGPC loader predates Ghidra 12's simplified loader API. Apply
`patches/ghidra-neogeopocket-loader-ghidra12.patch` before building it against
the pinned Ghidra release.

For a quick entry-point listing, build
[NGDis](https://github.com/jounikor/ngdis) locally and run:

```powershell
.\scripts\disassemble-entry.ps1 -Disassembler C:\path\to\ngd.exe
```

When building NGDis with 64-bit MSVC, first apply
`patches/ngdis-msvc.patch`; it adds the missing `stdlib.h` declaration needed
to keep `malloc` from being treated as a 32-bit integer.

Generated listings go under `out/` and are intentionally ignored. Commit only
human-authored symbols, notes, and reimplementation source—not ROMs or bulk generated
assembly.

After updating `analysis/symbols.csv`, close the Ghidra GUI and apply the names
to the local project with:

```powershell
.\scripts\apply-ghidra-symbols.ps1
```

The CSV `kind` column accepts `function` or `data`. Function rows are
disassembled and defined before naming, which lets indirect task/state entry
points become available to the headless decompiler.

Selected functions can be decompiled headlessly without opening the GUI:

```powershell
.\scripts\decompile-ghidra-targets.ps1 `
    -Addresses @('0x2000A0', '0x3F18FB') `
    -Output .\out\frame-routines.c
```

Addresses should be quoted so PowerShell passes the hexadecimal text through
unchanged. The generated C is evidence for analysis, not native-port source,
and stays under the ignored `out/` directory.

Ghidra references and defined listing ranges can also be exported headlessly:

```powershell
.\scripts\find-ghidra-references.ps1 -Addresses @('0x6F82')
.\scripts\export-ghidra-listing.ps1 -Ranges @('0x23C340', '0x23C3A0')
```

## First analysis targets

Startup and the VBlank handler are now anchored; see
[`analysis/vblank.md`](../analysis/vblank.md). The next targets are:

1. Locate and label the controller-status reads.
2. Identify the main mode/state dispatcher called by the frame loop.
3. Trace camera coordinates into the VBlank scroll and tile-map uploads.
4. Map the object update loop and Sonic's player-state structure.
5. Record every supported address in `analysis/symbols.csv`.
