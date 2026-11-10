#pragma once
// Effects: one InsertEffect adapter for each of the 19 effect DSP classes of chapters 01-12,
// plus a factory (Step 5).
//
//   chapter  DSP class            adapter                  meter    latency source
//   01       Gain                 GainEffect               -        0
//   02       StateVariableFilter  FilterEffect             -        0
//   03       ParametricEq         ParametricEqEffect       -        0
//   04       Compressor           CompressorEffect         GR dB    0
//   05       Limiter              LimiterEffect            GR dB    lookahead
//   06       Saturation           SaturationEffect         -        oversampling filters
//   07       Distortion           DistortionEffect         -        oversampling filters
//   07       AmpChain             AmpChainEffect           -        oversampling filters (2 stages)
//   08       Gate                 GateEffect               GR dB    0
//   08       Expander             ExpanderEffect           GR dB    0
//   08       DeEsser              DeEsserEffect            GR dB    0
//   08       TransientShaper      TransientShaperEffect    -        0
//   09       Delay                DelayEffect              -        0
//   10       Chorus               ChorusEffect             -        0
//   10       Flanger              FlangerEffect            -        0
//   10       Phaser               PhaserEffect             -        0
//   10       Tremolo              TremoloEffect            -        0
//   12       Reverb               ReverbEffect             -        0
//   12       ConvolutionReverb    ConvolutionReverbEffect  -        FFT partition
//
// Every adapter follows the same recipe (see EffectBase.h):
//   enum Param { ..., kCount };                       parameter indices, in UI order
//   static constexpr ParameterInfo kInfos[kCount];    name, unit, min, max, default, kind
//   applyParameter(index, value)                      one switch -> DSP setter
//
// Only the methods of the API contract in docs/authoring-guide.md section 5 are used, so the
// adapters keep working whatever else the chapter authors add to their classes. Latency is
// always *asked* from the DSP object (getLatencySamples()), never computed here.
//
// "Prepare-time" parameters: Saturation oversampling must be chosen before the DSP object's
// prepare(). The adapter reads that value at the start of prepare(); changing it afterwards
// takes effect at the next prepare() (a UI should mark it as "applies on reload").
//
// ConvolutionReverbEffect::setImpulseResponse() allocates and must be called on a non-audio
// thread *before* the effect is handed to the engine (or build a new effect and use
// Engine::replaceInsert).

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/AmpChain.h>
#include <dawlab/dsp/Chorus.h>
#include <dawlab/dsp/Compressor.h>
#include <dawlab/dsp/ConvolutionReverb.h>
#include <dawlab/dsp/DeEsser.h>
#include <dawlab/dsp/Delay.h>
#include <dawlab/dsp/Distortion.h>
#include <dawlab/dsp/Expander.h>
#include <dawlab/dsp/Flanger.h>
#include <dawlab/dsp/Gain.h>
#include <dawlab/dsp/Gate.h>
#include <dawlab/dsp/Lfo.h>
#include <dawlab/dsp/Limiter.h>
#include <dawlab/dsp/ParametricEq.h>
#include <dawlab/dsp/Phaser.h>
#include <dawlab/dsp/Reverb.h>
#include <dawlab/dsp/Saturation.h>
#include <dawlab/dsp/StateVariableFilter.h>
#include <dawlab/dsp/TransientShaper.h>
#include <dawlab/dsp/Tremolo.h>

#include <cstddef>
#include <cstring>
#include <memory>

#include "EffectBase.h"

