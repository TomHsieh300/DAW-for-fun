# 第 00 章練習解答

[English](README.md) | **繁體中文**

---

## 練習 1：dB 與振幅換算

1. $`20\log_{10}(0.25) = 20 \times (-0.602) = \mathbf{-12.04\ dBFS}`$。（0.25 = 0.5 × 0.5，而 0.5 是 -6.02 dB，兩次就是 -12.04。）
2. $`10^{-3/20} = 10^{-0.15} = \mathbf{0.708}`$（≈ $`1/\sqrt2`$）。$`10^{9.54/20} = 10^{0.477} = \mathbf{3.00}`$。
3. $`20\log_{10}(3) = \mathbf{+9.54\ dB}`$。（記法：×2 = 6.02、×3 = 9.54、×4 = 12.04、×10 = 20。）
4. 16 bit：$`20\log_{10}(1/32768) = -20 \times 15 \times 0.301 = \mathbf{-90.31\ dBFS}`$。24 bit：$`20\log_{10}(1/8388608) = -20 \times 23 \times 0.301 = \mathbf{-138.47\ dBFS}`$。
   這就是 $`6.02 \times (N-1)`$：一個 N-bit 有號整數的最小刻度是 $`2^{-(N-1)}`$。SNR 公式 $`6.02N + 1.76`$ 比它多出 $`6.02 + 1.76 = 7.78`$ dB，是因為 (a) 量化誤差的 RMS 是 $`\Delta/\sqrt{12}`$，比 $`\Delta`$ 小 10.79 dB，(b) 正弦波 RMS 比 peak 低 3.01 dB；$`10.79 - 3.01 = 7.78`$。
5. (a) 相同：振幅 ×2 → RMS +6.02 dB → RMS $`= -12 + 6.02 = \mathbf{-5.98\ dBFS}`$（≈ -6）。因為每個 sample 都剛好 ×2，peak 也同樣 +6.02 dB。
   (b) 不相關：功率 ×2 → RMS +3.01 dB → RMS $`= -12 + 3.01 = \mathbf{-8.99\ dBFS}`$（≈ -9）。注意這只對 **RMS** 成立：不相關訊號的 peak 不能用 +3 dB 推算——兩個訊號的峰值可能剛好在某一刻同號疊在一起（最壞情況是兩個 peak 直接相加），也可能永遠錯開，要看實際波形。
   `l00_db_math` 實測：相同正弦波 +6.02 dB，不相關白雜訊 +3.04 dB（雜訊是隨機的，所以和理論值差 0.03 dB 很正常）。
6. (a) 8 軌不相關：功率 ×8 → $`10\log_{10}8 = +9.03`$ dB → $`-18 + 9.03 = \mathbf{-8.97\ dBFS}`$。
   (b) 8 軌相同：振幅 ×8 → $`20\log_{10}8 = +18.06`$ dB → $`-18 + 18.06 = \mathbf{+0.06\ dBFS}`$。RMS 已經超過 0 dBFS，peak 更高（正弦波 peak 比 RMS 高 3 dB，鼓更多）——在 float 內部不會壞，但一寫成整數 WAV 或送到 DAC 就會 clip。這就是為什麼 mix bus 要留 headroom，以及為什麼第 05 章要有 limiter。

## 練習 2：latency 與 block size

1. $`256 / 44100 = \mathbf{5.80\ ms}`$；$`128 / 96000 = \mathbf{1.33\ ms}`$。
2. $`0.003 \times 48000 = 144`$ samples；不超過 144 的最大 2 的冪次是 **128**，latency $`128/48000 = \mathbf{2.67\ ms}`$。（256 會是 5.33 ms，超過。）
3. 輸入一塊 + 輸出一塊 $`= 2 \times 10.67 = \mathbf{21.33\ ms}`$。實際更大是因為：driver 通常在硬體與 callback 之間再放一層或兩層 buffer（double / triple buffering）；ADC / DAC 本身的 filter 有幾十到幾百個 sample 的 group delay；USB 介面還有傳輸的封包延遲；plugin 可能回報自己的 latency（例如 limiter 的 lookahead），DAW 會做 latency compensation 把所有軌對齊。
4. $`0.005 \times 48000 = \mathbf{240}`$ samples；$`0.005 \times 44100 = 220.5 \to \mathbf{221}`$ samples（`msToSamples` 用 `std::lround`）。
5. (a) **Allocation**：`new` 會進入記憶體配置器，它內部有 lock（多 thread 共用 heap），而且當 heap 沒有夠大的空位時會向 OS 要新的 page（system call，可能觸發 page fault、甚至等待 swap）。情境：UI thread 剛好在配置一張大圖，audio thread 的 `new` 等它放開 heap lock；等了 15 ms，512 @ 48 kHz 的 10.67 ms 期限已過，使用者聽到一聲 click。
   (b) **Lock**：如果 audio thread 要拿的 mutex 正被 UI thread 持有，而 UI thread 又被 OS 排程換掉（它優先權低），audio thread 就只能等 UI thread 被排回來再放開——這叫 priority inversion，等待時間完全由 OS 決定。情境：使用者正在拖曳視窗，UI thread 被 Windows 暫停 30 ms；這段時間 audio thread 卡在 `mutex.lock()`，播放中斷。解法是 `std::atomic` 與 lock-free queue：不管另一個 thread 在做什麼，audio thread 永遠不用等。
