#pragma once
// check.h - the self-check of Step 4 (punch and loop). Defines main(); included at the END of
// starter/step04.cpp and solution/step04.cpp, so both run exactly the same checks.
//
// The heart of it is a simulated loop-back in which every rendered sample says which timeline position
// it is. So a take checks itself, frame by frame: what it plays at timeline position t has to be the
// sound that was rendered at t. This is done for random loops, punches, latencies and blocks, also
// for latencies longer than the loop. Ends with real threads; build it with -fsanitize=thread as well.

#include <dawlab/Common.h>
#include <dawlab/WavFile.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace step04_check {

using namespace minidaw;
namespace fs = std::filesystem;

using ull = unsigned long long;

constexpr int kRingFrames = 1 << 16;

// What is rendered at a timeline position: exact in a float, and never 0.
float soundAt(std::int64_t position) {
    return static_cast<float>(position % (1 << 23) + 1);
}

bool same(const Placement& p, std::int64_t timelineStart, std::int64_t skipFrames, std::int64_t numFrames) {
    return p.timelineStart == timelineStart && p.skipFrames == skipFrames && p.numFrames == numFrames;
}

// A transport and a cable, as simple as they can be. `latency` is the real way around.
class Rig {
public:
    Rig(std::int64_t position, std::int64_t loopStart, std::int64_t loopEnd, int latency)
        : position_(position), loopStart_(loopStart), loopEnd_(loopEnd), line_(static_cast<std::size_t>(latency), 0.0f) {}

    // Fills `input` with what comes back during the next n frames, and `segments` with where they are.
    void block(float* input, int n, std::vector<TimelineSegment>& segments) {
        segments.clear();
        int offset = 0;
        while (offset < n) {
            int count = n - offset;
            const bool loops = loopEnd_ > loopStart_ && position_ < loopEnd_;
            if (loops)
                count = static_cast<int>(std::min<std::int64_t>(count, loopEnd_ - position_));
            segments.push_back({position_, offset, count});
            for (int i = 0; i < count; ++i) {
                line_.push_back(soundAt(position_ + i));
                input[offset + i] = line_.front();
                line_.pop_front();
            }
            position_ += count;
            offset += count;
            if (loops && position_ == loopEnd_)
                position_ = loopStart_;
        }
    }
    std::int64_t position() const noexcept { return position_; }

private:
    std::int64_t position_, loopStart_, loopEnd_;
    std::deque<float> line_;
};

// A take's frames compared with the sound of the timeline. `silentFrames`: so many frames at the start
// of the stream were recorded before anything had come around, and are allowed to be 0.
std::int64_t misplaced(const std::vector<float>& frames, const Placement& take, std::int64_t silentFrames) {
    std::int64_t wrong = 0;
    for (std::int64_t i = 0; i < take.numFrames; ++i) {
        const std::int64_t at = take.skipFrames + i;
        if (at >= static_cast<std::int64_t>(frames.size())) {
            ++wrong;
            continue;
        }
        const float got = frames[static_cast<std::size_t>(at)];
        if (got != soundAt(take.timelineStart + i) && !(got == 0.0f && at < silentFrames))
            ++wrong;
    }
    return wrong;
}

void checkWindow() {
    const auto window = [](RecordRange range, std::int64_t stamp) { return visibleWindow(range, stamp); };
    RecordRange nothing;
    DAWLAB_CHECK(window(nothing, 0).from == 0);
    DAWLAB_CHECK(window(nothing, 0).to == kForever);

    RecordRange loop;
    loop.loopStart = 1000;
    loop.loopEnd = 2000;
    DAWLAB_CHECK(window(loop, 1500).from == 1000);
    DAWLAB_CHECK(window(loop, 1500).to == 2000);
    DAWLAB_CHECK(window(loop, 0).from == 1000); // a run-up to the loop
    DAWLAB_CHECK(window(loop, 2000).from == 0); // already past it: no loop
    DAWLAB_CHECK(window(loop, 2000).to == kForever);

    RecordRange punch;
    punch.punchIn = 1200;
    punch.punchOut = 1300;
    DAWLAB_CHECK(window(punch, 0).from == 1200);
    DAWLAB_CHECK(window(punch, 5000).to == 1300);

    RecordRange both = loop;
    both.punchIn = 1500;
    both.punchOut = 2500;
    DAWLAB_CHECK(window(both, 1000).from == 1500);
    DAWLAB_CHECK(window(both, 1000).to == 2000);

    RecordRange outside = loop;
    outside.punchIn = 3000;
    outside.punchOut = 4000;
    DAWLAB_CHECK(window(outside, 1000).to <= window(outside, 1000).from);
}

