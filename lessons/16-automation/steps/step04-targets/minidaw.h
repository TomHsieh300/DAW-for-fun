#pragma once
// minidaw.h - Step 4 snapshot of chapter 16: what a curve belongs to.
//
// A curve moves SOMETHING: this track's fader, that compressor's threshold. Tracks get deleted,
// effects get moved to another slot or replaced, projects get saved and opened on another machine.
// So what does a lane say it belongs to?
//
//   AutomationTarget { strip, effect, parameter }
//
//   - the strip: a track BY ID (chapter 15, Step 1), a bus by index, or the master;
//   - the effect: an EffectId, handed out once and never reused, like a TrackId. NOT the slot: after
//     moving the compressor down the chain its curve must still mean the compressor. EffectId 0 means
//     the strip's own controls;
//   - the parameter: BY NAME, as chapter 15's project file saves them, because a later version of an
//     effect may add or reorder its parameters.
//
// Three things can happen to a target:
//
//   Moved                 nothing to do: an id does not care about slots.
//   Its owner is removed  the lanes go with it, inside the same edit, and its inverse brings them
//                         back. So no lane can ever point at a strip or an effect that is gone.
//   Its parameter is      (a file from another version of the effect) the lane stays: silent, but
//   unknown               saved again unchanged, so that opening a project cannot lose a recording.
//
// This snapshot is the model only (the UI thread): Step 5 connects it to the engine.
//
// The format (chapter 15, Step 4), new lines marked:
//
//   DawLabProject 4                    header: the format version (3 has no lanes)
//   sampleRate 48000
//   nextTrackId 5
//   nextEffectId 9
//   track 2 "Bass"                     id, name
//   bus "Reverb"                       name; the index is its place among the buses
//   effect track:2 7 "Compressor"      channel, id, type; the slot is its place on the channel
// * lane 1 track:2 7 "Threshold" touch 0.5
//                                      number, channel, effect id (0: the strip), parameter, mode, default
// * point 1 48000 0.25 linear          lane number, position, value 0..1, shape
//
// The number of a lane means something only inside the file. Points of one lane at one position keep
// the order of their lines: that is how a jump survives. Lines that refer to something are checked
// once the whole file is read, so the order of the lines does not matter otherwise.
//
// YOUR TURN (Step 4): resolve(), applyEdit(), writeLanes(), parseLaneLine(), attachLanes() and
// convertSampleRate() in starter/step04.cpp.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <locale>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace minidaw {

// ---- Step 1 (solved): the curve ------------------------------------------------------------------

enum class Shape { Linear, Hold };

struct AutomationPoint {
    std::int64_t position = 0;
    float value = 0.0f;
    Shape shape = Shape::Linear;
};

inline bool operator==(const AutomationPoint& a, const AutomationPoint& b) {
    return a.position == b.position && a.value == b.value && a.shape == b.shape;
}

class AutomationCurve {
public:
    AutomationCurve() = default;
    explicit AutomationCurve(float defaultValue) : default_(clamp01(defaultValue)) {}

    int add(AutomationPoint point) {
        point.value = clamp01(point.value);
        const auto at = std::upper_bound(points_.begin(), points_.end(), point.position,
                                         [](std::int64_t p, const AutomationPoint& a) { return p < a.position; });
        const auto inserted = points_.insert(at, point);
        return static_cast<int>(inserted - points_.begin());
    }
    float valueAt(std::int64_t position) const noexcept {
        if (points_.empty())
            return default_;
        const auto after = std::upper_bound(points_.begin(), points_.end(), position,
                                            [](std::int64_t p, const AutomationPoint& a) { return p < a.position; });
        const int i = static_cast<int>(after - points_.begin()) - 1;
        if (i < 0)
            return points_.front().value;
        if (i == size() - 1)
            return points_.back().value;
        const AutomationPoint& a = points_[static_cast<size_t>(i)];
        const AutomationPoint& b = points_[static_cast<size_t>(i) + 1];
        if (a.shape == Shape::Hold || a.position == b.position)
            return a.value;
        const double t = static_cast<double>(position - a.position) / static_cast<double>(b.position - a.position);
        return static_cast<float>(a.value + (static_cast<double>(b.value) - a.value) * t);
    }
    const std::vector<AutomationPoint>& points() const noexcept { return points_; }
    // For a conversion that keeps the order (see convertSampleRate).
    std::vector<AutomationPoint>& editPoints() noexcept { return points_; }
    int size() const noexcept { return static_cast<int>(points_.size()); }
    float defaultValue() const noexcept { return default_; }
    static float clamp01(float v) noexcept { return v >= 0.0f ? (v <= 1.0f ? v : 1.0f) : 0.0f; }

