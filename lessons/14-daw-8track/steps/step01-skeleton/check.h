#pragma once
// check.h - the self-check of Step 1. Defines main(); included at the END of
// starter/step01.cpp and solution/step01.cpp, so both run exactly the same checks.
//
// It only tests what Step 1 adds: processBlock() cuts any block into chunks, and processChunk()
// plays the source sample-accurately.

#include <dawlab/Common.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace step01_check {

using namespace dawlab;
using namespace minidaw;

constexpr double kSampleRate = 48000.0;

// Plays `total` samples with fixed-size callbacks of `blockSize`; returns the number of calls.
inline int renderWithBlocks(Engine& engine, AudioBuffer& out, int blockSize) {
    int calls = 0;
    const int total = out.getNumSamples();
    for (int pos = 0; pos < total; pos += blockSize) {
        engine.processBlock(out.toBlock().getSubBlock(pos, std::min(blockSize, total - pos)));
        ++calls;
    }
    return calls;
}

inline float maxDifference(const AudioBuffer& a, const AudioBuffer& b) {
    float d = 0.0f;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            d = std::max(d, std::abs(a.getSample(ch, i) - b.getSample(ch, i)));
    return d;
}

inline int run() {
    std::printf("Step 1: the engine skeleton (build-along)\n");
    auto sine = std::make_shared<const AudioBuffer>(makeSine(440.0, kSampleRate, 2.0, 0.5f, 1)); // mono
    const int total = secondsToSamples(2.5, kSampleRate);

    // 1) Before prepare(): silence, even if the buffer contained something.
    {
        Engine engine;
        AudioBuffer out(kNumChannels, 64);
        for (int i = 0; i < 64; ++i)
            out.setSample(0, i, 1.0f);
        engine.processBlock(out.toBlock());
        std::printf("  unprepared engine output peak : %.4f (expected 0)\n", peakAbs(out));
        DAWLAB_CHECK(peakAbs(out) == 0.0f);
    }

    // 2) The device loop: 512-sample callbacks, 2 s of sine then 0.5 s of silence.
    Engine engine;
    engine.prepare(kSampleRate, 512);
    engine.setSource(sine);
    AudioBuffer out512(kNumChannels, total);
    const int calls = renderWithBlocks(engine, out512, 512);
    float worst = 0.0f; // output vs source, both channels
    for (int ch = 0; ch < kNumChannels; ++ch)
        for (int i = 0; i < sine->getNumSamples(); ++i)
            worst = std::max(worst, std::abs(out512.getSample(ch, i) - sine->getSample(0, i)));
    float tail = 0.0f;
    for (int ch = 0; ch < kNumChannels; ++ch)
        for (int i = sine->getNumSamples(); i < total; ++i)
            tail = std::max(tail, std::abs(out512.getSample(ch, i)));
    std::printf("  processBlock() calls          : %d (expected 235)\n", calls);
    std::printf("  output peak                   : %.4f (expected 0.5000: no mixer yet, no pan law)\n",
                peakAbs(out512));
    std::printf("  max |output - source|         : %g (expected 0, both channels)\n", worst);
    std::printf("  peak after the source ends    : %g (expected 0)\n", tail);
    std::printf("  position at the end           : %lld samples (expected %d)\n",
                static_cast<long long>(engine.getPosition()), total);
    DAWLAB_CHECK(calls == 235);
    DAWLAB_CHECK_NEAR(peakAbs(out512), 0.5, 1e-4);
    DAWLAB_CHECK(worst == 0.0f);
    DAWLAB_CHECK(tail == 0.0f);
    DAWLAB_CHECK(engine.getPosition() == total);
    DAWLAB_CHECK(engine.getChunksProcessed() == 235);

    // 3) One huge callback (the whole 2.5 s at once): processBlock() must cut it into chunks of
    //    at most 512 samples, and the result must be identical.
    Engine big;
    big.prepare(kSampleRate, 512);
    big.setSource(sine);
    AudioBuffer outBig(kNumChannels, total);
    big.processBlock(outBig.toBlock());
    std::printf("  one %d-sample callback    : %lld chunks (expected 235), max difference to the 512 render %g\n",
                total, static_cast<long long>(big.getChunksProcessed()), maxDifference(out512, outBig));
    DAWLAB_CHECK(big.getChunksProcessed() == 235);
    DAWLAB_CHECK(maxDifference(out512, outBig) == 0.0f);

    // 4) Odd callback sizes (441, 1000, 7) give the same audio, too.
    for (int bs : {441, 1000, 7}) {
        Engine e;
        e.prepare(kSampleRate, 512);
        e.setSource(sine);
        AudioBuffer o(kNumChannels, total);
        renderWithBlocks(e, o, bs);
        DAWLAB_CHECK(maxDifference(out512, o) == 0.0f);
        DAWLAB_CHECK(e.getPosition() == total);
    }

    // 5) A mono output block gets channel 0.
    {
        Engine e;
        e.prepare(kSampleRate, 512);
        e.setSource(sine);
        AudioBuffer mono(1, 100);
        e.processBlock(mono.toBlock());
        DAWLAB_CHECK(mono.getSample(0, 99) == sine->getSample(0, 99));
    }

    if (writeWav(outputPath("l14_step01.wav"), out512, kSampleRate))
        std::printf("  wrote dawlab_output/l14_step01.wav\n");
    return dawlab::test::summary("l14_step01");
}

} // namespace step01_check

int main() { return step01_check::run(); }
