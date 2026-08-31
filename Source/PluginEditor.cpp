#include "PluginEditor.h"

namespace
{
    const juce::Colour backgroundTop   { 0xff23262b };
    const juce::Colour backgroundBottom{ 0xff15171a };
    const juce::Colour accent          { 0xff5fc9b0 };
    const juce::Colour textColour      { 0xffd6dae0 };
    const juce::Colour panelColour     { 0xff2c3036 };
    const juce::Colour outlineColour   { 0xff3a3f46 };

}

//==============================================================================
PatternGrid::PatternGrid (SimpleArpAudioProcessor& p)
    : processorRef (p)
{
}

void PatternGrid::setNumSteps (int steps)
{
    steps = juce::jlimit (1, SimpleArpAudioProcessor::maxPatternSteps, steps);

    if (steps != numSteps)
    {
        numSteps = steps;
        repaint();
    }
}

void PatternGrid::setPlayhead (int step)
{
    if (step != playhead)
    {
        playhead = step;
        repaint();
    }
}

juce::Rectangle<float> PatternGrid::cellBounds (int row, int step) const
{
    const auto area = getLocalBounds().toFloat();
    const float cellWidth = area.getWidth() / (float) numSteps;
    const float cellHeight = area.getHeight() / (float) SimpleArpAudioProcessor::numPatternRows;

    // Row 0 is the lowest chord tone, drawn at the bottom.
    const int drawRow = SimpleArpAudioProcessor::numPatternRows - 1 - row;

    return { (float) step * cellWidth, (float) drawRow * cellHeight, cellWidth, cellHeight };
}

bool PatternGrid::cellAt (juce::Point<float> position, int& row, int& step) const
{
    const auto area = getLocalBounds().toFloat();

    if (! area.contains (position))
        return false;

    const float cellWidth = area.getWidth() / (float) numSteps;
    const float cellHeight = area.getHeight() / (float) SimpleArpAudioProcessor::numPatternRows;

    step = juce::jlimit (0, numSteps - 1, (int) (position.x / cellWidth));

    const int drawRow = juce::jlimit (0, SimpleArpAudioProcessor::numPatternRows - 1,
                                      (int) (position.y / cellHeight));
    row = SimpleArpAudioProcessor::numPatternRows - 1 - drawRow;

    return true;
}

void PatternGrid::setActiveRows (int rows)
{
    rows = juce::jlimit (1, SimpleArpAudioProcessor::numPatternRows, rows);

    if (rows != activeRows)
    {
        activeRows = rows;
        repaint();
    }
}

void PatternGrid::paint (juce::Graphics& g)
{
    g.setColour (panelColour);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);

    for (int step = 0; step < numSteps; ++step)
    {
        const bool onBeat = (step % 4) == 0;

        for (int row = 0; row < SimpleArpAudioProcessor::numPatternRows; ++row)
        {
            const auto cell = cellBounds (row, step).reduced (1.0f);
            const bool isOn = processorRef.getPatternCell (row, step);

            // Rows beyond the chord x Octaves reach are drawn back: still editable,
            // but they will not sound until you widen Octaves or hold more notes.
            const bool inReach = row < activeRows;

            juce::Colour fill = onBeat ? outlineColour.brighter (0.08f) : outlineColour.darker (0.35f);

            if (isOn)
                fill = inReach ? accent : accent.withSaturation (0.12f).withAlpha (0.4f);

            if (step == playhead && inReach)
                fill = isOn ? accent.brighter (0.45f) : fill.brighter (0.25f);

            if (! inReach && ! isOn)
                fill = fill.withAlpha (0.45f);

            g.setColour (fill);
            g.fillRoundedRectangle (cell, 2.0f);
        }
    }

    // Bar/beat divisions on top of the cells.
    g.setColour (backgroundBottom.withAlpha (0.9f));

    for (int step = 4; step < numSteps; step += 4)
    {
        const float x = getLocalBounds().toFloat().getWidth() / (float) numSteps * (float) step;
        g.fillRect (x - 0.5f, 0.0f, 1.0f, (float) getHeight());
    }
}

