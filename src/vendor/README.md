# Vendored code

`z80/` is Nicolas Allemand's [superzazu/z80](https://github.com/superzazu/z80), an MIT-licensed Z80 interpreter in plain C (`LICENSE` retained unmodified).
One change: the cycle counter `cyc` is `uint64_t` rather than `unsigned long`. At 3.25 MHz a 32-bit counter (`unsigned long` on Windows) wraps after
22 minutes, which would break every cycle comparison in the machine. Nothing else differs; the machine in `src/core/Machine.hpp` reproduces
the research emulation it was validated against, so do not update the copy without re-running the tests.
