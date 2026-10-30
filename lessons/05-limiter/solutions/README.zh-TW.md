# 第 05 章 練習解答：Limiter

[English](README.md) | **繁體中文**

執行解答（在專案根目錄）：

```powershell
.\scripts\build.ps1 -Only 05-limiter -TestLabel solution     # 跑全部解答的 self-check
cd build
.\bin\Debug\l05_ex4_solution.exe                              # 或單獨執行某一題
```

---

## 練習 1（觀念）Limiter 與 Compressor、Clipper 的差別

**1. Limiter 是 ratio → ∞、attack → 0 的 compressor。**
ratio → ∞ 表示 threshold 以上的輸入不管多大，輸出都停在 threshold（靜態曲線變成水平線）；attack → 0 表示 gain 必須「立刻」降到位。第 04 章的 compressor 就算 ratio 設 100:1，它的 attack 是一個 one-pole 平滑器：gain reduction 從 0 開始、以 $`1 - e^{-t/\tau}`$ 的曲線逼近目標值。訊號剛變大的那一瞬間 gain reduction 還是 0，整個峰值原封不動地通過。`l05_compressor_vs_limiter` 的實測：輸入 -30 → -6 dBFS、threshold -20 dB、ratio 100:1、attack 1 ms，變大後的第 1 毫秒輸出 peak 是 **-7.99 dBFS**（超過 threshold 12 dB），第 2 毫秒 -15.54，第 3 毫秒 -18.27，要到 5 ms 之後才真的壓到 -19.8 dB 附近。這就是 overshoot。那 attack 設 0 呢？這個 feedforward compressor 的 detector 與 gain 作用在同一個 sample，所以不會再有 overshoot，輸出只比 threshold 高 $`(x-T)/R`$（這裡 $`14/100 = 0.14`$ dB；ratio → ∞ 時才剛好等於 threshold），但 gain 會在一個 sample 內跳變（click），release 再短一點就會跟著波形每個週期上下抖動，等於變成 clipper、產生諧波失真。「gain 平滑」與「不超過」要同時成立，gain 就必須在峰值抵達之前開始下降——這需要看見未來，也就是 lookahead。

**2. Brickwall 需要「提前知道未來」。**
要讓 sample $`x[n]`$ 乘上 gain 之後 ≤ ceiling，gain 必須在 $`x[n]`$ 抵達輸出「之前」就算好、而且已經降到 $`\le \text{ceiling}/|x[n]|`$。沒有延遲的系統在時間 $`n`$ 只看得到 $`x[0..n]`$，想要 gain 平滑（不是每個 sample 跳一次）就一定來不及；唯一的零延遲解是「每個 sample 各自乘上自己需要的 gain」$`y[n] = x[n]\cdot\min(1, c/|x[n]|)`$——這恰好就是 hard clipper。所以 brickwall + 平滑 gain ⇒ 必須把音訊延後 L 個 sample（lookahead），讓 gain 有 L 個 sample 的時間下降。

**3. 代價。**
Clipper：零延遲、無狀態、實作最簡單，但它改變「波形」→ 產生諧波失真（練習 5 量到 THD 約 21 %）；在持續的訊號（bass、pad）上很難聽。Limiter：保持波形、只改增益，穩態正弦波進去還是正弦波（THD ≈ 0），但要付出 latency、較多 CPU、以及 release 造成的 pumping / transient 變鈍。選 clipper 的場合：需要零延遲（live 監聽、硬體 DSP）、只需要削掉極短的 transient 尖峰（鼓的 inter-sample 尖峰，很多 mastering 工程師會在 limiter 前放一個 soft clipper 削 1–2 dB）、或者本來就要那種「破」的音色（第 06、07 章）。

## 練習 2（計算）Lookahead、latency 與 gain reduction

**1.** `round(ms × fs / 1000)`：

<div align="center">

| lookahead | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| 1.5 ms | 66.15 → **66** | **72** | **144** |
| 5 ms | 220.5 → **221** | **240** | **480** |
| 10 ms | **441** | **480** | **960** |

</div>

`dawlab::msToSamples` 用 `std::lround`，0.5 進位（離零遠的方向），所以 220.5 → 221。`l05_latency_impulse` 的表格印出的 `getLatencySamples` 與 impulse 實際出現的位置都是這些數字。

