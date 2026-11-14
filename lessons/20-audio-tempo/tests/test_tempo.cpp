// test_tempo: a tempo map and instrument latency in the real engine (chapter 19, Step 4).
//
//   1. Notes follow a map of jumps and ramps: every note on at its tick's sample, in any block size.
//   2. A loop set in ticks plays exactly its ticks every pass, also where two ticks share a sample.
//   3. A new map while a note is held: the playhead keeps its musical place, the note is not cut, and it ends at
//      its tick under the new map.
//   4. An instrument's latency is in its track's: the delay compensation lines the tracks up; low-latency
//      monitoring takes an armed track out of it.
//   4b. The song's end (Engine::getSongEndSample, a bounce's default length) puts the MIDI clips through the map.
//   5. Changing the map while it plays allocates nothing on the audio thread (this file replaces operator new).
//   6. A UI thread that changes the map while the audio thread plays: the audio thread never sees half a map
//      (ThreadSanitizer). Nor half a loop: a loop switched between samples and ticks arrives whole.

#include "TestHelpers.h"
#include "TestTracks.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <random>
#include <thread>

// ---- allocation counting (as in test_midi.cpp) -------------------------------------------------------------
namespace {
thread_local bool tCounting = false;
std::atomic<long> gAllocs{0};
void* countedAlloc(std::size_t n) {
    if (tCounting)
        gAllocs.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}
void countedFree(void* p) noexcept {
    if (p != nullptr && tCounting)
        gAllocs.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
struct CountScope {
    CountScope() noexcept { tCounting = true; }
    ~CountScope() { tCounting = false; }
    CountScope(const CountScope&) = delete;
    CountScope& operator=(const CountScope&) = delete;
};
} // namespace

void* operator new(std::size_t n) { return countedAlloc(n); }
void* operator new[](std::size_t n) { return countedAlloc(n); }
void operator delete(void* p) noexcept { countedFree(p); }
void operator delete[](void* p) noexcept { countedFree(p); }
void operator delete(void* p, std::size_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { countedFree(p); }

using namespace l15test;
using namespace dawlab::engine::test;

namespace {

constexpr int64_t kBar = 4 * kTicksPerQuarter;

// An instrument that writes down when each event came (in samples since it was put in) and answers every note on
// with an impulse `latency` samples later.
class Logger final : public Instrument {
public:
    struct Logged {
        int64_t sample;
        MidiMessage message;
    };
    explicit Logger(int latency = 0) : latency_(latency) { log.reserve(4096); }
    std::vector<Logged> log;

    void prepare(double, int) override {}
    void reset() noexcept override { numDue_ = 0; }
    int getLatencySamples() const noexcept override { return latency_; }
    void render(const BlockEvent* events, int numEvents, float* const* out, int numChannels, int numSamples) noexcept override {
        for (int k = 0; k < numEvents; ++k) {
            if (log.size() < log.capacity())
                log.push_back({rendered_ + events[k].offset, events[k].message});
            if (events[k].message.kind() == MidiKind::NoteOn && numDue_ < static_cast<int>(due_.size()))
                due_[static_cast<size_t>(numDue_++)] = rendered_ + events[k].offset + latency_;
        }
        for (int d = 0; d < numDue_;) {
            const int64_t at = due_[static_cast<size_t>(d)] - rendered_;
            if (at < numSamples) {
                for (int ch = 0; ch < numChannels; ++ch)
                    out[ch][at] += 1.0f;
                due_[static_cast<size_t>(d)] = due_[static_cast<size_t>(--numDue_)];
            } else {
                ++d;
            }
        }
        rendered_ += numSamples;
    }
    std::vector<int64_t> noteOns() const {
        std::vector<int64_t> at;
        for (const Logged& l : log)
            if (l.message.kind() == MidiKind::NoteOn)
                at.push_back(l.sample);
        return at;
    }

private:
    int latency_ = 0;
    int64_t rendered_ = 0;
    std::array<int64_t, 256> due_{};
    int numDue_ = 0;
};

MidiClip clipOf(int id, int64_t start, int64_t length, std::vector<Note> notes) {
    MidiClip c;
    c.id = id;
    c.start = start;
    c.length = length;
    c.notes = std::move(notes);
    return c;
}

TempoMap mapOf(std::vector<TempoPoint> points) {
    TempoMap m;
    m.set(points);
    return m;
}

// An engine at `rate` with 8 tracks, track 0 an instrument track with a Logger.
Logger* setUp(Engine& e, double rate = kSampleRate, int latency = 0) {
    e.prepare(rate, kBlockSize);
    addTestTracks(e);
    e.getMixer().getTrack(0).setInstrument(true);
    auto logger = std::make_unique<Logger>(latency);
    Logger* observer = logger.get();
    e.setTrackInstrument(testId(0), std::move(logger));
    e.applyPendingCommandsOffline();
    return observer;
}

// Plays `numSamples` in blocks of `block` (0: random 1..1200), counting allocations on this thread while processing.
AudioBuffer play(Engine& e, int64_t numSamples, int block, long* allocs = nullptr, unsigned seed = 1,
                 const std::function<void(int64_t)>& beforeBlock = {}) {
    AudioBuffer out(kNumChannels, static_cast<int>(numSamples));
    std::mt19937 rng(seed);
    for (int64_t done = 0; done < numSamples;) {
        if (beforeBlock)
            beforeBlock(done);
        const int n = static_cast<int>(std::min<int64_t>(block > 0 ? block : std::uniform_int_distribution<int>(1, 1200)(rng), numSamples - done));
        const long before = gAllocs.load();
        {
            CountScope count;
            e.processBlock(out.toBlock().getSubBlock(static_cast<int>(done), n));
        }
        if (allocs != nullptr)
            *allocs += gAllocs.load() - before;
        done += n;
    }
    return out;
}

// Speeds up over bars 1-2, holds, jumps down at bar 4, slows down over bars 5-6.
TempoMap song() {
    return mapOf({{0, 90.0, true}, {2 * kBar, 150.0, false}, {3 * kBar, 100.0, true}, {5 * kBar, 70.0, false}});
}

void testNotesFollowTheMap() {
    std::vector<Note> beats;
    for (int64_t t = 0; t < 8 * kBar; t += kTicksPerQuarter)
        beats.push_back({t, 480, 60, 100, 0});
    const TempoMap map = song();
    std::vector<int64_t> want;
    for (const Note& n : beats)
        want.push_back(map.sampleOfTick(n.start, kSampleRate));
    bool same = true;
    for (const int block : {512, 0, 7}) {
        Engine e;
        Logger* log = setUp(e);
        e.setTrackMidiClips(testId(0), {clipOf(1, 0, 8 * kBar, beats)});
        e.getTransport().setTempoMap(map);
        e.getTransport().play();
        play(e, map.sampleOfTick(8 * kBar, kSampleRate), block, nullptr, 3);
        same = same && log->noteOns() == want;
    }
    std::printf("  32 beats over a map that speeds up, jumps and slows down: every note on at its sample in blocks of 512, random and 7: %s\n",
                same ? "yes" : "NO");
    DAWLAB_CHECK(same);
    // The conversions the UI uses follow the map too.
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    e.getTransport().setTempoMap(map);
    const BarsBeats bar4 = e.getTransport().samplesToBarsBeats(map.sampleOfTick(3 * kBar, kSampleRate));
    DAWLAB_CHECK(bar4.bar == 4 && bar4.beat == 1 && bar4.tick == 0);
    const BarsBeats after = e.getTransport().samplesToBarsBeats(map.sampleOfTick(3 * kBar, kSampleRate) + 1); // between two ticks
    DAWLAB_CHECK(after.bar == 4 && after.beat == 1 && after.tick == 0);
    DAWLAB_CHECK(e.getTransport().barsBeatsToSamples({6, 1, 0}) == map.sampleOfTick(5 * kBar, kSampleRate));
    DAWLAB_CHECK(e.getTransport().getTempo() == 90.0);
}

void testLoopInTicks() {
    // 999 BPM at 8 kHz: two ticks a sample. The loop's edges sit on shared samples; every pass plays both notes.
    Engine e;
    Logger* log = setUp(e, 8000.0);
    e.setTrackMidiClips(testId(0), {clipOf(1, 0, 4 * kBar, {{959, 1, 59, 100, 0}, {960, 1, 60, 100, 0}, {1920, 1, 62, 100, 0}})});
    Transport& tr = e.getTransport();
    tr.setTempo(999.0);
    tr.setLoopRangeTicks(960, 1921);
    tr.setLoopEnabled(true);
    tr.play();
    const TempoMap& map = tr.getTempoMap();
    const int64_t first = map.sampleOfTick(960, 8000.0), len = map.sampleOfTick(1921, 8000.0) - first;
    play(e, first + 7 * len, 0, nullptr, 5);
    // Each pass: tick 960 at its first sample, tick 1920 on its last (it shares a sample with the loop's end).
    int c59 = 0, c60 = 0, c62 = 0, misplaced = 0;
    for (const Logger::Logged& l : log->log) {
        if (l.message.kind() != MidiKind::NoteOn)
            continue;
        const int64_t pass = (l.sample - first) / len, into = (l.sample - first) % len;
        if (l.message.note() == 59)
            ++c59;
        else if (l.message.note() == 60)
            ++c60, misplaced += into == 0 ? 0 : 1;
        else
            ++c62, misplaced += into == len - 1 && pass >= 0 ? 0 : 1;
    }
    std::printf("  a loop of ticks 960-1920 where two ticks share a sample, 7 passes in random blocks: %d and %d note ons, %d misplaced;\n"
                "    the note at 959 before it once: %d\n",
                c60, c62, misplaced, c59);
    DAWLAB_CHECK(c60 == 7 && c62 == 7 && misplaced == 0 && c59 == 1);
    DAWLAB_CHECK(tr.isLoopInTicks() && tr.getLoopStart() == first && tr.getLoopEnd() == first + len);
    // Bars are ticks: they move with the tempo.
    tr.setLoopRangeBars(2, 1);
    DAWLAB_CHECK(tr.getLoopStart() == map.sampleOfTick(kBar, 8000.0));
    tr.setLoopRange(1000, 2000); // samples: as before
    DAWLAB_CHECK(!tr.isLoopInTicks() && tr.getLoopStart() == 1000);
}

void testNewMapWhileANoteSounds() {
    // A note of two bars from bar 1 at 120 BPM. At beat 3 the map becomes 60 BPM: the note keeps sounding and
    // ends at its tick under the new map.
    Engine e;
    Logger* log = setUp(e);
    e.setTrackMidiClips(testId(0), {clipOf(1, 0, 4 * kBar, {{0, 2 * kBar, 60, 100, 0}})});
    Transport& tr = e.getTransport();
    tr.play();
    const TempoMap before = tr.getTempoMap(), after = TempoMap::constant(60.0);
    int64_t changedAt = -1;
    play(e, 400000, 512, nullptr, 1, [&](int64_t done) {
        if (changedAt < 0 && done >= 48000) {
            changedAt = done;
            tr.setTempoMap(after);
        }
    });
    // The block that took the map started at changedAt, where the old map's playhead was; the new one is at the
    // same tick.
    const int64_t newPosition = std::llround(after.secondsAt(before.tickAtSample(static_cast<double>(changedAt), kSampleRate)) * kSampleRate);
    const int64_t wantOff = changedAt + after.sampleOfTick(2 * kBar, kSampleRate) - newPosition;
    int ons = 0, offs = 0;
    int64_t off = -1;
    for (const Logger::Logged& l : log->log) {
        ons += l.message.kind() == MidiKind::NoteOn ? 1 : 0;
        if (l.message.kind() == MidiKind::NoteOff) {
            ++offs;
            off = l.sample;
        }
    }
    std::printf("  a tempo change half way through a held note: %d note on, %d note off, at %lld (wanted %lld)\n", ons, offs,
                static_cast<long long>(off), static_cast<long long>(wantOff));
    DAWLAB_CHECK(ons == 1 && offs == 1 && off == wantOff);
    DAWLAB_CHECK(tr.getTempo() == 60.0);
}

void testInstrumentLatency() {
    // Track 0 a sampler that answers 1000 samples late, track 1 an instrument that answers at once; a note each at
    // tick 960 (sample 24000).
    for (const bool lowLatency : {false, true}) {
        Engine e;
        setUp(e, kSampleRate, 1000);
        e.getMixer().getTrack(1).setInstrument(true);
        e.setTrackInstrument(testId(1), std::make_unique<Logger>(0));
        const MidiClip clip = clipOf(1, 0, kBar, {{960, 480, 60, 100, 0}});
        e.setTrackMidiClips(testId(0), {clip});
        e.setTrackMidiClips(testId(1), {clipOf(2, 0, kBar, {{960, 480, 60, 100, 0}})});
        e.getMixer().setLatencyCompensationEnabled(true);
        e.getMixer().setLowLatencyMonitoring(lowLatency);
        e.getMixer().getTrack(0).setArmed(true);
        e.applyPendingCommandsOffline();
        e.getTransport().play();
        const AudioBuffer out = play(e, 48000, 512);
        std::vector<int> at;
        for (int i = 0; i < out.getNumSamples(); ++i)
            if (out.getSample(0, i) != 0.0f)
                at.push_back(i);
        if (!lowLatency) {
            DAWLAB_CHECK(e.getLatencySamples() == 1000 && at == std::vector<int>({25000}));
            std::printf("  a sampler 1000 samples late next to an instant synth: mix latency %d, both heard at %d\n", e.getLatencySamples(),
                        at.empty() ? -1 : at[0]);
        } else {
            DAWLAB_CHECK(e.getLatencySamples() == 0 && at == std::vector<int>({24000, 25000}));
            std::printf("  the sampler armed, low-latency monitoring: mix latency %d, the synth at %d, the sampler on its own at %d\n",
                        e.getLatencySamples(), at.empty() ? -1 : at[0], at.size() < 2 ? -1 : at[1]);
        }
    }
}

void testLowLatencyOnTheFasterTrack() {
    // The synth (no latency) armed, the sampler (1000) not: with low-latency monitoring the synth does not wait for
    // the sampler, which is still compensated against the rest.
    Engine e;
    setUp(e, kSampleRate, 1000);
    e.getMixer().getTrack(1).setInstrument(true);
    e.setTrackInstrument(testId(1), std::make_unique<Logger>(0));
    e.setTrackMidiClips(testId(0), {clipOf(1, 0, kBar, {{960, 480, 60, 100, 0}})});
    e.setTrackMidiClips(testId(1), {clipOf(2, 0, kBar, {{960, 480, 60, 100, 0}})});
    e.getMixer().setLatencyCompensationEnabled(true);
    e.getMixer().setLowLatencyMonitoring(true);
    e.getMixer().getTrack(1).setArmed(true);
    e.applyPendingCommandsOffline();
    e.getTransport().play();
    const AudioBuffer out = play(e, 48000, 512);
    std::vector<int> at;
    for (int i = 0; i < out.getNumSamples(); ++i)
        if (out.getSample(0, i) != 0.0f)
            at.push_back(i);
    std::printf("  the synth armed next to the sampler, low-latency monitoring: the synth at %d, the sampler at %d\n", at.empty() ? -1 : at[0],
                at.size() < 2 ? -1 : at[1]);
    DAWLAB_CHECK(e.getLatencySamples() == 1000 && at == std::vector<int>({24000, 25000}));
}

// An insert that writes down the tempo its play head says, block by block.
class TempoSpy final : public InsertEffect {
public:
    std::vector<double> bpms;
    TempoSpy() { bpms.reserve(4096); }
    const char* getName() const noexcept override { return "Spy"; }
    void prepare(double, int, int) override {}
    void reset() noexcept override {}
    void process(AudioBlock) noexcept override {}
    int getNumParameters() const noexcept override { return 0; }
    ParameterInfo getParameterInfo(int) const noexcept override { return {}; }
    void setParameter(int, float) noexcept override {}
    float getParameter(int) const noexcept override { return 0.0f; }
    void setPlayHead(const PlayHead& p) noexcept override {
        if (p.isPlaying() && bpms.size() < bpms.capacity())
            bpms.push_back(p.bpm);
    }
};

void testPlayHeadTempo() {
    // An insert sees the tempo where the block plays: through a ramp from 60 to 120 BPM, rising.
    Engine e;
    setUp(e);
    auto spy = std::make_unique<TempoSpy>();
    TempoSpy* observer = spy.get();
    e.addInsert(testTrack(0), -1, std::move(spy));
    e.getTransport().setTempoMap(mapOf({{0, 60.0, true}, {4 * kBar, 120.0, false}}));
    e.applyPendingCommandsOffline();
    e.getTransport().play();
    play(e, 48000 * 12, 512);
    const auto& b = observer->bpms;
    const bool rising = b.size() > 100 && b.front() == 60.0 && std::is_sorted(b.begin(), b.end()) && b.back() == 120.0;
    std::printf("  an insert's play head through a ramp from 60 to 120 BPM: %.1f at the start, %.1f at the end, never down\n",
                b.empty() ? 0.0 : b.front(), b.empty() ? 0.0 : b.back());
    DAWLAB_CHECK(rising);
}

void testSwitchedOnWhilePlaying() {
    // A track that becomes an instrument while the song plays chases its controllers at once.
    Engine e;
    Logger* log = setUp(e);
    e.getMixer().getTrack(0).setInstrument(false);
    MidiClip clip = clipOf(1, 0, 8 * kBar, {});
    clip.controllers = {{0, 64, 127, 0}};
    e.setTrackMidiClips(testId(0), {clip});
    e.applyPendingCommandsOffline();
    e.getTransport().play();
    play(e, 96000, 512);
    e.getMixer().getTrack(0).setInstrument(true);
    play(e, 512, 512);
    const bool chased = !log->log.empty() && log->log[0].message.kind() == MidiKind::Controller && log->log[0].message.note() == 64;
    DAWLAB_CHECK(chased);
}

void testSongEndFollowsTheMap() {
    // 120 BPM, then 60 from bar 2: a clip over bar 2 ends at 96000 + 192000 samples. The song's end and a bounce of
    // the default length go through the map.
    Engine e;
    setUp(e);
    e.setTrackMidiClips(testId(0), {clipOf(1, kBar, kBar, {{0, 480, 60, 100, 0}})});
    e.getTransport().setTempoMap(mapOf({{0, 120.0, false}, {kBar, 60.0, false}}));
    RenderOptions options; // numSamples 0: up to the song's end
    const AudioBuffer out = renderOffline(e, options);
    std::printf("  a clip over bar 2 at 60 BPM after a bar at 120: the song ends at %lld, a bounce has %d samples\n",
                static_cast<long long>(e.getSongEndSample()), out.getNumSamples());
    DAWLAB_CHECK(e.getSongEndSample() == 288000 && e.getSongEndSample() == e.getTransport().tickToSample(2 * kBar));
    DAWLAB_CHECK(out.getNumSamples() == 288000);
}

void testNoAllocation() {
    Engine e;
    setUp(e);
    std::vector<Note> notes;
    for (int64_t t = 0; t < 16 * kBar; t += 240)
        notes.push_back({t, 200, 48 + static_cast<int>(t / 240 % 24), 100, 0});
    e.setTrackMidiClips(testId(0), {clipOf(1, 0, 16 * kBar, notes)});
    e.getTransport().setLoopRangeBars(2, 2);
    e.getTransport().setLoopEnabled(true);
    e.getTransport().play();
    long allocs = 0;
    int changes = 0;
    play(e, 600000, 0, &allocs, 9, [&](int64_t done) {
        if (done / 20000 > changes) {
            ++changes;
            e.getTransport().setTempoMap(changes % 2 == 0 ? song() : TempoMap::constant(60.0 + changes));
        }
    });
    std::printf("  %d new maps while a loop plays: %ld allocations on the audio thread\n", changes, allocs);
    DAWLAB_CHECK(allocs == 0 && changes > 20);
}

void testMapIsMadeWithoutAllocation() {
    // Making a map, or setting it to one tempo, writes into the map itself: nothing on the heap. The header says so,
    // and Mixer's fallback map and Transport build theirs this way.
    double nan = std::numeric_limits<double>::quiet_NaN();
    long allocs = 0;
    {
        CountScope count;
        TempoMap m;
        m.setConstant(90.0);
        const TempoMap c = TempoMap::constant(100.0);
        DAWLAB_CHECK(m.size() == 1 && m.point(0).bpm == 90.0);
        DAWLAB_CHECK(c.size() == 1 && c.point(0).bpm == 100.0);
        // The bounds as before: NaN is refused (the map stays), out of range clamps, infinity too.
        m.setConstant(nan);
        DAWLAB_CHECK(m.size() == 1 && m.point(0).bpm == 90.0);
        m.setConstant(5.0);
        DAWLAB_CHECK(m.point(0).bpm == TempoMap::kMinBpm);
        m.setConstant(5000.0);
        DAWLAB_CHECK(m.point(0).bpm == TempoMap::kMaxBpm);
        m.setConstant(-std::numeric_limits<double>::infinity());
        DAWLAB_CHECK(m.point(0).bpm == TempoMap::kMinBpm);
        m.setConstant(std::numeric_limits<double>::infinity());
        DAWLAB_CHECK(m.point(0).bpm == TempoMap::kMaxBpm);
        allocs = gAllocs.exchange(0);
    }
    std::printf("  a map made, set to one tempo and made with constant(): %ld allocations\n", allocs);
    DAWLAB_CHECK(allocs == 0);
}

void testThreads() {
    // The UI thread publishes maps whose points all have one tempo, a different one each time; the audio thread
    // must only ever see whole maps.
    Engine e;
    setUp(e);
    e.getTransport().play();
    std::atomic<bool> done{false};
    std::atomic<int> torn{0}, seen{0};
    std::thread audio([&] {
        AudioBuffer block(kNumChannels, kBlockSize);
        while (!done.load()) {
            e.processBlock(block.toBlock());
            const TempoMap& m = e.getTransport().getAudioTempoMap();
            const double bpm = m.point(0).bpm;
            for (int i = 1; i < m.size(); ++i)
                if (m.point(i).bpm != bpm)
                    torn.fetch_add(1);
            seen.fetch_add(1);
        }
    });
    int maps = 0;
    for (; maps < 2000 || seen.load() < 2000; ++maps) {
        std::vector<TempoPoint> points;
        const int n = 1 + maps % 200;
        for (int i = 0; i < n; ++i)
            points.push_back({i * 960LL, 60.0 + maps % 300, false});
        e.getTransport().setTempoMap(mapOf(points));
    }
    done = true;
    audio.join();
    std::printf("  %d maps of up to 200 points from the UI thread while the audio thread plays %d blocks: %d torn\n", maps, seen.load(),
                torn.load());
    DAWLAB_CHECK(torn.load() == 0);
}

void testLoopUnitArrivesWhole() {
    // The UI thread switches between a loop of 3000 samples and one of 2000 ticks (50000 samples at 120 BPM) while
    // another thread plays blocks of 4000 from 0. A wrap may only go from 3000 or 50000 to 0; ticks read as samples,
    // or samples as ticks, would wrap at 2000 or 75000. 20000 switches, or 10 s on a slow machine.
    Transport t;
    t.prepare(kSampleRate);
    t.setLoopRange(0, 3000);
    t.setLoopEnabled(true);
    t.play();
    std::atomic<bool> quit{false};
    std::atomic<long> calls{0};
    long legal = 0, torn = 0; // the audio thread's, read after join()
    std::thread audio([&] {
        TimelineSegment seg[Transport::kMaxSegmentsPerBlock];
        while (!quit.load(std::memory_order_relaxed)) {
            int covered = 0;
            const int n = t.advance(4000, seg, covered);
            for (int s = 0; s + 1 < n; ++s) {
                const int64_t end = seg[s].timelineStart + seg[s].numSamples, next = seg[s + 1].timelineStart;
                if (end != next)
                    ++((end == 3000 || end == 50000) && next == 0 ? legal : torn);
            }
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
        t.setLoopRange(0, 3000);
        t.setPosition(0);
        waitForBlocks(2);
        for (unsigned spin = rng() % 64; spin > 0; --spin) // a different moment each time
            sink = sink + 1;
        t.setLoopRangeTicks(0, 2000);
        waitForBlocks(1);
    }
    quit = true;
    audio.join();
    std::printf("  %d switches between a loop in samples and one in ticks while another thread plays: %ld wraps, %ld torn\n",
                switches, legal, torn);
    DAWLAB_CHECK(torn == 0 && legal > 0);
}

} // namespace

int main() {
    std::printf("l19_test_tempo: a tempo map and instrument latency in the engine\n");
    testNotesFollowTheMap();
    testLoopInTicks();
    testNewMapWhileANoteSounds();
    testInstrumentLatency();
    testLowLatencyOnTheFasterTrack();
    testPlayHeadTempo();
    testSwitchedOnWhilePlaying();
    testSongEndFollowsTheMap();
    testNoAllocation();
    testMapIsMadeWithoutAllocation();
    testThreads();
    testLoopUnitArrivesWhole();
    return dawlab::test::summary("l19_test_tempo");
}
