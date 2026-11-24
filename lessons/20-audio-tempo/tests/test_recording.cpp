// test_recording: the real engine with an input (chapter 17, Step 6). Everything on one thread;
// test_recording_threads.cpp has the audio thread.
//
// The heart of it is a loop-back through the whole engine: a click track goes through a real plugin
// and the real mixer, out of the device, through a cable and into an armed track. Each click must
// land where it is on the timeline, in every take, for any block size.

#include "TestHelpers.h"
#include "TestTracks.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <thread>
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

// A level that is not a number is ignored, and the monitor keeps the level it had: one NaN in the
// gain ramp would stay there for good, and the track would play NaN even after the monitor is off.
void monitorKeepsItsLevelWhenGivenNaN() {
    Engine e;
    const TrackId voice{1};
    e.addTrack(voice);
    e.prepare(kSampleRate, kMaxBlock);
    e.applyPendingCommandsOffline();
    Monitor& monitor = e.getTrack(voice)->getMonitor();
    monitor.setMode(MonitorMode::Always);
    monitor.setLevel(0.5f);
    e.getTransport().play();
    const auto constant = [](std::int64_t) { return 0.25f; };
    const AudioBuffer before = play(e, nullptr, 1000, 128, constant);
    DAWLAB_CHECK_NEAR(before.getSample(0, 999), 0.125f, 1e-3);

    monitor.setLevel(std::numeric_limits<float>::quiet_NaN());
    const float afterNaN = monitor.level();
    const AudioBuffer withNaN = play(e, nullptr, 1000, 128, constant);
    monitor.setLevel(std::numeric_limits<float>::infinity());
    const float afterInf = monitor.level();
    const AudioBuffer withInf = play(e, nullptr, 1000, 128, constant);
    std::printf("  monitor level 0.5, then NaN: level %g, output %g; then +inf: level %g, output %g\n",
                static_cast<double>(afterNaN), static_cast<double>(withNaN.getSample(0, 999)),
                static_cast<double>(afterInf), static_cast<double>(withInf.getSample(0, 999)));
    DAWLAB_CHECK(afterNaN == 0.5f && afterInf == 0.5f);
    DAWLAB_CHECK_NEAR(withNaN.getSample(0, 999), 0.125f, 1e-3);
    DAWLAB_CHECK_NEAR(withInf.getSample(0, 999), 0.125f, 1e-3);

    monitor.setLevel(1.0f);
    const AudioBuffer full = play(e, nullptr, 1000, 128, constant);
    DAWLAB_CHECK_NEAR(full.getSample(0, 999), 0.25f, 1e-3);
    monitor.setMode(MonitorMode::Off);
    play(e, nullptr, 1000, 128, constant);
    DAWLAB_CHECK(monitor.isSilent());
}

// Turning it off in the middle of the input: a fade, not a step.
void monitorRangeArrivesWhole() {
    // The UI thread switches a monitor between a punch of 1000-2000 and one of 100000-200000 while another
    // thread runs it in Auto at position 50000, outside both: it must never be heard there. Half of one and half
    // of the other is heard: 1000-200000 holds 50000, and 100000-2000 is no punch at all. 20000 switches, or 10 s.
    Monitor m;
    m.prepare(static_cast<int>(kSampleRate));
    m.setMode(MonitorMode::Auto);
    RecordRange near, far;
    near.punchIn = 1000;
    near.punchOut = 2000;
    far.punchIn = 100000;
    far.punchOut = 200000;
    m.setRange(near);
    std::atomic<bool> quit{false};
    std::atomic<long> calls{0};
    long heard = 0; // the audio thread's, read after join()
    std::thread audio([&] {
        constexpr int n = 16;
        float in[n], out[n];
        std::fill(in, in + n, 0.1f); // quieter than the feedback guard listens for
        const float* inputs[] = {in};
        float* outputs[] = {out};
        TimelineSegment seg;
        seg.timelineStart = 50000;
        seg.blockOffset = 0;
        seg.numSamples = n;
        while (!quit.load(std::memory_order_relaxed)) {
            std::fill(out, out + n, 0.0f);
            m.process(inputs, 1, outputs, 1, nullptr, 0, &seg, 1, n);
            if (std::any_of(out, out + n, [](float x) { return x != 0.0f; }))
                ++heard;
            calls.fetch_add(1, std::memory_order_release);
        }
    });
    const auto waitForBlocks = [&](long more) {
        const long target = calls.load(std::memory_order_acquire) + more;
        while (calls.load(std::memory_order_acquire) < target)
            std::this_thread::yield();
    };
    std::mt19937 rng(1);
    volatile unsigned sink = 0;
    int switches = 0;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (; switches < 20000 && std::chrono::steady_clock::now() < until; ++switches) {
        for (unsigned spin = rng() % 64; spin > 0; --spin) // a different moment each time
            sink = sink + 1;
        m.setRange(switches % 2 == 0 ? far : near);
        waitForBlocks(1);
    }
    quit = true;
    audio.join();
    std::printf("  %d punch switches while another thread monitors outside both: heard in %ld blocks\n", switches, heard);
    DAWLAB_CHECK(heard == 0 && switches > 0);
}

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

