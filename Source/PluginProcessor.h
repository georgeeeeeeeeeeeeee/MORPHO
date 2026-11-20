#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include "ParameterSlot.h"
#include "MorphEngine.h"
#include "LeverLink.h"

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
                        private juce::AsyncUpdater,
                        private juce::Timer
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

    // ── Click-to-map through the Lever script in Live (message thread) ──────────
    // Each slot can also drive any Live control: Map, then click the control in Live. MORPHO sends
    // the slot's value on the shared "IAC Driver Lever" bus (channel leverChannel, CC = slot index),
    // and Lever maps it there. A slot's own CC on MORPHO's MIDI out is unaffected.
    enum class Link { none, waiting, mapped, lost };
    void requestMap (int slot);           // again: cancel
    void unmapSlot (int slot);
    Link linkState (int slot) const noexcept { return (Link) link[(size_t) slot].load(); }
    juce::String mappedTo (int slot) const { return mappedNames[(size_t) slot]; }
    bool leverConnected() const noexcept;
    bool leverSilent() const noexcept { return silent; }
    int  leverChannel = 9;                // 9..15, one per MORPHO instance (Dial uses 1-8, Pocket 16)

    // Editor sets this to receive UI repaint notifications. Always called on the message thread.
    std::function<void()> onStateChanged;

private:
    // Tells the editor, on the message thread whichever thread the change came from
    // (hosts may restore state on another thread).
    void notifyStateChanged();
    void timerCallback() override;

    // Sends mapped slots' values on the Lever bus whenever they change (every 5 ms at most).
    struct BusSender final : juce::HighResolutionTimer
    {
        explicit BusSender (MorphoProcessor& o) : owner (o) {}
        void hiResTimerCallback() override;
        MorphoProcessor& owner;
        std::array<int, 16> last {};
    } busSender { *this };

    lever::Link leverLink;
    std::array<std::atomic<int>, 16> link {};
    std::array<int, 16> beforeWait {};
    std::array<juce::String, 16> mappedNames;
    double askedMs = 0.0, helloMs = 0.0;
    bool silent = false;
    void handleAsyncUpdate() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphoProcessor)
};
