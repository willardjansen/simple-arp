A tempo-synced MIDI arpeggiator. Hold a chord, and it plays back a pattern you either
draw on a 12-row grid or type as a sequence of chord degrees.

Built for writing trance-style arps, where the useful figures are groups of three against
4/4 and you want to type them rather than click them.

## Typing a pattern

Set **Rows** to `Chord tones` and the numbers are degrees of whatever you're holding,
counting up from the lowest note:

```
3-1-2|3-1-2|3-2-3|1-2-3|1-3-2-1
```

- `1`…`12` — a degree. Past the top of the chord it wraps up an octave.
- `.` `_` `0` — a rest.
- `1+3` — both degrees on one step.
- `|` — cosmetic. It groups the sequence so it reads in phrases; playback ignores it.
- `-` `,` space — all separate one step from the next.

Enter applies it and **Steps** follows the length. If it doesn't parse, the field tells
you why and nothing changes, so you fix the typo instead of retyping the line.

The grid stays the source of truth — paint a cell, hit Fill or Clear, or recall a preset,
and the text follows.

**Groups of three against 4/4 keep phasing.** The step column comes from the absolute step
number rather than from the bar, so a sequence whose length doesn't divide the bar walks
across it instead of restarting on every downbeat. `3-1-2` at 1/16 takes three bars to come
back round. The example above is sixteen steps precisely so it *does* land on the bar.

## Everything else

- **Rate** 1/4 to 1/32, with dotted and triplet divisions
- **Rows** — chord tones, or any of fourteen scales with a selectable key
- **Octaves**, **Gate**, **Latch**, **Steps** (1–32)
- A velocity lane, one bar per step
- **Fill** for the classic Up / Down / Up-Down / Random shapes as a starting point
- A note-name gutter showing what each row is actually playing for the chord you're holding

## Installing

Unzip and drop `Simple Arp.vst3` into your VST3 folder:

- **Windows** — `C:\Program Files\Common Files\VST3\`
- **macOS** — `/Library/Audio/Plug-Ins/VST3/`

The macOS build is universal (Apple Silicon and Intel) but **not signed or notarised**, so
Gatekeeper will block it and most DAWs will just report it as broken. Clear the quarantine
flag once:

```
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/Simple Arp.vst3"
```

## It needs two tracks

**VST3 has no MIDI-effect category** — the format has no slot for a plugin that only makes
notes. So this ships as an *instrument* that outputs silence and sends MIDI, which means
you'll find it under instruments, and it needs a second track to play into:

1. One track with Simple Arp on it — this is the one you play and record onto.
2. A second track with your instrument, its MIDI input set to the Simple Arp track, input
   monitoring on.

In Ableton Live, that second dropdown under **MIDI From** is the one people miss — it has
to be set to **Simple Arp**, not to the track's own output:

![Routing Simple Arp into Serum in Ableton Live 12](https://raw.githubusercontent.com/willardjansen/simple-arp/master/docs/images/ableton-routing.png)

`INSTALL.md` in the download has the per-DAW steps for Cubase, Live, Logic, Reaper, Bitwig
and Studio One.

## Worth knowing

**A degree is a position, not a role.** Degree 3 is the third note up from the bottom of
whatever is sounding — on a triad that's the top note, on a four-note voicing it's the
second from the top. If a chord change doesn't seem to come through, it's usually because
the voice that moved sits at a degree the sequence never asks for. The note-name gutter
makes that visible.

Limits: 12 rows, 32 steps.

## Licence

Free software under the GNU Affero General Public License v3.0. Use it, change it, pass it
on; if you distribute a modified version, share your source under the same licence.

Built with [JUCE](https://juce.com). VST is a trademark of Steinberg Media Technologies GmbH.
