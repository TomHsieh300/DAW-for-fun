// Self-test of rules that every effect in dsp/ shares, whichever chapter brought it in:
//   - process() before prepare() leaves the block as it is (Delay and Distortion already did): a host may call an
//     effect it has not set up yet, and a delay line of size 0 must not be read
//   - a sidechain shorter than the main block: only the samples both have are processed (Gate and Expander already
//     did), the rest of the main block is left as it is
//   - a non-finite input sample (NaN, +-inf) is never kept: no filter state, delay line, FFT buffer or meter holds on
//     to it, so the output is finite again from the next block on and soon sounds as if the bad sample had been 0
//     (Biquad, the envelope followers and Compressor already did). The bad sample may pass once through a dry path.
//   - a public setter ignores a non-finite value and keeps its last valid value; -inf given to a value in dB means
//     the floor kMinusInfinityDb (acceptParameter in DspMath.h). One row per public float / double setter, in a
//     configuration where a valid value changes the output. A stateless function maps a non-finite argument to a
//     documented default (finiteOr).
//   - the range of Gate, Expander and DeEsser is capped at 100 dB (acceptRangeDb): a larger one acts exactly like it
//   - process() costs about the same in every block: LevelMeter's RMS reads bit-identical to the exact re-sum, and no
//     block pays for the whole window (judged by wall-clock time only in an optimized build without sanitizers)
//   - setting a parameter to the value it already has changes nothing: once or before every block, same output
//   - a one-pole smoother of a gain, mix, length, delay or frequency arrives at its target exactly (smoothToward in
//     DspMath.h): it never parks at a subnormal number (toward 0) or a hair short of any other target
//   - a modulated sweep never rests on its safety clamp: the depth is limited where the sweep is used, after the
//     smoothers, and the clamp is only a rail (Chorus: depth at most centre - the minimum delay; Phaser: octaves at
//     most the room between the centre and the nearer end of [20 Hz, 0.45 fs])
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
#include <dawlab/dsp/Fft.h>
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
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
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

// ---- Rule: a public setter ignores a non-finite value ---------------------------------------------------------------

// Every class's standard prepare(); the rigs below have the same signature
template <typename Fx>
void prepareFx(Fx& fx) {
    fx.prepare(kSampleRate, kBlock, 2);
}

// The input level steps between loud (1) and quiet (0.1, -20 dB) every 10 blocks, so a gate closes and opens again,
// an expander has something below its threshold, and a transient shaper sees attacks
template <typename Fx>
struct Stepped {
    Fx fx;
    int blocks = 0;
    void prepare(double sr, int maxBlock, int numChannels) { fx.prepare(sr, maxBlock, numChannels); }
    void process(AudioBlock b) noexcept {
        if ((blocks++ / 10) % 2 == 1)
            b.multiplyBy(0.1f);
        fx.process(b);
    }
};

template <typename Block>
struct PreparedPair : StereoPair<Block> {
    void prepare(double, int, int) {
        for (Block& b : this->side)
            b.prepare(4800);
    }
};

struct EnvelopePair : StereoPair<EnvelopeFollower> {
    void prepare(double sr, int, int) {
        for (EnvelopeFollower& e : side)
            e.prepare(sr);
    }
};

// The attack / release filter alone: it smooths |x|
struct AttackReleaseRig {
    AttackReleaseFilter filter;
    void prepare(double sr, int, int) { filter.prepare(sr); }
    void process(AudioBlock b) noexcept {
        for (int i = 0; i < b.getNumSamples(); ++i) {
            const float y = filter.process(std::abs(b.getSample(0, i)));
            b.setSample(0, i, y);
            b.setSample(1, i, y);
        }
    }
};

// The LFO's value, scaled to 0.25
struct LfoRig {
    Lfo lfo;
    void prepare(double sr, int, int) { lfo.prepare(sr); }
    void process(AudioBlock b) noexcept {
        for (int i = 0; i < b.getNumSamples(); ++i) {
            const float v = 0.25f * lfo.getNextValue();
            b.setSample(0, i, v);
            b.setSample(1, i, v);
        }
    }
};

// SmoothedValue as a gain (starting at 1, ramp 50 ms)
struct SmoothedRig {
    SmoothedValue value{1.0f};
    void prepare(double sr, int, int) { value.reset(sr, 0.05); }
    void process(AudioBlock b) noexcept { value.applyGain(b); }
};

// The level meter, its input silent from block 12 on so hold, decay and the RMS window all show
struct DecayingMeterRig : LevelMeterRig {
    int blocks = 0;
    void prepare(double sr, int, int) { meter.prepare(sr, 2); }
    void process(AudioBlock b) noexcept {
        if (blocks++ >= 12)
            b.clear();
        LevelMeterRig::process(b);
    }
};

struct LoudnessRig : LoudnessMeterRig {
    void prepare(double sr, int, int) { meter.prepare(sr, 2); }
};

struct SpectrumParamRig : SpectrumRig {
    void prepare(double sr, int, int) { analyzer.prepare(sr); }
};

// 30 blocks of the sine; set(fx, value) is called before block 10 (if call). Returns every output sample.
template <typename Fx, typename Setup, typename Set>
std::vector<float> renderWithSetter(const Setup& setup, const Set& set, bool beforePrepare, bool call, float value) {
    constexpr int kSetBlock = 10, kBlocks = 30;
    const auto fx = std::make_unique<Fx>();
    if (call && beforePrepare)
        set(*fx, value);
    prepareFx(*fx);
    setup(*fx);
    AudioBuffer buf(2, kBlock);
    std::vector<float> out;
    out.reserve(static_cast<size_t>(2 * kBlock * kBlocks));
    for (int blk = 0; blk < kBlocks; ++blk) {
        if (call && !beforePrepare && blk == kSetBlock)
            set(*fx, value);
        fillSine(buf, static_cast<long>(blk) * kBlock);
        fx->process(AudioBlock(buf));
        for (int ch = 0; ch < 2; ++ch)
            out.insert(out.end(), buf.getReadPointer(ch), buf.getReadPointer(ch) + kBlock);
    }
    return out;
}

