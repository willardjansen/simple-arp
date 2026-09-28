#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <limits>
#include <vector>

//==============================================================================
/** A tempo-synced MIDI arpeggiator.

    Incoming notes are swallowed and replaced by a stepped pattern built from
    whatever is currently held down. Everything that isn't a note message
    (CC, pitch bend, aftertouch, ...) is passed straight through.

    The grid is the only sequencer: 12 rows you draw, where a row is either a chord
    tone or a degree of the selected scale. Fill writes the classic Up / Down /
    Up-Down / Random shapes into it as a starting point.
*/
class SimpleArpAudioProcessor final : public juce::AudioProcessor
{
public:
    //==============================================================================
    enum FillShape { fillUp = 0, fillDown, fillUpDown, fillRandom };
    // Row 0 follows the held chord; everything after it is a scale, index 1 being
    // Chromatic (the old Semitones behaviour).
    enum RowMode { rowsChordTones = 0, rowsChromatic = 1 };

    static constexpr int numPatternRows = 12;
    static constexpr int maxPatternSteps = 32;

    static juce::StringArray getRateNames();
    static juce::StringArray getRowModeNames();
    static juce::StringArray getKeyNames();

    //==============================================================================
    SimpleArpAudioProcessor();
    ~SimpleArpAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool isBusesLayoutSupported (const BusesLayout&) const override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                            { return true; }

    const juce::String getName() const override                { return JucePlugin_Name; }

    bool acceptsMidi() const override                          { return true; }
    bool producesMidi() const override                         { return true; }

    // VST3 has no MIDI-effect plug-in category, so this ships as an Instrument that
    // emits MIDI and outputs silence — the only shape hosts reliably route notes into.
    bool isMidiEffect() const override                         { return false; }
    double getTailLengthSeconds() const override               { return 0.0; }

