#include "PluginEditor.h"

using namespace MorphoColors;

//==============================================================================
// InlinePrompt
//==============================================================================
InlinePrompt::InlinePrompt()
{
    field.setFont (CustomLookAndFeel::getMonoFont (14.0f));
    field.setJustification (juce::Justification::centredLeft);
    field.onReturnKey = [this] { finish (true); };
    field.onEscapeKey = [this] { finish (false); };
    okButton.onClick = [this] { finish (true); };
    cancelButton.onClick = [this] { finish (false); };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &field, &okButton, &cancelButton }) addAndMakeVisible (*c);
    setVisible (false);
}

void InlinePrompt::ask (const juce::String& t, const juce::String& initial, int maxChars, const juce::String& allowed,
                        std::function<void (const juce::String&)> onOk)
{
    title = t;
    callback = std::move (onOk);
    field.setInputRestrictions (maxChars, allowed);
    field.setText (initial, false);
    setBounds (getParentComponent() != nullptr ? getParentComponent()->getLocalBounds() : getBounds());
    setVisible (true);
    toFront (true);
    field.grabKeyboardFocus();
    field.selectAll();
    repaint();
}

void InlinePrompt::finish (bool ok)
{
    auto cb = std::move (callback);
    const auto text = field.getText();
    setVisible (false);
    callback = nullptr;
    if (ok && cb) cb (text);
}

void InlinePrompt::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    g.setColour (surface);
    g.fillRoundedRectangle (box.toFloat(), 6.0f);
    g.setColour (accent);
    g.drawRoundedRectangle (box.toFloat(), 6.0f, 1.5f);
    g.setColour (brightText);
    g.setFont (CustomLookAndFeel::getMonoFont (13.0f).boldened());
    g.drawText (title, box.reduced (16, 12).removeFromTop (22), juce::Justification::centredLeft);
}

void InlinePrompt::resized()
{
    box = getLocalBounds().withSizeKeepingCentre (juce::jmin (380, getWidth() - 20), 130);
    auto r = box.reduced (16, 12);
    r.removeFromTop (28);
    field.setBounds (r.removeFromTop (30));
    r.removeFromTop (12);
    auto buttons = r.removeFromTop (28);
    okButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (90));
}