// A take's trail has room for three hours at any sample rate. A low rate makes a long take quick to record:
// at 256 Hz, 21 minutes are 1260 peaks of 256 frames.
void checkTrailRoom() {
    TempFolder folder;
    Engine e;
    const TrackId id{1};
    e.addTrack(id);
    e.prepare(256.0, kMaxBlock);
    e.applyPendingCommandsOffline();
    e.getTrack(id)->setArmed(true);
    RecordingSession session;
    DAWLAB_CHECK(session.begin(e, folder.path(), "long", Latencies{}) == 1);
    e.getTransport().play();
    play(e, &session, 1260 * PeakTrail::kFramesPerPeak, 256, steady);
    LiveTake live;
    DAWLAB_CHECK(session.getLive(id, live) && live.trail->size() == 1260);
    DAWLAB_CHECK(finish(e, session));
}

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
        // track 1's takes are numbered up from 1
        DAWLAB_CHECK(first.filename() == "take-1-001.wav" && session.results()[0].take.path.filename() == "take-1-002.wav");
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
    DAWLAB_CHECK(!e.isRecordingOver()); // the audio side is over; the disk thread still has frames to read
    DAWLAB_CHECK(session.pump());
    DAWLAB_CHECK(e.isRecordingOver());
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

// The audio thread has confirmed the stop, but the disk thread has not read the take's last frames yet: the ring
// is still the disk thread's. A new recording waits for it - also where the track has two channels by now - and
// the first take keeps every frame of its own.
void endNothingStartsBeforeTheDisk() {
    TempFolder folder;
    Engine e;
    const TrackId id{1};
    e.addTrack(id);
    e.prepare(kSampleRate, kMaxBlock);
    e.applyPendingCommandsOffline();
    e.getTrack(id)->setArmed(true);
    RecordingSession first;
    DAWLAB_CHECK(first.begin(e, folder.path(), "first", Latencies{}) == 1);
    e.getTransport().play();
    play(e, nullptr, 1280, 128, steady); // the disk thread does not run
    e.stopRecording();
    play(e, nullptr, 128, 128, steady); // the audio thread confirms the stop
    e.getTrack(id)->setFormat(TrackFormat::Stereo);
    RecordingSession second;
    DAWLAB_CHECK(!e.isRecordingOver() && second.begin(e, folder.path(), "second", Latencies{}) == 0);
    DAWLAB_CHECK(first.pump() && e.isRecordingOver());
    const auto results = first.results();
    DAWLAB_CHECK(results.size() == 1 && results[0].take.ok && results[0].take.numFrames == 1280 && results[0].take.info.numChannels == 1);
    if (results.size() == 1 && results[0].take.ok) {
        const auto take = readTake(results[0].take.path);
        DAWLAB_CHECK(take.size() == 1 && take[0].size() == 1280 && std::all_of(take[0].begin(), take[0].end(), [](float v) { return v == 0.5f; }));
    }
    DAWLAB_CHECK(second.begin(e, folder.path(), "second", Latencies{}) == 1);
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
    endNothingStartsBeforeTheDisk();
    endFreeSlotIsNotArmed();
    endBounce();
}