void checkTakes() {
    const Latencies l{240, 480, 752, 0}; // 1472 together

    // Nothing special: the same as place().
    {
        const auto takes = takesOf(10000, l, RecordRange{}, 5000);
        DAWLAB_CHECK(takes.size() == 1);
        if (!takes.empty()) {
            const Placement p = place(10000, l, 5000);
            DAWLAB_CHECK(same(takes[0], p.timelineStart, p.skipFrames, p.numFrames));
        }
        const auto fromZero = takesOf(100, l, RecordRange{}, 5000);
        DAWLAB_CHECK(fromZero.size() == 1 && same(fromZero[0], 0, 1372, 3628));
        DAWLAB_CHECK(takesOf(100, l, RecordRange{}, 1000).empty()); // all of it from before the song
        DAWLAB_CHECK(takesOf(10000, l, RecordRange{}, 0).empty());
    }

    // A punch: the frame heard at the punch-in comes L after it.
    {
        RecordRange punch;
        punch.punchIn = 20000;
        punch.punchOut = 21000;
        const auto takes = takesOf(10000, l, punch, 30000);
        DAWLAB_CHECK(takes.size() == 1 && same(takes[0], 20000, 11472, 1000));
        const auto stopped = takesOf(10000, l, punch, 11600); // stopped just after the punch-in
        DAWLAB_CHECK(stopped.size() == 1 && same(stopped[0], 20000, 11472, 128));
        DAWLAB_CHECK(takesOf(10000, l, punch, 11472).empty()); // stopped just before it
        const auto late = takesOf(20500 + 1472, l, punch, 30000); // recording began inside the punch
        DAWLAB_CHECK(late.size() == 1 && same(late[0], 20500, 0, 500));
        DAWLAB_CHECK(takesOf(30000, l, punch, 30000).empty()); // recording began after it
    }

    // A loop: pass 0 from where it began, then whole passes, then the last part.
    {
        RecordRange loop;
        loop.loopStart = 48000;
        loop.loopEnd = 96000;
        const auto takes = takesOf(48000, l, loop, 48000 * 2 + 10000);
        DAWLAB_CHECK(takes.size() == 3);
        if (takes.size() == 3) {
            // The first 1472 frames are from just before the loop.
            DAWLAB_CHECK(same(takes[0], 48000, 1472, 48000));
            DAWLAB_CHECK(same(takes[1], 48000, 1472 + 48000, 48000));
            DAWLAB_CHECK(same(takes[2], 48000, 1472 + 96000, 10000 - 1472));
        }

        // Stopped less than L after a wrap of the transport: the last pass is still complete, or nothing
        // of the next pass has come in.
        const auto exact = takesOf(48000, l, loop, 48000 + 1472);
        DAWLAB_CHECK(exact.size() == 1 && same(exact[0], 48000, 1472, 48000));
        const auto onlyHalf = takesOf(72000, l, loop, 24000 + 100);
        DAWLAB_CHECK(onlyHalf.size() == 1 && same(onlyHalf[0], 72000 - 1472, 0, 24000 + 100 - 0));

        // A take that is too short is left out, and only that one.
        const auto noShort = takesOf(48000, l, loop, 48000 * 2 + 1472 + 500, 1000);
        DAWLAB_CHECK(noShort.size() == 2);
        const auto withShort = takesOf(48000, l, loop, 48000 * 2 + 1472 + 500, 0);
        DAWLAB_CHECK(withShort.size() == 3);
        const auto notQuite = takesOf(48000, l, loop, 48000 * 2 + 1472 + 500, 501);
        DAWLAB_CHECK(notQuite.size() == 2);
        const auto justEnough = takesOf(48000, l, loop, 48000 * 2 + 1472 + 500, 500);
        DAWLAB_CHECK(justEnough.size() == 3);

        // Already past the loop's end: straight on.
        const auto past = takesOf(96000, l, loop, 96000);
        DAWLAB_CHECK(past.size() == 1 && same(past[0], 96000 - 1472, 0, 96000));
    }

    // A punch in a loop: the same part of every pass.
    {
        RecordRange both;
        both.loopStart = 1000;
        both.loopEnd = 2000;
        both.punchIn = 1200;
        both.punchOut = 1300;
        const Latencies small{10, 20, 0, 0};
        const auto takes = takesOf(1000, small, both, 3000);
        DAWLAB_CHECK(takes.size() == 3);
        if (takes.size() == 3) {
            DAWLAB_CHECK(same(takes[0], 1200, 230, 100));
            DAWLAB_CHECK(same(takes[1], 1200, 1230, 100));
            DAWLAB_CHECK(same(takes[2], 1200, 2230, 100));
        }
        RecordRange outside = both;
        outside.punchIn = 3000;
        outside.punchOut = 4000;
        DAWLAB_CHECK(takesOf(1000, small, outside, 3000).empty());
    }

    // More latency than loop: the frames are several passes behind the transport.
    {
        RecordRange tiny;
        tiny.loopStart = 1000;
        tiny.loopEnd = 1100;
        const auto takes = takesOf(1000, Latencies{150, 100, 0, 0}, tiny, 650);
        DAWLAB_CHECK(takes.size() == 4);
        if (takes.size() == 4) {
            DAWLAB_CHECK(same(takes[0], 1000, 250, 100));
            DAWLAB_CHECK(same(takes[3], 1000, 550, 100));
        }
    }
}

