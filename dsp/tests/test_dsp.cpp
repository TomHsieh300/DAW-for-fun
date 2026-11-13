// Self-test of rules that every effect in dsp/ shares, whichever chapter brought it in:
//   - process() before prepare() leaves the block as it is (Delay and Distortion already did): a host may call an
//     effect it has not set up yet, and a delay line of size 0 must not be read
//   - a sidechain shorter than the main block: only the samples both have are processed (Gate and Expander already
//     did), the rest of the main block is left as it is
//   - a non-finite input sample (NaN, +-inf) is never kept: no filter state, delay line, FFT buffer or meter holds on
//     to it, so the output is finite again from the next block on and soon sounds as if the bad sample had been 0
//     (Biquad, the envelope followers and Compressor already did). The bad sample may pass once through a dry path.
// The Limiter is the one exception to the first rule on purpose: before prepare() it clears the block (and asserts in
// a Debug build), because a limiter that cannot look ahead cannot keep its ceiling.
// Every check is table-driven: one row per class (and per variant that takes a different path). The objects live on
// the heap, so the test's stack stays small in a Windows Debug build.
#include <dawlab/Common.h>
#include <dawlab/dsp/AmpChain.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/Chorus.h>
#include <dawlab/dsp/Compressor.h>
#include <dawlab/dsp/ConvolutionReverb.h>
#include <dawlab/dsp/DeEsser.h>
#include <dawlab/dsp/Delay.h>
#include <dawlab/dsp/Distortion.h>
#include <dawlab/dsp/EnvelopeFollower.h>
#include <dawlab/dsp/Expander.h>
#include <dawlab/dsp/Flanger.h>
#include <dawlab/dsp/Gain.h>
#include <dawlab/dsp/Gate.h>
#include <dawlab/dsp/LevelMeter.h>
#include <dawlab/dsp/Limiter.h>
#include <dawlab/dsp/LoudnessMeter.h>
#include <dawlab/dsp/OnePole.h>
#include <dawlab/dsp/Panner.h>
#include <dawlab/dsp/ParametricEq.h>
#include <dawlab/dsp/Phaser.h>
#include <dawlab/dsp/Reverb.h>
#include <dawlab/dsp/ReverbBuildingBlocks.h>
#include <dawlab/dsp/Saturation.h>
#include <dawlab/dsp/SpectrumAnalyzer.h>
#include <dawlab/dsp/StateVariableFilter.h>
#include <dawlab/dsp/TransientShaper.h>
#include <dawlab/dsp/Tremolo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

// Two channels of a constant, left +0.5 and right -0.5
AudioBuffer constantBlock(int numSamples) {
    AudioBuffer b(2, numSamples);
    for (int i = 0; i < numSamples; ++i) {
        b.getWritePointer(0)[i] = 0.5f;
        b.getWritePointer(1)[i] = -0.5f;
    }
    return b;
}

// Both channels: a 440 Hz sine at 0.25 (-12 dBFS) whose phase continues from sample `start`
void fillSine(AudioBuffer& b, long start) {
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample(ch, i, 0.25f * static_cast<float>(std::sin(kTwoPi * 440.0 * static_cast<double>(start + i) /
                                                                     kSampleRate)));
}

bool untouchedFrom(const AudioBuffer& b, int first) {
    for (int i = first; i < b.getNumSamples(); ++i)
        if (b.getSample(0, i) != 0.5f || b.getSample(1, i) != -0.5f)
            return false;
    return true;
}

template <typename Fx>
void checkUnpreparedBlock(const char* name) {
    Fx fx;
    AudioBuffer b = constantBlock(64);
    fx.process(AudioBlock(b));
    const bool same = untouchedFrom(b, 0);
    std::printf("%-8s process() before prepare(): block %s\n", name, same ? "unchanged" : "CHANGED");
    DAWLAB_CHECK(same);
}

template <typename Fx>
void checkUnpreparedSample(const char* name) {
    Fx fx;
    const float y = fx.processSample(0, 0.5f);
    std::printf("%-8s processSample() before prepare(): %g\n", name, static_cast<double>(y));
    DAWLAB_CHECK(y == 0.5f);
}

// The main block has 512 samples and the sidechain 256 (loud): the samples past the sidechain's end are not
// touched (and nothing is read past the sidechain's end - ASan would say so)
template <typename Fx>
void checkShortSidechain(const char* name, Fx& fx) {
    AudioBuffer main = constantBlock(512);
    AudioBuffer side(1, 256);
    for (int i = 0; i < 256; ++i)
        side.getWritePointer(0)[i] = 1.0f;
    fx.process(AudioBlock(main), AudioBlock(side));
    const bool tail = untouchedFrom(main, 256);
    std::printf("%-10s sidechain of 256 for a block of 512: samples 256.. %s\n", name, tail ? "unchanged" : "CHANGED");
    DAWLAB_CHECK(tail);
}

