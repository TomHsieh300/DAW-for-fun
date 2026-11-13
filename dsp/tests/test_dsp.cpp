// Self-test of rules that every effect in dsp/ shares, whichever chapter brought it in:
//   - process() before prepare() leaves the block as it is (Delay and Distortion already did): a host may call an
//     effect it has not set up yet, and a delay line of size 0 must not be read
//   - a sidechain shorter than the main block: only the samples both have are processed (Gate and Expander already
//     did), the rest of the main block is left as it is
// The Limiter is the one exception to the first rule on purpose: before prepare() it clears the block (and asserts in
// a Debug build), because a limiter that cannot look ahead cannot keep its ceiling.
#include <dawlab/Common.h>
#include <dawlab/dsp/Chorus.h>
#include <dawlab/dsp/Compressor.h>
#include <dawlab/dsp/Expander.h>
#include <dawlab/dsp/Flanger.h>
#include <dawlab/dsp/Gate.h>
#include <dawlab/dsp/Phaser.h>
#include <dawlab/dsp/Reverb.h>

#include <cstdio>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;

// Two channels of a constant, left +0.5 and right -0.5
AudioBuffer constantBlock(int numSamples) {
    AudioBuffer b(2, numSamples);
    for (int i = 0; i < numSamples; ++i) {
        b.getWritePointer(0)[i] = 0.5f;
        b.getWritePointer(1)[i] = -0.5f;
    }
    return b;
}

bool untouchedFrom(const AudioBuffer& b, int first) {
    for (int i = first; i < b.getNumSamples(); ++i)
        if (b.getSample(0, i) != 0.5f || b.getSample(1, i) != -0.5f)
            return false;
    return true;
}

template <typename Fx>
void checkUnpreparedBlock(const char* name) {
    Fx fx;
    AudioBuffer b = constantBlock(64);
    fx.process(AudioBlock(b));
    const bool same = untouchedFrom(b, 0);
    std::printf("%-8s process() before prepare(): block %s\n", name, same ? "unchanged" : "CHANGED");
    DAWLAB_CHECK(same);
}

template <typename Fx>
void checkUnpreparedSample(const char* name) {
    Fx fx;
    const float y = fx.processSample(0, 0.5f);
    std::printf("%-8s processSample() before prepare(): %g\n", name, static_cast<double>(y));
    DAWLAB_CHECK(y == 0.5f);
}

// The main block has 512 samples and the sidechain 256 (loud): the samples past the sidechain's end are not
// touched (and nothing is read past the sidechain's end - ASan would say so)
template <typename Fx>
void checkShortSidechain(const char* name, Fx& fx) {
    AudioBuffer main = constantBlock(512);
    AudioBuffer side(1, 256);
    for (int i = 0; i < 256; ++i)
        side.getWritePointer(0)[i] = 1.0f;
    fx.process(AudioBlock(main), AudioBlock(side));
    const bool tail = untouchedFrom(main, 256);
    std::printf("%-10s sidechain of 256 for a block of 512: samples 256.. %s\n", name, tail ? "unchanged" : "CHANGED");
    DAWLAB_CHECK(tail);
}

} // namespace

int main() {
    checkUnpreparedBlock<Chorus>("Chorus");
    checkUnpreparedBlock<Flanger>("Flanger");
    checkUnpreparedBlock<Phaser>("Phaser");
    checkUnpreparedBlock<Reverb>("Reverb");
    checkUnpreparedSample<Chorus>("Chorus");
    checkUnpreparedSample<Flanger>("Flanger");
    checkUnpreparedSample<Phaser>("Phaser");
    {
        Reverb reverb;
        float wetL = 1.0f, wetR = 1.0f;
        reverb.processSampleStereo(0.5f, -0.5f, wetL, wetR);
        std::printf("Reverb   processSampleStereo() before prepare(): wet %g %g\n", static_cast<double>(wetL),
                    static_cast<double>(wetR));
        DAWLAB_CHECK(wetL == 0.0f && wetR == 0.0f);
    }

    for (bool link : {true, false}) {
        Compressor comp;
        comp.prepare(kSampleRate, 512, 2);
        comp.setStereoLink(link);
        checkShortSidechain(link ? "Comp link" : "Comp", comp);
    }
    Gate gate;
    gate.prepare(kSampleRate, 512, 2);
    checkShortSidechain("Gate", gate);
    Expander expander;
    expander.prepare(kSampleRate, 512, 2);
    checkShortSidechain("Expander", expander);

    return dawlab::test::summary("dsp");
}
