# 第 11 章 解答

[English](README.md) | **繁體中文**

實作題的完整程式在本資料夾，每支都有 `DAWLAB_CHECK` self-check，`ctest -L solution` 會自動跑：

<div align="center">

| 題 | 檔案 | target |
|---|---|---|
| 3 | `ex3_true_peak.cpp` | `l11_ex3_solution` |
| 4 | `ex4_vu_meter.cpp` | `l11_ex4_solution` |
| 5 | `ex5_loudness_range.cpp` | `l11_ex5_solution` |
| 6 | `ex6_fft_convolution.cpp` | `l11_ex6_solution` |
| 7 | `ex7_correlation_meter.cpp` | `l11_ex7_solution` |

</div>

---

## 練習 1：bin 解析度與需要的 N

1. $`\Delta f = 48000 / 2048 = 23.4375`$ Hz；frame 長 $`2048 / 48000 = 42.67`$ ms；bin 100 = $`100 \times 23.4375 = 2343.75`$ Hz。
2. $`1000 / 23.4375 = 42.67`$ → 落在 bin 42 與 43 之間（`l11_window_leakage` 開頭第二行就印了這個數字）。
   要讓 $`k \cdot f_s / N = 1000`$，$`N = 48 k`$，最小的是 $`N = 48`$（bin 1）。
   限定 2 的冪次：$`N = 2^m`$ 時 $`k = 2^m / 48 = 2^{m-4}/3`$，永遠不是整數 → **48 kHz 下任何 2 的冪次 FFT
   都不可能讓 1 kHz 落在 bin 中心**。這就是為什麼實際量測幾乎一定有 leakage，window 是必需品。
3. 兩音相差 $`43.65 - 41.20 = 2.45`$ Hz，要相隔 2 個 bin → $`\Delta f \le 1.225`$ Hz →
   $`N \ge 48000 / 1.225 \approx 39184`$ → 取 $`N = 65536`$（$`2^{16}`$），bin 寬 0.732 Hz、frame 長 **1.365 s**。
   問題：一個 frame 涵蓋 1.4 秒，裡面可能有好幾下鼓，頻譜是這 1.4 秒的「平均」，完全看不出 transient
   發生在什麼時候。這就是**時間–頻率解析度的取捨**（uncertainty principle）：頻率越細、時間越糊。
   專業工具會在低頻用長 frame、高頻用短 frame（multi-resolution / constant-Q）。
4. $`44100 / 4096 = 10.77`$ Hz；$`4096 / 44100 = 92.9`$ ms。
5. hop $`= 48000 / 30 = 1600`$ samples；overlap $`= 1 - 1600/4096 = 60.9\%`$。
   （`SpectrumAnalyzer::setOverlap(0.61f)` 就會得到差不多的 hop。）

## 練習 2：為什麼要 window？leakage 是怎麼來的？

1. **DFT 只看得到 N 個 sample，並隱含地假設這 N 個 sample 會無限循環**。把 frame 的尾巴接回開頭時，
   如果正弦不是剛好整數個週期，接縫處會有一個跳躍（不連續）。這個人造的不連續在頻域需要很多頻率才能拼出來，
   於是能量「漏」到所有 bin。數學上：取 N 個 sample = 乘上一個 rectangular window；時域相乘 = 頻域卷積，
   所以真正的頻譜（一根線）被卷上 rectangular window 的頻譜（sinc 形狀，side lobe 只有 -13 dB、而且衰減很慢），
   線就變成「一座山加兩側長長的裙襬」。
2. 整數個週期時接縫是連續的，沒有不連續。從頻域看：正弦的頻率剛好在 bin 中心，而 sinc 的零點剛好也落在
   其他每一個 bin 的位置上，FFT 只在零點取樣，所以看起來「完全沒有 leakage」（第二張表 Rectangular 的
   bin 44 是 -200 dB）。但裙襬其實還在，只是剛好沒被取樣到；頻率稍微偏一點就全部出現（1000 Hz 偏 1/3 bin 時，
   rectangular 的 bin ±10 還有 -30 dB）。
