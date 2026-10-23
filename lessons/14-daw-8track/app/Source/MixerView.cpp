#include "MixerView.h"

#include "DawLookAndFeel.h"
#include "EffectMenu.h"

#include <algorithm>
#include <cmath>

namespace dawlab::app {

using engine::ChannelKind;
using engine::ChannelRef;

// ---- FaderLaw ---------------------------------------------------------------------------------------

float FaderLaw::positionToDb(float p) noexcept {
    p = std::clamp(p, 0.0f, 1.0f);
    return kTopDb - kRangeDb * (1.0f - p) * (1.0f - p);
}

float FaderLaw::dbToPosition(float db) noexcept {
    if (!(db > kBottomDb))
        return 0.0f;
    db = std::min(db, kTopDb);
    return 1.0f - std::sqrt((kTopDb - db) / kRangeDb);
}

float FaderLaw::toEngineDb(float faderDb) noexcept { return faderDb <= kBottomDb + 0.01f ? kMinusInfinityDb : faderDb; }

juce::String FaderLaw::format(float db) {
    if (db <= kBottomDb + 0.01f)
        return "-inf";
    if (std::abs(db) < 0.05f)
        return "0.0 dB";
    return (db > 0.0f ? "+" : "") + juce::String(db, 1) + " dB";
}

// ---- slot button ------------------------------------------------------------------------------------

class ChannelStripView::SlotButton final : public juce::Button {
public:
    SlotButton(ChannelStripView& owner, int slot) : juce::Button("slot " + juce::String(slot + 1)), owner_(owner), slot_(slot) {
        setWantsKeyboardFocus(false);
    }

    void setState(const InsertSlotState& s) {
        const juce::String label = s.isEmpty() ? juce::String() : (s.kind == SlotKind::Builtin ? EffectMenu::labelFor(s.type) : s.name);
        const bool bypassed = !s.isEmpty() && s.effect->isBypassed();
        if (label != text_ || bypassed != bypassed_ || s.kind != kind_) {
            text_ = label;
            bypassed_ = bypassed;
            kind_ = s.kind;
            setTooltip(s.isEmpty() ? "Empty insert slot: click to choose an effect"
                                   : label + (bypassed ? " (bypassed)" : "") + " - click: edit, arrow / right click: menu");
            repaint();
        }
    }

    const juce::String& getText() const noexcept { return text_; }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu()) {
            if (owner_.onSlotClicked)
                owner_.onSlotClicked(owner_.ref_, slot_, *this, true);
            return;
        }
        lastX_ = e.x;
        juce::Button::mouseDown(e);
    }

    void clicked() override {
        const bool menu = text_.isEmpty() || lastX_ >= getWidth() - kArrowWidth;
        if (owner_.onSlotClicked)
            owner_.onSlotClicked(owner_.ref_, slot_, *this, menu);
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        const bool empty = text_.isEmpty();
        auto bg = empty ? DawColours::background() : DawColours::panelLight();
        if (kind_ == SlotKind::Vst3 && !empty)
            bg = bg.interpolatedWith(DawColours::accent(), 0.15f);
        if (highlighted)
            bg = bg.brighter(0.08f);
        if (down)
            bg = bg.brighter(0.15f);
        g.setColour(bg);
        g.fillRoundedRectangle(r, 2.5f);
        g.setColour(DawColours::outline());
        g.drawRoundedRectangle(r, 2.5f, 1.0f);
        auto textArea = getLocalBounds().reduced(4, 0);
        auto arrow = textArea.removeFromRight(kArrowWidth - 4);
        g.setFont(DawLookAndFeel::uiFont(12.0f));
        g.setColour(empty ? DawColours::textDim().withAlpha(0.5f) : (bypassed_ ? DawColours::textDim() : DawColours::text()));
        g.drawText(empty ? juce::String("-") : text_, textArea, juce::Justification::centredLeft, true);
        if (bypassed_)
            g.drawLine(static_cast<float>(textArea.getX()), r.getCentreY(), static_cast<float>(textArea.getRight()), r.getCentreY(), 1.0f);
        juce::Path tri;
        const auto c = arrow.toFloat().getCentre();
        tri.addTriangle(c.x - 3.5f, c.y - 2.0f, c.x + 3.5f, c.y - 2.0f, c.x, c.y + 2.5f);
        g.setColour(DawColours::textDim());
        g.fillPath(tri);
    }

private:
    static constexpr int kArrowWidth = 16;
    ChannelStripView& owner_;
    int slot_;
    int lastX_ = 0;
    juce::String text_;
    bool bypassed_ = false;
    SlotKind kind_ = SlotKind::Empty;
};

