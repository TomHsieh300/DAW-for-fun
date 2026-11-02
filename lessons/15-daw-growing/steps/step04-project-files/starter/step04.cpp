// Step 4 (starter): saving and opening projects.
//
// Build and run:  l15_step04_starter   (prints [FAIL] until all three TODOs are done)
// Reference:      ../solution/step04.cpp, docs/step-04-project-files.md ("Your turn")
//
// Project, LoadResult, the file format and the helpers splitLine(), quote() and parseNumber()
// are in ../minidaw.h; saveProjectFile() and openProjectFile() are provided there too.

#include "../minidaw.h"

#include <sstream>

namespace minidaw {

std::string writeProject(const Project& project) {
    // TODO 1. An std::ostringstream, imbued with std::locale::classic() (a decimal point and no
    // digit grouping in every language), precision std::numeric_limits<float>::max_digits10 (so
    // every float reads back exactly). Then, one per line:
    //   DawLabProject <kFormatVersion>
    //   sampleRate <rate>
    //   nextTrackId <id>
    //   track <id> <quote(name)> <gainDb> <pan>          for every track, in display order
    //   clip <track id> <start> <length> <quote(file)>    for every clip
    (void)project;
    return std::string{};
}

LoadResult readProject(std::string_view text, double targetSampleRate) {
    // TODO 2. Never trust the text. Report the FIRST problem as "line <n>: <what>" and return.
    //   - skip a UTF-8 byte order mark ("\xEF\xBB\xBF") at the start; split at '\n', drop a '\r'
    //     at the end of a line; count lines from 1; splitLine() each line, skip empty results;
    //   - the first item must be "DawLabProject <v>" with 1 <= v <= kFormatVersion (a larger v:
    //     say it was made by a newer DawLab); the next one must be "sampleRate <r>" with r > 0,
    //     and it may appear only once;
    //   - "nextTrackId <n>"; "track <id> \"name\" <gain> <pan>" (format 1: no pan) with id > 0,
    //     unique, a quoted name, pan in [-1, 1]; "clip <track> <start> <length> \"file\"" with
    //     start >= 0 and length >= 1; anything else is an unknown keyword;
    //   - read numbers with parseNumber() only; check the token count of every line;
    //   - after the last line: no header -> error; no sampleRate -> error; every clip's track
    //     must exist (report the clip's line); nextTrackId = max(nextTrackId, highest id + 1);
    //   - finally convertSampleRate(project, targetSampleRate), ok = true, formatVersion = v.
    (void)text;
    (void)targetSampleRate;
    LoadResult r;
    r.error = "line 1: readProject() is not written yet";
    return r;
}

void convertSampleRate(Project& project, double newRate) {
    // TODO 3. Nothing to do if newRate <= 0 or equals project.sampleRate. Otherwise, with
    // ratio = newRate / sampleRate, convert every clip's start AND end (start + length) with
    // std::llround, and set length = end - start, at least 1. Converting start and length
    // separately would open 1-sample gaps between clips that touched. Then sampleRate = newRate.
    (void)project;
    (void)newRate;
}

} // namespace minidaw

#include "../check.h"