void InlinePrompt::mouseDown (const juce::MouseEvent& e)
{
    if (! box.contains (e.getPosition())) finish (false);
}

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
        g.setColour (active ? brightText : brightText.withAlpha (0.8f));
        g.setFont (CustomLookAndFeel::getMonoFont (10.0f));
        g.drawText (preset.name, bounds.toNearestInt(),
                    juce::Justification::centred, true);
    }

    // Follow: how likely this preset is to be picked (dots), and a marker on the one coming next.
    if (preset.hasData && proc.followOn.load())
    {
        const int w = preset.weight;
        const auto full = getLocalBounds().toFloat().reduced (1.5f);
        if (w == 0)
        {
            g.setColour (dimText.withAlpha (0.7f));
            g.setFont (CustomLookAndFeel::getMonoFont (9.0f));
            g.drawText ("SKIP", full.withTrimmedTop (full.getHeight() - 12.0f).toNearestInt(), juce::Justification::centred, false);
        }
        else
        {
            const int dots = w == 1 ? 1 : w == 2 ? 2 : 3;
            const float d = 3.0f, gap = 3.0f, x0 = full.getCentreX() - (dots * d + (dots - 1) * gap) * 0.5f;
            g.setColour (dimText.withAlpha (0.8f));
            for (int k = 0; k < dots; ++k) g.fillEllipse (x0 + k * (d + gap), full.getBottom() - 6.0f, d, d);
        }
        if (proc.followNextPreset() == index && ! active)
        {
            g.setColour (accent);
            juce::Path tri;
            tri.addTriangle (full.getRight() - 10.0f, full.getY() + 2.0f, full.getRight() - 2.0f, full.getY() + 2.0f, full.getRight() - 2.0f, full.getY() + 10.0f);
            g.fillPath (tri);
            g.drawRoundedRectangle (full, 4.0f, 1.0f);
        }
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
    nameLabel.setFont (CustomLookAndFeel::getMonoFont (12.0f).boldened());
    nameLabel.onTextChange = [this]
    {
        proc.paramSlots[index].name = nameLabel.getText (true).substring (0, 16);
    };
    addAndMakeVisible (nameLabel);

    // CC label — double-click to edit
    ccLabel.setEditable (false, true, false);
    ccLabel.setJustificationType (juce::Justification::centred);
    ccLabel.setColour (juce::Label::textColourId, dimText);
    ccLabel.setFont (CustomLookAndFeel::getMonoFont (10.5f));
    ccLabel.onTextChange = [this]
    {
        int cc = ccLabel.getText().getIntValue();
        proc.paramSlots[index].ccNumber = juce::jlimit (0, 127, cc);
        ccLabel.setText ("CC " + juce::String (proc.paramSlots[index].ccNumber),
                         juce::dontSendNotification);
    };
    addAndMakeVisible (ccLabel);

    // Map: click it, then click any control in Live (through the Lever script). Right-click: unmap.
    mapButton.setTooltip ("Map: click this, then click any control in Live, on any track. Right-click to unmap");
    mapButton.onClick = [this]
    {
        if (juce::ModifierKeys::currentModifiers.isPopupMenu()) { showMapMenu(); return; }
        proc.requestMap (index);
        refresh();
    };
    addAndMakeVisible (mapButton);
    linkLabel.setFont (CustomLookAndFeel::getMonoFont (10.0f));
    linkLabel.setJustificationType (juce::Justification::centredLeft);
    linkLabel.setMinimumHorizontalScale (0.7f);
    linkLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (linkLabel);

    // Range: drag the left handle up to raise the lowest value, the right one down to lower the highest.
    range.setSliderStyle (juce::Slider::TwoValueHorizontal);
    range.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    range.setRange (0, 127, 1);
    range.setColour (juce::Slider::trackColourId, accent.withAlpha (0.55f));
    range.setColour (juce::Slider::backgroundColourId, trackBg);
    range.setColour (juce::Slider::thumbColourId, brightText);
    range.setDoubleClickReturnValue (false, 0);
    range.onValueChange = [this]
    {
        if (ignoreSlider) return;
        proc.setRange (index, (int) range.getMinValue(), (int) range.getMaxValue());
        repaint (rangeText);
        range.setTooltip ("Range " + juce::String ((int) range.getMinValue()) + " to " + juce::String ((int) range.getMaxValue())
                          + ": the knob sends only between these. Double-click: full range");
    };
    range.addMouseListener (this, false);
    range.setTooltip ("Range: the knob sends only between these. Double-click: full range");
    addAndMakeVisible (range);

    // Value knob
    valueKnob.setSliderStyle (juce::Slider::Rotary);
    valueKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    valueKnob.setRange (0, 127, 1);
    valueKnob.setDoubleClickReturnValue (true, 0);
    valueKnob.addListener (this);
    valueKnob.onDragStart = [this] { if (proc.morphEngine.isMorphing()) proc.stopMorph(); };

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

    // Refreshed 30 times a second now: leave a label alone while it's being typed into.
    if (! nameLabel.isBeingEdited()) nameLabel.setText (slot.name, juce::dontSendNotification);
    if (! ccLabel.isBeingEdited()) ccLabel.setText ("CC " + juce::String (slot.ccNumber), juce::dontSendNotification);

    ignoreSlider = true;

    // During morph, show interpolated value from engine
    int displayVal = slot.value;
    if (proc.morphEngine.isMorphing())
    {
        auto vals = proc.morphEngine.getCurrentValues();
        displayVal = vals[index];
    }
    if (! valueKnob.isMouseButtonDown())   // never fight a knob you're holding
        valueKnob.setValue (displayVal, juce::dontSendNotification);
    if (! range.isMouseButtonDown())
    {
        range.setMinValue (slot.minValue, juce::dontSendNotification, true);
        range.setMaxValue (slot.maxValue, juce::dontSendNotification, true);
    }
    ignoreSlider = false;

    int chId = (slot.channel == 0) ? 1 : (slot.channel + 1);
    channelBox.setSelectedId (chId, juce::dontSendNotification);

    using L = MorphoProcessor::Link;
    const auto ls = proc.linkState (index);
    const auto full = proc.mappedTo (index);
    const auto shortName = full.fromLastOccurrenceOf (juce::CharPointer_UTF8 ("\xe2\x80\xba"), false, false).trim();
    mapButton.setButtonText (ls == L::waiting ? "CLICK" : ls == L::mapped ? "MAPPED" : "MAP");
    mapButton.setColour (juce::TextButton::buttonColourId, ls == L::waiting ? accent : surface);
    mapButton.setColour (juce::TextButton::textColourOffId, ls == L::waiting ? background : ls == L::mapped ? accent : brightText);
    juce::String t;
    juce::Colour c = dimText;
    if (ls == L::waiting) { t = "click a Live control"; c = accent; }
    else if (ls == L::mapped) { t = juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92 ")) + (shortName.isNotEmpty() ? shortName : full); }
    else if (ls == L::lost) { t = "gone: " + shortName; c = warn; }
    linkLabel.setText (t, juce::dontSendNotification);
    linkLabel.setColour (juce::Label::textColourId, c);
    linkLabel.setTooltip (full);
}

void ParamSlotComponent::showMapMenu()
{
    using L = MorphoProcessor::Link;
    const auto ls = proc.linkState (index);
    juce::PopupMenu m;
    m.addItem (1, ls == L::waiting ? "Stop waiting for a click" : "Map: then click any control in Live");
    m.addItem (2, "Unmap", ls == L::mapped || ls == L::lost);
    if (ls == L::mapped || ls == L::lost) m.addItem (3, proc.mappedTo (index), false);
    juce::Component::SafePointer<ParamSlotComponent> safeThis (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&mapButton), [safeThis] (int r) {
        if (safeThis == nullptr) return;
        if (r == 1) safeThis->proc.requestMap (safeThis->index);
        else if (r == 2) safeThis->proc.unmapSlot (safeThis->index);
        safeThis->refresh();
    });
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

    // Bottom: [MAP] [CC n] [what it's mapped to]
    auto bottomRow = area.removeFromBottom (18);
    mapButton.setBounds (bottomRow.removeFromLeft (juce::jmin (64, bottomRow.getWidth() / 3)));
    bottomRow.removeFromLeft (3);
    ccLabel.setBounds (bottomRow.removeFromLeft (46));
    linkLabel.setBounds (bottomRow);

    area.removeFromBottom (2);

    // Middle: the knob on the left, its range bar beside it (with the numbers under the bar)
    const int knobSize = juce::jmin (62, area.getHeight(), area.getWidth() / 3);
    valueKnob.setBounds (area.removeFromLeft (knobSize).withSizeKeepingCentre (knobSize, knobSize));
    area.removeFromLeft (8);
    rangeText = area.withSizeKeepingCentre (area.getWidth(), 12).translated (0, 9);
    range.setBounds (area.withSizeKeepingCentre (area.getWidth(), 16).translated (0, -5));
}

