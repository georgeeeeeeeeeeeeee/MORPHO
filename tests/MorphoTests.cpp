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
        // Session round trip, and a damaged session
        MorphoProcessor a;
        a.globalChannel = 5; a.morphTimeMs = 2500.0; a.curveMode = MorphEngine::CurveMode::EASE;
        a.paramSlots[2].name = "Cutoff"; a.paramSlots[2].ccNumber = 74; a.paramSlots[2].value = 33; a.paramSlots[2].channel = 9;
        a.presets[4].hasData = true; a.presets[4].name = "Drop"; a.presets[4].values[2] = 120;
        juce::MemoryBlock mb;
        a.getStateInformation (mb);

        MorphoProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (b.globalChannel == 5 && b.morphTimeMs == 2500.0 && b.curveMode == MorphEngine::CurveMode::EASE, "global settings survive a save and load");
        check (b.paramSlots[2].name == "Cutoff" && b.paramSlots[2].ccNumber == 74 && b.paramSlots[2].value == 33 && b.paramSlots[2].channel == 9, "a slot survives a save and load");
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
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        check (ed != nullptr && ed->getWidth() == 820 && ed->getHeight() == 560, "the window opens at its full size");
        ed->setSize (410, 280);
        check (ed->getWidth() == 410, "the window resizes without trouble");
        ed->setSize (820, 560);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        check (img.isValid() && img.getWidth() == 1640, "the window draws");
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