bool sameBits(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

enum class When { BetweenBlocks, BeforePrepare };

int paramRows = 0, paramFailures = 0;

// Fx is built on the heap, prepared and set up (setup) as a twin of an untouched object. set(fx, v) is the setter
// under test. NaN and +inf leave the output bit-identical to the untouched twin (the setter kept its last valid
// value); so does -inf, except for a value in dB (unit Decibels), where -inf means silence and acts exactly like
// kMinusInfinityDb. So that a row cannot pass because the setter does nothing here, the valid value must change the
// output. A setter documented as "call before prepare()" is called before prepare() (When::BeforePrepare).
template <typename Fx, typename Setup, typename Set>
void checkNonFiniteParameterIgnored(const char* name, Setup setup, Set set, float valid,
                                    ParameterUnit unit = ParameterUnit::Plain, When when = When::BetweenBlocks) {
    const bool early = when == When::BeforePrepare;
    const std::vector<float> untouched = renderWithSetter<Fx>(setup, set, early, false, 0.0f);
    const bool observable = !sameBits(untouched, renderWithSetter<Fx>(setup, set, early, true, valid));
    ++paramRows;
    if (!observable) {
        ++paramFailures;
        std::printf("  %-36s the valid value %g changes nothing: the row proves nothing\n", name,
                    static_cast<double>(valid));
    }
    DAWLAB_CHECK(observable);
    for (float bad : {kNaN, kInf, -kInf}) {
        const bool floor = unit == ParameterUnit::Decibels && bad < 0.0f;
        const std::vector<float> expected =
            floor ? renderWithSetter<Fx>(setup, set, early, true, kMinusInfinityDb) : untouched;
        const bool ok = sameBits(expected, renderWithSetter<Fx>(setup, set, early, true, bad));
        if (!ok) {
            ++paramFailures;
            std::printf("  %-36s %-4s: output differs from %s\n", name,
                        std::isnan(bad) ? "NaN" : bad > 0.0f ? "+inf" : "-inf",
                        floor ? "the twin set to kMinusInfinityDb" : "the untouched twin");
        }
        DAWLAB_CHECK(ok);
    }
}

template <typename Fx, typename Setup, typename Set>
void row(const char* name, Setup setup, Set set, float valid, ParameterUnit unit = ParameterUnit::Plain,
         When when = When::BetweenBlocks) {
    checkNonFiniteParameterIgnored<Fx>(name, setup, set, valid, unit, when);
}

template <typename Fx, typename Set>
void row(const char* name, Set set, float valid, ParameterUnit unit = ParameterUnit::Plain) {
    checkNonFiniteParameterIgnored<Fx>(name, [](Fx&) {}, set, valid, unit);
}

constexpr ParameterUnit kDb = ParameterUnit::Decibels;
constexpr ParameterUnit kPlain = ParameterUnit::Plain;

void checkParameterRuleCh01to05() {
    row<Gain>("Gain::setGainDb", [](Gain& g, float v) { g.setGainDb(v); }, -6.0f, kDb);
    row<Gain>("Gain::setGainLinear", [](Gain& g, float v) { g.setGainLinear(v); }, 0.5f);
    row<Gain>("Gain::setRampSeconds", [](Gain& g) { g.setRampSeconds(1.0); g.setGainDb(-12.0f); },
              [](Gain& g, float v) { g.setRampSeconds(v); }, 0.1f);
    row<Panner>("Panner::setPan", [](Panner& p, float v) { p.setPan(v); }, 0.5f);
    row<Panner>("Panner::setRampSeconds", [](Panner& p) { p.setRampSeconds(1.0); p.setPan(0.8f); },
                [](Panner& p, float v) { p.setRampSeconds(v); }, 0.1f);
    row<SmoothedRig>("SmoothedValue::setTargetValue", [](SmoothedRig& r, float v) { r.value.setTargetValue(v); }, 0.5f);
    row<SmoothedRig>("SmoothedValue::setCurrentAndTargetValue",
                     [](SmoothedRig& r, float v) { r.value.setCurrentAndTargetValue(v); }, 0.5f);
    row<OnePole>("OnePole::setCutoff", [](OnePole& f) { f.setCutoff(2000.0f); },
                 [](OnePole& f, float v) { f.setCutoff(v); }, 500.0f);
    row<StateVariableFilter>("StateVariableFilter::setCutoff", [](StateVariableFilter& f, float v) { f.setCutoff(v); },
                             500.0f);
    row<StateVariableFilter>("StateVariableFilter::setQ", [](StateVariableFilter& f, float v) { f.setQ(v); }, 4.0f);
    const auto eqSetup = [](ParametricEq& eq) {
        eq.setBandEnabled(2, true);
        eq.setBandGainDb(2, 6.0f);
    };
    row<ParametricEq>("ParametricEq::setBandFrequency", eqSetup,
                      [](ParametricEq& eq, float v) { eq.setBandFrequency(2, v); }, 1000.0f);
    row<ParametricEq>("ParametricEq::setBandQ", eqSetup, [](ParametricEq& eq, float v) { eq.setBandQ(2, v); }, 3.0f);
    row<ParametricEq>("ParametricEq::setBandGainDb", eqSetup,
                      [](ParametricEq& eq, float v) { eq.setBandGainDb(2, v); }, -6.0f, kDb);
    row<ParametricEq>("ParametricEq::setSmoothingSeconds", eqSetup,
                      [](ParametricEq& eq, float v) { eq.setSmoothingSeconds(v); }, 0.2f, kPlain,
                      When::BeforePrepare);
    const auto compSetup = [](Compressor& c) {
        c.setThresholdDb(-30.0f);
        c.setRatio(4.0f);
    };
    row<Compressor>("Compressor::setThresholdDb", compSetup, [](Compressor& c, float v) { c.setThresholdDb(v); },
                    -20.0f, kDb);
    row<Compressor>("Compressor::setRatio", compSetup, [](Compressor& c, float v) { c.setRatio(v); }, 8.0f);
    row<Compressor>("Compressor::setKneeDb", compSetup, [](Compressor& c, float v) { c.setKneeDb(v); }, 40.0f, kDb);
    row<Compressor>("Compressor::setAttackMs", compSetup, [](Compressor& c, float v) { c.setAttackMs(v); }, 50.0f);
    row<Compressor>("Compressor::setReleaseMs", compSetup, [](Compressor& c, float v) { c.setReleaseMs(v); }, 500.0f);
    row<Compressor>("Compressor::setMakeupGainDb", compSetup, [](Compressor& c, float v) { c.setMakeupGainDb(v); },
                    6.0f, kDb);
    row<Compressor>("Compressor::setRmsWindowMs",
                    [](Compressor& c) {
                        c.setThresholdDb(-30.0f);
                        c.setRatio(4.0f);
                        c.setDetectorMode(DetectorMode::Rms);
                    },
                    [](Compressor& c, float v) { c.setRmsWindowMs(v); }, 50.0f);
    const auto limSetup = [](Limiter& l) { l.setInputGainDb(12.0f); };
    row<Limiter>("Limiter::setCeilingDb", limSetup, [](Limiter& l, float v) { l.setCeilingDb(v); }, -6.0f, kDb);
    row<Limiter>("Limiter::setInputGainDb", limSetup, [](Limiter& l, float v) { l.setInputGainDb(v); }, 6.0f, kDb);
    row<Limiter>("Limiter::setReleaseMs", limSetup, [](Limiter& l, float v) { l.setReleaseMs(v); }, 300.0f);
    row<Limiter>("Limiter::setLookaheadMs", limSetup, [](Limiter& l, float v) { l.setLookaheadMs(v); }, 2.0f);
    row<Limiter>("Limiter::setMaxLookaheadMs", limSetup, [](Limiter& l, float v) { l.setMaxLookaheadMs(v); }, 2.0f,
                 kPlain, When::BeforePrepare);
}

void checkParameterRuleCh06to08() {
    row<Saturation>("Saturation::setDriveDb", [](Saturation& s, float v) { s.setDriveDb(v); }, 12.0f, kDb);
    row<Saturation>("Saturation::setMix", [](Saturation& s) { s.setDriveDb(12.0f); },
                    [](Saturation& s, float v) { s.setMix(v); }, 0.5f);
    row<Saturation>("Saturation::setOutputGainDb", [](Saturation& s, float v) { s.setOutputGainDb(v); }, -6.0f, kDb);
    row<Saturation>("Saturation::setDcBlockerHz", [](Saturation& s, float v) { s.setDcBlockerHz(v); }, 200.0f);
    row<Distortion>("Distortion::setDriveDb", [](Distortion& d, float v) { d.setDriveDb(v); }, 24.0f, kDb);
    row<Distortion>("Distortion::setToneHz", [](Distortion& d) { d.setDriveDb(24.0f); },
                    [](Distortion& d, float v) { d.setToneHz(v); }, 2000.0f);
    row<Distortion>("Distortion::setMix", [](Distortion& d) { d.setDriveDb(24.0f); },
                    [](Distortion& d, float v) { d.setMix(v); }, 0.5f);
    row<Distortion>("Distortion::setOutputGainDb", [](Distortion& d, float v) { d.setOutputGainDb(v); }, -6.0f, kDb);
    row<Distortion>("Distortion::setBitDepth", [](Distortion& d) { d.setType(DistortionType::Bitcrush); },
                    [](Distortion& d, float v) { d.setBitDepth(v); }, 4.0f);
    row<AmpChain>("AmpChain::setInputHighpassHz", [](AmpChain& a, float v) { a.setInputHighpassHz(v); }, 300.0f);
    row<AmpChain>("AmpChain::setPreEqHz", [](AmpChain& a, float v) { a.setPreEqHz(v); }, 1500.0f);
    row<AmpChain>("AmpChain::setPreEqGainDb", [](AmpChain& a, float v) { a.setPreEqGainDb(v); }, -6.0f, kDb);
    row<AmpChain>("AmpChain::setPreampDriveDb", [](AmpChain& a, float v) { a.setPreampDriveDb(v); }, 20.0f, kDb);
    row<AmpChain>("AmpChain::setPowerAmpDriveDb", [](AmpChain& a, float v) { a.setPowerAmpDriveDb(v); }, 20.0f, kDb);
    row<AmpChain>("AmpChain::setBassDb", [](AmpChain& a, float v) { a.setBassDb(v); }, 6.0f, kDb);
    row<AmpChain>("AmpChain::setMidDb", [](AmpChain& a, float v) { a.setMidDb(v); }, 6.0f, kDb);
    row<AmpChain>("AmpChain::setTrebleDb", [](AmpChain& a, float v) { a.setTrebleDb(v); }, 6.0f, kDb);
    row<AmpChain>("AmpChain::setCabinetCutoffHz", [](AmpChain& a, float v) { a.setCabinetCutoffHz(v); }, 3000.0f);
    row<AmpChain>("AmpChain::setCabinetResonanceHz", [](AmpChain& a, float v) { a.setCabinetResonanceHz(v); },
                  200.0f);
    row<AmpChain>("AmpChain::setCabinetResonanceDb", [](AmpChain& a, float v) { a.setCabinetResonanceDb(v); }, 9.0f,
                  kDb);
    row<AmpChain>("AmpChain::setOutputGainDb", [](AmpChain& a, float v) { a.setOutputGainDb(v); }, -6.0f, kDb);
    using SGate = Stepped<Gate>;
    const auto gateSetup = [](SGate& g) { g.fx.setThresholdDb(-24.0f); };
    row<SGate>("Gate::setThresholdDb", gateSetup, [](SGate& g, float v) { g.fx.setThresholdDb(v); }, -40.0f, kDb);
    row<SGate>("Gate::setHysteresisDb", gateSetup, [](SGate& g, float v) { g.fx.setHysteresisDb(v); }, 12.0f, kDb);
    row<SGate>("Gate::setRangeDb", gateSetup, [](SGate& g, float v) { g.fx.setRangeDb(v); }, -20.0f, kDb);
    row<SGate>("Gate::setAttackMs", gateSetup, [](SGate& g, float v) { g.fx.setAttackMs(v); }, 20.0f);
    row<SGate>("Gate::setHoldMs", gateSetup, [](SGate& g, float v) { g.fx.setHoldMs(v); }, 5.0f);
    row<SGate>("Gate::setReleaseMs", gateSetup, [](SGate& g, float v) { g.fx.setReleaseMs(v); }, 20.0f);
    row<SGate>("Gate::setDetectorReleaseMs", gateSetup, [](SGate& g, float v) { g.fx.setDetectorReleaseMs(v); },
               100.0f);
    using SExp = Stepped<Expander>;
    const auto expSetup = [](SExp& e) {
        e.fx.setThresholdDb(-20.0f);
        e.fx.setReleaseMs(20.0f);
    };
    row<SExp>("Expander::setThresholdDb", expSetup, [](SExp& e, float v) { e.fx.setThresholdDb(v); }, -30.0f, kDb);
    row<SExp>("Expander::setRatio", expSetup, [](SExp& e, float v) { e.fx.setRatio(v); }, 4.0f);
    row<SExp>("Expander::setKneeDb", expSetup, [](SExp& e, float v) { e.fx.setKneeDb(v); }, 10.0f, kDb);
    row<SExp>("Expander::setRangeDb", expSetup, [](SExp& e, float v) { e.fx.setRangeDb(v); }, -6.0f, kDb);
    row<SExp>("Expander::setAttackMs", expSetup, [](SExp& e, float v) { e.fx.setAttackMs(v); }, 30.0f);
    row<SExp>("Expander::setReleaseMs", expSetup, [](SExp& e, float v) { e.fx.setReleaseMs(v); }, 300.0f);
    const auto deEssSetup = [](DeEsser& d) {
        d.setThresholdDb(-50.0f);
        d.setRangeDb(24.0f);
    };
    row<DeEsser>("DeEsser::setFrequency", deEssSetup, [](DeEsser& d, float v) { d.setFrequency(v); }, 3000.0f);
    row<DeEsser>("DeEsser::setThresholdDb", deEssSetup, [](DeEsser& d, float v) { d.setThresholdDb(v); }, -45.0f, kDb);
    row<DeEsser>("DeEsser::setRangeDb", deEssSetup, [](DeEsser& d, float v) { d.setRangeDb(v); }, 3.0f, kDb);
    row<DeEsser>("DeEsser::setAttackMs", deEssSetup, [](DeEsser& d, float v) { d.setAttackMs(v); }, 20.0f);
    row<DeEsser>("DeEsser::setReleaseMs", deEssSetup, [](DeEsser& d, float v) { d.setReleaseMs(v); }, 200.0f);
    using STs = Stepped<TransientShaper>;
    row<STs>("TransientShaper::setAttackDb", [](STs& t, float v) { t.fx.setAttackDb(v); }, 12.0f, kDb);
    row<STs>("TransientShaper::setSustainDb", [](STs& t, float v) { t.fx.setSustainDb(v); }, -12.0f, kDb);
    row<EnvelopePair>("EnvelopeFollower::setAttackMs", [](EnvelopePair& p, float v) {
        for (EnvelopeFollower& e : p.side)
            e.setAttackMs(v);
    }, 30.0f);
    row<EnvelopePair>("EnvelopeFollower::setReleaseMs", [](EnvelopePair& p, float v) {
        for (EnvelopeFollower& e : p.side)
            e.setReleaseMs(v);
    }, 300.0f);
    row<AttackReleaseRig>("AttackReleaseFilter::setCurrentValue",
                          [](AttackReleaseRig& r, float v) { r.filter.setCurrentValue(v); }, 0.9f);
}

void checkParameterRuleCh09to12() {
    const auto delaySetup = [](Delay& d) {
        d.setDelayMs(30.0f);
        d.reset();
    };
    row<Delay>("Delay::setDelayMs", delaySetup, [](Delay& d, float v) { d.setDelayMs(v); }, 50.0f);
    row<Delay>("Delay::setDelayRampSeconds",
               [](Delay& d) {
                   d.setDelayMs(30.0f);
                   d.reset();
                   d.setDelayRampSeconds(1.0);
                   d.setDelayMs(60.0f);
               },
               [](Delay& d, float v) { d.setDelayRampSeconds(v); }, 0.1f);
    row<Delay>("Delay::setFeedback", delaySetup, [](Delay& d, float v) { d.setFeedback(v); }, 0.8f);
    row<Delay>("Delay::setMix", delaySetup, [](Delay& d, float v) { d.setMix(v); }, 0.8f);
    row<Delay>("Delay::setLowpassHz", delaySetup, [](Delay& d, float v) { d.setLowpassHz(v); }, 1000.0f);
    row<Delay>("Delay::setHighpassHz", delaySetup, [](Delay& d, float v) { d.setHighpassHz(v); }, 2000.0f);
    row<Delay>("Delay::setMaxDelayMs", [](Delay& d) { d.setDelayMs(500.0f); },
               [](Delay& d, float v) { d.setMaxDelayMs(v); }, 100.0f, kPlain, When::BeforePrepare);
    row<Chorus>("Chorus::setRateHz", [](Chorus& c, float v) { c.setRateHz(v); }, 3.0f);
    row<Chorus>("Chorus::setDepthMs", [](Chorus& c, float v) { c.setDepthMs(v); }, 10.0f);
    row<Chorus>("Chorus::setCentreDelayMs", [](Chorus& c, float v) { c.setCentreDelayMs(v); }, 10.0f);
    row<Chorus>("Chorus::setMix", [](Chorus& c, float v) { c.setMix(v); }, 0.8f);
    row<Chorus>("Chorus::setStereoSpread", [](Chorus& c, float v) { c.setStereoSpread(v); }, 0.2f);
    row<Flanger>("Flanger::setRateHz", [](Flanger& f, float v) { f.setRateHz(v); }, 2.0f);
    row<Flanger>("Flanger::setDepthMs", [](Flanger& f, float v) { f.setDepthMs(v); }, 1.0f);
    row<Flanger>("Flanger::setDelayMs", [](Flanger& f, float v) { f.setDelayMs(v); }, 5.0f);
    row<Flanger>("Flanger::setFeedback", [](Flanger& f, float v) { f.setFeedback(v); }, 0.8f);
    row<Flanger>("Flanger::setMix", [](Flanger& f, float v) { f.setMix(v); }, 0.8f);
    row<Flanger>("Flanger::setLfoPhase", [](Flanger& f, float v) { f.setLfoPhase(v); }, 0.3f);
    row<Phaser>("Phaser::setRateHz", [](Phaser& p, float v) { p.setRateHz(v); }, 2.0f);
    row<Phaser>("Phaser::setDepth", [](Phaser& p, float v) { p.setDepth(v); }, 0.2f);
    row<Phaser>("Phaser::setCentreHz", [](Phaser& p, float v) { p.setCentreHz(v); }, 2000.0f);
    row<Phaser>("Phaser::setFeedback", [](Phaser& p, float v) { p.setFeedback(v); }, 0.7f);
    row<Phaser>("Phaser::setMix", [](Phaser& p, float v) { p.setMix(v); }, 0.8f);
    row<Phaser>("Phaser::setLfoPhase", [](Phaser& p, float v) { p.setLfoPhase(v); }, 0.3f);
    row<Phaser>("Phaser::setSweepOctaves", [](Phaser& p, float v) { p.setSweepOctaves(v); }, 4.0f);
    row<Tremolo>("Tremolo::setRateHz", [](Tremolo& t, float v) { t.setRateHz(v); }, 8.0f);
    row<Tremolo>("Tremolo::setDepth", [](Tremolo& t, float v) { t.setDepth(v); }, 0.2f);
    row<Tremolo>("Tremolo::setLfoPhase", [](Tremolo& t, float v) { t.setLfoPhase(v); }, 0.3f);
    row<Tremolo>("Tremolo::setSmoothingMs", [](Tremolo& t) { t.setShape(LfoShape::Square); t.setRateHz(20.0f); },
                 [](Tremolo& t, float v) { t.setSmoothingMs(v); }, 10.0f);
    const auto squareLfo = [](LfoRig& r) {
        r.lfo.setShape(LfoShape::Square);
        r.lfo.setRateHz(20.0f);
    };
    row<LfoRig>("Lfo::setRateHz", [](LfoRig& r, float v) { r.lfo.setRateHz(v); }, 3.0f);
    row<LfoRig>("Lfo::setPhase", [](LfoRig& r, float v) { r.lfo.setPhase(v); }, 0.3f);
    row<LfoRig>("Lfo::setSmoothingMs", squareLfo, [](LfoRig& r, float v) { r.lfo.setSmoothingMs(v); }, 10.0f);
    row<DecayingMeterRig>("LevelMeter::setPeakHoldMs", [](DecayingMeterRig& r, float v) { r.meter.setPeakHoldMs(v); },
                          10.0f);
    row<DecayingMeterRig>("LevelMeter::setDecayDbPerSecond",
                          [](DecayingMeterRig& r, float v) { r.meter.setDecayDbPerSecond(v); }, 60.0f);
    row<DecayingMeterRig>("LevelMeter::setRmsWindowMs", [](DecayingMeterRig& r, float v) { r.meter.setRmsWindowMs(v); },
                          50.0f);
    row<LoudnessRig>("LoudnessMeter::setChannelWeight", [](LoudnessRig& r, float v) { r.meter.setChannelWeight(0, v); },
                     0.5f);
    row<SpectrumParamRig>("SpectrumAnalyzer::setSmoothing",
                          [](SpectrumParamRig& r, float v) { r.analyzer.setSmoothing(v); }, 0.2f);
    row<SpectrumParamRig>("SpectrumAnalyzer::setOverlap", [](SpectrumParamRig& r, float v) { r.analyzer.setOverlap(v); },
                          0.75f);
    row<Reverb>("Reverb::setDecaySeconds", [](Reverb& r, float v) { r.setDecaySeconds(v); }, 8.0f);
    row<Reverb>("Reverb::setPreDelayMs", [](Reverb& r, float v) { r.setPreDelayMs(v); }, 50.0f);
    row<Reverb>("Reverb::setDampingHz", [](Reverb& r, float v) { r.setDampingHz(v); }, 2000.0f);
    row<Reverb>("Reverb::setSize", [](Reverb& r, float v) { r.setSize(v); }, 0.9f);
    row<Reverb>("Reverb::setWidth", [](Reverb& r, float v) { r.setWidth(v); }, 0.2f);
    row<Reverb>("Reverb::setMix", [](Reverb& r, float v) { r.setMix(v); }, 0.8f);
    row<Reverb>("Reverb::setModulationDepthMs", [](Reverb& r, float v) { r.setModulationDepthMs(v); }, 2.0f);
    row<Reverb>("Reverb::setModulationRateHz", [](Reverb& r) { r.setModulationDepthMs(2.0f); },
                [](Reverb& r, float v) { r.setModulationRateHz(v); }, 3.0f);
    row<ConvolutionReverb>("ConvolutionReverb::setMix",
                           [](ConvolutionReverb& c) {
                               c.setImpulseResponse(
                                   ConvolutionReverb::makeSyntheticImpulseResponse(kSampleRate, 0.2, 2));
                           },
                           [](ConvolutionReverb& c, float v) { c.setMix(v); }, 0.2f);
    const auto combSetup = [](PreparedPair<CombFilter>& p) {
        for (CombFilter& c : p.side) {
            c.setDelaySamples(700);
            c.setFeedback(0.6f);
        }
    };
    row<PreparedPair<CombFilter>>("CombFilter::setFeedback", combSetup, [](PreparedPair<CombFilter>& p, float v) {
        for (CombFilter& c : p.side)
            c.setFeedback(v);
    }, 0.3f);
    row<PreparedPair<CombFilter>>("CombFilter::setDamping", combSetup, [](PreparedPair<CombFilter>& p, float v) {
        for (CombFilter& c : p.side)
            c.setDamping(v);
    }, 0.8f);
    row<PreparedPair<AllpassDiffuser>>("AllpassDiffuser::setGain",
                                       [](PreparedPair<AllpassDiffuser>& p) {
                                           for (AllpassDiffuser& a : p.side)
                                               a.setDelaySamples(700);
                                       },
                                       [](PreparedPair<AllpassDiffuser>& p, float v) {
                                           for (AllpassDiffuser& a : p.side)
                                               a.setGain(v);
                                       },
                                       -0.3f);
    row<PreparedPair<TappedDelay>>("TappedDelay::setTaps", [](PreparedPair<TappedDelay>& p, float v) {
        for (TappedDelay& t : p.side)
            t.setTaps({{0, 1.0f}, {700, v}});
    }, 0.25f);
    std::printf("non-finite parameter: %d setters, %d failures\n", paramRows, paramFailures);
}

// Stateless functions have no last value to keep: a non-finite argument means a documented default
void checkStatelessDefaults() {
    int failures = 0;
    const auto same = [](const BiquadCoefficients& a, const BiquadCoefficients& b) {
        return a.b0 == b.b0 && a.b1 == b.b1 && a.b2 == b.b2 && a.a1 == b.a1 && a.a2 == b.a2;
    };
    using C = BiquadCoefficients;
    for (float badF : {kNaN, kInf, -kInf}) {
        const double bad = static_cast<double>(badF);
        // frequency -> 1 kHz, Q -> 1/sqrt(2), gain -> 0 dB
        const bool ok =
            same(C::makeLowpass(kSampleRate, bad, 2.0), C::makeLowpass(kSampleRate, 1000.0, 2.0)) &&
            same(C::makeHighpass(kSampleRate, 500.0, bad), C::makeHighpass(kSampleRate, 500.0, C::kButterworthQ)) &&
            same(C::makeBandpass(kSampleRate, bad, bad), C::makeBandpass(kSampleRate, 1000.0, C::kButterworthQ)) &&
            same(C::makeNotch(kSampleRate, bad, 2.0), C::makeNotch(kSampleRate, 1000.0, 2.0)) &&
            same(C::makeAllpass(kSampleRate, 500.0, bad), C::makeAllpass(kSampleRate, 500.0, C::kButterworthQ)) &&
            same(C::makePeak(kSampleRate, 500.0, 2.0, bad), C::makePeak(kSampleRate, 500.0, 2.0, 0.0)) &&
            same(C::makeLowShelf(kSampleRate, bad, 0.7, bad), C::makeLowShelf(kSampleRate, 1000.0, 0.7, 0.0)) &&
            same(C::makeHighShelf(kSampleRate, 500.0, bad, bad),
                 C::makeHighShelf(kSampleRate, 500.0, C::kButterworthQ, 0.0));
        if (!ok) {
            ++failures;
            std::printf("  BiquadCoefficients factories with %g: not the documented defaults\n", bad);
        }
        DAWLAB_CHECK(ok);

        float l = 0.0f, r = 0.0f, l0 = 0.0f, r0 = 0.0f;
        Panner::computeGains(badF, PanLaw::ConstantPower, l, r);
        Panner::computeGains(0.0f, PanLaw::ConstantPower, l0, r0);
        DAWLAB_CHECK(l == l0 && r == r0);
    }
    // A non-finite delay reads as the minimum delay (0 for linear, 1 for cubic and Lagrange). The old code cast a
    // NaN to int: readLagrange then read out of bounds, and -fsanitize=float-cast-overflow stopped the others.
    const auto line = std::make_unique<FractionalDelayLine>();
    line->prepare(64, 1);
    for (int i = 0; i < 64; ++i)
        line->push(0, static_cast<float>(i));
    bool reads = true;
    for (float bad : {kNaN, kInf, -kInf})
        reads = reads && line->readLinear(0, bad) == line->readLinear(0, 0.0f) &&
                line->readCubic(0, bad) == line->readCubic(0, 1.0f) &&
                line->readLagrange(0, bad) == line->readLagrange(0, 1.0f);
    if (!reads) {
        ++failures;
        std::printf("  FractionalDelayLine reads of a NaN delay: not the minimum delay\n");
    }
    DAWLAB_CHECK(reads);
    std::printf("non-finite argument of a stateless function: %d failures\n", failures);
}

// ---- Rule: a range is at most 100 dB --------------------------------------------------------------------------------

// The range of Gate, Expander and DeEsser (how far they attenuate) is capped at 100 dB (-kMinusInfinityDb), where
// dbToGain already gives silence. A larger range cannot be heard; an infinite one made the gate's attack step
// inf - inf = NaN. So any range beyond 100 dB (either sign, finite or -inf) acts exactly like 100 dB.
// Fx is set up, then gets setRange(fx, big) before the first block; its output must be bit-identical to a twin set
// to the cap, and getRange(fx) must read the cap (-100 for Gate and Expander, +100 for DeEsser).
template <typename Fx, typename Setup, typename SetRange, typename GetRange>
void checkRangeCapped(const char* name, Setup setup, SetRange setRange, GetRange getRange, float cap) {
    const auto noSetter = [](Fx&, float) {};
    const auto render = [&](float range) {
        return renderWithSetter<Fx>(
            [&](Fx& fx) {
                setup(fx);
                setRange(fx, range);
            },
            noSetter, false, false, 0.0f);
    };
    const std::vector<float> capped = render(cap);
    for (float big : {-1.0e30f, -150.0f, 1.0e30f, -kInf}) {
        const auto fx = std::make_unique<Fx>();
        prepareFx(*fx);
        setRange(*fx, big);
        const float read = getRange(*fx);
        const bool same = sameBits(capped, render(big));
        if (read != cap || !same)
            std::printf("  %-9s setRangeDb(%g): range reads %g (want %g), output %s the twin set to %g\n", name,
                        static_cast<double>(big), static_cast<double>(read), static_cast<double>(cap),
                        same ? "equals" : "DIFFERS from", static_cast<double>(cap));
        DAWLAB_CHECK(read == cap);
        DAWLAB_CHECK(same);
    }
}

void checkRangeRule() {
    using SGate = Stepped<Gate>;
    using SExp = Stepped<Expander>;
    checkRangeCapped<SGate>(
        "Gate", [](SGate& g) { g.fx.setThresholdDb(-24.0f); }, [](SGate& g, float v) { g.fx.setRangeDb(v); },
        [](SGate& g) { return g.fx.getRangeDb(); }, kMinusInfinityDb);
    checkRangeCapped<SExp>(
        "Expander",
        [](SExp& e) {
            e.fx.setThresholdDb(-20.0f);
            e.fx.setReleaseMs(20.0f);
        },
        [](SExp& e, float v) { e.fx.setRangeDb(v); }, [](SExp& e) { return e.fx.getRangeDb(); }, kMinusInfinityDb);
    // The detector sees the 440 Hz sine about 24 dB down; a threshold of -200 dBFS asks for more than 100 dB
    checkRangeCapped<DeEsser>(
        "DeEsser", [](DeEsser& d) { d.setThresholdDb(-200.0f); }, [](DeEsser& d, float v) { d.setRangeDb(v); },
        [](DeEsser& d) { return d.getRangeDb(); }, -kMinusInfinityDb);

    // The review's case: a gate whose range is -inf (set before or after prepare) still opens on a loud signal, and its
    // gain reduction reads a finite value
    for (bool beforePrepare : {true, false}) {
        const auto gate = std::make_unique<Gate>();
        if (beforePrepare)
            gate->setRangeDb(-kInf);
        gate->prepare(kSampleRate, kBlock, 2);
        if (!beforePrepare)
            gate->setRangeDb(-kInf);
        AudioBuffer buf(2, kBlock);
        double sum = 0.0;
        for (int blk = 0; blk < 120; ++blk) {
            fillSine(buf, static_cast<long>(blk) * kBlock);
            if (blk < 20)
                buf.clear();
            gate->process(AudioBlock(buf));
            if (blk >= 70)
                for (int i = 0; i < kBlock; ++i)
                    sum += static_cast<double>(buf.getSample(0, i)) * buf.getSample(0, i);
        }
        const double rms = std::sqrt(sum / (50.0 * kBlock));
        const bool opens = rms >= 0.9 * 0.25 / std::sqrt(2.0) && std::isfinite(gate->getGainReductionDb());
        if (!opens)
            std::printf("  Gate setRangeDb(-inf) %s prepare(): rms %g, gain reduction %g\n",
                        beforePrepare ? "before" : "after", rms, static_cast<double>(gate->getGainReductionDb()));
        DAWLAB_CHECK(opens);
    }
    std::printf("range beyond 100 dB: checked for Gate, Expander, DeEsser\n");
}

// ---- Rule: process() costs about the same in every block ------------------------------------------------------------

// LevelMeter's RMS as the course defines it: a ring of x^2, a running sum (add the new value, subtract the one pushed
// out), and every time the ring wraps the exact sum of the ring in slot order, so rounding error never builds up.
// This reference does the exact sum the slow way, O(W) at the wrap; LevelMeter must read the same, bit for bit.
struct ReferenceRms {
    std::vector<float> ring;
    double sum = 0.0;
    int pos = 0;
    explicit ReferenceRms(int window) : ring(static_cast<size_t>(window), 0.0f) {}
    float process(const float* x, int n) {
        for (int i = 0; i < n; ++i) {
            const float a = std::abs(finiteOrZero(x[i]));
            const float sq = a * a;
            float& slot = ring[static_cast<size_t>(pos)];
            sum += static_cast<double>(sq) - static_cast<double>(slot);
            slot = sq;
            if (++pos >= static_cast<int>(ring.size())) {
                pos = 0;
                double exact = 0.0;
                for (float v : ring)
                    exact += v;
                sum = exact;
            }
        }
        if (sum < 0.0)
            sum = 0.0;
        return gainToDb(static_cast<float>(std::sqrt(sum / static_cast<double>(ring.size()))));
    }
};

bool timingIsJudged() {
#if !defined(NDEBUG) || defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    return false;
#else
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(undefined_behavior_sanitizer)
    return false;
#endif
#endif
#if defined(DAWLAB_TIMEOUT_SCALE)
    return DAWLAB_TIMEOUT_SCALE == 1; // CMake (cmake/DawLab.cmake) sets it above 1 for a sanitizer build
#else
    return true;
#endif
#endif
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

void checkMeterCostIsFlat() {
    // (1) The reading: bit-identical to the reference over 5 trips of a 7 ms window (336 samples), blocks of 1..700
    {
        const auto meter = std::make_unique<LevelMeter>();
        meter->prepare(kSampleRate, 2);
        meter->setRmsWindowMs(7.0f);
        const int window = msToSamples(7.0, kSampleRate);
        ReferenceRms ref[2] = {ReferenceRms(window), ReferenceRms(window)};
        NoiseGenerator noise(7u);
        AudioBuffer buf(2, 700);
        bool same = true;
        long done = 0;
        for (int blk = 0; done < 5L * window * 40; ++blk) {
            const int n = 1 + (blk * 7919) % 700;
            AudioBlock block = AudioBlock(buf).getSubBlock(0, n);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    block.getChannelPointer(ch)[i] = 0.3f * noise.next();
            meter->process(block);
            for (int ch = 0; ch < 2; ++ch)
                same = same && meter->getRmsDb(ch) == ref[ch].process(block.getChannelPointer(ch), n);
            done += n;
        }
        if (!same)
            std::printf("  LevelMeter RMS differs from the exact reference\n");
        DAWLAB_CHECK(same);
    }
    // (2) The cost: with the longest window (3 s = 144000 samples), the block in which the ring wraps used to re-sum
    // the whole window (233 us instead of 0.5 us for a 64-sample block; with 17 stereo strips a certain dropout).
    // Wall-clock time is only judged in an optimized build without sanitizers (NDEBUG, DAWLAB_TIMEOUT_SCALE unset or
    // 1); elsewhere the check is skipped. Medians over 20 trips, and a 50 us allowance, keep a preempted block or a
    // coarse clock from failing it.
    if (!timingIsJudged()) {
        std::printf("LevelMeter block cost: not judged (not an optimized build without sanitizers)\n");
        return;
    }
    const auto meter = std::make_unique<LevelMeter>();
    meter->prepare(kSampleRate, 2);
    meter->setRmsWindowMs(LevelMeter::kMaxRmsWindowMs);
    constexpr int kSmall = 64;
    const long window = msToSamples(LevelMeter::kMaxRmsWindowMs, kSampleRate);
    AudioBuffer buf(2, kSmall);
    fillSine(buf, 0);
    std::vector<double> wrapUs, otherUs;
    const long blocks = 20 * window / kSmall;
    for (long blk = 0; blk < blocks; ++blk) {
        const auto t0 = std::chrono::steady_clock::now();
        meter->process(AudioBlock(buf));
        const auto t1 = std::chrono::steady_clock::now();
        const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        const bool wraps = ((blk + 1) * kSmall) / window != (blk * kSmall) / window;
        (wraps ? wrapUs : otherUs).push_back(us);
    }
    const double wrap = median(wrapUs), other = median(otherUs);
    const bool flat = wrap < 10.0 * other + 50.0;
    std::printf("LevelMeter block cost (3 s window, 64 samples): median %.2f us in the %zu blocks where the ring wraps, "
                "%.2f us in the others -> %s\n",
                wrap, wrapUs.size(), other, flat ? "flat" : "SPIKE");
    DAWLAB_CHECK(flat);
}

// ---- Rule: setting a parameter to the value it already has changes nothing -----------------------------------------

// A host (the chapter 13 plugin) and the chapter 16 automation push every parameter once per block, even when it has
// not changed. A ramp that restarted on every call would never arrive (Saturation's took 3.3 times as long).
template <typename Fx, typename Setup, typename Set>
std::vector<float> renderRepeatedSetter(const Setup& setup, const Set& set, float value, bool everyBlock, bool call) {
    const auto fx = std::make_unique<Fx>();
    prepareFx(*fx);
    setup(*fx);
    AudioBuffer buf(2, kBlock);
    std::vector<float> out;
    for (int blk = 0; blk < 8; ++blk) {
        if (call && (blk == 0 || everyBlock))
            set(*fx, value);
        fillSine(buf, static_cast<long>(blk) * kBlock);
        fx->process(AudioBlock(buf));
        for (int ch = 0; ch < 2; ++ch)
            out.insert(out.end(), buf.getReadPointer(ch), buf.getReadPointer(ch) + kBlock);
    }
    return out;
}

int sameValueRows = 0, sameValueFailures = 0;

// set(fx, value) once before block 0, or before every one of 8 blocks: the output is bit-identical. The value must
// change the output (against a twin where set is never called), so the row tests a ramp that really runs.
template <typename Fx, typename Setup, typename Set>
void checkSameValueKeepsRamp(const char* name, Setup setup, Set set, float value) {
    const std::vector<float> once = renderRepeatedSetter<Fx>(setup, set, value, false, true);
    const bool observable = !sameBits(once, renderRepeatedSetter<Fx>(setup, set, value, false, false));
    const bool same = sameBits(once, renderRepeatedSetter<Fx>(setup, set, value, true, true));
    ++sameValueRows;
    if (!observable || !same) {
        ++sameValueFailures;
        std::printf("  %-30s %s\n", name,
                    !observable ? "the value changes nothing: the row proves nothing"
                                : "set again every block: output differs from setting it once (the ramp restarts)");
    }
    DAWLAB_CHECK(observable);
    DAWLAB_CHECK(same);
}

template <typename Fx, typename Set>
void sameValue(const char* name, Set set, float value) {
    checkSameValueKeepsRamp<Fx>(name, [](Fx&) {}, set, value);
}

void checkSameValueRule() {
    sameValue<Saturation>("Saturation::setOutputGainDb", [](Saturation& s, float v) { s.setOutputGainDb(v); }, -20.0f);
    sameValue<Saturation>("Saturation::setMix", [](Saturation& s, float v) { s.setMix(v); }, 0.2f);
    sameValue<Saturation>("Saturation::setDriveDb", [](Saturation& s, float v) { s.setDriveDb(v); }, 12.0f);
    sameValue<Gain>("Gain::setGainDb", [](Gain& g, float v) { g.setGainDb(v); }, -12.0f);
    sameValue<Panner>("Panner::setPan", [](Panner& p, float v) { p.setPan(v); }, 0.7f);
    const auto shortDelay = [](Delay& d) {
        d.setDelayMs(30.0f);
        d.reset();
    };
    checkSameValueKeepsRamp<Delay>("Delay::setDelayMs", shortDelay, [](Delay& d, float v) { d.setDelayMs(v); }, 60.0f);
    checkSameValueKeepsRamp<Delay>("Delay::setFeedback", shortDelay, [](Delay& d, float v) { d.setFeedback(v); }, 0.8f);
    checkSameValueKeepsRamp<Delay>("Delay::setMix", shortDelay, [](Delay& d, float v) { d.setMix(v); }, 0.8f);
    sameValue<Expander>("Expander::setThresholdDb", [](Expander& e, float v) { e.setThresholdDb(v); }, -6.0f);
    checkSameValueKeepsRamp<Expander>("Expander::setRangeDb", [](Expander& e) { e.setThresholdDb(-6.0f); },
                        [](Expander& e, float v) { e.setRangeDb(v); }, -3.0f);
    sameValue<DeEsser>("DeEsser::setThresholdDb", [](DeEsser& d, float v) { d.setThresholdDb(v); }, -50.0f);
    sameValue<Limiter>("Limiter::setInputGainDb", [](Limiter& l, float v) { l.setInputGainDb(v); }, 12.0f);
    sameValue<Distortion>("Distortion::setDriveDb", [](Distortion& d, float v) { d.setDriveDb(v); }, 24.0f);
    sameValue<Distortion>("Distortion::setOutputGainDb", [](Distortion& d, float v) { d.setOutputGainDb(v); }, -6.0f);
    sameValue<Distortion>("Distortion::setMix", [](Distortion& d, float v) { d.setMix(v); }, 0.5f);
    sameValue<AmpChain>("AmpChain::setOutputGainDb", [](AmpChain& a, float v) { a.setOutputGainDb(v); }, -6.0f);
    sameValue<Compressor>("Compressor::setMakeupGainDb", [](Compressor& c, float v) { c.setMakeupGainDb(v); }, 6.0f);
    checkSameValueKeepsRamp<ParametricEq>("ParametricEq::setBandGainDb", [](ParametricEq& eq) { eq.setBandEnabled(2, true); },
                            [](ParametricEq& eq, float v) { eq.setBandGainDb(2, v); }, 6.0f);
    sameValue<Chorus>("Chorus::setMix", [](Chorus& c, float v) { c.setMix(v); }, 0.9f);
    sameValue<Chorus>("Chorus::setDepthMs", [](Chorus& c, float v) { c.setDepthMs(v); }, 10.0f);
    sameValue<Flanger>("Flanger::setMix", [](Flanger& f, float v) { f.setMix(v); }, 0.9f);
    sameValue<Flanger>("Flanger::setDepthMs", [](Flanger& f, float v) { f.setDepthMs(v); }, 1.0f);
    sameValue<Phaser>("Phaser::setMix", [](Phaser& p, float v) { p.setMix(v); }, 0.9f);
    sameValue<Phaser>("Phaser::setDepth", [](Phaser& p, float v) { p.setDepth(v); }, 0.2f);
    sameValue<Tremolo>("Tremolo::setDepth", [](Tremolo& t, float v) { t.setDepth(v); }, 0.2f);
    sameValue<Reverb>("Reverb::setMix", [](Reverb& r, float v) { r.setMix(v); }, 0.8f);
    std::printf("same value set again: %d setters, %d failures\n", sameValueRows, sameValueFailures);
}

// ---- A smoother arrives -----------------------------------------------------------------------------------------
// A one-pole smoother whose state is a gain, mix, length, delay or frequency takes its target exactly (smoothToward
// in DspMath.h). Toward 0 it would otherwise park at a subnormal number, and the output in silence (or the signal
// times a parked gain) would be subnormal forever; toward any other value it would park a hair short of it. The
// rows run at a low sample rate to keep the test fast: the time a glide takes to park does not depend on the rate.

constexpr double kArriveRate = 16000.0;

int arriveRows = 0, arriveFailures = 0;

// make() returns a prepared object (at kArriveRate). Half a second of a 440 Hz sine at 0.25, then move(fx), then
// `tailSeconds` of silence (ampAfter = 0) or of the sine at ampAfter. In the last quarter second no output sample may
// be subnormal, and arrived(fx) must hold at the end.
template <typename Make, typename Move, typename Arrived>
void checkSmootherArrives(const char* name, Make make, Move move, double tailSeconds, float ampAfter, Arrived arrived) {
    const auto fx = make();
    AudioBuffer buf(2, kBlock);
    const auto render = [&buf, &fx](long blk, float amp) {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
                buf.setSample(ch, i, amp * static_cast<float>(std::sin(kTwoPi * 440.0 *
                                                                       static_cast<double>(blk * kBlock + i) / kArriveRate)));
        fx->process(AudioBlock(buf));
    };
    const long before = static_cast<long>(0.5 * kArriveRate) / kBlock;
    const long after = static_cast<long>(tailSeconds * kArriveRate) / kBlock;
    const long judged = static_cast<long>(0.25 * kArriveRate) / kBlock;
    long blk = 0;
    for (; blk < before; ++blk)
        render(blk, 0.25f);
    move(*fx);
    long subnormal = 0;
    for (long k = 0; k < after; ++k, ++blk) {
        render(blk, ampAfter);
        if (k >= after - judged)
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlock; ++i)
                    subnormal += std::fpclassify(buf.getSample(ch, i)) == FP_SUBNORMAL ? 1 : 0;
    }
    const bool there = arrived(*fx);
    ++arriveRows;
    if (subnormal != 0 || !there) {
        ++arriveFailures;
        std::printf("  %-46s subnormal output samples at the end: %ld, arrived: %s\n", name, subnormal,
                    there ? "yes" : "NO");
    }
    DAWLAB_CHECK(subnormal == 0);
    DAWLAB_CHECK(there);
}

template <typename Fx, typename Setup>
auto preparedAt(Setup setup) {
    return [setup] {
        auto fx = std::make_unique<Fx>();
        fx->prepare(kArriveRate, kBlock, 2);
        setup(*fx);
        return fx;
    };
}

template <typename Fx>
bool outputOnly(const Fx&) {
    return true; // the class shows nothing else: the subnormal output is what the row judges
}

void checkSmootherRule() {
    // The freeze keeps the wet tail alive, so a mix parked just above 0 shows in the output
    checkSmootherArrives("Reverb::setMix(0), frozen tail, silence", preparedAt<Reverb>([](Reverb&) {}),
                         [](Reverb& r) {
                             r.setFreeze(true);
                             r.setMix(0.0f);
                         },
                         3.5, 0.0f, outputOnly<Reverb>);
    // An impulse response that is a pure 2.5 s delay: the wet signal is the sine again while the mix parks
    AudioBuffer delayIr(1, static_cast<int>(2.5 * kArriveRate));
    delayIr.setSample(0, delayIr.getNumSamples() - 1, 1.0f);
    checkSmootherArrives("ConvolutionReverb::setMix(0), silence",
                         preparedAt<ConvolutionReverb>([&delayIr](ConvolutionReverb& c) {
                             c.setImpulseResponse(delayIr);
                             c.setMix(0.3f);
                             c.reset();
                         }),
                         [](ConvolutionReverb& c) { c.setMix(0.0f); }, 2.5, 0.0f, outputOnly<ConvolutionReverb>);
    checkSmootherArrives("Compressor::setMakeupGainDb(-inf), sine", preparedAt<Compressor>([](Compressor&) {}),
                         [](Compressor& c) { c.setMakeupGainDb(-kInf); }, 1.5, 0.25f, outputOnly<Compressor>);
    checkSmootherArrives("TransientShaper gain back to 0 dB, silence",
                         preparedAt<TransientShaper>([](TransientShaper& t) {
                             t.setAttackDb(12.0f);
                             t.setSustainDb(-12.0f);
                         }),
                         [](TransientShaper&) {}, 1.5, 0.0f,
                         [](const TransientShaper& t) { return t.getLastGainDb() == 0.0f; });
    // Toward a target other than 0 the rounding stall shows as a value a hair short of it
    checkSmootherArrives("Limiter release back to 1 after a loud part",
                         preparedAt<Limiter>([](Limiter& l) { l.setInputGainDb(12.0f); }),
                         [](Limiter& l) { l.setInputGainDb(0.0f); }, 1.5, 0.01f,
                         [](const Limiter& l) { return l.getGainReductionDb() == 0.0f; });
    checkSmootherArrives("Phaser::setCentreHz(1200), depth 0", preparedAt<Phaser>([](Phaser& p) {
                             p.setDepth(0.0f);
                             p.reset();
                         }),
                         [](Phaser& p) { p.setCentreHz(1200.0f); }, 0.5, 0.25f,
                         [](const Phaser& p) { return p.getCurrentCentreHz() == 1200.0f; });
    checkSmootherArrives("Flanger::setDelayMs(5), depth 0", preparedAt<Flanger>([](Flanger& f) {
                             f.setDepthMs(0.0f);
                             f.reset();
                         }),
                         [](Flanger& f) { f.setDelayMs(5.0f); }, 0.5, 0.25f,
                         [](const Flanger& f) { return f.getCurrentDelayMs() == 5.0f; });
    std::printf("a smoother arrives: %d rows, %d failures\n", arriveRows, arriveFailures);
}

// ---- A modulated sweep never rests on its clamp -----------------------------------------------------------------
// The depth is limited where the sweep is used, after the smoothers; the clamp stays as a rail that a sweep inside the
// published ranges never reaches. Resting on it would hold the swept value still for part of every cycle.

// A modulated delay, fully wet, rate 1 Hz (prepared, mono, its smoothers at their targets), fed a ramp
// x[n] = slope * n: cubic interpolation reproduces a straight line, so the delay in use is (x - y) / slope. Returns
// the share of one LFO cycle the delay spends within 0.05 sample of `clampSamples`.
template <typename Fx>
double delayShareAtClamp(Fx& fx, double clampSamples) {
    constexpr double slope = 1.0e-5;
    constexpr long settle = 4000; // longer than the deepest delay (31 ms = 1488 samples)
    const long cycle = static_cast<long>(kSampleRate);
    long atClamp = 0;
    for (long i = 0; i < settle + cycle; ++i) {
        const float x = static_cast<float>(slope * static_cast<double>(i));
        const float y = fx.processSample(0, x);
        if (i >= settle && (static_cast<double>(x) - static_cast<double>(y)) / slope < clampSamples + 0.05)
            ++atClamp;
    }
    return static_cast<double>(atClamp) / static_cast<double>(cycle);
}

// One voice: the share of the cycle at the 2-sample clamp
double chorusShareAtClamp(float centreMs, float depthMs) {
    const auto c = std::make_unique<Chorus>();
    c->prepare(kSampleRate, kBlock, 1);
    c->setVoices(1);
    c->setMix(1.0f);
    c->setRateHz(1.0f);
    c->setCentreDelayMs(centreMs);
    c->setDepthMs(depthMs);
    c->reset(); // the smoothers jump to their targets
    return delayShareAtClamp(*c, 2.0);
}

// The share of one LFO cycle (rate 1 Hz) the phaser's allpass frequency spends on the rail of its clamp, 20 Hz or
// 0.45 fs (getCurrentCentreHz() is the frequency in use)
// One LFO cycle (rate 1 Hz) of a phaser at depth 1: the share of it on the rail [20 Hz, 0.45 fs], and how many
// octaves the frequency sweeps from its lowest to its highest
struct PhaserSweep {
    double onRail = 0.0, octaves = 0.0;
};
PhaserSweep phaserSweep(double sampleRate, float centreHz, float sweepOctaves) {
    const auto p = std::make_unique<Phaser>();
    p->prepare(sampleRate, kBlock, 1);
    p->setRateHz(1.0f);
    p->setDepth(1.0f);
    p->setSweepOctaves(sweepOctaves);
    p->setCentreHz(centreHz);
    p->reset();
    const float top = static_cast<float>(0.45 * sampleRate);
    const long cycle = static_cast<long>(sampleRate);
    long onRail = 0;
    float lowest = top, highest = 0.0f;
    for (long i = 0; i < cycle; ++i) {
        p->processSample(0, 0.0f);
        const float hz = p->getCurrentCentreHz();
        onRail += (hz >= top || hz <= 20.0f) ? 1 : 0;
        lowest = std::min(lowest, hz);
        highest = std::max(highest, hz);
    }
    return {static_cast<double>(onRail) / static_cast<double>(cycle), std::log2(static_cast<double>(highest) / lowest)};
}

void checkSweepRule() {
    // Centre 1 ms with depth 10 ms (both inside the engine's published ranges): without the limit the delay sat at
    // 2 samples for 47% of the cycle, a fixed comb with its first null at fs/4
    const double shortCentre = chorusShareAtClamp(1.0f, 10.0f);
    const double defaults = chorusShareAtClamp(15.0f, 3.0f);
    std::printf("Chorus centre 1 ms, depth 10 ms: %.2f%% of the cycle at the 2-sample clamp (defaults 15/3: %.2f%%)\n",
                100.0 * shortCentre, 100.0 * defaults);
    DAWLAB_CHECK(shortCentre < 0.05);
    DAWLAB_CHECK(defaults == 0.0);

    // Phaser: the engine's top centre, 8000 Hz, swept +-2 octaves would reach 32 kHz; +-6 octaves around 800 Hz
    // reaches below 20 Hz too; and a centre of 20 kHz is above 0.45 fs at 44.1 kHz. Without a limit the frequency sat
    // on the rail for a quarter of the cycle or more. The centre in use moves into the band so that the whole sweep
    // fits, and only a band narrower than the sweep narrows it: the sweep is min(2 x octaves, the band's octaves). It
    // may touch an end at its turning point, where the LFO's float sine rounds to exactly +-1 for a few samples (about
    // 0.01% of the cycle), as the chorus touches its 2 samples at the trough; it never rests there.
    for (const double sr : {48000.0, 44100.0}) {
        const double band = std::log2(0.45 * sr / 20.0);
        struct Row {
            float centre, octaves;
        };
        for (const Row row : {Row{8000.0f, 2.0f}, Row{800.0f, 6.0f}, Row{20000.0f, 2.0f}, Row{30.0f, 3.0f}, Row{1000.0f, 2.0f}}) {
            const PhaserSweep sweep = phaserSweep(sr, row.centre, row.octaves);
            const double wanted = std::min(2.0 * row.octaves, band);
            std::printf("Phaser at %.0f Hz, centre %.0f Hz +-%.0f oct: on the rail for %.2f%% of the cycle, sweeps %.2f "
                        "of %.2f octaves\n",
                        sr, static_cast<double>(row.centre), static_cast<double>(row.octaves), 100.0 * sweep.onRail,
                        sweep.octaves, wanted);
            DAWLAB_CHECK(sweep.onRail < 0.001);
            DAWLAB_CHECK_NEAR(sweep.octaves, wanted, 0.01);
        }
    }

    // Flanger (a pin): its delay runs from the base (at least 0.1 ms, 4.8 samples) upward, never down to its
    // 1-sample clamp
    const auto f = std::make_unique<Flanger>();
    f->prepare(kSampleRate, kBlock, 1);
    f->setFeedback(0.0f);
    f->setMix(1.0f);
    f->setRateHz(1.0f);
    f->setDelayMs(0.1f);
    f->setDepthMs(5.0f);
    f->reset();
    const double flanger = delayShareAtClamp(*f, 1.0);
    std::printf("Flanger delay 0.1 ms, depth 5 ms: %.2f%% of the cycle at the 1-sample clamp\n", 100.0 * flanger);
    DAWLAB_CHECK(flanger == 0.0);
}

} // namespace

