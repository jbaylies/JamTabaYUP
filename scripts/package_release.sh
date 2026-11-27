#!/bin/sh
# Builds the macOS release: universal (Apple Silicon and Intel), macOS 11 or
# later, signed ad hoc. Leaves the renamed bundles in build-release/stage/.
#
#   scripts/package_release.sh
#
# Building with JAMTABA_INSTALL_PLUGINS off leaves the plugins installed in
# ~/Library/Audio/Plug-Ins alone.

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
build="$root/build-release"
stage="$build/stage"

cmake -S "$root" -B "$build" \
    -DYUP_DIR="$root/yup" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DJAMTABA_INSTALL_PLUGINS=OFF \
    -DJAMTABA_BUILD_CLI=OFF
cmake --build "$build" --config Release -j

rm -rf "$stage"
mkdir -p "$stage"
ditto "$build/jamtaba_au_plugin.component" "$stage/JamTabaYUP.component"
ditto "$build/VST3/Release/jamtaba_vst3_plugin.vst3" "$stage/JamTabaYUP.vst3"
ditto "$build/jamtaba_clap_plugin.clap" "$stage/JamTabaYUP.clap"
ditto "$build/JamTabaYUP.app" "$stage/JamTabaYUP.app"

# YUP's bundles carry signatures that no longer match their contents
for bundle in "$stage"/*; do
    codesign --force --deep --sign - "$bundle"
    codesign --verify --strict "$bundle"
done

echo "Staged in $stage"
