// 範例 1：noise gate 清掉鼓組 loop 底下的背景雜訊
//
// 示範什麼：
//   - 把 makeDrumLoop() 的鼓組 loop 加上 RMS = -50 dBFS 的白雜訊（模擬麥克風底噪 / 串音），
//     後面再接 1 秒「只有雜訊」的空白，模擬鼓手停下來之後的靜音段
//   - 用 Gate 逐 block（512 samples）處理：threshold -30 dB、hysteresis 6 dB（關門門檻 -36 dB）、
//     attack 1 ms、hold 20 ms、release 50 ms、range -80 dB
//   - 量「靜音段」處理前後的 RMS：雜訊從 -50 dBFS 掉到 range 以下
//   - 用 Gate 的 copy + nextGain() 逐 sample 走一次同樣的訊號，印出狀態機的轉換時間軸
//     （Closed → Attack → Open → Hold → Release → Closed 各在第幾毫秒發生）
//   - 比較「hysteresis = 0」時 state 轉換的次數：門檻附近的 chattering 會讓轉換次數暴增
//   - 量測 gate 偵測器在穩態正弦波上的 envelope 漣漪（峰對峰 dB），和 README 2.4.1 的估算
//     ripple ≈ 8.686 · (1 − 2/π) / (2f·τ_release) 對照：低頻的漣漪大，所以 kick 需要比較大的 hysteresis
//   - 門關著時改 range（模擬自動化）：gain 以 attack / release 斜率滑到新的 range，不會跳
// 怎麼執行：l08_gate_drum_noise [input.wav]，不給參數就用合成鼓組 + 雜訊。
// 輸出檔案：dawlab_output/l08_gate_input.wav（鼓 + 雜訊）、l08_gate_output.wav（gate 後）、
//          l08_gate_no_hysteresis.wav（hysteresis = 0 的版本，用來聽 chattering）。
// 該聽到什麼：input 的空隙與結尾有一層「沙沙」的底噪；output 的空隙變成完全安靜，鼓的打擊感沒有被削掉；
//            no_hysteresis 版在 kick 尾巴附近可能聽到門快速開關的「碎裂」聲。

#include <dawlab/Common.h>
#include <dawlab/dsp/Gate.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace dawlab;
using dawlab::dsp::DetectorMode;
using dawlab::dsp::EnvelopeFollower;
using dawlab::dsp::Gate;
using dawlab::dsp::GateState;
using dawlab::dsp::gateStateName;

namespace {

constexpr int kBlockSize = 512;

// 用 double 直接算 dB，不經過 gainToDb() 的 -100 dB 下限，才看得到 gate 真正壓到多低
double rmsDbOfRange(const AudioBuffer& b, int channel, int start, int end) {
    const float* x = b.getReadPointer(channel);
    double sum = 0.0;
    for (int i = start; i < end; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    const double r = std::sqrt(sum / std::max(1, end - start));
    return r > 1.0e-12 ? 20.0 * std::log10(r) : -240.0;
}

// 逐 block 處理（模擬 audio callback）
AudioBuffer runGate(const AudioBuffer& input, Gate& gate) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    const int n = out.getNumSamples();
    for (int start = 0; start < n; start += kBlockSize)
        gate.process(whole.getSubBlock(start, std::min(kBlockSize, n - start)));
    return out;
}

struct Transition {
    double timeSec;
    GateState from, to;
};

// 用同樣設定的 gate 逐 sample 走一遍，記錄每次 state 改變的時間（process() 內部就是這樣呼叫 nextGain）
std::vector<Transition> traceStates(const AudioBuffer& input, Gate gate /* 複製一份，不動原本的 */) {
    gate.reset();
    std::vector<Transition> log;
    GateState last = gate.getState();
    for (int i = 0; i < input.getNumSamples(); ++i) {
        float det = 0.0f;
        for (int ch = 0; ch < input.getNumChannels(); ++ch)
            det = std::max(det, std::abs(input.getSample(ch, i)));
        gate.nextGain(det);
        if (gate.getState() != last) {
            log.push_back({i / 48000.0, last, gate.getState()});
            last = gate.getState();
        }
    }
    return log;
}

// 用和 Gate 內部相同設定的 peak 偵測器追一個穩態正弦波，回傳後半段 envelope 的峰對峰值（dB）
double measureRippleDb(double freq, float releaseMs, double sampleRate) {
    EnvelopeFollower env;
    env.prepare(sampleRate);
    env.setMode(DetectorMode::Peak);
    env.setAttackMs(Gate::kDetectorAttackMs);
    env.setReleaseMs(releaseMs);
    const int n = secondsToSamples(1.0, sampleRate);
    float lo = 1.0e9f, hi = 0.0f;
    for (int i = 0; i < n; ++i) {
        const auto x = static_cast<float>(0.5 * std::sin(kTwoPi * freq * i / sampleRate));
        const float e = env.processSample(x);
        if (i >= n / 2) { // 只看後半段（已達穩態）
            lo = std::min(lo, e);
            hi = std::max(hi, e);
        }
    }
    return 20.0 * std::log10(static_cast<double>(hi) / lo);
}

} // namespace

