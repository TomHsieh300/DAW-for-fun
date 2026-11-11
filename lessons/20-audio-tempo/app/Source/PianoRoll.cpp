#include "PianoRoll.h"

#include "DawLookAndFeel.h"

#include <cmath>

namespace dawlab::app {

namespace {
constexpr float kEndGrip = 5.0f; // pixels at a note's end that resize it

bool isBlackKey(int pitch) noexcept {
    const int k = pitch % 12;
    return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}

juce::String pitchName(int pitch) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[pitch % 12]) + juce::String(pitch / 12 - 1);
}
} // namespace

PianoRoll::PianoRoll(Session& session, engine::TrackId track, int clipId) : session_(session), track_(track), clipId_(clipId) {
    session_.addChangeListener(this);
    setSize(900, 480);
    scrollToPitch(60);
}

PianoRoll::~PianoRoll() { session_.removeChangeListener(this); }

int PianoRoll::clipIndex() const noexcept {
    const auto& clips = session_.getMidiClips(track_);
    for (size_t i = 0; i < clips.size(); ++i)
        if (clips[i].id == clipId_)
            return static_cast<int>(i);
    return -1;
}

const MidiClipModel* PianoRoll::getClip() const noexcept {
    const int i = clipIndex();
    return i >= 0 ? &session_.getMidiClips(track_)[static_cast<size_t>(i)] : nullptr;
}

void PianoRoll::setView(double viewTick, double ticksPerPixel) {
    viewTick_ = viewTick;
    ticksPerPixel_ = std::clamp(ticksPerPixel, 0.25, 200.0);
    repaint();
}

void PianoRoll::scrollToPitch(int pitch) {
    const int visible = std::max(1, getHeight() - kRulerHeight);
    scrollY_ = std::clamp((127 - pitch) * kRowHeight - visible / 2, 0, std::max(0, 128 * kRowHeight - visible));
    repaint();
}

float PianoRoll::tickToX(double tick) const noexcept {
    return static_cast<float>(kKeyWidth + (tick - viewTick_) / ticksPerPixel_);
}
double PianoRoll::xToTick(float x) const noexcept { return viewTick_ + (static_cast<double>(x) - kKeyWidth) * ticksPerPixel_; }
float PianoRoll::pitchToY(int pitch) const noexcept { return static_cast<float>(kRulerHeight + (127 - pitch) * kRowHeight - scrollY_); }
int PianoRoll::yToPitch(float y) const noexcept {
    return std::clamp(127 - static_cast<int>(std::floor((y - kRulerHeight + static_cast<float>(scrollY_)) / kRowHeight)), 0, 127);
}

juce::Rectangle<float> PianoRoll::getNoteBounds(int index) const {
    const MidiClipModel* clip = getClip();
    if (clip == nullptr || index < 0 || index >= static_cast<int>(clip->played.size()))
        return {};
    const engine::Note& n = clip->played[static_cast<size_t>(index)];
    const float x0 = tickToX(static_cast<double>(n.start));
    const float x1 = tickToX(static_cast<double>(n.start + n.length));
    return {x0, pitchToY(n.pitch), std::max(3.0f, x1 - x0), static_cast<float>(kRowHeight - 1)};
}

int PianoRoll::noteAt(juce::Point<float> p, bool* onEnd) const {
    const MidiClipModel* clip = getClip();
    if (clip == nullptr)
        return -1;
    for (int i = static_cast<int>(clip->played.size()) - 1; i >= 0; --i) {
        const juce::Rectangle<float> r = getNoteBounds(i);
        if (r.contains(p)) {
            if (onEnd != nullptr)
                *onEnd = p.x >= r.getRight() - kEndGrip && r.getWidth() > 2.0f * kEndGrip;
            return i;
        }
    }
    return -1;
}

std::int64_t PianoRoll::snap(double tick, bool free) const noexcept {
    const double t = free ? std::round(tick) : std::floor(tick / static_cast<double>(grid_)) * static_cast<double>(grid_);
    return std::max<std::int64_t>(0, static_cast<std::int64_t>(t));
}

void PianoRoll::apply(std::vector<engine::Note> notes, const juce::String& name) {
    const int index = clipIndex();
    if (index < 0)
        return;
    MidiClipModel clip = session_.getMidiClips(track_)[static_cast<size_t>(index)];
    clip.played = std::move(notes);
    session_.replaceMidiClip(track_, index, std::move(clip), name, mergeKey_);
}

