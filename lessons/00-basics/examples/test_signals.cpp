// l00_test_signals：產生 common/Signals.h 提供的每一種測試訊號，輸出成 WAV，
// 並印出每個訊號的長度、peak、RMS 與 crest factor。
//
// 示範什麼：
//   - 整套教材用來「餵」DSP 的訊號長什麼樣子（之後每章都會用到）。
//   - peak / RMS / crest factor 三個數字怎麼算、對不同波形有什麼差異。
//
// 怎麼執行：
//   cd <build 目錄>
//   bin/Debug/l00_test_signals.exe        （或由 ctest 執行）
//
// 會輸出（在 <build 目錄>/dawlab_output/）：
//   l00_sine_440.wav        440 Hz 正弦波，-6 dBFS（振幅 0.5）
//   l00_log_sweep.wav       20 Hz → 20 kHz 對數掃頻
//   l00_white_noise.wav     白雜訊（左右聲道不相關）
//   l00_impulse.wav         單一脈衝（一個 sample = 1.0，其餘 0）
//   l00_tone_bursts.wav     -30 / -10 / -30 dBFS 三段 1 kHz 正弦波
//   l00_drum_loop.wav       合成鼓組 loop（kick / snare / hi-hat）
//   l00_pluck_110.wav       Karplus-Strong 撥弦音 110 Hz
//   l00_pluck_sequence.wav  撥弦琶音
//
// 該聽到什麼：
//   用 Audacity 開啟這些檔案，切換到頻譜圖（spectrogram）看看：正弦波是一條水平線、
//   掃頻是一條往上彎的曲線、白雜訊是整片均勻的雜訊、脈衝是一條垂直線（所有頻率同時出現）。

#include <dawlab/Common.h>

#include <cstdio>
#include <string>

namespace {

constexpr double kSampleRate = 48000.0;

// 印出一個 buffer 的基本統計：長度、peak、RMS、crest factor，並寫成 WAV
void describeAndWrite(const char* name, const dawlab::AudioBuffer& buffer, const char* fileName) {
    using namespace dawlab;
    const float peak = peakAbs(buffer);
    const float rmsValue = rms(buffer, 0); // 只看第 0 個 channel（左聲道）
    const double seconds = buffer.getNumSamples() / kSampleRate;

    // crest factor = peak / RMS，用 dB 表示就是 peakDb - rmsDb
    const float peakDb = gainToDb(peak);
    const float rmsDb = gainToDb(rmsValue);
    const float crestDb = peakDb - rmsDb;

    std::printf("%-16s %d ch  %7d samples (%.2f s)  peak %6.2f dBFS  RMS %6.2f dBFS  crest %5.2f dB\n", name,
                buffer.getNumChannels(), buffer.getNumSamples(), seconds, peakDb, rmsDb, crestDb);

    std::string error;
    if (!writeWav(outputPath(fileName), buffer, kSampleRate, 24, &error))
        std::printf("  寫入失敗：%s\n", error.c_str());
}

} // namespace

int main() {
    using namespace dawlab;

    std::printf("sample rate = %.0f Hz，輸出目錄 = dawlab_output/\n\n", kSampleRate);

    // 1. 正弦波：最單純的訊號，只有一個頻率。振幅 0.5 = -6.02 dBFS。
    describeAndWrite("sine 440 Hz", makeSine(440.0, kSampleRate, 2.0, 0.5f), "l00_sine_440.wav");

    // 2. 對數掃頻：頻率隨時間指數上升，用來「聽」濾波器與 EQ 的頻率響應。
    describeAndWrite("log sweep", makeLogSweep(20.0, 20000.0, kSampleRate, 4.0, 0.5f), "l00_log_sweep.wav");

    // 3. 白雜訊：所有頻率能量平均。xorshift 均勻分布，振幅 0.25。
    describeAndWrite("white noise", makeWhiteNoise(kSampleRate, 2.0, 0.25f), "l00_white_noise.wav");

    // 4. 脈衝：一個 sample 是 1.0，其餘都是 0。送進任何線性系統，輸出就是它的 impulse response。
    describeAndWrite("impulse", makeImpulse(secondsToSamples(0.5, kSampleRate), 1, 100), "l00_impulse.wav");

    // 5. 音量階梯：-30 → -10 → -30 dBFS，各 0.5 秒。看 compressor 的 attack / release 最好用。
    describeAndWrite("tone bursts", makeToneBursts(1000.0, kSampleRate, {-30.0f, -10.0f, -30.0f}, 0.5),
                     "l00_tone_bursts.wav");

    // 6. 鼓組 loop：動態很大（crest factor 高），適合 compressor、limiter、saturation。
    describeAndWrite("drum loop", makeDrumLoop(kSampleRate), "l00_drum_loop.wav");

    // 7. 撥弦：諧波豐富，隨時間高頻先衰減，適合 EQ、saturation、distortion。
    describeAndWrite("pluck 110 Hz", makePluck(110.0, kSampleRate, 2.0, 0.5f), "l00_pluck_110.wav");

    // 8. 撥弦琶音：比較「像音樂」的素材。
    describeAndWrite("pluck sequence", makePluckSequence(kSampleRate), "l00_pluck_sequence.wav");

    std::printf("\n觀察：正弦波的 crest factor 是 3.01 dB（peak/RMS = √2），"
                "均勻分布白雜訊約 4.77 dB（√3），鼓組 loop 則高得多——這就是「動態大」的意思。\n");
    return 0;
}
