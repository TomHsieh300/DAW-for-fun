// 範例 3：latency——impulse 通過 limiter 會晚多久出來？gain 真的「提前」下降嗎？
//
// 示範什麼：
//   (1) 把一個 impulse 送進 limiter，輸出的 impulse 會晚 getLatencySamples() 個 sample 出現，
//       而且這個數字 = round(lookaheadMs × sampleRate / 1000)，在 44.1k / 48k / 96k 都成立。
//   (2) 把輸出往前移 latency 個 sample、跟輸入相減（null test）：低於 ceiling 的訊號應該完全抵消，
//       證明 limiter 在不需要動作時是「透明」的，只是晚了一點。
//   (3) 觀察 gain 曲線：訊號在第 s 個 sample 突然變大，但 gain 從第 s - L 個 sample 就開始線性下降，
//       在大聲的部分抵達輸出時剛好降到位——這就是 lookahead 的意義。
//
// 怎麼執行：l05_latency_impulse（不需要參數）
//
// 輸出檔案（在 dawlab_output/）：
//   l05_latency_impulse.wav   impulse 的輸出（48 kHz、lookahead 5 ms：第 240 個 sample 才有值）
//   l05_latency_burst.wav     左聲道 = 輸入 tone burst、右聲道 = limiter 輸出，拉進 DAW 看波形可以直接比對延遲與 gain ramp
//
// 該聽到：impulse 本身只是「喀」一聲；重點是看印出的表格與 WAV 的波形。

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

    // ---- (1) impulse 在不同 sample rate / lookahead 下的延遲 ----
    std::printf("(1) impulse 的輸出位置 vs getLatencySamples()\n");
    std::printf("%10s %12s %18s %16s %10s\n", "sampleRate", "lookahead", "getLatencySamples", "impulse 出現在", "一致?");
    for (double sr : {44100.0, 48000.0, 96000.0}) {
        for (float lookaheadMs : {1.5f, 5.0f, 10.0f}) {
            dawlab::AudioBuffer impulse = dawlab::makeImpulse(4096, 1, 0, 0.5f); // 0.5 < ceiling，不會被壓
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

    // ---- (2) null test：低於 ceiling 的訊號，補償 latency 後應完全抵消 ----
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
        std::printf("\n(2) null test（pluck 序列 peak %.2f dBFS < ceiling -1 dBFS）：補償 %d samples 後殘差 = %.3g（應為 0）\n",
                    dawlab::gainToDb(dawlab::peakAbs(in)), L, residual);
        ok = ok && residual == 0.0f && limiter.getSafetyClipCount() == 0;
    }

    // ---- (3) gain 提前下降：tone burst -30 → 0 dBFS，ceiling -6 dBFS ----
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
        const int step = dawlab::secondsToSamples(0.25, sr); // 輸入在這裡變大；在輸出端是 step + L

        // gain = 輸出 / 延遲後的輸入；取每 1 ms 內的最大 |輸出| 除以最大 |輸入| 比較穩
        std::printf("\n(3) gain ramp：輸入在 sample %d 變大 30 dB；輸出端對應位置是 %d（= %d + L=%d）\n", step,
                    step + L, step, L);
        std::printf("%22s %12s %12s\n", "輸出位置相對 step+L", "gain (dB)", "輸出 (dBFS)");
        const int win = dawlab::msToSamples(0.5, sr);
        for (int rel : {-2 * L, -L, -3 * L / 4, -L / 2, -L / 4, 0, L / 2, L, 2 * L, 20 * L}) {
            const int outStart = step + L + rel;
            const int inStart = outStart - L;
            const float outPeak = dawlab::peakAbs(out.getReadPointer(0) + outStart, win);
            const float inPeak = dawlab::peakAbs(in.getReadPointer(0) + inStart, win);
            std::printf("%+10d samples %12.2f %12.2f\n", rel, dawlab::gainToDb(outPeak / inPeak), dawlab::gainToDb(outPeak));
        }
        std::printf("輸出 peak %.2f dBFS（ceiling %.0f），safety clamp %llu 次\n", dawlab::gainToDb(dawlab::peakAbs(out)),
                    ceilingDb, static_cast<unsigned long long>(limiter.getSafetyClipCount()));
        // safety clamp 會把演算法的錯誤夾回 ceiling，所以也要求它沒觸發
        ok = ok && dawlab::peakAbs(out) <= dawlab::dbToGain(ceilingDb) * (1.0f + 1.0e-6f) &&
             limiter.getSafetyClipCount() == 0;

        dawlab::AudioBuffer both(2, in.getNumSamples());
        both.copyFrom(0, 0, in, 0, 0, in.getNumSamples());
        both.copyFrom(1, 0, out, 0, 0, out.getNumSamples());
        dawlab::writeWav(dawlab::outputPath("l05_latency_burst.wav"), both, sr);
    }

    std::printf("\n已輸出 dawlab_output/l05_latency_impulse.wav、l05_latency_burst.wav\n");
    std::printf("%s\n", ok ? "全部一致。" : "[FAIL] 有項目不一致！");
    return ok ? 0 : 1;
}