void ParamSlotComponent::paint (juce::Graphics& g)
{
    g.setColour (surfaceHigh);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (surface.brighter (0.1f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 0.8f);
    const auto& slot = proc.paramSlots[(size_t) index];
    const bool full = slot.minValue == 0 && slot.maxValue == 127;
    g.setColour (full ? dimText : accent);
    g.setFont (CustomLookAndFeel::getMonoFont (9.5f));
    g.drawText (full ? juce::String ("RANGE: FULL") : "RANGE " + juce::String (slot.minValue) + "-" + juce::String (slot.maxValue),
                rangeText, juce::Justification::centredLeft);
}

void ParamSlotComponent::sliderValueChanged (juce::Slider* s)
{
    if (ignoreSlider || s != &valueKnob) return;

    // Touching a knob mid-morph takes over: the morph stops where it is (every slot holds its
    // current value) and this knob goes where you put it.
    const int newVal = (int) valueKnob.getValue();   // before stopping: that redraws the knobs
    if (proc.morphEngine.isMorphing())
        proc.stopMorph();
    proc.paramSlots[index].value = newVal;
    ignoreSlider = true;
    valueKnob.setValue (newVal, juce::dontSendNotification);
    ignoreSlider = false;

    // A zero-length morph to the current values: the engine sends only the slots that
    // changed, so turning one knob sends one CC.
    std::array<int, 16> targets {};
    for (size_t i = 0; i < 16; ++i)
        targets[i] = proc.paramSlots[i].value;
    proc.morphEngine.startMorph (targets, proc.paramSlots, proc.globalChannel, 0.0);
}

void ParamSlotComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent == &valueKnob && e.mods.isRightButtonDown())
    {
        showCCPopup();
    }
    else if (e.eventComponent == &range && e.getNumberOfClicks() >= 2)
    {
        proc.setRange (index, 0, 127);
        refresh();
    }
}

