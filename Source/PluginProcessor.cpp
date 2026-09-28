#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <limits>

//==============================================================================
juce::StringArray SimpleArpAudioProcessor::getRateNames()
{
    return { "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16.", "1/16", "1/16T", "1/32" };
}

juce::StringArray SimpleArpAudioProcessor::getRowModeNames()
{
    return { "Chord tones", "Chromatic", "Major", "Dorian", "Phrygian", "Lydian",
             "Mixolydian", "Minor", "Locrian", "Harmonic minor", "Melodic minor",
             "Major pent.", "Minor pent.", "Blues", "Whole tone" };
}

juce::StringArray SimpleArpAudioProcessor::getKeyNames()
{
    return { "From chord", "C", "C#", "D", "D#", "E", "F", "F#",
             "G", "G#", "A", "A#", "B" };
}

namespace
{
    // Semitone offsets from the tonic. Index 0 is Chromatic, matching row mode 1.
    struct ScaleDef { int size; int steps[12]; };

    constexpr ScaleDef scaleTable[]
    {
        { 12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },   // Chromatic
        {  7, { 0, 2, 4, 5, 7, 9, 11 } },                   // Major
        {  7, { 0, 2, 3, 5, 7, 9, 10 } },                   // Dorian
        {  7, { 0, 1, 3, 5, 7, 8, 10 } },                   // Phrygian
        {  7, { 0, 2, 4, 6, 7, 9, 11 } },                   // Lydian
        {  7, { 0, 2, 4, 5, 7, 9, 10 } },                   // Mixolydian
        {  7, { 0, 2, 3, 5, 7, 8, 10 } },                   // Minor
        {  7, { 0, 1, 3, 5, 6, 8, 10 } },                   // Locrian
        {  7, { 0, 2, 3, 5, 7, 8, 11 } },                   // Harmonic minor
        {  7, { 0, 2, 3, 5, 7, 9, 11 } },                   // Melodic minor
        {  5, { 0, 2, 4, 7, 9 } },                          // Major pentatonic
        {  5, { 0, 3, 5, 7, 10 } },                         // Minor pentatonic
        {  6, { 0, 3, 5, 6, 7, 10 } },                      // Blues
        {  6, { 0, 2, 4, 6, 8, 10 } }                       // Whole tone
    };

    constexpr int numScales = (int) (sizeof (scaleTable) / sizeof (scaleTable[0]));
}

double SimpleArpAudioProcessor::quarterNotesForRate (int rateIndex)
{
    // Length of one step, measured in quarter notes.
    static constexpr double lengths[] =
    {
        1.0,          // 1/4
        2.0 / 3.0,    // 1/4T
        0.75,         // 1/8.
        0.5,          // 1/8
        1.0 / 3.0,    // 1/8T
        0.375,        // 1/16.
        0.25,         // 1/16
        1.0 / 6.0,    // 1/16T
        0.125         // 1/32
    };

    const int numLengths = (int) (sizeof (lengths) / sizeof (lengths[0]));
    return lengths[juce::jlimit (0, numLengths - 1, rateIndex)];
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SimpleArpAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "rate", 1 }, "Rate", getRateNames(), 6));   // default 1/16

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "octaves", 1 }, "Octaves", 1, 4, 1));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gate", 1 }, "Gate",
        juce::NormalisableRange<float> (5.0f, 100.0f, 0.1f), 50.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "latch", 1 }, "Latch", false));

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "steps", 1 }, "Steps", 1, maxPatternSteps, 16));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "rows", 1 }, "Rows", getRowModeNames(), rowsChromatic));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "key", 1 }, "Key", getKeyNames(), 0));

    return layout;
}

//==============================================================================
bool SimpleArpAudioProcessor::getPatternCell (int row, int step) const noexcept
{
    if (! juce::isPositiveAndBelow (row, numPatternRows)
        || ! juce::isPositiveAndBelow (step, maxPatternSteps))
        return false;

    return (patternRows[(size_t) row].load() & (1u << step)) != 0;
}