// Public functions the rules above do not reach: each against what it promises.
void checkSmallerFunctions() {
    // Fft::coherentGain(type): the theoretical value, against the sum of a long window
    for (const WindowType type : {WindowType::Rectangular, WindowType::Hann, WindowType::Hamming, WindowType::BlackmanHarris}) {
        std::vector<float> w;
        Fft::makeWindow(w, 65536, type);
        DAWLAB_CHECK_NEAR(Fft::coherentGain(type), Fft::coherentGain(w.data(), 65536), 1e-4);
    }

    // Distortion::getOversamplingFactor(): the setting before prepare(), the factor in effect after it
    {
        const auto d = std::make_unique<Distortion>();
        d->setOversamplingFactorLog2(2);
        DAWLAB_CHECK(d->getOversamplingFactor() == 4);
        d->prepare(kSampleRate, kBlock, 1);
        d->setOversamplingFactorLog2(3);
        DAWLAB_CHECK(d->getOversamplingFactor() == 4); // until the next prepare()
        d->prepare(kSampleRate, kBlock, 1);
        DAWLAB_CHECK(d->getOversamplingFactor() == 8);
        d->setOversamplingFactorLog2(0);
        d->prepare(kSampleRate, kBlock, 1);
        DAWLAB_CHECK(d->getOversamplingFactor() == 1);
    }

    // Distortion::processSample(): dry at mix 0; hard clip at mix 1 stays within the output gain
    for (const DistortionType type : {DistortionType::HardClip, DistortionType::Foldback, DistortionType::Fuzz,
                                      DistortionType::Rectify, DistortionType::Bitcrush}) {
        const auto d = std::make_unique<Distortion>();
        d->prepare(kSampleRate, kBlock, 1);
        d->setType(type);
        d->setDriveDb(24.0f);
        d->setMix(0.0f);
        d->reset();
        bool dry = true;
        for (int i = 0; i < 1000; ++i) {
            const float x = 0.5f * std::sin(0.03f * static_cast<float>(i));
            dry = dry && d->processSample(0, x) == x;
        }
        DAWLAB_CHECK(dry);
    }
    {
        const auto d = std::make_unique<Distortion>();
        d->prepare(kSampleRate, kBlock, 1);
        d->setType(DistortionType::HardClip);
        d->setDriveDb(24.0f);
        d->setToneHz(20000.0f);
        d->setMix(1.0f);
        d->setOutputGainDb(-6.0f);
        d->reset();
        float peak = 0.0f, sum = 0.0f;
        for (int i = 0; i < 4800; ++i) {
            const float y = d->processSample(0, 0.5f * std::sin(0.03f * static_cast<float>(i)));
            peak = std::max(peak, std::abs(y));
            sum += std::abs(y);
        }
        // 0.5 x 24 dB clips at 1, times -6 dB: about 0.5, with a little overshoot from the tone filter
        std::printf("Distortion::processSample hard clip, +24 dB drive, -6 dB out: peak %.3f\n", static_cast<double>(peak));
        DAWLAB_CHECK(peak > 0.45f && peak < 0.56f && sum > 0.3f * 4800.0f);
    }

    // Tremolo::processSample(): the same as process(), channel by channel, with autopan
    {
        const auto a = std::make_unique<Tremolo>();
        const auto b = std::make_unique<Tremolo>();
        for (Tremolo* t : {a.get(), b.get()}) {
            t->prepare(kSampleRate, kBlock, 2);
            t->setRateHz(5.0f);
            t->setDepth(0.8f);
            t->setAutoPan(true);
            t->reset();
        }
        AudioBuffer block(2, 2000);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 2000; ++i)
                block.setSample(ch, i, 0.25f);
        b->process(block.toBlock());
        bool same = true;
        for (int i = 0; i < 2000; ++i)
            for (int ch = 0; ch < 2; ++ch)
                same = same && a->processSample(ch, 0.25f) == block.getSample(ch, i);
        DAWLAB_CHECK(same);
    }

    // LoudnessMeter channel weights: what was set comes back; a channel out of range weighs nothing
    {
        const auto m = std::make_unique<LoudnessMeter>();
        m->prepare(kSampleRate, 6);
        m->setChannelWeight(3, 1.41f);
        DAWLAB_CHECK(m->getChannelWeight(3) == 1.41f);
        DAWLAB_CHECK(m->getChannelWeight(-1) == 0.0f && m->getChannelWeight(LoudnessMeter::kMaxChannels) == 0.0f);
    }

    // CombFilter: rt60ForFeedback() undoes feedbackForRt60(); no feedback is no tail; 1 or more never ends
    for (const double rt60 : {0.3, 1.5, 8.0}) {
        const float g = CombFilter::feedbackForRt60(1557, rt60, kSampleRate);
        DAWLAB_CHECK_NEAR(CombFilter::rt60ForFeedback(1557, g, kSampleRate), rt60, rt60 * 1e-4);
    }
    DAWLAB_CHECK(CombFilter::rt60ForFeedback(1557, 0.0f, kSampleRate) == 0.0);
    DAWLAB_CHECK(CombFilter::rt60ForFeedback(1557, 1.0f, kSampleRate) >= 1e30);
}

// ---- batch 6: dsp-filters ----
namespace {

// The RBJ cookbook as printed, before the division by a0
struct CookbookRow {
    double b0, b1, b2, a0, a1, a2;
};

bool followsCookbook(const BiquadCoefficients& c, const CookbookRow& r) {
    const double tol = 1e-12;
    return std::abs(c.b0 - r.b0 / r.a0) < tol && std::abs(c.b1 - r.b1 / r.a0) < tol &&
           std::abs(c.b2 - r.b2 / r.a0) < tol && std::abs(c.a1 - r.a1 / r.a0) < tol &&
           std::abs(c.a2 - r.a2 / r.a0) < tol;
}

bool sameCoefficients(const BiquadCoefficients& a, const BiquadCoefficients& b) {
    return a.b0 == b.b0 && a.b1 == b.b1 && a.b2 == b.b2 && a.a1 == b.a1 && a.a2 == b.a2;
}

// Every factory gives the cookbook's coefficients (Q = 2 and +6 dB, so no term drops out), and a
// frequency above Nyquist is held at 0.499 fs
void checkBiquadFactoriesFollowTheCookbook() {
    using C = BiquadCoefficients;
    const double fs = kSampleRate, f = 1000.0, q = 2.0, gainDb = 6.0;
    const double w0 = kTwoPi * f / fs;
    const double cs = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    const double A = std::pow(10.0, gainDb / 40.0), sa = 2.0 * std::sqrt(A) * alpha;
    DAWLAB_CHECK(followsCookbook(C::makeLowpass(fs, f, q),
                                 {(1.0 - cs) / 2.0, 1.0 - cs, (1.0 - cs) / 2.0, 1.0 + alpha, -2.0 * cs, 1.0 - alpha}));
    DAWLAB_CHECK(followsCookbook(C::makeHighpass(fs, f, q), {(1.0 + cs) / 2.0, -(1.0 + cs), (1.0 + cs) / 2.0,
                                                             1.0 + alpha, -2.0 * cs, 1.0 - alpha}));
    DAWLAB_CHECK(followsCookbook(C::makeBandpass(fs, f, q), {alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * cs, 1.0 - alpha}));
    DAWLAB_CHECK(followsCookbook(C::makeNotch(fs, f, q), {1.0, -2.0 * cs, 1.0, 1.0 + alpha, -2.0 * cs, 1.0 - alpha}));
    DAWLAB_CHECK(followsCookbook(C::makeAllpass(fs, f, q),
                                 {1.0 - alpha, -2.0 * cs, 1.0 + alpha, 1.0 + alpha, -2.0 * cs, 1.0 - alpha}));
    DAWLAB_CHECK(followsCookbook(C::makePeak(fs, f, q, gainDb), {1.0 + alpha * A, -2.0 * cs, 1.0 - alpha * A,
                                                                 1.0 + alpha / A, -2.0 * cs, 1.0 - alpha / A}));
    DAWLAB_CHECK(followsCookbook(C::makeLowShelf(fs, f, q, gainDb),
                                 {A * ((A + 1.0) - (A - 1.0) * cs + sa), 2.0 * A * ((A - 1.0) - (A + 1.0) * cs),
                                  A * ((A + 1.0) - (A - 1.0) * cs - sa), (A + 1.0) + (A - 1.0) * cs + sa,
                                  -2.0 * ((A - 1.0) + (A + 1.0) * cs), (A + 1.0) + (A - 1.0) * cs - sa}));
    DAWLAB_CHECK(followsCookbook(C::makeHighShelf(fs, f, q, gainDb),
                                 {A * ((A + 1.0) + (A - 1.0) * cs + sa), -2.0 * A * ((A - 1.0) + (A + 1.0) * cs),
                                  A * ((A + 1.0) + (A - 1.0) * cs - sa), (A + 1.0) - (A - 1.0) * cs + sa,
                                  2.0 * ((A - 1.0) - (A + 1.0) * cs), (A + 1.0) - (A - 1.0) * cs - sa}));
    DAWLAB_CHECK(sameCoefficients(C::makeLowpass(fs, 30000.0, q), C::makeLowpass(fs, fs * 0.499, q)));
}

// The first two samples of a unit step, worked out by hand from the difference equations
void checkOnePoleStepResponse() {
    const double a = OnePole::coefficientForCutoff(1000.0, kSampleRate);
    OnePole lp;
    lp.prepare(kSampleRate, 64, 1);
    lp.setCutoff(1000.0f);
    DAWLAB_CHECK(lp.getCoefficient() == a);
    DAWLAB_CHECK_NEAR(lp.processSample(0, 1.0f), 1.0 - a, 1e-7);             // (1 - a) x
    DAWLAB_CHECK_NEAR(lp.processSample(0, 1.0f), (1.0 - a) * (1.0 + a), 1e-7); // + a y[n-1]

    const double h = 0.5 * (1.0 + a);
    OnePole hp;
    hp.prepare(kSampleRate, 64, 1);
    hp.setType(OnePole::Type::Highpass);
    hp.setCutoff(1000.0f);
    DAWLAB_CHECK_NEAR(hp.processSample(0, 1.0f), h, 1e-7);     // h (x - 0)
    DAWLAB_CHECK_NEAR(hp.processSample(0, 1.0f), a * h, 1e-7); // h (x - x[n-1]) + a y[n-1]

    // A cutoff above Nyquist is held at 0.49 fs
    lp.setCutoff(30000.0f);
    DAWLAB_CHECK(lp.getCoefficient() == OnePole::coefficientForCutoff(0.49 * kSampleRate, kSampleRate));

    // Prepared with no sample rate there is nothing to design: the filter passes its input
    OnePole idle;
    idle.prepare(0.0, 64, 1);
    idle.setCutoff(500.0f);
    DAWLAB_CHECK(idle.getCoefficient() == 0.0);
    DAWLAB_CHECK(idle.processSample(0, 0.25f) == 0.25f);
}

// The first sample of a unit impulse: v1 = a2, v2 = a3 (Simper's coefficients, worked out here)
void checkStateVariableFilterImpulse() {
    for (const float cutoff : {1000.0f, 30000.0f}) {
        StateVariableFilter svf;
        svf.prepare(kSampleRate, 64, 1);
        svf.setCutoff(cutoff);
        svf.setQ(2.0f);
        const double fc = std::min(static_cast<double>(cutoff), 0.49 * kSampleRate);
        const double g = std::tan(kPi * fc / kSampleRate), k = 1.0 / 2.0;
        const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const StateVariableFilter::Outputs o = svf.processSampleAll(0, 1.0f);
        DAWLAB_CHECK_NEAR(o.low, a3, 1e-6);
        DAWLAB_CHECK_NEAR(o.band, k * a2, 1e-6);
        DAWLAB_CHECK_NEAR(o.high, 1.0 - k * a2 - a3, 1e-6);
        DAWLAB_CHECK_NEAR(o.notch, 1.0 - k * a2, 1e-6);
    }
    // Prepared with no sample rate: no filtering, the highpass and notch outputs are the input
    StateVariableFilter idle;
    idle.prepare(0.0, 64, 1);
    idle.setCutoff(500.0f);
    const StateVariableFilter::Outputs o = idle.processSampleAll(0, 0.25f);
    DAWLAB_CHECK(o.low == 0.0f && o.band == 0.0f && o.high == 0.25f && o.notch == 0.25f);
}

void checkGainPolarityInvert() {
    Gain gain;
    gain.prepare(kSampleRate, 64, 1);
    gain.setGainDb(-6.0f);
    gain.setPolarityInverted(true);
    gain.reset();
    DAWLAB_CHECK(gain.getCurrentGainLinear() == -dbToGain(-6.0f));
    DAWLAB_CHECK(gain.processSample(0, 0.5f) == 0.5f * -dbToGain(-6.0f));
}

// Pan +0.5 (theta = 0.75) under each law, worked out from the formulas in Panner.h
void checkPanLawsOffCentre() {
    const float theta = 0.75f, halfPi = static_cast<float>(kPi) * 0.5f;
    const float cpL = std::sin((1.0f - theta) * halfPi), cpR = std::sin(theta * halfPi);
    float l = 0.0f, r = 0.0f;
    Panner::computeGains(0.5f, PanLaw::Linear, l, r);
    DAWLAB_CHECK_NEAR(l, 0.25, 1e-7);
    DAWLAB_CHECK_NEAR(r, 0.75, 1e-7);
    Panner::computeGains(0.5f, PanLaw::ConstantPower, l, r);
    DAWLAB_CHECK_NEAR(l, cpL, 1e-7);
    DAWLAB_CHECK_NEAR(r, cpR, 1e-7);
    Panner::computeGains(0.5f, PanLaw::Minus4_5dB, l, r);
    DAWLAB_CHECK_NEAR(l, std::sqrt(0.25f * cpL), 1e-7);
    DAWLAB_CHECK_NEAR(r, std::sqrt(0.75f * cpR), 1e-7);
}

void checkSmoothedValueRamps() {
    // A ramp of 4 samples from 0 to 1 walks 0.25, 0.5, 0.75, 1; at rest it is not smoothing, and a new
    // ramp length or one more sample does not start anything
    SmoothedValue v;
    v.reset(1000.0, 0.004);
    v.setCurrentAndTargetValue(0.0f);
    DAWLAB_CHECK(!v.isSmoothing());
    v.setRampLength(4);
    DAWLAB_CHECK(!v.isSmoothing() && v.getRemainingSamples() == 0);
    DAWLAB_CHECK(v.getNextValue() == 0.0f && v.getRemainingSamples() == 0);
    v.setTargetValue(1.0f);
    DAWLAB_CHECK(v.getNextValue() == 0.25f && v.getCurrentValue() == 0.25f);
    DAWLAB_CHECK(v.getNextValue() == 0.5f);
    DAWLAB_CHECK(v.getNextValue() == 0.75f);
    DAWLAB_CHECK(v.getNextValue() == 1.0f && !v.isSmoothing());

    // skip(): 0 samples changes nothing (a 3-sample ramp, whose steps are not exact in float), part of
    // the ramp leaves the rest, more than the rest arrives
    SmoothedValue s;
    s.reset(1000.0, 0.003);
    s.setCurrentAndTargetValue(0.0f);
    s.setTargetValue(1.0f);
    s.skip(0);
    DAWLAB_CHECK(s.getCurrentValue() == 0.0f && s.getRemainingSamples() == 3);
    s.skip(1);
    DAWLAB_CHECK(s.isSmoothing() && s.getRemainingSamples() == 2);
    s.skip(5);
    DAWLAB_CHECK(!s.isSmoothing() && s.getCurrentValue() == 1.0f);

    // With no ramp a new target is taken at once, and a later ramp starts from there
    SmoothedValue j;
    j.reset(1000.0, 0.0);
    j.setCurrentAndTargetValue(0.0f);
    j.setTargetValue(1.0f);
    DAWLAB_CHECK(j.getCurrentValue() == 1.0f && !j.isSmoothing());
    j.setRampLength(4);
    j.setTargetValue(2.0f);
    DAWLAB_CHECK(j.getNextValue() == 1.25f);

    // applyGain(data, n) touches exactly n samples, at rest and while ramping
    SmoothedValue g{0.5f};
    float d[5] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    g.applyGain(d, 4);
    DAWLAB_CHECK(d[0] == 0.5f && d[3] == 0.5f && d[4] == 1.0f);
    g.reset(1000.0, 0.004);
    g.setTargetValue(1.0f); // 0.5 -> 1: 0.625, 0.75, 0.875, 1
    float e[3] = {1.0f, 1.0f, 1.0f};
    g.applyGain(e, 2);
    DAWLAB_CHECK(e[0] == 0.625f && e[1] == 0.75f && e[2] == 1.0f && g.getRemainingSamples() == 2);

    // applyGain(block) while ramping: every channel of a frame gets the same value, only the block's frames
    AudioBuffer buf(2, 3);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 3; ++i)
            buf.getWritePointer(ch)[i] = 1.0f;
    SmoothedValue b{0.5f};
    b.reset(1000.0, 0.004);
    b.setTargetValue(1.0f);
    b.applyGain(AudioBlock(buf).getSubBlock(0, 2));
    for (int ch = 0; ch < 2; ++ch) {
        const float* p = buf.getReadPointer(ch);
        DAWLAB_CHECK(p[0] == 0.625f && p[1] == 0.75f && p[2] == 1.0f);
    }
    DAWLAB_CHECK(b.getRemainingSamples() == 2);
}

// A 1024 Hz tone at 0.5 (1024 Hz is a power of two, so the EQ's log2 / exp2 round trip is exact)
float eqTone(int n) { return 0.5f * static_cast<float>(std::sin(kTwoPi * 1024.0 * n / kSampleRate)); }

// The curve of a cut band is one cookbook section at 12 dB/oct and two Butterworth sections at 24
void checkEqCutSlopesInTheCurve() {
    using C = BiquadCoefficients;
    ParametricEq eq;
    eq.prepare(kSampleRate, 64, 1);
    for (const EqBandType type : {EqBandType::LowCut, EqBandType::HighCut}) {
        const bool low = type == EqBandType::LowCut;
        const double at = low ? 500.0 : 2000.0;
        const auto section = [&](double q) {
            return (low ? C::makeHighpass(kSampleRate, 1024.0, q) : C::makeLowpass(kSampleRate, 1024.0, q))
                .magnitudeDb(at, kSampleRate);
        };
        eq.setBand(0, {type, 1024.0f, 0.9f, 0.0f, true});
        eq.setSlope(0, EqSlope::Db12);
        DAWLAB_CHECK_NEAR(eq.getBandMagnitudeDb(0, at), section(static_cast<double>(0.9f)), 1e-9);
        eq.setSlope(0, EqSlope::Db24);
        DAWLAB_CHECK_NEAR(eq.getBandMagnitudeDb(0, at),
                          section(ParametricEq::kButterworthQ1) + section(ParametricEq::kButterworthQ2), 1e-9);
    }
}

// A slope chosen while a cut band is off is what it runs when switched on: the output is the two
// Butterworth sections, sample for sample
void checkEqCutSlopeReachesTheAudio() {
    using C = BiquadCoefficients;
    for (const EqBandType type : {EqBandType::LowCut, EqBandType::HighCut}) {
        const bool low = type == EqBandType::LowCut;
        ParametricEq eq;
        eq.setSmoothingSeconds(0.0);
        eq.prepare(kSampleRate, 64, 1);
        eq.setBandType(0, type);
        eq.setSlope(0, EqSlope::Db24);
        eq.setBand(0, {type, 1024.0f, 0.707f, 0.0f, true});
        Biquad s1, s2;
        s1.prepare(kSampleRate, 64, 1);
        s2.prepare(kSampleRate, 64, 1);
        s1.setCoefficients(low ? C::makeHighpass(kSampleRate, 1024.0, ParametricEq::kButterworthQ1)
                               : C::makeLowpass(kSampleRate, 1024.0, ParametricEq::kButterworthQ1));
        s2.setCoefficients(low ? C::makeHighpass(kSampleRate, 1024.0, ParametricEq::kButterworthQ2)
                               : C::makeLowpass(kSampleRate, 1024.0, ParametricEq::kButterworthQ2));
        bool same = true;
        for (int i = 0; i < 2000; ++i) {
            const float x = eqTone(i);
            same = same && eq.processSample(0, x) == s2.processSample(0, s1.processSample(0, x));
        }
        DAWLAB_CHECK(same);
    }
}

// Switching a band on: from the next sample it crossfades (20 ms) from the dry signal into a filter
// with fresh memory, then it is the filter alone; a slope change on a Peak band changes nothing
void checkEqBandFadesIn() {
    ParametricEq eq;
    eq.prepare(kSampleRate, 64, 1);
    bool dry = true;
    for (int i = 0; i < 500; ++i)
        dry = dry && eq.processSample(0, eqTone(i)) == eqTone(i);
    DAWLAB_CHECK(dry);

    eq.setBand(2, {EqBandType::Peak, 1024.0f, 1.0f, 12.0f, true});
    Biquad wetPath;
    wetPath.prepare(kSampleRate, 64, 1);
    wetPath.setCoefficients(BiquadCoefficients::makePeak(kSampleRate, 1024.0, 1.0, 12.0));
    SmoothedValue mix;
    mix.reset(kSampleRate, ParametricEq::kDefaultSmoothingSeconds);
    mix.setCurrentAndTargetValue(0.0f);
    mix.setTargetValue(1.0f);
    bool fade = true, on = true;
    for (int i = 0; i < 3000; ++i) {
        if (i == 2000)
            eq.setSlope(2, EqSlope::Db24);
        const float x = eqTone(500 + i);
        const float y = eq.processSample(0, x);
        const float wet = wetPath.processSample(0, x);
        const float m = mix.getNextValue();
        if (m < 1.0f)
            fade = fade && std::abs(y - (x + m * (wet - x))) <= 1e-6f;
        else
            on = on && y == wet;
    }
    DAWLAB_CHECK(fade);
    DAWLAB_CHECK(on);
}

float eqPeak(ParametricEq& eq, int& n, int count, int measureFrom) {
    float peak = 0.0f;
    for (int i = 0; i < count; ++i, ++n) {
        const float y = std::abs(eq.processSample(0, eqTone(n)));
        if (i >= measureFrom)
            peak = std::max(peak, y);
    }
    return peak;
}

