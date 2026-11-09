// Exercise 9 (solution): find missing audio before opening.
//
// Build and run:  l15_ex9_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex9_relink_audio.cpp -> l15_ex9_starter

#include "../exercises/ex9_relink_audio.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

using namespace dawlab::app;

namespace ex9 {

std::vector<MissingAudio> findMissingAudio(const juce::File& project, const project::ProjectState& state) {
    std::vector<MissingAudio> missing;
    std::map<std::string, size_t> known; // path -> index in `missing`, or SIZE_MAX if it exists
    for (const project::ClipState& clip : state.clips) {
        auto it = known.find(clip.file);
        if (it == known.end()) {
            const juce::File f = resolve(project, clip.file);
            if (f.existsAsFile()) {
                known[clip.file] = SIZE_MAX;
                continue;
            }
            known[clip.file] = missing.size();
            missing.push_back(MissingAudio{clip.file, f, 0, 0});
            it = known.find(clip.file);
        }
        if (it->second == SIZE_MAX)
            continue;
        MissingAudio& m = missing[it->second];
        m.framesNeeded = std::max(m.framesNeeded, clip.sourceOffset + clip.length);
        ++m.clips;
    }
    return missing;
}

juce::String checkCandidate(const MissingAudio& missing, const juce::File& candidate, double projectRate) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(candidate));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return "not an audio file";
    // The project's frames at the file's own rate: the opened clip is resampled to the project.
    const auto needed = static_cast<std::int64_t>(std::ceil(static_cast<double>(missing.framesNeeded) * reader->sampleRate / projectRate));
    if (reader->lengthInSamples < needed)
        return "too short: " + juce::String(reader->lengthInSamples) + " of " + juce::String(needed) + " frames";
    return {};
}

RelinkResult relink(const juce::File& project, const Ask& ask) {
    RelinkResult result;
    const juce::String text = project.loadFileAsString();
    project::ReadResult parsed = project::readProject(text.toStdString());
    if (!parsed.ok) {
        result.error = project.getFileName() + ", " + juce::String(parsed.error);
        return result;
    }
    project::ProjectState& state = parsed.project;
    const std::vector<MissingAudio> missing = findMissingAudio(project, state);

    std::map<std::string, juce::File> found; // old path -> the file that stands in for it
    juce::Array<juce::File> folders;          // where earlier files were found
    auto lookNearby = [&](const MissingAudio& m) {
        for (const juce::File& folder : folders) {
            const juce::File f = folder.getChildFile(m.expected.getFileName());
            if (f.existsAsFile() && checkCandidate(m, f, state.sampleRate).isEmpty()) {
                found[m.path] = f;
                ++result.foundNearby;
                return true;
            }
        }
        return false;
    };
    for (const MissingAudio& m : missing) {
        if (lookNearby(m))
            continue;
        const juce::File candidate = ask(m);
        if (candidate == juce::File())
            continue; // cancelled for this one
        const juce::String why = checkCandidate(m, candidate, state.sampleRate);
        if (why.isNotEmpty()) {
            result.rejected.add(candidate.getFileName() + ": " + why);
            continue;
        }
        found[m.path] = candidate;
        folders.addIfNotAlreadyThere(candidate.getParentDirectory());
    }
    for (const MissingAudio& m : missing) // a folder found later may hold an earlier one too
        if (found.count(m.path) == 0 && !lookNearby(m))
            result.stillMissing.add(juce::String::fromUTF8(m.path.c_str()));
    result.relinked = static_cast<int>(found.size());

    if (!found.empty()) {
        const juce::File folder = project.getParentDirectory();
        for (project::ClipState& clip : state.clips) {
            const auto it = found.find(clip.file);
            if (it == found.end())
                continue;
            const juce::File& f = it->second;
            const juce::String path = f.isAChildOf(folder) ? f.getRelativePathFrom(folder).replaceCharacter('\\', '/') : f.getFullPathName();
            clip.file = path.toStdString();
        }
        result.backup = project.getSiblingFile(project.getFileName() + ".bak");
        if (!project.copyFileTo(result.backup)) {
            result.error = "cannot write " + result.backup.getFullPathName();
            return result;
        }
        juce::TemporaryFile temp(project);
        if (!temp.getFile().replaceWithText(juce::String(project::writeProject(state))) || !temp.overwriteTargetFileWithTemporary()) {
            result.error = "cannot write " + project.getFullPathName();
            return result;
        }
    }
    result.ok = true;
    return result;
}

} // namespace ex9

#include "../exercises/ex9_check.h"
