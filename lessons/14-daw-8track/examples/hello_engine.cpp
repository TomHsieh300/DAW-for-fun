// l14_hello_engine -- Step 1: the smallest possible use of the engine.
//
// What it shows
//   - Engine::prepare() is the only place that allocates.
//   - Audio is produced by calling Engine::processBlock(AudioBlock) again and again, exactly
//     like an audio device callback would (here: 512-sample blocks into a big buffer).
//   - One clip (a 2-second 440 Hz sine) on track 1, everything else at default settings.
//
// Run
//   build/bin/Debug/l14_hello_engine.exe        (from the build directory)
//
// Output
//   dawlab_output/l14_hello_engine.wav   2.5 s stereo: the sine for 2 s, then silence.
//
// What you should hear
//   A plain A4 tone, centred, a little quieter than the source: a track's pan at the centre
//   with the constant-power law multiplies each side by 0.7071 (-3.01 dB).
//
// Expected numbers
//   source peak 0.5 (-6.02 dBFS) -> output peak 0.5 * 0.7071 = 0.3536 (-9.03 dBFS)
//   2.5 s * 48000 / 512 = 234.375 -> 235 calls of processBlock()

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include <cstdio>
#include <memory>

using namespace dawlab;
using namespace dawlab::engine;

int main() {
    constexpr double kSampleRate = 48000.0;
    constexpr int kBlockSize = 512;

    // 1) Build and prepare the engine (allocation happens here, never in processBlock).
    Engine engine;
    engine.prepare(kSampleRate, kBlockSize);

    // 2) Put one clip on track 1 (index 0). The clip shares the buffer through a shared_ptr.
    auto sine = std::make_shared<const AudioBuffer>(makeSine(440.0, kSampleRate, 2.0, 0.5f, 1));
    engine.setTrackClips(0, {Clip::fromBuffer(sine, 0, "A4 sine")});

    // 3) Press play and run the "audio callback" block by block.
    engine.getTransport().play();
    const int total = secondsToSamples(2.5, kSampleRate);
    AudioBuffer out(kNumChannels, total);
    int calls = 0;
    for (int pos = 0; pos < total; pos += kBlockSize) {
        const int n = std::min(kBlockSize, total - pos);
        engine.processBlock(out.toBlock().getSubBlock(pos, n)); // <- what the device callback does
        ++calls;
    }
    engine.collectGarbage(); // the old (empty) clip list comes back to be freed here

    writeWav(outputPath("l14_hello_engine.wav"), out, kSampleRate);

    const float peak = peakAbs(out);
    std::printf("Step 1: hello engine\n");
    std::printf("  processBlock() calls         : %d (expected 235)\n", calls);
    std::printf("  output peak                  : %.4f = %.2f dBFS (expected 0.3536 = -9.03 dBFS)\n", peak,
                gainToDb(peak));
    std::printf("  transport position at the end: %lld samples = %.3f s\n",
                static_cast<long long>(engine.getTransport().getPosition()),
                engine.getTransport().getPositionSeconds());
    std::printf("  wrote dawlab_output/l14_hello_engine.wav\n");

    DAWLAB_CHECK(calls == 235);
    DAWLAB_CHECK_NEAR(peak, 0.5 * 0.70711, 1e-3);
    DAWLAB_CHECK(engine.getTransport().getPosition() == total);
    return dawlab::test::summary("l14_hello_engine");
}
