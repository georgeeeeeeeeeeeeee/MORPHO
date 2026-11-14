#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MorphoProcessor::MorphoProcessor()
    // Stereo in/out so Ableton Live will load it (Live rejects VST3s with no audio buses).
    // Same layout as LFO Sequencer, which is known to load and route MIDI in Live.
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), false))
{
    for (int i = 0; i < 16; ++i)
    {
        paramSlots[i].name     = "Param " + juce::String (i + 1);
        paramSlots[i].ccNumber = i + 1;
        paramSlots[i].channel  = 0;
        paramSlots[i].value    = 0;
    }

    morphEngine.onMorphFinished = [this] (const std::array<int, 16>& finalVals)
    {
        // Called on message thread when morph completes or is stopped
        for (int i = 0; i < 16; ++i)
            paramSlots[i].value = finalVals[i];

        if (onStateChanged)
            onStateChanged();
    };
}

MorphoProcessor::~MorphoProcessor() {}

//==============================================================================
void MorphoProcessor::prepareToPlay (double sampleRate, int)
{
    morphEngine.prepare (sampleRate);
}

void MorphoProcessor::releaseResources() {}

void MorphoProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midiMessages)
{
    // Audio and incoming MIDI pass through untouched; our CC messages are added.
    morphEngine.process (midiMessages, buffer.getNumSamples());
}

//==============================================================================
juce::AudioProcessorEditor* MorphoProcessor::createEditor()
{
    return new MorphoEditor (*this);
}

//==============================================================================
void MorphoProcessor::savePreset (int index)
{
    if (index < 0 || index >= 16) return;

    auto& p = presets[index];
    for (int i = 0; i < 16; ++i)
        p.values[i] = paramSlots[i].value;

    if (! p.hasData)
        p.name = "Preset " + juce::String (index + 1);

    // Cycle through 8 colors on each save
    p.colorIndex = (p.colorIndex + 1) % 8;
    p.hasData    = true;

    if (onStateChanged)
        onStateChanged();
}

void MorphoProcessor::recallPreset (int index)
{
    if (index < 0 || index >= 16) return;
    if (! presets[index].hasData) return;

    activePreset = index;
    morphEngine.setCurveMode (curveMode);

    morphEngine.startMorph (presets[index].values,
                             paramSlots,
                             globalChannel,
                             morphTimeMs);

    if (onStateChanged)
        onStateChanged();
}

void MorphoProcessor::stopMorph()
{
    morphEngine.stopMorph();
    if (onStateChanged)
        onStateChanged();
}

//==============================================================================
static juce::Identifier id (const char* s) { return juce::Identifier (s); }

void MorphoProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("MorphoState");
    state.setProperty (id ("globalChannel"), globalChannel, nullptr);
    state.setProperty (id ("morphTimeMs"),   morphTimeMs,   nullptr);
    state.setProperty (id ("curveMode"),     (int) curveMode, nullptr);
    state.setProperty (id ("activePreset"),  activePreset,  nullptr);

    for (int i = 0; i < 16; ++i)
    {
        juce::ValueTree slot ("Param");
        slot.setProperty (id ("name"),     paramSlots[i].name,     nullptr);
        slot.setProperty (id ("cc"),       paramSlots[i].ccNumber, nullptr);
        slot.setProperty (id ("channel"),  paramSlots[i].channel,  nullptr);
        slot.setProperty (id ("value"),    paramSlots[i].value,    nullptr);
        state.addChild (slot, -1, nullptr);
    }

    for (int i = 0; i < 16; ++i)
    {
        juce::ValueTree preset ("Preset");
        preset.setProperty (id ("name"),       presets[i].name,       nullptr);
        preset.setProperty (id ("colorIndex"), presets[i].colorIndex, nullptr);
        preset.setProperty (id ("hasData"),    presets[i].hasData,    nullptr);
        for (int j = 0; j < 16; ++j)
            preset.setProperty (juce::Identifier ("v" + juce::String (j)), presets[i].values[j], nullptr);
        state.addChild (preset, -1, nullptr);
    }

    juce::MemoryOutputStream stream (destData, false);
    state.writeToStream (stream);
}

void MorphoProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::ValueTree state = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! state.isValid()) return;

    globalChannel = state.getProperty (id ("globalChannel"), 1);
    morphTimeMs   = state.getProperty (id ("morphTimeMs"),   0.0);
    curveMode     = (MorphEngine::CurveMode) (int) state.getProperty (id ("curveMode"), 0);
    activePreset  = state.getProperty (id ("activePreset"),  -1);

    int paramIdx  = 0;
    int presetIdx = 0;

    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        auto child = state.getChild (i);

        if (child.getType() == id ("Param") && paramIdx < 16)
        {
            paramSlots[paramIdx].name     = child.getProperty (id ("name"),    "Param");
            paramSlots[paramIdx].ccNumber = child.getProperty (id ("cc"),      1);
            paramSlots[paramIdx].channel  = child.getProperty (id ("channel"), 0);
            paramSlots[paramIdx].value    = child.getProperty (id ("value"),   0);
            ++paramIdx;
        }
        else if (child.getType() == id ("Preset") && presetIdx < 16)
        {
            presets[presetIdx].name       = child.getProperty (id ("name"),       "");
            presets[presetIdx].colorIndex = child.getProperty (id ("colorIndex"), 0);
            presets[presetIdx].hasData    = (bool) child.getProperty (id ("hasData"), false);
            for (int j = 0; j < 16; ++j)
                presets[presetIdx].values[j] = child.getProperty (juce::Identifier ("v" + juce::String (j)), 0);
            ++presetIdx;
        }
    }

    // Sync MorphEngine's current values to loaded param values
    // (MorphEngine starts with all zeros; we need to prime it so morphs start from correct values)
    std::array<int, 16> vals {};
    for (int i = 0; i < 16; ++i) vals[i] = paramSlots[i].value;
    // Trigger a snap (0ms) to the current values to prime the engine
    morphEngine.startMorph (vals, paramSlots, globalChannel, 0.0);

    if (onStateChanged)
        onStateChanged();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MorphoProcessor();
}
