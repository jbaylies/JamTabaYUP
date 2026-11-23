# Installing

Download the zip for your system from the
[releases page](https://github.com/jbaylies/JamTabaYUP/releases):

| zip | contents | needs |
|---|---|---|
| `JamTabaYUP-<version>-macOS.zip` | `JamTabaYUP.app`, `.component` (AU), `.vst3`, `.clap` | macOS 11 or later, Apple Silicon or Intel |
| `JamTabaYUP-<version>-Windows-x64.zip` | `JamTabaYUP.exe`, `.vst3`, `.clap` | 64-bit Windows 10 or later |

Each zip has an `INSTALL.txt` with the same steps as this page.

## macOS

Put the app anywhere, such as `/Applications`. For the plugins, copy each one
into its folder. In Finder, use **Go > Go to Folder** and paste the path;
create the folder if it doesn't exist.

| file | folder | for |
|---|---|---|
| `JamTabaYUP.component` | `~/Library/Audio/Plug-Ins/Components` | Logic, GarageBand |
| `JamTabaYUP.vst3` | `~/Library/Audio/Plug-Ins/VST3` | Ableton Live, Cubase, Reaper |
| `JamTabaYUP.clap` | `~/Library/Audio/Plug-Ins/CLAP` | Bitwig, Reaper |

The builds aren't signed with an Apple Developer ID, so macOS blocks them after
download. Clear the quarantine flag once per copied file in Terminal:

```bash
xattr -dr com.apple.quarantine /Applications/JamTabaYUP.app
```

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/JamTabaYUP.vst3
```

and likewise for the `.component` and `.clap`. For the app you can instead try
to open it once, then click **Open Anyway** in **System Settings > Privacy &
Security**.

The first time the app uses your audio input, macOS asks for **microphone
access**. Click **Allow**, or the other musicians will hear silence. You can
change it later in **System Settings > Privacy & Security > Microphone**.

## Windows

Put `JamTabaYUP.exe` anywhere. Copy the plugins into their folders, creating
them if needed:

| file | folder | for |
|---|---|---|
| `JamTabaYUP.vst3` (a folder: copy all of it) | `C:\Program Files\Common Files\VST3` | Ableton Live, Cubase, Reaper |
| `JamTabaYUP.clap` | `C:\Program Files\Common Files\CLAP` | Bitwig, Reaper |

Copying into Program Files asks for administrator rights. Without them, use
`%LOCALAPPDATA%\Programs\Common\VST3` and `%LOCALAPPDATA%\Programs\Common\CLAP`,
though not every DAW scans those.

The app isn't code-signed, so SmartScreen warns the first time: click **More
info**, then **Run anyway**. If Windows asks whether JamTabaYUP may use the
network, allow it.

## Choosing your audio interface (standalone app)

Click **Settings** at the top of the window, choose your audio interface, its
inputs and outputs, and the smallest buffer size that plays without crackles.
On Windows the app uses Windows Audio (WASAPI); ASIO isn't supported yet.