// Where a take goes, and the round trip a recorded click shows (Step 2), at their edges.
void placeAndRoundTrip() {
    Latencies l;
    l.input = 100;
    l.output = 150;
    l.mix = 30;
    l.trim = 20; // total 300
    const auto same = [](const Placement& a, std::int64_t start, std::int64_t skip, std::int64_t frames) {
        return a.timelineStart == start && a.skipFrames == skip && a.numFrames == frames;
    };
    DAWLAB_CHECK(same(place(1000, l, 5000), 700, 0, 5000));
    DAWLAB_CHECK(same(place(300, l, 5000), 0, 0, 5000));     // exactly at zero
    DAWLAB_CHECK(same(place(100, l, 5000), 0, 200, 4800));   // what was before zero is left out
    DAWLAB_CHECK(same(place(100, l, 150), 0, 150, 0));       // all of it before zero

    std::vector<float> take(1000, 0.002f);                   // noise below half the peak
    take[250] = 0.3f;                                        // the click's leading edge: above half of 0.5
    take[251] = 0.5f;
    take[400] = -0.2f;
    const std::int64_t rt = measureRoundTrip(take.data(), 1000, 10000, 9900);
    std::printf("  a click sent at 9900 comes back at frame 250 of a take that began at 10000: round trip %lld\n",
                static_cast<long long>(rt));
    DAWLAB_CHECK(rt == 350);
    DAWLAB_CHECK(measureRoundTrip(take.data(), 1000, 10000, 10250) == 0);  // no latency at all
    DAWLAB_CHECK(measureRoundTrip(take.data(), 1000, 10000, 10300) == -1); // came back before it was sent
    const std::vector<float> quiet(1000, 0.005f);
    DAWLAB_CHECK(measureRoundTrip(quiet.data(), 1000, 10000, 9900) == -1);         // no click came back
    DAWLAB_CHECK(measureRoundTrip(quiet.data(), 1000, 10000, 9900, 0.001f) == 100); // a lower bar: frame 0
    DAWLAB_CHECK(measureRoundTrip(take.data(), 0, 10000, 9900) == -1);             // an empty take
}

// A stereo track on the device's last input: its right channel is one the device does not have, so it
// hears silence there - never what lies past the device's channels (Mixer: the track's inputs). The
// mixer is given the input directly, so that something does lie past them: a pointer to 0.5.
void checkLastInputChannel() {
    Engine e;
    const TrackId voice{1};
    e.addTrack(voice, TrackFormat::Stereo);
    e.prepare(kSampleRate, kMaxBlock);
    e.applyPendingCommandsOffline();
    Track* t = e.getTrack(voice);
    t->setInputChannel(1); // inputs 1 and 2 of a device that has 0 and 1
    t->setMonitorPath(MonitorPath::Direct);
    t->getMonitor().setMode(MonitorMode::Always);
    constexpr int kFrames = 4096;
    constexpr int kBlock = 128;
    const std::vector<float> left(kFrames, 0.0f), right(kFrames, 0.25f), past(kFrames, 0.5f);
    AudioBuffer out(2, kFrames);
    for (int done = 0; done < kFrames; done += kBlock) {
        const float* channels[3] = {left.data() + done, right.data() + done, past.data() + done};
        e.getMixer().process(out.toBlock().getSubBlock(done, kBlock), nullptr, 0, 120.0, 0, InputBlock{channels, 2});
    }
    float heardLeft = 0.0f, heardRight = 0.0f;
    for (int i = kFrames - 1000; i < kFrames; ++i) {
        heardLeft = std::max(heardLeft, std::abs(out.getSample(0, i)));
        heardRight = std::max(heardRight, std::abs(out.getSample(1, i)));
    }
    std::printf("  a stereo track on the last input: %.3f left, %.3f right (expected 0.250, 0.000)\n", heardLeft, heardRight);
    DAWLAB_CHECK_NEAR(heardLeft, 0.25f, 1e-4);
    DAWLAB_CHECK(heardRight == 0.0f);
}

// ---- the pieces on their own --------------------------------------------------------------------