3. window 讓 frame 兩端漸漸變成 0（消除接縫的跳躍），代價是把訊號整體縮小了：bin 中心的正弦讀數乘上
   $`\text{CG} = \frac{1}{N}\sum w[n]`$（coherent gain：Hann 0.5 = -6.02 dB、Hamming 0.54、Blackman-Harris 0.359）。
   補償：振幅 $`A = \dfrac{2\,|X[k]|}{N \cdot \text{CG}}`$（DC 與 Nyquist 不乘 2）。`SpectrumAnalyzer` 就是這樣讓
   full-scale 正弦讀 0 dBFS。
4. 找 -90 dB 的小訊號：需要 side lobe 夠低的 window → **Blackman-Harris**（最高 side lobe -92 dB；
   範例裡 bin±10 已經掉到 -99 dB，Hann 只有 -70 dB、rectangular -30 dB）。
   分辨 1000 / 1010 Hz：需要 main lobe 窄 → rectangular 或 Hann，而且 N 要夠大：相差 10 Hz，
   rectangular 至少要 1 個 bin 以上的距離 → $`N \ge 4800`$ → 8192（bin 5.86 Hz，相距 1.7 bin）；
   用 Hann 則建議 2 bin 以上 → 16384。這兩個需求互相衝突（side lobe 低 ⇔ main lobe 寬），沒有萬用的 window。
5. **不能**。zero-padding 沒有增加任何新的資訊（觀察時間還是 N 個 sample），它只是把同一條 DTFT 曲線取樣得更密，
   讓峰值的位置和高度看得更準（內插），但兩個靠太近、main lobe 已經融在一起的峰不會因此分開。
   `l11_window_leakage` 的最後一張表就是用 16 倍 zero-padding 把 window 的 DTFT 畫細，量 main lobe、side lobe 與
   scalloping loss（偏 0.5 bin 時的損失：Rectangular 3.92、Hann 1.42、Hamming 1.75、Blackman-Harris 0.82 dB）。
   zero-padding 讓這些數字「量得到」，但 main lobe 的寬度（以原本的 bin 為單位）完全沒變。

## 練習 3：重用 Oversampler 做 true-peak meter，並與第 05 章的 windowed-sinc 版比較

**TODO 1：重用。** `Oversampler::upsample()` 回傳一個 4 倍長的 `AudioBlock`，直接在上面找 `peakAbs`。不需要
`downsample`，因為 meter 只量測、不輸出（升頻後的訊號用完就丟，下一個 block 會覆寫同一塊內部 buffer）。

```cpp
const AudioBlock up = oversampler_.upsample(block);
for (int ch = 0; ch < nch; ++ch) {
    const float p = peakAbs(up.getChannelPointer(ch), up.getNumSamples());
    if (p > truePeak_[ch].load(std::memory_order_relaxed))
        truePeak_[ch].store(p, std::memory_order_relaxed);
}
```

**TODO 2：乘法數。** 第 $`i`$ 級的輸入取樣率是 $`f_s \cdot 2^{i-1}`$，每個輸入 sample 只算一次奇數 tap 的 FIR
（另一個輸出是純延遲，0 次乘法）：

```cpp
for (int i = 0; i < os.getNumStages(); ++i)
    total += os.getStage(i).getNumOddTaps() << i;   // 32·1 + 12·2 = 56
```

windowed-sinc 版是 4 個 phase × 16 tap = 64。

實際輸出（`l11_ex3_solution`，Debug 建置）：

<div align="center">

