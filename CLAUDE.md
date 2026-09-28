# Working in this repo

A JUCE 9 VST3 MIDI arpeggiator. Read `README.md` first — it has the architecture, the
control list, and a "things that cost time" section that is there specifically to stop
mistakes being repeated.

## Build and test

```powershell
.\build.ps1          # configure if needed, build Release, run tests, install
```

CMake is **not on PATH**. Use `C:\Program Files\CMake\bin\cmake.exe` if invoking it
directly. Compiler is VS Build Tools 2022, generator `Visual Studio 17 2022 -A x64`.

**Run `ArpTest.exe` after every change to the processor.** It is fast, needs no DAW, and
catches stuck notes and timing drift — the two things that are painful to diagnose in a
host. Never report a change as working on the strength of a clean compile alone.

Installing copies over `C:\Program Files\Common Files\VST3\Simple Arp.vst3`, which
**fails while a host has the plugin open**. If the copy fails, say so and ask for the host
to be closed rather than retrying.

## Conventions

- JUCE house style: `juce::` qualified everywhere, no `using namespace`, allman braces,
  space before the argument paren (`foo (bar)`), 4 spaces, members `camelCase`.
- Keep `juce_recommended_warning_flags` clean. Fix warnings you introduce.
- Audio-thread code allocates nothing. Anything the editor needs from the processor goes
  through an atomic, never a shared container. `sortedChord` and the sequencing state are
  audio-thread only.
- The grid and velocities are **not** parameters — they live in atomics and are serialised
  by hand into the APVTS tree in `getStateInformation`. Add new grid-shaped state the same
  way; add new scalar controls as APVTS parameters.
- **The grid is the source of truth; the Sequence field is a view of it.** Parsing lives on
  the processor (`parseSequence` / `applySequence` / `sequenceToString`) so it is testable
  headlessly, and the editor only renders and re-renders. Anything that writes the grid
  must leave the field correct on the next timer tick — don't add a second writer.
- `applySequence` writes rows only. `clearPattern` also resets velocities, which is right
  for the Clear button and wrong for retyping a sequence.
- In the block merge loop, a step position is a rounded `double` and `INT_MAX` means "no
  next step". Ties and near-ties go to the incoming message so a step plays the chord that
  is arriving, not the one being replaced. Do not add to the sentinel — it overflows and
  the loop stops terminating.
- Tests set every parameter they depend on explicitly. Do not lean on defaults.

## Things to be careful about

- **Never express a mode by disabling controls.** Disabled JUCE sliders and buttons ignore
  input silently and read as a broken plugin. Give the control a real job in that state, or
  remove the state.
- **Changing an `AudioParameterChoice`'s item list silently remaps saved values**, because
  the stored value is a normalised float. If the list has to grow, rename the parameter ID
  so stale state is ignored and the default applies.
- **Changing a default changes behaviour for every session saved before that parameter
  existed.** Call it out explicitly when it happens.
- Any change to pitch mapping, reach, or defaults will invalidate existing user patterns.
  Say so plainly rather than letting it be discovered.

## Host reality

VST3 has no MIDI-effect category. This is an `IS_SYNTH` plugin with a silent audio bus that
emits MIDI. Do not "fix" this by setting `IS_MIDI_EFFECT` — that produces a plugin declaring
itself `Fx`, which hosts put in audio insert slots where it receives no MIDI and does
nothing. `README.md` has the detail.
