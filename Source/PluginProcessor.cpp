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
        for (size_t i = 0; i < 16; ++i)
            paramSlots[i].value = finalVals[i];

        notifyStateChanged();
    };
}

MorphoProcessor::~MorphoProcessor() { cancelPendingUpdate(); }

void MorphoProcessor::notifyStateChanged()
{
    if (juce::MessageManager::existsAndIsCurrentThread())
        handleAsyncUpdate();
    else
        triggerAsyncUpdate();
}

void MorphoProcessor::handleAsyncUpdate()
{
    if (onStateChanged)
        onStateChanged();
}

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
    // Mid-morph, paramSlots still hold the values from before it: save what is being sent now.
    const auto live = morphEngine.isMorphing() ? morphEngine.getCurrentValues() : std::array<int, 16> {};
    for (size_t i = 0; i < 16; ++i)
        p.values[i] = morphEngine.isMorphing() ? live[i] : paramSlots[i].value;

    if (! p.hasData)
        p.name = "Preset " + juce::String (index + 1);

    // Cycle through 8 colors on each save
    p.colorIndex = (p.colorIndex + 1) % 8;
    p.hasData    = true;

    notifyStateChanged();
}

void MorphoProcessor::clearPreset (int index)
{
    if (index < 0 || index >= 16) return;
    presets[(size_t) index] = PresetSlot {};
    if (activePreset == index) activePreset = -1;
    notifyStateChanged();
}

void MorphoProcessor::renamePreset (int index, const juce::String& name)
{
    if (index < 0 || index >= 16) return;
    const auto clean = name.trim().substring (0, 16);
    if (clean.isNotEmpty())
        presets[(size_t) index].name = clean;
    notifyStateChanged();
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

    notifyStateChanged();
}

void MorphoProcessor::stopMorph()
{
    morphEngine.stopMorph();
    notifyStateChanged();
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

    // Everything read back is clamped: a damaged or hand-edited session must not produce
    // out-of-range CCs, channels or curve values.
    auto intProp = [] (const juce::ValueTree& t, const char* key, int def, int lo, int hi)
    {
        const juce::var v = t.getProperty (id (key), def);
        return juce::jlimit (lo, hi, v.isInt() || v.isInt64() || v.isDouble() || v.isBool() ? (int) v : def);
    };

    globalChannel = intProp (state, "globalChannel", 1, 1, 16);
    morphTimeMs   = juce::jlimit (0.0, 30000.0, (double) state.getProperty (id ("morphTimeMs"), 0.0));
    curveMode     = (MorphEngine::CurveMode) intProp (state, "curveMode", 0, 0, 2);
    activePreset  = intProp (state, "activePreset", -1, -1, 15);

    size_t paramIdx  = 0;
    size_t presetIdx = 0;

    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        auto child = state.getChild (i);

        if (child.getType() == id ("Param") && paramIdx < 16)
        {
            auto& slot   = paramSlots[paramIdx];
            slot.name     = child.getProperty (id ("name"), "Param " + juce::String ((int) paramIdx + 1)).toString().substring (0, 16);
            slot.ccNumber = intProp (child, "cc",      (int) paramIdx + 1, 0, 127);
            slot.channel  = intProp (child, "channel", 0, 0, 16);
            slot.value    = intProp (child, "value",   0, 0, 127);
            ++paramIdx;
        }
        else if (child.getType() == id ("Preset") && presetIdx < 16)
        {
            auto& preset      = presets[presetIdx];
            preset.name       = child.getProperty (id ("name"), "").toString().substring (0, 16);
            preset.colorIndex = intProp (child, "colorIndex", 0, 0, 7);
            preset.hasData    = (bool) child.getProperty (id ("hasData"), false);
            for (int j = 0; j < 16; ++j)
                preset.values[(size_t) j] = intProp (child, ("v" + juce::String (j)).toRawUTF8(), 0, 0, 127);
            ++presetIdx;
        }
    }

    if (activePreset >= 0 && ! presets[(size_t) activePreset].hasData)
        activePreset = -1;
    morphEngine.setCurveMode (curveMode);

    // Sync MorphEngine's current values to loaded param values
    // (MorphEngine starts with all zeros; we need to prime it so morphs start from correct values)
    std::array<int, 16> vals {};
    for (size_t i = 0; i < 16; ++i) vals[i] = paramSlots[i].value;
    // Trigger a snap (0ms) to the current values to prime the engine
    morphEngine.startMorph (vals, paramSlots, globalChannel, 0.0);

    notifyStateChanged();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MorphoProcessor();
}
