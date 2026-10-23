// 範例 1：fractional delay 的插值比較（linear vs cubic Hermite vs Lagrange）
//
// 示範什麼：
//   1. circular buffer 的 2 的次方大小與 mask。
//   2. 把一個 impulse 推進 FractionalDelayLine，用 delay = 2.5 sample 讀出來，
//      直接看到三種插值的「kernel」（每個方法把一個 sample 攤成哪幾個係數）。
//   3. 在 delay = 10.5 sample（Δ = 0.5，最壞情況）量測三種插值的頻率響應，
//      和理論值比較：linear 的 |H| = |cos(πf/fs)|，在 12 kHz 已經掉 3 dB、20 kHz 掉 11.7 dB；
//      cubic 的 |H| = |9/8·cos(ω/2) − 1/8·cos(3ω/2)|，在 12 kHz 只掉 1.07 dB。
//
// 怎麼執行：不需要參數，直接執行 l09_interpolation_compare。
// 輸出檔案（dawlab_output/）：
//   l09_interp_dry.wav     原始白雜訊
//   l09_interp_linear.wav  經過 delay = 10.5 sample 的 linear 插值（高頻明顯變暗）
//   l09_interp_cubic.wav   經過 delay = 10.5 sample 的 cubic 插值（幾乎聽不出差別）
// 該聽到什麼：linear 版本像蓋了一層薄布，cubic 版本與原音幾乎相同。

#include <dawlab/Common.h>
#include <dawlab/dsp/FractionalDelayLine.h>

#include <cmath>
#include <cstdio>
#include <vector>

using dawlab::dsp::FractionalDelayLine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// 線性插值在 Δ = 0.5 時的理論增益（dB）：|H| = |cos(ω/2)| = |cos(πf/fs)|
double linearTheoryDb(double f) {
    return dawlab::gainToDb(std::abs(std::cos(dawlab::kPi * f / kSampleRate)));
}

// cubic（Catmull-Rom = 3 階 Lagrange，在 Δ = 0.5 時係數都是 -1/16, 9/16, 9/16, -1/16）：
// |H| = |9/8·cos(ω/2) − 1/8·cos(3ω/2)|
double cubicTheoryDb(double f) {
    const double w = dawlab::kTwoPi * f / kSampleRate;
    return dawlab::gainToDb(std::abs(9.0 / 8.0 * std::cos(w / 2.0) - 1.0 / 8.0 * std::cos(3.0 * w / 2.0)));
}

enum class Method { Linear, Cubic, Lagrange };

float readWith(const FractionalDelayLine& line, Method m, float delay) {
    switch (m) {
    case Method::Linear: return line.readLinear(0, delay);
    case Method::Cubic: return line.readCubic(0, delay);
    default: return line.readLagrange(0, delay);
    }
}

double measure(Method m, float delay, double freq) {
    FractionalDelayLine line;
    line.prepare(64, 1);
    return dawlab::measureGainDb(
        [&](float x) {
            line.push(0, x);
            return readWith(line, m, delay);
        },
        freq, kSampleRate);
}

// 一塊一塊（block size 512）處理整段 buffer：模擬 audio callback
void renderDelayed(const dawlab::AudioBuffer& in, dawlab::AudioBuffer& out, Method m, float delay) {
    FractionalDelayLine line;
    line.prepare(64, in.getNumChannels());
    for (int start = 0; start < in.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, in.getNumSamples() - start);
        for (int ch = 0; ch < in.getNumChannels(); ++ch) {
            const float* src = in.getReadPointer(ch) + start;
            float* dst = out.getWritePointer(ch) + start;
            for (int i = 0; i < n; ++i) {
                line.push(ch, src[i]);
                dst[i] = m == Method::Linear ? line.readLinear(ch, delay) : line.readCubic(ch, delay);
            }
        }
    }
}

} // namespace

