#pragma once
// ProjectIO: a Session <-> a project file on disk (chapter 15, Step 8).
//
// ProjectFormat turns a ProjectState into text and back. ProjectIO is the edge around it:
//
//   save:  Session --capture()--> ProjectState + the audio buffers
//                                   |-> <Song>_media/000.wav, 001.wav ...  (32-bit float WAV)
//                                   '-> Song.dawlab                        (writeProject text)
//   open:  Song.dawlab --readProject()--> ProjectState --read every audio file--> restore()
//
// Rules:
//   - The audio is not in the text. Each distinct clip buffer is written once, as a 32-bit float
//     WAV at the project's sample rate, so every sample comes back bit for bit. Clips share
//     buffers (the drum loop is one buffer used by four clips), and they still do after opening.
//   - Every file is written next to its target and then moved over it (juce::TemporaryFile):
//     a crash or a full disk while saving leaves the previous version complete. WAVs that the
//     project no longer uses are deleted from its media folder after the text is written.
//   - Opening checks the whole text and reads every audio file BEFORE it touches the session.
//     A file that does not parse leaves the current project as it was. What can be skipped is
//     skipped with a warning instead: a missing audio file, a plugin that does not load, an
//     effect or parameter this version does not know.
//   - Track ids in the file are the file's own references, written 1..N in display order. The
//     session gives the tracks new ids when it opens them (Session.h: ids are never reused
//     while the program runs), and saving an opened project gives the same text again.
//   - A project saved at another sample rate is converted after opening, like after a device
//     change (Session::handleSampleRateChange): clip audio is resampled, positions scaled.

#include "ProjectFormat.h"
#include "Session.h"

#include <dawlab/host/PluginHost.h>

#include <juce_core/juce_core.h>

#include <memory>
#include <utility>
#include <vector>

namespace dawlab::app {

class ProjectIO {
public:
    static constexpr const char* kFileExtension = ".dawlab";

    // "Song.dawlab" -> the folder "Song_media" next to it.
    static juce::File mediaFolderFor(const juce::File& projectFile);

    struct Captured {
        project::ProjectState state;
        // Each distinct buffer once, with its file name relative to the project's folder.
        std::vector<std::pair<juce::String, std::shared_ptr<const AudioBuffer>>> media;
    };
    // The session as data; audio files are named "<mediaFolderName>/000.wav", ... in the order
    // the buffers first appear (tracks in display order, clips in start order).
    static Captured capture(const Session& session, const juce::String& mediaFolderName);

    // Writes the media folder and then the project file. False (and `error`) if anything could
    // not be written; the previous version of the file is then still complete.
    static bool save(const Session& session, const juce::File& file, juce::String& error);

    struct OpenResult {
        bool ok = false;
        juce::String error;         // when !ok: what is wrong, e.g. "Song.dawlab, line 7: ..."
        juce::StringArray warnings; // when ok: what was skipped
        juce::StringArray notes;    // when ok: what was repaired and lost nothing (it does not make Save refuse)
        int formatVersion = 0;
        int audioFiles = 0;         // distinct audio files read
    };
    // Replaces the session's project with the file's. `pluginHost` may be nullptr (VST3 inserts
    // are then skipped with a warning). Message thread.
    static OpenResult open(Session& session, host::PluginHost* pluginHost, const juce::File& file);
};

} // namespace dawlab::app
