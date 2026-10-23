# 第 00 章練習題

難度：★☆☆ 基本 / ★★☆ 中等 / ★★★ 進階。實作題（4～7）在 `starter/` 有可編譯的起始碼，留有 `// TODO`；未完成時執行會印出 `[FAIL]`，這是預期的。完成後所有 `DAWLAB_CHECK` 都要通過。解答在 [`../solutions/README.md`](../solutions/README.md)。

---

## 練習 1（★☆☆，計算）：dB 與振幅換算

不用計算機以外的工具，寫出算式與結果（取到小數點後兩位）。

1. 振幅 0.25 的正弦波是幾 dBFS？
2. -3 dB 對應的線性倍率是多少？+9.54 dB 呢？
3. 把訊號放大 3 倍是幾 dB？
4. 16-bit PCM 最小的非零 sample（1/32768）是幾 dBFS？24-bit（1/8388608）呢？這兩個數字和 2.3 節的 SNR 公式有什麼關係？
5. 兩個 RMS 都是 -12 dBFS 的訊號，(a) 同一個正弦波複製兩份、(b) 兩段不相關的雜訊（或兩個頻率不同的正弦波），相加後的 **RMS** 各是幾 dBFS？
6. 第 14 章的 DAW 有 8 軌。假設每軌 RMS 都是 -18 dBFS：(a) 8 軌互不相關，混起來的 RMS 是幾 dBFS？(b) 如果 8 軌其實是同一個訊號的複製，混起來是幾 dBFS？會發生什麼事？

提示：$20\log_{10}$、$10^{dB/20}$；相同訊號振幅相加，不相關訊號功率相加。

## 練習 2（★☆☆，計算 + 觀念）：latency 與 block size

1. block size 256 @ 44.1 kHz 的單程 latency 是幾 ms？128 @ 96 kHz 呢？
2. 想要單程 latency ≤ 3 ms @ 48 kHz，block size 最大可以是多少（限 2 的冪次）？對應的 latency 是幾 ms？
3. 512 @ 48 kHz，輸入一塊 + 輸出一塊的 round-trip 至少是幾 ms？為什麼實際 DAW 顯示的數字通常更大？
4. 一個 limiter 需要 5 ms 的 lookahead（第 05 章）。在 48 kHz 是幾個 sample？44.1 kHz 呢（四捨五入到整數）？
5. 觀念：用自己的話說明，為什麼 audio callback 裡不可以 (a) `new` 一個 `std::vector`、(b) 拿 `std::mutex`。各舉一個「會發生什麼事」的具體情境。
6. 觀念：為什麼本教材每支範例都用 block size 512 逐塊處理，而不是整段一次處理？這對「有狀態」的 DSP 元件（例如 filter）會抓出什麼 bug？

## 練習 3（★★☆，計算 + 觀念）：aliasing

1. 30 kHz @ 48 kHz 會 alias 到幾 Hz？（README 已算過，請自己再推一次。）
2. 25 kHz @ 44.1 kHz 會 alias 到幾 Hz？
3. 50 kHz @ 48 kHz 呢？（提示：$k$ 可以是任何整數。）
4. 一個 5 kHz 的音經過 distortion 產生了第 7 次諧波（35 kHz）。在 48 kHz 下它會變成幾 Hz？如果先 4 倍 oversampling 到 192 kHz 再做 distortion，這個諧波還會 alias 嗎？接下來要做什麼才能安全降回 48 kHz？
5. 觀念：為什麼 ADC 之前一定要有 anti-aliasing lowpass filter？為什麼 CD 選 44.1 kHz 而不是剛好 40 kHz？
6. 證明：30 kHz 與 18 kHz 的正弦波在 48 kHz 取樣後，sample 只差一個負號。（提示：$\sin(2\pi n - \theta) = -\sin\theta$。）

## 練習 4（★☆☆，實作）：normalize 與 clipping 計數

檔案：`starter/ex4_normalize.cpp`（target `l00_ex4_starter`），解答 `../solutions/ex4_normalize.cpp`（`l00_ex4_solution`）。

實作兩個函式：

- `int countClippedSamples(const AudioBuffer&, float threshold = 1.0f)`：所有 channel 中 $|x| \ge$ threshold 的 sample 數。
- `float normalizeToPeakDb(AudioBuffer&, float targetPeakDb)`：把整個 buffer 的 peak 調到 `targetPeakDb`，**所有 channel 乘同一個 gain**（不改變左右平衡），回傳套用的 gain（dB）。全靜音的 buffer 不動並回傳 0（不可以產生 NaN）。

