// 練習 5 解答：stereo → mono downmix 與 mono → stereo upmix（注意增益）。
//
// 執行：bin/Debug/l00_ex5_solution.exe（ctest label: solution）
// 輸出：dawlab_output/l00_ex5_mono.wav、l00_ex5_stereo_again.wav

#include <dawlab/Common.h>

#include <cmath>
#include <cstdio>

namespace {

// 把多聲道 buffer 混成單聲道：mono[i] = channelGain * Σ_ch in[ch][i]
//
// channelGain 的選擇（以立體聲為例）：
//   0.5   （-6 dB）：左右相同時音量不變，左右不相關時 -3 dB。永遠不會 clip。
//   0.707 （-3 dB）：左右不相關時音量不變，左右相同時 +3 dB，可能 clip。
dawlab::AudioBuffer downmixToMono(const dawlab::AudioBuffer& in, float channelGain) {
    const int n = in.getNumSamples();
    dawlab::AudioBuffer mono(1, n);
    float* out = mono.getWritePointer(0);
    for (int ch = 0; ch < in.getNumChannels(); ++ch) {
        const float* x = in.getReadPointer(ch);
        for (int i = 0; i < n; ++i)
            out[i] += channelGain * x[i];
    }
    return mono;
}

// 單聲道複製到 numChannels 個 channel。「同一個訊號放到左右」在聽感上會在正中間。
dawlab::AudioBuffer upmixToStereo(const dawlab::AudioBuffer& mono, int numChannels = 2) {
    const int n = mono.getNumSamples();
    dawlab::AudioBuffer out(numChannels, n);
    for (int ch = 0; ch < numChannels; ++ch)
        out.copyFrom(ch, 0, mono, 0, 0, n);
    return out;
}

float maxAbsDifference(const dawlab::AudioBuffer& a, const dawlab::AudioBuffer& b) {
    float d = 0.0f;
    for (int ch = 0; ch < std::min(a.getNumChannels(), b.getNumChannels()); ++ch)
        for (int i = 0; i < std::min(a.getNumSamples(), b.getNumSamples()); ++i)
            d = std::max(d, std::abs(a.getSample(ch, i) - b.getSample(ch, i)));
    return d;
}

} // namespace

int main() {
    using namespace dawlab;
    constexpr double sr = 48000.0;
    const float minus6 = 0.5f;
    const float minus3 = static_cast<float>(1.0 / std::sqrt(2.0));

    // 1. 左右相同（正弦波）：gain 0.5 → mono 跟原本一樣大；gain 0.707 → +3.01 dB
    AudioBuffer sine = makeSine(440.0, sr, 1.0, 0.5f, 2);
    AudioBuffer monoA = downmixToMono(sine, minus6);
    AudioBuffer monoB = downmixToMono(sine, minus3);
    const float sameA = gainToDb(rms(monoA, 0)) - gainToDb(rms(sine, 0));
    const float sameB = gainToDb(rms(monoB, 0)) - gainToDb(rms(sine, 0));
    std::printf("左右相同：  gain 0.5 → %+.2f dB，gain 0.707 → %+.2f dB\n", sameA, sameB);
    DAWLAB_CHECK_NEAR(sameA, 0.0, 0.01);
    DAWLAB_CHECK_NEAR(sameB, 3.01, 0.02);
    DAWLAB_CHECK_NEAR(maxAbsDifference(monoA, sine), 0.0, 1e-6); // 逐 sample 完全等於左聲道

    // 2. 左右不相關（白雜訊，兩聲道不同 seed）：gain 0.5 → -3.01 dB；gain 0.707 → 0 dB
    AudioBuffer noise = makeWhiteNoise(sr, 2.0, 0.25f, 2);
    AudioBuffer noiseA = downmixToMono(noise, minus6);
    AudioBuffer noiseB = downmixToMono(noise, minus3);
    const float uncA = gainToDb(rms(noiseA, 0)) - gainToDb(rms(noise, 0));
    const float uncB = gainToDb(rms(noiseB, 0)) - gainToDb(rms(noise, 0));
    std::printf("左右不相關：gain 0.5 → %+.2f dB，gain 0.707 → %+.2f dB\n", uncA, uncB);
    DAWLAB_CHECK_NEAR(uncA, -3.01, 0.1);
    DAWLAB_CHECK_NEAR(uncB, 0.0, 0.1);

    // 3. 左右反相（L = -R）：downmix 之後完全消失。這就是「mono 相容性」問題。
    AudioBuffer antiPhase = makeSine(440.0, sr, 1.0, 0.5f, 2);
    AudioBlock(antiPhase).getChannelSubset(1, 1).multiplyBy(-1.0f);
    AudioBuffer cancelled = downmixToMono(antiPhase, minus6);
    std::printf("左右反相：  downmix 後 peak = %g（完全抵消）\n", peakAbs(cancelled));
    DAWLAB_CHECK(peakAbs(cancelled) == 0.0f);

    // 4. gain 0.5 永遠不會 clip：就算左右都是 full-scale 的 1.0
    AudioBuffer fullScale = makeSine(440.0, sr, 0.1, 1.0f, 2);
    DAWLAB_CHECK(peakAbs(downmixToMono(fullScale, minus6)) <= 1.0f);
    DAWLAB_CHECK(peakAbs(downmixToMono(fullScale, minus3)) > 1.0f); // 0.707 會超過

    // 5. upmix：兩個 channel 都等於 mono；stereo → mono → stereo 對「左右相同」的素材是無損的
    AudioBuffer pluck = makePluckSequence(sr, 2.0, 0.4f, 2);
    AudioBuffer pluckMono = downmixToMono(pluck, minus6);
    AudioBuffer pluckStereo = upmixToStereo(pluckMono);
    DAWLAB_CHECK(pluckStereo.getNumChannels() == 2);
    DAWLAB_CHECK(pluckStereo.getNumSamples() == pluck.getNumSamples());
    DAWLAB_CHECK_NEAR(maxAbsDifference(pluckStereo, pluck), 0.0, 1e-6);
    float lrDiff = 0.0f;
    for (int i = 0; i < pluckStereo.getNumSamples(); ++i)
        lrDiff = std::max(lrDiff, std::abs(pluckStereo.getSample(0, i) - pluckStereo.getSample(1, i)));
    DAWLAB_CHECK(lrDiff == 0.0f);
    std::printf("stereo → mono → stereo：最大差異 %g\n", maxAbsDifference(pluckStereo, pluck));

    writeWav(outputPath("l00_ex5_mono.wav"), pluckMono, sr);
    writeWav(outputPath("l00_ex5_stereo_again.wav"), pluckStereo, sr);
    return dawlab::test::summary("l00_ex5");
}
