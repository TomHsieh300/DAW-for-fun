// 練習 7 解答：比較 1x / 2x / 4x / 8x oversampling 的 alias 能量與 CPU 成本
//
// 做法：
//   - 品質：7 kHz、peak 0.5 的正弦波，Saturation（Tanh、drive +18 dB）。量所有 alias 位置（奇數 kHz，扣掉 7、21 kHz）
//     的功率總和，相對於基頻（與範例 l06_aliasing_demo 相同）。
//   - 成本：同一個 Saturation 設定處理 10 秒 stereo white noise（512 sample 一個 block），用 steady_clock 計時。
//     為了減少雜訊，每個倍率跑 3 次取最小值。
//   - 再算「每秒音訊的 CPU 時間比例」= 處理時間 / 10 秒，就是這個 plugin 在 real-time 時大約吃掉多少 % 的一顆核心。
//
// 預期（作者機器上 Release 比 Debug 快約 3–4 倍；相對倍率也略有不同）：
//   alias 總和：1x ≈ -18 dB、2x ≈ -38 dB、4x ≈ -85 dB、8x ≈ -111 dB
//   成本：大致隨倍率線性成長（tanh 的呼叫次數 × L，halfband 每多一級再多一點）
//   結論：對 tanh 這種平滑曲線，4x 已經把 alias 壓到 16-bit 量化底噪（約 -96 dB）附近；8x 多花將近一倍 CPU、換到的改善聽不到。
//
// 執行：l06_ex7_solution

#include <dawlab/Common.h>
#include <dawlab/dsp/Saturation.h>

#include <chrono>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kF0 = 7000.0;

using dawlab::dsp::Saturation;
using dawlab::dsp::SaturationCurve;

void configure(Saturation& sat, int log2, int numChannels) {
    sat.setOversamplingFactorLog2(log2);
    sat.prepare(kSampleRate, kBlockSize, numChannels);
    sat.setCurve(SaturationCurve::Tanh);
    sat.setDriveDb(18.0f);
    sat.setMix(1.0f);
    sat.setOutputGainDb(-6.0f);
    sat.reset();
}

void processAll(Saturation& sat, dawlab::AudioBuffer& b) {
    dawlab::AudioBlock whole(b);
    for (int start = 0; start < b.getNumSamples(); start += kBlockSize)
        sat.process(whole.getSubBlock(start, std::min(kBlockSize, b.getNumSamples() - start)));
}

double aliasDb(int log2) {
    dawlab::AudioBuffer b = dawlab::makeSine(kF0, kSampleRate, 2.0, 0.5f, 1);
    Saturation sat;
    configure(sat, log2, 1);
    processAll(sat, b);
    const int skip = 4800;
    const float* d = b.getReadPointer(0) + skip;
    const int n = b.getNumSamples() - skip;
    const double h1 = dawlab::goertzelAmplitude(d, n, kF0, kSampleRate);
    double p = 0.0;
    for (int k = 1; k <= 23; k += 2) {
        const double f = 1000.0 * k;
        if (f == kF0 || f == 3 * kF0)
            continue;
        const double a = dawlab::goertzelAmplitude(d, n, f, kSampleRate);
        p += a * a;
    }
    return 10.0 * std::log10(std::max(p / (h1 * h1), 1e-15));
}

double timeMs(int log2, const dawlab::AudioBuffer& noise) {
    double best = 1e30;
    for (int rep = 0; rep < 3; ++rep) {
        dawlab::AudioBuffer b = noise; // 複製不計時
        Saturation sat;
        configure(sat, log2, 2);
        const auto t0 = std::chrono::steady_clock::now();
        processAll(sat, b);
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    return best;
}

} // namespace

int main() {
    const double seconds = 10.0;
    const dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, seconds, 0.5f, 2);

    std::vector<double> alias, ms;
    std::printf("%-6s %-10s %16s %12s %12s %14s\n", "倍率", "latency", "alias 總和 (dB)", "耗時 (ms)", "相對 1x",
                "CPU 佔用 (%)");
    for (int log2 = 0; log2 <= 3; ++log2) {
        Saturation probe;
        configure(probe, log2, 1);
        alias.push_back(aliasDb(log2));
        ms.push_back(timeMs(log2, noise));
        std::printf("%-6d %-10d %16.1f %12.1f %12.2f %14.2f\n", 1 << log2, probe.getLatencySamples(), alias.back(),
                    ms.back(), ms.back() / ms[0], 100.0 * ms.back() / (seconds * 1000.0));
    }
#ifdef NDEBUG
    std::printf("（本次是 Release 建置；絕對時間會隨機器變動，請看相對倍率。）\n");
#else
    std::printf("（本次是 Debug 建置，絕對時間偏慢；請看相對倍率。用 -Config Release 建置可以看到真實數字。）\n");
#endif

    // 品質：每加一倍都要更乾淨（8x 已經碰到量測地板，只要求不變差）
    DAWLAB_CHECK(alias[1] < alias[0] - 10.0);
    DAWLAB_CHECK(alias[2] < alias[1] - 20.0);
    DAWLAB_CHECK(alias[3] <= alias[2] + 1.0);
    DAWLAB_CHECK(alias[2] < -70.0);
    // 成本：倍率越高越貴（計時有雜訊，只檢查「明顯」的關係）
    DAWLAB_CHECK(ms[2] > ms[0] * 1.5);
    DAWLAB_CHECK(ms[3] > ms[1]);

    return dawlab::test::summary("l06_ex7");
}
