# 第 01 章 練習解答

[English](README.md) | **繁體中文**

題目見 [`../exercises/README.zh-TW.md`](../exercises/README.zh-TW.md)。實作題的完整程式在本資料夾，每一支都用 `DAWLAB_CHECK` / `DAWLAB_CHECK_NEAR` 以數字驗證，執行後印出 `[PASS] l01_exN (...)`。

---

## 練習 1：dB 心算

1. $`20\log_{10} 0.25 = 20 \times (-2 \times 0.301) = -12.04`$ dB。倍率 4 是 $`+12.04`$ dB（符號相反）。
2. 振幅倍率 $`10^{10/20} = 10^{0.5} = \sqrt{10} \approx 3.162`$；功率倍率 $`10^{10/10} = 10`$。（+10 dB 是「聽起來響度加倍」的經驗值，不是振幅加倍 +6 dB，也不是功率加倍 +3 dB。）
3. dB 直接相加：$`-3 + 8 - 11 = -6`$ dB。振幅倍率 $`10^{-6/20} = 10^{-0.3} \approx 0.5`$（精確值 0.501）。這就是用 dB 的好處：串接多級時乘法變加法。
4. Peak：$`20\log_{10} 0.5 = -6.02`$ dBFS。正弦波的 RMS 是 peak 的 $`1/\sqrt{2}`$：$`0.5 / 1.414 = 0.354`$，$`20\log_{10} 0.354 = -6.02 - 3.01 = -9.03`$ dBFS。正弦波的 peak 永遠比 RMS 高 3.01 dB（crest factor $`\sqrt{2}`$）。
5. 相同訊號：振幅相加 → $`2 \times 0.5 = 1.0`$ → **0 dBFS**（+6.02 dB）。不相關雜訊：功率相加 → RMS 乘 $`\sqrt{2}`$ → $-6 + 3.01 = $ **−2.99 dBFS**（≈ −3 dBFS，+3.01 dB）。
6. RMS：$-20 + 10\log_{10} 16 = -20 + 12.04 = $ **−7.96 dBFS**。最壞情況（16 軌 peak 同時出現）peak 可以高出 $20\log_{10} 16 = $ **+24.08 dB**。實際 peak 通常介於 +12.04 與 +24.08 之間。可以對照範例 3 (B)：8 軌時理論區間是 +9.03 到 +18.06 dB，實測 +17.5 dB。
7. $20\log_{10}(1/32768) = -20 \times 15 \times 0.301 = $ **−90.3 dBFS**（$`32768 = 2^{15}`$）。常聽到的「16-bit 動態範圍約 96 dB」是 $`20\log_{10}(2^{16}) = 6.02 \times 16 = 96.3`$ dB：它把 −32768..32767 全部 65536 個量化階都算進去，對應的是峰對峰（peak-to-peak）範圍 2；而 $`1/32768`$ 是以 peak = 1.0 為 0 dBFS 的步長，只用到 $`2^{15}`$。兩者剛好差一個 bit，也就是 $`20\log_{10} 2 = 6.02`$ dB（96.3 − 90.3）。若改用「滿刻度正弦波的 RMS ÷ 量化雜訊的 RMS（$`\Delta/\sqrt{12}`$）」來算，得到的是另一個公式 SQNR $`= 6.02N + 1.76 \approx 98.1`$ dB（第 00 章）；那個 +1.76 dB 才是 RMS 與正弦波 crest factor 造成的。

## 練習 2：參數平滑與 pan law

