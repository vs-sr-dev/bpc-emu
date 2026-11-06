# TODO

## Next

- Play through a cartridge by hand in the window: a whole lesson, a drill with a time limit.
  (Tried: the speed, the sound, Left, a save kept across a restart.)
- The long Esc (held over 150 frames): what command it is.
- Sound: heard in the window (beeps and square-wave arpeggios, as expected); find which games
  use what and how.
- The idle alarm (docs/RECON.md): find the counter in the code, and whether 確認 is Circle.
- Ei-Suu-Koumin Pack turns the LCD off and on (port `$14`, 20 writes in 6000 frames; every other
  cartridge writes it once): what for.

## Later

- The boot ROM: whether the PCV2 has the WonderSwan's (in its PCV2 mode) and what it leaves in
  the ports; a dump would settle it.
- The other Pocket Challenge V2 cartridges (only the *Chuugaku Kouza* ones are in the set).
- WonderSwan cartridges on the PCV2 (the two take each other's cartridges): EEPROM saves, the
  2003 mapper's RTC, a button layout for them.
- The frontend: a remembered window size, a file dialog, a picker for a zip of many
  cartridges.
