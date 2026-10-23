// l14_pdc_demo -- Step 8: plugin delay compensation, heard and measured.
//
// The experiment
//   Tracks 1 and 2 play the SAME material at the same timeline position.
//   Track 1 has a lookahead limiter (chapter 05, 5 ms lookahead = 240 samples of latency),
//   driven below its ceiling so it changes nothing but the timing. Track 2 is dry.
//   Bus A carries a convolution reverb (chapter 12, latency = one FFT partition), but nothing
//   is sent to it: it only exists to show that bus latency delays the dry sum as well.
//
//   PDC off: track 1 arrives 240 samples (5 ms) after track 2. Two copies 5 ms apart form a
//            comb filter: notches at f = (2k+1) / (2 * 5 ms) = 100, 300, 500 Hz ... and every
//            drum hit "flams".
//   PDC on : track 2 is delayed by 240 samples too; the copies coincide and simply add
//            (+6.02 dB), and the whole mix is reported as 240 + 512 samples late, which the
//            bounce removes again.
//
// Run
//   build/bin/Debug/l14_pdc_demo.exe
//
// Output
//   dawlab_output/l14_pdc_off.wav   flammed, hollow, phasey drums (comb filter)
//   dawlab_output/l14_pdc_on.wav    clean drums, 6 dB louder than one track
//   dawlab_output/l14_pdc_clicks.wav  click track: PDC off then PDC on (listen for the double click)

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include <cstdio>
#include <memory>
#include <vector>

using namespace dawlab;
using namespace dawlab::engine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// Positions of the first `count` samples whose |x| exceeds `threshold`, at least `gap` apart.
std::vector<int> findHits(const AudioBuffer& b, float threshold, int count, int gap) {
    std::vector<int> hits;
    for (int i = 0; i < b.getNumSamples() && static_cast<int>(hits.size()) < count; ++i)
        if (std::abs(b.getSample(0, i)) > threshold && (hits.empty() || i - hits.back() >= gap))
            hits.push_back(i);
    return hits;
}

void setupEngine(Engine& e, std::shared_ptr<const AudioBuffer> material) {
    e.prepare(kSampleRate, kBlockSize);
    e.setTrackClips(0, {Clip::fromBuffer(material, 0, "material")});
    e.setTrackClips(1, {Clip::fromBuffer(material, 0, "material")});
    e.getMixer().getTrack(0).setName("Limited");
    e.getMixer().getTrack(1).setName("Dry");
    auto limiter = std::make_unique<LimiterEffect>();
    limiter->setParameter(LimiterEffect::Ceiling, 0.0f);   // material peaks below 0 dBFS:
    limiter->setParameter(LimiterEffect::Lookahead, 5.0f); // the limiter only delays it
    e.addInsert(ChannelRef::track(0), -1, std::move(limiter));
    e.addInsert(ChannelRef::bus(0), -1, std::make_unique<ConvolutionReverbEffect>());
    e.getMixer().getTrack(0).setGainDb(-6.0f); // two copies summed: keep headroom
    e.getMixer().getTrack(1).setGainDb(-6.0f);
}

} // namespace

