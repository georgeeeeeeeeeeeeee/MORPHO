#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <mutex>

//==============================================================================
namespace
{
// The Lever channels this plug-in's instances use, so each new one takes a free channel in 9..15.
std::mutex channelLock;
std::array<int, 17> channelUsers {};

int claimChannel()
{
    const std::lock_guard<std::mutex> l (channelLock);
    int ch = 9;
    for (int c = 9; c <= 15; ++c) if (channelUsers[(size_t) c] == 0) { ch = c; break; }
    ++channelUsers[(size_t) ch];
    return ch;
}

void moveChannel (int from, int to)
{
    const std::lock_guard<std::mutex> l (channelLock);
    if (from >= 1 && from <= 16 && channelUsers[(size_t) from] > 0) --channelUsers[(size_t) from];
    if (to >= 1 && to <= 16) ++channelUsers[(size_t) to];
}
} // namespace

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

    syncRanges();
    leverChannel = claimChannel();
    for (auto& v : busSender.last) v = -1;
    busSender.startTimer (5);
    startTimerHz (10);
}

MorphoProcessor::~MorphoProcessor()
{
    busSender.stopTimer();
    stopTimer();
    moveChannel (leverChannel, 0);
    cancelPendingUpdate();
}

//==============================================================================
void MorphoProcessor::syncRanges()
{
    for (size_t i = 0; i < 16; ++i)
    {
        rangeLo[i] = juce::jlimit (0, 127, paramSlots[i].minValue);
        rangeHi[i] = juce::jlimit (0, 127, paramSlots[i].maxValue);
    }
}

void MorphoProcessor::setRange (int i, int lo, int hi)
{
    if (i < 0 || i >= 16) return;
    paramSlots[(size_t) i].minValue = juce::jlimit (0, 127, lo);
    paramSlots[(size_t) i].maxValue = juce::jlimit (0, 127, hi);
    syncRanges();
    // Resend outputs through the range (a zero-length morph to where the knobs already are).
    if (! morphEngine.isMorphing())
    {
        std::array<int, 16> v {};
        for (size_t k = 0; k < 16; ++k) v[k] = paramSlots[k].value;
        morphEngine.startMorph (v, paramSlots, globalChannel, 0.0);
    }
}

// Mapped slots go to Live as 14-bit (CC n + CC n+32), so even a narrow range moves smoothly.
void MorphoProcessor::BusSender::hiResTimerCallback()
{
    const auto vals = owner.morphEngine.getCurrentValues();
    const int ch = juce::jlimit (1, 16, owner.leverChannel) - 1;
    for (size_t i = 0; i < 16; ++i)
    {
        if (owner.link[i].load() != (int) Link::mapped) { last[i] = -1; continue; }
        const double lo = owner.rangeLo[i].load(), hi = owner.rangeHi[i].load();
        const double out = (lo + (hi - lo) * juce::jlimit (0, 127, vals[i]) / 127.0) / 127.0;
        const int q = juce::jlimit (0, 16383, juce::roundToInt (out * 16383.0));
        if (q == last[i]) continue;
        lever::Bus::get().send ((uint8_t) (0xB0 | ch), (uint8_t) i, (uint8_t) (q >> 7));
        lever::Bus::get().send ((uint8_t) (0xB0 | ch), (uint8_t) (i + 32), (uint8_t) (q & 127));
        last[i] = q;
    }
}

bool MorphoProcessor::leverConnected() const noexcept
{
    const double h = leverLink.lastHeardMs();
    return h > 0.0 && juce::Time::getMillisecondCounterHiRes() - h < 8000.0;
}

void MorphoProcessor::requestMap (int i)
{
    if (i < 0 || i >= 16) return;
    const int ch = leverChannel - 1;
    if (linkState (i) == Link::waiting)
    {
        lever::Bus::get().sendSysex (leverLink.cancel());
        link[(size_t) i] = beforeWait[(size_t) i];
        notifyStateChanged();
        return;
    }
    for (size_t k = 0; k < 16; ++k)
        if (link[k].load() == (int) Link::waiting) link[k] = beforeWait[k];
    beforeWait[(size_t) i] = link[(size_t) i].load();
    link[(size_t) i] = (int) Link::waiting;
    askedMs = juce::Time::getMillisecondCounterHiRes();
    silent = false;
    lever::Bus::get().sendSysex (leverLink.map (ch, i, true, "MORPHO: " + paramSlots[(size_t) i].name));
    notifyStateChanged();
}

