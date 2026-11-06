# bpc-emu

**An emulator for the Benesse Pocket Challenge V2, the handheld of Benesse's *Shinkenzemi*
correspondence courses: lessons and drills for Japanese school subjects on cartridges.**

bpc-emu is not affiliated with Benesse or Bandai. The names are used only to say which console
this emulates.

## Status

First session (2026-11-06):

- The console worked out from its cartridges: it is a mono WonderSwan (the ASWAN chip, an NEC
  V30MZ) with nine buttons wired to the key matrix differently, and a boot that starts the
  cartridge at `4000:0000` instead of the footer. See [docs/RECON.md](docs/RECON.md).
- The V30MZ, the ASWAN (display, sound, timers, interrupts, keypad, internal EEPROM) and the
  cartridge (Bandai 2001 banks, SRAM) are written for this project, in C.
- Checked against an oracle, wskit's WonderSwan interpreter (same author; `tools/oracle`): all
  13 cartridges of the set run 6000 frames with a scripted button-masher identical to it, cycle
  for cycle — CPU registers, RAM, SRAM and picture compared at every frame.
- All 13 reach their menus, drills and questions.
- A window with sound, the keyboard and game controllers; the SRAM kept beside the cartridge.

## Bring your own dumps

This repository contains no cartridge data and never will. The Pocket Challenge V2's dumps
are `.pc2` files (a WonderSwan cartridge's format: no header, the 16-byte footer at the end).
No BIOS is needed: the console's boot only hands over to the cartridge, which sets everything
up itself.

## Build

MSYS2 MinGW 64-bit with `mingw-w64-x86_64-gcc`, `mingw-w64-x86_64-cmake`,
`mingw-w64-x86_64-ninja` and `mingw-w64-x86_64-SDL2`:

```
cmake -S . -B build -G Ninja
cmake --build build
```

Without SDL2 only the headless runner, `bpc-cli`, is built.

## Running

`build/bpc-emu CART.pc2`, or drop a cartridge on its window. `--scale N` sets the window size.
The SRAM is kept as `CART.sav` beside the cartridge.

| Pocket Challenge V2 | Keyboard | Controller |
|---|---|---|
| Pad | arrows | d-pad, left stick |
| ○ (Circle) | Z, Enter | A |
| Clear | X, Backspace | X |
| Pass | C | Y |
| View | V | Back |
| Esc | Esc | B |

F2 resets, P pauses, F11 is full screen, F12 saves a screenshot beside the cartridge. Esc is
the console's button: close the window to quit.

## The headless runner

```
build/bpc-cli CART.pc2 --frames 3000 --snap 600,3000 --out DIR      # PNGs at those frames
build/bpc-cli CART.pc2 --keys run.keys --log state.txt              # scripted buttons, the state at every frame
python tools/monkey.py run.keys --frames 6000 --seed 1 [--no-left]  # a button-masher script
python tools/sheet.py sheet.png A.png B.png ... --cols 4            # PNGs tiled
```

A keys file has lines `FRAME BUTTONS`: the buttons held from that frame on, by name
(`up`, `down`, `left`, `right`, `circle`, `clear`, `pass`, `view`, `esc`, `none`, joined
with `+`) or as the key matrix word in hex.

## The oracle

`tools/oracle/build.sh` builds `build/oracle/oracle.exe` from wskit's runtime
(`WSKIT=path`, `../wskit` by default; MSYS2 ucrt64 g++). It writes, from the same start and the
same keys file, the lines `bpc-cli --log` writes:

```
build/oracle/oracle.exe CART.pc2 --frames 6000 --keys run.keys --log o.txt
build/bpc-cli CART.pc2 --frames 6000 --keys run.keys --log b.txt
cmp o.txt b.txt
```

The PCV2's Left has no WonderSwan button, so the oracle's scripts leave it out (`--no-left`).