    friend bool operator==(const AutomationCurve& a, const AutomationCurve& b) {
        return a.default_ == b.default_ && a.points_ == b.points_;
    }

private:
    std::vector<AutomationPoint> points_;
    float default_ = 0.0f;
};

enum class AutomationMode { Read, Touch, Latch, Write }; // Step 3

// ---- the model ------------------------------------------------------------------------------------

struct TrackId {
    std::uint32_t value = 0;
    constexpr bool isValid() const noexcept { return value != 0; }
    friend constexpr bool operator==(TrackId a, TrackId b) noexcept { return a.value == b.value; }
    friend constexpr bool operator!=(TrackId a, TrackId b) noexcept { return a.value != b.value; }
};

struct EffectId {
    std::uint32_t value = 0; // 0: none, "the strip's own controls"
    constexpr bool isValid() const noexcept { return value != 0; }
    friend constexpr bool operator==(EffectId a, EffectId b) noexcept { return a.value == b.value; }
    friend constexpr bool operator!=(EffectId a, EffectId b) noexcept { return a.value != b.value; }
};

enum class StripKind { Track, Bus, Master };

struct StripRef {
    StripKind kind = StripKind::Master;
    TrackId track; // kind == Track
    int bus = 0;   // kind == Bus

    static StripRef ofTrack(TrackId id) noexcept { return {StripKind::Track, id, 0}; }
    static StripRef ofBus(int index) noexcept { return {StripKind::Bus, TrackId{}, index}; }
    static StripRef master() noexcept { return {}; }
    friend bool operator==(const StripRef& a, const StripRef& b) noexcept {
        return a.kind == b.kind && a.track == b.track && a.bus == b.bus;
    }
};

struct AutomationTarget {
    StripRef strip;
    EffectId effect;       // none: the strip's own controls
    std::string parameter; // by name
    friend bool operator==(const AutomationTarget& a, const AutomationTarget& b) {
        return a.strip == b.strip && a.effect == b.effect && a.parameter == b.parameter;
    }
};

struct AutomationLaneState {
    AutomationTarget target;
    AutomationMode mode = AutomationMode::Read;
    AutomationCurve curve;
    friend bool operator==(const AutomationLaneState& a, const AutomationLaneState& b) {
        return a.target == b.target && a.mode == b.mode && a.curve == b.curve;
    }
};

struct EffectState {
    EffectId id;
    std::string type;
    friend bool operator==(const EffectState& a, const EffectState& b) { return a.id == b.id && a.type == b.type; }
};

struct TrackState {
    TrackId id;
    std::string name;
    std::vector<EffectState> effects; // in slot order
    friend bool operator==(const TrackState& a, const TrackState& b) {
        return a.id == b.id && a.name == b.name && a.effects == b.effects;
    }
};

struct BusState {
    std::string name;
    std::vector<EffectState> effects;
    friend bool operator==(const BusState& a, const BusState& b) { return a.name == b.name && a.effects == b.effects; }
};

struct Project {
    double sampleRate = 48000.0;
    std::uint32_t nextTrackId = 1;
    std::uint32_t nextEffectId = 1;
    std::vector<TrackState> tracks; // in display order
    std::vector<BusState> buses;
    std::vector<EffectState> masterEffects;
    std::vector<AutomationLaneState> lanes; // in the order of laneLess(), so equal projects compare equal