void PianoRoll::press(juce::Point<float> p, juce::ModifierKeys mods, int clicks) {
    gesture_ = Gesture::None;
    const MidiClipModel* clip = getClip();
    if (clip == nullptr || p.x < kKeyWidth || p.y < kRulerHeight)
        return;
    session_.getHistory().endMerge(); // every gesture is its own undo step
    static std::uint32_t nextKey = 0x50520000u; // "PR"
    mergeKey_ = ++nextKey;
    start_ = p;
    bool onEnd = false;
    const int hit = noteAt(p, &onEnd);
    std::vector<engine::Note> notes = clip->played;
    if (hit >= 0 && (mods.isPopupMenu() || clicks > 1)) {
        notes.erase(notes.begin() + hit);
        apply(std::move(notes), "Delete note");
        return;
    }
    if (hit >= 0) {
        grabbed_ = notes[static_cast<size_t>(hit)];
        grabbedIndex_ = hit;
        grabOffset_ = xToTick(p.x) - static_cast<double>(grabbed_.start);
        gesture_ = mods.isAltDown() ? Gesture::Velocity : (onEnd ? Gesture::Resize : Gesture::Move);
        return;
    }
    // A new note on the grid, one grid long.
    engine::Note n;
    n.start = snap(xToTick(p.x), mods.isAltDown());
    n.length = grid_;
    n.pitch = yToPitch(p.y);
    n.velocity = 100;
    notes.push_back(n);
    sortNotes(notes);
    grabbed_ = n;
    grabbedIndex_ = static_cast<int>(std::find(notes.begin(), notes.end(), n) - notes.begin());
    gesture_ = Gesture::Draw;
    apply(std::move(notes), "Add note");
}

void PianoRoll::dragTo(juce::Point<float> p, juce::ModifierKeys mods) {
    const MidiClipModel* clip = getClip();
    if (clip == nullptr || gesture_ == Gesture::None || grabbedIndex_ < 0 || grabbedIndex_ >= static_cast<int>(clip->played.size()))
        return;
    std::vector<engine::Note> notes = clip->played;
    engine::Note n = grabbed_;
    const bool free = mods.isAltDown();
    juce::String name;
    switch (gesture_) {
    case Gesture::Draw:
    case Gesture::Resize: {
        // The end follows the mouse, on the grid (rounded up to the line after it); at least one tick.
        const double end = xToTick(p.x);
        const std::int64_t snappedEnd =
            free ? static_cast<std::int64_t>(std::round(end))
                 : static_cast<std::int64_t>(std::ceil(end / static_cast<double>(grid_)) * static_cast<double>(grid_));
        n.length = std::max<std::int64_t>(1, snappedEnd - n.start);
        name = gesture_ == Gesture::Draw ? "Add note" : "Note length";
        break;
    }
    case Gesture::Move:
        n.start = snap(xToTick(p.x) - grabOffset_ + (free ? 0.0 : static_cast<double>(grid_) / 2.0), free);
        n.pitch = yToPitch(p.y);
        name = "Move note";
        break;
    case Gesture::Velocity:
        n.velocity = std::clamp(grabbed_.velocity + static_cast<int>(std::lround((start_.y - p.y) / 2.0f)), 1, 127);
        name = "Note velocity";
        break;
    case Gesture::None: return;
    }
    if (n == notes[static_cast<size_t>(grabbedIndex_)])
        return;
    notes[static_cast<size_t>(grabbedIndex_)] = n;
    sortNotes(notes);
    // Where it went: the first note equal to it from the end, which is where a stable sort puts it.
    for (int i = static_cast<int>(notes.size()) - 1; i >= 0; --i)
        if (notes[static_cast<size_t>(i)] == n) {
            grabbedIndex_ = i;
            break;
        }
    apply(std::move(notes), name);
}

void PianoRoll::release() {
    if (gesture_ != Gesture::None)
        session_.getHistory().endMerge();
    gesture_ = Gesture::None;
    grabbedIndex_ = -1;
}

void PianoRoll::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (e.mods.isCtrlDown() || e.mods.isCommandDown()) {
        const double at = xToTick(e.position.x);
        const double tpp = ticksPerPixel_ * (wheel.deltaY > 0 ? 0.8 : 1.25);
        setView(at - (static_cast<double>(e.position.x) - kKeyWidth) * tpp, tpp);
    } else if (e.mods.isShiftDown()) {
        setView(std::max(0.0, viewTick_ - static_cast<double>(wheel.deltaY) * 200.0 * ticksPerPixel_), ticksPerPixel_);
    } else {
        const int visible = std::max(1, getHeight() - kRulerHeight);
        scrollY_ = std::clamp(scrollY_ - static_cast<int>(wheel.deltaY * 120.0f), 0, std::max(0, 128 * kRowHeight - visible));
        repaint();
    }
}

