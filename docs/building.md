# Building and testing

## Building

You need CMake 3.31 or later, plus Xcode's command line tools on macOS or
Visual Studio 2022 (or its Build Tools) with the C++ workload on Windows.

```bash
git clone --recurse-submodules https://github.com/jbaylies/JamTabaYUP.git
```

```bash
cd JamTabaYUP && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

For a universal macOS build, add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.
On Windows, configure with `-G "Visual Studio 17 2022" -A x64` and build with
`cmake --build build --config Release --parallel`.

The first configure downloads YUP's dependencies (SDL, the plugin SDKs).
Builds install the VST3 and CLAP plugins into your per-user plugin folders;
pass `-DJAMTABA_INSTALL_PLUGINS=OFF` to skip that.

## Layout

| folder | contents |
|---|---|
| `source/engine` | The NINJAM client with no GUI: protocol, connection thread, Vorbis, the real-time audio engine. |
| `source/plugin` | The audio processor, the session model, saved settings. |
| `source/ui` | The window: server browser, jam room, widgets. |
| `tools/ninjam_cli` | A command-line client and self-tests. |
| `tools/test-server` | A config for a local NINJAM server. |
| `scripts` | Release packaging. |
| `docs` | This manual. |

[PORTING_PLAN.md](https://github.com/jbaylies/JamTabaYUP/blob/main/PORTING_PLAN.md)
describes the design and the state of each part.

## Testing the engine

`ninjam_cli` runs the engine with a simulated audio device:

```bash
build/ninjam_cli selftest
```

```bash
build/ninjam_cli servers
```

For full sessions, build Cockos `ninjamsrv`
([source](https://github.com/justinfrankel/ninjam), `ninjam/server`) and run
it from `tools/test-server` with `ninjamsrv-test.cfg`. Then connect two
clients, one sending a tone and one checking it hears it:

```bash
build/ninjam_cli connect localhost:20490 --name alice --tone 440 --seconds 30
```

```bash
build/ninjam_cli connect localhost:20490 --name bob --no-transmit --seconds 25 --expect-tone 440
```

To open the standalone app straight into a room, set `JAMTABA_CONNECT` (and
optionally `JAMTABA_NAME`) before launching it:

```bash
JAMTABA_CONNECT=localhost:20490 JAMTABA_NAME=me build/JamTabaYUP.app/Contents/MacOS/JamTabaYUP
```

## Releases

Releases are built locally, on macOS and on Windows:

1. `scripts/package_release.sh` on the Mac.
2. `scripts\package_release.ps1` on Windows.
3. Copy the Windows `build-release\stage` folder to `build-release/win-stage`
   on the Mac.
4. `scripts/make_release_zips.sh` writes both zips to `build-release/dist`.

## The manual

The manual is built with [MkDocs](https://www.mkdocs.org) and the Material
theme, from `docs/` and `mkdocs.yml`:

```bash
pip install mkdocs mkdocs-material && mkdocs serve
```

`mkdocs gh-deploy` publishes it to the `gh-pages` branch.