    friend bool operator==(const Project& a, const Project& b) {
        return a.sampleRate == b.sampleRate && a.nextTrackId == b.nextTrackId && a.nextEffectId == b.nextEffectId &&
               a.tracks == b.tracks && a.buses == b.buses && a.masterEffects == b.masterEffects && a.lanes == b.lanes;
    }
    friend bool operator!=(const Project& a, const Project& b) { return !(a == b); }
};

// ---- provided: looking things up --------------------------------------------------------------------

inline int indexOf(const Project& p, TrackId id) noexcept {
    for (size_t i = 0; i < p.tracks.size(); ++i)
        if (p.tracks[i].id == id)
            return static_cast<int>(i);
    return -1;
}

// The effects of a strip, or nullptr if there is no such strip.
inline const std::vector<EffectState>* effectsOf(const Project& p, const StripRef& s) noexcept {
    switch (s.kind) {
    case StripKind::Track: {
        const int i = indexOf(p, s.track);
        return i >= 0 ? &p.tracks[static_cast<size_t>(i)].effects : nullptr;
    }
    case StripKind::Bus:
        return s.bus >= 0 && s.bus < static_cast<int>(p.buses.size()) ? &p.buses[static_cast<size_t>(s.bus)].effects : nullptr;
    case StripKind::Master: return &p.masterEffects;
    }
    return nullptr;
}
inline std::vector<EffectState>* effectsOf(Project& p, const StripRef& s) noexcept {
    return const_cast<std::vector<EffectState>*>(effectsOf(static_cast<const Project&>(p), s));
}

// The slot of an effect on a strip, or -1.
inline int slotOf(const std::vector<EffectState>& effects, EffectId id) noexcept {
    for (size_t i = 0; i < effects.size(); ++i)
        if (effects[i].id == id)
            return static_cast<int>(i);
    return -1;
}

// Where an effect is, on any strip.
struct EffectPlace {
    StripRef strip;
    int slot = -1;
};
inline std::optional<EffectPlace> findEffect(const Project& p, EffectId id) noexcept {
    for (const TrackState& t : p.tracks) {
        const int slot = slotOf(t.effects, id);
        if (slot >= 0)
            return EffectPlace{StripRef::ofTrack(t.id), slot};
    }
    for (size_t b = 0; b < p.buses.size(); ++b) {
        const int slot = slotOf(p.buses[b].effects, id);
        if (slot >= 0)
            return EffectPlace{StripRef::ofBus(static_cast<int>(b)), slot};
    }
    const int slot = slotOf(p.masterEffects, id);
    if (slot >= 0)
        return EffectPlace{StripRef::master(), slot};
    return std::nullopt;
}

// The parameters of the strip itself and of the effects this version knows.
inline std::vector<std::string> stripParameters() { return {"Gain", "Pan"}; }
inline std::vector<std::string> effectParameters(std::string_view type) {
    if (type == "Compressor")
        return {"Threshold", "Ratio", "Attack", "Release"};
    if (type == "Delay")
        return {"Time", "Feedback", "Mix"};
    if (type == "Gain")
        return {"Gain"};
    return {}; // a type this version does not know
}

// The fixed order of the lanes in a project.
inline bool laneLess(const AutomationTarget& a, const AutomationTarget& b) {
    return std::make_tuple(static_cast<int>(a.strip.kind), a.strip.track.value, a.strip.bus, a.effect.value, a.parameter) <
           std::make_tuple(static_cast<int>(b.strip.kind), b.strip.track.value, b.strip.bus, b.effect.value, b.parameter);
}
inline void insertLane(Project& p, AutomationLaneState lane) {
    const auto at = std::lower_bound(p.lanes.begin(), p.lanes.end(), lane.target,
                                     [](const AutomationLaneState& a, const AutomationTarget& t) { return laneLess(a.target, t); });
    p.lanes.insert(at, std::move(lane));
}
inline const AutomationLaneState* findLane(const Project& p, const AutomationTarget& target) noexcept {
    for (const AutomationLaneState& lane : p.lanes)
        if (lane.target == target)
            return &lane;
    return nullptr;
}

// ---- YOUR TURN: what a target means now ----------------------------------------------------------

