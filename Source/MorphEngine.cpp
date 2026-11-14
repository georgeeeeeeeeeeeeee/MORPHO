#include "MorphEngine.h"

MorphEngine::MorphEngine()  = default;
MorphEngine::~MorphEngine() { stopTimer(); }

//==============================================================================
void MorphEngine::startMorph (const std::array<int, 16>& targets,
                               const std::array<ParameterSlot, 16>& slots,
                               int globalChannel,
                               double morphTimeMs)
{
    // Always called on message thread
    stopTimer();

    globalCh      = globalChannel;
    morphDuration = morphTimeMs;
    elapsedMs     = 0.0;

    for (int i = 0; i < 16; ++i)
    {
        startVals   [i] = static_cast<double> (currentVals[i]);
        targetVals  [i] = juce::jlimit (0, 127, targets[i]);
        lastSentVals[i] = currentVals[i];
        ccNums      [i] = slots[i].ccNumber;
        channels    [i] = slots[i].channel;
    }

    if (morphDuration <= 0.0)
    {
        // Snap: send all targets immediately
        for (int i = 0; i < 16; ++i)
        {
            int ch = (channels[i] == 0) ? globalCh : channels[i];
            int numFree = abstractFifo.getFreeSpace();
            if (numFree > 0)
            {
                int s1, b1, s2, b2;
                abstractFifo.prepareToWrite (1, s1, b1, s2, b2);
                if (b1 > 0) fifoData[s1] = { ch, ccNums[i], targetVals[i] };
                abstractFifo.finishedWrite (b1 + b2);
            }
            currentVals  [i] = targetVals[i];
            lastSentVals [i] = targetVals[i];
        }
        progress.store (1.0f, std::memory_order_relaxed);
        morphActive.store (false, std::memory_order_release);

        if (onMorphFinished)
            onMorphFinished (currentVals);

        return;
    }

    morphActive.store (true, std::memory_order_release);
    progress.store (0.0f, std::memory_order_relaxed);
    startTimerHz (60);
}

void MorphEngine::stopMorph()
{
    stopTimer();
    morphActive.store (false, std::memory_order_release);

    if (onMorphFinished)
        onMorphFinished (currentVals);
}

std::array<int, 16> MorphEngine::getCurrentValues() const
{
    return currentVals;   // message-thread-only read
}

bool MorphEngine::readNextMidiMessage (OutgoingMidi& out)
{
    // Called from audio thread
    if (abstractFifo.getNumReady() == 0)
        return false;

    int s1, b1, s2, b2;
    abstractFifo.prepareToRead (1, s1, b1, s2, b2);
    if (b1 > 0) out = fifoData[s1];
    abstractFifo.finishedRead (b1 + b2);
    return (b1 + b2) > 0;
}

//==============================================================================
void MorphEngine::timerCallback()
{
    constexpr double dt = 1000.0 / 60.0;
    elapsedMs += dt;

    double t  = juce::jlimit (0.0, 1.0, elapsedMs / morphDuration);
    double ct = applyCurve (t);

    for (int i = 0; i < 16; ++i)
    {
        int newVal = juce::roundToInt (startVals[i] + ct * (targetVals[i] - startVals[i]));
        newVal = juce::jlimit (0, 127, newVal);

        currentVals[i] = newVal;

        if (newVal != lastSentVals[i])
        {
            int ch = (channels[i] == 0) ? globalCh : channels[i];

            if (abstractFifo.getFreeSpace() > 0)
            {
                int s1, b1, s2, b2;
                abstractFifo.prepareToWrite (1, s1, b1, s2, b2);
                if (b1 > 0) fifoData[s1] = { ch, ccNums[i], newVal };
                abstractFifo.finishedWrite (b1 + b2);
            }
            lastSentVals[i] = newVal;
        }
    }

    progress.store (static_cast<float> (t), std::memory_order_relaxed);

    if (t >= 1.0)
    {
        stopTimer();
        morphActive.store (false, std::memory_order_release);

        if (onMorphFinished)
            onMorphFinished (currentVals);
    }
}

double MorphEngine::applyCurve (double t) const noexcept
{
    switch (curveMode)
    {
        case CurveMode::EASE:   return t * t * (3.0 - 2.0 * t);
        case CurveMode::SCURVE: return 0.5 - 0.5 * std::cos (t * juce::MathConstants<double>::pi);
        default:                return t;
    }
}