**2.** 每個 limiter 回報 240 samples；串聯的 latency 相加 = 480 samples = **10 ms**。host 會把其他所有不經過這條 chain 的路徑延後 480 samples（見練習 7）。

**3.** 平均 L + 1 = **241** 個值。一個 step 經過長度 241 的 box filter 會變成長度 241 個 sample 的線性 ramp = 241 / 48000 = **5.02 ms**。`l05_latency_impulse` 第 (3) 段的表格可以看到 gain 在 step 前 240 個 sample 開始下降（-0.12 dB）、在 step 當下剛好到 -6.00 dB。

**4.** 推 +12 dB 後 peak = -2.92 + 12 = +9.08 dBFS；要壓到 -1 dBFS 需要 9.08 − (−1) = **10.08 dB** 的 gain reduction。範例輸出的 `max GR` 欄位正是 10.08 dB（因為 lookahead 的 gain 在峰值抵達時「剛好」到位，不多不少）。

**5.** -1 dBTP 的線性值 = $`10^{-1/20} = 0.891`$。fs/4 的正弦波每個週期只有 4 個 sample，相位偏 45° 時 sample 落在 $`\pm A\sin 45° = \pm 0.707A`$，sample peak 比真正的峰值低 $20\log_{10}(1/\sqrt2) = $ **3.01 dB**。練習 4 的 meter 量到的差距是 2.85 dB 而不是 3.01 dB，原因不是濾波器在 fs/4 有衰減（每個 phase 在 fs/4 的增益都在 0.00～+0.01 dB 之內），而是偶數長度（64 tap）濾波器的插值點落在 1/8 sample 的格點上，碰不到正中間的峰值，見練習 4 的解說。

## 練習 3（實作）Sliding window minimum

解答：[`ex3_sliding_window_min.cpp`](ex3_sliding_window_min.cpp)

關鍵思路——deque 內只留「還有機會成為最小值」的 (value, index)：

1. 新值 $`v`$ 進來，尾端所有 value **≥** $`v`$ 的元素丟掉。理由：它們比 $`v`$ 更早過期、又不比 $`v`$ 小，在 $`v`$ 還在視窗裡的任何時刻都不可能贏過 $`v`$。用 `>=` 而不是 `>` 是因為相等的舊值也同樣沒有用處（丟掉它還能讓 deque 更短；正確性兩者都對）。
2. 前端 index ≤ n − W 的元素丟掉（已離開視窗）。因為 deque 的 index 遞增，只需要檢查最前面。
3. $`(v, n)`$ 放到尾端。此時 deque 內最多 W − 1 個元素（index 都在 (n − W, n) 內），所以長度 W 的環形陣列一定放得下。
4. 前端就是答案。

每個元素最多 push 一次、pop 一次 → 總工作量 O(N)。解答跑出來 9 種視窗長度、含大量重複值的隨機資料與階梯訊號都是 0 mismatch；deque 版快數十倍，而且視窗越長差距越大（Debug 建置的實測：視窗 241 約 20–30 倍、961 約 70–100 倍；確切倍率與絕對時間依機器與負載而異，Release 下差距更大）。`dawlab::dsp::SlidingWindowMin`（在 `Limiter.h`）就是同一個演算法。

## 練習 4（實作）4x oversampling true-peak meter

解答：[`ex4_true_peak_meter.cpp`](ex4_true_peak_meter.cpp)

**濾波器設計。** 升頻 4 倍 = 零插入 + lowpass。原型在新取樣率 $`4f_s`$ 下 cutoff 是 $`f_s/2`$，也就是新取樣率的 1/8，windowed sinc：

```math
h[k] = \mathrm{sinc}\!\Bigl(\frac{k-M}{4}\Bigr)\, w[k],\qquad M = \frac{N-1}{2},\quad N = 64,\quad w = \text{Hann}
```

零插入會讓平均能量掉成 1/4，所以把 $`\sum h = 4`$（每個 phase 的 DC gain ≈ 1）。

