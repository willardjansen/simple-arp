# Simple Arp

A tempo-synced MIDI arpeggiator built as a VST3 with JUCE. You hold a chord, it plays
back a pattern you draw on a 12-row grid or type as a sequence of chord degrees.

Built on Windows 11 with MSVC. Tested in Cubase and Ableton Live 12; the macOS build
is produced by CI as a universal binary.

---

## What it does

Incoming notes are swallowed and replaced by a stepped pattern. Everything that isn't a
note message (CC, pitch bend, aftertouch, program change) passes straight through.

The grid is the only sequencer — 12 rows by up to 32 steps.

| Control | What it does |
| --- | --- |
| **Rate** | Step length: `1/4` … `1/32`, with dotted and triplet divisions. |
| **Rows** | What a grid row means. `Chord tones` follows the held chord; every other entry is a scale (`Chromatic`, `Major`, `Dorian`, `Minor`, `Blues`, pentatonics, …) and each row is one degree of it. |
| **Key** | Tonic for the scale row modes. `From chord` (default) uses the lowest held note; `C`–`B` pins it to a fixed key. |
| **Octaves** | In `Chord tones`, how far up the grid reaches (chord size × Octaves rows). In scale modes, lifts the whole ladder. |
| **Gate** | Note length as a percentage of one step. Scales with Rate. |
| **Latch** | Keeps the pattern running after you lift your hands. |
| **Steps** | Pattern length, 1–32. The readout shows what that works out to (`16 × 1/16 = 1 bar`). |
| **Fill** | Writes `Up` / `Down` / `Up-Down` / `Random` into the grid as a starting point. Fully editable afterwards. |
| **Clear** | Wipes the grid and resets velocities to 100. |
| **Sequence** | The grid as text — see below. Type `3-1-2\|3-1-2` and press Enter. |

Below the grid is a **velocity lane**, one bar per step. In the grid, click to toggle and
drag to paint — the first cell you touch decides whether the drag draws or erases.

The note-name gutter on the left shows the pitch each row will actually play for whatever
you're holding right now. Rows outside the current reach are dimmed there and stay silent.

---

## Typing a pattern

The **Sequence** field is the grid as text. Set `Rows` to `Chord tones` and the numbers are
degrees of whatever you're holding, counting up from the lowest note — so on a triad
`1-2-3` is root, third, fifth, and `4` is the root an octave up.

> **`Rows` has to be `Chord tones` for this.** It still defaults to `Chromatic`, where a
> row is a semitone above the root and the notes above the bass are never read at all — so
> a progression that moves its upper voices over a held bass comes out identical bar after
> bar. That reads exactly like the arp ignoring your chord changes, and it is the first
> thing to check if it does.

```
3-1-2|3-1-2|3-2-3|1-2-3|1-3-2-1
```

| | |
| --- | --- |
| `1` … `12` | A degree. Degree N is grid row N, so past the top of the chord it wraps up an octave. |
| `.` `_` `0` | A rest — a step that holds its place and plays nothing. |
| `1+3` | Both degrees on the same step. |
| `\|` | Cosmetic. It groups the sequence so it reads in phrases; playback ignores it. |
| `-` `,` space | All separate one step from the next, so type it however it reads best. |

Enter applies it. Steps follows the length of what you typed, and in `Chord tones` Octaves
is raised if it would otherwise put your highest degree out of reach — it's never lowered.
If the sequence doesn't parse, the field says why and nothing changes, so you can fix the
typo rather than retype the line.

The grid is the source of truth, not the text. Painting a cell, `Fill` and `Clear` all
re-render the field, and the `|` positions from the last thing you typed are kept as long
as the sequence is still that long.

**Groups of three against 4/4.** The step column comes from the absolute step number, not
from the bar, so a sequence whose length doesn't divide the bar keeps phasing instead of
restarting on every downbeat. `3-1-2` at `1/16` takes three bars to come back round — which
is the whole point of the figure. The example above is sixteen steps precisely so it *does*
land on the bar; drop the trailing `-1` and it starts walking.

---

## Building

Requires CMake and MSVC. Neither is on `PATH` in Git Bash on this machine, hence the
absolute path below.

```powershell
# one-command build + test + install
.\build.ps1

# useful flags
.\build.ps1 -Configure      # force a CMake re-configure first
.\build.ps1 -SkipInstall    # build and test only
.\build.ps1 -SkipTests
```

Or by hand:

```bash
CM="/c/Program Files/CMake/bin/cmake.exe"
"$CM" -B build -G "Visual Studio 17 2022" -A x64      # first time only
"$CM" --build build --config Release --parallel
build/ArpTest_artefacts/Release/ArpTest.exe            # exit 0 == pass
```

Artefacts land in `build/SimpleArp_artefacts/Release/`:

