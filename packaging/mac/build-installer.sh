#!/bin/bash
# Builds a universal (Apple Silicon + Intel) Spectra.vst3 and wraps it in a double-click installer:
#   dist/Spectra-<version>-mac.pkg
# Usage: packaging/mac/build-installer.sh            (uses `cmake` on PATH, or set CMAKE=/path/to/cmake)
set -euo pipefail
export COPYFILE_DISABLE=1   # no "._" metadata files inside the installer

cd "$(dirname "$0")/../.."
ROOT="$PWD"
CMAKE="${CMAKE:-cmake}"
VERSION="$(sed -n 's/^project(Spectra VERSION \([0-9.]*\)).*/\1/p' CMakeLists.txt)"
BUILD="$ROOT/build-release-mac"
WORK="$BUILD/pkg"
OUT="$ROOT/dist/Spectra-$VERSION-mac.pkg"

"$CMAKE" -B "$BUILD" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DSPECTRA_COPY_PLUGIN=OFF ${JUCE_DIR:+-DJUCE_DIR="$JUCE_DIR"}
"$CMAKE" --build "$BUILD" --target Spectra_VST3 -j8

PLUGIN="$BUILD/Spectra_artefacts/Release/VST3/Spectra.vst3"
lipo -info "$PLUGIN/Contents/MacOS/Spectra"

rm -rf "$WORK" && mkdir -p "$WORK/root" "$WORK/resources"
cp -R "$PLUGIN" "$WORK/root/"
xattr -cr "$WORK/root"
codesign --force --deep --sign - "$WORK/root/Spectra.vst3"   # ad-hoc signature (free; not notarized)

# Always install to /Library/Audio/Plug-Ins/VST3, never "relocate" onto some other copy of the bundle.
pkgbuild --analyze --root "$WORK/root" "$WORK/components.plist"
plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/components.plist"

pkgbuild --root "$WORK/root" --component-plist "$WORK/components.plist" \
    --identifier com.shreyas.spectra.vst3 --version "$VERSION" \
    --install-location "/Library/Audio/Plug-Ins/VST3" "$WORK/Spectra.pkg"

sed "s/@VERSION@/$VERSION/g" packaging/mac/welcome.html > "$WORK/resources/welcome.html"
sed "s/@VERSION@/$VERSION/g" packaging/mac/distribution.xml > "$WORK/distribution.xml"

mkdir -p "$ROOT/dist"
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" \
    --package-path "$WORK" "$OUT"

echo "Built $OUT"
