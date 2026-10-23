#pragma once
// Test signal generators. The course examples all use these signals, so no audio files are needed;
// every generator is deterministic (same parameters -> same result), which makes automated checks easy to write.

#include "Console.h"
#include "AudioBuffer.h"
#include "DspMath.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace dawlab {

inline int secondsToSamples(double seconds, double sampleRate) {
    return static_cast<int>(std::lround(seconds * sampleRate));
}

// A simple xorshift32 random number generator: outputs [-1, 1)
class NoiseGenerator {
public:
    explicit NoiseGenerator(uint32_t seed = 1) noexcept : state_(seed ? seed : 1) {}

    float next() noexcept {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float>(state_ / 4294967296.0 * 2.0 - 1.0);
    }

private:
    uint32_t state_;
};

// Sine wave: x[n] = A * sin(2π f n / fs)
inline AudioBuffer makeSine(double frequency, double sampleRate, double seconds, float amplitude = 0.5f,
                            int numChannels = 2) {
    AudioBuffer b(numChannels, secondsToSamples(seconds, sampleRate));
    for (int i = 0; i < b.getNumSamples(); ++i) {
        const auto v = static_cast<float>(amplitude * std::sin(kTwoPi * frequency * i / sampleRate));
        for (int ch = 0; ch < numChannels; ++ch)
            b.setSample(ch, i, v);
    }
    return b;
}

// Logarithmic sweep (log sweep / chirp): the frequency rises exponentially from f0 to f1, good for hearing the effect of a filter or EQ
inline AudioBuffer makeLogSweep(double f0, double f1, double sampleRate, double seconds, float amplitude = 0.5f,
                                int numChannels = 2) {
    AudioBuffer b(numChannels, secondsToSamples(seconds, sampleRate));
    const double k = std::log(f1 / f0);
    for (int i = 0; i < b.getNumSamples(); ++i) {
        const double t = i / sampleRate;
        const double phase = kTwoPi * f0 * seconds / k * (std::exp(t * k / seconds) - 1.0);
        const auto v = static_cast<float>(amplitude * std::sin(phase));
        for (int ch = 0; ch < numChannels; ++ch)
            b.setSample(ch, i, v);
    }
    return b;
}

// White noise; each channel uses a different seed, so the left and right channels are uncorrelated
inline AudioBuffer makeWhiteNoise(double sampleRate, double seconds, float amplitude = 0.25f, int numChannels = 2,
                                  uint32_t seed = 1) {
    AudioBuffer b(numChannels, secondsToSamples(seconds, sampleRate));
    for (int ch = 0; ch < numChannels; ++ch) {
        NoiseGenerator noise(seed + 7919u * static_cast<uint32_t>(ch));
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample(ch, i, amplitude * noise.next());
    }
    return b;
}

