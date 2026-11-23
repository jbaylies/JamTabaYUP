# In your DAW

The plugin has the same window as the app. Insert it as an **audio effect** on
the track you play into (a guitar, a microphone or a synth track):

- The track's audio is your channel: JamTabaYUP sends it to the others.
- The plugin's output is the jam: the other musicians and the metronome, plus
  your own signal only if you turn **MON** on.

Your DAW already monitors the track, so **MON** starts off in the plugin.

| DAW | how |
|---|---|
| Logic Pro | Insert JamTabaYUP (AU) in an audio or instrument track's Audio FX slot. Turn on input monitoring for the track. |
| Ableton Live | Put the VST3 on an audio track with your input selected and Monitor set to In (or Auto with the track armed). |
| Bitwig, Reaper | Load the CLAP or VST3 on the track you play into. |
| Cubase | Insert the VST3 on the track and turn on its monitor button. |

## Timing

NINJAM keeps its own interval clock: the server sets the tempo, and the
intervals run whether or not your DAW is playing. To record a jam in your DAW,
set the DAW's tempo to the room's BPM. The project's bars won't line up with
the intervals unless you start recording on beat 1.

## Saved with your project

Your name, the last server, your channel settings, the metronome and master
levels, and each musician's mix are saved with the project. Reopening the
project doesn't reconnect automatically: join again from the browser.