// ---- Adapters: building blocks and meters behind the process(AudioBlock) interface the checks use ----------------

// Two mono building blocks (left and right) that process one sample at a time
template <typename Block>
struct StereoPair {
    Block side[2];
    void process(AudioBlock b) noexcept {
        for (int ch = 0; ch < 2; ++ch) {
            float* d = b.getChannelPointer(ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
                d[i] = side[ch].processSample(d[i]);
        }
    }
};

// A meter measures the block and then writes its readings into it (dB / 100, so about -1..0): a reading that is stuck
// or not finite then shows up like any other output
struct LevelMeterRig {
    LevelMeter meter;
    void process(AudioBlock b) noexcept {
        meter.process(b);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i) {
                const float db = i % 3 == 0 ? meter.getPeakDb(ch) : i % 3 == 1 ? meter.getRmsDb(ch) : meter.getPeakHoldDb(ch);
                b.getChannelPointer(ch)[i] = 0.01f * db;
            }
    }
};

struct LoudnessMeterRig {
    LoudnessMeter meter;
    void process(AudioBlock b) noexcept {
        meter.process(b);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.getChannelPointer(ch)[i] = 0.01f * (ch == 0 ? meter.getMomentaryLufs() : meter.getShortTermLufs());
    }
};

// The largest bin of the newest spectrum
struct SpectrumRig {
    SpectrumAnalyzer analyzer;
    std::vector<float> bins;
    float largestDb = kMinusInfinityDb;
    void process(AudioBlock b) {
        analyzer.pushBlock(b);
        if (analyzer.computeSpectrum(bins))
            largestDb = *std::max_element(bins.begin(), bins.end());
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.getChannelPointer(ch)[i] = 0.01f * largestDb;
    }
};

// Distortion in HardClip (the type hides a poisoned DC blocker), switched to Fuzz at block 50
struct HardClipThenFuzz {
    Distortion fx;
    int blocks = 0;
    void process(AudioBlock b) noexcept {
        if (++blocks == 50)
            fx.setType(DistortionType::Fuzz);
        fx.process(b);
    }
};

// ---- Rule: a non-finite input sample is never kept -----------------------------------------------------------------

struct InputRun {
    long nonFiniteAfter = 0; // non-finite output samples from the block after the bad one on
    double rms[2] = {0.0, 0.0}; // of the last 0.5 s
};

// About 2.5 s of the sine in blocks of 512; in block 20 sample 100 of channel 0 is `bad` (if inject)
template <typename Fx>
InputRun renderWithBadSample(Fx& fx, bool inject, float bad) {
    constexpr int kBadBlock = 20, kBlocks = 235, kTailBlocks = 47;
    AudioBuffer buf(2, kBlock);
    InputRun r;
    long count = 0;
    for (int blk = 0; blk < kBlocks; ++blk) {
        fillSine(buf, static_cast<long>(blk) * kBlock);
        if (inject && blk == kBadBlock)
            buf.setSample(0, 100, bad);
        fx.process(AudioBlock(buf));
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i) {
                const float v = buf.getSample(ch, i);
                if (blk > kBadBlock && !std::isfinite(v))
                    ++r.nonFiniteAfter;
                else if (blk >= kBlocks - kTailBlocks)
                    r.rms[ch] += static_cast<double>(v) * v;
            }
        if (blk >= kBlocks - kTailBlocks)
            ++count;
    }
    for (double& v : r.rms)
        v = std::sqrt(v / static_cast<double>(count * kBlock));
    return r;
}

int inputRows = 0, inputKept = 0;

// make() returns a std::unique_ptr to a prepared object. Twin objects get the same input, one of them with a NaN (then
// with a +inf) in block 20: from block 21 on its output is finite, and its last 0.5 s has the level of the clean twin
// (within 1%), so nothing - no filter state, delay line or buffer - kept the bad value (or got stuck at silence).
template <typename Make>
void checkNonFiniteInputNotKept(const char* name, Make make) {
    const auto clean = make();
    const InputRun ref = renderWithBadSample(*clean, false, 0.0f);
    for (float bad : {kNaN, kInf}) {
        const auto fx = make();
        const InputRun r = renderWithBadSample(*fx, true, bad);
        bool ok = r.nonFiniteAfter == 0;
        for (int ch = 0; ch < 2; ++ch)
            ok = ok && std::abs(r.rms[ch] - ref.rms[ch]) <= 0.01 * ref.rms[ch] + 1e-9;
        ++inputRows;
        if (!ok) {
            ++inputKept;
            std::printf("  %-26s one %s input: KEPT (non-finite after: %ld, last 0.5 s rms %.4f %.4f, clean twin %.4f "
                        "%.4f)\n",
                        name, std::isnan(bad) ? "NaN" : "+inf", r.nonFiniteAfter, r.rms[0], r.rms[1], ref.rms[0],
                        ref.rms[1]);
        }
        DAWLAB_CHECK(ok);
    }
}

