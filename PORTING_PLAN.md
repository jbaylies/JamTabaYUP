# JamTabaYUP porting plan

JamTabaYUP is a NINJAM client for macOS (universal) and Windows (x64), built with
[YUP](https://github.com/kunitoki/yup) as a standalone app and as VST3, AU (macOS)
and CLAP plugins. It takes its workflow and look from
[JamTaba 2](https://github.com/elieserdejesus/JamTaba).

## Licensing decision: reimplement, don't copy

Upstream JamTaba has no license. The repository has no LICENSE file, GitHub
reports none, and the source files carry no headers. Its code, wiki text and wiki
images are therefore all rights reserved, and a port of them can't be relicensed
as GPL by us.

So JamTabaYUP is a **clean reimplementation**:

- **Protocol and engine** are written from the NINJAM protocol as implemented by
  Cockos NINJAM ([justinfrankel/ninjam](https://github.com/justinfrankel/ninjam),
  GPL-2.0-or-later: `mpb.h`, `netmsg.h`, `njclient.cpp`). Where we follow that
  code closely, it is derived GPL code and attributed as such.
- **JamTaba is a behaviour and UX reference only**: which features exist, how the
  screens are laid out, what the controls do. No JamTaba source, Qt `.ui` files,
  stylesheets, icons, translations or wiki text are copied into this repository.
- **Docs** are written fresh in our own words. The wiki's structure (which topics
  a user manual needs) guides the outline. The wiki's screenshots are not reused:
  new screenshots come from JamTabaYUP itself.
- **License: GPL-2.0-or-later**, the same as NINJAM. "Or later" matters: the AU
  build links Apple's AudioUnitSDK (Apache 2.0), which is compatible with GPLv3
  but not GPLv2-only. Everything else linked is permissive: YUP (ISC), SDL3
  (zlib), VST3 SDK (MIT), CLAP (MIT), libogg and libvorbis (BSD), HarfBuzz,
  Rive, SheenBidi.

`README.md` credits JamTaba (Elieser de Jesus and contributors) for the design and
Cockos for NINJAM and its protocol.

## Reference repositories

Cloned outside the repository, in `../JamTabaYUP-refs/`:

| repo | used for |
|---|---|
| `jbaylies/philotaxis_moire_sequencer` | CMake layout, YUP integration, warnings, install/bundling, release scripts |
| `kunitoki/yup` | framework (vendored here as the `yup/` submodule) |
| `justinfrankel/ninjam` | protocol: message layouts, auth, interval timing, OGGv framing |
| `elieserdejesus/JamTaba` | feature list and UX only |
| `elieserdejesus/JamTaba.wiki` | outline of the user manual only |

## Build system (following philotaxis_moire_sequencer)

- `CMakeLists.txt` at the root, CMake 3.31+, `yup/` as a git submodule pinned to
  the commit philotaxis builds with (`4792e50e`), bumped deliberately later.
- One `yup_audio_plugin()` target, `jamtaba`, creating `jamtaba_standalone_plugin`,
  `jamtaba_vst3_plugin`, `jamtaba_au_plugin`, `jamtaba_clap_plugin` and the
  `jamtaba_shared` core they link. C++20.
- AU registered as an effect (`aufx`): audio in from the DAW, the jam mix out.
- `JAMTABA_INSTALL_PLUGINS` option (default ON) copying real bundles into the
  per-user plugin folders after each build, re-signed ad hoc on macOS, renamed
  inner VST3 binary on Windows. Off for release packaging.
- The standalone `.app` gets its own `Info.plist` (YUP's template plus
  `NSMicrophoneUsageDescription` and the music app category). Without that key
  macOS gives the app silence from the microphone.
- Strict warnings on our own sources only (`/W4` on MSVC, `-Wall -Wextra
  -Wpedantic -Wshadow -Wconversion …` on Clang), YUP treated as system headers.
- `jamtaba_engine_tests` / `ninjam_cli`: a console target (`yup_standalone_app`
  with `TARGET_CONSOLE ON`) that compiles the same engine sources with only
  `yup_core`, `yup_events`, `yup_audio_basics` and `libvorbis`. This proves the
  engine has no GUI dependency.
- Release: `scripts/package_release.sh` (universal arm64+x86_64, macOS 11+,
  ad hoc signed), `scripts/package_release.ps1` (VS 2022, x64, static CRT),
  `scripts/make_release_zips.sh`, `scripts/third_party_licenses.sh`.
- philotaxis has no GitHub Actions; JamTabaYUP adds a macOS + Windows build
  workflow and a Pages workflow in Phase 4.

### Toolchains verified in Phase 1

| machine | toolchain |
|---|---|
| macOS 15.8 (Apple M4) | CMake 4.4.4, Xcode 26.2, Apple clang 17, Unix Makefiles |
| `Sousastep-Windows.local` | CMake 4.4.4, Git 2.54, VS 2022 Build Tools (MSVC 14.44), generator `Visual Studio 17 2022`; checkout at `C:\Users\jbayl\Music\JamTabaYUP`, next to philotaxis |

### Phase 1 results (2026-11-23)

| check | macOS | Windows |
|---|---|---|
| configure + build, all formats | universal arm64 + x86_64: `.app`, `.vst3`, `.component`, `.clap` | x64: `.exe`, `.vst3`, `.clap`; 0 errors, no warnings from our sources |
| `ninjam_cli --check` (Vorbis round trip, HTTPS server list, NINJAM auth challenge from ninbot.com:2049) | passed | passed |
| standalone launches | stays up, `NSMicrophoneUsageDescription` in Info.plist | not run interactively |
| VST3 SDK `validator` | Phase 4 | 47 passed, 0 failed |

Findings that shape Phase 2:

- The server list is fetched over **HTTPS** (`https://ninbot.com/app/servers.php`).
  Plain HTTP would be blocked by App Transport Security inside macOS DAW hosts,
  whose Info.plist we don't control.
- Some list entries carry `"user_max": "NaN"`; parse defensively.
- ninbot.com advertises a 120 s keepalive and a licence agreement, so the client
  must handle the licence prompt before auth.
- Thirdparty YUP modules are named bare (`libvorbis`), not `yup::libvorbis`.

## Source layout

```
source/
  engine/              no GUI; yup_core + yup_events + libvorbis only
    protocol/          NINJAM wire messages, framing, auth
    net/               NinjamConnection (socket thread), PublicServerList (HTTP + JSON)
    codec/             VorbisIntervalEncoder / VorbisIntervalDecoder
    session/           JamSession: users, channels, interval clock, scheduled BPM/BPI changes
    audio/             RemoteChannel playback, Metronome, JamMixer, lock-free queues
  plugin/              JamProcessor (yup::AudioProcessor), state save/load
  ui/                  editor, server browser, transport bar, strips, meters, chat, theme
tools/ninjam_cli/      headless client / test harness
docs/                  MkDocs site, published to gh-pages
scripts/               packaging
```

## JamTaba features: what maps where

"Reimplemented" means new code with the same user-facing behaviour, using the
Qt-free replacement listed.

### Engine (JamTaba `src/Common`)

| JamTaba area | Qt it leaned on | JamTabaYUP replacement | status |
|---|---|---|---|
| `ninjam/client/ServerMessages`, `ClientMessages`, `Ninjam.h` | `QDataStream`, `QIODevice`, `QByteArray`, `QString` | `protocol/Messages.{h,cpp}`: little-endian reader/writer over `std::span<const std::byte>` / `std::vector<std::byte>`, UTF-8 `std::string`. Message ids and layouts from `mpb.h` | reimplemented |
| auth (`SHA1(SHA1(user:pass)+challenge)`, anonymous login, licence agreement) | `QCryptographicHash` | `yup::SHA1` | reimplemented |
| `ninjam/client/Service` (socket, keepalive, message dispatch, downloads keyed by GUID) | `QTcpSocket`, `QTimer`, signals | `net/NinjamConnection`: `yup::StreamingSocket` on its own `yup::Thread`, blocking reads with timeouts, keepalive by elapsed time. Events go to the message thread through a queue and `yup::AsyncUpdater`; listeners are plain C++ interfaces / `std::function` | reimplemented |
| `User`, `UserChannel`, `ServerInfo` | `QString`, `QMap`, `QList` | `session/` structs with `std::string`, `std::vector`, `std::unordered_map` | reimplemented |
| `NinjamController` (interval position, BPM/BPI scheduled at interval start, prepared-to-transmit after 2 intervals, xmit scheduling, chat) | `QObject`, `QMutex`, `QThread` | `session/JamSession` (message thread) + `audio/IntervalClock` (audio thread). Changes reach the audio thread as values in a lock-free SPSC queue, applied at the interval boundary | reimplemented |
| encoding thread (`NinjamController::EncodingThread`) | `QThread`, `QMutex` | `codec/EncoderThread`: audio thread pushes input blocks into a lock-free FIFO; a worker encodes and hands packets to `NinjamConnection` | reimplemented |
| `audio/vorbis/VorbisEncoder`, `VorbisDecoder` | `QByteArray` | `codec/` on YUP's bundled `libvorbis`/`libogg` (`vorbisenc` included), streaming chunk by chunk as intervals download. FourCC `OGGv` | reimplemented |
| `audio/NinjamTrackNode` (decoded interval queue, resampling to device rate, play next interval at boundary) | `QList`, `QMutex` | `audio/RemoteChannel`: decoder thread fills preallocated interval buffers; audio thread swaps them at the boundary through atomics. Resampling with a windowed-sinc / Lagrange interpolator in plain C++ | reimplemented |
| `audio/MetronomeTrackNode`, `MetronomeUtils` (beat/accent clicks, accent pattern) | `QFile` for samples | `audio/Metronome`: synthesized click and accent, preallocated per sample rate; accent every N beats or custom beats | reimplemented |
| `audio/core/AudioPeak`, `AudioNode` gain/pan/mute/solo, `Filters` | | `audio/JamMixer`: per-strip gain, pan, mute, solo, peak + RMS meters published as atomics | reimplemented (low-cut / EQ filters dropped) |
| `audio/core/LocalInputNode`, `LocalInputGroup` (local channels, mono/stereo, xmit) | | `audio/LocalChannel`: choose input pair or mono input, xmit toggle, monitor toggle, name | reimplemented |
| `loginserver/LoginService` (public server list from `http://ninbot.com/app/servers.php`, refreshed periodically) | `QNetworkAccessManager`, `QJsonDocument`, `QTimer` | `net/PublicServerList`: `yup::URL::createInputStream` on a background thread, `yup::JSON::parse`, refresh timer on the message thread. Same endpoint and field names (`name`, `port`, `bpm`, `bpi`, `user_count`, `user_max`, `users[]`) | reimplemented |
| `persistence/Settings` (user name, last server, channel names, metronome, window layout) | `QSettings`, `QJson*` | processor state via `yup::DataTree` (saved by the host / the standalone state store); app-wide preferences in a small JSON file under `yup::File::userApplicationDataDirectory` | reimplemented, much smaller |
| `persistence/UsersDataCache` (remembered per-user gain/pan/mute) | `QFile` | same JSON preferences file | reimplemented |
| `log/Logging` | `qDebug` | `yup::Logger` | replaced |
| chat voting (`!vote bpm N`, `!vote bpi N`) and admin commands | | `JamSession::voteBpm/voteBpi` send `MSG` chat lines; UI buttons beside the BPM/BPI fields | reimplemented |
| chord progressions in chat (`gui/chords`) | `QRegularExpression` | a small hand-written parser; shown in the chat panel and on the beat bar | reimplemented |
| **`looper/*`** (Looper, layers, states, persistence) and **`LooperWindow`** | | none | **dropped by design** |
| `recorder/*` (multitrack jam recording, Reaper project, clipsort log) | `QFile`, `QDir` | none | dropped (possible later feature) |
| `video/*` (FFmpeg camera streaming) | Qt Multimedia, FFmpeg | none | dropped |
| `audio/RoomStreamerNode`, `Mp3Decoder`, `minimp3` (listen to a room's MP3 stream before joining) | `QNetworkAccessManager` | none | dropped (possible later feature) |
| `ninjam/server/Server` (built-in private server) | `QTcpServer` | none | dropped |
| `upnp/*`, `loginserver/natmap`, `_geo/*` (IP to country flags) | | none | dropped |
| `vst/*`, `Scanners`, `Standalone/vst`, `PluginFinder` (hosting VST/AU plugins inside the standalone) | | none | dropped |
| `midi/*` (RtMidi, MIDI sync out) | RtMidi | none in v1. The plugin gets host transport; MIDI clock out is a possible later feature | dropped |
| `performance/*`, `log/stackwalker`, crash report dialog | | none | dropped |
| `audio/core/AudioDriver` (PortAudio) | PortAudio | YUP's standalone `AudioDeviceManager` and its settings panel (CoreAudio, WASAPI, ASIO where available) | replaced |
| `Libs/SingleApplication` | | not needed | dropped |
| translations (`translations/*.ts`) | Qt Linguist | English only for now | dropped |

### UI (JamTaba `src/Common/gui`)

| JamTaba view | JamTabaYUP component |
|---|---|
| `MainWindow` + `JamRoomViewPanel` (public room list: name, users with flags, BPM/BPI, enter button) | `ui/ServerBrowser`: live list refreshed every 30 s, user names and counts, BPM/BPI, one-click Join, plus a host:port + password "Connect" form |
| `UserNameDialog` | name field in the browser header, remembered |
| `NinjamPanel` + `MetronomePanel` + `intervalProgress` | `ui/TransportBar`: BPM and BPI with vote buttons, interval progress (circle / line / pie styles), beat counter, metronome gain/pan/mute and accent setting, master gain and meter |
| `LocalTrackGroupView`, `LocalTrackView`, `BaseTrackView` | `ui/LocalStrip`: input selector (standalone) or DAW bus (plugin), mono/stereo, xmit toggle, monitor toggle, gain, pan, mute, solo, peak/RMS meter, channel name |
| `NinjamTrackGroupView`, `NinjamTrackView` (per remote user, channels side by side) | `ui/UserStrip` with one `ui/ChannelStrip` per channel: gain, pan, mute, solo, receive toggle, interval download progress, peak/RMS meter, user colour |
| `NinjamRoomWindow` | `ui/JamRoomView`: transport bar on top, local strips left, remote users scrolling horizontally, chat on the right |
| `chat/*` | `ui/ChatPanel`: collapsible; public chat, server topic and join/part log, BPM/BPI vote messages, chord progression notices, text input |
| `PreferencesDialog` | standalone: YUP's audio settings overlay. Plus an app preferences page (metronome sound, meter style) |
| `ThemeLoader`, `css/themes/*` | one `ui/JamTheme` (dark, JamTaba-like palette, our own colours), drawn with YUP's GPU renderer and scaled for HiDPI |
| `LooperWindow` | none (dropped) |
| `PrivateServerWindow`, `PluginScanDialog`, `CrashReportDialog`, `BusyDialog`, `screensaver` | none (features dropped) |

## Real-time rules for the audio thread

- No locks, no allocation, no logging, no UI or socket calls in `processBlock`.
- Everything the audio thread touches is preallocated in `prepareToPlay` for the
  largest interval (max BPI × slowest BPM × sample rate) or swapped in through
  atomics by a non-real-time thread that also owns the old buffer's freeing.
- Thread to thread: SPSC FIFOs (single writer, single reader, atomics only),
  atomics for parameters and meters.
- Interval boundaries are sample-accurate inside a block.

## Plugin specifics

- Input bus: stereo from the DAW, sent as one local channel (optionally a second
  stereo bus for a second channel). Output: stereo jam mix, with an option to
  include or exclude the local monitor.
- Host transport is read for display only. NINJAM keeps its own interval clock
  (the server sets tempo), so the plugin does not try to follow the DAW.

## Phases

1. **Audit and scaffolding** (this phase): references cloned, this plan, GitHub
   repo `jbaylies/JamTabaYUP`, CMake skeleton with YUP + libvorbis, an empty
   editor and processor, and the console harness linking the engine modules.
   Verified on macOS and on Windows over SSH.
2. **Headless engine**: protocol, connection, Vorbis pipeline, interval clock,
   remote channels, metronome, mixer, public server list. `ninjam_cli` connects
   to a public server (e.g. ninbot.com), lists users and channels, follows
   BPM/BPI, decodes received intervals, sends chat and votes, and with
   `--transmit` uploads a test tone. Unit tests for message encode/decode and
   auth hashing. Verified on both platforms.
3. **UI and wrappers**: everything in the UI table above, audio-thread safety
   review.
4. **Polish and docs**: release scripts, GitHub Actions, pluginval / clap-validator
   / auval checks, MkDocs manual on `gh-pages`.