6. 因為真實的 audio 系統就是這樣運作的：callback 每次只拿到一小塊，DSP 元件必須把「上一塊結束時的狀態」記住（filter 的前一個輸入 / 輸出、envelope 目前的值、delay line 的內容）。整段一次處理時，這些狀態自然連續，就算你每次 `process()` 都把狀態歸零也不會發現；分塊處理時，忘了保存狀態會在每個 block 邊界產生不連續——每 512 個 sample 一個 click（48 kHz 下約每 10.67 ms 一次，聽起來像 94 Hz 的嗡嗡聲）。`l00_block_processing` 的「分塊 vs 整段最大差異」就是抓這種 bug 的方法。

## 練習 3：aliasing

1. $`|30000 - 48000| = \mathbf{18000\ Hz}`$。
2. $`|25000 - 44100| = \mathbf{19100\ Hz}`$。
3. $`k = 1`$：$`|50000 - 48000| = \mathbf{2000\ Hz}`$（落在 $`[0, 24000]`$ 內，所以是這個）。$`k=0`$ 的 50000 與 $`k=2`$ 的 46000 都超過 Nyquist，不是答案。注意：50 kHz 剛好「繞過一整圈」再多 2 kHz，所以 alias 到很低的 2 kHz——這比 30 kHz → 18 kHz 更難聽，因為它落在聽覺最敏感的區域。
4. $`|35000 - 48000| = \mathbf{13000\ Hz}`$——跟 5 kHz 的諧波系列（5、10、15、20 kHz）完全無關，聽起來是「不和諧的金屬聲」。
   4 倍 oversampling 到 192 kHz 後 Nyquist 是 96 kHz，35 kHz 低於它，**不會 alias**。但降回 48 kHz 之前，必須先用 lowpass filter 把 24 kHz 以上的成分（35 kHz 這個諧波以及更高次的）濾掉，再每 4 個 sample 取 1 個；否則降取樣（decimation）本身就是一次「取樣」，一樣會 alias。這就是第 06 章 `Oversampler` 的 `downsample()` 要做的事。
5. 真實世界的訊號（麥克風收到的）含有超音波成分與高頻雜訊；如果不先濾掉，它們在 ADC 取樣時全部 alias 到可聽範圍，變成無法事後移除的雜音（alias 之後跟真正的 18 kHz 訊號在數位上完全無法分辨）。
   44.1 kHz 而不是 40 kHz：anti-aliasing filter 不可能在 20 kHz 完全通過、20.001 kHz 完全阻擋；它需要一段過渡帶（transition band）。20 kHz 到 22.05 kHz 這 2 kHz 就是留給 filter 的過渡帶。（44100 這個具體數字來自早期用錄影機儲存數位音訊的歷史：$`3 \times 490 \times 30 = 44100`$。）
6. 令 $`f_s = 48000`$，$`n`$ 為整數：
   ```math
   \sin\!\left(2\pi\frac{30000}{48000}n\right) = \sin\!\left(2\pi n \cdot \frac{48000 - 18000}{48000}\right) = \sin\!\left(2\pi n - 2\pi\frac{18000}{48000}n\right)
   ```
   因為 $`\sin(2\pi n - \theta) = \sin(-\theta) = -\sin\theta`$（$`2\pi n`$ 是整數圈），所以
   ```math
   = -\sin\!\left(2\pi\frac{18000}{48000}n\right)
   ```
   兩組 sample 只差正負號，振幅相同。負號只是 180° 的相位差，對單一正弦波聽不出來。`l00_db_math` 在 18 kHz 量到 0.5000 的振幅就是這個結果。

