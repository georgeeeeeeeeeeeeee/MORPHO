# Changelog

All notable changes to MORPHO are recorded here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning: [SemVer](https://semver.org/).

## [1.0.0] - 2026-11-21

First public release, VST3 for macOS 12+ (universal: Apple silicon and Intel) and 64-bit Windows 10/11.

- 16 CC slots, each with a name, CC number and MIDI channel (or the global channel)
- 16 presets: save, recall, rename and clear
- Morphs from an instant snap to 30 seconds, linear, eased or S-curve, timed by the audio clock
  and placed sample-accurately inside each block
- Only changed values are sent
- Each slot has its own range: the lowest and highest value its knob sends
- Touching a knob mid-morph takes it over from the morph
- Follow: on phrase lines (every 1, 2, 4, 8 or 16 bars), with a chance, MORPHO moves on to
  another preset by itself (any other, any, next or previous, with a weight per preset)
- Click-to-map in Ableton Live: click Map, then click a Live control. This needs the Lever
  control script, which isn't released yet; without it, MORPHO works as before
- Everything is saved with your project
- Every build is checked with pluginval at its strictest level

The macOS plugin is ad-hoc signed and not notarised, so macOS asks you to allow it once; see the
install guide.

### Fixed since the private build of April 2026
- The plugin window could crash when it opened.
- Right-clicking a preset overwrote it at once; it now opens a menu and asks before replacing.
- Closing the plugin window while a dialog was open could crash.
- Saving a preset during a morph saved the values from before the morph.
- A damaged session could produce out-of-range channels, CCs or values; everything is now
  checked on load.
- Opening a session with the window already open left the morph time, curve and channel showing
  old values.