void PatternGrid::mouseDown (const juce::MouseEvent& event)
{
    paintCell (event, true);
}

void PatternGrid::mouseDrag (const juce::MouseEvent& event)
{
    paintCell (event, false);
}

void PatternGrid::paintCell (const juce::MouseEvent& event, bool isFirstClick)
{
    int row = 0, step = 0;

    if (! cellAt (event.position, row, step))
        return;

    // The first click decides whether this drag is drawing or erasing.
    if (isFirstClick)
        paintValue = ! processorRef.getPatternCell (row, step);

    if (processorRef.getPatternCell (row, step) != paintValue)
    {
        processorRef.setPatternCell (row, step, paintValue);
        repaint();
    }
}

//==============================================================================
VelocityLane::VelocityLane (SimpleArpAudioProcessor& p)
    : processorRef (p)
{
}

void VelocityLane::setNumSteps (int steps)
{
    steps = juce::jlimit (1, SimpleArpAudioProcessor::maxPatternSteps, steps);

    if (steps != numSteps)
    {
        numSteps = steps;
        repaint();
    }
}

void VelocityLane::setPlayhead (int step)
{
    if (step != playhead)
    {
        playhead = step;
        repaint();
    }
}

void VelocityLane::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    g.setColour (panelColour);
    g.fillRoundedRectangle (area, 3.0f);

    const float barWidth = area.getWidth() / (float) numSteps;

    for (int step = 0; step < numSteps; ++step)
    {
        const float velocity = (float) processorRef.getStepVelocity (step);
        const float height = area.getHeight() * (velocity / 127.0f);

        auto bar = juce::Rectangle<float> ((float) step * barWidth,
                                           area.getBottom() - height,
                                           barWidth, height).reduced (1.0f, 0.0f);

        // Steps with nothing switched on in the grid are dimmed — the value is still
        // there, it just isn't doing anything yet.
        const bool used = processorRef.isStepActive (step);
        juce::Colour fill = used ? accent : accent.withSaturation (0.15f).withAlpha (0.35f);

        if (step == playhead)
            fill = fill.brighter (0.45f);

        g.setColour (fill);
        g.fillRoundedRectangle (bar, 1.5f);
    }

    g.setColour (backgroundBottom.withAlpha (0.9f));

    for (int step = 4; step < numSteps; step += 4)
        g.fillRect (barWidth * (float) step - 0.5f, 0.0f, 1.0f, area.getHeight());

    if (editingStep >= 0)
    {
        g.setColour (textColour);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (juce::String (processorRef.getStepVelocity (editingStep)),
                    getLocalBounds().removeFromTop (16), juce::Justification::centred, false);
    }
}

void VelocityLane::mouseDown (const juce::MouseEvent& event)
{
    applyDrag (event);
}

void VelocityLane::mouseDrag (const juce::MouseEvent& event)
{
    applyDrag (event);
}

void VelocityLane::mouseUp (const juce::MouseEvent&)
{
    editingStep = -1;
    repaint();
}

void VelocityLane::applyDrag (const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat();

    if (area.getWidth() <= 0.0f || area.getHeight() <= 0.0f)
        return;

    const float barWidth = area.getWidth() / (float) numSteps;
    const int step = juce::jlimit (0, numSteps - 1, (int) (event.position.x / barWidth));

    const float fraction = 1.0f - juce::jlimit (0.0f, 1.0f, event.position.y / area.getHeight());
    const int velocity = juce::jlimit (1, 127, juce::roundToInt (fraction * 127.0f));

    processorRef.setStepVelocity (step, velocity);
    editingStep = step;
    repaint();
}