enum class TargetStatus {
    Live,        // the strip, the effect and the parameter exist
    NoStrip,     // the track was deleted, the bus does not exist
    NoEffect,    // no effect with that id ON THAT STRIP
    NoParameter, // the effect exists, but this version knows no parameter of that name
};

// starter/step04.cpp
TargetStatus resolve(const Project& p, const AutomationTarget& target);

// ---- edits (chapter 15, Step 5) --------------------------------------------------------------------

struct LaneContent {
    AutomationMode mode = AutomationMode::Read;
    AutomationCurve curve;
};
// Sets a lane's content (a recording, a dragged point, another mode), creates the lane, or (no
// content) removes it. Its strip and effect must exist; its parameter may be unknown, because
// undoing the removal of a lane without a known parameter must work.
struct SetLane {
    AutomationTarget target;
    std::optional<LaneContent> content;
};
// Puts the effect at `index` (clamped) of the strip, together with its lanes. The id must be new
// to the project. nextEffectId goes up only: an undone add keeps its id used.
struct AddEffect {
    StripRef strip;
    int index = 0;
    EffectState effect;
    std::vector<AutomationLaneState> lanes;
};
// Removes the effect and its lanes.
struct RemoveEffect {
    EffectId id;
};
// Moves the effect to `index` (clamped) of its strip. Its lanes do not change: that is the point.
struct MoveEffect {
    EffectId id;
    int index = 0;
};
// Puts the track at `index` (clamped), together with all the lanes on it.
struct AddTrack {
    TrackState track;
    int index = 0;
    std::vector<AutomationLaneState> lanes;
};
// Removes the track and all the lanes on it, those of its effects too.
struct RemoveTrack {
    TrackId id;
};

using Edit = std::variant<SetLane, AddEffect, RemoveEffect, MoveEffect, AddTrack, RemoveTrack>;
using Change = std::vector<Edit>;

inline AddTrack newTrack(const Project& p, std::string name) {
    return AddTrack{TrackState{TrackId{p.nextTrackId}, std::move(name), {}}, static_cast<int>(p.tracks.size()), {}};
}
inline AddEffect newEffect(const Project& p, StripRef strip, int index, std::string type) {
    return AddEffect{strip, index, EffectState{EffectId{p.nextEffectId}, std::move(type)}, {}};
}

// YOUR TURN (starter/step04.cpp): applies the edit and returns the edit that undoes it; std::nullopt
// with `p` unchanged if it does not apply (see above; a remove or a move of what does not exist).
std::optional<Edit> applyEdit(Project& p, const Edit& edit);

// Provided, as in chapter 15: a whole change, all or nothing.
inline bool applyAll(Project& p, const Change& change, Change& inverses) {
    inverses.clear();
    for (const Edit& e : change) {
        std::optional<Edit> inverse = applyEdit(p, e);
        if (!inverse) {
            for (auto it = inverses.rbegin(); it != inverses.rend(); ++it)
                applyEdit(p, *it);
            inverses.clear();
            return false;
        }
        inverses.push_back(std::move(*inverse));
    }
    return true;
}

// The history of chapter 15, Step 5 (provided).
class History {
public:
    explicit History(std::size_t limit = 100) : limit_(std::max<std::size_t>(limit, 1)) {}

    bool perform(Project& p, const Change& change, std::uint32_t mergeKey = 0) {
        Change inverses;
        if (!applyAll(p, change, inverses))
            return false;
        redo_.clear();
        if (mergeKey != 0 && mergeKey == openMergeKey_ && !undo_.empty())
            return true;
        undo_.push_back(Entry{std::move(inverses), mergeKey});
        if (undo_.size() > limit_)
            undo_.pop_front();
        openMergeKey_ = mergeKey;
        return true;
    }
    bool undo(Project& p) {
        openMergeKey_ = 0;
        if (undo_.empty())
            return false;
        const Change reversed(undo_.back().edits.rbegin(), undo_.back().edits.rend());
        Change inverses;
        const bool applied = applyAll(p, reversed, inverses);
        undo_.pop_back();
        if (!applied)
            return false;
        std::reverse(inverses.begin(), inverses.end());
        redo_.push_back(Entry{std::move(inverses), 0});
        return true;
    }
    bool redo(Project& p) {
        openMergeKey_ = 0;
        if (redo_.empty())
            return false;
        Change inverses;
        const bool applied = applyAll(p, redo_.back().edits, inverses);
        Entry entry{std::move(inverses), 0};
        redo_.pop_back();
        if (!applied)
            return false;
        undo_.push_back(std::move(entry));
        if (undo_.size() > limit_)
            undo_.pop_front();
        return true;
    }
    void endMerge() noexcept { openMergeKey_ = 0; }
    std::size_t undoCount() const noexcept { return undo_.size(); }

private:
    struct Entry {
        Change edits;
        std::uint32_t mergeKey = 0;
    };
    std::deque<Entry> undo_;
    std::vector<Entry> redo_;
    std::size_t limit_;
    std::uint32_t openMergeKey_ = 0;
};

