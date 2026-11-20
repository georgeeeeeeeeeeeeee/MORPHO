#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

struct ParameterSlot
{
    juce::String name     = "Param";
    int          ccNumber = 1;
    int          channel  = 0;   // 0 = follow global channel
    int          value    = 0;   // 0–127: the knob
    int          minValue = 0;   // the output range the knob covers: 0 maps to minValue,
    int          maxValue = 127; // 127 to maxValue (presets store knob positions, not outputs)
};
