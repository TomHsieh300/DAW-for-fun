// 範例 3：多軌 mixing、headroom 與 gain staging、polarity invert、mute
//
// 示範什麼：
//   (A) 相關（coherent）訊號相加：8 軌一模一樣的正弦波，peak 直接乘 8（+18.06 dB）。
//   (B) 不相關訊號相加：8 軌不同 seed 的白雜訊，RMS 只成長 √8（+9.03 dB），peak 介於中間。
//   (C) gain staging：8 軌「音樂」素材每軌 0 dB 直接相加會超過 0 dBFS（寫成 24-bit WAV 會被 clip）；
//       從「各軌 peak 的線性總和」反推每軌該拉到 -18 dB（master 0 dB），最壞情況也保有 6 dB headroom。
//   (D) polarity invert：原始訊號 + 反相複本 = 完全抵消（null test），peak = 0。
//   (E) mute：用 Gain 把 gain 設成 -inf dB（50 ms ramp），mute 的瞬間不會有 click。
//
// 怎麼執行：
//   cd <build 目錄> && bin/Debug/l01_mixing_headroom.exe
//
// 輸出檔案（在 <build 目錄>/dawlab_output/）：
//   l01_mix_hot_clipped.wav   每軌 0 dB 直接相加，寫成 24-bit → 超過 0 dBFS 的地方被削掉（會聽到破音）
//   l01_mix_hot_float.wav     同一個混音寫成 32-bit float → 沒有 clip（播放器通常會自動縮小）
//   l01_mix_staged.wav        每軌 -18 dB、master 0 dB，peak 落在安全範圍
//   l01_mix_mute.wav          一軌在 1.0 s 被 mute（平滑）
//
// 該聽到什麼：hot_clipped 在鼓點處有刺耳的破音，staged 乾淨但整體小聲一點（這就是 headroom 的代價，
// 之後用 limiter（第 05 章）才會把音量推回來）。

#include <dawlab/Common.h>
#include <dawlab/dsp/Gain.h>

#include <cstdio>
#include <vector>

using namespace dawlab;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kNumTracks = 8;

