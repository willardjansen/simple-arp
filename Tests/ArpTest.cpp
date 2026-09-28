/*
    Headless harness for the arpeggiator core: drives SimpleArpAudioProcessor with a
    fake host transport and checks the MIDI it emits. Run the ArpTest target.
*/

#include "../Source/PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <iostream>
#include <algorithm>
#include <map>
#include <vector>

//==============================================================================
namespace
{

struct TestPlayHead final : juce::AudioPlayHead
{
    double bpm = 120.0;
    double ppq = 0.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setIsPlaying (playing);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }
};

struct Event
{
    long long sample = 0;
    bool isOn = false;
    int note = 0;
    int velocity = 0;
};

constexpr double testSampleRate = 48000.0;
constexpr int blockSize = 512;

int failures = 0;

void check (bool condition, const juce::String& what)
{
    if (condition)
    {
        std::cout << "  [ok]   " << what << std::endl;
    }
    else
    {
        std::cout << "  [FAIL] " << what << std::endl;
        ++failures;
    }
}

void setParam (juce::AudioProcessorValueTreeState& state, const juce::String& id, float value)
{
    auto* param = state.getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

/** APVTS copies parameter changes into its ValueTree asynchronously, so
    getParameterAsValue lags behind in a headless test. Read the atomic the processor
    itself reads instead.
*/
int paramValue (juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    auto* raw = state.getRawParameterValue (id);
    jassert (raw != nullptr);
    return (int) raw->load();
}

/** Runs the processor for a number of blocks, injecting a chord at sample 0 of the
    first block, and returns every note event it produced (absolute sample positions).
*/
std::vector<Event> run (SimpleArpAudioProcessor& proc, TestPlayHead& playHead,
                        const std::vector<int>& chord, int numBlocks,
                        int releaseChordAtBlock = -1, int stopTransportAtBlock = -1)
{
    std::vector<Event> events;
    juce::AudioBuffer<float> buffer (2, blockSize);

    const double ppqPerBlock = (double) blockSize / testSampleRate * playHead.bpm / 60.0;

    for (int block = 0; block < numBlocks; ++block)
    {
        juce::MidiBuffer midi;

        if (block == 0)
            for (int note : chord)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);

        if (block == releaseChordAtBlock)
            for (int note : chord)
                midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);

        if (block == stopTransportAtBlock)
            playHead.playing = false;

        proc.processBlock (buffer, midi);

        const long long blockStart = (long long) block * blockSize;

        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();

            if (message.isNoteOn())
                events.push_back ({ blockStart + metadata.samplePosition, true, message.getNoteNumber(), message.getVelocity() });
            else if (message.isNoteOff())
                events.push_back ({ blockStart + metadata.samplePosition, false, message.getNoteNumber() });
        }

        if (playHead.playing)
            playHead.ppq += ppqPerBlock;
    }

    return events;
}

std::vector<int> noteOnOrder (const std::vector<Event>& events, size_t limit)
{
    std::vector<int> notes;

    for (const auto& e : events)
    {
        if (e.isOn)
        {
            notes.push_back (e.note);

            if (notes.size() >= limit)
                break;
        }
    }

    return notes;
}

juce::String describe (const std::vector<int>& notes)
{
    juce::StringArray parts;

    for (int n : notes)
        parts.add (juce::String (n));

    return parts.joinIntoString (", ");
}

bool notesMatch (const std::vector<int>& actual, const std::vector<int>& expected)
{
    return actual == expected;
}

/** Counts how many notes are still sounding at the end, or -1 if the stream is
    malformed (a doubled note-on, or a note-off for something that wasn't playing).
*/
int openNotesAtEnd (const std::vector<Event>& events)
{
    std::map<int, int> open;

    for (const auto& e : events)
    {
        if (e.isOn)
        {
            if (++open[e.note] > 1)
                return -1;              // overlapping note-on for the same pitch
        }
        else
        {
            if (--open[e.note] < 0)
                return -1;              // note-off with nothing sounding
        }
    }

    int stillOpen = 0;

    for (const auto& entry : open)
        stillOpen += entry.second;

    return stillOpen;
}

/** Well-formed stream, allowing the one note that may still be sounding when the
    capture window ends.
*/
bool noStuckNotes (const std::vector<Event>& events)
{
    const int stillOpen = openNotesAtEnd (events);
    return stillOpen >= 0 && stillOpen <= 1;
}

/** Well-formed stream with nothing left sounding at all. */
bool allNotesClosed (const std::vector<Event>& events)
{
    return openNotesAtEnd (events) == 0;
}

//==============================================================================
void testTimingAndOrder()
{
    std::cout << "Up, 2 octaves, 1/16 @ 120bpm, C-E-G:" << std::endl;

    SimpleArpAudioProcessor proc;
    proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
    proc.prepareToPlay (testSampleRate, blockSize);

    TestPlayHead playHead;
    proc.setPlayHead (&playHead);

    setParam (proc.apvts, "rate", 6);                                  // 1/16
    setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
    setParam (proc.apvts, "octaves", 2);
    setParam (proc.apvts, "gate", 50.0f);
    setParam (proc.apvts, "steps", 16);

    // Two octaves of a triad reach six rows; walk them as a rising staircase.
    proc.clearPattern();

    for (int step = 0; step < 16; ++step)
        proc.setPatternCell (step % 6, step, true);

    // 1/16 at 120bpm == 0.25 quarter notes == 0.125s == 6000 samples.
    const auto events = run (proc, playHead, { 60, 64, 67 }, 100);
    const auto notes = noteOnOrder (events, 7);

    check (notesMatch (notes, { 60, 64, 67, 72, 76, 79, 60 }),
           "step order is " + describe (notes) + " (expected 60, 64, 67, 72, 76, 79, 60)");

    std::vector<long long> onSamples;

    for (const auto& e : events)
        if (e.isOn)
            onSamples.push_back (e.sample);

    bool spacingOk = onSamples.size() >= 7;

    for (size_t i = 1; i < onSamples.size() && spacingOk; ++i)
        spacingOk = std::abs ((onSamples[i] - onSamples[i - 1]) - 6000LL) <= 1;

    check (spacingOk, "steps are 6000 samples apart, first at "
                        + juce::String (onSamples.empty() ? -1 : onSamples.front()));

    // Gate 50% of a 6000-sample step == 3000 samples.
    bool gateOk = true;

    for (const auto& on : events)
    {
        if (! on.isOn)
            continue;

        bool found = false;

        for (const auto& off : events)
            if (! off.isOn && off.note == on.note && off.sample > on.sample)
            {
                found = std::abs ((off.sample - on.sample) - 3000LL) <= 1;
                break;
            }

        if (! found)
        {
            gateOk = false;
            break;
        }
    }

    check (gateOk, "note lengths honour the 50% gate (3000 samples)");
    check (noStuckNotes (events), "no overlapping or unmatched notes");
}

/** Reads the row switched on in each column, or -1 for an empty column. Assumes a
    single note per step, which is what every Fill shape writes.
*/
std::vector<int> patternRowsPerStep (const SimpleArpAudioProcessor& proc, int numSteps)
{
    std::vector<int> rows;

    for (int step = 0; step < numSteps; ++step)
    {
        int found = -1;

        for (int row = 0; row < SimpleArpAudioProcessor::numPatternRows; ++row)
            if (proc.getPatternCell (row, step))
            {
                if (found >= 0)
                {
                    found = -2;         // more than one cell in this column
                    break;
                }

                found = row;
            }

        rows.push_back (found);
    }

    return rows;
}

