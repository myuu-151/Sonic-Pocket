# Capturing a player runtime trace

The BizHawk trace script records the player task once per emulated frame. It
does not modify emulated memory or require a debugger.

## Start the trace

1. Launch BizHawk and load the verified ROM.
2. Play to Neo South Island Act 1 and stop on the first flat section.
3. Open **Tools > Lua Console**.
4. Open or drag in `scripts/bizhawk-player-trace.lua`.
5. Confirm that a small `TRACE` overlay appears in the game window.

The script normally writes `out/player-runtime-trace.csv`. The Lua Console
prints the exact path it successfully opened, along with the selected
main-memory domain and address adjustment. As a final fallback it writes
`player-runtime-trace.csv` in BizHawk's working directory. If the script
reports an address-range error, save the Lua Console text; that identifies the
memory-domain mismatch we need to correct.

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
| Plane 2 camera world origin | `0x506C` and `0x506E` |
| Current player screen position | `0x67A4` and `0x67A6` |
| Target player screen position | `0x67B8` and `0x67BA` |
| Camera stage bounds | `0x507A` through `0x5080` |

Once captured, the first analysis pass should locate frames where the task
state changes, where the angle leaves zero on a slope, where airborne flag bit
1 changes, and where the vertical radius changes between 13 and 10. Camera
captures should additionally compare `player X - camera X` and
`player Y - camera Y` against the follow-offset columns.

## Analyze the trace

Use the trace analyzer to find the runtime windows that should drive native
porting:

```powershell
python tools\analyze_player_trace.py out\player-runtime-trace.csv --max-events 80
python tools\analyze_player_trace.py out\player-runtime-trace.csv --window 3200 --radius 24
```

The analyzer is intentionally not a physics model. It only reports ROM state,
input, slope, skid/reversal, and camera evidence from the CSV. Any movement or
camera implementation change should be compared against these values before it
is treated as correct.

The current older `out/player-runtime-trace.csv` contains slope and
skid/reversal evidence, but it was captured before camera columns were added to
the Lua script. Capture a fresh camera trace before implementing camera follow.

## Capture animation and sprite parity

When movement looks close but sprites are resetting, hanging, or using the
wrong skid/peelout frame range, capture the wider animation trace:

1. Enter Neo South Island Act 1 on flat ground.
2. Open **Tools > Lua Console**.
3. Open or drag in `scripts/bizhawk-animation-trace.lua`.
4. Stand still for roughly one second.
5. Hold Right until Sonic reaches full run speed.
6. Keep holding Right briefly, then hold Left through the skid and reversal.
7. Repeat once in the other direction if possible, then stop the script.

The script writes `out/player-animation-trace.csv`. It includes the normal
movement fields, the raw player task bytes from `0x6708`, animation byte
`0x6751` (`XIZ+0x49`), and the live sprite-object list rooted at `0x49FA`.

Known ROM evidence from the disassembly:

| Behavior | Evidence |
| --- | --- |
| Skid animation script | `PAniScr_3988DD` |
| Skid mode byte | writes `XIZ+0x49 = 0x06` |
| Skid player sprites | `000C` for 20 frames, then `000D` for 3 frames |
| Sprite object list | head at `0x49FA`, next free at `0x49F8`, entries are 12 bytes |

Use this trace before changing animation timing or effect spawning. If the
viewer does not follow the same `anim_mode_49` changes and sprite-list changes,
the bug is in our state mapping rather than the extracted art.
