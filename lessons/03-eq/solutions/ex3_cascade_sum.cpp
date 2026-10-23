// 練習 3（解答）：驗證兩個 peak 串接時 dB 響應相加
//
// 串接（cascade）= 兩個 LTI 系統的轉移函數相乘：H(z) = H1(z)·H2(z)
//   |H| = |H1|·|H2|  →  20 log10|H| = 20 log10|H1| + 20 log10|H2|   （dB 相加）
// 乘法可交換，所以串接順序不影響「頻率響應」（但會影響數值行為與 headroom，見 README 4 節）。
// 這裡用兩種方式驗證：
//   1. 公式：H1·H2 的 magnitudeDb 等於各自 magnitudeDb 的和。
//   2. 實測：真的把正弦波送進兩顆串接的 Biquad，用 measureGainDb 量。

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

// 兩顆 Biquad 串接：y = H2(H1(x))
class Cascade2 {
public:
    void prepare(double sr, int maxBlock, int nch) {
        a_.prepare(sr, maxBlock, nch);
        b_.prepare(sr, maxBlock, nch);
    }
    void reset() noexcept {
        a_.reset();
        b_.reset();
    }
    void setCoefficients(const BiquadCoefficients& first, const BiquadCoefficients& second) noexcept {
        a_.setCoefficients(first);
        b_.setCoefficients(second);
    }
    float processSample(int ch, float x) noexcept { return b_.processSample(ch, a_.processSample(ch, x)); }

private:
    Biquad a_, b_;
};

} // namespace

int main() {
    constexpr double sr = 48000.0;
    std::printf("=== 第 03 章 練習 3：串接 = dB 相加 ===\n\n");

    const BiquadCoefficients p1 = BiquadCoefficients::makePeak(sr, 500.0, 1.0, 6.0);   // +6 dB @ 500 Hz
    const BiquadCoefficients p2 = BiquadCoefficients::makePeak(sr, 2000.0, 2.0, -4.0); // -4 dB @ 2 kHz
    Cascade2 forward, backward;
    forward.prepare(sr, 512, 1);
    backward.prepare(sr, 512, 1);
    forward.setCoefficients(p1, p2);
    backward.setCoefficients(p2, p1); // 顛倒順序

    std::printf("  freq(Hz)   H1(dB)   H2(dB)   H1+H2   |H1·H2|   實測 p1->p2   實測 p2->p1\n");
    for (double f : {100.0, 250.0, 500.0, 1000.0, 1414.0, 2000.0, 3000.0, 8000.0}) {
        const double d1 = p1.magnitudeDb(f, sr), d2 = p2.magnitudeDb(f, sr);
        const double product = gainToDb(std::abs(p1.response(f, sr) * p2.response(f, sr)));
        forward.reset();
        backward.reset();
        const double mf = measureGainDb([&](float x) { return forward.processSample(0, x); }, f, sr);
        const double mb = measureGainDb([&](float x) { return backward.processSample(0, x); }, f, sr);
        std::printf("  %7.0f   %+6.2f   %+6.2f   %+6.2f   %+6.2f     %+6.2f        %+6.2f\n", f, d1, d2, d1 + d2, product,
                    mf, mb);
        DAWLAB_CHECK_NEAR(product, d1 + d2, 1e-9); // 複數相乘 ⇔ dB 相加（數學恆等式）
        DAWLAB_CHECK_NEAR(mf, d1 + d2, 0.05);      // 實測
        DAWLAB_CHECK_NEAR(mb, mf, 0.02);           // 順序無關
    }

    // 同一個頻率疊兩顆 +6 dB：每個頻率的 dB 都剛好加倍（中心 +12、邊緣 +6），
    // 所以「中點增益」邊緣的位置不變——曲線形狀在 dB 軸上只是被拉高兩倍
    const BiquadCoefficients q1 = BiquadCoefficients::makePeak(sr, 1000.0, std::sqrt(2.0), 6.0);
    const double centre = 2.0 * q1.magnitudeDb(1000.0, sr);
    const double edge = 2.0 * q1.magnitudeDb(1000.0 * std::sqrt(2.0), sr);
    std::printf("\n  兩顆 +6 dB Q=sqrt(2) @ 1 kHz 串接：中心 %+.2f dB，1414 Hz 處 %+.2f dB（單顆是 %+.2f）\n", centre, edge,
                0.5 * edge);
    DAWLAB_CHECK_NEAR(centre, 12.0, 0.01);
    DAWLAB_CHECK_NEAR(edge, 6.0, 0.1); // 邊緣的 dB 也加倍：+3 → +6，所以「相對中心」仍是一半 → 中點邊緣不變
    // 線性振幅相加的錯誤直覺：2× 與 2× 是 4×（+12 dB），不是 2+2 = 4 的「加法」——是乘法
    DAWLAB_CHECK_NEAR(dbToGain(6.0) * dbToGain(6.0), dbToGain(12.0), 1e-6);

    // boost 與同 f、同 Q 的 cut 串接 = 完全抵銷：RBJ peak 的 H_{-g}(z) = 1 / H_{+g}(z)
    // （analog 原型的分子分母 s²+s·A/Q+1 與 s²+s/(A·Q)+1 剛好互換，bilinear 之後仍然成立）
    const BiquadCoefficients boost = BiquadCoefficients::makePeak(sr, 1000.0, 1.0, 6.0);
    const BiquadCoefficients cut = BiquadCoefficients::makePeak(sr, 1000.0, 1.0, -6.0);
    double worst = 0.0;
    for (double f = 20.0; f < 20000.0; f *= 1.1)
        worst = std::max(worst, std::abs(gainToDb(std::abs(boost.response(f, sr) * cut.response(f, sr)))));
    std::printf("  +6 dB 與 -6 dB（1 kHz, Q=1）串接：20 Hz–20 kHz 最大偏差 %.2e dB\n", worst);
    DAWLAB_CHECK(worst < 1e-9);

    return dawlab::test::summary("l03_ex3");
}