// ---- the file ------------------------------------------------------------------------------------

constexpr int kFormatVersion = 4; // 3: no lanes; 4: lanes and points

struct Token {
    std::string text;
    bool quoted = false;
};

// As in chapter 15, Step 4: words, or "strings" with the escapes \" \\ \n; '#' starts a comment.
inline bool splitLine(std::string_view line, std::vector<Token>& out, std::string& error) {
    out.clear();
    size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (c == ' ' || c == '\t') {
            ++i;
        } else if (c == '#') {
            break;
        } else if (c == '"') {
            Token t{std::string{}, true};
            ++i;
            bool closed = false;
            while (i < line.size()) {
                const char d = line[i++];
                if (d == '"') {
                    closed = true;
                    break;
                }
                if (d == '\\') {
                    if (i == line.size())
                        break;
                    const char e = line[i++];
                    if (e == '"' || e == '\\')
                        t.text += e;
                    else if (e == 'n')
                        t.text += '\n';
                    else {
                        error = std::string("unknown escape \\") + e;
                        return false;
                    }
                } else {
                    t.text += d;
                }
            }
            if (!closed) {
                error = "unterminated string";
                return false;
            }
            out.push_back(std::move(t));
        } else {
            const size_t start = i;
            while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '"' && line[i] != '#')
                ++i;
            out.push_back(Token{std::string(line.substr(start, i - start)), false});
        }
    }
    return true;
}

inline std::string quote(std::string_view s) {
    std::string q = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\')
            q += '\\', q += c;
        else if (c == '\n')
            q += "\\n";
        else
            q += c;
    }
    return q + "\"";
}

template <typename T>
inline bool parseNumber(const Token& token, T& out) {
    if (token.quoted || token.text.empty())
        return false;
    std::istringstream in(token.text);
    in.imbue(std::locale::classic());
    T value{};
    in >> value;
    if (in.fail() || in.peek() != std::char_traits<char>::eof())
        return false;
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(value))
            return false;
    } else if constexpr (std::is_unsigned_v<T>) {
        if (token.text[0] == '-')
            return false;
    }
    out = value;
    return true;
}

inline std::string channelText(const StripRef& s) {
    switch (s.kind) {
    case StripKind::Track: return "track:" + std::to_string(s.track.value);
    case StripKind::Bus: return "bus:" + std::to_string(s.bus);
    case StripKind::Master: return "master";
    }
    return "master";
}
inline bool parseChannel(const Token& token, StripRef& out) {
    if (token.quoted)
        return false;
    if (token.text == "master") {
        out = StripRef::master();
        return true;
    }
    const auto colon = token.text.find(':');
    if (colon == std::string::npos)
        return false;
    const Token number{token.text.substr(colon + 1), false};
    const std::string kind = token.text.substr(0, colon);
    if (kind == "track") {
        TrackId id;
        if (!parseNumber(number, id.value) || !id.isValid())
            return false;
        out = StripRef::ofTrack(id);
        return true;
    }
    int bus = 0;
    if (kind == "bus" && parseNumber(number, bus) && bus >= 0) {
        out = StripRef::ofBus(bus);
        return true;
    }
    return false;
}