1. $`y[n] = x[n]\,g[n]`$，時域相乘等於頻域摺積 $`Y(f) = X(f) * G(f)`$。$`g[n]`$ 是階梯函數，它的頻譜 $`|G(f)| \propto 1/f`$，從 DC 一路延伸到 Nyquist（每倍頻程只掉 6 dB）。純音 $`X(f)`$ 是一根線，摺積後整個 $`G(f)`$ 的形狀被「貼」到那根線的位置——原本只有 1 kHz 的訊號突然在所有頻率上都有能量，耳朵聽到寬頻的「喀」。平滑之後 $`g[n]`$ 是連續的（分段線性），頻譜 $`\propto 1/f^2`$（每倍頻程 12 dB），而且整體小很多。範例 1 (B) 的 sideband 表：階梯式 −34.7 / −40.8 / −46.8 dB（每倍頻程 6 dB），平滑 −74.4 / −86.4 / −98.5 dB（每倍頻程 12 dB）。
2. $`N = 0.05 \times 44100 = 2205`$ 個 sample。每步 $`(0.1 - 1.0) / 2205 = -4.08 \times 10^{-4}`$。
3. $`\tau f_s = 0.02 \times 44100 = 882`$；$`a = 1 - e^{-1/882} = 1 - 0.998867 = 1.133 \times 10^{-3}`$（近似 $`1/882 = 1.134 \times 10^{-3}`$）。到 99%：$`n = \ln(100) \times 882 = 4.605 \times 882 \approx 4062`$ 個 sample ≈ **92.1 ms**。注意「τ = 20 ms」的 smoother 其實要 92 ms 才走到 99%，比同樣標示 20 ms 的 linear ramp 慢得多——選擇 τ 時要記得乘 4.6。
4. 中央 $`\theta = 0.5`$：

   | | $`g_L = g_R`$ | dB | $`g_L^2 + g_R^2`$ | $`g_L + g_R`$ |
   |---|---:|---:|---:|---:|
   | Linear | $`1 - 0.5 = 0.5`$ | −6.02 | $`0.25 + 0.25 = 0.5`$（−3.01 dB） | 1（0 dB） |
   | Constant power | $`\cos(\pi/4) = 0.7071`$ | −3.01 | $`0.5 + 0.5 = 1`$（0 dB） | 1.414（+3.01 dB） |

   廣播 / 影視的內容常常會被 downmix 成 mono（電視喇叭、電話、公共廣播）。Constant power 在 mono 相加時中央的素材（對白、主唱）會比兩端大 3 dB，整體平衡跑掉；linear 的振幅和永遠是 1，mono 下完全不變；−4.5 dB 是兩邊各讓 1.5 dB 的折衷。音樂製作在喇叭上監聽、很少 downmix，所以偏好 constant power。
5. Mono pan 是「一個訊號分配到左右兩個輸出」（$`L = g_L x`$、$`R = g_R x`$，內容全部保留只是位置移動）；stereo balance 是「左右兩個已存在的訊號各自乘一個 gain」（$`L' = g_L L`$、$`R' = g_R R`$，轉到一邊時另一邊的內容消失、不會搬過去）。`Panner::process()` 對立體聲 block 做的是 **balance**（channel 0 乘 $`g_L`$、channel 1 乘 $`g_R`$）；只有當兩個 channel 內容相同時它才等價於 mono pan。真正的 mono pan 用 `processMonoToStereo()`。
6. 直接乘 −1 等於 gain 從 $`+g`$ 瞬間跳到 $`-g`$，跳變量是 $`2g`$——比 mute 還大的階梯，一定 click。把目標設成 $`-g`$ 之後 SmoothedValue 從 $`+g`$ 線性走到 $`-g`$，中途會**經過 0**（ramp 的正中間），整個過程連續，所以切換 Ø 不會有聲音。代價是切換的 50 ms 內音量會先降到 0 再回來（$`|g|`$ 呈 V 字形，除了兩個端點之外整段都比 $`g`$ 小），聽起來像一個短暫的 dip。

## 練習 3：one-pole exponential smoother（`ex3_exp_smoother.cpp`）

關鍵思路：

```cpp
coeff_ = tauSamples > 0.0 ? 1.0 - std::exp(-1.0 / tauSamples) : 1.0;   // a = 1 − e^{−1/τfs}

float getNextValue() noexcept {
    current_ += coeff_ * (target_ - current_);          // 每個 sample 走剩餘距離的 a 倍
    if (std::abs(target_ - current_) < kSnapThreshold)  // 1e-6：低於 −120 dB 視為已到達
        current_ = target_;
    return static_cast<float>(current_);
}
```

