# Neo Geo Pocket Color memory map

The game runs on a Toshiba TLCS-900/H main CPU. Audio is assisted by a Z80 and
a T6W28-compatible PSG. The color display controller is the K2GE.

## TLCS-900/H address space

| Range | Purpose |
| --- | --- |
| `0x000080-0x0000BF` | Console I/O registers |
| `0x004000-0x006FFF` | Main work RAM |
| `0x007000-0x007FFF` | RAM shared with the Z80 |
| `0x008000-0x00BFFF` | K2GE graphics/LCD controller |
| `0x200000-0x3FFFFF` | Cartridge area 1 |
| `0x800000-0x9FFFFF` | Cartridge area 2 |
| `0xFF0000-0xFFFFFF` | System ROM/BIOS |

The two-megabyte reference image occupies cartridge area 1. Its header points
to `0x200040`, which is file offset `0x40`.

## Z80 address space

| Range | Purpose |
| --- | --- |
| `0x0000-0x0FFF` | Shared sound RAM |
| `0x4000-0x4001` | T6W28 left/right writes |
| `0x8000` | Main/sound CPU communication |
| `0xC000` | Signal interrupt to main CPU |

## Timing baseline

- Main CPU clock: 6.144 MHz
- Z80/PSG clock: 3.072 MHz
- Native display: 160 by 152 pixels
- VBlank enters TLCS-900/H interrupt input INT4

Source: MAME's BSD-licensed
[Neo Geo Pocket driver](https://github.com/mamedev/mame/blob/master/src/mame/snk/ngp.cpp)
and [K1GE/K2GE implementation](https://github.com/mamedev/mame/blob/master/src/mame/snk/k1ge.cpp).
