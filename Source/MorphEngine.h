#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include "ParameterSlot.h"

struct OutgoingMidi
{
    int channel  = 1;
    int cc       = 0;
    int value    = 0;
};

//==============================================================================
// Runs on the JUCE message thread at 60 Hz.
// Pushes OutgoingMidi items into a lock-free FIFO that processBlock() drains.
//==============================================================================
class MorphEngine : public juce::Timer
{
public:
    enum class CurveMode { LINEAR, EASE, SCURVE };

    MorphEngine();
    ~MorphEngine() override;

    // Called from UI / message thread
    void startMorph (const std::array<int, 16>& targets,
                     const std::array<ParameterSlot, 16>& slots,
                     int globalChannel,
                     double morphTimeMs);

    void stopMorph();

    bool  isMorphing()       const noexcept { return morphActive.load (std::memory_order_acquire); }
    float getProgress()      const noexcept { return progress.load   (std::memory_order_relaxed); }
    void  setCurveMode (CurveMode m)        { curveMode = m; }

    // Returns copy of current interpolated values (message-thread-safe)
    std::array<int, 16> getCurrentValues() const;

    // Called from audio thread in processBlock()
    bool readNextMidiMessage (OutgoingMidi& out);

    // Invoked by MorphEngine when morph completes or is stopped
    std::function<void (const std::array<int, 16>&)> onMorphFinished;

private:
    void timerCallback() override;
    double applyCurve (double t) const noexcept;

    static constexpr int FIFO_SIZE = 512;

    juce::AbstractFifo        abstractFifo { FIFO_SIZE };
    std::array<OutgoingMidi, FIFO_SIZE> fifoData {};

    std::atomic<bool>  morphActive { false };
    std::atomic<float> progress    { 0.0f  };

    std::array<double, 16> startVals    {};
    std::array<int,    16> targetVals   {};
    std::array<int,    16> lastSentVals {};
    std::array<int,    16> currentVals  {};
    std::array<int,    16> ccNums       {};
    std::array<int,    16> channels     {};

    int    globalCh      = 1;
    double morphDuration = 0.0;
    double elapsedMs     = 0.0;

    CurveMode curveMode { CurveMode::LINEAR };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphEngine)
};