量測時間常數就是對階梯 0 → 1 逐 sample 呼叫、數到第幾個 sample 第一次 ≥ 0.632 / 0.99 / 0.999。執行輸出：

```
tau = 10.0 ms = 480 samples, a = 0.002081 (theory 1 - e^(-1/480) = 0.002081)
Reaches 63.2%: 480 samples (1.00 tau, expected 1.00 tau)
Reaches 99%  : 2211 samples (4.61 tau, expected ln(100) = 4.605 tau)
Reaches 99.9%: 3316 samples (6.91 tau, expected ln(1000) = 6.908 tau)
Max error vs. the closed form 1 - (1-a)^n: 4.29e-08; monotonically increasing: yes; never exceeds the target: yes
After 50 ms: linear ramp = 1.000, exponential (tau = 50 ms) = 0.632
[PASS] l01_ex3 (11 checks passed)
```

最後一行值得記住：同樣標示「50 ms」，linear ramp 在 50 ms 時剛好到達 1.000，exponential（τ = 50 ms）只走到 0.632。

**為什麼狀態要用 double？** 這是題目提示裡要你踩的坑。如果 `current_` 是 float，當剩餘距離 $`d = t - y`$ 小到 $`a \cdot d`$ 比 $`y`$ 的半個 ulp 還小，`current_ += a·d` 等於沒加——狀態會卡住。$`y`$ 從下方接近 1（落在 $`[0.5, 1)`$）時，ulp $`= 2^{-24} \approx 6 \times 10^{-8}`$，半個 ulp $`\approx 3 \times 10^{-8}`$，所以卡在 $`d \approx 3 \times 10^{-8} / a = \varepsilon/(4a) \approx 3 \times 10^{-8} / 0.0021 \approx 1.4 \times 10^{-5}`$ 的地方（$`\varepsilon = 2^{-23} \approx 1.19 \times 10^{-7}`$ 是 1.0 的 ulp；實測 float 版本卡在 $`d = 1.43 \times 10^{-5}`$，與推導吻合），永遠到不了 1e-6 的 snap 門檻。於是 `isSmoothing()` 永遠是 true（最後兩個 check 會 FAIL），而且每個 block 都在做無意義的乘法。用 double（ε ≈ 2.2 × 10⁻¹⁶）卡住的位置在 $`10^{-14}`$ 量級，遠低於門檻。另一個做法是把 snap 門檻設得比卡住位置大（例如 1e-4 ≈ −80 dB），但門檻就得跟著 τ 變，不如直接用 double 乾淨。

## 練習 4：fader taper（`ex4_fader_taper.cpp`）

關鍵思路：底段 $`p \le 0.05`$ 時倍率線性 `dbToGain(-60) * (p / 0.05)`（$`p = 0`$ 剛好得到 0）；其他段找出 $`p`$ 落在哪兩個分段點之間，在 **dB** 上做 `lerp`，最後 `dbToGain`。反函式做一模一樣的事：倍率 ≤ −60 dB 的倍率時用倍率反推，否則先 `gainToDb` 再在 dB 上反推位置。

```cpp
for (int i = 1; i < kNumPoints; ++i)
    if (p <= kPoints[i].position) {
        const float t = (p - kPoints[i-1].position) / (kPoints[i].position - kPoints[i-1].position);
        return dbToGain(lerp(kPoints[i-1].db, kPoints[i].db, t));
    }
```

執行輸出（節錄）：

```
position         dB       gain
    0.00    -100.00    0.00000      <- a true 0 (printed as this course's −∞ = −100 dB)
    0.05     -60.00    0.00100
    0.25     -30.00    0.03162
    0.50     -12.00    0.25119
    0.75       0.00    1.00000
    0.90       3.60    1.51356
    1.00       6.00    1.99526
round-trip max error of gainToFader(faderToGain(p)): 5.96e-08
[PASS] l01_ex4 (16 checks passed)
```

第一列是真正的 0，本教材把它印成 −∞ = −100 dB。

