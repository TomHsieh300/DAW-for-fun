// Example 4: stereo link — how linking (or not) affects the stereo image.
//
// What it demonstrates:
//   - A stereo signal with a loud left channel (-6 dBFS) and a quiet right channel (-30 dBFS)
//   - Without stereo link: each channel is compressed on its own, only the left is squashed -> the level difference
//     between the channels shrinks -> the image "drifts" to the right
//   - With stereo link: both channels share one gain (determined by the larger level) -> the level difference
//     between the channels is preserved
//   - Also shows an external sidechain: a mono sidechain controlling a stereo main signal
// How to run: just run l04_stereo_link.
// Output files: dawlab_output/l04_stereo_unlinked.wav, l04_stereo_linked.wav.
// What you should hear: the unlinked version sounds shifted to the right and narrower; the linked version is
//                       simply quieter overall.

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>

using namespace dawlab;
using dawlab::dsp::Compressor;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

AudioBuffer processBlocks(const AudioBuffer& input, Compressor& comp) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    for (int start = 0; start < out.getNumSamples(); start += kBlockSize)
        comp.process(whole.getSubBlock(start, std::min(kBlockSize, out.getNumSamples() - start)));
    return out;
}

// Measure only the second half (steady state)
float steadyLevelDb(const AudioBuffer& b, int ch) {
    const int n = b.getNumSamples();
    return gainToDb(peakAbs(b.getReadPointer(ch) + n / 2, n - n / 2));
}

} // namespace

int main() {
    std::printf("Chapter 04, example 4: stereo link\n");

    // 1 kHz sine, left -6 dBFS, right -30 dBFS, 1 second
    AudioBuffer input = makeSine(1000.0, kSampleRate, 1.0, 1.0f, 2);
    for (int i = 0; i < input.getNumSamples(); ++i) {
        input.setSample(0, i, input.getSample(0, i) * dbToGain(-6.0f));
        input.setSample(1, i, input.getSample(1, i) * dbToGain(-30.0f));
    }
    std::printf("  Input: L %.2f dBFS, R %.2f dBFS, L-R difference %.2f dB\n", steadyLevelDb(input, 0),
                steadyLevelDb(input, 1), steadyLevelDb(input, 0) - steadyLevelDb(input, 1));
    std::printf("  Compressor: T -20 dB, R 4:1, hard knee, attack 5 ms, release 50 ms\n");

    auto makeComp = [](bool link) {
        Compressor c;
        c.prepare(kSampleRate, kBlockSize, 2);
        c.setThresholdDb(-20.0f);
        c.setRatio(4.0f);
        c.setKneeDb(0.0f);
        c.setAttackMs(5.0f);
        c.setReleaseMs(50.0f);
        c.setStereoLink(link);
        return c;
    };

    for (bool link : {false, true}) {
        Compressor comp = makeComp(link);
        AudioBuffer out = processBlocks(input, comp);
        const float l = steadyLevelDb(out, 0), r = steadyLevelDb(out, 1);
        std::printf("\n  stereo link = %s\n", link ? "ON" : "OFF");
        std::printf("    Output: L %.2f dBFS, R %.2f dBFS, L-R difference %.2f dB (the input is 24.00 dB)\n", l, r,
                    l - r);
        std::printf("    GR: %.2f dB (L's static GR should be (−6 − (−20))·(1 − 1/4) = 10.5 dB)\n",
                    comp.getGainReductionDb());
        std::printf("    → %s\n", link ? "Both channels are squashed by 10.5 dB, the L-R difference stays at 24 dB, "
                                         "and the image does not move."
                                       : "Only L is squashed and R is untouched, so the L-R difference shrinks from "
                                         "24 dB to 13.5 dB and the image drifts right.");
        writeWav(outputPath(link ? "l04_stereo_linked.wav" : "l04_stereo_unlinked.wav"), out, kSampleRate);
    }

    // External sidechain: a mono -10 dBFS sine acts as the sidechain that compresses the stereo main signal
    {
        Compressor comp = makeComp(true);
        AudioBuffer sidechain = makeSine(1000.0, kSampleRate, 1.0, dbToGain(-10.0f), 1);
        AudioBuffer out = input;
        AudioBlock whole(out), sc(sidechain);
        for (int start = 0; start < out.getNumSamples(); start += kBlockSize) {
            const int len = std::min(kBlockSize, out.getNumSamples() - start);
            comp.process(whole.getSubBlock(start, len), sc.getSubBlock(start, len));
        }
        std::printf("\n  External sidechain (mono -10 dBFS) controlling the stereo main signal:\n");
        std::printf("    GR = %.2f dB (set by the sidechain: (−10 − (−20))·0.75 = 7.5 dB), output L %.2f / R %.2f dBFS\n",
                    comp.getGainReductionDb(), steadyLevelDb(out, 0), steadyLevelDb(out, 1));
    }
    std::printf("\n  Wrote dawlab_output/l04_stereo_unlinked.wav, l04_stereo_linked.wav\n");
    return 0;
}