void SimpleArpAudioProcessor::setPatternCell (int row, int step, bool shouldBeOn) noexcept
{
    if (! juce::isPositiveAndBelow (row, numPatternRows)
        || ! juce::isPositiveAndBelow (step, maxPatternSteps))
        return;

    auto& bits = patternRows[(size_t) row];
    const auto mask = (juce::uint32) (1u << step);

    if (shouldBeOn)
        bits.fetch_or (mask);
    else
        bits.fetch_and (~mask);
}

void SimpleArpAudioProcessor::clearPattern() noexcept
{
    for (auto& row : patternRows)
        row.store (0);

    for (auto& velocity : stepVelocities)
        velocity.store (defaultVelocity);
}

void SimpleArpAudioProcessor::clearPatternRows() noexcept
{
    // Rows only: typing a sequence rewrites which notes play, not how hard they play.
    for (auto& row : patternRows)
        row.store (0);
}

//==============================================================================
SimpleArpAudioProcessor::SequenceParse
SimpleArpAudioProcessor::parseSequence (const juce::String& text)
{
    SequenceParse result;

    // '|' is cosmetic grouping, so split on it first and keep the sizes for the
    // round trip back out. Everything else separates one step from the next.
    juce::StringArray groupTexts;
    groupTexts.addTokens (text, "|", "");

    for (const auto& groupText : groupTexts)
    {
        juce::StringArray stepTokens;
        stepTokens.addTokens (groupText, "-,;/ \t\r\n", "");
        stepTokens.removeEmptyStrings();

        for (const auto& token : stepTokens)
        {
            if ((int) result.steps.size() >= maxPatternSteps)
            {
                result.error = "more than " + juce::String (maxPatternSteps) + " steps";
                return result;
            }

            // A whole token of '.', '_' or '0' is a rest: a step that plays nothing.
            if (token == "." || token == "_" || token == "0")
            {
                result.steps.emplace_back();
                continue;
            }

            juce::StringArray degreeTexts;
            degreeTexts.addTokens (token, "+", "");
            degreeTexts.removeEmptyStrings();

            std::vector<int> rows;

            for (const auto& degreeText : degreeTexts)
            {
                if (! degreeText.containsOnly ("0123456789"))
                {
                    result.error = "\"" + degreeText + "\" is not a degree";
                    return result;
                }

                const int degree = degreeText.getIntValue();

                if (degree < 1 || degree > numPatternRows)
                {
                    result.error = "degree " + juce::String (degree) + " is outside 1-"
                                     + juce::String (numPatternRows);
                    return result;
                }

                if (std::find (rows.begin(), rows.end(), degree - 1) == rows.end())
                    rows.push_back (degree - 1);

                result.highestDegree = juce::jmax (result.highestDegree, degree);
            }

            if (rows.empty())
            {
                result.error = "\"" + token + "\" is not a degree";
                return result;
            }

            std::sort (rows.begin(), rows.end());
            result.steps.push_back (std::move (rows));
        }

        const int stepsSoFar = (int) result.steps.size();
        int used = 0;

        for (int size : result.groups)
            used += size;

        if (stepsSoFar > used)
            result.groups.push_back (stepsSoFar - used);
    }

    if (result.steps.empty())
    {
        result.error = "nothing to play";
        return result;
    }

    result.numSteps = (int) result.steps.size();
    result.ok = true;

    return result;
}