// First in, first out; 16 gaps fit, and a removed one makes room again.
void gapQueueKeepsItsOrder() {
    GapQueue q;
    Gap g;
    DAWLAB_CHECK(!q.peek(g));
    DAWLAB_CHECK(q.push({1, 2}) && q.push({5, 3}));
    DAWLAB_CHECK(q.peek(g) && g.atFrame == 1 && g.frames == 2);
    q.removeFirst();
    DAWLAB_CHECK(q.peek(g) && g.atFrame == 5 && g.frames == 3);
    q.removeFirst();
    DAWLAB_CHECK(!q.peek(g));
    bool all = true;
    for (std::uint64_t i = 0; i < GapQueue::kCapacity; ++i)
        all = q.push({i, 1}) && all;
    DAWLAB_CHECK(all && !q.push({99, 1}));
    q.removeFirst();
    DAWLAB_CHECK(q.push({99, 1}) && q.peek(g) && g.atFrame == 1);
}

// A ring holds exactly the frames it was made for, minus what is still unread.
void inputRingHoldsWhatItWasMadeFor() {
    const std::vector<float> a{1, 2, 3, 4, 5, 6, 7, 8};
    const float* in[1] = {a.data()};
    std::vector<float> got(16, -1.0f);
    float* out[1] = {got.data()};

    InputRing ring;
    ring.prepare(1, 8);
    DAWLAB_CHECK(ring.push(in, 1, 8));
    DAWLAB_CHECK(!ring.push(in, 1, 1) && ring.droppedFrames() == 1); // full

    ring.prepare(1, 8);
    DAWLAB_CHECK(ring.push(in, 1, 4) && !ring.push(in, 1, 5)); // room for 4 more

    ring.prepare(1, 8);
    DAWLAB_CHECK(ring.push(in, 1, 4) && ring.pop(out, 4) == 4);
    DAWLAB_CHECK(ring.push(in, 1, 8)); // what was read is room again

    ring.prepare(0, 8);
    DAWLAB_CHECK(ring.numChannels() == 1);
}

// The ring's channels that the input lacks are silent; and the frames come out whole across its end.
void inputRingChannelsAndWrap() {
    const std::vector<float> ones(4, 1.0f), twos(4, 2.0f);
    const float* in[2] = {ones.data(), twos.data()};
    std::vector<float> left(8, -1.0f), right(8, -1.0f);
    float* out[2] = {left.data(), right.data()};
    InputRing ring;
    ring.prepare(2, 8);
    DAWLAB_CHECK(ring.push(in, 1, 4) && ring.pop(out, 8) == 4);
    DAWLAB_CHECK(left[0] == 1.0f && left[3] == 1.0f && right[0] == 0.0f && right[3] == 0.0f);

    const std::vector<float> first{0, 1, 2, 3, 4, 5}, second{10, 11, 12, 13, 14, 15};
    std::vector<float> got(8, -1.0f);
    float* mono[1] = {got.data()};
    ring.prepare(1, 8);
    const float* f[1] = {first.data()};
    const float* s[1] = {second.data()};
    DAWLAB_CHECK(ring.push(f, 1, 6) && ring.pop(mono, 6) == 6);
    DAWLAB_CHECK(ring.push(s, 1, 6) && ring.pop(mono, 6) == 6); // frames 6..11: across the end
    DAWLAB_CHECK(std::equal(second.begin(), second.end(), got.begin()));
}

// Dropped frames come out as silence, exactly where they were, and pop() never gives more than asked.
void inputRingGapsArriveInPlace() {
    const std::vector<float> a{1, 2, 3, 4, 5, 6, 7, 8}, b{11, 12, 13, 14};
    const float* inA[2] = {a.data(), a.data()};
    const float* inB[2] = {b.data(), b.data()};
    std::vector<float> left(16, -1.0f), right(16, -1.0f);
    float* out[2] = {left.data(), right.data()};
    InputRing ring;
    ring.prepare(2, 8);
    DAWLAB_CHECK(ring.push(inA, 2, 8));
    DAWLAB_CHECK(!ring.push(inA, 2, 2)); // dropped: a gap of 2 after frame 8
    DAWLAB_CHECK(ring.pop(out, 4) == 4 && left[3] == 4.0f);
    DAWLAB_CHECK(ring.push(inB, 2, 4));
    std::fill(left.begin(), left.end(), -1.0f);
    std::fill(right.begin(), right.end(), -1.0f);
    const int n = ring.pop(out, 7);
    const std::vector<float> want{5, 6, 7, 8, 0, 0, 11};
    std::printf("  a gap in the ring: %d frames, %.0f %.0f %.0f %.0f %.0f %.0f %.0f\n", n, left[0], left[1], left[2], left[3], left[4],
                left[5], left[6]);
    DAWLAB_CHECK(n == 7 && std::equal(want.begin(), want.end(), left.begin()) && std::equal(want.begin(), want.end(), right.begin()));
    DAWLAB_CHECK(left[7] == -1.0f);
    DAWLAB_CHECK(ring.pop(out, 10) == 3 && left[0] == 12.0f && left[2] == 14.0f);
}

