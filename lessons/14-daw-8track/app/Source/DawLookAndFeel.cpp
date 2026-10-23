#include "DawLookAndFeel.h"

#include <cmath>

namespace dawlab::app {

DawLookAndFeel::DawLookAndFeel() {
    // LookAndFeel_V4 is driven by a ColourScheme of 9 roles; then a few widget colours on top.
    setColourScheme({DawColours::background(), // windowBackground
                     DawColours::panel(),      // widgetBackground
                     DawColours::panel(),      // menuBackground
                     DawColours::outline(),    // outline
                     DawColours::text(),       // defaultText
                     DawColours::panelLight(), // defaultFill
                     DawColours::background(), // highlightedText
                     DawColours::accent(),     // highlightedFill
                     DawColours::text()});     // menuText

    setColour(juce::ResizableWindow::backgroundColourId, DawColours::background());
    setColour(juce::DocumentWindow::textColourId, DawColours::text());
    setColour(juce::TextButton::buttonColourId, DawColours::panelLight());
    setColour(juce::TextButton::buttonOnColourId, DawColours::accent());
    setColour(juce::TextButton::textColourOffId, DawColours::text());
    setColour(juce::TextButton::textColourOnId, DawColours::background());
    setColour(juce::Slider::rotarySliderFillColourId, DawColours::accent());
    setColour(juce::Slider::rotarySliderOutlineColourId, DawColours::outline());
    setColour(juce::Slider::thumbColourId, DawColours::text());
    setColour(juce::Slider::trackColourId, DawColours::accent());
    setColour(juce::Slider::backgroundColourId, DawColours::background());
    setColour(juce::Slider::textBoxTextColourId, DawColours::text());
    setColour(juce::Slider::textBoxBackgroundColourId, DawColours::background());
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, DawColours::text());
    setColour(juce::ComboBox::backgroundColourId, DawColours::panelLight());
    setColour(juce::ComboBox::outlineColourId, DawColours::outline());
    setColour(juce::ComboBox::textColourId, DawColours::text());
    setColour(juce::ComboBox::arrowColourId, DawColours::textDim());
    setColour(juce::PopupMenu::backgroundColourId, DawColours::panel());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, DawColours::accent().withAlpha(0.35f));
    setColour(juce::PopupMenu::textColourId, DawColours::text());
    setColour(juce::ToggleButton::textColourId, DawColours::text());
    setColour(juce::ToggleButton::tickColourId, DawColours::accent());
    setColour(juce::ToggleButton::tickDisabledColourId, DawColours::textDim());
    setColour(juce::ProgressBar::foregroundColourId, DawColours::accent());
    setColour(juce::ProgressBar::backgroundColourId, DawColours::panel());
    setColour(juce::TooltipWindow::backgroundColourId, DawColours::panelLight());
    setColour(juce::TooltipWindow::textColourId, DawColours::text());
    setColour(juce::AlertWindow::backgroundColourId, DawColours::panel());
    setColour(juce::AlertWindow::textColourId, DawColours::text());
}

juce::Font DawLookAndFeel::uiFont(float height, bool bold) {
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

void DawLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPos, float startAngle, float endAngle, juce::Slider& slider) {
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(3.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const bool bipolar = slider.getProperties()[kBipolarProperty];
    const float arcFrom = bipolar ? 0.5f * (startAngle + endAngle) : startAngle;
    const float lineW = std::max(2.0f, radius * 0.16f);
    const float arcRadius = radius - lineW * 0.5f;

    // track (full range), value arc, disc, pointer
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour(DawColours::outline());
    g.strokePath(track, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (slider.isEnabled() && std::abs(angle - arcFrom) > 0.001f) {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, std::min(arcFrom, angle),
                            std::max(arcFrom, angle), true);
        g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
        g.strokePath(value, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float discR = radius - lineW * 1.8f;
    g.setColour(DawColours::panelLight());
    g.fillEllipse(centre.x - discR, centre.y - discR, 2.0f * discR, 2.0f * discR);
    juce::Path pointer;
    pointer.addRoundedRectangle(-1.0f, -discR, 2.0f, discR * 0.6f, 1.0f);
    g.setColour(slider.isEnabled() ? DawColours::text() : DawColours::textDim());
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
}

void DawLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                      float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                                      juce::Slider& slider) {
    if (style != juce::Slider::LinearVertical) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }
    // A mixing-desk fader: a dark slot, a filled part below the cap, a flat cap with a centre line.
    const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float slotW = 4.0f;
    const auto slot = juce::Rectangle<float>(area.getCentreX() - slotW * 0.5f, area.getY(), slotW, area.getHeight());
    g.setColour(DawColours::background());
    g.fillRoundedRectangle(slot, 2.0f);
    g.setColour(DawColours::accent().withAlpha(slider.isEnabled() ? 0.6f : 0.2f));
    g.fillRoundedRectangle(slot.withTop(sliderPos), 2.0f);

    const float capW = std::min(area.getWidth() - 2.0f, 34.0f);
    const float capH = 22.0f;
    const auto cap = juce::Rectangle<float>(capW, capH).withCentre({area.getCentreX(), sliderPos});
    g.setColour(juce::Colour(0xff4a515c));
    g.fillRoundedRectangle(cap, 3.0f);
    g.setColour(DawColours::outline().brighter(0.4f));
    g.drawRoundedRectangle(cap, 3.0f, 1.0f);
    g.setColour(DawColours::text());
    g.fillRect(cap.withSizeKeepingCentre(capW - 8.0f, 2.0f));
}

void DawLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                                          bool highlighted, bool down) {
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto colour = background;
    if (down)
        colour = colour.brighter(0.25f);
    else if (highlighted)
        colour = colour.brighter(0.1f);
    if (!button.isEnabled())
        colour = colour.withMultipliedAlpha(0.5f);
    g.setColour(colour);
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(DawColours::outline());
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
}

juce::Font DawLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight) {
    return uiFont(std::min(14.0f, static_cast<float>(buttonHeight) * 0.6f));
}

juce::Font DawLookAndFeel::getLabelFont(juce::Label& label) { return uiFont(std::min(15.0f, label.getFont().getHeight())); }

juce::Font DawLookAndFeel::getPopupMenuFont() { return uiFont(15.0f); }

void DawLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) {
    g.fillAll(findColour(juce::TooltipWindow::backgroundColourId));
    g.setColour(DawColours::outline());
    g.drawRect(0, 0, width, height);
    g.setColour(findColour(juce::TooltipWindow::textColourId));
    g.setFont(uiFont(13.0f));
    g.drawFittedText(text, juce::Rectangle<int>(width, height).reduced(4, 2), juce::Justification::centredLeft, 3);
}

} // namespace dawlab::app
