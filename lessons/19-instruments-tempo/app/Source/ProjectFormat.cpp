#include "ProjectFormat.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>
#include <type_traits>

namespace dawlab::app::project {

using engine::TrackId;

// ---- comparisons (round-trip tests) ---------------------------------------------------------------

bool operator==(const SendState& a, const SendState& b) { return a.levelDb == b.levelDb && a.preFader == b.preFader; }
bool operator==(const ParamState& a, const ParamState& b) { return a.name == b.name && a.value == b.value; }
bool operator==(const InsertState& a, const InsertState& b) {
    return a.slot == b.slot && a.vst3 == b.vst3 && a.type == b.type && a.bypassed == b.bypassed && a.params == b.params &&
           a.state == b.state;
}
bool operator==(const PointState& a, const PointState& b) {
    return a.position == b.position && a.value == b.value && a.hold == b.hold;
}
bool operator==(const LaneState& a, const LaneState& b) {
    return a.slot == b.slot && a.parameter == b.parameter && a.mode == b.mode && a.defaultValue == b.defaultValue && a.points == b.points;
}
bool operator==(const StripState& a, const StripState& b) {
    return a.name == b.name && a.gainDb == b.gainDb && a.pan == b.pan && a.mute == b.mute && a.solo == b.solo &&
           a.sends == b.sends && a.inserts == b.inserts && a.lanes == b.lanes;
}
bool operator==(const TrackState& a, const TrackState& b) {
    return a.id == b.id && a.format == b.format && a.strip == b.strip && a.instrument == b.instrument;
}
bool operator==(const MidiClipState& a, const MidiClipState& b) {
    auto sameControllers = [](const std::vector<engine::ControllerPoint>& x, const std::vector<engine::ControllerPoint>& y) {
        if (x.size() != y.size())
            return false;
        for (size_t i = 0; i < x.size(); ++i)
            if (x[i].tick != y[i].tick || x[i].number != y[i].number || x[i].value != y[i].value || x[i].channel != y[i].channel)
                return false;
        return true;
    };
    return a.track == b.track && a.start == b.start && a.length == b.length && a.offset == b.offset && a.muted == b.muted &&
           a.name == b.name && a.grid == b.grid && a.strength == b.strength && a.swing == b.swing && a.notes == b.notes &&
           sameControllers(a.controllers, b.controllers);
}
bool operator==(const ClipState& a, const ClipState& b) {
    return a.track == b.track && a.start == b.start && a.length == b.length && a.file == b.file &&
           a.sourceOffset == b.sourceOffset && a.fadeIn == b.fadeIn && a.fadeOut == b.fadeOut &&
           a.equalPowerFades == b.equalPowerFades && a.gainDb == b.gainDb && a.muted == b.muted && a.name == b.name;
}
bool operator==(const TakeState& a, const TakeState& b) {
    return a.start == b.start && a.sourceOffset == b.sourceOffset && a.length == b.length;
}
bool operator==(const TakeSetState& a, const TakeSetState& b) { return a.file == b.file && a.takes == b.takes; }
bool operator==(const TempoPointState& a, const TempoPointState& b) { return a.tick == b.tick && a.bpm == b.bpm && a.ramp == b.ramp; }
bool operator==(const ProjectState& a, const ProjectState& b) {
    return a.sampleRate == b.sampleRate && a.nextTrackId == b.nextTrackId && a.tempo == b.tempo &&
           a.loopInTicks == b.loopInTicks && a.tempoPoints == b.tempoPoints &&
           a.beatsPerBar == b.beatsPerBar && a.beatUnit == b.beatUnit && a.loopStart == b.loopStart &&
           a.loopEnd == b.loopEnd && a.loopEnabled == b.loopEnabled && a.tracks == b.tracks && a.buses == b.buses &&
           a.master == b.master && a.clips == b.clips && a.takeSets == b.takeSets && a.midiClips == b.midiClips;
}

namespace {

// ---- tokens: Step 4's splitLine / quote / parseNumber ----------------------------------------------

struct Token {
    std::string text; // without the quotes, escapes resolved
    bool quoted = false;
};

bool splitLine(std::string_view line, std::vector<Token>& out, std::string& error) {
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

std::string quote(std::string_view s) {
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
bool parseNumber(const Token& token, T& out) {
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
        if (token.text[0] == '-') // istream would wrap "-1" around to a huge unsigned value
            return false;
    }
    out = value;
    return true;
}

bool parseFlag(const Token& token, bool& out) {
    if (token.quoted || (token.text != "0" && token.text != "1"))
        return false;
    out = token.text == "1";
    return true;
}

// ---- channels: "track:17", "bus:1", "master" ---------------------------------------------------------

struct ChannelName {
    enum class Kind { Track, Bus, Master } kind = Kind::Master;
    std::uint32_t index = 0; // track id or bus index
};

std::string channelText(ChannelName c) {
    switch (c.kind) {
    case ChannelName::Kind::Track: return "track:" + std::to_string(c.index);
    case ChannelName::Kind::Bus: return "bus:" + std::to_string(c.index);
    case ChannelName::Kind::Master: return "master";
    }
    return "?";
}

bool parseChannel(const Token& token, ChannelName& out) {
    if (token.quoted)
        return false;
    const std::string& t = token.text;
    if (t == "master") {
        out = {ChannelName::Kind::Master, 0};
        return true;
    }
    const auto colon = t.find(':');
    if (colon == std::string::npos)
        return false;
    const std::string kind = t.substr(0, colon);
    Token number{t.substr(colon + 1), false};
    if (kind == "track" && parseNumber(number, out.index) && out.index != 0) {
        out.kind = ChannelName::Kind::Track;
        return true;
    }
    if (kind == "bus" && parseNumber(number, out.index) && out.index < static_cast<std::uint32_t>(engine::kNumBuses)) {
        out.kind = ChannelName::Kind::Bus;
        return true;
    }
    return false;
}

// ---- automation modes -----------------------------------------------------------------------------

const char* modeText(engine::AutomationMode m) noexcept {
    switch (m) {
    case engine::AutomationMode::Read: return "read";
    case engine::AutomationMode::Touch: return "touch";
    case engine::AutomationMode::Latch: return "latch";
    case engine::AutomationMode::Write: return "write";
    }
    return "read";
}

bool parseMode(const std::string& text, engine::AutomationMode& out) noexcept {
    for (const engine::AutomationMode m : {engine::AutomationMode::Read, engine::AutomationMode::Touch, engine::AutomationMode::Latch,
                                           engine::AutomationMode::Write})
        if (text == modeText(m)) {
            out = m;
            return true;
        }
    return false;
}

// ---- writing --------------------------------------------------------------------------------------

// `number` counts the lanes through the file: a lane's number means something only inside it.
void writeLanes(std::ostream& out, ChannelName channel, const StripState& s, int& number) {
    for (const LaneState& lane : s.lanes) {
        ++number;
        out << "lane " << number << ' ' << channelText(channel) << ' ';
        if (lane.slot < 0)
            out << '-';
        else
            out << lane.slot;
        out << ' ' << quote(lane.parameter) << ' ' << modeText(lane.mode) << ' ' << lane.defaultValue << '\n';
        for (const PointState& point : lane.points)
            out << "point " << number << ' ' << point.position << ' ' << point.value << ' ' << (point.hold ? "hold" : "linear") << '\n';
    }
}

void writeStripDetails(std::ostream& out, ChannelName channel, const StripState& s) {
    const std::string ch = channelText(channel);
    for (int b = 0; b < engine::kNumBuses; ++b) {
        const SendState& send = s.sends[static_cast<size_t>(b)];
        if (send.levelDb > kMinusInfinityDb || send.preFader) // an unused send is the default
            out << "send " << ch << ' ' << b << ' ' << send.levelDb << ' ' << send.preFader << '\n';
    }
    for (const InsertState& ins : s.inserts) {
        out << "insert " << ch << ' ' << ins.slot << ' ' << (ins.vst3 ? "vst3 " : "builtin ") << quote(ins.type) << ' '
            << ins.bypassed;
        if (ins.vst3)
            out << ' ' << quote(ins.state);
        out << '\n';
        for (const ParamState& p : ins.params)
            out << "param " << ch << ' ' << ins.slot << ' ' << quote(p.name) << ' ' << p.value << '\n';
    }
}

} // namespace

std::string writeProject(const ProjectState& p) {
    std::ostringstream out;
    out.imbue(std::locale::classic()); // "-6.5" and "48000" in every language, no digit grouping
    out.precision(std::numeric_limits<float>::max_digits10); // 9 digits: every float reads back exactly
    out << "DawLabProject " << kFormatVersion << '\n';
    out << "sampleRate " << p.sampleRate << '\n';
    out << "nextTrackId " << p.nextTrackId << '\n';
    out << "tempo " << p.tempo << ' ' << p.beatsPerBar << ' ' << p.beatUnit << '\n';
    out << "loop " << p.loopStart << ' ' << p.loopEnd << ' ' << p.loopEnabled << (p.loopInTicks ? " ticks" : "") << '\n';
    {
        const auto digits = out.precision(std::numeric_limits<double>::max_digits10); // a tempo is a double
        for (const TempoPointState& t : p.tempoPoints)
            out << "tempopoint " << t.tick << ' ' << t.bpm << ' ' << t.ramp << '\n';
        out.precision(digits);
    }
    for (const TrackState& t : p.tracks)
        out << "track " << t.id.value << ' ' << quote(t.strip.name) << ' ' << t.strip.gainDb << ' ' << t.strip.pan << ' '
            << (t.format == engine::TrackFormat::Mono ? "mono" : "stereo") << ' ' << t.strip.mute << ' ' << t.strip.solo
            << '\n';
    for (int b = 0; b < engine::kNumBuses; ++b) {
        const StripState& s = p.buses[static_cast<size_t>(b)];
        out << "bus " << b << ' ' << quote(s.name) << ' ' << s.gainDb << ' ' << s.pan << ' ' << s.mute << '\n';
    }
    out << "master " << quote(p.master.name) << ' ' << p.master.gainDb << ' ' << p.master.pan << ' ' << p.master.mute << '\n';
    for (const TrackState& t : p.tracks)
        writeStripDetails(out, {ChannelName::Kind::Track, t.id.value}, t.strip);
    for (int b = 0; b < engine::kNumBuses; ++b)
        writeStripDetails(out, {ChannelName::Kind::Bus, static_cast<std::uint32_t>(b)}, p.buses[static_cast<size_t>(b)]);
    writeStripDetails(out, {ChannelName::Kind::Master, 0}, p.master);
    for (const ClipState& c : p.clips)
        out << "clip " << c.track.value << ' ' << c.start << ' ' << c.length << ' ' << quote(c.file) << ' ' << c.sourceOffset
            << ' ' << c.fadeIn << ' ' << c.fadeOut << ' ' << (c.equalPowerFades ? "equalPower" : "linear") << ' ' << c.gainDb
            << ' ' << c.muted << ' ' << quote(c.name) << '\n';
    for (const TakeSetState& s : p.takeSets) {
        out << "takes " << quote(s.file);
        for (const TakeState& t : s.takes)
            out << ' ' << t.start << ' ' << t.sourceOffset << ' ' << t.length;
        out << '\n';
    }
    for (const TrackState& t : p.tracks)
        if (t.instrument)
            out << "instrument " << t.id.value << '\n';
    int clipNumber = 0;
    for (const MidiClipState& c : p.midiClips) {
        ++clipNumber;
        out << "midiclip " << c.track.value << ' ' << clipNumber << ' ' << c.start << ' ' << c.length << ' ' << c.offset << ' '
            << c.muted << ' ' << quote(c.name) << ' ' << c.grid << ' ' << c.strength << ' ' << c.swing << '\n';
        for (const engine::Note& n : c.notes)
            out << "note " << clipNumber << ' ' << n.start << ' ' << n.length << ' ' << n.pitch << ' ' << n.velocity << ' ' << n.channel
                << '\n';
        for (const engine::ControllerPoint& cc : c.controllers)
            out << "cc " << clipNumber << ' ' << cc.tick << ' ' << cc.number << ' ' << cc.value << ' ' << cc.channel << '\n';
    }
    int laneNumber = 0;
    for (const TrackState& t : p.tracks)
        writeLanes(out, {ChannelName::Kind::Track, t.id.value}, t.strip, laneNumber);
    for (int b = 0; b < engine::kNumBuses; ++b)
        writeLanes(out, {ChannelName::Kind::Bus, static_cast<std::uint32_t>(b)}, p.buses[static_cast<size_t>(b)], laneNumber);
    writeLanes(out, {ChannelName::Kind::Master, 0}, p.master, laneNumber);
    return out.str();
}

// ---- reading ------------------------------------------------------------------------------------------

ReadResult readProject(std::string_view text) {
    auto fail = [](int line, const std::string& message) {
        ReadResult r;
        r.error = "line " + std::to_string(line > 0 ? line : 1) + ": " + message;
        return r;
    };
    int lineNo = 0;

    if (text.substr(0, 3) == "\xEF\xBB\xBF")
        text.remove_prefix(3); // a UTF-8 byte order mark (Notepad)

    ProjectState p;
    p.sampleRate = 0.0;
    p.nextTrackId = 0;
    int version = 0;
    bool haveRate = false;
    std::array<bool, engine::kNumBuses> haveBus{};
    bool haveMaster = false;

    // Lines that refer to a channel are resolved after the last line (any order is fine).
    struct Pending {
        int line = 0;
        ChannelName channel;
        SendState send;
        int bus = 0;
        InsertState insert;
        ParamState param;
    };
    std::vector<Pending> sends, inserts, params;
    struct PendingLane {
        int line = 0;
        int number = 0;
        ChannelName channel;
        LaneState lane;
    };
    struct PendingPoint {
        int line = 0;
        int number = 0;
        PointState point;
    };
    std::vector<PendingLane> lanes;
    std::vector<PendingPoint> points;
    std::vector<int> clipLines;
    // Format 6: MIDI clips by their number in the file, and what refers to them.
    std::vector<int> midiClipNumbers, midiClipLines;
    struct PendingNote {
        int line = 0, clip = 0;
        engine::Note note;
    };
    struct PendingCc {
        int line = 0, clip = 0;
        engine::ControllerPoint cc;
    };
    std::vector<PendingNote> notes;
    std::vector<PendingCc> ccs;
    std::vector<std::pair<int, engine::TrackId>> instruments; // line, track

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
            line.remove_suffix(1); // CRLF
        if (!splitLine(line, tok, error))
            return fail(lineNo, error);
        if (tok.empty())
            continue; // blank or comment
        const std::string& kw = tok[0].text;
        if (tok[0].quoted)
            return fail(lineNo, "a line must start with a keyword");

        if (version == 0) { // the first item must be the header
            if (kw != "DawLabProject" || tok.size() != 2)
                return fail(lineNo, "not a DawLab project (no \"DawLabProject <version>\" header)");
            if (!parseNumber(tok[1], version) || version < 1)
                return fail(lineNo, "bad format version \"" + tok[1].text + "\"");
            if (version > kFormatVersion)
                return fail(lineNo, "made by a newer DawLab (format " + std::to_string(version) + ", this one reads up to " +
                                        std::to_string(kFormatVersion) + ")");
            continue;
        }
        if (!haveRate && kw != "sampleRate")
            return fail(lineNo, "sampleRate must come right after the header");
        if (version < 3 && kw != "sampleRate" && kw != "nextTrackId" && kw != "track" && kw != "clip")
            return fail(lineNo, "\"" + kw + "\" needs format 3 (this file is format " + std::to_string(version) + ")");

        if (kw == "sampleRate") {
            if (haveRate)
                return fail(lineNo, "sampleRate given twice");
            if (tok.size() != 2 || !parseNumber(tok[1], p.sampleRate) || p.sampleRate <= 0.0)
                return fail(lineNo, "sampleRate needs one positive number");
            haveRate = true;
        } else if (kw == "nextTrackId") {
            if (tok.size() != 2 || !parseNumber(tok[1], p.nextTrackId))
                return fail(lineNo, "nextTrackId needs one whole number");
        } else if (kw == "tempo") {
            if (tok.size() != 4 || !parseNumber(tok[1], p.tempo) || p.tempo < 20.0 || p.tempo > 999.0)
                return fail(lineNo, "tempo needs BPM (20 .. 999), beats per bar and beat unit");
            if (!parseNumber(tok[2], p.beatsPerBar) || p.beatsPerBar < 1 || p.beatsPerBar > 32)
                return fail(lineNo, "beats per bar must be 1 .. 32");
            if (!parseNumber(tok[3], p.beatUnit) || p.beatUnit < 1 || p.beatUnit > 32 || (p.beatUnit & (p.beatUnit - 1)) != 0)
                return fail(lineNo, "the beat unit must be 1, 2, 4, 8, 16 or 32");
        } else if (kw == "loop") {
            const bool ticks = version >= 7 && tok.size() == 5 && tok[4].text == "ticks";
            if ((tok.size() != 4 && !ticks) || !parseNumber(tok[1], p.loopStart) || !parseNumber(tok[2], p.loopEnd) ||
                !parseFlag(tok[3], p.loopEnabled) || p.loopStart < 0 || p.loopEnd < p.loopStart)
                return fail(lineNo, version >= 7 ? "loop needs start >= 0, end >= start, 0/1 and, for a loop in ticks, \"ticks\""
                                                 : "loop needs start >= 0, end >= start (samples) and 0/1");
            p.loopInTicks = ticks;
        } else if (kw == "tempopoint") {
            TempoPointState t;
            if (version < 7)
                return fail(lineNo, "tempopoint needs format 7");
            if (tok.size() != 4 || !parseNumber(tok[1], t.tick) || !parseNumber(tok[2], t.bpm) || !parseFlag(tok[3], t.ramp) ||
                t.bpm < 20.0 || t.bpm > 999.0)
                return fail(lineNo, "tempopoint needs a tick, BPM (20 .. 999) and 0/1");
            if (p.tempoPoints.empty() ? t.tick != 0 : t.tick <= p.tempoPoints.back().tick)
                return fail(lineNo, "tempo points start at tick 0, each after the one before");
            if (static_cast<int>(p.tempoPoints.size()) == kMaxTempoPoints)
                return fail(lineNo, "more than " + std::to_string(kMaxTempoPoints) + " tempo points");
            p.tempoPoints.push_back(t);
        } else if (kw == "track") {
            const size_t expected = version >= 3 ? 8 : version == 2 ? 5 : 4; // 1: no pan; 3: format, mute, solo
            if (tok.size() != expected)
                return fail(lineNo, "track needs " + std::to_string(expected - 1) + " values in format " + std::to_string(version));
            TrackState t;
            if (!parseNumber(tok[1], t.id.value) || !t.id.isValid())
                return fail(lineNo, "bad track id \"" + tok[1].text + "\"");
            if (!tok[2].quoted)
                return fail(lineNo, "the track name must be in quotes");
            t.strip.name = tok[2].text;
            if (!parseNumber(tok[3], t.strip.gainDb))
                return fail(lineNo, "bad gain \"" + tok[3].text + "\"");
            if (version >= 2 && (!parseNumber(tok[4], t.strip.pan) || t.strip.pan < -1.0f || t.strip.pan > 1.0f))
                return fail(lineNo, "pan must be a number from -1 to 1");
            if (version >= 3) {
                if (tok[5].quoted || (tok[5].text != "mono" && tok[5].text != "stereo"))
                    return fail(lineNo, "the track format must be mono or stereo");
                t.format = tok[5].text == "mono" ? engine::TrackFormat::Mono : engine::TrackFormat::Stereo;
                if (!parseFlag(tok[6], t.strip.mute) || !parseFlag(tok[7], t.strip.solo))
                    return fail(lineNo, "mute and solo must be 0 or 1");
            }
            for (const TrackState& other : p.tracks)
                if (other.id == t.id)
                    return fail(lineNo, "track id " + tok[1].text + " used twice");
            p.tracks.push_back(std::move(t));
        } else if (kw == "bus" || kw == "master") {
            const bool master = kw == "master";
            const size_t first = master ? 1 : 2; // where the name is
            if (tok.size() != first + 4)
                return fail(lineNo, kw + " needs " + std::to_string(first + 3) + " values");
            int b = 0;
            if (!master && (!parseNumber(tok[1], b) || b < 0 || b >= engine::kNumBuses))
                return fail(lineNo, "bus index must be 0 .. " + std::to_string(engine::kNumBuses - 1));
            bool& seen = master ? haveMaster : haveBus[static_cast<size_t>(b)];
            if (seen)
                return fail(lineNo, master ? std::string("master given twice") : "bus " + tok[1].text + " given twice");
            seen = true;
            StripState& s = master ? p.master : p.buses[static_cast<size_t>(b)];
            if (!tok[first].quoted)
                return fail(lineNo, "the name must be in quotes");
            s.name = tok[first].text;
            if (!parseNumber(tok[first + 1], s.gainDb) || !parseNumber(tok[first + 2], s.pan) || s.pan < -1.0f || s.pan > 1.0f)
                return fail(lineNo, "bad gain or pan");
            if (!parseFlag(tok[first + 3], s.mute))
                return fail(lineNo, "mute must be 0 or 1");
        } else if (kw == "send") {
            Pending s;
            s.line = lineNo;
            if (tok.size() != 5 || !parseChannel(tok[1], s.channel))
                return fail(lineNo, "send needs a channel, a bus, a level and 0/1 for pre-fader");
            if (!parseNumber(tok[2], s.bus) || s.bus < 0 || s.bus >= engine::kNumBuses)
                return fail(lineNo, "bus index must be 0 .. " + std::to_string(engine::kNumBuses - 1));
            if (!parseNumber(tok[3], s.send.levelDb) || !parseFlag(tok[4], s.send.preFader))
                return fail(lineNo, "bad send level or pre-fader flag");
            sends.push_back(s);
        } else if (kw == "insert") {
            Pending s;
            s.line = lineNo;
            if (tok.size() < 6 || !parseChannel(tok[1], s.channel))
                return fail(lineNo, "insert needs a channel, a slot, a kind, an effect and 0/1 for bypassed");
            if (!parseNumber(tok[2], s.insert.slot) || s.insert.slot < 0 || s.insert.slot >= kInsertSlotsPerStrip)
                return fail(lineNo, "slot must be 0 .. " + std::to_string(kInsertSlotsPerStrip - 1));
            if (tok[3].quoted || (tok[3].text != "builtin" && tok[3].text != "vst3"))
                return fail(lineNo, "the kind must be builtin or vst3");
            s.insert.vst3 = tok[3].text == "vst3";
            if (tok.size() != (s.insert.vst3 ? 7u : 6u))
                return fail(lineNo, s.insert.vst3 ? "a vst3 insert needs a file, 0/1 and its state in quotes"
                                                  : "a builtin insert needs an effect name and 0/1");
            if (!tok[4].quoted || tok[4].text.empty())
                return fail(lineNo, "the effect must be in quotes");
            s.insert.type = tok[4].text;
            if (!parseFlag(tok[5], s.insert.bypassed))
                return fail(lineNo, "bypassed must be 0 or 1");
            if (s.insert.vst3) {
                if (!tok[6].quoted)
                    return fail(lineNo, "the plugin state must be in quotes");
                s.insert.state = tok[6].text;
            }
            inserts.push_back(s);
        } else if (kw == "param") {
            Pending s;
            s.line = lineNo;
            if (tok.size() != 5 || !parseChannel(tok[1], s.channel) || !parseNumber(tok[2], s.insert.slot) ||
                !tok[3].quoted || !parseNumber(tok[4], s.param.value))
                return fail(lineNo, "param needs a channel, a slot, a name in quotes and a value");
            s.param.name = tok[3].text;
            params.push_back(s);
        } else if (kw == "clip") {
            const bool full = tok.size() == 12;
            if (tok.size() != 5 && !(version >= 3 && full))
                return fail(lineNo, version >= 3 ? "clip needs 4 or 11 values" : "clip needs 4 values");
            ClipState c;
            if (!parseNumber(tok[1], c.track.value) || !c.track.isValid())
                return fail(lineNo, "bad track id \"" + tok[1].text + "\"");
            if (!parseNumber(tok[2], c.start) || c.start < 0)
                return fail(lineNo, "clip start must be 0 or more samples");
            if (!parseNumber(tok[3], c.length) || c.length <= 0)
                return fail(lineNo, "clip length must be at least 1 sample");
            if (!tok[4].quoted)
                return fail(lineNo, "the file name must be in quotes");
            c.file = tok[4].text;
            if (full) {
                if (!parseNumber(tok[5], c.sourceOffset) || c.sourceOffset < 0 || !parseNumber(tok[6], c.fadeIn) ||
                    !parseNumber(tok[7], c.fadeOut) || c.fadeIn < 0 || c.fadeOut < 0)
                    return fail(lineNo, "source offset and fades must be 0 or more samples");
                if (tok[8].quoted || (tok[8].text != "linear" && tok[8].text != "equalPower"))
                    return fail(lineNo, "the fade shape must be linear or equalPower");
                c.equalPowerFades = tok[8].text == "equalPower";
                if (!parseNumber(tok[9], c.gainDb) || !parseFlag(tok[10], c.muted) || !tok[11].quoted)
                    return fail(lineNo, "clip needs a gain, 0/1 for muted and a name in quotes");
                c.name = tok[11].text;
            }
            p.clips.push_back(std::move(c));
            clipLines.push_back(lineNo);
        } else if (kw == "takes") {
            if (version < 5)
                return fail(lineNo, "\"takes\" needs format 5 (this file is format " + std::to_string(version) + ")");
            if (tok.size() < 5 || (tok.size() - 2) % 3 != 0 || !tok[1].quoted)
                return fail(lineNo, "takes needs a file name in quotes, then a start, a source offset and a length per take");
            TakeSetState s;
            s.file = tok[1].text;
            for (size_t i = 2; i < tok.size(); i += 3) {
                TakeState t;
                if (!parseNumber(tok[i], t.start) || t.start < 0 || !parseNumber(tok[i + 1], t.sourceOffset) || t.sourceOffset < 0 ||
                    !parseNumber(tok[i + 2], t.length) || t.length <= 0)
                    return fail(lineNo, "a take needs a start and a source offset of 0 or more, and a length of at least 1");
                s.takes.push_back(t);
            }
            p.takeSets.push_back(std::move(s));
        } else if (kw == "instrument" || kw == "midiclip" || kw == "note" || kw == "cc") {
            if (version < 6)
                return fail(lineNo, "\"" + kw + "\" needs format 6 (this file is format " + std::to_string(version) + ")");
            if (kw == "instrument") {
                engine::TrackId id;
                if (tok.size() != 2 || !parseNumber(tok[1], id.value) || !id.isValid())
                    return fail(lineNo, "instrument needs a track id");
                instruments.push_back({lineNo, id});
            } else if (kw == "midiclip") {
                MidiClipState c;
                int number = 0;
                if (tok.size() != 11 || !parseNumber(tok[1], c.track.value) || !c.track.isValid() || !parseNumber(tok[2], number))
                    return fail(lineNo, "midiclip needs a track id, a clip number, start, length, offset, muted, a name, grid, strength, swing");
                if (!parseNumber(tok[3], c.start) || c.start < 0 || !parseNumber(tok[4], c.length) || c.length <= 0 ||
                    !parseNumber(tok[5], c.offset) || c.offset < 0)
                    return fail(lineNo, "a MIDI clip needs a start and an offset of 0 or more ticks, and a length of at least 1");
                if (!parseFlag(tok[6], c.muted) || !tok[7].quoted)
                    return fail(lineNo, "a MIDI clip needs 0/1 for muted and a name in quotes");
                c.name = tok[7].text;
                if (!parseNumber(tok[8], c.grid) || c.grid <= 0 || !parseNumber(tok[9], c.strength) || c.strength < 0.0f ||
                    c.strength > 1.0f || !parseNumber(tok[10], c.swing) || c.swing < 0.0f || c.swing > 0.5f)
                    return fail(lineNo, "quantize needs a grid of at least 1 tick, a strength from 0 to 1 and a swing from 0 to 0.5");
                if (std::find(midiClipNumbers.begin(), midiClipNumbers.end(), number) != midiClipNumbers.end())
                    return fail(lineNo, "MIDI clip number " + std::to_string(number) + " used twice");
                midiClipNumbers.push_back(number);
                midiClipLines.push_back(lineNo);
                p.midiClips.push_back(std::move(c));
            } else if (kw == "note") {
                PendingNote e;
                e.line = lineNo;
                engine::Note& n = e.note;
                if (tok.size() != 7 || !parseNumber(tok[1], e.clip) || !parseNumber(tok[2], n.start) || !parseNumber(tok[3], n.length) ||
                    !parseNumber(tok[4], n.pitch) || !parseNumber(tok[5], n.velocity) || !parseNumber(tok[6], n.channel))
                    return fail(lineNo, "note needs a clip number, start, length, pitch, velocity and channel");
                if (n.start < 0 || n.length <= 0 || n.pitch < 0 || n.pitch > 127 || n.velocity < 1 || n.velocity > 127 || n.channel < 0 ||
                    n.channel > 15)
                    return fail(lineNo, "a note needs a start of 0 or more, a length of at least 1, a pitch 0-127, a velocity 1-127 "
                                        "and a channel 0-15");
                notes.push_back(e);
            } else {
                PendingCc e;
                e.line = lineNo;
                engine::ControllerPoint& c = e.cc;
                if (tok.size() != 6 || !parseNumber(tok[1], e.clip) || !parseNumber(tok[2], c.tick) || !parseNumber(tok[3], c.number) ||
                    !parseNumber(tok[4], c.value) || !parseNumber(tok[5], c.channel))
                    return fail(lineNo, "cc needs a clip number, a tick, a controller, a value and a channel");
                if (c.tick < 0 || c.number < 0 || c.number > 127 || c.value < 0 || c.value > 127 || c.channel < 0 || c.channel > 15)
                    return fail(lineNo, "a controller point needs a tick of 0 or more, a controller and a value 0-127 and a channel 0-15");
                ccs.push_back(e);
            }
        } else if (kw == "lane") {
            if (version < 4)
                return fail(lineNo, "\"lane\" needs format 4 (this file is format " + std::to_string(version) + ")");
            PendingLane l;
            l.line = lineNo;
            if (tok.size() != 7 || !parseNumber(tok[1], l.number) || l.number <= 0 || !parseChannel(tok[2], l.channel) || !tok[4].quoted ||
                tok[5].quoted)
                return fail(lineNo, "lane needs a number, a channel, a slot or -, a parameter in quotes, a mode and a default");
            if (tok[3].text != "-" && (!parseNumber(tok[3], l.lane.slot) || l.lane.slot < 0 || l.lane.slot >= kInsertSlotsPerStrip))
                return fail(lineNo, "slot must be - or 0 .. " + std::to_string(kInsertSlotsPerStrip - 1));
            l.lane.parameter = tok[4].text;
            if (!parseMode(tok[5].text, l.lane.mode))
                return fail(lineNo, "unknown mode \"" + tok[5].text + "\"");
            if (!parseNumber(tok[6], l.lane.defaultValue) || l.lane.defaultValue < 0.0f || l.lane.defaultValue > 1.0f)
                return fail(lineNo, "the default must be a number from 0 to 1");
            lanes.push_back(std::move(l));
        } else if (kw == "point") {
            if (version < 4)
                return fail(lineNo, "\"point\" needs format 4 (this file is format " + std::to_string(version) + ")");
            PendingPoint e;
            e.line = lineNo;
            if (tok.size() != 5 || !parseNumber(tok[1], e.number) || tok[4].quoted)
                return fail(lineNo, "point needs a lane number, a position, a value and a shape");
            if (!parseNumber(tok[2], e.point.position) || e.point.position < 0)
                return fail(lineNo, "a point's position must be 0 or more samples");
            if (!parseNumber(tok[3], e.point.value) || e.point.value < 0.0f || e.point.value > 1.0f)
                return fail(lineNo, "a point's value must be a number from 0 to 1");
            if (tok[4].text != "linear" && tok[4].text != "hold")
                return fail(lineNo, "unknown shape \"" + tok[4].text + "\"");
            e.point.hold = tok[4].text == "hold";
            points.push_back(e);
        } else {
            return fail(lineNo, "unknown keyword \"" + kw + "\"");
        }
    }

