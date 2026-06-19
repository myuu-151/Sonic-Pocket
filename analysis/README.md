# Analysis notes

This directory contains human-authored observations and symbols. Generated
full-ROM disassembly belongs in the ignored `out/` directory.

Confidence values used by `symbols.csv`:

- `confirmed`: directly established by the ROM header or instruction structure.
- `probable`: strong control-flow or hardware evidence; runtime confirmation pending.
- `unknown`: address recorded for follow-up without a semantic claim.
