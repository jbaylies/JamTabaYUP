# Tempo, metronome and voting

## BPM and BPI

The server decides the tempo for everyone: **BPM** (beats per minute) and
**BPI** (beats per interval). When either changes, the change takes effect at
the start of the next interval, and the chat panel says so.

## The metronome

The metronome clicks every beat, with a higher click on beat 1 of each
interval. Set its volume with the **Metronome** fader in the top bar, or mute
it with **M**. In the plugin, the click is mixed into the plugin's output.

## Voting for a change

Most servers let the people in the room vote on the tempo:

1. Click the arrows next to **BPM** or **BPI** (or scroll on it) to choose a
   value. Hold Shift to step by 10. The proposed value turns teal.
2. Click **Vote**.

JamTabaYUP sends the vote as a chat command, so you can also type it in chat:

```
!vote bpm 110
!vote bpi 16
```

The server changes the tempo once enough people vote for the same value,
often half the room, and answers in chat. If voting is off on that server, the
server says so instead.