// ---- ChannelStripView ---------------------------------------------------------------------------------

ChannelStripView::ChannelStripView(Session& session, ChannelRef ref) : session_(session), ref_(ref) {
    const bool isTrack = ref_.kind == ChannelKind::Track;
    const bool isMaster = ref_.kind == ChannelKind::Master;

    name_.setJustificationType(juce::Justification::centred);
    name_.setFont(DawLookAndFeel::uiFont(14.0f, true));
    name_.setEditable(false, isTrack || ref_.kind == ChannelKind::Bus, false);
    name_.setTooltip("Double-click to rename");
    name_.onTextChange = [this] { session_.setStripName(ref_, name_.getText()); };
    addAndMakeVisible(name_);

    if (isTrack) {
        load_.setTooltip("Load a WAV / AIFF file into this track");
        load_.setWantsKeyboardFocus(false);
        load_.onClick = [this] { if (onLoadRequested) onLoadRequested(ref_.index); };
        addAndMakeVisible(load_);
    }

    for (int s = 0; s < kInsertSlotsPerStrip; ++s) {
        slots_.push_back(std::make_unique<SlotButton>(*this, s));
        addAndMakeVisible(*slots_.back());
    }

    if (isTrack) {
        for (int b = 0; b < engine::kNumBuses; ++b) {
            auto knob = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
            knob->setRange(FaderLaw::kBottomDb, 6.0, 0.1);
            knob->setDoubleClickReturnValue(true, -12.0);
            knob->setPopupDisplayEnabled(true, true, this);
            knob->textFromValueFunction = [](double v) { return FaderLaw::format(static_cast<float>(v)); };
            knob->setWantsKeyboardFocus(false);
            knob->setColour(juce::Slider::rotarySliderFillColourId, DawColours::accentWarm());
            // The tooltip and the label follow pre/post-fader and the bus name in update().
            knob->onValueChange = [this, b, k = knob.get()] {
                strip().setSendLevelDb(b, FaderLaw::toEngineDb(static_cast<float>(k->getValue())));
            };
            addAndMakeVisible(*knob);
            sends_.push_back(std::move(knob));
            auto label = std::make_unique<juce::Label>();
            label->setText(juce::String::charToString(static_cast<juce::juce_wchar>('A' + b)), juce::dontSendNotification);
            label->setJustificationType(juce::Justification::centred);
            label->setFont(DawLookAndFeel::uiFont(11.0f));
            label->setColour(juce::Label::textColourId, DawColours::textDim());
            addAndMakeVisible(*label);
            sendLabels_.push_back(std::move(label));
        }
    }
    if (isMaster) {
        ceiling_.setRange(-12.0, 0.0, 0.1);
        ceiling_.setDoubleClickReturnValue(true, -1.0);
        ceiling_.setPopupDisplayEnabled(true, true, this);
        ceiling_.textFromValueFunction = [](double v) { return juce::String(v, 1) + " dBFS"; };
        ceiling_.setWantsKeyboardFocus(false);
        ceiling_.setColour(juce::Slider::rotarySliderFillColourId, DawColours::meterHigh());
        ceiling_.setTooltip("Master limiter ceiling (drives the first Limiter on the master)");
        ceiling_.onValueChange = [this] { session_.setMasterCeilingDb(static_cast<float>(ceiling_.getValue())); };
        addAndMakeVisible(ceiling_);
        ceilingLabel_.setText("Ceiling", juce::dontSendNotification);
        ceilingLabel_.setJustificationType(juce::Justification::centred);
        ceilingLabel_.setFont(DawLookAndFeel::uiFont(11.0f));
        ceilingLabel_.setColour(juce::Label::textColourId, DawColours::textDim());
        addAndMakeVisible(ceilingLabel_);
    }

    pan_.setRange(-1.0, 1.0, 0.01);
    pan_.setDoubleClickReturnValue(true, 0.0);
    pan_.getProperties().set(DawLookAndFeel::kBipolarProperty, true);
    pan_.setPopupDisplayEnabled(true, true, this);
    pan_.textFromValueFunction = [](double v) {
        const int pct = juce::roundToInt(std::abs(v) * 100.0);
        return pct == 0 ? juce::String("C") : juce::String(pct) + (v < 0 ? " L" : " R");
    };
    pan_.setWantsKeyboardFocus(false);
    pan_.setTooltip(isTrack ? "Pan (constant-power law)" : "Balance");
    pan_.onValueChange = [this] { strip().setPan(static_cast<float>(pan_.getValue())); };
    addAndMakeVisible(pan_);
    panLabel_.setText(isTrack ? "Pan" : "Bal", juce::dontSendNotification);
    panLabel_.setJustificationType(juce::Justification::centred);
    panLabel_.setFont(DawLookAndFeel::uiFont(11.0f));
    panLabel_.setColour(juce::Label::textColourId, DawColours::textDim());
    addAndMakeVisible(panLabel_);

    fader_.setRange(0.0, 1.0, 0.0);
    fader_.setDoubleClickReturnValue(true, FaderLaw::dbToPosition(0.0f));
    fader_.setPopupDisplayEnabled(true, true, this);
    fader_.textFromValueFunction = [](double p) { return FaderLaw::format(FaderLaw::positionToDb(static_cast<float>(p))); };
    fader_.setWantsKeyboardFocus(false);
    fader_.setTooltip("Fader (double-click: 0 dB)");
    fader_.onValueChange = [this] {
        const float db = FaderLaw::positionToDb(static_cast<float>(fader_.getValue()));
        strip().setGainDb(FaderLaw::toEngineDb(db)); // atomic; the engine ramps over 50 ms
        gainLabel_.setText(FaderLaw::format(db), juce::dontSendNotification);
    };
    addAndMakeVisible(fader_);
    addAndMakeVisible(meter_);
    gainLabel_.setJustificationType(juce::Justification::centred);
    gainLabel_.setFont(DawLookAndFeel::uiFont(11.0f));
    gainLabel_.setColour(juce::Label::textColourId, DawColours::textDim());
    addAndMakeVisible(gainLabel_);

    if (!isMaster) {
        mute_.setClickingTogglesState(true);
        mute_.setWantsKeyboardFocus(false);
        mute_.setColour(juce::TextButton::buttonOnColourId, DawColours::mute());
        mute_.setTooltip("Mute");
        mute_.onClick = [this] { strip().setMute(mute_.getToggleState()); };
        addAndMakeVisible(mute_);
    }
    if (isTrack) { // buses are solo-safe (stage 1): no solo button
        solo_.setClickingTogglesState(true);
        solo_.setWantsKeyboardFocus(false);
        solo_.setColour(juce::TextButton::buttonOnColourId, DawColours::accentWarm());
        solo_.setTooltip("Solo");
        solo_.onClick = [this] { strip().setSolo(solo_.getToggleState()); };
        addAndMakeVisible(solo_);
    }
    refreshStructure();
    update();
}