void testFillShapes()
{
    std::cout << "Fill shapes (nothing held, so all 12 rows are in reach):" << std::endl;

    struct Case
    {
        int shape;
        const char* name;
        std::vector<int> expected;
    };

    // 16 steps over 12 rows.
    const Case cases[]
    {
        { SimpleArpAudioProcessor::fillUp,   "Up",
          { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 1, 2, 3 } },
        { SimpleArpAudioProcessor::fillDown, "Down",
          { 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 11, 10, 9, 8 } },
        // Mirrors at the top without repeating the turning point.
        { SimpleArpAudioProcessor::fillUpDown, "Up-Down",
          { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 10, 9, 8, 7 } }
    };

    for (const auto& testCase : cases)
    {
        SimpleArpAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
        proc.prepareToPlay (testSampleRate, blockSize);

        setParam (proc.apvts, "steps", 16);
        proc.fillPattern (testCase.shape);

        const auto rows = patternRowsPerStep (proc, 16);

        check (rows == testCase.expected,
               juce::String (testCase.name) + " -> " + describe (rows)
                   + " (expected " + describe (testCase.expected) + ")");
    }

    {
        SimpleArpAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
        proc.prepareToPlay (testSampleRate, blockSize);

        setParam (proc.apvts, "steps", 16);
        proc.fillPattern (SimpleArpAudioProcessor::fillRandom);

        const auto rows = patternRowsPerStep (proc, 16);
        bool valid = rows.size() == 16;

        for (int row : rows)
            if (row < 0 || row >= SimpleArpAudioProcessor::numPatternRows)
                valid = false;

        check (valid, "Random fills every step with exactly one row in range");
    }

    {
        // Fill respects the reach, so a triad on Octaves 1 only uses three rows.
        SimpleArpAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
        proc.prepareToPlay (testSampleRate, blockSize);

        TestPlayHead playHead;
        proc.setPlayHead (&playHead);

        setParam (proc.apvts, "rate", 6);
        setParam (proc.apvts, "steps", 16);
        setParam (proc.apvts, "octaves", 1);
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);

        run (proc, playHead, { 60, 64, 67 }, 2);    // let the chord register
        proc.fillPattern (SimpleArpAudioProcessor::fillUp);

        const auto rows = patternRowsPerStep (proc, 6);

        check (rows == std::vector<int> { 0, 1, 2, 0, 1, 2 },
               "Fill follows the chord x Octaves reach -> " + describe (rows)
                   + " (expected 0, 1, 2, 0, 1, 2)");
    }
}

/** Reproduces Willard's session: 125bpm, 16 steps of 1/16, one note on the downbeat.
    ppq is computed from the absolute sample position the way a host does it, not
    accumulated, because that is what puts a step exactly on a block boundary.
*/
void testDownbeatFiresOncePerBar()
{
    std::cout << "Downbeat at 125bpm (bar = 92160 samples):" << std::endl;

    for (int blocks : { 512, 256, 1024 })
    {
        SimpleArpAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (testSampleRate, blocks);
        proc.prepareToPlay (testSampleRate, blocks);

        TestPlayHead playHead;
        playHead.bpm = 125.0;
        proc.setPlayHead (&playHead);

        setParam (proc.apvts, "rate", 6);          // 1/16
        setParam (proc.apvts, "steps", 16);        // one bar
        setParam (proc.apvts, "gate", 50.0f);
        setParam (proc.apvts, "octaves", 1);
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChromatic);

        proc.clearPattern();
        proc.setPatternCell (0, 0, true);          // the leftmost cell only

        juce::AudioBuffer<float> buffer (2, blocks);
        std::vector<long long> ons;

        const int numBars = 8;
        const long long barSamples = 92160;
        const int numBlocks = (int) (numBars * barSamples / blocks);

        for (int block = 0; block < numBlocks; ++block)
        {
            const long long samplePos = (long long) block * blocks;

            // Host-style: derived from where we are, not summed up block by block.
            playHead.ppq = (double) samplePos / testSampleRate * 125.0 / 60.0;

            juce::MidiBuffer midi;

            if (block == 0)
                for (int note : { 60, 64, 67 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);

            proc.processBlock (buffer, midi);

            for (const auto metadata : midi)
                if (metadata.getMessage().isNoteOn())
                    ons.push_back (samplePos + metadata.samplePosition);
        }

        long long tightest = barSamples;

        for (size_t i = 1; i < ons.size(); ++i)
            tightest = juce::jmin (tightest, ons[i] - ons[i - 1]);

        // The downbeat closing the window may land a sample early at the very end of
        // the last block, so numBars + 1 is legitimate. Two within a bar is not.
        check ((int) ons.size() >= numBars && (int) ons.size() <= numBars + 1
                   && tightest >= barSamples - 2,
               juce::String (blocks) + "-sample blocks: " + juce::String ((int) ons.size())
                   + " downbeats over " + juce::String (numBars)
                   + " bars, closest pair " + juce::String (tightest) + " samples apart");
    }
}

/** A real host's ppqPosition is a float derived from time, so it lands near the exact
    value rather than on it. Model that noise and make sure no step is lost.
*/
/** Hosts call reset() and prepareToPlay() during playback — locates, loop jumps, device
    changes. Notes sounding at that moment must still be released.
*/
void testResetReleasesSoundingNotes()
{
    std::cout << "Reset while notes are sounding:" << std::endl;

    SimpleArpAudioProcessor proc;
    proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
    proc.prepareToPlay (testSampleRate, blockSize);

    TestPlayHead playHead;
    proc.setPlayHead (&playHead);

    setParam (proc.apvts, "rate", 6);
    setParam (proc.apvts, "steps", 4);
    setParam (proc.apvts, "gate", 95.0f);      // long notes, very likely still sounding
    setParam (proc.apvts, "octaves", 1);
    setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChromatic);

    proc.clearPattern();

    for (int step = 0; step < 4; ++step)
        proc.setPatternCell (step, step, true);

    auto events = run (proc, playHead, { 60, 64, 67 }, 40);

    const int openBefore = openNotesAtEnd (events);
    check (openBefore > 0, "a note is sounding when the reset lands ("
                               + juce::String (openBefore) + " open)");

    proc.reset();

    // One more block, no incoming MIDI: the orphans must come out here.
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    proc.processBlock (buffer, midi);

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())
            events.push_back ({ 40LL * blockSize + metadata.samplePosition, true, message.getNoteNumber() });
        else if (message.isNoteOff())
            events.push_back ({ 40LL * blockSize + metadata.samplePosition, false, message.getNoteNumber() });
    }

    check (allNotesClosed (events), "everything is released after reset()");
}

