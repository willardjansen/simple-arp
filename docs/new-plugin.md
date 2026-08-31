# Starting the next plugin

A checklist distilled from building Simple Arp, so plugin #2 skips the dead ends.

## 1. Decide the plugin type before writing anything

This is the decision that wastes the most time if you get it wrong.

| You are building | Build it as | Why |
| --- | --- | --- |
| A synth or sampler | `IS_SYNTH TRUE` | Obvious case. |
| An audio effect | default (`Fx`) | Obvious case. |
| **Anything that generates MIDI** (arpeggiator, chord tool, sequencer) | `IS_SYNTH TRUE` + `NEEDS_MIDI_OUTPUT TRUE` + `VST3_CATEGORIES "Instrument"`, with a silent audio bus | **VST3 has no MIDI-effect category.** `IS_MIDI_EFFECT TRUE` does not change the declared category — JUCE only branches on `IS_SYNTH` — so you get a plugin that declares itself `Fx`, loads into audio insert slots, receives no MIDI, and does nothing while looking perfectly healthy. |

In Cubase a MIDI generator needs two tracks: play into the generator's instrument track, and
set the destination instrument's MIDI input to it. There is no single-track option; Cubase's
MIDI Inserts slot does not accept VST3. Reaper, Bitwig and FL can chain in one track.

## 2. Scaffold

```bash
mkdir my-plugin && cd my-plugin
git clone --depth 1 --branch master https://github.com/juce-framework/JUCE.git
mkdir Source Tests
```

Copy `CMakeLists.txt`, `build.ps1` and `.gitignore` from `arp-plugin` and change:

- `project(...)` name
- `juce_add_plugin` target name, `PLUGIN_CODE` (4 chars, unique per plugin),
  `PRODUCT_NAME`, `BUNDLE_ID`
- `PLUGIN_MANUFACTURER_CODE` stays `Wljn`
- the artefact paths in `build.ps1`

Set `COPY_PLUGIN_AFTER_BUILD FALSE` and let the script install — JUCE's own copy step wants
to write to `Program Files` mid-build and fails less gracefully.

`BUNDLE_ID` must not contain spaces or CMake warns. `com.willardjansen.<name>`.

## 3. Stand up the test harness on day one

Copy `Tests/ArpTest.cpp` and strip it back to the fake `AudioPlayHead` plus the `run()`
helper. It is a `juce_add_console_app` target that compiles the processor sources directly
and defines `JucePlugin_Name` itself.

This was worth more than everything else combined. Every real bug in Simple Arp was either
caught here in seconds, or was a *host/UI* problem that the harness proved was **not** in the
DSP — which is just as valuable, because it stops you rewriting working code.

Remember: call `setRateAndBufferSizeDetails()` before `prepareToPlay()`, or `getSampleRate()`
returns 0 and every timing assertion is subtly wrong.

## 4. Order of work

1. Processor + parameters + tests. No GUI.
2. Prove behaviour in the harness.
3. Minimal GUI wired to APVTS attachments.
4. Load in the host and check routing before building anything visual on top.

Loading in the DAW early is worth it — the VST3 category problem is invisible until you do,
and no amount of unit testing surfaces it.

## 5. GUI lessons worth reusing

- **Never disable a control to express a mode.** It reads as a broken plugin. Either give
  the control a job in that state or remove the state.
- **Make derived values visible.** Anything computed from user input — a row's pitch, a
  resulting bar length — should be shown somewhere. Simple Arp's note-name gutter and the
  `16 × 1/16 = 1 bar` readout both replaced entire rounds of "is this broken?".
- **Lay out rows in a loop, not by hand-trimming each control.** Per-control
  `withTrimmedRight(10)` drifts; one loop over a column list stays aligned.
- Poll the processor from a `Timer` at ~20 Hz for display state. Simple and it picks up
  host automation and preset loads for free.

## 6. State

Scalar controls → APVTS parameters. Grid- or table-shaped data → atomics, serialised by hand
into the APVTS tree in `getStateInformation`.

Be careful with `AudioParameterChoice`: the stored value is normalised, so **adding items to
the list silently remaps saved sessions**. If a list has to grow, rename the parameter ID.
