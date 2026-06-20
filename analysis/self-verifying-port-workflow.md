# Self-verifying decomp/port workflow

The project should not depend on manual “try the ramp again” testing for core
parity. Manual testing is only for human feel after the automated evidence says
a routine is close.

## Ratchet loop

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\port-improvement-loop.ps1
```

The loop:

1. Builds the PC viewer.
2. Replays the ROM input trace in the native viewer.
3. Runs teacher-forced trace mode, where each ROM logic sample is loaded into
   the native player, stepped once, and compared to the next ROM sample.
4. Clusters mismatches by field and collision/debug context.
5. Writes `out\port-improvement-report.md`.

Use this as a hard gate with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\port-improvement-loop.ps1 -Strict
```

## Acceptance rule

A code change is allowed to stay only if it improves the measured target without
regressing earlier checks.

For collision work, that means:

- `dy_raw` teacher-forced mismatches must go down.
- `surface_angle` mismatches must stay at zero.
- `grounded` mismatches must stay at zero.
- normal replay must not diverge earlier than the current baseline.

If a patch makes screenshots look better but worsens these metrics, reject it.

## Current active target

Current evidence is always whatever `scripts\port-improvement-loop.ps1` reports
in `out\port-improvement-report.md`.

Completed target:

- `sub_39BC22`
- `BGCollChk4`
- the collision-table scan semantics used by those routines

The floor-Y teacher-forced class is now clean. The next target is the first
remaining movement-integration mismatch; do not tune camera, animation, or
sprite draw offsets for that class.

## Team workflow

Only one lane patches gameplay at a time.

- Disassembly lane: map original routines and write ROM-coordinate pseudocode.
- Trace lane: collect/compare runtime traces and identify the next mismatch
  class.
- Implementation lane: port one routine or helper at a time.
- Gate lane: run the ratchet and reject changes that do not improve metrics.

This keeps the port general. Fixes must be routine-level, not per-ramp or
per-coordinate patches.
