// 練習 4（起始碼）：fader taper —— 推桿位置 (0..1) ↔ dB 的分段線性映射
//
// 分段點（位置 → dB）：
//   1.00 → +6     0.75 → 0     0.50 → −12     0.25 → −30     0.05 → −60     0.00 → −∞
// 0.05 以上的每一段「dB 對位置線性」；最底段 [0, 0.05]「線性倍率對位置線性」，
// 從 −60 dB 平滑走到 0（真正的靜音）。
// 完成 TODO 之後，self-check 全部通過就算完成。起始碼執行時會印出 FAIL，這是預期的。

#include <dawlab/Common.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace dawlab;

namespace {

struct Breakpoint {
    float position;
    float db;
};

constexpr float kBottomPosition = 0.05f;
constexpr float kBottomDb = -60.0f;
constexpr Breakpoint kPoints[] = {{0.05f, -60.0f}, {0.25f, -30.0f}, {0.5f, -12.0f}, {0.75f, 0.0f}, {1.0f, 6.0f}};
constexpr int kNumPoints = 5;

} // namespace

// 推桿位置 → 線性倍率
float faderToGain(float position) {
    const float p = std::clamp(position, 0.0f, 1.0f);
    // TODO: p <= kBottomPosition 時：倍率 = dbToGain(kBottomDb) · (p / kBottomPosition)
    //       否則找出 p 落在 kPoints 的哪一段，在 dB 上做線性插值（dawlab::lerp）再 dbToGain
    return p;
}

float faderToDb(float position) { return gainToDb(faderToGain(position)); }

// 反函式：線性倍率 → 推桿位置
float gainToFader(float gain) {
    const float g = std::max(0.0f, gain);
    // TODO: faderToGain 的反函式（底段用倍率反推，其他段把 gain 換成 dB 後在 dB 上反推）
    return g;
}

float dbToFader(float db) { return gainToFader(dbToGain(db)); }

int main() {
    std::printf("%8s %10s %10s\n", "position", "dB", "gain");
    for (int i = 0; i <= 20; ++i) {
        const float p = 0.05f * static_cast<float>(i);
        std::printf("%8.2f %10.2f %10.5f\n", p, faderToDb(p), faderToGain(p));
    }

    DAWLAB_CHECK_NEAR(faderToDb(1.0f), 6.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.75f), 0.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.5f), -12.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.25f), -30.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.05f), -60.0, 1e-4);
    DAWLAB_CHECK(faderToGain(0.0f) == 0.0f);
    DAWLAB_CHECK_NEAR(faderToDb(0.875f), 3.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.625f), -6.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToGain(0.025f), dbToGain(-60.0f) * 0.5, 1e-7);

    bool monotonic = true;
    float prev = -1.0f;
    for (int i = 0; i <= 1000; ++i) {
        const float g = faderToGain(0.001f * static_cast<float>(i));
        monotonic = monotonic && (g >= prev);
        prev = g;
    }
    DAWLAB_CHECK(monotonic);

    float maxErr = 0.0f;
    for (int i = 0; i <= 1000; ++i) {
        const float p = 0.001f * static_cast<float>(i);
        maxErr = std::max(maxErr, std::abs(gainToFader(faderToGain(p)) - p));
    }
    std::printf("round-trip 最大誤差：%.2e\n", maxErr);
    DAWLAB_CHECK(maxErr < 1e-4f);
    DAWLAB_CHECK_NEAR(dbToFader(0.0f), 0.75, 1e-5);
    DAWLAB_CHECK_NEAR(dbToFader(-12.0f), 0.5, 1e-5);
    DAWLAB_CHECK_NEAR(dbToFader(6.0f), 1.0, 1e-5);
    DAWLAB_CHECK_NEAR(dbToFader(-200.0f), 0.0, 1e-6);
    DAWLAB_CHECK_NEAR(dbToFader(20.0f), 1.0, 1e-6);

    return dawlab::test::summary("l01_ex4_starter");
}