void ParamSlotComponent::showCCPopup()
{
    auto* ed = findParentComponentOfClass<MorphoEditor>();
    if (ed == nullptr) return;
    juce::Component::SafePointer<ParamSlotComponent> safeThis (this);
    ed->prompt().ask ("CC number for " + proc.paramSlots[(size_t) index].name, juce::String (proc.paramSlots[(size_t) index].ccNumber), 3, "0123456789",
        [safeThis] (const juce::String& text)
        {
            if (safeThis == nullptr) return;
            safeThis->proc.paramSlots[(size_t) safeThis->index].ccNumber = juce::jlimit (0, 127, text.getIntValue());
            safeThis->refresh();
        });
}

//==============================================================================
// MorphoEditor
//==============================================================================
MorphoEditor::MorphoEditor (MorphoProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&laf);

    // Build the controls before the first setSize(): setSize calls resized(), which lays
    // them out (it used to run first and crash on the missing buttons when the window opened).
    buildUI();

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / (double) kHeight);
    getConstrainer()->setSizeLimits (kWidth / 2, kHeight / 2, kWidth * 2, kHeight * 2);
    setSize (kWidth, kHeight);

    proc.onStateChanged = [this] { refreshPresetButtons(); refreshParamSlots(); refreshMorphControls(); };

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

    leverStatus.setFont (CustomLookAndFeel::getMonoFont (10.0f));
    leverStatus.setJustificationType (juce::Justification::centredLeft);
    leverStatus.setMinimumHorizontalScale (0.6f);
    addAndMakeVisible (leverStatus);

    // Global channel
    globalChLabel.setText ("CH OUT", juce::dontSendNotification);
    globalChLabel.setFont (CustomLookAndFeel::getMonoFont (11.0f));
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
                showPresetMenu (idx);
            else
                proc.recallPreset (idx);   // left-click: recall
        };
        addAndMakeVisible (btn);
    }

    // Morph time label + slider
    morphTimeLabel2.setText ("MORPH TIME", juce::dontSendNotification);
    morphTimeLabel2.setFont (CustomLookAndFeel::getMonoFont (11.0f));
    morphTimeLabel2.setColour (juce::Label::textColourId, dimText);
    morphTimeLabel2.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (morphTimeLabel2);

    morphTimeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    morphTimeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    morphTimeSlider.setRange (0.0, 30000.0, 1.0);
    morphTimeSlider.setSkewFactorFromMidPoint (3000.0);   // half the travel covers 0 to 3 s
    morphTimeSlider.setValue (proc.morphTimeMs, juce::dontSendNotification);
    morphTimeSlider.onValueChange = [this]
    {
        proc.morphTimeMs = morphTimeSlider.getValue();
        morphTimeValueLabel.setText (morphTimeLabel (proc.morphTimeMs),
                                     juce::dontSendNotification);
    };
    addAndMakeVisible (morphTimeSlider);

    morphTimeValueLabel.setText (morphTimeLabel (proc.morphTimeMs), juce::dontSendNotification);
    morphTimeValueLabel.setFont (CustomLookAndFeel::getMonoFont (12.0f).boldened());
    morphTimeValueLabel.setColour (juce::Label::textColourId, accent);
    morphTimeValueLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (morphTimeValueLabel);

    // Curve selector
    curveLabel.setText ("CURVE", juce::dontSendNotification);
    curveLabel.setFont (CustomLookAndFeel::getMonoFont (11.0f));
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

    // Follow: Live-style follow actions for presets
    followButton.setClickingTogglesState (true);
    followButton.setToggleState (proc.followOn.load(), juce::dontSendNotification);
    followButton.setColour (juce::TextButton::buttonOnColourId, accent);
    followButton.setColour (juce::TextButton::textColourOnId, background);
    followButton.setTooltip ("At the end of each phrase, maybe morph to another preset (with the transport playing)");
    followButton.onClick = [this] { proc.followOn = followButton.getToggleState(); proc.syncFollow(); refreshPresetButtons(); };
    addAndMakeVisible (followButton);
    const auto smallLabel = [this] (juce::Label& l, const char* text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (CustomLookAndFeel::getMonoFont (11.0f));
        l.setColour (juce::Label::textColourId, dimText);
        l.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    };
    smallLabel (everyLabel, "EVERY");
    smallLabel (landLabel, "MORPH");
    smallLabel (chanceLabel, "CHANCE");
    smallLabel (pickLabel, "PICK");
    for (int i = 0; i < 5; ++i) everyBox.addItem (juce::String (FollowSettings::kEveryBars[i]) + (i == 0 ? " BAR" : " BARS"), i + 1);
    everyBox.onChange = [this] { proc.followEvery = everyBox.getSelectedId() - 1; };
    everyBox.setTooltip ("How long a phrase is: a change can only happen on these bar lines");
    landBox.addItem ("STARTS ON THE LINE", 1);
    landBox.addItem ("ENDS ON THE LINE", 2);
    landBox.onChange = [this] { proc.followLand = landBox.getSelectedId() - 1; };
    landBox.setTooltip ("Starts on the line: the morph begins on the bar line. Ends on the line: it begins a morph time earlier and arrives on it");
    pickBox.addItem ("ANY OTHER", 1);
    pickBox.addItem ("ANY (CAN STAY)", 2);
    pickBox.addItem ("NEXT", 3);
    pickBox.addItem ("PREVIOUS", 4);
    pickBox.onChange = [this] { proc.followPick = pickBox.getSelectedId() - 1; };
    pickBox.setTooltip ("Which preset comes next. Right-click a preset to make it come up more or less often, or never");
    for (auto* b : { &everyBox, &landBox, &pickBox }) addAndMakeVisible (*b);
    chanceSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    chanceSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
    chanceSlider.setRange (0.0, 100.0, 1.0);
    chanceSlider.setTextValueSuffix ("%");
    chanceSlider.setColour (juce::Slider::textBoxTextColourId, accent);
    chanceSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    chanceSlider.setTooltip ("How likely a change is at each phrase line");
    chanceSlider.onValueChange = [this] { proc.followChance = (int) chanceSlider.getValue(); };
    addAndMakeVisible (chanceSlider);
    followStatus.setFont (CustomLookAndFeel::getMonoFont (11.0f));
    followStatus.setColour (juce::Label::textColourId, dimText);
    followStatus.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (followStatus);
    refreshFollow();

    // Param grid
    for (int i = 0; i < 16; ++i)
    {
        auto* comp = paramComponents.add (new ParamSlotComponent (i, proc, laf));
        addAndMakeVisible (comp);
    }

    refreshPresetButtons();
    refreshParamSlots();
    refreshMorphControls();
    addChildComponent (inlinePrompt);   // last, so it sits above everything
}

