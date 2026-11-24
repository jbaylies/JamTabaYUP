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
- **Releases are built locally, as in philotaxis; no GitHub Actions.** The Mac
  builds the universal macOS release, the Windows machine (over SSH) builds the
  x64 one, and `make_release_zips.sh` zips both for `gh release create`. Why:
  the builds are unsigned (ad hoc), so CI adds no signing or notarization; the
  machines that build the release are the ones it is tested on (DAWs, audio
  interfaces); YUP's first configure downloads several SDKs, which CI repeats on
  every run; and macOS runners are slow and cost 10x minutes. The cost is that
  no build runs on push, so each phase ends with a build on both machines.
- Docs: `mkdocs gh-deploy` run locally pushes the built site to `gh-pages`.

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
  engine/              no GUI; yup_core + yup_events + yup_audio_basics + libvorbis only
    protocol/          Messages: NINJAM wire messages, framing, auth hash
    net/               PublicServerList (HTTPS + JSON)
    codec/             VorbisEncoder, VorbisDecoder (streaming), StreamResampler
    audio/             AudioEngine (audio thread: interval clock, remote slots,
                       capture, metronome, mix, meters), SpscQueue
    JamEngine          the engine thread: socket, session state, downloads,
                       uploads, events and commands
  plugin/              JamProcessor (yup::AudioProcessor), state save/load
  ui/                  editor, server browser, transport bar, strips, meters, chat, theme
