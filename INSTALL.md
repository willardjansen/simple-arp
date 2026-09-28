# Simple Arp — installing and routing

A tempo-synced MIDI arpeggiator. You hold a chord, it plays back a pattern you draw on a
grid or type as a sequence of chord degrees.

---

## Install

**Windows** — unzip and put `Simple Arp.vst3` in:

```
C:\Program Files\Common Files\VST3\
```

**macOS** — unzip and put `Simple Arp.vst3` in:

```
/Library/Audio/Plug-Ins/VST3/          (all users)
~/Library/Audio/Plug-Ins/VST3/         (just you)
```

Then rescan plugins in your DAW.

> **macOS, first launch.** The build is not signed or notarised, so Gatekeeper will
> refuse it and most DAWs will report it as broken with no explanation. Clear the
> quarantine flag once, in Terminal:
>
> ```
> xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/Simple Arp.vst3"
> ```

---

## It's an instrument that emits MIDI — route it accordingly

**VST3 has no MIDI-effect plugin category.** There is no entry in the format for a
plugin that only makes notes, so this ships as an *Instrument* that outputs silence and
sends MIDI. That means you will find it under instruments, not under MIDI inserts or
audio effects, and it needs two tracks rather than one.

### Cubase / Nuendo

1. Instrument track → **Simple Arp**. This is the track you play and record onto.
2. On the destination instrument's track, set **MIDI input → Simple Arp**.
3. Monitor on for the destination track; play into the Simple Arp track.

Your held chords get recorded, so pattern, rate and scale all stay editable after the
take. Save it as a Track Preset and it's a two-click recall.

### Ableton Live

1. MIDI track with **Simple Arp** on it.
2. Second MIDI track with your instrument; set its **MIDI From** to the Simple Arp
   track, and pick **Simple Arp** in the second dropdown. Monitor: **In**.

### Logic, Reaper, Bitwig, Studio One

Same shape everywhere: Simple Arp on one track, a second track whose MIDI input is the
first track's output, with input monitoring on.

---

## Using it

| Control | What it does |
| --- | --- |
| **Rate** | Step length: `1/4` … `1/32`, with dotted and triplet divisions. |
| **Rows** | What a grid row means. `Chord tones` follows the held chord; every other entry is a scale and each row is one degree of it. |
| **Key** | Tonic for the scale row modes. `From chord` uses the lowest held note. |
| **Octaves** | In `Chord tones`, how far up the grid reaches. In scale modes, lifts the whole ladder. |
| **Gate** | Note length as a percentage of one step. |
| **Latch** | Keeps the pattern running after you lift your hands. |
| **Steps** | Pattern length, 1–32. |
| **Sequence** | The grid as text — see below. |
| **Fill** / **Clear** | Write a classic shape into the grid, or wipe it. |

### The Sequence field

With `Rows` set to `Chord tones`, type the pattern as degrees of whatever you're holding:

```
3-1-2|3-1-2|3-2-3|1-2-3|1-3-2-1
```

| | |
| --- | --- |
| `1` … `12` | A degree, counting **up from the lowest note sounding**. Past the top of the chord it wraps up an octave. |
| `.` `_` `0` | A rest. |
| `1+3` | Both degrees on one step. |
| `\|` | Cosmetic — it groups the sequence so it reads in phrases. Playback ignores it. |
| `-` `,` space | All separate one step from the next. |

Enter applies it, and Steps follows the length of what you typed.

**A degree is a position, not a role.** Degree 3 is the third note up from the bottom —
on a triad that's the top note, on a four-note voicing it's the second from the top. If
a chord change doesn't seem to come through, it's usually because the voice that moved
sits at a degree your sequence never asks for. The note-name gutter down the left of the
grid shows what each row is currently playing, which makes that obvious.

**Groups of three against 4/4.** The step column comes from the absolute step number, not
from the bar, so a sequence whose length doesn't divide the bar keeps phasing instead of
restarting every downbeat. `3-1-2` at `1/16` takes three bars to come back round. The
example above is sixteen steps precisely so it *does* land on the bar.

---

## Licence

Simple Arp is free software under the **GNU Affero General Public License v3.0**. You are
free to use it, change it and pass it on; if you distribute a modified version you have to
share your source under the same licence.

Source: <https://github.com/willardjansen/simple-arp>

Built with [JUCE](https://juce.com). VST is a trademark of Steinberg Media Technologies
GmbH.