void checkBestTake() {
    DAWLAB_CHECK(bestTake({}) == -1);
    // The last whole one, not the piece after it.
    DAWLAB_CHECK(bestTake({{1500, 0, 500}, {1000, 500, 1000}, {1000, 1500, 1000}, {1000, 2500, 200}}) == 2);
    // None is whole: the longest.
    DAWLAB_CHECK(bestTake({{1500, 0, 500}, {1000, 500, 300}}) == 0);
    // Two as long: the later one.
    DAWLAB_CHECK(bestTake({{1500, 0, 300}, {1000, 500, 300}}) == 1);
    DAWLAB_CHECK(bestTake({{5000, 0, 800}}) == 0);
}

// The audio thread's part, frame by frame.
void checkProcess() {
    constexpr int kBlock = 64;
    float input[kBlock];
    const float* inputs[] = {input};
    std::fill_n(input, kBlock, 0.5f);
    const Latencies device{10, 20, 0, 0};
    RecordRange loop;
    loop.loopStart = 1000;
    loop.loopEnd = 1100;

    const auto ringFrames = [](Recorder& r) {
        Take take(1);
        take.collect(r.ring());
        return static_cast<std::int64_t>(take.numFrames());
    };
    const auto oneSegment = [](std::int64_t at, int n) { return TimelineSegment{at, 0, n}; };

    // A stream over a wrap inside a block.
    {
        Recorder r;
        DAWLAB_CHECK(r.start(device, loop, 1, kRingFrames));
        r.process(inputs, 1, nullptr, 0, 99);
        DAWLAB_CHECK(r.state() == Recorder::State::Armed); // waits for the transport
        TimelineSegment first = oneSegment(1000, kBlock);
        r.process(inputs, 1, &first, 1, 99);
        DAWLAB_CHECK(r.state() == Recorder::State::Recording);
        const TimelineSegment second[] = {{1064, 0, 36}, {1000, 36, 28}};
        r.process(inputs, 1, second, 2, 11);
        DAWLAB_CHECK(r.state() == Recorder::State::Recording);
        TimelineSegment third = oneSegment(1028, kBlock);
        r.process(inputs, 1, &third, 1, 11);
        DAWLAB_CHECK(r.state() == Recorder::State::Recording);
        DAWLAB_CHECK(r.streamFrames() == 3 * kBlock);
        DAWLAB_CHECK(ringFrames(r) == 3 * kBlock);
        r.process(inputs, 1, nullptr, 0, 11); // the transport stops
        DAWLAB_CHECK(r.isIdle());
        DAWLAB_CHECK(r.wasStarted());
        DAWLAB_CHECK(r.stampedPosition() == 1000);
        DAWLAB_CHECK(r.latencies().mix == 99); // as stamped
        DAWLAB_CHECK(r.range().loopEnd == 1100);
    }

    // A wrap at the edge of a block, and a short loop that takes two calls for one device block.
    {
        Recorder r;
        DAWLAB_CHECK(r.start(device, loop, 1, kRingFrames));
        TimelineSegment toEnd = oneSegment(1100 - 8, 8);
        r.process(inputs, 1, &toEnd, 1, 0);
        TimelineSegment after = oneSegment(1000, 8);
        r.process(inputs, 1, &after, 1, 0);
        DAWLAB_CHECK(r.state() == Recorder::State::Recording);
        DAWLAB_CHECK(r.streamFrames() == 16);
    }

    // A seek ends it, at the block.
    {
        Recorder r;
        DAWLAB_CHECK(r.start(device, RecordRange{}, 1, kRingFrames));
        TimelineSegment a = oneSegment(0, kBlock);
        r.process(inputs, 1, &a, 1, 0);
        TimelineSegment b = oneSegment(kBlock + 1, kBlock);
        r.process(inputs, 1, &b, 1, 0);
        DAWLAB_CHECK(r.isIdle());
        DAWLAB_CHECK(r.streamFrames() == kBlock);
        DAWLAB_CHECK(ringFrames(r) == kBlock);
    }

    // Not the loop's start: the range has changed. What came before the break is kept.
    {
        Recorder r;
        DAWLAB_CHECK(r.start(device, loop, 1, kRingFrames));
        TimelineSegment a = oneSegment(1100 - 8, 8);
        r.process(inputs, 1, &a, 1, 0);
        DAWLAB_CHECK(r.state() == Recorder::State::Recording);
        TimelineSegment b = oneSegment(1100 + 0, 8);
        r.process(inputs, 1, &b, 1, 0); // running past the end: the user has switched the loop off
        DAWLAB_CHECK(r.isIdle());
        DAWLAB_CHECK(r.streamFrames() == 8);
    }
    {
        Recorder r;
        DAWLAB_CHECK(r.start(device, loop, 1, kRingFrames));
        const TimelineSegment parts[] = {{1100 - 8, 0, 8}, {1050, 8, 8}};
        r.process(inputs, 1, parts, 2, 0); // wraps to another place
        DAWLAB_CHECK(r.isIdle());
        DAWLAB_CHECK(r.streamFrames() == 8);
        DAWLAB_CHECK(ringFrames(r) == 8);
    }

    // Without a loop, the same numbers are not a wrap.
    {
        Recorder r;
        DAWLAB_CHECK(r.start(device, RecordRange{}, 1, kRingFrames));
        TimelineSegment a = oneSegment(1100 - 8, 8);
        r.process(inputs, 1, &a, 1, 0);
        TimelineSegment b = oneSegment(1000, 8);
        r.process(inputs, 1, &b, 1, 0);
        DAWLAB_CHECK(r.isIdle());
    }

    // Past the loop's end when it begins: straight on, through the same numbers.
    {
        Recorder r;
        DAWLAB_CHECK(r.start(device, loop, 1, kRingFrames));
        TimelineSegment a = oneSegment(1100, 8); // exactly at the loop's end
        r.process(inputs, 1, &a, 1, 0);
        DAWLAB_CHECK(r.state() == Recorder::State::Recording);
        DAWLAB_CHECK(r.streamFrames() == 8);
        TimelineSegment b = oneSegment(1108, 8);
        r.process(inputs, 1, &b, 1, 0);
        DAWLAB_CHECK(r.state() == Recorder::State::Recording);
    }

    // Started once and stopped; the next start begins a new stream.
    {
        Recorder r;
        DAWLAB_CHECK(r.start(device, loop, 1, kRingFrames));
        TimelineSegment a = oneSegment(1000, 8);
        r.process(inputs, 1, &a, 1, 0);
        r.stop();
        r.process(inputs, 1, &a, 1, 0);
        DAWLAB_CHECK(r.isIdle());
        DAWLAB_CHECK(r.start(device, loop, 1, kRingFrames));
        DAWLAB_CHECK(!r.wasStarted());
        DAWLAB_CHECK(r.streamFrames() == 0);
    }
}

