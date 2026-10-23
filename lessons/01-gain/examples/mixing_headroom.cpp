// Example 3: multitrack mixing, headroom and gain staging, polarity invert, mute
//
// What it demonstrates:
//   (A) Summing correlated (coherent) signals: 8 identical sine tracks, the peak simply multiplies by 8 (+18.06 dB).
//   (B) Summing uncorrelated signals: 8 white-noise tracks with different seeds; the RMS grows only by sqrt(8) (+9.03 dB) and the peak lies in between.
//   (C) Gain staging: summing 8 tracks of "music" material at 0 dB each exceeds 0 dBFS (and is clipped when written as a 24-bit WAV);
//       working backwards from the "linear sum of the per-track peaks" gives -18 dB per track (master 0 dB), which keeps 6 dB of headroom even in the worst case.
//   (D) Polarity invert: the original signal + an inverted copy = complete cancellation (null test), peak = 0.
//   (E) Mute: use a Gain to set the gain to -inf dB (50 ms ramp); there is no click at the moment of the mute.
//
// How to run:
//   cd <build dir> && bin/Debug/l01_mixing_headroom.exe
//
// Output files (in <build dir>/dawlab_output/):
//   l01_mix_hot_clipped.wav   every track at 0 dB summed directly, written as 24-bit -> everything above 0 dBFS is clipped off (you hear distortion)
//   l01_mix_hot_float.wav     the same mix written as 32-bit float -> no clipping (players usually turn it down automatically)
//   l01_mix_staged.wav        -18 dB per track, master 0 dB, the peak stays in a safe range
//   l01_mix_mute.wav          one track is muted at 1.0 s (smoothed)
//
// What you should hear: hot_clipped has harsh distortion on the drum hits; staged is clean but somewhat quieter overall (this is the cost of headroom;
// a limiter (chapter 05) is what brings the level back up later).

#include <dawlab/Common.h>
#include <dawlab/dsp/Gain.h>

#include <cstdio>
#include <vector>

using namespace dawlab;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kNumTracks = 8;

// Apply a Gain to each of the N tracks and add them into the master block by block (simulating a DAW's mix bus)
AudioBuffer mixTracks(const std::vector<AudioBuffer>& tracks, float trackGainDb, float masterGainDb) {
    const int numSamples = tracks.front().getNumSamples();
    const int numChannels = tracks.front().getNumChannels();
    AudioBuffer master(numChannels, numSamples);
    AudioBuffer scratch(numChannels, kBlockSize);

    std::vector<dsp::Gain> gains(tracks.size());
    for (auto& g : gains) {
        g.prepare(kSampleRate, kBlockSize, numChannels);
        g.setGainDb(trackGainDb);
        g.reset(); // Start at the target value right away; do not ramp from 0 dB
    }
    dsp::Gain masterGain;
    masterGain.prepare(kSampleRate, kBlockSize, numChannels);
    masterGain.setGainDb(masterGainDb);
    masterGain.reset();

    AudioBlock masterBlock(master);
    for (int start = 0; start < numSamples; start += kBlockSize) {
        const int len = std::min(kBlockSize, numSamples - start);
        AudioBlock out = masterBlock.getSubBlock(start, len);
        AudioBlock tmp = AudioBlock(scratch).getSubBlock(0, len);
        for (size_t t = 0; t < tracks.size(); ++t) {
            // Copy to scratch -> apply the track gain -> add into the master (the track itself is not modified)
            const AudioBuffer& src = tracks[t];
            for (int ch = 0; ch < numChannels; ++ch)
                std::copy_n(src.getReadPointer(ch) + start, len, tmp.getChannelPointer(ch));
            gains[t].process(tmp);
            out.addFrom(tmp);
        }
        masterGain.process(out);
    }
    return master;
}

// Maximum first difference max|x[n] - x[n-1]|, looking only at [start, start + length): used to quantify a click
float maxFirstDifference(const AudioBuffer& b, int channel, int start, int length) {
    const float* x = b.getReadPointer(channel);
    const int first = std::max(1, start);
    const int last = std::min(b.getNumSamples(), start + length);
    float m = 0.0f;
    for (int i = first; i < last; ++i)
        m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}

} // namespace

