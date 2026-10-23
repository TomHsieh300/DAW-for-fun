// Exercise 6 (starter code): mute / solo resolution logic for N tracks
//
// Rules:
//   1. As soon as any track is soloed, only the soloed tracks are audible.
//   2. Mute always wins (solo + mute -> silent).
//   3. A soloSafe track stays audible in solo mode (but is still silent if it is muted itself).
//   audible = !mute && (!anySolo || solo || soloSafe)
// You are done when every self-check passes after you complete the TODOs. The starter code prints FAIL when it runs; that is expected.

#include <dawlab/Common.h>
#include <dawlab/dsp/Gain.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace dawlab;

struct TrackState {
    bool mute = false;
    bool solo = false;
    bool soloSafe = false;
};

// Returns, for each track, "should it be audible right now?"
std::vector<bool> resolveAudible(const std::vector<TrackState>& tracks) {
    // TODO: first find out whether any track is soloed, then decide track by track according to the rules
    return std::vector<bool>(tracks.size(), true);
}

// Audible or not -> the target dB of this track's Gain (audible = 0 dB, silent = kMinusInfinityDb)
float audibleToGainDb(bool audible) {
    (void)audible;
    // TODO
    return 0.0f;
}

// ---- Feeding the mute resolution result into Gain (this part is not a TODO; use it as is) ----
constexpr double kSr = 48000.0;
constexpr int kBlock = 512;
// The mute / unmute times are deliberately NOT placed at zero crossings of the 1 kHz sine:
//   15372 = 30*512 + 12,  15372 / 48 = 320.25 periods -> the positive peak of the sine (+0.5)
//   30756 = 60*512 + 36,  30756 / 48 = 640.75 periods -> the negative peak of the sine (-0.5)
// If they were placed on the block boundary 30*512 = 15360 (exactly 320 periods), x = 0 there, and even with no ramp at all,
// jumping instantly to 0 would produce no steep slope (the zero-crossing exception in README section 2.4), so the check could not detect a click.
constexpr int kMuteAt = 30 * kBlock + 12;   // mute pressed at about 0.320 s
constexpr int kUnmuteAt = 60 * kBlock + 36; // mute released at about 0.641 s

// One 1 kHz sine track goes through a Gain; mute is pressed at kMuteAt and released at kUnmuteAt.
// When an event falls in the middle of a block, the block is split into two sub-blocks (the old setting before the event, the new setting after it),
// which is how a DAW does sample-accurate automation. rampSeconds = 0 means no smoothing at all (the control case).
AudioBuffer runMuteScenario(double rampSeconds) {
    AudioBuffer audio = makeSine(1000.0, kSr, 1.0, 0.5f, 1);
    dsp::Gain gain;
    gain.setRampSeconds(rampSeconds);
    gain.prepare(kSr, kBlock, 1);
    std::vector<TrackState> one(1);
    AudioBlock ab(audio);
    const int n = audio.getNumSamples();
    int pos = 0;
    while (pos < n) {
        if (pos == kMuteAt)
            one[0].mute = true;
        if (pos == kUnmuteAt)
            one[0].mute = false;
        gain.setGainDb(audibleToGainDb(resolveAudible(one)[0]));
        int end = std::min(n, (pos / kBlock + 1) * kBlock); // the end of this audio block
        if (kMuteAt > pos && kMuteAt < end)
            end = kMuteAt; // the event falls in the middle of the block: split it
        if (kUnmuteAt > pos && kUnmuteAt < end)
            end = kUnmuteAt;
        gain.process(ab.getSubBlock(pos, end - pos));
        pos = end;
    }
    return audio;
}

// Maximum first difference max |x[n] - x[n-1]|: a click makes it far larger than the natural value A*2*pi*f/fs of the sine
float maxFirstDifference(const AudioBuffer& audio) {
    float maxDiff = 0.0f;
    for (int i = 1; i < audio.getNumSamples(); ++i)
        maxDiff = std::max(maxDiff, std::abs(audio.getSample(0, i) - audio.getSample(0, i - 1)));
    return maxDiff;
}

namespace {

bool sameAs(const std::vector<bool>& actual, std::initializer_list<bool> expected) {
    if (actual.size() != expected.size())
        return false;
    size_t i = 0;
    for (bool e : expected)
        if (actual[i++] != e)
            return false;
    return true;
}

} // namespace

int main() {
    std::vector<TrackState> t(4);
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, true, true, true}));

    t[1].mute = true;
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, false, true, true}));

    t = std::vector<TrackState>(4);
    t[2].solo = true;
    DAWLAB_CHECK(sameAs(resolveAudible(t), {false, false, true, false}));

    t[0].solo = true;
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, false, true, false}));

    t[2].mute = true;
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, false, false, false}));

    t = std::vector<TrackState>(4);
    t[3].soloSafe = true;
    t[1].solo = true;
    DAWLAB_CHECK(sameAs(resolveAudible(t), {false, true, false, true}));

    t[3].mute = true;
    DAWLAB_CHECK(sameAs(resolveAudible(t), {false, true, false, false}));

    t = std::vector<TrackState>(4);
    t[3].soloSafe = true;
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, true, true, true}));

    DAWLAB_CHECK(resolveAudible({}).empty());

    // Feed the result into Gain: the mute must go through a ramp and must not click
    const float naturalDiff = 0.5f * static_cast<float>(kTwoPi * 1000.0 / kSr);
    const AudioBuffer audio = runMuteScenario(0.05); // Gain's default 50 ms ramp
    const AudioBuffer raw = runMuteScenario(0.0);    // control case: no smoothing at all
    const float maxDiff = maxFirstDifference(audio);
    const float rawDiff = maxFirstDifference(raw);
    const int quietStart = kMuteAt + msToSamples(60.0, kSr);
    const float quietPeak = peakAbs(audio.getReadPointer(0) + quietStart, kUnmuteAt - quietStart);
    DAWLAB_CHECK(maxDiff < naturalDiff * 1.05f);
    DAWLAB_CHECK(rawDiff > naturalDiff * 2.0f);
    DAWLAB_CHECK(quietPeak == 0.0f);
    DAWLAB_CHECK(peakAbs(audio.getReadPointer(0) + 70 * kBlock, 10 * kBlock) > 0.49f);

    return dawlab::test::summary("l01_ex6_starter");
}
