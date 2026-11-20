// MORPHO tests: the morph engine, presets and session restore, run headless.
//   cmake -B build-tests -DMORPHO_BUILD_TESTS=ON && cmake --build build-tests --target MORPHO_Tests
//   ./build-tests/MORPHO_Tests_artefacts/MORPHO_Tests   (exit code 0 = all passed)
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (! ok) ++failures;
}

struct Cc { int sample, channel, cc, value; };

std::vector<Cc> run (MorphoProcessor& p, int samples, int block = 512)
{
    std::vector<Cc> out;
    juce::AudioBuffer<float> audio (2, block);
    for (int done = 0; done < samples; done += block)
    {
        juce::MidiBuffer midi;
        audio.clear();
        p.processBlock (audio, midi);
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (msg.isController())
                out.push_back ({ done + m.samplePosition, msg.getChannel(), msg.getControllerNumber(), msg.getControllerValue() });
        }
    }
    return out;
}

std::array<int, 16> lastValues (const std::vector<Cc>& ccs, std::array<int, 16> start = {})
{
    for (const auto& c : ccs)
        if (c.cc >= 1 && c.cc <= 16) start[(size_t) c.cc - 1] = c.value;
    return start;
}

// Live's transport, for Follow: 4/4 at 120 BPM unless set otherwise; ppq moves on with each block.
struct Transport final : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 120.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setIsPlaying (playing);
        p.setPpqPosition (ppq);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        return p;
    }
};

std::vector<Cc> play (MorphoProcessor& p, Transport& t, int samples, double sr = 48000.0, int block = 512)
{
    std::vector<Cc> out;
    juce::AudioBuffer<float> audio (2, block);
    for (int done = 0; done < samples; done += block)
    {
        juce::MidiBuffer midi;
        audio.clear();
        p.processBlock (audio, midi);
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (msg.isController())
                out.push_back ({ done + m.samplePosition, msg.getChannel(), msg.getControllerNumber(), msg.getControllerValue() });
        }
        if (t.playing) t.ppq += block * t.bpm / 60.0 / sr;
    }
    return out;
}

// A processor with presets 1..n saved (preset k: every knob at 10 * (k + 1)), playing preset 1.
void withPresets (MorphoProcessor& p, int n)
{
    for (int k = 0; k < n; ++k)
    {
        p.presets[(size_t) k].hasData = true;
        p.presets[(size_t) k].name = "P" + juce::String (k + 1);
        p.presets[(size_t) k].values.fill (10 * (k + 1));
    }
    p.syncFollow();
    p.recallPreset (0);
}