//==============================================================================
SimpleArpAudioProcessorEditor::SimpleArpAudioProcessorEditor (SimpleArpAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processorRef (p), rowLabels (p), grid (p), velocities (p)
{
    configureCombo (rateBox, rateLabel, "Rate", SimpleArpAudioProcessor::getRateNames());
    configureCombo (rowModeBox, rowModeLabel, "Rows", SimpleArpAudioProcessor::getRowModeNames());
    configureCombo (keyBox, keyLabel, "Key", SimpleArpAudioProcessor::getKeyNames());

    configureRotary (octavesSlider, octavesLabel, "Octaves");
    octavesSlider.setRange (1.0, 4.0, 1.0);

    configureRotary (gateSlider, gateLabel, "Gate");
    gateSlider.setTextValueSuffix (" %");

    configureToggle (latchButton);

    stepsSlider.setSliderStyle (juce::Slider::IncDecButtons);
    stepsSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 50, 20);
    stepsSlider.setRange (1.0, (double) SimpleArpAudioProcessor::maxPatternSteps, 1.0);
    stepsSlider.setColour (juce::Slider::textBoxTextColourId, textColour);
    stepsSlider.setColour (juce::Slider::textBoxOutlineColourId, outlineColour);
    addAndMakeVisible (stepsSlider);

    stepsLabel.setText ("Steps", juce::dontSendNotification);
    stepsLabel.setJustificationType (juce::Justification::centredLeft);
    stepsLabel.setColour (juce::Label::textColourId, textColour.withAlpha (0.7f));
    addAndMakeVisible (stepsLabel);

    lengthLabel.setJustificationType (juce::Justification::centredRight);
    lengthLabel.setColour (juce::Label::textColourId, textColour.withAlpha (0.5f));
    addAndMakeVisible (lengthLabel);

    fillButton.setColour (juce::TextButton::buttonColourId, panelColour);
    fillButton.setColour (juce::TextButton::textColourOffId, textColour);
    fillButton.onClick = [this] { showFillMenu(); };
    addAndMakeVisible (fillButton);

    clearButton.setColour (juce::TextButton::buttonColourId, panelColour);
    clearButton.setColour (juce::TextButton::textColourOffId, textColour);
    clearButton.onClick = [this]
    {
        processorRef.clearPattern();
        grid.repaint();
        velocities.repaint();
    };
    addAndMakeVisible (clearButton);

    velocityLabel.setText ("Velocity", juce::dontSendNotification);
    velocityLabel.setJustificationType (juce::Justification::centredLeft);
    velocityLabel.setColour (juce::Label::textColourId, textColour.withAlpha (0.7f));
    addAndMakeVisible (velocityLabel);

    addAndMakeVisible (rowLabels);
    addAndMakeVisible (grid);
    addAndMakeVisible (velocities);

    auto& state = processorRef.apvts;
    rateAttachment    = std::make_unique<ComboBoxAttachment> (state, "rate", rateBox);
    rowModeAttachment = std::make_unique<ComboBoxAttachment> (state, "rows", rowModeBox);
    keyAttachment     = std::make_unique<ComboBoxAttachment> (state, "key", keyBox);
    octavesAttachment = std::make_unique<SliderAttachment> (state, "octaves", octavesSlider);
    gateAttachment    = std::make_unique<SliderAttachment> (state, "gate", gateSlider);
    stepsAttachment   = std::make_unique<SliderAttachment> (state, "steps", stepsSlider);
    latchAttachment   = std::make_unique<ButtonAttachment> (state, "latch", latchButton);

    setSize (720, 560);
    startTimerHz (20);
}

//==============================================================================
void SimpleArpAudioProcessorEditor::configureRotary (juce::Slider& slider, juce::Label& label,
                                                     const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff33373d));
    slider.setColour (juce::Slider::thumbColourId, textColour);
    slider.setColour (juce::Slider::textBoxTextColourId, textColour);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, textColour.withAlpha (0.7f));
    addAndMakeVisible (label);
}