/** A one-bar cycle: the transport wraps ppq back to the loop start every bar. The wrap
    lands at a different place inside a block depending on when playback began, which is
    why the symptom is unpredictable rather than every-other-bar.
*/
/** A cell drawn in column N must sound at exactly ppq N * stepQuarters — no earlier,
    no later. This is the "is the arp a step ahead?" question, asked directly.
*/
/** A one-bar part on cycle: at every loop point the host releases the held note and
    re-presses it. If the note-on lands after the downbeat step, the step finds no chord.
*/
void testLoopRetriggerAtCycleStart()
{
    std::cout << "One-bar part on cycle, note released and re-pressed at the loop:" << std::endl;

    const long long barSamples = 92160;

    for (int gap : { 0, 1, 2, 64 })
    {
        SimpleArpAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (testSampleRate, 512);
        proc.prepareToPlay (testSampleRate, 512);

        TestPlayHead playHead;
        playHead.bpm = 125.0;
        proc.setPlayHead (&playHead);

        setParam (proc.apvts, "rate", 6);
        setParam (proc.apvts, "steps", 16);
        setParam (proc.apvts, "gate", 50.0f);
        setParam (proc.apvts, "octaves", 1);
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChromatic);

        proc.clearPattern();
        proc.setPatternCell (0, 0, true);          // leftmost cell only

        juce::AudioBuffer<float> buffer (2, 512);
        int downbeats = 0;

        const int numLoops = 12;
        const int numBlocks = numLoops * (int) (barSamples / 512);

        for (int block = 0; block < numBlocks; ++block)
        {
            const long long blockStart = (long long) block * 512;
            const long long inBar = blockStart % barSamples;

            playHead.ppq = (double) inBar / testSampleRate * 125.0 / 60.0;

            juce::MidiBuffer midi;

            if (block == 0)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, 68, (juce::uint8) 100), 0);
            }
            else if (inBar == 0)
            {
                // Cycle point: the part ends and restarts.
                midi.addEvent (juce::MidiMessage::noteOff (1, 68), 0);
                midi.addEvent (juce::MidiMessage::noteOn (1, 68, (juce::uint8) 100),
                               juce::jmin (511, gap));
            }

            proc.processBlock (buffer, midi);

            for (const auto metadata : midi)
                if (metadata.getMessage().isNoteOn())
                    ++downbeats;
        }

        check (downbeats == numLoops,
               "note-on " + juce::String (gap) + " sample(s) after the release: "
                   + juce::String (downbeats) + " downbeats over "
                   + juce::String (numLoops) + " loops");
    }
}

void testColumnLandsOnItsOwnStep()
{
    std::cout << "Column N sounds on 16th N (125bpm, one cell at a time):" << std::endl;

    const long long samplesPerStep = 5760;      // 1/16 at 125bpm, 48k
    int wrong = 0;
    juce::String detail;

    for (int column = 0; column < 16; ++column)
    {
        SimpleArpAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (testSampleRate, 512);
        proc.prepareToPlay (testSampleRate, 512);

        TestPlayHead playHead;
        playHead.bpm = 125.0;
        proc.setPlayHead (&playHead);

        setParam (proc.apvts, "rate", 6);
        setParam (proc.apvts, "steps", 16);
        setParam (proc.apvts, "gate", 50.0f);
        setParam (proc.apvts, "octaves", 1);
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChromatic);

        proc.clearPattern();
        proc.setPatternCell (0, column, true);   // exactly one cell, in this column

        juce::AudioBuffer<float> buffer (2, 512);
        long long firstOn = -1;

        for (int block = 0; block < 180; ++block)   // one bar
        {
            const long long samplePos = (long long) block * 512;
            playHead.ppq = (double) samplePos / testSampleRate * 125.0 / 60.0;

            juce::MidiBuffer midi;

            if (block == 0)
                for (int note : { 60, 64, 67 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);

            proc.processBlock (buffer, midi);

            for (const auto metadata : midi)
                if (metadata.getMessage().isNoteOn() && firstOn < 0)
                    firstOn = samplePos + metadata.samplePosition;
        }

        const long long expected = (long long) column * samplesPerStep;

        if (std::abs (firstOn - expected) > 1)
        {
            ++wrong;
            detail += " col" + juce::String (column) + "@" + juce::String (firstOn)
                        + "(want " + juce::String (expected) + ")";
        }
    }

    check (wrong == 0, wrong == 0 ? "all 16 columns land on their own 16th"
                                  : juce::String (wrong) + " column(s) misplaced:" + detail);
}

void testLoopedBar()
{
    std::cout << "One-bar loop at 125bpm (wrap at varying block offsets):" << std::endl;

    for (int startOffset : { 0, 128, 256, 384 })
    {
        SimpleArpAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (testSampleRate, 512);
        proc.prepareToPlay (testSampleRate, 512);

        TestPlayHead playHead;
        playHead.bpm = 125.0;
        proc.setPlayHead (&playHead);

        setParam (proc.apvts, "rate", 6);
        setParam (proc.apvts, "steps", 16);
        setParam (proc.apvts, "gate", 50.0f);
        setParam (proc.apvts, "octaves", 1);
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChromatic);

        proc.clearPattern();
        proc.setPatternCell (0, 0, true);           // leftmost cell only

        juce::AudioBuffer<float> buffer (2, 512);
        std::vector<long long> ons;

        const long long barSamples = 92160;
        const int numLoops = 12;
        const int numBlocks = numLoops * (int) (barSamples / 512);

        for (int block = 0; block < numBlocks; ++block)
        {
            const long long played = (long long) block * 512 + startOffset;

            // Cycle: position within the bar, wrapping back to the loop start.
            const long long inBar = played % barSamples;
            playHead.ppq = (double) inBar / testSampleRate * 125.0 / 60.0;

            juce::MidiBuffer midi;

            if (block == 0)
                for (int note : { 60, 64, 67 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);

            proc.processBlock (buffer, midi);

            for (const auto metadata : midi)
                if (metadata.getMessage().isNoteOn())
                    ons.push_back ((long long) block * 512 + metadata.samplePosition);
        }

        // One downbeat per loop, and never two nearly on top of each other.
        long long tightest = barSamples;

        for (size_t i = 1; i < ons.size(); ++i)
            tightest = juce::jmin (tightest, ons[i] - ons[i - 1]);

        const bool ok = (int) ons.size() >= numLoops - 1
                     && (int) ons.size() <= numLoops + 1
                     && tightest > 2880;            // half a step

        check (ok, "start offset " + juce::String (startOffset) + ": "
                       + juce::String ((int) ons.size()) + " downbeats over "
                       + juce::String (numLoops) + " loops, closest pair "
                       + juce::String (tightest));
    }
}

void testPpqJitter()
{
    std::cout << "Downbeat with host-style ppq noise (125bpm, 512 blocks):" << std::endl;

    for (double noise : { 1.0e-12, 1.0e-10, 1.0e-9, 1.0e-8, 1.0e-7 })
    {
        SimpleArpAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (testSampleRate, 512);
        proc.prepareToPlay (testSampleRate, 512);

        TestPlayHead playHead;
        playHead.bpm = 125.0;
        proc.setPlayHead (&playHead);

        setParam (proc.apvts, "rate", 6);
        setParam (proc.apvts, "steps", 16);
        setParam (proc.apvts, "gate", 50.0f);
        setParam (proc.apvts, "octaves", 1);
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChromatic);

        proc.clearPattern();
        proc.setPatternCell (0, 0, true);      // leftmost cell only

        juce::AudioBuffer<float> buffer (2, 512);
        juce::Random random (1234);
        std::vector<long long> ons;

        const int numBars = 16;
        const int numBlocks = numBars * 92160 / 512;

        for (int block = 0; block < numBlocks; ++block)
        {
            const long long samplePos = (long long) block * 512;
            const double exact = (double) samplePos / testSampleRate * 125.0 / 60.0;

            playHead.ppq = exact + (random.nextDouble() * 2.0 - 1.0) * noise;

            juce::MidiBuffer midi;

            if (block == 0)
                for (int note : { 60, 64, 67 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);

            proc.processBlock (buffer, midi);

            for (const auto metadata : midi)
                if (metadata.getMessage().isNoteOn())
                    ons.push_back (samplePos + metadata.samplePosition);
        }

        check ((int) ons.size() >= numBars,
               "noise " + juce::String (noise, 13) + " -> " + juce::String ((int) ons.size())
                   + " downbeats over " + juce::String (numBars) + " bars");
    }
}

