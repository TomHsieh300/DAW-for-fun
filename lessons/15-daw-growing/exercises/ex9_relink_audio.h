#pragma once
// Exercise 9 - Find missing audio before opening (optional, JUCE).
//
// A project names its audio files; the files live outside it. Move the project folder to another
// disk, rename the media folder, open a project from a colleague - and Step 8 opens it with
// "audio file ...: not found (its clips are left out)". Save it then, and those clips are gone for
// good. A DAW asks first:
//
//   relink(project, ask)
//     1. reads the project file (ProjectFormat) and finds the audio files that are not there;
//     2. for each one: first looks in the folders where earlier missing files were found (a moved
//        media folder: find one file, and the rest come with it); otherwise asks the user (ask());
//     3. checks every candidate: it must be an audio file, long enough for the clips that use it;
//     4. once more at the end, tries the found folders for files that are still missing;
//     5. if anything was found: keeps the old file as "<name>.dawlab.bak" and writes the project
//        with the new paths (relative to the project's folder when inside it, else absolute).
//   Then the project opens normally.

#include "ProjectFormat.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ex9 {

struct MissingAudio {
    std::string path;               // as written in the project file
    juce::File expected;            // where that path points
    std::int64_t framesNeeded = 0;  // at the project's rate: the largest sourceOffset + length of its clips
    int clips = 0;                  // how many clips use it
};

// Provided: where a path of the project file points (relative to the project's folder, or absolute).
inline juce::File resolve(const juce::File& project, const std::string& path) {
    const juce::String p = juce::String::fromUTF8(path.c_str());
    return juce::File::isAbsolutePath(p) ? juce::File(p) : project.getParentDirectory().getChildFile(p);
}

// ---- YOUR TURN (starter/ex9_relink_audio.cpp) -------------------------------------------------
// The distinct audio files of `state` that do not exist, in the order their first clip appears.
std::vector<MissingAudio> findMissingAudio(const juce::File& project, const dawlab::app::project::ProjectState& state);

// Why `candidate` cannot stand in for `missing`, or an empty string if it can:
//   "not an audio file"                           no reader for it (juce::AudioFormatManager)
//   "too short: 100 of 96000 frames"              shorter than framesNeeded, converted to the
//                                                 candidate's own sample rate (rounded up)
juce::String checkCandidate(const MissingAudio& missing, const juce::File& candidate, double projectRate);

struct RelinkResult {
    bool ok = false;               // false: the project file could not be read or written
    juce::String error;
    int relinked = 0;              // files found (asked or nearby)
    int foundNearby = 0;           // of those, found in a folder of an earlier find, without asking
    juce::StringArray stillMissing; // paths that are still missing
    juce::StringArray rejected;    // "<candidate file name>: <reason>" for every refused candidate
    juce::File backup;             // the old project file, when the project was rewritten
};

// Asks the user for a replacement; juce::File() = cancelled for this file.
using Ask = std::function<juce::File(const MissingAudio&)>;

RelinkResult relink(const juce::File& project, const Ask& ask);

} // namespace ex9
