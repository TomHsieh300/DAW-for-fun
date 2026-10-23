// l02_direct_forms：Direct Form I / Direct Form II / Transposed Direct Form II 的比較。
//
// 示範什麼：
//   三種結構算的是同一個 H(z)，在「無限精度、係數固定」下輸出完全一樣；差別在：
//   (a) 狀態變數的數量（DF-I 4 個、DF-II / TDF-II 2 個）。
//   (b) 內部節點的大小：用一個「很難算」的 filter —— fc = 30 Hz、Q = 0.707 的 lowpass（fs = 48 kHz），
//       pole 非常靠近 z = 1，b0 ≈ 3.8e-6、a1 ≈ -1.994、a2 ≈ 0.994。DF-II 的中間變數 w[n] 會比輸出大
//       幾萬倍：float 把 0.3 的輸入加進 18908 的 w 時，輸入的低位元全部被吃掉；在 fixed-point 下更會直接溢位。
//   (c) 係數改變時的行為：DF-I 的狀態是「真實的過去輸入 / 輸出」，TDF-II 的狀態和輸出同一個量級，
//       換係數時都很平順；DF-II 的狀態是放大了幾萬倍的 w，換係數瞬間輸出直接跳一大步（click）。
//   (d) float 的 round-off 誤差：三種結構都在同一個量級（約 -70 dB）。精度要靠 double，而且
//       「係數」和「狀態」都要：只把狀態換成 double、係數還是 float，誤差只到約 -90 dB（float 係數讓
//       pole 偏離設計值，算出來的根本是另一個 filter）；係數與狀態都用 double，和參考實作的差異
//       就低於 -200 dB。所以 dawlab::dsp::Biquad 用 double 係數 + double 狀態 + TDF-II。
//       Biquad 對外回傳 float，這一步的量化（約 -151 dB）才是它輸出的誤差底線。
//
// 怎麼執行：不帶參數直接執行。沒有 WAV 輸出。
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;

constexpr double kSampleRate = 48000.0;

// ---- 三種結構（教學用，單 channel，T = float 或 double）--------------------------------
// DirectForm1 另外讓「狀態型別 T」和「係數型別 C」可以分開選，用來隔離兩者對精度的影響：
// 係數先存成 C（float 時就被量化了），運算用兩者中較精確的型別（Acc），算完再存回 T。
template <typename T, typename C = T>
struct DirectForm1 {
    using Acc = decltype(T{} * C{}); // float × float → float；只要有一邊是 double → double
    C b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    T x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set(const BiquadCoefficients& c) {
        b0 = C(c.b0); b1 = C(c.b1); b2 = C(c.b2); a1 = C(c.a1); a2 = C(c.a2);
    }
    // y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]：4 個狀態（過去的輸入與輸出）
    T process(T x) {
        const Acc y = Acc(b0) * x + Acc(b1) * x1 + Acc(b2) * x2 - Acc(a1) * y1 - Acc(a2) * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = T(y);
        return T(y);
    }
};

template <typename T>
struct DirectForm2 {
    T b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    T w1 = 0, w2 = 0;
    T maxW = 0; // 觀察中間變數的大小
    void set(const BiquadCoefficients& c) {
        b0 = T(c.b0); b1 = T(c.b1); b2 = T(c.b2); a1 = T(c.a1); a2 = T(c.a2);
    }
    // w[n] = x[n] - a1 w[n-1] - a2 w[n-2]；y[n] = b0 w[n] + b1 w[n-1] + b2 w[n-2]：2 個狀態
    T process(T x) {
        const T w = x - a1 * w1 - a2 * w2;
        const T y = b0 * w + b1 * w1 + b2 * w2;
        w2 = w1;
        w1 = w;
        maxW = std::max(maxW, std::abs(w));
        return y;
    }
};

template <typename T>
struct TransposedDirectForm2 {
    T b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    T s1 = 0, s2 = 0;
    void set(const BiquadCoefficients& c) {
        b0 = T(c.b0); b1 = T(c.b1); b2 = T(c.b2); a1 = T(c.a1); a2 = T(c.a2);
    }
    // y[n] = b0 x[n] + s1；s1 = b1 x[n] - a1 y[n] + s2；s2 = b2 x[n] - a2 y[n]：2 個狀態（與輸出同量級）
    T process(T x) {
        const T y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }
};