struct Session {
    std::int64_t begin = 0, loopStart = 0, loopEnd = 0;
    RecordRange range;
    Latencies reported;
    int block = 256;
    std::int64_t runUp = 0, frames = 0;
};

// One recording, from the transport to the takes. Returns the number of frames in the wrong place.
std::int64_t record(const Session& s, int& numTakes) {
    Recorder recorder;
    Latencies device = s.reported;
    const int mix = device.mix;
    device.mix = 0;
    if (!recorder.start(device, s.range, 1, kRingFrames))
        return 1;
    Rig rig(s.begin, s.loopStart, s.loopEnd, static_cast<int>(s.reported.total()));
    std::vector<float> input(static_cast<std::size_t>(s.block));
    const float* inputs[] = {input.data()};
    std::vector<TimelineSegment> segments;

    // The transport may have been running for a while before the recording starts: run it first,
    // with the recorder not looking.
    std::int64_t ranUp = 0;
    for (; ranUp < s.runUp; ranUp += s.block)
        rig.block(input.data(), s.block, segments);

    Take take(1);
    for (std::int64_t done = 0; done < s.frames; done += s.block) {
        rig.block(input.data(), s.block, segments);
        recorder.process(inputs, 1, segments.data(), static_cast<int>(segments.size()), mix);
        take.collect(recorder.ring());
    }
    recorder.process(inputs, 1, nullptr, 0, mix);
    take.collect(recorder.ring());
    if (!recorder.isIdle())
        return 1;
    if (take.numFrames() != static_cast<std::size_t>(recorder.streamFrames()))
        return 1;

    const auto takes = takesOf(recorder.stampedPosition(), recorder.latencies(), recorder.range(),
                               static_cast<std::int64_t>(take.numFrames()));
    numTakes = static_cast<int>(takes.size());
    const std::int64_t silent = std::max<std::int64_t>(s.reported.total() - ranUp, 0);
    std::int64_t wrong = 0;
    for (const Placement& p : takes)
        wrong += misplaced(take.channels()[0], p, silent);
    return wrong;
}

