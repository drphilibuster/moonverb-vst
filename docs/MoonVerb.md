# MoonVerb manual

## What it is

A reverb / delay / chorus / resonant-chord processor from the mid-1980s, running its own firmware. Nothing is re-implemented: the firmware
(master Z80 and slave Z80) builds each program's microcode, the emulated signal processor runs it, and the host (this plug-in) supplies only what the hardware's
surroundings did: the front panel, MIDI, the analogue converter chain, the level detector and the wet/dry mix.

## The firmware images (you supply them)

Choose **LOAD ROM FOLDER** in the rear-panel strip and select the folder holding your dumps. MoonVerb tells the files apart by their content hash:

| Image | Size | What it is |
|---|---|---|
| U62 | 32 KB | master program |
| U95 | 16 KB | slave program (also holds the factory programs) |
| U67 | 8 KB | opcode ROM |
| U48 | 512 B | sequencer PROM |
| U49 | 32 B | sequencer PROM |

A V2.0 set and a V3.01 set are both recognised (V3.01 adds MIDI-clock tempo for the BPM programs); the three large images must come from the same version.
A wrong or damaged file is named on the strip with the reason. The folder is remembered for every new instance (`~/Library/Application Support/Moon Technologies/MoonVerb/settings.json`
holds paths only) and saved with each project. **Forget** clears it. Power-up takes about 10 seconds, as on the hardware; the plug-in is silent until the display lights.

## The front panel

| Control | What it does |
|---|---|
| **Level display** | The five-lamp signal level / headroom display (0 dB at the top). The firmware's gated programs read the same detector. |
| **Input level** | The input knob (also the host parameter *Input level*). |
| **Display** | The 16-character display, drawn from the firmware's own display memory. |
| **PGM**, **REG** | Enter program mode / register mode (lamps show the mode). PGM toggles between program and parameter modes. |
| **ROW ▲ ▼** | Step through rows of programs, registers or parameters. |
| **0–9** | Choose a program or register within the row, or a parameter within the row in parameter mode. |
| **LOAD** | Load the chosen program or register. Hold **REG** and press **LOAD** to store the running program in the chosen register. |
| **BYPASS** | Toggle bypass (lamp). Also the plug-in's host bypass. |
| **Soft knob** | Edits the parameter the display shows (parameter mode): the firmware's own edit routine, so ranges, limits and masters behave exactly as on the hardware. Drag up/down or use the wheel; a fast turn travels further. |
| **Power** | Off silences the unit; on powers it up again. The registers are kept (battery RAM). |

The keys are pressed into the firmware's key matrix as a finger would; a quick click is held long enough to be seen.

## The rear-panel strip

* **Firmware images**: loads and lists the images, as above.
* **MIDI**: channel, OMNI, and whether program change is accepted (settings the firmware keeps in its battery RAM).
* **Levels**: the two rear switches, *main input* and *output*, each `+4 dBu` or `-20 dBV` (15 dB less at the converter input, 24.7 dB less at the output).
* **Registers**: import and export a standard SysEx register bank (all 50 registers).
* **System**: *Power cycle*, and *Clear memory* (the hardware's CLEAR MEMORY: factory-fresh registers).

## Host integration

* **Audio**: mono or stereo in, stereo out. The unit has one input, so a stereo signal is summed (average of left and right). The host sample rate is used directly: the converter's own
  filters and rate conversion run at your rate, and the plug-in reports its latency (about 25 samples at 48 kHz) for delay compensation; the dry path is delayed to match.
  Digital full scale is the converter's full scale, with the input knob at maximum and the switch at `+4 dBu`.
* **Parameters**: *Input level*, the two level switches, *Bypass* and 45 parameter knobs `Parameter r.c` (the machine's own parameter matrix, 5 rows × 9 columns, whatever the running program
  calls them; the host shows the firmware's name and value while a program is loaded). Moving one asks the firmware to set that word; when the firmware changes a word itself (a program load) the host parameter follows.
  A scheduler keeps fast automation from overwhelming the firmware's main loop (about 100 edits a second in total, 65 % of its time at most).
* **MIDI**: notes, controllers, pitch bend, channel pressure, program change and SysEx go to the firmware's MIDI port, so its own *dynamic MIDI* patches (controller → parameter, programmed in the registers) work.
  Channel and OMNI follow the rear-panel setting. With V3.01 firmware the host transport (tempo and position) drives the 24-pulses-per-quarter-note clock the BPM programs measure; MIDI clock arriving from the host is used instead if present.
* **State**: projects save the firmware paths, the 8 KB battery RAM (your registers and settings, no firmware data) and the host parameters.

## What is exact and what is not

Exact: the firmware (it is the firmware); every program, parameter and register; the display; the keys; the MIDI behaviour; the signal processor's arithmetic (bit-identical to the reference model in the tests).

Modelled from measurement, not the schematic alone: the converter filters, the level detector and the mix stage (tested against recordings of the real unit; reverb decay envelopes match within about 1 dB on most programs).

Known limits: Long Hall and Concert Hall at long reverb times decay about 10 % faster than the recordings (3 to 4 dB envelope difference); the output DC blocker's corner (3 Hz) was not measured; the converters are modelled as ideal apart from their filters; programs without a recording were not validated by ear.

## Test data (optional)

`MOONVERB_ROMS` the folder of images; `MOONVERB_SYX` a folder holding register banks (the tests find `*Ver-2.syx` / `*Ver-3*.syx`); `MOONVERB_ORACLE` reference captures from the original research emulation;
`MOONVERB_CELLS` and `MOONVERB_CELLS3` JSON parameter tables for V2 and V3. None of it is in this repository.
