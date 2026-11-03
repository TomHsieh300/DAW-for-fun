#include "TransportBar.h"

#include "AudioHost.h"
#include "DawLookAndFeel.h"

#include <cmath>

namespace dawlab::app {

// A flat button that draws a transport icon with juce::Path (no image files needed).
class TransportBar::IconButton final : public juce::Button {
public:
    enum class Icon { ReturnToStart, Play, Stop, Loop };

    IconButton(const juce::String& name, Icon icon) : juce::Button(name), icon_(icon) {
        setWantsKeyboardFocus(false); // keep the space bar for the main component
    }

    void setIcon(Icon icon) {
        if (icon != icon_) {
            icon_ = icon;
            repaint();
        }
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        const bool on = getToggleState();
        g.setColour(on ? DawColours::accentWarm().withAlpha(0.25f)
                       : (down ? DawColours::outline() : (highlighted ? DawColours::panelLight().brighter(0.08f) : DawColours::panelLight())));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(on ? DawColours::accentWarm() : DawColours::outline());
        g.drawRoundedRectangle(r, 4.0f, 1.0f);

        const auto c = r.getCentre();
        const float s = std::min(r.getWidth(), r.getHeight()) * 0.28f;
        juce::Path p;
        switch (icon_) {
        case Icon::Play: p.addTriangle(c.x - s * 0.8f, c.y - s, c.x - s * 0.8f, c.y + s, c.x + s, c.y); break;
        case Icon::Stop: p.addRectangle(c.x - s * 0.8f, c.y - s * 0.8f, 1.6f * s, 1.6f * s); break;
        case Icon::ReturnToStart:
            p.addRectangle(c.x - s, c.y - s, s * 0.3f, 2.0f * s);
            p.addTriangle(c.x + s, c.y - s, c.x + s, c.y + s, c.x - s * 0.6f, c.y);
            break;
        case Icon::Loop: {
            juce::Path arc;
            arc.addCentredArc(c.x, c.y, s, s * 0.75f, 0.0f, 0.4f, juce::MathConstants<float>::twoPi - 0.4f, true);
            g.setColour(on ? DawColours::accentWarm() : DawColours::text());
            g.strokePath(arc, juce::PathStrokeType(2.0f));
            p.addTriangle(c.x + s * 0.15f, c.y - s * 0.75f - 4.0f, c.x + s * 0.15f, c.y - s * 0.75f + 4.0f,
                          c.x + s * 0.65f, c.y - s * 0.75f);
            break;
        }
        }
        g.setColour(icon_ == Icon::Loop ? (on ? DawColours::accentWarm() : DawColours::text())
                                        : (icon_ == Icon::Stop ? DawColours::accent() : DawColours::text()));
        g.fillPath(p);
    }

private:
    Icon icon_;
};

TransportBar::TransportBar(engine::Engine& engine, AudioHost& audioHost) : engine_(engine), audioHost_(audioHost) {
    returnButton_ = std::make_unique<IconButton>("Return to start", IconButton::Icon::ReturnToStart);
    playButton_ = std::make_unique<IconButton>("Play / Stop", IconButton::Icon::Play);
    loopButton_ = std::make_unique<IconButton>("Loop", IconButton::Icon::Loop);
    returnButton_->setTooltip("Return to start (Home)");
    playButton_->setTooltip("Play / Stop (Space)");
    loopButton_->setTooltip("Loop on/off (L)");
    returnButton_->onClick = [this] { if (onReturnToStart) onReturnToStart(); };
    playButton_->onClick = [this] { if (onPlayStop) onPlayStop(); };
    loopButton_->onClick = [this] { if (onToggleLoop) onToggleLoop(); };
    for (auto* b : {returnButton_.get(), playButton_.get(), loopButton_.get()})
        addAndMakeVisible(*b);

    for (auto* l : {&barsBeats_, &time_}) {
        l->setJustificationType(juce::Justification::centred);
        l->setColour(juce::Label::backgroundColourId, DawColours::background());
        l->setColour(juce::Label::textColourId, DawColours::accent());
        addAndMakeVisible(*l);
    }
    barsBeats_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 22.0f, juce::Font::bold)));
    time_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 17.0f, juce::Font::plain)));
    barsBeats_.setTooltip("Position: bar . beat . tick (960 ticks per beat)");
    time_.setTooltip("Position: minutes : seconds");

    tempo_.setRange(engine::Transport::kMinTempo, 300.0, 0.1);
    tempo_.setTextValueSuffix(" BPM");
    tempo_.setNumDecimalPlacesToDisplay(1);
    tempo_.setValue(engine_.getTransport().getTempo(), juce::dontSendNotification);
    tempo_.setWantsKeyboardFocus(false);
    tempo_.setTooltip("Tempo (drag, or double-click to type)");
    tempo_.setColour(juce::Slider::trackColourId, DawColours::accent().withAlpha(0.3f));
    tempo_.onValueChange = [this] { engine_.getTransport().setTempo(tempo_.getValue()); };
    addAndMakeVisible(tempo_);

    masterLabel_.setText("Master", juce::dontSendNotification);
    masterLabel_.setColour(juce::Label::textColourId, DawColours::textDim());
    addAndMakeVisible(masterLabel_);
    masterMeter_.setTooltip("Master peak level, -60 .. +6 dBFS");
    addAndMakeVisible(masterMeter_);
    loudness_.setTooltip("Master loudness (EBU R 128): Momentary 400 ms, Short-term 3 s, Integrated");
    addAndMakeVisible(loudness_);
    cpu_.setJustificationType(juce::Justification::centredRight);
    cpu_.setColour(juce::Label::textColourId, DawColours::textDim());
    cpu_.setTooltip("Time spent in the audio callback / time available");
    addAndMakeVisible(cpu_);
    update();
}