| 訊號 | sample peak | Oversampler | windowed-sinc | FFT 理想內插 |
|---|---|---|---|---|
| 1 kHz 0 dBFS | 0.000 dBFS | 0.000 dBTP | -0.003 dBTP | （0） |
| 12 kHz（fs/4）相位 45° | -3.010 dBFS | 0.000 dBTP | -0.157 dBTP | （0） |
| +12 dB 後 hard clip 的鼓組 | 0.000 dBFS | **+5.201 dBTP** | +4.589 dBTP | +5.295 dBTP |
| 同上，用 Oversampler 的讀數 normalize 到 -1 dBTP | -6.201 dBFS | -1.000 dBTP | | |
| pluck sequence | -5.289 dBFS | -2.923 dBTP | -3.388 dBTP | -2.172 dBTP |

</div>

相位掃描（fs/4 正弦，振幅 1，誤差 = 讀數 − 0 dBTP；節錄每 11.25° 一列）：

<div align="center">

| 相位 | 峰值在 sample 之間的位置 | Oversampler | windowed-sinc |
|---|---|---|---|
| 0° | 0 | +0.000 dB | -0.169 dB |
| 11.25° | 7/8 | -0.168 dB | +0.000 dB |
| 22.5° | 3/4 | +0.001 dB | -0.159 dB |
| 33.75° | 5/8 | -0.168 dB | +0.010 dB |
| 45° | 1/2 | +0.000 dB | -0.157 dB |

</div>

頻率響應（振幅 0.5 的正弦，頻率稍微偏離整數比，讓峰值掃過所有位置；誤差 = 讀數 − (-6.02)）：

<div align="center">

| 頻率 | 1 k | 10 k | 16 k | 18 k | 20 k | 21 k | 22 k |
|---|---|---|---|---|---|---|---|
| Oversampler | +0.001 | +0.000 | +0.000 | +0.001 | +0.000 | +0.000 | +0.000 |
| windowed-sinc | -0.000 | -0.002 | -0.002 | +0.092 | -0.081 | -0.229 | -0.430 |

</div>

CPU：乘法數 56 對 64；實際計時（10 s stereo 白雜訊，每個 channel-sample）Debug 約 130～150 ns 對 410～530 ns，
Release 約 20 ns 對 59 ns——windowed-sinc 慢 3 倍左右（確切數字依機器與負載而異）。

**問題 1：插值點的位置。** Oversampler 是兩級 2x halfband，每一級都有一個 phase 是純延遲（輸出就是輸入 sample），
所以 4x 的點落在原始 sample 之間的 0、1/4、1/2、3/4；64 tap 的 FIR 是偶數長度，中心在 31.5，點落在
1/8、3/8、5/8、7/8。峰值剛好在某個點上就量得準，落在兩個點正中間（離最近的點 1/8 sample）時最差：
fs/4 的正弦每個 sample 轉 90°，1/8 sample = 11.25°，讀到 $`\cos 11.25° = 0.981`$ 倍，也就是 -0.17 dB。
兩組點的間距都是 1/4 sample，所以**最差誤差一樣大**，只是發生在對方量得最準的相位。單看「fs/4、相位 45°」
這一個例子會以為 halfband 比較準，那只是剛好選到它的點上（第 05 章 README 2.8 節表格的 -6.18 dBTP 也是同一件事）。

**問題 2：Nyquist 附近。** 22 kHz 時第 1 級 halfband 單趟只有 0.928 倍（-0.65 dB），但它沒濾乾淨的 image 在
$`48 - 22 = 26`$ kHz、振幅 0.072 倍。halfband 的對稱性 $`|H(f)| + |H(f_s - f)| = 1`$ 讓兩者加起來剛好是 1：
峰值掃過所有位置時，總會碰到兩者同相疊加的那一刻，讀數就回到原本的振幅。這是「量峰值」才有的巧合——
若要的是乾淨的 4x 訊號（例如 saturation），那個 26 kHz 的 image 是雜質而不是幫手。
Hann windowed-sinc 沒有這種互補性，它的 transition band 又寬（64 tap 的 Hann 主瓣寬），18 kHz 起就有
±0.1 dB 的漣波、22 kHz 低估 0.43 dB。這也解釋了鼓組與撥弦的差異：hard clip 的諧波與撥弦的白雜訊激發都有大量
接近 Nyquist 的成分，windowed-sinc 比理想值低 0.7～1.2 dB，Oversampler 只低 0.1～0.75 dB。
（第 05 章練習 4 量到的 +4.54 dBTP 用的是 1 小節的鼓組，這一題是 2 小節；這一題把同一段訊號同時餵給兩種 meter，
比較才公平：+4.59 對 +5.20。）

