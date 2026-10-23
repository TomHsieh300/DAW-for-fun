// l00_wav_info：讀取一個 WAV 檔，印出它的資訊，然後 normalize 到 -1 dBFS 另存新檔。
//
// 示範什麼：
//   - WavFile.h 的 readWav / writeWav / outputPath 怎麼用。
//   - 一個音檔最基本的「體檢」：channel 數、sample rate、bit depth、長度、
//     每個 channel 的 peak / RMS / crest factor、DC offset、clip 的 sample 數。
//   - normalize：找出最大 |sample|，算出要乘多少才能讓 peak 剛好等於目標 dBFS。
//
// 怎麼執行：
//   cd <build 目錄>
//   bin/Debug/l00_wav_info.exe                    不帶參數：自己產生一段撥弦琶音當輸入
//   bin/Debug/l00_wav_info.exe path/to/your.wav   分析你自己的檔案（16/24/32-bit PCM 或 float）
//
// 會輸出：
//   l00_wav_info_input.wav        （只有不帶參數時）程式自己產生的輸入
//   l00_wav_info_normalized.wav   normalize 到 -1 dBFS 的結果
//
// 該聽到什麼：
//   normalized 版本比原本大聲（或小聲），但波形形狀完全一樣——normalize 只是乘上一個常數。

#include <dawlab/Common.h>

#include <cstdio>
#include <string>

namespace {

constexpr float kTargetPeakDb = -1.0f;

// 把 peak 調到 targetPeakDb。回傳套用的 gain（dB）；全靜音時什麼都不做並回傳 0。
float normalizeToPeakDb(dawlab::AudioBuffer& buffer, float targetPeakDb) {
    const float peak = dawlab::peakAbs(buffer);
    if (peak <= 0.0f)
        return 0.0f; // 避免除以 0
    const float gain = dawlab::dbToGain(targetPeakDb) / peak;
    buffer.applyGain(gain);
    return dawlab::gainToDb(gain);
}

// |x| >= 1.0 的 sample 視為 clip（在整數 PCM 檔裡，這就是「撞到天花板」的 sample）
int countClippedSamples(const dawlab::AudioBuffer& buffer) {
    int count = 0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (std::abs(buffer.getSample(ch, i)) >= 1.0f)
                ++count;
    return count;
}

void printChannelStats(const dawlab::AudioBuffer& buffer) {
    using namespace dawlab;
    std::printf("  %-8s %10s %10s %10s %12s\n", "channel", "peak dBFS", "RMS dBFS", "crest dB", "DC offset");
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const float* x = buffer.getReadPointer(ch);
        const int n = buffer.getNumSamples();
        const float peak = peakAbs(x, n);
        const float r = rms(x, n);
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += x[i];
        const double dc = n > 0 ? sum / n : 0.0;
        std::printf("  %-8d %10.2f %10.2f %10.2f %12.6f\n", ch, gainToDb(peak), gainToDb(r),
                    gainToDb(peak) - gainToDb(r), dc);
    }
}

} // namespace

int main(int argc, char** argv) {
    using namespace dawlab;

    WavData wav;
    std::string source;
    if (argc >= 2) {
        source = argv[1];
        std::string error;
        if (!readWav(source, wav, &error)) {
            std::printf("讀取失敗：%s\n", error.c_str());
            return 1;
        }
    } else {
        // 沒有給檔案：自己做一段，寫出去再讀回來，順便示範 readWav 的流程
        source = outputPath("l00_wav_info_input.wav");
        AudioBuffer generated = makePluckSequence(48000.0, 4.0, 0.3f);
        writeWav(source, generated, 48000.0, 24);
        std::string error;
        if (!readWav(source, wav, &error)) {
            std::printf("讀取失敗：%s\n", error.c_str());
            return 1;
        }
    }

    const AudioBuffer& buffer = wav.buffer;
    const double seconds = buffer.getNumSamples() / wav.sampleRate;
    std::printf("檔案：%s\n", source.c_str());
    std::printf("  channels    = %d\n", buffer.getNumChannels());
    std::printf("  sample rate = %.0f Hz\n", wav.sampleRate);
    std::printf("  bit depth   = %d bit\n", wav.bitDepth);
    std::printf("  frames      = %d (%.3f 秒)\n", buffer.getNumSamples(), seconds);
    std::printf("  記憶體用量  = %.1f KB（float，%d ch × %d samples × 4 bytes）\n",
                4.0 * buffer.getNumChannels() * buffer.getNumSamples() / 1024.0, buffer.getNumChannels(),
                buffer.getNumSamples());
    std::printf("  clipped     = %d samples（|x| >= 1.0）\n", countClippedSamples(buffer));
    if (hasNanOrInf(buffer))
        std::printf("  警告：含有 NaN 或 Inf！\n");
    std::printf("處理前：\n");
    printChannelStats(buffer);

    // normalize 到 -1 dBFS：所有 channel 乘同一個 gain，才不會改變左右平衡
    AudioBuffer normalized = buffer;
    const float appliedDb = normalizeToPeakDb(normalized, kTargetPeakDb);
    std::printf("normalize 到 %.1f dBFS：套用 %+.2f dB（× %.4f）\n", kTargetPeakDb, appliedDb, dbToGain(appliedDb));
    std::printf("處理後：\n");
    printChannelStats(normalized);
    std::printf("  整體 peak = %.2f dBFS（目標 %.1f）\n", gainToDb(peakAbs(normalized)), kTargetPeakDb);

    const std::string outPath = outputPath("l00_wav_info_normalized.wav");
    // 用 32-bit float 寫出：就算之後有人再加 gain 也不會被截斷
    if (!writeWav(outPath, normalized, wav.sampleRate, 32)) {
        std::printf("寫入失敗：%s\n", outPath.c_str());
        return 1;
    }
    std::printf("已輸出 %s\n", outPath.c_str());
    return 0;
}
