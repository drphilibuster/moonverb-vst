#!/bin/sh
# Build MoonVerb-<version>-macOS.pkg from the built plug-ins: VST3, Audio Unit and the standalone app as separately selectable choices.
#   scripts/package-macos.sh [version]
# Optional signing (CI sets these from secrets; without them the package is unsigned and macOS asks you to approve it once):
#   MOONVERB_CODESIGN_ID      "Developer ID Application: ..."   signs the bundles (hardened runtime)
#   MOONVERB_INSTALLER_ID     "Developer ID Installer: ..."     signs the package
#   MOONVERB_NOTARY_PROFILE   notarytool keychain profile       notarizes and staples the package
set -e
cd "$(dirname "$0")/.."
VER="${1:-$(sed -n 's/^project(MoonVerb VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)}"
A=build/MoonVerb_artefacts/Release
OUT=build/dist; STAGE=build/pkgstage
rm -rf "$OUT" "$STAGE"; mkdir -p "$OUT" "$STAGE/vst3/Library/Audio/Plug-Ins/VST3" "$STAGE/au/Library/Audio/Plug-Ins/Components" "$STAGE/app/Applications" "$STAGE/pkgs"
cp -R "$A/VST3/MoonVerb.vst3" "$STAGE/vst3/Library/Audio/Plug-Ins/VST3/"
cp -R "$A/AU/MoonVerb.component" "$STAGE/au/Library/Audio/Plug-Ins/Components/"
cp -R "$A/Standalone/MoonVerb.app" "$STAGE/app/Applications/"
if [ -n "$MOONVERB_CODESIGN_ID" ]; then
  for b in "$STAGE/vst3/Library/Audio/Plug-Ins/VST3/MoonVerb.vst3" "$STAGE/au/Library/Audio/Plug-Ins/Components/MoonVerb.component" "$STAGE/app/Applications/MoonVerb.app"; do
    codesign --force --deep --options runtime --timestamp -s "$MOONVERB_CODESIGN_ID" "$b"
  done
fi
for c in vst3 au; do pkgbuild --root "$STAGE/$c" --identifier "com.moontechnologies.moonverb.$c" --version "$VER" --install-location / "$STAGE/pkgs/MoonVerb-$(echo $c | tr a-z A-Z).pkg" >/dev/null; done
pkgbuild --root "$STAGE/app" --identifier com.moontechnologies.moonverb.standalone --version "$VER" --install-location / "$STAGE/pkgs/MoonVerb-Standalone.pkg" >/dev/null
mkdir -p "$STAGE/res"; cp installer/macos/welcome.txt LICENSE "$STAGE/res/"
SIGN=""; [ -n "$MOONVERB_INSTALLER_ID" ] && SIGN="--sign $MOONVERB_INSTALLER_ID"
productbuild --distribution installer/macos/distribution.xml --resources "$STAGE/res" --package-path "$STAGE/pkgs" $SIGN "$OUT/MoonVerb-$VER-macOS.pkg" >/dev/null
if [ -n "$MOONVERB_NOTARY_PROFILE" ]; then
  xcrun notarytool submit "$OUT/MoonVerb-$VER-macOS.pkg" --keychain-profile "$MOONVERB_NOTARY_PROFILE" --wait
  xcrun stapler staple "$OUT/MoonVerb-$VER-macOS.pkg"
fi
echo "$OUT/MoonVerb-$VER-macOS.pkg"