tools/ninjam_cli/      headless client and self-tests
tools/test-server/     config for a local Cockos ninjamsrv
docs/                  MkDocs site, published to gh-pages
scripts/               packaging
```

### Engine design as built (Phase 2)

- **Two threads.** The *engine thread* (`JamEngine::Worker`, one per
  connection) owns the socket, parses messages, keeps the user/channel state,
  decodes received intervals, encodes ours and sends keepalives. The *audio
  thread* runs `AudioEngine::process()`. The message thread only sends commands
  (posted to the engine thread) and polls events.
- **Remote playback.** Each remote channel gets one of 64 preallocated slots.
  A received interval is decoded and resampled into a `RemoteInterval` buffer
  as its bytes arrive; after 8 KB (NINJAM's prebuffer) it is queued on the
  slot's SPSC queue, and the audio thread switches to it at its next interval
  start. A silent interval (zero GUID) queues `nullptr`. Played intervals go
  back to the engine thread through a "retired" queue to be freed there.
  Slots are released with a handshake (active, releasing, free) so the audio
  thread never sees a buffer disappear.
- **Upload.** The audio thread copies local input into fixed 256-frame chunks
  on an SPSC queue, with a marker at each interval start. The engine thread
  closes the previous Ogg stream, starts a new one with a random GUID (or sends
  a silent interval when not transmitting) and streams pages in messages of at
  most 8 KB. Devices above 48 kHz are resampled to 48 kHz before encoding.
- **Tempo.** Server BPM/BPI changes reach the audio thread as one atomic value
  and apply at the next interval start; the first one starts the clock.
- **Resampling** uses YUP's Lagrange interpolator, on the engine thread.
- **Licence agreements** are surfaced as an event; the engine waits for
  `acceptLicence()`. Nothing accepts a licence automatically.

## JamTaba features: what maps where

"Reimplemented" means new code with the same user-facing behaviour, using the
Qt-free replacement listed.

### Engine (JamTaba `src/Common`)

| JamTaba area | Qt it leaned on | JamTabaYUP replacement | status |
|---|---|---|---|
| `ninjam/client/ServerMessages`, `ClientMessages`, `Ninjam.h` | `QDataStream`, `QIODevice`, `QByteArray`, `QString` | `protocol/Messages.{h,cpp}`: little-endian reader/writer over `std::span<const std::byte>` / `std::vector<std::byte>`, UTF-8 `std::string`. Message ids and layouts from `mpb.h` | reimplemented |
| auth (`SHA1(SHA1(user:pass)+challenge)`, anonymous login, licence agreement) | `QCryptographicHash` | `yup::SHA1` | reimplemented |
| `ninjam/client/Service` (socket, keepalive, message dispatch, downloads keyed by GUID) | `QTcpSocket`, `QTimer`, signals | `JamEngine` engine thread: `yup::StreamingSocket` polled with short timeouts, keepalive by elapsed time. Events queue up for `pollEvents()` on the message thread; commands are posted to the engine thread | reimplemented |
| `User`, `UserChannel`, `ServerInfo` | `QString`, `QMap`, `QList` | `session/` structs with `std::string`, `std::vector`, `std::unordered_map` | reimplemented |
| `NinjamController` (interval position, BPM/BPI scheduled at interval start, prepared-to-transmit after 2 intervals, xmit scheduling, chat) | `QObject`, `QMutex`, `QThread` | `JamEngine` (engine thread) + `AudioEngine` clock (audio thread). Tempo changes reach the audio thread as one atomic value, applied at the interval start; transmit changes also apply at the interval start | reimplemented |
| encoding thread (`NinjamController::EncodingThread`) | `QThread`, `QMutex` | audio thread pushes 256-frame input chunks into an SPSC queue; the engine thread encodes and sends them | reimplemented |
| `audio/vorbis/VorbisEncoder`, `VorbisDecoder` | `QByteArray` | `codec/` on YUP's bundled `libvorbis`/`libogg` (`vorbisenc` included), streaming chunk by chunk as intervals download. FourCC `OGGv` | reimplemented |
| `audio/NinjamTrackNode` (decoded interval queue, resampling to device rate, play next interval at boundary) | `QList`, `QMutex` | `RemoteSlot` + `RemoteInterval` in `AudioEngine`: the engine thread decodes into interval buffers; the audio thread switches to the queued one at the interval start. Resampling with YUP's Lagrange interpolator | reimplemented |
| `audio/MetronomeTrackNode`, `MetronomeUtils` (beat/accent clicks, accent pattern) | `QFile` for samples | metronome in `AudioEngine`: synthesized click and accent, made in `prepare()`; accent on the first beat or every N beats | reimplemented |
| `audio/core/AudioPeak`, `AudioNode` gain/pan/mute/solo, `Filters` | | `AudioEngine` mix: per-strip gain, pan, mute, solo, peak + RMS meters published as atomics | reimplemented (low-cut / EQ filters dropped) |
| `audio/core/LocalInputNode`, `LocalInputGroup` (local channels, mono/stereo, xmit) | | `LocalChannel` in `AudioEngine`: input pair or mono input, xmit toggle, monitor toggle; name sent by `JamEngine` | reimplemented |
| `loginserver/LoginService` (public server list from `http://ninbot.com/app/servers.php`, refreshed periodically) | `QNetworkAccessManager`, `QJsonDocument`, `QTimer` | `net/PublicServerList`: `yup::URL::createInputStream` on a background thread, `yup::JSON::parse`, refresh timer on the message thread. Same endpoint and field names (`name`, `port`, `bpm`, `bpi`, `user_count`, `user_max`, `users[]`) | reimplemented |
| `persistence/Settings` (user name, last server, channel names, metronome, window layout) | `QSettings`, `QJson*` | processor state via `yup::DataTree` (saved by the host / the standalone state store); app-wide preferences in a small JSON file under `yup::File::userApplicationDataDirectory` | reimplemented, much smaller |
| `persistence/UsersDataCache` (remembered per-user gain/pan/mute) | `QFile` | same JSON preferences file | reimplemented |
| `log/Logging` | `qDebug` | `yup::Logger` | replaced |
| chat voting (`!vote bpm N`, `!vote bpi N`) and admin commands | | `JamEngine::voteBpm/voteBpi` send `MSG` chat lines; UI buttons beside the BPM/BPI fields | reimplemented |
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
   **Phase 2 results (2026-11-23):**

   | check | macOS | Windows |
   |---|---|---|
   | build, all targets | universal, no warnings from our code | x64 MSVC, 0 warnings |
   | `ninjam_cli selftest` | 427 checks passed | 427 checks passed |
   | two clients on a local `ninjamsrv` (tone each way, chat, BPM and BPI votes) | passed | — |
   | Windows client and Mac client on the Mac's `ninjamsrv` over the LAN (440 Hz and 550 Hz each way, chat) | passed | passed |
   | `ninjam_cli servers` (live public list) | 19 servers | — |

   Not covered yet: sessions on public servers (they show a licence agreement,
   and accepting it is the user's call), voice-chat and session-mode channels
   (received but not played), settings persistence and chord parsing (they
   belong with the UI in Phase 3).
   **Phase 3 results (2026-11-23):**

   - **Processor:** owns the engine, a `SessionModel` fed from a 30 Hz timer,
     a background `ServerListService`, and settings saved in the host project
     or the standalone state: name, last server, channel, input, faders and the
     remote mix per user.
   - **UI:** server browser (live list, seat dots, one-click Join, host:port +
     password form). Jam room: tempo controls with voting, beat bar with chord
     progressions, local strip (input choice in the standalone, XMIT, MON, M/S,
     fader, pan, meter), one group of strips per remote user (RX, M/S, fader,
     pan, meter, interval-received flash), chat and a licence prompt.
   - **macOS:** standalone checked against a local `ninjamsrv` with two tone
     bots: strips, meters, chords, chat and the licence prompt render
     correctly (screenshots). The microphone prompt shows our usage text.
   - **Engine fix found in this phase:** the engine sent keepalives while the
     licence was on screen, and the reference server drops a connection on any
     message before login. Keepalives now start after login.
   - **Audio thread:** scanned the whole `process()` path; it touches only
     atomics, preallocated buffers and SPSC queues.
   - **Windows:** all targets build with 0 warnings, the VST3 validator passes
     47/47, and selftest passes. The standalone can't start over SSH: in a
     non-interactive session Direct3D refuses the swap chain ("resource not
     available ... may become available later") and YUP aborts. philotaxis
     does the same, so it is the environment, not this app.
   - **Windows standalone on the real desktop:** launched in the logged-in
     session (via a one-off scheduled task) and captured: the browser loads
     the live list, and the jam room on the Mac's test server plays both tone
     bots with meters, chords and chat. It still shows YUP's default window
     icon; an app icon is a Phase 4 item.
   - **Not verified yet:** clicking through the UI (votes, dragging faders,
     accepting a licence in the window), small window sizes, and the plugins
     inside a DAW.
   - **Known gaps:** one local channel; the standalone exposes two inputs.
     Rooms whose list has no user names count their bot as a user.

4. **Polish and docs**: release scripts, pluginval / clap-validator
   / auval checks, MkDocs manual deployed to `gh-pages` with `mkdocs gh-deploy`.

   **Phase 4 results (2026-11-23):**

   | check | macOS | Windows |
   |---|---|---|
   | release build (`scripts/package_release.*`) | universal, macOS 11+, ad hoc signed; signatures verify, also after unzipping | x64, static CRT: no VC++ runtime DLLs needed |
   | AU: `auval -v aufx JmTb Sous` | AU VALIDATION SUCCEEDED | — |
   | VST3 SDK `validator` | 47 passed, 0 failed | 47 passed, 0 failed |
   | CLAP: clap-validator 0.4.1 (run by the user) | 44 tests: 30 passed, 3 failed, 11 skipped | 44 tests: 28 passed, 3 failed, 1 warning, 12 skipped |
   | app icon | ours (`resources/JamTabaYUP.icns`) | ours (embedded in the exe) |
   | zips (`scripts/make_release_zips.sh`) | `JamTabaYUP-0.1.0-macOS.zip`, 37 MB | `JamTabaYUP-0.1.0-Windows-x64.zip`, 16 MB |

   - **Manual:** https://jbaylies.github.io/JamTabaYUP/, written fresh (not
     copied from the JamTaba wiki), with screenshots of JamTabaYUP in which
     public users' names are removed. It covers the removed looper and the
     other dropped features in *Differences from JamTaba 2*.
   - **CLAP validation:** the same three tests fail on both platforms, all
     in YUP's CLAP wrapper (YUP_ISSUES.md entries 2 and 3):
     `process-audio-basic-in-place` ("Output port 0 has an in-place pair (0),
     but the corresponding input port does not exist"), and `param-set-events`
     and `param-set-no-cookies` (after `clap_plugin_params::flush()` the
     values didn't change; the only parameter is the wrapper's synthetic
     bypass, since JamTabaYUP defines none). Windows also warned on
     `process-audio-denormals` (6.69x slower with denormals), so
     `AudioEngine::process` now flushes denormals to zero; a re-run will show
     whether the warning is gone. Everything else passed: out-of-place
     processing, varying and random block sizes and sample rates,
     reset/reactivate, parameter fuzzing, state reproducibility, transport
     fuzzing. Scan time was 6 ms on the Mac and 12 ms on Windows.
   - **YUP findings** are listed in `YUP_ISSUES.md` for the user to pass to the
     YUP developer (no PRs to YUP). JamTabaYUP keeps its workarounds local:
     the standalone icon swap in CMake (entry 4), its own Info.plist (entry
     6), the centred text field (entry 8) and the VST3 folder icon in the
     packaging script (entry 10).
   - **Zips rebuilt** from `cd51e09` with flush-to-zero and our VST3 folder
     icon: signatures verify after unzipping, auval passes, and the VST3
     validator passes 47/47 on both platforms (macOS 37 MB, Windows 16 MB).
   - **Not done:** nothing is published as a GitHub release yet.
     ASIO on Windows (YUP has it, off by default; it needs Steinberg's ASIO
     SDK). Clicking through the UI and the plugins inside a DAW are still
     untested.

## Before the first release (2026-11-23)

- **Musicians on a globe.** JamTaba showed where each room's users are on a
  flat world map. JamTabaYUP shows them on a rotating 3D globe beside the
  room list: Natural Earth's 1:110m land (public domain, embedded by
  `tools/world/make_world_data.py`) on an orthographic projection, eased
  toward the room under the mouse, draggable. Each located musician gets a pin
  and a line to their name and country. The coordinates are the coarse ones
  the ninbot list already publishes (`lat`, `lon`); JamTabaYUP never sees
  or looks up IP addresses. Verified on macOS with fictional data
  (`tools/test-server/servers-sample.json`, via `JAMTABA_SERVER_LIST`);
  builds and self-tests pass on Windows, and the Windows standalone renders
  and turns the globe on the real desktop (captured from the logged-in
  session). Not verified by hand: hovering rooms to retarget the globe, and
  dragging it.
- **Zips rebuilt** from `60a0fec` with the globe: signatures verify after
  unzipping, auval passes, the VST3 validator passes 47/47 on both platforms,
  and THIRD_PARTY_LICENSES.txt includes Natural Earth (macOS 37 MB, Windows
  16 MB). Still not published as a GitHub release.
- **Crash fixes found while building it.**
  - YUP records drawing and renders it at the end of the frame, so a `Path`
    must not be changed after it is drawn. The globe reused one `Path` for
    every continent, which crashed inside Rive.
  - YUP calls `paint()` and `refreshDisplay()` on its render thread, and
    timers and mouse events on the message thread, with no synchronisation
    between them. The UI now reads an immutable published copy of the
    session model (`JamProcessor::uiModel()`), does all per-frame work in
    `JamEditor::refreshDisplay()` under a UI lock, and makes message-thread
    changes through `ui::runUiLocked()`, which holds the graphics context
    lock as well. Component tree traversal in YUP's `refreshDisplay` pass
    is still unsynchronised with message-thread changes (YUP_ISSUES.md).

