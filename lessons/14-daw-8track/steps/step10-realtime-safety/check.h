#pragma once
// check.h - the self-check of Step 10 (real-time safety audit). Defines main(); included at the
// END of starter/step10.cpp and solution/step10.cpp.
//
//   Audit 1  the allocation counter itself: counts this thread, ignores other threads
//   Audit 2  the whole mini engine on an "audio thread" with random block sizes while a "UI
//            thread" edits everything: 0 allocations and 0 frees inside processBlock(), no NaN
//   Audit 3  block-size invariance: 512 vs 64 vs random sizes 1..1500 -> identical output

#include <dawlab/Common.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <thread>
#include <vector>

namespace step10_check {

using namespace dawlab;
using namespace minidaw;

constexpr double kSampleRate = 48000.0;

// An 8-track project that uses everything Steps 1-9 built: clips with fades, inserts on tracks,
// buses and master (Gain, Compressor, PureDelay = latency), sends, PDC and a loop.
inline void buildProject(Engine& engine) {
    engine.prepare(kSampleRate, 512);
    auto drums = std::make_shared<const AudioBuffer>(makeDrumLoop(kSampleRate, 1, 120.0, 0.6f));
    auto pluck = std::make_shared<const AudioBuffer>(makePluck(220.0, kSampleRate, 0.5, 0.4f));
    for (int t = 0; t < kNumTracks; ++t) {
        Clip c = Clip::fromBuffer(t % 2 == 0 ? drums : pluck, 777 + 3001 * t); // odd start positions
        c.fadeInSamples = 64;
        c.fadeOutSamples = 480;
        c.gainDb = -3.0f;
        engine.setTrackClips(t, {c});
        engine.addInsert(ChannelRef::track(t), -1, createEffect(t % 3 == 0 ? "Compressor" : "Gain"));
        auto d = createEffect("PureDelay");
        d->setParameter(0, static_cast<float>(13 * t));
        engine.addInsert(ChannelRef::track(t), -1, std::move(d));
        engine.getMixer().getTrack(t).setPan(-0.8f + 0.2f * static_cast<float>(t));
        engine.getMixer().getTrack(t).setSendLevelDb(t % 2, -6.0f);
        engine.getMixer().getTrack(t).setSendPreFader(1, t == 3);
    }
    auto busDelay = createEffect("PureDelay");
    busDelay->setParameter(0, 100.0f);
    engine.addInsert(ChannelRef::bus(0), -1, std::move(busDelay));
    engine.addInsert(ChannelRef::bus(1), -1, createEffect("Compressor"));
    engine.addInsert(ChannelRef::master(), -1, createEffect("Compressor"));
    engine.getTransport().setLoopRange(0, 50000);
    engine.getTransport().setLoopEnabled(true);
}

inline float maxDifference(const AudioBuffer& a, const AudioBuffer& b) {
    if (a.getNumSamples() != b.getNumSamples() || a.getNumSamples() == 0)
        return 1.0f; // nothing rendered (starter) or different lengths
    float d = 0.0f;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            d = std::max(d, std::abs(a.getSample(ch, i) - b.getSample(ch, i)));
    return d;
}

inline int run() {
    std::printf("Step 10: real-time safety audit (build-along)\n");

    // ---- Audit 1: does the counter work, and only for this thread? ------------------------------------
    audit::resetCounters();
    {
        audit::CountScope scope;
        void* p = ::operator new(16); // explicit calls: a compiler may not optimise them away
        ::operator delete(p);
        void* a = ::operator new[](64); // the array forms count too
        ::operator delete[](a);
    }
    const long long sanityAllocs = audit::counters().allocations.load();
    const long long sanityFrees = audit::counters().frees.load();
    auto* outside = new int(6); // not counted: counting is off
    delete outside;
    std::printf("  counter: new/delete + new[]/delete[] inside a CountScope -> %lld allocations, %lld frees (expected 2, 2)\n",
                sanityAllocs, sanityFrees);
    DAWLAB_CHECK(sanityAllocs == 2 && sanityFrees == 2);
    DAWLAB_CHECK(audit::counters().allocations.load() == 2);
    {
        audit::resetCounters();
        std::atomic<bool> go{false};
        std::thread other([&] {
            while (!go.load())
                std::this_thread::yield();
            for (int i = 0; i < 1000; ++i)
                delete new int(i);
        });
        {
            audit::CountScope scope; // this thread counts; the other thread allocates meanwhile
            go.store(true);
            other.join();
        }
        std::printf("  another thread allocating 1000 times meanwhile: %lld counted (expected 0)\n",
                    audit::counters().allocations.load());
        DAWLAB_CHECK(audit::counters().allocations.load() == 0);
    }

    // ---- Audit 2: the engine under fire ------------------------------------------------------------
    {
        Engine engine;
        buildProject(engine);
        engine.applyPendingCommandsOffline();
        engine.getTransport().play();
        audit::resetCounters();
        std::atomic<bool> stop{false};
        std::atomic<long long> samples{0}, nanBlocks{0};
        std::thread audioThread([&] {
            AudioBuffer buffer(kNumChannels, 2048); // allocated before the loop
            std::mt19937 rng(11);
            std::uniform_int_distribution<int> size(1, 2048);
            while (!stop.load() || samples.load() < 3 * 48000) {
                const int n = size(rng);
                AudioBlock block = buffer.toBlock().getSubBlock(0, n);
                {
                    audit::CountScope scope;
                    engine.processBlock(block);
                }
                bool bad = false;
                for (int ch = 0; ch < kNumChannels; ++ch)
                    for (int i = 0; i < n; ++i)
                        bad = bad || !std::isfinite(block.getChannelPointer(ch)[i]);
                if (bad)
                    ++nanBlocks;
                samples += n;
            }
        });
        std::mt19937 rng(5);
        auto rnd = [&](float lo, float hi) { return lo + (hi - lo) * static_cast<float>(rng() % 10001) / 10000.0f; };
        int uiActions = 0;
        for (; uiActions < 400; ++uiActions) { // the busy user (allocations here are fine: not counted)
            const int t = static_cast<int>(rng() % kNumTracks);
            Track& track = engine.getMixer().getTrack(t);
            switch (rng() % 9) {
            case 0: track.setGainDb(rnd(-40.0f, 6.0f)); break;
            case 1: track.setPan(rnd(-1.0f, 1.0f)); break;
            case 2: track.setMute(rng() % 4 == 0); break;
            case 3: track.setSolo(rng() % 6 == 0); break;
            case 4: track.setSendLevelDb(static_cast<int>(rng() % kNumBuses), rnd(-30.0f, 0.0f)); break;
            case 5: engine.addInsert(ChannelRef::track(t), -1, createEffect(rng() % 2 ? "Gain" : "Compressor")); break;
            case 6: engine.removeInsert(ChannelRef::track(t), 0); break;
            case 7: {
                Clip c = Clip::fromBuffer(std::make_shared<const AudioBuffer>(makeSine(rnd(100.0f, 900.0f), kSampleRate, 0.3, 0.2f, 1)),
                                          static_cast<int64_t>(rng() % 40000));
                c.fadeOutSamples = 100;
                engine.setTrackClips(t, {c});
                break;
            }
            default: engine.getTransport().setPosition(static_cast<int64_t>(rng() % 50000)); break;
            }
            engine.collectGarbage();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        stop.store(true);
        audioThread.join();
        engine.collectGarbage();
        std::printf("  audio thread: %lld samples, random blocks 1..2048; UI thread: %d actions, %llu commands applied\n",
                    samples.load(), uiActions, static_cast<unsigned long long>(engine.getCommandsApplied()));
        std::printf("  allocations + frees inside processBlock(): %lld + %lld (expected 0), blocks with NaN/inf: %lld\n",
                    audit::counters().allocations.load(), audit::counters().frees.load(), nanBlocks.load());
        DAWLAB_CHECK(sanityAllocs == 2); // the zeros below only mean something if the counter works
        DAWLAB_CHECK(audit::counters().allocations.load() == 0 && audit::counters().frees.load() == 0);
        DAWLAB_CHECK(nanBlocks.load() == 0);
        DAWLAB_CHECK(engine.getGarbageLeaked() == 0);
    }

    // ---- Audit 3: block-size invariance --------------------------------------------------------------
    {
        const int total = 3 * 48000;
        std::mt19937 rng(17);
        std::vector<int> randomSizes(4000);
        for (int& s : randomSizes)
            s = 1 + static_cast<int>(rng() % 1500);
        Engine e512, e64, eRandom;
        buildProject(e512);
        buildProject(e64);
        buildProject(eRandom);
        const AudioBuffer a = audit::renderWithBlockSizes(e512, total, {512});
        const AudioBuffer b = audit::renderWithBlockSizes(e64, total, {64});
        const AudioBuffer c = audit::renderWithBlockSizes(eRandom, total, randomSizes);
        std::printf("  max |difference| 512 vs 64: %g, 512 vs random 1..1500: %g (expected 0), peak %.3f\n",
                    maxDifference(a, b), maxDifference(a, c), a.getNumSamples() > 0 ? peakAbs(a) : 0.0f);
        DAWLAB_CHECK(a.getNumSamples() == total);
        DAWLAB_CHECK(maxDifference(a, b) < 1e-5f);
        DAWLAB_CHECK(maxDifference(a, c) < 1e-5f);
        DAWLAB_CHECK(a.getNumSamples() > 0 && peakAbs(a) > 0.01f); // the project is not silent
    }

    return dawlab::test::summary("l14_step10");
}

} // namespace step10_check

int main() { return step10_check::run(); }
