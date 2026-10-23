// Exercise 14 (solution): one track block, mono or stereo.
//
// processTrackBlock() is the per-track part of Mixer::process() for one TrackFormat, without the
// engine around it (no smoothing, no PDC, no sends):
//
//   Mono    L = (L + R) / 2 -> insert on ONE channel -> copy L to R -> fader x pan law
//   Stereo  insert on BOTH channels                               -> fader x balance
//
//   pan law (constant power):  panL = cos((pan + 1) * pi/4),  panR = sin((pan + 1) * pi/4)
//                              centre: 0.7071 = -3.01 dB per side
//   balance:                   panL = min(1, 1 - pan),        panR = min(1, 1 + pan)
//                              centre: 1 on both sides, the image is kept
//
// Self-check: the gains against ChannelStrip::computeStripGains, the 3.01 dB centre difference
// of a mono file on a mono vs a stereo track, balance keeping L != R, a chorus with full spread
// collapsing to L = R on a mono track (and staying wide on a stereo track), and a constant clip
// rendered by the real engine.

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

using namespace dawlab;
using namespace dawlab::engine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr float kPi = 3.14159265358979f;

// The pan gains of a track: pan law for a mono track, balance for a stereo track.
void trackPanGains(TrackFormat format, float pan, float& left, float& right) {
    const float p = std::clamp(pan, -1.0f, 1.0f);
    if (format == TrackFormat::Mono) {
        const float angle = (p + 1.0f) * 0.25f * kPi; // 0 .. pi/2
        left = std::cos(angle);
        right = std::sin(angle);
    } else {
        left = std::min(1.0f, 1.0f - p);
        right = std::min(1.0f, 1.0f + p);
    }
}

// Processes one stereo block in place as a track of the given format. `insert` may be null; it
// was prepared for two channels and must accept a one-channel block (all built-in effects do).
void processTrackBlock(AudioBlock block, TrackFormat format, InsertEffect* insert, float gainDb, float pan) {
    const int n = block.getNumSamples();
    float* l = block.getChannelPointer(0);
    float* r = block.getChannelPointer(1);
    if (format == TrackFormat::Mono) {
        for (int i = 0; i < n; ++i) // fold: exact for a mono file stored as L = R
            l[i] = 0.5f * (l[i] + r[i]);
        if (insert != nullptr)
            insert->process(block.getChannelSubset(0, 1));
        std::copy_n(l, n, r);
    } else if (insert != nullptr) {
        insert->process(block);
    }
    float pl = 1.0f, pr = 1.0f;
    trackPanGains(format, pan, pl, pr);
    const float g = dbToGain(gainDb);
    for (int i = 0; i < n; ++i) {
        l[i] *= g * pl;
        r[i] *= g * pr;
    }
}

// Copies `source` (1 or 2 channels; 1 = a mono file, stored as L = R) into a stereo buffer and
// runs it block by block through processTrackBlock().
AudioBuffer renderTrack(const AudioBuffer& source, TrackFormat format, InsertEffect* insert, float gainDb,
                        float pan) {
    const int n = source.getNumSamples();
    AudioBuffer out(2, n);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
            out.setSample(ch, i, source.getSample(std::min(ch, source.getNumChannels() - 1), i));
    for (int start = 0; start < n; start += kBlockSize)
        processTrackBlock(AudioBlock(out).getSubBlock(start, std::min(kBlockSize, n - start)), format, insert, gainDb,
                          pan);
    return out;
}

AudioBuffer constant(float left, float right, int n) {
    AudioBuffer b(2, n);
    for (int i = 0; i < n; ++i) {
        b.setSample(0, i, left);
        b.setSample(1, i, right);
    }
    return b;
}

float maxSideDifference(const AudioBuffer& b) {
    float d = 0.0f;
    for (int i = 0; i < b.getNumSamples(); ++i)
        d = std::max(d, std::abs(b.getSample(0, i) - b.getSample(1, i)));
    return d;
}

} // namespace

