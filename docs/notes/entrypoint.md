# Cartridge entry point

The cartridge header transfers control to `0x200040`.

Initial static analysis establishes the following sequence:

1. Startup state is checked against a byte in the cartridge header.
2. The stack pointer is initialized to `0x006C00` on the cold-start path.
3. Software interrupt 1 is invoked during early platform setup.
4. A large portion of work RAM beginning at `0x004000` is cleared.
5. A small initialization table at `0x20024E` is copied to RAM at `0x006FB8`.
6. Control passes through additional initialization routines and into the main
   startup sequence beginning around `0x200124`.

The block at `0x2000A0` saves the major register groups, calls several
subroutines, restores the registers, and finishes with `RETI`. It is therefore
confirmed to be an interrupt handler; its specific interrupt source remains to
be proven with a runtime trace.

## Next questions

- Which vector or BIOS service installs `0x2000A0`?
- Which called routine acknowledges the interrupt?
- Does the handler correspond directly to K2GE VBlank/INT4?
- What roles do counters at `0x400A`, `0x400C`, and `0x400E` play?