void PianoRoll::paint(juce::Graphics& g) {
    g.fillAll(DawColours::background());
    const MidiClipModel* clip = getClip();
    const auto area = getLocalBounds();
    const juce::Colour colour = DawColours::track(track_);

    // rows: black keys darker, C lines brighter
    for (int pitch = 0; pitch < 128; ++pitch) {
        const float y = pitchToY(pitch);
        if (y + kRowHeight < kRulerHeight || y > static_cast<float>(area.getBottom()))
            continue;
        g.setColour(isBlackKey(pitch) ? DawColours::panel().darker(0.3f) : DawColours::background().brighter(0.04f));
        g.fillRect(juce::Rectangle<float>(static_cast<float>(kKeyWidth), y, static_cast<float>(getWidth() - kKeyWidth), kRowHeight));
        if (pitch % 12 == 0) {
            g.setColour(DawColours::outline());
            g.drawHorizontalLine(static_cast<int>(y + kRowHeight), static_cast<float>(kKeyWidth), static_cast<float>(getWidth()));
        }
    }
    // the grid and the beats
    for (double t = std::floor(viewTick_ / static_cast<double>(grid_)) * static_cast<double>(grid_); tickToX(t) < getWidth();
         t += static_cast<double>(grid_)) {
        const auto x = static_cast<int>(tickToX(t));
        if (x < kKeyWidth)
            continue;
        const bool beat = static_cast<std::int64_t>(t) % 960 == 0;
        g.setColour(DawColours::outline().withAlpha(beat ? 0.9f : 0.35f));
        g.drawVerticalLine(x, static_cast<float>(kRulerHeight), static_cast<float>(getHeight()));
    }
    if (clip != nullptr) {
        // the clip's window: outside it nothing plays
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        const float w0 = tickToX(static_cast<double>(clip->offset)), w1 = tickToX(static_cast<double>(clip->offset + clip->length));
        g.fillRect(juce::Rectangle<float>(static_cast<float>(kKeyWidth), static_cast<float>(kRulerHeight), std::max(0.0f, w0 - kKeyWidth),
                                          static_cast<float>(getHeight())));
        g.fillRect(juce::Rectangle<float>(w1, static_cast<float>(kRulerHeight), std::max(0.0f, static_cast<float>(getWidth()) - w1),
                                          static_cast<float>(getHeight())));
        // where quantize makes them play, as outlines
        if (clip->quantize.strength > 0.0) {
            g.setColour(DawColours::text().withAlpha(0.5f));
            for (const engine::Note& n : clip->notes())
                g.drawRect(juce::Rectangle<float>(tickToX(static_cast<double>(n.start)), pitchToY(n.pitch),
                                                  std::max(3.0f, tickToX(static_cast<double>(n.start + n.length)) - tickToX(static_cast<double>(n.start))),
                                                  static_cast<float>(kRowHeight - 1)),
                           1.0f);
        }
        // the notes as played, brighter the louder
        for (int i = 0; i < static_cast<int>(clip->played.size()); ++i) {
            const engine::Note& n = clip->played[static_cast<size_t>(i)];
            g.setColour(colour.withAlpha(0.35f + 0.65f * static_cast<float>(n.velocity) / 127.0f));
            g.fillRoundedRectangle(getNoteBounds(i), 2.0f);
        }
    }
    // keys
    for (int pitch = 0; pitch < 128; ++pitch) {
        const float y = pitchToY(pitch);
        if (y + kRowHeight < kRulerHeight || y > static_cast<float>(area.getBottom()))
            continue;
        g.setColour(isBlackKey(pitch) ? juce::Colour(0xff202020) : juce::Colour(0xffe8e8e8));
        g.fillRect(juce::Rectangle<float>(0.0f, y, static_cast<float>(kKeyWidth), static_cast<float>(kRowHeight - 1)));
        if (pitch % 12 == 0) {
            g.setColour(juce::Colours::black);
            g.setFont(DawLookAndFeel::uiFont(9.0f));
            g.drawText(pitchName(pitch), juce::Rectangle<float>(2.0f, y, kKeyWidth - 4.0f, static_cast<float>(kRowHeight)),
                       juce::Justification::centredRight);
        }
    }
    // ruler: beats of the material
    g.setColour(DawColours::panel());
    g.fillRect(0, 0, getWidth(), kRulerHeight);
    g.setColour(DawColours::textDim());
    g.setFont(DawLookAndFeel::uiFont(10.0f));
    for (double t = std::floor(viewTick_ / 960.0) * 960.0; tickToX(t) < getWidth(); t += 960.0) {
        const float x = tickToX(t);
        if (x >= kKeyWidth)
            g.drawText(juce::String(static_cast<int>(t / 960.0) + 1), juce::Rectangle<float>(x + 2.0f, 0.0f, 40.0f, kRulerHeight),
                       juce::Justification::centredLeft);
    }
    if (clip == nullptr) {
        g.setColour(DawColours::textDim());
        g.drawText("This clip is gone", area, juce::Justification::centred);
    }
}

PianoRollWindow::PianoRollWindow(Session& session, engine::TrackId track, int clipId, const juce::String& title,
                                 juce::LookAndFeel* lookAndFeel)
    : juce::DocumentWindow(title, DawColours::panel(), juce::DocumentWindow::closeButton) {
    if (lookAndFeel != nullptr)
        setLookAndFeel(lookAndFeel);
    setUsingNativeTitleBar(true);
    auto roll = std::make_unique<PianoRoll>(session, track, clipId);
    roll_ = roll.get();
    setContentOwned(roll.release(), true);
    setResizable(true, false);
}

PianoRollWindow::~PianoRollWindow() {
    clearContentComponent();
    setLookAndFeel(nullptr);
}

void PianoRollWindow::closeButtonPressed() {
    if (onClose)
        onClose();
}

} // namespace dawlab::app