SimpleArpAudioProcessor::SequenceParse
SimpleArpAudioProcessor::applySequence (const juce::String& text)
{
    auto parsed = parseSequence (text);

    if (! parsed.ok)
        return parsed;

    clearPatternRows();

    for (int step = 0; step < parsed.numSteps; ++step)
        for (int row : parsed.steps[(size_t) step])
            setPatternCell (row, step, true);

    if (auto* steps = apvts.getParameter ("steps"))
        steps->setValueNotifyingHost (steps->convertTo0to1 ((float) parsed.numSteps));

    // In Chord tones, Octaves caps how far up the grid reaches, so a sequence that
    // asks for degree 4 on a triad would be silently half-muted. Raise it to match.
    // Scale modes always reach all 12 rows, and there Octaves transposes instead --
    // touching it would move the pattern, so it is left alone.
    if ((int) rowModeParam->load() == rowsChordTones && parsed.highestDegree > 0)
    {
        const int chordSize = displayChordSize.load();

        // Nothing held yet, so size the reach for the common case rather than guess
        // high; the dimmed grid rows show it if a smaller chord falls short.
        const int assumed = chordSize > 0 ? chordSize : 3;
        const int needed = juce::jlimit (1, 4, (parsed.highestDegree + assumed - 1) / assumed);

        if (auto* octaves = apvts.getParameter ("octaves"))
        {
            if (needed > (int) octavesParam->load())
            {
                octaves->setValueNotifyingHost (octaves->convertTo0to1 ((float) needed));
                parsed.octavesRaisedTo = needed;
            }
        }
    }

    return parsed;
}

juce::String SimpleArpAudioProcessor::sequenceToString (const std::vector<int>& groups) const
{
    const int numSteps = juce::jlimit (1, maxPatternSteps, (int) stepsParam->load());

    juce::StringArray stepTexts;

    for (int step = 0; step < numSteps; ++step)
    {
        juce::StringArray degrees;

        for (int row = 0; row < numPatternRows; ++row)
            if (getPatternCell (row, step))
                degrees.add (juce::String (row + 1));

        stepTexts.add (degrees.isEmpty() ? juce::String (".") : degrees.joinIntoString ("+"));
    }

    int grouped = 0;

    for (int size : groups)
        grouped += size;

    // The remembered grouping only survives while it still describes this many steps.
    if (grouped != numSteps)
        return stepTexts.joinIntoString ("-");

    juce::StringArray groupTexts;
    int index = 0;

    for (int size : groups)
    {
        juce::StringArray part;

        for (int i = 0; i < size; ++i)
            part.add (stepTexts[index++]);

        groupTexts.add (part.joinIntoString ("-"));
    }

    return groupTexts.joinIntoString ("|");
}

int SimpleArpAudioProcessor::scaleRootFor (int lowestNote) const
{
    const int keyIndex = (int) keyParam->load();

    if (keyIndex <= 0)
        return lowestNote;              // "From chord": the note you played is the tonic

    // Place the chosen tonic in the octave at or below what is being played, so the
    // ladder starts near your hand rather than jumping registers.
    const int pitchClass = keyIndex - 1;
    int root = 12 * (lowestNote / 12) + pitchClass;

    if (root > lowestNote)
        root -= 12;

    return root;
}

int SimpleArpAudioProcessor::pitchForRow (int row) const
{
    if (sortedChord.empty() || ! juce::isPositiveAndBelow (row, numPatternRows))
        return -1;

    const int octaves = juce::jlimit (1, 4, (int) octavesParam->load());
    const int rowMode = (int) rowModeParam->load();
    int note = 0;

    if (rowMode == rowsChordTones)
    {
        const int chordSize = (int) sortedChord.size();
        note = sortedChord[(size_t) (row % chordSize)].note + 12 * (row / chordSize);
    }
    else
    {
        // A fixed ladder of scale degrees from the tonic. Octaves lifts the whole
        // grid so the shape you draw can sit in a higher register.
        const auto& scale = scaleTable[juce::jlimit (0, numScales - 1, rowMode - 1)];
        const int root = scaleRootFor (sortedChord.front().note);

        note = root + scale.steps[row % scale.size] + 12 * (row / scale.size)
                    + 12 * (octaves - 1);
    }

    return (note >= 0 && note <= 127) ? note : -1;
}

int SimpleArpAudioProcessor::getRowPitch (int row) const noexcept
{
    if (! juce::isPositiveAndBelow (row, numPatternRows))
        return -1;

    return displayRowPitch[(size_t) row].load();
}