void testNoSkippedSteps()
{
    std::cout << "Step boundaries across tempos and block sizes:" << std::endl;

    // Awkward on purpose: tempos and buffer sizes where bar lines do not land on block
    // boundaries, and some where they land exactly on them.
    const double tempos[] { 120.0, 128.0, 137.3, 90.0, 174.0 };
    const int blockSizes[] { 512, 256, 480, 1024, 128 };

    for (double bpm : tempos)
    {
        for (int blocks : blockSizes)
        {
            SimpleArpAudioProcessor proc;
            proc.setRateAndBufferSizeDetails (testSampleRate, blocks);
            proc.prepareToPlay (testSampleRate, blocks);

            TestPlayHead playHead;
            playHead.bpm = bpm;
            proc.setPlayHead (&playHead);

            setParam (proc.apvts, "rate", 6);                 // 1/16
            setParam (proc.apvts, "steps", 1);                // every step sounds
            setParam (proc.apvts, "gate", 50.0f);
            setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChromatic);

            proc.clearPattern();
            proc.setPatternCell (0, 0, true);

            const double samplesPerStep = testSampleRate * 60.0 / bpm * 0.25;
            const double ppqPerBlock = (double) blocks / testSampleRate * bpm / 60.0;

            juce::AudioBuffer<float> buffer (2, blocks);
            std::vector<long long> ons;

            const int numBlocks = (int) (testSampleRate * 8.0 / blocks);   // ~8 seconds

            for (int block = 0; block < numBlocks; ++block)
            {
                juce::MidiBuffer midi;

                if (block == 0)
                    for (int note : { 60, 64, 67 })
                        midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);

                proc.processBlock (buffer, midi);

                for (const auto metadata : midi)
                    if (metadata.getMessage().isNoteOn())
                        ons.push_back ((long long) block * blocks + metadata.samplePosition);

                playHead.ppq += ppqPerBlock;
            }

            // A skipped step shows up as a gap of roughly two steps.
            long long worst = 0;

            for (size_t i = 1; i < ons.size(); ++i)
                worst = juce::jmax (worst, ons[i] - ons[i - 1]);

            // Steps land at k * stepQuarters for every k inside the window.
            const double totalQuarters = (double) numBlocks * blocks / testSampleRate * bpm / 60.0;
            const int expected = (int) std::floor (totalQuarters / 0.25) + 1;

            // Step positions are rounded to the nearest sample, so a gap can be a sample
            // either side of the ideal. A skipped step is a gap of roughly two steps, so
            // a couple of samples of slack still catches what this is here to catch.
            const bool ok = std::abs ((int) ons.size() - expected) <= 1
                         && (double) worst <= samplesPerStep + 2.0;

            check (ok, juce::String (bpm, 1) + "bpm / " + juce::String (blocks)
                           + "-sample blocks: " + juce::String ((int) ons.size())
                           + " steps (expected " + juce::String (expected)
                           + "), widest gap " + juce::String (worst)
                           + " (step is " + juce::String (samplesPerStep, 1) + ")");
        }
    }
}

void testGateLengths()
{
    std::cout << "Gate, 1/16 @ 120bpm (6000-sample step):" << std::endl;

    struct Case { float gate; long long expected; };

    const Case cases[] { { 10.0f, 600 }, { 25.0f, 1500 }, { 50.0f, 3000 }, { 90.0f, 5400 } };

    for (const auto& testCase : cases)
    {
        SimpleArpAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
        proc.prepareToPlay (testSampleRate, blockSize);

        TestPlayHead playHead;
        proc.setPlayHead (&playHead);

        setParam (proc.apvts, "rate", 6);
        setParam (proc.apvts, "steps", 4);
        setParam (proc.apvts, "octaves", 1);
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
        setParam (proc.apvts, "gate", testCase.gate);

        proc.clearPattern();
        proc.setPatternCell (0, 0, true);

        const auto events = run (proc, playHead, { 60, 64, 67 }, 100);

        long long length = -1;

        for (const auto& on : events)
        {
            if (! on.isOn)
                continue;

            for (const auto& off : events)
                if (! off.isOn && off.note == on.note && off.sample > on.sample)
                {
                    length = off.sample - on.sample;
                    break;
                }

            break;
        }

        check (std::abs (length - testCase.expected) <= 1,
               juce::String (testCase.gate, 0) + "% -> " + juce::String (length)
                   + " samples (expected " + juce::String (testCase.expected) + ")");
    }
}

void testFreeRunStart()
{
    std::cout << "Free-running (transport stopped):" << std::endl;

    SimpleArpAudioProcessor proc;
    proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
    proc.prepareToPlay (testSampleRate, blockSize);

    TestPlayHead playHead;
    playHead.playing = false;               // no host transport
    proc.setPlayHead (&playHead);

    setParam (proc.apvts, "rate", 6);
    setParam (proc.apvts, "gate", 50.0f);
    setParam (proc.apvts, "steps", 4);
    setParam (proc.apvts, "octaves", 1);
    setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);

    // Only the first column sounds, so a pattern that starts anywhere else is silent.
    proc.clearPattern();
    proc.setPatternCell (0, 0, true);

    // Idle first: step boundaries keep passing with nothing held, which is what used to
    // leave the pattern starting at an arbitrary column when the chord finally arrived.
    run (proc, playHead, {}, 30);

    const auto events = run (proc, playHead, { 60, 64, 67 }, 60);

    std::vector<Event> ons;

    for (const auto& e : events)
        if (e.isOn)
            ons.push_back (e);

    check (! ons.empty() && ons.front().sample == 0 && ons.front().note == 60,
           "the pattern starts on step 1 under the finger (first note-on at "
               + juce::String (ons.empty() ? -1 : ons.front().sample) + ")");

    // 4 steps of 6000 samples: the single active column comes round every 24000.
    bool spacingOk = ons.size() >= 2;

    for (size_t i = 1; i < ons.size() && spacingOk; ++i)
        spacingOk = std::abs ((ons[i].sample - ons[i - 1].sample) - 24000LL) <= 1;

    check (spacingOk, "free-running steps stay on the pattern length");
    check (noStuckNotes (events), "no stuck notes while free-running");
}

void testKeyReleaseAndTransportStop()
{
    std::cout << "Note-off and transport handling:" << std::endl;

    {
        SimpleArpAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
        proc.prepareToPlay (testSampleRate, blockSize);

        TestPlayHead playHead;
        proc.setPlayHead (&playHead);

        setParam (proc.apvts, "rate", 6);
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
        setParam (proc.apvts, "octaves", 1);
        setParam (proc.apvts, "gate", 50.0f);

        // Release the chord at block 30 (~15360 samples, after two steps).
        const auto events = run (proc, playHead, { 60, 64, 67 }, 100, 30);

        long long lastOn = -1;

        for (const auto& e : events)
            if (e.isOn)
                lastOn = e.sample;

        check (lastOn >= 0 && lastOn < 30LL * blockSize,
               "arp stops when the keys are released (last note-on at " + juce::String (lastOn) + ")");
        check (noStuckNotes (events), "no stuck notes after release");
    }

    {
        SimpleArpAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
        proc.prepareToPlay (testSampleRate, blockSize);

        TestPlayHead playHead;
        proc.setPlayHead (&playHead);

        setParam (proc.apvts, "rate", 6);
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
        setParam (proc.apvts, "octaves", 1);
        setParam (proc.apvts, "gate", 95.0f);   // long notes, still sounding when the stop lands

        // Stop the transport and lift the keys at the same time: nothing should be
        // left hanging afterwards.
        const auto events = run (proc, playHead, { 60, 64, 67 }, 40, 20, 20);

        check (allNotesClosed (events), "everything is released when the transport stops");
    }
}

