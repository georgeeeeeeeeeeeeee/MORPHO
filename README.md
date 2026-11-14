# MORPHO

A MIDI CC snapshot recall plugin with morphing interpolation. VST3 only.

## Requirements

- **JUCE 8.x** (fetched automatically via CMake FetchContent — tag `8.0.4`)
- **CMake 3.22+**
- **C++17** compiler
  - macOS: Xcode 13+ / Apple Clang 13+
  - Windows: Visual Studio 2019+ (MSVC or Clang-CL)

---

## Build — macOS

```bash
# Clone the repository
git clone <repo-url> MORPHO
cd MORPHO

# Configure (Xcode generator recommended for code signing)
cmake -B build -G Xcode -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release

# The bundle will be at:
# build/MORPHO_artefacts/Release/VST3/MORPHO.vst3
```

Copy to your VST3 folder:
```bash
cp -r build/MORPHO_artefacts/Release/VST3/MORPHO.vst3 \
      ~/Library/Audio/Plug-Ins/VST3/
```

---

## Build — Windows

```bat
REM Configure
cmake -B build -G "Visual Studio 17 2022" -A x64

REM Build
cmake --build build --config Release

REM Bundle location:
REM build\MORPHO_artefacts\Release\VST3\MORPHO.vst3
```

Copy to your VST3 folder:
```
%COMMONPROGRAMFILES%\VST3\
```
or user-local:
```
%LOCALAPPDATA%\Programs\Common\VST3\
```

---

## Usage

Load **MORPHO** as a MIDI effect on a MIDI track in Ableton Live or Sequoia.

- **Parameter grid**: 16 slots, each with a name, CC number, current value knob, and channel override.
  - Double-click the name or CC label to edit.
  - Right-click a knob to set CC number via popup.
- **Preset bank**: 16 slots. Right-click to save; left-click to recall.
- **Morph time**: 0 ms ("SNAP") to 30 s. Set before recalling.
- **Curve**: LINEAR / EASE / S-CURVE interpolation.
- **STOP MORPH**: freezes interpolation at current values.

All state is saved with the DAW session via `getStateInformation` / `setStateInformation`.

---

## Architecture Notes

| Component | Thread | Role |
|-----------|--------|------|
| `MorphEngine` | Message thread (JUCE Timer 60 Hz) | Interpolation, writes to lock-free FIFO |
| `PluginProcessor::processBlock` | Audio thread | Drains FIFO, emits MIDI CC messages |
| `MorphoEditor` | Message thread (JUCE Timer 30 Hz) | UI refresh, knob/preset display |
