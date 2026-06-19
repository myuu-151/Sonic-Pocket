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

Generated listings go under `out/` and are intentionally ignored. Commit only
human-authored symbols, notes, and clean-room source—not ROMs or bulk generated
assembly.

After updating `analysis/symbols.csv`, close the Ghidra GUI and apply the names
to the local project with:

```powershell
.\scripts\apply-ghidra-symbols.ps1
```

## First analysis targets

1. Confirm startup and work-RAM initialization from `0x200040`.
2. Determine which interrupt reaches the handler at `0x2000A0`.
3. Label the callbacks reached by the startup sequence at `0x200124`.
4. Identify joypad reads, K2GE writes, and frame synchronization.
5. Record every confirmed address in `analysis/symbols.csv`.