    if (version == 0)
        return fail(lineNo, "not a DawLab project (empty file)");
    if (!haveRate)
        return fail(lineNo, "no sampleRate");

    // ---- references, now that everything is read ----
    auto stripOf = [&p](ChannelName c) -> StripState* {
        switch (c.kind) {
        case ChannelName::Kind::Track:
            for (TrackState& t : p.tracks)
                if (t.id.value == c.index)
                    return &t.strip;
            return nullptr;
        case ChannelName::Kind::Bus: return &p.buses[c.index];
        case ChannelName::Kind::Master: return &p.master;
        }
        return nullptr;
    };
    const auto noSuchTrack = [](const Pending& s) { return "there is no " + channelText(s.channel); };
    for (const Pending& s : sends) {
        StripState* strip = stripOf(s.channel);
        if (strip == nullptr)
            return fail(s.line, noSuchTrack(s));
        strip->sends[static_cast<size_t>(s.bus)] = s.send;
    }
    for (const Pending& s : inserts) {
        StripState* strip = stripOf(s.channel);
        if (strip == nullptr)
            return fail(s.line, noSuchTrack(s));
        for (const InsertState& other : strip->inserts)
            if (other.slot == s.insert.slot)
                return fail(s.line, "slot " + std::to_string(s.insert.slot) + " of " + channelText(s.channel) + " used twice");
        strip->inserts.push_back(s.insert);
    }
    for (const Pending& s : params) {
        StripState* strip = stripOf(s.channel);
        InsertState* target = nullptr;
        if (strip != nullptr)
            for (InsertState& ins : strip->inserts)
                if (ins.slot == s.insert.slot)
                    target = &ins;
        if (target == nullptr || target->vst3)
            return fail(s.line, "param for slot " + std::to_string(s.insert.slot) + " of " + channelText(s.channel) +
                                    ", which has no built-in insert");
        target->params.push_back(s.param);
    }
    // Lanes: what they refer to must exist. A parameter the effect does not know is NOT checked: that
    // is for the program that knows the effect, and such a lane stays (see ProjectFormat.h).
    for (size_t i = 0; i < lanes.size(); ++i) {
        PendingLane& l = lanes[i];
        const StripState* strip = stripOf(l.channel);
        if (strip == nullptr)
            return fail(l.line, "lane on " + channelText(l.channel) + ", which does not exist");
        if (l.lane.slot >= 0 && std::none_of(strip->inserts.begin(), strip->inserts.end(),
                                             [&l](const InsertState& ins) { return ins.slot == l.lane.slot; }))
            return fail(l.line, "lane for slot " + std::to_string(l.lane.slot) + " of " + channelText(l.channel) + ", which has no insert");
        for (size_t j = 0; j < i; ++j) {
            if (lanes[j].number == l.number)
                return fail(l.line, "lane number " + std::to_string(l.number) + " used twice");
            if (lanes[j].channel.kind == l.channel.kind && lanes[j].channel.index == l.channel.index && lanes[j].lane.slot == l.lane.slot &&
                lanes[j].lane.parameter == l.lane.parameter)
                return fail(l.line, "two lanes for one parameter");
        }
    }
    for (const PendingPoint& e : points) {
        const auto owner = std::find_if(lanes.begin(), lanes.end(), [&e](const PendingLane& l) { return l.number == e.number; });
        if (owner == lanes.end())
            return fail(e.line, "point for lane " + std::to_string(e.number) + ", which does not exist");
        owner->lane.points.push_back(e.point);
    }
    for (PendingLane& l : lanes) {
        std::stable_sort(l.lane.points.begin(), l.lane.points.end(),
                         [](const PointState& a, const PointState& b) { return a.position < b.position; });
        stripOf(l.channel)->lanes.push_back(std::move(l.lane));
    }

