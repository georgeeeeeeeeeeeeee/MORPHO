MORPHO @VERSION@ for macOS
==========================

Store up to 16 snapshots of 16 MIDI CC values and glide between them, from a
snap to 30 seconds. For hardware synths, soft synths and anything that
listens to MIDI CC. Free and open source (GNU AGPLv3).

Needs: macOS 12 or later (Apple silicon or Intel) and a VST3 host such as
Ableton Live.


1. INSTALL
----------
Copy "MORPHO.vst3" into your VST3 folder:

    ~/Library/Audio/Plug-Ins/VST3

In Finder: choose Go > Go to Folder..., paste the line above, press Return,
then drag "MORPHO.vst3" into that window. (If the VST3 folder doesn't exist,
create it.)

Do step 2 before you open your music software.


2. LET MACOS OPEN IT (one time only)
------------------------------------
This plugin is NOT notarised by Apple and is NOT signed with an Apple
Developer ID. It is a free release with an "ad hoc" signature, so the first
time it loads macOS blocks it with a message like "MORPHO.vst3 Not Opened"
or "Apple could not verify...". This is expected. Unblock it with ONE of
these:

  A) Terminal - quickest
     Open Terminal (Applications > Utilities > Terminal), paste this line and
     press Return:

       xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/MORPHO.vst3

     Nothing is printed when it works.

  B) No Terminal
     Open your music software and let it scan. When macOS says the plugin was
     not opened, click Done. Then open System Settings > Privacy & Security,
     scroll down to Security, and click "Open Anyway" next to the message
     about MORPHO. Confirm with your password, then quit and reopen your
     music software and rescan.

Only do this for software you trust. The full source code is public; see
the link at the end.


3. USE IT IN ABLETON LIVE
-------------------------
1. Live > Settings (Preferences in older versions) > Plug-Ins: turn on
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