void testLatch()
{
    std::cout << "Latch:" << std::endl;

    SimpleArpAudioProcessor proc;
    proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
    proc.prepareToPlay (testSampleRate, blockSize);

    TestPlayHead playHead;
    proc.setPlayHead (&playHead);

    setParam (proc.apvts, "rate", 6);
    setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
    setParam (proc.apvts, "octaves", 1);
    setParam (proc.apvts, "gate", 50.0f);
    setParam (proc.apvts, "latch", 1.0f);

    const auto events = run (proc, playHead, { 60, 64, 67 }, 100, 30);

    long long lastOn = -1;

    for (const auto& e : events)
        if (e.isOn)
            lastOn = e.sample;

    check (lastOn > 30LL * blockSize,
           "pattern keeps running after the keys are released (last note-on at "
               + juce::String (lastOn) + ")");
    check (noStuckNotes (events), "no stuck notes with latch on");
}

void testPatternGrid()
{
    std::cout << "Pattern grid, chord C-E-G:" << std::endl;

    {
        SimpleArpAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
        proc.prepareToPlay (testSampleRate, blockSize);

        TestPlayHead playHead;
        proc.setPlayHead (&playHead);

        setParam (proc.apvts, "rate", 6);
        setParam (proc.apvts, "gate", 50.0f);
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
        setParam (proc.apvts, "steps", 4);
        setParam (proc.apvts, "octaves", 2);     // row 3 needs two octaves of reach

        // A rising staircase; row 3 wraps past the 3-note chord into the next octave.
        proc.clearPattern();
        proc.setPatternCell (0, 0, true);
        proc.setPatternCell (1, 1, true);
        proc.setPatternCell (2, 2, true);
        proc.setPatternCell (3, 3, true);

        const auto events = run (proc, playHead, { 60, 64, 67 }, 100);
        const auto notes = noteOnOrder (events, 6);

        check (notesMatch (notes, { 60, 64, 67, 72, 60, 64 }),
               "rows map to chord tones and wrap an octave -> " + describe (notes)
                   + " (expected 60, 64, 67, 72, 60, 64)");
        check (noStuckNotes (events), "no stuck notes while the grid runs");
    }

    {
        SimpleArpAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
        proc.prepareToPlay (testSampleRate, blockSize);

        TestPlayHead playHead;
        proc.setPlayHead (&playHead);

        setParam (proc.apvts, "rate", 6);
        setParam (proc.apvts, "gate", 50.0f);
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
        setParam (proc.apvts, "steps", 2);

        // Two rows in the same column must fire together.
        proc.clearPattern();
        proc.setPatternCell (0, 0, true);
        proc.setPatternCell (2, 0, true);

        const auto events = run (proc, playHead, { 60, 64, 67 }, 40);

        std::vector<Event> firstOns;

        for (const auto& e : events)
            if (e.isOn && firstOns.size() < 2)
                firstOns.push_back (e);

        const bool stacked = firstOns.size() == 2
                          && firstOns[0].sample == firstOns[1].sample
                          && std::min (firstOns[0].note, firstOns[1].note) == 60
                          && std::max (firstOns[0].note, firstOns[1].note) == 67;

        check (stacked, "stacked rows fire together on one step");

        // Column 1 is empty, so steps must alternate sounding / silent.
        std::vector<long long> onSamples;

        for (const auto& e : events)
            if (e.isOn)
                onSamples.push_back (e.sample);

        bool gapsOk = onSamples.size() >= 4;

        for (size_t i = 2; i < onSamples.size() && gapsOk; i += 2)
            gapsOk = std::abs ((onSamples[i] - onSamples[i - 2]) - 12000LL) <= 1;

        check (gapsOk, "empty columns stay silent (12000 samples between hits)");
    }

    {
        SimpleArpAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
        proc.prepareToPlay (testSampleRate, blockSize);

        TestPlayHead playHead;
        proc.setPlayHead (&playHead);

        setParam (proc.apvts, "rate", 6);
        setParam (proc.apvts, "gate", 50.0f);
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
        setParam (proc.apvts, "steps", 3);

        proc.clearPattern();

        for (int step = 0; step < 3; ++step)
            proc.setPatternCell (0, step, true);

        proc.setStepVelocity (0, 30);
        proc.setStepVelocity (1, 80);
        proc.setStepVelocity (2, 127);

        // The chord is played at velocity 100; the lane must override that.
        const auto events = run (proc, playHead, { 60, 64, 67 }, 100);

        std::vector<int> played;

        for (const auto& e : events)
            if (e.isOn && played.size() < 6)
                played.push_back (e.velocity);

        check (played == std::vector<int> { 30, 80, 127, 30, 80, 127 },
               "per-step velocity drives the note-ons -> " + describe (played)
                   + " (expected 30, 80, 127, 30, 80, 127)");

        // Stacked notes on one step should share that step's velocity.
        proc.clearPattern();
        proc.setPatternCell (0, 0, true);
        proc.setPatternCell (2, 0, true);
        proc.setStepVelocity (0, 55);
        setParam (proc.apvts, "steps", 1);

        const auto stacked = run (proc, playHead, { 60, 64, 67 }, 20);
        bool allFiftyFive = ! stacked.empty();

        for (const auto& e : stacked)
            if (e.isOn && e.velocity != 55)
                allFiftyFive = false;

        check (allFiftyFive, "stacked notes share the step velocity");
    }

    {
        // Octaves caps how far up the grid reaches. A triad at Octaves 1 fills rows
        // 0-2; row 3 (the root an octave up) must stay silent until Octaves is 2.
        const int octaveCases[] { 1, 2 };
        const std::vector<int> expected[] { { 60, 60, 60 }, { 60, 72, 60 } };

        for (int i = 0; i < 2; ++i)
        {
            SimpleArpAudioProcessor proc;
            proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
            proc.prepareToPlay (testSampleRate, blockSize);

            TestPlayHead playHead;
            proc.setPlayHead (&playHead);

            setParam (proc.apvts, "rate", 6);
            setParam (proc.apvts, "gate", 50.0f);
            setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
            setParam (proc.apvts, "steps", 2);
            setParam (proc.apvts, "octaves", (float) octaveCases[i]);

            proc.clearPattern();
            proc.setPatternCell (0, 0, true);   // root, always in reach
            proc.setPatternCell (3, 1, true);   // root + 12, only in reach at Octaves 2

            const auto events = run (proc, playHead, { 60, 64, 67 }, 100);
            const auto notes = noteOnOrder (events, expected[i].size());

            check (notesMatch (notes, expected[i]),
                   "Octaves " + juce::String (octaveCases[i]) + " -> " + describe (notes)
                       + " (expected " + describe (expected[i]) + ")");
        }
    }

    {
        // Semitone rows: a fixed chromatic ladder from the lowest held note, the same
        // intervals whatever chord is underneath.
        SimpleArpAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
        proc.prepareToPlay (testSampleRate, blockSize);

        TestPlayHead playHead;
        proc.setPlayHead (&playHead);

        setParam (proc.apvts, "rate", 6);
        setParam (proc.apvts, "gate", 50.0f);
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
        setParam (proc.apvts, "steps", 4);
        setParam (proc.apvts, "octaves", 1);
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChromatic);

        proc.clearPattern();
        proc.setPatternCell (0, 0, true);    // root
        proc.setPatternCell (3, 1, true);    // minor 3rd
        proc.setPatternCell (7, 2, true);    // 5th
        proc.setPatternCell (11, 3, true);   // major 7th — all 12 rows always in reach

        const auto events = run (proc, playHead, { 60, 64, 67 }, 100);
        const auto notes = noteOnOrder (events, 5);

        check (notesMatch (notes, { 60, 63, 67, 71, 60 }),
               "semitone rows are a chromatic ladder -> " + describe (notes)
                   + " (expected 60, 63, 67, 71, 60)");

        // The same grid over a different chord must give the same intervals.
        SimpleArpAudioProcessor other;
        other.setRateAndBufferSizeDetails (testSampleRate, blockSize);
        other.prepareToPlay (testSampleRate, blockSize);

        TestPlayHead otherHead;
        other.setPlayHead (&otherHead);

        setParam (other.apvts, "rate", 6);
        setParam (other.apvts, "gate", 50.0f);
        setParam (other.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
        setParam (other.apvts, "steps", 4);
        setParam (other.apvts, "octaves", 1);
        setParam (other.apvts, "rows", SimpleArpAudioProcessor::rowsChromatic);

        other.clearPattern();
        other.setPatternCell (0, 0, true);
        other.setPatternCell (3, 1, true);
        other.setPatternCell (7, 2, true);
        other.setPatternCell (11, 3, true);

        // Root is now D (62) rather than C.
        const auto otherEvents = run (other, otherHead, { 62, 65, 69 }, 100);
        const auto otherNotes = noteOnOrder (otherEvents, 5);

        check (notesMatch (otherNotes, { 62, 65, 69, 73, 62 }),
               "the same shape transposes with the root -> " + describe (otherNotes)
                   + " (expected 62, 65, 69, 73, 62)");
    }

    {
        // Scale rows: every row is a degree of the chosen scale, so nothing lands
        // outside the key however you draw.
        struct Case
        {
            int rowMode;
            int key;
            const char* name;
            std::vector<int> expected;
        };

        // Rows 0..4 on consecutive steps, chord C-E-G held.
        const Case cases[]
        {
            //                                          C  D  E  F  G
            { 2,  0, "Major, from chord",     { 60, 62, 64, 65, 67 } },
            //                                          C  D Eb  F  G
            { 7,  0, "Minor, from chord",     { 60, 62, 63, 65, 67 } },
            //                                          C Eb  F Gb  G
            { 13, 0, "Blues, from chord",     { 60, 63, 65, 66, 67 } },
            //                        A minor tonic below C: A  B  C  D  E
            { 7, 10, "Minor, key A",          { 57, 59, 60, 62, 64 } }
        };

        for (const auto& testCase : cases)
        {
            SimpleArpAudioProcessor proc;
            proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
            proc.prepareToPlay (testSampleRate, blockSize);

            TestPlayHead playHead;
            proc.setPlayHead (&playHead);

            setParam (proc.apvts, "rate", 6);
            setParam (proc.apvts, "gate", 50.0f);
            setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
            setParam (proc.apvts, "steps", 5);
            setParam (proc.apvts, "octaves", 1);
            setParam (proc.apvts, "rows", (float) testCase.rowMode);
            setParam (proc.apvts, "key", (float) testCase.key);

            proc.clearPattern();

            for (int row = 0; row < 5; ++row)
                proc.setPatternCell (row, row, true);

            const auto events = run (proc, playHead, { 60, 64, 67 }, 100);
            const auto notes = noteOnOrder (events, testCase.expected.size());

            check (notesMatch (notes, testCase.expected),
                   juce::String (testCase.name) + " -> " + describe (notes)
                       + " (expected " + describe (testCase.expected) + ")");
        }
    }
}