觀察：推桿上 1/4 行程（0.75 → 1.0）只有 6 dB，每 0.05 的行程 1.2 dB，適合微調；0.25 → 0.5 這一段每 0.05 有 3.6 dB；0.05 → 0.25 每 0.05 有 7.5 dB。這就是「上密下疏」。`dbToFader(-200)` 得到 0、`dbToFader(20)` 得到 1：超出範圍要夾住，UI 才不會畫出推桿外面的位置。

## 練習 5：multiplicative ramp（`ex5_multiplicative_ramp.cpp`）

關鍵思路：linear ramp 每個 sample **加**同一個 step，multiplicative ramp 每個 sample **乘**同一個倍率，所以後者在 dB 上是直線：

```cpp
const double from = std::max(current_, kFloorGain);                    // 0 換成 −100 dB 地板
const double to   = std::max(static_cast<double>(value), kFloorGain);
current_    = from;
multiplier_ = std::pow(to / from, 1.0 / static_cast<double>(rampSamples_));
// getNextValue()：最後一步 snap 成精確的 target_，其餘 current_ *= multiplier_
```

執行輸出：

```
multiplier r = 0.998562 (-0.0125 dB per sample)
Halfway (2400 samples): multiplicative -30.00 dB, linear -6.01 dB
Max deviation from the straight line in dB: 0.00000 dB
Time spent above -20 dB during the ramp: multiplicative 33.3%, linear 90.1%
Max |delta dB| between adjacent samples: multiplicative 0.0125 dB, linear 1.381 dB (in the last few samples)
mute: halfway -50.00 dB, second-to-last -99.98 dB, last = 0
unmute: first sample 1.002e-05 (-99.9792 dB), last sample 1.000000
Switching back to 0 dB at -15.00 dB: the next sample advances only +0.0031 dB
Wrote dawlab_output/l01_ex5_multiplicative_ramp.wav (first half: linear fade-out, second half: multiplicative fade-out)
[PASS] l01_ex5 (17 checks passed)
```

- $`r = 0.001^{1/4800} = e^{\ln(0.001)/4800} = 0.998562`$，$`20\log_{10} r = -60/4800 = -0.0125`$ dB。
- Linear ramp 走到一半時倍率還有 $`1 - 0.999/2 = 0.5005`$（−6.01 dB），要到 $`(1 - 0.1)/0.999 = 90.1\%`$ 的時間才跌破 −20 dB；剩下不到 10% 的時間要走完 −20 → −60 dB，最後幾個 sample 每一步掉 1.4 dB。這就是「一直很大聲、最後突然消失」的原因。Multiplicative 的 −20 dB 剛好在 1/3 處，每一步都是 0.0125 dB。
- 地板的副作用：mute 時 dB 直線是 0 → −100 dB，一半時已經 −50 dB；unmute 時則前一半都在 −50 dB 以下，聽起來像「延遲了一下才淡入」。所以 multiplicative 適合長的 fade 與 dB 參數的 automation，短的 mute / unmute（本章 `Gain` 的 50 ms、第 14 章 `ChannelStrip` 的 10 ms）用 linear 反而比較自然——這回答了「想一想」的第一、二題。
- 為什麼用 `double`：4800 次累乘，每次 float 捨入約 $`6\times10^{-8}`$ 的相對誤差，累積起來到 $`10^{-4}`$ 量級（約 0.001 dB），而且 ramp 時間越長越糟；double 則是 $`10^{-13}`$ 量級。最後一步的 snap 讓結尾不受累積誤差影響（mute 時停在精確的 0）。

## 練習 6：mute / solo 解析（`ex6_mute_solo.cpp`）

關鍵思路：先掃一遍找出 `anySolo`，再逐軌套用

```cpp
audible[i] = !t.mute && (!anySolo || t.solo || t.soloSafe);
```

執行輸出：

