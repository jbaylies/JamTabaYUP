# FAQ and troubleshooting

## Nobody hears me

- Is **XMIT** lit on your strip? It takes effect at the next interval.
- Does your meter move when you play? If not, check the input: in the app,
  **Settings** at the top of the window and the input button on your strip; in
  a DAW, the track's input.
- On macOS, has the app been allowed to use the microphone? See **System
  Settings > Privacy & Security > Microphone**.
- Is your channel muted (**M**)? Mute silences what you send too.

## I can't hear someone

- Is their **RX** on and their channel not muted? Is someone else soloed?
- Does their line flash green each interval? If not, nothing is arriving: they
  may not be transmitting.
- They may have only just joined: a channel starts at the next interval.

## I hear myself twice, or with an echo

Turn off **MON** if your interface or DAW already monitors your input.

## The others say I'm late

Nothing to fix: in NINJAM everyone hears everyone else one interval late, by
design. Play in time with the metronome and the beat bar, and it lines up for
everyone.

## The public room list is empty

JamTabaYUP fetches it from ninbot.com every 30 seconds. If it can't, the
browser says so; check your internet connection, or connect to a server by its
address.

## "The server closed the connection" or "The server stopped responding"

The server dropped you: it may be full, restarting, or your connection broke.
Join again. Some servers drop you if you don't accept their licence within
two minutes.

## The app doesn't open on macOS

See [Installing](installing.md#macos): the builds aren't notarized, so macOS
blocks them until you clear the quarantine flag or click Open Anyway.

## Can I record the jam?

Record the plugin's output in your DAW. JamTabaYUP doesn't record by itself.
