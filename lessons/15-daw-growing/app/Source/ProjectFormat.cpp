#include "ProjectFormat.h"
#include "ProjectLimits.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <utility>

namespace dawlab::app::project {

using engine::TrackId;

// ---- comparisons (round-trip tests) ---------------------------------------------------------------
// Each compares all of its members, as tuples: a member added to a state is added to its list here.

bool operator==(const SendState& a, const SendState& b) { return a.levelDb == b.levelDb && a.preFader == b.preFader; }
bool operator==(const ParamState& a, const ParamState& b) { return a.name == b.name && a.value == b.value; }
bool operator==(const InsertState& a, const InsertState& b) {
    return std::tie(a.slot, a.vst3, a.type, a.bypassed, a.params, a.state) == std::tie(b.slot, b.vst3, b.type, b.bypassed, b.params, b.state);
}
bool operator==(const StripState& a, const StripState& b) {
    return std::tie(a.name, a.gainDb, a.pan, a.mute, a.solo, a.sends, a.inserts) == std::tie(b.name, b.gainDb, b.pan, b.mute, b.solo, b.sends, b.inserts);
}
bool operator==(const TrackState& a, const TrackState& b) { return std::tie(a.id, a.format, a.strip) == std::tie(b.id, b.format, b.strip); }
bool operator==(const ClipState& a, const ClipState& b) {
    return std::tie(a.track, a.start, a.length, a.file, a.sourceOffset, a.fadeIn, a.fadeOut, a.equalPowerFades, a.gainDb, a.muted, a.name) ==
           std::tie(b.track, b.start, b.length, b.file, b.sourceOffset, b.fadeIn, b.fadeOut, b.equalPowerFades, b.gainDb, b.muted, b.name);
}
bool operator==(const ProjectState& a, const ProjectState& b) {
    const auto song = [](const ProjectState& p) {
        return std::tie(p.sampleRate, p.nextTrackId, p.tempo, p.beatsPerBar, p.beatUnit, p.loopStart, p.loopEnd, p.loopEnabled);
    };
    const auto parts = [](const ProjectState& p) { return std::tie(p.tracks, p.buses, p.master, p.clips); };
    return song(a) == song(b) && parts(a) == parts(b);
}

namespace {

// ---- tokens: Step 4's splitLine / quote / parseNumber ----------------------------------------------

struct Token {
    std::string text; // without the quotes, escapes resolved
    bool quoted = false;
};

// The quoted token that starts at line[i] (the quote): `i` moves past its closing quote. False, with the error,
// for an unknown escape or no closing quote.
bool readQuoted(std::string_view line, size_t& i, Token& t, std::string& error) {
    t = Token{std::string{}, true};
    ++i;
    while (i < line.size()) {
        const char d = line[i++];
        if (d == '"')
            return true;
        if (d != '\\') {
            t.text += d;
            continue;
        }
        if (i == line.size())
            break;
        const char e = line[i++];
        if (e != '"' && e != '\\' && e != 'n') {
            error = std::string("unknown escape \\") + e;
            return false;
        }
        t.text += e == 'n' ? '\n' : e;
    }
    error = "unterminated string";
    return false;
}

// The bare token that starts at line[i]: `i` moves to the blank, quote or comment that ends it.
Token readBare(std::string_view line, size_t& i) {
    const size_t start = i;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '"' && line[i] != '#')
        ++i;
    return Token{std::string(line.substr(start, i - start)), false};
}

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
            Token t;
            if (!readQuoted(line, i, t, error))
                return false;
            out.push_back(std::move(t));
        } else {
            out.push_back(readBare(line, i));
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

// The two sides of "track:17" or "bus:1": a track id of 1 or more, or a bus index below kNumBuses.
bool parseNumberedChannel(const std::string& kind, const Token& number, ChannelName& out) {
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
    return parseNumberedChannel(t.substr(0, colon), Token{t.substr(colon + 1), false}, out);
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

namespace {

// The lines of writeProject(), one kind at a time and in the file's order. Each leaves the stream's precision as it
// found it: 9 digits, which read every float back exactly; doubles - the sample rate and the tempo - get 17.

void writeHeader(std::ostream& out, const ProjectState& p) {
    out << "DawLabProject " << kFormatVersion << '\n';
    const auto digits = out.precision(std::numeric_limits<double>::max_digits10); // the rate and the tempo are doubles
    out << "sampleRate " << p.sampleRate << '\n';
    out << "nextTrackId " << p.nextTrackId << '\n';
    out << "tempo " << p.tempo << ' ' << p.beatsPerBar << ' ' << p.beatUnit << '\n';
    out << "loop " << p.loopStart << ' ' << p.loopEnd << ' ' << p.loopEnabled << '\n';
    out.precision(digits);
}

void writeChannels(std::ostream& out, const ProjectState& p) {
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
}

void writeClips(std::ostream& out, const ProjectState& p) {
    for (const ClipState& c : p.clips)
        out << "clip " << c.track.value << ' ' << c.start << ' ' << c.length << ' ' << quote(c.file) << ' ' << c.sourceOffset
            << ' ' << c.fadeIn << ' ' << c.fadeOut << ' ' << (c.equalPowerFades ? "equalPower" : "linear") << ' ' << c.gainDb
            << ' ' << c.muted << ' ' << quote(c.name) << '\n';
}

} // namespace

std::string writeProject(const ProjectState& p) {
    std::ostringstream out;
    out.imbue(std::locale::classic()); // "-6.5" and "48000" in every language, no digit grouping
    out.precision(std::numeric_limits<float>::max_digits10); // 9 digits: every float reads back exactly
    writeHeader(out, p);
    writeChannels(out, p);
    writeClips(out, p);
    return out.str();
}

// ---- reading ------------------------------------------------------------------------------------------

namespace {

// readProject's own helpers. Every number they check comes from parseNumber, which refuses NaN and infinity.

// From lo to hi, both included.
template <typename T>
bool inRange(T value, T lo, T hi) {
    return value >= lo && value <= hi;
}

// A word, not in quotes, that is one of the two.
bool isEither(const Token& token, const char* a, const char* b) { return !token.quoted && (token.text == a || token.text == b); }

bool parseTrackId(const Token& token, TrackId& out) { return parseNumber(token, out.value) && out.isValid(); }
bool parsePan(const Token& token, float& out) { return parseNumber(token, out) && inRange(out, -1.0f, 1.0f); }
bool parseBus(const Token& token, int& out) { return parseNumber(token, out) && out >= 0 && out < engine::kNumBuses; }
bool parseSlot(const Token& token, int& out) { return parseNumber(token, out) && out >= 0 && out < kInsertSlotsPerStrip; }

std::string busIndexProblem() { return "bus index must be 0 .. " + std::to_string(engine::kNumBuses - 1); }

// Lines that refer to a channel are resolved after the last line (any order is fine).
struct Pending {
    int line = 0;
    ChannelName channel;
    SendState send;
    int bus = 0;
    InsertState insert;
    ParamState param;
};

// What is wrong with a file, and on which line. An empty message: nothing.
struct Problem {
    int line = 0;
    std::string message;
};

ReadResult failure(int line, const std::string& message) {
    ReadResult r;
    r.error = "line " + std::to_string(line > 0 ? line : 1) + ": " + message;
    return r;
}

// The work of readProject, for one file. The file is read line by line: one member function per keyword reads
// its line and says what is wrong with it ("": nothing). After the last line, what the lines refer to is
// resolved, one step after the other.
class ProjectReader {
public:
    ReadResult read(std::string_view text) {
        if (text.substr(0, 3) == "\xEF\xBB\xBF")
            text.remove_prefix(3); // a UTF-8 byte order mark (Notepad)
        p_.sampleRate = 0.0;
        p_.nextTrackId = 0;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string_view::npos)
                end = text.size();
            std::string_view line = text.substr(pos, end - pos);
            pos = end + 1;
            ++lineNo_;
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1); // CRLF
            const std::string problem = readLine(line);
            if (!problem.empty())
                return failure(lineNo_, problem);
        }
        const Problem problem = finish();
        if (!problem.message.empty())
            return failure(problem.line, problem.message);
        ReadResult r;
        r.ok = true;
        r.project = std::move(p_);
        r.formatVersion = version_;
        return r;
    }

private:
    using LineReader = std::string (ProjectReader::*)();
    using Step = Problem (ProjectReader::*)();

    // ---- one line ----

    const std::string& keyword() const { return tok_[0].text; }

    std::string readLine(std::string_view line) {
        std::string error;
        if (!splitLine(line, tok_, error))
            return error;
        if (tok_.empty())
            return ""; // blank or comment
        if (tok_[0].quoted)
            return "a line must start with a keyword";
        if (version_ == 0) // the first item must be the header
            return readHeader();
        std::string problem = checkOrder();
        if (problem.empty())
            problem = checkOnce();
        if (!problem.empty())
            return problem;
        return readItem();
    }

    std::string readHeader() {
        if (keyword() != "DawLabProject" || tok_.size() != 2)
            return "not a DawLab project (no \"DawLabProject <version>\" header)";
        if (!parseNumber(tok_[1], version_) || version_ < 1)
            return "bad format version \"" + tok_[1].text + "\"";
        if (version_ > kFormatVersion)
            return "made by a newer DawLab (format " + std::to_string(version_) + ", this one reads up to " +
                   std::to_string(kFormatVersion) + ")";
        return "";
    }

    // sampleRate comes right after the header, and formats 1 and 2 know only four keywords.
    std::string checkOrder() const {
        const std::string& kw = keyword();
        if (once_.count("sampleRate") == 0 && kw != "sampleRate")
            return "sampleRate must come right after the header";
        if (version_ < 3 && kw != "sampleRate" && kw != "nextTrackId" && kw != "track" && kw != "clip")
            return needsFormat(3);
        return "";
    }

    // The keywords a file has at most once. A second line is damage, not a change of mind: the last one would win
    // without a word. (A bus is once per index, in readBus.)
    std::string checkOnce() {
        static const char* const kOnce[] = {"sampleRate", "nextTrackId", "tempo", "loop", "master"};
        for (const char* once : kOnce)
            if (keyword() == once && !once_.insert(once).second)
                return keyword() + " given twice";
        return "";
    }

    std::string needsFormat(int format) const {
        return "\"" + keyword() + "\" needs format " + std::to_string(format) + " (this file is format " + std::to_string(version_) + ")";
    }

    // From the keyword to the member function that reads its line.
    std::string readItem() {
        static const std::pair<const char*, LineReader> kReaders[] = {
            {"sampleRate", &ProjectReader::readSampleRate},
            {"nextTrackId", &ProjectReader::readNextTrackId},
            {"tempo", &ProjectReader::readTempo},
            {"loop", &ProjectReader::readLoop},
            {"track", &ProjectReader::readTrack},
            {"bus", &ProjectReader::readBus},
            {"master", &ProjectReader::readMaster},
            {"send", &ProjectReader::readSend},
            {"insert", &ProjectReader::readInsert},
            {"param", &ProjectReader::readParam},
            {"clip", &ProjectReader::readClip},
        };
        for (const auto& [name, reader] : kReaders)
            if (keyword() == name)
                return (this->*reader)();
        return "unknown keyword \"" + keyword() + "\"";
    }

    // ---- the song: rate, tempo, loop ----

    std::string readSampleRate() {
        if (tok_.size() != 2 || !parseNumber(tok_[1], p_.sampleRate) || !isSampleRate(p_.sampleRate))
            return "sampleRate needs one number, 8000 to 768000";
        return "";
    }

    std::string readNextTrackId() {
        if (tok_.size() != 2 || !parseNumber(tok_[1], p_.nextTrackId))
            return "nextTrackId needs one whole number";
        return "";
    }

    std::string readTempo() {
        if (tok_.size() != 4 || !parseNumber(tok_[1], p_.tempo) || !inRange(p_.tempo, 20.0, 999.0))
            return "tempo needs BPM (20 .. 999), beats per bar and beat unit";
        if (!parseNumber(tok_[2], p_.beatsPerBar) || !inRange(p_.beatsPerBar, 1, 32))
            return "beats per bar must be 1 .. 32";
        if (!parseNumber(tok_[3], p_.beatUnit) || !inRange(p_.beatUnit, 1, 32) || (p_.beatUnit & (p_.beatUnit - 1)) != 0)
            return "the beat unit must be 1, 2, 4, 8, 16 or 32";
        return "";
    }

    std::string readLoop() {
        if (tok_.size() != 4 || !parseNumber(tok_[1], p_.loopStart) || !parseNumber(tok_[2], p_.loopEnd) ||
            !parseFlag(tok_[3], p_.loopEnabled) || !isPosition(p_.loopStart) || !isPosition(p_.loopEnd, p_.loopStart))
            return "loop needs start >= 0, end >= start (samples, at most 2^40) and 0/1";
        return "";
    }

    // ---- channels: tracks, buses, the master, and what is on them ----

    std::string readTrack() {
        const size_t expected = version_ >= 3 ? 8 : version_ == 2 ? 5 : 4; // 1: no pan; 3: format, mute, solo
        if (tok_.size() != expected)
            return "track needs " + std::to_string(expected - 1) + " values in format " + std::to_string(version_);
        TrackState t;
        const std::string problem = readTrackValues(t);
        if (!problem.empty())
            return problem;
        if (hasTrack(t.id))
            return "track id " + tok_[1].text + " used twice";
        p_.tracks.push_back(std::move(t));
        return "";
    }

    // A track's id, name and gain; from format 2 on its pan, from format 3 on its switches.
    std::string readTrackValues(TrackState& t) {
        if (!parseTrackId(tok_[1], t.id))
            return "bad track id \"" + tok_[1].text + "\"";
        if (!tok_[2].quoted)
            return "the track name must be in quotes";
        t.strip.name = tok_[2].text;
        if (!parseNumber(tok_[3], t.strip.gainDb))
            return "bad gain \"" + tok_[3].text + "\"";
        if (version_ >= 2 && !parsePan(tok_[4], t.strip.pan))
            return "pan must be a number from -1 to 1";
        return version_ >= 3 ? readTrackSwitches(t) : std::string();
    }

    // Format 3: the track's format, mute and solo.
    std::string readTrackSwitches(TrackState& t) {
        if (!isEither(tok_[5], "mono", "stereo"))
            return "the track format must be mono or stereo";
        t.format = tok_[5].text == "mono" ? engine::TrackFormat::Mono : engine::TrackFormat::Stereo;
        if (!parseFlag(tok_[6], t.strip.mute) || !parseFlag(tok_[7], t.strip.solo))
            return "mute and solo must be 0 or 1";
        return "";
    }

    std::string readBus() {
        if (tok_.size() != 6)
            return "bus needs 5 values";
        int b = 0;
        if (!parseBus(tok_[1], b))
            return busIndexProblem();
        if (haveBus_[static_cast<size_t>(b)])
            return "bus " + tok_[1].text + " given twice";
        haveBus_[static_cast<size_t>(b)] = true;
        return readStripValues(p_.buses[static_cast<size_t>(b)], 2);
    }

    std::string readMaster() {
        if (tok_.size() != 5)
            return "master needs 4 values";
        return readStripValues(p_.master, 1);
    }

    // A bus's or the master's name, gain, pan and mute; `first` is where the name is.
    std::string readStripValues(StripState& s, size_t first) {
        if (!tok_[first].quoted)
            return "the name must be in quotes";
        s.name = tok_[first].text;
        if (!parseNumber(tok_[first + 1], s.gainDb) || !parsePan(tok_[first + 2], s.pan))
            return "bad gain or pan";
        if (!parseFlag(tok_[first + 3], s.mute))
            return "mute must be 0 or 1";
        return "";
    }

    std::string readSend() {
        Pending s;
        s.line = lineNo_;
        if (tok_.size() != 5 || !parseChannel(tok_[1], s.channel))
            return "send needs a channel, a bus, a level and 0/1 for pre-fader";
        if (!parseBus(tok_[2], s.bus))
            return busIndexProblem();
        if (!parseNumber(tok_[3], s.send.levelDb) || !parseFlag(tok_[4], s.send.preFader))
            return "bad send level or pre-fader flag";
        sends_.push_back(s);
        return "";
    }

    std::string readInsert() {
        Pending s;
        s.line = lineNo_;
        if (tok_.size() < 6 || !parseChannel(tok_[1], s.channel))
            return "insert needs a channel, a slot, a kind, an effect and 0/1 for bypassed";
        if (!parseSlot(tok_[2], s.insert.slot))
            return "slot must be 0 .. " + std::to_string(kInsertSlotsPerStrip - 1);
        if (!isEither(tok_[3], "builtin", "vst3"))
            return "the kind must be builtin or vst3";
        s.insert.vst3 = tok_[3].text == "vst3";
        if (tok_.size() != (s.insert.vst3 ? 7u : 6u))
            return s.insert.vst3 ? "a vst3 insert needs a file, 0/1 and its state in quotes" : "a builtin insert needs an effect name and 0/1";
        const std::string problem = readEffect(s.insert);
        if (!problem.empty())
            return problem;
        inserts_.push_back(s);
        return "";
    }

    // An insert's effect (a built-in one's name, or a plugin file), bypassed and, for a plugin, its state.
    std::string readEffect(InsertState& insert) {
        if (!tok_[4].quoted || tok_[4].text.empty())
            return "the effect must be in quotes";
        insert.type = tok_[4].text;
        if (!parseFlag(tok_[5], insert.bypassed))
            return "bypassed must be 0 or 1";
        if (insert.vst3) {
            if (!tok_[6].quoted)
                return "the plugin state must be in quotes";
            insert.state = tok_[6].text;
        }
        return "";
    }

    std::string readParam() {
        Pending s;
        s.line = lineNo_;
        if (tok_.size() != 5 || !parseChannel(tok_[1], s.channel) || !parseNumber(tok_[2], s.insert.slot) || !tok_[3].quoted ||
            !parseNumber(tok_[4], s.param.value))
            return "param needs a channel, a slot, a name in quotes and a value";
        s.param.name = tok_[3].text;
        params_.push_back(s);
        return "";
    }

    // ---- audio clips ----

    std::string readClip() {
        const bool full = tok_.size() == 12;
        if (tok_.size() != 5 && !(version_ >= 3 && full))
            return version_ >= 3 ? "clip needs 4 or 11 values" : "clip needs 4 values";
        ClipState c;
        std::string problem = readClipPlace(c);
        if (problem.empty() && full)
            problem = readClipDetails(c);
        if (!problem.empty())
            return problem;
        p_.clips.push_back(std::move(c));
        clipLines_.push_back(lineNo_);
        return "";
    }

    // Where a clip is: its track, start and length, and its file.
    std::string readClipPlace(ClipState& c) const {
        if (!parseTrackId(tok_[1], c.track))
            return "bad track id \"" + tok_[1].text + "\"";
        if (!parseNumber(tok_[2], c.start) || !isPosition(c.start))
            return "clip start must be 0 to 2^40 samples";
        if (!parseNumber(tok_[3], c.length) || !isPosition(c.length, 1))
            return "clip length must be 1 to 2^40 samples";
        if (!tok_[4].quoted)
            return "the file name must be in quotes";
        c.file = tok_[4].text;
        return "";
    }

    // Format 3: a clip's source offset, fades, gain, muted and name.
    std::string readClipDetails(ClipState& c) {
        if (!parseOffsetAndFades(c))
            return "source offset and fades must be 0 or more samples (the offset at most 2^40, a fade at most 60 s)";
        if (!isEither(tok_[8], "linear", "equalPower"))
            return "the fade shape must be linear or equalPower";
        c.equalPowerFades = tok_[8].text == "equalPower";
        if (!parseNumber(tok_[9], c.gainDb) || !parseFlag(tok_[10], c.muted) || !tok_[11].quoted)
            return "clip needs a gain, 0/1 for muted and a name in quotes";
        c.name = tok_[11].text;
        return "";
    }

    // The source offset (at most 2^40) and the two fades (at most 60 s each) of a format 3 clip.
    bool parseOffsetAndFades(ClipState& c) const {
        return parseNumber(tok_[5], c.sourceOffset) && isPosition(c.sourceOffset) && parseNumber(tok_[6], c.fadeIn) &&
               parseNumber(tok_[7], c.fadeOut) && isFade(c.fadeIn, p_.sampleRate) && isFade(c.fadeOut, p_.sampleRate);
    }

    // ---- after the last line: references, now that everything is read ----

    Problem finish() {
        if (version_ == 0)
            return {lineNo_, "not a DawLab project (empty file)"};
        if (once_.count("sampleRate") == 0)
            return {lineNo_, "no sampleRate"};
        // One step after the other, in this order: of several problems, the first one found is the one reported.
        for (const Step step : {&ProjectReader::resolveSends, &ProjectReader::resolveInserts, &ProjectReader::resolveParams,
                                &ProjectReader::checkClips, &ProjectReader::checkTrackIds}) {
            Problem problem = (this->*step)();
            if (!problem.message.empty())
                return problem;
        }
        sortInserts();
        p_.nextTrackId = std::max(p_.nextTrackId, maxTrackId() + 1); // never hand out an id that is in the file
        return {};
    }

    StripState* stripOf(ChannelName c) {
        switch (c.kind) {
        case ChannelName::Kind::Track:
            for (TrackState& t : p_.tracks)
                if (t.id.value == c.index)
                    return &t.strip;
            return nullptr;
        case ChannelName::Kind::Bus: return &p_.buses[c.index];
        case ChannelName::Kind::Master: return &p_.master;
        }
        return nullptr;
    }

    bool hasTrack(engine::TrackId id) const {
        return std::any_of(p_.tracks.begin(), p_.tracks.end(), [id](const TrackState& t) { return t.id == id; });
    }

    std::uint32_t maxTrackId() const {
        std::uint32_t maxId = 0;
        for (const TrackState& t : p_.tracks)
            maxId = std::max(maxId, t.id.value);
        return maxId;
    }

    Problem resolveSends() {
        for (const Pending& s : sends_) {
            StripState* strip = stripOf(s.channel);
            if (strip == nullptr)
                return {s.line, "there is no " + channelText(s.channel)};
            strip->sends[static_cast<size_t>(s.bus)] = s.send;
        }
        return {};
    }

    Problem resolveInserts() {
        for (const Pending& s : inserts_) {
            StripState* strip = stripOf(s.channel);
            if (strip == nullptr)
                return {s.line, "there is no " + channelText(s.channel)};
            for (const InsertState& other : strip->inserts)
                if (other.slot == s.insert.slot)
                    return {s.line, "slot " + std::to_string(s.insert.slot) + " of " + channelText(s.channel) + " used twice"};
            strip->inserts.push_back(s.insert);
        }
        return {};
    }

    Problem resolveParams() {
        for (const Pending& s : params_) {
            StripState* strip = stripOf(s.channel);
            InsertState* target = nullptr;
            if (strip != nullptr)
                for (InsertState& ins : strip->inserts)
                    if (ins.slot == s.insert.slot)
                        target = &ins;
            if (target == nullptr || target->vst3)
                return {s.line, "param for slot " + std::to_string(s.insert.slot) + " of " + channelText(s.channel) +
                                    ", which has no built-in insert"};
            target->params.push_back(s.param);
        }
        return {};
    }

    Problem checkClips() {
        for (size_t i = 0; i < p_.clips.size(); ++i) {
            const TrackId owner = p_.clips[i].track;
            if (!hasTrack(owner))
                return {clipLines_[i], "clip on track " + std::to_string(owner.value) + ", which does not exist"};
        }
        return {};
    }

    Problem checkTrackIds() {
        if (maxTrackId() == std::numeric_limits<std::uint32_t>::max())
            return {lineNo_, "no track ids left"};
        return {};
    }

    void sortInserts() {
        auto bySlot = [](const InsertState& a, const InsertState& b) { return a.slot < b.slot; };
        for (TrackState& t : p_.tracks)
            std::sort(t.strip.inserts.begin(), t.strip.inserts.end(), bySlot);
        for (StripState& b : p_.buses)
            std::sort(b.inserts.begin(), b.inserts.end(), bySlot);
        std::sort(p_.master.inserts.begin(), p_.master.inserts.end(), bySlot);
    }

    ProjectState p_;
    int version_ = 0;
    int lineNo_ = 0;
    std::set<std::string> once_; // the keywords of checkOnce() read so far
    std::array<bool, engine::kNumBuses> haveBus_{};
    std::vector<Token> tok_; // the line being read

    std::vector<Pending> sends_, inserts_, params_;
    std::vector<int> clipLines_;
};

} // namespace

ReadResult readProject(std::string_view text) { return ProjectReader().read(text); }

} // namespace dawlab::app::project
