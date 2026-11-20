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
    int                 weight     = 2;   // Follow: how often it's picked (0 never, 1 less, 2 normal, 4 more)
};

// Follow: like Live's follow actions, for presets. At the end of each phrase (every N bars) it may
// morph to another preset, picked by chance and each preset's weight.
struct FollowSettings
{
    static constexpr int kEveryBars[] = { 1, 2, 4, 8, 16 };
    enum Land { onTheLine = 0, arriveOnTheLine = 1 };     // the morph starts on the bar line / ends on it
    enum Pick { anyOther = 0, any = 1, next = 2, previous = 3 };
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
    void setRange (int slot, int lo, int hi);   // message thread: also resends the slot's output

    // ── Follow (message thread writes, the audio thread reads) ─────────────────
    std::atomic<bool> followOn { false };
    std::atomic<int>  followEvery { 2 };         // index into FollowSettings::kEveryBars (default 4 bars)
    std::atomic<int>  followLand { 0 };          // FollowSettings::Land
    std::atomic<int>  followChance { 100 };      // % chance of a change at each phrase
    std::atomic<int>  followPick { 0 };          // FollowSettings::Pick
    void setPresetWeight (int index, int weight);
    void syncFollow();                           // after presets or settings change: refresh what the audio thread uses
    // What's coming (for the editor): the bar number of the next phrase line (-1: none), and the preset
    // it goes to there (-1: it stays).
    int followNextBar() const noexcept { return nextBar.load(); }
    int followNextPreset() const noexcept { return nextPreset.load(); }
    void setFollowSeed (uint32_t s) noexcept { rng.setSeed ((juce::int64) s); }   // tests
    void timerCallbackForTests() { timerCallback(); }

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
    std::array<std::atomic<int>, 16> rangeLo {}, rangeHi {};   // copies for the bus thread
    void syncRanges();

    // Follow, audio thread.
    void runFollow (juce::MidiBuffer&, int numSamples);
    int pickNext (int current) noexcept;
    std::array<std::atomic<int>, 16> weights {};   // 0 for empty presets
    std::atomic<int> current { -1 };               // the preset playing (either thread sets it)
    std::atomic<int> fired { -1 };                 // Follow moved to this preset (the timer picks it up)
    std::atomic<int> nextBar { -1 }, nextPreset { -1 };
    std::atomic<double> morphTimeMsAtomic { 0.0 };   // the morph time, for the audio thread
    double decided = -1.0e9, lastPpq = -1.0e9, playFrom = 0.0, sr = 44100.0;
    int planned = -1;
    bool wasPlaying = false;
    juce::Random rng;

    lever::Link leverLink;
    std::array<std::atomic<int>, 16> link {};
    std::array<int, 16> beforeWait {};
    std::array<juce::String, 16> mappedNames;
    double askedMs = 0.0, helloMs = 0.0;
    bool silent = false;
    void handleAsyncUpdate() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphoProcessor)
};
