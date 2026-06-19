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
| `+0x36` | `0x673E` | 1 | `collision_radius_x` | Horizontal sensor radius; initialized to 7. |
| `+0x37` | `0x673F` | 1 | `collision_radius_y` | Vertical sensor radius; 13 standing and 10 rolling. |

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

## Spindash and roll

The crouching path enters `player_enter_spindash_charge` at `0x399B98` when a
new A or B press is detected. It plays sound command `0xA3`, selects the
spindash animation, clears the charge word at task offset `+0x3C`, and changes
the task function to `player_state_spindash_charge` at `0x399BB7`.

While Down remains held, `update_spindash_charge` at `0x399B5E` decays the
existing value by one thirty-second each frame. Each new A or B press adds
`0x0200`, caps the value at `0x0800`, and retriggers sound `0xA3`. Releasing
Down plays sound `0xA2`, indexes the launch-speed table at `0x3993BC`, writes
the result to `ground_speed`, and enters rolling.

`player_enter_roll` at `0x399F3B` selects the rolling collision shape before
entering `player_state_roll` at `0x399F48`. The shape transition is explicit:

- Standing uses radii 7 by 13.
- Rolling uses radii 7 by 10 and shifts the integer Y position by three pixels
  to keep the contact point stable.
- Task flag `+0x0B` bit 2 records the rolling collision mode.

The rolling state applies lower-friction ground physics, resolves surface
collision, permits jumping, and returns to idle when speed drops below the
exit threshold.

## Collision overview

Grounded movement converts `ground_speed` and `surface_angle` into X/Y
velocity, advances the position, then calls the surface collision routines.
Airborne movement follows a separate velocity-directed resolver. The detailed
sensor pipeline and scratch layout are recorded in [collision.md](collision.md).

## Still to map

The next player pass should assign names to the hurt, death, and spring states,
then finish the exact meanings of the remaining movement flag bits. Task flag
`+0x0B` bit 1 is strongly associated with airborne movement, while movement
flag `+0x14` bit 7 controls facing and horizontal mirroring; runtime traces are
still needed before marking every use as confirmed.
