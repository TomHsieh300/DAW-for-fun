// Exercise 9 (starter): find missing audio before opening.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex9_relink_audio.cpp. What each function must do is in ../ex9_relink_audio.h.
// Build with -Juce: .\scripts\build.ps1 -Only 15-daw-growing -Juce

#include "../ex9_relink_audio.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

using namespace dawlab::app;

namespace ex9 {

std::vector<MissingAudio> findMissingAudio(const juce::File& project, const project::ProjectState& state) {
    // TODO: for every clip, resolve(project, clip.file); a path that does not exist is missing.
    //       Each missing path once, in order; framesNeeded = the largest sourceOffset + length of
    //       its clips; clips = how many use it.
    (void)project;
    (void)state;
    return {};
}

juce::String checkCandidate(const MissingAudio& missing, const juce::File& candidate, double projectRate) {
    // TODO: a juce::AudioFormatManager with registerBasicFormats(); no reader -> "not an audio
    //       file"; needed = ceil(framesNeeded * reader->sampleRate / projectRate); fewer frames ->
    //       "too short: <frames> of <needed> frames". Otherwise "".
    (void)missing;
    (void)candidate;
    (void)projectRate;
    return "not written yet";
}

RelinkResult relink(const juce::File& project, const Ask& ask) {
    // TODO:
    //   1. project::readProject(project.loadFileAsString().toStdString()); not ok -> error.
    //   2. findMissingAudio(); for each: look in the folders of earlier finds (same file name,
    //      checkCandidate ok) - foundNearby; else ask(); juce::File() = skip; refused -> rejected.
    //   3. Once more at the end: the found folders, for what is still missing; the rest goes into
    //      stillMissing.
    //   4. Anything found: copy the project to "<name>.dawlab.bak", change clip.file (relative with
    //      '/' inside the project's folder, else the full path), write project::writeProject()
    //      through a juce::TemporaryFile.
    (void)project;
    (void)ask;
    RelinkResult result;
    result.error = "not written yet";
    return result;
}

} // namespace ex9

#include "../ex9_check.h"