// 把 N 軌各自套上 Gain 之後一塊一塊加進 master（模擬 DAW 的 mix bus）
AudioBuffer mixTracks(const std::vector<AudioBuffer>& tracks, float trackGainDb, float masterGainDb) {
    const int numSamples = tracks.front().getNumSamples();
    const int numChannels = tracks.front().getNumChannels();
    AudioBuffer master(numChannels, numSamples);
    AudioBuffer scratch(numChannels, kBlockSize);

    std::vector<dsp::Gain> gains(tracks.size());
    for (auto& g : gains) {
        g.prepare(kSampleRate, kBlockSize, numChannels);
        g.setGainDb(trackGainDb);
        g.reset(); // 一開始就在目標值，不要從 0 dB ramp 過去
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
            // 複製到 scratch → 套 track gain → 加進 master（track 本身不被改動）
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

// 一階差分最大值 max|x[n] - x[n-1]|，只看 [start, start + length)：用來量化 click
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
    std::printf("=== 第 01 章 範例 3：mixing、headroom 與 gain staging ===\n\n");

    // ---- (A) 相關訊號：8 軌相同的正弦波 ----------------------------------------
    {
        const float levelDb = -12.0f;
        std::vector<AudioBuffer> tracks(static_cast<size_t>(kNumTracks),
                                        makeSine(220.0, kSampleRate, 0.5, dbToGain(levelDb), 1));
        AudioBuffer sum = mixTracks(tracks, 0.0f, 0.0f);
        std::printf("(A) %d 軌一模一樣的正弦波，每軌 peak %.2f dBFS：\n", kNumTracks, levelDb);
        std::printf("    總和 peak = %.2f dBFS（公式 20·log10(N) = +%.2f dB → %.2f dBFS）%s\n", gainToDb(peakAbs(sum)),
                    20.0 * std::log10(kNumTracks), levelDb + 20.0 * std::log10(kNumTracks),
                    peakAbs(sum) > 1.0f ? "← 超過 0 dBFS！" : "");
    }

    // ---- (B) 不相關訊號：8 軌不同 seed 的白雜訊 --------------------------------
    {
        std::vector<AudioBuffer> tracks;
        for (int t = 0; t < kNumTracks; ++t)
            tracks.push_back(makeWhiteNoise(kSampleRate, 1.0, 0.25f, 1, 1000u + static_cast<uint32_t>(t)));
        const float oneRms = rms(tracks[0], 0);
        const float onePeak = peakAbs(tracks[0]);
        AudioBuffer sum = mixTracks(tracks, 0.0f, 0.0f);
        std::printf("(B) %d 軌彼此不相關的白雜訊，每軌 RMS %.2f dBFS、peak %.2f dBFS：\n", kNumTracks,
                    gainToDb(oneRms), gainToDb(onePeak));
        std::printf("    總和 RMS = %.2f dBFS，成長 %.2f dB（公式 10·log10(N) = +%.2f dB）\n", gainToDb(rms(sum, 0)),
                    gainToDb(rms(sum, 0) / oneRms), 10.0 * std::log10(kNumTracks));
        std::printf("    總和 peak = %.2f dBFS，成長 %.2f dB（介於 +%.2f 與 +%.2f 之間）\n", gainToDb(peakAbs(sum)),
                    gainToDb(peakAbs(sum) / onePeak), 10.0 * std::log10(kNumTracks), 20.0 * std::log10(kNumTracks));
    }
    std::printf("\n");

    // ---- (C) gain staging：8 軌音樂素材 ------------------------------------------
    std::vector<AudioBuffer> music;
    music.push_back(makeDrumLoop(kSampleRate, 2, 120.0, 0.8f, 2));
    const double roots[] = {110.0, 146.83, 164.81, 196.0, 220.0, 261.63, 329.63};
    for (int t = 0; t < kNumTracks - 1; ++t) {
        AudioBuffer p = makePluck(roots[t], kSampleRate, 4.0, 0.45f, 2, 500u + static_cast<uint32_t>(t));
        music.push_back(std::move(p));
    }
    std::printf("(C) %d 軌音樂素材（1 軌鼓 + 7 軌撥弦），每軌 peak：", kNumTracks);
    float sumOfPeaks = 0.0f;
    for (const auto& t : music) {
        std::printf(" %.1f", gainToDb(peakAbs(t)));
        sumOfPeaks += peakAbs(t);
    }
    std::printf(" dBFS\n    各軌 peak 的線性總和 = %.2f（%.2f dBFS）：這是最壞情況（全部同時到達峰值）\n", sumOfPeaks,
                gainToDb(sumOfPeaks));

    AudioBuffer hot = mixTracks(music, 0.0f, 0.0f);
    const float hotPeak = peakAbs(hot);
    int clippedSamples = 0;
    for (int ch = 0; ch < hot.getNumChannels(); ++ch)
        for (int i = 0; i < hot.getNumSamples(); ++i)
            clippedSamples += std::abs(hot.getSample(ch, i)) > 1.0f ? 1 : 0;
    std::printf("    每軌 0 dB 直接相加：peak = %.2f dBFS，%d 個 sample 超過 0 dBFS%s\n", gainToDb(hotPeak),
                clippedSamples, hotPeak > 1.0f ? " → 寫成 24-bit 會 clip" : "");
    writeWav(outputPath("l01_mix_hot_clipped.wav"), hot, kSampleRate, 24);
    writeWav(outputPath("l01_mix_hot_float.wav"), hot, kSampleRate, 32);

    // gain staging：想保證「最壞情況」（各軌 peak 同時出現）也留 6 dB headroom，
    // 「每軌 + master」合計要拉低 (sumOfPeaks dBFS + 6 dB)，取整數 → 每軌 -18 dB、master 0 dB。
    // 注意：如果 master 再補 +3 dB，整體只降 15 dB，最壞情況就只剩約 3 dB headroom——
    // 訊號鏈上每一級的 gain 都要算進去（dB 直接相加），這就是 gain staging。
    const float requiredTotalDb = -(gainToDb(sumOfPeaks) + 6.0f);
    const float trackDb = -18.0f;
    const float masterDb = 0.0f;
    std::printf("    要保證最壞情況也有 6 dB headroom：每軌 + master 合計至少 %.1f dB → 每軌 %.0f dB、master %+.0f dB\n",
                requiredTotalDb, trackDb, masterDb);
    std::printf("    最壞情況 peak = %.2f %+.0f = %.2f dBFS（headroom %.2f dB）\n", gainToDb(sumOfPeaks),
                trackDb + masterDb, gainToDb(sumOfPeaks) + trackDb + masterDb,
                -(gainToDb(sumOfPeaks) + trackDb + masterDb));
    AudioBuffer staged = mixTracks(music, trackDb, masterDb);
    const float stagedPeak = peakAbs(staged);
    std::printf("    每軌 %.0f dB、master %+.0f dB：peak = %.2f dBFS，headroom %.2f dB\n", trackDb, masterDb,
                gainToDb(stagedPeak), -gainToDb(stagedPeak));
    std::printf("    （等於整體 %.0f dB，所以 peak 剛好 = %.2f %+.0f = %.2f dBFS）\n", trackDb + masterDb,
                gainToDb(hotPeak), trackDb + masterDb, gainToDb(hotPeak) + trackDb + masterDb);
    writeWav(outputPath("l01_mix_staged.wav"), staged, kSampleRate, 24);

    // 驗證 24-bit 真的被 clip 了：讀回來比較 peak
    WavData back;
    if (readWav(outputPath("l01_mix_hot_clipped.wav"), back))
        std::printf("    讀回 l01_mix_hot_clipped.wav（24-bit 整數）：peak = %.2f dBFS（已被削到 0 dBFS）\n",
                    gainToDb(peakAbs(back.buffer)));
    WavData backFloat;
    if (readWav(outputPath("l01_mix_hot_float.wav"), backFloat))
        std::printf("    讀回 l01_mix_hot_float.wav（32-bit float）：peak = %.2f dBFS（超過 0 dBFS 的部分完好保留）\n",
                    gainToDb(peakAbs(backFloat.buffer)));
    std::printf("\n");

    // ---- (D) polarity invert：null test -------------------------------------------
    {
        const AudioBuffer& src = music[1];
        AudioBuffer inverted = src;
        dsp::Gain inv;
        inv.prepare(kSampleRate, kBlockSize, 2);
        inv.setPolarityInverted(true);
        inv.reset(); // 直接 -1，不 ramp
        AudioBlock ib(inverted);
        for (int start = 0; start < inverted.getNumSamples(); start += kBlockSize)
            inv.process(ib.getSubBlock(start, std::min(kBlockSize, inverted.getNumSamples() - start)));
        AudioBuffer null = src;
        AudioBlock(null).addFrom(AudioBlock(inverted));
        std::printf("(D) polarity invert null test：原始 peak %.2f dBFS，反相後 peak %.2f dBFS，\n", gainToDb(peakAbs(src)),
                    gainToDb(peakAbs(inverted)));
        std::printf("    兩者相加 peak = %.1f dBFS（%s）\n", gainToDb(peakAbs(null)),
                    peakAbs(null) == 0.0f ? "完全抵消 = -inf dB" : "沒有完全抵消");
    }
    std::printf("\n");

    // ---- (E) mute：gain → -inf dB，平滑 ------------------------------------------
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
                g.setGainDb(kMinusInfinityDb); // mute = -inf dB（-100 dB 以下視為 0）
            g.process(tb.getSubBlock(start, std::min(kBlockSize, track.getNumSamples() - start)));
        }
        // 對照組：不平滑、直接把 mute 之後的 sample 設成 0（錯誤示範）
        AudioBuffer hardMute = original;
        for (int ch = 0; ch < hardMute.getNumChannels(); ++ch)
            for (int i = muteSample; i < hardMute.getNumSamples(); ++i)
                hardMute.setSample(ch, i, 0.0f);
        // 只看 mute 附近 [-50 ms, +100 ms] 的視窗，才不會被其他時間點的撥弦 attack 蓋過去
        const int winStart = muteSample - secondsToSamples(0.05, kSampleRate);
        const int winLen = secondsToSamples(0.15, kSampleRate);
        const int tail = track.getNumSamples() - secondsToSamples(1.2, kSampleRate);
        std::printf("(E) mute（第 %d 個 sample = %.3f s，該處訊號值 %+.3f），mute 附近 150 ms 的一階差分最大值：\n",
                    muteSample, muteSample / kSampleRate, original.getSample(0, muteSample - 1));
        std::printf("    原始訊號 %.4f｜瞬間設成 0：%.4f（click）｜Gain 50 ms ramp：%.4f（不超過原始 = 沒有 click）\n",
                    maxFirstDifference(original, 0, winStart, winLen), maxFirstDifference(hardMute, 0, winStart, winLen),
                    maxFirstDifference(track, 0, winStart, winLen));
        std::printf("    1.2 s 之後的 peak = %.1f dBFS（%s）\n",
                    gainToDb(peakAbs(track.getReadPointer(0) + secondsToSamples(1.2, kSampleRate), tail)),
                    peakAbs(track.getReadPointer(0) + secondsToSamples(1.2, kSampleRate), tail) == 0.0f ? "完全靜音"
                                                                                                           : "還有殘留");
        writeWav(outputPath("l01_mix_mute.wav"), track, kSampleRate);
    }

    std::printf("\n輸出：dawlab_output/l01_mix_hot_clipped.wav, l01_mix_hot_float.wav, l01_mix_staged.wav, "
                "l01_mix_mute.wav\n");
    return 0;
}
