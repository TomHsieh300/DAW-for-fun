#pragma once
// ChannelStrip: one strip of the mixer (Steps 4 and 6). Tracks, the two FX buses and the
// master are all ChannelStrips; a Track adds clips on top (Track.h).
//
//   in --> [inserts] --> [PDC delay] --> [mute/solo] --+--> pre-fader sends
//                                                      |
//                                                   [fader x pan]
//                                                      |
//                                                      +--> post-fader sends
//                                                      |
//                                                   [meters] --> out
//
// Mute/solo sits *before* the pre-fader send tap: a muted (or solo-silenced) track sends
// nothing to the buses, so soloing the drums really gives you only the drums (plus their own
// reverb return, because buses are "solo safe").
//
// Fader and pan are one gain per side:
//   g_L = 10^(faderDb / 20) * panL,   g_R = 10^(faderDb / 20) * panR
// Each side is ramped with a dsp::SmoothedValue (50 ms) so moving a fader never clicks;
// mute/solo uses its own 10 ms ramp. Two pan modes:
//   PanMode::PanLaw   panL / panR from dsp::Panner::computeGains(pan, law) (chapter 01).
//                     Default for tracks: their material is treated as a mono source that
//                     is positioned in the stereo field, so the centre is -3 dB (ConstantPower).
//   PanMode::Balance  centre = unity, turning right only attenuates the left side linearly:
//                     panL = min(1, 1 - pan), panR = min(1, 1 + pan).
//                     Default for buses and the master: they already carry a stereo mix,
//                     and a master at "centre" must not drop the whole mix by 3 dB.
//
// Threading
//   - Parameters (gain, pan, law, mute, solo, sends) are atomics. The UI writes them at any
//     time; the audio thread reads them once per block in beginBlock() and turns them into
//     smoother targets.
//   - The InsertChain and the LatencyCompensator are changed structurally only by the audio
//     thread (through Engine commands) or by setup code before the engine runs.
//   - Meters are written by the audio thread and read by anyone (atomics).
//
// Numbers worth remembering (checked by tests/test_mixer.cpp):
//   fader -6.02 dB           -> x0.5
//   pan centre, ConstantPower -> each side x0.7071 (-3.01 dB)
//   pan centre, Minus4_5dB    -> each side x0.5946 (-4.52 dB)
//   pan centre, Linear        -> each side x0.5    (-6.02 dB)
//   pan hard left             -> L x1, R x0
//   Balance mode, centre      -> x1 on both sides

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/LevelMeter.h>
#include <dawlab/dsp/LoudnessMeter.h>
#include <dawlab/dsp/Panner.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>

#include "Constants.h"
#include "InsertEffect.h"
#include "LatencyCompensator.h"

namespace dawlab::engine {

enum class PanMode { PanLaw, Balance };

// Exact statistics of everything a strip has output since its last reset (offline reports).
struct StripStats {
    float peakDb = kMinusInfinityDb;          // highest |sample| over both channels
    float rmsDb = kMinusInfinityDb;           // RMS over all samples of both channels
    float integratedLufs = kMinusInfinityDb;  // BS.1770 gated loudness (chapter 11)
};

class ChannelStrip {
public:
    static constexpr double kFaderRampSeconds = 0.05; // 50 ms, like dsp::Gain
    static constexpr double kMuteRampSeconds = 0.01;  // 10 ms: fast but click-free

    ChannelStrip() noexcept {
        for (auto& d : sendDb_) // sends default to "off"
            d.store(kMinusInfinityDb, std::memory_order_relaxed);
        for (auto& p : sendPre_)
            p.store(false, std::memory_order_relaxed);
    }
    ChannelStrip(const ChannelStrip&) = delete;
    ChannelStrip& operator=(const ChannelStrip&) = delete;

    // Allocates (meters, compensator). Non-audio thread.
    void prepare(double sampleRate, int maxBlockSize, int maxPdcSamples = kDefaultMaxPdcSamples) {
        (void)maxBlockSize; // everything here works per sample
        sampleRate_ = sampleRate;
        gainL_.reset(sampleRate, kFaderRampSeconds);
        gainR_.reset(sampleRate, kFaderRampSeconds);
        audible_.reset(sampleRate, kMuteRampSeconds);
        for (auto& s : sendGain_)
            s.reset(sampleRate, kFaderRampSeconds);
        compensator_.prepare(maxPdcSamples, kNumChannels);
        levelMeter_.prepare(sampleRate, kNumChannels);
        loudnessMeter_.prepare(sampleRate, kNumChannels);
        prepared_ = true;
        reset();
    }