//==============================================================================
void testSequenceParsing()
{
    std::cout << "Sequence parsing" << std::endl;

    using Parse = SimpleArpAudioProcessor::SequenceParse;

    {
        // The trance figure from the brief: five groups, the last one four long so the
        // whole thing lands on 16 sixteenths.
        const auto parsed = SimpleArpAudioProcessor::parseSequence ("3-1-2|3-1-2|3-2-3|1-2-3|1-3-2-1");

        check (parsed.ok, "the 3-1-2 figure parses");
        check (parsed.numSteps == 16, "it is 16 steps (got " + juce::String (parsed.numSteps) + ")");
        check (parsed.highestDegree == 3, "highest degree is 3");
        check (parsed.groups == std::vector<int> { 3, 3, 3, 3, 4 }, "groups are 3,3,3,3,4");

        const int firstThree[] { 2, 0, 1 };
        bool rowsOk = parsed.steps.size() == 16;

        for (int i = 0; i < 3 && rowsOk; ++i)
            rowsOk = parsed.steps[(size_t) i] == std::vector<int> { firstThree[i] };

        check (rowsOk, "degree N maps to row N-1");
    }

    {
        // Separators are interchangeable and whitespace is free, so a sequence can be
        // typed however it reads best.
        const auto spaced = SimpleArpAudioProcessor::parseSequence ("3 1 2 | 3, 1, 2");
        const auto dashed = SimpleArpAudioProcessor::parseSequence ("3-1-2|3-1-2");

        check (spaced.ok && dashed.ok && spaced.steps == dashed.steps,
               "spaces and commas separate steps the way '-' does");
    }

    {
        const auto parsed = SimpleArpAudioProcessor::parseSequence ("1-.-3-0-2-_");

        check (parsed.ok && parsed.numSteps == 6, "rests count as steps");
        check (parsed.ok && parsed.steps[1].empty() && parsed.steps[3].empty()
                 && parsed.steps[5].empty(), "'.', '0' and '_' are all rests");
        check (parsed.ok && parsed.steps[0] == std::vector<int> { 0 },
               "a rest does not shift the degrees around it");
    }

    {
        const auto parsed = SimpleArpAudioProcessor::parseSequence ("1+3-2-1+2+3");

        check (parsed.ok, "stacks parse");
        check (parsed.ok && parsed.steps[0] == std::vector<int> { 0, 2 },
               "1+3 is two rows on one step");
        check (parsed.ok && parsed.steps[2] == std::vector<int> { 0, 1, 2 },
               "1+2+3 is three rows on one step");
    }

    {
        check (SimpleArpAudioProcessor::parseSequence ("1+1-2").steps[0] == std::vector<int> { 0 },
               "a repeated degree in a stack collapses");
        check (SimpleArpAudioProcessor::parseSequence ("12-1").ok,
               "12 is one degree, not 1 followed by 2");
    }

    {
        const Parse cases[]
        {
            SimpleArpAudioProcessor::parseSequence (""),
            SimpleArpAudioProcessor::parseSequence ("|||"),
            SimpleArpAudioProcessor::parseSequence ("3-x-2"),
            SimpleArpAudioProcessor::parseSequence ("3-13-2"),
            SimpleArpAudioProcessor::parseSequence ("3-1.5-2"),
        };

        bool allRejected = true;

        for (const auto& parse : cases)
            allRejected = allRejected && ! parse.ok && parse.error.isNotEmpty();

        check (allRejected, "empty, stray and out-of-range input is rejected with a reason");

        juce::String tooLong;

        for (int i = 0; i < SimpleArpAudioProcessor::maxPatternSteps + 1; ++i)
            tooLong += (i == 0 ? "1" : "-1");

        check (! SimpleArpAudioProcessor::parseSequence (tooLong).ok,
               "more than 32 steps is rejected");
        check (SimpleArpAudioProcessor::parseSequence (tooLong.dropLastCharacters (2)).ok,
               "exactly 32 steps is accepted");
    }
}