// Shorthand for the usual row: an effect with the standard prepare(), then `setup` on it
template <typename Fx, typename Setup>
auto prepared(Setup setup) {
    return [setup] {
        auto fx = std::make_unique<Fx>();
        fx->prepare(kSampleRate, kBlock, 2);
        setup(*fx);
        return fx;
    };
}

template <typename Fx>
auto prepared() {
    return prepared<Fx>([](Fx&) {});
}

void checkInputRule() {
    checkNonFiniteInputNotKept("Gain", prepared<Gain>([](Gain& g) { g.setGainDb(-6.0f); }));
    checkNonFiniteInputNotKept("Panner", prepared<Panner>([](Panner& p) { p.setPan(0.3f); }));
    checkNonFiniteInputNotKept("OnePole lowpass", prepared<OnePole>([](OnePole& f) { f.setCutoff(2000.0f); }));
    checkNonFiniteInputNotKept("OnePole highpass", prepared<OnePole>([](OnePole& f) {
                                   f.setType(OnePole::Type::Highpass);
                                   f.setCutoff(200.0f);
                               }));
    checkNonFiniteInputNotKept("Biquad", prepared<Biquad>([](Biquad& b) {
                                   b.setCoefficients(BiquadCoefficients::makeLowpass(kSampleRate, 2000.0, 0.7071));
                               }));
    checkNonFiniteInputNotKept("StateVariableFilter", prepared<StateVariableFilter>([](StateVariableFilter& f) {
                                   f.setCutoff(2000.0f);
                                   f.setQ(2.0f);
                               }));
    checkNonFiniteInputNotKept("ParametricEq", prepared<ParametricEq>([](ParametricEq& eq) {
                                   eq.setBandEnabled(2, true);
                                   eq.setBandGainDb(2, 6.0f);
                               }));
    checkNonFiniteInputNotKept("Compressor peak", prepared<Compressor>([](Compressor& c) {
                                   c.setThresholdDb(-30.0f);
                                   c.setRatio(4.0f);
                               }));
    checkNonFiniteInputNotKept("Compressor rms", prepared<Compressor>([](Compressor& c) {
                                   c.setThresholdDb(-30.0f);
                                   c.setRatio(4.0f);
                                   c.setDetectorMode(DetectorMode::Rms);
                               }));
    checkNonFiniteInputNotKept("Limiter", prepared<Limiter>([](Limiter& l) { l.setInputGainDb(12.0f); }));
    checkNonFiniteInputNotKept("Saturation", prepared<Saturation>([](Saturation& s) { s.setDriveDb(12.0f); }));
    for (DistortionType type : {DistortionType::HardClip, DistortionType::Foldback, DistortionType::Fuzz,
                                DistortionType::Rectify, DistortionType::Bitcrush}) {
        static const char* const names[] = {"Distortion HardClip", "Distortion Foldback", "Distortion Fuzz",
                                            "Distortion Rectify", "Distortion Bitcrush"};
        checkNonFiniteInputNotKept(names[static_cast<int>(type)], prepared<Distortion>([type](Distortion& d) {
                                       d.setType(type);
                                       d.setDriveDb(12.0f);
                                   }));
    }
    checkNonFiniteInputNotKept("Distortion HardClip, Fuzz", [] {
        auto r = std::make_unique<HardClipThenFuzz>();
        r->fx.prepare(kSampleRate, kBlock, 2);
        r->fx.setDriveDb(12.0f);
        return r;
    });
    checkNonFiniteInputNotKept("AmpChain", prepared<AmpChain>());
    checkNonFiniteInputNotKept("Gate", prepared<Gate>([](Gate& g) { g.setThresholdDb(-30.0f); }));
    checkNonFiniteInputNotKept("Expander", prepared<Expander>([](Expander& e) { e.setThresholdDb(-30.0f); }));
    checkNonFiniteInputNotKept("DeEsser", prepared<DeEsser>([](DeEsser& d) { d.setThresholdDb(-40.0f); }));
    checkNonFiniteInputNotKept("TransientShaper", prepared<TransientShaper>([](TransientShaper& t) {
                                   t.setAttackDb(6.0f);
                                   t.setSustainDb(-6.0f);
                               }));
    checkNonFiniteInputNotKept("Delay feedback 0", prepared<Delay>([](Delay& d) {
                                   d.setDelayMs(100.0f);
                                   d.setFeedback(0.0f);
                                   d.setMix(0.5f);
                               }));
    checkNonFiniteInputNotKept("Delay", prepared<Delay>());
    checkNonFiniteInputNotKept("Delay ping-pong", prepared<Delay>([](Delay& d) { d.setPingPong(true); }));
    checkNonFiniteInputNotKept("Chorus", prepared<Chorus>());
    checkNonFiniteInputNotKept("Flanger", prepared<Flanger>());
    checkNonFiniteInputNotKept("Phaser", prepared<Phaser>());
    checkNonFiniteInputNotKept("Tremolo", prepared<Tremolo>());
    checkNonFiniteInputNotKept("Reverb", prepared<Reverb>());
    checkNonFiniteInputNotKept("ConvolutionReverb", prepared<ConvolutionReverb>([](ConvolutionReverb& c) {
                                   c.setImpulseResponse(ConvolutionReverb::makeSyntheticImpulseResponse(kSampleRate, 0.5, 2));
                                   c.setMix(0.5f);
                               }));
    checkNonFiniteInputNotKept("CombFilter", [] {
        auto p = std::make_unique<StereoPair<CombFilter>>();
        for (CombFilter& c : p->side) {
            c.prepare(4800);
            c.setDelaySamples(1500);
            c.setFeedback(0.7f);
            c.setDamping(0.3f);
        }
        return p;
    });
    checkNonFiniteInputNotKept("AllpassDiffuser", [] {
        auto p = std::make_unique<StereoPair<AllpassDiffuser>>();
        for (AllpassDiffuser& a : p->side) {
            a.prepare(4800);
            a.setDelaySamples(1500);
            a.setGain(0.6f);
        }
        return p;
    });
    checkNonFiniteInputNotKept("TappedDelay", [] {
        auto p = std::make_unique<StereoPair<TappedDelay>>();
        for (TappedDelay& t : p->side) {
            t.prepare(4800);
            t.setTaps({{0, 1.0f}, {700, 0.5f}, {2300, 0.25f}});
        }
        return p;
    });
    checkNonFiniteInputNotKept("EnvelopeFollower", [] {
        auto p = std::make_unique<StereoPair<EnvelopeFollower>>();
        for (EnvelopeFollower& e : p->side)
            e.prepare(kSampleRate);
        return p;
    });
    checkNonFiniteInputNotKept("LevelMeter", [] {
        auto r = std::make_unique<LevelMeterRig>();
        r->meter.prepare(kSampleRate, 2);
        return r;
    });
    checkNonFiniteInputNotKept("LoudnessMeter", [] {
        auto r = std::make_unique<LoudnessMeterRig>();
        r->meter.prepare(kSampleRate, 2);
        return r;
    });
    checkNonFiniteInputNotKept("SpectrumAnalyzer", [] {
        auto r = std::make_unique<SpectrumRig>();
        r->analyzer.prepare(kSampleRate);
        return r;
    });
    std::printf("non-finite input: %d cases, %d kept the bad value\n", inputRows, inputKept);
}

} // namespace

