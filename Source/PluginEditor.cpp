#include "PluginEditor.h"

using namespace MorphoColors;

//==============================================================================
// PresetSlotButton
//==============================================================================
PresetSlotButton::PresetSlotButton (int idx, MorphoProcessor& p)
    : index (idx), proc (p)
{
    setRepaintsOnMouseActivity (true);
}

void PresetSlotButton::refresh() { repaint(); }

void PresetSlotButton::paint (juce::Graphics& g)
{
    auto& preset = proc.presets[index];
    auto  bounds = getLocalBounds().toFloat().reduced (1.5f);
    bool  active = (proc.activePreset == index);
    bool  hover  = isMouseOver();

    // Background
    juce::Colour bg = surface;
    if (active)  bg = accent.withAlpha (0.18f);
    else if (hover) bg = surfaceHigh;

    g.setColour (bg);
    g.fillRoundedRectangle (bounds, 4.0f);

    // Color stripe on left edge (if saved)
    if (preset.hasData)
    {
        auto stripe = bounds.withWidth (4.0f);
        g.setColour (presetColors[preset.colorIndex % 8]);
        g.fillRoundedRectangle (stripe, 2.0f);
    }

    // Border
    g.setColour (active ? accent : surfaceHigh);
    g.drawRoundedRectangle (bounds, 4.0f, active ? 1.5f : 0.8f);

    // Slot number
    auto numBounds = bounds.removeFromTop (bounds.getHeight() * 0.45f);
    g.setColour (active ? accent : dimText);
    g.setFont (CustomLookAndFeel::getMonoFont (11.0f).boldened());
    g.drawText (juce::String (index + 1), numBounds.toNearestInt(),
                juce::Justification::centred, false);

    // Name
    if (preset.hasData && preset.name.isNotEmpty())
    {
        g.setColour (active ? brightText : dimText.brighter (0.3f));
        g.setFont (CustomLookAndFeel::getMonoFont (9.0f));
        g.drawText (preset.name, bounds.toNearestInt(),
                    juce::Justification::centred, true);
    }

    // Progress bar overlay during morph
    if (active && proc.morphEngine.isMorphing())
    {
        float prog = proc.morphEngine.getProgress();
        auto  bar  = getLocalBounds().toFloat().reduced (1.5f);
        bar = bar.removeFromBottom (4.0f);

        g.setColour (trackBg);
        g.fillRoundedRectangle (bar, 2.0f);

        g.setColour (accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * prog), 2.0f);
    }
}

void PresetSlotButton::mouseDown (const juce::MouseEvent& e)
{
    if (onClick)
        onClick (index, e.mods.isRightButtonDown());
}

//==============================================================================
// ParamSlotComponent
//==============================================================================
ParamSlotComponent::ParamSlotComponent (int idx, MorphoProcessor& p, CustomLookAndFeel& l)
    : index (idx), proc (p)
{
    setLookAndFeel (&l);

    // Name label — double-click to edit
    nameLabel.setEditable (false, true, false);
    nameLabel.setJustificationType (juce::Justification::centredLeft);
    nameLabel.setColour (juce::Label::textColourId, brightText);
    nameLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    nameLabel.setFont (CustomLookAndFeel::getMonoFont (11.0f));
    nameLabel.onTextChange = [this]
    {
        proc.paramSlots[index].name = nameLabel.getText (true).substring (0, 16);
    };
    addAndMakeVisible (nameLabel);

    // CC label — double-click to edit
    ccLabel.setEditable (false, true, false);
    ccLabel.setJustificationType (juce::Justification::centred);
    ccLabel.setColour (juce::Label::textColourId, dimText);
    ccLabel.setFont (CustomLookAndFeel::getMonoFont (10.0f));
    ccLabel.onTextChange = [this]
    {
        int cc = ccLabel.getText().getIntValue();
        proc.paramSlots[index].ccNumber = juce::jlimit (0, 127, cc);
        ccLabel.setText ("CC " + juce::String (proc.paramSlots[index].ccNumber),
                         juce::dontSendNotification);
    };
    addAndMakeVisible (ccLabel);

    // Value knob
    valueKnob.setSliderStyle (juce::Slider::Rotary);
    valueKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    valueKnob.setRange (0, 127, 1);
    valueKnob.setDoubleClickReturnValue (true, 0);
    valueKnob.addListener (this);

    // Right-click popup to set CC number
    valueKnob.onValueChange = nullptr;  // using listener instead
    valueKnob.addMouseListener (this, false);

    addAndMakeVisible (valueKnob);

    // Channel combobox
    channelBox.addItem ("G", 1);
    for (int ch = 1; ch <= 16; ++ch)
        channelBox.addItem (juce::String (ch), ch + 1);
    channelBox.setSelectedId (1, juce::dontSendNotification);
    channelBox.onChange = [this]
    {
        int id = channelBox.getSelectedId();
        proc.paramSlots[index].channel = (id == 1) ? 0 : (id - 1);
    };
    addAndMakeVisible (channelBox);
}

