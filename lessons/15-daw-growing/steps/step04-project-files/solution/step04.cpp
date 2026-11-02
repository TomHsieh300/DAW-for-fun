// Step 4 (solution): saving and opening projects.
//
// Build and run:  l15_step04_solution   (ctest, label "solution")
// Your version:   starter/step04.cpp -> l15_step04_starter

#include "../minidaw.h"

#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>

namespace minidaw {

std::string writeProject(const Project& project) {
    std::ostringstream out;
    out.imbue(std::locale::classic()); // "-6.5" and "48000" in every language, no digit grouping
    out.precision(std::numeric_limits<float>::max_digits10); // 9 digits: every float reads back exactly
    out << "DawLabProject " << kFormatVersion << '\n';
    out << "sampleRate " << project.sampleRate << '\n';
    out << "nextTrackId " << project.nextTrackId << '\n';
    for (const TrackState& t : project.tracks)
        out << "track " << t.id.value << ' ' << quote(t.name) << ' ' << t.gainDb << ' ' << t.pan << '\n';
    for (const ClipState& c : project.clips)
        out << "clip " << c.track.value << ' ' << c.start << ' ' << c.length << ' ' << quote(c.file) << '\n';
    return out.str();
}

LoadResult readProject(std::string_view text, double targetSampleRate) {
    auto fail = [](int line, const std::string& message) {
        LoadResult r;
        r.error = "line " + std::to_string(line > 0 ? line : 1) + ": " + message;
        return r;
    };
    int lineNo = 0;

    if (text.substr(0, 3) == "\xEF\xBB\xBF")
        text.remove_prefix(3); // a UTF-8 byte order mark (Notepad)

    Project p;
    p.sampleRate = 0.0;
    p.nextTrackId = 0;
    int version = 0;
    bool haveRate = false;
    std::vector<int> clipLines; // where each clip was, for errors found after the last line
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
                return fail(lineNo, "made by a newer DawLab (format " + std::to_string(version) +
                                        ", this one reads up to " + std::to_string(kFormatVersion) + ")");
            continue;
        }
        if (!haveRate && kw != "sampleRate")
            return fail(lineNo, "sampleRate must come right after the header");

        if (kw == "sampleRate") {
            if (haveRate)
                return fail(lineNo, "sampleRate given twice");
            if (tok.size() != 2 || !parseNumber(tok[1], p.sampleRate) || p.sampleRate <= 0.0)
                return fail(lineNo, "sampleRate needs one positive number");
            haveRate = true;
        } else if (kw == "nextTrackId") {
            if (tok.size() != 2 || !parseNumber(tok[1], p.nextTrackId))
                return fail(lineNo, "nextTrackId needs one whole number");
        } else if (kw == "track") {
            const size_t expected = version >= 2 ? 5 : 4; // format 1 had no pan
            if (tok.size() != expected)
                return fail(lineNo, "track needs " + std::to_string(expected - 1) + " values in format " + std::to_string(version));
            TrackState t;
            if (!parseNumber(tok[1], t.id.value) || !t.id.isValid())
                return fail(lineNo, "bad track id \"" + tok[1].text + "\"");
            if (!tok[2].quoted)
                return fail(lineNo, "the track name must be in quotes");
            t.name = tok[2].text;
            if (!parseNumber(tok[3], t.gainDb))
                return fail(lineNo, "bad gain \"" + tok[3].text + "\"");
            if (version >= 2 && (!parseNumber(tok[4], t.pan) || t.pan < -1.0f || t.pan > 1.0f))
                return fail(lineNo, "pan must be a number from -1 to 1");
            for (const TrackState& other : p.tracks)
                if (other.id == t.id)
                    return fail(lineNo, "track id " + tok[1].text + " used twice");
            p.tracks.push_back(std::move(t));
        } else if (kw == "clip") {
            if (tok.size() != 5)
                return fail(lineNo, "clip needs 4 values");
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
    // References are checked once everything is read, so the order of the lines does not matter.
    std::uint32_t maxId = 0;
    for (const TrackState& t : p.tracks)
        maxId = std::max(maxId, t.id.value);
    for (size_t i = 0; i < p.clips.size(); ++i) {
        const TrackId owner = p.clips[i].track;
        const bool found = std::any_of(p.tracks.begin(), p.tracks.end(), [owner](const TrackState& t) { return t.id == owner; });
        if (!found)
            return fail(clipLines[i], "clip on track " + std::to_string(owner.value) + ", which does not exist");
    }
    if (maxId == std::numeric_limits<std::uint32_t>::max())
        return fail(lineNo, "no track ids left");
    p.nextTrackId = std::max(p.nextTrackId, maxId + 1); // never hand out an id that is in the file

    convertSampleRate(p, targetSampleRate);
    LoadResult r;
    r.ok = true;
    r.project = std::move(p);
    r.formatVersion = version;
    return r;
}

void convertSampleRate(Project& project, double newRate) {
    if (newRate <= 0.0 || newRate == project.sampleRate)
        return;
    const double ratio = newRate / project.sampleRate;
    for (ClipState& c : project.clips) {
        // Convert both edges: a clip that ended where the next one starts still does.
        const std::int64_t start = std::llround(static_cast<double>(c.start) * ratio);
        const std::int64_t end = std::llround(static_cast<double>(c.start + c.length) * ratio);
        c.start = start;
        c.length = std::max<std::int64_t>(1, end - start);
    }
    project.sampleRate = newRate;
}

} // namespace minidaw

#include "../check.h"