void testSequenceAppliesToGrid()
{
    std::cout << "Sequence writes the grid" << std::endl;

    {
        SimpleArpAudioProcessor proc;

        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
        setParam (proc.apvts, "steps", 16);
        setParam (proc.apvts, "octaves", 1);

        proc.clearPattern();
        proc.setStepVelocity (1, 42);

        const auto parsed = proc.applySequence ("3-1-2|3-1-2|3-2-3|1-2-3|1-3-2-1");

        check (parsed.ok, "the figure applies");
        check (paramValue (proc.apvts, "steps") == 16,
               "Steps follows the sequence length");

        const int expectedRows[] { 2, 0, 1, 2, 0, 1, 2, 1, 2, 0, 1, 2, 0, 2, 1, 0 };
        bool gridOk = true;

        for (int step = 0; step < 16; ++step)
            for (int row = 0; row < SimpleArpAudioProcessor::numPatternRows; ++row)
                if (proc.getPatternCell (row, step) != (row == expectedRows[step]))
                    gridOk = false;

        check (gridOk, "exactly one cell per column, on the degree that was typed");
        check (proc.getStepVelocity (1) == 42, "velocities survive a sequence being typed");
    }

    {
        // A sequence that does not parse must not half-write the grid.
        SimpleArpAudioProcessor proc;
        setParam (proc.apvts, "steps", 4);
        proc.clearPattern();
        proc.setPatternCell (0, 0, true);

        const auto parsed = proc.applySequence ("2-2-nope-2");

        check (! parsed.ok, "a bad sequence is rejected");
        check (proc.getPatternCell (0, 0) && ! proc.getPatternCell (1, 0),
               "and leaves the grid exactly as it was");
        check (paramValue (proc.apvts, "steps") == 4,
               "and leaves Steps alone");
    }

    {
        // Degree 4 on a triad is the root an octave up, which Octaves 1 would mute.
        SimpleArpAudioProcessor proc;
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
        setParam (proc.apvts, "octaves", 1);

        proc.applySequence ("1-2-3-4-5-6");

        check (paramValue (proc.apvts, "octaves") == 2,
               "Octaves is raised to put the highest degree in reach");

        setParam (proc.apvts, "octaves", 4);
        proc.applySequence ("1-2-3");

        check (paramValue (proc.apvts, "octaves") == 4,
               "but never lowered behind your back");
    }

    {
        // In the scale modes Octaves transposes rather than gating reach, so moving it
        // would shift the whole pattern.
        SimpleArpAudioProcessor proc;
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChromatic);
        setParam (proc.apvts, "octaves", 1);

        proc.applySequence ("1-6-12");

        check (paramValue (proc.apvts, "octaves") == 1,
               "Octaves is left alone in the scale row modes");
    }
}

void testSequenceRoundTrip()
{
    std::cout << "Sequence round trip" << std::endl;

    SimpleArpAudioProcessor proc;
    setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
    setParam (proc.apvts, "octaves", 2);

    const juce::String typed { "3-1-2|3-1-2|3-2-3|1-2-3|1-3-2-1" };
    const auto parsed = proc.applySequence (typed);

    check (proc.sequenceToString (parsed.groups) == typed,
           "the grid renders back to what was typed");
    check (proc.sequenceToString ({ }) == "3-1-2-3-1-2-3-2-3-1-2-3-1-3-2-1",
           "and to a flat sequence without the grouping");
    check (proc.sequenceToString ({ 2, 2 }) == "3-1-2-3-1-2-3-2-3-1-2-3-1-3-2-1",
           "a stale grouping is ignored rather than truncating the sequence");

    proc.applySequence ("1+3-.-2-.");

    check (proc.sequenceToString ({ }) == "1+3-.-2-.", "rests and stacks round trip");

    // What a grid click does: the text has to follow the grid, not the other way round.
    proc.setPatternCell (1, 1, true);

    check (proc.sequenceToString ({ }) == "1+3-2-2-.", "a grid edit shows up in the text");
}

void testSequencePlaysBack()
{
    std::cout << "Sequence playback" << std::endl;

    {
        SimpleArpAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
        proc.prepareToPlay (testSampleRate, blockSize);

        TestPlayHead playHead;
        proc.setPlayHead (&playHead);

        setParam (proc.apvts, "rate", 6);            // 1/16
        setParam (proc.apvts, "gate", 50.0f);
        setParam (proc.apvts, "latch", 0.0f);
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
        setParam (proc.apvts, "key", 0);
        setParam (proc.apvts, "octaves", 1);

        proc.applySequence ("3-1-2");

        // Three steps against a 4/4 bar: the figure has to keep phasing rather than
        // restarting on the downbeat, which is the whole point of the 3-over-4 feel.
        const auto events = run (proc, playHead, { 60, 64, 67 }, 120);
        const auto notes = noteOnOrder (events, 9);

        check (notesMatch (notes, { 67, 60, 64, 67, 60, 64, 67, 60, 64 }),
               "3-1-2 on C major plays G, C, E and keeps going -> " + describe (notes));
    }

    {
        SimpleArpAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
        proc.prepareToPlay (testSampleRate, blockSize);

        TestPlayHead playHead;
        proc.setPlayHead (&playHead);

        setParam (proc.apvts, "rate", 6);
        setParam (proc.apvts, "gate", 50.0f);
        setParam (proc.apvts, "latch", 0.0f);
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
        setParam (proc.apvts, "key", 0);
        setParam (proc.apvts, "octaves", 1);

        // Degree 4 is the root an octave up, which applySequence has to open reach for.
        proc.applySequence ("1-4-2-.-3");

        const auto events = run (proc, playHead, { 60, 64, 67 }, 140);
        const auto notes = noteOnOrder (events, 8);

        check (notesMatch (notes, { 60, 72, 64, 67, 60, 72, 64, 67 }),
               "degree 4 sounds an octave up and the rest stays silent -> " + describe (notes));
    }
}