// The round trip counts from the first frame at half the peak, inclusive, and reads only the take.
void roundTripAtItsEdges() {
    const float take[5] = {0.0f, 0.25f, 0.5f, 0.0f, 10.0f}; // the last one is not part of the take
    DAWLAB_CHECK(measureRoundTrip(take, 4, 1000, 900) == 101);
    const float quiet[2] = {0.0f, 0.01f};
    DAWLAB_CHECK(measureRoundTrip(quiet, 2, 1000, 900, 0.01f) == 101); // a peak just at the bar
}

void recordRangeLoopsOnlyWhereItCan() {
    RecordRange none;
    none.loopStart = none.loopEnd = 1000;
    DAWLAB_CHECK(!none.loopsFrom(500)); // an empty loop is no loop
    RecordRange loop;
    loop.loopEnd = 1000;
    DAWLAB_CHECK(loop.loopsFrom(999) && !loop.loopsFrom(1000));
}

bool samePlacement(const Placement& p, std::int64_t start, std::int64_t skip, std::int64_t frames) {
    return p.timelineStart == start && p.skipFrames == skip && p.numFrames == frames;
}

// Three passes of a loop, the last one short; minFrames keeps a take exactly that long; a punch at the
// loop's end leaves an empty window, so no takes at all.
void takesOfALoop() {
    Latencies latencies;
    latencies.input = 100; // frame i was heard at 900 + i
    RecordRange range;
    range.loopStart = 1000;
    range.loopEnd = 2000;
    const auto takes = takesOf(1000, latencies, range, 2600);
    std::printf("  takes of a loop: %d\n", static_cast<int>(takes.size()));
    DAWLAB_CHECK(takes.size() == 3);
    if (takes.size() == 3) {
        DAWLAB_CHECK(samePlacement(takes[0], 1000, 100, 1000));
        DAWLAB_CHECK(samePlacement(takes[1], 1000, 1100, 1000));
        DAWLAB_CHECK(samePlacement(takes[2], 1000, 2100, 500));
    }
    DAWLAB_CHECK(takesOf(1000, latencies, range, 2600, 500).size() == 3);
    DAWLAB_CHECK(takesOf(1000, latencies, range, 2600, 501).size() == 2);
    range.punchIn = 2000;
    range.punchOut = 3000;
    DAWLAB_CHECK(takesOf(1000, latencies, range, 2600).empty());
}

void monitorLatencyOfBothPaths() {
    Latencies latencies;
    latencies.input = 10;
    latencies.output = 20;
    latencies.mix = 30;
    DAWLAB_CHECK(monitorLatency(latencies, MonitorPath::Direct) == 30);
    DAWLAB_CHECK(monitorLatency(latencies, MonitorPath::ThroughInserts) == 60);
}

// A recording that begins exactly at the loop's end plays straight on (the transport does too): its stream is
// not broken by the loop it does not have.
void recordingFromTheLoopsEnd() {
    Recorder r;
    RecordRange range;
    range.loopEnd = 1000;
    DAWLAB_CHECK(r.start(Latencies{}, range, 1, 4096));
    std::vector<float> samples(512, 0.5f);
    const float* input[1] = {samples.data()};
    for (std::int64_t at = 1000; at < 2000; at += 500) {
        const TimelineSegment segment{at, 0, 500};
        r.process(input, 1, &segment, 1, 0);
    }
    DAWLAB_CHECK(r.state() == Recorder::State::Recording && r.wasStarted() && r.stampedPosition() == 1000 && r.streamFrames() == 1000);
    r.stop();
    r.process(input, 1, nullptr, 0, 0);
    DAWLAB_CHECK(r.state() == Recorder::State::Idle);
}

