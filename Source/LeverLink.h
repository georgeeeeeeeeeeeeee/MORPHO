#pragma once
// The plug-in side of Lever, the Live script that maps by clicking (see ~/claude/lever-link).
// The plug-in sends SysEx requests on its own virtual MIDI port (the port Lever has as its Input);
// Lever answers on a local UDP port this class listens on. Shared by Dial, MORPHO and Pocket: keep the
// copies identical (~/claude/lever-link/plugin is the original).
//
// Threads: the requests are plain byte vectors (send them however the plug-in sends MIDI); replies are
// read on this class's own thread and collected with pop() on the message thread.

#include <juce_core/juce_core.h>

#include <cstdint>
#include <vector>

namespace lever
{
// The shared, always-present port: the IAC Driver bus named "Lever" (macOS; Audio MIDI Setup can
// make one, and Claude switched it on 20 Nov). Every Lever plug-in sends its requests and value CCs
// here, so Lever needs one Control Surface slot with Input "IAC Driver Lever", whichever plug-ins
// are open. Any thread; looks the bus up again if it wasn't there.
class Bus
{
public:
    static Bus& get();
    void send (uint8_t a, uint8_t b, uint8_t c);
    void sendSysex (const std::vector<uint8_t>& bytes);
    bool available();
    static constexpr const char* kName = "Lever";

private:
    Bus();
    void find();
    juce::CriticalSection lock_;
    uint32_t client_ = 0, port_ = 0, dest_ = 0;
    double lastLook_ = -1.0e9;
};

struct Reply
{
    juce::String kind;   // hi, waiting, mapped, unmapped, cancelled, lost, tempo
    int channel = -1;    // 0..15
    int cc = -1;
    juce::String text;   // the mapped parameter ("Bass > Auto Filter > Frequency") or the label
};

class Link final : private juce::Thread
{
public:
    Link();
    ~Link() override;

    int udpPort() const noexcept { return port_; }

    // SysEx requests (F0 ... F7). channel is 0..15.
    std::vector<uint8_t> hello (const juce::String& family) const;
    std::vector<uint8_t> map (int channel, int cc, bool hiRes, const juce::String& label) const;
    std::vector<uint8_t> unmap (int channel, int cc) const;
    std::vector<uint8_t> cancel() const;
    std::vector<uint8_t> tempo (int channel, int cc, bool hiRes, int minBpm, int maxBpm) const;

    std::vector<Reply> pop();                      // message thread
    double lastHeardMs() const noexcept { return heard_.load(); }   // Time::getMillisecondCounterHiRes, 0 = never

    static Reply parse (const juce::String& line);

private:
    void run() override;
    std::vector<uint8_t> head (int cmd) const;

    juce::DatagramSocket socket_ { false };
    int port_ = 0;
    juce::CriticalSection lock_;
    std::vector<Reply> queue_;
    std::atomic<double> heard_ { 0.0 };
};
} // namespace lever
