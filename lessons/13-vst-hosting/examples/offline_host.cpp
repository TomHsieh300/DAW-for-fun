// l13_offline_host: the smallest possible "offline host" -- load a VST3, list its parameters, set parameters, process a drum loop, write WAVs
//
// Demonstrates every step of the plugin lifecycle (seen from the host side):
//   scan / instantiate -> prepareToPlay (bus negotiation, latency) -> processBlock x N -> releaseResources
// and normalized parameters: the host only sends 0..1, and the plugin converts to dB / ms itself.
//
// Run:
//   l13_offline_host                         loads the DawLabCompressor built by this chapter
//   l13_offline_host <plugin.vst3> [in.wav]  loads any VST3 (without a wav it uses the synthesized drum loop)
//
// Output (dawlab_output/):
//   l13_offline_dry.wav   the original drum loop
//   l13_offline_wet.wav   the result after the plugin
// What to listen for (default DawLabCompressor: threshold -30 dB, ratio 4:1, attack 5 ms, release 80 ms):
//   wet is much quieter overall (makeup is not enabled); the "head" of every drum hit (the first few ms) is still there, but the body after it is pushed down,
//   so it sounds shorter and "snappier". Making up the level (or using the Makeup parameter) before comparing makes this more obvious.
//
// Verification:
//   1. The parameter text the plugin reports (e.g. "-30.0 dB") = the value we compute with the normalized formula -> the normalized conversion is correct;
//   2. The output peak drops at least 3 dB and the RMS at least 6 dB -> compression really happens; the crest factor (peak - RMS) actually grows
//      (the 5 ms attack lets the drum head through first, see the README "How it sounds, and the numbers" paragraph);
//   3. The result is identical to "calling dawlab::dsp::Compressor directly" (difference = 0, bit-exact) -> the plugin wrapper does not change the DSP.

#include "L13Common.h"

#include <dawlab/dsp/Compressor.h>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

void printParameters(const dawlab::host::HostedPlugin& plugin) {
    std::printf("  %3s  %-12s %-10s %10s   %s\n", "#", "name", "id", "normalized", "text");
    for (int i = 0; i < plugin.getNumParameters(); ++i)
        std::printf("  %3d  %-12s %-10s %10.4f   %s\n", i, plugin.getParameterName(i).toRawUTF8(),
                    plugin.getParameterId(i).toRawUTF8(), plugin.getParameterValue(i),
                    plugin.getParameterText(i).toRawUTF8());
}

} // namespace