namespace dawlab::engine {

// ---- shared choice tables ----------------------------------------------------------------
namespace choices {
inline constexpr const char* kOnOff[] = {"Off", "On"};
inline constexpr const char* kOversampling[] = {"1x", "2x", "4x", "8x"};
inline constexpr const char* kSaturationCurve[] = {"Tanh", "Cubic", "Arctan", "Asymmetric"};
inline constexpr const char* kDistortionType[] = {"HardClip", "Foldback", "Fuzz", "Rectify", "Bitcrush"};
inline constexpr const char* kLfoShape[] = {"Sine", "Triangle", "Saw", "Square", "S&H"};
inline constexpr const char* kEqBandType[] = {"LowCut", "LowShelf", "Peak", "HighShelf", "HighCut", "Notch"};
// Same order as dsp::StateVariableFilter::Type.
inline constexpr const char* kFilterType[] = {"Lowpass", "Highpass", "Bandpass", "Notch"};
} // namespace choices

// Small helpers so the parameter tables stay readable.
constexpr ParameterInfo paramFloat(const char* name, const char* unit, float min, float max, float def,
                                   bool log = false) noexcept {
    return {name, unit, min, max, def, ParameterKind::Float, log, nullptr, 0};
}
constexpr ParameterInfo paramInt(const char* name, const char* unit, int min, int max, int def) noexcept {
    return {name,
            unit,
            static_cast<float>(min),
            static_cast<float>(max),
            static_cast<float>(def),
            ParameterKind::Int,
            false,
            nullptr,
            0};
}
constexpr ParameterInfo paramBool(const char* name, bool def) noexcept {
    return {name, "", 0.0f, 1.0f, def ? 1.0f : 0.0f, ParameterKind::Bool, false, choices::kOnOff, 2};
}
template <std::size_t N>
constexpr ParameterInfo paramChoice(const char* name, const char* const (&labels)[N], int def) noexcept {
    return {name, "", 0.0f, static_cast<float>(N - 1), static_cast<float>(def), ParameterKind::Choice, false, labels,
            static_cast<int>(N)};
}

// =============================================================================================
// 01  Gain
// =============================================================================================
class GainEffect final : public EffectBase {
public:
    enum Param { Gain, kCount };
    GainEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "Gain"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
        dsp_.reset(); // start at the configured gain instead of ramping from 0 dB
    }
    void reset() noexcept override { dsp_.reset(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        if (index == Gain)
            dsp_.setGainDb(v);
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        paramFloat("Gain", "dB", -60.0f, 24.0f, 0.0f),
    };
    dsp::Gain dsp_;
};

// =============================================================================================
// 02  StateVariableFilter  (TPT SVF: one state, four responses; cutoff may move every block)
// =============================================================================================
class FilterEffect final : public EffectBase {
public:
    enum Param { Type, Cutoff, Resonance, kCount };
    FilterEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "Filter"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
        dsp_.reset();
    }
    void reset() noexcept override { dsp_.reset(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        switch (index) {
        case Type: dsp_.setType(static_cast<dsp::StateVariableFilter::Type>(static_cast<int>(v))); break;
        case Cutoff: dsp_.setCutoff(v); break;
        case Resonance: dsp_.setQ(v); break; // resonance = Q of the 2-pole filter (0.707 = no peak)
        default: break;
        }
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        paramChoice("Type", choices::kFilterType, 0),
        paramFloat("Cutoff", "Hz", 20.0f, 20000.0f, 1000.0f, true),
        paramFloat("Resonance", "Q", 0.5f, 20.0f, 0.707f, true),
    };
    dsp::StateVariableFilter dsp_;
};

// =============================================================================================
// 03  ParametricEq  (6 bands x {Enabled, Type, Frequency, Q, Gain} = 30 parameters)
// =============================================================================================
class ParametricEqEffect final : public EffectBase {
public:
    static constexpr int kNumBands = dsp::ParametricEq::kNumBands;
    static constexpr int kParamsPerBand = 5;
    enum BandParam { Enabled, Type, Frequency, Q, Gain };
    static constexpr int kCount = kNumBands * kParamsPerBand;

    static constexpr int paramIndex(int band, BandParam p) noexcept { return band * kParamsPerBand + p; }

    ParametricEqEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "ParametricEQ"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
        dsp_.reset();
    }
    void reset() noexcept override { dsp_.reset(); }

