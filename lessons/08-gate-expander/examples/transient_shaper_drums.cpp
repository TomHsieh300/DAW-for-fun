// Example 4: transient shaper on a drum loop, attack +/-12 dB and sustain +/-12 dB
//
// What it shows:
//   - First uses two EnvelopeFollowers (fast: 0.5 / 30 ms, slow: 10 / 150 ms) to track the first kick,
//     printing fast, slow, and the difference d = 20*log10(fast/slow) every 5 ms: d > 0 during the attack,
//     d < 0 during the sustain
//   - Four TransientShaper settings, attack +12 / -12 dB and sustain -12 / +12 dB, each written to its own WAV
//   - Prints peak, RMS, and crest factor (peak - RMS) for every version: attack +12 increases the crest factor,
//     attack -12 decreases it; sustain +12 pushes the kick's tail louder than its attack (so the peak actually rises)
// How to run: l08_transient_shaper_drums [input.wav]; with no argument it uses makeDrumLoop().
// Output files: dawlab_output/l08_ts_dry.wav, l08_ts_attack_plus12.wav, l08_ts_attack_minus12.wav,
//          l08_ts_sustain_minus12.wav, l08_ts_sustain_plus12.wav.
// What to listen for: with attack +12 the drums hit harder and sound closer; with attack -12 they get rounder and
//            more distant; with sustain -12 the tails (the kick's low-frequency tail, the snare's rattle) are
//            shortened, as if in a drier room; with sustain +12 the tails get longer and the whole kit sounds fuller.

#include <dawlab/Common.h>
#include <dawlab/dsp/EnvelopeFollower.h>
#include <dawlab/dsp/TransientShaper.h>

#include <cstdio>
#include <string>

using namespace dawlab;
using dawlab::dsp::DetectorMode;
using dawlab::dsp::EnvelopeFollower;
using dawlab::dsp::TransientShaper;

namespace {

constexpr int kBlockSize = 512;

struct Stats {
    float peakDb, rmsDb, crestDb;
};

Stats measure(const AudioBuffer& b) {
    const float p = peakAbs(b);
    float r = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        r = std::max(r, rms(b, ch));
    return {gainToDb(p), gainToDb(r), gainToDb(p) - gainToDb(r)};
}

AudioBuffer runShaper(const AudioBuffer& input, TransientShaper& ts) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    const int n = out.getNumSamples();
    for (int start = 0; start < n; start += kBlockSize)
        ts.process(whole.getSubBlock(start, std::min(kBlockSize, n - start)));
    return out;
}

} // namespace

int main(int argc, char** argv) {
    std::printf("Chapter 08 Example 4: transient shaper on a drum loop\n");

    const double sampleRate = 48000.0;
    AudioBuffer dry;
    if (argc > 1) {
        WavData wav;
        std::string err;
        if (!readWav(argv[1], wav, &err)) {
            std::printf("Read failed: %s\n", err.c_str());
            return 1;
        }
        dry = wav.buffer;
    } else {
        dry = makeDrumLoop(sampleRate);
    }

    // ---- 1. Difference between the fast and slow envelopes ------------------------------------------------
    std::printf("\n[1] Fast / slow envelopes and their difference d around the first kick (starting at 0 ms, one row every 5 ms)\n");
    EnvelopeFollower fast, slow;
    fast.prepare(sampleRate);
    fast.setMode(DetectorMode::Peak);
    fast.setAttackMs(TransientShaper::kFastAttackMs);
    fast.setReleaseMs(TransientShaper::kFastReleaseMs);
    slow.prepare(sampleRate);
    slow.setMode(DetectorMode::Peak);
    slow.setAttackMs(TransientShaper::kSlowAttackMs);
    slow.setReleaseMs(TransientShaper::kSlowReleaseMs);
    std::printf("    time(ms)  fast(dB)  slow(dB)    d(dB)   -> gain (attack +12 / sustain -12)\n");
    const int probeSamples = secondsToSamples(0.20, sampleRate);
    const int printEvery = msToSamples(5.0, sampleRate);
    for (int i = 0; i < probeSamples; ++i) {
        float det = 0.0f;
        for (int ch = 0; ch < dry.getNumChannels(); ++ch)
            det = std::max(det, std::abs(dry.getSample(ch, i)));
        const float f = fast.processSample(det);
        const float s = slow.processSample(det);
        if (i % printEvery == 0) {
            const float d = gainToDb(f) - gainToDb(s);
            const float g = d > 0.0f ? 12.0f * std::min(d / TransientShaper::kFullScaleDb, 1.0f)
                                     : -12.0f * std::min(-d / TransientShaper::kFullScaleDb, 1.0f);
            std::printf("    %7.1f   %7.1f   %7.1f   %+6.1f   %+6.1f dB\n", samplesToMs(i, sampleRate), gainToDb(f),
                        gainToDb(s), d, g);
        }
    }
    std::printf("    (Attack: fast shoots up while slow is still climbing -> d > 0; afterwards fast follows the signal down while slow holds on -> d < 0)\n");

    // ---- 2. Four settings ------------------------------------------------------------
    struct Setting {
        const char* name;
        const char* file;
        float attackDb, sustainDb;
    };
    const Setting settings[] = {
        {"dry", "l08_ts_dry.wav", 0.0f, 0.0f},
        {"attack +12", "l08_ts_attack_plus12.wav", 12.0f, 0.0f},
        {"attack -12", "l08_ts_attack_minus12.wav", -12.0f, 0.0f},
        {"sustain -12", "l08_ts_sustain_minus12.wav", 0.0f, -12.0f},
        {"sustain +12", "l08_ts_sustain_plus12.wav", 0.0f, 12.0f},
    };

    std::printf("\n[2] Peak / RMS / crest factor of the four settings (dB)\n");
    std::printf("    setting       peak     RMS    crest   dcrest vs dry\n");
    float dryCrest = 0.0f;
    for (const auto& s : settings) {
        TransientShaper ts;
        ts.prepare(sampleRate, kBlockSize, dry.getNumChannels());
        ts.setAttackDb(s.attackDb);
        ts.setSustainDb(s.sustainDb);
        const AudioBuffer out = runShaper(dry, ts);
        writeWav(outputPath(s.file), out, sampleRate, 32); // attack +12 may exceed 0 dBFS, so store as float
        const Stats st = measure(out);
        if (s.attackDb == 0.0f && s.sustainDb == 0.0f)
            dryCrest = st.crestDb;
        std::printf("    %-12s %6.2f  %6.2f  %6.2f   %+6.2f%s\n", s.name, st.peakDb, st.rmsDb, st.crestDb,
                    st.crestDb - dryCrest, st.peakDb > 0.0f ? "   (peak exceeds 0 dBFS: the WAV is stored as 32-bit float)" : "");
    }
    std::printf("    -> attack +12: peak rises by almost +12 dB while RMS rises less -> crest factor grows (more punch)\n");
    std::printf("    -> attack -12: peak drops more than RMS -> crest factor shrinks (rounder, more distant)\n");
    std::printf("    -> sustain -12: tails get shorter, RMS drops, peak barely moves -> crest factor grows (drier)\n");
    std::printf("    -> sustain +12: the whole kick tail is raised by 12 dB, louder than the original attack -> the peak rises with it;\n"
                "      in practice sustain boosts are usually only +3 to +6 dB, followed by a limiter\n");

    std::printf("\nOutput: dawlab_output/l08_ts_dry.wav, l08_ts_attack_plus12.wav, l08_ts_attack_minus12.wav, "
                "l08_ts_sustain_minus12.wav, l08_ts_sustain_plus12.wav\n");
    return 0;
}
