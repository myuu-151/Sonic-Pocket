# VBlank handler

The cartridge's initial vector table installs `0x2000A0` in the vertical-blanking
interrupt slot at RAM `0x6FCC`. This makes `vblank_handler` one of the first firm
anchors for mapping the game's frame loop.

## Confirmed structure

The handler saves all seven extended general-purpose registers, performs its
per-frame work, restores the registers, and returns with `RETI`.

| Order | Address | Working name | Observed role |
| ---: | ---: | --- | --- |
| 1 | `0x3A59C4` | `vblank_restart_timer0_if_enabled` | Restarts Timer 0 when RAM flag `0x68E8` is nonzero. |
| 2 | `0x23C583` | `bios_com_off_rts` | Calls BIOS vector `0x16`, `VEC_COMOFFRTS`, when BIOS services are available. |
| 3 | `0x3F18FB` | `service_z80_command_queue` | Moves queued commands through the TLCS-900H/Z80 communication interface. |
| 4 | `0x23E86A` | `upload_window_registers` | Conditionally writes cached window coordinates to K2GE registers. |
| 5 | `0x23BFE2` | `upload_sprite_state_to_vram` | Uploads sprite patterns, attributes, and optional palette numbers. |
| 6 | `0x2911B9` | unnamed | Updates Scroll Plane 2 registers; exact higher-level purpose remains open. |
| 7 | `0x23E17D` | `upload_palettes_if_dirty` | Uploads dirty background and palette state. |
| 8 | `0x3C9370` | unnamed | Performs conditional tile-map copies; exact ownership remains open. |
| 9 | `0x23E9A1` | unnamed | Processes a counted batch through two subordinate routines. |
| 10 | RAM `0x4006` | frame callback | Calls an optional callback when the pointer is nonzero. |
| 11 | `0x23C572` | `bios_com_on_rts` | Calls BIOS vector `0x15`, `VEC_COMONRTS`, when BIOS services are available. |

The handler also advances counters at RAM `0x400A` and `0x400C`. The BIOS
COM-off/COM-on calls bracket most of the interrupt work, which is consistent with
temporarily protecting a communication-sensitive critical section.

## Confidence convention

`confirmed` means an address or behavior follows directly from a vector, hardware
register access, or other unambiguous data. `probable` names describe behavior
visible in disassembly but may be refined as callers and data structures become
clearer. Unnamed calls stay unnamed until there is enough evidence to avoid
baking guesses into the symbol database.