int main() {
    std::printf("=== 第 02 章：Direct Form I / II / Transposed II 比較 ===\n\n");

    const double fc = 30.0;
    const BiquadCoefficients c = BiquadCoefficients::makeLowpass(kSampleRate, fc, BiquadCoefficients::kButterworthQ);
    std::printf("[1] LPF fc = %.0f Hz, Q = 0.707, fs = %.0f：\n", fc, kSampleRate);
    std::printf("    b0 = %.6e  b1 = %.6e  b2 = %.6e\n    a1 = %.9f  a2 = %.9f\n", c.b0, c.b1, c.b2, c.a1, c.a2);
    const auto [p1, p2] = c.poles();
    std::printf("    poles: %.6f %+.6fj, |p| = %.6f（非常靠近單位圓）\n\n", p1.real(), p1.imag(), std::abs(p1));

    // 輸入：白雜訊 + 一個 20 Hz 的正弦（在通帶內，應該幾乎原封不動通過）
    const int n = 48000 * 4;
    AudioBuffer in = makeWhiteNoise(kSampleRate, 4.0, 0.3f, 1);
    for (int i = 0; i < n; ++i)
        in.addSample(0, i, static_cast<float>(0.3 * std::sin(kTwoPi * 20.0 * i / kSampleRate)));

    // ---- [2] round-off 誤差：對「double 係數 + double 狀態」的 TDF-II（輸出也保留 double）比較 --------
    // 參考值必須是 double 輸出：dawlab::dsp::Biquad 內部雖然是 double，但 processSample() 回傳 float，
    // 拿它當參考的話，任何比 float 量化（約 -151 dB）更準的結果都量不出來。
    DirectForm1<float> df1f;                 // 係數 float、狀態 float
    DirectForm2<float> df2f;
    TransposedDirectForm2<float> tdf2f;
    DirectForm1<double, float> df1sd;        // 係數 float、狀態 double（只換狀態）
    DirectForm1<float, double> df1cd;        // 係數 double、狀態 float（只換係數）
    DirectForm1<double> df1d;                // 係數 double、狀態 double
    TransposedDirectForm2<double> ref;       // 參考：double TDF-II，double 輸出
    df1f.set(c);
    df2f.set(c);
    tdf2f.set(c);
    df1sd.set(c);
    df1cd.set(c);
    df1d.set(c);
    ref.set(c);
    Biquad bq; // dawlab::dsp::Biquad：double 係數 + 狀態，但輸出 float
    bq.prepare(kSampleRate, 512, 1);
    bq.setCoefficients(c);

    double err1 = 0, err2 = 0, errT = 0, err1sd = 0, err1cd = 0, err1d = 0, errBq = 0, errQ = 0, refPow = 0;
    double peakRef = 0;
    for (int i = 0; i < n; ++i) {
        const float x = in.getSample(0, i);
        const double y1 = df1f.process(x);
        const double y2 = df2f.process(x);
        const double yT = tdf2f.process(x);
        const double y1sd = df1sd.process(x);
        const double y1cd = df1cd.process(x);
        const double y1d = df1d.process(x);
        const double yB = bq.processSample(0, x);
        const double yR = ref.process(x);
        const double yQ = static_cast<float>(yR); // 只把參考輸出 round 成 float
        if (i >= 48000) { // 跳過 1 秒 transient
            err1 += (y1 - yR) * (y1 - yR);
            err2 += (y2 - yR) * (y2 - yR);
            errT += (yT - yR) * (yT - yR);
            err1sd += (y1sd - yR) * (y1sd - yR);
            err1cd += (y1cd - yR) * (y1cd - yR);
            err1d += (y1d - yR) * (y1d - yR);
            errBq += (yB - yR) * (yB - yR);
            errQ += (yQ - yR) * (yQ - yR);
            refPow += yR * yR;
            peakRef = std::max(peakRef, std::abs(yR));
        }
    }
    const auto errDb = [&](double e) { return 10.0 * std::log10(std::max(e, 1e-30) / refPow); };
    std::printf("[2] 輸出相對於 double TDF-II 參考（double 係數 + 狀態 + 輸出）的誤差（RMS，dB re. 輸出）：\n");
    std::printf("    結構    係數    狀態\n");
    std::printf("    DF-I    float   float  (4 states)   %8.2f dB\n", errDb(err1));
    std::printf("    DF-II   float   float  (2 states)   %8.2f dB\n", errDb(err2));
    std::printf("    TDF-II  float   float  (2 states)   %8.2f dB\n", errDb(errT));
    std::printf("    DF-I    float   double              %8.2f dB   ← 只換狀態：係數量化讓 pole 跑掉\n", errDb(err1sd));
    std::printf("    DF-I    double  float               %8.2f dB   ← 只換係數：狀態的 round-off 還在\n", errDb(err1cd));
    std::printf("    DF-I    double  double              %8.2f dB   ← 兩者都用 double\n", errDb(err1d));
    std::printf("    dawlab::dsp::Biquad（double 內部、float 輸出）%8.2f dB\n", errDb(errBq));
    std::printf("    （對照：只把參考輸出 round 成 float     %8.2f dB ← Biquad 的誤差就是這個量化底線）\n",
                errDb(errQ));
    std::printf("    參考輸出 peak = %.3f；DF-II 的中間變數 w[n] peak = %.1f（放大了 %.0f 倍）\n\n", peakRef, df2f.maxW,
                df2f.maxW / peakRef);
    // float 三種結構都在同一個量級（-60 dB 以下）
    DAWLAB_CHECK(errDb(err1) < -60.0 && errDb(err2) < -60.0 && errDb(errT) < -60.0);
    DAWLAB_CHECK(std::abs(errDb(err1) - errDb(errT)) < 10.0);
    // 只換狀態不夠：float 係數的誤差仍遠大於 -130 dB；只換係數也不夠
    DAWLAB_CHECK(errDb(err1sd) > -110.0);
    DAWLAB_CHECK(errDb(err1cd) > -110.0);
    // 係數與狀態都用 double：兩種 double 實作吻合到 -200 dB 以下
    DAWLAB_CHECK(errDb(err1d) < -200.0);
    // Biquad 的誤差 = float 輸出的量化（兩者相差 < 0.5 dB）
    DAWLAB_CHECK_NEAR(errDb(errBq), errDb(errQ), 0.5);
    DAWLAB_CHECK(errDb(errBq) < -140.0);
    DAWLAB_CHECK(df2f.maxW > 1000.0f * peakRef);

    // ---- [3] 係數改變時的 click：20 Hz 正弦進行到一半時，cutoff 從 30 Hz 跳到 60 Hz -----------------
    std::printf("[3] 係數改變（fc 30 → 60 Hz）瞬間，輸出 sample 之間的最大跳動：\n");
    const BiquadCoefficients c2 = BiquadCoefficients::makeLowpass(kSampleRate, 2.0 * fc, BiquadCoefficients::kButterworthQ);
    DirectForm1<double> s1;
    DirectForm2<double> s2;
    TransposedDirectForm2<double> sT;
    s1.set(c);
    s2.set(c);
    sT.set(c);
    const int switchAt = 48000 * 2;
    double prev1 = 0, prev2 = 0, prevT = 0, before = 0, jump1 = 0, jump2 = 0, jumpT = 0;
    for (int i = 0; i < switchAt + 2000; ++i) {
        if (i == switchAt) {
            s1.set(c2);
            s2.set(c2);
            sT.set(c2);
        }
        const double x = 0.3 * std::sin(kTwoPi * 20.0 * i / kSampleRate);
        const double y1 = s1.process(x), y2 = s2.process(x), yT = sT.process(x);
        if (i > 0) {
            const double d1 = std::abs(y1 - prev1), d2 = std::abs(y2 - prev2), dT = std::abs(yT - prevT);
            if (i < switchAt)
                before = std::max(before, d1); // 正常情況下 20 Hz 正弦 sample 之間的最大差
            else {
                jump1 = std::max(jump1, d1);
                jump2 = std::max(jump2, d2);
                jumpT = std::max(jumpT, dT);
            }
        }
        prev1 = y1;
        prev2 = y2;
        prevT = yT;
    }
    std::printf("    切換前的正常最大跳動 %.5f（20 Hz 正弦本身）\n", before);
    std::printf("    DF-I   %.5f（%.1f 倍）\n", jump1, jump1 / before);
    std::printf("    DF-II  %.5f（%.1f 倍）← 狀態 w 是放大過的量，換 b 係數就直接「跳」\n", jump2, jump2 / before);
    std::printf("    TDF-II %.5f（%.1f 倍）\n\n", jumpT, jumpT / before);
    DAWLAB_CHECK(jump2 > 20.0 * jumpT);
    DAWLAB_CHECK(jumpT < 3.0 * before);
    DAWLAB_CHECK(jump1 < 3.0 * before);

    std::printf("結論：DF-II 的內部節點會放大、換係數會 click，不適合 audio；DF-I 行為好但要 4 個狀態；\n"
                "      TDF-II 用 2 個狀態、內部量級 = 輸出量級、換係數平順，所以 dawlab::dsp::Biquad 用 TDF-II。\n"
                "      float 的 round-off 三者差不多（約 -70 dB）；係數與狀態「都」換成 double 才是真正的解法，\n"
                "      之後 Biquad 輸出的誤差只剩 float 輸出本身的量化（約 -151 dB）。\n\n");
    return dawlab::test::summary("l02_direct_forms");
}
