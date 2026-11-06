# Sources

bpc-emu is MIT-licensed (`LICENSE`) and contains no third-party code.

The hardware it models — the NEC V30MZ's instruction set, its cycle counts and flags, the
ASWAN's memory map, ports, interrupt controller, display controller, sound and timers, the
Bandai 2001 cartridge's bank registers and SRAM — follows the behaviour of MAME's wswan
driver and devices (`src/mame/bandai/wswan.cpp`, `wswan_v.cpp`,
`src/mame/shared/wswansound.cpp`, `src/devices/bus/wswan/`,
`src/devices/cpu/v30mz/v30mz.cpp`; BSD-3-Clause), as wskit
(https://github.com/vs-sr-dev/wskit, same author, MIT) documents and runs it. The code here is
written for bpc-emu; wskit's interpreter is used only as a black-box oracle (`tools/oracle`,
built from a separate wskit checkout, not part of the emulator).

The Pocket Challenge V2's key matrix and boot mode: the WSdev wiki
(https://ws.nesdev.org/wiki/Keypad), checked against the cartridges' own code
(`docs/RECON.md`).

`src/png.c` is MobiGoEmu's PNG writer (same author, MIT).
