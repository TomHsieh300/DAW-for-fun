// test_midi: instrument tracks in the real engine (chapter 18, Step 7).
//
//   1. A note starts on the sample of its tick, through the real transport and mixer, in any block size, and a
//      whole song with a loop is the same in any block size.
//   2. Nothing hangs: after a stop, a seek, turning the instrument off, removing the track - silence.
//   2b. Nor with the sustain pedal down: a clip's pedal is let go when the clip stops sounding, and a key or
//       pedal held on the keyboard is let go on a track disarmed meanwhile.
//   3. Live MIDI reaches the armed instrument tracks only, at its offset, also when processBlock() is given more
//      than one chunk.
//   4. Removing a track that has everything hands back exactly kMaxGarbagePerCommand objects.
//   4b. Another instrument in the synth's place gets the track's events; the synth's settings change while it
//       plays (the hook added after the chapter for exercises 8 and 12). An instrument still queued when the
//       device changes is prepared for the new device.
//   4c. The song's end counts the MIDI clips, so a bounce of the default length plays a song of notes only.
//   5. The audio thread never allocates, with clips, loops, seeks and live MIDI (this file replaces operator new).
//   6. Three threads at once - UI, audio, MIDI - for ThreadSanitizer.
//   7. The synth by hand: stealing, the pedal, all notes off, the bend.
//   8. Raw MIDI clip and instrument commands: what comes back as garbage.

#include "TestHelpers.h"
#include "TestTracks.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <random>
#include <thread>