**Polyphase。** 升頻後的 $`y[4n+p] = \sum_k h[k]\,u[4n+p-k]`$，而零插入的 $`u`$ 只在 4 的倍數非零，所以只剩 $`y[4n+p] = \sum_j h[4j+p]\,x[n-j]`$：每個輸入 sample 做 4 個長度 16 的內積，不需要真的做出 4 倍長的 buffer，也不需要任何 allocate。（零插入為什麼會產生 image、為什麼要乘 4、為什麼 $`h[n] = \mathrm{sinc}(n/4)`$，見 README 2.8 節「升頻 4 倍怎麼做？」。）

**先驗證濾波器，再跑訊號。** self-check (0) 不跑任何訊號，只檢查係數：$`\sum h = 4.0000`$；四個 phase 的總和是 0.9999、1.0001、1.0001、0.9999（每個 phase 的 DC gain ≈ 1：輸入是常數時，4 個插值點都要等於那個常數；不是剛好 1，是因為只取 64 tap 再乘 window 之後已經不是理想 lowpass，image 在 DC 對應的頻率 $`f_s`$、$`2f_s`$ 上沒有被完全消掉）；$`h[k] = h[63-k]`$ 完全對稱（線性相位）。TODO 1 寫錯時（例如忘了正規化、或 phase 拆成 `coeffs_[k / 16][k % 16]`）這一段就會先 FAIL，不必等到 true peak 的數字不對才回頭找。

**實測（`l05_ex4_solution`）：**

<div align="center">

| 訊號 | sample peak | true peak | 差 |
|---|---|---|---|
| 1 kHz sine, amp 0.5 | -6.02 dBFS | -6.02 dBTP | 0.00 |
| 12 kHz (fs/4) sine, phase 45°（暖機後） | -9.03 dBFS | -6.18 dBTP | +2.85 dB |
| drums +12 dB, hard clip 到 0 dBFS | 0.00 dBFS | +4.54 dBTP | +4.54 dB |
| drums +12 dB → Limiter (ceiling -1) | -1.00 dBFS | +1.02 dBTP | +2.02 dB |

</div>

**fs/4 那一列為什麼是 -6.18 而不是 -6.02？** 原型長度 $`N = 64`$ 是偶數，中心 $`M = 31.5`$ 不在整數上。換算回原始取樣率，phase $`p`$ 的延遲是 $`(M - p)/4 = 7.875 - p/4`$ 個 sample，小數部分是 0.875、0.625、0.375、0.125——也就是說 4 個插值點落在兩個原始 sample 之間的 1/8、3/8、5/8、7/8 處，而不是 0、1/4、1/2、3/4。這個訊號的峰值剛好在兩個 sample 正中間（1/2），最近的插值點離它 1/8 sample（= 11.25°），量到的是 $`0.5\cos 11.25° = 0.4904`$，也就是 -6.19 dBTP（濾波器在 fs/4 附近有 +0.01 dB 的漣波，所以印出 -6.18）。這是 4x oversampling meter 的固有誤差：最多低估約 0.17 dB。BS.1770-4 的 48-tap 濾波器同樣是偶數長度的對稱設計，也有同樣的性質；想讓某一個 phase 剛好對齊原始 sample，可以改用奇數長度的原型（例如 63 tap，最後補一個 0）。

量這一列時，`measure()` 會先餵 `kTapsPerPhase` 個 sample 讓濾波器暖機再開始記錄 peak（`resetPeaks()`）。不跳過的話，訊號「從 0 突然開始」的那一下會讓插值濾波器產生 onset 暫態（Gibbs ringing），量到 -6.07 dBTP——那是開頭暫態的峰值，不是正弦波穩態的 true peak。

hard clip 的平頂與 limiter 壓住的高頻（hi-hat 是接近 Nyquist 的雜訊）重建之後都會明顯超過 sample peak。想保證 -1 dBTP，這個簡易 sample-peak limiter 的 ceiling 要再往下約 2 dB（解答最後一行印出建議值：約 -3.02 dBFS），或者改用 true-peak 偵測（在 limiter 的 sidechain 裡升頻後再算 gain）。BS.1770-4 的正式濾波器是 48 tap（4 phase × 12）的設計，數值會和這個簡易版略有差異。

