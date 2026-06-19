# Player collision pipeline

The player uses paired sensors around an angle-aware collision box. Collision
is not a single axis-aligned test: the current eight-bit `surface_angle`
selects floor, right-wall, ceiling, or left-wall probing, allowing grounded
movement around loops and curved surfaces.

## Bounds and scratch state

The player task stores its horizontal and vertical radii at offsets `+0x36`
and `+0x37`. `load_collision_radii` at `0x39B4C8` copies those bytes to shared
scratch words at RAM `0x668A` and `0x668C` before a collision pass. Standing
uses radii 7 by 13; rolling uses 7 by 10.

The collision code also uses:

| RAM address | Working meaning |
| ---: | --- |
| `0x6688` | Temporary signed sensor offset selected from a collision radius. |
| `0x668A` | Zero-extended horizontal radius for the current object. |
| `0x668C` | Zero-extended vertical radius for the current object. |
| `0x668E` | Absolute horizontal displacement since the saved position. |
| `0x668F` | Absolute vertical displacement since the saved position. |
| `0x6690` | Start of a compact dynamic collision-rectangle table. |
| `0x66FE` | Number of active dynamic collision rectangles. |
| `0x6700`-`0x6706` | Tile-probe coordinates, accumulated distance, limit, and collision plane scratch. |

The values at `0x668E` and `0x668F` are derived from task offsets `+0x38` and
`+0x3A`. Their exact gameplay purpose needs a runtime trace, so those names
remain descriptive rather than entered in the symbol database.

## Ground attachment

`resolve_ground_surface_collision` at `0x39B508` groups the current angle into
one of four 90-degree quadrants. It places two sensors at the appropriate side
of the player, queries the static tile collision data and dynamic rectangle
list, chooses the nearer valid result, adjusts X or Y, and replaces
`surface_angle` with the returned angle.

`check_ground_attachment` at `0x39B83E` performs the smaller follow-up test
used by normal grounded states. It probes a pair of points eight pixels beyond
the current collision radius. A valid result snaps the integer coordinate and
updates the surface angle. If neither sensor finds support, it sets carry; its
callers use that result to enter airborne movement.

## Airborne collision

`resolve_airborne_collision` at `0x39BAD4` first loads the collision radii,
performs pre-collision helpers, and integrates Y. It then chooses floor versus
ceiling probes from the sign of `y_velocity`, and left versus right probes from
the sign of `x_velocity`. The response adjusts the integer coordinate and can
replace the surface angle when the player lands.

Static terrain probes ultimately scan the ROM's collision-height data in the
four cardinal directions. Parallel routines scan the dynamic rectangle table,
whose 11-byte entries contain bounds, a related task pointer, and directional
enable bits. This means the eventual native engine should expose one common
sensor interface with both tile-map and moving-object providers.

## Port-facing model

The minimum faithful native representation now looks like:

```text
Player collision state
  position:       signed 16.8 X/Y
  velocity:       signed 8.8 X/Y
  ground speed:   signed 8.8 scalar
  surface angle:  unsigned 8-bit turn
  bounds:         horizontal/vertical radii
  mode:           grounded or airborne, standing or rolling

Collision query
  input:  point, cardinal direction, collision plane
  output: signed correction distance, surface angle, optional owner
```

The next validation step is a BizHawk or MAME trace at three checkpoints: flat
ground, a slope, and the first jump landing. Those traces can confirm the carry
contract, angle orientation, and the task fields that select collision planes.