void MorphoProcessor::unmapSlot (int i)
{
    if (i < 0 || i >= 16) return;
    lever::Bus::get().sendSysex (leverLink.unmap (leverChannel - 1, i));
    link[(size_t) i] = (int) Link::none;
    mappedNames[(size_t) i].clear();
    notifyStateChanged();
}

void MorphoProcessor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - helloMs > 3000.0)
    {
        helloMs = now;
        lever::Bus::get().sendSysex (leverLink.hello ("MORPHO"));
    }
    bool changed = false;
    // Follow: show what it moved to, and keep its table current (knob ranges, CC numbers, morph time).
    if (const int f = fired.exchange (-1); f >= 0 && f < 16) { activePreset = f; changed = true; }
    morphTimeMsAtomic = morphTimeMs;
    if (followOn.load()) syncFollow();
    const int ch = leverChannel - 1;
    for (const auto& r : leverLink.pop())
    {
        if (r.kind == "hi") { changed = true; continue; }
        if (r.channel != ch || r.cc < 0 || r.cc > 15) continue;
        const auto i = (size_t) r.cc;
        auto st = (Link) link[i].load();
        if (r.kind == "mapped")
        {
            st = Link::mapped;
            mappedNames[i] = r.text;
            // A slot still called "Param N" takes the name of what it now drives.
            if (paramSlots[i].name == "Param " + juce::String ((int) i + 1))
                paramSlots[i].name = r.text.fromLastOccurrenceOf (juce::CharPointer_UTF8 ("\xe2\x80\xba"), false, false).trim().substring (0, 16);
        }
        else if (r.kind == "unmapped") { if (st != Link::waiting) st = Link::none; mappedNames[i].clear(); }
        else if (r.kind == "lost") { st = Link::lost; mappedNames[i] = r.text; }
        else if (r.kind == "cancelled" && st == Link::waiting) st = (Link) beforeWait[i];
        link[i] = (int) st;
        changed = true;
    }
    if (silent && leverConnected() && leverLink.lastHeardMs() > askedMs) { silent = false; changed = true; }
    if (askedMs > 0.0 && now - askedMs > 1500.0)
    {
        if (leverLink.lastHeardMs() < askedMs)
        {
            silent = true;
            for (size_t k = 0; k < 16; ++k)
                if (link[k].load() == (int) Link::waiting) link[k] = beforeWait[k];
            changed = true;
        }
        askedMs = 0.0;
    }
    if (changed) notifyStateChanged();
}

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
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    morphEngine.prepare (sampleRate);
    syncFollow();
}

void MorphoProcessor::releaseResources() {}

void MorphoProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midiMessages)
{
    // Audio and incoming MIDI pass through untouched; our CC messages are added.
    runFollow (midiMessages, buffer.getNumSamples());
}

//==============================================================================
// Follow
//==============================================================================
void MorphoProcessor::setPresetWeight (int index, int weight)
{
    if (index < 0 || index >= 16) return;
    presets[(size_t) index].weight = juce::jlimit (0, 4, weight);
    syncFollow();
    notifyStateChanged();
}

void MorphoProcessor::syncFollow()
{
    std::array<std::array<int, 16>, 16> values {};
    for (size_t i = 0; i < 16; ++i)
    {
        values[i] = presets[i].values;
        weights[i] = presets[i].hasData ? presets[i].weight : 0;
    }
    morphEngine.setCurveMode (curveMode);
    morphEngine.setTable (values, paramSlots, globalChannel, morphTimeMs);
}

