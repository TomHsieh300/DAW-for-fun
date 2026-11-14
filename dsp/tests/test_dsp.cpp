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
    checkParameterRuleCh01to05();
    checkParameterRuleCh06to08();
    checkParameterRuleCh09to12();
    checkStatelessDefaults();
    checkRangeRule();
    checkMeterCostIsFlat();
    checkSameValueRule();
    checkSmootherRule();

    return dawlab::test::summary("dsp");
}