## 練習 5（實作）Soft clipper vs limiter

解答：[`ex5_soft_clipper.cpp`](ex5_soft_clipper.cpp)

```cpp
float softClip(float x, float threshold) noexcept {
    const float t = std::clamp(threshold, 1.0e-3f, 0.999f);
    const float a = std::abs(x);
    if (a <= t) return x;
    const float over = (a - t) / (1.0f - t);
    return std::copysign(t + (1.0f - t) * std::tanh(over), x);
}
```

$`\tanh'(0) = 1`$，所以曲線在 $`t`$ 處不但連續、斜率也連續（解答用數值微分驗證兩側斜率都 ≈ 1）；$`\tanh < 1`$ 所以 $`|y| < t + (1-t) = 1`$（float 下極大的輸入會飽和到剛好 1.0）。

**實測：** 1 kHz +6 dBFS 的正弦波、$`t = 0.5`$：clipper 輸出 peak -0.02 dBFS、THD **20.9 %**，諧波 H3 -13.8 dB、H5 -27.6 dB，而 H2、H4 都是 -100 dB（量不到）——因為曲線是奇對稱 $`f(-x) = -f(x)`$，奇函數只會產生奇次諧波。Limiter（ceiling -0.5 dB）輸出 THD **0.0000 %**：穩態下視窗最小值是常數 $`c/A`$，移動平均也是常數，整段正弦波乘上同一個數，波形完全不變。

Drum loop +12 dB：clipper (t = 0.6) peak -0.00 dBFS、RMS -7.75 dBFS；limiter (ceiling -0.1) peak -0.10 dBFS、RMS -9.75 dBFS（dry 是 -17.43）。clipper 的 RMS 更高（它把 transient 整個削平，而不是把整段音量拉下來），代價是 kick / snare 上聽得到「沙沙」的諧波；limiter 乾淨得多，但 transient 變鈍、有輕微 pumping。

試聽檔（在 `dawlab_output/`）：`l05_ex5_sine_clipped.wav` / `l05_ex5_sine_limited.wav`（1 kHz 正弦波：clipped 明顯多了刺耳的高次泛音，limited 只是變小聲）、`l05_ex5_drums_clipped.wav` / `l05_ex5_drums_limited.wav`。練習 4 另外輸出 `l05_ex4_intersample.wav`（fs/4、相位 45° 的正弦波，可以用 DAW 的 true-peak meter 對照 sample peak 與 true peak）。

## 練習 6（實作）Fuzz test

解答：[`ex6_fuzz_test.cpp`](ex6_fuzz_test.cpp)

結構：一個可重現的 `Random`（包 `NoiseGenerator`，固定 seed，失敗時才能重跑同一個 case）、`makeRandomSignal()` 產生 7 種訊號、`processRandomBlocks()` 用 1..maxBlock 的隨機 block 長度餵 limiter。每個 case 檢查三件事：peak ≤ ceiling（線性值、精確比較）、沒有 NaN / Inf、`getSafetyClipCount() == 0`。最後一項很重要：Limiter 最後有一道 safety clamp，就算演算法寫錯也會把輸出夾回 ceiling，所以單看 peak 會漏掉 bug；clip count 才是「演算法本身沒超過」的證據。

實測（`l05_ex6_solution`）：

