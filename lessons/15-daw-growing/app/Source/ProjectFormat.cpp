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
bool operator==(const StripState& a, const StripState& b) {
    return a.name == b.name && a.gainDb == b.gainDb && a.pan == b.pan && a.mute == b.mute && a.solo == b.solo &&
           a.sends == b.sends && a.inserts == b.inserts;
}
bool operator==(const TrackState& a, const TrackState& b) { return a.id == b.id && a.format == b.format && a.strip == b.strip; }
bool operator==(const ClipState& a, const ClipState& b) {
    return a.track == b.track && a.start == b.start && a.length == b.length && a.file == b.file &&
           a.sourceOffset == b.sourceOffset && a.fadeIn == b.fadeIn && a.fadeOut == b.fadeOut &&
           a.equalPowerFades == b.equalPowerFades && a.gainDb == b.gainDb && a.muted == b.muted && a.name == b.name;
}
bool operator==(const ProjectState& a, const ProjectState& b) {
    return a.sampleRate == b.sampleRate && a.nextTrackId == b.nextTrackId && a.tempo == b.tempo &&
           a.beatsPerBar == b.beatsPerBar && a.beatUnit == b.beatUnit && a.loopStart == b.loopStart &&
           a.loopEnd == b.loopEnd && a.loopEnabled == b.loopEnabled && a.tracks == b.tracks && a.buses == b.buses &&
           a.master == b.master && a.clips == b.clips;
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

bool operator==(const ChannelName& a, const ChannelName& b) { return a.kind == b.kind && a.index == b.index; }

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

// ---- writing --------------------------------------------------------------------------------------

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
    out << "loop " << p.loopStart << ' ' << p.loopEnd << ' ' << p.loopEnabled << '\n';
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
    std::vector<int> clipLines;

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
            if (tok.size() != 4 || !parseNumber(tok[1], p.loopStart) || !parseNumber(tok[2], p.loopEnd) ||
                !parseFlag(tok[3], p.loopEnabled) || p.loopStart < 0 || p.loopEnd < p.loopStart)
                return fail(lineNo, "loop needs start >= 0, end >= start (samples) and 0/1");
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
