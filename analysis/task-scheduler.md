# Cooperative task scheduler

The cartridge does not run all game logic from one direct main-loop call.
`rom_entry` passes `engine_task_init` to `initialize_task_scheduler` at
`0x237706`, then jumps to `run_task_scheduler` at `0x23773F`.

## Scheduler loop

`run_task_scheduler` starts with the task at RAM `0x401A`. For each node it:

1. Loads and calls the function pointer at task offset `+0x00`.
2. Loads the next-task pointer from offset `+0x06`.
3. Repeats forever.

Tasks change state by replacing their own function pointer. This explains the
many small functions that end by storing another ROM address through `XIZ`.

## Task record

The scheduler allocates fixed-size `0x4C`-byte records. The common header
fields identified so far are:

| Offset | Size | Meaning |
| ---: | ---: | --- |
| `+0x00` | 4 | Current task/state function pointer. |
| `+0x04` | 2 | Previous task pointer. |
| `+0x06` | 2 | Next task pointer. |
| `+0x08` | 2 | Related/owner task pointer used by task helpers. |
| `+0x0A` | 1 | Task generation or identity byte. |
| `+0x0B` | 1 | Scheduler/task flags; bit 0 marks a static task. |
| `+0x0C` | varies | Task-specific state begins here. |

`allocate_task` at `0x237752` takes a function pointer in `XWA`, removes a
record from the free list at `0x4014`, and links it after the current `XIZ`
task. `register_static_task` at `0x2377A5` instead links a caller-supplied RAM
record and sets the static-task flag.

## Boot task chain

The initial engine task installs VBlank/input/rendering services and yields to
the scheduler once per frame. During initialization it registers
`frontend_manager_task_init` at `0x23AE72`. That task drives the opening
presentation, attract-mode timeout, menus, and the transition into a selected
game mode.

The gameplay path eventually creates `gameplay_session_task_init` at
`0x39E1AB`. During stage setup that task registers two persistent nodes:

| RAM node | Initial function | Working role |
| ---: | ---: | --- |
| `0x6708` | `0x399740` | Player task. |
| `0x6754` | `0x39D085` | Level manager task. |

This scheduler/task boundary is the natural architecture to reproduce in the
native port before translating individual objects.