// The next preset after `current`, by the Pick rule and the weights (-1: none, or it stays).
int MorphoProcessor::pickNext (int cur) noexcept
{
    const int pick = followPick.load();
    if (pick == FollowSettings::next || pick == FollowSettings::previous)
    {
        const int dir = pick == FollowSettings::next ? 1 : -1;
        int i = cur < 0 ? (dir > 0 ? -1 : 16) : cur;
        for (int n = 0; n < 16; ++n)
        {
            i = (i + dir + 16) % 16;
            if (i != cur && weights[(size_t) i].load() > 0) return i;
        }
        return -1;
    }
    int total = 0;
    for (int i = 0; i < 16; ++i)
        if (pick == FollowSettings::any || i != cur) total += weights[(size_t) i].load();
    if (total <= 0) return -1;
    int r = rng.nextInt (total);
    for (int i = 0; i < 16; ++i)
    {
        if (pick != FollowSettings::any && i == cur) continue;
        r -= weights[(size_t) i].load();
        if (r < 0) return i == cur ? -1 : i;   // Any: picking the one playing means it stays
    }
    return -1;
}

// Phrase lines are every N bars from the song's bar 1. Each line is decided once, as soon as it's
// the next one: a chance roll, then a pick. "Start on the line" starts the morph on it; "arrive on
// the line" starts it the morph time earlier (at most one phrase), so it lands on the line. The
// morph starts on its exact sample; the block is processed in two parts around it.
void MorphoProcessor::runFollow (juce::MidiBuffer& midi, int n)
{
    juce::Optional<juce::AudioPlayHead::PositionInfo> pos;
    if (auto* ph = getPlayHead()) pos = ph->getPosition();
    const bool playing = followOn.load() && pos && pos->getIsPlaying() && pos->getPpqPosition() && pos->getBpm() && *pos->getBpm() > 1.0;
    if (! playing)
    {
        wasPlaying = false;
        planned = -1;
        nextBar = -1;
        morphEngine.process (midi, n);
        return;
    }
    const double ppq = *pos->getPpqPosition(), bpm = *pos->getBpm();
    const auto sig = pos->getTimeSignature().orFallback (juce::AudioPlayHead::TimeSignature {});
    const double bar = std::max (0.25, sig.numerator * 4.0 / std::max (1, sig.denominator));
    const double origin = pos->getPpqPositionOfLastBarStart() ? std::fmod (*pos->getPpqPositionOfLastBarStart(), bar) : 0.0;
    const double phrase = bar * FollowSettings::kEveryBars[juce::jlimit (0, 4, followEvery.load())];
    const double perSample = bpm / 60.0 / sr, end = ppq + n * perSample, eps = 1.0e-7;
    // Press play, or the transport jumped back (a loop, a click on the timeline): start fresh, and
    // a line exactly where play starts doesn't count (pressing play on bar 1 shouldn't change preset).
    if (! wasPlaying || ppq < lastPpq - eps) { playFrom = ppq; decided = -1.0e9; planned = -1; }
    wasPlaying = true;
    lastPpq = ppq;

    const double lead = followLand.load() == FollowSettings::arriveOnTheLine
                            ? std::min (morphTimeMsAtomic.load() * 0.001 * bpm / 60.0, phrase) : 0.0;
    // The next line whose start point (line - lead) is still ahead (or exactly now).
    double line = origin + std::ceil ((ppq + lead - origin) / phrase - eps) * phrase;
    if (line - lead <= playFrom + eps && ppq <= playFrom + eps) line += phrase;
    if (std::abs (line - decided) > eps)
    {
        decided = line;
        planned = -1;
        if (rng.nextInt (100) < juce::jlimit (0, 100, followChance.load()))
            planned = pickNext (current.load());
        nextBar = (int) std::llround ((line - origin) / bar) + 1;
        nextPreset = planned;
    }
    const double at = line - lead;
    if (planned >= 0 && at >= ppq - eps && at < end)
    {
        const int k = juce::jlimit (0, n, (int) std::llround ((at - ppq) / perSample));
        morphEngine.process (midi, k, 0);
        if (morphEngine.startFromTable (planned, midi, k))
        {
            current = planned;
            fired = planned;
            planned = -1;
            nextPreset = -1;
        }
        morphEngine.process (midi, n - k, k);
        return;
    }
    morphEngine.process (midi, n);
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
    syncFollow();

    notifyStateChanged();
}

