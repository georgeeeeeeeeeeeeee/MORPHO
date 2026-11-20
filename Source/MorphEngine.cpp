#include "MorphEngine.h"

namespace
{
    // How often the interpolation is evaluated within a block
    constexpr int kStepSamples = 32;
}

MorphEngine::MorphEngine()
{
    for (auto& v : currentVals) v.store (0, std::memory_order_relaxed);
    sentVals.fill (-1);   // nothing sent yet, so the first request sends every slot
    sentCc.fill (-1);
    sentCh.fill (-1);
    startTimerHz (30);
}

MorphEngine::~MorphEngine() { stopTimer(); }

//==============================================================================
// Message thread
//==============================================================================
void MorphEngine::startMorph (const std::array<int, 16>& targets,
                               const std::array<ParameterSlot, 16>& slots,
                               int globalChannel,
                               double morphTimeMs)
{
    Request r;
    r.durationMs = juce::jmax (0.0, morphTimeMs);
    r.curve      = curveMode.load (std::memory_order_relaxed);

    for (size_t i = 0; i < 16; ++i)
    {
        r.targets [i] = juce::jlimit (0, 127, targets[i]);
        r.ccNums  [i] = juce::jlimit (0, 127, slots[i].ccNumber);
        r.channels[i] = juce::jlimit (1, 16, slots[i].channel == 0 ? globalChannel : slots[i].channel);
        r.lows    [i] = juce::jlimit (0, 127, slots[i].minValue);
        r.highs   [i] = juce::jlimit (0, 127, slots[i].maxValue);
    }

    const bool snap = r.durationMs <= 0.0;

    {
        const juce::SpinLock::ScopedLockType lock (requestLock);
        pendingRequest = r;
        stopRequested.store (false, std::memory_order_release);
        hasPending.store (true, std::memory_order_release);
    }

    if (! snap)
    {
        progress.store (0.0f, std::memory_order_relaxed);
        morphActive.store (true, std::memory_order_release);
        return;
    }

    // Snap: the values are final straight away
    for (size_t i = 0; i < 16; ++i)
        currentVals[i].store (r.targets[i], std::memory_order_relaxed);

    progress.store (1.0f, std::memory_order_relaxed);
    morphActive.store (false, std::memory_order_release);

    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        if (onMorphFinished)
            onMorphFinished (r.targets);
    }
    else
    {
        // e.g. a session load on a host thread: report from the timer instead
        finishedPending.store (true, std::memory_order_release);
    }
}

void MorphEngine::stopMorph()
{
    {
        const juce::SpinLock::ScopedLockType lock (requestLock);
        hasPending.store (false, std::memory_order_release);
        stopRequested.store (true, std::memory_order_release);
    }

    morphActive.store (false, std::memory_order_release);
    finishedPending.store (false, std::memory_order_release);

    if (onMorphFinished)
        onMorphFinished (getCurrentValues());
}

std::array<int, 16> MorphEngine::getCurrentValues() const
{
    std::array<int, 16> out {};
    for (size_t i = 0; i < 16; ++i)
        out[i] = currentVals[i].load (std::memory_order_relaxed);
    return out;
}

void MorphEngine::timerCallback()
{
    if (finishedPending.exchange (false, std::memory_order_acq_rel))
        if (onMorphFinished)
            onMorphFinished (getCurrentValues());
}

//==============================================================================
// Audio thread
//==============================================================================
void MorphEngine::emitChanged (juce::MidiBuffer& midi, int samplePos) noexcept
{
    for (size_t i = 0; i < 16; ++i)
    {
        // The knob position (0–127) scaled into the slot's range.
        const int val = juce::roundToInt (active.lows[i] + (active.highs[i] - active.lows[i]) * liveVals[i] / 127.0);
        const int cc  = active.ccNums[i];
        const int ch  = active.channels[i];

        if (val != sentVals[i] || cc != sentCc[i] || ch != sentCh[i])
        {
            midi.addEvent (juce::MidiMessage::controllerEvent (ch, cc, val), samplePos);
            sentVals[i] = val;
            sentCc[i]   = cc;
            sentCh[i]   = ch;
        }
    }
}

void MorphEngine::process (juce::MidiBuffer& midi, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    if (stopRequested.exchange (false, std::memory_order_acq_rel))
        running = false;

    // Pick up a new request, if the message thread isn't in the middle of writing one
    if (hasPending.load (std::memory_order_acquire))
    {
        const juce::SpinLock::ScopedTryLockType lock (requestLock);

        if (lock.isLocked() && hasPending.exchange (false, std::memory_order_acq_rel))
        {
            active = pendingRequest;

            if (active.durationMs <= 0.0)
            {
                running  = false;
                liveVals = active.targets;
                emitChanged (midi, 0);
                // currentVals was already set by startMorph
                return;
            }

            for (size_t i = 0; i < 16; ++i)
                startVals[i] = (double) liveVals[i];

            elapsedSamples = 0.0;
            totalSamples   = juce::jmax (1.0, active.durationMs * 0.001 * sampleRate);
            running        = true;
            morphActive.store (true, std::memory_order_release);
        }
    }

    if (! running)
        return;

    for (int pos = 0; pos < numSamples && running; pos += kStepSamples)
    {
        const int step = juce::jmin (kStepSamples, numSamples - pos);
        elapsedSamples += step;

        const double t  = juce::jlimit (0.0, 1.0, elapsedSamples / totalSamples);
        const double ct = applyCurve (active.curve, t);

        for (size_t i = 0; i < 16; ++i)
        {
            const double v = startVals[i] + ct * ((double) active.targets[i] - startVals[i]);
            liveVals[i] = juce::jlimit (0, 127, juce::roundToInt (v));
        }

        emitChanged (midi, pos);

        if (t >= 1.0)
            running = false;
    }

    for (size_t i = 0; i < 16; ++i)
        currentVals[i].store (liveVals[i], std::memory_order_relaxed);

    progress.store ((float) juce::jlimit (0.0, 1.0, elapsedSamples / totalSamples),
                    std::memory_order_relaxed);

    if (! running && ! hasPending.load (std::memory_order_acquire))
    {
        morphActive.store (false, std::memory_order_release);
        finishedPending.store (true, std::memory_order_release);
    }
}

double MorphEngine::applyCurve (int curve, double t) noexcept
{
    switch ((CurveMode) curve)
    {
        case CurveMode::EASE:   return t * t * (3.0 - 2.0 * t);
        case CurveMode::SCURVE: return 0.5 - 0.5 * std::cos (t * juce::MathConstants<double>::pi);
        case CurveMode::LINEAR:
        default:                return t;
    }
}