ParamSlotComponent::~ParamSlotComponent()
{
    setLookAndFeel (nullptr);
    valueKnob.removeListener (this);
}

void ParamSlotComponent::refresh()
{
    auto& slot = proc.paramSlots[index];

    nameLabel.setText (slot.name, juce::dontSendNotification);
    ccLabel.setText ("CC " + juce::String (slot.ccNumber), juce::dontSendNotification);

    ignoreSlider = true;

    // During morph, show interpolated value from engine
    int displayVal = slot.value;
    if (proc.morphEngine.isMorphing())
    {
        auto vals = proc.morphEngine.getCurrentValues();
        displayVal = vals[index];
    }
    valueKnob.setValue (displayVal, juce::dontSendNotification);
    ignoreSlider = false;

    int chId = (slot.channel == 0) ? 1 : (slot.channel + 1);
    channelBox.setSelectedId (chId, juce::dontSendNotification);
}

void ParamSlotComponent::resized()
{
    auto area = getLocalBounds().reduced (4);

    // Top row: name + channel box
    auto topRow = area.removeFromTop (20);
    channelBox.setBounds  (topRow.removeFromRight (46));
    topRow.removeFromRight (3);
    nameLabel.setBounds (topRow);

    area.removeFromTop (2);

    // CC label at bottom
    auto bottomRow = area.removeFromBottom (16);
    ccLabel.setBounds (bottomRow);

    area.removeFromBottom (2);

    // Knob fills remaining space, centered
    int knobSize = juce::jmin (area.getWidth(), area.getHeight());
    knobSize = juce::jmin (knobSize, 62);
    valueKnob.setBounds (area.withSizeKeepingCentre (knobSize, knobSize));
}

void ParamSlotComponent::paint (juce::Graphics& g)
{
    g.setColour (surfaceHigh);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (surface.brighter (0.1f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 0.8f);
}

void ParamSlotComponent::sliderValueChanged (juce::Slider* s)
{
    if (ignoreSlider || s != &valueKnob) return;

    int newVal = (int) valueKnob.getValue();
    proc.paramSlots[index].value = newVal;

    // Send CC immediately (snap, 0ms morph for live knob control)
    std::array<int, 16> targets {};
    for (int i = 0; i < 16; ++i)
        targets[i] = proc.paramSlots[i].value;
    targets[index] = newVal;

    // Only send the single changed parameter — wrap in a quick snap for this param
    // by directly emitting through the engine as a one-param snap
    // We do this by creating a temporary single-param approach:
    // Actually just queue via processor sending directly is cleaner here.
    // We'll push a raw MIDI CC via the engine's FIFO by doing a fake snap.
    // Simpler: send direct via engine helper — but engine doesn't have that.
    // Use processBlock path: store value, let the next processBlock send it
    // via a flag. For simplicity, we prime the engine with a snap on this param only.

    // Push directly to a separate "live CC" mechanism:
    // We store a flag and let the processor send it in processBlock.
    // For simplicity here, trigger a zero-duration morph on all slots
    // from their current values — this sends CCs for changed params only.
    if (! proc.morphEngine.isMorphing())
    {
        proc.morphEngine.startMorph (targets, proc.paramSlots, proc.globalChannel, 0.0);
    }
}

void ParamSlotComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent == &valueKnob && e.mods.isRightButtonDown())
    {
        showCCPopup();
    }
}

void ParamSlotComponent::showCCPopup()
{
    auto* aw = new juce::AlertWindow ("Set CC",
        "CC number for " + proc.paramSlots[(size_t) index].name + ":",
        juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("cc", juce::String (proc.paramSlots[(size_t) index].ccNumber));
    aw->addButton ("OK",     1);
    aw->addButton ("Cancel", 0);
    aw->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, aw] (int result)
        {
            if (result == 1)
            {
                int cc = aw->getTextEditorContents ("cc").getIntValue();
                proc.paramSlots[(size_t) index].ccNumber = juce::jlimit (0, 127, cc);
            }
            delete aw;
        }), false);
}

//==============================================================================
// MorphoEditor
//==============================================================================
MorphoEditor::MorphoEditor (MorphoProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&laf);

    setSize (kWidth, kHeight);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / (double) kHeight);
    getConstrainer()->setSizeLimits (kWidth / 2, kHeight / 2, kWidth * 2, kHeight * 2);

    buildUI();

    proc.onStateChanged = [this] { refreshPresetButtons(); refreshParamSlots(); };

    startTimerHz (30);
}