- `VST3/Simple Arp.vst3` — the plugin
- `Standalone/Simple Arp.exe` — standalone build for quick smoke tests
- `../ArpTest_artefacts/Release/ArpTest.exe` — the headless test harness

`COPY_PLUGIN_AFTER_BUILD` is off; `build.ps1` copies the VST3 to
`C:\Program Files\Common Files\VST3\` instead. That has worked without elevation here.
**The copy fails while a host has the plugin loaded** — close Cubase first.

---

## Testing

`Tests/ArpTest.cpp` builds a console app that drives `SimpleArpAudioProcessor` directly
against a fake `AudioPlayHead` and inspects the MIDI it emits. No DAW, no audio device.

It covers step timing against the host PPQ, gate lengths, the row→pitch mapping in every
row mode, per-step velocity, Fill shapes, key release, transport stop, and latch — plus a
standing check that every note-on is closed and none overlap.

Run it after every change. It catches the things that are miserable to diagnose in a DAW,
particularly stuck notes.

Three harness details worth knowing if you add tests:

- Call `setRateAndBufferSizeDetails()` before `prepareToPlay()`. `getSampleRate()` returns
  0 otherwise and the processor falls back to 44100, quietly breaking sample-count maths.
- Set parameters explicitly rather than relying on defaults. Defaults have moved twice
  and silently invalidated expectations both times.
- Read parameters back with `paramValue()`, which goes through `getRawParameterValue`.
  `getParameterAsValue` reads the APVTS *ValueTree*, which is only brought up to date
  asynchronously — with no message loop running it still holds the default, so an
  assertion against it passes or fails on what the default happens to be rather than on
  what the code did.

---

## Using it in Cubase

**VST3 has no MIDI-effect plugin category**, so this ships as an Instrument that emits MIDI
and outputs silence. Cubase has no way to chain a VST3 note effect inside one track, so it
needs two:

1. Instrument track → **Simple Arp**. This is the track you play and record onto.
2. On the destination instrument's track, set **MIDI input → Simple Arp**.
3. Monitor on for the destination track; play into the Simple Arp track.

Your held chords get recorded, and on playback they run through the arp — so pattern, rate
and scale stay editable after the take. Save it as a Track Preset once and it's a two-click
recall.

Cubase caches plugin GUIs and categories. **Remove and re-add the plugin** after a rebuild,
and rescan if the category ever changes.

---

## How it fits together

```
Source/
  PluginProcessor.{h,cpp}   parameters, MIDI in/out, the step engine
  PluginEditor.{h,cpp}      PatternGrid, RowLabels, VelocityLane, the editor
Tests/
  ArpTest.cpp               headless harness