ChannelStripView::~ChannelStripView() = default;

engine::ChannelStrip& ChannelStripView::strip() noexcept { return *session_.getEngine().getMixer().getStrip(ref_); }

juce::Slider* ChannelStripView::getSendKnob(int bus) noexcept {
    return (bus >= 0 && bus < static_cast<int>(sends_.size())) ? sends_[static_cast<size_t>(bus)].get() : nullptr;
}

juce::Button* ChannelStripView::getSlotButton(int slot) noexcept {
    return (slot >= 0 && slot < static_cast<int>(slots_.size())) ? slots_[static_cast<size_t>(slot)].get() : nullptr;
}

void ChannelStripView::refreshStructure() {
    const juce::String name = session_.getStripName(ref_);
    if (!name_.isBeingEdited() && name_.getText() != name)
        name_.setText(name, juce::dontSendNotification);
    for (int s = 0; s < kInsertSlotsPerStrip; ++s)
        slots_[static_cast<size_t>(s)]->setState(session_.getSlot(ref_, s));
    if (ref_.kind == ChannelKind::Master)
        ceiling_.setEnabled(session_.hasMasterLimiter());
}

void ChannelStripView::update() {
    engine::ChannelStrip& s = strip();
    meter_.setLevels(s.getPeakDb(0), s.getPeakDb(1), s.getPeakHoldDb(0), s.getPeakHoldDb(1));

    // Values can change elsewhere (project load, keyboard shortcut, exercise code): follow them
    // unless the user is holding the control right now.
    if (!fader_.isMouseButtonDown()) {
        const float pos = FaderLaw::dbToPosition(s.getGainDb());
        if (std::abs(static_cast<float>(fader_.getValue()) - pos) > 1e-4f) {
            fader_.setValue(pos, juce::dontSendNotification);
        }
        gainLabel_.setText(FaderLaw::format(FaderLaw::positionToDb(static_cast<float>(fader_.getValue()))), juce::dontSendNotification);
    }
    if (!pan_.isMouseButtonDown() && std::abs(pan_.getValue() - s.getPan()) > 1e-4)
        pan_.setValue(s.getPan(), juce::dontSendNotification);
    for (int b = 0; b < static_cast<int>(sends_.size()); ++b) {
        auto& k = *sends_[static_cast<size_t>(b)];
        const float db = std::max(FaderLaw::kBottomDb, s.getSendLevelDb(b));
        if (!k.isMouseButtonDown() && std::abs(k.getValue() - db) > 0.05)
            k.setValue(db, juce::dontSendNotification);
        // A pre-fader send (Exercise 12's doubler bus, a cue mix) ignores the fader: say so.
        const bool pre = s.isSendPreFader(b);
        const juce::String tip = "Send to " + session_.getStripName(ChannelRef::bus(b)) +
                                 (pre ? " (pre-fader: independent of the fader)" : " (post-fader)");
        if (k.getTooltip() != tip)
            k.setTooltip(tip);
        const juce::String letter = juce::String::charToString(static_cast<juce::juce_wchar>('A' + b));
        auto& label = *sendLabels_[static_cast<size_t>(b)];
        const juce::String text = pre ? letter + " pre" : letter;
        if (label.getText() != text) {
            label.setText(text, juce::dontSendNotification);
            label.setColour(juce::Label::textColourId, pre ? DawColours::accentWarm() : DawColours::textDim());
        }
    }
    mute_.setToggleState(s.isMuted(), juce::dontSendNotification);
    solo_.setToggleState(s.isSoloed(), juce::dontSendNotification);
    if (ref_.kind == ChannelKind::Master && !ceiling_.isMouseButtonDown() && session_.hasMasterLimiter())
        ceiling_.setValue(session_.getMasterCeilingDb(), juce::dontSendNotification);
}

