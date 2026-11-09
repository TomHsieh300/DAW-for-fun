#pragma once
// Exercise 9 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex9_relink_audio.h"

#include "MainComponent.h"
#include "ProjectIO.h"

#include <dawlab/Check.h>

#include <cstdio>

namespace ex9::check {

using namespace dawlab;
using namespace dawlab::app;

inline int totalClips(const Session& s) {
    int n = 0;
    for (int i = 0; i < s.getNumTracks(); ++i)
        n += static_cast<int>(s.getClips(s.getTrackId(i)).size());
    return n;
}

inline project::ProjectState stateOf(const juce::File& project) {
    return project::readProject(project.loadFileAsString().toStdString()).project;
}

// A short 32-bit float WAV.
inline bool writeShortWav(const juce::File& file, double rate, int frames) {
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> out = file.createOutputStream();
    if (out == nullptr)
        return false;
    const juce::AudioFormatWriterOptions options = juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(1).withBitsPerSample(32);
    std::unique_ptr<juce::AudioFormatWriter> writer = wav.createWriterFor(out, options);
    if (writer == nullptr)
        return false;
    juce::AudioBuffer<float> silence(1, frames);
    silence.clear();
    return writer->writeFromAudioSampleBuffer(silence, 0, frames);
}

inline void run() {
    const juce::File folder = juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile("l15_ex9");
    const juce::File elsewhere = folder.getSiblingFile("l15_ex9_elsewhere");
    folder.deleteRecursively();
    elsewhere.deleteRecursively();
    folder.createDirectory();

    MainComponent main(MainComponent::Mode::Offline);
    DAWLAB_CHECK(main.loadDemoSongNow() == 0);
    main.simulatePlayback(512);
    const int clips = totalClips(main.getSession());
    const juce::File song = folder.getChildFile("Song.dawlab");
    DAWLAB_CHECK(main.saveProject(song));
    const juce::File media = ProjectIO::mediaFolderFor(song);
    const int files = media.getNumberOfChildFiles(juce::File::findFiles, "*.wav");
    DAWLAB_CHECK(files >= 3);
    const double rate = stateOf(song).sampleRate;

    // Nothing missing: nothing to do.
    RelinkResult r = relink(song, [](const MissingAudio&) { return juce::File(); });
    DAWLAB_CHECK(r.ok && r.relinked == 0 && r.stillMissing.isEmpty() && r.backup == juce::File());

    // The media folder is renamed.
    const juce::File moved = folder.getChildFile("Moved");
    DAWLAB_CHECK(media.moveFileTo(moved));
    const std::vector<MissingAudio> missing = findMissingAudio(song, stateOf(song));
    DAWLAB_CHECK(static_cast<int>(missing.size()) == files);
    int used = 0;
    bool needs = true;
    for (const MissingAudio& m : missing) {
        used += m.clips;
        needs = needs && m.framesNeeded > 0 && !m.expected.exists();
    }
    DAWLAB_CHECK(used == clips && needs);
    {
        // Two clips on one missing file: the file must hold the later end, offset included.
        project::ProjectState s;
        s.clips.push_back({engine::TrackId{1}, 0, 1000, "gone.wav", 5000});
        s.clips.push_back({engine::TrackId{1}, 9000, 4000, "gone.wav", 200});
        s.clips.push_back({engine::TrackId{1}, 0, 10, "Moved/" + missing[0].expected.getFileName().toStdString()}); // exists
        const std::vector<MissingAudio> m = findMissingAudio(song, s);
        DAWLAB_CHECK(m.size() == 1 && m[0].path == "gone.wav" && m[0].clips == 2 && m[0].framesNeeded == 6000);
    }
    if (missing.size() < 3)
        return;
    const std::string original = song.loadFileAsString().toStdString();

    // Cancelled: the file is not touched.
    r = relink(song, [](const MissingAudio&) { return juce::File(); });
    DAWLAB_CHECK(r.ok && r.relinked == 0 && r.stillMissing.size() == files && r.backup == juce::File());
    DAWLAB_CHECK(song.loadFileAsString().toStdString() == original);

    // Wrong candidates are refused with a reason.
    const juce::File junk = folder.getChildFile("Junk").getChildFile(missing[0].expected.getFileName());
    junk.getParentDirectory().createDirectory();
    DAWLAB_CHECK(junk.replaceWithText("not audio"));
    DAWLAB_CHECK(checkCandidate(missing[0], junk, rate) == "not an audio file");
    const juce::File shortFile = folder.getChildFile("Short").getChildFile(missing[0].expected.getFileName());
    shortFile.getParentDirectory().createDirectory();
    DAWLAB_CHECK(writeShortWav(shortFile, rate, 100));
    DAWLAB_CHECK(checkCandidate(missing[0], shortFile, rate).startsWith("too short: 100 of "));
    DAWLAB_CHECK(checkCandidate(missing[0], moved.getChildFile(missing[0].expected.getFileName()), rate).isEmpty());
    // At another rate, the frames needed scale with it.
    const juce::File half = folder.getChildFile("Short").getChildFile("half.wav");
    DAWLAB_CHECK(writeShortWav(half, rate / 2, static_cast<int>(missing[0].framesNeeded / 2) + 1));
    DAWLAB_CHECK(checkCandidate(missing[0], half, rate).isEmpty());

    // The user cancels the first, picks a short file for the second and the right one for the
    // third: the rest - and the first two, at the end - are found in that folder without asking.
    int asked = 0;
    r = relink(song, [&](const MissingAudio& m) {
        ++asked;
        return asked == 1 ? juce::File() : (asked == 2 ? shortFile : moved.getChildFile(m.expected.getFileName()));
    });
    std::printf("  a renamed media folder of %d files: asked %d times, %d found nearby, %d refused\n", files, asked, r.foundNearby,
                r.rejected.size());
    DAWLAB_CHECK(r.ok && r.relinked == files && r.stillMissing.isEmpty());
    DAWLAB_CHECK(asked == 3);
    DAWLAB_CHECK(r.foundNearby == files - 1);
    DAWLAB_CHECK(r.rejected.size() == 1 && r.rejected[0].contains("too short"));
    DAWLAB_CHECK(r.backup.existsAsFile() && r.backup.loadFileAsString().toStdString() == original);
    // Inside the project's folder: relative paths, with '/'.
    const juce::String text = song.loadFileAsString();
    DAWLAB_CHECK(text.contains("\"Moved/" + missing[1].expected.getFileName() + "\""));
    DAWLAB_CHECK(findMissingAudio(song, stateOf(song)).empty());
    // It opens with every clip.
    {
        MainComponent opened(MainComponent::Mode::Offline);
        const ProjectIO::OpenResult o = opened.openProject(song);
        DAWLAB_CHECK(o.ok && o.warnings.isEmpty() && o.audioFiles == files);
        DAWLAB_CHECK(totalClips(opened.getSession()) == clips);
    }
    // Again: nothing missing any more, nothing written.
    const juce::Time written = song.getLastModificationTime();
    r = relink(song, [](const MissingAudio&) { return juce::File(); });
    DAWLAB_CHECK(r.ok && r.relinked == 0 && song.getLastModificationTime() == written);

    // Outside the project's folder: absolute paths.
    DAWLAB_CHECK(moved.moveFileTo(elsewhere));
    asked = 0;
    r = relink(song, [&](const MissingAudio& m) {
        ++asked;
        return elsewhere.getChildFile(m.expected.getFileName());
    });
    DAWLAB_CHECK(r.ok && r.relinked == files && asked == 1);
    const project::ProjectState relinked = stateOf(song);
    bool absolute = !relinked.clips.empty();
    for (const project::ClipState& c : relinked.clips)
        absolute = absolute && juce::File::isAbsolutePath(juce::String::fromUTF8(c.file.c_str()));
    DAWLAB_CHECK(absolute);
    {
        MainComponent opened(MainComponent::Mode::Offline);
        const ProjectIO::OpenResult o = opened.openProject(song);
        DAWLAB_CHECK(o.ok && o.warnings.isEmpty() && totalClips(opened.getSession()) == clips);
    }

    // A file that is not a project.
    const juce::File broken = folder.getChildFile("Broken.dawlab");
    DAWLAB_CHECK(broken.replaceWithText("hello"));
    r = relink(broken, [](const MissingAudio&) { return juce::File(); });
    DAWLAB_CHECK(!r.ok && r.error.contains("Broken.dawlab"));
}

} // namespace ex9::check

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf("Exercise 9 - find missing audio before opening\n");
    ex9::check::run();
    return dawlab::test::summary("l15_ex9");
}