self-check 會驗證：振幅 0.25 → -1 dBFS 要套 +11.04 dB；左右不等音量的立體聲 normalize 後左右比例不變；靜音 buffer 不產生 NaN；振幅 1.5 的 1 kHz 正弦波在 48 kHz 下 1 秒有 **26000** 個 sample 超過 ±1（想一想為什麼是 26000）；鼓組 loop normalize 到 -1 dBFS 並輸出 `l00_ex4_normalized.wav`。

提示：`dawlab::peakAbs`、`dawlab::dbToGain`、`dawlab::gainToDb`、`AudioBuffer::applyGain`。

## 練習 5（★★☆，實作）：stereo ↔ mono downmix / upmix（注意增益）

檔案：`starter/ex5_downmix.cpp`（`l00_ex5_starter`），解答 `../solutions/ex5_downmix.cpp`（`l00_ex5_solution`）。

- `AudioBuffer downmixToMono(const AudioBuffer& in, float channelGain)`：$\text{mono}[i] = g \sum_{ch} \text{in}[ch][i]$。
- `AudioBuffer upmixToStereo(const AudioBuffer& mono, int numChannels = 2)`：把 channel 0 複製到每個 channel。

self-check 驗證 `channelGain` 的兩種常見選擇在不同素材上的結果：

| 素材 | gain 0.5（-6 dB） | gain 0.707（-3 dB） |
|---|---|---|
| 左右相同（正弦波） | 0 dB（不變） | +3.01 dB |
| 左右不相關（白雜訊） | -3.01 dB | 0 dB（不變） |
| 左右反相（L = -R） | 完全消失 | 完全消失 |
| 左右相同且 full-scale | 不 clip（剛好 1.0） | 超過 1.0（1.414） |

註：gain 0.5 在**任何**素材下都保證不 clip（$|0.5(L+R)| \le 0.5(|L|+|R|) \le 1$）；gain 0.707 只有在左右夠相關（同號）而且夠大時才會超過 1.0，左右反相或不相關的 full-scale 素材不一定會超過。

另外 stereo → mono → stereo 對左右相同的素材要是無損的。輸出 `l00_ex5_mono.wav`、`l00_ex5_stereo_again.wav`。

思考：為什麼多數 DAW 的 mono 按鈕用 -6 dB 而不是 -3 dB？什麼情況下你會選 -3 dB？

## 練習 6（★★☆，實作）：linear vs equal-power fade 與 crossfade

檔案：`starter/ex6_fade.cpp`（`l00_ex6_starter`），解答 `../solutions/ex6_fade.cpp`（`l00_ex6_solution`）。

實作三個函式：

- `float fadeGain(float t, FadeShape shape)`：$t \in [0,1]$ 的 fade-in 增益。Linear：$g = t$；EqualPower：$g = \sin(t\pi/2)$。
- `void applyFadeIn(AudioBuffer&, int numSamples, FadeShape)`：開頭 `numSamples` 個 sample 乘上 $g(i/(n-1))$。
- `void applyFadeOut(AudioBuffer&, int numSamples, FadeShape)`：結尾 `numSamples` 個 sample 乘上 $g(1 - i/(n-1))$。

起始碼已提供 `crossfade()`（a fade-out + b fade-in 相加）與 self-check。驗證重點：

- equal-power 任何位置都滿足 $g_{in}^2 + g_{out}^2 = 1$（功率守恆）；linear 在中點只有 0.5。
- 兩段**不相關**的雜訊 crossfade：linear 中段掉 **-3.01 dB**（聽得到的「凹陷」），equal-power 中段 **0 dB**。
- 兩段**相同**的訊號 crossfade：linear 剛好 0 dB（0.5 + 0.5 = 1），equal-power 反而 **+3.01 dB**。

輸出 `l00_ex6_crossfade_linear.wav`、`l00_ex6_crossfade_equal_power.wav`，用 Audacity 看波形包絡的差別。

思考：DAW 的 crossfade 預設通常是 equal-power，但「同一段錄音剪接點」的 crossfade 常用 linear——為什麼？

## 練習 7（★☆☆，實作）：interleave / deinterleave

檔案：`starter/ex7_interleave.cpp`（`l00_ex7_starter`），解答 `../solutions/ex7_interleave.cpp`（`l00_ex7_solution`）。

- `std::vector<float> interleave(const AudioBuffer&)`：`out[frame * numChannels + ch] = buffer[ch][frame]`。
- `AudioBuffer deinterleave(const float* data, int numChannels, int numFrames)`：反向。

self-check 用「看得出位置」的數字驗證（L = 0 1 2 3、R = 100 101 102 103 → 0 100 1 101 2 102 3 103）、單聲道、6 聲道，以及立體聲鼓組來回一次要 bit-exact。

注意 `/W4`：index 計算用 `size_t`，避免 signed/unsigned 比較警告。

思考：為什麼 DSP 用 non-interleaved，而 driver / 檔案用 interleaved？在第 14 章的 DAW 裡，這個轉換會發生在哪一層？