void settle()   // let message-thread timers (morph finished) run
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    constexpr double sr = 48000.0;

    {
        MorphoProcessor p;
        p.prepareToPlay (sr, 512);
        auto first = run (p, 512);
        check (first.empty(), "nothing is sent until there is something to send");

        for (size_t i = 0; i < 16; ++i) p.paramSlots[i].value = (int) i * 8;
        p.morphEngine.startMorph ({ 0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120 }, p.paramSlots, 1, 0.0);
        auto snap = run (p, 512);
        check (snap.size() == 16, "the first snap sends all 16 CCs");
        check (std::all_of (snap.begin(), snap.end(), [] (const Cc& c) { return c.sample == 0 && c.channel == 1; }), "a snap lands at the start of the block, on the global channel");

        p.paramSlots[3].value = 99;
        std::array<int, 16> t {};
        for (size_t i = 0; i < 16; ++i) t[i] = p.paramSlots[i].value;
        p.morphEngine.startMorph (t, p.paramSlots, 1, 0.0);
        auto one = run (p, 512);
        check (one.size() == 1 && one[0].cc == 4 && one[0].value == 99, "turning one knob sends one CC");
    }

    {
        MorphoProcessor p;
        p.prepareToPlay (sr, 512);
        p.morphTimeMs = 1000.0;
        for (size_t i = 0; i < 16; ++i) p.presets[0].values[i] = 127;
        p.presets[0].hasData = true;
        p.recallPreset (0);
        check (p.morphEngine.isMorphing(), "recalling with a morph time starts a morph");

        auto half = run (p, 24064);
        auto mid = lastValues (half);
        check (mid[0] >= 60 && mid[0] <= 67, "linear morph is about halfway after half the time (got " + juce::String (mid[0]) + ")");

        // Saving mid-morph stores what is being sent, not the values from before the morph
        p.savePreset (1);
        check (std::abs (p.presets[1].values[0] - mid[0]) <= 1, "saving mid-morph saves the live values");

        auto rest = run (p, 26000);
        auto end = lastValues (rest, mid);
        check (std::all_of (end.begin(), end.end(), [] (int v) { return v == 127; }), "the morph reaches its targets");
        check (std::is_sorted (rest.begin(), rest.end(), [] (const Cc& a, const Cc& b) { return a.sample < b.sample; }), "CCs are in time order");
        settle();
        check (! p.morphEngine.isMorphing() && p.paramSlots[0].value == 127, "when it finishes, the knobs hold the target values");
        check (run (p, 4096).empty(), "nothing more is sent once the morph is over");
    }

    {
        MorphoProcessor p;
        p.prepareToPlay (sr, 512);
        p.morphTimeMs = 1000.0;
        p.curveMode = MorphEngine::CurveMode::SCURVE;
        for (size_t i = 0; i < 16; ++i) p.presets[0].values[i] = 127;
        p.presets[0].hasData = true;
        p.recallPreset (0);
        auto quarter = lastValues (run (p, 12032));
        check (quarter[0] < 32, "S-curve starts slower than linear (got " + juce::String (quarter[0]) + " at a quarter)");
        p.stopMorph();
        auto after = run (p, 4096);
        check (after.empty(), "Stop freezes the morph: nothing more is sent");
        settle();
        check (p.paramSlots[0].value == quarter[0], "Stop leaves the knobs where the morph was");
    }

    {
        // Turning a knob mid-morph takes over, and nothing pulls it back afterwards
        MorphoProcessor p;
        p.prepareToPlay (sr, 512);
        run (p, 1024);
        for (auto& v : p.presets[0].values) v = 127;
        p.presets[0].hasData = true;
        p.morphTimeMs = 2000.0;
        p.recallPreset (0);
        run (p, 24000);   // half way
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        juce::Slider* knob = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
            for (auto* ch : c.getChildren())
            {
                if (auto* sl = dynamic_cast<juce::Slider*> (ch); sl != nullptr && knob == nullptr && sl->getSliderStyle() == juce::Slider::Rotary) knob = sl;
                find (*ch);
            }
        };
        find (*ed);
        check (knob != nullptr && p.morphEngine.isMorphing(), "(set-up: mid-morph, found slot 1's knob)");
        if (knob != nullptr)
        {
            knob->setValue (10.0, juce::sendNotificationSync);
            check (! p.morphEngine.isMorphing(), "turning a knob mid-morph stops the morph");
            const auto sent = run (p, 48000 * 3);
            const auto after = lastValues (sent, std::array<int, 16> { 10 });
            settle();
            run (p, 2048);
            check (after[0] == 10 && p.paramSlots[0].value == 10 && (int) knob->getValue() == 10,
                   "the knob stays where it was put (sent " + juce::String (after[0]) + ", slot " + juce::String (p.paramSlots[0].value) + ")");
            knob->setValue (90.0, juce::sendNotificationSync);
            run (p, 2048);
            settle();
            check (p.paramSlots[0].value == 90 && (int) knob->getValue() == 90, "after a finished morph, the knob moves freely");
        }
        ed.reset();
    }

    {
        // Range: the knob covers only Min..Max (presets keep knob positions)
        MorphoProcessor p;
        p.prepareToPlay (sr, 512);
        run (p, 1024);
        p.setRange (0, 40, 80);
        std::array<int, 16> v {};
        v[0] = 127;
        p.morphEngine.startMorph (v, p.paramSlots, p.globalChannel, 0.0);
        auto top = lastValues (run (p, 1024));
        v[0] = 0;
        p.morphEngine.startMorph (v, p.paramSlots, p.globalChannel, 0.0);
        auto bottom = lastValues (run (p, 1024));
        v[0] = 64;
        p.morphEngine.startMorph (v, p.paramSlots, p.globalChannel, 0.0);
        auto mid = lastValues (run (p, 1024));
        check (top[0] == 80 && bottom[0] == 40 && std::abs (mid[0] - 60) <= 1,
               "range 40-80: knob 127 sends 80, 0 sends 40, 64 sends 60 (got " + juce::String (top[0]) + ", " + juce::String (bottom[0]) + ", " + juce::String (mid[0]) + ")");
        p.paramSlots[0].value = 127;
        p.setRange (0, 0, 100);
        check (lastValues (run (p, 1024))[0] == 100, "changing the range resends the slot through the new range");
        settle();
    }

    {
        // Session round trip, and a damaged session
        MorphoProcessor a;
        a.globalChannel = 5; a.morphTimeMs = 2500.0; a.curveMode = MorphEngine::CurveMode::EASE;
        a.paramSlots[2].name = "Cutoff"; a.paramSlots[2].ccNumber = 74; a.paramSlots[2].value = 33; a.paramSlots[2].channel = 9;
        a.paramSlots[2].minValue = 20; a.paramSlots[2].maxValue = 90;
        a.presets[4].hasData = true; a.presets[4].name = "Drop"; a.presets[4].values[2] = 120;
        juce::MemoryBlock mb;
        a.getStateInformation (mb);

        MorphoProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (b.globalChannel == 5 && b.morphTimeMs == 2500.0 && b.curveMode == MorphEngine::CurveMode::EASE, "global settings survive a save and load");
        check (b.paramSlots[2].name == "Cutoff" && b.paramSlots[2].ccNumber == 74 && b.paramSlots[2].value == 33 && b.paramSlots[2].channel == 9
               && b.paramSlots[2].minValue == 20 && b.paramSlots[2].maxValue == 90, "a slot (with its range) survives a save and load");
        check (b.presets[4].hasData && b.presets[4].name == "Drop" && b.presets[4].values[2] == 120, "a preset survives a save and load");

        juce::ValueTree bad ("MorphoState");
        bad.setProperty ("globalChannel", 99, nullptr);
        bad.setProperty ("morphTimeMs", -50.0, nullptr);
        bad.setProperty ("curveMode", 7, nullptr);
        bad.setProperty ("activePreset", 40, nullptr);
        juce::ValueTree slot ("Param");
        slot.setProperty ("cc", 300, nullptr); slot.setProperty ("value", -4, nullptr); slot.setProperty ("channel", 30, nullptr);
        bad.addChild (slot, -1, nullptr);
        juce::MemoryOutputStream os;
        bad.writeToStream (os);
        MorphoProcessor c;
        c.setStateInformation (os.getData(), (int) os.getDataSize());
        check (c.globalChannel == 16 && c.morphTimeMs == 0.0 && (int) c.curveMode == 2 && c.activePreset == -1, "a damaged session's settings are clamped");
        check (c.paramSlots[0].ccNumber == 127 && c.paramSlots[0].value == 0 && c.paramSlots[0].channel == 16, "a damaged session's slot values are clamped");

        const char junk[] = "not a session";
        MorphoProcessor d;
        d.setStateInformation (junk, (int) sizeof (junk));
        check (d.globalChannel == 1 && d.paramSlots[0].ccNumber == 1, "garbage state is ignored");
    }

    // ── Follow ────────────────────────────────────────────────────────────────
    {
        MorphoProcessor p;
        p.prepareToPlay (sr, 512);
        Transport t;
        p.setPlayHead (&t);
        withPresets (p, 2);
        p.followOn = true; p.followEvery = 0; p.followChance = 100; p.followPick = FollowSettings::next;
        p.syncFollow();
        auto ccs = play (p, t, 48000 * 5);   // 2.5 bars at 120
        std::vector<Cc> cc1;
        for (const auto& c : ccs) if (c.cc == 1) cc1.push_back (c);
        check (! cc1.empty() && cc1[0].sample == 0 && cc1[0].value == 10, "(set-up: preset 1 snapped in at the start)");
        check (cc1.size() >= 3 && cc1[1].sample == 96000 && cc1[1].value == 20, "Follow: on bar 2's line (sample 96000, mid-block) it moves to the next preset");
        check (cc1.size() >= 3 && cc1[2].sample == 192000 && cc1[2].value == 10, "Follow: Next wraps round at bar 3");
        check (cc1.size() == 3, "Follow: nothing else moves, and pressing play on bar 1 doesn't count as a line");
        check (p.followNextBar() == 4, "Follow: it says which bar is next (bar 4)");
        p.setPlayHead (nullptr);
    }
    {
        // Ends on the line: a 1-beat morph (500 ms at 120) starts a beat before the line and lands on it.
        MorphoProcessor p;
        p.prepareToPlay (sr, 512);
        Transport t;
        p.setPlayHead (&t);
        withPresets (p, 2);
        p.morphTimeMs = 500.0;
        p.recallPreset (0);
        play (p, t, 512);   // let the (500 ms) recall run from 0 ...
        t.ppq = 0.0;
        play (p, t, 24000);   // ... and finish
        t.ppq = 0.0;
        t.playing = false;
        play (p, t, 512);
        t.playing = true;
        p.followOn = true; p.followEvery = 0; p.followChance = 100; p.followPick = FollowSettings::next;
        p.followLand = FollowSettings::arriveOnTheLine;
        p.syncFollow();
        p.timerCallbackForTests();
        auto ccs = play (p, t, 120000);
        std::vector<Cc> cc1;
        for (const auto& c : ccs) if (c.cc == 1) cc1.push_back (c);
        // 10 -> 20 linear over 24000 samples from 72000: 11 is first sent 5% in (73200), 20 at 95% (94800);
        // the engine steps every 32 samples.
        check (! cc1.empty() && cc1.front().value == 11 && std::abs (cc1.front().sample - 73200) <= 32, "Follow, ends on the line: the morph starts a beat early (11 at " + juce::String (cc1.empty() ? -1 : cc1.front().sample) + ")");
        check (! cc1.empty() && cc1.back().value == 20 && std::abs (cc1.back().sample - 94800) <= 32, "Follow, ends on the line: it arrives on the line (20 at " + juce::String (cc1.empty() ? -1 : cc1.back().sample) + ")");
        check (! p.morphEngine.isMorphing(), "Follow, ends on the line: the morph is over by the line");
        p.setPlayHead (nullptr);
    }
    {
        MorphoProcessor p;
        p.prepareToPlay (sr, 512);
        Transport t;
        p.setPlayHead (&t);
        withPresets (p, 3);
        play (p, t, 512);
        p.followOn = true; p.followEvery = 0; p.followChance = 0;
        p.syncFollow();
        check (play (p, t, 48000 * 16).empty(), "Follow: Chance 0% never changes");
        p.followOn = false; p.followChance = 100;
        check (play (p, t, 48000 * 16).empty(), "Follow off: nothing changes");
        p.followOn = true;
        t.playing = false;
        check (play (p, t, 48000 * 16).empty(), "Follow: nothing happens with the transport stopped");
        p.setPlayHead (nullptr);
    }
    {
        // Any other, by weight: preset 3 set to Never is never picked; the others both come up.
        MorphoProcessor p;
        p.prepareToPlay (sr, 512);
        Transport t;
        p.setPlayHead (&t);
        withPresets (p, 3);
        p.setFollowSeed (7);
        p.setPresetWeight (2, 0);
        p.followOn = true; p.followEvery = 0; p.followChance = 100; p.followPick = FollowSettings::anyOther;
        p.syncFollow();
        auto ccs = play (p, t, 48000 * 2 * 40);   // 40 bars
        std::array<int, 4> seen {};
        int changes = 0;
        for (const auto& c : ccs) if (c.cc == 1 && c.sample > 0) { ++changes; seen[(size_t) juce::jlimit (0, 3, c.value / 10 - 1)]++; }
        check (seen[2] == 0, "Follow: a preset set to Never is never picked");
        check (changes == 39, "Follow, any other: every line changes preset (2 to pick from, never the same twice) (got " + juce::String (changes) + ")");
        check (seen[0] > 0 && seen[1] > 0, "Follow: the others are both picked");
        p.setPlayHead (nullptr);
    }
    {
        // Every 4 bars, in a loop: the line is decided again after the loop jumps back.
        MorphoProcessor p;
        p.prepareToPlay (sr, 512);
        Transport t;
        p.setPlayHead (&t);
        withPresets (p, 2);
        p.followOn = true; p.followEvery = 2; p.followChance = 100; p.followPick = FollowSettings::next;
        p.syncFollow();
        auto ccs = play (p, t, 48000 * 9);   // 4.5 bars: one line, bar 5 (sample 384000)
        int at = -1;
        for (const auto& c : ccs) if (c.cc == 1 && c.sample > 0) at = c.sample;
        check (at == 384000, "Follow: every 4 bars changes on bar 5's line (got " + juce::String (at) + ")");
        t.ppq = 0.0;   // the loop jumps back
        auto again = play (p, t, 48000 * 9);
        int at2 = -1;
        for (const auto& c : again) if (c.cc == 1 && c.sample > 0) at2 = c.sample;
        check (at2 == 384000, "Follow: after a jump back it changes on the line again");
        p.setPlayHead (nullptr);
    }
    {
        MorphoProcessor a;
        withPresets (a, 2);
        a.followOn = true; a.followEvery = 4; a.followLand = 1; a.followChance = 40; a.followPick = 3;
        a.setPresetWeight (1, 4);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        MorphoProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (b.followOn.load() && b.followEvery.load() == 4 && b.followLand.load() == 1 && b.followChance.load() == 40 && b.followPick.load() == 3,
               "Follow settings survive a save and load");
        check (b.presets[1].weight == 4 && b.presets[0].weight == 2, "preset weights survive a save and load");
    }

    {
        MorphoProcessor p;
        p.presets[3].hasData = true; p.presets[3].name = "X";
        p.activePreset = 3;
        p.clearPreset (3);
        check (! p.presets[3].hasData && p.activePreset == -1, "clearing a preset empties it and deselects it");
        p.presets[2].hasData = true;
        p.renamePreset (2, "   a very long preset name indeed  ");
        check (p.presets[2].name == "a very long pres", "names are trimmed to 16 characters");
        p.renamePreset (2, "   ");
        check (p.presets[2].name == "a very long pres", "a blank name is ignored");
    }

    {
        // The window opens, lays out and draws (it used to crash on opening). Writes a
        // screenshot when MORPHO_SNAPSHOT is set to a .png path.
        MorphoProcessor p;
        p.presets[0].hasData = true; p.presets[0].name = "Intro"; p.presets[0].colorIndex = 2;
        p.presets[1].hasData = true; p.presets[1].name = "Drop";  p.presets[1].colorIndex = 5;
        p.activePreset = 1;
        p.paramSlots[0].name = "Cutoff"; p.paramSlots[0].ccNumber = 74; p.paramSlots[0].value = 90;
        p.paramSlots[1].name = "Resonance"; p.paramSlots[1].ccNumber = 71; p.paramSlots[1].value = 40;
        p.paramSlots[0].minValue = 30; p.paramSlots[0].maxValue = 100;
        p.presets[0].hasData = true; p.presets[0].name = "Preset 1";
        p.followOn = true;   // draw the Follow row in use
        p.followChance = 75;
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        check (ed != nullptr && ed->getWidth() == 820 && ed->getHeight() == 620, "the window opens at its full size");
        ed->setSize (410, 310);
        check (ed->getWidth() == 410, "the window resizes without trouble");
        ed->setSize (820, 620);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        check (img.isValid() && img.getWidth() == 1640, "the window draws");
        if (auto* path = std::getenv ("MORPHO_SNAPSHOT"))   // MORPHO_SNAPSHOT=ui.png: save the picture
        {
            juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (path));
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
        }
        if (auto path = juce::SystemStats::getEnvironmentVariable ("MORPHO_SNAPSHOT", {}); path.isNotEmpty())
        {
            juce::File f (path);
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
        }
    }

    std::cout << (failures == 0 ? "\nAll tests passed\n" : "\nSome tests FAILED\n");
    return failures == 0 ? 0 : 1;
}