**問題 3：乘法數 vs 實際速度。** 乘法數只差 14%，實際差 3 倍，差別在內迴圈怎麼寫：

- `HalfbandStage` 用「鏡像 ring buffer」（每個 sample 寫兩份），最近 N 個輸入永遠是一段連續記憶體，內迴圈就是
  `acc += g[j] * hist[j]`：沒有分支、全是 float，編譯器（Release）可以直接向量化。
- 對照組每個 tap 都要 `if (--idx < 0) idx += kTapsPerPhase` 判斷繞回，還把 float 轉 double 再累加；
  4 個 phase 各掃一次歷史。分支與型別轉換讓它很難向量化。

教訓：估 CPU 時乘法數只是起點，記憶體排列與迴圈結構常常更重要。可以自己試試把對照組改成鏡像 ring buffer + float 累加，
再看計時差多少。

**問題 4：選哪一個？** 即時顯示用 Oversampler：已經測過、比較快、Nyquist 附近的讀數也比較接近理想值。
但 latency 剛好相反：只升頻時，Oversampler 的延遲是第 1 級 31 個 2x sample + 第 2 級 11 個 4x sample
= 15.5 + 2.75 = 18.25 個原始 sample（約 0.38 ms）；64 tap FIR 是 31.5 個 4x sample = 7.875 個原始 sample
（約 0.16 ms）。meter 不在乎這一點延遲（顯示晚 0.4 ms 沒人看得出來），但 true-peak limiter 的 sidechain
延遲要算進 lookahead，短的 filter 比較省。

其他重點：
- hard clip 會產生大量高頻諧波，平頂的兩側在重建後會「彈」出去，inter-sample 過衝可達好幾 dB。
  只看 sample peak 的「0 dBFS 母帶」，轉成 AAC/MP3 或經過 DAC 之後就會 clip。
- 4x 不是完美的：Nyquist 附近的正弦峰值可能剛好落在兩個 4x sample 中間，最多低估
  $`20\log_{10}\cos(\pi/8) = -0.69`$ dB；filter 的 transition band 也會衰減接近 fs/2 的成分。
  所以規範建議 ceiling 設 -1 dBTP。
- polyphase halfband 的純延遲 phase 就是原始 sample，所以 Oversampler 版的 true peak 永遠 ≥ sample peak
  （self-check 有驗證）；windowed-sinc 版沒有這個保證（它的點全都不在原始 sample 上）。

## 練習 4：VU meter

```cpp
const double u = std::abs(x[i]) * inputScale_;                    // 全波整流 + 校準
const double acc = wn2_ * (u - s.position) - twoZetaWn_ * s.velocity;
s.velocity += acc * dt_;                                          // semi-implicit Euler：先速度
s.position += s.velocity * dt_;                                   //                      再位置
...
inputScale_ = kPi / (2.0 * dbToGain(referenceDbfs_));             // 振幅 A 的正弦整流後平均 = 2A/π
```

推導：
- 過衝 $`M_p = e^{-\pi\zeta/\sqrt{1-\zeta^2}}`$；$`\zeta = 0.82`$ → 1.11%（規範 1~1.5%）。
- 二階欠阻尼系統的步階響應（$`u = \omega_n t`$）：$`y(u) = 1 - \dfrac{e^{-\zeta u}}{\sqrt{1-\zeta^2}}\sin\!\left(\sqrt{1-\zeta^2}\,u + \arccos\zeta\right)`$。
  用二分法解出 $`y(u_{99}) = 0.99`$ → $`u_{99} = 4.128`$，所以 $`\omega_n = 4.128 / 0.3\text{ s} = 13.76`$ rad/s（2.19 Hz）。
