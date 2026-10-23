// 練習 4 解答：fader taper —— 推桿位置 (0..1) ↔ dB 的分段線性映射
//
// 分段點（位置 → dB）：
//   1.00 → +6     0.75 → 0     0.50 → −12     0.25 → −30     0.05 → −60     0.00 → −∞
// 0.05 以上的每一段都是「dB 對位置線性」（所以推桿上半段每格的 dB 變化很細，適合微調），
// 最底下 [0, 0.05] 則是「線性倍率對位置線性」，從 −60 dB 平滑走到 0（真正的靜音）。
// 如果底段也用 dB 線性，推桿永遠到不了 −∞；如果整支推桿都用倍率線性，上半段會擠在幾 dB 內，
// 下半段幾乎都是「聽不見」——這就是為什麼 DAW 的推桿都是對數 / 分段的。

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

// 由下往上；最底段 [0, kBottomPosition] 特殊處理
constexpr float kBottomPosition = 0.05f;
constexpr float kBottomDb = -60.0f;
constexpr Breakpoint kPoints[] = {{0.05f, -60.0f}, {0.25f, -30.0f}, {0.5f, -12.0f}, {0.75f, 0.0f}, {1.0f, 6.0f}};
constexpr int kNumPoints = 5;

} // namespace

// 推桿位置 → 線性倍率
float faderToGain(float position) {
    const float p = std::clamp(position, 0.0f, 1.0f);
    if (p <= kBottomPosition) // 底段：倍率對位置線性，p = 0 時剛好是 0
        return dbToGain(kBottomDb) * (p / kBottomPosition);
    for (int i = 1; i < kNumPoints; ++i) {
        if (p <= kPoints[i].position) {
            const float t = (p - kPoints[i - 1].position) / (kPoints[i].position - kPoints[i - 1].position);
            return dbToGain(lerp(kPoints[i - 1].db, kPoints[i].db, t)); // dB 對位置線性
        }
    }
    return dbToGain(kPoints[kNumPoints - 1].db);
}

float faderToDb(float position) { return gainToDb(faderToGain(position)); }

// 反函式：線性倍率 → 推桿位置（UI 要把目前的 gain 畫回推桿位置時用）
float gainToFader(float gain) {
    const float g = std::max(0.0f, gain);
    const float bottomGain = dbToGain(kBottomDb);
    if (g <= bottomGain)
        return kBottomPosition * (g / bottomGain);
    const float db = gainToDb(g);
    for (int i = 1; i < kNumPoints; ++i) {
        if (db <= kPoints[i].db) {
            const float t = (db - kPoints[i - 1].db) / (kPoints[i].db - kPoints[i - 1].db);
            return lerp(kPoints[i - 1].position, kPoints[i].position, t);
        }
    }
    return 1.0f;
}

float dbToFader(float db) { return gainToFader(dbToGain(db)); }

int main() {
    std::printf("%8s %10s %10s\n", "position", "dB", "gain");
    for (int i = 0; i <= 20; ++i) {
        const float p = 0.05f * static_cast<float>(i);
        std::printf("%8.2f %10.2f %10.5f\n", p, faderToDb(p), faderToGain(p));
    }

    // 分段點必須精確
    DAWLAB_CHECK_NEAR(faderToDb(1.0f), 6.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.75f), 0.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.5f), -12.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.25f), -30.0, 1e-4);
    DAWLAB_CHECK_NEAR(faderToDb(0.05f), -60.0, 1e-4);
    DAWLAB_CHECK(faderToGain(0.0f) == 0.0f);          // 推到底 = 真正的靜音
    DAWLAB_CHECK_NEAR(faderToDb(0.875f), 3.0, 1e-4);  // 段內 dB 線性：0.75..1.0 的中點 = +3 dB
    DAWLAB_CHECK_NEAR(faderToDb(0.625f), -6.0, 1e-4); // 0.5..0.75 的中點 = −6 dB
    DAWLAB_CHECK_NEAR(faderToGain(0.025f), dbToGain(-60.0f) * 0.5, 1e-7); // 底段倍率線性

    // 單調遞增（推桿往上，聲音只能變大）
    bool monotonic = true;
    float prev = -1.0f;
    for (int i = 0; i <= 1000; ++i) {
        const float g = faderToGain(0.001f * static_cast<float>(i));
        monotonic = monotonic && (g >= prev);
        prev = g;
    }
    DAWLAB_CHECK(monotonic);

    // 反函式 round-trip
    float maxErr = 0.0f;
    for (int i = 0; i <= 1000; ++i) {
        const float p = 0.001f * static_cast<float>(i);
        maxErr = std::max(maxErr, std::abs(gainToFader(faderToGain(p)) - p));
    }
    std::printf("round-trip gainToFader(faderToGain(p)) 的最大誤差：%.2e\n", maxErr);
    DAWLAB_CHECK(maxErr < 1e-4f);
    DAWLAB_CHECK_NEAR(dbToFader(0.0f), 0.75, 1e-5);
    DAWLAB_CHECK_NEAR(dbToFader(-12.0f), 0.5, 1e-5);
    DAWLAB_CHECK_NEAR(dbToFader(6.0f), 1.0, 1e-5);
    DAWLAB_CHECK_NEAR(dbToFader(-200.0f), 0.0, 1e-6);  // 低於 −100 dB 視為 −∞
    DAWLAB_CHECK_NEAR(dbToFader(20.0f), 1.0, 1e-6);    // 超過上限夾在頂端

    return dawlab::test::summary("l01_ex4");
}
