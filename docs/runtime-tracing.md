# Capturing a player runtime trace

The BizHawk trace script records the player task once per emulated frame. It
does not modify emulated memory or require a debugger.

## Start the trace

1. Launch BizHawk and load the verified ROM.
2. Play to Neo South Island Act 1 and stop on the first flat section.
3. Open **Tools > Lua Console**.
4. Open or drag in `scripts/bizhawk-player-trace.lua`.
5. Confirm that a small `TRACE` overlay appears in the game window.

The script writes `out/player-runtime-trace.csv`. The Lua Console also prints
the selected main-memory domain and address adjustment. If the script reports
an address-range error, save the Lua Console text; that identifies the memory
domain mismatch we need to correct.

## Perform one short capture

Keep the script running and perform these actions in order:

1. Stand still on flat ground for roughly two seconds.
2. Run right across the first visible slope, then release Right.
3. From flat ground, press A once and allow Sonic to land without steering.
4. Crouch, charge a spindash with three A presses, release Down, and let Sonic
   roll to a stop if the terrain permits.

Stop the Lua script after the final action. The CSV is flushed every frame, so
the captured rows remain valid even if the script is stopped from the console.

For exact input reproduction, record a BizHawk movie at the same time using
**File > Movie > Record Movie**. Start the movie from a save state made on the
flat section, and place the resulting `.bk2` under `out/` alongside the CSV.

## Captured fields

The trace contains controller state and the following player-task fields:

| Field | ROM RAM address |
| --- | ---: |
| Task state function | `0x6708` |
| Task flags | `0x6713` |
| Surface angle | `0x6718` |
| 16.8 X position | `0x6719` |
| Movement flags | `0x671C` |
| 16.8 Y position | `0x671D` |
| Ground speed | `0x6720` |
| X velocity | `0x6722` |
| Y velocity | `0x6724` |
| Collision radii | `0x673E` and `0x673F` |

Once captured, the first analysis pass should locate frames where the task
state changes, where the angle leaves zero on a slope, where airborne flag bit
1 changes, and where the vertical radius changes between 13 and 10.
