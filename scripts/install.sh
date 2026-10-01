#!/bin/sh
# Install the built plug-ins for the current user: VST3 and Audio Unit. Run it from your own Terminal.
#
# macOS refuses to load a plug-in that was written by a sandboxed process (an IDE's or an assistant's build step stamps its files with an
# "unverified origin" flag) and says it "can't verify it's free of malware". Copying the bundle from your own shell without that flag, and
# signing it ad hoc, is enough on your own machine. (To hand the plug-in to anyone else you need a Developer ID signature and notarization.)
#
#   cmake --build build --target MoonVerb_VST3 MoonVerb_AU && scripts/install.sh
set -e
cd "$(dirname "$0")/.."
A=build/MoonVerb_artefacts/Release
for f in "$A/VST3/MoonVerb.vst3/Contents/MacOS/MoonVerb" "$A/AU/MoonVerb.component/Contents/MacOS/MoonVerb"; do
  [ -f "$f" ] || { echo "not built yet: $f"; exit 1; }
done
mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/Components"
rm -rf "$HOME/Library/Audio/Plug-Ins/VST3/MoonVerb.vst3" "$HOME/Library/Audio/Plug-Ins/Components/MoonVerb.component"
cp -RX "$A/VST3/MoonVerb.vst3" "$HOME/Library/Audio/Plug-Ins/VST3/"
cp -RX "$A/AU/MoonVerb.component" "$HOME/Library/Audio/Plug-Ins/Components/"
codesign --force --deep -s - "$HOME/Library/Audio/Plug-Ins/VST3/MoonVerb.vst3"
codesign --force --deep -s - "$HOME/Library/Audio/Plug-Ins/Components/MoonVerb.component"
echo "installed: ~/Library/Audio/Plug-Ins/VST3/MoonVerb.vst3 and ~/Library/Audio/Plug-Ins/Components/MoonVerb.component"