int SimpleArpAudioProcessor::getActivePatternRows() const noexcept
{
    const int chordSize = displayChordSize.load();

    // Nothing held yet — show the whole grid rather than an empty one.
    if (chordSize <= 0)
        return numPatternRows;

    if ((int) rowModeParam->load() != rowsChordTones)
        return numPatternRows;

    const int octaves = juce::jlimit (1, 4, (int) octavesParam->load());

    return juce::jlimit (1, numPatternRows, chordSize * octaves);
}

bool SimpleArpAudioProcessor::isStepActive (int step) const noexcept
{
    if (! juce::isPositiveAndBelow (step, maxPatternSteps))
        return false;

    const auto mask = (juce::uint32) (1u << step);

    for (const auto& row : patternRows)
        if ((row.load() & mask) != 0)
            return true;

    return false;
}

int SimpleArpAudioProcessor::getStepVelocity (int step) const noexcept
{
    if (! juce::isPositiveAndBelow (step, maxPatternSteps))
        return defaultVelocity;

    return stepVelocities[(size_t) step].load();
}

void SimpleArpAudioProcessor::setStepVelocity (int step, int velocity) noexcept
{
    if (! juce::isPositiveAndBelow (step, maxPatternSteps))
        return;

    stepVelocities[(size_t) step].store (juce::jlimit (1, 127, velocity));
}

juce::String SimpleArpAudioProcessor::velocitiesToString() const
{
    juce::StringArray values;

    for (const auto& velocity : stepVelocities)
        values.add (juce::String (velocity.load()));

    return values.joinIntoString (",");
}

void SimpleArpAudioProcessor::velocitiesFromString (const juce::String& text)
{
    if (text.isEmpty())
        return;

    juce::StringArray values;
    values.addTokens (text, ",", "");

    for (int i = 0; i < maxPatternSteps; ++i)
        stepVelocities[(size_t) i].store (i < values.size()
                                              ? juce::jlimit (1, 127, values[i].getIntValue())
                                              : defaultVelocity);
}

juce::String SimpleArpAudioProcessor::patternToString() const
{
    juce::StringArray rows;

    for (const auto& row : patternRows)
        rows.add (juce::String::toHexString ((int) row.load()));

    return rows.joinIntoString (",");
}

void SimpleArpAudioProcessor::patternFromString (const juce::String& text)
{
    if (text.isEmpty())
        return;

    juce::StringArray rows;
    rows.addTokens (text, ",", "");

    for (int i = 0; i < numPatternRows; ++i)
        patternRows[(size_t) i].store (i < rows.size()
                                           ? (juce::uint32) rows[i].getHexValue32()
                                           : 0u);
}

//==============================================================================
SimpleArpAudioProcessor::SimpleArpAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    rateParam    = apvts.getRawParameterValue ("rate");
    octavesParam = apvts.getRawParameterValue ("octaves");
    gateParam    = apvts.getRawParameterValue ("gate");
    latchParam   = apvts.getRawParameterValue ("latch");
    stepsParam   = apvts.getRawParameterValue ("steps");
    rowModeParam = apvts.getRawParameterValue ("rows");
    keyParam     = apvts.getRawParameterValue ("key");

    // A rolling four-tone arp, so switching Pattern on does something musical
    // before you have drawn anything.
    clearPattern();

    for (int step = 0; step < 16; ++step)
        setPatternCell (step % 4, step, true);
}

//==============================================================================
bool SimpleArpAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // The audio output only ever carries silence; accept whatever the host offers.
    const auto& output = layouts.getMainOutputChannelSet();

    return output == juce::AudioChannelSet::mono()
        || output == juce::AudioChannelSet::stereo();
}

void SimpleArpAudioProcessor::prepareToPlay (double, int)
{
    clearArpState();
}

void SimpleArpAudioProcessor::reset()
{
    clearArpState();
}

