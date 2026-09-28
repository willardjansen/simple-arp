#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

//==============================================================================
/** The 12-row pattern grid. Rows run bottom-to-top: the bottom row is the lowest
    held note, and rows past the top of the chord wrap up an octave.
*/
class PatternGrid final : public juce::Component
{
public:
    explicit PatternGrid (SimpleArpAudioProcessor&);

    void setNumSteps (int);
    void setPlayhead (int step);
    void setActiveRows (int);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cellBounds (int row, int step) const;
    bool cellAt (juce::Point<float>, int& row, int& step) const;
    void paintCell (const juce::MouseEvent&, bool isFirstClick);

    SimpleArpAudioProcessor& processorRef;
    int numSteps = 16;
    int playhead = -1;
    bool paintValue = true;
    int activeRows = SimpleArpAudioProcessor::numPatternRows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternGrid)
};

//==============================================================================
/** The note-name gutter beside the grid, showing what each row currently plays. */
class RowLabels final : public juce::Component
{
public:
    explicit RowLabels (SimpleArpAudioProcessor&);

    void setActiveRows (int);
    void refresh();

    void paint (juce::Graphics&) override;

private:
    SimpleArpAudioProcessor& processorRef;
    int activeRows = SimpleArpAudioProcessor::numPatternRows;
    std::array<int, SimpleArpAudioProcessor::numPatternRows> shown { };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RowLabels)
};

//==============================================================================
/** One bar per step, dragged vertically to set that step's velocity. */
class VelocityLane final : public juce::Component
{
public:
    explicit VelocityLane (SimpleArpAudioProcessor&);

    void setNumSteps (int);
    void setPlayhead (int step);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void applyDrag (const juce::MouseEvent&);

    SimpleArpAudioProcessor& processorRef;
    int numSteps = 16;
    int playhead = -1;
    int editingStep = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelocityLane)
};

//==============================================================================
class SimpleArpAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit SimpleArpAudioProcessorEditor (SimpleArpAudioProcessor&);
    ~SimpleArpAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void configureRotary (juce::Slider&, juce::Label&, const juce::String& text);
    void configureCombo (juce::ComboBox&, juce::Label&, const juce::String& text,
                         const juce::StringArray& items);
    void configureToggle (juce::ToggleButton&);
    juce::String describePatternLength() const;
    void showFillMenu();

    /** Reads the sequence field and writes it into the grid, or reports why it would not
        parse and leaves both the grid and the typed text alone.
    */
    void applySequenceText();

    /** Re-renders the field from the grid, so Fill, Clear, a grid click or a recalled
        preset all show up as text. Skipped while the field has focus, so it never
        rewrites what is being typed.
    */
    void refreshSequenceText();

    void setSequenceStatus (const juce::String& text, bool isError);

    /** The '|' positions from the last sequence that parsed, kept so grid edits do not
        flatten the grouping. Stored with the preset as a `sequenceGroups` property.
    */
    std::vector<int> readStoredGroups() const;
    void storeGroups (const std::vector<int>&);

    SimpleArpAudioProcessor& processorRef;

    juce::ComboBox rateBox, rowModeBox, keyBox;
    juce::Slider octavesSlider, gateSlider, stepsSlider;
    juce::ToggleButton latchButton { "Latch" };
    juce::TextButton fillButton { "Fill" };
    juce::TextButton clearButton { "Clear" };

    juce::Label rateLabel, rowModeLabel, octavesLabel, gateLabel, stepsLabel,
                lengthLabel, velocityLabel, keyLabel;

    juce::TextEditor sequenceEditor;
    juce::Label sequenceLabel, sequenceStatus;
    juce::String shownSequence;

    RowLabels rowLabels;
    PatternGrid grid;
    VelocityLane velocities;

    std::unique_ptr<ComboBoxAttachment> rateAttachment, rowModeAttachment, keyAttachment;
    std::unique_ptr<SliderAttachment> octavesAttachment, gateAttachment, stepsAttachment;
    std::unique_ptr<ButtonAttachment> latchAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleArpAudioProcessorEditor)
};
