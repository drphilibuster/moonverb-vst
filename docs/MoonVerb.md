# MoonVerb manual

## What it is

A reverb / delay / chorus / resonant-chord processor from the mid-1980s, running its own firmware. Nothing is re-implemented: the firmware
(master Z80 and slave Z80) builds each program's microcode, the emulated signal processor runs it, and the host (this plug-in) supplies only what the hardware's
surroundings did: the controls, MIDI, the analogue converter chain, the level detector and the wet/dry mix. The panel is not a copy of the unit's (a ten-key pad and one knob):
the sound is the unit's, the way you reach it is a plug-in's.

## The firmware images (you supply them)

Open **SETTINGS**, choose **LOAD ROM FOLDER** and select the folder holding your dumps. MoonVerb tells the files apart by their content hash:

| Image | Size | What it is |
|---|---|---|
| U62 | 32 KB | master program |
| U95 | 16 KB | slave program (also holds the factory programs) |
| U67 | 8 KB | opcode ROM |
| U48 | 512 B | sequencer PROM |
| U49 | 32 B | sequencer PROM |

A V2.0 set and a V3.01 set are both recognised (V3.01 adds MIDI-clock tempo for the BPM programs); the three large images must come from the same version.
A wrong or damaged file is named on the strip with the reason. The folder is remembered for every new instance (the MoonVerb settings file in your user library folder holds
paths only, plus the factory program names read from your images) and saved with each project. **Forget** clears it. Power-up takes about 10 seconds, as on the hardware; the plug-in is silent until the display lights.

## The panel

| Control | What it does |
|---|---|
| **< preset >** | The preset browser. **<** and **>** step to the previous / next preset that holds something: the factory programs in order, then your user presets, wrapping round. The mouse wheel over the name steps too; clicking the name opens the list (FACTORY by row, USER by slot). Choosing a preset loads it at once: the plug-in calls the firmware's own program-change routine, so the result is exactly what the unit's LOAD key gives (about 1 s, the firmware's own load time). The line above the name shows the slot, LOADING, STORING, or EDITED once you have changed something. |
| **STORE** | Saves the running program, as it is now, as a user preset (50 slots; replacing one asks first). |
| **BYPASS** | The unit's own bypass (also the plug-in's host bypass). |
| **POWER** | Off silences the unit; on powers it up again. The user presets and settings are kept (battery RAM). |
| **Display** | The unit's own 16-character display. Beside it: the parameter you last touched, or what is loading, or the preset and its number of parameters. |
| **Parameters** | Every parameter of the running program, as a knob: its name and value are the firmware's own, laid out in the firmware's own rows. The masters (… MST, amber) move their six voices with them, as on the unit. Drag or use the wheel (shift: fine); click a knob, then **up / down** step it by one firmware step (shift: ten) and **left / right** choose the next knob; **double-click** returns it to the value the preset had. |
| **RATE** (BPM programs) | **HZ**: the program's beat as a rate. **SYNC**: the beat is a note value of the host's tempo (1/4, 1/8., 1/8T, ...), followed as the tempo changes; drag or use the wheel to choose the note value. The unit plays 64 to 191 BPM, so note values outside that at the current tempo are skipped. |
| **INPUT**, lamps | The input level (also the host parameter *Input level*) and the five-lamp level display (-24 to 0 dB). The firmware's gated programs read the same detector. |
| **INPUT / OUTPUT -20 dBV** | The unit's two rear level switches: 15 dB less at the converter input, 24.7 dB less at the output. |

**SETTINGS** opens a drawer:

* **Firmware images**: load and forget, the files found, the version.
* **MIDI**: channel, OMNI, and PROGRAM CHANGE (MIDI program change *n* loads user preset *n*); settings the firmware keeps in its battery RAM.
* **User presets**: import and export a standard SysEx bank (all 50).
* **System**: *Power cycle*, *Clear memory* (the hardware's CLEAR MEMORY: no user presets, factory settings), and *Show matrix numbers* (each knob's `row.column`, for anyone who knows the unit).

## Host integration

* **Audio**: mono or stereo in, stereo out. The unit has one input, so a stereo signal is summed (average of left and right). The host sample rate is used directly: the converter's own
  filters and rate conversion run at your rate, and the plug-in reports its latency (about 25 samples at 48 kHz) for delay compensation; the dry path is delayed to match.
  Digital full scale is the converter's full scale, with the input knob at maximum and the switch at `+4 dBu`.
* **Programs**: the host's program list holds the 70 factory slots and the 50 user presets, so a host's preset menu or program change selects them.
* **Parameters**: *Input level*, the two level switches, *Bypass* and 45 parameter knobs `Parameter r.c` (the machine's own parameter matrix, 5 rows × 9 columns, whatever the running program
  calls them; the host shows the firmware's name and value while a program is loaded). Moving one asks the firmware to set that word; when the firmware changes a word itself (a program load) the host parameter follows.
  A scheduler keeps fast automation from overwhelming the firmware's main loop (about 100 edits a second in total, 65 % of its time at most).
* **MIDI**: notes, controllers, pitch bend, channel pressure, program change and SysEx go to the firmware's MIDI port, so its own *dynamic MIDI* patches (controller → parameter, programmed in the registers) work.
  Channel and OMNI follow the setting in SETTINGS. MIDI clock arriving from the host reaches the firmware's clock input (V3.01 measures it); the host's tempo itself reaches the BPM programs through RATE's SYNC mode.
* **State**: projects save the firmware paths, the 8 KB battery RAM (your user presets and settings, no firmware data), the running preset, RATE's mode and note value, and the host parameters.

## What is exact and what is not

Exact: the firmware (it is the firmware); every program, parameter and register; program loads (the firmware's own routine); the display; the MIDI behaviour; the signal processor's arithmetic (bit-identical to the reference model in the tests).

Modelled from measurement, not the schematic alone: the converter filters, the level detector and the mix stage (tested against recordings of the real unit; reverb decay envelopes match within about 1 dB on most programs).

Known limits: Long Hall and Concert Hall at long reverb times decay about 10 % faster than the recordings (3 to 4 dB envelope difference); the output DC blocker's corner (3 Hz) was not measured; the converters are modelled as ideal apart from their filters; programs without a recording were not validated by ear.

## Test data (optional)

`MOONVERB_ROMS` the folder of images; `MOONVERB_SYX` a folder holding register banks (the tests find `*Ver-2.syx` / `*Ver-3*.syx`); `MOONVERB_ORACLE` reference captures from the original research emulation;
`MOONVERB_CELLS` and `MOONVERB_CELLS3` JSON parameter tables for V2 and V3. None of it is in this repository.