## 練習 4：normalize 與 clipping 計數

解答：[`ex4_normalize.cpp`](ex4_normalize.cpp)，執行 `l00_ex4_solution`。

**關鍵思路**：

- `countClippedSamples`：兩層迴圈，每個 channel 用 `getReadPointer(ch)` 拿到指標走過 `getNumSamples()` 個 sample，`std::abs(x[i]) >= threshold` 就 `++count`。
- `normalizeToPeakDb`：想要 $`\text{peak} \times g = 10^{target/20}`$，所以 $`g = \text{dbToGain(target)} / \text{peak}`$。用 `peakAbs(buffer)`（所有 channel 的最大值）而不是逐 channel 算，才能「所有 channel 乘同一個 gain」。`peak <= 0` 時直接回傳 0——否則會除以 0 得到 Inf，再乘回去變成 NaN。

**為什麼是 26000？** 1 kHz 在 48 kHz 下每週期 48 個 sample，相位間隔 7.5°。$`1.5|\sin\theta| \ge 1 \Leftrightarrow |\sin\theta| \ge 2/3 \Leftrightarrow \theta \in [41.8°, 138.2°]`$（及負半週）。落在這區間的相位是 $`45°, 52.5°, \ldots, 135°`$，共 13 個；負半週也 13 個，每週期 26 個；1 秒 1000 個週期 → 26000。比例 26/48 = 54.2%，接近連續理論值 $`1 - \frac{2}{\pi}\arcsin(2/3) = 53.5\%`$。

執行結果：

```
Sine 0.25 -> -1 dBFS: applied +11.04 dB, resulting peak = 0.8913 (-1.00 dBFS)
Stereo: left peak 1.000, right peak 0.500 (ratio 0.500, before normalizing 0.500)
Sine of amplitude 1.5: 26000 / 48000 samples exceed ±1.0 (54.2%)
Drum loop: peak -2.92 dBFS -> applied +1.92 dB -> -1.00 dBFS
[PASS] l00_ex4 (12 checks passed)
```

## 練習 5：stereo ↔ mono downmix / upmix

解答：[`ex5_downmix.cpp`](ex5_downmix.cpp)，執行 `l00_ex5_solution`。

**關鍵思路**：downmix 是「對每個 channel，`out[i] += gain * x[i]`」——`AudioBuffer` 建構時已清 0，所以可以直接累加。upmix 用 `AudioBuffer::copyFrom(ch, 0, mono, 0, 0, n)` 逐 channel 複製。

**增益的選擇**：這是 2.5 節「相同 vs 不相關」的直接應用。

- 左右相同時 $`L + R = 2L`$：乘 0.5 剛好還原（0 dB），乘 0.707 變 $`1.414L`$（+3.01 dB）。
- 左右不相關時功率相加：$`\text{RMS}(L+R) = \sqrt2 \cdot \text{RMS}(L)`$，乘 0.5 變 $`0.707`$（-3.01 dB），乘 0.707 剛好 1（0 dB）。
- 左右反相：$`L + (-L) = 0`$，不管乘多少都是 0。這就是「mono 相容性」問題——stereo widener、某些麥克風擺位會讓低頻在 mono 播放時消失。
- 乘 0.5：$`|0.5(L+R)| \le 0.5(|L|+|R|) \le 1`$，**永遠不 clip**。乘 0.707 在左右相同且接近 full-scale 時會超過 1。

執行結果：

```
Identical L/R:     gain 0.5 -> +0.00 dB, gain 0.707 -> +3.01 dB
Uncorrelated L/R:  gain 0.5 -> -3.03 dB, gain 0.707 -> -0.02 dB
Anti-phase L/R:    peak after downmix = 0 (complete cancellation)
stereo -> mono -> stereo: max difference 0
[PASS] l00_ex5 (12 checks passed)
```

**思考題**：DAW 的 mono 按鈕用 -6 dB 是因為「絕對安全」——任何素材都不會 clip，而且大部分混音的主要元素（主唱、kick、bass）本來就在正中間（左右相同），-6 dB 讓它們音量不變。-3 dB 適合「左右幾乎完全不相關」的素材（例如兩支相距很遠的 room mic），這時 -6 dB 會讓它聽起來小聲 3 dB。有些 DAW 讓使用者選（pan law 也有同樣的 -3 / -4.5 / -6 dB 選項，第 01 章 `Panner`）。