//==============================================================================
// Right-click on a preset: save, rename or clear. Saving over a stored preset asks first, so a
// stray right-click can't lose one.
void MorphoEditor::showPresetMenu (int idx)
{
    const auto& preset = proc.presets[(size_t) idx];
    juce::PopupMenu m;
    m.addSectionHeader ("Preset " + juce::String (idx + 1) + (preset.hasData ? ": " + preset.name : juce::String()));
    m.addItem (1, preset.hasData ? "Save current values here (replace)" : "Save current values here");
    m.addItem (2, "Rename...", preset.hasData);
    m.addItem (3, "Clear", preset.hasData);
    juce::PopupMenu follow;
    const std::pair<int, const char*> weights[] = { { 0, "Never (skip it)" }, { 1, "Less often" }, { 2, "Normal" }, { 4, "More often" } };
    for (auto [w, label] : weights) follow.addItem (10 + w, label, preset.hasData, preset.weight == w);
    m.addSubMenu ("Follow: how often it's picked", follow, preset.hasData);

    juce::Component::SafePointer<MorphoEditor> safeThis (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButtons[idx]),
        [safeThis, idx] (int choice)
        {
            if (safeThis == nullptr) return;
            auto& ed = *safeThis;
            if (choice == 1)
            {
                ed.proc.savePreset (idx);   // no naming step (George: not needed); Rename... is there if wanted
            }
            else if (choice == 2) ed.askPresetName (idx);
            else if (choice == 3) ed.proc.clearPreset (idx);
            else if (choice >= 10 && choice <= 14) { ed.proc.setPresetWeight (idx, choice - 10); ed.refreshPresetButtons(); }
        });
}

void MorphoEditor::askPresetName (int idx)
{
    juce::Component::SafePointer<MorphoEditor> safeThis (this);
    inlinePrompt.ask ("Name preset " + juce::String (idx + 1), proc.presets[(size_t) idx].name, 16, {},
        [safeThis, idx] (const juce::String& text)
        {
            if (safeThis != nullptr) safeThis->proc.renamePreset (idx, text);
        });
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
    g.drawHorizontalLine (235, 0, (float) w);
}

