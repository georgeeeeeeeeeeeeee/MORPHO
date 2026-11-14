cmake_minimum_required(VERSION 3.22)
project(MORPHO VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build universal binary" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)

# ── Fetch JUCE 8 ──────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ── Plugin target ─────────────────────────────────────────────────────────────
juce_add_plugin(MORPHO
    COMPANY_NAME                "Morpho Audio"
    BUNDLE_ID                   "com.morphoaudio.MORPHO"
    PLUGIN_MANUFACTURER_CODE    Mrph
    PLUGIN_CODE                 Mrph
    FORMATS                     VST3
    PRODUCT_NAME                "MORPHO"
    IS_MIDI_EFFECT              FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_SYNTH                    FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             "Fx" "Tools"
)

# ── Sources ───────────────────────────────────────────────────────────────────
target_sources(MORPHO PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/MorphEngine.cpp
)

target_include_directories(MORPHO PRIVATE Source)

# ── Compile definitions ───────────────────────────────────────────────────────
target_compile_definitions(MORPHO
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
)

# ── Link libraries ─────────────────────────────────────────────────────────────
target_link_libraries(MORPHO
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Ad-hoc sign the bundle so macOS will load it
if(APPLE)
    add_custom_command(TARGET MORPHO_VST3 POST_BUILD
        COMMAND xattr -cr "$<TARGET_BUNDLE_DIR:MORPHO_VST3>"
        COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:MORPHO_VST3>"
        VERBATIM
    )
endif()