void SimpleArpAudioProcessor::clearArpState()
{
    keysDown.clear();
    patternNotes.clear();
    sortedChord.clear();

    // reset() and prepareToPlay() can land while notes are sounding, and neither has a
    // buffer to write to. Hand them to the next processBlock rather than orphaning them.
    notesToRelease.insert (notesToRelease.end(), activeNotes.begin(), activeNotes.end());
    activeNotes.clear();
    freeStepCounter = 0;
    lastSyncedStep = std::numeric_limits<long long>::min();
    pendingStepNumber = -1;
    nextFreeRunStep = 0.0;
    wasPlaying = false;
    cachedOctaves = -1;
    cachedRowMode = -1;
    cachedKey = -1;
    displayStep.store (-1);
    displayChordSize.store (0);

    for (auto& pitch : displayRowPitch)
        pitch.store (-1);
}

//==============================================================================
void SimpleArpAudioProcessor::rebuildSequence()
{
    sortedChord.clear();

    if (patternNotes.empty())
    {
        displayChordSize.store (0);

        for (auto& pitch : displayRowPitch)
            pitch.store (-1);

        return;
    }

    // The grid indexes chord tones low to high, whatever order they were played in.
    sortedChord.assign (patternNotes.begin(), patternNotes.end());
    std::sort (sortedChord.begin(), sortedChord.end(),
               [] (const HeldNote& a, const HeldNote& b) { return a.note < b.note; });

    displayChordSize.store ((int) sortedChord.size());

    for (int row = 0; row < numPatternRows; ++row)
        displayRowPitch[(size_t) row].store (pitchForRow (row));

}

//==============================================================================
void SimpleArpAudioProcessor::stopAllActiveNotes (juce::MidiBuffer& out, int sampleOffset)
{
    for (const auto& active : activeNotes)
        out.addEvent (juce::MidiMessage::noteOff (active.channel, active.note),
                      juce::jmax (0, sampleOffset));

    activeNotes.clear();
}

void SimpleArpAudioProcessor::triggerStep (int sampleOffset, juce::MidiBuffer& out,
                                           double gateSamples, long long stepNumber)
{
    if (sortedChord.empty())
    {
        // Nothing held at this instant. On cycle playback the host releases the part's
        // note and re-presses it, and the re-press can land a sample or two after the
        // downbeat step. Hold the step briefly rather than dropping it.
        pendingStepNumber = stepNumber;
        pendingStepAge = -sampleOffset;
        return;
    }

    const int numSteps = juce::jlimit (1, maxPatternSteps, (int) stepsParam->load());
    const int column = (int) (((stepNumber % numSteps) + numSteps) % numSteps);
    const int velocity = getStepVelocity (column);

    // In Chord tones, Octaves caps how far up the grid reaches: a triad with Octaves 1
    // fills three rows, Octaves 2 fills six. Every scale always fills all 12.
    const int reach = getActivePatternRows();

    displayStep.store (column);

    for (int row = 0; row < reach; ++row)
    {
        if (! getPatternCell (row, column))
            continue;

        const int note = pitchForRow (row);

        if (note >= 0)
            emitNote ({ note, velocity }, sampleOffset, out, gateSamples);
    }
}

void SimpleArpAudioProcessor::fillPattern (int shape)
{
    // Writes one of the classic arpeggiator shapes into the grid as a starting point.
    // Everything stays editable afterwards — this is a generator, not a mode.
    clearPattern();

    const int numSteps = juce::jlimit (1, maxPatternSteps, (int) stepsParam->load());
    const int reach = juce::jlimit (1, numPatternRows, getActivePatternRows());

    juce::Random random;

    for (int step = 0; step < numSteps; ++step)
    {
        int row = 0;

        switch (shape)
        {
            case fillDown:
                row = reach - 1 - (step % reach);
                break;

            case fillUpDown:
            {
                // Mirror without repeating the two turning points.
                const int span = juce::jmax (1, reach * 2 - 2);
                const int phase = step % span;
                row = phase < reach ? phase : span - phase;
                break;
            }

            case fillRandom:
                row = random.nextInt (reach);
                break;

            case fillUp:
            default:
                row = step % reach;
                break;
        }

        setPatternCell (juce::jlimit (0, numPatternRows - 1, row), step, true);
    }
}

