# MoonVerb

A digital reverb and effects processor as a plug-in: **VST3** (macOS, Windows, Linux), **Audio Unit** (macOS) and a **standalone app**.

MoonVerb does not imitate a vintage rack unit: it **runs the unit's own firmware**. Two emulated Z80 processors execute the original program, which builds every reverb, delay and chorus
program exactly as the hardware did, and an emulated signal processor runs what the firmware writes, behind the converters' real filters. The front panel is laid out like the hardware's;
the rear-panel switches are in a strip beneath it.

> **MoonVerb makes no sound until you give it firmware images.** They belong to the manufacturer, so they are not included and cannot be. See [Firmware images (ROMs)](#firmware-images-roms) below.

## Install

Download the installer for your system from the [latest release](https://github.com/drphilibuster/moonverb-vst/releases/latest). The installers are **not code-signed** (that needs a paid developer
account), so each system asks you to approve them once. That is expected; the steps are below.

### macOS

1. Download `MoonVerb-<version>-macOS.pkg` and double-click it. macOS says it "can't be opened" or "can't verify it is free of malware".
2. Open **System Settings → Privacy & Security**, scroll down to the message about MoonVerb and click **Open Anyway**, then enter your password. (Alternatively, Control-click the `.pkg` and choose **Open**.)
3. In the installer you can tick which parts you want: **VST3** (`/Library/Audio/Plug-Ins/VST3`), **Audio Unit** (`/Library/Audio/Plug-Ins/Components`) and the **standalone app** (`/Applications/MoonVerb.app`).
4. Restart your DAW, or rescan plug-ins. Logic Pro and GarageBand use the Audio Unit (it appears under *MIDI-controlled Effects*); most other DAWs use the VST3.
5. If a DAW still refuses to load it with the same "can't verify" message, clear the download flag once from Terminal:
   ```sh
   sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/MoonVerb.vst3 /Library/Audio/Plug-Ins/Components/MoonVerb.component /Applications/MoonVerb.app
   ```
6. The first time you open the standalone app, macOS asks for microphone access: that is its audio input.

Uninstall: delete those three items.

### Windows

1. Download `MoonVerb-<version>-Windows.exe` and run it. Windows shows **"Windows protected your PC"**: click **More info**, then **Run anyway**.
2. Choose the parts you want. The VST3 installs to `C:\Program Files\Common Files\VST3\MoonVerb.vst3`, the standalone app to `C:\Program Files\MoonVerb`.
3. Rescan plug-ins in your DAW.

Uninstall from *Settings → Apps*.

### Linux (x86_64)

* **Debian / Ubuntu:** `sudo apt install ./MoonVerb-<version>-linux-amd64.deb`. This installs the VST3 to `/usr/lib/vst3/` and the standalone app as `moonverb`.
* **Any distribution:** extract `MoonVerb-<version>-linux-amd64.tar.gz` and copy the plug-in where your DAW looks:
  ```sh
  tar -xzf MoonVerb-<version>-linux-amd64.tar.gz
  mkdir -p ~/.vst3 && cp -r lib/vst3/MoonVerb.vst3 ~/.vst3/      # the plug-in
  ./bin/moonverb                                                   # the standalone app
  ```
* The standalone app needs ALSA or JACK for audio; the plug-in needs nothing extra.

## Firmware images (ROMs)

MoonVerb runs the firmware of a Lexicon PCM 70 digital effects processor (software **V2.00** or **V3.01**). You need the contents of the unit's chips, dumped from a unit **you own**; this project
cannot provide or link them. It needs five images:

| Chip | Size | What it is |
|---|---|---|
| U62 | 32 KB | master program |
| U95 | 16 KB | slave program (also holds the factory programs) |
| U67 | 8 KB | opcode ROM |
| U48 | 512 bytes | control PROM |
| U49 | 32 bytes | sequencer PROM |

* The three large images (U62, U95, U67) must all come from the **same software version**, V2.00 or V3.01. V3.01 adds MIDI-clock tempo for the BPM programs; either version works.
* They must be **unmodified, known-good dumps**. MoonVerb recognises each by its content (a SHA-256 hash), not by its file name, so the names do not matter.

### How to load them

1. Put the five files in **one folder**. Sub-folders are searched, other files in the folder are ignored, and a folder holding both a V2 and a V3 set uses the newer complete one.
2. Open MoonVerb (insert it in your DAW, or start the standalone app). The display is blank and the plug-in is silent.
3. In the **rear-panel strip** under the front panel, click **LOAD ROM FOLDER** and choose that folder.
4. The five image rows list the files it found and the display says **POWERING ON**. The real firmware boots, which takes **about 10 seconds**; then the display shows a program name such as `0.0 MOD WOBBLE` and audio passes.
5. That's it. MoonVerb remembers the folder for every new instance and saves it in your project (the path only, never the images). **FORGET** clears it. If you move the folder, load it again.

If something is wrong the display and the strip say exactly what:

| Message | Meaning |
|---|---|
| `LOAD ROMS` | No folder chosen yet. |
| `MISSING V3.01 U62` | That image was not found in the folder (the version shown is the set it is trying to complete). |
| `UNKNOWN U95 1a2b3c4d` | A file of the right size is there, but it is not a known-good dump (damaged, modified, or from a different unit). The number is its hash. |
| `U67 IS V2.0, NEEDS V3.01` | The images come from different software versions. |

### Getting your first sound

1. Insert MoonVerb on an aux/send or an insert; send it audio. The unit has **one input**, so a stereo signal is summed. Raise **INPUT LEVEL** until the level lamps on the left flicker.
2. Pick a program with the keys, as on the hardware: make sure the **PGM** lamp is lit (press **PGM** if it is not), step the row with **ROW ▲ ▼**, press a digit **0–9**, then **LOAD**. (`1.3` is row 1, digit 3.) Press **REG** and do the same to load one of your own registers.
3. To edit, press **PGM** while a program is showing: this switches to parameter mode (both lamps go dark) and the display shows a parameter. Step through the parameters with **ROW** and the digits, and turn the
   **soft knob** (drag up and down, or use the mouse wheel) to change the one on the display. Press **PGM** again to go back. Every parameter is also a host parameter you can automate.
4. Hold **REG** and press **LOAD** to store what you've built into the chosen register. Registers are saved with your project.

The full guide is [docs/MoonVerb.md](docs/MoonVerb.md).

## Build

Needs macOS, CMake 3.22+ and the Xcode command-line tools. JUCE 9.0.3 is fetched by CMake.

```sh
cmake -S . -B build
cmake --build build --target MoonVerb_VST3 MoonVerb_AU MoonVerb_Standalone
scripts/install.sh          # run it from your own Terminal (see the note in the script)
```

The standalone app ends up in `build/MoonVerb_artefacts/Release/Standalone/`. Binaries are ad-hoc signed and arm64; pass
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` for a universal build. To give the plug-in to anyone else it needs a Developer ID signature and notarization.

## Installers

`.github/workflows/build.yml` builds on macOS, Windows and Linux, validates the plug-ins and produces an installer for each (macOS `.pkg` with VST3, Audio Unit and standalone, universal;
Windows `.exe` via Inno Setup with VST3 and standalone; Linux `.deb` and `.tar.gz`). A tag `v*` publishes them as a GitHub release. Locally: `scripts/package-macos.sh`, `scripts/package-linux.sh`,
`installer/windows/MoonVerb.iss`. macOS signing and notarization use the `MACOS_CODESIGN_ID` / `MACOS_INSTALLER_ID` secrets when present; without them the package is unsigned and macOS asks you to approve it once.

## Test

```sh
make -C tests test                                       # the core (machine, signal processor, control, panel logic), bit-for-bit
MOONVERB_ROMS=/path/to/your/images make -C tests test   # ...and against your firmware images (without them those checks print SKIP)
MOONVERB_ROMS=/path/to/your/images cmake --build build --target moonverb_plugin_test && build/moonverb_plugin_test_artefacts/Release/moonverb_plugin_test
```

The manual (`docs/MoonVerb.md`) lists the optional data the longer tests read (`MOONVERB_SYX`, `MOONVERB_ORACLE`, `MOONVERB_CELLS`, `MOONVERB_CELLS3`).
The installed plug-ins also pass [pluginval](https://github.com/Tracktion/pluginval) at strictness 10 and Apple's `auval`.

## Licence

GPLv3 (see `LICENSE`). JUCE is used under its AGPLv3 option, so a distributed binary carries AGPLv3 terms; the Z80 core in `src/vendor/z80` is MIT
(its licence is retained). The firmware images are never part of this repository: `.gitignore` refuses them and only their hashes are in the source.
