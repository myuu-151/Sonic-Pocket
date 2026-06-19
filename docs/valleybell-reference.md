# ValleyBell SPA disassembly reference

The local, ignored `SonicPocketAdventure_disasm+tools/` directory contains
ValleyBell's 2023 release of research performed mainly from 2014 through 2016.
It is an exceptionally useful reference, but it is not part of this repository.

## Useful contents

- `disassembly/spa.asm`: a roughly two-million-line named TLCS-900/H
  disassembly with RAM labels and cross-references.
- `disassembly/Levels.asm` and `level/`: complete level, collision, palette,
  block, and object-placement extraction.
- `disassembly/Sprites.asm`, `sprites/`, and `art/`: sprite layouts, tile data,
  palettes, and tile maps.
- `other-material/Objects.txt`: object IDs, animation data, hitboxes, palettes,
  and descriptive names.
- `tools/`: GPLv2 analysis utilities for detecting/dumping animations,
  hitboxes, tile data, tile maps, objects, and sound banks.
- `SonLVL/`: an SPA-specific level editor and matching project files.
- `sound-driver/`: Z80 driver disassembly, RAM/sequence documentation, a C
  translation, sequence packer, and standalone playback work.

The disassembly states that the original game uses no compression. Most assets
are directly addressable ROM data, which simplifies a user-ROM asset extractor
and makes the existing split tables especially valuable.

## Project policy

The bundle contains extracted proprietary game data and does not present one
clear license covering the entire release. It therefore stays ignored and
must not be copied wholesale into this GitHub repository.

We may use it as a public reverse-engineering reference, independently verify
important behavior against the owned ROM and runtime traces, and record concise
facts, addresses, formats, and our own implementation. Code from individual
tools should only be reused when its GPLv2 terms and project-license impact are
acceptable.

## Immediate impact

This reference changes the most efficient path forward:

1. Import high-value labels and RAM names into the Ghidra symbol database.
2. Use the documented object list and level layouts to specify ROM-driven
   extractors rather than rediscovering every table.
3. Use SonLVL and the extracted level files to validate maps, collision, and
   object placement.
4. Use the sound format documentation and C driver as behavioral references
   for native audio.
5. Retain BizHawk traces for timing, regression, and verification rather than
   primary symbol discovery.
