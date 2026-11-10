// Automation in the engine (chapter 16, Step 5): the snapshots of Steps 1-4 on the real audio path,
// through real commands and queues. A DC clip makes every gain visible: out = dc * gain.

#include "TestHelpers.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
#include <vector>

// ---- allocation counting on one thread ------------------------------------------------------------
namespace {
thread_local bool tCounting = false;
std::atomic<long> gAllocs{0};

void* countedAlloc(std::size_t n) {
    if (tCounting)
        gAllocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n > 0 ? n : 1))
        return p;
    throw std::bad_alloc();
}
void countedFree(void* p) noexcept {
    if (p != nullptr && tCounting)
        gAllocs.fetch_add(1, std::memory_order_relaxed); // freeing on the audio thread is as bad
    std::free(p);
}
} // namespace
void* operator new(std::size_t n) { return countedAlloc(n); }
void* operator new[](std::size_t n) { return countedAlloc(n); }
void operator delete(void* p) noexcept { countedFree(p); }
void operator delete[](void* p) noexcept { countedFree(p); }
void operator delete(void* p, std::size_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { countedFree(p); }

using namespace l15test;

namespace {

constexpr TrackId kTrack{1};

// An effect that shows what the lanes do: "Level" scales the audio, "Type" is a choice of 4.
class TestEffect final : public InsertEffect {
public:
    static inline std::atomic<int> alive{0};
    explicit TestEffect(int latency = 0) : latency_(latency) { alive.fetch_add(1); }
    ~TestEffect() override { alive.fetch_sub(1); }
    const char* getName() const noexcept override { return "Test"; }
    void prepare(double, int, int) override { prepared_ = true; }
    void reset() noexcept override {
        std::fill(delay_.begin(), delay_.end(), 0.0f);
        at_ = 0;
    }
    void process(AudioBlock block) noexcept override {
        applied_ = active_;
        for (int ch = 0; ch < block.getNumChannels(); ++ch)
            for (int i = 0; i < block.getNumSamples(); ++i)
                block.getChannelPointer(ch)[i] *= applied_;
        if (latency_ > 0) { // a pure delay, on the first channel; the tests use mono tracks
            float* x = block.getChannelPointer(0);
            for (int i = 0; i < block.getNumSamples(); ++i) {
                const float in = x[i];
                x[i] = delay_[static_cast<size_t>(at_)];
                delay_[static_cast<size_t>(at_)] = in;
                at_ = (at_ + 1) % latency_;
            }
        }
    }
    void applyPendingParameters() noexcept override { active_ = level_.load(); }
    int getLatencySamples() const noexcept override { return latency_; }
    int getNumParameters() const noexcept override { return 2; }
    ParameterInfo getParameterInfo(int index) const noexcept override {
        ParameterInfo i;
        i.name = index == 0 ? "Level" : "Type";
        i.max = index == 0 ? 1.0f : 3.0f;
        i.defaultValue = index == 0 ? 1.0f : 0.0f;
        i.kind = index == 0 ? ParameterKind::Float : ParameterKind::Choice;
        return i;
    }
    float getParameter(int index) const noexcept override { return index == 0 ? level_.load() : type_.load(); }
    void setParameter(int index, float value) noexcept override { (index == 0 ? level_ : type_).store(getParameterInfo(index).clamp(value)); }
    float applied() const noexcept { return applied_; }

private:
    int latency_;
    bool prepared_ = false;
    std::atomic<float> level_{1.0f}, type_{0.0f};
    float active_ = 1.0f; // what a real effect's DSP object holds; set in applyPendingParameters()
    float applied_ = 1.0f;
    std::vector<float> delay_ = std::vector<float>(static_cast<size_t>(std::max(1, latency_)), 0.0f);
    int at_ = 0;
};

// The left gain of a strip at that lane value. Tracks here are mono (a pan law); buses and the master
// are stereo (Balance: 1 at the centre).
float faderGain(float normalized, PanMode mode = PanMode::PanLaw) {
    float l = 0.0f, r = 0.0f;
    ChannelStrip::computeStripGains(ChannelStrip::faderInfo().fromNormalized(normalized), 0.0f, dsp::PanLaw::ConstantPower, mode, l, r);
    return l;
}

LaneTarget gainOf(Engine& e, TrackId id = kTrack) { return {e.trackRef(id), EffectId{}, kLaneGain}; }

AutomationCurve ramp(int64_t from, float a, int64_t to, float b) {
    AutomationCurve c;
    c.add({from, a, Shape::Linear});
    c.add({to, b, Shape::Linear});
    return c;
}

// An engine with one mono track holding a DC clip of 1.0, n samples long.
void setUp(Engine& e, int n = 400000, int maxBlock = kBlockSize, double rate = kSampleRate) {
    e.prepare(rate, maxBlock);
    e.addTrack(kTrack);
    e.applyPendingCommandsOffline(); // a test may use a very small queue
    e.setTrackClips(kTrack, {Clip::fromBuffer(makeConstant(1.0f, n), 0)});
    e.applyPendingCommandsOffline();
}

// Plays n samples from where the transport is, in blocks; `size` 0 means random sizes.
AudioBuffer play(Engine& e, int n, int size = kBlockSize) {
    AudioBuffer out(kNumChannels, n);
    std::uint32_t state = 7u;
    for (int done = 0; done < n;) {
        state = state * 1664525u + 1013904223u;
        const int wanted = size > 0 ? size : 1 + static_cast<int>((state >> 8) % static_cast<std::uint32_t>(e.getMaxBlockSize()));
        const int len = std::min(wanted, n - done);
        e.processBlock(out.toBlock().getSubBlock(done, len));
        e.collectGarbage();
        done += len;
    }
    return out;
}

// ---- one gain per sample ---------------------------------------------------------------------------

void testFaderFollowsCurveEverySample() {
    Engine e;
    setUp(e);
    const AutomationCurve curve = ramp(0, 0.5f, 48000, 1.0f);
    DAWLAB_CHECK(e.setLaneCurve(gainOf(e), curve));
    e.applyPendingCommandsOffline();
    e.getTransport().play();
    const AudioBuffer out = play(e, 48000);
    float worst = 0.0f, biggestStep = 0.0f;
    for (int i = 0; i < 48000; ++i) {
        worst = std::max(worst, std::abs(out.getSample(0, i) - faderGain(curve.valueAt(i))));
        if (i > 0)
            biggestStep = std::max(biggestStep, std::abs(out.getSample(0, i) - out.getSample(0, i - 1)));
    }
    // A 1 s ramp over 56 dB, at the top (gain 2.8): 0.0004 per sample. As a staircase of one value per
    // block, 512 times that.
    std::printf("  fader ramp: worst error %.2e, biggest step %.6f\n", static_cast<double>(worst), static_cast<double>(biggestStep));
    DAWLAB_CHECK(worst < 1e-6f);
    DAWLAB_CHECK(biggestStep < 5e-4f);
}

void testJumpLandsOnItsSampleAsAShortRamp() {
    Engine e;
    setUp(e);
    AutomationCurve curve;
    curve.add({0, 0.5f, Shape::Hold});
    curve.add({1000, 0.75f, Shape::Linear});
    e.setLaneCurve(gainOf(e), curve);
    e.applyPendingCommandsOffline();
    e.getTransport().play();
    const AudioBuffer out = play(e, 1500, 512);
    const float before = faderGain(0.5f), after = faderGain(0.75f);
    std::printf("  jump at 1000: %.5f | %.5f ... %.5f (before %.5f, after %.5f; 2 ms of range: 24 steps)\n",
                static_cast<double>(out.getSample(0, 999)), static_cast<double>(out.getSample(0, 1000)),
                static_cast<double>(out.getSample(0, 1024)), static_cast<double>(before), static_cast<double>(after));
    DAWLAB_CHECK(out.getSample(0, 999) == before);
    DAWLAB_CHECK(out.getSample(0, 1000) > before && out.getSample(0, 1000) < before * 1.15f);
    DAWLAB_CHECK(out.getSample(0, 1020) < after);
    DAWLAB_CHECK(out.getSample(0, 1030) == after);
}

void testTheRampFollowsTheSampleRate() {
    Engine e;
    setUp(e, 400000, kBlockSize, 96000.0); // twice the samples for the same 2 ms
    AutomationCurve curve;
    curve.add({0, 0.5f, Shape::Hold});
    curve.add({1000, 0.75f, Shape::Linear});
    e.setLaneCurve(gainOf(e), curve);
    e.applyPendingCommandsOffline();
    e.getTransport().play();
    const AudioBuffer out = play(e, 1500);
    DAWLAB_CHECK(out.getSample(0, 1040) < faderGain(0.75f));
    DAWLAB_CHECK(out.getSample(0, 1050) == faderGain(0.75f));
}

void testBlockSizeChangesNothing() {
    auto session = [](int blockSize) {
        auto e = std::make_unique<Engine>();
        setUp(*e);
        AutomationCurve curve;
        curve.add({2000, 0.5f, Shape::Linear});
        curve.add({9000, 0.9f, Shape::Hold});
        curve.add({15000, 0.6f, Shape::Linear});
        curve.add({15000, 0.8f, Shape::Linear});
        curve.add({40000, 0.5f, Shape::Linear});
        e->setLaneCurve(gainOf(*e), curve);
        e->setLaneCurve(LaneTarget{e->trackRef(kTrack), EffectId{}, kLanePan}, ramp(0, 0.25f, 30000, 0.75f));
        e->applyPendingCommandsOffline();
        e->getTransport().setLoopRange(10000, 30000);
        e->getTransport().setLoopEnabled(true);
        e->getTransport().play();
        return play(*e, 60000, blockSize);
    };
    const AudioBuffer one = session(1);
    float worst = 0.0f;
    for (const int size : {64, 300, 512, 0})
        worst = std::max(worst, maxAbsDiff(one, session(size)));
    std::printf("  block sizes 64, 300, 512, random against 1: biggest difference %g (expected 0)\n", static_cast<double>(worst));
    DAWLAB_CHECK(worst == 0.0f);
}

void testPanAndTheOtherStrips() {
    { // pan, per sample, per side
        Engine e;
        setUp(e);
        const AutomationCurve pan = ramp(0, 0.0f, 24000, 1.0f); // hard left to hard right
        e.setLaneCurve(LaneTarget{e.trackRef(kTrack), EffectId{}, kLanePan}, pan);
        e.applyPendingCommandsOffline();
        e.getTransport().play();
        const AudioBuffer out = play(e, 24000);
        float worst = 0.0f;
        const float db = 0.0f;
        for (int i = 0; i < 24000; ++i) {
            float l = 0.0f, r = 0.0f;
            ChannelStrip::computeStripGains(db, ChannelStrip::panInfo().fromNormalized(pan.valueAt(i)), dsp::PanLaw::ConstantPower,
                                            PanMode::PanLaw, l, r);
            worst = std::max({worst, std::abs(out.getSample(0, i) - l), std::abs(out.getSample(1, i) - r)});
        }
        std::printf("  pan ramp: worst error %.2e on either side\n", static_cast<double>(worst));
        DAWLAB_CHECK(worst < 1e-5f);
    }
    { // the master
        Engine e;
        setUp(e);
        const AutomationCurve curve = ramp(0, 0.5f, 48000, 0.75f);
        e.setLaneCurve(LaneTarget{ChannelRef::master(), EffectId{}, kLaneGain}, curve);
        e.applyPendingCommandsOffline();
        e.getTransport().play();
        const AudioBuffer out = play(e, 10000);
        const float track = faderGain(ChannelStrip::faderInfo().toNormalized(0.0f));
        float worst = 0.0f;
        for (int i = 0; i < 10000; ++i)
            worst = std::max(worst, std::abs(out.getSample(0, i) - track * faderGain(curve.valueAt(i), e.getMixer().getMaster().getPanMode())));
        DAWLAB_CHECK(worst < 1e-5f);
    }
    { // a bus: the track only reaches it through a pre-fader send
        Engine e;
        setUp(e);
        Track* t = e.getTrack(kTrack);
        t->setGainDb(kMinusInfinityDb);
        t->setSendPreFader(0, true);
        t->setSendLevelDb(0, 0.0f);
        const AutomationCurve curve = ramp(0, 0.5f, 48000, 0.75f);
        e.setLaneCurve(LaneTarget{ChannelRef::bus(0), EffectId{}, kLaneGain}, curve);
        e.requestReset();
        e.applyPendingCommandsOffline();
        e.getTransport().play();
        const AudioBuffer out = play(e, 10000);
        const float masterGain = faderGain(ChannelStrip::faderInfo().toNormalized(0.0f), e.getMixer().getMaster().getPanMode());
        float worst = 0.0f;
        for (int i = 0; i < 10000; ++i)
            worst = std::max(worst, std::abs(out.getSample(0, i) - masterGain * faderGain(curve.valueAt(i), e.getMixer().getBus(0).getPanMode())));
        DAWLAB_CHECK(worst < 1e-5f);
    }
}

// ---- the knob ----------------------------------------------------------------------------------------

void testCurveArrivesAndLeavesWithoutAClick() {
    Engine e;
    setUp(e);
    e.getTrack(kTrack)->setGainDb(0.0f);
    e.getTransport().play();
    play(e, 4800);
    AudioBuffer playing = play(e, 512);
    const float knob = faderGain(ChannelStrip::faderInfo().toNormalized(0.0f));
    DAWLAB_CHECK(playing.getSample(0, 511) == knob || std::abs(playing.getSample(0, 511) - knob) < 1e-5f);

    AutomationCurve low;
    low.add({0, 0.6f, Shape::Linear});
    e.setLaneCurve(gainOf(e), low); // into a playing engine
    const AudioBuffer arriving = play(e, 512);
    float biggestDb = 0.0f;
    float previous = playing.getSample(0, 511);
    for (int i = 0; i < 512; ++i) {
        biggestDb = std::max(biggestDb, std::abs(gainToDb(arriving.getSample(0, i)) - gainToDb(previous)));
        previous = arriving.getSample(0, i);
    }
    // The range is 112 dB and the limit 1/96 of it per sample.
    std::printf("  a curve arrives 33 dB below the fader: biggest step %.2f dB (limit %.2f), block ends on the curve: %s\n",
                static_cast<double>(biggestDb), 112.0 / 96.0, arriving.getSample(0, 511) == faderGain(0.6f) ? "yes" : "no");
    DAWLAB_CHECK(biggestDb < 1.2f);
    DAWLAB_CHECK(arriving.getSample(0, 511) == faderGain(0.6f));

    e.clearLaneCurve(gainOf(e));
    const AudioBuffer leaving = play(e, 512);
    const AudioBuffer later = play(e, 4800);
    std::printf("  the curve leaves: first sample %.5f (was %.5f), 100 ms later %.5f (the fader: %.5f)\n",
                static_cast<double>(leaving.getSample(0, 0)), static_cast<double>(faderGain(0.6f)),
                static_cast<double>(later.getSample(0, 4799)), static_cast<double>(knob));
    DAWLAB_CHECK(std::abs(leaving.getSample(0, 0) - faderGain(0.6f)) < 0.002f); // the smoother goes on from there
    DAWLAB_CHECK(std::abs(later.getSample(0, 4799) - knob) < 1e-5f);
    DAWLAB_CHECK(e.getLane(gainOf(e))->getCurve() == nullptr);
}

void testShownValue() {
    Engine e;
    setUp(e);
    Track* t = e.getTrack(kTrack);
    t->setGainDb(-6.0f);
    DAWLAB_CHECK(t->getShownGainDb() == -6.0f); // no curve: the knob
    AutomationCurve held;
    held.add({0, 0.5f, Shape::Linear});
    e.setLaneCurve(gainOf(e), held);
    e.getTransport().play();
    play(e, 512);
    const float expected = ChannelStrip::faderInfo().fromNormalized(0.5f);
    std::printf("  fader knob -6 dB, curve at %.1f dB: shown %.1f dB, knob still %.1f dB\n", static_cast<double>(expected),
                static_cast<double>(t->getShownGainDb()), static_cast<double>(t->getGainDb()));
    DAWLAB_CHECK_NEAR(t->getShownGainDb(), expected, 1e-4);
    DAWLAB_CHECK(t->getGainDb() == -6.0f);
}

// ---- effects ------------------------------------------------------------------------------------------

void testEffectParameters() {
    const int alive = TestEffect::alive.load();
    {
        Engine e;
        setUp(e);
        InsertEffect* first = e.addInsert(e.trackRef(kTrack), -1, std::make_unique<TestEffect>());
        auto owned = std::make_unique<TestEffect>();
        TestEffect* fx = owned.get();
        InsertEffect* second = e.addInsert(e.trackRef(kTrack), -1, std::move(owned));
        DAWLAB_CHECK(first != nullptr && second != nullptr);
        DAWLAB_CHECK(first->getEffectId().isValid() && second->getEffectId() != first->getEffectId());
        const LaneTarget level{e.trackRef(kTrack), second->getEffectId(), 0};
        const LaneTarget type{e.trackRef(kTrack), second->getEffectId(), 1};

        // Right after addInsert(): the audio thread has not taken the effect yet, and it works.
        const AutomationCurve curve = ramp(0, 0.0f, 48000, 1.0f);
        DAWLAB_CHECK(e.setLaneCurve(level, curve));
        AutomationCurve steps;
        steps.add({0, 0.0f, Shape::Hold});
        steps.add({5120, 1.0f, Shape::Hold});
        DAWLAB_CHECK(e.setLaneCurve(type, steps));
        e.getTransport().play();
        float worst = 0.0f;
        AudioBuffer block(kNumChannels, 512);
        for (int b = 0; b < 40; ++b) {
            const int64_t at = e.getTransport().getPosition();
            e.processBlock(block.toBlock());
            // One value per block, the one at its start, already in force in that block.
            worst = std::max(worst, std::abs(fx->applied() - curve.valueAt(at)));
        }
        std::printf("  effect parameter: worst error per block %.2e; id of the effect %u\n", static_cast<double>(worst), second->getEffectId().value);
        DAWLAB_CHECK(worst < 1e-6f);
        // A choice does not pass through values in between, and no ramp delays it.
        e.getTransport().setPosition(0);
        e.processBlock(block.toBlock());
        const float low = fx->getParameter(1);
        e.getTransport().setPosition(5120);
        e.processBlock(block.toBlock());
        DAWLAB_CHECK(low == 0.0f && fx->getParameter(1) == 3.0f);

        // Moving the effect changes nothing: its id finds it, and the lane is inside it.
        DAWLAB_CHECK(e.moveInsert(e.trackRef(kTrack), 1, 0));
        e.getTransport().setPosition(24000);
        e.processBlock(block.toBlock());
        DAWLAB_CHECK(e.getInsert(e.trackRef(kTrack), 0) == second);
        DAWLAB_CHECK_NEAR(fx->applied(), 0.5f, 1e-6);
        AutomationCurve held;
        held.add({0, 0.25f, Shape::Linear});
        DAWLAB_CHECK(e.setLaneCurve(level, held)); // found by id, in its new slot
        for (int b = 0; b < 3; ++b)
            e.processBlock(block.toBlock());
        DAWLAB_CHECK(fx->applied() == 0.25f);

        // The curve goes: back to the value the knob had when the first curve came.
        e.clearLaneCurve(level);
        e.processBlock(block.toBlock());
        std::printf("  the curve leaves: the parameter returns to %.2f (expected 1.00)\n", static_cast<double>(fx->getParameter(0)));
        DAWLAB_CHECK(fx->getParameter(0) == 1.0f);
        e.collectGarbage();

        // The effect goes, and its lanes and curves with it, on the UI thread.
        DAWLAB_CHECK(e.setLaneCurve(level, held));
        e.removeInsert(e.trackRef(kTrack), 0);
        // This one is queued after the removal: the audio thread finds no lane and hands it back.
        DAWLAB_CHECK(e.setLaneCurve(level, held));
        const uint64_t notFound = e.getLanesNotFound();
        e.processBlock(block.toBlock());
        e.collectGarbage();
        DAWLAB_CHECK(e.getLanesNotFound() == notFound + 1);
        DAWLAB_CHECK(e.getInsert(e.trackRef(kTrack), 0) == first && e.getNumInserts(e.trackRef(kTrack)) == 1);
        DAWLAB_CHECK(TestEffect::alive.load() == alive + 1);
        DAWLAB_CHECK(e.getLane(level) == nullptr);

        // The first curve starts from the parameter's value NOW, not from when the effect was added.
        auto lateOwned = std::make_unique<TestEffect>();
        TestEffect* late = lateOwned.get();
        e.addInsert(e.trackRef(kTrack), -1, std::move(lateOwned));
        e.processBlock(block.toBlock());
        late->setParameter(0, 0.5f); // the user turns the knob
        const LaneTarget lateLevel{e.trackRef(kTrack), late->getEffectId(), 0};
        DAWLAB_CHECK(e.setLaneCurve(lateLevel, held));
        e.processBlock(block.toBlock());
        const float whileCurve = late->getParameter(0);
        e.clearLaneCurve(lateLevel);
        e.processBlock(block.toBlock());
        std::printf("  knob turned to 0.50, a curve at %.2f, the curve leaves: %.2f (expected 0.50)\n", static_cast<double>(whileCurve),
                    static_cast<double>(late->getParameter(0)));
        DAWLAB_CHECK(whileCurve == 0.25f && late->getParameter(0) == 0.5f);
        e.removeInsert(e.trackRef(kTrack), 1);
        e.processBlock(block.toBlock());
        e.collectGarbage();

        // A replaced effect is a new effect: its own id, its own lanes, and the old curves are gone.
        auto swapOwned = std::make_unique<TestEffect>();
        TestEffect* swapped = swapOwned.get();
        const EffectId before = first->getEffectId();
        DAWLAB_CHECK(e.replaceInsert(e.trackRef(kTrack), 0, std::move(swapOwned)) == swapped);
        e.processBlock(block.toBlock());
        e.collectGarbage();
        DAWLAB_CHECK(swapped->getEffectId().isValid() && swapped->getEffectId() != before);
        DAWLAB_CHECK(e.getLane(LaneTarget{e.trackRef(kTrack), swapped->getEffectId(), 0}) != nullptr);
        DAWLAB_CHECK(e.getLane(LaneTarget{e.trackRef(kTrack), before, 0}) == nullptr);

        // An id that undo brings back stays, and new ids stay above it.
        auto back = std::make_unique<TestEffect>();
        back->setEffectId(EffectId{50});
        InsertEffect* restored = e.addInsert(e.trackRef(kTrack), -1, std::move(back));
        InsertEffect* next = e.addInsert(e.trackRef(kTrack), -1, std::make_unique<TestEffect>());
        DAWLAB_CHECK(restored->getEffectId() == EffectId{50} && next->getEffectId() == EffectId{51});
    }
    DAWLAB_CHECK(TestEffect::alive.load() == alive);
}

// ---- latency ------------------------------------------------------------------------------------------

void testBusFaderWaitsToo() {
    Engine e;
    setUp(e);
    e.addInsert(e.trackRef(kTrack), -1, std::make_unique<TestEffect>(100));
    e.addInsert(ChannelRef::bus(0), -1, std::make_unique<TestEffect>(30));
    Track* t = e.getTrack(kTrack);
    t->setGainDb(kMinusInfinityDb); // only the send reaches the output
    t->setSendPreFader(0, true);
    t->setSendLevelDb(0, 0.0f);
    AutomationCurve curve;
    curve.add({0, 0.5f, Shape::Hold});
    curve.add({10000, 0.75f, Shape::Linear});
    e.setLaneCurve(LaneTarget{ChannelRef::bus(0), EffectId{}, kLaneGain}, curve);
    e.applyPendingCommandsOffline();
    DAWLAB_CHECK(e.getLatencySamples() == 130);
    const AudioBuffer out = render(e, 12000, kBlockSize, true);
    const float a = out.getSample(0, 9999), b = out.getSample(0, 10000);
    std::printf("  a bus behind 100 + 30 samples of latency: sample 9999 %.5f, sample 10000 %.5f\n", static_cast<double>(a), static_cast<double>(b));
    DAWLAB_CHECK(a == out.getSample(0, 9000) && b > a);
}

void testFaderWaitsForDelayedAudio() {
    for (const int lane : {0, 1, 2}) { // on the track with the latency, on a track with its PDC delay, on the master
        Engine e;
        setUp(e);
        e.addTrack(TrackId{2});
        e.addInsert(e.trackRef(lane == 1 ? TrackId{2} : kTrack), -1, std::make_unique<TestEffect>(100));
        if (lane == 1) // the DC is on track 1, which now has a compensator of 100 samples
            DAWLAB_CHECK(e.getTrack(kTrack) != nullptr);
        AutomationCurve curve;
        curve.add({0, 0.5f, Shape::Hold});
        curve.add({10000, 0.75f, Shape::Linear});
        const LaneTarget target = lane == 2 ? LaneTarget{ChannelRef::master(), EffectId{}, kLaneGain} : gainOf(e);
        e.setLaneCurve(target, curve);
        e.applyPendingCommandsOffline();
        DAWLAB_CHECK(e.getLatencySamples() == 100);
        const AudioBuffer out = render(e, 12000, kBlockSize, true); // what a bounce does: latency compensated
        const float a = out.getSample(0, 9999), b = out.getSample(0, 10000);
        std::printf("  100 samples of latency, lane %d: sample 9999 %.5f, sample 10000 %.5f, 200 later %.5f\n", lane, static_cast<double>(a),
                    static_cast<double>(b), static_cast<double>(out.getSample(0, 10200)));
        DAWLAB_CHECK(a == out.getSample(0, 9000)); // not moving yet
        DAWLAB_CHECK(b > a);                       // on the very sample
        DAWLAB_CHECK(out.getSample(0, 10200) > a * 1.5f);
    }
}

// ---- modes and recording ---------------------------------------------------------------------------------

void testModes() {
    const char* names[] = {"Read", "Touch", "Latch", "Write"};
    const AutomationMode modes[] = {AutomationMode::Read, AutomationMode::Touch, AutomationMode::Latch, AutomationMode::Write};
    const float C = 0.25f, H = 0.75f;
    const float expected[4][5] = {{C, C, C, C, H}, {C, H, C, C, H}, {C, H, H, C, H}, {H, H, H, C, H}};
    std::printf("           untouched touched released stopped  stopped+touch\n");
    for (int m = 0; m < 4; ++m) {
        Engine e;
        setUp(e);
        AutomationCurve curve;
        curve.add({0, C, Shape::Linear});
        e.setLaneCurve(gainOf(e), curve);
        AutomationLane* lane = e.getLane(gainOf(e));
        lane->setKnobValue(H);
        lane->setMode(modes[m]);
        e.getTransport().play();
        const int settle = 6 * 512; // the hand's 50 ms for the full range
        float shown[5] = {};
        auto stage = [&](int s) {
            play(e, settle);
            shown[s] = lane->getShownValue();
        };
        stage(0);
        lane->touch();
        stage(1);
        lane->release();
        stage(2);
        e.getTransport().stop();
        stage(3);
        lane->touch();
        stage(4);
        lane->release();
        std::printf("  %-7s", names[m]);
        bool right = true;
        for (int s = 0; s < 5; ++s) {
            std::printf("  %.2f   ", static_cast<double>(shown[s]));
            right = right && shown[s] == expected[m][s];
        }
        std::printf("\n");
        DAWLAB_CHECK(right);
    }
}

void testRecordAndPlayBack() {
    Engine e;
    setUp(e);
    AutomationCurve master; // the UI's copy
    master.add({0, 0.5f, Shape::Linear});
    const LaneTarget target = gainOf(e);
    e.setLaneCurve(target, master);
    e.applyPendingCommandsOffline();
    AutomationLane* lane = e.getLane(target);
    lane->setMode(AutomationMode::Touch);
    lane->setKnobValue(0.5f);
    AutomationRecorder recorder;
    e.getTransport().play();
    std::vector<float> first;
    const int blocks = 300, touchAt = 50, releaseAt = 250;
    AudioBuffer block(kNumChannels, 512);
    int passes = 0;
    auto onPass = [&](const LaneTarget& t, const std::vector<AutomationPoint>& points) {
        DAWLAB_CHECK(t == target);
        master = punch(master, points);
        e.setLaneCurve(target, master);
        ++passes;
    };
    int64_t from = 0, to = 0;
    for (int b = 0; b < blocks; ++b) {
        if (b == touchAt) {
            lane->touch();
            from = e.getTransport().getPosition();
        }
        if (b >= touchAt && b < releaseAt)
            lane->setKnobValue(0.5f + 0.2f * std::sin(static_cast<float>(b - touchAt) * 512.0f / 48000.0f * 3.0f));
        if (b == releaseAt) {
            lane->release();
            to = e.getTransport().getPosition();
        }
        e.processBlock(block.toBlock());
        for (int i = 0; i < 512; ++i)
            first.push_back(block.getSample(0, i));
        if (b % 3 == 2) {
            recorder.collect(e.getCaptureQueue(), onPass);
            e.collectGarbage();
        }
    }
    recorder.collect(e.getCaptureQueue(), onPass);
    std::printf("  recorded 200 blocks of a moving hand: %d pass, %d points; recording %s\n", passes, master.size(), recorder.isRecording(target) ? "yes" : "no");
    DAWLAB_CHECK(passes == 1 && !recorder.isRecording(target));
    DAWLAB_CHECK(master.size() >= 6 && master.size() < 60);
    DAWLAB_CHECK(master.valueAt(from - 1) == 0.5f && master.valueAt(to + 100) == 0.5f);

    // Played back in Read, from the start, nobody at the fader.
    lane->setMode(AutomationMode::Read);
    lane->setKnobValue(0.0f);
    e.getTransport().setPosition(0);
    e.applyPendingCommandsOffline();
    const AudioBuffer second = play(e, blocks * 512);
    double worstDb = 0.0;
    for (int i = 4800; i < blocks * 512; ++i)
        if (i < from - 2400 || i > from + 4800) // the hand arrives slower than it replays
            if (i < to - 2400 || i > to + 4800)
                worstDb = std::max(worstDb, std::abs(static_cast<double>(gainToDb(first[static_cast<size_t>(i)]) - gainToDb(second.getSample(0, i)))));
    std::printf("  played back in Read: at most %.2f dB from what was heard\n", worstDb);
    DAWLAB_CHECK(worstDb < 2.5);
}

void testTheFirstRecordingHasNoCurveYet() {
    Engine e;
    setUp(e);
    Track* t = e.getTrack(kTrack);
    const LaneTarget gain = gainOf(e), pan{e.trackRef(kTrack), EffectId{}, kLanePan};
    t->setGainDb(-12.0f); // the fader's value IS the lane's knob, without anyone saying so
    t->setPan(0.5f);
    AutomationLane* gainLane = e.getLane(gain);
    AutomationLane* panLane = e.getLane(pan);
    gainLane->setMode(AutomationMode::Write);
    panLane->setMode(AutomationMode::Write);
    e.getTransport().play();
    play(e, 1024);
    std::printf("  Write without a curve: fader %.2f dB, pan %.2f (expected -12.00, 0.50)\n",
                static_cast<double>(t->getShownGainDb()), static_cast<double>(t->getShownPan()));
    DAWLAB_CHECK_NEAR(t->getShownGainDb(), -12.0f, 1e-3);
    DAWLAB_CHECK_NEAR(t->getShownPan(), 0.5f, 1e-5);

    // Back to Read in the middle: the lane goes idle, and still ends its recording.
    gainLane->setMode(AutomationMode::Read);
    panLane->setMode(AutomationMode::Read);
    play(e, 512);
    AutomationRecorder recorder;
    int passes = 0;
    std::vector<AutomationPoint> gainPoints;
    recorder.collect(e.getCaptureQueue(), [&](const LaneTarget& target, const std::vector<AutomationPoint>& points) {
        ++passes;
        if (target == gain)
            gainPoints = points;
    });
    std::printf("  back to Read: %d passes (expected 2), recording %s (expected no)\n", passes, recorder.isRecording(gain) ? "yes" : "no");
    DAWLAB_CHECK(passes == 2 && !recorder.isRecording(gain) && !recorder.isRecording(pan));
    DAWLAB_CHECK(gainPoints.size() == 2 && gainPoints.front().position == 0 && gainPoints.back().position == 1024);
}

void testTheHandMovesSlowerThanACurve() {
    Engine e;
    setUp(e);
    AutomationCurve curve;
    curve.add({0, 0.25f, Shape::Linear});
    e.setLaneCurve(gainOf(e), curve);
    AutomationLane* lane = e.getLane(gainOf(e));
    lane->setMode(AutomationMode::Touch);
    lane->setKnobValue(0.75f);
    e.getTransport().play();
    play(e, 512);
    lane->touch();
    play(e, 512);
    const float afterBlock = lane->getShownValue(); // 512 samples of 1/2400
    std::printf("  a hand that jumps 0.5: after one block %.4f (expected %.4f)\n", static_cast<double>(afterBlock), 0.25 + 512.0 / 2400.0);
    DAWLAB_CHECK_NEAR(afterBlock, 0.25f + 512.0f / 2400.0f, 1e-4);
}

void testStoppedFollowsThePlayhead() {
    Engine e;
    setUp(e);
    const AutomationCurve curve = ramp(0, 0.0f, 48000, 1.0f);
    e.setLaneCurve(gainOf(e), curve);
    e.applyPendingCommandsOffline();
    AudioBuffer block(kNumChannels, 512);
    e.getTransport().setPosition(24000);
    e.processBlock(block.toBlock());
    e.processBlock(block.toBlock());
    const float shown = e.getLane(gainOf(e))->getShownValue();
    std::printf("  stopped, seek to the middle of a ramp: lane at %.2f (expected 0.50)\n", static_cast<double>(shown));
    DAWLAB_CHECK(shown == 0.5f);
}

void testEventsAreStampedWithWhatIsHeard() {
    Engine e;
    setUp(e);
    e.addInsert(e.trackRef(kTrack), -1, std::make_unique<TestEffect>(100));
    e.applyPendingCommandsOffline();
    e.getLane(gainOf(e))->setMode(AutomationMode::Write);
    e.getTransport().setPosition(1000);
    e.getTransport().play();
    AudioBuffer block(kNumChannels, 512);
    e.processBlock(block.toBlock());
    CaptureEvent event;
    DAWLAB_CHECK(e.getCaptureQueue().pop(event));
    std::printf("  100 samples of latency, playing at 1000: the hand is stamped at %lld (expected 900)\n", static_cast<long long>(event.position));
    DAWLAB_CHECK(event.position == 900);
}

void testEffectsOnBusesAndTheMaster() {
    Engine e;
    setUp(e);
    Track* t = e.getTrack(kTrack);
    t->setSendPreFader(0, true);
    t->setSendLevelDb(0, 0.0f);
    auto busOwned = std::make_unique<TestEffect>();
    auto masterOwned = std::make_unique<TestEffect>();
    TestEffect* busFx = busOwned.get();
    TestEffect* masterFx = masterOwned.get();
    e.addInsert(ChannelRef::bus(0), -1, std::move(busOwned));
    e.addInsert(ChannelRef::master(), -1, std::move(masterOwned));
    e.setLaneCurve(LaneTarget{ChannelRef::bus(0), busFx->getEffectId(), 0}, ramp(0, 0.25f, 1000, 0.25f));
    e.setLaneCurve(LaneTarget{ChannelRef::master(), masterFx->getEffectId(), 0}, ramp(0, 0.5f, 1000, 0.5f));
    // A recording on a bus says which bus it is.
    e.getLane(LaneTarget{ChannelRef::bus(1), EffectId{}, kLaneGain})->setMode(AutomationMode::Write);
    e.getTransport().play();
    AudioBuffer block(kNumChannels, 512);
    e.processBlock(block.toBlock());
    e.processBlock(block.toBlock());
    CaptureEvent event;
    const bool got = e.getCaptureQueue().pop(event);
    DAWLAB_CHECK(busFx->applied() == 0.25f && masterFx->applied() == 0.5f);
    DAWLAB_CHECK(got && event.target.channel == ChannelRef::bus(1));
}

void testQueuesThatAreFull() {
    { // A command that does not fit still owns its curve (AddressSanitizer would see the leak).
        Engine e(4);
        setUp(e);
        int accepted = 0;
        for (int i = 0; i < 10; ++i)
            accepted += e.setLaneCurve(gainOf(e), ramp(0, 0.0f, 1000, 1.0f)) ? 1 : 0;
        DAWLAB_CHECK(accepted == 4 && e.getCommandsDropped() == 6);
    }
    { // The most one command can hand back: a track with all its inserts, clips and two curves.
        Engine e(1);
        setUp(e);
        AudioBuffer block(kNumChannels, 512);
        for (int i = 0; i < kMaxInserts; ++i) {
            e.addInsert(e.trackRef(kTrack), -1, std::make_unique<TestEffect>());
            e.processBlock(block.toBlock());
        }
        e.setLaneCurve(gainOf(e), ramp(0, 0.0f, 1000, 1.0f));
        e.processBlock(block.toBlock());
        e.setLaneCurve(LaneTarget{e.trackRef(kTrack), EffectId{}, kLanePan}, ramp(0, 0.0f, 1000, 1.0f));
        e.processBlock(block.toBlock());
        e.collectGarbage();
        e.removeTrack(kTrack);
        e.processBlock(block.toBlock());
        DAWLAB_CHECK(e.getGarbageLeaked() == 0);
        // Chapter 18: a track can also hand back a MIDI clip list and an instrument, which this one does not
        // have. test_midi.cpp removes one that has everything.
        DAWLAB_CHECK(e.collectGarbage() == kMaxGarbagePerCommand - 2);
    }
}

void testABouncePlaysWhatIsWritten() {
    Engine e;
    setUp(e);
    const AutomationCurve curve = ramp(0, 0.5f, 48000, 1.0f);
    e.setLaneCurve(gainOf(e), curve);
    AutomationLane* lane = e.getLane(gainOf(e));
    lane->setMode(AutomationMode::Write); // the hand would win, if this were not a bounce
    lane->setKnobValue(0.1f);
    e.applyPendingCommandsOffline();
    const AudioBuffer out = render(e, 12000);
    CaptureEvent event;
    const bool recorded = e.getCaptureQueue().pop(event);
    std::printf("  a bounce in Write mode: sample 6000 %.5f (the curve: %.5f), events: %s\n", static_cast<double>(out.getSample(0, 6000)),
                static_cast<double>(faderGain(curve.valueAt(6000))), recorded ? "yes" : "none");
    DAWLAB_CHECK(out.getSample(0, 6000) == faderGain(curve.valueAt(6000)));
    DAWLAB_CHECK(!recorded);
    // Afterwards the hand counts again.
    e.getTransport().play();
    play(e, 6 * 512);
    DAWLAB_CHECK(lane->getShownValue() == 0.1f);
    DAWLAB_CHECK(e.getCaptureQueue().pop(event));
}

void testTrackRemoval() {
    Engine e;
    setUp(e);
    const ChannelRef oldRef = e.trackRef(kTrack);
    const LaneTarget oldGain = gainOf(e);
    e.setLaneCurve(oldGain, ramp(0, 0.0f, 1000, 1.0f));
    e.setLaneCurve(LaneTarget{oldRef, EffectId{}, kLanePan}, ramp(0, 0.0f, 1000, 1.0f));
    e.getLane(oldGain)->setMode(AutomationMode::Write);
    e.getLane(oldGain)->touch();
    e.getTransport().play();
    play(e, 512);
    e.collectGarbage();
    e.removeTrack(kTrack);
    // Queued after the removal, for the id that is gone: nothing may land on the next track.
    DAWLAB_CHECK(!e.setLaneCurve(oldGain, ramp(0, 0.0f, 1000, 1.0f)));
    const TrackId next{2};
    const ChannelRef nextRef = e.addTrack(next);
    DAWLAB_CHECK(nextRef.index == oldRef.index); // the same slot
    const Command stale = Command::replaceCurve(oldRef, EffectId{}, kLaneGain, new AutomationCurve());
    e.pushCommand(stale);
    AudioBuffer block(kNumChannels, 512);
    e.processBlock(block.toBlock());
    const int freed = e.collectGarbage();
    AutomationLane* lane = e.getLane(LaneTarget{nextRef, EffectId{}, kLaneGain});
    std::printf("  a track with two curves removed: %d objects back (2 curves, a clip list, the stale curve); leaked %llu\n",
                freed, static_cast<unsigned long long>(e.getGarbageLeaked()));
    DAWLAB_CHECK(freed == 4 && e.getGarbageLeaked() == 0);
    DAWLAB_CHECK(lane != nullptr && !lane->isActive() && lane->getCurve() == nullptr && lane->getMode() == AutomationMode::Read);
    DAWLAB_CHECK(e.getLane(oldGain) == nullptr);
}

void testBounceTwiceIsTheSame() {
    Engine e;
    setUp(e);
    e.setLaneCurve(gainOf(e), ramp(0, 0.75f, 48000, 0.25f));
    e.applyPendingCommandsOffline();
    const AudioBuffer a = render(e, 12000);
    const AudioBuffer b = render(e, 12000);
    std::printf("  two bounces: biggest difference %g, first sample on the curve: %s\n", static_cast<double>(maxAbsDiff(a, b)),
                a.getSample(0, 0) == faderGain(0.75f) ? "yes" : "no");
    DAWLAB_CHECK(maxAbsDiff(a, b) == 0.0f);
    DAWLAB_CHECK(a.getSample(0, 0) == faderGain(0.75f)); // no ramp from where the last bounce ended
}

// ---- real time -------------------------------------------------------------------------------------------

void testNoAllocationOnTheAudioThread() {
    Engine e;
    setUp(e);
    InsertEffect* fx = e.addInsert(e.trackRef(kTrack), -1, std::make_unique<TestEffect>());
    const LaneTarget level{e.trackRef(kTrack), fx->getEffectId(), 0};
    e.applyPendingCommandsOffline();
    AutomationLane* gain = e.getLane(gainOf(e));
    gain->setMode(AutomationMode::Latch);
    e.getTransport().setLoopRange(100, 7000);
    e.getTransport().setLoopEnabled(true);
    e.getTransport().play();
    AudioBuffer block(kNumChannels, 512);
    AutomationRecorder recorder;
    long made = 0;
    for (int i = 0; i < 1000; ++i) {
        if (i % 50 == 0) { // the UI's part, not counted
            e.setLaneCurve(level, ramp(0, 0.25f, 5000, 0.75f));
            e.setLaneCurve(gainOf(e), ramp(0, 0.25f, 5000, 0.75f));
        }
        if (i % 50 == 10)
            gain->touch();
        if (i % 50 == 30)
            gain->release();
        if (i % 200 == 199)
            e.getTransport().stop();
        if (i % 200 == 0)
            e.getTransport().play();
        if (i % 77 == 0)
            e.getTransport().setPosition(i);
        gAllocs.store(0);
        tCounting = true;
        e.processBlock(block.toBlock());
        tCounting = false;
        made += gAllocs.load();
        e.collectGarbage();
        recorder.collect(e.getCaptureQueue(), [](const LaneTarget&, const std::vector<AutomationPoint>&) {});
    }
    std::printf("  1000 blocks with curves arriving, touches, stops, seeks, a loop: %ld allocations or frees (expected 0)\n", made);
    DAWLAB_CHECK(made == 0);
}

void testRealThreads() {
    Engine e;
    setUp(e);
    std::atomic<bool> running{true};
    std::thread audio([&] {
        AudioBuffer block(kNumChannels, kBlockSize);
        while (running.load())
            e.processBlock(block.toBlock());
    });
    const LaneTarget gain = gainOf(e);
    AutomationLane* lane = e.getLane(gain);
    lane->setMode(AutomationMode::Touch);
    AutomationRecorder recorder;
    AutomationCurve master;
    int passes = 0;
    e.getTransport().play();
    for (int i = 0; i < 3000; ++i) {
        // UI work a real session does: new curves, a hand on the fader, effects and tracks that come
        // and go with curves on them, the recorder, the garbage, what the screen shows.
        AutomationCurve curve = ramp(i % 1000, 0.25f, 5000 + i % 1000, 0.75f);
        e.setLaneCurve(gain, curve);
        if (i % 16 == 4)
            lane->touch();
        lane->setKnobValue(static_cast<float>(i % 100) / 100.0f);
        if (i % 16 == 12)
            lane->release();
        if (i % 40 == 0) {
            InsertEffect* fx = e.addInsert(e.trackRef(kTrack), -1, std::make_unique<TestEffect>());
            if (fx != nullptr) {
                const LaneTarget param{e.trackRef(kTrack), fx->getEffectId(), 0};
                e.setLaneCurve(param, curve);
                e.removeInsert(e.trackRef(kTrack), 0);
            }
        }
        if (i % 64 == 0) {
            const TrackId id{static_cast<std::uint32_t>(2 + i / 64)};
            if (e.addTrack(id).index >= 0) {
                e.setLaneCurve(gainOf(e, id), curve);
                e.removeTrack(id);
            }
        }
        if (i % 500 == 250)
            e.getTransport().setPosition(i);
        recorder.collect(e.getCaptureQueue(), [&](const LaneTarget& t, const std::vector<AutomationPoint>& points) {
            if (t == gain) {
                master = punch(master, points);
                ++passes;
            }
        });
        (void)e.getTrack(kTrack)->getShownGainDb();
        e.collectGarbage();
        if (i % 4 == 0) { // let the audio thread keep up: a block for every few UI rounds
            const uint64_t blocks = e.getBlocksProcessed();
            while (e.getBlocksProcessed() == blocks)
                std::this_thread::yield();
        }
    }
    running.store(false);
    audio.join();
    e.collectGarbage();
    std::printf("  3000 UI rounds next to a real audio thread: %llu blocks, %d passes recorded, %llu commands dropped, %llu leaked\n",
                static_cast<unsigned long long>(e.getBlocksProcessed()), passes, static_cast<unsigned long long>(e.getCommandsDropped()),
                static_cast<unsigned long long>(e.getGarbageLeaked()));
    DAWLAB_CHECK(e.getBlocksProcessed() > 0);
    DAWLAB_CHECK(e.getGarbageLeaked() == 0);
}

} // namespace

int main() {
    std::printf("Automation in the engine\n");
    testFaderFollowsCurveEverySample();
    testJumpLandsOnItsSampleAsAShortRamp();
    testTheRampFollowsTheSampleRate();
    testBlockSizeChangesNothing();
    testPanAndTheOtherStrips();
    testCurveArrivesAndLeavesWithoutAClick();
    testShownValue();
    testEffectParameters();
    testFaderWaitsForDelayedAudio();
    testBusFaderWaitsToo();
    testModes();
    testRecordAndPlayBack();
    testTheFirstRecordingHasNoCurveYet();
    testTheHandMovesSlowerThanACurve();
    testStoppedFollowsThePlayhead();
    testEventsAreStampedWithWhatIsHeard();
    testEffectsOnBusesAndTheMaster();
    testQueuesThatAreFull();
    testABouncePlaysWhatIsWritten();
    testTrackRemoval();
    testBounceTwiceIsTheSame();
    testNoAllocationOnTheAudioThread();
    testRealThreads();
    return dawlab::test::summary("l16_test_automation");
}
