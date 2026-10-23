#pragma once
// AmpChain.h —— 第 07 章：簡易吉他 amp 模擬鏈。
//
//   in → input HPF → pre-EQ → preamp（Saturation, tanh）→ tone stack（bass / mid / treble）
//      → power amp（Distortion, 預設 HardClip）→ cabinet（LR4 lowpass + 共振 peak）→ output gain → out
//
// 每一級都是前幾章已經有的元件：第 02 章 OnePole / Biquad、第 06 章 Saturation、本章 Distortion。
// 這個 class 的重點不是模擬得多像，而是示範「順序」：同樣的元件換個順序，聲音就完全不同（見練習 7）。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/Distortion.h>
#include <dawlab/dsp/OnePole.h>
#include <dawlab/dsp/Saturation.h>

#include <algorithm>
#include <cmath>

namespace dawlab::dsp {

class AmpChain {
public:
    // 各線性級的固定頻率（只有增益可調）：真實 tone stack 的轉折頻率也是由零件決定的
    static constexpr double kPreEqQ = 0.8;
    static constexpr double kBassHz = 120.0;
    static constexpr double kMidHz = 700.0;
    static constexpr double kMidQ = 0.7;
    static constexpr double kTrebleHz = 3000.0;
    static constexpr double kShelfQ = 0.7071;
    static constexpr double kCabinetQ = 0.7071; // 兩顆 Butterworth 2 階串接 = Linkwitz-Riley 4 階（cutoff 處 -6 dB）
    static constexpr double kCabinetResonanceQ = 1.5;

    // 在 prepare() 之前呼叫；同時套用到 preamp 與 power amp
    void setOversamplingFactorLog2(int factorLog2) noexcept {
        osLog2_ = std::clamp(factorLog2, 0, Distortion::kMaxOversamplingLog2);
    }

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        inputHp_.prepare(sampleRate, maxBlockSize, numChannels);
        inputHp_.setType(OnePole::Type::Highpass);
        preEq_.prepare(sampleRate, maxBlockSize, numChannels);
        preamp_.setOversamplingFactorLog2(osLog2_);
        preamp_.prepare(sampleRate, maxBlockSize, numChannels);
        preamp_.setCurve(SaturationCurve::Tanh);
        preamp_.setMix(1.0f);
        preamp_.setOutputGainDb(0.0f);
        bass_.prepare(sampleRate, maxBlockSize, numChannels);
        mid_.prepare(sampleRate, maxBlockSize, numChannels);
        treble_.prepare(sampleRate, maxBlockSize, numChannels);
        powerAmp_.setOversamplingFactorLog2(osLog2_);
        powerAmp_.prepare(sampleRate, maxBlockSize, numChannels);
        powerAmp_.setMix(1.0f);
        powerAmp_.setToneHz(Distortion::kToneBypassHz);
        cabLp1_.prepare(sampleRate, maxBlockSize, numChannels);
        cabLp2_.prepare(sampleRate, maxBlockSize, numChannels);
        cabResonance_.prepare(sampleRate, maxBlockSize, numChannels);
        outputGain_.snap(dbToGain(outputGainDb_));
        applyParameters();
    }

    void reset() noexcept {
        inputHp_.reset();
        preEq_.reset();
        preamp_.reset();
        bass_.reset();
        mid_.reset();
        treble_.reset();
        powerAmp_.reset();
        cabLp1_.reset();
        cabLp2_.reset();
        cabResonance_.reset();
        outputGain_.snap(outputGain_.target);
    }

    // ---- 參數（都可以在 block 之間呼叫）--------------------------------------------------------
    void setInputHighpassHz(float hz) noexcept { inputHpHz_ = std::clamp(hz, 20.0f, 500.0f); dirty_ = true; }
    void setPreEqHz(float hz) noexcept { preEqHz_ = std::clamp(hz, 200.0f, 3000.0f); dirty_ = true; }
    void setPreEqGainDb(float db) noexcept { preEqGainDb_ = std::clamp(db, -12.0f, 12.0f); dirty_ = true; }
    void setPreampDriveDb(float db) noexcept { preamp_.setDriveDb(db); }
    void setPowerAmpDriveDb(float db) noexcept { powerAmp_.setDriveDb(db); }
    void setPowerAmpType(DistortionType type) noexcept { powerAmp_.setType(type); }
    void setBassDb(float db) noexcept { bassDb_ = std::clamp(db, -12.0f, 12.0f); dirty_ = true; }
    void setMidDb(float db) noexcept { midDb_ = std::clamp(db, -12.0f, 12.0f); dirty_ = true; }
    void setTrebleDb(float db) noexcept { trebleDb_ = std::clamp(db, -12.0f, 12.0f); dirty_ = true; }
    void setCabinetEnabled(bool enabled) noexcept { cabinetEnabled_ = enabled; }
    void setCabinetCutoffHz(float hz) noexcept { cabHz_ = std::clamp(hz, 1000.0f, 12000.0f); dirty_ = true; }
    void setCabinetResonanceHz(float hz) noexcept { cabResHz_ = std::clamp(hz, 50.0f, 400.0f); dirty_ = true; }
    void setCabinetResonanceDb(float db) noexcept { cabResDb_ = std::clamp(db, 0.0f, 12.0f); dirty_ = true; }
    void setOutputGainDb(float db) noexcept {
        outputGainDb_ = std::clamp(db, -60.0f, 24.0f);
        outputGain_.target = dbToGain(outputGainDb_);
    }

    int getLatencySamples() const noexcept { return preamp_.getLatencySamples() + powerAmp_.getLatencySamples(); }