void SimpleArpAudioProcessor::emitNote (const HeldNote& step, int sampleOffset,
                                        juce::MidiBuffer& out, double gateSamples)
{
    const int channel = juce::jlimit (1, 16, lastChannel);

    // If this note is already sounding, close it a sample early so the host sees
    // a clean retrigger rather than an overlapping pair.
    for (auto it = activeNotes.begin(); it != activeNotes.end(); )
    {
        if (it->note == step.note && it->channel == channel)
        {
            out.addEvent (juce::MidiMessage::noteOff (channel, it->note),
                          juce::jmax (0, sampleOffset - 1));
            it = activeNotes.erase (it);
        }
        else
        {
            ++it;
        }
    }

    const auto velocity = (juce::uint8) juce::jlimit (1, 127, step.velocity);
    out.addEvent (juce::MidiMessage::noteOn (channel, step.note, velocity), sampleOffset);

    activeNotes.push_back ({ step.note, channel, sampleOffset + (int) gateSamples });
}

//==============================================================================
void SimpleArpAudioProcessor::handleIncomingMessage (const juce::MidiMessage& message, int sampleOffset,
                                                     juce::MidiBuffer& out, bool synced)
{
    if (message.isNoteOn())
    {
        const int note = message.getNoteNumber();
        lastChannel = message.getChannel();

        // Latch: the first key of a new chord wipes the previous one.
        if (latchParam->load() > 0.5f && keysDown.empty())
            patternNotes.clear();

        const bool wasEmpty = patternNotes.empty();

        if (std::find (keysDown.begin(), keysDown.end(), note) == keysDown.end())
            keysDown.push_back (note);

        auto existing = std::find_if (patternNotes.begin(), patternNotes.end(),
                                      [note] (const HeldNote& h) { return h.note == note; });

        if (existing != patternNotes.end())
            existing->velocity = message.getVelocity();
        else
            patternNotes.push_back ({ note, message.getVelocity() });

        rebuildSequence();

        // A step just missed this chord by a hair — play it now rather than never.
        if (pendingStepNumber >= 0 && ! sortedChord.empty())
        {
            const long long owed = pendingStepNumber;
            const int age = pendingStepAge + sampleOffset;

            pendingStepNumber = -1;

            if (age >= 0 && age <= retriggerGraceSamples)
                triggerStep (sampleOffset, out, currentGateSamples, owed);
        }

        if (wasEmpty && ! synced)
        {
            // Free-running (no host transport): start the pattern under the finger, at
            // its first column. The counter keeps ticking whether or not anything is
            // held, so without this the pattern would begin at an arbitrary step.
            nextFreeRunStep = (double) sampleOffset;
            freeStepCounter = 0;
        }
    }
    else if (message.isNoteOff())
    {
        const int note = message.getNoteNumber();

        keysDown.erase (std::remove (keysDown.begin(), keysDown.end(), note), keysDown.end());

        if (latchParam->load() <= 0.5f)
        {
            patternNotes.erase (std::remove_if (patternNotes.begin(), patternNotes.end(),
                                                [note] (const HeldNote& h) { return h.note == note; }),
                                patternNotes.end());
            rebuildSequence();
        }
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        keysDown.clear();
        patternNotes.clear();
        sortedChord.clear();
        stopAllActiveNotes (out, sampleOffset);
        out.addEvent (message, sampleOffset);
    }
    else
    {
        // CC, pitch bend, aftertouch, program change: straight through.
        out.addEvent (message, sampleOffset);
    }
}

