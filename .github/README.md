# JamTabaYUP

A NINJAM client for playing online jam sessions, as a standalone app and as VST3,
AU and CLAP plugins, for macOS (universal: Apple Silicon and Intel) and Windows
(x64). Built with [YUP](https://github.com/kunitoki/yup), with a GPU-rendered UI.

Browse the public rooms, join one, and jam: the app and plugins play the
other musicians, send your input, show meters, the beat and chord
progressions, chat, and let you vote on BPM/BPI.

**[Read the manual](https://jbaylies.github.io/JamTabaYUP/)** for installing
and using it. The design and the state of each part are in
[PORTING_PLAN.md](../PORTING_PLAN.md).

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

## Download

Builds for macOS (universal, macOS 11+) and Windows (x64) will be on the
[releases page](https://github.com/jbaylies/JamTabaYUP/releases), with install
steps in each zip. They aren't code-signed: see
[Installing](https://jbaylies.github.io/JamTabaYUP/installing/).

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

### Testing the engine

`ninjam_cli` runs the engine with no GUI, driving it with a simulated audio
device:

```bash
build/ninjam_cli selftest     # protocol, auth, Vorbis, resampling, audio engine
build/ninjam_cli servers      # the public server list
```

For a full session, run Cockos's `ninjamsrv` with
[tools/test-server/ninjamsrv-test.cfg](../tools/test-server/ninjamsrv-test.cfg)
and connect two clients: one sends a tone, the other checks it hears it.

```bash
build/ninjam_cli connect localhost:20490 --name alice --tone 440 --seconds 30
build/ninjam_cli connect localhost:20490 --name bob --no-transmit --seconds 25 --expect-tone 440
```

To open the standalone app straight into a room, for scripted testing, set
`JAMTABA_CONNECT=host:port` (and optionally `JAMTABA_NAME`) before launching it.

To package a release, run `scripts/package_release.sh` on macOS and
`scripts/package_release.ps1` on Windows, copy the Windows
`build-release/stage` folder to `build-release/win-stage` on the Mac, then run
`scripts/make_release_zips.sh`.

Run `ninjam_cli` without arguments for the other options (chat, votes, WAV
capture). Public servers show a licence agreement first; `ninjam_cli` declines
it unless you pass `--accept-licence`.

## License

GPL-2.0-or-later. See [LICENSE](../LICENSE).
