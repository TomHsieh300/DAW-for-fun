#pragma once
// ProjectFormat: the text of a DawLab project file (chapter 15, Step 8) - Step 4's format, grown
// to hold everything the application keeps.
//
// This file knows no JUCE, no files and no engine objects: a ProjectState is plain data, and
//
//   writeProject(state) -> text          readProject(text) -> state, or "line 7: ..."
//
// are pure functions, tested without the GUI (tests/test_project_format.cpp). ProjectIO turns
// a Session into a ProjectState and back, and does the files (the text, the audio, the plugins).
//
// The format (UTF-8 text, one item per line, '#' starts a comment outside quotes; numbers in the
// classic locale; floats with 9 digits and doubles with 17, so every number reads back exactly):
//
//   DawLabProject 4                                header: the format version
//   sampleRate 48000                               the rate of every sample position below
//   nextTrackId 25                                 (Step 4) above every track id in the file
//   tempo 120 4 4                                  BPM, time signature
//   loop 0 384000 0                                start, end (samples), enabled 0/1
//   track 17 "Drums" -2 0 mono 0 0                 id, name, gain dB, pan, format, mute, solo
//   bus 0 "Reverb" -2 0 0                          index, name, gain dB, pan, mute
//   master "Master" 0 0 0                          name, gain dB, pan, mute
//   send track:17 0 -18 0                          channel, bus, level dB, pre-fader 0/1
//   insert track:17 0 builtin "Compressor" 0       channel, GUI slot, kind, effect, bypassed 0/1
//   param track:17 0 "Threshold" -20               channel, slot, parameter name, value
//   insert master 3 vst3 "C:/.../Lim.vst3" 0 "<base64>"   ... plugin file, its state
//   clip 17 0 96000 "Song_media/000.wav" 0 240 240 linear 0 0 "drums"
//        track, start, length, audio file, then: source offset, fade in, fade out, fade shape,
//        gain dB, muted 0/1, clip name
//   lane 1 track:17 0 "Threshold" touch 0.5        (format 4) number, channel, GUI slot ("-": the strip's
//                                                  own Gain or Pan), parameter, mode, default 0..1
//   point 1 48000 0.25 linear                      (format 4) lane number, position, value 0..1, shape
//
// A lane names its effect by slot: like a track's id, a slot here is only the file's own reference,
// because an opened project gets new EffectIds. Points of one lane at one position keep the order of
// their lines: that is how a jump survives. A parameter this version does not know is not an error
// (chapter 16, Step 4): the lane stays, silent, and is saved again.
//
// Numbers are checked for more than their sign (ProjectLimits.h, the same in chapters 15 to 20): a
// sample rate of 8000 to 768000 Hz, every position and length at most 2^40 samples or ticks, and a
// fade at most 60 seconds at the file's sample rate. sampleRate, nextTrackId, tempo, loop and master come at
// most once (a bus once per index).
//
// Channels are written "track:<id>", "bus:<index>" or "master". Lines that refer to something -
// a send, an insert, a parameter, a clip - are checked once the whole file is read, so their
// order does not matter. Built-in parameters are saved BY NAME: a later version of an effect
// may add parameters or reorder them, and the file still means the same thing.
//
// Formats 1 and 2 are Step 4's: tracks with id, name, gain (and pan from 2), clips with only the
// first four values (a file of any audio format). The app opens them too; everything they do not
// say keeps its default.

#include <dawlab/DspMath.h>
#include <dawlab/engine/Automation.h>
#include <dawlab/engine/Constants.h>
#include <dawlab/engine/Track.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dawlab::app {

constexpr int kInsertSlotsPerStrip = 4; // GUI insert slots per strip (Session, MixerView)

namespace project {

constexpr int kFormatVersion = 4; // 1: Step 4 without pan; 2: Step 4; 3: the whole app project; 4: automation

struct SendState {
    float levelDb = kMinusInfinityDb;
    bool preFader = false;
};

struct ParamState {
    std::string name;
    float value = 0.0f;
};

struct InsertState {
    int slot = 0;      // GUI slot, 0 .. kInsertSlotsPerStrip - 1
    bool vst3 = false; // false: a built-in effect
    std::string type;  // built-in: engine::effectTypeName(); VST3: the plugin file
    bool bypassed = false;
    std::vector<ParamState> params; // built-in only, in the order they were written
    std::string state;              // VST3 only: the plugin's own state, base64
};

struct PointState {
    std::int64_t position = 0;
    float value = 0.0f;
    bool hold = false;
};

struct LaneState {
    int slot = -1;         // GUI slot of the effect; -1: the strip's own control
    std::string parameter; // by name
    engine::AutomationMode mode = engine::AutomationMode::Read;
    float defaultValue = 0.0f;
    std::vector<PointState> points; // by position; at one position in the order of their lines
};

struct StripState {
    std::string name;
    float gainDb = 0.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false; // tracks only
    std::array<SendState, engine::kNumBuses> sends{};
    std::vector<InsertState> inserts; // in slot order
    std::vector<LaneState> lanes;     // format 4
};

struct TrackState {
    engine::TrackId id;
    engine::TrackFormat format = engine::TrackFormat::Mono;
    StripState strip;
};

struct ClipState {
    engine::TrackId track;
    std::int64_t start = 0;
    std::int64_t length = 0; // > 0
    std::string file;        // relative to the project file's folder, or absolute
    std::int64_t sourceOffset = 0;
    int fadeIn = 0;
    int fadeOut = 0;
    bool equalPowerFades = false;
    float gainDb = 0.0f;
    bool muted = false;
    std::string name;
};

struct ProjectState {
    double sampleRate = 48000.0;
    std::uint32_t nextTrackId = 1;
    double tempo = 120.0;
    int beatsPerBar = 4;
    int beatUnit = 4;
    std::int64_t loopStart = 0;
    std::int64_t loopEnd = 0;
    bool loopEnabled = false;
    std::vector<TrackState> tracks; // display order
    std::array<StripState, engine::kNumBuses> buses{};
    StripState master;
    std::vector<ClipState> clips; // per track in start order (any order is accepted)
};

bool operator==(const SendState& a, const SendState& b);
bool operator==(const ParamState& a, const ParamState& b);
bool operator==(const InsertState& a, const InsertState& b);
bool operator==(const PointState& a, const PointState& b);
bool operator==(const LaneState& a, const LaneState& b);
bool operator==(const StripState& a, const StripState& b);
bool operator==(const TrackState& a, const TrackState& b);
bool operator==(const ClipState& a, const ClipState& b);
bool operator==(const ProjectState& a, const ProjectState& b);

struct ReadResult {
    bool ok = false;
    ProjectState project;  // valid only if ok
    std::string error;     // if not ok: "line 4: ...", the first problem found
    int formatVersion = 0; // the version the file was written in
};

// The text of the file, in the current format (kFormatVersion).
std::string writeProject(const ProjectState& project);

// Parses and checks a whole file; never throws, never trusts the text. Accepts format versions
// 1 .. kFormatVersion and refuses newer ones with a clear message. Sample positions stay at the
// file's sampleRate: converting them belongs to whoever also resamples the audio (ProjectIO).
ReadResult readProject(std::string_view text);

} // namespace project
} // namespace dawlab::app