int main() {
    std::printf("=== 第 09 章 範例 1：fractional delay 插值比較 ===\n\n");

    // ---- 1. circular buffer 大小 --------------------------------------------
    {
        FractionalDelayLine line;
        line.prepare(1000, 2);
        std::printf("prepare(maxDelay = 1000) -> buffer size = %d（2 的次方，mask = 0x%X）\n\n",
                    line.getBufferSize(), line.getBufferSize() - 1);
    }

    // ---- 2. 插值 kernel：impulse 經過 delay = 2.5 --------------------------
    {
        std::printf("impulse 經過 delay = 2.5 sample 的輸出（每一行是一個 sample 時刻 n）：\n");
        std::printf("   n   linear    cubic   lagrange\n");
        FractionalDelayLine lin, cub, lag;
        lin.prepare(16, 1);
        cub.prepare(16, 1);
        lag.prepare(16, 1);
        for (int n = 0; n < 7; ++n) {
            const float x = n == 0 ? 1.0f : 0.0f;
            lin.push(0, x);
            cub.push(0, x);
            lag.push(0, x);
            std::printf("  %2d  %+7.4f  %+7.4f  %+7.4f\n", n, lin.readLinear(0, 2.5f), cub.readCubic(0, 2.5f),
                        lag.readLagrange(0, 2.5f));
        }
        std::printf("  -> linear 只用兩個係數 (0.5, 0.5)；cubic 與 Lagrange 在 Δ = 0.5 時完全相同：\n");
        std::printf("     (-1/16, 9/16, 9/16, -1/16) = (-0.0625, 0.5625, 0.5625, -0.0625)\n\n");
    }

    // ---- 3. 頻率響應量測（delay = 10.5）------------------------------------
    {
        const float delay = 10.5f;
        const double freqs[] = {100, 500, 1000, 2000, 4000, 6000, 8000, 10000, 12000, 15000, 18000, 20000};
        std::printf("delay = %.1f sample 的頻率響應（dB），fs = %.0f Hz：\n", delay, kSampleRate);
        std::printf("   freq(Hz)  linear   理論    cubic    理論   lagrange\n");
        for (double f : freqs) {
            const double gl = measure(Method::Linear, delay, f);
            const double gc = measure(Method::Cubic, delay, f);
            const double gg = measure(Method::Lagrange, delay, f);
            std::printf("  %8.0f  %+6.2f  %+6.2f  %+6.2f  %+6.2f  %+6.2f\n", f, gl, linearTheoryDb(f), gc,
                        cubicTheoryDb(f), gg);
        }
        std::printf("  -> linear 在 fs/4 = 12 kHz 掉 3 dB、20 kHz 掉 11.7 dB；cubic 在 12 kHz 只掉 1.07 dB。\n");
        std::printf("     兩者都是「低通」：這就是為什麼 chorus / flanger（第 10 章）要用 cubic 插值。\n\n");
    }

    // ---- 4. 輸出 WAV 給耳朵聽 ----------------------------------------------
    {
        dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 3.0, 0.25f);
        dawlab::AudioBuffer linear(noise.getNumChannels(), noise.getNumSamples());
        dawlab::AudioBuffer cubic(noise.getNumChannels(), noise.getNumSamples());
        renderDelayed(noise, linear, Method::Linear, 10.5f);
        renderDelayed(noise, cubic, Method::Cubic, 10.5f);
        dawlab::writeWav(dawlab::outputPath("l09_interp_dry.wav"), noise, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l09_interp_linear.wav"), linear, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l09_interp_cubic.wav"), cubic, kSampleRate);
        std::printf("白雜訊 RMS：dry %.4f, linear %.4f, cubic %.4f（linear 少掉的就是高頻能量）\n",
                    dawlab::rms(noise, 0), dawlab::rms(linear, 0), dawlab::rms(cubic, 0));
        std::printf("已輸出 dawlab_output/l09_interp_dry.wav, l09_interp_linear.wav, l09_interp_cubic.wav\n");
    }
    return 0;
}
