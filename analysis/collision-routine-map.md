# Collision routine map

This note pins the native viewer collision work to the ValleyBell disassembly
instead of screenshot tuning.

Source disassembly:

`C:\Users\NoSig\Documents\SonicPocket\SonicPocketAdventure_disasm+tools\disassembly\spa.asm`

## Player ground collision path

- `Plr_IsOnGround` at `0039BADB`
  - Calls player pre-collision helpers.
  - Calls `Obj_AddSpeedXY`.
  - Calls `CalcPlrMoveSpd`.
  - Dispatches to floor/ceiling/side collision depending on velocity signs.

- `CalcPlrMoveSpdY` at `0039B4F7`
  - Computes `abs(current_y - previous_y) + 1`.
  - Stores low byte in `byte_668F`.
  - This is the scan length used by `sub_39BC22`.

- `sub_39BC22` at `0039BC22`
  - This is the main floor contact routine used from `Plr_IsOnGround`.
  - Probes the player's right foot with `BGCollChk4`.
  - Probes the player's left foot with `BGCollChk4`.
  - Chooses the greater/effective `HL` correction.
  - Applies `ADD (XIZ+16h), HL`.
  - Stores the returned angle byte `E` into `(XIZ+10h)`.

Approximate pseudocode:

```c
wa = player.x;
bc = player.y - radius_y;

right = BGCollChk4(wa + radius_x, bc, plane, byte_668F);
left  = BGCollChk4(wa - radius_x, bc, plane, byte_668F);

hit = choose_greater_hl(right, left);
if (hit.hl != 0) {
    player.y += hit.hl;
    player.angle = hit.e;
    carry = 1;
} else {
    carry = 0;
}
```

## Tile collision primitives

- `GetCollDataPtr` at `0039BFCC`
  - Calls `GetCollValue`.
  - Computes collision table index:
    - row: `7 - (ROM_Y & 7)`
    - column: `ROM_X & 7`
  - Returns pointers to horizontal and vertical collision tables.

- `BGCollChk4` / `BGCollChk4_10` at `0039C160`
  - Checks push-hitbox floor collision through `sub_39BF88` first.
  - If no push-hitbox collision, scans downward through the vertical collision table.
  - Returns:
    - `HL`: signed correction
    - `E`: collision angle
    - carry set on hit

- `BGCollChk3` at `0039C0EF`
  - Ceiling scan using the vertical collision table.

- `BGCollChk1` at `0039C01B`
  - Left wall scan using the horizontal collision table.

- `BGCollChk2` at `0039C08A`
  - Right wall scan using the horizontal collision table.

## Tested false leads

These were tested against `out/player-runtime-trace.csv` and rejected:

1. Changing C++ collision table X index from `7 - local_x` to `local_x`.
   - Result: first replay mismatch moved earlier, from row 527 to row 363.

2. Forcing `apply_rom_vertical_ground_pair()` to use raw `floor_hit.delta_y`.
   - Result: first replay mismatch moved earlier, from row 527 to row 361.

So the current mismatch is probably not solved by a simple X-index flip or by
removing all Y-correction compensation. The next useful step is to port
`BGCollChk4` and `sub_39BC22` as a separate ROM-coordinate sandbox, then compare
that sandbox to the trace before replacing viewer gameplay code.

## Teacher-forced diagnosis

The viewer now has a teacher-forced trace mode:

```powershell
build\viewer\Release\sonic-pocket-viewer.exe out\nsi1 `
  --teacher-trace out\player-runtime-trace.csv `
  --teacher-out out\native-teacher-trace.csv

python tools\analyze_teacher_trace.py out\native-teacher-trace.csv
```

Unlike normal replay, this resets the native player from each ROM trace row,
runs exactly one native update, and compares against the next ROM row. This
prevents one bad frame from contaminating the rest of the trace.

Current teacher-forced result:

- 518 independent logic samples.
- `dy_raw` mismatches: 16 samples.
- `dx_raw` mismatches: 1 sample.
- `ground_speed` mismatches: 1 sample.
- `y_velocity` mismatches: 0 samples.
- `surface_angle` mismatches: 0 samples.
- `grounded` mismatches: 0 samples.

That means the active bug class is not broad movement, animation, camera, or
grounded-state logic. The active bug class is floor Y correction around shallow
slope/flat transitions, especially angles `0xF6`, `0x0A`, and `0x00`.

The repair target is therefore:

1. Port/check `sub_39BC22` as a small ROM-coordinate function.
2. Port/check the `BGCollChk4` table scan semantics it depends on.
3. Use `native-teacher-trace.csv` to clear the 16 `dy_raw` mismatches before
   touching live viewer feel again.
