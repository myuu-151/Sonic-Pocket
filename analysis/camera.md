# Camera system

The first static camera pass identifies the world-space origin, stage bounds,
and player follow offsets. A directed BizHawk capture is still needed to
confirm the remaining vertical modes and facing-dependent horizontal target.

## Camera state

| RAM address | Working name | Meaning |
| ---: | --- | --- |
| `0x506C` | `camera_x` | Horizontal world-space origin used by object transforms and tile-map upload. |
| `0x506E` | `camera_y` | Vertical world-space origin used by object transforms and tile-map upload. |
| `0x507A` | `camera_min_x` | Lower horizontal stage bound. |
| `0x507C` | `camera_max_x` | Upper horizontal stage bound. |
| `0x507E` | `camera_min_y` | Lower vertical stage bound. |
| `0x5080` | `camera_max_y` | Upper vertical stage bound. |
| `0x67A4` | `camera_follow_x_offset` | Player horizontal position within the viewport. |
| `0x67A6` | `camera_follow_y_offset` | Player vertical position within the viewport. |
| `0x67BA` | `camera_follow_y_target` | Desired vertical viewport offset. |

`initialize_camera_horizontal_follow` at `0x39D0D5` directly establishes:

```text
camera_follow_x_offset = player_x - camera_x
```

The render and map-upload paths both read `camera_x` and `camera_y`, while
player/object transforms subtract those values from world coordinates.

## Horizontal following

`update_player_camera_follow` at `0x39C9D9` selects a horizontal target of
either 48 or 112 pixels. The choice depends on player movement/facing state;
the exact polarity remains to be runtime-confirmed. The current follow offset
moves toward the selected target by no more than two pixels per player tick.

The routine derives a camera correction from the player world X and the
current follow offset, then clamps the resulting origin to
`camera_min_x..camera_max_x`. Another flag prevents backward camera movement
under normal stage conditions.

This is a look-ahead camera rather than a permanently centered camera: Sonic
can occupy either side of the 160-pixel viewport, and the framing target
slides gradually rather than snapping.

## Vertical following

`apply_vertical_camera_follow` at `0x39C960` compares player Y with
`camera_follow_y_offset`, applies the compact-shape three-pixel adjustment,
and clamps the resulting camera correction to
`camera_min_y..camera_max_y`.

The vertical follow offset itself is constrained to 8 through 136 pixels.
`update_player_camera_follow` moves it toward `camera_follow_y_target` by no
more than four pixels per player tick. Static code also includes conditions
that suppress downward or backward correction; these likely cover grounded
camera stability, death, scripted movement, and stage-specific locks.

## Runtime validation

The BizHawk player tracer now records all camera fields. A useful directed
capture should:

1. Stand still, then run right at full speed for several seconds.
2. Reverse and run left for several seconds.
3. Jump at the bottom of a tall arc or use a vertical spring.
4. Continue until the camera reaches a stage boundary.

The capture will establish which state selects horizontal targets 48 and 112,
when vertical tracking begins, and whether camera values update on the same
30 Hz player cadence.
