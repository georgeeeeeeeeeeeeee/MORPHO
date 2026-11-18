MORPHO @VERSION@ for Windows
============================

Store up to 16 snapshots of 16 MIDI CC values and glide between them, from a
snap to 30 seconds. For hardware synths, soft synths and anything that
listens to MIDI CC. Free and open source (GNU AGPLv3).

Needs: 64-bit Windows 10 or 11 and a 64-bit VST3 host such as Ableton Live.
No installer and no extra runtimes are needed.


1. INSTALL
----------
1. Right-click the zip and choose "Extract All...".
2. Copy the whole "MORPHO.vst3" folder into:

       C:\Program Files\Common Files\VST3

   Windows asks for administrator permission; click Continue.

Not showing up? Check that you copied the whole "MORPHO.vst3" folder (not
just the file inside it) and that your host is 64-bit.


2. USE IT IN ABLETON LIVE
-------------------------
1. Options > Settings (Preferences in older versions) > Plug-Ins: turn on
   "Use VST3 Plug-In System Folders", then click Rescan.
2. Load MORPHO on a MIDI track.
3. On a second MIDI track with your instrument, set "MIDI From" to the
   MORPHO track, and choose "MORPHO" in the box below it.
4. Arm or monitor the instrument track.

Then, in MORPHO:
- Give each of the 16 slots a CC number (double-click "CC 1") and a name.
- Turn the knobs until it sounds right, then right-click a preset button and
  choose "Save current values here".
- Set MORPH TIME and a CURVE, and click another preset to glide to it.
  STOP MORPH freezes it where it is.


LICENCE AND SOURCE
------------------
Copyright (c) 2026 Lever Studios. Licensed under the GNU Affero General
Public License v3.0; see LICENSE.txt. No warranty.
Source code: @SOURCE_URL@
