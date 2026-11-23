# Differences from JamTaba 2

JamTabaYUP takes its workflow and layout from
[JamTaba 2](https://github.com/elieserdejesus/JamTaba): a browser of public
rooms, then a jam room with your channel, everyone else's strips, the interval
progress and chat. It isn't a port of JamTaba's code. JamTaba carries no
license, so JamTabaYUP was written from scratch on
[YUP](https://github.com/kunitoki/yup), with its network code following
Cockos NINJAM.

## New

- A GPU-drawn interface that stays sharp on Retina and high-DPI displays.
- CLAP and VST3 plugins on macOS, alongside AU; VST3 and CLAP on Windows.
- Universal macOS builds for Apple Silicon and Intel.

## Left out on purpose

| JamTaba 2 feature | in JamTabaYUP |
|---|---|
| The looper (recording and replaying intervals) | Not included. |
| Hosting VST/AU plugins inside the standalone app | Not included: use the plugin version in a DAW for effects. |
| Video (camera streaming) | Not included. |
| Multitrack recording of jams, Reaper project export | Not included: record the plugin's output in your DAW. |
| Built-in private server | Not included: run Cockos `ninjamsrv`. |
| Listening to a room's stream before joining | Not included. |
| MIDI sync out | Not included. |
| Voice chat channels | Received channels in voice-chat mode aren't played. |
| Several local channels and subchannels | One local channel for now. |
| Themes, translations | One theme, English only. |
| Country flags from IP addresses | Not included. |
| ASIO on Windows | Not yet: Windows Audio (WASAPI) only. |