// ---- allocation counting (as in test_realtime_safety.cpp) --------------------------------------------------
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
void* countedAlignedAlloc(std::size_t n, std::size_t align) {
    if (tCounting)
        gAllocs.fetch_add(1, std::memory_order_relaxed);
#if defined(_MSC_VER)
    void* p = _aligned_malloc(n == 0 ? 1 : n, align);
#else
    void* p = std::aligned_alloc(align, ((n == 0 ? 1 : n) + align - 1) / align * align);
#endif
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}
void countedAlignedFree(void* p) noexcept {
    if (p != nullptr && tCounting)
        gAllocs.fetch_add(1, std::memory_order_relaxed);
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
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
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try {
        return countedAlloc(n);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    try {
        return countedAlloc(n);
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void* p) noexcept { countedFree(p); }
void operator delete[](void* p) noexcept { countedFree(p); }
void operator delete(void* p, std::size_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { countedFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { countedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { countedFree(p); }
void* operator new(std::size_t n, std::align_val_t a) { return countedAlignedAlloc(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return countedAlignedAlloc(n, static_cast<std::size_t>(a)); }
void operator delete(void* p, std::align_val_t) noexcept { countedAlignedFree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { countedAlignedFree(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { countedAlignedFree(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { countedAlignedFree(p); }

using namespace l15test;
using namespace dawlab::engine::test;

namespace {

constexpr int64_t kBar = 4 * kTicksPerQuarter; // 3840 ticks, 96000 samples at 120 BPM

void setUp(Engine& e, int maxBlock = kBlockSize) {
    e.prepare(kSampleRate, maxBlock);
    addTestTracks(e);
    e.getMixer().getTrack(0).setInstrument(true);
}

MidiClip clipOf(int id, int64_t start, int64_t length, std::vector<Note> notes) {
    MidiClip c;
    c.id = id;
    c.start = start;
    c.length = length;
    c.notes = std::move(notes);
    return c;
}

// Plays from the transport's position: `numSamples` in blocks of `block` (0: random 1..1200), the device's way.
// The output, and the live events to hand each block. Counts allocations on this thread while processing.
AudioBuffer play(Engine& e, int64_t numSamples, int block, long* allocs = nullptr, std::mt19937* rng = nullptr,
                 const std::vector<std::pair<int64_t, MidiMessage>>* live = nullptr) {
    AudioBuffer out(kNumChannels, static_cast<int>(numSamples));
    std::vector<BlockEvent> ev;
    ev.reserve(256);
    std::uniform_int_distribution<int> size(1, 1200);
    size_t next = 0;
    for (int64_t done = 0; done < numSamples;) {
        const int n = static_cast<int>(std::min<int64_t>(block > 0 ? block : size(*rng), numSamples - done));
        ev.clear();
        while (live != nullptr && next < live->size() && (*live)[next].first < done + n) {
            ev.push_back({static_cast<int>((*live)[next].first - done), (*live)[next].second});
            ++next;
        }
        {
            const long before = gAllocs.load();
            CountScope count;
            e.processBlock(out.toBlock().getSubBlock(static_cast<int>(done), n), {}, MidiBlock{ev.data(), static_cast<int>(ev.size())});
            if (allocs != nullptr)
                *allocs += gAllocs.load() - before;
        }
        done += n;
    }
    return out;
}

int firstNonZero(const AudioBuffer& b, int from = 0) {
    for (int i = from; i < b.getNumSamples(); ++i)
        if (b.getSample(0, i) != 0.0f || b.getSample(1, i) != 0.0f)
            return i;
    return -1;
}

bool silentFrom(const AudioBuffer& b, int from) { return firstNonZero(b, from) < 0; }

void testNoteOnItsSample() {
    // One note at beat 2 (tick 960 = sample 24000). The envelope starts at 0, so the first sound is one sample
    // later: 24001, whatever the block size.
    int wrong = 0;
    for (const int block : {512, 64, 100, 1, 0}) {
        Engine e;
        setUp(e);
        e.setTrackMidiClips(testId(0), {clipOf(1, 0, kBar, {{960, 480, 60, 100, 0}})});
        e.applyPendingCommandsOffline();
        e.getTransport().setPosition(0);
        e.getTransport().play();
        std::mt19937 rng(7);
        const AudioBuffer out = play(e, 48000, block, nullptr, &rng);
        wrong += firstNonZero(out) == 24001 ? 0 : 1;
    }
    std::printf("  a note at tick 960 sounds from sample 24001 in blocks of 512, 64, 100, 1 and random: %s\n", wrong == 0 ? "yes" : "no");
    DAWLAB_CHECK(wrong == 0);
}

std::vector<MidiClip> song() {
    std::vector<MidiClip> clips;
    std::vector<Note> chords;
    for (int bar = 0; bar < 4; ++bar)
        for (const int p : {48, 55, 60, 64})
            chords.push_back({bar * kBar, kBar - 240, p + bar * 2, 90, 0});
    std::stable_sort(chords.begin(), chords.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    clips.push_back(clipOf(1, 0, 4 * kBar, chords));
    std::vector<Note> melody;
    for (int i = 0; i < 32; ++i)
        melody.push_back({i * 480, i % 3 == 0 ? 960 : 400, 72 + (i * 5) % 12, 100, 0}); // overlapping, one pitch again
    MidiClip m = clipOf(2, kBar / 2, 3 * kBar, melody); // a window starting half a bar in
    m.offset = 240;
    m.controllers = {{0, 1, 10, 0}, {3000, 1, 90, 0}};
    clips.push_back(m);
    return clips;
}

void testBlockInvariance() {
    // The song, with a loop of bars 2-3 played three times: the same in any block size.
    auto run = [](int block) {
        Engine e;
        setUp(e);
        e.setTrackMidiClips(testId(0), song());
        e.applyPendingCommandsOffline();
        Transport& tr = e.getTransport();
        tr.setLoopRange(96000, 3 * 96000);
        tr.setLoopEnabled(true);
        tr.setPosition(48000);
        tr.play();
        std::mt19937 rng(77);
        return play(e, 7 * 96000, block, nullptr, &rng);
    };
    const AudioBuffer ref = run(512);
    int differ = 0;
    for (const int block : {37, 1000, 0})
        differ += maxAbsDiff(run(block), ref) == 0.0f ? 0 : 1;
    float peak = 0.0f;
    for (int i = 0; i < ref.getNumSamples(); ++i)
        peak = std::max(peak, std::abs(ref.getSample(0, i)));
    std::printf("  a song with a loop, 7 bars: the same in blocks of 512, 37, 1000 and random: %s (peak %.2f)\n",
                differ == 0 ? "yes" : "no", static_cast<double>(peak));
    DAWLAB_CHECK(differ == 0 && peak > 0.05f && peak < 1.0f);
}

void testNothingHangs() {
    Engine e;
    setUp(e);
    const std::vector<MidiClip> chords{song()[0]}; // a chord a bar: nothing new starts within a bar of a seek
    e.setTrackMidiClips(testId(0), chords);
    e.applyPendingCommandsOffline();
    Transport& tr = e.getTransport();
    const Track& t = e.getMixer().getTrack(0);
    tr.play();
    play(e, 30000, 512);
    DAWLAB_CHECK(t.getSynth()->activeVoices() >= 4);
    // A stop: everything is released, and after the release (0.3 s) there is silence.
    tr.stop();
    const AudioBuffer afterStop = play(e, 24000, 512);
    DAWLAB_CHECK(silentFrom(afterStop, 15000) && t.getSynth()->activeVoices() == 0);

    // A seek into the middle of a chord: the old one ends, and the one there is not started half-way.
    tr.setPosition(0);
    tr.play();
    play(e, 30000, 512);
    tr.setPosition(200000); // bar 3 (from 192000), past its first beat; bar 4 starts at 288000
    const AudioBuffer afterSeek = play(e, 40000, 512);
    DAWLAB_CHECK(t.getClipPlayer()->soundingCount() == 0 && silentFrom(afterSeek, 20000));

    // The instrument switched off while it sounds: silent at once. Back on: nothing old comes back.
    tr.setPosition(0);
    play(e, 30000, 512);
    e.getMixer().getTrack(0).setInstrument(false);
    const AudioBuffer off = play(e, 512, 512);
    DAWLAB_CHECK(silentFrom(off, 0) && t.getSynth()->activeVoices() == 0);
    tr.stop();
    play(e, 512, 512);
    e.getMixer().getTrack(0).setInstrument(true);
    const AudioBuffer back = play(e, 4800, 512);
    DAWLAB_CHECK(silentFrom(back, 0));

    // The clips replaced by nothing while they sound: the notes end.
    tr.setPosition(0);
    tr.play();
    play(e, 30000, 512);
    e.setTrackMidiClips(testId(0), {});
    play(e, 512, 512);
    DAWLAB_CHECK(t.getClipPlayer()->soundingCount() == 0);
    const AudioBuffer gone = play(e, 24000, 512);
    DAWLAB_CHECK(silentFrom(gone, 15000));

    // The track removed while it sounds: silent from the next block, and a new track in its slot starts clean.
    e.setTrackMidiClips(testId(0), chords);
    tr.setPosition(0);
    play(e, 30000, 512);
    DAWLAB_CHECK(t.getSynth()->activeVoices() >= 4);
    e.removeTrack(testId(0));
    const AudioBuffer removed = play(e, 512, 512);
    DAWLAB_CHECK(silentFrom(removed, 0) && t.getSynth()->activeVoices() == 0);
    e.collectGarbage();
    DAWLAB_CHECK(e.getGarbageLeaked() == 0);
    const ChannelRef again = e.addTrack(TrackId{100}, TrackFormat::Mono, "New");
    e.applyPendingCommandsOffline();
    DAWLAB_CHECK(again.index == 0 && !e.getMixer().getTrack(0).isInstrument() && e.getMixer().getTrack(0).getMidiClips() == nullptr);

    // The UI reuses a slot at once: an instrument track removed and a new instrument track added in its slot
    // before the audio thread has seen either. The RemoveTrack that arrives first must not undo what the UI set
    // up for the new track (undo / redo, opening a project).
    e.getMixer().getTrack(0).setInstrument(true);
    e.removeTrack(TrackId{100});
    const ChannelRef reused = e.addTrack(TrackId{101}, TrackFormat::Mono, "Again");
    e.getMixer().getTrack(reused.index).setInstrument(true);
    e.setTrackMidiClips(TrackId{101}, {clipOf(9, 0, kBar, {{0, 960, 60, 100, 0}})});
    tr.setPosition(0);
    tr.play();
    play(e, 4800, 512); // RemoveTrack, AddTrack and the clips arrive in this block, in that order
    DAWLAB_CHECK(reused.index == 0 && e.getMixer().getTrack(0).isInstrument() && t.getSynth()->activeVoices() == 1);
}

// A clip that presses the sustain pedal (CC 64 = 127 at its start) and plays a chord, `length` ticks long.
MidiClip pedalClip(int id, int64_t start, int64_t length) {
    MidiClip c = clipOf(id, start, length, {{0, 960, 60, 100, 0}, {0, 960, 64, 100, 0}, {0, 960, 67, 100, 0}});
    c.controllers = {{0, 64, 127, 0}};
    return c;
}

// Each case is silent 0.3 s (the release) after the clip stops sounding, or after the key or pedal is let go.
void testPedalLetGo() {
    // The engines here are on the heap: see testMoreCases.
    {
        auto e = std::make_unique<Engine>(); // a stop
        setUp(*e);
        e->setTrackMidiClips(testId(0), {pedalClip(1, 0, kBar)});
        e->applyPendingCommandsOffline();
        e->getTransport().play();
        play(*e, 30000, 512);
        DAWLAB_CHECK(e->getMixer().getTrack(0).getSynth()->activeVoices() == 3); // held by the pedal
        e->getTransport().stop();
        const AudioBuffer after = play(*e, 24000, 512);
        DAWLAB_CHECK(silentFrom(after, 15000) && e->getMixer().getTrack(0).getSynth()->activeVoices() == 0);
    }
    {
        auto e = std::make_unique<Engine>(); // a seek to before the clip, playing on
        setUp(*e);
        e->setTrackMidiClips(testId(0), {pedalClip(1, 2 * kBar, kBar)});
        e->applyPendingCommandsOffline();
        Transport& tr = e->getTransport();
        tr.setPosition(2 * 96000); // where the clip starts
        tr.play();
        play(*e, 30000, 512);
        tr.setPosition(0);
        const AudioBuffer after = play(*e, 24000, 512);
        DAWLAB_CHECK(silentFrom(after, 15000) && e->getMixer().getTrack(0).getSynth()->activeVoices() == 0);
    }
    {
        auto e = std::make_unique<Engine>(); // the clip ends with the pedal down, the song plays on
        setUp(*e);
        e->setTrackMidiClips(testId(0), {pedalClip(1, 0, kBar)});
        e->applyPendingCommandsOffline();
        e->getTransport().play();
        const AudioBuffer song = play(*e, 96000 + 24000, 512); // a bar is 96000 samples
        DAWLAB_CHECK(silentFrom(song, 96000 + 15000) && e->getMixer().getTrack(0).getSynth()->activeVoices() == 0);
        // the clips replaced by nothing while the pedal is down
        e->getTransport().setPosition(0);
        play(*e, 30000, 512);
        e->setTrackMidiClips(testId(0), {});
        const AudioBuffer gone = play(*e, 24000, 512);
        DAWLAB_CHECK(silentFrom(gone, 15000) && e->getMixer().getTrack(0).getSynth()->activeVoices() == 0);
    }
    {
        auto e = std::make_unique<Engine>(); // live: a key held, the track disarmed, then the key let go
        setUp(*e);
        Track& inst = e->getMixer().getTrack(0);
        inst.setArmed(true);
        std::vector<std::pair<int64_t, MidiMessage>> down{{0, MidiMessage::noteOn(0, 60, 100)}};
        play(*e, 512, 512, nullptr, nullptr, &down);
        inst.setArmed(false);
        std::vector<std::pair<int64_t, MidiMessage>> up{{0, MidiMessage::noteOff(0, 60)}};
        play(*e, 512, 512, nullptr, nullptr, &up);
        DAWLAB_CHECK(silentFrom(play(*e, 24000, 512), 15000) && inst.getSynth()->activeVoices() == 0);
        // the same with the pedal: pressed while armed, let go after
        inst.setArmed(true);
        std::vector<std::pair<int64_t, MidiMessage>> pedalAndKey{{0, MidiMessage::controller(0, 64, 127)},
                                                                 {10, MidiMessage::noteOn(0, 62, 100)},
                                                                 {20, MidiMessage::noteOff(0, 62)}};
        play(*e, 512, 512, nullptr, nullptr, &pedalAndKey);
        inst.setArmed(false);
        std::vector<std::pair<int64_t, MidiMessage>> pedalUp{{0, MidiMessage::controller(0, 64, 0)}};
        play(*e, 512, 512, nullptr, nullptr, &pedalUp);
        DAWLAB_CHECK(silentFrom(play(*e, 24000, 512), 15000) && inst.getSynth()->activeVoices() == 0);
        // a disarmed track still ignores what the keyboard did not start on it: a clip's note plays on
        e->setTrackMidiClips(testId(0), {clipOf(1, 0, kBar, {{0, 3840, 60, 100, 0}})});
        e->applyPendingCommandsOffline();
        e->getTransport().setPosition(0);
        e->getTransport().play();
        play(*e, 4800, 512);
        play(*e, 512, 512, nullptr, nullptr, &up);
        play(*e, 24000, 512);
        DAWLAB_CHECK(inst.getSynth()->activeVoices() == 1);
    }
}

// What the cases above do not reach.
void testMoreCases() {
    // The engines here are on the heap: an Engine holds all its track slots (some 190 KB), and a Debug build of
    // MSVC gives each block below its own room on the stack, so a few of them overflowed Windows' 1 MB stack.

    // A seek BACK into a note: it ends at the jump (its end is far ahead), and it is not started again.
    {
        const auto engine = std::make_unique<Engine>();
        Engine& e = *engine;
        setUp(e);
        e.setTrackMidiClips(testId(0), {clipOf(1, 0, 2 * kBar, {{0, 2 * kBar, 60, 100, 0}})});
        e.applyPendingCommandsOffline();
        e.getTransport().play();
        play(e, 30000, 512);
        e.getTransport().setPosition(10000);
        const AudioBuffer back = play(e, 30000, 512);
        DAWLAB_CHECK(silentFrom(back, 15000));
    }
    // Controllers are chased: the sustain pedal, pressed at the start of the clip, still holds after a seek past it,
    // so a short note at bar 2 is still heard half a second after it ended.
    {
        const auto engine = std::make_unique<Engine>();
        Engine& e = *engine;
        setUp(e);
        MidiClip c = clipOf(1, 0, 4 * kBar, {{kBar, 100, 60, 100, 0}}); // 100 ticks: 2500 samples
        c.controllers = {{0, Synth::kSustainPedal, 127, 0}};
        e.setTrackMidiClips(testId(0), {c});
        e.applyPendingCommandsOffline();
        e.getTransport().setPosition(48000);
        e.getTransport().play();
        const AudioBuffer out = play(e, 48000 + 2500 + 24000 + 512, 512);
        const int at = 48000 + 2500 + 24000; // the note ended 0.5 s ago; its release would be over
        DAWLAB_CHECK(out.getSample(0, at) != 0.0f);
    }
    // An instrument track's audio clips still play: the synth adds to them.
    {
        const auto engine = std::make_unique<Engine>();
        Engine& e = *engine;
        setUp(e);
        e.setTrackClips(testId(0), {Clip::fromBuffer(makeConstant(0.1f, 48000), 0)});
        e.setTrackMidiClips(testId(0), {clipOf(1, 0, kBar, {{960, 100, 60, 100, 0}})});
        e.applyPendingCommandsOffline();
        e.getTransport().play();
        const AudioBuffer out = play(e, 30000, 512);
        DAWLAB_CHECK(out.getSample(0, 1000) != 0.0f && out.getSample(0, 24100) != out.getSample(0, 1000));
    }
    // At another tempo: 60 BPM, tick 960 is sample 48000.
    {
        const auto engine = std::make_unique<Engine>();
        Engine& e = *engine;
        setUp(e);
        e.getTransport().setTempo(60.0);
        e.setTrackMidiClips(testId(0), {clipOf(1, 0, kBar, {{960, 480, 60, 100, 0}})});
        e.applyPendingCommandsOffline();
        e.getTransport().play();
        DAWLAB_CHECK(firstNonZero(play(e, 60000, 512)) == 48001);
    }
    // A reset (before a bounce) silences the synth at once.
    {
        const auto engine = std::make_unique<Engine>();
        Engine& e = *engine;
        setUp(e);
        e.setTrackMidiClips(testId(0), {clipOf(1, 0, kBar, {{0, kBar, 60, 100, 0}})});
        e.applyPendingCommandsOffline();
        e.getTransport().play();
        play(e, 4800, 512);
        e.getTransport().stop();
        e.requestReset();
        play(e, 512, 512);
        DAWLAB_CHECK(e.getMixer().getTrack(0).getSynth()->activeVoices() == 0);
    }
}

void testLiveMidi() {
    Engine e;
    setUp(e);
    Track& inst = e.getMixer().getTrack(0);
    e.getTransport().stop(); // live MIDI plays with the transport stopped too
    // Not armed: nothing.
    std::vector<std::pair<int64_t, MidiMessage>> live{{100, MidiMessage::noteOn(0, 60, 100)}};
    DAWLAB_CHECK(silentFrom(play(e, 512, 512, nullptr, nullptr, &live), 0));
    // Armed: from the sample after the event's offset.
    inst.setArmed(true);
    AudioBuffer out = play(e, 512, 512, nullptr, nullptr, &live);
    DAWLAB_CHECK(firstNonZero(out) == 101);
    std::vector<std::pair<int64_t, MidiMessage>> up{{0, MidiMessage::noteOff(0, 60)}};
    play(e, 512, 512, nullptr, nullptr, &up);
    play(e, 24000, 512);
    DAWLAB_CHECK(inst.getSynth()->activeVoices() == 0);
    // An armed instrument track records MIDI, not audio: only the armed audio track starts a Recorder.
    e.getMixer().getTrack(1).setArmed(true);
    DAWLAB_CHECK(e.startRecording(Latencies{}) == 1 && !e.getMixer().getTrack(1).getRecorder().isIdle() && inst.getRecorder().isIdle());
    e.stopRecording();
    play(e, 512, 512);
    e.getMixer().getTrack(1).setArmed(false);
    // An armed audio track does not play MIDI.
    inst.setArmed(false);
    e.getMixer().getTrack(1).setArmed(true);
    DAWLAB_CHECK(silentFrom(play(e, 512, 512, nullptr, nullptr, &live), 0));
    e.getMixer().getTrack(1).setArmed(false);

    // processBlock() with 2048 samples on an engine prepared for 512: four chunks, each with its own events.
    inst.setArmed(true);
    std::vector<std::pair<int64_t, MidiMessage>> late{{1500, MidiMessage::noteOn(0, 64, 100)}};
    out = play(e, 2048, 2048, nullptr, nullptr, &late);
    DAWLAB_CHECK(firstNonZero(out) == 1501);
    // Live and clip notes together, in one block, at their offsets.
    e.setTrackMidiClips(testId(0), {clipOf(1, 0, kBar, {{4, 100, 67, 100, 0}})}); // tick 4 = sample 100
    e.applyPendingCommandsOffline();
    e.getTransport().setPosition(0);
    e.getTransport().play();
    std::vector<std::pair<int64_t, MidiMessage>> both{{50, MidiMessage::noteOn(0, 72, 100)}};
    play(e, 512, 512, nullptr, nullptr, &both);
    DAWLAB_CHECK(inst.getSynth()->activeVoices() == 3); // 64 still held, 72 live, 67 from the clip
}

void testGarbageOfAFullTrack() {
    // Everything a track can own: all its inserts, audio clips, MIDI clips and the curves of its fader and pan.
    Engine e(1);
    setUp(e);
    AudioBuffer block(kNumChannels, 512);
    for (int i = 0; i < kMaxInserts; ++i) {
        e.addInsert(testTrack(0), -1, createEffect(EffectType::Gain));
        e.processBlock(block.toBlock());
    }
    e.setTrackClips(testId(0), {Clip::fromBuffer(makeConstant(0.1f, 1000), 0)});
    e.processBlock(block.toBlock());
    e.setTrackMidiClips(testId(0), {clipOf(1, 0, kBar, {{0, 100, 60, 100, 0}})});
    e.processBlock(block.toBlock());
    AutomationCurve curve;
    curve.add({0, 0.5f, Shape::Linear});
    e.setLaneCurve(LaneTarget{testTrack(0), EffectId{}, kLaneGain}, curve);
    e.processBlock(block.toBlock());
    e.setLaneCurve(LaneTarget{testTrack(0), EffectId{}, kLanePan}, curve);
    e.processBlock(block.toBlock());
    e.setTrackInstrument(testId(0), std::make_unique<SynthInstrument>());
    e.processBlock(block.toBlock());
    e.collectGarbage();
    e.removeTrack(testId(0));
    e.processBlock(block.toBlock());
    DAWLAB_CHECK(e.getGarbageLeaked() == 0);
    const int handedBack = static_cast<int>(e.collectGarbage());
    std::printf("  removing a track with everything hands back %d objects (kMaxGarbagePerCommand = %d)\n", handedBack, kMaxGarbagePerCommand);
    DAWLAB_CHECK(handedBack == kMaxGarbagePerCommand);
}

// An instrument that writes down what it gets and adds 0.5 from each note on to the block's end.
class Recorder final : public Instrument {
public:
    explicit Recorder(std::vector<std::pair<int64_t, MidiMessage>>& log) : log_(log) {}
    void prepare(double sampleRate, int maxBlockSize) override {
        prepared_ = true;
        rate_ = sampleRate;
        maxBlock_ = maxBlockSize;
    }
    void reset() noexcept override { ++resets_; }
    void render(const BlockEvent* events, int numEvents, float* const* out, int numChannels, int numSamples) noexcept override {
        largestBlock_ = std::max(largestBlock_, numSamples);
        for (int k = 0; k < numEvents; ++k) {
            if (log_.size() < log_.capacity())
                log_.push_back({rendered_ + events[k].offset, events[k].message});
            if (events[k].message.kind() == MidiKind::NoteOn)
                for (int ch = 0; ch < numChannels; ++ch)
                    for (int i = events[k].offset; i < numSamples; ++i)
                        out[ch][i] += 0.5f;
        }
        rendered_ += numSamples;
    }
    bool prepared_ = false;
    double rate_ = 0.0;
    int maxBlock_ = 0;
    int largestBlock_ = 0; // the most samples one render() was given
    int resets_ = 0;

private:
    std::vector<std::pair<int64_t, MidiMessage>>& log_;
    int64_t rendered_ = 0;
};

void testAnotherInstrument() {
    Engine e;
    setUp(e);
    e.setTrackMidiClips(testId(0), {clipOf(1, 0, kBar, {{960, 480, 60, 100, 0}, {1920, 480, 64, 90, 0}})});
    std::vector<std::pair<int64_t, MidiMessage>> log;
    log.reserve(64);
    auto recorder = std::make_unique<Recorder>(log);
    Recorder* observer = recorder.get();
    DAWLAB_CHECK(e.setTrackInstrument(testId(0), std::move(recorder)) == observer && observer->prepared_);
    DAWLAB_CHECK(e.setTrackInstrument(TrackId{999}, std::make_unique<SynthInstrument>()) == nullptr); // no such track
    e.getTransport().play();
    const AudioBuffer out = play(e, kBar * 25, 512);
    // Its events at their samples (25 a tick), its sound in the track, and nothing of the synth.
    const bool events = log.size() == 4 && log[0].first == 24000 && log[0].second.note() == 60 && log[1].first == 36000 &&
                        log[2].first == 48000 && log[2].second.note() == 64 && log[3].first == 60000;
    DAWLAB_CHECK(events && e.getMixer().getTrack(0).getReplacementInstrument() == observer);
    DAWLAB_CHECK(out.getSample(0, 23999) == 0.0f && out.getSample(0, 24000) > 0.1f && e.getMixer().getTrack(0).getSynth()->activeVoices() == 0);
    std::printf("  another instrument in the synth's place: %zu events at their samples, its sound on the track\n", log.size());

    // Back to the synth: the old one comes back as garbage, and the synth plays the clip again.
    e.getTransport().setPosition(0);
    DAWLAB_CHECK(e.setTrackInstrument(testId(0), nullptr) == nullptr);
    play(e, 512, 512);
    DAWLAB_CHECK(e.collectGarbage() == 1 && e.getMixer().getTrack(0).getReplacementInstrument() == nullptr);
    play(e, 30000, 512);
    DAWLAB_CHECK(e.getMixer().getTrack(0).getSynth()->activeVoices() == 1);

    // The synth's settings change while a note sounds: level 0 silences it at the next block, without a reset.
    SynthInstrument* synth = e.getTrackSynth(testId(0));
    DAWLAB_CHECK(synth != nullptr && synth->getSettings().level == VoiceSettings{}.level);
    VoiceSettings quiet;
    quiet.level = 0.0f;
    synth->setSettings(quiet);
    const AudioBuffer after = play(e, 1024, 512);
    DAWLAB_CHECK(silentFrom(after, 0) &&e.getMixer().getTrack(0).getSynth()->activeVoices() == 1);
    // A new track in the slot starts with the synth's own sound.
    e.removeTrack(testId(0));
    play(e, 512, 512);
    e.collectGarbage();
    const ChannelRef again = e.addTrack(TrackId{77});
    DAWLAB_CHECK(again.index == 0 && e.getTrackSynth(TrackId{77})->getSettings().level == VoiceSettings{}.level);
    std::printf("  the synth again, its settings changed while a note sounds, a new track with the default sound\n");
}

void testQueuedThroughADeviceChange() {
    // An instrument and an insert are still in the queue when the device changes to 96 kHz and blocks of 1024:
    // Engine::prepare() applies the queue first and then prepares everything, so both play at the new settings.
    Engine e;
    setUp(e);
    std::vector<std::pair<int64_t, MidiMessage>> log;
    auto recorder = std::make_unique<Recorder>(log);
    Recorder* observer = recorder.get();
    DAWLAB_CHECK(e.setTrackInstrument(testId(0), std::move(recorder)) == observer);
    DAWLAB_CHECK(e.addInsert(testTrack(0), -1, std::make_unique<LimiterEffect>()) != nullptr);
    e.prepare(96000.0, 1024);
    e.getTransport().play();
    play(e, 1024, 1024);
    std::printf("  queued through a device change: the instrument is prepared for %.0f Hz / %d (renders %d), latency %d\n",
                observer->rate_, observer->maxBlock_, observer->largestBlock_, e.getLatencySamples());
    DAWLAB_CHECK(e.getMixer().getTrack(0).getReplacementInstrument() == observer);
    DAWLAB_CHECK(observer->rate_ == 96000.0 && observer->largestBlock_ == 1024 && observer->maxBlock_ >= observer->largestBlock_);
    DAWLAB_CHECK(e.getLatencySamples() == 480); // the limiter's 5 ms lookahead at 96 kHz
}

void testSongEndCountsNotes() {
    // A song of notes only: one bar from bar 2 ends at tick 7680, sample 192000 (25 samples a tick). The song's end
    // counts it, so a bounce of the default length plays the whole clip.
    Engine e;
    setUp(e);
    e.setTrackMidiClips(testId(0), {clipOf(1, kBar, kBar, {{0, 480, 60, 100, 0}})});
    RenderOptions options; // numSamples 0: up to the song's end
    const AudioBuffer out = renderOffline(e, options);
    std::printf("  a song of notes only: it ends at %lld, a bounce of the default length has %d samples, peak %.2f\n",
                static_cast<long long>(e.getSongEndSample()), out.getNumSamples(), static_cast<double>(peakAbs(out)));
    DAWLAB_CHECK(e.getSongEndSample() == 2 * kBar * 25);
    DAWLAB_CHECK(out.getNumSamples() == 2 * kBar * 25 && peakAbs(out) > 0.01f);
}

void testNoAllocation() {
    Engine e;
    setUp(e);
    e.setTrackMidiClips(testId(0), song());
    e.getMixer().getTrack(0).setArmed(true);
    e.applyPendingCommandsOffline();
    Transport& tr = e.getTransport();
    tr.setLoopRange(96000, 2 * 96000);
    tr.setLoopEnabled(true);
    tr.play();
    std::mt19937 rng(3);
    std::vector<std::pair<int64_t, MidiMessage>> live;
    for (int i = 0; i < 400; ++i)
        live.push_back({i * 1200, i % 2 == 0 ? MidiMessage::noteOn(0, 40 + i % 30, 100) : MidiMessage::noteOff(0, 40 + (i - 1) % 30)});
    long allocs = 0;
    play(e, 4 * 96000, 0, &allocs, &rng, &live);
    tr.setPosition(10000); // a seek
    play(e, 48000, 0, &allocs, &rng);
    e.setTrackMidiClips(testId(0), song()); // a new list arrives
    play(e, 48000, 0, &allocs, &rng);
    VoiceSettings bright;
    bright.cutoffHz = 9000.0;
    bright.attack = 0.05;
    e.getTrackSynth(testId(0))->setSettings(bright); // new settings while notes sound
    play(e, 48000, 0, &allocs, &rng);
    tr.stop();
    play(e, 48000, 0, &allocs, &rng);
    std::printf("  allocations on the audio thread with clips, a loop, seeks and live MIDI: %ld\n", allocs);
    DAWLAB_CHECK(allocs == 0);
    e.collectGarbage();
}

// UI, audio and MIDI threads at once (run it under ThreadSanitizer as well).
void testThreads() {
    Engine e;
    setUp(e);
    e.getMixer().getTrack(0).setArmed(true);
    e.setTrackMidiClips(testId(0), song());
    e.applyPendingCommandsOffline();
    e.getTransport().play();
    MidiFifo fifo;
    std::atomic<bool> stop{false};
    std::atomic<long> audioAllocs{0};
    std::atomic<int> nanBlocks{0};
    const auto t0 = std::chrono::steady_clock::now();
    auto now = [t0] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };

    std::thread audio([&] {
        AudioBuffer out(kNumChannels, 256);
        std::vector<BlockEvent> ev;
        ev.reserve(256);
        long late = 0;
        while (!stop.load()) {
            const double blockTime = now() + 0.01;
            const long before = gAllocs.load();
            {
                CountScope count;
                drainMidi(fifo, blockTime, 256, kSampleRate, MidiTiming::Scheduled, 0.02, ev, late);
                e.processBlock(out.toBlock(), {}, MidiBlock{ev.data(), static_cast<int>(ev.size())});
            }
            audioAllocs.fetch_add(gAllocs.load() - before);
            for (int i = 0; i < 256; ++i)
                if (!std::isfinite(out.getSample(0, i)))
                    nanBlocks.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });
    std::thread keyboard([&] {
        for (int i = 0; !stop.load(); ++i) {
            fifo.push({now(), i % 2 == 0 ? MidiMessage::noteOn(0, 50 + i % 20, 100) : MidiMessage::noteOff(0, 50 + (i - 1) % 20)});
            std::this_thread::sleep_for(std::chrono::microseconds(700));
        }
    });
    std::mt19937 rng(9);
    for (int i = 0; i < 300; ++i) { // the UI
        switch (i % 5) {
        case 0: e.setTrackMidiClips(testId(0), song()); break;
        case 1: e.getTransport().setPosition(rng() % 400000); break;
        case 2: e.getMixer().getTrack(0).setInstrument(i % 3 != 0); break;
        case 3: e.getMixer().getTrack(0).setArmed(i % 2 == 0); break;
        default: e.collectGarbage(); break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    stop.store(true);
    audio.join();
    keyboard.join();
    e.collectGarbage();
    std::printf("  UI, audio and MIDI threads for 300 UI steps: %ld allocations on the audio thread, %d bad blocks, %llu leaked\n",
                audioAllocs.load(), nanBlocks.load(), static_cast<unsigned long long>(e.getGarbageLeaked()));
    DAWLAB_CHECK(audioAllocs.load() == 0 && nanBlocks.load() == 0 && e.getGarbageLeaked() == 0);
}

// The parser that turns a MIDI byte stream into messages (Step 5): running status, real-time bytes inside a message,
// system exclusive skipped, data bytes with no status counted, and a message with one data byte.
void testMidiParser() {
    MidiParser p;
    std::vector<MidiMessage> got;
    const auto feed = [&](std::initializer_list<int> bytes) {
        for (const int b : bytes) {
            MidiMessage m;
            if (p.feed(static_cast<std::uint8_t>(b), m))
                got.push_back(m);
        }
    };
    feed({0x45, 0x10});                    // data with no status yet: skipped
    feed({0x91, 60, 100});                 // a note-on on channel 2
    feed({62, 0xF8, 90});                  // running status, with a clock tick in the middle
    feed({0xF0, 0x7E, 0x01, 0x02, 0xF7});  // system exclusive: nothing
    feed({64, 80});                        // after it, no running status: skipped
    feed({0xC3, 5});                       // a program change has one data byte
    feed({0xE0, 0x00, 0x40});              // pitch bend in the middle
    const std::vector<MidiMessage> want = {
        MidiMessage::noteOn(1, 60, 100), MidiMessage::noteOn(1, 62, 90),
        MidiMessage{0xC3, 5, 0}, MidiMessage::pitchBend(0, 0)};
    std::printf("  a byte stream: %zu messages, %ld data bytes skipped\n", got.size(), p.skipped());
    DAWLAB_CHECK(got == want);
    DAWLAB_CHECK(p.skipped() == 4);
    DAWLAB_CHECK(!(MidiMessage::noteOn(0, 60, 100) == MidiMessage::noteOn(0, 60, 99)));
    DAWLAB_CHECK(!(MidiMessage::noteOn(0, 60, 100) == MidiMessage::noteOn(0, 61, 100)));
    DAWLAB_CHECK(!(MidiMessage::noteOn(0, 60, 100) == MidiMessage::noteOn(1, 60, 100)));
}

// 7. The synth by hand, one message at a time (Synth::handle()): which voice a note steals, the pedal's
//    threshold, all notes off (also of a voice in the middle of a steal), and the bend's range.
int activeAfter(Synth& s, int numSamples) {
    std::vector<float> out(static_cast<size_t>(numSamples), 0.0f);
    s.render(nullptr, 0, out.data(), numSamples);
    return s.activeVoices();
}

void testSynthByHand() {
    const int second = static_cast<int>(kSampleRate);
    auto s = std::make_unique<Synth>();
    s->prepare(kSampleRate, VoiceSettings{});
    // All 16 voices sound; 40 and 41 are let go, in that order; a second note off for 40 changes nothing.
    for (int pitch = 40; pitch < 40 + Synth::kNumVoices; ++pitch)
        s->handle(MidiMessage::noteOn(0, pitch, 100));
    s->handle(MidiMessage::noteOff(0, 40));
    s->handle(MidiMessage::noteOff(0, 41));
    s->handle(MidiMessage::noteOff(0, 40));
    DAWLAB_CHECK(s->activeVoices() == Synth::kNumVoices && s->stolen() == 0);
    s->handle(MidiMessage::noteOn(0, 60, 100)); // steals 40, released first
    DAWLAB_CHECK(s->stolen() == 1);
    s->handle(MidiMessage::noteOn(0, 41, 100)); // 41 still rings: its own voice again, nothing stolen
    DAWLAB_CHECK(s->stolen() == 1);
    s->handle(MidiMessage::noteOn(0, 62, 100)); // steals again: a voice in the middle of a steal...
    DAWLAB_CHECK(s->stolen() == 2);
    s->handle(MidiMessage::controller(0, Synth::kAllNotesOff, 0)); // ...ends with all the others
    DAWLAB_CHECK(activeAfter(*s, second) == 0);

    // The pedal is down from 64 on; a note let go under it rings until the pedal comes up (63).
    s->handle(MidiMessage::controller(0, Synth::kSustainPedal, 64));
    s->handle(MidiMessage::noteOn(0, 50, 100));
    s->handle(MidiMessage::noteOff(0, 50));
    DAWLAB_CHECK(activeAfter(*s, second) == 1);
    s->handle(MidiMessage::controller(0, Synth::kSustainPedal, 63));
    DAWLAB_CHECK(activeAfter(*s, second) == 0);
    // All notes off lets the pedal up too: the next note let go ends.
    s->handle(MidiMessage::controller(0, Synth::kSustainPedal, 127));
    s->handle(MidiMessage::controller(0, Synth::kAllNotesOff, 0));
    s->handle(MidiMessage::noteOn(0, 52, 100));
    s->handle(MidiMessage::noteOff(0, 52));
    DAWLAB_CHECK(activeAfter(*s, second) == 0);

    // The bend wheel all the way up is kBendRange (2) semitones: A4 (440 Hz) sounds as B4, 493.9 Hz.
    VoiceSettings soft; // a low cutoff leaves little more than the fundamental: one upward zero crossing a period
    soft.cutoffHz = 800.0;
    soft.resonance = 0.707;
    s->prepare(kSampleRate, soft);
    s->handle(MidiMessage::pitchBend(0, 8191));
    s->handle(MidiMessage::noteOn(0, 69, 100));
    std::vector<float> out(static_cast<size_t>(second), 0.0f);
    s->render(nullptr, 0, out.data(), second);
    int upward = 0;
    for (size_t i = static_cast<size_t>(second / 10) + 1; i < out.size(); ++i)
        if (out[i - 1] < 0.0f && out[i] >= 0.0f)
            ++upward;
    const double hz = upward / 0.9; // crossings in the last 0.9 s
    std::printf("  synth by hand: %ld stolen, bend up %.1f Hz (expected 2, 493.9)\n", s->stolen(), hz);
    DAWLAB_CHECK(std::abs(hz - 493.9) < 10.0);
}

// 8. The MIDI clips and the instrument of a track by raw command (Engine::pushCommand(), past the UI's checks):
//    the old ones come back as garbage, and a command aimed at a bus hands its own straight back.
void testRawMidiCommands() {
    Engine e;
    setUp(e);
    AudioBuffer block(kNumChannels, 64);
    auto apply = [&e, &block](const Command& c) {
        e.pushCommand(c);
        e.processBlock(block.toBlock());
        return e.collectGarbage();
    };
    auto newSynth = [] {
        auto* s = new SynthInstrument();
        s->prepare(kSampleRate, kBlockSize);
        return s;
    };
    apply(Command::replaceMidiClips(testTrack(0), new std::vector<MidiClip>{clipOf(1, 0, kBar, {})}));
    auto* clips = new std::vector<MidiClip>{clipOf(2, 0, kBar, {}), clipOf(3, kBar, kBar, {})};
    DAWLAB_CHECK(apply(Command::replaceMidiClips(testTrack(0), clips)) == 1); // the list before
    DAWLAB_CHECK(e.getMixer().getTrack(0).getMidiClips() == clips);
    apply(Command::replaceInstrument(testTrack(0), newSynth()));
    SynthInstrument* synth = newSynth();
    DAWLAB_CHECK(apply(Command::replaceInstrument(testTrack(0), synth)) == 1); // the instrument before
    DAWLAB_CHECK(e.getMixer().getTrack(0).getReplacementInstrument() == synth);
    DAWLAB_CHECK(apply(Command::replaceMidiClips(ChannelRef::bus(0), new std::vector<MidiClip>{clipOf(4, 0, kBar, {})})) == 1);
    DAWLAB_CHECK(apply(Command::replaceInstrument(ChannelRef::bus(0), newSynth())) == 1);
    DAWLAB_CHECK(e.getMixer().getTrack(0).getMidiClips() == clips && e.getMixer().getTrack(0).getReplacementInstrument() == synth);
    DAWLAB_CHECK(e.getGarbageLeaked() == 0);
    std::printf("  raw MIDI clip and instrument commands: the old ones back as garbage, a bus hands them straight back\n");
}

} // namespace

int main() {
    std::printf("l18_test_midi: instrument tracks in the engine\n");
    testNoteOnItsSample();
    testBlockInvariance();
    testNothingHangs();
    testPedalLetGo();
    testMoreCases();
    testLiveMidi();
    testGarbageOfAFullTrack();
    testAnotherInstrument();
    testQueuedThroughADeviceChange();
    testSongEndCountsNotes();
    testMidiParser();
    testSynthByHand();
    testRawMidiCommands();
    testNoAllocation();
    testThreads();
    return dawlab::test::summary("l18_test_midi");
}
