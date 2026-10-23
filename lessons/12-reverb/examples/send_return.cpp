// Example 4: using reverb on a send / return (aux bus) -- pre-fader vs post-fader
//
// What it demonstrates:
//   In a mix, reverb is usually not "inserted on every track" but placed on an aux / return bus:
//
//     drum track --> fader ----------------------+
//            |                                   +--> master
//            +-(send level)-> reverb bus (100% wet) -> return fader --+
//
//   1. The reverb is set to mix = 1 (pure wet); the send level decides how much goes in,
//      and the return fader decides how much is mixed back.
//   2. Post-fader send (the default): the send is taken after the fader -> pulling down the track fader
//      also lowers the reverb. Pre-fader send: the send is taken before the fader -> with the fader all
//      the way down the reverb is still there (the "reverb only" effect).
//   3. One reverb bus can be shared by several tracks (here a drum loop and a pluck), so they sound
//      like they are in the same space.
//
// How to run: run with no arguments (ctest runs it too).
// Output files (build/dawlab_output/):
//   l12_send_dry.wav          the two tracks summed directly (no reverb)
//   l12_send_mix.wav          dry + reverb return (post-fader send, send -6 dB, return 0 dB, RT60 1.6 s)
//   l12_send_prefader.wav     track fader at -inf but the pre-fader send still feeds the reverb -> reverb tail only
// What you should hear: in the mix version the drums and pluck gain a sense of space while the transients
// stay intact; the prefader version contains only the "echo of the room" and no direct sound.

#include <dawlab/Common.h>
#include <dawlab/dsp/Reverb.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

struct Track {
    AudioBuffer audio;
    float faderDb = 0.0f;
    float sendDb = -6.0f;
    bool preFader = false;
};

// Mixdown: returns (master, returnOnly). The reverb sits on the aux bus with mix = 1.
void mixdown(const std::vector<Track>& tracks, Reverb& reverb, float returnDb, AudioBuffer& master,
             AudioBuffer& wetOnly) {
    const int n = tracks.front().audio.getNumSamples();
    master.setSize(2, n);
    wetOnly.setSize(2, n);
    AudioBuffer bus(2, n); // input of the reverb bus (the sum of every track's send)
    for (const Track& t : tracks) {
        const float fader = dbToGain(t.faderDb);
        const float send = dbToGain(t.sendDb) * (t.preFader ? 1.0f : fader); // post-fader: the send is after the fader
        for (int ch = 0; ch < 2; ++ch) {
            master.addFrom(ch, 0, t.audio, ch, 0, n, fader);
            bus.addFrom(ch, 0, t.audio, ch, 0, n, send);
        }
    }
    // Reverb bus: process block by block (mix = 1 -> pure wet)
    reverb.reset();
    for (int start = 0; start < n; start += kBlockSize) {
        const int len = std::min(kBlockSize, n - start);
        reverb.process(bus.toBlock().getSubBlock(start, len));
    }
    const float ret = dbToGain(returnDb);
    for (int ch = 0; ch < 2; ++ch) {
        wetOnly.addFrom(ch, 0, bus, ch, 0, n, ret);
        master.addFrom(ch, 0, bus, ch, 0, n, ret);
    }
}

} // namespace

int main() {
    std::printf("=== Chapter 12 Example 4: send / return ===\n\n");

    // Two tracks of material: a drum loop (2 bars @ 120 BPM = 4 s) and a pluck sequence (4 s)
    std::vector<Track> tracks(2);
    tracks[0].audio = makeDrumLoop(kSampleRate, 2, 120.0, 0.55f, 2);
    tracks[1].audio = makePluckSequence(kSampleRate, 4.0, 0.25f, 2);
    tracks[0].sendDb = -6.0f;
    tracks[1].sendDb = -3.0f; // send a bit more of the pluck (melodic instruments usually need more space than drums)

    Reverb reverb;
    reverb.prepare(kSampleRate, kBlockSize, 2);
    reverb.setMix(1.0f); // a reverb on a bus is always 100% wet
    reverb.setDecaySeconds(1.6f);
    reverb.setPreDelayMs(20.0f); // 20 ms pre-delay: let the transient come out first, the reverb follows slightly later
    reverb.setDampingHz(5000.0f);
    reverb.setSize(0.6f);
    reverb.setWidth(1.0f);

    AudioBuffer master, wet;

    // ---- 1. No reverb ---------------------------------------------------------------
    {
        AudioBuffer dry(2, tracks[0].audio.getNumSamples());
        for (const Track& t : tracks)
            for (int ch = 0; ch < 2; ++ch)
                dry.addFrom(ch, 0, t.audio, ch, 0, dry.getNumSamples(), dbToGain(t.faderDb));
        writeWav(outputPath("l12_send_dry.wav"), dry, kSampleRate);
        std::printf("dry, two tracks summed : RMS %.4f, peak %.3f\n", rms(dry, 0), peakAbs(dry));
    }

    // ---- 2. Post-fader send, fader at 0 dB ------------------------------------------
    mixdown(tracks, reverb, 0.0f, master, wet);
    const float wetRmsFader0 = rms(wet, 0);
    writeWav(outputPath("l12_send_mix.wav"), master, kSampleRate);
    std::printf("post-fader, fader 0 dB : master RMS %.4f, peak %.3f, return(wet) RMS %.4f\n", rms(master, 0),
                peakAbs(master), wetRmsFader0);

    // ---- 3. Post-fader send, fader at -12 dB: the return also drops by 12 dB ---------
    for (Track& t : tracks)
        t.faderDb = -12.0f;
    mixdown(tracks, reverb, 0.0f, master, wet);
    std::printf("post-fader, fader -12dB: return RMS %.4f (%+.2f dB relative to 0 dB -> follows the fader down)\n",
                rms(wet, 0), gainToDb(rms(wet, 0) / wetRmsFader0));

    // ---- 4. Pre-fader send, fader at -inf: the return is unchanged, the master is reverb only
    for (Track& t : tracks) {
        t.faderDb = -120.0f; // dbToGain returns 0 (below kMinusInfinityDb)
        t.preFader = true;
    }
    mixdown(tracks, reverb, 0.0f, master, wet);
    writeWav(outputPath("l12_send_prefader.wav"), master, kSampleRate);
    std::printf("pre-fader,  fader -inf : return RMS %.4f (%+.2f dB relative to 0 dB -> unaffected by the fader), "
                "master = reverb only\n\n",
                rms(wet, 0), gainToDb(rms(wet, 0) / wetRmsFader0));

    std::printf("Key takeaways:\n");
    std::printf("  - A reverb on a reverb bus always runs at mix = 1; \"how wet\" is set by the send and return faders.\n");
    std::printf("  - Sharing one reverb bus across tracks saves CPU, and all the instruments sound like they are in the same space.\n");
    std::printf("  - Pre-fader sends are often used for headphone monitor mixes, or for the \"pull the fader down and keep only the reverb\" effect.\n");
    std::printf("\nOutput: dawlab_output/l12_send_dry.wav, l12_send_mix.wav, l12_send_prefader.wav\n");
    return 0;
}