int main() {
    std::printf("Exercise 14\n");

    // ---- the pan gains match the engine's strip ----
    float worst = 0.0f;
    for (TrackFormat format : {TrackFormat::Mono, TrackFormat::Stereo})
        for (float pan = -1.0f; pan <= 1.0f; pan += 0.25f) {
            float l = 0, r = 0, el = 0, er = 0;
            trackPanGains(format, pan, l, r);
            ChannelStrip::computeStripGains(0.0f, pan, dsp::PanLaw::ConstantPower,
                                            format == TrackFormat::Mono ? PanMode::PanLaw : PanMode::Balance, el, er);
            worst = std::max({worst, std::abs(l - el), std::abs(r - er)});
        }
    std::printf("  pan gains vs ChannelStrip::computeStripGains: max |difference| = %.3g\n", static_cast<double>(worst));
    DAWLAB_CHECK(worst < 1e-5f);

    // ---- a mono file at the centre: mono track vs stereo track ----
    constexpr int kLen = 4800;
    const AudioBuffer monoFile = constant(0.5f, 0.5f, kLen); // the loader stores mono as L = R
    const AudioBuffer onMono = renderTrack(monoFile, TrackFormat::Mono, nullptr, 0.0f, 0.0f);
    const AudioBuffer onStereo = renderTrack(monoFile, TrackFormat::Stereo, nullptr, 0.0f, 0.0f);
    const float diffDb = gainToDb(onStereo.getSample(0, 100) / onMono.getSample(0, 100));
    std::printf("  mono file at the centre: mono track %.4f, stereo track %.4f -> %.2f dB\n",
                static_cast<double>(onMono.getSample(0, 100)), static_cast<double>(onStereo.getSample(0, 100)),
                static_cast<double>(diffDb));
    DAWLAB_CHECK_NEAR(onMono.getSample(0, 100), 0.5 * 0.70711, 1e-4);
    DAWLAB_CHECK_NEAR(onMono.getSample(1, 100), 0.5 * 0.70711, 1e-4);
    DAWLAB_CHECK_NEAR(diffDb, 3.0103, 1e-3);

    // ---- a stereo source (0.6 | 0.2) ----
    const AudioBuffer stereoFile = constant(0.6f, 0.2f, kLen);
    const AudioBuffer bal = renderTrack(stereoFile, TrackFormat::Stereo, nullptr, 0.0f, 0.0f);
    const AudioBuffer balRight = renderTrack(stereoFile, TrackFormat::Stereo, nullptr, 0.0f, 0.5f);
    const AudioBuffer folded = renderTrack(stereoFile, TrackFormat::Mono, nullptr, 0.0f, 0.0f);
    const AudioBuffer hardRight = renderTrack(stereoFile, TrackFormat::Mono, nullptr, 0.0f, 1.0f);
    std::printf("  stereo source 0.6 | 0.2: stereo track %.3f | %.3f, balance +0.5 %.3f | %.3f, mono track %.3f | %.3f\n",
                static_cast<double>(bal.getSample(0, 100)), static_cast<double>(bal.getSample(1, 100)),
                static_cast<double>(balRight.getSample(0, 100)), static_cast<double>(balRight.getSample(1, 100)),
                static_cast<double>(folded.getSample(0, 100)), static_cast<double>(folded.getSample(1, 100)));
    DAWLAB_CHECK_NEAR(bal.getSample(0, 100), 0.6, 1e-5); // balance at the centre: unity, L != R kept
    DAWLAB_CHECK_NEAR(bal.getSample(1, 100), 0.2, 1e-5);
    DAWLAB_CHECK_NEAR(balRight.getSample(0, 100), 0.3, 1e-5); // only the left side is turned down
    DAWLAB_CHECK_NEAR(balRight.getSample(1, 100), 0.2, 1e-5);
    DAWLAB_CHECK_NEAR(folded.getSample(0, 100), 0.4 * 0.70711, 1e-4); // (0.6 + 0.2) / 2, pan law
    DAWLAB_CHECK_NEAR(folded.getSample(1, 100), 0.4 * 0.70711, 1e-4);
    DAWLAB_CHECK_NEAR(hardRight.getSample(0, 100), 0.0, 1e-6); // pan law hard right: R only
    DAWLAB_CHECK_NEAR(hardRight.getSample(1, 100), 0.4, 1e-5);

    // ---- a stereo-generating insert: chorus with full spread ----
    const AudioBuffer sine = makeSine(440.0, kSampleRate, 0.25, 0.5f, 2);
    float chorusDiff[2] = {};
    for (TrackFormat format : {TrackFormat::Mono, TrackFormat::Stereo}) {
        auto chorus = createEffect(EffectType::Chorus);
        chorus->prepare(kSampleRate, kBlockSize, kNumChannels); // prepared stereo, as the engine does
        chorus->setParameter(chorus->findParameter("Stereo Spread"), 1.0f);
        const AudioBuffer y = renderTrack(sine, format, chorus.get(), 0.0f, 0.0f);
        chorusDiff[format == TrackFormat::Mono ? 0 : 1] = maxSideDifference(y);
        DAWLAB_CHECK(!hasNanOrInf(y));
    }
    std::printf("  chorus (spread 1): max |L - R| mono track %.6f, stereo track %.4f\n",
                static_cast<double>(chorusDiff[0]), static_cast<double>(chorusDiff[1]));
    DAWLAB_CHECK(chorusDiff[0] < 1e-6f); // collapsed: the second voice has nowhere to go
    DAWLAB_CHECK(chorusDiff[1] > 0.01f); // wide

    // ---- the same as the engine (a constant needs no fader smoothing to settle) ----
    for (TrackFormat format : {TrackFormat::Mono, TrackFormat::Stereo}) {
        Engine e;
        e.prepare(kSampleRate, kBlockSize);
        auto clip = std::make_shared<AudioBuffer>(stereoFile);
        e.setTrackClips(0, {Clip::fromBuffer(clip, 0, "clip")});
        Track& t = e.getMixer().getTrack(0);
        t.setFormat(format);
        t.setGainDb(-6.0f);
        t.setPan(-0.4f);
        RenderOptions o;
        o.numSamples = kLen;
        const AudioBuffer engineOut = renderOffline(e, o);
        const AudioBuffer mine = renderTrack(stereoFile, format, nullptr, -6.0f, -0.4f);
        const float dl = std::abs(engineOut.getSample(0, kLen - 1) - mine.getSample(0, kLen - 1));
        const float dr = std::abs(engineOut.getSample(1, kLen - 1) - mine.getSample(1, kLen - 1));
        std::printf("  engine vs processTrackBlock, %-6s track, -6 dB, pan -0.4: |difference| %.2g | %.2g\n",
                    trackFormatName(format), static_cast<double>(dl), static_cast<double>(dr));
        DAWLAB_CHECK(dl < 1e-5f && dr < 1e-5f);
    }

    return dawlab::test::summary("l14_ex14");
}