// A single impulse, used to measure an impulse response
inline AudioBuffer makeImpulse(int numSamples, int numChannels = 1, int position = 0, float amplitude = 1.0f) {
    AudioBuffer b(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        b.setSample(ch, position, amplitude);
    return b;
}

// A series of sine-wave segments at different levels, e.g. {-30, -10, -30} dBFS for 0.5 s each.
// Used to observe the attack and release behaviour (step response) of a compressor / limiter.
inline AudioBuffer makeToneBursts(double frequency, double sampleRate, const std::vector<float>& levelsDb,
                                  double segmentSeconds, int numChannels = 2) {
    const int segLen = secondsToSamples(segmentSeconds, sampleRate);
    AudioBuffer b(numChannels, segLen * static_cast<int>(levelsDb.size()));
    for (int i = 0; i < b.getNumSamples(); ++i) {
        const float amp = dbToGain(levelsDb[static_cast<size_t>(i / segLen)]);
        const auto v = static_cast<float>(amp * std::sin(kTwoPi * frequency * i / sampleRate));
        for (int ch = 0; ch < numChannels; ++ch)
            b.setSample(ch, i, v);
    }
    return b;
}

// A synthesized drum loop (kick / snare / hi-hat) with a wide dynamic range, well suited to compressor, limiter and saturation demos
inline AudioBuffer makeDrumLoop(double sampleRate, int bars = 2, double bpm = 120.0, float amplitude = 0.8f,
                                int numChannels = 2) {
    const double beat = 60.0 / bpm;
    const int total = secondsToSamples(bars * 4 * beat, sampleRate);
    AudioBuffer b(numChannels, total);
    NoiseGenerator noise(12345);

    auto addHit = [&](double startSec, auto&& voice, double lengthSec) {
        const int start = secondsToSamples(startSec, sampleRate);
        const int len = secondsToSamples(lengthSec, sampleRate);
        for (int i = 0; i < len && start + i < total; ++i) {
            const float v = voice(i / sampleRate);
            for (int ch = 0; ch < numChannels; ++ch)
                b.addSample(ch, start + i, v);
        }
    };

    for (int step = 0; step < bars * 8; ++step) { // one step = one eighth note
        const double t = step * beat * 0.5;
        const int inBar = step % 8;
        if (inBar == 0 || inBar == 3 || inBar == 4) { // kick
            double phase = 0.0;
            addHit(t, [&](double s) {
                const double f = 45.0 + 75.0 * std::exp(-s * 30.0); // pitch drops from 120 Hz to 45 Hz
                phase += kTwoPi * f / sampleRate;
                return static_cast<float>(0.9 * std::sin(phase) * std::exp(-s * 8.0));
            }, 0.45);
        }
        if (inBar == 2 || inBar == 6) { // snare
            addHit(t, [&](double s) {
                const double body = std::sin(kTwoPi * 185.0 * s) * std::exp(-s * 25.0);
                const double rattle = noise.next() * std::exp(-s * 18.0);
                return static_cast<float>(0.35 * body + 0.45 * rattle);
            }, 0.25);
        }
        float prev = 0.0f; // hi-hat: a first-order difference tilts the noise toward high frequencies
        addHit(t, [&](double s) {
            const float n = noise.next();
            const float hp = n - prev;
            prev = n;
            return static_cast<float>(0.12 * hp * std::exp(-s * 60.0));
        }, 0.06);
    }
    b.applyGain(amplitude / 1.2f);
    return b;
}

// Karplus-Strong plucked string (guitar / bass-like), rich in harmonics, well suited to EQ, saturation and distortion demos
inline AudioBuffer makePluck(double frequency, double sampleRate, double seconds, float amplitude = 0.5f,
                             int numChannels = 2, uint32_t seed = 99) {
    AudioBuffer b(numChannels, secondsToSamples(seconds, sampleRate));
    const int period = std::max(2, static_cast<int>(std::lround(sampleRate / frequency)));
    std::vector<float> ring(static_cast<size_t>(period));
    NoiseGenerator noise(seed);
    for (float& s : ring)
        s = noise.next();
    size_t idx = 0;
    for (int i = 0; i < b.getNumSamples(); ++i) {
        const size_t next = (idx + 1) % ring.size();
        const float out = ring[idx];
        ring[idx] = 0.996f * 0.5f * (ring[idx] + ring[next]); // averaging = low-pass, so high frequencies decay first
        idx = next;
        for (int ch = 0; ch < numChannels; ++ch)
            b.setSample(ch, i, amplitude * out);
    }
    return b;
}

// A plucked arpeggio (A minor), used as music-like test material
inline AudioBuffer makePluckSequence(double sampleRate, double seconds = 4.0, float amplitude = 0.4f,
                                     int numChannels = 2) {
    const double notes[] = {110.0, 164.81, 220.0, 261.63, 329.63, 261.63, 220.0, 164.81};
    const double noteLen = 0.25;
    AudioBuffer b(numChannels, secondsToSamples(seconds, sampleRate));
    int n = 0;
    for (double t = 0.0; t < seconds; t += noteLen, ++n) {
        const double f = notes[n % 8];
        AudioBuffer note = makePluck(f, sampleRate, std::min(1.5, seconds - t), amplitude, numChannels,
                                     99u + static_cast<uint32_t>(n));
        const int start = secondsToSamples(t, sampleRate);
        const int len = std::min(note.getNumSamples(), b.getNumSamples() - start);
        for (int ch = 0; ch < numChannels && len > 0; ++ch)
            b.addFrom(ch, start, note, ch, 0, len);
    }
    return b;
}

} // namespace dawlab