// Cutting where the transport wraps: what the obvious way would do.
void checkCutAtTheWrap() {
    const Latencies l{240, 480, 752, 0};
    const std::int64_t loopStart = 48000, loopEnd = 96000;
    RecordRange range;
    range.loopStart = loopStart;
    range.loopEnd = loopEnd;

    Session s;
    s.begin = loopStart;
    s.loopStart = loopStart;
    s.loopEnd = loopEnd;
    s.range = range;
    s.reported = l;
    s.block = 480;
    s.frames = 3 * (loopEnd - loopStart) + 2000;
    int numTakes = 0;
    const std::int64_t wrong = record(s, numTakes);
    DAWLAB_CHECK(numTakes == 4); // three whole passes and the start of a fourth
    DAWLAB_CHECK(wrong == 0);
    std::printf("  a loop, L = %lld: %d takes, %lld frames in the wrong place (expected 0)\n",
                static_cast<long long>(l.total()), numTakes, static_cast<long long>(wrong));
    std::printf("  cutting where the transport wraps would put the last %lld frames of each pass into the next\n",
                static_cast<long long>(l.total()));
}

void checkRandom() {
    std::mt19937 random(20261105u);
    const auto between = [&random](std::int64_t a, std::int64_t b) {
        return std::uniform_int_distribution<std::int64_t>(a, b)(random);
    };
    int sessions = 0, numTakesTotal = 0;
    std::int64_t wrong = 0;
    for (int round = 0; round < 400; ++round) {
        Session s;
        s.block = static_cast<int>(between(1, 3) * 64 + between(0, 63));
        s.reported.input = static_cast<int>(between(0, 800));
        s.reported.output = static_cast<int>(between(0, 800));
        s.reported.mix = static_cast<int>(between(0, 2000));
        s.reported.trim = static_cast<int>(between(0, 50));
        s.runUp = round % 3 == 0 ? 0 : between(1, 20000);
        const auto kind = round % 4;
        if (kind == 1 || kind == 3) {
            s.loopStart = between(0, 30000);
            s.loopEnd = s.loopStart + (round % 8 == 1 ? between(1, 900) : between(1000, 30000));
            s.range.loopStart = s.loopStart;
            s.range.loopEnd = s.loopEnd;
        }
        s.begin = between(0, 60000);
        s.frames = between(1, 150000);
        if (kind == 2 || kind == 3) {
            s.range.punchIn = between(0, 60000);
            s.range.punchOut = s.range.punchIn + between(1, 20000);
        }
        int numTakes = 0;
        wrong += record(s, numTakes);
        numTakesTotal += numTakes;
        ++sessions;
    }
    std::printf("  %d random sessions, %d takes: %lld frames in the wrong place (expected 0)\n", sessions, numTakesTotal,
                static_cast<long long>(wrong));
    DAWLAB_CHECK(numTakesTotal > sessions);
    DAWLAB_CHECK(wrong == 0);
}