inline const char* modeText(AutomationMode m) noexcept {
    switch (m) {
    case AutomationMode::Read: return "read";
    case AutomationMode::Touch: return "touch";
    case AutomationMode::Latch: return "latch";
    case AutomationMode::Write: return "write";
    }
    return "read";
}
inline bool parseMode(std::string_view text, AutomationMode& out) noexcept {
    for (const AutomationMode m : {AutomationMode::Read, AutomationMode::Touch, AutomationMode::Latch, AutomationMode::Write})
        if (text == modeText(m)) {
            out = m;
            return true;
        }
    return false;
}
inline const char* shapeText(Shape s) noexcept { return s == Shape::Hold ? "hold" : "linear"; }
inline bool parseShape(std::string_view text, Shape& out) noexcept {
    if (text == "linear" || text == "hold") {
        out = text == "hold" ? Shape::Hold : Shape::Linear;
        return true;
    }
    return false;
}

// What readProject() holds until the whole file is read.
struct PendingLane {
    int number = 0; // only means something inside the file
    int line = 0;
    AutomationLaneState lane;
};
struct PendingPoint {
    int laneNumber = 0;
    int line = 0;
    AutomationPoint point;
};

// ---- YOUR TURN (starter/step04.cpp) ---------------------------------------------------------------

// The "lane" and "point" lines of the project. `out` already has the classic locale and 9 digits.
void writeLanes(std::ostream& out, const Project& project);

// One "lane" or "point" line (tok[0] says which): checks ONLY the line itself and adds it to `lanes`
// or `points`. False and `error` (no line number) if the values are wrong. Outside data: a value out
// of 0..1 is an error, not something to clamp.
bool parseLaneLine(const std::vector<Token>& tok, int line, std::vector<PendingLane>& lanes,
                   std::vector<PendingPoint>& points, std::string& error);

// After the whole file: checks what the lines refer to and puts the lanes into the project. Numbers
// used twice, a strip or an effect that does not exist, two lanes with one target, a point for a lane
// that does not exist: false, with `error` and `errorLine`. An unknown parameter is NOT an error.
bool attachLanes(Project& p, const std::vector<PendingLane>& lanes, const std::vector<PendingPoint>& points,
                 std::string& error, int& errorLine);

// Every position of every curve to newRate (if > 0 and different); values, order and jumps stay.
void convertSampleRate(Project& project, double newRate);

// ---- provided: the rest of the file -----------------------------------------------------------------

inline void writeEffects(std::ostream& out, const StripRef& strip, const std::vector<EffectState>& effects) {
    for (const EffectState& e : effects)
        out << "effect " << channelText(strip) << ' ' << e.id.value << ' ' << quote(e.type) << '\n';
}

inline std::string writeProject(const Project& project) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(std::numeric_limits<float>::max_digits10);
    out << "DawLabProject " << kFormatVersion << '\n';
    out << "sampleRate " << project.sampleRate << '\n';
    out << "nextTrackId " << project.nextTrackId << '\n';
    out << "nextEffectId " << project.nextEffectId << '\n';
    for (const TrackState& t : project.tracks)
        out << "track " << t.id.value << ' ' << quote(t.name) << '\n';
    for (const BusState& b : project.buses)
        out << "bus " << quote(b.name) << '\n';
    for (const TrackState& t : project.tracks)
        writeEffects(out, StripRef::ofTrack(t.id), t.effects);
    for (size_t b = 0; b < project.buses.size(); ++b)
        writeEffects(out, StripRef::ofBus(static_cast<int>(b)), project.buses[b].effects);
    writeEffects(out, StripRef::master(), project.masterEffects);
    writeLanes(out, project);
    return out.str();
}

struct LoadResult {
    bool ok = false;
    Project project;
    std::string error; // "line 4: ..."
};

