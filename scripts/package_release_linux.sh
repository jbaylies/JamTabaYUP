#!/bin/sh
# Builds the Linux release for the machine it runs on (x86_64 or aarch64), on
# Linux. Leaves the stripped plugins and app in build-release/linux-<arch>-stage/,
# where scripts/make_release_zips.sh picks them up.
#
#   scripts/package_release_linux.sh
#
# Build on the oldest Linux the release should run on: the binaries need that
# glibc or newer. The release is built on Ubuntu 22.04 (glibc 2.35). Needs
# clang, ninja, CMake 3.31 and YUP's Linux packages.
#
# Building with JAMTABA_INSTALL_PLUGINS off leaves ~/.vst3 and ~/.clap alone.
# No LV2 until YUP's LV2 client is fixed (see CMakeLists.txt).

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
arch=$(uname -m)
build="$root/build-release/linux-$arch"
stage="$root/build-release/linux-$arch-stage"

cmake -S "$root" -B "$build" -G Ninja \
    -DYUP_DIR="$root/yup" \
    -DCMAKE_BUILD_TYPE=Release \
    -DJAMTABA_INSTALL_PLUGINS=OFF \
    -DJAMTABA_BUILD_CLI=OFF
cmake --build "$build"

rm -rf "$stage"
mkdir -p "$stage"
cp -R "$build/VST3/Release/JamTabaYUP.vst3" "$stage/JamTabaYUP.vst3"
cp "$build/CLAP/JamTabaYUP.clap" "$stage/JamTabaYUP.clap"
cp "$build/JamTabaYUP" "$stage/JamTabaYUP"

# Release builds keep their symbols; the release doesn't need them
strip --strip-unneeded \
    "$stage"/JamTabaYUP.vst3/Contents/*-linux/*.so \
    "$stage/JamTabaYUP.clap" \
    "$stage/JamTabaYUP"

echo "Staged in $stage"
