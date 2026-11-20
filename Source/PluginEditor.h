#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"

//==============================================================================
// Forward declarations
class ParamSlotComponent;
class PresetSlotButton;

//==============================================================================
// A small question drawn inside MORPHO's own window (a separate pop-up window ends up behind plug-in
// windows that Live keeps on top). Enter: OK. Esc or a click outside: cancel.
class InlinePrompt : public juce::Component
{
public:
    InlinePrompt();
    void ask (const juce::String& title, const juce::String& initial, int maxChars, const juce::String& allowed,
              std::function<void (const juce::String&)> onOk);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void finish (bool ok);
    juce::String title;
    juce::TextEditor field;
    juce::TextButton okButton { "OK" }, cancelButton { "CANCEL" };
    std::function<void (const juce::String&)> callback;
    juce::Rectangle<int> box;
};

//==============================================================================
class MorphoEditor : public juce::AudioProcessorEditor,
                     public juce::Timer
{
public:
    explicit MorphoEditor (MorphoProcessor&);
    ~MorphoEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;
    void timerCallback () override;   // 30 Hz UI refresh
    InlinePrompt& prompt() { return inlinePrompt; }

private:
    void buildUI();
    void layoutHeader    (juce::Rectangle<int> area);
    void layoutPresets   (juce::Rectangle<int> area);
    void layoutMorph     (juce::Rectangle<int> area);
    void layoutParams    (juce::Rectangle<int> area);

    void showPresetMenu (int idx);
    void askPresetName  (int idx);

    void refreshPresetButtons();
    void refreshParamSlots();
    void refreshMorphControls();

    static juce::String morphTimeLabel (double ms);

    MorphoProcessor& proc;
    CustomLookAndFeel laf;

    //--- Header ---
    juce::Label titleLabel;
    juce::Label leverStatus;

    //--- Global channel ---
    juce::Label    globalChLabel;
    juce::ComboBox globalChBox;

    //--- Preset row ---
    juce::OwnedArray<PresetSlotButton> presetButtons;

    //--- Morph controls ---
    juce::Label  morphTimeLabel2;
    juce::Slider morphTimeSlider;
    juce::Label  morphTimeValueLabel;
    juce::Label  curveLabel;
    juce::ComboBox curveBox;
    juce::TextButton stopMorphButton { "STOP MORPH" };

    //--- Parameter grid ---
    juce::OwnedArray<ParamSlotComponent> paramComponents;

    InlinePrompt inlinePrompt;   // on top of everything

    static constexpr int kWidth  = 820;
    static constexpr int kHeight = 560;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphoEditor)
};

//==============================================================================
class PresetSlotButton : public juce::Component
{
public:
    std::function<void (int, bool)> onClick;   // (index, isRightClick)

    PresetSlotButton (int idx, MorphoProcessor& p);
    void refresh();
    void paint    (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    int              index;
    MorphoProcessor& proc;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetSlotButton)
};

//==============================================================================
class ParamSlotComponent : public juce::Component,
                           public juce::Slider::Listener
{
public:
    ParamSlotComponent (int idx, MorphoProcessor& p, CustomLookAndFeel& l);
    ~ParamSlotComponent() override;

    void refresh();
    void resized () override;
    void paint   (juce::Graphics&) override;

    void sliderValueChanged (juce::Slider*) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    void showCCPopup();
    void showMapMenu();

    int              index;
    MorphoProcessor& proc;

    juce::Label    nameLabel;
    juce::Label    ccLabel;
    juce::TextButton mapButton { "MAP" };
    juce::Label    linkLabel;
    juce::Slider   range;   // two handles: the lowest and highest value the knob sends
    juce::Rectangle<int> rangeText;
    juce::Slider   valueKnob;
    juce::ComboBox channelBox;

    bool ignoreSlider = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamSlotComponent)
};
