#pragma once
// DawLookAndFeel: the dark theme of DawLab 8 (Step 12).
//
// JUCE draws every standard widget through a LookAndFeel object. Instead of subclassing each
// slider, we derive from LookAndFeel_V4 once, set a colour scheme, and override the few drawing
// functions that make an audio app look like one:
//
//   drawRotarySlider   knobs: a dark disc, a value arc from the "zero" point, a pointer line.
//                      Bipolar knobs (pan, gain in dB around 0) draw the arc from the centre.
//   drawLinearSlider   faders: a thin slot and a flat cap with a line, like a mixing desk.
//   drawButtonBackground / getTextButtonFont: flat buttons with rounded corners.
//
// Colours live in one place (DawColours) so that custom components (meters, clips, the
// arrangement) use the same palette.

#include <juce_gui_basics/juce_gui_basics.h>

namespace dawlab::app {

struct DawColours {
    static juce::Colour background() { return juce::Colour(0xff1b1e23); }
    static juce::Colour panel() { return juce::Colour(0xff252930); }
    static juce::Colour panelLight() { return juce::Colour(0xff2f343c); }
    static juce::Colour outline() { return juce::Colour(0xff3a4049); }
    static juce::Colour text() { return juce::Colour(0xffd8dde3); }
    static juce::Colour textDim() { return juce::Colour(0xff8a929c); }
    static juce::Colour accent() { return juce::Colour(0xff4fc3f7); }     // cyan: values, playhead
    static juce::Colour accentWarm() { return juce::Colour(0xffffb74d); } // orange: solo, loop
    static juce::Colour mute() { return juce::Colour(0xffef5350); }
    static juce::Colour meterLow() { return juce::Colour(0xff66bb6a); }
    static juce::Colour meterMid() { return juce::Colour(0xffffee58); }
    static juce::Colour meterHigh() { return juce::Colour(0xffef5350); }

    // One colour per track, for clips and strip headers.
    static juce::Colour track(int index) {
        static const juce::uint32 colours[] = {0xffe57373, 0xffffb74d, 0xfffff176, 0xff81c784,
                                               0xff4dd0e1, 0xff7986cb, 0xffba68c8, 0xfff06292};
        return juce::Colour(colours[static_cast<size_t>(index) % (sizeof(colours) / sizeof(colours[0]))]);
    }
};

class DawLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    DawLookAndFeel();

    // Knobs whose arc starts in the middle (pan, +-dB): set slider.getProperties().set("bipolar", true).
    static constexpr const char* kBipolarProperty = "bipolar";

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                          float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;

    static juce::Font uiFont(float height, bool bold = false);
};

} // namespace dawlab::app
