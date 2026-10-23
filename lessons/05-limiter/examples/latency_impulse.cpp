// Example 3: latency -- how late does an impulse come out of the limiter? Does the gain really come down "ahead of time"?
//
// What it shows:
//   (1) Send an impulse into the limiter: the output impulse appears getLatencySamples() samples
//       late, and that number = round(lookaheadMs * sampleRate / 1000), at 44.1k / 48k / 96k alike.
//   (2) Shift the output earlier by `latency` samples and subtract it from the input (null test):
//       a signal below the ceiling should cancel completely, proving the limiter is "transparent"
//       when it has nothing to do -- just a little late.
//   (3) Look at the gain curve: the signal suddenly gets louder at sample s, but the gain starts
//       falling linearly from sample s - L and reaches its target exactly when the loud part
//       arrives at the output -- that is what lookahead means.
//
// How to run: l05_latency_impulse (no arguments)
//
// Output files (in dawlab_output/):
//   l05_latency_impulse.wav   the impulse output (48 kHz, lookahead 5 ms: the first value appears at sample 240)
//   l05_latency_burst.wav     left channel = input tone burst, right channel = limiter output; drag it into a DAW to compare the delay and gain ramp on the waveform
//
// What to listen for: the impulse itself is just a "click"; the point is the printed table and the WAV waveforms.

#include <dawlab/Common.h>
#include <dawlab/dsp/Limiter.h>

#include <cstdio>

namespace {

constexpr int kBlockSize = 512;

template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
        processor.process(whole.getSubBlock(start, len));
    }
}

int indexOfPeak(const dawlab::AudioBuffer& b, int channel) {
    const float* d = b.getReadPointer(channel);
    int best = 0;
    for (int i = 1; i < b.getNumSamples(); ++i)
        if (std::abs(d[i]) > std::abs(d[best]))
            best = i;
    return best;
}

} // namespace

int main() {
    bool ok = true;

    // ---- (1) impulse delay at different sample rates / lookaheads ----
    std::printf("(1) impulse output position vs getLatencySamples()\n");
    std::printf("%10s %12s %18s %16s %10s\n", "sampleRate", "lookahead", "getLatencySamples", "impulse at", "match?");
    for (double sr : {44100.0, 48000.0, 96000.0}) {
        for (float lookaheadMs : {1.5f, 5.0f, 10.0f}) {
            dawlab::AudioBuffer impulse = dawlab::makeImpulse(4096, 1, 0, 0.5f); // 0.5 < ceiling, so it is not reduced
            dawlab::dsp::Limiter limiter;
            limiter.prepare(sr, kBlockSize, 1);
            limiter.setCeilingDb(-1.0f);
            limiter.setLookaheadMs(lookaheadMs);
            processInBlocks(impulse, limiter);

            const int latency = limiter.getLatencySamples();
            const int measured = indexOfPeak(impulse, 0);
            const bool same = (latency == measured) && (latency == dawlab::msToSamples(lookaheadMs, sr)) &&
                              limiter.getSafetyClipCount() == 0;
            ok = ok && same;
            std::printf("%10.0f %9.1f ms %18d %16d %10s\n", sr, lookaheadMs, latency, measured, same ? "yes" : "NO");

            if (sr == 48000.0 && lookaheadMs == 5.0f)
                dawlab::writeWav(dawlab::outputPath("l05_latency_impulse.wav"), impulse, sr);
        }
    }

    // ---- (2) null test: a signal below the ceiling should cancel completely once latency is compensated ----
    {
        const double sr = 48000.0;
        dawlab::AudioBuffer in = dawlab::makePluckSequence(sr, 2.0, 0.3f);
        dawlab::AudioBuffer out = in;
        dawlab::dsp::Limiter limiter;
        limiter.prepare(sr, kBlockSize, 2);
        limiter.setCeilingDb(-1.0f);
        limiter.setLookaheadMs(5.0f);
        processInBlocks(out, limiter);
        const int L = limiter.getLatencySamples();

        float residual = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i + L < out.getNumSamples(); ++i)
                residual = std::max(residual, std::abs(out.getSample(ch, i + L) - in.getSample(ch, i)));
        std::printf("\n(2) null test (pluck sequence peak %.2f dBFS < ceiling -1 dBFS): residual after compensating %d samples = %.3g (should be 0)\n",
                    dawlab::gainToDb(dawlab::peakAbs(in)), L, residual);
        ok = ok && residual == 0.0f && limiter.getSafetyClipCount() == 0;
    }

    // ---- (3) gain comes down ahead of time: tone burst -30 -> 0 dBFS, ceiling -6 dBFS ----
    {
        const double sr = 48000.0;
        const float ceilingDb = -6.0f;
        dawlab::AudioBuffer in = dawlab::makeToneBursts(1000.0, sr, {-30.0f, 0.0f}, 0.25, 1);
        dawlab::AudioBuffer out = in;
        dawlab::dsp::Limiter limiter;
        limiter.prepare(sr, kBlockSize, 1);
        limiter.setCeilingDb(ceilingDb);
        limiter.setLookaheadMs(5.0f);
        limiter.setReleaseMs(100.0f);
        processInBlocks(out, limiter);
        const int L = limiter.getLatencySamples();
        const int step = dawlab::secondsToSamples(0.25, sr); // the input gets louder here; at the output it is step + L

        // gain = output / delayed input; dividing the largest |output| by the largest |input| within each window is more robust
        std::printf("\n(3) gain ramp: the input gets 30 dB louder at sample %d; the corresponding output position is %d (= %d + L=%d)\n", step,
                    step + L, step, L);
        std::printf("%27s %12s %14s\n", "Output pos. rel. to step+L", "gain (dB)", "output (dBFS)");
        const int win = dawlab::msToSamples(0.5, sr);
        for (int rel : {-2 * L, -L, -3 * L / 4, -L / 2, -L / 4, 0, L / 2, L, 2 * L, 20 * L}) {
            const int outStart = step + L + rel;
            const int inStart = outStart - L;
            const float outPeak = dawlab::peakAbs(out.getReadPointer(0) + outStart, win);
            const float inPeak = dawlab::peakAbs(in.getReadPointer(0) + inStart, win);
            std::printf("%+15d samples %12.2f %14.2f\n", rel, dawlab::gainToDb(outPeak / inPeak), dawlab::gainToDb(outPeak));
        }
        std::printf("Output peak %.2f dBFS (ceiling %.0f), safety clamp fired %llu times\n", dawlab::gainToDb(dawlab::peakAbs(out)),
                    ceilingDb, static_cast<unsigned long long>(limiter.getSafetyClipCount()));
        // the safety clamp pulls algorithm errors back to the ceiling, so we also require that it never fired
        ok = ok && dawlab::peakAbs(out) <= dawlab::dbToGain(ceilingDb) * (1.0f + 1.0e-6f) &&
             limiter.getSafetyClipCount() == 0;

        dawlab::AudioBuffer both(2, in.getNumSamples());
        both.copyFrom(0, 0, in, 0, 0, in.getNumSamples());
        both.copyFrom(1, 0, out, 0, 0, out.getNumSamples());
        dawlab::writeWav(dawlab::outputPath("l05_latency_burst.wav"), both, sr);
    }

    std::printf("\nWrote dawlab_output/l05_latency_impulse.wav and l05_latency_burst.wav\n");
    std::printf("%s\n", ok ? "Everything matches." : "[FAIL] Some item does not match!");
    return ok ? 0 : 1;
}
