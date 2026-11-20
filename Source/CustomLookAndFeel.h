#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

//==============================================================================
// Color palette
//==============================================================================
namespace MorphoColors
{
    // Brighter than 1.0's palette (George: "too dark to read"): text and outlines lifted, same look.
    inline const juce::Colour background  { 0xff121212 };
    inline const juce::Colour surface     { 0xff1e1e1e };
    inline const juce::Colour surfaceHigh { 0xff303030 };
    inline const juce::Colour accent      { 0xffe8ff00 };
    inline const juce::Colour dimText     { 0xffa6a6a6 };
    inline const juce::Colour brightText  { 0xfff2f2f2 };
    inline const juce::Colour trackBg     { 0xff3c3c3c };
    inline const juce::Colour warn        { 0xffff6b5e };

    inline const juce::Colour presetColors[8] {
        juce::Colour (0xffff4444),
        juce::Colour (0xffff8800),
        juce::Colour (0xffffdd00),
        juce::Colour (0xff44ff44),
        juce::Colour (0xff00ffff),
        juce::Colour (0xff4488ff),
        juce::Colour (0xffff44ff),
        juce::Colour (0xffffffff),
    };
}

//==============================================================================
class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel()
    {
        using namespace MorphoColors;

        setColour (juce::Slider::backgroundColourId,       trackBg);
        setColour (juce::Slider::thumbColourId,            accent);
        setColour (juce::Slider::trackColourId,            accent);
        setColour (juce::ComboBox::backgroundColourId,     surface);
        setColour (juce::ComboBox::outlineColourId,        surfaceHigh);
        setColour (juce::ComboBox::textColourId,           brightText);
        setColour (juce::ComboBox::arrowColourId,          dimText);
        setColour (juce::PopupMenu::backgroundColourId,    surface);
        setColour (juce::PopupMenu::textColourId,          brightText);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, surfaceHigh);
        setColour (juce::TextEditor::backgroundColourId,   surface);
        setColour (juce::TextEditor::textColourId,         accent);
        setColour (juce::TextEditor::outlineColourId,      accent);
        setColour (juce::TextEditor::highlightColourId,    accent.withAlpha (0.3f));
        setColour (juce::Label::textColourId,              brightText);
        setColour (juce::Label::backgroundColourId,        juce::Colours::transparentBlack);
        setColour (juce::TextButton::buttonColourId,       surface);
        setColour (juce::TextButton::buttonOnColourId,     accent.withAlpha (0.25f));
        setColour (juce::TextButton::textColourOffId,      dimText);
        setColour (juce::TextButton::textColourOnId,       accent);

        auto mono = juce::Font (juce::FontOptions()
                                    .withName ("Courier New")
                                    .withHeight (12.0f));
        (void) mono;
    }

    //==========================================================================
    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override
    {
        using namespace MorphoColors;

        auto radius   = static_cast<float> (juce::jmin (width, height)) * 0.5f - 4.0f;
        auto cx       = static_cast<float> (x) + width  * 0.5f;
        auto cy       = static_cast<float> (y) + height * 0.5f;
        auto angle    = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        auto rw       = radius * 2.0f;
        auto rx       = cx - radius;
        auto ry       = cy - radius;

        // Track background
        {
            juce::Path arc;
            arc.addArc (rx, ry, rw, rw, rotaryStartAngle, rotaryEndAngle, true);
            g.setColour (trackBg);
            g.strokePath (arc, juce::PathStrokeType (3.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Value arc
        if (sliderPos > 0.0f)
        {
            juce::Path arc;
            arc.addArc (rx, ry, rw, rw, rotaryStartAngle, angle, true);
            g.setColour (accent);
            g.strokePath (arc, juce::PathStrokeType (3.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Center fill
        g.setColour (surface);
        g.fillEllipse (cx - radius * 0.55f, cy - radius * 0.55f,
                       radius * 1.1f,       radius * 1.1f);

        // Pointer line
        {
            float len = radius * 0.65f;
            float px  = cx + std::sin (angle) * len;
            float py  = cy - std::cos (angle) * len;

            g.setColour (accent);
            g.drawLine (cx, cy, px, py, 2.0f);
        }

        // Center dot
        g.setColour (background);
        g.fillEllipse (cx - 2.5f, cy - 2.5f, 5.0f, 5.0f);
    }

    //==========================================================================
    void drawLinearSlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style,
                           juce::Slider& slider) override
    {
        if (style == juce::Slider::LinearHorizontal)
        {
            using namespace MorphoColors;
            float trackY  = y + height * 0.5f;
            float trackH  = 4.0f;

            // Track bg
            g.setColour (trackBg);
            g.fillRoundedRectangle ((float) x, trackY - trackH * 0.5f,
                                    (float) width, trackH, 2.0f);

            // Filled portion
            float filledW = sliderPos - x;
            if (filledW > 0)
            {
                g.setColour (accent);
                g.fillRoundedRectangle ((float) x, trackY - trackH * 0.5f,
                                        filledW, trackH, 2.0f);
            }

            // Thumb
            float thumbR = 7.0f;
            g.setColour (accent);
            g.fillEllipse (sliderPos - thumbR, trackY - thumbR, thumbR * 2, thumbR * 2);
            g.setColour (background);
            g.fillEllipse (sliderPos - 3.0f, trackY - 3.0f, 6.0f, 6.0f);
            return;
        }

        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height,
                                                 sliderPos, minSliderPos, maxSliderPos,
                                                 style, slider);
    }

    //==========================================================================
    void drawButtonBackground (juce::Graphics& g,
                                juce::Button& button,
                                const juce::Colour& backgroundColour,
                                bool isMouseOverButton, bool isButtonDown) override
    {
        using namespace MorphoColors;
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);

        juce::Colour fill = isButtonDown  ? backgroundColour.brighter (0.1f)
                          : isMouseOverButton ? backgroundColour.brighter (0.05f)
                          : backgroundColour;

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 3.0f);

        if (button.getToggleState())
        {
            g.setColour (accent);
            g.drawRoundedRectangle (bounds, 3.0f, 1.5f);
        }
        else
        {
            g.setColour (surfaceHigh);
            g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
        }
    }

    //==========================================================================
    juce::Font getLabelFont (juce::Label& l) override
    {
        return l.getFont();
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return getMonoFont (12.5f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override
    {
        return getMonoFont (12.0f).boldened();
    }

    // Menlo: a sturdier monospace than Courier New, which was thin and hard to read on black.
    // Sizes are scaled up a little to match.
    static juce::Font getMonoFont (float size)
    {
        return juce::Font (juce::FontOptions()
                               .withName ("Menlo")
                               .withHeight (size * 1.12f));
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CustomLookAndFeel)
};