void testSequenceSurvivesAPreset()
{
    std::cout << "Sequence survives a preset" << std::endl;

    juce::MemoryBlock saved;

    {
        SimpleArpAudioProcessor proc;
        setParam (proc.apvts, "rows", SimpleArpAudioProcessor::rowsChordTones);
        setParam (proc.apvts, "octaves", 1);

        const auto parsed = proc.applySequence ("3-1-2|3-1-2|3-2-3|1-2-3|1-3-2-1");

        // What the editor writes alongside the grid so the '|' survive a recall.
        juce::StringArray groups;

        for (int size : parsed.groups)
            groups.add (juce::String (size));

        proc.apvts.state.setProperty ("sequenceGroups", groups.joinIntoString (","), nullptr);
        proc.setStepVelocity (3, 77);
        proc.getStateInformation (saved);
    }

    SimpleArpAudioProcessor proc;
    proc.setStateInformation (saved.getData(), (int) saved.getSize());

    check (paramValue (proc.apvts, "steps") == 16, "Steps comes back as 16");

    std::vector<int> groups;
    juce::StringArray parts;
    parts.addTokens (proc.apvts.state.getProperty ("sequenceGroups").toString(), ",", "");

    for (const auto& part : parts)
        groups.push_back (part.getIntValue());

    check (groups == std::vector<int> { 3, 3, 3, 3, 4 }, "the grouping comes back");
    check (proc.sequenceToString (groups) == "3-1-2|3-1-2|3-2-3|1-2-3|1-3-2-1",
           "and the field would redraw exactly as it was typed");
    check (proc.getStepVelocity (3) == 77, "velocities still come back too");
}

/** Holds `first`, then swaps to `second` at an exact sample position, and returns every
    note-on the arp produced. The two chords deliberately share their lowest note.
*/
std::vector<int> runWithChordChange (SimpleArpAudioProcessor& proc, TestPlayHead& playHead,
                                     const std::vector<int>& first,
                                     const std::vector<int>& second,
                                     long long swapAtSample, int numBlocks)
{
    std::vector<int> notes;
    juce::AudioBuffer<float> buffer (2, blockSize);

    const double ppqPerBlock = (double) blockSize / testSampleRate * playHead.bpm / 60.0;

    for (int block = 0; block < numBlocks; ++block)
    {
        juce::MidiBuffer midi;
        const long long blockStart = (long long) block * blockSize;

        if (block == 0)
            for (int note : first)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);

        if (swapAtSample >= blockStart && swapAtSample < blockStart + blockSize)
        {
            const int offset = (int) (swapAtSample - blockStart);

            // Lift only what changes, exactly as a player moving the upper voices would.
            for (int note : first)
                if (std::find (second.begin(), second.end(), note) == second.end())
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), offset);

            for (int note : second)
                if (std::find (first.begin(), first.end(), note) == first.end())
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), offset);
        }

        buffer.clear();
        proc.processBlock (buffer, midi);

        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();

            if (message.isNoteOn())
                notes.push_back (message.getNoteNumber());
        }

        playHead.ppq += ppqPerBlock;
    }

    return notes;
}

void testChordChangeMidBar()
{
    std::cout << "Chord changes under a sequence" << std::endl;

    // C major to F/C: the upper voices move but the lowest note is still C, which is
    // the case that separates "follows the chord" from "follows the root".
    const std::vector<int> cMajor { 60, 64, 67 };
    const std::vector<int> fOverC { 60, 65, 69 };

    // 1/16 at 120bpm is 6000 samples at 48k, so step 6 begins at sample 36000.
    constexpr long long stepSixSample = 36000;

    auto prepare = [] (SimpleArpAudioProcessor& proc, TestPlayHead& playHead, int rowMode)
    {
        proc.setRateAndBufferSizeDetails (testSampleRate, blockSize);
        proc.prepareToPlay (testSampleRate, blockSize);
        proc.setPlayHead (&playHead);

        setParam (proc.apvts, "rate", 6);            // 1/16
        setParam (proc.apvts, "gate", 50.0f);
        setParam (proc.apvts, "latch", 0.0f);
        setParam (proc.apvts, "key", 0);             // From chord
        setParam (proc.apvts, "octaves", 1);
        setParam (proc.apvts, "rows", (float) rowMode);

        proc.applySequence ("3-1-2");
    };

    {
        SimpleArpAudioProcessor proc;
        TestPlayHead playHead;
        prepare (proc, playHead, SimpleArpAudioProcessor::rowsChordTones);

        const auto notes = runWithChordChange (proc, playHead, cMajor, fOverC,
                                               stepSixSample - 1000, 160);

        const std::vector<int> before (notes.begin(), notes.begin() + 6);
        const std::vector<int> after (notes.begin() + 6, notes.begin() + 12);

        check (notesMatch (before, { 67, 60, 64, 67, 60, 64 }),
               "Chord tones: C major arps G, C, E -> " + describe (before));
        check (notesMatch (after, { 69, 60, 65, 69, 60, 65 }),
               "Chord tones: a swap between steps is followed by the next step -> "
                   + describe (after));
    }

    {
        // The case that actually happens: the chord changes on the beat, so the new
        // notes land on the very sample the step fires on.
        SimpleArpAudioProcessor proc;
        TestPlayHead playHead;
        prepare (proc, playHead, SimpleArpAudioProcessor::rowsChordTones);

        const auto notes = runWithChordChange (proc, playHead, cMajor, fOverC,
                                               stepSixSample, 160);

        const std::vector<int> after (notes.begin() + 6, notes.begin() + 12);

        check (notesMatch (after, { 69, 60, 65, 69, 60, 65 }),
               "Chord tones: a swap landing exactly on a step is heard on that step -> "
                   + describe (after));
    }

    {
        // And the way a host actually delivers it: the re-press can land a sample or
        // two after the downbeat, the same skew the cycle-start grace exists for.
        SimpleArpAudioProcessor proc;
        TestPlayHead playHead;
        prepare (proc, playHead, SimpleArpAudioProcessor::rowsChordTones);

        const auto notes = runWithChordChange (proc, playHead, cMajor, fOverC,
                                               stepSixSample + 2, 160);

        const std::vector<int> after (notes.begin() + 6, notes.begin() + 12);

        check (notesMatch (after, { 69, 60, 65, 69, 60, 65 }),
               "Chord tones: a swap arriving a sample or two late is still heard on that step -> "
                   + describe (after));
    }

    {
        SimpleArpAudioProcessor proc;
        TestPlayHead playHead;
        prepare (proc, playHead, SimpleArpAudioProcessor::rowsChromatic);

        const auto notes = runWithChordChange (proc, playHead, cMajor, fOverC,
                                               stepSixSample - 1000, 160);

        const std::vector<int> before (notes.begin(), notes.begin() + 6);
        const std::vector<int> after (notes.begin() + 6, notes.begin() + 12);

        // Not a bug, but the thing that reads as one: a scale row mode only ever looks
        // at the root, so voices moving above a held bass change nothing at all.
        check (notesMatch (before, after),
               "Chromatic: the same swap changes nothing, because only the root is read -> "
                   + describe (before) + " then " + describe (after));
    }
}

} // namespace

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    testTimingAndOrder();
    testFillShapes();
    testDownbeatFiresOncePerBar();
    testResetReleasesSoundingNotes();
    testLoopRetriggerAtCycleStart();
    testColumnLandsOnItsOwnStep();
    testLoopedBar();
    testPpqJitter();
    testNoSkippedSteps();
    testGateLengths();
    testFreeRunStart();
    testKeyReleaseAndTransportStop();
    testLatch();
    testPatternGrid();
    testSequenceParsing();
    testSequenceAppliesToGrid();
    testSequenceRoundTrip();
    testSequencePlaysBack();
    testSequenceSurvivesAPreset();
    testChordChangeMidBar();

    std::cout << std::endl
              << (failures == 0 ? "ALL TESTS PASSED"
                                : juce::String (failures) + " TEST(S) FAILED").toStdString()
              << std::endl;

    return failures == 0 ? 0 : 1;
}