```

**Parameters** (`AudioProcessorValueTreeState`): `rate`, `octaves`, `gate`, `latch`,
`steps`, `rows`, `key`.

**The grid is not a parameter.** 12 rows × 32 steps is far too many for host automation, so
it lives in `std::array<std::atomic<uint32>, 12>` — one bitmask per row, bit N being step N.
Lock-free for the editor, and serialised into the APVTS state tree as a `pattern` property
in `getStateInformation`. Per-step velocities go alongside as `velocities`.

**Step timing.** `processBlock` walks the block merging incoming MIDI with generated steps
in sample order. When the host transport is running, step positions come from the PPQ
position, so the pattern is locked to the bar and identical on every playback pass. When
it isn't, an internal sample counter free-runs so the plugin still works with the transport
stopped, starting the pattern under your finger on the first key.

**Note-offs** are scheduled as a sample offset from the block start and carried across
blocks. Retriggering a sounding pitch closes it a sample early so the host never sees
overlapping pairs.

**Threading.** `sortedChord` and the sequencing state are audio-thread only. Anything the
editor reads — row pitches, chord size, current step — is published through atomics
(`displayRowPitch`, `displayChordSize`, `displayStep`). The editor polls at 20 Hz.

---

## Things that cost time, so they're written down

**VST3 has no MIDI-effect category.** The SDK's `PlugType` list has `Instrument*`, `Fx|*`,
`Analyzer`, `OnlyARA`, `OnlyRT` — and nothing for a MIDI-only plugin. Worse, JUCE's
`juce_add_plugin` only branches on `IS_SYNTH` when picking `VST3_CATEGORIES`; setting
`IS_MIDI_EFFECT TRUE` does **not** change the declared category. The result declares itself
`"Sub Categories": ["Fx"]`, hosts offer it in *audio insert* slots where no MIDI ever
reaches it, and it loads and looks fine while doing absolutely nothing. Ship MIDI
generators as `IS_SYNTH TRUE` with `VST3_CATEGORIES "Instrument"` and a silent output bus.

**Don't disable controls to express a mode.** An earlier build greyed out Steps, Clear and
the grid whenever a Pattern toggle was off. JUCE sliders and buttons ignore input when
disabled, so half the window went dead with nothing on screen explaining why — it read as a
broken plugin. If a control is irrelevant, either give it a real job in that state or remove
the state. The Mode dropdown was eventually demoted to the Fill button for exactly this
reason.

**On cycle playback the held note is released and re-pressed at the loop point.** The
re-press can land a sample or two after the downbeat step, so the step found an empty chord
and produced nothing — the first note of the pattern vanished on most loops. Tests that
inject a chord once and hold it forever never see this; the harness has to release and
re-press at the cycle boundary. A step that finds no chord is now held for up to 15ms and
fires as soon as the notes arrive.

**A step and the chord change it should play arrive at the same moment, and the step was
winning.** The block loop merges incoming MIDI with generated steps in sample order and
already gave ties to the message. But the step's position is a `double` derived from the
host ppq, so a step whose true home is sample 160 computes as 159.99999 — and truncating
that to an `int` put it *before* a note-on on sample 160. A chord change written on the
beat is exactly that case, so the first note of every new chord was the old chord's. It
reads as "the arp only follows chord changes at bar lines", especially under a pattern a
bar long. Two parts to the fix: round the step position instead of truncating it, and
treat a message landing within half a millisecond *after* a step as simultaneous with it,
since hosts deliver the release and re-press a sample or two late. Rounding also means a
step can land a sample either side of its ideal position, so a test asserting an exact
gap between steps needs a couple of samples of slack.

**`INT_MAX` is used as "no next step", so do not do arithmetic on it.** Adding the
simultaneity grace to the step position overflowed the sentinel, the comparison went the
wrong way, and the merge loop stopped terminating — the test run hung rather than failed.
Check for the sentinel before adding anything to it.

**Host ppq is not exact, and the tolerance must reflect that.**
 Step positions came from
`ceil (ppqStart / stepQuarters - 1.0e-9)`. That epsilon is in units of *steps* — around half
a nanosecond. A host derives `ppqPosition` as a float from time, so it lands slightly either
side of a boundary; whenever it overshot by more than the epsilon, `ceil` jumped to the next
step and the current one was dropped. Random noise meant it happened on roughly every other
bar, which read as "the first note of the pattern keeps skipping". The tolerance is now
1.0e-4 steps, about half a sample — inaudible, but far above host float noise. Model the
noise in tests: exact ppq will never reproduce this.

**A step on a block boundary can fire twice.** `nextStepPpq` is derived fresh from the
host position every block, so a step landing exactly on a boundary is found at the end of
one block (floating point puts it at 511.9999 rather than 512.0) and again at the start of
the next, where `ppqPosition` sits exactly on it. The retrigger guard then turns that into
note-on, note-off one sample later, note-on — which chokes the note on any sustaining
patch. `lastSyncedStep` now dedupes. Note this only shows up when ppq is derived from the
absolute sample position the way a real host does it; a test that accumulates ppq per block
will not reproduce it.

**Free-running state needs its own reset.** The pattern column in free-run comes from a
step counter that keeps ticking whether or not anything is held. Resetting only the step
*phase* when a chord arrives left the pattern starting on an arbitrary column — with 16
steps, a 1-in-16 chance of starting where you expect. Reset every piece of derived timing
state together, and test from a non-zero starting state or the bug hides.

**Make derived mappings visible.** With `Chord tones` and a single note held, all 12 rows
are octaves of that note — correct, but indistinguishable from a bug. The note-name gutter
was the actual fix; it turns an invisible rule into something you can read.

**Changing a parameter's choice list breaks saved state.** `AudioParameterChoice` stores a
normalised float, so adding entries silently remaps old values. Renaming the parameter ID is
the clean way out: stale states are ignored and the new parameter takes its default.

---

## Licence

Simple Arp is free software under the **GNU Affero General Public License v3.0**. The full
text is in [`LICENSE`](LICENSE).

Use it, study it, change it, pass it on. If you distribute a modified version — or run one
as a network service — you have to make your source available under the same licence.
Commercial use is allowed; keeping your changes closed is not.

This is what the dependencies require rather than a free choice:

- **JUCE 9** is dual-licensed AGPLv3 or commercial. This project takes the AGPLv3 route,
  which is also why `JUCE_DISPLAY_SPLASH_SCREEN=0` is fine here — the splash requirement
  belongs to the commercial terms, not to the AGPL.
- **Steinberg's VST3 SDK**, vendored inside JUCE, is GPLv3 or Steinberg's proprietary
  licence. GPLv3 is compatible with AGPLv3 for this, so the AGPL route covers it without
  separate registration.

Note that AGPLv3 forbids adding further restrictions, so a "non-commercial" clause cannot
be layered on top. That would need a commercial JUCE licence first.

VST is a trademark of Steinberg Media Technologies GmbH.