    int getNumPrograms() override                              { return 1; }
    int getCurrentProgram() override                           { return 0; }
    void setCurrentProgram (int) override                      {}
    const juce::String getProgramName (int) override           { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    /** The pattern grid. Row 0 is the lowest chord tone; rows at or above the number
        of held notes wrap up an octave. Safe to call from either thread.
    */
    bool getPatternCell (int row, int step) const noexcept;
    void setPatternCell (int row, int step, bool shouldBeOn) noexcept;
    void clearPattern() noexcept;

    /** Writes a classic arpeggiator shape into the grid, ready to be edited. */
    void fillPattern (int shape);

    //==============================================================================
    /** The outcome of reading a degree sequence such as "3-1-2|3-1-2|1+3-.-2". */
    struct SequenceParse
    {
        bool ok = false;
        juce::String error;             // why it was rejected; empty when ok
        int numSteps = 0;
        int highestDegree = 0;          // 1-based, 0 when every step is a rest
        int octavesRaisedTo = 0;        // non-zero only when applying moved the knob
        std::vector<int> groups;        // step counts between the '|' that were typed
        std::vector<std::vector<int>> steps;   // 0-based rows per step, empty == rest
    };

    /** Reads a degree sequence without touching anything.

        Degrees are 1-based chord tones counting up from the lowest held note, so on a
        triad 1-2-3 is root, third, fifth and 4 is the root an octave up. '.', '_' and
        '0' are rests, '1+3' stacks two degrees on one step, and '|' is cosmetic: it
        groups the sequence for reading and has no effect on playback.
    */
    static SequenceParse parseSequence (const juce::String& text);

    /** Parses `text` and, if the whole string is valid, writes it into the grid and
        sets Steps to its length. Leaves the grid untouched when it does not parse.

        Per-step velocities survive, and in Chord tones the Octaves parameter is raised
        if it would otherwise put the highest degree out of reach.
    */
    SequenceParse applySequence (const juce::String& text);

    /** Renders the grid back out as a degree sequence. `groups` reinstates the '|'
        positions when its sizes add up to the current pattern length.
    */
    juce::String sequenceToString (const std::vector<int>& groups = {}) const;

    /** Per-step velocity, 1..127. In Pattern mode this replaces the played velocity. */
    int getStepVelocity (int step) const noexcept;
    void setStepVelocity (int step, int velocity) noexcept;

    /** True if any row is switched on in this column. */
    bool isStepActive (int step) const noexcept;

    static constexpr int defaultVelocity = 100;

    /** How many grid rows the held chord and the Octaves setting actually reach.
        Rows above this are drawn dimmed and stay silent.
    */
    int getActivePatternRows() const noexcept;

    /** The MIDI note a grid row currently maps to, or -1 when nothing is held. */
    int getRowPitch (int row) const noexcept;

    /** The step the pattern is currently on, for the editor's playhead. -1 when idle. */
    int getCurrentPatternStep() const noexcept  { return displayStep.load(); }

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

private:
    //==============================================================================
    struct HeldNote  { int note = 0; int velocity = 100; };
    struct ActiveNote { int note = 0; int channel = 1; int offOffset = 0; };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static double quarterNotesForRate (int rateIndex);

    void handleIncomingMessage (const juce::MidiMessage&, int sampleOffset,
                                juce::MidiBuffer& out, bool synced);
    void triggerStep (int sampleOffset, juce::MidiBuffer& out, double gateSamples,
                      long long stepNumber);
    void emitNote (const HeldNote&, int sampleOffset, juce::MidiBuffer& out, double gateSamples);
    void stopAllActiveNotes (juce::MidiBuffer& out, int sampleOffset);
    int pitchForRow (int row) const;
    int scaleRootFor (int lowestNote) const;
    void rebuildSequence();
    void clearArpState();
    void clearPatternRows() noexcept;

    juce::String patternToString() const;
    void patternFromString (const juce::String&);
    juce::String velocitiesToString() const;
    void velocitiesFromString (const juce::String&);

    //==============================================================================
    std::atomic<float>* rateParam    = nullptr;
    std::atomic<float>* octavesParam = nullptr;
    std::atomic<float>* gateParam    = nullptr;
    std::atomic<float>* latchParam   = nullptr;
    std::atomic<float>* stepsParam   = nullptr;
    std::atomic<float>* rowModeParam = nullptr;
    std::atomic<float>* keyParam     = nullptr;

    // One bitmask per row; bit N is step N. Lock-free for the editor.
    std::array<std::atomic<juce::uint32>, numPatternRows> patternRows;
    std::array<std::atomic<int>, maxPatternSteps> stepVelocities;
    std::atomic<int> displayStep { -1 };
    std::atomic<int> displayChordSize { 0 };
    std::array<std::atomic<int>, numPatternRows> displayRowPitch;

    // Notes physically held on the keyboard.
    std::vector<int> keysDown;
    // Notes feeding the pattern (== keysDown unless Latch is on), in played order.
    std::vector<HeldNote> patternNotes;
    // The same notes sorted low to high — the chord tones the grid indexes into.
    std::vector<HeldNote> sortedChord;
    // Notes we have sounded and still owe a note-off for.
    std::vector<ActiveNote> activeNotes;
    // Notes orphaned by a reset, to be released at the top of the next block.
    std::vector<ActiveNote> notesToRelease;

    long long freeStepCounter = 0;
    long long lastSyncedStep = std::numeric_limits<long long>::min();

    // A step that found no chord, waiting for notes that are arriving a hair late.
    long long pendingStepNumber = -1;
    int pendingStepAge = 0;
    int retriggerGraceSamples = 0;
    double currentGateSamples = 0.0;
    int lastChannel = 1;
    double nextFreeRunStep = 0.0;
    bool wasPlaying = false;
    bool wasLatched = false;
    int cachedOctaves = -1;
    int cachedRowMode = -1;
    int cachedKey = -1;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleArpAudioProcessor)
};