void SimpleArpAudioProcessorEditor::configureCombo (juce::ComboBox& box, juce::Label& label,
                                                    const juce::String& text,
                                                    const juce::StringArray& items)
{
    box.addItemList (items, 1);
    box.setColour (juce::ComboBox::backgroundColourId, panelColour);
    box.setColour (juce::ComboBox::textColourId, textColour);
    box.setColour (juce::ComboBox::outlineColourId, outlineColour);
    box.setColour (juce::ComboBox::arrowColourId, accent);
    addAndMakeVisible (box);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centredLeft);
    label.setColour (juce::Label::textColourId, textColour.withAlpha (0.7f));
    addAndMakeVisible (label);
}

void SimpleArpAudioProcessorEditor::configureToggle (juce::ToggleButton& button)
{
    button.setColour (juce::ToggleButton::textColourId, textColour);
    button.setColour (juce::ToggleButton::tickColourId, accent);
    button.setColour (juce::ToggleButton::tickDisabledColourId, textColour.withAlpha (0.4f));
    addAndMakeVisible (button);
}

//==============================================================================
juce::String SimpleArpAudioProcessorEditor::describePatternLength() const
{
    const int steps = (int) stepsSlider.getValue();
    const int rate = rateBox.getSelectedItemIndex();

    if (rate < 0)
        return {};

    // Length of one step in quarter notes, mirroring the processor's table.
    static const double lengths[] { 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0,
                                    0.375, 0.25, 1.0 / 6.0, 0.125 };

    const double quarters = (double) steps * lengths[juce::jlimit (0, 8, rate)];
    const double bars = quarters / 4.0;

    juce::String barText = juce::String (bars, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");

    return juce::String (steps) + " x " + rateBox.getText() + "  =  " + barText
             + (bars == 1.0 ? " bar" : " bars");
}

void SimpleArpAudioProcessorEditor::timerCallback()
{
    grid.setActiveRows (processorRef.getActivePatternRows());
    rowLabels.setActiveRows (processorRef.getActivePatternRows());
    rowLabels.refresh();

    grid.setNumSteps ((int) stepsSlider.getValue());
    grid.setPlayhead (processorRef.getCurrentPatternStep());

    velocities.setNumSteps ((int) stepsSlider.getValue());
    velocities.setPlayhead (processorRef.getCurrentPatternStep());

    const auto text = describePatternLength();

    if (lengthLabel.getText() != text)
        lengthLabel.setText (text, juce::dontSendNotification);
}

//==============================================================================
void SimpleArpAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.setGradientFill ({ backgroundTop, 0.0f, 0.0f,
                         backgroundBottom, 0.0f, (float) getHeight(), false });
    g.fillAll();

    g.setColour (accent);
    g.setFont (juce::FontOptions (20.0f).withStyle ("Bold"));
    g.drawText ("SIMPLE ARP", 20, 14, getWidth() - 40, 24,
                juce::Justification::centredLeft, false);

    g.setColour (outlineColour);
    g.fillRect (20, 44, getWidth() - 40, 1);
}

void SimpleArpAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (40);

    auto comboRow = area.removeFromTop (52);

    // Four equal columns with a fixed gutter, the rounding spread across them so the
    // first box starts on the left margin and the last ends exactly on the right one.
    struct ComboColumn { juce::Label& label; juce::ComboBox& box; };

    const ComboColumn columns[]
    {
        { rateLabel,    rateBox    },
        { rowModeLabel, rowModeBox },
        { keyLabel,     keyBox     }
    };

    constexpr int numColumns = (int) (sizeof (columns) / sizeof (columns[0]));
    constexpr int gutter = 10;

    const int usable = comboRow.getWidth() - gutter * (numColumns - 1);

    for (int i = 0; i < numColumns; ++i)
    {
        const int start = (usable * i) / numColumns;
        const int end   = (usable * (i + 1)) / numColumns;

        auto cell = juce::Rectangle<int> (comboRow.getX() + start + gutter * i,
                                          comboRow.getY(), end - start, comboRow.getHeight());

        columns[i].label.setBounds (cell.removeFromTop (18));
        columns[i].box.setBounds (cell.removeFromTop (28));
    }

    area.removeFromTop (8);

    auto knobRow = area.removeFromTop (104);
    const int quarter = knobRow.getWidth() / 4;

    auto octavesArea = knobRow.removeFromLeft (quarter);
    octavesLabel.setBounds (octavesArea.removeFromTop (18));
    octavesSlider.setBounds (octavesArea.reduced (6, 0));

    auto gateArea = knobRow.removeFromLeft (quarter);
    gateLabel.setBounds (gateArea.removeFromTop (18));
    gateSlider.setBounds (gateArea.reduced (6, 0));

    auto togglesArea = knobRow.reduced (10, 0);
    latchButton.setBounds (togglesArea.removeFromTop (34).withTrimmedTop (6).withWidth (110));

    area.removeFromTop (10);

    auto stepsRow = area.removeFromTop (26);
    stepsLabel.setBounds (stepsRow.removeFromLeft (44));
    stepsSlider.setBounds (stepsRow.removeFromLeft (110));
    fillButton.setBounds (stepsRow.removeFromLeft (62).reduced (4, 1));
    clearButton.setBounds (stepsRow.removeFromLeft (62).reduced (4, 1));
    lengthLabel.setBounds (stepsRow);

    area.removeFromTop (8);

    auto laneArea = area.removeFromBottom (86);
    velocityLabel.setBounds (laneArea.removeFromTop (18).withTrimmedLeft (44));
    velocities.setBounds (laneArea.withTrimmedLeft (44));

    auto gridArea = area.withTrimmedBottom (8);
    rowLabels.setBounds (gridArea.removeFromLeft (40));
    grid.setBounds (gridArea.withTrimmedLeft (4));
}

//==============================================================================
RowLabels::RowLabels (SimpleArpAudioProcessor& p)
    : processorRef (p)
{
    shown.fill (-2);   // force the first refresh to repaint
}

void RowLabels::setActiveRows (int rows)
{
    rows = juce::jlimit (1, SimpleArpAudioProcessor::numPatternRows, rows);

    if (rows != activeRows)
    {
        activeRows = rows;
        repaint();
    }
}

void RowLabels::refresh()
{
    bool changed = false;

    for (int row = 0; row < SimpleArpAudioProcessor::numPatternRows; ++row)
    {
        const int pitch = processorRef.getRowPitch (row);

        if (pitch != shown[(size_t) row])
        {
            shown[(size_t) row] = pitch;
            changed = true;
        }
    }

    if (changed)
        repaint();
}

void RowLabels::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float rowHeight = area.getHeight() / (float) SimpleArpAudioProcessor::numPatternRows;

    g.setFont (juce::FontOptions (11.0f));

    for (int row = 0; row < SimpleArpAudioProcessor::numPatternRows; ++row)
    {
        const int drawRow = SimpleArpAudioProcessor::numPatternRows - 1 - row;
        const auto bounds = juce::Rectangle<float> (0.0f, (float) drawRow * rowHeight,
                                                    area.getWidth(), rowHeight);

        const int pitch = shown[(size_t) row];
        const bool inReach = row < activeRows;

        // Nothing held yet, so there is no pitch to name.
        const juce::String text = pitch < 0
                                    ? juce::String ("-")
                                    : juce::MidiMessage::getMidiNoteName (pitch, true, true, 3);

        g.setColour (textColour.withAlpha (pitch < 0 ? 0.25f : (inReach ? 0.85f : 0.28f)));
        g.drawText (text, bounds.reduced (4.0f, 0.0f), juce::Justification::centredRight, false);
    }
}

//==============================================================================
void SimpleArpAudioProcessorEditor::showFillMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Up");
    menu.addItem (2, "Down");
    menu.addItem (3, "Up-Down");
    menu.addItem (4, "Random");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fillButton),
                        [this] (int choice)
                        {
                            if (choice <= 0)
                                return;

                            processorRef.fillPattern (choice - 1);
                            grid.repaint();
                            velocities.repaint();
                        });
}