// A folder of its own under the build folder, gone at the end.
class TempFolder {
public:
    TempFolder() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::path("dawlab_output") / ("l17_step04_" + std::to_string(now));
        fs::create_directories(path_);
    }
    ~TempFolder() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    fs::path file(const std::string& name) const { return path_ / name; }
    const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

bool readFrames(const fs::path& path, std::vector<float>& frames) {
    dawlab::WavData wav;
    if (!dawlab::readWav(path.string(), wav) || wav.buffer.getNumChannels() != 1)
        return false;
    frames.resize(static_cast<std::size_t>(wav.buffer.getNumSamples()));
    for (int i = 0; i < wav.buffer.getNumSamples(); ++i)
        frames[static_cast<std::size_t>(i)] = wav.buffer.getSample(0, i);
    return true;
}

// The range is in the file, so a recovered recording still knows all its takes.
void checkCrashInLoop() {
    TakeInfo info;
    info.stampedPosition = -7;
    info.range.loopStart = 5000000000LL;
    info.range.loopEnd = 6000000000LL;
    info.range.punchIn = 5100000000LL;
    info.range.punchOut = 5200000000LL;
    TakeInfo back;
    DAWLAB_CHECK(parseHeader(makeHeader(info), back));
    DAWLAB_CHECK(back.stampedPosition == -7);
    DAWLAB_CHECK(back.range.loopStart == info.range.loopStart);
    DAWLAB_CHECK(back.range.loopEnd == info.range.loopEnd);
    DAWLAB_CHECK(back.range.punchIn == info.range.punchIn);
    DAWLAB_CHECK(back.range.punchOut == info.range.punchOut);

    TempFolder folder;
    const fs::path path = folder.file("take.wav");
    const Latencies device{240, 480, 0, 0};
    const int kMix = 752;
    constexpr int kBlock = 480;
    RecordRange range;
    range.loopStart = 48000;
    range.loopEnd = 72000;
    {
        Recorder recorder;
        DAWLAB_CHECK(recorder.start(device, range, 1, kRingFrames));
        TakeWriter writer(recorder, path, 48000);
        Rig rig(0, range.loopStart, range.loopEnd, 240 + 480 + kMix);
        std::vector<float> input(kBlock);
        const float* inputs[] = {input.data()};
        std::vector<TimelineSegment> segments;
        for (int block = 0; block < 300; ++block) {
            rig.block(input.data(), kBlock, segments);
            recorder.process(inputs, 1, segments.data(), static_cast<int>(segments.size()), kMix);
            DAWLAB_CHECK(!writer.pump());
        }
    } // the program dies here

    const auto found = findUnfinishedTakes(folder.path());
    DAWLAB_CHECK(found.size() == 1);
    if (found.size() != 1)
        return;
    const SealedTake sealed = sealTake(found[0]);
    DAWLAB_CHECK(sealed.ok);
    DAWLAB_CHECK(sealed.numFrames == 300 * kBlock);
    DAWLAB_CHECK(sealed.info.range.loopEnd == range.loopEnd);
    std::vector<float> frames;
    DAWLAB_CHECK(readFrames(path, frames));
    const auto takes = takesOf(sealed.info.stampedPosition, sealed.info.latencies, sealed.info.range,
                               static_cast<std::int64_t>(sealed.numFrames));
    std::int64_t wrong = 0;
    for (const Placement& p : takes)
        wrong += misplaced(frames, p, 240 + 480 + kMix);
    std::printf("  a crash in the 4th pass: one file, %zu takes recovered, %lld frames in the wrong place (expected 0)\n",
                takes.size(), static_cast<long long>(wrong));
    DAWLAB_CHECK(takes.size() == 4);
    DAWLAB_CHECK(wrong == 0);
}

