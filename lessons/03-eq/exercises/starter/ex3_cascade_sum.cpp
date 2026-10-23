// 練習 3（起始碼）：驗證兩個 peak 串接時 dB 響應相加
//
// 目標：
//   1. 完成 Cascade2（兩顆 Biquad 串接：y = H2(H1(x))）。
//   2. 對幾個頻率驗證：|H1·H2| 的 dB = |H1| 的 dB + |H2| 的 dB，而且實測值（measureGainDb）也一樣。
//   3. 顛倒串接順序，確認頻率響應不變。
// 執行時 self-check 會印出 FAIL，直到你把 TODO 補完。

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

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
    float processSample(int ch, float x) noexcept {
        (void)ch;
        return x; // TODO: 先過 a_ 再過 b_
    }

private:
    Biquad a_, b_;
};

} // namespace

int main() {
    constexpr double sr = 48000.0;
    std::printf("=== 第 03 章 練習 3：串接 = dB 相加 ===\n\n");

    const BiquadCoefficients p1 = BiquadCoefficients::makePeak(sr, 500.0, 1.0, 6.0);
    const BiquadCoefficients p2 = BiquadCoefficients::makePeak(sr, 2000.0, 2.0, -4.0);
    Cascade2 forward, backward;
    forward.prepare(sr, 512, 1);
    backward.prepare(sr, 512, 1);
    forward.setCoefficients(p1, p2);
    backward.setCoefficients(p2, p1);

    std::printf("  freq(Hz)   H1(dB)   H2(dB)   H1+H2   |H1·H2|   實測 p1->p2   實測 p2->p1\n");
    for (double f : {100.0, 250.0, 500.0, 1000.0, 1414.0, 2000.0, 3000.0, 8000.0}) {
        const double d1 = p1.magnitudeDb(f, sr), d2 = p2.magnitudeDb(f, sr);
        // TODO: 用 p1.response(f, sr) * p2.response(f, sr) 算出 |H1·H2| 的 dB
        const double product = 0.0;
        forward.reset();
        backward.reset();
        const double mf = measureGainDb([&](float x) { return forward.processSample(0, x); }, f, sr);
        const double mb = measureGainDb([&](float x) { return backward.processSample(0, x); }, f, sr);
        std::printf("  %7.0f   %+6.2f   %+6.2f   %+6.2f   %+6.2f     %+6.2f        %+6.2f\n", f, d1, d2, d1 + d2, product,
                    mf, mb);
        DAWLAB_CHECK_NEAR(product, d1 + d2, 1e-9);
        DAWLAB_CHECK_NEAR(mf, d1 + d2, 0.05);
        DAWLAB_CHECK_NEAR(mb, mf, 0.02);
    }

    // TODO（加分）：兩顆 +6 dB Q=√2 @ 1 kHz 串接，中心應為 +12 dB，1414 Hz 處應為 +6 dB
    return dawlab::test::summary("l03_ex3_starter");
}