//==============================================================================
void SimpleArpAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int numSamples = buffer.getNumSamples();

    if (numSamples <= 0)
    {
        midiMessages.clear();
        return;
    }

    const double sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;

    // ---- host transport -------------------------------------------------
    double bpm = 120.0;
    double ppqStart = 0.0;
    bool playing = false;
    bool havePpq = false;

    if (auto* transport = getPlayHead())
    {
        if (auto position = transport->getPosition(); position.hasValue())
        {
            if (auto tempo = position->getBpm(); tempo.hasValue() && *tempo > 0.0)
                bpm = *tempo;

            playing = position->getIsPlaying();

            if (auto ppq = position->getPpqPosition(); ppq.hasValue())
            {
                ppqStart = *ppq;
                havePpq = true;
            }
        }
    }

    const bool synced = playing && havePpq;

    // ---- step geometry --------------------------------------------------
    const double stepQuarters   = quarterNotesForRate ((int) rateParam->load());
    const double samplesPerStep = juce::jmax (2.0, sampleRate * 60.0 / bpm * stepQuarters);
    const double gateFraction   = juce::jlimit (0.05, 1.0, (double) gateParam->load() * 0.01);
    const double gateSamples    = juce::jlimit (1.0, samplesPerStep - 1.0, samplesPerStep * gateFraction);

    // How late a re-pressed chord may be and still claim the step it missed. 15ms is
    // under a fifth of a step at these rates, and comfortably covers one buffer.
    currentGateSamples = gateSamples;
    retriggerGraceSamples = (int) (sampleRate * 0.015);

    // A chord change written on the beat does not always arrive exactly on the sample
    // its step fires on -- hosts deliver the release and re-press a sample or two late.
    // Messages that close are treated as simultaneous with the step, so the step plays
    // the chord that is arriving rather than the one being replaced. Half a millisecond
    // is far below anything audible and far above the skew a host introduces.
    simultaneousGraceSamples = juce::jlimit (1, 64, (int) (sampleRate * 0.0005));

    juce::MidiBuffer output;

    // Anything orphaned by a reset gets released before this block plays anything.
    for (const auto& orphan : notesToRelease)
        output.addEvent (juce::MidiMessage::noteOff (orphan.channel, orphan.note), 0);

    notesToRelease.clear();

    // ---- state that can change between blocks ---------------------------
    const bool latched = latchParam->load() > 0.5f;

    if (wasLatched && ! latched)
    {
        // Latch released: keep only what is actually still held down.
        patternNotes.erase (std::remove_if (patternNotes.begin(), patternNotes.end(),
                                            [this] (const HeldNote& h)
                                            {
                                                return std::find (keysDown.begin(), keysDown.end(), h.note) == keysDown.end();
                                            }),
                            patternNotes.end());
        rebuildSequence();
    }

    wasLatched = latched;

    const int octaves = juce::jlimit (1, 4, (int) octavesParam->load());
    const int rowMode = (int) rowModeParam->load();
    const int key = (int) keyParam->load();

    if (octaves != cachedOctaves || rowMode != cachedRowMode || key != cachedKey)
    {
        cachedOctaves = octaves;
        cachedRowMode = rowMode;
        cachedKey = key;
        rebuildSequence();
    }

    if (wasPlaying && ! playing)
    {
        // Handing over to free-running: restart the pattern rather than resuming at
        // whatever column the bar happened to stop on.
        stopAllActiveNotes (output, 0);
        freeStepCounter = 0;
        lastSyncedStep = std::numeric_limits<long long>::min();
        nextFreeRunStep = 0.0;
    }

    wasPlaying = playing;

    // ---- walk the block, merging incoming MIDI with generated steps ------
    const double blockQuarters     = (double) numSamples / sampleRate * bpm / 60.0;
    const double quartersPerSample = blockQuarters > 0.0 ? blockQuarters / (double) numSamples : 0.0;

    // The host reports ppqPosition as a float derived from time, so it lands near a step
    // boundary rather than exactly on it. Tolerate a fraction of a step either side: at
    // 1/16 and 125bpm this is about half a sample, far too small to move a note audibly,
    // but enough that a step is not missed when the host overshoots the boundary.
    constexpr double stepTolerance = 1.0e-4;

    double nextStepPpq = std::ceil (ppqStart / stepQuarters - stepTolerance) * stepQuarters;

    auto nextStepSample = [&]() -> double
    {
        if (synced)
            return quartersPerSample > 0.0 ? (nextStepPpq - ppqStart) / quartersPerSample
                                           : std::numeric_limits<double>::max();

        return nextFreeRunStep;
    };

    auto advanceStep = [&]()
    {
        if (synced) nextStepPpq += stepQuarters;
        else        nextFreeRunStep += samplesPerStep;
    };

    auto midiIt = midiMessages.cbegin();
    const auto midiEnd = midiMessages.cend();

    for (;;)
    {
        const double stepAt = nextStepSample();

        // Round rather than truncate. stepAt is a float derived from the host ppq, so a
        // step whose true position is sample 160 computes as 159.99999 -- and truncating
        // that puts it *before* an incoming message on sample 160, which is exactly where
        // a chord change written on the beat arrives. The step would then sound the chord
        // that is being replaced.
        const int stepSample = stepAt < (double) numSamples
                                 ? juce::jlimit (0, numSamples - 1, juce::roundToInt (stepAt))
                                 : std::numeric_limits<int>::max();
        const int midiSample = midiIt != midiEnd
                                 ? juce::jlimit (0, numSamples - 1, (*midiIt).samplePosition)
                                 : std::numeric_limits<int>::max();

        if (stepSample == std::numeric_limits<int>::max() && midiSample == std::numeric_limits<int>::max())
            break;

        // Ties, and near-ties, go to the message: see simultaneousGraceSamples above.
        // Guard the sentinel: INT_MAX + grace overflows, and the loop never terminates.
        const int stepCutoff = stepSample == std::numeric_limits<int>::max()
                                 ? stepSample
                                 : stepSample + simultaneousGraceSamples;

        if (midiSample <= stepCutoff)
        {
            const auto metadata = *midiIt;
            ++midiIt;
            handleIncomingMessage (metadata.getMessage(), midiSample, output, synced);
        }
        else
        {
            // In Pattern mode the column is derived from the absolute step count, so a
            // drawn pattern always lines up with the bar on playback.
            const long long stepNumber = synced ? (long long) std::llround (nextStepPpq / stepQuarters)
                                                : freeStepCounter++;

            // A step sitting exactly on a block boundary gets found twice: once at the
            // end of one block, and again at the start of the next, because nextStepPpq
            // is derived fresh from the host position every block. Fire it once.
            if (! synced || stepNumber != lastSyncedStep)
            {
                triggerStep (stepSample, output, gateSamples, stepNumber);

                if (synced)
                    lastSyncedStep = stepNumber;
            }

            advanceStep();
        }
    }

    if (sortedChord.empty())
        displayStep.store (-1);

    if (pendingStepNumber >= 0)
    {
        pendingStepAge += numSamples;

        if (pendingStepAge > retriggerGraceSamples)
            pendingStepNumber = -1;
    }

    if (synced)
        nextFreeRunStep = 0.0;
    else
        nextFreeRunStep = juce::jmax (0.0, nextFreeRunStep - (double) numSamples);

    // ---- release notes whose gate expires inside this block --------------
    for (auto it = activeNotes.begin(); it != activeNotes.end(); )
    {
        if (it->offOffset < numSamples)
        {
            output.addEvent (juce::MidiMessage::noteOff (it->channel, it->note),
                             juce::jmax (0, it->offOffset));
            it = activeNotes.erase (it);
        }
        else
        {
            it->offOffset -= numSamples;
            ++it;
        }
    }

    midiMessages.swapWith (output);
}

void SimpleArpAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    buffer.clear();

    if (! activeNotes.empty())
    {
        juce::MidiBuffer output;
        stopAllActiveNotes (output, 0);

        for (const auto metadata : midiMessages)
            output.addEvent (metadata.getMessage(), metadata.samplePosition);

        midiMessages.swapWith (output);
    }

    keysDown.clear();
    patternNotes.clear();
    sortedChord.clear();
}

//==============================================================================
juce::AudioProcessorEditor* SimpleArpAudioProcessor::createEditor()
{
    return new SimpleArpAudioProcessorEditor (*this);
}

void SimpleArpAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("pattern", patternToString(), nullptr);
    state.setProperty ("velocities", velocitiesToString(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SimpleArpAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            patternFromString (state.getProperty ("pattern").toString());
            velocitiesFromString (state.getProperty ("velocities").toString());
            apvts.replaceState (state);
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpleArpAudioProcessor();
}