int main() {
    // ---- 1. click track: measure ----
    auto clicks = std::make_shared<AudioBuffer>(1, secondsToSamples(2.0, kSampleRate));
    for (int beat = 0; beat < 4; ++beat)
        clicks->setSample(0, beat * 24000 + 1000, 0.5f);

    Engine e;
    setupEngine(e, clicks);
    e.applyPendingCommandsOffline();
    const Mixer& m = e.getMixer();
    std::printf("Step 8: plugin delay compensation\n");
    std::printf("  insert latency  track 1 (Limiter)          : %d samples\n", m.getTrack(0).getInsertLatencySamples());
    std::printf("  insert latency  track 2 (dry)              : %d samples\n", m.getTrack(1).getInsertLatencySamples());
    std::printf("  insert latency  bus A (ConvolutionReverb)  : %d samples\n", m.getBus(0).getInsertLatencySamples());
    std::printf("  compensation    track 1 / track 2          : %d / %d samples\n",
                m.getTrack(0).getCompensator().getDelaySamples(), m.getTrack(1).getCompensator().getDelaySamples());
    std::printf("  compensation    track sum (waits for buses): %d samples\n", m.getTrackSumCompensationSamples());
    std::printf("  total latency (maxTrack + maxBus + master) : %d samples = %.2f ms\n\n", e.getLatencySamples(),
                samplesToMs(e.getLatencySamples(), kSampleRate));

    RenderOptions opt;
    opt.numSamples = clicks->getNumSamples();
    opt.compensateLatency = false; // show the raw timing first
    e.getMixer().setLatencyCompensationEnabled(false);
    e.applyPendingCommandsOffline();
    AudioBuffer off = renderOffline(e, opt);
    e.getMixer().setLatencyCompensationEnabled(true);
    e.applyPendingCommandsOffline();
    AudioBuffer on = renderOffline(e, opt);
    opt.compensateLatency = true;
    AudioBuffer onComp = renderOffline(e, opt);

    const std::vector<int> hOff = findHits(off, 0.01f, 2, 10);
    const std::vector<int> hOn = findHits(on, 0.01f, 2, 10);
    const std::vector<int> hComp = findHits(onComp, 0.01f, 1, 10);
    std::printf("  click at timeline sample 1000:\n");
    std::printf("    PDC off            : arrivals at %d and %d (flam of %d samples), peak %.3f\n", hOff[0],
                hOff.size() > 1 ? hOff[1] : -1, hOff.size() > 1 ? hOff[1] - hOff[0] : 0, peakAbs(off));
    std::printf("    PDC on             : first arrival at %d, second arrival at %d (beat 2), peak %.3f\n", hOn[0],
                hOn.size() > 1 ? hOn[1] : -1, peakAbs(on));
    std::printf("    PDC on + bounce    : first arrival at %d (latency removed)\n\n", hComp[0]);

    // Write one listening file: 2 s with PDC off, then 2 s with PDC on (output-compensated).
    AudioBuffer clickFile(2, off.getNumSamples() + onComp.getNumSamples());
    for (int ch = 0; ch < 2; ++ch) {
        clickFile.copyFrom(ch, 0, off, ch, 0, off.getNumSamples());
        clickFile.copyFrom(ch, off.getNumSamples(), onComp, ch, 0, onComp.getNumSamples());
    }
    writeWav(outputPath("l14_pdc_clicks.wav"), clickFile, kSampleRate);

    // ---- 2. drums: listen ----
    auto drums = std::make_shared<const AudioBuffer>(makeDrumLoop(kSampleRate, 2, 120.0, 0.8f));
    Engine d;
    setupEngine(d, drums);
    RenderOptions dopt;
    dopt.tailSeconds = 0.5;
    d.getMixer().setLatencyCompensationEnabled(false);
    AudioBuffer drumsOff = renderOffline(d, dopt);
    d.getMixer().setLatencyCompensationEnabled(true);
    AudioBuffer drumsOn = renderOffline(d, dopt);
    writeWav(outputPath("l14_pdc_off.wav"), drumsOff, kSampleRate);
    writeWav(outputPath("l14_pdc_on.wav"), drumsOn, kSampleRate);
    const float drumsOffDb = gainToDb(rms(drumsOff, 0));
    const float drumsOnDb = gainToDb(rms(drumsOn, 0));
    std::printf("  drums RMS  PDC off %.2f dBFS, PDC on %.2f dBFS (comb filter loses energy)\n",
                static_cast<double>(drumsOffDb), static_cast<double>(drumsOnDb));
    std::printf("Wrote dawlab_output/l14_pdc_off.wav, l14_pdc_on.wav, l14_pdc_clicks.wav\n");

    const int L = e.getMixer().getTrack(0).getInsertLatencySamples();
    DAWLAB_CHECK(L > 0);
    DAWLAB_CHECK(hOff.size() == 2 && hOff[1] - hOff[0] == L);              // two copies, L apart
    DAWLAB_CHECK(hOn.size() == 2 && hOn[0] == 1000 + e.getLatencySamples()); // one copy, total-latency late
    DAWLAB_CHECK(hOn[1] - hOn[0] == 24000);                                  // the next hit is the next beat
    DAWLAB_CHECK(hComp.size() == 1 && hComp[0] == 1000);                     // bounce: on the grid again
    DAWLAB_CHECK_NEAR(peakAbs(on), 2.0 * 0.5 * dbToGain(-6.0f) * 0.70711, 1e-3); // coherent sum
    DAWLAB_CHECK(drumsOnDb > drumsOffDb + 3.0f); // aligned copies add in amplitude, the comb loses energy
    return dawlab::test::summary("l14_pdc_demo");
}
