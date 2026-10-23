// Exercise 6 solution: mute / solo resolution logic for N tracks (the chapter 14 DAW mixer uses the same expression without soloSafe:
//   a track has no solo-safe flag, only the FX bus and the master are implicitly solo-safe; see solutions/README.md)
//
// Rules (this course's definition, consistent with the default behavior of Pro Tools / Logic):
//   1. As soon as any track has solo pressed, we enter "solo mode": only soloed tracks may be audible.
//   2. Mute always wins: a track that is both soloed and muted -> silent (mute takes priority).
//   3. solo-safe (some DAWs call it solo isolate): the track stays audible in solo mode (for example an FX return or
//      the metronome), but it is still silent when it is muted itself.
//
// Truth table (S = any track is soloed):
//   audible = !mute && (!S || solo || soloSafe)
//
// A real DAW does not simply set the samples to 0; it sets each track's Gain target to 0 dB or -inf dB
// and lets a SmoothedValue ramp there, so that mute / unmute does not click.

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
    bool anySolo = false;
    for (const auto& t : tracks)
        anySolo = anySolo || t.solo;

    std::vector<bool> audible(tracks.size());
    for (size_t i = 0; i < tracks.size(); ++i) {
        const TrackState& t = tracks[i];
        audible[i] = !t.mute && (!anySolo || t.solo || t.soloSafe);
    }
    return audible;
}

// Audible or not -> the target dB of this track's Gain
float audibleToGainDb(bool audible) { return audible ? 0.0f : kMinusInfinityDb; }

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

void printCase(const char* name, const std::vector<TrackState>& tracks) {
    const auto a = resolveAudible(tracks);
    std::printf("%-40s", name);
    for (size_t i = 0; i < tracks.size(); ++i)
        std::printf(" [%zu:%s%s%s->%s]", i, tracks[i].mute ? "M" : "-", tracks[i].solo ? "S" : "-",
                    tracks[i].soloSafe ? "F" : "-", a[i] ? "on " : "off");
    std::printf("\n");
}

} // namespace

int main() {
    std::vector<TrackState> t(4);

    printCase("all normal", t);
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, true, true, true}));

    t[1].mute = true;
    printCase("mute 1", t);
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, false, true, true}));

    t = std::vector<TrackState>(4);
    t[2].solo = true;
    printCase("solo 2", t);
    DAWLAB_CHECK(sameAs(resolveAudible(t), {false, false, true, false}));

    t[0].solo = true;
    printCase("solo 0 + solo 2", t);
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, false, true, false}));

    t[2].mute = true;
    printCase("solo 0 + solo 2 + mute 2 (mute wins)", t);
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, false, false, false}));

    t = std::vector<TrackState>(4);
    t[3].soloSafe = true;
    t[1].solo = true;
    printCase("solo 1 + 3 is solo-safe", t);
    DAWLAB_CHECK(sameAs(resolveAudible(t), {false, true, false, true}));

    t[3].mute = true;
    printCase("solo 1 + 3 solo-safe but muted", t);
    DAWLAB_CHECK(sameAs(resolveAudible(t), {false, true, false, false}));

    t = std::vector<TrackState>(4);
    t[3].soloSafe = true;
    printCase("nobody soloed, 3 is solo-safe", t);
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
    std::printf("mute/unmute through Gain (50 ms ramp): max first difference %.4f (natural value of the sine %.4f), peak during the mute = %.1f dBFS\n",
                maxDiff, naturalDiff, gainToDb(quietPeak));
    std::printf("Control case (ramp = 0, instant jump): max first difference %.4f (%.1fx the natural value) -> proves this check can catch a click\n",
                rawDiff, rawDiff / naturalDiff);
    DAWLAB_CHECK(maxDiff < naturalDiff * 1.05f); // no click
    DAWLAB_CHECK(rawDiff > naturalDiff * 2.0f);  // control case: without smoothing there is a click (the check has discriminating power)
    DAWLAB_CHECK(quietPeak == 0.0f);             // during the mute it really is 0
    DAWLAB_CHECK(peakAbs(audio.getReadPointer(0) + 70 * kBlock, 10 * kBlock) > 0.49f); // the signal is back after the unmute
    writeWav(outputPath("l01_ex6_mute_solo.wav"), audio, kSr);

    return dawlab::test::summary("l01_ex6");
}