void MorphoProcessor::clearPreset (int index)
{
    if (index < 0 || index >= 16) return;
    presets[(size_t) index] = PresetSlot {};
    if (activePreset == index) { activePreset = -1; current = -1; }
    syncFollow();
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
    current = index;
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
    state.setProperty (id ("leverChannel"),  leverChannel,  nullptr);
    state.setProperty (id ("followOn"),      followOn.load(),     nullptr);
    state.setProperty (id ("followEvery"),   followEvery.load(),  nullptr);
    state.setProperty (id ("followLand"),    followLand.load(),   nullptr);
    state.setProperty (id ("followChance"),  followChance.load(), nullptr);
    state.setProperty (id ("followPick"),    followPick.load(),   nullptr);

    for (int i = 0; i < 16; ++i)
    {
        juce::ValueTree slot ("Param");
        slot.setProperty (id ("name"),     paramSlots[i].name,     nullptr);
        slot.setProperty (id ("cc"),       paramSlots[i].ccNumber, nullptr);
        slot.setProperty (id ("channel"),  paramSlots[i].channel,  nullptr);
        slot.setProperty (id ("value"),    paramSlots[i].value,    nullptr);
        slot.setProperty (id ("min"),      paramSlots[i].minValue, nullptr);
        slot.setProperty (id ("max"),      paramSlots[i].maxValue, nullptr);
        if (linkState (i) == Link::mapped || linkState (i) == Link::lost)
            slot.setProperty (id ("mappedTo"), mappedNames[(size_t) i], nullptr);
        state.addChild (slot, -1, nullptr);
    }

    for (int i = 0; i < 16; ++i)
    {
        juce::ValueTree preset ("Preset");
        preset.setProperty (id ("name"),       presets[i].name,       nullptr);
        preset.setProperty (id ("colorIndex"), presets[i].colorIndex, nullptr);
        preset.setProperty (id ("hasData"),    presets[i].hasData,    nullptr);
        preset.setProperty (id ("weight"),     presets[i].weight,     nullptr);
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
    followOn      = (bool) state.getProperty (id ("followOn"), false);
    followEvery   = intProp (state, "followEvery", 2, 0, 4);
    followLand    = intProp (state, "followLand", 0, 0, 1);
    followChance  = intProp (state, "followChance", 100, 0, 100);
    followPick    = intProp (state, "followPick", 0, 0, 3);
    // The saved Lever channel, so the set's mappings (made on it) still apply.
    if (state.hasProperty (id ("leverChannel")))
    {
        const int ch = intProp (state, "leverChannel", leverChannel, 9, 15);
        moveChannel (leverChannel, ch);
        leverChannel = ch;
    }

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
            slot.minValue = intProp (child, "min",     0, 0, 127);
            slot.maxValue = intProp (child, "max",   127, 0, 127);
            // Shown until Lever confirms it (or reports it gone) a moment after the set opens.
            mappedNames[paramIdx] = child.getProperty (id ("mappedTo"), "").toString();
            link[paramIdx] = (int) (mappedNames[paramIdx].isEmpty() ? Link::none : Link::mapped);
            ++paramIdx;
        }
        else if (child.getType() == id ("Preset") && presetIdx < 16)
        {
            auto& preset      = presets[presetIdx];
            preset.name       = child.getProperty (id ("name"), "").toString().substring (0, 16);
            preset.colorIndex = intProp (child, "colorIndex", 0, 0, 7);
            preset.hasData    = (bool) child.getProperty (id ("hasData"), false);
            preset.weight     = intProp (child, "weight", 2, 0, 4);
            for (int j = 0; j < 16; ++j)
                preset.values[(size_t) j] = intProp (child, ("v" + juce::String (j)).toRawUTF8(), 0, 0, 127);
            ++presetIdx;
        }
    }

    syncRanges();
    if (activePreset >= 0 && ! presets[(size_t) activePreset].hasData)
        activePreset = -1;
    morphEngine.setCurveMode (curveMode);

    // Sync MorphEngine's current values to loaded param values
    // (MorphEngine starts with all zeros; we need to prime it so morphs start from correct values)
    std::array<int, 16> vals {};
    for (size_t i = 0; i < 16; ++i) vals[i] = paramSlots[i].value;
    // Trigger a snap (0ms) to the current values to prime the engine
    morphEngine.startMorph (vals, paramSlots, globalChannel, 0.0);
    current = activePreset;
    morphTimeMsAtomic = morphTimeMs;
    syncFollow();

    notifyStateChanged();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MorphoProcessor();
}