    // Clears all DSP state; every smoother jumps to its target at the next block.
    // Audio thread (or setup code before the engine runs).
    void reset() noexcept {
        inserts_.reset();
        compensator_.reset();
        levelMeter_.reset();
        loudnessMeter_.reset();
        maxPeak_.store(0.0f, std::memory_order_relaxed);
        sumSquares_.store(0.0, std::memory_order_relaxed);
        sampleCount_.store(0, std::memory_order_relaxed);
        snapNext_ = true;
    }

    // ---- identity (UI thread only; the audio thread never reads the name) -----------------
    void setName(std::string name) { name_ = std::move(name); }
    const std::string& getName() const noexcept { return name_; }

    // ---- parameters (any thread) ------------------------------------------------------------
    void setGainDb(float db) noexcept {
        gainDb_.store(std::clamp(db, kMinusInfinityDb, kMaxFaderDb), std::memory_order_relaxed);
    }
    float getGainDb() const noexcept { return gainDb_.load(std::memory_order_relaxed); }

    void setPan(float pan) noexcept { pan_.store(std::clamp(pan, -1.0f, 1.0f), std::memory_order_relaxed); }
    float getPan() const noexcept { return pan_.load(std::memory_order_relaxed); }

    void setPanLaw(dsp::PanLaw law) noexcept { panLaw_.store(static_cast<int>(law), std::memory_order_relaxed); }
    dsp::PanLaw getPanLaw() const noexcept { return static_cast<dsp::PanLaw>(panLaw_.load(std::memory_order_relaxed)); }

    void setPanMode(PanMode mode) noexcept { panMode_.store(static_cast<int>(mode), std::memory_order_relaxed); }
    PanMode getPanMode() const noexcept { return static_cast<PanMode>(panMode_.load(std::memory_order_relaxed)); }

    void setMute(bool mute) noexcept { mute_.store(mute, std::memory_order_relaxed); }
    bool isMuted() const noexcept { return mute_.load(std::memory_order_relaxed); }

    // Solo is only meaningful on tracks (see Mixer); buses and master ignore it.
    void setSolo(bool solo) noexcept { solo_.store(solo, std::memory_order_relaxed); }
    bool isSoloed() const noexcept { return solo_.load(std::memory_order_relaxed); }

    // Send level in dB; kMinusInfinityDb (-100) or lower = send off (the default).
    void setSendLevelDb(int bus, float db) noexcept {
        if (validBus(bus))
            sendDb_[static_cast<size_t>(bus)].store(std::clamp(db, kMinusInfinityDb, kMaxFaderDb),
                                                    std::memory_order_relaxed);
    }
    float getSendLevelDb(int bus) const noexcept {
        return validBus(bus) ? sendDb_[static_cast<size_t>(bus)].load(std::memory_order_relaxed) : kMinusInfinityDb;
    }
    void setSendPreFader(int bus, bool preFader) noexcept {
        if (validBus(bus))
            sendPre_[static_cast<size_t>(bus)].store(preFader, std::memory_order_relaxed);
    }
    bool isSendPreFader(int bus) const noexcept {
        return validBus(bus) && sendPre_[static_cast<size_t>(bus)].load(std::memory_order_relaxed);
    }

    // The per-side gains the fader and pan settle at: fader * (pan law or balance).
    static void computeStripGains(float gainDb, float pan, dsp::PanLaw law, PanMode mode, float& left,
                                  float& right) noexcept {
        float pl = 1.0f, pr = 1.0f;
        if (mode == PanMode::PanLaw) {
            dsp::Panner::computeGains(pan, law, pl, pr);
        } else {
            const float p = std::clamp(pan, -1.0f, 1.0f);
            pl = std::min(1.0f, 1.0f - p);
            pr = std::min(1.0f, 1.0f + p);
        }
        const float g = gainDb > kMinusInfinityDb ? dbToGain(gainDb) : 0.0f;
        left = g * pl;
        right = g * pr;
    }