// Each of the five chunk names and each format field must be a take's, or the header is not one.
void takeHeaderNamesAndFormat() {
    TakeInfo info;
    info.numChannels = 2;
    info.sampleRate = 48000;
    TakeInfo parsed;
    int refused = 0;
    for (int at : {0, 8, 12, 36, 100}) {
        WavHeader h = makeHeader(info);
        h[static_cast<size_t>(at)] = 'x';
        refused += parseHeader(h, parsed) ? 0 : 1;
    }
    const std::pair<int, std::uint32_t> fields[] = {{16, 18}, {20, 1}, {34, 16}, {40, 52}, {22, 0}, {24, 0}, {32, 4}};
    for (const auto& f : fields) {
        WavHeader h = makeHeader(info);
        if (f.first == 16 || f.first == 40 || f.first == 24)
            putU32(h.data() + f.first, f.second);
        else
            putU16(h.data() + f.first, static_cast<std::uint16_t>(f.second));
        refused += parseHeader(h, parsed) ? 0 : 1;
    }
    std::printf("  a take's header with one name or format field changed: %d of 12 refused\n", refused);
    DAWLAB_CHECK(refused == 12);
    DAWLAB_CHECK(parseHeader(makeHeader(info), parsed) && parsed.numChannels == 2);
}

// The header says how many bytes a second holds, and takes any sample rate a WAV file can say.
void takeHeaderFields() {
    TakeInfo info;
    info.numChannels = 2;
    info.sampleRate = 48000;
    DAWLAB_CHECK(getU32(makeHeader(info).data() + 28) == 384000u);
    info.sampleRate = std::numeric_limits<std::int32_t>::max();
    TakeInfo parsed;
    DAWLAB_CHECK(parseHeader(makeHeader(info), parsed) && parsed.sampleRate == info.sampleRate);
    WavHeader tooFast = makeHeader(info);
    putU32(tooFast.data() + 24, 0x80000000u);
    DAWLAB_CHECK(!parseHeader(tooFast, parsed));
}

// A take of exactly maxDataBytes seals; a partial frame at the end is cut off, and the sizes in the
// header are the real ones.
void sealTakeAtItsLimits() {
    const TempFolder folder;
    const fs::path recording = folder.path() / "limit.wav.recording";
    {
        const WavHeader header = makeHeader(TakeInfo{});
        std::ofstream file(recording, std::ios::binary);
        file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
        const unsigned char data[9] = {}; // two mono frames and one byte of a third
        file.write(reinterpret_cast<const char*>(data), 9);
    }
    DAWLAB_CHECK(!sealTake(recording, 7).ok && fs::file_size(recording) == kHeaderBytes + 9u);
    const SealedTake sealed = sealTake(recording, 8);
    DAWLAB_CHECK(sealed.ok && sealed.numFrames == 2);
    if (!sealed.ok)
        return;
    DAWLAB_CHECK(fs::file_size(sealed.path) == kHeaderBytes + 8u);
    WavHeader header{};
    std::ifstream file(sealed.path, std::ios::binary);
    file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    DAWLAB_CHECK(getU32(header.data() + 4) == kHeaderBytes - 8u + 8u && getU32(header.data() + kHeaderBytes - 4) == 8u);
}