## 練習 6：linear vs equal-power fade

解答：[`ex6_fade.cpp`](ex6_fade.cpp)，執行 `l00_ex6_solution`。

**關鍵思路**：

- `fadeGain`：`switch (shape)`，Linear 回傳 `t`，EqualPower 回傳 `std::sin(t * float(kPi) * 0.5f)`。`kPi` 是 `double`，要 `static_cast<float>` 才不會有 `/W4` 的轉換警告。
- `applyFadeIn`：第 $`i`$ 個 sample 的 $`t = i/(n-1)`$，所以第 0 個是 0、第 $`n-1`$ 個剛好 1。`n <= 1` 時沒有定義（除以 0），直接 return。
- `applyFadeOut`：從 `total - n` 開始，用 $`1 - t`$ 代入。

**為什麼 equal-power 用 sin / cos？** fade-out 用 $`1-t`$ 代入 $`\sin(t\pi/2)`$ 得到 $`\sin((1-t)\pi/2) = \cos(t\pi/2)`$，而 $`\sin^2 + \cos^2 = 1`$：任何位置兩段的**功率**加起來都是 1。對不相關的素材功率相加，所以總功率守恆、聽起來音量不變。Linear 在中點 $`0.5^2 + 0.5^2 = 0.5`$ → -3.01 dB，聽得到凹陷。

反過來，對**相同**的素材是振幅相加：linear $`0.5 + 0.5 = 1`$ 剛好；equal-power $`0.707 + 0.707 = 1.414`$ → +3.01 dB。

執行結果：

```
fade-in curve  t=0 / 0.5 / 1: linear 0.000 / 0.500 / 1.000, equal-power 0.000 / 0.707 / 1.000
Uncorrelated material, crossfade mid RMS change: linear -3.02 dB, equal-power -0.03 dB
Identical material, crossfade mid RMS change: linear +0.00 dB, equal-power +3.00 dB
[PASS] l00_ex6 (20 checks passed)
```

**思考題**：兩段**不同**的素材（兩首歌、兩個 take）互不相關，要用 equal-power 才不會凹陷。但「同一段錄音的剪接點」兩邊的波形在接點附近幾乎是相同（相關）的訊號——這時 equal-power 會在中間凸起 +3 dB，linear 才是平的。所以好的 DAW 會讓你選 crossfade 的形狀。

## 練習 7：interleave / deinterleave

解答：[`ex7_interleave.cpp`](ex7_interleave.cpp)，執行 `l00_ex7_solution`。

**關鍵思路**：就是兩個 index 公式。interleaved 的位置 = `frame * numChannels + ch`：

```cpp
for (int ch = 0; ch < numChannels; ++ch) {
    const float* src = buffer.getReadPointer(ch);
    for (int i = 0; i < numFrames; ++i)
        out[size_t(i) * size_t(numChannels) + size_t(ch)] = src[i];
}
```

deinterleave 是同一個公式左右對調。全部用 `size_t` 計算 index，`/W4` 才不會抱怨 signed/unsigned。因為只是搬資料沒有運算，來回一次必須 bit-exact（最大差異 = 0）。

執行結果：

```
non-interleaved: L = [0 1 2 3], R = [100 101 102 103]
interleaved:     [ 0 100 1 101 2 102 3 103 ]
Drum loop (2 ch × 96000 frames), max difference after a round trip = 0
[PASS] l00_ex7 (504 checks passed)
```

**思考題**：non-interleaved 讓「處理一個 channel」是一個指標 + 一個連續迴圈，cache 友善、SIMD 容易（一次處理 4 或 8 個相鄰 sample），而且 mono 與 stereo 的 DSP 程式碼可以完全一樣（只是 channel 迴圈跑幾次）。driver 與檔案用 interleaved 是因為硬體 DAC 一次吃一個 frame（左右一起），串流時一個 frame 接一個 frame 最自然。在第 14 章的 DAW 裡，這個轉換發生在**audio device 層**：JUCE 的 `AudioIODeviceCallback` 已經把 driver 的 interleaved 資料轉成 `float**`（non-interleaved）交給你，所以 engine 與所有 DSP 元件只看得到 non-interleaved；而讀寫 WAV 時由 `WavFile.h`（或 JUCE 的 `AudioFormatReader`）負責轉換。