int main(int argc, char** argv) {
    std::printf("第 08 章 範例 1：noise gate 清掉鼓組底下的雜訊\n");

    const double sampleRate = 48000.0;
    AudioBuffer input;
    bool synthetic = true;
    if (argc > 1) {
        WavData wav;
        std::string err;
        if (!readWav(argv[1], wav, &err)) {
            std::printf("讀取失敗：%s\n", err.c_str());
            return 1;
        }
        if (static_cast<int>(wav.sampleRate) != 48000) {
            std::printf("此範例假設 48 kHz，輸入檔是 %.0f Hz\n", wav.sampleRate);
            return 1;
        }
        input = wav.buffer;
        synthetic = false;
    } else {
        // 鼓組 2 小節（4 秒）+ 1 秒空白；整段疊上 RMS -50 dBFS 的白雜訊
        const AudioBuffer drums = makeDrumLoop(sampleRate);
        const int tail = secondsToSamples(1.0, sampleRate);
        input.setSize(drums.getNumChannels(), drums.getNumSamples() + tail);
        for (int ch = 0; ch < drums.getNumChannels(); ++ch)
            input.copyFrom(ch, 0, drums, ch, 0, drums.getNumSamples());
        // 均勻分布 [-A, A) 的 RMS = A / sqrt(3)，想要 RMS = -50 dBFS → A = 10^(-50/20) · sqrt(3)
        const float noiseAmp = dbToGain(-50.0f) * std::sqrt(3.0f);
        const AudioBuffer noise = makeWhiteNoise(sampleRate, input.getNumSamples() / sampleRate, noiseAmp,
                                                 input.getNumChannels(), 2024);
        for (int ch = 0; ch < input.getNumChannels(); ++ch)
            input.addFrom(ch, 0, noise, ch, 0, input.getNumSamples());
    }
    const double totalSec = input.getNumSamples() / sampleRate;

    Gate gate;
    gate.prepare(sampleRate, kBlockSize, input.getNumChannels());
    gate.setThresholdDb(-30.0f);
    gate.setHysteresisDb(6.0f);
    gate.setRangeDb(-80.0f);
    gate.setAttackMs(1.0f);
    gate.setHoldMs(20.0f);
    gate.setReleaseMs(50.0f);

    std::printf("\n設定：threshold %.0f dB、hysteresis %.0f dB（關門門檻 %.0f dB）、attack %.0f ms、hold %.0f ms、"
                "release %.0f ms、range %.0f dB\n",
                gate.getThresholdDb(), gate.getHysteresisDb(), gate.getCloseThresholdDb(), gate.getAttackMs(),
                gate.getHoldMs(), gate.getReleaseMs(), gate.getRangeDb());

    const AudioBuffer output = runGate(input, gate);
    writeWav(outputPath("l08_gate_input.wav"), input, sampleRate);
    writeWav(outputPath("l08_gate_output.wav"), output, sampleRate);

    // ---- 1. 靜音段 RMS 改善 -------------------------------------------------
    std::printf("\n[1] 靜音段的 RMS（左聲道）\n");
    struct Segment {
        const char* name;
        double start, end;
    };
    std::vector<Segment> segments;
    if (synthetic) {
        segments.push_back({"第 1 小節結尾空隙 1.90–2.00 s", 1.90, 2.00});
        segments.push_back({"結尾空白 4.50–5.00 s         ", 4.50, 5.00});
        segments.push_back({"整段（含鼓）0.00–5.00 s       ", 0.0, totalSec});
    } else {
        segments.push_back({"整段", 0.0, totalSec});
    }
    for (const auto& s : segments) {
        const int a = secondsToSamples(s.start, sampleRate);
        const int b = std::min(input.getNumSamples(), secondsToSamples(s.end, sampleRate));
        const double before = rmsDbOfRange(input, 0, a, b);
        const double after = rmsDbOfRange(output, 0, a, b);
        std::printf("    %s  處理前 %7.1f dBFS → 處理後 %7.1f dBFS（改善 %.1f dB）\n", s.name, before, after,
                    before - after);
    }
    std::printf("    peak：處理前 %.2f dBFS、處理後 %.2f dBFS（打擊的 peak 不該被削掉）\n",
                gainToDb(peakAbs(input)), gainToDb(peakAbs(output)));

    // ---- 2. 狀態機轉換時間軸 ---------------------------------------------------
    std::printf("\n[2] 狀態機轉換時間軸（前 20 次）\n");
    const auto log = traceStates(input, gate);
    std::printf("    時間(ms)   轉換\n");
    for (size_t i = 0; i < log.size() && i < 20; ++i)
        std::printf("    %8.2f   %-7s → %s\n", log[i].timeSec * 1000.0, gateStateName(log[i].from),
                    gateStateName(log[i].to));
    std::printf("    …總共 %zu 次轉換（%.1f 秒）\n", log.size(), totalSec);

    // ---- 3. 沒有 hysteresis 會怎樣 ---------------------------------------------
    Gate noHyst = gate;
    noHyst.setHysteresisDb(0.0f);
    noHyst.setHoldMs(0.0f);
    noHyst.reset();
    const auto logNoHyst = traceStates(input, noHyst);
    const AudioBuffer outNoHyst = runGate(input, noHyst);
    writeWav(outputPath("l08_gate_no_hysteresis.wav"), outNoHyst, sampleRate);
    std::printf("\n[3] hysteresis = 0、hold = 0 時：%zu 次轉換（有 hysteresis + hold：%zu 次）\n", logNoHyst.size(),
                log.size());
    std::printf("    轉換次數越多，代表門在門檻附近抖得越厲害（chattering）。\n");

    // ---- 4. 偵測器 envelope 的漣漪 ----------------------------------------------
    std::printf("\n[4] gate 偵測器（peak，attack %.1f ms）在穩態正弦波上的 envelope 漣漪（峰對峰，dB）\n",
                Gate::kDetectorAttackMs);
    std::printf("    估算（README 2.4.1）：one-pole 在 release 時是往 |x| 衰減，不是往 0；每半個週期 1/(2f) 掉\n"
                "    ΔE/A ≈ (1 − 2/π) / (2fτ) → 漣漪 ≈ 8.686 · (1 − 2/π) / (2fτ) dB。"
                "若偵測器往 0 衰減（類比 diode + RC），則是 8.686 / (2fτ) dB\n");
    std::printf("    頻率(Hz)   release 20 ms 實測 / 估算 / 往 0 衰減    release 50 ms 實測 / 估算 / 往 0 衰減\n");
    const double kOneMinus2OverPi = 1.0 - 2.0 / kPi; // |sin| 的平均值是 2/π
    for (double f : {45.0, 100.0, 185.0, 1000.0}) {
        const double halfPeriodMs = 1000.0 / (2.0 * f);
        std::printf("    %7.0f      %6.2f / %5.2f / %5.2f                  %6.2f / %5.2f / %5.2f\n", f,
                    measureRippleDb(f, 20.0f, sampleRate), 8.686 * kOneMinus2OverPi * halfPeriodMs / 20.0,
                    8.686 * halfPeriodMs / 20.0, measureRippleDb(f, 50.0f, sampleRate),
                    8.686 * kOneMinus2OverPi * halfPeriodMs / 50.0, 8.686 * halfPeriodMs / 50.0);
    }
    std::printf("    → hysteresis 要大於這個漣漪：低頻（kick）比高頻（hi-hat）需要更大的 hysteresis 或更長的偵測器 release\n");

    // ---- 5. 門關著時改 range：gain 用斜坡滑過去，不會跳 --------------------------------
    {
        Gate g = gate;
        g.reset(); // Closed，gain = -80 dB
        auto glide = [&](float newRangeDb) {
            g.setRangeDb(newRangeDb);
            float prevDb = gainToDb(g.nextGain(0.0f), -200.0f);
            float maxStep = 0.0f;
            int n = 1;
            for (; n < 48000; ++n) {
                const float db = gainToDb(g.nextGain(0.0f), -200.0f);
                maxStep = std::max(maxStep, std::abs(db - prevDb));
                prevDb = db;
                if (std::abs(db - g.getRangeDb()) < 1.0e-3f)
                    break;
            }
            std::printf("    range → %4.0f dB：%5d samples（%5.2f ms）滑到位，每 sample 最大變化 %.3f dB\n", newRangeDb, n + 1,
                        samplesToMs(n + 1, sampleRate), maxStep);
        };
        std::printf("\n[5] 門關著時把 range 從 -80 改成 -20 再改回 -80（模擬自動化）\n");
        glide(-20.0f); // 往上用 attack 斜率：60 dB / (20 dB / 1 ms) = 3 ms
        glide(-80.0f); // 往下用 release 斜率：60 dB / (80 dB / 50 ms) = 37.5 ms
    }

    std::printf("\n輸出：dawlab_output/l08_gate_input.wav、l08_gate_output.wav、l08_gate_no_hysteresis.wav\n");
    return 0;
}