    // ---- structure ----------------------------------------------------------------------------
    InsertChain& getInserts() noexcept { return inserts_; }
    const InsertChain& getInserts() const noexcept { return inserts_; }
    LatencyCompensator& getCompensator() noexcept { return compensator_; }
    const LatencyCompensator& getCompensator() const noexcept { return compensator_; }
    int getInsertLatencySamples() const noexcept { return inserts_.getLatencySamples(); }

    // ---- meters (any thread) ------------------------------------------------------------------
    const dsp::LevelMeter& getLevelMeter() const noexcept { return levelMeter_; }
    const dsp::LoudnessMeter& getLoudnessMeter() const noexcept { return loudnessMeter_; }
    float getPeakDb(int ch) const noexcept { return levelMeter_.getPeakDb(ch); } // ballistic, for a UI meter
    float getPeakHoldDb(int ch) const noexcept { return levelMeter_.getPeakHoldDb(ch); }
    float getRmsDb(int ch) const noexcept { return levelMeter_.getRmsDb(ch); }
    float getMomentaryLufs() const noexcept { return loudnessMeter_.getMomentaryLufs(); }
    float getShortTermLufs() const noexcept { return loudnessMeter_.getShortTermLufs(); }
    float getIntegratedLufs() const noexcept { return loudnessMeter_.getIntegratedLufs(); }

    // Exact statistics since reset(): highest |sample| and RMS over both channels.
    float getMaxPeakLinear() const noexcept { return maxPeak_.load(std::memory_order_relaxed); }
    float getMaxPeakDb() const noexcept { return gainToDb(getMaxPeakLinear()); }
    float getIntegratedRmsDb() const noexcept {
        const int64_t n = sampleCount_.load(std::memory_order_relaxed);
        if (n <= 0)
            return kMinusInfinityDb;
        const double ms = sumSquares_.load(std::memory_order_relaxed) / static_cast<double>(n);
        return gainToDb(static_cast<float>(std::sqrt(ms)));
    }
    StripStats getStats() const noexcept { return {getMaxPeakDb(), getIntegratedRmsDb(), getIntegratedLufs()}; }

    // ---- audio thread ---------------------------------------------------------------------------
    // Pulls the atomics into the smoothers. Call once per block before processing.
    // `audible` is decided by the Mixer (mute + solo logic).
    void beginBlock(bool audible) noexcept {
        float l = 0.0f, r = 0.0f;
        computeStripGains(gainDb_.load(std::memory_order_relaxed), pan_.load(std::memory_order_relaxed), getPanLaw(),
                          getPanMode(), l, r);
        gainL_.setTargetValue(l);
        gainR_.setTargetValue(r);
        audible_.setTargetValue(audible ? 1.0f : 0.0f);
        for (int b = 0; b < kNumBuses; ++b) {
            const float db = sendDb_[static_cast<size_t>(b)].load(std::memory_order_relaxed);
            sendGain_[static_cast<size_t>(b)].setTargetValue(db > kMinusInfinityDb ? dbToGain(db) : 0.0f);
        }
        if (snapNext_) { // first block after reset(): start at the targets, do not ramp from 0
            snapNext_ = false;
            gainL_.setCurrentAndTargetValue(l);
            gainR_.setCurrentAndTargetValue(r);
            audible_.setCurrentAndTargetValue(audible ? 1.0f : 0.0f);
            for (auto& s : sendGain_)
                s.setCurrentAndTargetValue(s.getTargetValue());
        }
    }

    // Inserts in order, then the PDC delay (its amount is set by Mixer::updateLatencies()).
    void processInserts(AudioBlock block) noexcept {
        inserts_.process(block);
        compensator_.process(block);
    }

    // Mute / solo gain (10 ms ramp), same gain on both channels.
    void applyMuteSolo(AudioBlock block) noexcept {
        if (!audible_.isSmoothing()) {
            if (audible_.getTargetValue() < 1.0f)
                block.multiplyBy(audible_.getTargetValue());
            return;
        }
        const int n = block.getNumSamples();
        for (int i = 0; i < n; ++i) {
            const float g = audible_.getNextValue();
            for (int ch = 0; ch < block.getNumChannels(); ++ch)
                block.getChannelPointer(ch)[i] *= g;
        }
    }