int main() {
    std::printf("=== Chapter 01 Example 3: mixing, headroom and gain staging ===\n\n");

    // ---- (A) Correlated signals: 8 identical sine tracks ----------------------
    {
        const float levelDb = -12.0f;
        std::vector<AudioBuffer> tracks(static_cast<size_t>(kNumTracks),
                                        makeSine(220.0, kSampleRate, 0.5, dbToGain(levelDb), 1));
        AudioBuffer sum = mixTracks(tracks, 0.0f, 0.0f);
        std::printf("(A) %d identical sine tracks, each with peak %.2f dBFS:\n", kNumTracks, levelDb);
        std::printf("    Sum peak = %.2f dBFS (formula 20*log10(N) = +%.2f dB -> %.2f dBFS)%s\n", gainToDb(peakAbs(sum)),
                    20.0 * std::log10(kNumTracks), levelDb + 20.0 * std::log10(kNumTracks),
                    peakAbs(sum) > 1.0f ? " <- exceeds 0 dBFS!" : "");
    }

    // ---- (B) Uncorrelated signals: 8 white-noise tracks with different seeds ---
    {
        std::vector<AudioBuffer> tracks;
        for (int t = 0; t < kNumTracks; ++t)
            tracks.push_back(makeWhiteNoise(kSampleRate, 1.0, 0.25f, 1, 1000u + static_cast<uint32_t>(t)));
        const float oneRms = rms(tracks[0], 0);
        const float onePeak = peakAbs(tracks[0]);
        AudioBuffer sum = mixTracks(tracks, 0.0f, 0.0f);
        std::printf("(B) %d mutually uncorrelated white-noise tracks, each with RMS %.2f dBFS, peak %.2f dBFS:\n", kNumTracks,
                    gainToDb(oneRms), gainToDb(onePeak));
        std::printf("    Sum RMS = %.2f dBFS, growth %.2f dB (formula 10*log10(N) = +%.2f dB)\n", gainToDb(rms(sum, 0)),
                    gainToDb(rms(sum, 0) / oneRms), 10.0 * std::log10(kNumTracks));
        std::printf("    Sum peak = %.2f dBFS, growth %.2f dB (between +%.2f and +%.2f)\n", gainToDb(peakAbs(sum)),
                    gainToDb(peakAbs(sum) / onePeak), 10.0 * std::log10(kNumTracks), 20.0 * std::log10(kNumTracks));
    }
    std::printf("\n");

    // ---- (C) Gain staging: 8 tracks of music material ---------------------------
    std::vector<AudioBuffer> music;
    music.push_back(makeDrumLoop(kSampleRate, 2, 120.0, 0.8f, 2));
    const double roots[] = {110.0, 146.83, 164.81, 196.0, 220.0, 261.63, 329.63};
    for (int t = 0; t < kNumTracks - 1; ++t) {
        AudioBuffer p = makePluck(roots[t], kSampleRate, 4.0, 0.45f, 2, 500u + static_cast<uint32_t>(t));
        music.push_back(std::move(p));
    }
    std::printf("(C) %d tracks of music material (1 drum track + 7 plucked-string tracks), peak of each track:", kNumTracks);
    float sumOfPeaks = 0.0f;
    for (const auto& t : music) {
        std::printf(" %.1f", gainToDb(peakAbs(t)));
        sumOfPeaks += peakAbs(t);
    }
    std::printf(" dBFS\n    Linear sum of the per-track peaks = %.2f (%.2f dBFS): this is the worst case (all peaks arrive at the same time)\n", sumOfPeaks,
                gainToDb(sumOfPeaks));

    AudioBuffer hot = mixTracks(music, 0.0f, 0.0f);
    const float hotPeak = peakAbs(hot);
    int clippedSamples = 0;
    for (int ch = 0; ch < hot.getNumChannels(); ++ch)
        for (int i = 0; i < hot.getNumSamples(); ++i)
            clippedSamples += std::abs(hot.getSample(ch, i)) > 1.0f ? 1 : 0;
    std::printf("    Every track at 0 dB summed directly: peak = %.2f dBFS, %d samples exceed 0 dBFS%s\n", gainToDb(hotPeak),
                clippedSamples, hotPeak > 1.0f ? " -> clips when written as 24-bit" : "");
    writeWav(outputPath("l01_mix_hot_clipped.wav"), hot, kSampleRate, 24);
    writeWav(outputPath("l01_mix_hot_float.wav"), hot, kSampleRate, 32);

    // Gain staging: to guarantee 6 dB of headroom even in the "worst case" (all track peaks occurring together),
    // "per-track + master" together must lower the level by (sumOfPeaks dBFS + 6 dB); rounded to whole numbers -> -18 dB per track, master 0 dB.
    // Note: if the master then adds +3 dB, the overall reduction is only 15 dB and the worst case has only about 3 dB of headroom left --
    // the gain of every stage in the signal chain must be counted (dB values simply add), and that is gain staging.
    const float requiredTotalDb = -(gainToDb(sumOfPeaks) + 6.0f);
    const float trackDb = -18.0f;
    const float masterDb = 0.0f;
    std::printf("    To guarantee 6 dB of headroom even in the worst case: per-track + master must total at least %.1f dB -> %.0f dB per track, master %+.0f dB\n",
                requiredTotalDb, trackDb, masterDb);
    std::printf("    Worst-case peak = %.2f %+.0f = %.2f dBFS (headroom %.2f dB)\n", gainToDb(sumOfPeaks),
                trackDb + masterDb, gainToDb(sumOfPeaks) + trackDb + masterDb,
                -(gainToDb(sumOfPeaks) + trackDb + masterDb));
    AudioBuffer staged = mixTracks(music, trackDb, masterDb);
    const float stagedPeak = peakAbs(staged);
    std::printf("    %.0f dB per track, master %+.0f dB: peak = %.2f dBFS, headroom %.2f dB\n", trackDb, masterDb,
                gainToDb(stagedPeak), -gainToDb(stagedPeak));
    std::printf("    (Equivalent to %.0f dB overall, so the peak is exactly %.2f %+.0f = %.2f dBFS)\n", trackDb + masterDb,
                gainToDb(hotPeak), trackDb + masterDb, gainToDb(hotPeak) + trackDb + masterDb);
    writeWav(outputPath("l01_mix_staged.wav"), staged, kSampleRate, 24);

    // Verify that the 24-bit file really was clipped: read it back and compare the peaks
    WavData back;
    if (readWav(outputPath("l01_mix_hot_clipped.wav"), back))
        std::printf("    Read back l01_mix_hot_clipped.wav (24-bit integer): peak = %.2f dBFS (clipped to 0 dBFS)\n",
                    gainToDb(peakAbs(back.buffer)));
    WavData backFloat;
    if (readWav(outputPath("l01_mix_hot_float.wav"), backFloat))
        std::printf("    Read back l01_mix_hot_float.wav (32-bit float): peak = %.2f dBFS (the part above 0 dBFS is preserved intact)\n",
                    gainToDb(peakAbs(backFloat.buffer)));
    std::printf("\n");

    // ---- (D) Polarity invert: null test --------------------------------------------
    {
        const AudioBuffer& src = music[1];
        AudioBuffer inverted = src;
        dsp::Gain inv;
        inv.prepare(kSampleRate, kBlockSize, 2);
        inv.setPolarityInverted(true);
        inv.reset(); // straight to -1, no ramp
        AudioBlock ib(inverted);
        for (int start = 0; start < inverted.getNumSamples(); start += kBlockSize)
            inv.process(ib.getSubBlock(start, std::min(kBlockSize, inverted.getNumSamples() - start)));
        AudioBuffer null = src;
        AudioBlock(null).addFrom(AudioBlock(inverted));
        std::printf("(D) Polarity invert null test: original peak %.2f dBFS, inverted peak %.2f dBFS,\n", gainToDb(peakAbs(src)),
                    gainToDb(peakAbs(inverted)));
        std::printf("    peak of the sum of the two = %.1f dBFS (%s)\n", gainToDb(peakAbs(null)),
                    peakAbs(null) == 0.0f ? "complete cancellation = -inf dB" : "not fully cancelled");
    }
    std::printf("\n");

    // ---- (E) Mute: gain -> -inf dB, smoothed ------------------------------------
    {
        AudioBuffer track = music[2];
        const AudioBuffer original = track;
        dsp::Gain g;
        g.prepare(kSampleRate, kBlockSize, 2);
        const int muteBlock = secondsToSamples(1.0, kSampleRate) / kBlockSize;
        const int muteSample = muteBlock * kBlockSize;
        AudioBlock tb(track);
        for (int start = 0, b = 0; start < track.getNumSamples(); start += kBlockSize, ++b) {
            if (b == muteBlock)
                g.setGainDb(kMinusInfinityDb); // mute = -inf dB (anything at or below -100 dB is treated as 0)
            g.process(tb.getSubBlock(start, std::min(kBlockSize, track.getNumSamples() - start)));
        }
        // Control: no smoothing, simply set the samples after the mute to 0 (the wrong way to do it)
        AudioBuffer hardMute = original;
        for (int ch = 0; ch < hardMute.getNumChannels(); ++ch)
            for (int i = muteSample; i < hardMute.getNumSamples(); ++i)
                hardMute.setSample(ch, i, 0.0f);
        // Look only at the window [-50 ms, +100 ms] around the mute, so that plucked-string attacks at other times do not dominate
        const int winStart = muteSample - secondsToSamples(0.05, kSampleRate);
        const int winLen = secondsToSamples(0.15, kSampleRate);
        const int tail = track.getNumSamples() - secondsToSamples(1.2, kSampleRate);
        std::printf("(E) Mute (at sample %d = %.3f s, signal value there %+.3f), maximum first difference in the 150 ms around the mute:\n",
                    muteSample, muteSample / kSampleRate, original.getSample(0, muteSample - 1));
        std::printf("    Original signal %.4f | instantly set to 0: %.4f (click) | Gain 50 ms ramp: %.4f (not above the original = no click)\n",
                    maxFirstDifference(original, 0, winStart, winLen), maxFirstDifference(hardMute, 0, winStart, winLen),
                    maxFirstDifference(track, 0, winStart, winLen));
        std::printf("    Peak after 1.2 s = %.1f dBFS (%s)\n",
                    gainToDb(peakAbs(track.getReadPointer(0) + secondsToSamples(1.2, kSampleRate), tail)),
                    peakAbs(track.getReadPointer(0) + secondsToSamples(1.2, kSampleRate), tail) == 0.0f ? "completely silent"
                                                                                                           : "residue remains");
        writeWav(outputPath("l01_mix_mute.wav"), track, kSampleRate);
    }

    std::printf("\nOutput: dawlab_output/l01_mix_hot_clipped.wav, l01_mix_hot_float.wav, l01_mix_staged.wav, "
                "l01_mix_mute.wav\n");
    return 0;
}
