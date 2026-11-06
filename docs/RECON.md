# Benesse Pocket Challenge V2 — what is known

The Pocket Challenge V2 (Benesse, 2000s) is the handheld of Benesse's *Shinkenzemi* correspondence
courses: the cartridges are lessons and drills for Japanese middle-school subjects. Inside it is
a Bandai WonderSwan: the same ASWAN system-on-chip (NEC V30MZ at 3.072 MHz, 16 KB of RAM, the
mono display controller, 224×144 LCD with 8 grey shades, the four-channel sound), and the same
cartridge bus. What differs is the case, the buttons and the way it starts.

Sources: the cartridges themselves (disassembled with wskit's `v30mz.py`, run under wskit's
`wstrace`), and the WSdev wiki for the key matrix and the boot mode.

## The cartridges

The set (Internet Archive, not distributed) has 13 dumps, all *Shinkenzemi Chuugaku Kouza*,
2, 4 or 8 MiB, extension `.pc2`. A dump is a WonderSwan dump: no header, and the last 16 bytes
are the WonderSwan footer.

```
EA 10 00 00 40  00 00 00 00 00  06  02  04  00  86 E4
jmp 4000:0010   ..publisher etc  ROM SRAM flg map checksum
```

| footer byte | value in the set | meaning (WonderSwan) |
|---|---|---|
| 0-4 | `EA 10 00 00 40` (`EA 30 00 00 40` in Rika 2BF400) | far jump: the game's entry |
| 5-9 | all zero (Kokugo: `80 80` at 8-9) | developer, colour flag, game id, version |
| 10 | `04` / `06` / `08` | ROM 16 / 32 / 64 Mbit |
| 11 | `02` / `03` | SRAM 256 Kbit (32 KB) / 1 Mbit (128 KB) |
| 12 | `04` | flags (horizontal, 16-bit bus, 1-cycle ROM) |
| 13 | `00` | Bandai 2001 mapper, no RTC |

Mono only (colour flag 0), horizontal only, no EEPROM, no RTC. The games keep their working
state in the SRAM window (segment `1000`) besides the 16 KB of internal RAM.

### Two entries

At reset the WonderSwan's boot ROM jumps to `FFFF:0000`, the footer, whose far jump goes to the
game. The PCV2 does not: per the WSdev wiki the ASWAN is pin-strapped into a PCV2 mode where the
boot ROM skips the Bandai splash (the WonderSwan logo, the owner's name) and starts the
cartridge at **`4000:0000`**, with DS still on the boot ROM's segment.

The cartridges are built for both. At `4000:0000` (the start of the last megabyte, bank `$FF` in
window 4) every one has something that runs into the real entry the footer also points at:

- 16 zero bytes, then `cli` at `4000:0010` (11 of the 13): eight `add [bx+si],al`, writes that go
  to DS — the boot ROM — and so to nothing;
- 16 `nop`s (Ei-Suu-Koumin Pack);
- the build stamp `01/10/16 20:46:57 2BF400` (2001-10-16), `FF`s and zeros, then `cli` at
  `4000:0030` (Rika Pack 2BF400): ASCII run as code — `xor [bx+di],dh`, `das`, `xor [3032],si`,
  `aaa`, the do-nothing `FF FF` — harmless only because DS is the boot ROM.

The footer's jump is what makes a PCV2 cartridge start on a WonderSwan (the two consoles take
each other's cartridges).

The entry code (`cli`, port `$B7` = 0, registers and segments zeroed, SP = `0E00`, RAM from `00A0`
cleared) sets everything up itself: nothing the boot ROM leaves is relied on but the bank
registers.

## The keypad

The ASWAN's key matrix (port `$B5`: write a row select in bits 4-6, read the row in bits 0-3) is
wired to nine buttons. Every cartridge has one routine (Suugaku: `4000:0368`) that reads the
three rows and packs them into a word:

```
out B5,40 / in -> CH     row 6: bits 8-11
out B5,20 / in -> CL hi  row 5: bits 4-7
out B5,10 / in -> CL lo  row 4: bits 0-3
and bx, 0DDD             bit 1 of each row is not connected
```

and keeps, in the SRAM window (`1000:003A`...): `[3A]` held, `[3C]` pressed this frame, `[40]`
released this frame, `[3E]` the "pressed" word the game tests, `[42]` the pressed word with
auto-repeat (first repeat after 20 frames, then every 8).

| row (B5 select) | bit 3 | bit 2 | bit 1 | bit 0 |
|---|---|---|---|---|
| `$10` (WS: Y1-Y4) | Pass `0008` | Circle `0004` | — | Clear `0001` |
| `$20` (WS: X1-X4) | Right `0080` | Esc `0040` | — | View `0010` |
| `$40` (WS: Start, A, B) | Up `0800` | Down `0400` | — | Left `0100` |

The names are the WSdev wiki's; the code agrees with them:

- a cursor routine (Suugaku `4000:4420`) moves up and down by 8 pixels on `0800` and `0400` of
  the auto-repeat word, left and right by a pixel on `0100` and `0080` held; `0D80` (all four)
  is tested as "an arrow";
- `0040`, Esc, is the most tested bit of all (120 `test ax,40` after reading `[3E]`): every screen
  leaves on it. The routine reports it on *release*, not on press, and if it is held for more
  than 150 frames (2 s) it sets a long-press flag (`[47]`) and the release is not reported:
  a long Esc is a different command;
- `000D` (Clear, Circle, Pass) is tested as a group of three answers.

On the WonderSwan, bit 0 of row `$40` is not wired (Start is bit 1, A bit 2, B bit 3), and
the games ignore bit 1 of every row. So on a WonderSwan a PCV2 cartridge has no Left at all, and
Start, X2, Y2 do nothing; the rest is Up = B, Down = A, Right = X4, Esc = X3, View = X1,
Pass = Y4, Circle = Y3, Clear = Y1.

## Under wskit's WonderSwan

Run as a plain mono WonderSwan from the footer (wskit's `wstrace`, its pad mashed at random),
Chuu1 Suugaku goes through its textbook choice ("きみの数学の教科書": Tokyo Shoseki,
Keirinkan...) to its unit menus with nothing missing. In 1200 frames it uses the VBlank
interrupt only (vector `$26`, a base of `$20`), sleeps in `HLT` between frames, runs its code from
windows 4-8 (one megabyte, bank register `$C0` = `$xF`), switches banks 2 and 3 for data
thousands of times a minute, and touches no EEPROM, RTC, serial port or DMA. No code in RAM, no
self-modifying code.

## Unknowns

- whether the PCV2's boot ROM is the WonderSwan's (in its PCV2 mode) and what it leaves in the
  ports; bpc-emu starts at `4000:0000` with DS = `FF00`, SP = `2000` and the ports as the chip's
  reset leaves them (every cartridge's start-up code sets what it uses);
- the PCV2's LCD: whether it has the WonderSwan's segment icons;
- sound: bpc-emu plays the speaker's mono mix and reports no headphones (port `$91` bit 7 = 0);
  whether the PCV2 has a headphone jack is not known.