// A sounding Peak band changed to a Notch fades out, switches and fades back in as the Notch
void checkEqTypeChangeCompletes() {
    ParametricEq eq;
    eq.prepare(kSampleRate, 64, 1);
    eq.setBand(2, {EqBandType::Peak, 1024.0f, 1.0f, 0.0f, true});
    int n = 0;
    eqPeak(eq, n, 2000, 0);
    eq.setBandType(2, EqBandType::Notch);
    DAWLAB_CHECK(eq.getBand(2).type == EqBandType::Notch);
    DAWLAB_CHECK(eqPeak(eq, n, 9600, 7200) < 0.01f);
}

// A new target in the middle of a ramp starts from where the ramp has got to, even between two
// coefficient updates: +12 dB stopped after 900 of 960 samples is +11.25 dB
void checkEqRampChangedMidway() {
    ParametricEq eq;
    eq.setCoefficientUpdateInterval(1000);
    eq.prepare(kSampleRate, 64, 1);
    eq.setBand(2, {EqBandType::Peak, 1024.0f, 1.0f, 0.0f, true});
    int n = 0;
    eqPeak(eq, n, 1000, 0);
    eq.setBandGainDb(2, 12.0f);
    eqPeak(eq, n, 900, 0);
    eq.setBandGainDb(2, -12.0f);
    DAWLAB_CHECK_NEAR(eqPeak(eq, n, 900, 500), 0.5 * std::pow(10.0, 11.25 / 20.0), 0.05);
}

} // namespace