    // ---- 給 UI / 範例畫曲線用的線性級頻率響應（不含非線性級）-------------------------------------
    // 直接用「目前的參數值」算係數，所以 setter 之後馬上呼叫就是新的曲線（不必等下一個 process()）。
    // 串接的線性系統：響應相乘 = dB 相加。
    double getPreEqMagnitudeDb(double freq) const noexcept { return preEqCoefficients().magnitudeDb(freq, sampleRate_); }
    double getToneStackMagnitudeDb(double freq) const noexcept {
        return bassCoefficients().magnitudeDb(freq, sampleRate_) + midCoefficients().magnitudeDb(freq, sampleRate_) +
               trebleCoefficients().magnitudeDb(freq, sampleRate_);
    }
    double getCabinetMagnitudeDb(double freq) const noexcept {
        if (!cabinetEnabled_)
            return 0.0;
        return 2.0 * cabLowpassCoefficients().magnitudeDb(freq, sampleRate_) +
               cabResonanceCoefficients().magnitudeDb(freq, sampleRate_);
    }

    // ---- 處理 ---------------------------------------------------------------------------------
    void process(AudioBlock block) noexcept {
        if (dirty_)
            applyParameters();
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        if (n <= 0 || nch <= 0)
            return;

        // 1. input HPF + pre-EQ：先把不需要的低頻拿掉，再把要「推」的中頻抬起來
        for (int ch = 0; ch < nch; ++ch) {
            float* x = block.getChannelPointer(ch);
            for (int i = 0; i < n; ++i)
                x[i] = preEq_.processSample(ch, inputHp_.processSample(ch, x[i]));
        }
        // 2. preamp（軟 clip）
        preamp_.process(block);
        // 3. tone stack（在兩個非線性級之間，跟真的 amp 一樣）
        for (int ch = 0; ch < nch; ++ch) {
            float* x = block.getChannelPointer(ch);
            for (int i = 0; i < n; ++i)
                x[i] = treble_.processSample(ch, mid_.processSample(ch, bass_.processSample(ch, x[i])));
        }
        // 4. power amp（硬 clip）
        powerAmp_.process(block);
        // 5. cabinet：喇叭單體聽不到 5 kHz 以上，而且在低頻有共振
        if (cabinetEnabled_) {
            for (int ch = 0; ch < nch; ++ch) {
                float* x = block.getChannelPointer(ch);
                for (int i = 0; i < n; ++i)
                    x[i] = cabResonance_.processSample(ch, cabLp2_.processSample(ch, cabLp1_.processSample(ch, x[i])));
            }
        }
        // 6. output gain（每個 block 線性插值）
        const float g0 = outputGain_.current, inc = outputGain_.incrementFor(n);
        for (int ch = 0; ch < nch; ++ch) {
            float* x = block.getChannelPointer(ch);
            for (int i = 0; i < n; ++i)
                x[i] *= g0 + inc * static_cast<float>(i);
        }
        outputGain_.current = outputGain_.target;
    }

private:
    BiquadCoefficients preEqCoefficients() const noexcept {
        return BiquadCoefficients::makePeak(sampleRate_, preEqHz_, kPreEqQ, preEqGainDb_);
    }
    BiquadCoefficients bassCoefficients() const noexcept {
        return BiquadCoefficients::makeLowShelf(sampleRate_, kBassHz, kShelfQ, bassDb_);
    }
    BiquadCoefficients midCoefficients() const noexcept {
        return BiquadCoefficients::makePeak(sampleRate_, kMidHz, kMidQ, midDb_);
    }
    BiquadCoefficients trebleCoefficients() const noexcept {
        return BiquadCoefficients::makeHighShelf(sampleRate_, kTrebleHz, kShelfQ, trebleDb_);
    }
    BiquadCoefficients cabLowpassCoefficients() const noexcept {
        return BiquadCoefficients::makeLowpass(sampleRate_, cabHz_, kCabinetQ);
    }
    BiquadCoefficients cabResonanceCoefficients() const noexcept {
        return BiquadCoefficients::makePeak(sampleRate_, cabResHz_, kCabinetResonanceQ, cabResDb_);
    }

    void applyParameters() noexcept {
        dirty_ = false;
        inputHp_.setCutoff(inputHpHz_);
        preEq_.setCoefficients(preEqCoefficients());
        bass_.setCoefficients(bassCoefficients());
        mid_.setCoefficients(midCoefficients());
        treble_.setCoefficients(trebleCoefficients());
        cabLp1_.setCoefficients(cabLowpassCoefficients());
        cabLp2_.setCoefficients(cabLowpassCoefficients());
        cabResonance_.setCoefficients(cabResonanceCoefficients());
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    int osLog2_ = 2;
    bool dirty_ = true;
    bool cabinetEnabled_ = true;

    float inputHpHz_ = 80.0f;
    float preEqHz_ = 800.0f;
    float preEqGainDb_ = 6.0f;
    float bassDb_ = 0.0f, midDb_ = 0.0f, trebleDb_ = 0.0f;
    float cabHz_ = 4500.0f;
    float cabResHz_ = 110.0f;
    float cabResDb_ = 4.0f;
    float outputGainDb_ = 0.0f;

    OnePole inputHp_;
    Biquad preEq_;
    Saturation preamp_;
    Biquad bass_, mid_, treble_;
    Distortion powerAmp_;
    Biquad cabLp1_, cabLp2_, cabResonance_;
    detail::BlockRamp outputGain_;
};

} // namespace dawlab::dsp
