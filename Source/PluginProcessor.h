#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include "ParameterSlot.h"
#include "MorphEngine.h"

//==============================================================================
struct PresetSlot
{
    juce::String        name       = "";
    std::array<int, 16> values     = {};
    int                 colorIndex = 0;
    bool                hasData    = false;
};

//==============================================================================
class MorphoProcessor : public juce::AudioProcessor,
                        private juce::AsyncUpdater
{
public:
    MorphoProcessor();
    ~MorphoProcessor() override;

    //==========================================================================
    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock   (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
            && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName()        const override { return "MORPHO"; }
    bool  acceptsMidi()                 const override { return true;  }
    bool  producesMidi()                const override { return true;  }
    bool  isMidiEffect()                const override { return false; }   // Live doesn't host VST3 MIDI effects
    double getTailLengthSeconds()       const override { return 0.0;   }

    int  getNumPrograms()                         override { return 1; }
    int  getCurrentProgram()                      override { return 0; }
    void setCurrentProgram (int)                  override {}
    const juce::String getProgramName (int)       override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData)         override;
    void setStateInformation (const void* data, int sizeInBytes)   override;

    //==========================================================================
    // State – accessed from message thread
    std::array<ParameterSlot, 16> paramSlots;
    std::array<PresetSlot,    16> presets;
    int    globalChannel = 1;
    double morphTimeMs   = 0.0;
    MorphEngine::CurveMode curveMode { MorphEngine::CurveMode::LINEAR };
    int    activePreset  = -1;

    MorphEngine morphEngine;

    void savePreset   (int index);
    void clearPreset  (int index);
    void renamePreset (int index, const juce::String& name);
    void recallPreset (int index);
    void stopMorph();

    // Editor sets this to receive UI repaint notifications. Always called on the message thread.
    std::function<void()> onStateChanged;

private:
    // Tells the editor, on the message thread whichever thread the change came from
    // (hosts may restore state on another thread).
    void notifyStateChanged();
    void handleAsyncUpdate() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphoProcessor)
};
