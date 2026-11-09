// test_recording: the real engine with an input (chapter 17, Step 6). Everything on one thread;
// test_recording_threads.cpp has the audio thread.
//
// The heart of it is a loop-back through the whole engine: a click track goes through a real plugin
// and the real mixer, out of the device, through a cable and into an armed track. Each click must
// land where it is on the timeline, in every take, for any block size.

#include "TestHelpers.h"
#include "TestTracks.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace dawlab;
using namespace dawlab::engine;
using namespace dawlab::engine::test;
using namespace l15test;
namespace fs = std::filesystem;

namespace {

constexpr int kMaxBlock = 512;

class TempFolder {
public:
    TempFolder() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::path("dawlab_output") / ("l17_test_recording_" + std::to_string(now));
        fs::create_directories(path_);
    }
    ~TempFolder() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

std::vector<std::vector<float>> readTake(const fs::path& path) {
    WavData wav;
    std::vector<std::vector<float>> channels;
    if (!readWav(path.string(), wav))
        return channels;
    for (int ch = 0; ch < wav.buffer.getNumChannels(); ++ch) {
        channels.emplace_back(static_cast<std::size_t>(wav.buffer.getNumSamples()));
        for (int i = 0; i < wav.buffer.getNumSamples(); ++i)
            channels.back()[static_cast<std::size_t>(i)] = wav.buffer.getSample(ch, i);
    }
    return channels;
}

// A mono clip with a click at every multiple of `every`, from `every` on.
std::shared_ptr<const AudioBuffer> makeClicks(int numSamples, int every) {
    auto clip = std::make_shared<AudioBuffer>(1, numSamples);
    for (int i = every; i < numSamples; i += every)
        clip->setSample(0, i, 1.0f);
    return clip;
}

// Plays `frames` frames through the engine in blocks of `block`: the input of frame f is input(f), and
// the session's disk side runs after every block. Returns the stereo output.
template <typename Input>
AudioBuffer play(Engine& e, RecordingSession* session, std::int64_t frames, int block, Input&& input, std::int64_t first = 0) {
    AudioBuffer out(2, static_cast<int>(frames));
    std::vector<float> in(static_cast<std::size_t>(block));
    const float* channels[] = {in.data()};
    for (std::int64_t done = 0; done < frames; done += block) {
        const int n = static_cast<int>(std::min<std::int64_t>(block, frames - done));
        for (int i = 0; i < n; ++i)
            in[static_cast<std::size_t>(i)] = input(first + done + i);
        e.processBlock(out.toBlock().getSubBlock(static_cast<int>(done), n), InputBlock{channels, 1});
        if (session != nullptr)
            session->pump();
    }
    return out;
}

// One more block, with the transport stopped: the recording ends. Then the disk thread finishes.
bool finish(Engine& e, RecordingSession& session) {
    e.stopRecording();
    AudioBuffer out(2, 64);
    e.processBlock(out.toBlock());
    return session.pump();
}

void checkNothingChangesWithoutArming() {
    const auto render = [](bool withInput) {
        Engine e;
        DAWLAB_CHECK(addTestTracks(e));
        e.prepare(kSampleRate, kMaxBlock);
        e.setTrackClips(testId(0), {Clip::fromBuffer(makeClicks(9000, 100), 0)});
        e.addInsert(testTrack(0), -1, std::make_unique<PureDelayEffect>(37));
        e.applyPendingCommandsOffline();
        e.getTransport().play();
        AudioBuffer out(2, 9000);
        std::vector<float> in(9000, 0.75f);
        const float* channels[] = {in.data()};
        e.processBlock(out.toBlock(), withInput ? InputBlock{channels, 1} : InputBlock{});
        return out;
    };
    const AudioBuffer plain = render(false), withInput = render(true);
    DAWLAB_CHECK(maxAbsDiff(plain, withInput) == 0.0f);
    DAWLAB_CHECK(countAbove(plain, 0, 0.1f) > 0);
}

// The whole way around: the mix, the device, the cable, the input.
void checkLoopback() {
    constexpr int kClickEvery = 1000;
    constexpr int kReportedIn = 240, kReportedOut = 480, kPlugin = 752;
    int totalTakes = 0, totalClicks = 0, wrong = 0;
    for (const int block : {64, 97, 480, 512, 700}) {
        for (const bool loop : {false, true}) {
            TempFolder folder;
            Engine e;
            const TrackId drums{1}, voice{2};
            e.addTrack(drums);
            e.addTrack(voice);
            e.prepare(kSampleRate, kMaxBlock);
            e.setTrackClips(drums, {Clip::fromBuffer(makeClicks(48000, kClickEvery), 0)});
            e.addInsert(e.trackRef(drums), -1, std::make_unique<PureDelayEffect>(kPlugin));
            e.applyPendingCommandsOffline();
            e.getTrack(voice)->setArmed(true);
            e.getMixer().updateLatencies();
            DAWLAB_CHECK(e.getLatencySamples() == kPlugin);

            RecordRange range;
            if (loop) {
                range.loopStart = 4000;
                range.loopEnd = 9000;
                e.getTransport().setLoopRange(range.loopStart, range.loopEnd);
                e.getTransport().setLoopEnabled(true);
            }
            RecordingSession session;
            DAWLAB_CHECK(session.begin(e, folder.path(), "take", Latencies{kReportedIn, kReportedOut, 0, 0}, range) == 1);
            e.getTransport().play();

            // The cable: what went out comes back after the device's two latencies. A device block is
            // what they are made of, so it has to fit.
            const std::int64_t frames = 40000;
            const int kCable = kReportedIn + kReportedOut;
            AudioBuffer out(2, static_cast<int>(frames));
            std::vector<float> in(static_cast<std::size_t>(block));
            const float* channels[] = {in.data()};
            for (std::int64_t done = 0; done < frames; done += block) {
                const int n = static_cast<int>(std::min<std::int64_t>(block, frames - done));
                for (int i = 0; i < n; ++i) {
                    const std::int64_t from = done + i - kCable;
                    in[static_cast<std::size_t>(i)] = from >= 0 ? out.getSample(0, static_cast<int>(from)) : 0.0f;
                }
                e.processBlock(out.toBlock().getSubBlock(static_cast<int>(done), n), InputBlock{channels, 1});
                session.pump();
            }
            DAWLAB_CHECK(finish(e, session));

            const auto results = session.results();
            DAWLAB_CHECK(results.size() == 1 && results[0].take.ok);
            if (results.size() != 1 || !results[0].take.ok)
                continue;
            const SealedTake& sealed = results[0].take;
            DAWLAB_CHECK(sealed.info.latencies.total() == kReportedIn + kReportedOut + kPlugin);
            const auto samples = readTake(sealed.path);
            DAWLAB_CHECK(samples.size() == 1);
            if (samples.size() != 1)
                continue;
            const auto takes = takesOf(sealed.info.stampedPosition, sealed.info.latencies, sealed.info.range,
                                       static_cast<std::int64_t>(sealed.numFrames));
            DAWLAB_CHECK(!takes.empty());
            if (loop)
                DAWLAB_CHECK(takes.size() >= 7);
            else
                DAWLAB_CHECK(takes.size() == 1);
            for (const Placement& take : takes) {
                ++totalTakes;
                int clicks = 0;
                for (std::int64_t i = 0; i < take.numFrames; ++i) {
                    if (samples[0][static_cast<std::size_t>(take.skipFrames + i)] != 0.0f) {
                        ++clicks;
                        wrong += (take.timelineStart + i) % kClickEvery == 0 ? 0 : 1;
                    }
                }
                totalClicks += clicks;
                // A whole pass of 5000 frames holds all of its 5 clicks.
                if (loop && take.numFrames == 5000)
                    DAWLAB_CHECK(clicks == 5);
            }
        }
    }
    std::printf("  a loop-back through the engine: %d takes, %d clicks, %d in the wrong place (expected 0)\n", totalTakes,
                totalClicks, wrong);
    DAWLAB_CHECK(totalClicks > 100);
    DAWLAB_CHECK(wrong == 0);
}

// Every frame arrives, whatever the device's blocks or the engine's chunks. A loop shorter than a
// block needs several calls to advance(); the input has to go along.
void checkEveryFrameArrives() {
    TempFolder folder;
    const auto signal = [](std::int64_t f) { return static_cast<float>(f + 1); };
    for (const int block : {1, 64, 512, 513, 1400}) {
        for (const std::int64_t loopLength : {std::int64_t{0}, std::int64_t{3}, std::int64_t{100}}) {
            Engine e;
            const TrackId id{1};
            e.addTrack(id);
            e.prepare(kSampleRate, kMaxBlock);
            e.applyPendingCommandsOffline();
            e.getTrack(id)->setArmed(true);
            RecordRange range;
            if (loopLength > 0) {
                range.loopStart = 200;
                range.loopEnd = 200 + loopLength;
                e.getTransport().setLoopRange(range.loopStart, range.loopEnd);
                e.getTransport().setLoopEnabled(true);
            }
            e.getTransport().setPosition(150);
            RecordingSession session;
            DAWLAB_CHECK(session.begin(e, folder.path(), "every", Latencies{}, range) == 1);
            e.getTransport().play();
            const std::int64_t frames = 6000;
            play(e, &session, frames, block, signal);
            DAWLAB_CHECK(finish(e, session));
            const auto results = session.results();
            DAWLAB_CHECK(results.size() == 1 && results[0].take.ok);
            if (results.empty() || !results[0].take.ok)
                continue;
            DAWLAB_CHECK(static_cast<std::int64_t>(results[0].take.numFrames) == frames);
            DAWLAB_CHECK(results[0].take.info.stampedPosition == 150);
            const auto samples = readTake(results[0].take.path);
            int wrong = samples.empty() ? 1 : 0;
            if (!samples.empty())
                for (std::size_t i = 0; i < samples[0].size(); ++i)
                    wrong += samples[0][i] == signal(static_cast<std::int64_t>(i)) ? 0 : 1;
            DAWLAB_CHECK(wrong == 0);
        }
    }
}

// Which input goes where, for several tracks at once, and what is recorded is dry.
void checkChannelsAndDry() {
    TempFolder folder;
    Engine e;
    const TrackId mono{1}, stereo{2}, missing{3}, none{4}, notArmed{5};
    e.addTrack(mono, TrackFormat::Mono);
    e.addTrack(stereo, TrackFormat::Stereo);
    e.addTrack(missing, TrackFormat::Mono);
    e.addTrack(none, TrackFormat::Mono);
    e.addTrack(notArmed, TrackFormat::Mono);
    e.prepare(kSampleRate, kMaxBlock);
    e.applyPendingCommandsOffline();

    e.getTrack(mono)->setArmed(true);
    e.getTrack(mono)->setInputChannel(1);
    e.getTrack(stereo)->setArmed(true);
    e.getTrack(stereo)->setInputChannel(2);
    e.getTrack(missing)->setArmed(true);
    e.getTrack(missing)->setInputChannel(7); // the device has 4
    e.getTrack(none)->setArmed(true);
    e.getTrack(none)->setInputChannel(-1);

    // None of what a track does to its sound may reach the file.
    e.getTrack(mono)->setGainDb(kMinusInfinityDb);
    e.getTrack(mono)->setMute(true);
    e.getTrack(mono)->setPan(1.0f);
    e.addInsert(e.trackRef(mono), -1, std::make_unique<PureDelayEffect>(100));
    e.applyPendingCommandsOffline();

    RecordingSession session;
    DAWLAB_CHECK(session.begin(e, folder.path(), "ch", Latencies{}) == 4);
    e.getTransport().play();

    constexpr int kFrames = 4000;
    std::vector<std::vector<float>> in(4, std::vector<float>(kFrames));
    const float* channels[4];
    for (int ch = 0; ch < 4; ++ch) {
        channels[ch] = in[static_cast<std::size_t>(ch)].data();
        for (int i = 0; i < kFrames; ++i)
            in[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] = static_cast<float>((ch + 1) * 10000 + i);
    }
    AudioBuffer out(2, kFrames);
    e.processBlock(out.toBlock(), InputBlock{channels, 4});
    DAWLAB_CHECK(finish(e, session) || session.pump());

    const auto results = session.results();
    DAWLAB_CHECK(results.size() == 4);
    if (results.size() != 4)
        return;
    const auto of = [&results](TrackId id) -> const RecordingSession::Result& {
        for (const auto& r : results)
            if (r.track == id)
                return r;
        return results[0];
    };
    const auto monoTake = readTake(of(mono).take.path);
    const auto stereoTake = readTake(of(stereo).take.path);
    const auto missingTake = readTake(of(missing).take.path);
    const auto noneTake = readTake(of(none).take.path);
    DAWLAB_CHECK(monoTake.size() == 1);
    DAWLAB_CHECK(stereoTake.size() == 2);
    if (monoTake.size() != 1 || stereoTake.size() != 2 || missingTake.size() != 1 || noneTake.size() != 1) {
        DAWLAB_CHECK(false);
        return;
    }
    DAWLAB_CHECK(monoTake[0] == in[1]);
    DAWLAB_CHECK(stereoTake[0] == in[2]);
    DAWLAB_CHECK(stereoTake[1] == in[3]);
    DAWLAB_CHECK(*std::max_element(missingTake[0].begin(), missingTake[0].end()) == 0.0f);
    DAWLAB_CHECK(*std::max_element(noneTake[0].begin(), noneTake[0].end()) == 0.0f);

    // All takes of one recording start together.
    DAWLAB_CHECK(of(mono).take.info.stampedPosition == of(stereo).take.info.stampedPosition);
    DAWLAB_CHECK(of(mono).take.numFrames == of(stereo).take.numFrames);
    // The mix's latency is in the stamp, with the plugin on the armed track in it.
    DAWLAB_CHECK(of(mono).take.info.latencies.mix == 100);
    DAWLAB_CHECK(of(stereo).take.info.latencies.mix == 100);
}

int firstAbove(const AudioBuffer& b, float threshold, int from = 0) {
    for (int i = from; i < b.getNumSamples(); ++i)
        if (std::abs(b.getSample(0, i)) > threshold)
            return i;
    return -1;
}

// What the musician hears, measured at the output of the engine.
void checkMonitorPaths() {
    constexpr int kAt = 2000; // the ramp has long finished
    const auto impulse = [](std::int64_t f) { return f == kAt ? 1.0f : 0.0f; };

    const auto heard = [&](MonitorPath path, int otherPlugin, int ownPlugin, float faderDb, bool muted) {
        Engine e;
        const TrackId voice{1}, other{2};
        e.addTrack(voice);
        e.addTrack(other);
        e.prepare(kSampleRate, kMaxBlock);
        if (otherPlugin > 0)
            e.addInsert(e.trackRef(other), -1, std::make_unique<PureDelayEffect>(otherPlugin));
        if (ownPlugin > 0)
            e.addInsert(e.trackRef(voice), -1, std::make_unique<PureDelayEffect>(ownPlugin));
        e.applyPendingCommandsOffline();
        Track* t = e.getTrack(voice);
        t->setMonitorPath(path);
        t->getMonitor().setMode(MonitorMode::Always);
        t->setGainDb(faderDb);
        t->setMute(muted);
        const AudioBuffer out = play(e, nullptr, 6000, 128, impulse);
        const int at = firstAbove(out, 0.001f);
        struct {
            int delay;
            float level;
        } result{at < 0 ? -1 : at - kAt, at < 0 ? 0.0f : out.getSample(0, at)};
        return result;
    };

    // Directly: at once, at full level, whatever the track or the mix does.
    const auto direct = heard(MonitorPath::Direct, 752, 300, -12.0f, true);
    DAWLAB_CHECK(direct.delay == 0);
    DAWLAB_CHECK_NEAR(direct.level, 1.0f, 1e-5);

    // Through the track: with its plugins, and with the slowest plugin of ANY track.
    const auto slowOther = heard(MonitorPath::ThroughInserts, 752, 0, 0.0f, false);
    DAWLAB_CHECK(slowOther.delay == 752);
    const auto slowOwn = heard(MonitorPath::ThroughInserts, 0, 300, 0.0f, false);
    DAWLAB_CHECK(slowOwn.delay == 300);
    DAWLAB_CHECK(heard(MonitorPath::ThroughInserts, 0, 0, 0.0f, false).delay == 0);

    // Through the track: its fader, its pan law, its mute.
    DAWLAB_CHECK_NEAR(slowOwn.level, std::sqrt(0.5f), 1e-4);
    const auto quiet = heard(MonitorPath::ThroughInserts, 0, 0, -6.0f, false);
    DAWLAB_CHECK_NEAR(quiet.level, std::sqrt(0.5f) * dbToGain(-6.0f), 1e-3);
    DAWLAB_CHECK(heard(MonitorPath::ThroughInserts, 0, 0, 0.0f, true).delay == -1);

    const Latencies l{240, 480, 752, 0};
    std::printf("  what the musician hears: %lld samples directly, %lld through the inserts\n",
                static_cast<long long>(monitorLatency(l, MonitorPath::Direct)),
                static_cast<long long>(monitorLatency(l, MonitorPath::ThroughInserts)));
}

// In a punch the input takes the place of the old take, to the frame.
void checkAuto() {
    constexpr int kIn = 2000, kOut = 5000, kRamp = 240;
    constexpr float kOld = 0.5f, kInput = 0.25f;
    const auto constant = [](std::int64_t) { return kInput; };

    const auto run = [&](MonitorMode mode, MonitorPath path, bool withPunch = true) {
        Engine e;
        const TrackId voice{1};
        e.addTrack(voice);
        e.prepare(kSampleRate, kMaxBlock);
        e.setTrackClips(voice, {Clip::fromBuffer(makeConstant(kOld, 8000), 0)});
        e.applyPendingCommandsOffline();
        Track* t = e.getTrack(voice);
        t->setMonitorPath(path);
        t->getMonitor().setMode(mode);
        RecordRange range;
        if (withPunch) {
            range.punchIn = kIn;
            range.punchOut = kOut;
        }
        t->getMonitor().setRange(range);
        e.getTransport().play();
        return play(e, nullptr, 8000, 128, constant);
    };
    const float panned = std::sqrt(0.5f);

    // Direct: the old take through its pan law, the input at full level.
    {
        const AudioBuffer out = run(MonitorMode::Auto, MonitorPath::Direct);
        DAWLAB_CHECK_NEAR(out.getSample(0, kIn - 1), kOld * panned, 1e-5);
        DAWLAB_CHECK(out.getSample(0, kIn) != out.getSample(0, kIn - 1)); // exact
        DAWLAB_CHECK_NEAR(out.getSample(0, kIn + kRamp), kInput, 1e-3);   // no old take left
        DAWLAB_CHECK_NEAR(out.getSample(0, kOut - 1), kInput, 1e-3);
        DAWLAB_CHECK(out.getSample(0, kOut) != out.getSample(0, kOut - 1)); // exact
        DAWLAB_CHECK_NEAR(out.getSample(0, kOut + kRamp), kOld * panned, 1e-3);
        // A crossfade: no step is bigger than one ramp step of the two together.
        float largest = 0.0f;
        for (int i = 1; i < out.getNumSamples(); ++i)
            largest = std::max(largest, std::abs(out.getSample(0, i) - out.getSample(0, i - 1)));
        DAWLAB_CHECK(largest < 2.0f * (kOld + kInput) / kRamp);
    }
    // Through the inserts: the same, but all of it through the track.
    {
        const AudioBuffer out = run(MonitorMode::Auto, MonitorPath::ThroughInserts);
        DAWLAB_CHECK_NEAR(out.getSample(0, kIn - 1), kOld * panned, 1e-5);
        DAWLAB_CHECK_NEAR(out.getSample(0, kIn + kRamp), kInput * panned, 1e-3);
    }
    // Off: the engine changes nothing.
    {
        const AudioBuffer out = run(MonitorMode::Off, MonitorPath::Direct);
        DAWLAB_CHECK_NEAR(out.getSample(0, kIn + kRamp), kOld * panned, 1e-5);
    }
    // Always: the old take stays away all the time (the first frames are its fade).
    {
        const AudioBuffer out = run(MonitorMode::Always, MonitorPath::Direct);
        DAWLAB_CHECK_NEAR(out.getSample(0, kRamp), kInput, 1e-3);
        DAWLAB_CHECK_NEAR(out.getSample(0, 7000), kInput, 1e-3);
    }
    // Auto without a punch: everything is inside the window.
    {
        const AudioBuffer out = run(MonitorMode::Auto, MonitorPath::Direct, false);
        DAWLAB_CHECK_NEAR(out.getSample(0, 7000), kInput, 1e-3);
    }
}

// The rest of what the monitors owe the mix. One function each, for the reason given at checkEnds.
float largestStep(const AudioBuffer& b, int from = 1) {
    float largest = 0.0f;
    for (int i = from; i < b.getNumSamples(); ++i)
        largest = std::max(largest, std::abs(b.getSample(0, i) - b.getSample(0, i - 1)));
    return largest;
}

// A track with a mode but no input has nothing to replace its old take with.
void monitorWithoutInput() {
    Engine e;
    const TrackId voice{1};
    e.addTrack(voice);
    e.prepare(kSampleRate, kMaxBlock);
    e.setTrackClips(voice, {Clip::fromBuffer(makeConstant(0.5f, 4000), 0)});
    e.applyPendingCommandsOffline();
    e.getTrack(voice)->setInputChannel(-1);
    e.getTrack(voice)->getMonitor().setMode(MonitorMode::Always);
    e.getTransport().play();
    const AudioBuffer out = play(e, nullptr, 4000, 128, [](std::int64_t) { return 0.25f; });
    DAWLAB_CHECK_NEAR(out.getSample(0, 3000), 0.5f * std::sqrt(0.5f), 1e-5);
}

// A level of 0 means "I do not want to hear myself", not "bring back the old take". And when the
// monitor is turned off, the old take comes back with a fade, as the input leaves with one.
void monitorLevelAndOff() {
    Engine e;
    const TrackId voice{1};
    e.addTrack(voice);
    e.prepare(kSampleRate, kMaxBlock);
    e.setTrackClips(voice, {Clip::fromBuffer(makeConstant(0.5f, 8000), 0)});
    e.applyPendingCommandsOffline();
    Monitor& monitor = e.getTrack(voice)->getMonitor();
    monitor.setMode(MonitorMode::Always);
    monitor.setLevel(0.5f);
    e.getTransport().play();
    const auto constant = [](std::int64_t) { return 0.25f; };
    const AudioBuffer half = play(e, nullptr, 1000, 128, constant);
    DAWLAB_CHECK_NEAR(half.getSample(0, 999), 0.125f, 1e-3); // half the input, and nothing of the old take

    monitor.setLevel(0.0f);
    const AudioBuffer nothing = play(e, nullptr, 1000, 128, constant);
    DAWLAB_CHECK_NEAR(nothing.getSample(0, 999), 0.0f, 1e-3);

    monitor.setMode(MonitorMode::Off);
    const AudioBuffer back = play(e, nullptr, 1000, 128, constant);
    DAWLAB_CHECK(back.getSample(0, 0) < 0.01f);
    DAWLAB_CHECK_NEAR(back.getSample(0, 999), 0.5f * std::sqrt(0.5f), 1e-5);
    DAWLAB_CHECK(largestStep(back) < 0.01f);
}

// Turning it off in the middle of the input: a fade, not a step.
void monitorFadesOut() {
    Engine e;
    const TrackId voice{1};
    e.addTrack(voice);
    e.prepare(kSampleRate, kMaxBlock);
    e.applyPendingCommandsOffline();
    Monitor& monitor = e.getTrack(voice)->getMonitor();
    monitor.setMode(MonitorMode::Always);
    const auto constant = [](std::int64_t) { return 1.0f; };
    play(e, nullptr, 1000, 128, constant);
    monitor.setMode(MonitorMode::Off);
    const AudioBuffer out = play(e, nullptr, 1000, 128, constant);
    DAWLAB_CHECK(out.getSample(0, 0) > 0.9f);
    DAWLAB_CHECK(out.getSample(0, 999) == 0.0f);
    DAWLAB_CHECK(largestStep(out) < 0.01f);
}

// The ramp follows the sample rate of the engine.
void monitorFollowsTheRate() {
    Engine e;
    const TrackId voice{1};
    e.addTrack(voice);
    e.prepare(96000.0, kMaxBlock);
    e.applyPendingCommandsOffline();
    e.getTrack(voice)->getMonitor().setMode(MonitorMode::Always);
    const AudioBuffer out = play(e, nullptr, 1000, 128, [](std::int64_t) { return 1.0f; });
    DAWLAB_CHECK_NEAR(out.getSample(0, 239), 0.5f, 1e-3); // halfway through 5 ms at 96 kHz
    DAWLAB_CHECK_NEAR(out.getSample(0, 479), 1.0f, 1e-3);
}

// A guard that tripped belongs to the track that made it trip, not to the next one in that slot.
void monitorOfANewTrack() {
    Engine e;
    const TrackId first{1}, second{2};
    e.addTrack(first);
    e.prepare(kSampleRate, kMaxBlock);
    e.applyPendingCommandsOffline();
    e.getTrack(first)->getMonitor().setMode(MonitorMode::Always);
    const auto loud = [](std::int64_t) { return 1.0f; };
    play(e, nullptr, 48000, 128, loud);
    DAWLAB_CHECK(e.getTrack(first)->getMonitor().feedbackDetected());

    DAWLAB_CHECK(e.removeTrack(first));
    e.addTrack(second);
    e.applyPendingCommandsOffline();
    DAWLAB_CHECK(e.slotOf(second) == 0);
    Monitor& monitor = e.getTrack(second)->getMonitor();
    DAWLAB_CHECK(!monitor.feedbackDetected());
    monitor.setMode(MonitorMode::Always);
    const AudioBuffer out = play(e, nullptr, 1000, 128, [](std::int64_t) { return 0.25f; });
    DAWLAB_CHECK_NEAR(out.getSample(0, 999), 0.25f, 1e-3);
    DAWLAB_CHECK(!monitor.feedbackDetected());
}

// The session tells the monitors what the recording is for.
void monitorFollowsTheSession() {
    TempFolder folder;
    Engine e;
    const TrackId voice{1};
    e.addTrack(voice);
    e.prepare(kSampleRate, kMaxBlock);
    e.applyPendingCommandsOffline();
    Track* t = e.getTrack(voice);
    t->setArmed(true);
    t->getMonitor().setMode(MonitorMode::Auto);
    RecordRange punch;
    punch.punchIn = 2000;
    punch.punchOut = 3000;
    RecordingSession session;
    DAWLAB_CHECK(session.begin(e, folder.path(), "punch", Latencies{}, punch) == 1);
    e.getTransport().play();
    const AudioBuffer out = play(e, &session, 4000, 128, [](std::int64_t) { return 1.0f; });
    DAWLAB_CHECK(out.getSample(0, 1999) == 0.0f);
    DAWLAB_CHECK(out.getSample(0, 2000) > 0.0f);
    DAWLAB_CHECK(out.getSample(0, 3500) == 0.0f);
}

// The guard on the real path, also when it is the track's fader that makes the loop louder.
void checkFeedback() {
    TempFolder folder;
    Engine e;
    const TrackId voice{1};
    e.addTrack(voice);
    e.prepare(kSampleRate, kMaxBlock);
    e.setTrackClips(voice, {Clip::fromBuffer(makeConstant(0.0f, 10), 0)});
    e.applyPendingCommandsOffline();
    Track* t = e.getTrack(voice);
    t->setArmed(true);
    t->setMonitorPath(MonitorPath::ThroughInserts);
    t->getMonitor().setMode(MonitorMode::Always);
    t->setGainDb(6.0f);

    RecordingSession session;
    DAWLAB_CHECK(session.begin(e, folder.path(), "howl", Latencies{}) == 1);
    e.getTransport().play();

    constexpr int kBlock = 256, kFrames = 48000 * 3, kAir = 1024;
    AudioBuffer out(2, kFrames);
    std::vector<float> in(kBlock), recorded;
    const float* channels[] = {in.data()};
    int trippedAt = -1;
    for (int done = 0; done + kBlock <= kFrames; done += kBlock) {
        for (int i = 0; i < kBlock; ++i) {
            const int from = done + i - kAir;
            const float noise = 0.01f * static_cast<float>(((done + i) * 7919) % 201 - 100) / 100.0f;
            in[static_cast<std::size_t>(i)] = std::clamp(noise + (from >= 0 ? out.getSample(0, from) : 0.0f), -1.0f, 1.0f);
            recorded.push_back(in[static_cast<std::size_t>(i)]);
        }
        e.processBlock(out.toBlock().getSubBlock(done, kBlock), InputBlock{channels, 1});
        session.pump();
        if (trippedAt < 0 && t->getMonitor().feedbackDetected())
            trippedAt = done;
    }
    DAWLAB_CHECK(trippedAt >= 0);
    DAWLAB_CHECK(trippedAt < 48000);
    float after = 0.0f;
    for (int i = trippedAt + 48000 / 2; i + kBlock < kFrames; ++i)
        after = std::max(after, std::abs(out.getSample(0, i)));
    DAWLAB_CHECK(after < 0.001f);

    // The recording did not notice.
    DAWLAB_CHECK(finish(e, session) || session.pump());
    const auto results = session.results();
    DAWLAB_CHECK(results.size() == 1 && results[0].take.ok);
    if (results.empty())
        return;
    const auto samples = readTake(results[0].take.path);
    DAWLAB_CHECK(samples.size() == 1);
    if (!samples.empty()) {
        DAWLAB_CHECK(samples[0].size() == recorded.size());
        DAWLAB_CHECK(samples[0] == recorded);
    }
    std::printf("  the mix makes the loop louder: detected after %d ms, the take has every frame\n", trippedAt * 1000 / 48000);
}

// What a display can see of a take while it is still being recorded.
void checkLive() {
    TempFolder folder;
    Engine e;
    const TrackId id{1}, other{2};
    e.addTrack(id);
    e.addTrack(other);
    e.prepare(kSampleRate, kMaxBlock);
    e.applyPendingCommandsOffline();
    e.getTrack(id)->setArmed(true);
    RecordRange range;
    range.punchIn = 1000;
    range.punchOut = 90000;
    RecordingSession session;
    DAWLAB_CHECK(session.begin(e, folder.path(), "live", Latencies{10, 20, 0, 5}, range) == 1);
    LiveTake live;
    DAWLAB_CHECK(!session.getLive(id, live)); // nothing yet
    e.getTransport().setPosition(700);
    e.getTransport().play();
    // Half of it loud on one side of a peak's frames, so that a peak really is the highest frame.
    play(e, &session, PeakTrail::kFramesPerPeak * 10, 64, [](std::int64_t f) {
        return f % PeakTrail::kFramesPerPeak == 7 ? -0.75f : static_cast<float>(f % 3) * 0.01f;
    });
    DAWLAB_CHECK(session.getLive(id, live));
    DAWLAB_CHECK(!session.getLive(other, live) || live.trail != nullptr);
    LiveTake none;
    DAWLAB_CHECK(!session.getLive(other, none));
    DAWLAB_CHECK(live.stampedPosition == 700);
    DAWLAB_CHECK(live.latencies.total() == 35);
    DAWLAB_CHECK(live.range.punchOut == 90000);
    DAWLAB_CHECK(live.trail->size() == 10);
    if (live.trail->size() == 10) {
        DAWLAB_CHECK(live.trail->at(0) == 0.75f);
        DAWLAB_CHECK(live.trail->at(9) == 0.75f);
    }
    DAWLAB_CHECK(session.droppedFrames() == 0);
    DAWLAB_CHECK(finish(e, session));

    // It stops growing at its room, and that is all.
    PeakTrail small(2);
    std::vector<float> ones(PeakTrail::kFramesPerPeak * 5, 0.5f);
    const float* channels[] = {ones.data()};
    small.add(channels, 1, static_cast<int>(ones.size()));
    DAWLAB_CHECK(small.size() == 2);
    DAWLAB_CHECK(small.at(1) == 0.5f);
}

// All the ways a recording can end, and what comes after. One function each: an Engine is big, and
// MSVC Debug gives every local in a function its own place on the stack.
float steady(std::int64_t) { return 0.5f; }

// Nothing armed: nothing starts, and no file.
void endNothingArmed() {
    TempFolder folder;
    Engine e;
    e.addTrack(TrackId{1});
    e.prepare(kSampleRate, kMaxBlock);
    e.applyPendingCommandsOffline();
    RecordingSession session;
    DAWLAB_CHECK(session.begin(e, folder.path(), "x", Latencies{}) == 0);
    DAWLAB_CHECK(session.pump());
    DAWLAB_CHECK(e.isRecordingOver());
}

// The transport never played: armed, stopped, and no file.
void endNeverPlayed() {
    TempFolder folder;
    Engine e;
    const TrackId id{1};
    e.addTrack(id);
    e.prepare(kSampleRate, kMaxBlock);
    e.applyPendingCommandsOffline();
    e.getTrack(id)->setArmed(true);
    RecordingSession session;
    DAWLAB_CHECK(session.begin(e, folder.path(), "x", Latencies{}) == 1);
    play(e, &session, 1000, 128, steady);
    DAWLAB_CHECK(!e.isRecordingOver());
    DAWLAB_CHECK(finish(e, session) || session.pump());
    DAWLAB_CHECK(!session.results()[0].take.ok);
    DAWLAB_CHECK(fs::is_empty(folder.path()));
}

// The transport stops, then a seek: each ends the take by itself. A new one can follow, under a new
// name, while the old one stays.
void endTransportAndSeek() {
    TempFolder folder;
    Engine e;
    const TrackId id{1};
    e.addTrack(id);
    e.prepare(kSampleRate, kMaxBlock);
    e.applyPendingCommandsOffline();
    e.getTrack(id)->setArmed(true);
    fs::path first;
    {
        RecordingSession session;
        DAWLAB_CHECK(session.begin(e, folder.path(), "take", Latencies{}) == 1);
        RecordingSession tooEarly;
        DAWLAB_CHECK(tooEarly.begin(e, folder.path(), "take", Latencies{}) == 0); // one at a time
        e.getTransport().play();
        play(e, &session, 1000, 128, steady);
        e.getTransport().stop();
        play(e, &session, 128, 128, steady);
        DAWLAB_CHECK(e.isRecordingOver());
        DAWLAB_CHECK(session.pump());
        DAWLAB_CHECK(session.results()[0].take.ok);
        first = session.results()[0].take.path;
    }
    {
        RecordingSession session;
        DAWLAB_CHECK(session.begin(e, folder.path(), "take", Latencies{}) == 1);
        e.getTransport().play();
        play(e, &session, 1000, 128, steady);
        e.getTransport().setPosition(50000);
        play(e, &session, 128, 128, steady);
        DAWLAB_CHECK(e.isRecordingOver());
        DAWLAB_CHECK(session.pump());
        DAWLAB_CHECK(session.results()[0].take.ok);
        DAWLAB_CHECK(session.results()[0].take.path != first);
        DAWLAB_CHECK(fs::exists(first));
    }
}

// A track is removed while it records: its recording ends, in a slot that no longer plays.
void endTrackRemoved() {
    TempFolder folder;
    Engine e;
    const TrackId id{1}, next{2};
    e.addTrack(id);
    e.prepare(kSampleRate, kMaxBlock);
    e.applyPendingCommandsOffline();
    e.getTrack(id)->setArmed(true);
    RecordingSession session;
    DAWLAB_CHECK(session.begin(e, folder.path(), "gone", Latencies{}) == 1);
    e.getTransport().play();
    play(e, &session, 1000, 128, steady);
    DAWLAB_CHECK(e.removeTrack(id));
    play(e, &session, 128, 128, steady);
    e.stopRecording();
    play(e, &session, 128, 128, steady);
    DAWLAB_CHECK(e.isRecordingOver());
    DAWLAB_CHECK(session.pump());
    DAWLAB_CHECK(session.results()[0].take.ok);
    // The slot starts over: not armed, no monitor.
    e.addTrack(next);
    e.applyPendingCommandsOffline();
    DAWLAB_CHECK(!e.getTrack(next)->isArmed());
    DAWLAB_CHECK(e.startRecording(Latencies{}) == 0);
}

// The device stops: nobody is left to confirm the end. prepare() says it is over, and the take
// keeps the sample rate it was recorded at.
void endDeviceStops() {
    TempFolder folder;
    Engine e;
    const TrackId id{1};
    e.addTrack(id);
    e.prepare(48000.0, kMaxBlock);
    e.applyPendingCommandsOffline();
    e.getTrack(id)->setArmed(true);
    RecordingSession session;
    DAWLAB_CHECK(session.begin(e, folder.path(), "device", Latencies{}) == 1);
    e.getTransport().play();
    play(e, &session, 1000, 128, steady);
    DAWLAB_CHECK(!e.isRecordingOver());
    e.prepare(44100.0, kMaxBlock);
    DAWLAB_CHECK(e.isRecordingOver());
    DAWLAB_CHECK(session.pump());
    const auto results = session.results();
    DAWLAB_CHECK(results[0].take.ok);
    DAWLAB_CHECK(results[0].take.info.sampleRate == 48000);
    DAWLAB_CHECK(results[0].take.numFrames > 0);

    RecordingSession next;
    DAWLAB_CHECK(next.begin(e, folder.path(), "device", Latencies{}) == 1);
    play(e, &next, 1000, 128, steady);
    e.stopRecording();
    play(e, &next, 128, 128, steady);
    DAWLAB_CHECK(next.pump());
    DAWLAB_CHECK(next.results()[0].take.info.sampleRate == 44100);
}

// The program dies: the next run finds the file, and it still knows all its takes.
void endProgramDies() {
    TempFolder folder;
    {
        Engine e;
        const TrackId id{1};
        e.addTrack(id);
        e.prepare(kSampleRate, kMaxBlock);
        e.applyPendingCommandsOffline();
        e.getTrack(id)->setArmed(true);
        RecordRange range;
        range.loopStart = 1000;
        range.loopEnd = 2000;
        e.getTransport().setLoopRange(1000, 2000);
        e.getTransport().setLoopEnabled(true);
        RecordingSession session;
        DAWLAB_CHECK(session.begin(e, folder.path(), "crash", Latencies{}, range) == 1);
        e.getTransport().play();
        play(e, &session, 5000, 128, steady);
    } // never finished
    const auto found = findUnfinishedTakes(folder.path());
    DAWLAB_CHECK(found.size() == 1);
    if (!found.empty()) {
        const SealedTake sealed = sealTake(found[0]);
        DAWLAB_CHECK(sealed.ok);
        DAWLAB_CHECK(takesOf(sealed.info.stampedPosition, sealed.info.latencies, sealed.info.range,
                             static_cast<std::int64_t>(sealed.numFrames)).size() == 4);
    }
}

// A session is one recording: it cannot start twice, not even after it has ended.
void endSessionIsOnce() {
    TempFolder folder;
    Engine e;
    const TrackId id{1};
    e.addTrack(id);
    e.prepare(kSampleRate, kMaxBlock);
    e.applyPendingCommandsOffline();
    e.getTrack(id)->setArmed(true);
    RecordingSession session;
    DAWLAB_CHECK(session.begin(e, folder.path(), "once", Latencies{}) == 1);
    e.getTransport().play();
    play(e, &session, 1000, 128, steady);
    e.getTransport().stop();
    play(e, &session, 128, 128, steady);
    DAWLAB_CHECK(session.pump());
    DAWLAB_CHECK(session.begin(e, folder.path(), "once", Latencies{}) == 0);
    DAWLAB_CHECK(e.isRecordingOver());
    DAWLAB_CHECK(session.results().size() == 1);
}

// While a recording is ending, nothing else starts, not even a track that has nothing to do with it.
void endNothingStartsMeanwhile() {
    Engine e;
    const TrackId a{1}, b{2};
    e.addTrack(a);
    e.addTrack(b);
    e.prepare(kSampleRate, kMaxBlock);
    e.applyPendingCommandsOffline();
    e.getTrack(a)->setArmed(true);
    DAWLAB_CHECK(e.startRecording(Latencies{}) == 1);
    e.getTrack(b)->setArmed(true);
    DAWLAB_CHECK(e.startRecording(Latencies{}) == 0);
    DAWLAB_CHECK(e.getTrack(b)->getRecorder().isIdle());
}

// A slot whose track has gone is not armed, whatever its values say.
void endFreeSlotIsNotArmed() {
    Engine e;
    const TrackId id{1};
    e.addTrack(id);
    e.prepare(kSampleRate, kMaxBlock);
    e.applyPendingCommandsOffline();
    e.getTrack(id)->setArmed(true);
    DAWLAB_CHECK(e.removeTrack(id));
    e.applyPendingCommandsOffline();
    DAWLAB_CHECK(e.startRecording(Latencies{}) == 0);
    DAWLAB_CHECK(e.isRecordingOver());
}

// A bounce has no device: an armed, monitored track makes no difference.
void endBounce() {
    const auto bounce = [](bool armed) {
        Engine e;
        const TrackId id{1};
        e.addTrack(id);
        e.prepare(kSampleRate, kMaxBlock);
        e.setTrackClips(id, {Clip::fromBuffer(makeClicks(4000, 500), 0)});
        e.applyPendingCommandsOffline();
        if (armed) {
            e.getTrack(id)->setArmed(true);
            e.getTrack(id)->setInputChannel(-1);
        }
        return render(e, 4000);
    };
    DAWLAB_CHECK(maxAbsDiff(bounce(false), bounce(true)) == 0.0f);
}
void checkEnds() {
    endNothingArmed();
    endNeverPlayed();
    endTransportAndSeek();
    endTrackRemoved();
    endDeviceStops();
    endProgramDies();
    endSessionIsOnce();
    endNothingStartsMeanwhile();
    endFreeSlotIsNotArmed();
    endBounce();
}

} // namespace

int main() {
    std::printf("the engine with an input\n");
    checkNothingChangesWithoutArming();
    checkLoopback();
    checkEveryFrameArrives();
    checkChannelsAndDry();
    checkMonitorPaths();
    checkAuto();
    monitorWithoutInput();
    monitorLevelAndOff();
    monitorFadesOut();
    monitorFollowsTheRate();
    monitorOfANewTrack();
    monitorFollowsTheSession();
    checkFeedback();
    checkLive();
    checkEnds();
    return dawlab::test::summary("l17_test_recording");
}