- (1) 400 個隨機 case：0 失敗；最小 margin（ceiling − 輸出 peak）= -0.0000 dB，表示確實有 case 頂到 ceiling（而且在浮點捨入等級內沒有超過）。`getLatencySamples()` 全部等於 `clamp(round(lookaheadMs·fs/1000), 0, round(maxLookaheadMs·fs/1000))`。
- (2) 每個 block 之間隨機改 release 與 input gain：50 case、0 失敗。2.5 節的證明只用到「$`gr[n] \le g[n]`$」，release 怎麼變都成立；input gain 是在 delay line「之前」乘上去、peak 也是用乘完的值算的，所以也不影響。
- (3) 播放中把 ceiling 從 -1 調到 -12 dB：後半段 peak -12.00 dBFS，但 safety clamp 觸發 471 次。原因：delay line 裡還有 L 個已經用「舊 ceiling」算好 gain 的 sample，它們出來時會超過新 ceiling，只能靠 clamp 兜底（會有 clip 失真）。這就是為什麼 ceiling 要用平滑或在停止播放時改——或在 setter 裡只允許 ceiling 以 ramp 的方式下降。
- (4) 塞入 NaN / ±Inf：輸出 peak -6.02 dBFS、沒有 NaN / Inf。`std::max(peak, NaN)` 會忽略 NaN；Inf 讓 $`g = 0`$；乘出來的 NaN 在 clamp 那一行被 `!(|y| <= ceiling)` 攔住並換成 0。
- (5) 模擬 plugin host 每個 block 都呼叫一次 `setInputGainDb(6)`：25 ms 後輸出 = 0.199526（= 0.1 × $`10^{6/20}`$），表示 20 ms 的 ramp 確實走完。若 setter 每次都依「剩下的距離」重算 step，ramp 會變成越來越慢的指數曲線，這一項就會失敗——這是本章在驗證時真的抓到並修掉的 bug。

## 練習 7（觀念）DAW 如何補償 latency

**1.** vocal 軌的聲音會比 guitar 軌晚 240 samples（5 ms）出來——聽起來是 vocal 微微「拖拍」，若兩軌有相關的內容（例如同一來源的兩支麥克風）還會有 comb filtering。PDC 的做法是：找出所有路徑中最大的 latency（240），然後對 **latency 比較小的軌（guitar，0）** 插入 240 − 0 = 240 samples 的延遲，讓所有軌到達 mix bus 時都一樣晚。總延遲變成 240 samples，但各軌彼此對齊。

**2.** master bus 是所有軌都要經過的地方，所以不需要延遲任何一軌（大家都一起晚 240 samples）。但「播放頭」要修正：engine 的 transport 正在算位置 $`t`$ 時，master 輸出的其實是 $`t - 240`$ samples 的內容；這個 block 交給音訊裝置之後，還要再經過裝置自己的輸出 latency（driver 與 DA converter 的 buffer）才從喇叭出來。所以畫面上的游標要畫在 `transport position − PDC latency − 裝置輸出 latency` 的位置（只在播放中扣；停止時沒有聲音，直接顯示 transport position），metering 與 automation 的對齊也要考慮這個偏移。第 14 章分兩部分實作：engine 負責 PDC 並用 `Engine::getLatencySamples()` 回報 mix latency（[Step 8](../../14-daw-8track/docs/step-08-pdc.zh-TW.md)）；app 的 TransportBar 計數器與 ArrangementView 播放游標顯示 `AudioHost::getAudiblePosition()`，也就是 $`\max(0,\ \text{position} - \text{engine latency} - \text{裝置輸出 latency})`$（[Step 13](../../14-daw-8track/docs/step-13-transport-bar.zh-TW.md)、[Step 14](../../14-daw-8track/docs/step-14-arrangement.zh-TW.md)）。

**3.** 即時監聽的 latency 是「歌手聽到自己聲音的延遲」，lookahead 會直接加在上面。5 ms 單看不大，但加上 audio interface 的 buffer（例如 128 samples × 2 ≈ 5.3 ms）、driver、AD/DA，很容易超過 10 ms，歌手會覺得自己「聽到回音」、音準與節奏都會受影響。監聽路徑上應該用零 latency 的處理（或硬體 direct monitoring），有 lookahead 的 limiter 等到 mix 階段再加。

**4.** `setLookaheadMs()` 改變了 `getLatencySamples()`，plugin 要呼叫 host 的通知機制（JUCE：`AudioProcessor::setLatencySamples(n)`，host 會收到 latency changed 的事件），host 收到後要重新計算整個 session 的 PDC、重新配置延遲 buffer——這會打斷播放、甚至短暫靜音，所以大多數 plugin 把 lookahead 設計成「停止播放才生效」或只在 `prepareToPlay` 回報固定的最大 lookahead（永遠延遲 max，lookahead 較短時只是 gain 提前更多）。本章的 `Limiter` 用的是前者：`setLookaheadMs()` 會重設視窗狀態，本章 README 也建議在停止播放時改。

**5.** 同一軌串聯：240 + 64 = 304 samples。另一軌插入 304 samples 的延遲。