// A real audio thread with a looping transport, a real disk thread, and the UI (this thread).
// The test's own counters are relaxed: they must not synchronize anything for the code under test.
void checkThreads() {
    TempFolder folder;
    constexpr int kBlock = 128;
    constexpr int kLatency = 200;
    RecordRange range;
    range.loopStart = 1000;
    range.loopEnd = 6000;

    Recorder recorder;
    std::atomic<bool> quit{false};
    std::atomic<std::int64_t> frames{0}, allowed{0};
    std::thread audio([&] {
        float input[kBlock];
        const float* inputs[] = {input};
        Rig rig(0, range.loopStart, range.loopEnd, kLatency);
        std::vector<TimelineSegment> segments;
        std::int64_t total = 0;
        while (!quit.load(std::memory_order_relaxed)) {
            if (total >= allowed.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
                continue;
            }
            rig.block(input, kBlock, segments);
            recorder.process(inputs, 1, segments.data(), static_cast<int>(segments.size()), 0);
            total += kBlock;
            frames.store(total, std::memory_order_relaxed);
        }
    });

    const auto begin = std::chrono::steady_clock::now();
    const auto running = [&] { return std::chrono::steady_clock::now() - begin < std::chrono::seconds(30); };

    int recordings = 0, numTakes = 0;
    std::int64_t wrong = 0;
    for (int round = 0; round < 40 && running(); ++round) {
        const fs::path path = folder.file("t" + std::to_string(round) + ".wav");
        if (!recorder.start(Latencies{kLatency, 0, 0, 0}, range, 1, kRingFrames)) {
            ++wrong;
            break;
        }
        TakeWriter writer(recorder, path, 48000);
        std::atomic<bool> diskDone{false};
        std::thread disk([&] {
            while (!writer.pump()) {
                allowed.store(frames.load(std::memory_order_relaxed) + (1 << 12), std::memory_order_relaxed);
                std::this_thread::yield();
            }
            diskDone.store(true, std::memory_order_relaxed);
        });
        const std::int64_t until = frames.load(std::memory_order_relaxed) + 3000 + round * 900;
        bool asked = false;
        while (!diskDone.load(std::memory_order_relaxed)) {
            allowed.store(frames.load(std::memory_order_relaxed) + (1 << 12), std::memory_order_relaxed);
            if (!asked && (frames.load(std::memory_order_relaxed) >= until || !running())) {
                asked = true;
                recorder.stop();
            }
            std::this_thread::yield();
        }
        disk.join();

        const SealedTake& sealed = writer.result();
        if (!sealed.ok) {
            wrong += recorder.wasStarted() ? 1 : 0;
            continue;
        }
        std::vector<float> samples;
        if (!readFrames(path, samples)) {
            ++wrong;
            continue;
        }
        ++recordings;
        wrong += recorder.ring().droppedFrames() == 0 && static_cast<std::int64_t>(sealed.numFrames) != recorder.streamFrames() ? 1 : 0;
        const auto takes = takesOf(sealed.info.stampedPosition, sealed.info.latencies, sealed.info.range,
                                   static_cast<std::int64_t>(sealed.numFrames));
        numTakes += static_cast<int>(takes.size());
        const bool dropped = recorder.ring().droppedFrames() > 0;
        for (const Placement& p : takes)
            wrong += misplaced(samples, p, dropped ? static_cast<std::int64_t>(samples.size()) : 0);
        std::error_code ec;
        fs::remove(path, ec);
    }
    quit.store(true, std::memory_order_relaxed);
    audio.join();

    std::printf("  three threads: %d recordings, %d takes, %lld wrong (expected 0)\n", recordings, numTakes,
                static_cast<long long>(wrong));
    DAWLAB_CHECK(recordings > 0);
    DAWLAB_CHECK(numTakes > recordings);
    DAWLAB_CHECK(wrong == 0);
}

} // namespace step04_check

int main() {
    using namespace step04_check;
    std::printf("Step 4: punch and loop\n");
    checkWindow();
    checkTakes();
    checkBestTake();
    checkProcess();
    if (dawlab::test::counters().failures > 0)
        return dawlab::test::summary("l17_step04"); // the rest needs takes that work
    checkCutAtTheWrap();
    checkRandom();
    checkCrashInLoop();
    checkThreads();
    return dawlab::test::summary("l17_step04");
}