    // Fader x pan, one smoothed gain per side. A mono block only gets the left gain.
    void applyFaderAndPan(AudioBlock block) noexcept {
        if (block.getNumChannels() >= 1)
            applyRamp(gainL_, block.getChannelPointer(0), block.getNumSamples());
        if (block.getNumChannels() >= 2)
            applyRamp(gainR_, block.getChannelPointer(1), block.getNumSamples());
        else
            gainR_.skip(block.getNumSamples()); // keep both ramps in step
    }

    // dest += src * sendGain[bus], ramped per sample. Cheap no-op when the send is off.
    void addSend(int bus, AudioBlock dest, const AudioBlock& src) noexcept {
        if (!validBus(bus))
            return;
        dsp::SmoothedValue& g = sendGain_[static_cast<size_t>(bus)];
        if (!g.isSmoothing()) {
            const float gain = g.getTargetValue();
            if (gain > 0.0f)
                dest.addFrom(src, gain);
            return;
        }
        const int n = std::min(dest.getNumSamples(), src.getNumSamples());
        const int nch = std::min(dest.getNumChannels(), src.getNumChannels());
        for (int i = 0; i < n; ++i) {
            const float gain = g.getNextValue();
            for (int ch = 0; ch < nch; ++ch)
                dest.getChannelPointer(ch)[i] += src.getChannelPointer(ch)[i] * gain;
        }
    }

    // Level meter, loudness meter and the exact since-reset statistics.
    void meter(const AudioBlock& block) noexcept {
        levelMeter_.process(block);
        loudnessMeter_.process(block);
        float peak = maxPeak_.load(std::memory_order_relaxed);
        double sum = 0.0;
        const int n = block.getNumSamples();
        for (int ch = 0; ch < block.getNumChannels(); ++ch) {
            const float* x = block.getChannelPointer(ch);
            for (int i = 0; i < n; ++i) {
                peak = std::max(peak, std::abs(x[i]));
                sum += static_cast<double>(x[i]) * x[i];
            }
        }
        // Only the audio thread writes these, so load + store (no RMW) is enough.
        maxPeak_.store(peak, std::memory_order_relaxed);
        sumSquares_.store(sumSquares_.load(std::memory_order_relaxed) + sum, std::memory_order_relaxed);
        sampleCount_.store(sampleCount_.load(std::memory_order_relaxed) +
                               static_cast<int64_t>(n) * block.getNumChannels(),
                           std::memory_order_relaxed);
    }

    bool isPrepared() const noexcept { return prepared_; }
    double getSampleRate() const noexcept { return sampleRate_; }

private:
    static bool validBus(int bus) noexcept { return bus >= 0 && bus < kNumBuses; }

    static void applyRamp(dsp::SmoothedValue& g, float* x, int n) noexcept {
        if (!g.isSmoothing()) {
            const float v = g.getTargetValue();
            if (v != 1.0f)
                for (int i = 0; i < n; ++i)
                    x[i] *= v;
            return;
        }
        for (int i = 0; i < n; ++i)
            x[i] *= g.getNextValue();
    }

    std::string name_;
    double sampleRate_ = 0.0;
    bool prepared_ = false;
    bool snapNext_ = true;

    std::atomic<float> gainDb_{0.0f};
    std::atomic<float> pan_{0.0f};
    std::atomic<int> panLaw_{static_cast<int>(dsp::PanLaw::ConstantPower)};
    std::atomic<int> panMode_{static_cast<int>(PanMode::PanLaw)};
    std::atomic<bool> mute_{false};
    std::atomic<bool> solo_{false};
    std::array<std::atomic<float>, kNumBuses> sendDb_;
    std::array<std::atomic<bool>, kNumBuses> sendPre_;

    dsp::SmoothedValue gainL_, gainR_, audible_;
    std::array<dsp::SmoothedValue, kNumBuses> sendGain_{};
    InsertChain inserts_;
    LatencyCompensator compensator_;

    dsp::LevelMeter levelMeter_;
    dsp::LoudnessMeter loudnessMeter_;
    std::atomic<float> maxPeak_{0.0f};
    std::atomic<double> sumSquares_{0.0};
    std::atomic<int64_t> sampleCount_{0};
};

} // namespace dawlab::engine