// ---- batch 6: dsp-analysis ----
// Fft, SpectrumAnalyzer and LoudnessMeter against what they promise: exact values on bin centres, the edges of
// their counters, and the gating rules of BS.1770.
namespace {

// A deterministic complex test signal with energy in every bin
std::complex<float> analysisTestSample(int i) {
    return {static_cast<float>(std::sin(0.7 * i + 0.3)), static_cast<float>(std::cos(1.3 * i * i))};
}

// The largest |a[i] - b[i]| over the first n elements
template <typename A, typename B>
double largestDifference(const std::vector<A>& a, const std::vector<B>& b, int n) {
    double largest = 0.0;
    for (size_t i = 0; i < static_cast<size_t>(n); ++i)
        largest = std::max(largest, static_cast<double>(std::abs(a[i] - b[i])));
    return largest;
}

// Fft::forward() and inverse() agree with the naive DFT at every size from 1 to 64 points
void checkFftMatchesNaiveDft() {
    for (int order = 0; order <= 6; ++order) {
        const Fft fft(order);
        const int n = fft.getSize();
        DAWLAB_CHECK(n == 1 << order && fft.getNumRealBins() == n / 2 + 1);
        std::vector<std::complex<float>> x(static_cast<size_t>(n)), ref(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            x[static_cast<size_t>(i)] = analysisTestSample(i);
        naiveDft(x.data(), ref.data(), n);
        std::vector<std::complex<float>> y = x;
        fft.forward(y.data());
        const double forwardError = largestDifference(y, ref, n);
        fft.inverse(y.data());
        const double roundTripError = largestDifference(y, x, n);
        std::printf("Fft order %d: forward vs naive DFT %.2g, inverse(forward(x)) - x %.2g\n", order, forwardError,
                    roundTripError);
        DAWLAB_CHECK(forwardError < 1e-4 && roundTripError < 1e-5);
    }
}

// forwardReal() gives the naive DFT's first N/2+1 bins (DC and Nyquist included); inverseReal() of them on a fresh
// Fft gives the signal back and writes exactly N samples
void checkFftRealTransforms() {
    for (int order = 1; order <= 6; ++order) {
        const Fft forward(order), inverse(order);
        const int n = forward.getSize();
        std::vector<float> x(static_cast<size_t>(n));
        std::vector<std::complex<float>> xc(static_cast<size_t>(n)), ref(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            x[static_cast<size_t>(i)] = 0.2f + 0.3f * analysisTestSample(i).real() + (i % 2 == 0 ? 0.5f : -0.5f);
            xc[static_cast<size_t>(i)] = x[static_cast<size_t>(i)];
        }
        naiveDft(xc.data(), ref.data(), n);
        std::vector<std::complex<float>> bins(static_cast<size_t>(forward.getNumRealBins()));
        forward.forwardReal(x.data(), bins.data());
        const double binError = largestDifference(bins, ref, n / 2 + 1);
        constexpr float kSentinel = 12345.0f;
        std::vector<float> back(static_cast<size_t>(n) + 1, kSentinel);
        inverse.inverseReal(bins.data(), back.data());
        const double backError = largestDifference(back, x, n);
        std::printf("Fft order %d real: bins vs naive DFT %.2g, inverseReal - x %.2g\n", order, binError, backError);
        DAWLAB_CHECK(binError < 1e-4 && backError < 1e-5 && back[static_cast<size_t>(n)] == kSentinel);
    }
}

// coherentGain(window, size): no window has gain 1; only the first `size` samples count
void checkCoherentGainEdges() {
    const std::vector<float> w = {0.5f, 1.0f, 1.0f, 0.5f, 100.0f};
    DAWLAB_CHECK(Fft::coherentGain(w.data(), 0) == 1.0);
    DAWLAB_CHECK(Fft::coherentGain(w.data(), 4) == 0.75);
}

// Sample i of a 64-point frame: 0.25 DC + a sine on bin 4 (0.5 by default) + 0.125 at Nyquist
float analyzerTestSample(int i, float sineAmplitude = 0.5f) {
    const float nyquist = i % 2 == 0 ? 0.125f : -0.125f;
    return 0.25f + sineAmplitude * static_cast<float>(std::sin(kTwoPi * 4.0 * (i % 16) / 64.0)) + nyquist;
}

// A sine on a bin centre reads its amplitude in dBFS with a rectangular window; DC and Nyquist count once
void checkSpectrumReadsBinCentres() {
    const auto a = std::make_unique<SpectrumAnalyzer>();
    a->prepare(kSampleRate, 6);
    a->setWindowType(WindowType::Rectangular);
    a->setSmoothing(0.0f);
    DAWLAB_CHECK(a->getHopSize() == 32);
    std::vector<float> x(64);
    for (int i = 0; i < 64; ++i)
        x[static_cast<size_t>(i)] = analyzerTestSample(i);
    std::vector<float> db;
    a->pushSamples(x.data(), 63);
    DAWLAB_CHECK(!a->computeSpectrum(db)); // one sample short of a frame
    a->pushSamples(x.data() + 63, 1);
    DAWLAB_CHECK(a->computeSpectrum(db)); // exactly one frame
    DAWLAB_CHECK(db.size() == 33u);
    if (db.size() == 33u) {
        std::printf("SpectrumAnalyzer bins: DC %.3f, bin 4 %.3f, Nyquist %.3f, bin 10 %.1f dB\n",
                    static_cast<double>(db[0]), static_cast<double>(db[4]), static_cast<double>(db[32]),
                    static_cast<double>(db[10]));
        DAWLAB_CHECK_NEAR(db[0], gainToDb(0.25f), 0.01);
        DAWLAB_CHECK_NEAR(db[4], gainToDb(0.5f), 0.01);
        DAWLAB_CHECK_NEAR(db[32], gainToDb(0.125f), 0.01);
        DAWLAB_CHECK(db[10] < -80.0f);
    }
    // The next frame needs exactly one hop of new samples
    a->pushSamples(x.data(), 31);
    DAWLAB_CHECK(!a->computeSpectrum(db));
    a->pushSamples(x.data() + 31, 1);
    DAWLAB_CHECK(a->computeSpectrum(db));
    a->setOverlap(0.75f);
    DAWLAB_CHECK(a->getHopSize() == 16);
}

// The default Hann window is normalized by its coherent gain: a sine on a bin centre still reads its amplitude
void checkSpectrumHannReadsAmplitude() {
    const auto a = std::make_unique<SpectrumAnalyzer>();
    a->prepare(kSampleRate, 6);
    std::vector<float> x(64), db;
    for (int i = 0; i < 64; ++i)
        x[static_cast<size_t>(i)] = 0.5f * static_cast<float>(std::sin(kTwoPi * 8.0 * i / 64.0));
    a->pushSamples(x.data(), 64);
    DAWLAB_CHECK(a->computeSpectrum(db));
    DAWLAB_CHECK(db.size() == 33u && std::abs(db[8] - gainToDb(0.5f)) < 0.1f);
}

// Smoothing: the second frame is alpha * old + (1 - alpha) * new, in linear amplitude
void checkSpectrumSmoothing() {
    const auto a = std::make_unique<SpectrumAnalyzer>();
    a->prepare(kSampleRate, 6);
    a->setWindowType(WindowType::Rectangular);
    a->setSmoothing(0.5f);
    std::vector<float> x(64), db;
    for (int i = 0; i < 64; ++i)
        x[static_cast<size_t>(i)] = analyzerTestSample(i, 0.5f);
    a->pushSamples(x.data(), 64);
    DAWLAB_CHECK(a->computeSpectrum(db));
    for (int i = 0; i < 64; ++i)
        x[static_cast<size_t>(i)] = analyzerTestSample(i, 0.25f);
    a->pushSamples(x.data(), 64);
    DAWLAB_CHECK(a->computeSpectrum(db));
    DAWLAB_CHECK(db.size() == 33u && std::abs(db[4] - gainToDb(0.375f)) < 0.01f);
}

// pushBlock() averages the channels: (2x, 0) reads like x pushed alone; a block with no channels pushes nothing
void checkSpectrumAveragesChannels() {
    const auto mono = std::make_unique<SpectrumAnalyzer>();
    const auto stereo = std::make_unique<SpectrumAnalyzer>();
    AudioBuffer block(2, 64);
    std::vector<float> x(64);
    for (int i = 0; i < 64; ++i) {
        x[static_cast<size_t>(i)] = analyzerTestSample(i);
        block.setSample(0, i, 2.0f * x[static_cast<size_t>(i)]);
        block.setSample(1, i, 0.0f);
    }
    for (SpectrumAnalyzer* a : {mono.get(), stereo.get()}) {
        a->prepare(kSampleRate, 6);
        a->setWindowType(WindowType::Rectangular);
    }
    mono->pushSamples(x.data(), 64);
    stereo->pushBlock(block.toBlock());
    std::vector<float> monoDb, stereoDb;
    DAWLAB_CHECK(mono->computeSpectrum(monoDb) && stereo->computeSpectrum(stereoDb));
    bool same = monoDb.size() == stereoDb.size();
    for (size_t k = 0; same && k < monoDb.size(); ++k)
        same = monoDb[k] < -80.0f ? stereoDb[k] < -80.0f : std::abs(monoDb[k] - stereoDb[k]) < 1e-3f;
    DAWLAB_CHECK(same);
    stereo->pushBlock(AudioBlock(nullptr, 0, 64));
    DAWLAB_CHECK(stereo->getPendingSamples() == 0u);
}

// K-weighting stage 1 at 48 kHz is the standard's Table 1; the conversions and the 100 ms step
void checkLoudnessConstants() {
    const BiquadCoefficients c = LoudnessMeter::makeKWeightingStage1(48000.0);
    const double* t = LoudnessMeter::kStage1Table48k;
    DAWLAB_CHECK(std::abs(c.b0 - t[0]) < 1e-9 && std::abs(c.b1 - t[1]) < 1e-9 && std::abs(c.b2 - t[2]) < 1e-9 &&
                 std::abs(c.a1 - t[3]) < 1e-9 && std::abs(c.a2 - t[4]) < 1e-9);
    DAWLAB_CHECK_NEAR(LoudnessMeter::powerToLufs(10.0), 10.0 - 0.691, 1e-5);
    DAWLAB_CHECK_NEAR(LoudnessMeter::powerToLufs(0.01), -20.0 - 0.691, 1e-5);
    const auto m = std::make_unique<LoudnessMeter>();
    DAWLAB_CHECK(m->getChannelWeight(0) == 1.0f);
    m->prepare(48000.0, 1);
    DAWLAB_CHECK(m->getStepSamples() == 4800);
    m->prepare(44100.0, 1);
    DAWLAB_CHECK(m->getStepSamples() == 4410);
}

// One 100 ms step of a 1 kHz sine (48 kHz, exactly 100 periods) into a mono meter
void loudnessStep(LoudnessMeter& m, AudioBuffer& step, float amplitude) {
    for (int i = 0; i < step.getNumSamples(); ++i)
        step.setSample(0, i, amplitude * static_cast<float>(std::sin(kTwoPi * (i % 48) / 48.0)));
    m.process(step.toBlock());
}

// A steady tone: momentary = short-term = the integrated value of its one block; maxima follow once windows fill
void checkLoudnessSteadyTone() {
    const auto m = std::make_unique<LoudnessMeter>();
    m->prepare(kSampleRate, 1);
    AudioBuffer step(1, 4800);
    for (int s = 0; s < 3; ++s)
        loudnessStep(*m, step, 0.5f);
    DAWLAB_CHECK(m->getIntegratedLufs() == kMinusInfinityDb); // no complete 400 ms block yet
    loudnessStep(*m, step, 0.5f);
    std::printf("LoudnessMeter after 400 ms: momentary %.3f, integrated %.3f LUFS\n",
                static_cast<double>(m->getMomentaryLufs()), static_cast<double>(m->getIntegratedLufs()));
    DAWLAB_CHECK_NEAR(m->getIntegratedLufs(), m->getMomentaryLufs(), 1e-4);
    for (int s = 4; s < 31; ++s)
        loudnessStep(*m, step, 0.5f);
    // 1 kHz sine of 0.5: mean square 0.125, K-weighting +0.691 dB at 1 kHz -> -9.03 LUFS
    std::printf("LoudnessMeter steady 0.5 sine: momentary %.3f, short-term %.3f, integrated %.3f, max S %.3f LUFS\n",
                static_cast<double>(m->getMomentaryLufs()), static_cast<double>(m->getShortTermLufs()),
                static_cast<double>(m->getIntegratedLufs()), static_cast<double>(m->getMaxShortTermLufs()));
    DAWLAB_CHECK_NEAR(m->getMomentaryLufs(), -9.03, 0.05);
    DAWLAB_CHECK_NEAR(m->getShortTermLufs(), m->getMomentaryLufs(), 0.01);
    DAWLAB_CHECK_NEAR(m->getIntegratedLufs(), m->getMomentaryLufs(), 0.01);
    DAWLAB_CHECK_NEAR(m->getMaxShortTermLufs(), m->getShortTermLufs(), 0.01);
}

// Short-term spans exactly the last 30 steps: 30 silent steps then one tone step read 10 log10(30 / 4) below momentary
void checkLoudnessShortTermWindow() {
    const auto m = std::make_unique<LoudnessMeter>();
    m->prepare(kSampleRate, 1);
    AudioBuffer step(1, 4800);
    for (int s = 0; s < 30; ++s)
        loudnessStep(*m, step, 0.0f);
    loudnessStep(*m, step, 0.5f);
    DAWLAB_CHECK_NEAR(m->getMomentaryLufs() - m->getShortTermLufs(), 10.0 * std::log10(30.0 / 4.0), 1e-3);
}

// Integrated = the BS.1770 gates applied to the momentary blocks, with the relative gate judged at the centre of the
// block's 0.1 LU histogram bin. A steady tone is weighted 1 for 12 steps and q for 16; the q sweep puts the quiet
// blocks on both sides of the relative gate, some of them in the bin where the bin centre decides.
void checkLoudnessGatesAtBinCentres() {
    const auto m = std::make_unique<LoudnessMeter>();
    AudioBuffer step(1, 4800);
    int nearGate = 0;
    double worst = 0.0;
    for (int c = 0; c <= 20; ++c) {
        const float qDb = -13.0f - 0.05f * static_cast<float>(c);
        m->prepare(kSampleRate, 1);
        std::vector<float> blocks;
        for (int s = 0; s < 28; ++s) {
            m->setChannelWeight(0, s < 12 ? 1.0f : std::pow(10.0f, 0.1f * qDb)); // a weight scales power
            loudnessStep(*m, step, 0.5f);
            if (s >= 3)
                blocks.push_back(m->getMomentaryLufs());
        }
        double sum = 0.0;
        for (const float l : blocks)
            sum += LoudnessMeter::lufsToPower(l);
        const double gate = LoudnessMeter::powerToLufs(sum / static_cast<double>(blocks.size())) - 10.0;
        double gatedSum = 0.0;
        int gatedCount = 0;
        for (const float l : blocks) {
            const double centre = -70.0 + (std::floor((l + 70.0) / 0.1) + 0.5) * 0.1;
            nearGate += centre >= gate + 0.01 && centre < gate + 0.09 ? 1 : 0;
            if (centre >= gate) {
                gatedSum += LoudnessMeter::lufsToPower(l);
                ++gatedCount;
            }
        }
        const double expected = LoudnessMeter::powerToLufs(gatedSum / gatedCount);
        worst = std::max(worst, std::abs(static_cast<double>(m->getIntegratedLufs()) - expected));
    }
    std::printf("LoudnessMeter gating: %d blocks just above the relative gate, worst error %.2g LU\n", nearGate, worst);
    DAWLAB_CHECK(nearGate > 0 && worst < 1e-3);
}

} // namespace

// ---- batch 6: dsp-dynamics ----
// Exact behaviour of the dynamics processors: the static curves at points inside and outside the knee, the state
// machines sample by sample, and the smoothers step by step against the formula each header documents.
namespace {

// AttackReleaseFilter: Branching falls with the release only; Decoupled holds the peak with the release and then
// smooths it with the attack. A value at the denormal-flush level (1e-15) is kept, below it is flushed.
void checkAttackReleaseFilterSteps() {
    const float a = attackReleaseCoefficient(10.0, kSampleRate);
    const float r = attackReleaseCoefficient(100.0, kSampleRate);
    AttackReleaseFilter branching;
    branching.prepare(kSampleRate);
    branching.setMode(SmoothingMode::Branching);
    branching.setCurrentValue(1.0f);
    DAWLAB_CHECK_NEAR(branching.process(0.0f), r, 1e-7);
    AttackReleaseFilter decoupled;
    decoupled.prepare(kSampleRate);
    decoupled.setMode(SmoothingMode::Decoupled);
    decoupled.setCurrentValue(1.0f);
    DAWLAB_CHECK_NEAR(decoupled.process(0.0f), r + a * (1.0f - r), 1e-7);

    AttackReleaseFilter instant;
    instant.prepare(kSampleRate);
    instant.setAttackMs(0.0f);
    instant.setReleaseMs(0.0f);
    DAWLAB_CHECK(instant.process(1.0e-15f) == 1.0e-15f);
    DAWLAB_CHECK(instant.process(0.5e-15f) == 0.0f);

    // The peak hold keeps 1e-15 as well: the next step decays from it, not from 0
    AttackReleaseFilter hold;
    hold.prepare(kSampleRate);
    hold.setMode(SmoothingMode::Decoupled);
    hold.setAttackMs(0.0f);
    hold.setReleaseMs(0.0f);
    DAWLAB_CHECK(hold.process(4.0e-15f) == 4.0e-15f);
    hold.setAttackMs(0.03f);
    const float a2 = hold.getAttackCoefficient();
    const float y1 = hold.process(1.0e-15f); // peak hold = 1e-15, y between 1e-15 and 4e-15
    DAWLAB_CHECK_NEAR(y1, 1.0e-15f + a2 * 3.0e-15f, 1e-20);
    hold.setReleaseMs(1000.0f);
    const float r2 = hold.getReleaseCoefficient();
    const float held = r2 * 1.0e-15f;
    DAWLAB_CHECK_NEAR(hold.process(0.0f), held + a2 * (y1 - held), 1e-20);
}

// EnvelopeFollower in Rms mode with instant times: sqrt(x^2) = |x|
void checkRmsFollowerOfOneSample() {
    EnvelopeFollower f;
    f.prepare(kSampleRate);
    f.setMode(DetectorMode::Rms);
    f.setAttackMs(0.0f);
    f.setReleaseMs(0.0f);
    DAWLAB_CHECK_NEAR(f.processSample(0.5f), 0.5f, 1e-7);
    DAWLAB_CHECK_NEAR(f.processSample(-0.25f), 0.25f, 1e-7);
}

// Compressor static curve: below, inside and above a 6 dB knee, and a hard knee
void checkCompressorCurve() {
    struct Point { float in, knee, out; };
    const Point points[] = {
        {-10.0f, 0.0f, -17.5f}, {-30.0f, 0.0f, -30.0f},  // hard knee: T + (x - T) / R above, x below
        {-10.0f, 6.0f, -17.5f}, {-25.0f, 6.0f, -25.0f},  // outside the knee
        {-21.0f, 6.0f, -21.25f}, {-19.0f, 6.0f, -20.0f}, // inside: x + (1/R - 1)(x - T + W/2)^2 / (2W)
    };
    for (const Point& p : points)
        DAWLAB_CHECK_NEAR(Compressor::computeOutputDb(p.in, -20.0f, 4.0f, p.knee), p.out, 1e-5);
    DAWLAB_CHECK_NEAR(Compressor::computeGainReductionDb(-10.0f, -20.0f, 4.0f, 0.0f), 7.5f, 1e-5);
}

bool nearRel(float a, float b, float rel) {
    return std::abs(a - b) <= rel * std::max(std::abs(a), std::abs(b));
}

// Compressor with instant times: every sample is multiplied by dbToGain(-GR) and the makeup gain, linked (one GR from
// the louder channel) or not (each channel its own GR)
void checkCompressorAppliesGain() {
    for (const bool link : {true, false}) {
        Compressor c;
        c.prepare(kSampleRate, 64, 2);
        c.setThresholdDb(-20.0f);
        c.setRatio(4.0f);
        c.setKneeDb(0.0f);
        c.setAttackMs(0.0f);
        c.setReleaseMs(0.0f);
        c.setMakeupGainDb(6.0f);
        c.setStereoLink(link);
        c.reset();
        AudioBuffer b(2, 16);
        for (int i = 0; i < 16; ++i) {
            b.getWritePointer(0)[i] = 0.5f;
            b.getWritePointer(1)[i] = -0.25f;
        }
        c.process(b);
        const float grL = Compressor::computeGainReductionDb(gainToDb(0.5f), -20.0f, 4.0f, 0.0f);
        const float grR = link ? grL : Compressor::computeGainReductionDb(gainToDb(0.25f), -20.0f, 4.0f, 0.0f);
        const float makeup = dbToGain(6.0f);
        DAWLAB_CHECK(nearRel(b.getReadPointer(0)[15], 0.5f * dbToGain(-grL) * makeup, 1e-5f));
        DAWLAB_CHECK(nearRel(b.getReadPointer(1)[15], -0.25f * dbToGain(-grR) * makeup, 1e-5f));
        DAWLAB_CHECK_NEAR(c.getGainReductionDb(), grL, 1e-4);

        // A main block without channels processes nothing and leaves the meter as it was
        AudioBlock none(nullptr, 0, 16);
        c.process(none, b);
        DAWLAB_CHECK_NEAR(c.getGainReductionDb(), grL, 1e-4);
    }
}

// Compressor makeup gain: glides up to the new value without passing it
void checkCompressorMakeupGlide() {
    Compressor c;
    c.prepare(kSampleRate, 512, 1);
    AudioBuffer b(1, 20000);
    std::fill_n(b.getWritePointer(0), 20000, 0.01f); // -40 dB, far below the threshold: no compression
    c.setMakeupGainDb(6.0f);
    c.process(b);
    const float target = 0.01f * dbToGain(6.0f);
    const float* y = b.getReadPointer(0);
    bool rising = y[0] > 0.01f;
    for (int i = 1; i < 20000; ++i)
        rising = rising && y[i] >= y[i - 1] && y[i] <= target * 1.000001f;
    DAWLAB_CHECK(rising);
    DAWLAB_CHECK_NEAR(y[19999], target, 1e-7);
}

// Expander static curve (T = -40, R = 2, W = 10) and the gain it returns
void checkExpanderCurve() {
    struct Point { float in, knee, out; };
    const Point points[] = {
        {-30.0f, 10.0f, -30.0f}, // above the knee: untouched
        {-40.0f, 10.0f, -41.25f}, {-43.0f, 10.0f, -46.2f}, // inside: x + (1 - R)(x - T - W/2)^2 / (2W)
        {-60.0f, 10.0f, -80.0f}, // below: T + R (x - T)
        {-50.0f, 0.0f, -60.0f}, {-40.0f, 0.0f, -40.0f}, // hard knee
    };
    for (const Point& p : points)
        DAWLAB_CHECK_NEAR(Expander::computeOutputDb(p.in, -40.0f, 2.0f, p.knee), p.out, 1e-4);
    DAWLAB_CHECK_NEAR(Expander::computeGainDb(-60.0f, -40.0f, 2.0f, 10.0f), -20.0f, 1e-4);
}

// Expander: closed (silence) reads the whole range as gain reduction, after reset() and after a block
void checkExpanderMeterReadsRange() {
    Expander e;
    e.prepare(kSampleRate, 64, 1);
    e.setRangeDb(-50.0f);
    e.reset();
    DAWLAB_CHECK(e.getGainReductionDb() == 50.0f);
    AudioBuffer b(1, 64);
    b.clear();
    e.process(b);
    DAWLAB_CHECK(e.getGainReductionDb() == 50.0f);
}

// Gate: close threshold = threshold - hysteresis; a closed gate reads the whole range as gain reduction
void checkGateThresholdsAndMeter() {
    Gate g;
    g.prepare(kSampleRate, 64, 1);
    DAWLAB_CHECK(g.getGainReductionDb() == 80.0f);
    g.setThresholdDb(-30.0f);
    DAWLAB_CHECK(g.getCloseThresholdDb() == -36.0f);
    g.setHysteresisDb(10.0f);
    DAWLAB_CHECK(g.getCloseThresholdDb() == -40.0f);
    AudioBuffer b(1, 64);
    b.clear();
    g.process(b);
    DAWLAB_CHECK(g.getGainReductionDb() == 80.0f);
}

// An envelope follower set up like the gate's detector, to know the envelope the gate sees
EnvelopeFollower gateDetector(double sampleRate) {
    EnvelopeFollower f;
    f.prepare(sampleRate);
    f.setMode(DetectorMode::Peak);
    f.setAttackMs(Gate::kDetectorAttackMs);
    f.setReleaseMs(20.0f);
    return f;
}

// Gate: opens on the sample whose envelope reaches the threshold; stays open while the envelope equals the close
// threshold and goes to Release (hold 0) on the next sample below it
void checkGateThresholdEdges() {
    EnvelopeFollower f = gateDetector(kSampleRate);
    f.processSample(0.5f);
    f.processSample(0.5f);
    const float third = f.processSample(0.5f);
    Gate g;
    g.prepare(kSampleRate, 64, 1);
    g.setThresholdDb(gainToDb(third));
    g.nextGain(0.5f);
    g.nextGain(0.5f);
    DAWLAB_CHECK(g.getState() == GateState::Closed);
    g.nextGain(0.5f);
    DAWLAB_CHECK(g.getState() == GateState::Attack);

    EnvelopeFollower f2 = gateDetector(kSampleRate);
    for (int i = 0; i < 200; ++i)
        f2.processSample(1.0f);
    float env = 0.0f;
    for (int i = 0; i < 5; ++i)
        env = f2.processSample(0.0f);
    Gate h;
    h.prepare(kSampleRate, 64, 1);
    h.setHoldMs(0.0f);
    h.setThresholdDb(gainToDb(env));
    h.setHysteresisDb(0.0f);
    for (int i = 0; i < 200; ++i)
        h.nextGain(1.0f);
    DAWLAB_CHECK(h.getState() == GateState::Open);
    for (int i = 0; i < 5; ++i)
        h.nextGain(0.0f);
    DAWLAB_CHECK(h.getState() == GateState::Open);
    h.nextGain(0.0f);
    DAWLAB_CHECK(h.getState() == GateState::Release);
}

// Gate: hold lasts exactly holdMs; with 1 dB steps attack and release take exactly range / step samples
void checkGateStateDurations() {
    Gate g;
    g.prepare(kSampleRate, 64, 1);
    g.setDetectorReleaseMs(0.1f);
    g.setHoldMs(1.0f);
    for (int i = 0; i < 200; ++i)
        g.nextGain(1.0f);
    int holdSamples = 0;
    for (int i = 0; i < 400; ++i) {
        g.nextGain(0.0f);
        holdSamples += g.getState() == GateState::Hold ? 1 : 0;
    }
    DAWLAB_CHECK(holdSamples == 48);

    // 32 kHz, 2 ms = 64 samples, range -64 dB: 1 dB per sample
    Gate s;
    s.prepare(32000.0, 64, 1);
    s.setDetectorReleaseMs(0.1f);
    s.setHoldMs(0.0f);
    s.setRangeDb(-64.0f);
    s.setAttackMs(2.0f);
    s.setReleaseMs(2.0f);
    s.reset();
    int attack = 0, open = 0;
    for (int i = 0; i < 200; ++i) {
        s.nextGain(1.0f);
        attack += s.getState() == GateState::Attack ? 1 : 0;
        open += s.getState() == GateState::Open ? 1 : 0;
    }
    DAWLAB_CHECK(attack == 64 && open == 136);
    int release = 0;
    float lastGain = 1.0f;
    for (int i = 0; i < 400; ++i) {
        lastGain = s.nextGain(0.0f);
        release += s.getState() == GateState::Release ? 1 : 0;
    }
    DAWLAB_CHECK(release == 64);
    DAWLAB_CHECK(s.getState() == GateState::Closed && lastGain == dbToGain(-64.0f));
}

// Gate: a range changed while closed is reached by gliding, up with the attack slope and down with the release slope
void checkGateClosedRangeGlide() {
    Gate up;
    up.prepare(kSampleRate, 64, 1);
    up.nextGain(0.0f);
    up.setRangeDb(-40.0f); // attack slope = 40 dB / 48 samples
    DAWLAB_CHECK(nearRel(up.nextGain(0.0f), dbToGain(-80.0f + 40.0f / 48.0f), 1e-5f));

    Gate down;
    down.prepare(kSampleRate, 64, 1);
    down.setRangeDb(-40.0f);
    down.reset();
    down.nextGain(0.0f);
    down.setRangeDb(-80.0f); // release slope = 80 dB / 4800 samples
    DAWLAB_CHECK(nearRel(down.nextGain(0.0f), dbToGain(-40.0f - 80.0f / 4800.0f), 1e-5f));

    // An attack of exactly one sample climbs range-span dB in that sample, not more
    Gate one;
    one.prepare(1000.0, 64, 1);
    one.setAttackMs(1.0f);
    one.setRangeDb(-100.0f);
    one.reset();
    one.nextGain(0.0f);
    one.setRangeDb(-10.0f); // span 10 dB per sample
    DAWLAB_CHECK(nearRel(one.nextGain(0.0f), dbToGain(-90.0f), 1e-5f));
}

// LevelMeter: reset() clears the readings of every channel
void checkLevelMeterResetClearsAllChannels() {
    LevelMeter m;
    m.prepare(kSampleRate, 2);
    AudioBuffer b = constantBlock(64);
    m.process(b);
    DAWLAB_CHECK(m.getPeakDb(1) > -7.0f);
    DAWLAB_CHECK(m.getPeakDb(2) == kMinusInfinityDb && m.getPeakDb(-1) == kMinusInfinityDb);
    m.reset();
    for (int ch = 0; ch < 2; ++ch)
        DAWLAB_CHECK(m.getPeakDb(ch) == kMinusInfinityDb && m.getPeakHoldDb(ch) == kMinusInfinityDb &&
                     m.getRmsDb(ch) == kMinusInfinityDb);
}

// LevelMeter ballistics: the peak jumps only on a larger sample (an equal one lets it keep falling); the hold waits
// exactly holdMs and then falls one step; a hold of 0 falls at once
void checkLevelMeterBallistics() {
    const float d = LevelMeter::decayCoefficient(4800.0f, kSampleRate); // 0.1 dB per sample
    DAWLAB_CHECK_NEAR(LevelMeter::decayCoefficient(20.0f, 1.0), 0.1, 1e-7);
    {
        LevelMeter m;
        m.prepare(kSampleRate, 1);
        m.setDecayDbPerSecond(4800.0f);
        AudioBuffer b(1, 2);
        std::fill_n(b.getWritePointer(0), 2, 0.5f);
        m.process(b);
        DAWLAB_CHECK_NEAR(m.getPeakDb(0), gainToDb(0.5f * d), 1e-4);
    }
    {
        LevelMeter m;
        m.prepare(kSampleRate, 1);
        m.setDecayDbPerSecond(4800.0f);
        m.setPeakHoldMs(0.0f);
        AudioBuffer b(1, 2);
        b.getWritePointer(0)[0] = 1.0f;
        b.getWritePointer(0)[1] = 0.0f;
        m.process(b);
        DAWLAB_CHECK_NEAR(m.getPeakHoldDb(0), gainToDb(d), 1e-4);
    }
    {
        LevelMeter m;
        m.prepare(kSampleRate, 1);
        m.setDecayDbPerSecond(4800.0f);
        m.setPeakHoldMs(1.0f); // 48 samples
        AudioBuffer b(1, 49);
        b.clear();
        b.getWritePointer(0)[0] = 1.0f;
        m.process(b);
        DAWLAB_CHECK(m.getPeakHoldDb(0) == 0.0f);
        AudioBuffer one(1, 1);
        one.clear();
        m.process(one);
        DAWLAB_CHECK_NEAR(m.getPeakHoldDb(0), gainToDb(d), 1e-4);
    }
}

// DeEsser: the frequency is capped at 0.45 fs, the crossover sits at 0.7208 f, a non-finite sample is replaced by 0,
// and loud sibilance is turned down (never up)
void checkDeEsserBasics() {
    DeEsser e;
    e.prepare(kSampleRate, 512, 1);
    e.setFrequency(30000.0f);
    DAWLAB_CHECK_NEAR(e.getFrequency(), 21600.0f, 0.01);
    e.setFrequency(5000.0f);
    DAWLAB_CHECK_NEAR(e.getCrossoverHz(), 5000.0f * 0.7208f, 0.01);

    DeEsser n;
    n.prepare(kSampleRate, 512, 1);
    AudioBuffer bad(1, 4);
    std::fill_n(bad.getWritePointer(0), 4, 0.1f);
    bad.getWritePointer(0)[0] = kNaN;
    n.process(bad);
    DAWLAB_CHECK(bad.getReadPointer(0)[0] == 0.0f);

    DeEsser s;
    s.prepare(kSampleRate, 512, 1);
    s.setFrequency(7000.0f);
    s.setThresholdDb(-30.0f);
    s.setRangeDb(12.0f);
    AudioBuffer b(1, 4800);
    for (int i = 0; i < 4800; ++i)
        b.getWritePointer(0)[i] = 0.5f * static_cast<float>(std::sin(2.0 * kPi * 7000.0 * i / kSampleRate));
    s.process(b);
    DAWLAB_CHECK(s.getGainReductionDb() > 6.0f);
    float peak = 0.0f;
    for (int i = 4320; i < 4800; ++i)
        peak = std::max(peak, std::abs(b.getReadPointer(0)[i]));
    DAWLAB_CHECK(peak < 0.5f * dbToGain(-6.0f));
}

EnvelopeFollower shaperFollower(float attackMs, float releaseMs) {
    EnvelopeFollower f;
    f.prepare(kSampleRate);
    f.setAttackMs(attackMs);
    f.setReleaseMs(releaseMs);
    return f;
}

// How many samples of x the shaper (attack +12 dB, sustain -9 dB) answers with a gain other than the documented
// formula: fast and slow peak envelopes, silence while the slow one is at or below -80 dB, the dB difference d scaled
// by 6 dB (attack for d > 0, sustain otherwise), smoothed with a 0.5 ms one-pole
int shaperMismatches(const std::vector<float>& x) {
    TransientShaper t;
    t.prepare(kSampleRate, 64, 1);
    t.setAttackDb(12.0f);
    t.setSustainDb(-9.0f);
    EnvelopeFollower fast = shaperFollower(TransientShaper::kFastAttackMs, TransientShaper::kFastReleaseMs);
    EnvelopeFollower slow = shaperFollower(TransientShaper::kSlowAttackMs, TransientShaper::kSlowReleaseMs);
    const float k = static_cast<float>(1.0 - std::exp(-1.0 / (0.5 * 0.001 * kSampleRate)));
    float gainDb = 0.0f;
    int wrong = 0;
    for (const float v : x) {
        const float fastDb = gainToDb(fast.processSample(v));
        const float slowDb = gainToDb(slow.processSample(v));
        float target = 0.0f;
        if (slowDb > -80.0f) {
            const float diff = fastDb - slowDb;
            target = diff > 0.0f ? 12.0f * std::min(diff / 6.0f, 1.0f) : -9.0f * std::min(-diff / 6.0f, 1.0f);
        }
        gainDb = smoothToward(gainDb, target, k);
        wrong += nearRel(t.nextGain(v), dbToGain(gainDb), 1e-5f) ? 0 : 1;
    }
    return wrong;
}

void checkTransientShaperFormula() {
    std::vector<float> drums(9600, 0.0f); // three hits with decaying tails, then silence
    for (int i = 0; i < 7200; ++i) {
        const int phase = i % 2400;
        drums[static_cast<size_t>(i)] = phase < 24 ? 0.8f : 0.8f * std::exp(-static_cast<float>(phase) / 300.0f);
    }
    DAWLAB_CHECK(shaperMismatches(drums) == 0);

    // One hit, then silence until the slow envelope lands exactly on -80 dB: that sample counts as silence. Search the
    // hit level for a decay that lands exactly on it.
    std::vector<float> landing;
    for (int candidate = 0; candidate < 4000 && landing.empty(); ++candidate) {
        const float hit = 0.0485f * (1.0f + static_cast<float>(candidate) * 2e-6f);
        EnvelopeFollower slow = shaperFollower(TransientShaper::kSlowAttackMs, TransientShaper::kSlowReleaseMs);
        float db = gainToDb(slow.processSample(hit));
        int silent = 0;
        while (db > -80.0f) {
            db = gainToDb(slow.processSample(0.0f));
            ++silent;
        }
        if (db == -80.0f) {
            landing.assign(static_cast<size_t>(silent) + 1, 0.0f);
            landing[0] = hit;
        }
    }
    DAWLAB_CHECK(!landing.empty() && shaperMismatches(landing) == 0);
}

// SlidingWindowMin returns the minimum of the last `window` values (checked against a plain search), with ties,
// with the window as large as the capacity and smaller, and after the ring wraps
void checkSlidingWindowMin() {
    for (const int capacity : {1, 3, 8}) {
        for (int window = 1; window <= capacity; ++window) {
            SlidingWindowMin w;
            w.prepare(capacity);
            w.setWindowLength(window);
            std::vector<float> history;
            uint32_t seed = 12345u + static_cast<uint32_t>(capacity * 16 + window);
            int wrong = 0;
            for (int n = 0; n < 300; ++n) {
                seed = seed * 1664525u + 1013904223u;
                const float v = static_cast<float>((seed >> 24) % 5) * 0.25f; // 5 levels: many ties
                history.push_back(v);
                float expected = v;
                for (int k = std::max(0, n - window + 1); k <= n; ++k)
                    expected = std::min(expected, history[static_cast<size_t>(k)]);
                wrong += w.push(v) == expected ? 0 : 1;
            }
            DAWLAB_CHECK(wrong == 0);
        }
    }
}

// The limiter's six steps written out plainly (README 2.4): input gain 1, g = min(1, c / peak), release
// toward 1, minimum over L + 1 values, mean over L + 1 values, output = delayed input x mean
std::vector<float> referenceLimiter(const std::vector<float>& x, int L, float ceiling, double releaseMs) {
    const float cRel = static_cast<float>(1.0 - std::exp(-1.0 / (releaseMs * 0.001 * kSampleRate)));
    std::vector<float> gr, m, y;
    float released = 1.0f;
    for (size_t n = 0; n < x.size(); ++n) {
        const float peak = std::abs(x[n]);
        const float g = peak > ceiling ? ceiling / peak : 1.0f;
        released = std::min(g, smoothToward(released, 1.0f, cRel));
        gr.push_back(released);
        float mn = 1.0f;
        double sum = 0.0;
        for (int k = 0; k <= L; ++k)
            if (n >= static_cast<size_t>(k))
                mn = std::min(mn, gr[n - static_cast<size_t>(k)]);
        m.push_back(mn);
        for (int k = 0; k <= L; ++k)
            sum += n >= static_cast<size_t>(k) ? static_cast<double>(m[n - static_cast<size_t>(k)]) : 1.0;
        const float s = static_cast<float>(sum / (L + 1));
        const float delayed = n >= static_cast<size_t>(L) ? x[n - static_cast<size_t>(L)] : 0.0f;
        y.push_back(std::clamp(delayed * s, -ceiling, ceiling));
    }
    return y;
}

void checkLimiterMatchesItsSteps() {
    std::vector<float> x(4000);
    for (size_t n = 0; n < x.size(); ++n) {
        const float burst = (n / 500) % 2 == 0 ? 1.6f : 0.3f;
        x[n] = burst * static_cast<float>(std::sin(0.05 * static_cast<double>(n)));
    }
    struct Setup { float maxMs, lookMs; int L; };
    for (const Setup su : {Setup{20.0f, 1.0f, 48}, Setup{1.0f, 1.0f, 48}, Setup{20.0f, 0.0f, 0}}) {
        Limiter l;
        l.setMaxLookaheadMs(su.maxMs);
        l.prepare(kSampleRate, 4000, 1);
        l.setLookaheadMs(su.lookMs);
        l.setCeilingDb(-3.0f);
        l.setReleaseMs(5.0f);
        DAWLAB_CHECK(l.getLatencySamples() == su.L);
        AudioBuffer b(1, 4000);
        std::copy(x.begin(), x.end(), b.getWritePointer(0));
        l.process(b);
        const std::vector<float> ref = referenceLimiter(x, su.L, dbToGain(-3.0f), 5.0);
        int wrong = 0;
        for (size_t n = 0; n < x.size(); ++n)
            wrong += std::abs(b.getReadPointer(0)[n] - ref[n]) <= 1e-6f ? 0 : 1;
        DAWLAB_CHECK(wrong == 0);
        DAWLAB_CHECK(l.getSafetyClipCount() == 0);
    }
}

// Limiter input gain: a linear ramp over 20 ms that ends exactly on the target; with a one-sample ramp it lands on it
void checkLimiterInputGainRamp() {
    Limiter l;
    l.prepare(kSampleRate, 4000, 1);
    l.setLookaheadMs(0.0f);
    l.setCeilingDb(0.0f);
    AudioBuffer b(1, 4000);
    std::fill_n(b.getWritePointer(0), 4000, 0.1f);
    l.setInputGainDb(6.0f);
    l.process(b);
    const float target = dbToGain(6.0f);
    const float step = (target - 1.0f) / 960.0f;
    const float* y = b.getReadPointer(0);
    DAWLAB_CHECK_NEAR(y[0], 0.1f * (1.0f + step), 1e-6);
    DAWLAB_CHECK_NEAR(y[479], 0.1f * (1.0f + 480.0f * step), 1e-5);
    DAWLAB_CHECK(y[1000] == 0.1f * target && y[3999] == 0.1f * target);

    float db = -6.5f;
    while (1.0f + (dbToGain(db) - 1.0f) == dbToGain(db))
        db -= 0.01f; // a target that 1 + (target - 1) misses by rounding
    Limiter one;
    one.prepare(50.0, 64, 1); // 20 ms = 1 sample
    one.setCeilingDb(20.0f);
    AudioBuffer s(1, 1);
    s.getWritePointer(0)[0] = 1.0f;
    one.process(s);
    one.setInputGainDb(db);
    s.getWritePointer(0)[0] = 1.0f;
    one.process(s);
    DAWLAB_CHECK(s.getReadPointer(0)[0] == dbToGain(db));
}

// Limiter lookahead bound: before prepare it is the value given to setMaxLookaheadMs(); after prepare it is what
// prepare allocated, until the next prepare
void checkLimiterLookaheadBound() {
    Limiter before;
    before.setMaxLookaheadMs(2.0f);
    before.setLookaheadMs(10.0f);
    DAWLAB_CHECK(before.getLookaheadMs() == 2.0f);
    Limiter after;
    after.prepare(kSampleRate, 64, 1);
    after.setMaxLookaheadMs(2.0f);
    after.setLookaheadMs(10.0f);
    DAWLAB_CHECK(after.getLookaheadMs() == 10.0f && after.getLatencySamples() == 480);
}

// Limiter safety clamp: a ceiling lowered during playback is held, positive and negative, and each clearly
// exceeding sample is counted; an overshoot at rounding level (below 1e-6) is clamped but not counted. The gain
// reduction reading is the largest of the block.
void checkLimiterSafetyClamp() {
    Limiter l;
    l.prepare(kSampleRate, 512, 2);
    l.setLookaheadMs(1.0f);
    l.setCeilingDb(0.0f);
    AudioBuffer b(2, 512);
    const auto fill = [&b]() {
        std::fill_n(b.getWritePointer(0), 512, 0.9f);
        std::fill_n(b.getWritePointer(1), 512, -0.9f);
    };
    fill();
    l.process(b);
    DAWLAB_CHECK(l.getSafetyClipCount() == 0);
    l.setCeilingDb(-12.0f);
    fill();
    l.process(b);
    const float c = dbToGain(-12.0f);
    DAWLAB_CHECK(l.getSafetyClipCount() > 0 && l.getSafetyClipCount() < 1024);
    DAWLAB_CHECK(b.getReadPointer(0)[0] == c && b.getReadPointer(1)[0] == -c);

    Limiter t;
    t.prepare(kSampleRate, 4096, 1);
    t.setLookaheadMs(1.0f);
    t.setCeilingDb(-1.0f);
    AudioBuffer x(1, 4096);
    std::fill_n(x.getWritePointer(0), 4096, 2.0f);
    t.process(x);
    DAWLAB_CHECK_NEAR(t.getGainReductionDb(), -gainToDb(dbToGain(-1.0f) / 2.0f), 1e-4);
    const float before = x.getReadPointer(0)[4095];
    t.setCeilingDb(-1.000002f); // a few ulps lower
    const float lower = dbToGain(-1.000002f);
    DAWLAB_CHECK(before > lower && before < lower * (1.0f + 1.0e-6f));
    std::fill_n(x.getWritePointer(0), 4096, 2.0f);
    t.process(x);
    DAWLAB_CHECK(t.getSafetyClipCount() == 0 && x.getReadPointer(0)[0] == lower);
}

#ifdef NDEBUG
// Release build only (a Debug build asserts): an unprepared limiter clears the block, and channels beyond those
// given to prepare() are cleared
void checkLimiterClearsUnpreparedChannels() {
    AudioBuffer b(2, 64);
    std::fill_n(b.getWritePointer(0), 64, 0.5f);
    std::fill_n(b.getWritePointer(1), 64, 0.5f);
    Limiter unprepared;
    unprepared.process(b);
    DAWLAB_CHECK(b.getReadPointer(0)[63] == 0.0f && b.getReadPointer(1)[63] == 0.0f);
    std::fill_n(b.getWritePointer(0), 64, 0.5f);
    std::fill_n(b.getWritePointer(1), 64, 0.5f);
    Limiter mono;
    mono.prepare(kSampleRate, 64, 1);
    mono.process(b);
    DAWLAB_CHECK(std::all_of(b.getReadPointer(1), b.getReadPointer(1) + 64, [](float v) { return v == 0.0f; }));
}
#endif

} // namespace

void checkDspFilters() {
    checkBiquadFactoriesFollowTheCookbook();
    checkOnePoleStepResponse();
    checkStateVariableFilterImpulse();
    checkGainPolarityInvert();
    checkPanLawsOffCentre();
    checkSmoothedValueRamps();
    checkEqCutSlopesInTheCurve();
    checkEqCutSlopeReachesTheAudio();
    checkEqBandFadesIn();
    checkEqTypeChangeCompletes();
    checkEqRampChangedMidway();
}

// ---- batch 6: dsp-distortion ----
// Oversampler, Saturation and Distortion: the exact values of their pieces (halfband design, polyphase paths,
// latency, transfer curves, delay lines, ramps, type crossfade) and the rules that tie them together: without
// oversampling process() is processSample(), one block larger than maxBlockSize is the same audio in pieces, and a
// block with no channels changes nothing
namespace b6_distortion {

constexpr double kRate = 48000.0;

// I0(x) summed to k = 60 with no early stop: the reference for the Kaiser window
double besselI0Reference(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k <= 60; ++k) {
        const double ratio = 0.5 * x / static_cast<double>(k);
        term *= ratio * ratio;
        sum += term;
    }
    return sum;
}

// A fixed pseudo-random signal in [-1, 1)
std::vector<float> noise(int n, unsigned seed) {
    std::vector<float> v(static_cast<size_t>(n));
    for (float& x : v) {
        seed = seed * 1664525u + 1013904223u;
        x = static_cast<float>(seed >> 8) / 8388608.0f - 1.0f;
    }
    return v;
}

float at(const std::vector<float>& v, int i) { return i >= 0 ? v[static_cast<size_t>(i)] : 0.0f; }

double gap(float a, float b) { return std::abs(static_cast<double>(a) - static_cast<double>(b)); }

bool sameSamples(const AudioBuffer& a, const AudioBuffer& b) {
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (a.getSample(ch, i) != b.getSample(ch, i))
                return false;
    return true;
}

AudioBuffer monoOf(const std::vector<float>& v) {
    AudioBuffer b(1, static_cast<int>(v.size()));
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample(0, i, v[static_cast<size_t>(i)]);
    return b;
}

// The halfband FIR is the ideal halfband (1/2 in the centre, sin(pi n/2)/(pi n) on odd taps, 0 on the other even
// taps) times a Kaiser window, and I0 is right to 1e-12
void checkHalfbandDesign() {
    for (const double x : {0.0, 1.0, 4.0, 8.0, 12.0})
        DAWLAB_CHECK_NEAR(dsp::detail::besselI0(x), besselI0Reference(x), 1e-12 * besselI0Reference(x));
    for (const int numOddTaps : {8, 12, 32}) {
        const int M = numOddTaps - 1;
        const std::vector<double> h = HalfbandStage::designHalfband(numOddTaps, 8.0);
        DAWLAB_CHECK(static_cast<int>(h.size()) == 2 * M + 1);
        if (static_cast<int>(h.size()) != 2 * M + 1)
            continue;
        double worst = 0.0;
        for (int n = -M; n <= M; ++n) {
            const double ideal = n == 0 ? 0.5 : (n % 2 == 0 ? 0.0 : std::sin(0.5 * kPi * n) / (kPi * n));
            const double r = static_cast<double>(n) / static_cast<double>(M);
            const double window = besselI0Reference(8.0 * std::sqrt(1.0 - r * r)) / besselI0Reference(8.0);
            worst = std::max(worst, std::abs(h[static_cast<size_t>(n + M)] - ideal * window));
        }
        std::printf("designHalfband(%d odd taps): worst tap error %.3g\n", numOddTaps, worst);
        DAWLAB_CHECK(worst < 1e-12);
    }
}

// HalfbandStage: the polyphase coefficients are 2 h on the odd taps; upsample and downsample are the polyphase
// formulas in its comments, for both decimation phases, with the history carried from one call to the next
void checkHalfbandStage() {
    for (const int numOddTaps : {8, 32}) {
        for (const int phase : {0, 1}) {
            const auto st = std::make_unique<HalfbandStage>();
            st->prepare(numOddTaps, 8.0, 1, phase);
            const int K = numOddTaps / 2, M = 2 * K - 1;
            const std::vector<double> h = HalfbandStage::designHalfband(numOddTaps, 8.0);
            const std::vector<float>& g = st->getPolyphaseCoefficients();
            bool coefficients = static_cast<int>(g.size()) == 2 * K;
            for (int j = 0; coefficients && j < 2 * K; ++j)
                coefficients = g[static_cast<size_t>(j)] ==
                               static_cast<float>(2.0 * h[static_cast<size_t>(2 * (j - K) + 1 + M)]);
            DAWLAB_CHECK(coefficients);
            DAWLAB_CHECK(st->getRoundTripDelay() == 2 * M - phase);
            if (!coefficients)
                continue;

            const int n = 40, first = 15;
            const std::vector<float> x = noise(n, 1u + static_cast<unsigned>(phase));
            std::vector<float> up(static_cast<size_t>(2 * n));
            st->upsample(0, x.data(), up.data(), first);
            st->upsample(0, x.data() + first, up.data() + 2 * first, n - first);
            double worstUp = 0.0;
            for (int i = 0; i < n; ++i) {
                float acc = 0.0f;
                for (int j = 0; j < 2 * K; ++j)
                    acc += g[static_cast<size_t>(j)] * at(x, i - j);
                worstUp = std::max(worstUp, gap(up[static_cast<size_t>(2 * i)], acc));
                worstUp = std::max(worstUp, gap(up[static_cast<size_t>(2 * i + 1)], at(x, i - (K - 1))));
            }

            const std::vector<float> w = noise(2 * n, 7u + static_cast<unsigned>(phase));
            std::vector<float> down(static_cast<size_t>(n));
            st->downsample(0, w.data(), down.data(), first);
            st->downsample(0, w.data() + 2 * first, down.data() + first, n - first);
            double worstDown = 0.0;
            for (int i = 0; i < n; ++i) {
                float acc = 0.0f;
                for (int j = 0; j < 2 * K; ++j)
                    acc += g[static_cast<size_t>(j)] * at(w, 2 * (i - j) + phase);
                const float delayed = phase == 0 ? at(w, 2 * (i - K) + 1) : at(w, 2 * (i - (K - 1)));
                worstDown = std::max(worstDown, gap(down[static_cast<size_t>(i)], 0.5f * (acc + delayed)));
            }
            std::printf("HalfbandStage %d odd taps, phase %d: worst error up %.3g, down %.3g\n", numOddTaps, phase,
                        worstUp, worstDown);
            DAWLAB_CHECK(worstUp < 1e-6 && worstDown < 1e-6);
        }
    }
}

// Oversampler: the stages, their decimation phases and the latency at 1x .. 8x; an impulse through upsample and
// downsample (nothing in between) comes back (at height 0.968 when oversampled) exactly getLatencySamples() later
void checkOversamplerLatency() {
    struct Row {
        int log2, latency;
        int taps[3], phases[3];
    };
    const Row rows[] = {{0, 0, {0, 0, 0}, {0, 0, 0}},
                        {1, 31, {32, 0, 0}, {0, 0, 0}},
                        {2, 36, {32, 12, 0}, {1, 0, 0}},
                        {3, 38, {32, 12, 8}, {0, 1, 0}}};
    for (const Row& r : rows) {
        const auto os = std::make_unique<Oversampler>(r.log2);
        os->prepare(kRate, 256, 1);
        DAWLAB_CHECK(os->getLatencySamples() == r.latency);
        bool stages = os->getNumStages() == r.log2;
        for (int s = 0; stages && s < r.log2; ++s)
            stages = os->getStage(s).getNumOddTaps() == r.taps[s] && os->getStage(s).getDecimationPhase() == r.phases[s];
        DAWLAB_CHECK(stages);
        AudioBuffer b(1, 256);
        b.setSample(0, 0, 1.0f);
        os->upsample(b.toBlock());
        os->downsample(b.toBlock());
        int peakAt = 0;
        for (int i = 1; i < 256; ++i)
            if (std::abs(b.getSample(0, i)) > std::abs(b.getSample(0, peakAt)))
                peakAt = i;
        std::printf("Oversampler %dx: latency %d, impulse back at %d, height %.5f\n", 1 << r.log2,
                    os->getLatencySamples(), peakAt, static_cast<double>(b.getSample(0, peakAt)));
        DAWLAB_CHECK(peakAt == r.latency);
        DAWLAB_CHECK_NEAR(b.getSample(0, peakAt), r.log2 == 0 ? 1.0 : 0.968, 0.001);
    }
}

// Saturation::shape(): the documented curves, at both sides of the knees
void checkSaturationCurves() {
    struct Point {
        SaturationCurve curve;
        float x;
        double y;
    };
    const double cubicHalf = 0.5 - 0.125 / 3.0;
    const Point points[] = {
        {SaturationCurve::Cubic, 0.5f, cubicHalf},       {SaturationCurve::Cubic, -0.5f, -cubicHalf},
        {SaturationCurve::Cubic, 1.0f, 2.0 / 3.0},       {SaturationCurve::Cubic, -1.0f, -2.0 / 3.0},
        {SaturationCurve::Cubic, 3.0f, 2.0 / 3.0},       {SaturationCurve::Cubic, -3.0f, -2.0 / 3.0},
        {SaturationCurve::Arctan, 0.5f, (2.0 / kPi) * std::atan(0.25 * kPi)},
        {SaturationCurve::Arctan, -2.0f, (2.0 / kPi) * std::atan(-kPi)},
        {SaturationCurve::Asymmetric, 0.5f, std::tanh(0.5)}, {SaturationCurve::Asymmetric, -0.5f, 0.5 * std::tanh(-1.0)},
        {SaturationCurve::Tanh, 0.7f, std::tanh(0.7)}};
    for (const Point& p : points)
        DAWLAB_CHECK_NEAR(Saturation::shape(p.curve, p.x), p.y, 1e-6);
    // from |x| = 1 on, Cubic is the hard clip itself: exactly +-2/3 (the polynomial would round to 1 ulp less)
    DAWLAB_CHECK(Saturation::shape(SaturationCurve::Cubic, 1.0f) == 2.0f / 3.0f);
    DAWLAB_CHECK(Saturation::shape(SaturationCurve::Cubic, -1.0f) == -2.0f / 3.0f);
}

// Saturation::process() at 1x without the DC blocker: every sample is dry + (shape(drive x) - dry) mix, on every
// channel alike (a stereo block of exactly maxBlockSize samples that does not start at 0)
void checkSaturationProcessCurves() {
    const int n = 128;
    const float drive = dbToGain(6.0f);
    for (const SaturationCurve curve :
         {SaturationCurve::Tanh, SaturationCurve::Cubic, SaturationCurve::Arctan, SaturationCurve::Asymmetric}) {
        for (const float mix : {1.0f, 0.5f}) {
            const auto s = std::make_unique<Saturation>();
            s->setOversamplingFactorLog2(0);
            s->setCurve(curve);
            s->setDriveDb(6.0f);
            s->setMix(mix);
            s->setDcBlockerEnabled(false);
            s->prepare(kRate, n, 2);
            AudioBuffer b(2, n);
            for (int i = 0; i < n; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    b.setSample(ch, i, 0.6f * std::sin(0.05f * static_cast<float>(i) + 0.3f));
            const AudioBuffer in = b;
            s->process(b.toBlock());
            double worst = 0.0;
            bool channelsAlike = true;
            for (int i = 0; i < n; ++i) {
                const float x = in.getSample(0, i);
                const float y = x + (Saturation::shape(curve, x * drive) - x) * mix;
                worst = std::max(worst, static_cast<double>(std::abs(b.getSample(0, i) - y)));
                channelsAlike = channelsAlike && b.getSample(0, i) == b.getSample(1, i);
            }
            std::printf("Saturation %s, mix %.1f at 1x: worst error %.3g\n", saturationCurveName(curve),
                        static_cast<double>(mix), worst);
            DAWLAB_CHECK(worst < 1e-6 && channelsAlike);
        }
    }
}

// Saturation at mix 0: the output is the input delayed by exactly getLatencySamples() (0, 31, 36 at 1x, 2x, 4x)
void checkSaturationDryDelay() {
    const int expected[] = {0, 31, 36};
    for (int log2 = 0; log2 <= 2; ++log2) {
        const auto s = std::make_unique<Saturation>();
        s->setOversamplingFactorLog2(log2);
        s->setDriveDb(12.0f);
        s->setMix(0.0f);
        s->prepare(kRate, 256, 1);
        DAWLAB_CHECK(s->getLatencySamples() == expected[log2]);
        const std::vector<float> x = noise(600, 3u);
        AudioBuffer b = monoOf(x);
        for (int start = 0; start < 600; start += 200)
            s->process(b.toBlock().getSubBlock(start, 200));
        bool delayed = true;
        for (int i = 0; i < 600; ++i)
            delayed = delayed && b.getSample(0, i) == at(x, i - s->getLatencySamples());
        DAWLAB_CHECK(delayed);
    }
}

// Saturation's mix moves over 20 ms (960 samples at 48 kHz): halfway after 480, all the way after 960
void checkSaturationRamp() {
    const auto s = std::make_unique<Saturation>();
    s->setOversamplingFactorLog2(0);
    s->setDcBlockerEnabled(false);
    s->prepare(kRate, 256, 1);
    AudioBuffer b(1, 256);
    auto run = [&] {
        for (int i = 0; i < 256; ++i)
            b.setSample(0, i, 0.5f);
        s->process(b.toBlock());
    };
    run();
    DAWLAB_CHECK_NEAR(b.getSample(0, 255), std::tanh(0.5), 1e-6);
    s->setMix(0.0f);
    run();
    run();
    const double half = 0.5 * (std::tanh(0.5) + 0.5);
    std::printf("Saturation mix 1 -> 0: sample 479 %.5f (halfway %.5f)\n", static_cast<double>(b.getSample(0, 479 - 256)),
                half);
    DAWLAB_CHECK_NEAR(b.getSample(0, 479 - 256), half, 1e-3);
    run();
    run();
    DAWLAB_CHECK(b.getSample(0, 255) == 0.5f);
}

// Saturation: a block larger than maxBlockSize is the same audio as the same samples in maxBlockSize pieces, and
// a block with no channels changes nothing (not even a ramp)
void checkSaturationBlocks() {
    auto make = [] {
        auto s = std::make_unique<Saturation>();
        s->setDriveDb(12.0f);
        s->setMix(0.5f);
        s->prepare(kRate, 64, 1);
        return s;
    };
    {
        const auto a = make();
        const auto b = make();
        const std::vector<float> x = noise(100, 5u);
        AudioBuffer whole = monoOf(x);
        a->process(whole.toBlock());
        AudioBuffer first = monoOf(std::vector<float>(x.begin(), x.begin() + 64));
        AudioBuffer second = monoOf(std::vector<float>(x.begin() + 64, x.end()));
        b->process(first.toBlock());
        b->process(second.toBlock());
        bool same = true;
        for (int i = 0; i < 100; ++i)
            same = same && whole.getSample(0, i) == (i < 64 ? first.getSample(0, i) : second.getSample(0, i - 64));
        DAWLAB_CHECK(same);
    }
    {
        const auto a = make();
        const auto b = make();
        AudioBuffer ba = monoOf(noise(64, 6u)), bb = ba;
        a->process(ba.toBlock());
        b->process(bb.toBlock());
        a->setDriveDb(24.0f);
        b->setDriveDb(24.0f);
        a->process(AudioBlock(nullptr, 0, 64));
        ba = monoOf(noise(64, 8u));
        bb = ba;
        a->process(ba.toBlock());
        b->process(bb.toBlock());
        DAWLAB_CHECK(sameSamples(ba, bb));
    }
}

// Distortion's static curves at both sides of every knee
void checkDistortionCurves() {
    const float fold[][2] = {{0.0f, 0.0f},   {0.5f, 0.5f},   {1.0f, 1.0f},   {1.5f, 0.5f},   {2.0f, 0.0f},
                             {2.5f, -0.5f},  {3.0f, -1.0f},  {3.5f, -0.5f},  {4.5f, 0.5f},   {-1.0f, -1.0f},
                             {-1.5f, -0.5f}, {-2.5f, 0.5f},  {-3.5f, 0.5f},  {-4.5f, -0.5f}};
    for (const auto& p : fold)
        DAWLAB_CHECK_NEAR(Distortion::foldback(p[0]), p[1], 1e-6);
    const float fuzz[][2] = {{0.0f, 0.0f},   {0.05f, 0.0f}, {-0.05f, 0.0f}, {0.3f, 0.2f},
                             {-0.3f, -0.2f}, {2.0f, 1.0f},  {-2.0f, -0.6f}, {-0.5f, -0.4f}};
    for (const auto& p : fuzz)
        DAWLAB_CHECK_NEAR(Distortion::fuzz(p[0]), p[1], 1e-6);
    DAWLAB_CHECK(Distortion::rectify(-0.5f, RectifyMode::FullWave) == 0.5f);
    DAWLAB_CHECK(Distortion::rectify(-0.5f, RectifyMode::HalfWave) == 0.0f);
    DAWLAB_CHECK(Distortion::rectify(0.5f, RectifyMode::HalfWave) == 0.5f);
    DAWLAB_CHECK(Distortion::rectify(-2.0f, RectifyMode::FullWave) == 1.0f);
    DAWLAB_CHECK(Distortion::rectify(2.0f, RectifyMode::HalfWave) == 1.0f);
    // quantize(u, bits): steps of 2 / 2^bits, rounded to the nearest, codes -M .. M - 1 with M = floor(2^(bits - 1))
    const float q[][3] = {{0.3f, 3.0f, 0.25f},   {0.4f, 3.0f, 0.5f},     {1.0f, 3.0f, 0.75f}, {-1.0f, 3.0f, -1.0f},
                          {-2.0f, 3.0f, -1.0f},  {0.9f, 1.5f, 0.0f},     {0.3f, 1.0f, 0.0f},  {-0.3f, 1.0f, 0.0f},
                          {-0.9f, 1.5f, -0.70710678f}};
    for (const auto& p : q)
        DAWLAB_CHECK_NEAR(Distortion::quantize(p[0], p[1]), p[2], 1e-6);
}

std::unique_ptr<Distortion> makeDistortion(int log2, DistortionType type, int maxBlock) {
    auto d = std::make_unique<Distortion>();
    d->setOversamplingFactorLog2(log2);
    d->setType(type);
    d->prepare(kRate, maxBlock, 1);
    return d;
}

// Distortion at mix 0: the output is the input delayed by exactly getLatencySamples() (0, 31, 36 at 1x, 2x, 4x);
// at 1x with no drive and the tone at 20 kHz (bypass), a hard clip of a quiet signal is the signal itself
void checkDistortionDelayLines() {
    const int expected[] = {0, 31, 36};
    for (int log2 = 0; log2 <= 2; ++log2) {
        const auto d = std::make_unique<Distortion>();
        d->setOversamplingFactorLog2(log2);
        d->setDriveDb(24.0f);
        d->setMix(0.0f);
        d->prepare(kRate, 256, 1);
        DAWLAB_CHECK(d->getLatencySamples() == expected[log2]);
        const std::vector<float> x = noise(600, 9u);
        AudioBuffer b = monoOf(x);
        for (int start = 0; start < 600; start += 200)
            d->process(b.toBlock().getSubBlock(start, 200));
        bool delayed = true;
        for (int i = 0; i < 600; ++i)
            delayed = delayed && b.getSample(0, i) == at(x, i - d->getLatencySamples());
        DAWLAB_CHECK(delayed);
    }
    const auto d = makeDistortion(0, DistortionType::HardClip, 256);
    d->setToneHz(Distortion::kToneBypassHz);
    std::vector<float> x(256);
    for (int i = 0; i < 256; ++i)
        x[static_cast<size_t>(i)] = 0.5f * std::sin(0.3f * static_cast<float>(i));
    AudioBuffer b = monoOf(x);
    d->process(b.toBlock());
    d->process(b.toBlock());
    bool same = true;
    for (int i = 0; i < 256; ++i)
        same = same && b.getSample(0, i) == x[static_cast<size_t>(i)];
    DAWLAB_CHECK(same);
}

// Without oversampling and with no parameter moving, process() gives what processSample() gives, sample for sample,
// for every type (the type set before prepare()), with and without the tone filter; the block comes in pieces of 100
void checkDistortionProcessIsProcessSample() {
    const int n = 1000;
    struct Config {
        DistortionType type;
        RectifyMode mode;
    };
    const Config configs[] = {{DistortionType::HardClip, RectifyMode::FullWave},
                              {DistortionType::Foldback, RectifyMode::FullWave},
                              {DistortionType::Fuzz, RectifyMode::FullWave},
                              {DistortionType::Rectify, RectifyMode::FullWave},
                              {DistortionType::Rectify, RectifyMode::HalfWave},
                              {DistortionType::Bitcrush, RectifyMode::FullWave}};
    for (const Config& c : configs) {
        for (const float tone : {3000.0f, Distortion::kToneBypassHz}) {
            std::unique_ptr<Distortion> d[2];
            for (auto& one : d) {
                one = std::make_unique<Distortion>();
                one->setOversamplingFactorLog2(0);
                one->setType(c.type);
                one->setRectifyMode(c.mode);
                one->setDriveDb(12.0f);
                one->setToneHz(tone);
                one->setBitDepth(6.0f);
                one->setDownsampleFactor(3);
                one->prepare(kRate, 512, 1);
            }
            std::vector<float> x(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i)
                x[static_cast<size_t>(i)] = 0.1f + 0.3f * std::sin(0.02f * static_cast<float>(i));
            AudioBuffer b = monoOf(x);
            for (int start = 0; start < n; start += 100)
                d[0]->process(b.toBlock().getSubBlock(start, 100));
            double worst = 0.0;
            for (int i = 0; i < n; ++i)
                worst = std::max(worst, gap(b.getSample(0, i), d[1]->processSample(0, x[static_cast<size_t>(i)])));
            std::printf("Distortion %s, tone %.0f Hz: process() vs processSample() worst %.3g\n",
                        distortionTypeName(c.type), static_cast<double>(tone), worst);
            DAWLAB_CHECK(worst < 1e-6);  // not == 0: a compiler may fuse a multiply-add (FMA) on one path only
        }
    }
}

// Distortion's drive, output gain and mix move linearly across one block, on both paths (HardClip and Bitcrush)
void checkDistortionRamps() {
    const int n = 64;
    for (const DistortionType type : {DistortionType::HardClip, DistortionType::Bitcrush}) {
        const auto d = std::make_unique<Distortion>();
        d->setOversamplingFactorLog2(0);
        d->setType(type);
        d->setBitDepth(24.0f);
        d->prepare(kRate, n, 1);
        AudioBuffer b(1, n);
        auto run = [&] {
            for (int i = 0; i < n; ++i)
                b.setSample(0, i, 0.25f);
            d->process(b.toBlock());
        };
        auto wet = [type](float u) { return type == DistortionType::Bitcrush ? Distortion::quantize(u, 24.0f) : u; };
        run();
        DAWLAB_CHECK(b.getSample(0, n - 1) == 0.25f);
        d->setDriveDb(6.0f);
        d->setOutputGainDb(-6.0f);
        d->setMix(0.5f);
        run();
        const float g1 = dbToGain(6.0f), o1 = dbToGain(-6.0f);
        const float gInc = (g1 - 1.0f) / static_cast<float>(n), oInc = (o1 - 1.0f) / static_cast<float>(n),
                    mInc = (0.5f - 1.0f) / static_cast<float>(n);
        double worst = 0.0;
        for (int i = 0; i < n; ++i) {
            const float fi = static_cast<float>(i);
            const float m = 1.0f + mInc * fi;
            const float y = 0.25f * (1.0f - m) + wet(0.25f * (1.0f + gInc * fi)) * (1.0f + oInc * fi) * m;
            worst = std::max(worst, static_cast<double>(std::abs(b.getSample(0, i) - y)));
        }
        run();
        const float settled = 0.25f * 0.5f + wet(0.25f * g1) * o1 * 0.5f;
        std::printf("Distortion %s ramps: worst error %.3g, then %.6f (expected %.6f)\n", distortionTypeName(type),
                    worst, static_cast<double>(b.getSample(0, 10)), static_cast<double>(settled));
        DAWLAB_CHECK(worst < 1e-6);
        DAWLAB_CHECK_NEAR(b.getSample(0, 10), settled, 1e-6);
    }
}

// Distortion's sample & hold: with a factor of 3 every input is held for 3 samples, across blocks of 7
void checkDistortionSampleAndHold() {
    const auto d = std::make_unique<Distortion>();
    d->setOversamplingFactorLog2(0);
    d->setType(DistortionType::Bitcrush);
    d->setBitDepth(24.0f);
    d->setDownsampleFactor(3);
    d->prepare(kRate, 64, 1);
    std::vector<float> x(210);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = 0.001f * static_cast<float>(i);
    AudioBuffer b = monoOf(x);
    for (int start = 0; start < 210; start += 7)
        d->process(b.toBlock().getSubBlock(start, 7));
    double worst = 0.0;
    for (int i = 0; i < 210; ++i)
        worst = std::max(worst, static_cast<double>(std::abs(b.getSample(0, i) - x[static_cast<size_t>(3 * (i / 3))])));
    DAWLAB_CHECK(worst < 1e-6);
}

// A type change crossfades the two curves linearly over kTypeFadeMs (480 samples at 48 kHz), across blocks; with
// oversampling the fade takes as long in original samples
void checkDistortionTypeFade() {
    {
        const auto d = makeDistortion(0, DistortionType::HardClip, 256);
        AudioBuffer b(1, 256);
        auto run = [&] {
            for (int i = 0; i < 256; ++i)
                b.setSample(0, i, 1.5f);
            d->process(b.toBlock());
        };
        run();
        DAWLAB_CHECK(b.getSample(0, 255) == 1.0f); // hard clip of 1.5
        d->setType(DistortionType::Foldback);
        double worst = 0.0;
        for (int k = 0; k < 3; ++k) {
            run();
            for (int i = 0; i < 256; ++i) {
                const float done = static_cast<float>(256 * k);
                const float w = std::min(1.0f, (done + static_cast<float>(i) + 1.0f) * (1.0f / 480.0f));
                worst = std::max(worst, static_cast<double>(std::abs(b.getSample(0, i) - (1.0f + (0.5f - 1.0f) * w))));
            }
        }
        std::printf("Distortion HardClip -> Foldback fade at 1x: worst error %.3g\n", worst);
        DAWLAB_CHECK(worst < 1e-6);
    }
    {
        const auto d = makeDistortion(2, DistortionType::HardClip, 512);
        AudioBuffer b(1, 512);
        auto run = [&] {
            for (int i = 0; i < 512; ++i)
                b.setSample(0, i, 1.5f);
            d->process(b.toBlock());
        };
        run();
        run();
        d->setType(DistortionType::Foldback);
        run();
        std::printf("Distortion HardClip -> Foldback fade at 4x: %.4f %.4f %.4f %.4f\n",
                    static_cast<double>(b.getSample(0, 60)), static_cast<double>(b.getSample(0, 240)),
                    static_cast<double>(b.getSample(0, 400)), static_cast<double>(b.getSample(0, 511)));
        DAWLAB_CHECK(b.getSample(0, 60) > 0.9f);
        DAWLAB_CHECK(b.getSample(0, 240) > 0.7f && b.getSample(0, 240) < 0.8f);
        DAWLAB_CHECK_NEAR(b.getSample(0, 511), 0.5, 0.01);
    }
}

// Distortion: a block larger than maxBlockSize is the same audio as the same samples in maxBlockSize pieces; a block
// with no channels changes nothing (not even a ramp); process() before prepare() leaves the block as it is
void checkDistortionBlocks() {
    for (const DistortionType type : {DistortionType::HardClip, DistortionType::Bitcrush}) {
        std::unique_ptr<Distortion> d[2];
        for (auto& one : d) {
            one = std::make_unique<Distortion>();
            one->setOversamplingFactorLog2(1);
            one->setType(type);
            one->setDriveDb(12.0f);
            one->setToneHz(3000.0f);
            one->setMix(0.5f);
            one->setBitDepth(6.0f);
            one->setDownsampleFactor(3);
            one->prepare(kRate, 64, 1);
        }
        const std::vector<float> x = noise(100, 11u);
        AudioBuffer whole = monoOf(x);
        d[0]->process(whole.toBlock());
        AudioBuffer first = monoOf(std::vector<float>(x.begin(), x.begin() + 64));
        AudioBuffer second = monoOf(std::vector<float>(x.begin() + 64, x.end()));
        d[1]->process(first.toBlock());
        d[1]->process(second.toBlock());
        bool same = true;
        for (int i = 0; i < 100; ++i)
            same = same && whole.getSample(0, i) == (i < 64 ? first.getSample(0, i) : second.getSample(0, i - 64));
        DAWLAB_CHECK(same);
    }
    {
        const auto a = makeDistortion(0, DistortionType::HardClip, 64);
        const auto b = makeDistortion(0, DistortionType::HardClip, 64);
        AudioBuffer ba = monoOf(noise(64, 12u)), bb = ba;
        a->process(ba.toBlock());
        b->process(bb.toBlock());
        a->setDriveDb(12.0f);
        b->setDriveDb(12.0f);
        a->process(AudioBlock(nullptr, 0, 64));
        ba = monoOf(noise(64, 13u));
        bb = ba;
        a->process(ba.toBlock());
        b->process(bb.toBlock());
        DAWLAB_CHECK(sameSamples(ba, bb));
    }
    {
        const auto d = std::make_unique<Distortion>();
        AudioBuffer b = monoOf(std::vector<float>(64, 0.5f));
        d->process(b.toBlock());
        bool same = true;
        for (int i = 0; i < 64; ++i)
            same = same && b.getSample(0, i) == 0.5f;
        DAWLAB_CHECK(same);
    }
}

} // namespace b6_distortion
// ---- end batch 6: dsp-distortion ----

// ---- batch 6: dsp-time ----
// Exact behaviour of the time-based effects: delay lines and echo timing, the LFO's shapes and steps, the modulated
// effects' sweep ends, the FDN reverb against its own formulas, partitioned convolution against direct convolution,
// and the reverb building blocks.
#include <dawlab/dsp/DelayLine.h>
#include <dawlab/dsp/Lfo.h>

namespace b6time {

AudioBuffer impulseBuffer(int numChannels, int numSamples, float amplitude = 1.0f) {
    AudioBuffer b(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch) {
        std::fill(b.getWritePointer(ch), b.getWritePointer(ch) + numSamples, 0.0f);
        b.getWritePointer(ch)[0] = amplitude;
    }
    return b;
}

// Every sample of channel ch is 0 except those listed (index, value), which match within tol
bool onlyAt(const AudioBuffer& b, int ch, std::vector<std::pair<int, float>> expected, double tol = 1.0e-5) {
    const float* d = b.getReadPointer(ch);
    for (int i = 0; i < b.getNumSamples(); ++i) {
        float want = 0.0f;
        for (const auto& e : expected)
            if (e.first == i)
                want = e.second;
        if (!(std::abs(static_cast<double>(d[i]) - static_cast<double>(want)) <= tol)) {
            std::printf("  ch %d sample %d: %g, expected %g\n", ch, i, static_cast<double>(d[i]),
                        static_cast<double>(want));
            return false;
        }
    }
    return true;
}

void fillNoise(AudioBuffer& b, uint32_t seed) {
    NoiseGenerator noise(seed);
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.getWritePointer(ch)[i] = 0.5f * noise.next();
}

// ---- DelayLine ---------------------------------------------------------------
void checkDelayLineReadsEachChannelBack() {
    DelayLine line;
    line.prepare(4, 2);
    for (int i = 1; i <= 9; ++i) {
        line.push(0, static_cast<float>(i));
        line.push(1, static_cast<float>(100 + i));
    }
    bool ok = true;
    for (int d = 0; d <= 4; ++d)
        ok = ok && line.read(0, d) == static_cast<float>(9 - d) && line.read(1, d) == static_cast<float>(109 - d);
    std::printf("DelayLine reads d = 0..4 back on both channels: %s\n", ok ? "yes" : "no");
    DAWLAB_CHECK(ok);
    DAWLAB_CHECK(line.read(0, 9) == 5.0f); // above the maximum: clamped to 4
}

// ---- Lfo ---------------------------------------------------------------------
void checkLfoShapesAndSteps() {
    DAWLAB_CHECK(Lfo::shapeValue(LfoShape::Triangle, 0.125f) == 0.5f);
    DAWLAB_CHECK(Lfo::shapeValue(LfoShape::Triangle, 0.5f) == 0.0f);
    DAWLAB_CHECK(Lfo::shapeValue(LfoShape::Triangle, 0.875f) == -0.5f);
    DAWLAB_CHECK(Lfo::shapeValue(LfoShape::Square, 0.25f) == 1.0f);
    DAWLAB_CHECK(Lfo::shapeValue(LfoShape::Square, 0.5f) == -1.0f);
    // -1e-9 wraps to 1.0f in float, which is the start of the next cycle: phase 0, not 1
    DAWLAB_CHECK(Lfo::shapeValue(LfoShape::Saw, -1.0e-9f) == -1.0f);

    // Sample & hold at fs / 4: the phase reaches exactly 1.0 every 4 samples, and each held value lasts one cycle
    Lfo hold;
    hold.setShape(LfoShape::SampleAndHold);
    hold.setSmoothingMs(0.0f);
    hold.setRateHz(12000.0f);
    hold.prepare(48000.0);
    std::vector<float> v(400);
    for (float& x : v)
        x = hold.getNextValue();
    bool held = true;
    for (size_t i = 0; i < v.size(); ++i)
        held = held && (i % 4 == 0 ? (i == 0 || v[i] != v[i - 1]) : v[i] == v[i - 1]);
    std::printf("Lfo      S&H at fs/4 holds each value for exactly 4 samples: %s\n", held ? "yes" : "no");
    DAWLAB_CHECK(held);

    // Smoothing 0: every shape returns its exact waveform value, even right after a jump
    Lfo saw;
    saw.setShape(LfoShape::Saw);
    saw.setSmoothingMs(0.0f);
    saw.setRateHz(24000.1f);
    saw.prepare(48000.0);
    bool exact = true;
    for (int i = 0; i < 64; ++i) {
        const float p = saw.getPhase();
        exact = exact && saw.getNextValue() == Lfo::shapeValue(LfoShape::Saw, p);
    }
    DAWLAB_CHECK(exact);

    // Default smoothing (1 ms = 48 samples): a jump moves by k = 1 - exp(-1/48) of the way in one sample
    const float k = static_cast<float>(1.0 - std::exp(-1.0 / 48.0));
    Lfo square;
    square.setShape(LfoShape::Square);
    square.setRateHz(0.0f);
    square.prepare(48000.0); // at phase 0: +1
    square.setPhase(0.5f);   // -1 from now on
    DAWLAB_CHECK_NEAR(square.getNextValue(), 1.0f - 2.0f * k, 1e-6);
    Lfo ramp;
    ramp.setShape(LfoShape::Saw);
    ramp.setRateHz(0.0f);
    ramp.prepare(48000.0); // at phase 0: -1
    ramp.setPhase(0.75f);  // +0.5 from now on
    DAWLAB_CHECK_NEAR(ramp.getNextValue(), -1.0f + 1.5f * k, 1e-6);
    Lfo smoothHold;
    smoothHold.setShape(LfoShape::SampleAndHold);
    smoothHold.setRateHz(12000.0f);
    smoothHold.prepare(48000.0);
    float s[7];
    for (float& x : s)
        x = smoothHold.getNextValue();
    DAWLAB_CHECK(s[0] == s[3] && s[4] != s[5] && s[5] != s[6]); // a new held value is glided to

    // The sample rate: 0 means the 48 kHz default, any positive rate is used as it is
    Lfo zero, def, slow;
    zero.setRateHz(250.0f);
    def.setRateHz(250.0f);
    slow.setRateHz(250.0f);
    zero.prepare(0.0);
    def.prepare(48000.0);
    slow.prepare(1000.0);
    bool sameAsDefault = true;
    for (int i = 0; i < 8; ++i)
        sameAsDefault = sameAsDefault && zero.getNextValue() == def.getNextValue();
    DAWLAB_CHECK(sameAsDefault);
    (void)slow.getNextValue();
    DAWLAB_CHECK_NEAR(slow.getNextValue(), 1.0, 1e-6); // 250 Hz at 1 kHz: a quarter cycle per sample

    // A different seed gives a different S&H sequence
    Lfo seedA, seedB;
    for (Lfo* l : {&seedA, &seedB}) {
        l->setShape(LfoShape::SampleAndHold);
        l->setSmoothingMs(0.0f);
        l->setRateHz(12000.0f);
    }
    seedA.setSeed(1u);
    seedB.setSeed(2u);
    seedA.prepare(48000.0);
    seedB.prepare(48000.0);
    DAWLAB_CHECK(seedA.getNextValue() != seedB.getNextValue());

    // The shared parameter smoother: tau = 20 ms = 960 samples at 48 kHz
    dsp::detail::ModParamSmoother smoother(0.0f);
    smoother.prepare(48000.0, 20.0);
    smoother.setTarget(1.0f);
    DAWLAB_CHECK_NEAR(smoother.next(), 1.0 - std::exp(-1.0 / 960.0), 1e-7);
}

// ---- Tremolo -----------------------------------------------------------------
void checkTremoloGainAtLfoEnds() {
    Tremolo t;
    t.setRateHz(0.0f);
    t.setDepth(0.6f);
    t.setAutoPan(true);
    t.setLfoPhase(0.75f); // sine at -1
    t.prepare(48000.0, 16, 2);
    AudioBuffer b(2, 4);
    for (float phase : {0.75f, 0.25f}) {
        t.setLfoPhase(phase);
        for (int ch = 0; ch < 2; ++ch)
            std::fill(b.getWritePointer(ch), b.getWritePointer(ch) + 4, 1.0f);
        t.process(b);
        const bool low = phase == 0.75f;
        std::printf("Tremolo  depth 0.6, autopan, lfo %s: gains %g %g\n", low ? "-1" : "+1",
                    static_cast<double>(b.getReadPointer(0)[0]), static_cast<double>(b.getReadPointer(1)[0]));
        DAWLAB_CHECK_NEAR(b.getReadPointer(0)[0], low ? 0.4 : 1.0, 1e-6);
        DAWLAB_CHECK_NEAR(b.getReadPointer(1)[0], low ? 1.0 : 0.4, 1e-6);
    }
}

// ---- Delay -------------------------------------------------------------------
void checkDelayEchoes() {
    // 1 ms = 48 samples, feedback 0.5, all wet, linear reads at whole samples
    Delay d;
    d.setDelayMs(1.0f);
    d.setFeedback(0.5f);
    d.setMix(1.0f);
    d.setInterpolation(Delay::Interpolation::Linear);
    d.prepare(48000.0, 256, 2);
    DAWLAB_CHECK(d.getMaxDelaySamples() == msToSamples(2000.0, 48000.0) + 2);
    AudioBuffer b = impulseBuffer(2, 160);
    d.process(b);
    DAWLAB_CHECK(onlyAt(b, 0, {{48, 1.0f}, {96, 0.5f}, {144, 0.25f}}));

    // Ping-pong: (L+R)/2 goes left, then each echo crosses over
    Delay p;
    p.setDelayMs(1.0f);
    p.setFeedback(0.5f);
    p.setMix(1.0f);
    p.setInterpolation(Delay::Interpolation::Linear);
    p.setPingPong(true);
    p.prepare(48000.0, 256, 2);
    AudioBuffer pp(2, 161);
    for (int ch = 0; ch < 2; ++ch)
        std::fill(pp.getWritePointer(ch), pp.getWritePointer(ch) + 161, 0.0f);
    pp.getWritePointer(0)[0] = 0.5f;
    pp.getWritePointer(1)[0] = 0.25f;
    pp.getWritePointer(0)[160] = 7.0f; // just past the processed block: must stay as it is
    pp.getWritePointer(1)[160] = 7.0f;
    p.process(AudioBlock(pp).getSubBlock(0, 160));
    DAWLAB_CHECK(pp.getReadPointer(0)[160] == 7.0f && pp.getReadPointer(1)[160] == 7.0f);
    pp.getWritePointer(0)[160] = 0.0f;
    pp.getWritePointer(1)[160] = 0.0f;
    DAWLAB_CHECK(onlyAt(pp, 0, {{48, 0.375f}, {144, 0.09375f}}));
    DAWLAB_CHECK(onlyAt(pp, 1, {{96, 0.1875f}}));

    // Linear and cubic reads differ between whole samples: 10.5 samples
    for (auto interp : {Delay::Interpolation::Linear, Delay::Interpolation::Cubic}) {
        Delay f;
        f.setDelayMs(0.21875f); // 10.5 samples
        f.setFeedback(0.0f);
        f.setMix(1.0f);
        f.setInterpolation(interp);
        f.prepare(48000.0, 64, 1);
        AudioBuffer m = impulseBuffer(1, 16);
        f.process(m);
        if (interp == Delay::Interpolation::Linear)
            DAWLAB_CHECK(onlyAt(m, 0, {{10, 0.5f}, {11, 0.5f}}));
        else
            DAWLAB_CHECK(onlyAt(m, 0, {{9, -0.0625f}, {10, 0.5625f}, {11, 0.5625f}, {12, -0.0625f}}));
    }

    // Filters at the "off" ends of their ranges leave the feedback untouched
    Delay off;
    off.setDelayMs(1.0f);
    off.setFeedback(0.5f);
    off.setMix(1.0f);
    off.setInterpolation(Delay::Interpolation::Linear);
    off.setLowpassHz(20000.0f);
    off.setHighpassHz(20.0f);
    off.prepare(48000.0, 256, 1);
    AudioBuffer o = impulseBuffer(1, 100);
    off.process(o);
    DAWLAB_CHECK(onlyAt(o, 0, {{48, 1.0f}, {96, 0.5f}}, 1e-6));

    // A tiny echo at exactly the flush threshold (1e-15) is still heard
    Delay tiny;
    tiny.setDelayMs(1.0f);
    tiny.setFeedback(0.0f);
    tiny.setMix(1.0f);
    tiny.setInterpolation(Delay::Interpolation::Linear);
    tiny.prepare(48000.0, 64, 1);
    AudioBuffer t = impulseBuffer(1, 64, 1.0e-15f);
    tiny.process(t);
    DAWLAB_CHECK(t.getReadPointer(0)[48] > 0.0f);
}

void checkDelayTimeRamp() {
    Delay d;
    d.setDelayMs(10.0f);
    d.setDelayRampSeconds(0.01); // 480 samples
    d.prepare(48000.0, 512, 1);
    d.setDelayMs(20.0f);
    AudioBuffer b(1, 240);
    std::fill(b.getWritePointer(0), b.getWritePointer(0) + 240, 0.0f);
    d.process(b);
    std::printf("Delay    10 -> 20 ms over 480 samples, after 240: %g ms\n",
                static_cast<double>(d.getCurrentDelayMs()));
    DAWLAB_CHECK_NEAR(d.getCurrentDelayMs(), 15.0, 1e-4);
    d.process(b);
    d.process(b); // 240 samples past the end of the ramp
    DAWLAB_CHECK(d.getCurrentDelayMs() == 20.0f);
    d.setDelayRampSeconds(0.0); // no ramp: the delay jumps
    d.setDelayMs(5.0f);
    DAWLAB_CHECK(d.getCurrentDelayMs() == 5.0f);

    // A block with no channels moves nothing
    Delay z;
    z.setDelayMs(10.0f);
    z.prepare(48000.0, 512, 2);
    z.setDelayMs(20.0f);
    z.process(AudioBlock(nullptr, 0, 64));
    DAWLAB_CHECK(z.getCurrentDelayMs() == 10.0f);
}

// ---- Chorus / Flanger / Phaser -----------------------------------------------
void checkChorusVoicesAndSpread() {
    // Four voices a quarter cycle apart, rate 0: sine at 0, +1, 0, -1 -> 50, 80, 50, 20 ms
    Chorus c;
    c.setVoices(4);
    c.setRateHz(0.0f);
    c.setCentreDelayMs(50.0f);
    c.setDepthMs(30.0f);
    c.setMix(1.0f);
    c.prepare(48000.0, 4096, 1);
    DAWLAB_CHECK(c.getVoices() == 4);
    AudioBuffer b = impulseBuffer(1, 3900);
    c.process(b);
    DAWLAB_CHECK(onlyAt(b, 0, {{960, 0.25f}, {2400, 0.5f}, {3840, 0.25f}}));

    // One voice, full spread: the right channel's LFO is a quarter cycle ahead (sine +1)
    Chorus s;
    s.setVoices(1);
    s.setRateHz(0.0f);
    s.setCentreDelayMs(20.0f);
    s.setDepthMs(10.0f);
    s.setMix(1.0f);
    s.setStereoSpread(1.0f);
    s.prepare(48000.0, 2048, 2);
    AudioBuffer st = impulseBuffer(2, 1500);
    s.process(st);
    DAWLAB_CHECK(onlyAt(st, 0, {{960, 1.0f}}));
    DAWLAB_CHECK(onlyAt(st, 1, {{1440, 1.0f}}));

    // Half spread: the right channel is an eighth of a cycle ahead, sin(pi/4) -> 20 + 10 x 0.707 ms = 1299.4 samples
    Chorus h;
    h.setVoices(1);
    h.setRateHz(0.0f);
    h.setCentreDelayMs(20.0f);
    h.setDepthMs(10.0f);
    h.setMix(1.0f);
    h.setStereoSpread(0.5f);
    h.prepare(48000.0, 2048, 2);
    AudioBuffer hb = impulseBuffer(2, 1500);
    h.process(hb);
    const float* r = hb.getReadPointer(1);
    const int peak = static_cast<int>(std::max_element(r, r + 1500) - r);
    DAWLAB_CHECK(r[960] == 0.0f && (peak == 1299 || peak == 1300));
}

void checkFlangerSweepTop() {
    // LFO parked at +1: delay = base + depth = 40 ms = 1920 samples, the longest the flanger can reach
    Flanger f;
    f.setRateHz(0.0f);
    f.setLfoPhase(0.25f);
    f.setDelayMs(20.0f);
    f.setDepthMs(20.0f);
    f.setFeedback(0.0f);
    f.setMix(1.0f);
    f.prepare(48000.0, 2048, 1);
    AudioBuffer b = impulseBuffer(1, 2000);
    f.process(b);
    DAWLAB_CHECK(f.getCurrentDelayMs() == 40.0f);
    DAWLAB_CHECK(onlyAt(b, 0, {{1920, 1.0f}}));

    Flanger g;
    g.setRateHz(0.0f);
    g.setLfoPhase(0.25f);
    g.setDelayMs(5.0f);
    g.setDepthMs(10.0f);
    g.prepare(48000.0, 64, 1);
    AudioBuffer one(1, 1);
    one.getWritePointer(0)[0] = 0.0f;
    g.process(one);
    DAWLAB_CHECK(g.getCurrentDelayMs() == 15.0f);

    // A sample of exactly the flush threshold (1e-20) is kept in the loop
    Flanger t;
    t.setRateHz(0.0f);
    t.setDelayMs(5.0f);
    t.setDepthMs(0.0f);
    t.setFeedback(0.0f);
    t.setMix(1.0f);
    t.prepare(48000.0, 512, 1);
    AudioBuffer tb = impulseBuffer(1, 260, 1.0e-20f);
    t.process(tb);
    DAWLAB_CHECK(tb.getReadPointer(0)[240] == 1.0e-20f);
}

void checkPhaserCoefficientsAndStages() {
    const double t = std::tan(kPi / 6.0);
    DAWLAB_CHECK_NEAR(Phaser::allpassCoefficient(8000.0, 48000.0), (t - 1.0) / (t + 1.0), 1e-12);

    // LFO parked at +1, depth 1, sweep 1 octave: centre 1000 Hz -> 2000 Hz
    Phaser p;
    p.setRateHz(0.0f);
    p.setLfoPhase(0.25f);
    p.setDepth(1.0f);
    p.setSweepOctaves(1.0f);
    p.setCentreHz(1000.0f);
    p.prepare(48000.0, 64, 1);
    AudioBuffer one(1, 1);
    one.getWritePointer(0)[0] = 0.0f;
    p.process(one);
    DAWLAB_CHECK_NEAR(p.getCurrentCentreHz(), 2000.0, 1e-3);

    // Dropping to 2 stages sounds the same as having 2 stages from the start
    AudioBuffer noise(1, 256);
    fillNoise(noise, 5u);
    Phaser a, b;
    for (Phaser* ph : {&a, &b}) {
        ph->setRateHz(0.0f);
        ph->setFeedback(0.0f);
        ph->setMix(1.0f);
    }
    a.setStages(4);
    a.prepare(48000.0, 256, 1);
    AudioBuffer warm = noise;
    a.process(warm);
    a.setStages(2);
    a.reset();
    b.setStages(2);
    b.prepare(48000.0, 256, 1);
    AudioBuffer outA = noise, outB = noise;
    a.process(outA);
    b.process(outB);
    DAWLAB_CHECK(std::equal(outA.getReadPointer(0), outA.getReadPointer(0) + 256, outB.getReadPointer(0)));

    // Twelve allpass stages keep the energy of noise (allpass: flat magnitude)
    Phaser twelve;
    twelve.setStages(12);
    twelve.setRateHz(0.0f);
    twelve.setFeedback(0.0f);
    twelve.setMix(1.0f);
    twelve.prepare(48000.0, 4800, 1);
    AudioBuffer n12(1, 4800);
    fillNoise(n12, 9u);
    double inE = 0.0, outE = 0.0;
    for (int i = 0; i < 4800; ++i)
        inE += static_cast<double>(n12.getReadPointer(0)[i]) * n12.getReadPointer(0)[i];
    twelve.process(n12);
    for (int i = 0; i < 4800; ++i)
        outE += static_cast<double>(n12.getReadPointer(0)[i]) * n12.getReadPointer(0)[i];
    std::printf("Phaser   12 stages: energy out / in = %g\n", outE / inE);
    DAWLAB_CHECK(std::abs(outE / inE - 1.0) < 0.05);

    // At fc = fs/4 each stage is (all but) a one-sample delay: a 1e-20 impulse comes out after 4 stages intact,
    // and with mix 1 nothing else is heard
    Phaser d;
    d.setRateHz(0.0f);
    d.setDepth(0.0f);
    d.setCentreHz(12000.0f);
    d.setFeedback(0.0f);
    d.setMix(1.0f);
    d.setStages(4);
    d.prepare(48000.0, 8, 1);
    AudioBuffer tb = impulseBuffer(1, 8, 1.0e-20f);
    d.process(tb);
    const float* o = tb.getReadPointer(0);
    DAWLAB_CHECK(o[0] == 0.0f && o[1] == 0.0f && o[2] == 0.0f && o[3] == 0.0f && o[4] == 1.0e-20f);
}

// ---- Reverb building blocks --------------------------------------------------
void checkReverbBuildingBlocks() {
    CombFilter comb;
    comb.prepare(10);
    comb.setDelaySamples(99);
    DAWLAB_CHECK(comb.getDelaySamples() == 10); // the capacity is exactly the prepared maximum
    comb.setDelaySamples(10);
    DAWLAB_CHECK(comb.getDelaySamples() == 10);

    // D = 3 in a ring of exactly 3: delta[n-3] + g * lp(...) with damping 0.5
    CombFilter c3;
    c3.prepare(3);
    c3.setDelaySamples(3);
    c3.setFeedback(0.5f);
    c3.setDamping(0.5f);
    float y[10];
    for (int i = 0; i < 10; ++i)
        y[i] = c3.processSample(i == 0 ? 1.0f : 0.0f);
    std::printf("Comb     D 3, g 0.5, damping 0.5: y[3] %g y[6] %g y[9] %g\n", static_cast<double>(y[3]),
                static_cast<double>(y[6]), static_cast<double>(y[9]));
    // at n = 3 the lowpass gives 1 x (1 - 0.5) = 0.5, so 0.5 x 0.5 = 0.25 is written back and heard at n = 6
    DAWLAB_CHECK(y[0] == 0.0f && y[1] == 0.0f && y[2] == 0.0f && y[3] == 1.0f && y[4] == 0.0f && y[6] == 0.25f);

    AllpassDiffuser ap;
    ap.prepare(8);
    ap.setDelaySamples(3);
    ap.setGain(0.5f);
    float a[7];
    for (int i = 0; i < 7; ++i)
        a[i] = ap.processSample(i == 0 ? 1.0f : 0.0f);
    // -g, then (1 - g^2) at D, then g(1 - g^2) at 2D
    DAWLAB_CHECK(a[0] == -0.5f && a[1] == 0.0f && a[3] == 0.75f && a[6] == 0.375f);

    AllpassDiffuser tiny;
    tiny.prepare(4);
    tiny.setDelaySamples(2);
    tiny.setGain(0.0f);
    (void)tiny.processSample(1.0e-20f);
    (void)tiny.processSample(0.0f);
    DAWLAB_CHECK(tiny.processSample(0.0f) == 1.0e-20f); // exactly at the flush threshold: kept

    DAWLAB_CHECK(CombFilter::feedbackForRt60(0, 0.0, 48000.0) == 0.0f);

    DAWLAB_CHECK(nextPrime(2) == 2);
    DAWLAB_CHECK(nextPrime(4) == 5);
    DAWLAB_CHECK(nextPrime(8) == 11);
    DAWLAB_CHECK(nextPrime(31400) == 31469);
}

// ---- Reverb ------------------------------------------------------------------
const double kRevBaseMs[Reverb::kNumLines] = {29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.3, 73.1};
const double kRevDiffuserMs[Reverb::kNumInputDiffusers] = {4.77, 3.60, 12.73, 9.31};

int expectedLineSamples(int k, float size, double fs) {
    return nextPrime(msToSamples(kRevBaseMs[k] * (0.5f + size), fs));
}

void checkReverbLinesAndGains() {
    Reverb before; // the lengths are known before prepare(), at the default 48 kHz
    before.setSize(0.5f);
    bool lengths = true;
    for (int k = 0; k < Reverb::kNumLines; ++k)
        lengths = lengths && before.getLineDelaySamples(k) == static_cast<float>(expectedLineSamples(k, 0.5f, 48000.0));
    DAWLAB_CHECK(lengths);

    Reverb rv;
    rv.setDecaySeconds(1.5f);
    rv.prepare(48000.0, 512, 2);
    for (float size : {0.5f, 1.0f}) {
        rv.setSize(size);
        bool ok = true;
        for (int k = 0; k < Reverb::kNumLines; ++k) {
            const int len = expectedLineSamples(k, size, 48000.0);
            const double g = std::pow(10.0, -3.0 * len / (1.5 * 48000.0));
            ok = ok && rv.getLineDelaySamples(k) == static_cast<float>(len) &&
                 std::abs(rv.getLineGain(k) - g) < 1e-6;
        }
        std::printf("Reverb   size %g: line lengths are the next primes, gains 10^(-3L/(RT60 fs)): %s\n",
                    static_cast<double>(size), ok ? "yes" : "no");
        DAWLAB_CHECK(ok);
    }

    // The longest line at size 1 has 64 samples of room above its length for the step up to a prime. At this rate
    // the next prime is 69 samples away (31400 -> 31469, a prime gap), so the line stops at the room's end.
    const double fs = 286366.0;
    const int n = msToSamples(kRevBaseMs[7] * 1.5f, fs);
    DAWLAB_CHECK(n == 31400 && nextPrime(n) - n > 64);
    Reverb wide;
    wide.setSize(1.0f);
    wide.prepare(fs, 512, 2);
    std::printf("Reverb   size 1 at %g Hz: longest line %g samples\n", fs,
                static_cast<double>(wide.getLineDelaySamples(7)));
    DAWLAB_CHECK(wide.getLineDelaySamples(7) == static_cast<float>(n + 64));
}

// The FDN written out from its formulas (no modulation): pre-delay, 4 allpass diffusers per side (x1.07 on the
// right), input gain 0.35, v_k = u_k delayed by L_k, u_k = damp(v_k - (2/N) sum v) * g_k + (even ? L : R),
// wetL = v0 - v2 + v4 - v6, wetR = v1 - v3 + v5 - v7. The reverb's processSampleStereo() must give the same.
void checkReverbMatchesFdnFormula(double fs, float dampingHz, float preDelayMs, int numSamples) {
    Reverb rv;
    rv.setDampingHz(dampingHz);
    rv.setPreDelayMs(preDelayMs);
    rv.prepare(fs, 512, 2);
    const int P = msToSamples(preDelayMs, fs);
    const float inL = 1.0f, inR = 0.5f;
    int L[Reverb::kNumLines];
    float g[Reverb::kNumLines];
    for (int k = 0; k < Reverb::kNumLines; ++k) {
        L[k] = static_cast<int>(rv.getLineDelaySamples(k));
        g[k] = rv.getLineGain(k);
    }
    const bool bypass = dampingHz >= 20000.0f;
    OnePole damping;
    damping.prepare(fs, 1, Reverb::kNumLines);
    damping.setType(OnePole::Type::Lowpass);
    damping.setCutoff(std::min(dampingHz, static_cast<float>(fs * 0.45)));
    std::vector<AllpassDiffuser> diffusers(2 * Reverb::kNumInputDiffusers);
    for (int ch = 0; ch < 2; ++ch)
        for (int k = 0; k < Reverb::kNumInputDiffusers; ++k) {
            const double ms = kRevDiffuserMs[k] * (ch == 0 ? 1.0 : 1.07);
            AllpassDiffuser& ap = diffusers[static_cast<size_t>(ch * Reverb::kNumInputDiffusers + k)];
            ap.prepare(msToSamples(ms, fs) + 1);
            ap.setDelaySamples(msToSamples(ms, fs));
            ap.setGain(0.5f);
        }
    std::vector<std::vector<float>> pushed(Reverb::kNumLines, std::vector<float>(static_cast<size_t>(numSamples)));
    int mismatches = 0;
    for (int i = 0; i < numSamples; ++i) {
        float d[2] = {i == P ? inL : 0.0f, i == P ? inR : 0.0f};
        for (int ch = 0; ch < 2; ++ch) {
            for (int k = 0; k < Reverb::kNumInputDiffusers; ++k)
                d[ch] = diffusers[static_cast<size_t>(ch * Reverb::kNumInputDiffusers + k)].processSample(d[ch]);
            d[ch] *= 0.35f;
        }
        float v[Reverb::kNumLines];
        float sum = 0.0f;
        for (int k = 0; k < Reverb::kNumLines; ++k) {
            v[k] = i >= L[k] ? pushed[static_cast<size_t>(k)][static_cast<size_t>(i - L[k])] : 0.0f;
            sum += v[k];
        }
        for (int k = 0; k < Reverb::kNumLines; ++k) {
            float u = v[k] - 0.25f * sum;
            if (!bypass)
                u = damping.processSample(k, u);
            u *= g[k];
            u += d[k % 2];
            pushed[static_cast<size_t>(k)][static_cast<size_t>(i)] = reverb_detail::flushDenormal(u);
        }
        const float wantL = v[0] - v[2] + v[4] - v[6];
        const float wantR = v[1] - v[3] + v[5] - v[7];
        float wetL = 0.0f, wetR = 0.0f;
        rv.processSampleStereo(i == 0 ? inL : 0.0f, i == 0 ? inR : 0.0f, wetL, wetR);
        if (!(std::abs(wetL - wantL) <= 1e-6f && std::abs(wetR - wantR) <= 1e-6f)) {
            if (mismatches == 0)
                std::printf("  sample %d: wet %g %g, formula %g %g\n", i, static_cast<double>(wetL),
                            static_cast<double>(wetR), static_cast<double>(wantL), static_cast<double>(wantR));
            ++mismatches;
        }
    }
    std::printf("Reverb   %g Hz, damping %g Hz, pre-delay %g ms: %d of %d samples differ from the FDN formula\n", fs,
                static_cast<double>(dampingHz), static_cast<double>(preDelayMs), mismatches, numSamples);
    DAWLAB_CHECK(mismatches == 0);
}

// process() = processSampleStereo() + width (mid/side) + dry/wet mix; mono uses the left input for both sides
void checkReverbProcessMixesTheWetSignal() {
    const int n = 3000;
    AudioBuffer in(2, n);
    fillNoise(in, 3u);
    Reverb blockFx, sampleFx, monoFx, twinFx;
    for (Reverb* r : {&blockFx, &sampleFx, &monoFx, &twinFx}) {
        r->setMix(0.5f);
        r->setWidth(0.5f);
        r->prepare(48000.0, n, 2);
    }
    AudioBuffer out = in;
    blockFx.process(out);
    int mismatches = 0;
    for (int i = 0; i < n; ++i) {
        const float dl = in.getReadPointer(0)[i], dr = in.getReadPointer(1)[i];
        float wl = 0.0f, wr = 0.0f;
        sampleFx.processSampleStereo(dl, dr, wl, wr);
        const float mid = 0.5f * (wl + wr);
        const float side = 0.5f * (wl - wr) * 0.5f;
        const float wantL = dl * 0.5f + (mid + side) * 0.5f;
        const float wantR = dr * 0.5f + (mid - side) * 0.5f;
        if (!(std::abs(out.getReadPointer(0)[i] - wantL) <= 1e-6f && std::abs(out.getReadPointer(1)[i] - wantR) <= 1e-6f))
            ++mismatches;
    }
    std::printf("Reverb   process() vs wet + width 0.5 + mix 0.5: %d of %d samples differ\n", mismatches, n);
    DAWLAB_CHECK(mismatches == 0);

    AudioBuffer mono(1, n), twin(2, n);
    for (int i = 0; i < n; ++i)
        mono.getWritePointer(0)[i] = twin.getWritePointer(0)[i] = twin.getWritePointer(1)[i] = in.getReadPointer(0)[i];
    monoFx.process(mono);
    twinFx.process(twin);
    DAWLAB_CHECK(std::equal(mono.getReadPointer(0), mono.getReadPointer(0) + n, twin.getReadPointer(0)));

    // A mix change glides with a 30 ms time constant
    Reverb glide;
    glide.prepare(48000.0, 512, 2); // mix 0.3
    glide.setMix(1.0f);
    AudioBuffer dc(2, 1);
    dc.getWritePointer(0)[0] = dc.getWritePointer(1)[0] = 1.0f;
    glide.process(dc);
    const float k = static_cast<float>(1.0 - std::exp(-1.0 / (0.03 * 48000.0)));
    DAWLAB_CHECK_NEAR(dc.getReadPointer(0)[0], 1.0f - smoothToward(0.3f, 1.0f, k), 1e-6);
}

// With modulation, line 0 is read at L0 - 1 + depth sin(phase), the phase advancing 2 pi rate / fs per sample:
// at 5 Hz and 1 ms the first echo arrives about 38 samples after L0
void checkReverbModulationMovesTheFirstEcho(bool rateAfterPrepare) {
    const double fs = 48000.0;
    Reverb rv;
    rv.setModulationDepthMs(1.0f);
    if (!rateAfterPrepare)
        rv.setModulationRateHz(5.0f);
    rv.prepare(fs, 512, 2);
    if (rateAfterPrepare)
        rv.setModulationRateHz(5.0f);
    const int l0 = static_cast<int>(rv.getLineDelaySamples(0));
    int expected = -1;
    for (int i = 0; i < 3 * l0 && expected < 0; ++i)
        if (i >= l0 + 48.0 * std::sin(kTwoPi * 5.0 * (i + 1) / fs))
            expected = i;
    int first = -1;
    for (int i = 0; i < 3 * l0 && first < 0; ++i) {
        float wl = 0.0f, wr = 0.0f;
        rv.processSampleStereo(i == 0 ? 1.0f : 0.0f, 0.0f, wl, wr);
        if (wl != 0.0f)
            first = i;
    }
    std::printf("Reverb   modulation 1 ms at 5 Hz (rate set %s prepare): first echo at %d, L0 %d, expected about %d\n",
                rateAfterPrepare ? "after" : "before", first, l0, expected);
    DAWLAB_CHECK(std::abs(first - expected) <= 2);
}

// ---- ConvolutionReverb -------------------------------------------------------
void checkConvolutionMatchesDirectConvolution() {
    ConvolutionReverb small;
    small.prepare(48000.0, 64, 1);
    DAWLAB_CHECK(small.getPartitionSize() == 64);
    ConvolutionReverb big;
    big.prepare(48000.0, 512, 1);
    DAWLAB_CHECK(big.getPartitionSize() == 512 && big.getFftSize() == 1024);

    // P = 128, an IR of exactly 3 partitions
    const int P = 128, irLen = 3 * P, n = 8 * P;
    AudioBuffer ir(1, irLen);
    NoiseGenerator noise(11u);
    for (int i = 0; i < irLen; ++i)
        ir.getWritePointer(0)[i] = noise.next() * static_cast<float>(std::exp(-i / 100.0));
    ConvolutionReverb conv;
    conv.prepare(48000.0, P, 2);
    conv.setImpulseResponse(ir);
    DAWLAB_CHECK(conv.getPartitionSize() == P && conv.getLatencySamples() == P);
    DAWLAB_CHECK(conv.getNumPartitions() == 3);
    // 3 IR spectra + 3 FDL spectra per channel, each N/2 + 1 = 129 bins
    DAWLAB_CHECK(conv.getSpectraMemoryBytes() == (3u + 2u * 3u) * 129u * sizeof(std::complex<float>));
    AudioBuffer in(2, n);
    fillNoise(in, 13u);
    AudioBuffer out = in;
    for (int start = 0; start < n; start += P)
        conv.process(AudioBlock(out).getSubBlock(start, P));
    double worst = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) {
            double want = 0.0;
            for (int k = 0; k < irLen && k <= i - P; ++k)
                want += static_cast<double>(ir.getReadPointer(0)[k]) * in.getReadPointer(ch)[i - P - k];
            worst = std::max(worst, std::abs(want - out.getReadPointer(ch)[i]));
        }
    std::printf("Conv     P 128, 3 partitions: worst difference from direct convolution %g\n", worst);
    DAWLAB_CHECK(worst < 1e-4);