void MorphoEditor::resized()
{
    inlinePrompt.setBounds (getLocalBounds());
    if (presetButtons.size() < 16 || paramComponents.size() < 16)
        return;

    auto scaled = [&] (int px) { return juce::roundToInt (px * getWidth() / (double) kWidth); };
    auto area   = getLocalBounds().reduced (scaled (6));

    layoutHeader  (area.removeFromTop    (scaled (40)));
    area.removeFromTop (scaled (4));
    layoutPresets (area.removeFromTop    (scaled (56)));
    area.removeFromTop (scaled (4));
    layoutMorph   (area.removeFromTop    (scaled (56)));
    area.removeFromTop (scaled (4));
    layoutFollow  (area.removeFromTop    (scaled (56)));
    area.removeFromTop (scaled (4));
    layoutParams  (area);
}

void MorphoEditor::layoutHeader (juce::Rectangle<int> area)
{
    titleLabel.setBounds (area.removeFromLeft (110));
    auto right = area;
    globalChBox.setBounds   (right.removeFromRight (50));
    right.removeFromRight (6);
    globalChLabel.setBounds (right.removeFromRight (70));
    right.removeFromRight (10);
    leverStatus.setBounds (right);
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

void MorphoEditor::layoutFollow (juce::Rectangle<int> area)
{
    // [FOLLOW] [EVERY][4 BARS] [MORPH][STARTS ON THE LINE] [CHANCE][----] [PICK][ANY OTHER]
    // and under them, what's coming.
    auto status = area.removeFromBottom (area.getHeight() / 3);
    followStatus.setBounds (status.withTrimmedLeft (4));
    const auto row = [&] (int w) { return area.removeFromLeft (juce::roundToInt (w * getWidth() / (double) kWidth)); };
    followButton.setBounds (row (90).reduced (2, 4));
    everyLabel.setBounds (row (52).reduced (2, 4));
    everyBox.setBounds (row (90).reduced (2, 4));
    landLabel.setBounds (row (52).reduced (2, 4));
    landBox.setBounds (row (160).reduced (2, 4));
    chanceLabel.setBounds (row (60).reduced (2, 4));
    chanceSlider.setBounds (row (130).reduced (2, 4));
    pickLabel.setBounds (row (42).reduced (2, 4));
    pickBox.setBounds (area.reduced (2, 4));
}

void MorphoEditor::refreshFollow()
{
    followButton.setToggleState (proc.followOn.load(), juce::dontSendNotification);
    everyBox.setSelectedId (proc.followEvery.load() + 1, juce::dontSendNotification);
    landBox.setSelectedId (proc.followLand.load() + 1, juce::dontSendNotification);
    pickBox.setSelectedId (proc.followPick.load() + 1, juce::dontSendNotification);
    chanceSlider.setValue (proc.followChance.load(), juce::dontSendNotification);
    const bool on = proc.followOn.load();
    for (juce::Component* c : { (juce::Component*) &everyBox, (juce::Component*) &landBox, (juce::Component*) &pickBox, (juce::Component*) &chanceSlider })
        c->setAlpha (on ? 1.0f : 0.5f);

    juce::String st;
    int saved = 0;
    for (const auto& pr : proc.presets) saved += pr.hasData ? 1 : 0;
    if (! on) st = "Follow is off: turn it on to let MORPHO move between presets on its own, in time";
    else if (saved < 2) st = "Save at least two presets for Follow to move between";
    else if (proc.followNextBar() < 0) st = "Waiting for Live to play";
    else
    {
        const int nx = proc.followNextPreset();
        st = "BAR " + juce::String (proc.followNextBar()) + ": "
           + (nx >= 0 ? "-> " + juce::String (nx + 1) + " " + proc.presets[(size_t) nx].name : juce::String ("stays"));
    }
    if (followStatus.getText() != st) followStatus.setText (st, juce::dontSendNotification);
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

    juce::String st;
    juce::Colour c = dimText;
    if (proc.leverConnected()) st = "LEVER CONNECTED: MAP a slot, then click any Live control";
    else if (proc.leverSilent()) { st = "LEVER DIDN'T ANSWER: Live Settings > Control Surface: Lever, Input: IAC Driver Lever"; c = warn; }
    else st = "Click-to-map: set Lever as a Control Surface (Input: IAC Driver Lever)";
    if (leverStatus.getText() != st) { leverStatus.setText (st, juce::dontSendNotification); leverStatus.setColour (juce::Label::textColourId, c); }
    // Map buttons change state without a morph (waiting, mapped): keep them current.
    for (auto* pc : paramComponents) pc->refresh();
    refreshFollow();
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
