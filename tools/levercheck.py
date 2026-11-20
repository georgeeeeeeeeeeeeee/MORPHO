#!/usr/bin/env python3
"""Runs morpho-levercheck against the real Lever script (~/claude/lever-link/Lever) inside a fake Live.
Usage: tools/levercheck.py build-tests/MORPHO_LeverCheck_artefacts/Release/MORPHO_LeverCheck  -> exit 0 = all good."""
import os
import subprocess
import sys
import tempfile
import threading
import time

LEVER = os.path.expanduser("~/claude/lever-link/Lever")
DICE = os.path.expanduser("~/ArrangementIdeas")

# A fake Live, just enough for Lever: Live.MidiMap, _Framework.ControlSurface, a song with two tracks.
fake = tempfile.mkdtemp()
os.makedirs(os.path.join(fake, "_Framework"))
os.makedirs(os.path.join(fake, "pkg", "Lever"))
open(os.path.join(fake, "_Framework", "__init__.py"), "w").close()
open(os.path.join(fake, "_Framework", "ControlSurface.py"), "w").write('''
class ControlSurface:
    def __init__(self, c): self._c_instance = c; self.queue = []; self.messages = []
    def song(self): return self._c_instance.song
    def schedule_message(self, d, fn): self.queue.append(fn)
    def show_message(self, m): self.messages.append(m)
    def request_rebuild_midi_map(self): pass
    def build_midi_map(self, h): pass
    def receive_midi(self, b): pass
    def update_display(self): pass
    def disconnect(self): pass
''')
open(os.path.join(fake, "Live.py"), "w").write('''
class MidiMap:
    class MapMode: absolute = "abs"; absolute_14_bit = "abs14"
    maps = []
    @staticmethod
    def map_midi_cc(h, p, ch, cc, mode, avoid): MidiMap.maps.append((p.name, ch, cc, mode))
    @staticmethod
    def forward_midi_cc(*a): pass
''')
for src, names in ((LEVER, ("__init__.py", "Lever.py", "linkpath.py")), (os.path.join(DICE, "ArrangementDice"), ("ArrangementDice.py",)), (DICE, ("rules.py",))):
    for n in names:
        open(os.path.join(fake, "pkg", "Lever", n), "w").write(open(os.path.join(src, n)).read())
sys.path[:0] = [fake, os.path.join(fake, "pkg")]
import Live  # noqa: E402
from Lever import create_instance  # noqa: E402


class O:
    def __init__(s, name=None, parent=None, **kw):
        s.name, s.canonical_parent = name, parent
        s.__dict__.update(kw)


class View:
    def __init__(s): s.l, s.selected_parameter = [], None
    def add_selected_parameter_listener(s, f): s.l.append(f)
    def remove_selected_parameter_listener(s, f): s.l.remove(f)


class Song(O):
    signature_numerator = signature_denominator = 4
    def __init__(s):
        O.__init__(s)
        s.view, s.data, s.cue_points, s.tempo = View(), {}, [], 120.0
    def add_tracks_listener(s, f): pass
    def tracks_has_listener(s, f): return False
    def get_data(s, k, d): return s.data.get(k, d)
    def set_data(s, k, v): s.data[k] = v


song = Song()
def track(n):
    t = O(n, song, has_midi_input=False, arrangement_clips=[])
    t.mixer_device = O(None, t)
    t.mixer_device.volume = O("Track Volume", t.mixer_device)
    d = O("Auto Filter", t)
    d.parameters = [O("Device On", d), O("Frequency", d)]
    t.devices = [d]
    return t
song.tracks = [track("Drums"), track("Bass")]
song.return_tracks, song.master_track = [], track("Main")


class CI:
    song = song
    def handle(s): return 1


lever = create_instance(CI())
lock = threading.Lock()
proc = subprocess.Popen([sys.argv[1]], stdout=subprocess.PIPE, text=True)
lines, answering, live_maps = [], False, []
fails = []


def check(ok, what):
    print(("  ok    " if ok else "  FAIL  ") + what)
    if not ok:
        fails.append(what)


for line in proc.stdout:
    line = line.rstrip("\n")
    lines.append(line)
    if line == "PHASE2":
        answering = True
        check(any("[silent]" in l for l in lines), "no Lever: Dial notices nobody answered (status line tells you how to set it up)")
        check(lines[-2].startswith("STATE none"), "no Lever: the target stops waiting")
    elif line.startswith("SYSEX") and answering:
        b = tuple(int(x, 16) for x in line.split()[1:])
        lever.receive_midi(b)
        if b[4] == 2:   # MAP: George clicks Bass > Auto Filter > Frequency a moment later
            def click():
                time.sleep(0.3)
                song.view.selected_parameter = song.tracks[1].devices[0].parameters[1]
                for f in song.view.l:
                    f()
                while lever.queue:
                    lever.queue.pop(0)()
                Live.MidiMap.maps[:] = []
                lever.build_midi_map(0)
                live_maps.extend(Live.MidiMap.maps)
            threading.Thread(target=click).start()
    elif line == "END":
        break
proc.wait()

states = [l for l in lines if l.startswith("STATE")]
print("\n".join(states))
ch = int([l for l in lines if l.startswith("CHANNEL")][0].split()[1]) - 1
check(9 - 1 <= ch <= 15 - 1, "MORPHO uses a Lever channel in 9..15 (here %d), clear of Dial's 1..8" % (ch + 1))
check(any("[connected]" in l for l in states), "Lever answers HELLO: MORPHO shows it as connected")
check(any(l.startswith("STATE waiting") for l in states[2:]), "MAP: the slot waits for a click")
check(any(l.startswith("STATE mapped Bass \u203a Auto Filter \u203a Frequency") for l in states), "the click maps it: the slot shows Bass > Auto Filter > Frequency")
check(any("slot name: Frequency" in l for l in states), "a slot still called Param 3 takes the name Frequency")
check(("Frequency", ch, 2, "abs14") in live_maps, "Lever made a real Live mapping: ch %d, CC 2 + 34 (slot 3), Absolute 14-bit" % (ch + 1))
ccs = [l for l in lines if l.startswith("CC ")]
print("CCs on the bus after mapping:", ccs)
check("CC %d 2 100" % ch in ccs, "slot 3's value (100) goes out on the bus for Live")
check(not any(l.startswith("CC %d 5 " % ch) for l in ccs), "unmapped slots stay off the bus")
Live.MidiMap.maps[:] = []
lever.build_midi_map(0)
check(Live.MidiMap.maps == [], "Unmap removes the Live mapping")
print("morpho-levercheck: all passed" if not fails else "morpho-levercheck: %d failed" % len(fails))
sys.exit(1 if fails else 0)
