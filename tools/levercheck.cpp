// morpho-levercheck: MORPHO's half of click-to-map, driven by tools/levercheck.py (the real Lever script
// in a fake Live). Prints the SysEx MORPHO sends on the IAC Lever bus and what slot 3 shows.
#include "PluginProcessor.h"

#include <iostream>

namespace
{
struct Catcher final : juce::MidiInputCallback
{
    juce::CriticalSection lock;
    std::vector<juce::MidiMessage> got;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override { const juce::ScopedLock sl (lock); got.push_back (m); }
    std::vector<juce::MidiMessage> take() { const juce::ScopedLock sl (lock); auto g = got; got.clear(); return g; }
};
const char* name (MorphoProcessor::Link l)
{
    using L = MorphoProcessor::Link;
    return l == L::waiting ? "waiting" : l == L::mapped ? "mapped" : l == L::lost ? "lost" : "none";
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto proc = std::make_unique<MorphoProcessor>();
    proc->prepareToPlay (48000.0, 960);
    juce::MidiDeviceInfo info;
    for (int t = 0; t < 40 && info.name.isEmpty(); ++t)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        for (const auto& d : juce::MidiInput::getAvailableDevices()) if (d.name.endsWithIgnoreCase ("Lever")) info = d;
    }
    Catcher c;
    auto in = juce::MidiInput::openDevice (info.identifier, &c);
    if (in == nullptr) { std::cout << "FAIL no IAC Driver Lever bus" << std::endl; return 1; }
    in->start();
    std::cout << "CHANNEL " << proc->leverChannel << std::endl;

    juce::String last;
    std::vector<juce::String> ccs;
    juce::AudioBuffer<float> buf (2, 960);
    const auto pump = [&] (double ms) {
        const double end = juce::Time::getMillisecondCounterHiRes() + ms;
        while (juce::Time::getMillisecondCounterHiRes() < end)
        {
            juce::MidiBuffer midi;
            proc->processBlock (buf, midi);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            for (const auto& m : c.take())
            {
                if (m.isSysEx())
                {
                    juce::String hex ("SYSEX");
                    for (int i = 0; i < m.getRawDataSize(); ++i) hex << " " << juce::String::toHexString (m.getRawData()[i]);
                    std::cout << hex << std::endl;
                }
                else if (m.isController())
                    ccs.push_back (juce::String (m.getChannel() - 1) + " " + juce::String (m.getControllerNumber()) + " " + juce::String (m.getControllerValue()));
            }
            const auto s = juce::String ("STATE ") + name (proc->linkState (2)) + " " + proc->mappedTo (2) + " | slot name: " + proc->paramSlots[2].name
                           + (proc->leverSilent() ? " [silent]" : "") + (proc->leverConnected() ? " [connected]" : "");
            if (s != last) { std::cout << s << std::endl; last = s; }
        }
    };

    pump (500);
    proc->requestMap (2);
    pump (2200);
    std::cout << "PHASE2" << std::endl;
    pump (3500);
    proc->requestMap (2);
    pump (2500);
    ccs.clear();
    std::array<int, 16> vals {};
    vals[2] = 100;
    vals[5] = 50;   // an unmapped slot: nothing on the bus for it
    proc->morphEngine.startMorph (vals, proc->paramSlots, proc->globalChannel, 0.0);
    pump (600);
    for (const auto& s : ccs) std::cout << "CC " << s << std::endl;
    proc->unmapSlot (2);
    pump (600);
    std::cout << "END" << std::endl;
    in->stop();
    return 0;
}