    // Mix 0.5: half the dry signal (P samples late) plus half the convolution
    ConvolutionReverb half;
    half.setMix(0.5f);
    half.prepare(48000.0, P, 1);
    half.setImpulseResponse(ir);
    AudioBuffer hb(1, n);
    std::copy(in.getReadPointer(0), in.getReadPointer(0) + n, hb.getWritePointer(0));
    for (int start = 0; start < n; start += P)
        half.process(AudioBlock(hb).getSubBlock(start, P));
    double worstHalf = 0.0;
    for (int i = P; i < n; ++i) {
        double wet = 0.0;
        for (int k = 0; k < irLen && k <= i - P; ++k)
            wet += static_cast<double>(ir.getReadPointer(0)[k]) * in.getReadPointer(0)[i - P - k];
        const double want = 0.5 * in.getReadPointer(0)[i - P] + 0.5 * wet;
        worstHalf = std::max(worstHalf, std::abs(want - hb.getReadPointer(0)[i]));
    }
    DAWLAB_CHECK(worstHalf < 1e-4);
}

void checkConvolutionMixGlide() {
    // No IR: the output is the dry signal P samples late, times (1 - mix); a mix change glides (20 ms)
    const int P = 64;
    const float k = static_cast<float>(1.0 - std::exp(-1.0 / (0.02 * 48000.0)));
    ConvolutionReverb conv;
    conv.prepare(48000.0, P, 1);
    conv.setMix(0.0f);
    AudioBuffer ones(1, 2 * P);
    std::fill(ones.getWritePointer(0), ones.getWritePointer(0) + 2 * P, 1.0f);
    conv.process(AudioBlock(ones).getSubBlock(0, P)); // two blocks: the glide carries across
    conv.process(AudioBlock(ones).getSubBlock(P, P));
    float mix = 1.0f;
    bool ok = true;
    for (int i = 0; i < 2 * P; ++i) {
        mix = smoothToward(mix, 0.0f, k);
        const float want = i < P ? 0.0f : 1.0f - mix;
        ok = ok && std::abs(ones.getReadPointer(0)[i] - want) <= 1e-6f;
    }
    std::printf("Conv     mix 1 -> 0 glides across blocks: %s\n", ok ? "yes" : "no");
    DAWLAB_CHECK(ok);

    // Every channel glides from the same starting point
    ConvolutionReverb three;
    three.prepare(48000.0, P, 3);
    three.setMix(0.0f);
    AudioBuffer b3(3, 2 * P);
    for (int ch = 0; ch < 3; ++ch)
        std::fill(b3.getWritePointer(ch), b3.getWritePointer(ch) + 2 * P, 1.0f);
    three.process(AudioBlock(b3).getSubBlock(0, P));
    three.process(AudioBlock(b3).getSubBlock(P, P));
    bool same = true;
    for (int ch = 1; ch < 3; ++ch)
        same = same && std::equal(b3.getReadPointer(0), b3.getReadPointer(0) + 2 * P, b3.getReadPointer(ch));
    DAWLAB_CHECK(same);
}

double rmsOf(const float* x, int from, int to) {
    double s = 0.0;
    for (int i = from; i < to; ++i)
        s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / (to - from));
}