void ChannelStripView::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(2.0f, 0.0f);
    g.setColour(DawColours::panel());
    g.fillRoundedRectangle(r, 4.0f);
    juce::Colour stripe = ref_.kind == ChannelKind::Track ? DawColours::track(ref_.index)
                          : ref_.kind == ChannelKind::Bus ? DawColours::accentWarm()
                                                          : DawColours::accent();
    g.setColour(stripe);
    g.fillRoundedRectangle(r.withHeight(4.0f), 2.0f);
    // dB scale next to the fader
    const auto faderArea = fader_.getBounds();
    g.setFont(DawLookAndFeel::uiFont(9.0f));
    g.setColour(DawColours::textDim());
    for (float db : {12.0f, 0.0f, -12.0f, -30.0f}) {
        // The slider's thumb travels between y = top + 11 and bottom - 11 (cap height 22)
        const float travel = static_cast<float>(faderArea.getHeight()) - 22.0f;
        const float y = static_cast<float>(faderArea.getY()) + 11.0f + (1.0f - FaderLaw::dbToPosition(db)) * travel;
        g.drawText(juce::String(static_cast<int>(db)), faderArea.getX() - 22, static_cast<int>(y) - 5, 20, 10,
                   juce::Justification::centredRight);
    }
}

void ChannelStripView::resized() {
    auto area = getLocalBounds().reduced(6, 6);
    area.removeFromTop(2);
    name_.setBounds(area.removeFromTop(20));
    area.removeFromTop(2);
    const auto loadRow = area.removeFromTop(20);
    if (ref_.kind == ChannelKind::Track)
        load_.setBounds(loadRow);
    area.removeFromTop(4);
    for (auto& s : slots_) {
        s->setBounds(area.removeFromTop(19));
        area.removeFromTop(2);
    }
    area.removeFromTop(4);

    auto knobRow = area.removeFromTop(46);
    if (!sends_.empty()) {
        const int w = knobRow.getWidth() / static_cast<int>(sends_.size());
        for (size_t b = 0; b < sends_.size(); ++b) {
            auto cell = knobRow.removeFromLeft(w);
            sendLabels_[b]->setBounds(cell.removeFromBottom(12));
            sends_[b]->setBounds(cell.withSizeKeepingCentre(std::min(cell.getWidth(), 34), 34));
        }
    } else if (ref_.kind == ChannelKind::Master) {
        ceilingLabel_.setBounds(knobRow.removeFromBottom(12));
        ceiling_.setBounds(knobRow.withSizeKeepingCentre(34, 34));
    }
    auto panRow = area.removeFromTop(46);
    panLabel_.setBounds(panRow.removeFromBottom(12));
    pan_.setBounds(panRow.withSizeKeepingCentre(34, 34));
    area.removeFromTop(4);

    auto buttons = area.removeFromBottom(22);
    if (ref_.kind == ChannelKind::Track) {
        mute_.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).reduced(2, 0));
        solo_.setBounds(buttons.reduced(2, 0));
    } else if (ref_.kind == ChannelKind::Bus) {
        mute_.setBounds(buttons.withSizeKeepingCentre(buttons.getWidth() / 2, buttons.getHeight()));
    }
    gainLabel_.setBounds(area.removeFromBottom(14));
    // fader on the left half (with the dB scale painted left of it), meter on the right
    auto faderArea = area.reduced(0, 2);
    auto meterArea = faderArea.removeFromRight(16);
    meter_.setBounds(meterArea);
    faderArea.removeFromRight(2);
    fader_.setBounds(faderArea.removeFromRight(std::min(40, faderArea.getWidth() - 22)));
}

