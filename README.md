# MoonVerb

A digital reverb and effects processor as a plug-in (VST3, Audio Unit and a standalone app, macOS).

MoonVerb does not imitate a vintage rack unit: it **runs the unit's own firmware**. Two emulated Z80 processors execute the original program,
which builds every reverb, delay and chorus patch exactly as the hardware did, and an emulated signal processor runs what the firmware
writes, behind the converters' real anti-alias and reconstruction filters. The front panel is laid out like the hardware's: level display,
input knob, 16-character display, soft knob, row keys, the ten digit keys, PGM, REG, LOAD and BYPASS, with the rear-panel switches in a
strip beneath it.

**The firmware is not included.** It belongs to the manufacturer. You supply your own dumps from a unit you own: point MoonVerb at the folder
that holds them (five files: master ROM, slave ROM, opcode ROM and two small PROMs). MoonVerb recognises them by SHA-256, says which is
missing or wrong, and remembers the folder. See [docs/MoonVerb.md](docs/MoonVerb.md).

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