void checkSyntheticImpulseResponse() {
    const double fs = 48000.0;
    AudioBuffer ir = ConvolutionReverb::makeSyntheticImpulseResponse(fs, 1.0, 2, 1u);
    const int len = ir.getNumSamples();
    DAWLAB_CHECK(len == 52800 && ir.getNumChannels() == 2); // 1.1 x RT60
    DAWLAB_CHECK(ConvolutionReverb::makeSyntheticImpulseResponse(fs, 0.5, 1).getNumSamples() == 26400);
    AudioBuffer other = ConvolutionReverb::makeSyntheticImpulseResponse(fs, 1.0, 1, 1u + 7919u);
    DAWLAB_CHECK(std::equal(ir.getReadPointer(1), ir.getReadPointer(1) + len, other.getReadPointer(0)));
    for (int ch = 0; ch < 2; ++ch) {
        const float* h = ir.getReadPointer(ch);
        double energy = 0.0, lag1 = 0.0;
        for (int i = 0; i < len; ++i) {
            energy += static_cast<double>(h[i]) * h[i];
            if (i > 0)
                lag1 += static_cast<double>(h[i]) * h[i - 1];
        }
        // -60 dB per RT60: two windows 0.5 s apart differ by about 30 dB
        const double dropDb = 20.0 * std::log10(rmsOf(h, 24000, 28800) / rmsOf(h, 0, 4800));
        // the fade over the last 10 %: the last 1 % is far below the 1 % just before the fade starts
        const double fadeRatio = rmsOf(h, len - 480, len) / rmsOf(h, 48000 - 480, 48000);
        std::printf("Synth IR ch %d: energy %g, lag-1 correlation %g, drop over 0.5 s %g dB, fade ratio %g\n", ch,
                    energy, lag1 / energy, dropDb, fadeRatio);
        DAWLAB_CHECK(std::abs(energy - 1.0) < 1e-4);
        DAWLAB_CHECK(lag1 / energy > 0.25 && lag1 / energy < 0.45); // the 8 kHz one-pole lowpass
        DAWLAB_CHECK(dropDb < -27.0 && dropDb > -33.0);
        DAWLAB_CHECK(fadeRatio < 0.15);
    }
}

} // namespace b6time