TransportBar::~TransportBar() = default;

juce::String TransportBar::formatBarsBeats(const engine::BarsBeats& p) {
    return juce::String(p.bar) + "." + juce::String(p.beat) + "." + juce::String(p.tick).paddedLeft('0', 3);
}

juce::String TransportBar::formatTime(double seconds) {
    if (seconds < 0.0)
        seconds = 0.0;
    const auto totalMs = static_cast<juce::int64>(std::floor(seconds * 1000.0));
    const auto minutes = totalMs / 60000;
    const auto secs = (totalMs / 1000) % 60;
    const auto ms = totalMs % 1000;
    return juce::String(minutes) + ":" + juce::String(secs).paddedLeft('0', 2) + "." + juce::String(ms).paddedLeft('0', 3);
}

void TransportBar::update() {
    const engine::Transport& tr = engine_.getTransport();
    // What the listener hears now, not where the engine renders (AudioHost::getAudiblePosition:
    // minus the mix latency and the device's output latency while playing).
    const int64_t audible = audioHost_.getAudiblePosition();
    barsBeats_.setText(formatBarsBeats(tr.samplesToBarsBeats(audible)), juce::dontSendNotification);
    time_.setText(formatTime(tr.toSeconds(audible)), juce::dontSendNotification);
    playButton_->setIcon(tr.isPlaying() ? IconButton::Icon::Stop : IconButton::Icon::Play);
    loopButton_->setToggleState(tr.isLoopEnabled(), juce::dontSendNotification);
    if (!tempo_.isMouseButtonDown() && std::abs(tempo_.getValue() - tr.getTempo()) > 1e-6)
        tempo_.setValue(tr.getTempo(), juce::dontSendNotification);

    const engine::ChannelStrip& master = engine_.getMixer().getMaster();
    masterMeter_.setLevels(master.getPeakDb(0), master.getPeakDb(1), master.getPeakHoldDb(0), master.getPeakHoldDb(1));
    loudness_.setValues(master.getMomentaryLufs(), master.getShortTermLufs(), master.getIntegratedLufs());
    cpu_.setText("CPU " + juce::String(juce::roundToInt(100.0f * audioHost_.getCpuLoad())) + "%", juce::dontSendNotification);
}

void TransportBar::paint(juce::Graphics& g) {
    g.fillAll(DawColours::panel());
    g.setColour(DawColours::outline());
    g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
}

void TransportBar::resized() {
    auto area = getLocalBounds().reduced(10, 8);
    const int h = area.getHeight();
    returnButton_->setBounds(area.removeFromLeft(h + 6));
    area.removeFromLeft(4);
    playButton_->setBounds(area.removeFromLeft(h + 14));
    area.removeFromLeft(4);
    loopButton_->setBounds(area.removeFromLeft(h + 6));
    area.removeFromLeft(14);
    barsBeats_.setBounds(area.removeFromLeft(130));
    area.removeFromLeft(6);
    time_.setBounds(area.removeFromLeft(110));
    area.removeFromLeft(14);
    tempo_.setBounds(area.removeFromLeft(130).reduced(0, 4));
    area.removeFromLeft(20);
    cpu_.setBounds(area.removeFromRight(80));
    area.removeFromRight(10);
    loudness_.setBounds(area.removeFromRight(230).reduced(0, 4));
    area.removeFromRight(10);
    masterLabel_.setBounds(area.removeFromLeft(52));
    masterMeter_.setBounds(area.reduced(0, 6));
}

} // namespace dawlab::app
