#include "LeverLink.h"

#if JUCE_MAC
 #include <CoreMIDI/CoreMIDI.h>
#endif

namespace lever
{
namespace
{
constexpr uint8_t kHead[] = { 0xF0, 0x7D, 0x4C, 0x56 };
enum { kHello = 1, kMap = 2, kUnmap = 3, kCancel = 4, kTempo = 5 };

void ascii (std::vector<uint8_t>& v, const juce::String& s)
{
    for (auto c : s.substring (0, 48))
        v.push_back ((uint8_t) (c >= 32 && c < 127 ? c : '?'));
}
} // namespace

// ---------------------------------------------------------------------------------------------- Bus
Bus& Bus::get()
{
    static Bus b;
    return b;
}

Bus::Bus()
{
   #if JUCE_MAC
    MIDIClientRef c = 0;
    MIDIPortRef p = 0;
    if (MIDIClientCreate (CFSTR ("Lever link"), nullptr, nullptr, &c) == noErr && MIDIOutputPortCreate (c, CFSTR ("out"), &p) == noErr)
    {
        client_ = c;
        port_ = p;
    }
   #endif
}

void Bus::find()   // under lock
{
   #if JUCE_MAC
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (dest_ != 0 || now - lastLook_ < 3000.0) return;
    lastLook_ = now;
    for (ItemCount i = 0; i < MIDIGetNumberOfDestinations(); ++i)
    {
        const auto d = MIDIGetDestination (i);
        CFStringRef n = nullptr;
        // "IAC Driver Lever" (or a bus called just "Lever" on another driver).
        if (MIDIObjectGetStringProperty (d, kMIDIPropertyDisplayName, &n) == noErr && n != nullptr)
        {
            const auto r = CFStringFind (n, CFSTR ("Lever"), kCFCompareCaseInsensitive | kCFCompareBackwards);
            const bool match = r.location != kCFNotFound && r.location + r.length == CFStringGetLength (n);
            CFRelease (n);
            if (match) { dest_ = d; return; }
        }
    }
   #endif
}

bool Bus::available()
{
    const juce::ScopedLock sl (lock_);
    find();
    return dest_ != 0;
}

void Bus::send (uint8_t a, uint8_t b, uint8_t c)
{
   #if JUCE_MAC
    const juce::ScopedLock sl (lock_);
    find();
    if (dest_ == 0 || port_ == 0) return;
    Byte buffer[64];
    auto* list = reinterpret_cast<MIDIPacketList*> (buffer);
    auto* pkt = MIDIPacketListInit (list);
    const Byte msg[3] = { a, b, c };
    pkt = MIDIPacketListAdd (list, sizeof (buffer), pkt, 0, 3, msg);
    if (pkt != nullptr && MIDISend (port_, dest_, list) != noErr) dest_ = 0;
   #else
    juce::ignoreUnused (a, b, c);
   #endif
}

void Bus::sendSysex (const std::vector<uint8_t>& bytes)
{
   #if JUCE_MAC
    const juce::ScopedLock sl (lock_);
    find();
    if (dest_ == 0 || port_ == 0 || bytes.empty() || bytes.size() > 400) return;
    Byte buffer[512];
    auto* list = reinterpret_cast<MIDIPacketList*> (buffer);
    auto* pkt = MIDIPacketListInit (list);
    pkt = MIDIPacketListAdd (list, sizeof (buffer), pkt, 0, (ByteCount) bytes.size(), bytes.data());
    if (pkt != nullptr && MIDISend (port_, dest_, list) != noErr) dest_ = 0;
   #else
    juce::ignoreUnused (bytes);
   #endif
}

// ---------------------------------------------------------------------------------------------- Link
Link::Link() : juce::Thread ("Lever link")
{
    if (socket_.bindToPort (0, "127.0.0.1"))
    {
        port_ = socket_.getBoundPort();
        startThread();
    }
}

Link::~Link()
{
    signalThreadShouldExit();
    socket_.shutdown();
    stopThread (1000);
}

std::vector<uint8_t> Link::head (int cmd) const
{
    std::vector<uint8_t> v (std::begin (kHead), std::end (kHead));
    v.push_back ((uint8_t) cmd);
    v.push_back ((uint8_t) ((port_ >> 14) & 127));
    v.push_back ((uint8_t) ((port_ >> 7) & 127));
    v.push_back ((uint8_t) (port_ & 127));
    return v;
}

std::vector<uint8_t> Link::hello (const juce::String& family) const
{
    auto v = head (kHello);
    ascii (v, family);
    v.push_back (0xF7);
    return v;
}

std::vector<uint8_t> Link::map (int channel, int cc, bool hiRes, const juce::String& label) const
{
    auto v = head (kMap);
    v.push_back ((uint8_t) (channel & 15));
    v.push_back ((uint8_t) (cc & 127));
    v.push_back (hiRes ? 1 : 0);
    ascii (v, label);
    v.push_back (0xF7);
    return v;
}

std::vector<uint8_t> Link::unmap (int channel, int cc) const
{
    auto v = head (kUnmap);
    v.push_back ((uint8_t) (channel & 15));
    v.push_back ((uint8_t) (cc & 127));
    v.push_back (0xF7);
    return v;
}

std::vector<uint8_t> Link::cancel() const
{
    auto v = head (kCancel);
    v.push_back (0xF7);
    return v;
}

std::vector<uint8_t> Link::tempo (int channel, int cc, bool hiRes, int minBpm, int maxBpm) const
{
    auto v = head (kTempo);
    v.push_back ((uint8_t) (channel & 15));
    v.push_back ((uint8_t) (cc & 127));
    v.push_back (hiRes ? 1 : 0);
    for (int b : { minBpm, maxBpm })
    {
        b = juce::jlimit (1, 999, b);
        v.push_back ((uint8_t) ((b >> 7) & 127));
        v.push_back ((uint8_t) (b & 127));
    }
    v.push_back (0xF7);
    return v;
}

Reply Link::parse (const juce::String& line)
{
    Reply r;
    auto rest = line.trim();
    r.kind = rest.upToFirstOccurrenceOf (" ", false, false);
    rest = rest.fromFirstOccurrenceOf (" ", false, false);
    if (r.kind == "hi") return r;
    r.channel = rest.upToFirstOccurrenceOf (" ", false, false).getIntValue();
    rest = rest.fromFirstOccurrenceOf (" ", false, false);
    r.cc = rest.upToFirstOccurrenceOf (" ", false, false).getIntValue();
    r.text = rest.containsChar (' ') ? rest.fromFirstOccurrenceOf (" ", false, false) : juce::String();
    return r;
}

std::vector<Reply> Link::pop()
{
    const juce::ScopedLock sl (lock_);
    std::vector<Reply> out;
    out.swap (queue_);
    return out;
}

void Link::run()
{
    char buf[1024];
    while (! threadShouldExit())
    {
        if (socket_.waitUntilReady (true, 200) != 1) continue;
        const int n = socket_.read (buf, (int) sizeof (buf) - 1, false);
        if (n <= 0) continue;
        const auto r = parse (juce::String::fromUTF8 (buf, n));
        heard_.store (juce::Time::getMillisecondCounterHiRes());
        const juce::ScopedLock sl (lock_);
        if (queue_.size() < 256) queue_.push_back (r);
    }
}
} // namespace lever