// A take file writes nothing for no frames, syncs once a second's worth (exactly) has gone out, stops at
// maxDataBytes, and says when it cannot seal.
void takeFileWritesAndSyncs() {
    const TempFolder folder;
    int writes = 0, syncs = 0;
    DiskOps ops;
    ops.write = [&](const void* bytes, std::size_t numBytes, std::FILE* file) {
        ++writes;
        return std::fwrite(bytes, 1, numBytes, file);
    };
    ops.sync = [&](std::FILE*) {
        ++syncs;
        return true;
    };
    ops.maxDataBytes = 16; // four mono frames
    TakeInfo info;
    info.sampleRate = 4; // a sync every 4 frames
    const std::vector<float> frames{0.1f, 0.2f, 0.3f};
    const float* channels[1] = {frames.data()};
    {
        TakeFile file(ops);
        DAWLAB_CHECK(file.open(folder.path() / "a.wav", info) && writes == 1);
        file.write(channels, 0);
        DAWLAB_CHECK(writes == 1);
        file.write(channels, 3);
        DAWLAB_CHECK(writes == 2 && syncs == 0 && file.problem() == TakeProblem::None);
        file.write(channels, 3); // only one more fits
        DAWLAB_CHECK(syncs == 1 && file.problem() == TakeProblem::TooLong);
        const SealedTake sealed = file.finish();
        DAWLAB_CHECK(sealed.ok && sealed.numFrames == 4);
    }
    {
        TakeFile file;
        DAWLAB_CHECK(file.open(folder.path() / "b.wav", info));
        file.write(channels, 1);
        std::ofstream(folder.path() / "b.wav") << "taken"; // the name is taken by now
        DAWLAB_CHECK(!file.finish().ok && file.problem() == TakeProblem::CannotFinish);
    }
    { // one frame that does not fit is enough to be too long
        TakeFile file(ops);
        DAWLAB_CHECK(file.open(folder.path() / "c.wav", info));
        file.write(channels, 3);
        file.write(channels, 2);
        DAWLAB_CHECK(file.problem() == TakeProblem::TooLong);
    }
    { // a sync that fails is a failed write, even when the frames fit
        DiskOps failing = ops;
        int failed = 0;
        failing.sync = [&](std::FILE*) {
            ++failed;
            return false;
        };
        TakeFile file(failing);
        DAWLAB_CHECK(file.open(folder.path() / "d.wav", info));
        file.write(channels, 3);
        DAWLAB_CHECK(file.problem() == TakeProblem::None);
        file.write(channels, 1); // the fourth frame: a sync is due
        DAWLAB_CHECK(failed == 1 && file.problem() == TakeProblem::WriteFailed);
    }
}

// One window of 4 frames (a guard prepared at 80 Hz) at each of these peaks.
void feed(FeedbackGuard& guard, const std::vector<float>& peaks) {
    for (const float peak : peaks)
        for (int i = 0; i < 4; ++i)
            guard.listen(peak);
}

// Exactly kPinnedWindows pinned windows trip it, exactly kRisingWindows rising ones too (a rise of
// exactly kRise from exactly kLoud counts); a steady loud signal does not.
void feedbackGuardAtItsLimits() {
    FeedbackGuard guard;
    guard.prepare(80);
    feed(guard, std::vector<float>(4, FeedbackGuard::kPinned));
    for (int i = 0; i < 3; ++i)
        guard.listen(FeedbackGuard::kPinned);
    DAWLAB_CHECK(!guard.tripped());
    guard.listen(FeedbackGuard::kPinned); // the fifth window is full
    DAWLAB_CHECK(guard.tripped());

    std::vector<float> rising{FeedbackGuard::kLoud};
    while (rising.size() < static_cast<std::size_t>(FeedbackGuard::kRisingWindows))
        rising.push_back(FeedbackGuard::kRise * rising.back());
    guard.prepare(80);
    feed(guard, std::vector<float>(rising.begin(), rising.end() - 1));
    DAWLAB_CHECK(!guard.tripped());
    feed(guard, {rising.back()});
    DAWLAB_CHECK(guard.tripped());

    guard.prepare(80);
    feed(guard, std::vector<float>(20, 0.5f));
    DAWLAB_CHECK(!guard.tripped());
}

// A monitor at 100 Hz: its ramp is one frame, its guard's windows 5 frames.
struct MonitorRig {
    Monitor monitor;
    std::vector<float> in = std::vector<float>(32, 1.0f), out = std::vector<float>(32, 0.0f);
    const float* inputs[1] = {in.data()};
    float* outputs[1] = {out.data()};
    MonitorRig() { monitor.prepare(100); }
};

