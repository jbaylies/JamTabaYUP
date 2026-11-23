# JamTabaYUP

A NINJAM client for playing online jam sessions, as a standalone app and as VST3,
AU and CLAP plugins, for macOS (universal: Apple Silicon and Intel) and Windows
(x64). Built with [YUP](https://github.com/kunitoki/yup), with a GPU-rendered UI.

**Status: Phase 1 (scaffolding).** The plugin and app build and open an empty
window. The NINJAM engine comes next. See [PORTING_PLAN.md](../PORTING_PLAN.md).

## Credits

- **[JamTaba 2](https://github.com/elieserdejesus/JamTaba)** by Elieser de Jesus
  and contributors. JamTabaYUP recreates JamTaba's workflow and layout. JamTaba
  carries no license, so none of its code, artwork or text is copied here:
  JamTabaYUP is a new implementation that uses JamTaba as a design reference.
- **[NINJAM](https://www.cockos.com/ninjam/)** by Cockos Incorporated
  ([source](https://github.com/justinfrankel/ninjam), GPL-2.0-or-later). The
  protocol code follows the NINJAM implementation.
- **[YUP](https://github.com/kunitoki/yup)** (ISC), with SDL3, the VST3, CLAP and
  AudioUnit SDKs, libogg and libvorbis.

## Differences from JamTaba

Left out on purpose: the looper, the built-in VST/AU host, video, jam recording,
the private server, MIDI sync and translations. See the plan for the full list.

## Building

Needs CMake 3.31+, plus Xcode's command line tools on macOS or Visual Studio 2022
(or its Build Tools) with the C++ workload on Windows.

```bash
git clone --recurse-submodules https://github.com/jbaylies/JamTabaYUP.git
cd JamTabaYUP
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

For a universal macOS build add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`. On
Windows, configure with `-G "Visual Studio 17 2022" -A x64` and build with
`cmake --build build --config Release --parallel`.

The first configure downloads YUP's dependencies. Each build installs the VST3
and CLAP plugins into the per-user plugin folders; pass
`-DJAMTABA_INSTALL_PLUGINS=OFF` to skip that.

`ninjam_cli --check` tests the engine's platform pieces with no GUI: Vorbis
encode and decode, the public server list over HTTPS, and a NINJAM handshake.

## License

GPL-2.0-or-later. See [LICENSE](../LICENSE).