    auto bySlot = [](const InsertState& a, const InsertState& b) { return a.slot < b.slot; };
    for (TrackState& t : p.tracks)
        std::sort(t.strip.inserts.begin(), t.strip.inserts.end(), bySlot);
    for (StripState& b : p.buses)
        std::sort(b.inserts.begin(), b.inserts.end(), bySlot);
    std::sort(p.master.inserts.begin(), p.master.inserts.end(), bySlot);

    std::uint32_t maxId = 0;
    for (const TrackState& t : p.tracks)
        maxId = std::max(maxId, t.id.value);
    for (size_t i = 0; i < p.clips.size(); ++i) {
        const TrackId owner = p.clips[i].track;
        if (std::none_of(p.tracks.begin(), p.tracks.end(), [owner](const TrackState& t) { return t.id == owner; }))
            return fail(clipLines[i], "clip on track " + std::to_string(owner.value) + ", which does not exist");
    }
    auto hasTrack = [&p](engine::TrackId id) {
        return std::any_of(p.tracks.begin(), p.tracks.end(), [id](const TrackState& t) { return t.id == id; });
    };
    for (const auto& [line, id] : instruments) {
        if (!hasTrack(id))
            return fail(line, "instrument track " + std::to_string(id.value) + ", which does not exist");
        for (TrackState& t : p.tracks)
            if (t.id == id)
                t.instrument = true;
    }
    for (size_t i = 0; i < p.midiClips.size(); ++i)
        if (!hasTrack(p.midiClips[i].track))
            return fail(midiClipLines[i], "MIDI clip on track " + std::to_string(p.midiClips[i].track.value) + ", which does not exist");
    auto clipNumbered = [&](int number) -> MidiClipState* {
        const auto it = std::find(midiClipNumbers.begin(), midiClipNumbers.end(), number);
        return it == midiClipNumbers.end() ? nullptr : &p.midiClips[static_cast<size_t>(it - midiClipNumbers.begin())];
    };
    for (const PendingNote& e : notes) {
        MidiClipState* c = clipNumbered(e.clip);
        if (c == nullptr)
            return fail(e.line, "note for MIDI clip " + std::to_string(e.clip) + ", which does not exist");
        c->notes.push_back(e.note);
    }
    for (const PendingCc& e : ccs) {
        MidiClipState* c = clipNumbered(e.clip);
        if (c == nullptr)
            return fail(e.line, "cc for MIDI clip " + std::to_string(e.clip) + ", which does not exist");
        c->controllers.push_back(e.cc);
    }
    for (MidiClipState& c : p.midiClips) {
        std::stable_sort(c.notes.begin(), c.notes.end(), [](const engine::Note& a, const engine::Note& b) { return a.start < b.start; });
        std::stable_sort(c.controllers.begin(), c.controllers.end(),
                         [](const engine::ControllerPoint& a, const engine::ControllerPoint& b) { return a.tick < b.tick; });
    }
    if (maxId == std::numeric_limits<std::uint32_t>::max())
        return fail(lineNo, "no track ids left");
    p.nextTrackId = std::max(p.nextTrackId, maxId + 1); // never hand out an id that is in the file

    ReadResult r;
    r.ok = true;
    r.project = std::move(p);
    r.formatVersion = version;
    return r;
}

} // namespace dawlab::app::project