- 為什麼用 semi-implicit Euler？顯式 Euler 對無阻尼振盪會讓能量越積越大；先更新速度再用新速度更新位置是
  symplectic 的，穩定得多。$`\omega_n T = 13.76/48000 \approx 3\times10^{-4}`$，誤差可忽略。

實際輸出：最終讀數 0.9986（不是 1.0：48 kHz 下 1 kHz 一週期只有 48 點，取樣後的 |sin| 平均
$`= \cot(\pi/48)/24 = 0.63571`$，比 $`2/\pi`$ 低 0.14%）；300 ms 時 = 最終讀數的 99.00%；過衝 1.11%；
-24 dBFS → -6.01 VU、0 dBFS → +17.99 VU、改成 -20 dBFS = 0 VU 後 -24 dBFS → -4.01 VU；停止後 300 ms 指針只剩 0.0100。
10 ms 的 0 VU burst 只把指針推到 0.0573（**-24.8 VU**）。注意要觀察夠久：指針有慣性，burst 結束後還會再往上走約 80 ms
才到最高點（二階系統脈衝響應的峰值時間 $`t = \arccos\zeta / (\omega_n\sqrt{1-\zeta^2}) \approx 77`$ ms）。
鼓組的 VU 在 -36.7 ~ +7.3 VU 之間擺動，最大值換算 -10.7 dBFS，比 sample peak（-2.92 dBFS）低約 8 dB。

## 練習 5：LRA

```cpp
// 1. relative gate（功率平均）
for (b) { sumPower += count[b] * lufsToPower(binCentre(b)); total += count[b]; }
gate = powerToLufs(sumPower / total) - 20;
// 2. 第一個 binCentre >= gate 的格子，累計通過的總數 gated
// 3. 百分位數：累積次數第一次 >= ceil(p · gated)
lra = percentile(0.95) - percentile(0.10);
```

實際輸出（`l11_ex5_solution`）：

<div align="center">

| A / B (dBFS) | LRA | 10% | 95% |
|---|---|---|---|
| -20 / -30 | 10.00 LU | -29.95 | -19.95 |
| -20 / -15 | 5.00 LU | -19.95 | -14.95 |
| -40 / -20 | 20.00 LU | -39.95 | -19.95 |

</div>

（立體聲兩聲道相同 → LUFS = dBFS + 0.01；直方圖格中心的 0.05 偏移在相減時抵消。）

重點：
- **為什麼用百分位數不用 min/max**：一聲爆炸或一段淡出只佔很少的時間，不應該把「這首歌的動態範圍」撐大。
- relative gate 是 **-20 LU**（integrated 是 -10 LU）：LRA 想保留比較安靜的段落，只排除接近靜音的部分。
  self-check 的「-20 dBFS 20 s → -50 dBFS 10 s」：short-term 值的功率平均約為
  $`10\log_{10}\frac{20P + 10\cdot 0.001P}{30} \approx -20 - 1.76 = -21.8`$ LUFS（$`P`$ 是 -20 dBFS 段的功率），
  -50 的那段（約 -50 LUFS）比平均低約 28 LU，遠低於 -20 LU 的 relative gate，所以被排除，LRA 只有 1.6 LU。
- 直方圖讓 audio thread 不需要動態配置；EBU 的參考實作（例如 libebur128）也用類似的作法。
- 限制：一次 `process()` 的長度不能超過 100 ms，否則會漏掉 short-term 的更新（程式開頭有說明）。

## 練習 6：overlap-add 快速卷積

