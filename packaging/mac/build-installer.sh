#!/bin/bash
# Builds universal (Apple Silicon + Intel) VST3 and Audio Unit plugins and wraps them in a double-click installer:
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
"$CMAKE" --build "$BUILD" --target Spectra_VST3 Spectra_AU -j8

ARTEFACTS="$BUILD/Spectra_artefacts/Release"
lipo -info "$ARTEFACTS/VST3/Spectra.vst3/Contents/MacOS/Spectra"
lipo -info "$ARTEFACTS/AU/Spectra.component/Contents/MacOS/Spectra"

rm -rf "$WORK" && mkdir -p "$WORK/resources"

# One component package per format: name, bundle, install folder.
make_component () {
    local id="$1" bundle="$2" location="$3"
    local root="$WORK/root-$id"
    mkdir -p "$root"
    cp -R "$bundle" "$root/"
    xattr -cr "$root"
    codesign --force --deep --sign - "$root/$(basename "$bundle")"   # ad-hoc signature (free; not notarized)

    # Always install to the standard folder, never "relocate" onto some other copy of the bundle.
    pkgbuild --analyze --root "$root" "$WORK/$id.plist"
    plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/$id.plist"
    pkgbuild --root "$root" --component-plist "$WORK/$id.plist" \
        --identifier "com.shreyas.spectra.$id" --version "$VERSION" \
        --install-location "$location" "$WORK/Spectra-$id.pkg"
}

make_component vst3 "$ARTEFACTS/VST3/Spectra.vst3"     "/Library/Audio/Plug-Ins/VST3"
make_component au   "$ARTEFACTS/AU/Spectra.component"  "/Library/Audio/Plug-Ins/Components"

sed "s/@VERSION@/$VERSION/g" packaging/mac/welcome.html > "$WORK/resources/welcome.html"
sed "s/@VERSION@/$VERSION/g" packaging/mac/distribution.xml > "$WORK/distribution.xml"

mkdir -p "$ROOT/dist"
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" \
    --package-path "$WORK" "$OUT"

echo "Built $OUT"
