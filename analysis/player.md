# Player task

The selected-stage flow registers `player_task_init` at `0x399740` in the
static task node at RAM `0x6708`. Its later states read controller input,
choose standing/crouching/looking/moving states, apply Sonic-style ground
physics, perform collision calls, and prepare animation/rendering state.

## Position and velocity

The following fields are confirmed from initialization and the movement
integrators at `0x291606` and `0x291613`:

| Task offset | RAM address | Size | Working name | Meaning |
| ---: | ---: | ---: | --- | --- |
| `+0x10` | `0x6718` | 1 | `surface_angle` | Angle used to resolve scalar ground speed into X/Y velocity. |
| `+0x11` | `0x6719` | 3 | `x_position` | 16.8 fixed-point horizontal position; integer word begins at `+0x12`. |
| `+0x14` | `0x671C` | 1 | `movement_flags` | Includes facing/mode flags; bit meanings remain under analysis. |
| `+0x15` | `0x671D` | 3 | `y_position` | 16.8 fixed-point vertical position; integer word begins at `+0x16`. |
| `+0x18` | `0x6720` | 2 | `ground_speed` | Signed scalar speed along the current surface. |
| `+0x1A` | `0x6722` | 2 | `x_velocity` | Signed 8.8 horizontal velocity. |
| `+0x1C` | `0x6724` | 2 | `y_velocity` | Signed 8.8 vertical velocity. |

`velocity_from_angle_and_speed` at `0x291494` indexes two signed lookup tables
with `surface_angle`, multiplies both components by `ground_speed`, and returns
X/Y velocity. The two position integrators then add the low velocity byte to
the fractional position byte and sign-extend the high byte into the integer
word.

## Initial states

`player_enter_idle` at `0x3999F7` clears ground speed and both velocity
components, resets movement flags, selects the standing animation, and changes
the task function to `player_state_idle` at `0x399A2B`.

The standing state demonstrates the input mapping directly:

- Left or Right (`controller_state_current & 0x0C`) enters movement.
- Down enters the crouching state.
- Up enters the look-up state.
- Later crouch/movement states test newly pressed A or B (`0x30`) for actions.

`player_enter_walk` at `0x399EE5` changes the task to `player_state_walk` at
`0x399EF2`. The walking state applies ground acceleration, converts ground
speed through the angle table, performs collision/attachment work, updates
animation, and returns to standing when speed and input conditions permit.

## Jump transition

`player_try_jump` at `0x399697` tests newly pressed A or B and requests
`player_enter_jump` through `set_player_state_if_active`. The jump entry at
`0x39AA86`:

1. Plays sound command `0x85`.
2. Selects a jump scalar of `0x0900`, or `0x0490` under a reduced-velocity
   condition that is probably the underwater path.
3. Adds `0x40` to the surface angle to obtain the outward surface normal.
4. Uses `velocity_from_angle_and_speed` and adds the resulting components to
   X/Y velocity.
5. Selects the rolling/jump animation and transitions to airborne movement at
   `0x39AAF7`.

This confirms that the port can share a signed 8.8 velocity representation and
an eight-bit angle system with the original player physics.

## Still to map

The next player pass should assign names to the roll, hurt, death, and spring
states, then determine collision sensor fields and the exact flag bits
at offsets `+0x0B` and `+0x14`.