int main(int argc, char* argv[]) {
    const juce::ScopedJuceInitialiser_GUI juceInit; // main thread = message thread
    dawlab::host::PluginHost host;

    const juce::File pluginFile = argc > 1 ? juce::File(juce::String::fromUTF8(argv[1])) : l13::defaultCompressorVst3();
    const bool isDefaultPlugin = argc <= 1;

    // ---- 1. instantiate ------------------------------------------------------------
    std::printf("== Loading %s ==\n", pluginFile.getFullPathName().toRawUTF8());
    auto plugin = l13::mustLoad(host, pluginFile, kSampleRate, kBlockSize);
    if (plugin == nullptr)
        return 1;

    // ---- 2. prepare (bus layout negotiation + prepareToPlay) --------------------------
    plugin->prepare(kSampleRate, kBlockSize, 2);
    std::printf("Name: %s   editor: %s   latency: %d samples\n", plugin->getName().toRawUTF8(),
                plugin->hasEditor() ? "yes" : "no", plugin->getLatencySamples());
    std::printf("bus layout: %s\n", plugin->describeBusLayout().toRawUTF8());
    std::printf("\nParameters (defaults):\n");
    printParameters(*plugin);

    // ---- 3. Set the parameters (the host can only send 0..1) -------------------------
    // DawLabCompressor's ranges (see plugin/compressor/CompressorProcessor.cpp):
    //   Threshold -60..0 dB (linear), Ratio 1..20 (skew 0.5), Attack 0.1..200 ms (centre 10 ms), Release 5..2000 ms (centre 100 ms)
    const float thresholdDb = -30.0f, ratio = 4.0f, attackMs = 5.0f, releaseMs = 80.0f;
    const float nThreshold = l13::toNormalized(thresholdDb, -60.0f, 0.0f);
    const float nRatio = l13::toNormalized(ratio, 1.0f, 20.0f, 0.5f);
    const float nAttack = l13::toNormalized(attackMs, 0.1f, 200.0f, l13::skewForCentre(0.1f, 200.0f, 10.0f));
    const float nRelease = l13::toNormalized(releaseMs, 5.0f, 2000.0f, l13::skewForCentre(5.0f, 2000.0f, 100.0f));

    bool paramsOk = true;
    if (plugin->findParameter("Threshold") >= 0) {
        paramsOk &= l13::setParam(*plugin, "Threshold", nThreshold);
        paramsOk &= l13::setParam(*plugin, "Ratio", nRatio);
        paramsOk &= l13::setParam(*plugin, "Attack", nAttack);
        paramsOk &= l13::setParam(*plugin, "Release", nRelease);
        std::printf("\nSet: Threshold %.1f dB -> %.4f, Ratio %.1f -> %.4f, Attack %.1f ms -> %.4f, Release %.1f ms -> %.4f\n",
                    thresholdDb, nThreshold, ratio, nRatio, attackMs, nAttack, releaseMs, nRelease);
        printParameters(*plugin);
    } else {
        std::printf("\n(this plugin has no Threshold parameter; processing with the default values)\n");
    }

    // ---- 4. Prepare the input: the drum loop or the user's WAV ------------------------
    dawlab::AudioBuffer dry = dawlab::makeDrumLoop(kSampleRate, 4);
    double sampleRate = kSampleRate;
    if (argc > 2) {
        dawlab::WavData wav;
        std::string err;
        if (dawlab::readWav(argv[2], wav, &err) && std::abs(wav.sampleRate - kSampleRate) < 1.0) {
            dry = wav.buffer;
            sampleRate = wav.sampleRate;
        } else {
            std::printf("Cannot read %s (or its sample rate is not 48000); using the drum loop instead\n", argv[2]);
        }
    }

    // ---- 5. process: feed the plugin block by block (simulating the audio callback) ----
    const dawlab::AudioBuffer wet = l13::renderOffline(*plugin, dry, kBlockSize);
    plugin->releaseResources();

    dawlab::writeWav(dawlab::outputPath("l13_offline_dry.wav"), dry, sampleRate);
    dawlab::writeWav(dawlab::outputPath("l13_offline_wet.wav"), wet, sampleRate);

    const float dryPeak = l13::peakDb(dry), wetPeak = l13::peakDb(wet);
    const float dryRms = l13::rmsDb(dry), wetRms = l13::rmsDb(wet);
    std::printf("\n%-8s %10s %10s %14s\n", "", "peak dBFS", "RMS dBFS", "crest (dB)");
    std::printf("%-8s %10.2f %10.2f %14.2f\n", "dry", dryPeak, dryRms, dryPeak - dryRms);
    std::printf("%-8s %10.2f %10.2f %14.2f\n", "wet", wetPeak, wetRms, wetPeak - wetRms);
    std::printf("%-8s %10.2f %10.2f %14.2f\n", "diff", wetPeak - dryPeak, wetRms - dryRms,
                (wetPeak - wetRms) - (dryPeak - dryRms));
    std::printf("Output: dawlab_output/l13_offline_dry.wav, l13_offline_wet.wav\n");

    if (!isDefaultPlugin)
        return 0;

    // ---- self-check (default DawLabCompressor) ----------------------------------------
    // (1) normalized conversion: the value the plugin displays = the value we wanted to set
    DAWLAB_CHECK(paramsOk);
    DAWLAB_CHECK_NEAR(plugin->getParameterText(plugin->findParameter("Threshold")).getFloatValue(), thresholdDb, 0.051);
    DAWLAB_CHECK_NEAR(plugin->getParameterText(plugin->findParameter("Ratio")).getFloatValue(), ratio, 0.006);
    DAWLAB_CHECK_NEAR(plugin->getParameterText(plugin->findParameter("Attack")).getFloatValue(), attackMs, 0.006);
    DAWLAB_CHECK_NEAR(plugin->getParameterText(plugin->findParameter("Release")).getFloatValue(), releaseMs, 0.051);
    // (2) compression really happens
    DAWLAB_CHECK(!dawlab::hasNanOrInf(wet));
    DAWLAB_CHECK(wetPeak < dryPeak - 3.0f);
    DAWLAB_CHECK(wetRms < dryRms - 6.0f);
    // Note: the crest factor actually "grows" -- the 5 ms attack lets the first few ms of each drum head through before the body is compressed,
    // so the RMS drops more than the peak. This is exactly why a compressor with a medium attack makes drums sound more "punchy".
    DAWLAB_CHECK((wetPeak - wetRms) > (dryPeak - dryRms));

    // (3) compare with calling dsp::Compressor directly (same parameters, same block size)
    dawlab::dsp::Compressor reference;
    reference.prepare(kSampleRate, kBlockSize, 2);
    reference.setThresholdDb(thresholdDb);
    reference.setRatio(ratio);
    reference.setKneeDb(6.0f); // the plugin's default
    reference.setAttackMs(attackMs);
    reference.setReleaseMs(releaseMs);
    reference.setMakeupGainDb(0.0f);
    reference.reset();
    dawlab::AudioBuffer expected = dry;
    dawlab::AudioBlock all(expected);
    for (int start = 0; start < expected.getNumSamples(); start += kBlockSize)
        reference.process(all.getSubBlock(start, std::min(kBlockSize, expected.getNumSamples() - start)));
    const float diff = l13::maxAbsDifference(wet, expected);
    std::printf("Max difference between the plugin and calling dsp::Compressor directly: %.3g (%.1f dB)\n", diff, dawlab::gainToDb(diff));
    DAWLAB_CHECK(diff == 0.0f); // bit-exact: with mix 100 % and makeup 0 dB the plugin does not touch the compressor's output at all

    return dawlab::test::summary("l13_offline_host");
}