// ---- MixerView ----------------------------------------------------------------------------------------

MixerView::MixerView(Session& session) : session_(session) {
    for (int i = 0; i < kNumStrips; ++i) {
        auto strip = std::make_unique<ChannelStripView>(session_, stripRefAt(i));
        strip->onLoadRequested = [this](int track) { if (onLoadRequested) onLoadRequested(track); };
        strip->onSlotClicked = [this](ChannelRef ref, int slot, juce::Component& anchor, bool menu) {
            if (onSlotClicked)
                onSlotClicked(ref, slot, anchor, menu);
        };
        addAndMakeVisible(*strip);
        strips_.push_back(std::move(strip));
    }
    for (auto* l : {&spectrumLabel_, &loudnessLabel_, &latencyLabel_}) {
        l->setFont(DawLookAndFeel::uiFont(12.0f));
        l->setColour(juce::Label::textColourId, DawColours::textDim());
        addAndMakeVisible(*l);
    }
    spectrumLabel_.setText("Master spectrum", juce::dontSendNotification);
    loudnessLabel_.setText("Loudness (LUFS)", juce::dontSendNotification);
    spectrum_.setTooltip("Spectrum of the master output (4096-point FFT, chapter 11)");
    addAndMakeVisible(spectrum_);
    addAndMakeVisible(loudness_);
    session_.addChangeListener(this);
}

MixerView::~MixerView() { session_.removeChangeListener(this); }

void MixerView::changeListenerCallback(juce::ChangeBroadcaster*) {
    for (auto& s : strips_)
        s->refreshStructure();
}

void MixerView::update() {
    for (auto& s : strips_)
        s->update();
    const auto& master = session_.getEngine().getMixer().getMaster();
    loudness_.setValues(master.getMomentaryLufs(), master.getShortTermLufs(), master.getIntegratedLufs());
    const int latency = session_.getEngine().getLatencySamples();
    const double sr = session_.getEngine().getSampleRate();
    // An insert whose latency exceeds the PDC limit (Mixer.h) cannot be aligned: say so loudly.
    const bool overflow = session_.getEngine().hasPdcOverflow();
    latencyLabel_.setText("Mix latency (PDC): " + juce::String(latency) + " samples = " +
                              juce::String(sr > 0.0 ? 1000.0 * latency / sr : 0.0, 2) + " ms" +
                              (overflow ? " - PDC limit exceeded, not aligned!" : ""),
                          juce::dontSendNotification);
    latencyLabel_.setColour(juce::Label::textColourId, overflow ? DawColours::meterHigh() : DawColours::textDim());
}

void MixerView::paint(juce::Graphics& g) { g.fillAll(DawColours::background()); }

void MixerView::resized() {
    auto area = getLocalBounds().reduced(4, 4);
    for (int i = 0; i < kNumStrips; ++i) {
        const ChannelRef ref = stripRefAt(i);
        if (ref.kind == ChannelKind::Bus && ref.index == 0)
            area.removeFromLeft(8); // gap between tracks and buses
        if (ref.kind == ChannelKind::Master)
            area.removeFromLeft(8);
        const int w = ref.kind == ChannelKind::Master ? ChannelStripView::kMasterWidth : ChannelStripView::kTrackWidth;
        strips_[static_cast<size_t>(i)]->setBounds(area.removeFromLeft(w));
    }
    area.removeFromLeft(8);
    auto right = area;
    spectrumLabel_.setBounds(right.removeFromTop(18));
    auto bottom = right.removeFromBottom(64);
    latencyLabel_.setBounds(bottom.removeFromBottom(18));
    loudnessLabel_.setBounds(bottom.removeFromTop(18));
    loudness_.setBounds(bottom.reduced(0, 2));
    spectrum_.setBounds(right.reduced(0, 2));
}

} // namespace dawlab::app