// Each segment is at its own place in the block and on the timeline, and only the block's segments count.
void monitorSegmentsInTheBlock() {
    MonitorRig rig;
    rig.monitor.setMode(MonitorMode::Auto);
    RecordRange range;
    range.punchIn = 10;
    range.punchOut = 20;
    rig.monitor.setRange(range);
    TimelineSegment segments[2];
    segments[0].timelineStart = 8; // frames 4..7 of the block play 8..11
    segments[0].blockOffset = 4;
    segments[0].numSamples = 4;
    segments[1].numSamples = 4; // not in this block
    rig.monitor.process(rig.inputs, 1, rig.outputs, 1, nullptr, 0, segments, 1, 8);
    std::printf("  a monitor in a punch at 10: %.0f %.0f %.0f %.0f | %.0f %.0f %.0f %.0f\n", rig.out[0], rig.out[1], rig.out[2], rig.out[3],
                rig.out[4], rig.out[5], rig.out[6], rig.out[7]);
    DAWLAB_CHECK(rig.out[0] == 0.0f && rig.out[3] == 0.0f);                          // segment 1 is not played
    DAWLAB_CHECK(rig.out[4] == 0.0f && rig.out[5] == 0.0f && rig.out[6] == 1.0f && rig.out[7] == 1.0f); // 10 is the punch in

    MonitorRig always;
    always.monitor.setMode(MonitorMode::Always);
    always.monitor.process(always.inputs, 1, always.outputs, 1, nullptr, 0, segments, 1, 8);
    DAWLAB_CHECK(always.out[0] == 0.0f && always.out[4] == 1.0f);
}

// What the monitor touches: only the replaced channels it is given, only the input channels it is
// given, and nothing it does not hear.
void monitorTouchesOnlyWhatItIsGiven() {
    {
        MonitorRig rig;
        rig.monitor.setMode(MonitorMode::Always);
        std::vector<float> old0(4, 1.0f), old1(4, 1.0f);
        float* replaced[2] = {old0.data(), old1.data()};
        rig.monitor.process(rig.inputs, 1, rig.outputs, 1, replaced, 1, nullptr, 0, 4);
        DAWLAB_CHECK(old0[3] == 0.0f && old1[0] == 1.0f && old1[3] == 1.0f);
    }
    {
        MonitorRig rig; // Off: the input is not heard, so not listened to either
        rig.monitor.process(rig.inputs, 1, rig.outputs, 1, nullptr, 0, nullptr, 0, 30);
        DAWLAB_CHECK(!rig.monitor.feedbackDetected() && rig.out[0] == 0.0f);
    }
    {
        MonitorRig rig;
        rig.monitor.setMode(MonitorMode::Always);
        const std::vector<float> silent(32, 0.0f);
        const float* inputs[2] = {silent.data(), rig.in.data()}; // only the first is the input
        rig.monitor.process(inputs, 1, rig.outputs, 1, nullptr, 0, nullptr, 0, 30);
        DAWLAB_CHECK(!rig.monitor.feedbackDetected() && rig.out[29] == 0.0f);
    }
    {
        MonitorRig rig;
        rig.monitor.setMode(MonitorMode::Always);
        rig.monitor.process(nullptr, 0, rig.outputs, 1, nullptr, 0, nullptr, 0, 8); // no input at all
        DAWLAB_CHECK(rig.out[7] == 0.0f);
    }
}

void checkPieces() {
    gapQueueKeepsItsOrder();
    inputRingHoldsWhatItWasMadeFor();
    inputRingChannelsAndWrap();
    inputRingGapsArriveInPlace();
    roundTripAtItsEdges();
    recordRangeLoopsOnlyWhereItCan();
    takesOfALoop();
    monitorLatencyOfBothPaths();
    takeHeaderFields();
    takeHeaderNamesAndFormat();
    recordingFromTheLoopsEnd();
    sealTakeAtItsLimits();
    takeFileWritesAndSyncs();
    feedbackGuardAtItsLimits();
    monitorSegmentsInTheBlock();
    monitorTouchesOnlyWhatItIsGiven();
}

} // namespace

int main() {
    std::printf("the engine with an input\n");
    checkNothingChangesWithoutArming();
    checkLoopback();
    placeAndRoundTrip();
    checkEveryFrameArrives();
    checkChannelsAndDry();
    checkLastInputChannel();
    checkMonitorPaths();
    checkAuto();
    monitorWithoutInput();
    monitorLevelAndOff();
    monitorKeepsItsLevelWhenGivenNaN();
    monitorRangeArrivesWhole();
    monitorFadesOut();
    monitorFollowsTheRate();
    monitorOfANewTrack();
    monitorFollowsTheSession();
    checkFeedback();
    checkLive();
    checkTrailRoom();
    checkEnds();
    checkPieces();
    return dawlab::test::summary("l17_test_recording");
}