```
all normal                               [0:---->on ] [1:---->on ] [2:---->on ] [3:---->on ]
mute 1                                   [0:---->on ] [1:M--->off] [2:---->on ] [3:---->on ]
solo 2                                   [0:---->off] [1:---->off] [2:-S-->on ] [3:---->off]
solo 0 + solo 2                          [0:-S-->on ] [1:---->off] [2:-S-->on ] [3:---->off]
solo 0 + solo 2 + mute 2 (mute wins)     [0:-S-->on ] [1:---->off] [2:MS-->off] [3:---->off]
solo 1 + 3 is solo-safe                  [0:---->off] [1:-S-->on ] [2:---->off] [3:--F->on ]
solo 1 + 3 solo-safe but muted           [0:---->off] [1:-S-->on ] [2:---->off] [3:M-F->off]
nobody soloed, 3 is solo-safe            [0:---->on ] [1:---->on ] [2:---->on ] [3:--F->on ]
mute/unmute through Gain (50 ms ramp): max first difference 0.0653 (natural value of the sine 0.0654), peak during the mute = -100.0 dBFS
Control case (ramp = 0, instant jump): max first difference 0.5000 (7.6x the natural value) -> proves this check can catch a click
[PASS] l01_ex6 (13 checks passed)
```

最後兩行是重點：mute 的實作不是「把 sample 設成 0」，而是把 `Gain` 的目標設成 `kMinusInfinityDb`，讓 50 ms 的 ramp 走過去，所以一階差分與純正弦波一樣（沒有 click）；而 −100 dB 經 `dbToGain` 變成精確的 0，mute 期間 peak 是真正的 0（印成 −100 dBFS）。

為什麼還要一個「對照組」？一個測不出錯誤的檢查等於沒有檢查。mute / unmute 刻意放在 sample 15372 與 30756（block 中間，分別是 1 kHz 正弦波的正、負峰值）：如果放在 block 邊界 15360 = 320 × 48，該處剛好是正弦波的零交越（README 2.4 節的例外），就算完全不 ramp、瞬間跳到 0 也不會產生陡坡，`maxDiff < naturalDiff * 1.05` 永遠會通過。所以程式用同一個情境、`setRampSeconds(0)` 再跑一次，並要求對照組的一階差分 > 自然值的 2 倍（實測 7.6 倍）——證明這個檢查真的有鑑別力。事件落在 block 中間時，程式把 block 切成兩個 sub-block 處理，這就是 DAW 的 sample-accurate automation 的做法。

第 14 章的 mixer 做的是同一件事，只是拆在兩個地方：`Mixer::process()` 在每個 block 開頭先算一次 `anySolo`，再對每一軌用 `!mute && (!anySolo || solo)` 算出該不該出聲（與 `Mixer::isTrackAudible(t)` 是同一個式子，後者給 UI / 測試查詢用），交給 `ChannelStrip::beginBlock(audible)`；`ChannelStrip` 再用自己的 10 ms `SmoothedValue`（不是 `dsp::Gain` 的 50 ms）把 mute gain ramp 到 0 或 1。那裡的 track 沒有 soloSafe 旗標：solo-safe 只用在 FX bus 與 master（它們只有 mute），所以 solo 一軌時仍聽得到它送進 reverb bus 的殘響。

## 練習 7：M/S stereo width 與 balance（`ex7_stereo_width.cpp`）

關鍵思路：逐 sample 算 $`M`$、$`S`$，把 $`S`$ 乘上 $`w`$ 再 decode：

```cpp
const float mid  = 0.5f * (l[i] + r[i]);
const float side = 0.5f * (l[i] - r[i]) * w;
l[i] = mid + side;
r[i] = mid - side;
```

因為 $`(L' + R')/2 = M`$ 與 $`w`$ 無關，所以任何寬度處理在 mono downmix 下都「消失」——這是 M/S 處理最重要的性質（mono 相容），self-check 對三種 $`w`$ 都驗證了這一點。

執行輸出：

```
width 0/1/2: L peak 0.250 / 0.500 / 0.750, R peak 0.250 / 0.000 / 0.250 (the mono sum is unchanged)
Uncorrelated noise, width 2: mid -19.83 -> -19.83 dB, side -19.83 -> -13.81 dB (+6.02 dB)
balance +0.5: L -6.02 dB, R 0.00 dB; balance +1: L peak = -100.0 dBFS
Wrote dawlab_output/l01_ex7_width.wav (width 0 -> 1 -> 2, 2 seconds each)
[PASS] l01_ex7 (17 checks passed)
```