MorphoEditor::~MorphoEditor()
{
    stopTimer();
    proc.onStateChanged = nullptr;
    setLookAndFeel (nullptr);
}

void MorphoEditor::buildUI()
{
    // Title
    titleLabel.setText ("MORPHO", juce::dontSendNotification);
    titleLabel.setFont (CustomLookAndFeel::getMonoFont (20.0f).boldened());
    titleLabel.setColour (juce::Label::textColourId, accent);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    // Global channel
    globalChLabel.setText ("CH OUT", juce::dontSendNotification);
    globalChLabel.setFont (CustomLookAndFeel::getMonoFont (10.0f));
    globalChLabel.setColour (juce::Label::textColourId, dimText);
    globalChLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (globalChLabel);

    for (int ch = 1; ch <= 16; ++ch)
        globalChBox.addItem (juce::String (ch), ch);
    globalChBox.setSelectedId (proc.globalChannel, juce::dontSendNotification);
    globalChBox.onChange = [this]
    {
        proc.globalChannel = globalChBox.getSelectedId();
    };
    addAndMakeVisible (globalChBox);

    // Preset buttons
    for (int i = 0; i < 16; ++i)
    {
        auto* btn = presetButtons.add (new PresetSlotButton (i, proc));
        btn->onClick = [this] (int idx, bool rightClick)
        {
            if (rightClick)
            {
                // Right-click: save preset with name dialog
                proc.savePreset (idx);
                auto* aw = new juce::AlertWindow ("Rename Preset", "Enter new name:", juce::MessageBoxIconType::NoIcon);
                aw->addTextEditor ("name", proc.presets[(size_t) idx].name);
                aw->addButton ("OK",     1);
                aw->addButton ("Cancel", 0);
                aw->enterModalState (true, juce::ModalCallbackFunction::create (
                    [this, aw, idx] (int result)
                    {
                        if (result == 1)
                        {
                            juce::String newName = aw->getTextEditorContents ("name").substring (0, 16);
                            if (newName.isNotEmpty())
                                proc.presets[(size_t) idx].name = newName;
                        }
                        delete aw;
                        refreshPresetButtons();
                    }), false);
            }
            else
            {
                // Left-click: recall
                proc.recallPreset (idx);
            }
        };
        addAndMakeVisible (btn);
    }

    // Morph time label + slider
    morphTimeLabel2.setText ("MORPH TIME", juce::dontSendNotification);
    morphTimeLabel2.setFont (CustomLookAndFeel::getMonoFont (10.0f));
    morphTimeLabel2.setColour (juce::Label::textColourId, dimText);
    morphTimeLabel2.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (morphTimeLabel2);

    morphTimeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    morphTimeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    morphTimeSlider.setRange (0.0, 30000.0, 1.0);
    morphTimeSlider.setValue (proc.morphTimeMs, juce::dontSendNotification);
    morphTimeSlider.onValueChange = [this]
    {
        proc.morphTimeMs = morphTimeSlider.getValue();
        morphTimeValueLabel.setText (morphTimeLabel (proc.morphTimeMs),
                                     juce::dontSendNotification);
    };
    addAndMakeVisible (morphTimeSlider);

    morphTimeValueLabel.setText (morphTimeLabel (proc.morphTimeMs), juce::dontSendNotification);
    morphTimeValueLabel.setFont (CustomLookAndFeel::getMonoFont (11.0f).boldened());
    morphTimeValueLabel.setColour (juce::Label::textColourId, accent);
    morphTimeValueLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (morphTimeValueLabel);

    // Curve selector
    curveLabel.setText ("CURVE", juce::dontSendNotification);
    curveLabel.setFont (CustomLookAndFeel::getMonoFont (10.0f));
    curveLabel.setColour (juce::Label::textColourId, dimText);
    curveLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (curveLabel);

    curveBox.addItem ("LINEAR",  1);
    curveBox.addItem ("EASE",    2);
    curveBox.addItem ("S-CURVE", 3);
    curveBox.setSelectedId ((int) proc.curveMode + 1, juce::dontSendNotification);
    curveBox.onChange = [this]
    {
        proc.curveMode = (MorphEngine::CurveMode) (curveBox.getSelectedId() - 1);
        proc.morphEngine.setCurveMode (proc.curveMode);
    };
    addAndMakeVisible (curveBox);

    // Stop morph button
    stopMorphButton.onClick = [this] { proc.stopMorph(); };
    stopMorphButton.setColour (juce::TextButton::buttonColourId,    juce::Colour (0xff330000));
    stopMorphButton.setColour (juce::TextButton::textColourOffId,   juce::Colour (0xffff4444));
    addAndMakeVisible (stopMorphButton);

    // Param grid
    for (int i = 0; i < 16; ++i)
    {
        auto* comp = paramComponents.add (new ParamSlotComponent (i, proc, laf));
        addAndMakeVisible (comp);
    }

    refreshPresetButtons();
    refreshParamSlots();
    refreshMorphControls();
}