void checkAnalysisClasses() {
    checkFftMatchesNaiveDft();
    checkFftRealTransforms();
    checkCoherentGainEdges();
    checkSpectrumReadsBinCentres();
    checkSpectrumHannReadsAmplitude();
    checkSpectrumSmoothing();
    checkSpectrumAveragesChannels();
    checkLoudnessConstants();
    checkLoudnessSteadyTone();
    checkLoudnessShortTermWindow();
    checkLoudnessGatesAtBinCentres();
}

// ---- batch 7: FractionalDelayLine ----
#include <dawlab/dsp/FractionalDelayLine.h>

namespace b7fdl {

// A line of 14 samples taken from p at x = 0 .. 13: the sample "D pushes ago" is p(13 - D), so a read at a
// fractional delay D should give p(13 - D) whenever the interpolation can reproduce p exactly
template <typename P>
std::unique_ptr<FractionalDelayLine> lineOf(P p) {
    auto line = std::make_unique<FractionalDelayLine>();
    line->prepare(8, 1);
    for (int k = 0; k < 14; ++k)
        line->push(0, static_cast<float>(p(static_cast<double>(k))));
    return line;
}

// Linear reads reproduce a straight line, Catmull-Rom (readCubic) a parabola, Lagrange a cubic, at any fraction
void checkInterpolationReproducesPolynomials() {
    const auto straight = [](double x) { return 0.5 + 0.25 * x; };
    const auto parabola = [](double x) { return 2.0 - 0.4 * x + 0.05 * x * x; };
    const auto cubic = [](double x) { return 1.0 + 0.3 * x - 0.02 * x * x + 0.001 * x * x * x; };
    const auto lin = lineOf(straight);
    const auto par = lineOf(parabola);
    const auto cub = lineOf(cubic);
    double worst[4] = {0.0, 0.0, 0.0, 0.0};
    for (const float delay : {1.25f, 2.5f, 3.75f, 6.6f}) {
        const double x = 13.0 - static_cast<double>(delay);
        worst[0] = std::max(worst[0], std::abs(lin->readLinear(0, delay) - straight(x)));
        worst[1] = std::max(worst[1], std::abs(par->readCubic(0, delay) - parabola(x)));
        worst[2] = std::max(worst[2], std::abs(cub->readLagrange(0, delay) - cubic(x)));
        const int d = static_cast<int>(delay);
        const float t = delay - static_cast<float>(d);
        worst[3] = std::max(worst[3], std::abs(par->readCubicSplit(0, d, t) - parabola(x)));
    }
    std::printf("FractionalDelayLine polynomial reads: linear %.2g, cubic %.2g, Lagrange %.2g, cubic split %.2g\n",
                worst[0], worst[1], worst[2], worst[3]);
    for (const double w : worst)
        DAWLAB_CHECK(w < 1e-5);
}

// The split reads clamp (d, frac): a fraction is kept at the smallest delay and dropped at the largest, and a
// fraction of exactly 1 carries into the next sample (not a + 1 * (b - a), which can round away from b)
void checkSplitReadEdges() {
    auto line = std::make_unique<FractionalDelayLine>();
    line->prepare(4, 1); // 8 slots
    for (int k = 0; k < 10; ++k)
        line->push(0, static_cast<float>(k + 1)); // d pushes ago: 10 - d
    DAWLAB_CHECK(line->readLinearSplit(0, 0, 0.5f) == 9.5f);
    DAWLAB_CHECK(line->readCubicSplit(0, 1, 0.5f) == 8.5f);
    DAWLAB_CHECK(line->readLinearSplit(0, 4, 0.5f) == 6.0f);
    DAWLAB_CHECK(line->readCubicSplit(0, 4, 0.5f) == 6.0f);

    line->reset();
    line->push(0, 1e-8f);
    line->push(0, 1.0f);
    DAWLAB_CHECK(line->readLinearSplit(0, 0, 1.0f) == 1e-8f);
}

// The buffer is the next power of two that holds the largest delay plus 4 samples of headroom
void checkBufferSize() {
    DAWLAB_CHECK(FractionalDelayLine::nextPowerOfTwo(1) == 1);
    DAWLAB_CHECK(FractionalDelayLine::nextPowerOfTwo(3) == 4);
    DAWLAB_CHECK(FractionalDelayLine::nextPowerOfTwo(8) == 8);
    DAWLAB_CHECK(FractionalDelayLine::nextPowerOfTwo(9) == 16);
    FractionalDelayLine line;
    line.prepare(6, 1);
    DAWLAB_CHECK(line.getBufferSize() == 16);
    line.prepare(4, 1);
    DAWLAB_CHECK(line.getBufferSize() == 8);
}

} // namespace b7fdl

// ---- batch 9: AmpChain output ramp ----
namespace b9amp {

// A prepared mono AmpChain at its defaults that has run one block of silence
std::unique_ptr<AmpChain> warmAmp() {
    auto amp = std::make_unique<AmpChain>();
    amp->prepare(48000.0, 512, 1);
    AudioBuffer silence(1, 64);
    amp->process(silence.toBlock());
    return amp;
}

// The same 64-sample tone block through `amp`
std::vector<float> toneThrough(AmpChain& amp) {
    AudioBuffer b(1, 64);
    for (int i = 0; i < 64; ++i)
        b.getWritePointer(0)[i] = 0.3f * std::sin(0.13f * static_cast<float>(i));
    amp.process(b.toBlock());
    return std::vector<float>(b.getReadPointer(0), b.getReadPointer(0) + 64);
}

// A new output gain moves linearly across the next block (from the old gain at sample 0 towards the new one), and a
// block with no samples or no channels leaves that ramp where it was
void checkAmpChainOutputRamp() {
    const float g1 = dbToGain(-12.0f);
    auto steady = warmAmp();
    steady->setOutputGainDb(-12.0f);
    toneThrough(*steady); // the gain has arrived; its filters have run the same as the others'
    auto moving = warmAmp();
    toneThrough(*moving);
    moving->setOutputGainDb(-12.0f);
    const std::vector<float> s = toneThrough(*steady), m = toneThrough(*moving);
    double worst = 0.0;
    for (int i = 0; i < 64; ++i) {
        const double ramp = 1.0 + (static_cast<double>(g1) - 1.0) * i / 64.0; // the gain at sample i
        worst = std::max(worst, std::abs(static_cast<double>(m[static_cast<size_t>(i)]) * g1 - s[static_cast<size_t>(i)] * ramp));
    }
    std::printf("AmpChain output gain ramp: worst error %.3g\n", worst);
    DAWLAB_CHECK(worst < 1e-6);

    for (const int channels : {1, 0}) {
        auto paused = warmAmp();
        toneThrough(*paused);
        paused->setOutputGainDb(-12.0f);
        AudioBuffer empty(channels, channels == 1 ? 0 : 64);
        paused->process(empty.toBlock());
        DAWLAB_CHECK(toneThrough(*paused) == m);
    }
}

} // namespace b9amp

// ---- batch 9: dsp-b ----
namespace b9b {

// K-weighting stage 2 (the high-pass) at 48 kHz is the standard's Table 2; silence is exactly the floor
void checkLoudnessStage2AndSilence() {
    const BiquadCoefficients c = LoudnessMeter::makeKWeightingStage2(48000.0);
    const double* t = LoudnessMeter::kStage2Table48k;
    DAWLAB_CHECK(c.b0 == t[0] && c.b1 == t[1] && c.b2 == t[2]);
    DAWLAB_CHECK(std::abs(c.a1 - t[3]) < 1e-9 && std::abs(c.a2 - t[4]) < 1e-9);
    DAWLAB_CHECK(LoudnessMeter::powerToLufs(1e-15) == kMinusInfinityDb);
    DAWLAB_CHECK(LoudnessMeter::powerToLufs(0.0) == kMinusInfinityDb);
}

// `steps` 100 ms steps of a 1 kHz sine of `amplitude` (48 kHz) on every channel of `m`
void loudnessSteps(LoudnessMeter& m, int channels, int steps, float amplitude) {
    AudioBuffer step(channels, 4800);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < 4800; ++i)
            step.setSample(ch, i, amplitude * static_cast<float>(std::sin(kTwoPi * (i % 48) / 48.0)));
    for (int s = 0; s < steps; ++s)
        m.process(step.toBlock());
}

// A channel beyond kMaxChannels weighs nothing, also while a step is being summed; eight channels of one tone read
// exactly 10 log10(8) above one channel of it, whichever ring slot the newest step went into
void checkLoudnessChannelLimits() {
    const auto m = std::make_unique<LoudnessMeter>();
    m->prepare(kSampleRate, 1);
    AudioBuffer part(1, 100);
    for (int i = 0; i < 100; ++i)
        part.setSample(0, i, 0.5f * std::sin(0.3f * static_cast<float>(i)));
    m->process(part.toBlock());
    DAWLAB_CHECK(m->getChannelWeight(LoudnessMeter::kMaxChannels) == 0.0f);

    const auto mono = std::make_unique<LoudnessMeter>();
    const auto eight = std::make_unique<LoudnessMeter>();
    mono->prepare(kSampleRate, 1);
    eight->prepare(kSampleRate, LoudnessMeter::kMaxChannels);
    for (int s = 0; s < 33; ++s) {
        loudnessSteps(*mono, 1, 1, 0.5f);
        loudnessSteps(*eight, LoudnessMeter::kMaxChannels, 1, 0.5f);
        DAWLAB_CHECK_NEAR(eight->getMomentaryLufs() - mono->getMomentaryLufs(), 10.0 * std::log10(8.0), 1e-3);
        DAWLAB_CHECK_NEAR(eight->getShortTermLufs() - mono->getShortTermLufs(), 10.0 * std::log10(8.0), 1e-3);
    }
}

// The maxima start with the first full window: max momentary at exactly 400 ms, also when the steps before were
// silent; max short-term at 3 s, also when all 3 s were silent
void checkLoudnessMaximaStartWithFullWindows() {
    const auto first = std::make_unique<LoudnessMeter>();
    first->prepare(kSampleRate, 1);
    loudnessSteps(*first, 1, 4, 0.5f);
    DAWLAB_CHECK(first->getMomentaryLufs() > -10.0f && first->getMaxMomentaryLufs() == first->getMomentaryLufs());

    const auto late = std::make_unique<LoudnessMeter>();
    late->prepare(kSampleRate, 1);
    loudnessSteps(*late, 1, 3, 0.0f);
    loudnessSteps(*late, 1, 4, 0.5f);
    DAWLAB_CHECK(late->getMomentaryLufs() > -10.0f && late->getMaxMomentaryLufs() == late->getMomentaryLufs());

    const auto shortTerm = std::make_unique<LoudnessMeter>();
    shortTerm->prepare(kSampleRate, 1);
    loudnessSteps(*shortTerm, 1, 30, 0.0f);
    DAWLAB_CHECK(shortTerm->getMaxShortTermLufs() == kMinusInfinityDb);
    loudnessSteps(*shortTerm, 1, 1, 0.5f);
    DAWLAB_CHECK(shortTerm->getShortTermLufs() > -30.0f &&
                 shortTerm->getMaxShortTermLufs() == shortTerm->getShortTermLufs());
}

// The absolute gate is strict: a 400 ms block that reads exactly -70 LUFS is not a gating block. The channel weight
// that lands the reading on -70 is found by bisection (the reading grows with the weight), so the test does not
// depend on how the platform rounds the filters
float momentaryWithWeight(float weight, float& integrated) {
    const auto m = std::make_unique<LoudnessMeter>();
    m->prepare(kSampleRate, 1);
    m->setChannelWeight(0, weight);
    loudnessSteps(*m, 1, 4, 0.5f);
    integrated = m->getIntegratedLufs();
    return m->getMomentaryLufs();
}

void checkLoudnessAbsoluteGateIsStrict() {
    float lo = 1.0e-7f, hi = 1.0e-5f, integrated = 0.0f; // about -79 and -59 LUFS
    DAWLAB_CHECK(momentaryWithWeight(lo, integrated) < -70.0f && momentaryWithWeight(hi, integrated) >= -70.0f);
    while (std::nextafter(lo, hi) != hi) {
        const float mid = lo + 0.5f * (hi - lo);
        if (mid == lo || mid == hi)
            break;
        (momentaryWithWeight(mid, integrated) >= -70.0f ? hi : lo) = mid;
    }
    const float atGate = momentaryWithWeight(hi, integrated);
    std::printf("LoudnessMeter at the absolute gate: weight %.9g reads %.9g LUFS, integrated %.9g\n",
                static_cast<double>(hi), static_cast<double>(atGate), static_cast<double>(integrated));
    DAWLAB_CHECK(atGate == -70.0f && integrated == kMinusInfinityDb);
}

// The safety clamp counts only an output beyond ceiling * (1 + 1e-6): one exactly on that bound is clamped but not
// counted, the next float up is counted. The loud sample is in the lookahead when the ceiling drops and everything
// after it is silent, so its gain stays exactly 1 and it reaches the clamp unchanged
void checkLimiterClampBoundIsInclusive() {
    const float c = dbToGain(-6.0f);
    const float bound = c * (1.0f + 1.0e-6f);
    for (const float x : {bound, std::nextafter(bound, 1.0f)}) {
        Limiter l;
        l.prepare(kSampleRate, 64, 1);
        l.setLookaheadMs(1.0f); // 48 samples
        l.setCeilingDb(0.0f);
        AudioBuffer one(1, 1);
        one.setSample(0, 0, x);
        l.process(one);
        l.setCeilingDb(-6.0f);
        AudioBuffer rest(1, 64);
        l.process(rest);
        DAWLAB_CHECK(rest.getReadPointer(0)[47] == c);
        DAWLAB_CHECK(l.getSafetyClipCount() == (x == bound ? 0u : 1u));
    }
}

// Once a freeze has faded the modulation out, the modulation depth no longer matters: a copy of the frozen reverb
// with depth 0 gives exactly the same output, and the frozen tail still rings (it has not collapsed to a constant)
void checkReverbFrozenModulationIsOff() {
    auto modulated = std::make_unique<Reverb>();
    modulated->setModulationDepthMs(1.0f);
    modulated->setModulationRateHz(5.0f);
    modulated->prepare(kSampleRate, 512, 2);
    float wl = 0.0f, wr = 0.0f;
    for (int i = 0; i < 4800; ++i)
        modulated->processSampleStereo(i == 0 ? 1.0f : 0.0f, 0.0f, wl, wr);
    modulated->setFreeze(true);
    for (int i = 0; i < 24000; ++i) // 500 ms: the modulation fades out in about 280 ms
        modulated->processSampleStereo(0.0f, 0.0f, wl, wr);
    auto still = std::make_unique<Reverb>(*modulated);
    still->setModulationDepthMs(0.0f);
    int differing = 0, moving = 0;
    float previous = wl;
    for (int i = 0; i < 4800; ++i) {
        float ml = 0.0f, mr = 0.0f, sl = 0.0f, sr = 0.0f;
        modulated->processSampleStereo(0.0f, 0.0f, ml, mr);
        still->processSampleStereo(0.0f, 0.0f, sl, sr);
        differing += (ml != sl || mr != sr) ? 1 : 0;
        moving += ml != previous ? 1 : 0;
        previous = ml;
    }
    std::printf("Reverb   frozen 100 ms after the fade: %d samples differ from depth 0, %d of 4800 move\n", differing,
                moving);
    DAWLAB_CHECK(differing == 0 && moving > 4000);
}

// A new size glides every line toward its new length; setting the same size again before every block leaves that
// glide alone (the same-value rule, as checkSameValueRule())
void checkReverbSizeSameValue() {
    sameValue<Reverb>("Reverb::setSize", [](Reverb& r, float v) { r.setSize(v); }, 0.9f);
}

} // namespace b9b

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
    checkParameterRuleCh01to05();
    checkParameterRuleCh06to08();
    checkParameterRuleCh09to12();
    checkStatelessDefaults();
    checkRangeRule();
    checkMeterCostIsFlat();
    checkSameValueRule();
    checkSmootherRule();
    checkSweepRule();
    checkSmallerFunctions();

    // ---- batch 6: dsp-filters ----
    checkDspFilters();

    // ---- batch 6: dsp-distortion ----
    b6_distortion::checkHalfbandDesign();
    b6_distortion::checkHalfbandStage();
    b6_distortion::checkOversamplerLatency();
    b6_distortion::checkSaturationCurves();
    b6_distortion::checkSaturationProcessCurves();
    b6_distortion::checkSaturationDryDelay();
    b6_distortion::checkSaturationRamp();
    b6_distortion::checkSaturationBlocks();
    b6_distortion::checkDistortionCurves();
    b6_distortion::checkDistortionDelayLines();
    b6_distortion::checkDistortionProcessIsProcessSample();
    b6_distortion::checkDistortionRamps();
    b6_distortion::checkDistortionSampleAndHold();
    b6_distortion::checkDistortionTypeFade();
    b6_distortion::checkDistortionBlocks();

    // ---- batch 6: dsp-dynamics ----
    checkAttackReleaseFilterSteps();
    checkRmsFollowerOfOneSample();
    checkCompressorCurve();
    checkCompressorAppliesGain();
    checkCompressorMakeupGlide();
    checkExpanderCurve();
    checkExpanderMeterReadsRange();
    checkGateThresholdsAndMeter();
    checkGateThresholdEdges();
    checkGateStateDurations();
    checkGateClosedRangeGlide();
    checkLevelMeterResetClearsAllChannels();
    checkLevelMeterBallistics();
    checkDeEsserBasics();
    checkTransientShaperFormula();
    checkSlidingWindowMin();
    checkLimiterMatchesItsSteps();
    checkLimiterInputGainRamp();
    checkLimiterLookaheadBound();
    checkLimiterSafetyClamp();
#ifdef NDEBUG
    checkLimiterClearsUnpreparedChannels();
#endif

    // ---- batch 6: dsp-time ----
    b6time::checkDelayLineReadsEachChannelBack();
    b6time::checkLfoShapesAndSteps();
    b6time::checkTremoloGainAtLfoEnds();
    b6time::checkDelayEchoes();
    b6time::checkDelayTimeRamp();
    b6time::checkChorusVoicesAndSpread();
    b6time::checkFlangerSweepTop();
    b6time::checkPhaserCoefficientsAndStages();
    b6time::checkReverbBuildingBlocks();
    b6time::checkReverbLinesAndGains();
    b6time::checkReverbMatchesFdnFormula(48000.0, 20000.0f, 250.0f, 12000 + 4000);
    b6time::checkReverbMatchesFdnFormula(22050.0, 15000.0f, 0.0f, 3000);
    b6time::checkReverbMatchesFdnFormula(48000.0, 6000.0f, 0.0f, 5000);
    b6time::checkReverbProcessMixesTheWetSignal();
    b6time::checkReverbModulationMovesTheFirstEcho(false);
    b6time::checkReverbModulationMovesTheFirstEcho(true);
    b6time::checkConvolutionMatchesDirectConvolution();
    b6time::checkConvolutionMixGlide();
    b6time::checkSyntheticImpulseResponse();

    // ---- batch 6: dsp-analysis ----
    checkAnalysisClasses();

    // ---- batch 7: FractionalDelayLine ----
    b7fdl::checkInterpolationReproducesPolynomials();
    b7fdl::checkSplitReadEdges();
    b7fdl::checkBufferSize();

    // ---- batch 9: AmpChain output ramp ----
    b9amp::checkAmpChainOutputRamp();

    // ---- batch 9: dsp-b ----
    b9b::checkLoudnessStage2AndSilence();
    b9b::checkLoudnessChannelLimits();
    b9b::checkLoudnessMaximaStartWithFullWindows();
    b9b::checkLoudnessAbsoluteGateIsStrict();
    b9b::checkLimiterClampBoundIsInclusive();
    b9b::checkReverbFrozenModulationIsOff();
    b9b::checkReverbSizeSameValue();

    return dawlab::test::summary("dsp");
}