```cpp
std::copy_n(data, numSamples, timeBuffer_.begin());                  // 1. 補 0
std::fill(timeBuffer_.begin() + numSamples, timeBuffer_.end(), 0.0f);
fft_.forwardReal(timeBuffer_.data(), spectrum_.data());              // 2. X[k]
for (k) spectrum_[k] *= irSpectrum_[k];                              //    Y[k] = X[k]·H[k]
fft_.inverseReal(spectrum_.data(), timeBuffer_.data());              //    y = IFFT（含 1/N）
for (i < N) overlap_[i] += timeBuffer_[i];                           // 3. 疊加
std::copy_n(overlap_.begin(), numSamples, data);                     // 4. 輸出完整的前 B 個
std::copy(overlap_.begin() + numSamples, overlap_.end(), overlap_.begin()); // 5. 尾巴前移
std::fill(overlap_.end() - numSamples, overlap_.end(), 0.0f);
```

為什麼 $`N \ge B + L - 1`$：長 B 與長 L 的線性卷積長 $`B + L - 1`$。DFT 的乘積是**循環卷積**，
超過 N 的部分會繞回開頭疊加（time-domain aliasing）；N 夠大就沒有東西會繞回來。

實際輸出（Debug，IR 12000 taps、96000 samples；計時每次執行都不同，這裡列兩次執行的範圍）：
直接卷積 2700 ~ 3020 ms；overlap-add B = 512（N = 16384）2530 ~ 4650 ms——**跟直接卷積差不多、甚至更慢**；
B = 4096（N 一樣 16384）320 ~ 650 ms，快 4 ~ 9 倍。兩種 block 長度的誤差都是 -100 dB（float 的精度）。
self-check 只要求 B = 4096 時比直接卷積快一倍以上，以及 B = 4096 比 B = 512 快。
為什麼 B = 512 這麼不划算？每 512 個 sample 就要做一次 16384 點的 forward + inverse FFT，
換算每個 sample 約 $`2 \cdot \frac{N}{2}\log_2 N / B \approx 448`$ 次複數乘法，和直接卷積的 12000 次乘加同一個數量級
（Debug 組態下 `std::complex` 的成本又特別高）；B = 4096 時降到約 56 次。
**單一 partition 要 B 和 L 同一個數量級才划算**；但 audio callback 的 B 很小，而且 B 越大 latency 越大。
第 12 章的 uniform partitioned convolution 把 IR 切成很多段長 B 的小段，每段各自乘、在頻域累加，同時解決這兩個問題。

## 練習 7：correlation meter

```cpp
lr_ += a_ * (x * y - lr_);
ll_ += a_ * (x * x - ll_);
rr_ += a_ * (y * y - rr_);
const double denom = std::sqrt(ll_ * rr_);
const double corr = denom > 1e-10 ? std::clamp(lr_ / denom, -1.0, 1.0) : 0.0;
```

$`a = 1 - e^{-1/(\tau f_s)}`$ 是一階 lowpass 的係數（第 04 章 envelope follower 的同一個公式）。

實際輸出：mono +1.000（S/M -100 dB = 完全沒有 side）、反相 -1.000、sin/cos -0.001（S/M 0.0 dB）、
獨立雜訊 -0.008（S/M 0.1 dB）、共同 : 獨立 = 1 : 1 → 0.494
（理論 $`r = \frac{E[s^2]}{E[s^2]+E[n^2]} = 0.5`$；S/M 能量 -4.7 dB，理論 $`10\log_{10}\frac{1-r}{1+r} = -4.77`$ dB）。

反應時間：從 mono 切到反相，$`E[LR]`$ 從 $`+P`$ 指數地走向 $`-P`$：$`m(t) = -P + 2P e^{-t/\tau}`$，
越過 0 的時間 $`t = \tau\ln 2 = 208`$ ms；實測 205 ms（block 粒度 10.7 ms）。

聽感：`l11_ex7_out_of_phase.wav` 用耳機聽，聲音像是「從頭的兩側、甚至頭的後面」傳來，很不自然；
切成 mono 時（L+R）完全抵消，什麼都聽不到。混音時 correlation 長時間在 0 以下就要小心。