//==============================================================================
void MorphoEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    // Subtle separator lines
    auto w = getWidth();
    g.setColour (surfaceHigh);
    g.drawHorizontalLine (40,  0, (float) w);
    g.drawHorizontalLine (105, 0, (float) w);
    g.drawHorizontalLine (170, 0, (float) w);
}

void MorphoEditor::resized()
{
    auto scaled = [&] (int px) { return juce::roundToInt (px * getWidth() / (double) kWidth); };
    auto area   = getLocalBounds().reduced (scaled (6));

    layoutHeader  (area.removeFromTop    (scaled (40)));
    area.removeFromTop (scaled (4));
    layoutPresets (area.removeFromTop    (scaled (56)));
    area.removeFromTop (scaled (4));
    layoutMorph   (area.removeFromTop    (scaled (56)));
    area.removeFromTop (scaled (4));
    layoutParams  (area);
}

void MorphoEditor::layoutHeader (juce::Rectangle<int> area)
{
    titleLabel.setBounds (area.removeFromLeft (area.getWidth() / 2));
    auto right = area;
    globalChBox.setBounds   (right.removeFromRight (50));
    right.removeFromRight (6);
    globalChLabel.setBounds (right.removeFromRight (70));
}

void MorphoEditor::layoutPresets (juce::Rectangle<int> area)
{
    int btnW = area.getWidth() / 16;
    for (int i = 0; i < 16; ++i)
        presetButtons[i]->setBounds (area.removeFromLeft (btnW).reduced (2));
}

void MorphoEditor::layoutMorph (juce::Rectangle<int> area)
{
    // [MORPH TIME label][slider][value]  [CURVE label][combobox]  [STOP]
    auto stopArea = area.removeFromRight (110);
    stopMorphButton.setBounds (stopArea.reduced (4, 8));

    area.removeFromRight (8);

    auto curveArea = area.removeFromRight (180);
    curveBox.setBounds   (curveArea.removeFromRight (100).reduced (2, 8));
    curveLabel.setBounds (curveArea.reduced (2, 8));

    area.removeFromRight (8);

    morphTimeValueLabel.setBounds (area.removeFromRight  (64).reduced (2, 8));
    morphTimeSlider.setBounds     (area.removeFromRight  (area.getWidth() - 90).reduced (0, 8));
    morphTimeLabel2.setBounds     (area.reduced (2, 8));
}

void MorphoEditor::layoutParams (juce::Rectangle<int> area)
{
    const int cols = 4;
    const int rows = 4;
    int cellW = area.getWidth()  / cols;
    int cellH = area.getHeight() / rows;

    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            int i = row * cols + col;
            paramComponents[i]->setBounds (
                juce::Rectangle<int> (
                    area.getX() + col * cellW,
                    area.getY() + row * cellH,
                    cellW, cellH).reduced (3));
        }
    }
}

//==============================================================================
void MorphoEditor::timerCallback()
{
    // Refresh preset buttons to update progress bars
    bool morphing = proc.morphEngine.isMorphing();

    for (auto* btn : presetButtons)
        btn->refresh();

    // During morph, refresh param knobs to show interpolated values
    if (morphing)
        refreshParamSlots();

    stopMorphButton.setEnabled (morphing);
}

void MorphoEditor::refreshPresetButtons()
{
    for (auto* btn : presetButtons)
        btn->refresh();
}

void MorphoEditor::refreshParamSlots()
{
    for (auto* p : paramComponents)
        p->refresh();
}

void MorphoEditor::refreshMorphControls()
{
    morphTimeSlider.setValue (proc.morphTimeMs, juce::dontSendNotification);
    morphTimeValueLabel.setText (morphTimeLabel (proc.morphTimeMs),
                                 juce::dontSendNotification);
    curveBox.setSelectedId ((int) proc.curveMode + 1, juce::dontSendNotification);
    globalChBox.setSelectedId (proc.globalChannel, juce::dontSendNotification);
}

juce::String MorphoEditor::morphTimeLabel (double ms)
{
    if (ms < 1.0) return "SNAP";
    return juce::String (ms / 1000.0, 1) + " s";
}
