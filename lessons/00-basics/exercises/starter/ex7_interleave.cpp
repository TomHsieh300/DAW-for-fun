// 練習 7 起始碼：interleave / deinterleave。
//
// non-interleaved（我們的 AudioBuffer、JUCE、VST3）：  LLLL... RRRR...
// interleaved（WAV 檔、大多數 audio driver API）：       LRLRLRLR...
//
// 完成下面兩個 // TODO 的函式，讓所有 self-check 通過。
// 執行：bin/Debug/l00_ex7_starter.exe（未完成時會印出 FAIL，這是預期的）

#include <dawlab/Common.h>

#include <cstdio>
#include <vector>

namespace {

// AudioBuffer（non-interleaved）→ 一維 interleaved 陣列。
// out[frame * numChannels + ch] = buffer[ch][frame]
std::vector<float> interleave(const dawlab::AudioBuffer& buffer) {
    const int numChannels = buffer.getNumChannels();
    const int numFrames = buffer.getNumSamples();
    std::vector<float> out(static_cast<size_t>(numChannels) * static_cast<size_t>(numFrames));
    // TODO: 填入 out（注意 index 要用 size_t 計算，避免 signed/unsigned warning）
    return out;
}

// interleaved 陣列 → AudioBuffer。data 長度必須是 numChannels * numFrames。
// buffer[ch][frame] = data[frame * numChannels + ch]
dawlab::AudioBuffer deinterleave(const float* data, int numChannels, int numFrames) {
    dawlab::AudioBuffer buffer(numChannels, numFrames);
    // TODO: 從 data 填入 buffer
    (void)data;
    return buffer;
}

} // namespace

int main() {
    using namespace dawlab;

    // 1. 用「看得出位置」的數字驗證排列：buffer[ch][i] = 100*ch + i
    AudioBuffer small(2, 4);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            small.setSample(ch, i, static_cast<float>(100 * ch + i));
    const std::vector<float> inter = interleave(small);
    std::printf("interleaved: [");
    for (float v : inter)
        std::printf(" %g", v);
    std::printf(" ]  （預期 0 100 1 101 2 102 3 103）\n");
    DAWLAB_CHECK(inter.size() == 8);
    const float expected[8] = {0, 100, 1, 101, 2, 102, 3, 103};
    for (size_t k = 0; k < 8; ++k)
        DAWLAB_CHECK_NEAR(inter[k], expected[k], 0.0);

    // 2. 反向：從 interleaved 還原
    AudioBuffer back = deinterleave(inter.data(), 2, 4);
    DAWLAB_CHECK(back.getNumChannels() == 2 && back.getNumSamples() == 4);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            DAWLAB_CHECK_NEAR(back.getSample(ch, i), small.getSample(ch, i), 0.0);

    // 3. 單聲道：interleave 就是原樣複製
    AudioBuffer mono = makeSine(440.0, 48000.0, 0.01, 0.5f, 1);
    const std::vector<float> monoInter = interleave(mono);
    DAWLAB_CHECK(monoInter.size() == static_cast<size_t>(mono.getNumSamples()));
    for (int i = 0; i < mono.getNumSamples(); ++i)
        DAWLAB_CHECK_NEAR(monoInter[static_cast<size_t>(i)], mono.getSample(0, i), 0.0);

    // 4. 大一點的立體聲素材來回一次要完全一致（bit-exact）
    AudioBuffer drums = makeDrumLoop(48000.0, 1);
    AudioBuffer drumsBack = deinterleave(interleave(drums).data(), drums.getNumChannels(), drums.getNumSamples());
    float maxDiff = 0.0f;
    for (int ch = 0; ch < drums.getNumChannels(); ++ch)
        for (int i = 0; i < drums.getNumSamples(); ++i)
            maxDiff = std::max(maxDiff, std::abs(drums.getSample(ch, i) - drumsBack.getSample(ch, i)));
    DAWLAB_CHECK(maxDiff == 0.0f);

    // 5. 多聲道（例如 5.1 的 6 ch）也要對
    AudioBuffer six(6, 3);
    for (int ch = 0; ch < 6; ++ch)
        for (int i = 0; i < 3; ++i)
            six.setSample(ch, i, static_cast<float>(10 * ch + i));
    const std::vector<float> sixInter = interleave(six);
    DAWLAB_CHECK_NEAR(sixInter[0], 0.0, 0.0);
    DAWLAB_CHECK_NEAR(sixInter[5], 50.0, 0.0);
    DAWLAB_CHECK_NEAR(sixInter[6], 1.0, 0.0);
    DAWLAB_CHECK_NEAR(sixInter[17], 52.0, 0.0);

    return dawlab::test::summary("l00_ex7");
}
