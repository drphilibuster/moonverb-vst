#!/bin/sh
# Build MoonVerb-<version>-linux-amd64.deb and a .tar.gz from the built plug-in and standalone app.
#   scripts/package-linux.sh [version]
set -e
cd "$(dirname "$0")/.."
VER="${1:-$(sed -n 's/^project(MoonVerb VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)}"
A=build/MoonVerb_artefacts/Release
OUT=build/dist; ROOT=build/debroot
rm -rf "$OUT" "$ROOT"; mkdir -p "$OUT" "$ROOT/DEBIAN" "$ROOT/usr/lib/vst3" "$ROOT/usr/bin" "$ROOT/usr/share/doc/moonverb"
cp -R "$A/VST3/MoonVerb.vst3" "$ROOT/usr/lib/vst3/"
cp "$A/Standalone/MoonVerb" "$ROOT/usr/bin/moonverb"
cp LICENSE README.md docs/MoonVerb.md "$ROOT/usr/share/doc/moonverb/"
cat > "$ROOT/DEBIAN/control" <<CTL
Package: moonverb
Version: $VER
Architecture: amd64
Maintainer: Moon Technologies <noreply@example.invalid>
Depends: libasound2 | libasound2t64, libfreetype6, libx11-6, libxext6, libxinerama1, libxrandr2, libxcursor1, libgl1
Section: sound
Priority: optional
Description: MoonVerb, a digital reverb that runs its own firmware
 VST3 plug-in and standalone app. The firmware images are not included;
 point MoonVerb at a folder holding your own.
CTL
dpkg-deb --root-owner-group --build "$ROOT" "$OUT/MoonVerb-$VER-linux-amd64.deb" >/dev/null
tar -C "$ROOT/usr" -czf "$OUT/MoonVerb-$VER-linux-amd64.tar.gz" lib/vst3 bin share/doc/moonverb
echo "$OUT/MoonVerb-$VER-linux-amd64.deb"