    // Setup-code convenience: sets all five parameters of one band (band = 0..5).
    void setBand(int band, bool enabled, dsp::EqBandType type, float hz, float q, float gainDb) noexcept {
        setParameter(paramIndex(band, Enabled), enabled ? 1.0f : 0.0f);
        setParameter(paramIndex(band, Type), static_cast<float>(static_cast<int>(type)));
        setParameter(paramIndex(band, Frequency), hz);
        setParameter(paramIndex(band, Q), q);
        setParameter(paramIndex(band, Gain), gainDb);
    }

protected:
    // The contract only has setBand(index, EqBand) / getBand(index): read-modify-write.
    void applyParameter(int index, float v) noexcept override {
        const int band = index / kParamsPerBand;
        dsp::EqBand b = dsp_.getBand(band);
        switch (static_cast<BandParam>(index % kParamsPerBand)) {
        case Enabled: b.enabled = v > 0.5f; break;
        case Type: b.type = static_cast<dsp::EqBandType>(static_cast<int>(v)); break;
        case Frequency: b.frequency = v; break;
        case Q: b.q = v; break;
        case Gain: b.gainDb = v; break;
        }
        dsp_.setBand(band, b);
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
#define DAWLAB_EQ_BAND(n, enabled, type, hz, q)                                                         \
    paramBool("B" #n " Enabled", enabled), paramChoice("B" #n " Type", choices::kEqBandType, type),      \
        paramFloat("B" #n " Frequency", "Hz", 20.0f, 20000.0f, hz, true),                               \
        paramFloat("B" #n " Q", "", 0.1f, 18.0f, q), paramFloat("B" #n " Gain", "dB", -24.0f, 24.0f, 0.0f)
    static constexpr ParameterInfo kInfos[kCount] = {
        DAWLAB_EQ_BAND(1, false, 0, 80.0f, 0.707f),    // LowCut
        DAWLAB_EQ_BAND(2, true, 1, 200.0f, 0.707f),    // LowShelf
        DAWLAB_EQ_BAND(3, true, 2, 500.0f, 1.0f),      // Peak
        DAWLAB_EQ_BAND(4, true, 2, 2000.0f, 1.0f),     // Peak
        DAWLAB_EQ_BAND(5, true, 3, 6000.0f, 0.707f),   // HighShelf
        DAWLAB_EQ_BAND(6, false, 4, 12000.0f, 0.707f), // HighCut
    };
#undef DAWLAB_EQ_BAND
    dsp::ParametricEq dsp_;
};

// =============================================================================================
// 04  Compressor
// =============================================================================================
class CompressorEffect final : public EffectBase {
public:
    enum Param { Threshold, Ratio, Knee, Attack, Release, Makeup, StereoLink, kCount };
    CompressorEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "Compressor"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
    }
    void reset() noexcept override { dsp_.reset(); }
    bool hasMeter() const noexcept override { return true; }
    float getMeterDb() const noexcept override { return dsp_.getGainReductionDb(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        switch (index) {
        case Threshold: dsp_.setThresholdDb(v); break;
        case Ratio: dsp_.setRatio(v); break;
        case Knee: dsp_.setKneeDb(v); break;
        case Attack: dsp_.setAttackMs(v); break;
        case Release: dsp_.setReleaseMs(v); break;
        case Makeup: dsp_.setMakeupGainDb(v); break;
        case StereoLink: dsp_.setStereoLink(v > 0.5f); break;
        default: break;
        }
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        paramFloat("Threshold", "dB", -60.0f, 0.0f, -18.0f),
        paramFloat("Ratio", ":1", 1.0f, 20.0f, 4.0f),
        paramFloat("Knee", "dB", 0.0f, 24.0f, 6.0f),
        paramFloat("Attack", "ms", 0.1f, 200.0f, 10.0f, true),
        paramFloat("Release", "ms", 5.0f, 2000.0f, 100.0f, true),
        paramFloat("Makeup", "dB", 0.0f, 24.0f, 0.0f),
        paramBool("Stereo Link", true),
    };
    dsp::Compressor dsp_;
};

// =============================================================================================
// 05  Limiter  (lookahead = latency)
// =============================================================================================
class LimiterEffect final : public EffectBase {
public:
    enum Param { Ceiling, InputGain, Release, Lookahead, kCount };
    LimiterEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "Limiter"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
    }
    void reset() noexcept override { dsp_.reset(); }
    // Asked from the DSP object: if the chapter-05 limiter caps the lookahead lower than our
    // knob range, the reported latency is still the truth.
    int getLatencySamples() const noexcept override { return dsp_.getLatencySamples(); }
    bool hasMeter() const noexcept override { return true; }
    float getMeterDb() const noexcept override { return dsp_.getGainReductionDb(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        switch (index) {
        case Ceiling: dsp_.setCeilingDb(v); break;
        case InputGain: dsp_.setInputGainDb(v); break;
        case Release: dsp_.setReleaseMs(v); break;
        case Lookahead: dsp_.setLookaheadMs(v); break;
        default: break;
        }
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        paramFloat("Ceiling", "dB", -20.0f, 0.0f, -1.0f),
        paramFloat("Input Gain", "dB", 0.0f, 24.0f, 0.0f),
        paramFloat("Release", "ms", 1.0f, 1000.0f, 50.0f, true),
        paramFloat("Lookahead", "ms", 0.0f, 10.0f, 5.0f),
    };
    dsp::Limiter dsp_;
};

// =============================================================================================
// 06  Saturation  (oversampling is a prepare-time parameter)
// =============================================================================================
class SaturationEffect final : public EffectBase {
public:
    enum Param { Curve, Drive, Mix, Output, Oversampling, kCount };
    SaturationEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "Saturation"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.setOversamplingFactorLog2(static_cast<int>(getParameter(Oversampling))); // before prepare
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
    }
    void reset() noexcept override { dsp_.reset(); }
    int getLatencySamples() const noexcept override { return dsp_.getLatencySamples(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        switch (index) {
        case Curve: dsp_.setCurve(static_cast<dsp::SaturationCurve>(static_cast<int>(v))); break;
        case Drive: dsp_.setDriveDb(v); break;
        case Mix: dsp_.setMix(v); break;
        case Output: dsp_.setOutputGainDb(v); break;
        default: break; // Oversampling: applied at prepare()
        }
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        paramChoice("Curve", choices::kSaturationCurve, 0),
        paramFloat("Drive", "dB", 0.0f, 36.0f, 6.0f),
        paramFloat("Mix", "", 0.0f, 1.0f, 1.0f),
        paramFloat("Output", "dB", -24.0f, 24.0f, 0.0f),
        paramChoice("Oversampling", choices::kOversampling, 1),
    };
    dsp::Saturation dsp_;
};

// =============================================================================================
// 07  Distortion
// =============================================================================================
class DistortionEffect final : public EffectBase {
public:
    enum Param { Type, Drive, Tone, Mix, Output, BitDepth, Downsample, kCount };
    DistortionEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "Distortion"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
    }
    void reset() noexcept override { dsp_.reset(); }
    int getLatencySamples() const noexcept override { return dsp_.getLatencySamples(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        switch (index) {
        case Type: dsp_.setType(static_cast<dsp::DistortionType>(static_cast<int>(v))); break;
        case Drive: dsp_.setDriveDb(v); break;
        case Tone: dsp_.setToneHz(v); break;
        case Mix: dsp_.setMix(v); break;
        case Output: dsp_.setOutputGainDb(v); break;
        case BitDepth: dsp_.setBitDepth(v); break;
        case Downsample: dsp_.setDownsampleFactor(static_cast<int>(v)); break;
        default: break;
        }
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        paramChoice("Type", choices::kDistortionType, 0),
        paramFloat("Drive", "dB", 0.0f, 48.0f, 12.0f),
        paramFloat("Tone", "Hz", 200.0f, 20000.0f, 8000.0f, true),
        paramFloat("Mix", "", 0.0f, 1.0f, 1.0f),
        paramFloat("Output", "dB", -24.0f, 24.0f, -6.0f),
        paramFloat("Bit Depth", "bits", 1.0f, 24.0f, 8.0f),
        paramInt("Downsample", "x", 1, 64, 1),
    };
    dsp::Distortion dsp_;
};

// =============================================================================================
// 07  AmpChain  (input HPF -> pre-EQ -> preamp -> tone stack -> power amp -> cabinet -> output;
//               oversampling is a prepare-time parameter, like Saturation's)
// =============================================================================================
class AmpChainEffect final : public EffectBase {
public:
    enum Param {
        InputHighpass,
        PreEqFrequency,
        PreEqGain,
        PreampDrive,
        Bass,
        Mid,
        Treble,
        PowerAmpDrive,
        PowerAmpType,
        Cabinet,
        CabinetCutoff,
        CabinetResonance,
        CabinetResonanceGain,
        Output,
        Oversampling,
        kCount
    };
    AmpChainEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "AmpChain"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.setOversamplingFactorLog2(static_cast<int>(getParameter(Oversampling))); // before prepare
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
        dsp_.reset(); // start at the configured output gain instead of ramping to it
    }
    void reset() noexcept override { dsp_.reset(); }
    // Both nonlinear stages oversample; the chain adds their two latencies.
    int getLatencySamples() const noexcept override { return dsp_.getLatencySamples(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        switch (index) {
        case InputHighpass: dsp_.setInputHighpassHz(v); break;
        case PreEqFrequency: dsp_.setPreEqHz(v); break;
        case PreEqGain: dsp_.setPreEqGainDb(v); break;
        case PreampDrive: dsp_.setPreampDriveDb(v); break;
        case Bass: dsp_.setBassDb(v); break;
        case Mid: dsp_.setMidDb(v); break;
        case Treble: dsp_.setTrebleDb(v); break;
        case PowerAmpDrive: dsp_.setPowerAmpDriveDb(v); break;
        case PowerAmpType: dsp_.setPowerAmpType(static_cast<dsp::DistortionType>(static_cast<int>(v))); break;
        case Cabinet: dsp_.setCabinetEnabled(v > 0.5f); break;
        case CabinetCutoff: dsp_.setCabinetCutoffHz(v); break;
        case CabinetResonance: dsp_.setCabinetResonanceHz(v); break;
        case CabinetResonanceGain: dsp_.setCabinetResonanceDb(v); break;
        case Output: dsp_.setOutputGainDb(v); break;
        default: break; // Oversampling: applied at prepare()
        }
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    // Ranges follow the clamps inside dsp::AmpChain (chapter 07); the defaults are a crunchy
    // rhythm sound.
    static constexpr ParameterInfo kInfos[kCount] = {
        paramFloat("Input Highpass", "Hz", 20.0f, 500.0f, 80.0f, true),
        paramFloat("Pre-EQ Frequency", "Hz", 200.0f, 3000.0f, 800.0f, true),
        paramFloat("Pre-EQ Gain", "dB", -12.0f, 12.0f, 6.0f),
        paramFloat("Preamp Drive", "dB", 0.0f, 48.0f, 18.0f),
        paramFloat("Bass", "dB", -12.0f, 12.0f, 0.0f),
        paramFloat("Mid", "dB", -12.0f, 12.0f, 0.0f),
        paramFloat("Treble", "dB", -12.0f, 12.0f, 0.0f),
        paramFloat("Power Amp Drive", "dB", 0.0f, 36.0f, 6.0f),
        paramChoice("Power Amp Type", choices::kDistortionType, 0),
        paramBool("Cabinet", true),
        paramFloat("Cabinet Cutoff", "Hz", 1000.0f, 12000.0f, 4500.0f, true),
        paramFloat("Cabinet Resonance", "Hz", 50.0f, 400.0f, 110.0f, true),
        paramFloat("Resonance Gain", "dB", 0.0f, 12.0f, 4.0f),
        paramFloat("Output", "dB", -36.0f, 12.0f, -12.0f),
        paramChoice("Oversampling", choices::kOversampling, 2),
    };
    dsp::AmpChain dsp_;
};

// =============================================================================================
// 08  Gate / Expander / DeEsser / TransientShaper
// =============================================================================================
class GateEffect final : public EffectBase {
public:
    enum Param { Threshold, Hysteresis, Range, Attack, Hold, Release, kCount };
    GateEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "Gate"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
    }
    void reset() noexcept override { dsp_.reset(); }
    bool hasMeter() const noexcept override { return true; }
    float getMeterDb() const noexcept override { return dsp_.getGainReductionDb(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        switch (index) {
        case Threshold: dsp_.setThresholdDb(v); break;
        case Hysteresis: dsp_.setHysteresisDb(v); break;
        case Range: dsp_.setRangeDb(v); break;
        case Attack: dsp_.setAttackMs(v); break;
        case Hold: dsp_.setHoldMs(v); break;
        case Release: dsp_.setReleaseMs(v); break;
        default: break;
        }
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        paramFloat("Threshold", "dB", -80.0f, 0.0f, -40.0f),
        paramFloat("Hysteresis", "dB", 0.0f, 24.0f, 3.0f),
        paramFloat("Range", "dB", -100.0f, 0.0f, -80.0f),
        paramFloat("Attack", "ms", 0.1f, 100.0f, 1.0f, true),
        paramFloat("Hold", "ms", 0.0f, 1000.0f, 50.0f),
        paramFloat("Release", "ms", 1.0f, 2000.0f, 100.0f, true),
    };
    dsp::Gate dsp_;
};

class ExpanderEffect final : public EffectBase {
public:
    enum Param { Threshold, Ratio, Knee, Range, Attack, Release, kCount };
    ExpanderEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "Expander"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
    }
    void reset() noexcept override { dsp_.reset(); }
    bool hasMeter() const noexcept override { return true; }
    float getMeterDb() const noexcept override { return dsp_.getGainReductionDb(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        switch (index) {
        case Threshold: dsp_.setThresholdDb(v); break;
        case Ratio: dsp_.setRatio(v); break;
        case Knee: dsp_.setKneeDb(v); break;
        case Range: dsp_.setRangeDb(v); break;
        case Attack: dsp_.setAttackMs(v); break;
        case Release: dsp_.setReleaseMs(v); break;
        default: break;
        }
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        paramFloat("Threshold", "dB", -80.0f, 0.0f, -40.0f),
        paramFloat("Ratio", ":1", 1.0f, 10.0f, 2.0f),
        paramFloat("Knee", "dB", 0.0f, 24.0f, 6.0f),
        paramFloat("Range", "dB", -100.0f, 0.0f, -40.0f),
        paramFloat("Attack", "ms", 0.1f, 100.0f, 5.0f, true),
        paramFloat("Release", "ms", 1.0f, 2000.0f, 100.0f, true),
    };
    dsp::Expander dsp_;
};

class DeEsserEffect final : public EffectBase {
public:
    enum Param { Frequency, Threshold, Range, SplitBand, Listen, kCount };
    DeEsserEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "DeEsser"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
    }
    void reset() noexcept override { dsp_.reset(); }
    bool hasMeter() const noexcept override { return true; }
    float getMeterDb() const noexcept override { return dsp_.getGainReductionDb(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        switch (index) {
        case Frequency: dsp_.setFrequency(v); break;
        case Threshold: dsp_.setThresholdDb(v); break;
        case Range: dsp_.setRangeDb(v); break;
        case SplitBand: dsp_.setSplitBand(v > 0.5f); break;
        case Listen: dsp_.setListen(v > 0.5f); break;
        default: break;
        }
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        paramFloat("Frequency", "Hz", 2000.0f, 12000.0f, 6000.0f, true),
        paramFloat("Threshold", "dB", -60.0f, 0.0f, -20.0f),
        paramFloat("Range", "dB", 0.0f, 24.0f, 12.0f),
        paramBool("Split Band", true),
        paramBool("Listen", false),
    };
    dsp::DeEsser dsp_;
};

class TransientShaperEffect final : public EffectBase {
public:
    enum Param { Attack, Sustain, kCount };
    TransientShaperEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "TransientShaper"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
    }
    void reset() noexcept override { dsp_.reset(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        switch (index) {
        case Attack: dsp_.setAttackDb(v); break;
        case Sustain: dsp_.setSustainDb(v); break;
        default: break;
        }
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        paramFloat("Attack", "dB", -24.0f, 24.0f, 0.0f),
        paramFloat("Sustain", "dB", -24.0f, 24.0f, 0.0f),
    };
    dsp::TransientShaper dsp_;
};

// =============================================================================================
// 09  Delay
// =============================================================================================
class DelayEffect final : public EffectBase {
public:
    enum Param { Time, Feedback, Mix, PingPong, Lowpass, Highpass, kCount };
    static constexpr float kMaxDelayMs = 2000.0f;
    DelayEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "Delay"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.setMaxDelayMs(kMaxDelayMs); // prepare-time
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
    }
    void reset() noexcept override { dsp_.reset(); }

    // Tempo helper: a dotted eighth (beats = 0.75) at 120 BPM is 375 ms.
    static float noteToMs(double bpm, double beats) noexcept { return dsp::Delay::noteToMs(bpm, beats); }

protected:
    void applyParameter(int index, float v) noexcept override {
        switch (index) {
        case Time: dsp_.setDelayMs(v); break;
        case Feedback: dsp_.setFeedback(v); break;
        case Mix: dsp_.setMix(v); break;
        case PingPong: dsp_.setPingPong(v > 0.5f); break;
        case Lowpass: dsp_.setLowpassHz(v); break;
        case Highpass: dsp_.setHighpassHz(v); break;
        default: break;
        }
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        paramFloat("Time", "ms", 1.0f, kMaxDelayMs, 375.0f, true),
        paramFloat("Feedback", "", 0.0f, 0.95f, 0.4f),
        paramFloat("Mix", "", 0.0f, 1.0f, 0.35f),
        paramBool("Ping Pong", false),
        paramFloat("Lowpass", "Hz", 200.0f, 20000.0f, 8000.0f, true),
        paramFloat("Highpass", "Hz", 20.0f, 2000.0f, 100.0f, true),
    };
    dsp::Delay dsp_;
};

// =============================================================================================
// 10  Chorus / Flanger / Phaser / Tremolo
// =============================================================================================
class ChorusEffect final : public EffectBase {
public:
    enum Param { Rate, Depth, Centre, Voices, Mix, Spread, kCount };
    ChorusEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "Chorus"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
    }
    void reset() noexcept override { dsp_.reset(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        switch (index) {
        case Rate: dsp_.setRateHz(v); break;
        case Depth: dsp_.setDepthMs(v); break;
        case Centre: dsp_.setCentreDelayMs(v); break;
        case Voices: dsp_.setVoices(static_cast<int>(v)); break;
        case Mix: dsp_.setMix(v); break;
        case Spread: dsp_.setStereoSpread(v); break;
        default: break;
        }
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        paramFloat("Rate", "Hz", 0.01f, 10.0f, 0.8f, true),
        paramFloat("Depth", "ms", 0.0f, 10.0f, 3.0f),
        paramFloat("Centre Delay", "ms", 1.0f, 30.0f, 7.0f),
        paramInt("Voices", "", 1, 4, 2),
        paramFloat("Mix", "", 0.0f, 1.0f, 0.5f),
        paramFloat("Stereo Spread", "", 0.0f, 1.0f, 1.0f),
    };
    dsp::Chorus dsp_;
};

class FlangerEffect final : public EffectBase {
public:
    enum Param { Rate, Depth, Delay, Feedback, Mix, kCount };
    FlangerEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "Flanger"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
    }
    void reset() noexcept override { dsp_.reset(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        switch (index) {
        case Rate: dsp_.setRateHz(v); break;
        case Depth: dsp_.setDepthMs(v); break;
        case Delay: dsp_.setDelayMs(v); break;
        case Feedback: dsp_.setFeedback(v); break;
        case Mix: dsp_.setMix(v); break;
        default: break;
        }
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        paramFloat("Rate", "Hz", 0.01f, 10.0f, 0.3f, true),
        paramFloat("Depth", "ms", 0.0f, 5.0f, 2.0f),
        paramFloat("Delay", "ms", 0.1f, 10.0f, 1.0f),
        paramFloat("Feedback", "", -0.95f, 0.95f, 0.5f),
        paramFloat("Mix", "", 0.0f, 1.0f, 0.5f),
    };
    dsp::Flanger dsp_;
};

class PhaserEffect final : public EffectBase {
public:
    enum Param { Rate, Depth, Centre, Feedback, Stages, Mix, kCount };
    PhaserEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "Phaser"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
    }
    void reset() noexcept override { dsp_.reset(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        switch (index) {
        case Rate: dsp_.setRateHz(v); break;
        case Depth: dsp_.setDepth(v); break;
        case Centre: dsp_.setCentreHz(v); break;
        case Feedback: dsp_.setFeedback(v); break;
        case Stages: dsp_.setStages((static_cast<int>(v) / 2) * 2); break; // even counts only
        case Mix: dsp_.setMix(v); break;
        default: break;
        }
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        paramFloat("Rate", "Hz", 0.01f, 10.0f, 0.5f, true),
        paramFloat("Depth", "", 0.0f, 1.0f, 0.8f),
        paramFloat("Centre", "Hz", 100.0f, 8000.0f, 800.0f, true),
        paramFloat("Feedback", "", -0.9f, 0.9f, 0.3f),
        paramInt("Stages", "", 2, 12, 4),
        paramFloat("Mix", "", 0.0f, 1.0f, 0.5f),
    };
    dsp::Phaser dsp_;
};

class TremoloEffect final : public EffectBase {
public:
    enum Param { Rate, Depth, Shape, AutoPan, kCount };
    TremoloEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "Tremolo"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
    }
    void reset() noexcept override { dsp_.reset(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        switch (index) {
        case Rate: dsp_.setRateHz(v); break;
        case Depth: dsp_.setDepth(v); break;
        case Shape: dsp_.setShape(static_cast<dsp::LfoShape>(static_cast<int>(v))); break;
        case AutoPan: dsp_.setAutoPan(v > 0.5f); break;
        default: break;
        }
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        paramFloat("Rate", "Hz", 0.1f, 20.0f, 5.0f, true),
        paramFloat("Depth", "", 0.0f, 1.0f, 0.5f),
        paramChoice("Shape", choices::kLfoShape, 0),
        paramBool("Auto Pan", false),
    };
    dsp::Tremolo dsp_;
};

// =============================================================================================
// 12  Reverb / ConvolutionReverb
// =============================================================================================
class ReverbEffect final : public EffectBase {
public:
    enum Param { Decay, PreDelay, Damping, Size, Width, Mix, Freeze, kCount };
    ReverbEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "Reverb"; }
    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.prepare(sr, maxBlock, nch);
        finishPrepare(sr);
    }
    void reset() noexcept override { dsp_.reset(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        switch (index) {
        case Decay: dsp_.setDecaySeconds(v); break;
        case PreDelay: dsp_.setPreDelayMs(v); break;
        case Damping: dsp_.setDampingHz(v); break;
        case Size: dsp_.setSize(v); break;
        case Width: dsp_.setWidth(v); break;
        case Mix: dsp_.setMix(v); break;
        case Freeze: dsp_.setFreeze(v > 0.5f); break;
        default: break;
        }
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        paramFloat("Decay", "s", 0.1f, 20.0f, 2.0f, true),
        paramFloat("Pre-delay", "ms", 0.0f, 200.0f, 20.0f),
        paramFloat("Damping", "Hz", 1000.0f, 20000.0f, 6000.0f, true),
        paramFloat("Size", "", 0.0f, 1.0f, 0.7f),
        paramFloat("Width", "", 0.0f, 1.0f, 1.0f),
        paramFloat("Mix", "", 0.0f, 1.0f, 0.3f),
        paramBool("Freeze", false),
    };
    dsp::Reverb dsp_;
};

class ConvolutionReverbEffect final : public EffectBase {
public:
    enum Param { Mix, kCount };
    static constexpr double kDefaultRt60Seconds = 1.5;
    ConvolutionReverbEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "ConvolutionReverb"; }

    // Non-audio thread only (allocates). Before the effect is handed to the engine.
    void setImpulseResponse(const AudioBuffer& ir) {
        ir_ = ir;
        hasCustomIr_ = true;
        if (isPrepared())
            dsp_.setImpulseResponse(ir_);
    }

    void prepare(double sr, int maxBlock, int nch) override {
        dsp_.prepare(sr, maxBlock, nch);
        if (!hasCustomIr_) // a synthetic room so the effect does something out of the box
            ir_ = dsp::ConvolutionReverb::makeSyntheticImpulseResponse(sr, kDefaultRt60Seconds, nch);
        dsp_.setImpulseResponse(ir_);
        finishPrepare(sr);
    }
    void reset() noexcept override { dsp_.reset(); }
    int getLatencySamples() const noexcept override { return dsp_.getLatencySamples(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        if (index == Mix)
            dsp_.setMix(v);
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        paramFloat("Mix", "", 0.0f, 1.0f, 0.3f),
    };
    dsp::ConvolutionReverb dsp_;
    AudioBuffer ir_;
    bool hasCustomIr_ = false;
};

// =============================================================================================
// Factory: name <-> type <-> instance. The UI's "add insert" menu is built from this list,
// and a project file (Exercise 4) stores effects by name.
// =============================================================================================
enum class EffectType {
    Gain,
    ParametricEq,
    Compressor,
    Limiter,
    Saturation,
    Distortion,
    Gate,
    Expander,
    DeEsser,
    TransientShaper,
    Delay,
    Chorus,
    Flanger,
    Phaser,
    Tremolo,
    Reverb,
    ConvolutionReverb,
    // Added after the demo song (Step 9) was fixed: the song does not use these two, so every
    // number it produces stays the same. Appended so that the indices above do not move.
    Filter,    // chapter 02 StateVariableFilter
    AmpChain,  // chapter 07 AmpChain
    PureDelay, // test helper (Step 8); not meant for a user-facing menu
};
constexpr int kNumEffectTypes = static_cast<int>(EffectType::PureDelay) + 1;
constexpr int kNumUserEffectTypes = static_cast<int>(EffectType::AmpChain) + 1; // the 19 musical ones

inline const char* effectTypeName(EffectType type) noexcept {
    switch (type) {
    case EffectType::Gain: return "Gain";
    case EffectType::ParametricEq: return "ParametricEQ";
    case EffectType::Compressor: return "Compressor";
    case EffectType::Limiter: return "Limiter";
    case EffectType::Saturation: return "Saturation";
    case EffectType::Distortion: return "Distortion";
    case EffectType::Gate: return "Gate";
    case EffectType::Expander: return "Expander";
    case EffectType::DeEsser: return "DeEsser";
    case EffectType::TransientShaper: return "TransientShaper";
    case EffectType::Delay: return "Delay";
    case EffectType::Chorus: return "Chorus";
    case EffectType::Flanger: return "Flanger";
    case EffectType::Phaser: return "Phaser";
    case EffectType::Tremolo: return "Tremolo";
    case EffectType::Reverb: return "Reverb";
    case EffectType::ConvolutionReverb: return "ConvolutionReverb";
    case EffectType::Filter: return "Filter";
    case EffectType::AmpChain: return "AmpChain";
    case EffectType::PureDelay: return "PureDelay";
    }
    return "?";
}

// The name equals InsertEffect::getName() of the created effect. False if unknown.
inline bool effectTypeFromName(const char* name, EffectType& out) noexcept {
    if (name == nullptr)
        return false;
    for (int i = 0; i < kNumEffectTypes; ++i) {
        const auto t = static_cast<EffectType>(i);
        if (std::strcmp(effectTypeName(t), name) == 0) {
            out = t;
            return true;
        }
    }
    return false;
}

// Allocates: UI / loading thread only. The effect is NOT prepared yet.
inline std::unique_ptr<InsertEffect> createEffect(EffectType type) {
    switch (type) {
    case EffectType::Gain: return std::make_unique<GainEffect>();
    case EffectType::ParametricEq: return std::make_unique<ParametricEqEffect>();
    case EffectType::Compressor: return std::make_unique<CompressorEffect>();
    case EffectType::Limiter: return std::make_unique<LimiterEffect>();
    case EffectType::Saturation: return std::make_unique<SaturationEffect>();
    case EffectType::Distortion: return std::make_unique<DistortionEffect>();
    case EffectType::Gate: return std::make_unique<GateEffect>();
    case EffectType::Expander: return std::make_unique<ExpanderEffect>();
    case EffectType::DeEsser: return std::make_unique<DeEsserEffect>();
    case EffectType::TransientShaper: return std::make_unique<TransientShaperEffect>();
    case EffectType::Delay: return std::make_unique<DelayEffect>();
    case EffectType::Chorus: return std::make_unique<ChorusEffect>();
    case EffectType::Flanger: return std::make_unique<FlangerEffect>();
    case EffectType::Phaser: return std::make_unique<PhaserEffect>();
    case EffectType::Tremolo: return std::make_unique<TremoloEffect>();
    case EffectType::Reverb: return std::make_unique<ReverbEffect>();
    case EffectType::ConvolutionReverb: return std::make_unique<ConvolutionReverbEffect>();
    case EffectType::Filter: return std::make_unique<FilterEffect>();
    case EffectType::AmpChain: return std::make_unique<AmpChainEffect>();
    case EffectType::PureDelay: return std::make_unique<PureDelayEffect>();
    }
    return nullptr;
}

inline std::unique_ptr<InsertEffect> createEffect(const char* name) {
    EffectType t{};
    return effectTypeFromName(name, t) ? createEffect(t) : nullptr;
}

} // namespace dawlab::engine
