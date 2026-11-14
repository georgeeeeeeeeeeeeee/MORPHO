#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include "ParameterSlot.h"

//==============================================================================
// Interpolates 16 CC values towards a target set.
//
// The interpolation runs on the audio thread (process()), so morph timing
// follows the audio clock and doesn't stall when the UI is busy. The message
// thread hands over a morph request with startMorph(); a low-rate timer on the
// message thread reports completion through onMorphFinished.
//==============================================================================
class MorphEngine : private juce::Timer
{
public:
    enum class CurveMode { LINEAR, EASE, SCURVE };

    MorphEngine();
    ~MorphEngine() override;

    // ── Message thread ────────────────────────────────────────────────────────
    void startMorph (const std::array<int, 16>& targets,
                     const std::array<ParameterSlot, 16>& slots,
                     int globalChannel,
                     double morphTimeMs);

    void stopMorph();

    bool  isMorphing()       const noexcept { return morphActive.load (std::memory_order_acquire); }
    float getProgress()      const noexcept { return progress.load   (std::memory_order_relaxed); }
    void  setCurveMode (CurveMode m)        { curveMode.store ((int) m, std::memory_order_relaxed); }

    // Current interpolated values (safe from any thread)
    std::array<int, 16> getCurrentValues() const;

    // Invoked on the message thread when a morph completes or is stopped
    std::function<void (const std::array<int, 16>&)> onMorphFinished;

    // ── Audio thread ──────────────────────────────────────────────────────────
    void prepare (double newSampleRate) noexcept   { sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0; }
    void process (juce::MidiBuffer& midi, int numSamples) noexcept;

private:
    struct Request
    {
        std::array<int, 16> targets  {};
        std::array<int, 16> ccNums   {};
        std::array<int, 16> channels {};   // already resolved to 1–16
        double durationMs = 0.0;
        int    curve      = 0;
    };

    void timerCallback() override;
    static double applyCurve (int curve, double t) noexcept;
    void emitChanged (juce::MidiBuffer& midi, int samplePos) noexcept;

    // Hand-over from message thread to audio thread
    juce::SpinLock    requestLock;
    Request           pendingRequest;
    std::atomic<bool> hasPending    { false };
    std::atomic<bool> stopRequested { false };

    // Shared state
    std::atomic<bool>  morphActive     { false };
    std::atomic<float> progress        { 0.0f  };
    std::atomic<bool>  finishedPending { false };
    std::atomic<int>   curveMode       { 0 };
    std::array<std::atomic<int>, 16> currentVals;

    // Audio-thread state
    Request active;
    bool    running        = false;
    double  sampleRate     = 44100.0;
    double  elapsedSamples = 0.0;
    double  totalSamples   = 0.0;
    std::array<double, 16> startVals {};
    std::array<int, 16>    liveVals  {};
    std::array<int, 16>    sentVals  {};
    std::array<int, 16>    sentCc    {};
    std::array<int, 16>    sentCh    {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphEngine)
};