- 左 = 正弦波、右 = 0 時 $`M = S = L/2`$。$`w = 2`$：$`L' = M + 2S = 1.5L`$（peak 0.75）、$`R' = M - 2S = -0.5L`$（右邊出現反相的訊號，peak 0.25）。這回答了「想一想」的第一題：$`w > 1`$ 會讓 peak 變大（這裡 +3.5 dB），寬度處理之後要重新檢查 headroom。
- 真正的 mono（$`L = R`$）時 $`S = 0`$，乘任何 $`w`$ 都還是 0，所以沒有效果——width 只能放大「已經存在」的左右差異，不能無中生有。
- Balance 是各聲道獨立乘 gain：+1 時左聲道精確為 0、右聲道一個 sample 都沒變，證明它不會把左邊的內容搬到右邊（和範例 2 (C) 的結論一致）。

## 練習 8：同一軌人聲放在 mono track 與 stereo track 上

1. $`x = 0.5`$（$`-6.02`$ dBFS）：

   | 情況 | $`g_L`$ / $`g_R`$ | L | R |
   |---|---|---|---|
   | (a) mono track，中央，constant power（$`\theta = 0.5`$） | 0.7071 / 0.7071 | 0.3536（−9.03 dBFS） | 0.3536（−9.03 dBFS） |
   | (b) stereo track $`L = R`$，balance 中央（$`p = 0`$） | 1 / 1 | 0.5（−6.02 dBFS） | 0.5（−6.02 dBFS） |
   | (c) mono track，全右（$`\theta = 1`$） | $`\cos(\pi/2) = 0`$ / $`\sin(\pi/2) = 1`$ | 0（無聲） | 0.5（−6.02 dBFS） |
   | (d) stereo track，balance +0.5 | $`\min(1, 0.5) = 0.5`$ / $`\min(1, 1.5) = 1`$ | 0.25（−12.04 dBFS） | 0.5（−6.02 dBFS） |

2. $`20\log_{10}(0.5 / 0.3536) = 20\log_{10}\sqrt{2} = `$ 每邊 **+3.01 dB**。Mono track 套用了 constant power law 在中央的 −3 dB；stereo track 的 balance 在中央是 unity。從功率來看，(a) 送進房間的是 $`2 \times 0.3536^2 = 0.125 = 0.5^2`$，正好等於音源的功率；(b) 是 $`2 \times 0.5^2 = 0.25`$，兩倍（+3.01 dB）。這正是 pan law 存在的理由（pan 時響度不變），也正是立體聲素材要用 balance 的理由（放在中央的立體聲音源不能變小聲）。
3. (c) 左喇叭完全無聲，人聲全部在右邊：pan 是把音源搬過去。(d) 左喇叭仍然以 0.25 播放人聲（比右邊小 6.02 dB）：balance 只是衰減左邊，所以聲像偏右（左右差 6 dB），但不是全右。要讓 stereo track 的 L = 0，balance 必須轉到 +1。
4. 在 (b) 裡 chorus 收到兩個聲道；有 stereo spread 時，它對左右的 delay line 做不同的調變（例如 LFO 相位差 90° 或 180°），所以輸出的 L 與 R 不同、部分去相關，耳朵就聽到寬度。在 (a) 裡 insert 位於 panner 之前，跑在 mono track 唯一的那個聲道上：根本沒有第二邊可以做出差異，效果原本會產生的 spread 也就不見了，之後 panner 只是把同一個訊號乘上兩個 gain 送到 L 與 R（相關係數 1，一個點音源）。要讓人聲維持在 mono track 上、又讓 chorus 變寬，可以把 chorus 放在用 send 送過去的立體聲 FX channel / bus 上（panner 之後全部都是立體聲），或放在這軌所 route 到的立體聲 group 上；把 track 改成 stereo 也可以，但處理量加倍，而且 pan 旋鈕會變成 balance。