inline LoadResult readProject(std::string_view text, double targetSampleRate = 0.0) {
    auto fail = [](int line, const std::string& message) {
        LoadResult r;
        r.error = "line " + std::to_string(line > 0 ? line : 1) + ": " + message;
        return r;
    };
    struct PendingEffect {
        int line = 0;
        StripRef strip;
        EffectState effect;
    };
    Project p;
    p.nextTrackId = 0;
    p.nextEffectId = 0;
    int version = 0, lineNo = 0;
    std::vector<PendingEffect> effects;
    std::vector<PendingLane> lanes;
    std::vector<PendingPoint> points;
    std::vector<Token> tok;
    std::string error;

    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (!splitLine(line, tok, error))
            return fail(lineNo, error);
        if (tok.empty())
            continue;
        const std::string& kw = tok[0].text;
        if (version == 0) {
            if (kw != "DawLabProject" || tok.size() != 2 || !parseNumber(tok[1], version))
                return fail(lineNo, "not a DawLab project");
            if (version > kFormatVersion || version < 3)
                return fail(lineNo, "this version reads formats 3 and 4, not " + std::to_string(version));
            continue;
        }
        if (kw == "sampleRate") {
            if (tok.size() != 2 || !parseNumber(tok[1], p.sampleRate) || p.sampleRate <= 0.0)
                return fail(lineNo, "sampleRate needs one positive number");
        } else if (kw == "nextTrackId") {
            if (tok.size() != 2 || !parseNumber(tok[1], p.nextTrackId))
                return fail(lineNo, "nextTrackId needs one whole number");
        } else if (kw == "nextEffectId") {
            if (tok.size() != 2 || !parseNumber(tok[1], p.nextEffectId))
                return fail(lineNo, "nextEffectId needs one whole number");
        } else if (kw == "track") {
            TrackState t;
            if (tok.size() != 3 || !parseNumber(tok[1], t.id.value) || !t.id.isValid() || !tok[2].quoted)
                return fail(lineNo, "track needs an id and a name in quotes");
            if (indexOf(p, t.id) >= 0)
                return fail(lineNo, "track id " + tok[1].text + " used twice");
            t.name = tok[2].text;
            p.tracks.push_back(std::move(t));
        } else if (kw == "bus") {
            if (tok.size() != 2 || !tok[1].quoted)
                return fail(lineNo, "bus needs a name in quotes");
            p.buses.push_back(BusState{tok[1].text, {}});
        } else if (kw == "effect") {
            PendingEffect e;
            e.line = lineNo;
            if (tok.size() != 4 || !parseChannel(tok[1], e.strip) || !parseNumber(tok[2], e.effect.id.value) ||
                !e.effect.id.isValid() || !tok[3].quoted)
                return fail(lineNo, "effect needs a channel, an id and a type in quotes");
            e.effect.type = tok[3].text;
            effects.push_back(std::move(e));
        } else if (kw == "lane" || kw == "point") {
            if (version < 4)
                return fail(lineNo, kw + " needs format 4");
            if (!parseLaneLine(tok, lineNo, lanes, points, error))
                return fail(lineNo, error);
        } else {
            return fail(lineNo, "unknown keyword \"" + kw + "\"");
        }
    }
    if (version == 0)
        return fail(lineNo, "not a DawLab project (empty file)");

    for (const PendingEffect& e : effects) {
        if (findEffect(p, e.effect.id))
            return fail(e.line, "effect id " + std::to_string(e.effect.id.value) + " used twice");
        std::vector<EffectState>* strip = effectsOf(p, e.strip);
        if (strip == nullptr)
            return fail(e.line, "effect on " + channelText(e.strip) + ", which does not exist");
        strip->push_back(e.effect);
    }
    int errorLine = lineNo;
    if (!attachLanes(p, lanes, points, error, errorLine))
        return fail(errorLine, error);

    std::uint32_t maxTrack = 0, maxEffect = 0;
    for (const TrackState& t : p.tracks)
        maxTrack = std::max(maxTrack, t.id.value);
    for (const PendingEffect& e : effects)
        maxEffect = std::max(maxEffect, e.effect.id.value);
    p.nextTrackId = std::max(p.nextTrackId, maxTrack + 1);
    p.nextEffectId = std::max(p.nextEffectId, maxEffect + 1);

    convertSampleRate(p, targetSampleRate);
    LoadResult r;
    r.ok = true;
    r.project = std::move(p);
    return r;
}

} // namespace minidaw