int main() {
    checkUnpreparedBlock<Chorus>("Chorus");
    checkUnpreparedBlock<Flanger>("Flanger");
    checkUnpreparedBlock<Phaser>("Phaser");
    checkUnpreparedBlock<Reverb>("Reverb");
    checkUnpreparedSample<Chorus>("Chorus");
    checkUnpreparedSample<Flanger>("Flanger");
    checkUnpreparedSample<Phaser>("Phaser");
    {
        Reverb reverb;
        float wetL = 1.0f, wetR = 1.0f;
        reverb.processSampleStereo(0.5f, -0.5f, wetL, wetR);
        std::printf("Reverb   processSampleStereo() before prepare(): wet %g %g\n", static_cast<double>(wetL),
                    static_cast<double>(wetR));
        DAWLAB_CHECK(wetL == 0.0f && wetR == 0.0f);
    }

    for (bool link : {true, false}) {
        Compressor comp;
        comp.prepare(kSampleRate, 512, 2);
        comp.setStereoLink(link);
        checkShortSidechain(link ? "Comp link" : "Comp", comp);
    }
    Gate gate;
    gate.prepare(kSampleRate, 512, 2);
    checkShortSidechain("Gate", gate);
    Expander expander;
    expander.prepare(kSampleRate, 512, 2);
    checkShortSidechain("Expander", expander);

    checkInputRule();

    return dawlab::test::summary("dsp");
}
