# 第 01 章 練習題：Gain、參數平滑與 Pan

[English](README.md) | **繁體中文**

難度：★☆☆ 基礎 / ★★☆ 中等 / ★★★ 進階。
實作題（3–7）都有起始碼 `exercises/starter/exN_xxx.cpp`（可編譯、留 `// TODO`，執行會印出 FAIL——這是預期的）與完整解答 `solutions/exN_xxx.cpp`。
解答與解說（含觀念題、計算題）在 [`../solutions/README.zh-TW.md`](../solutions/README.zh-TW.md)，建議先自己做完再看。

建置與執行（Windows，在專案根目錄）：

```powershell
.\scripts\build.ps1 -Only 01-gain -NoTest
cd build
.\bin\Debug\l01_ex3_starter.exe      # 你的版本（改 exercises/starter/ex3_exp_smoother.cpp）
.\bin\Debug\l01_ex3_solution.exe     # 參考解答
```

改完 starter 之後回到專案根目錄重新執行 `build.ps1`（或在 build 目錄裡執行 `cmake --build . --config Debug --target l01_ex3_starter`）即可。
每支 starter 的 self-check 全部通過、印出 `[PASS]` 就算完成。

---

## 練習 1 ★☆☆（計算）dB 心算

不要用計算機（最多用 $`\log_{10} 2 \approx 0.301`$、$`\log_{10} 3 \approx 0.477`$、$`\sqrt{2} \approx 1.414`$、$`\sqrt{10} \approx 3.162`$ 這幾個數），回答：

1. 振幅倍率 0.25 是幾 dB？振幅倍率 4 呢？
2. +10 dB 對應的振幅倍率是多少？功率倍率是多少？
3. 一個 gain plugin 串了三級：−3 dB、+8 dB、−11 dB。總增益幾 dB？振幅倍率大約多少？
4. 一個 peak 振幅 0.5 的正弦波，它的 peak 是幾 dBFS？RMS 是幾 dBFS？
5. 兩軌**一模一樣**的訊號，各自 peak −6 dBFS，相加後 peak 幾 dBFS？如果是兩軌**彼此不相關**的雜訊，各自 RMS −6 dBFS，相加後 RMS 幾 dBFS？
6. 16 軌彼此不相關的雜訊，每軌 RMS −20 dBFS，混在一起 RMS 幾 dBFS？最壞情況下 peak 可能比單軌 peak 高多少 dB？
7. 16-bit 整數 PCM 的最小量化步長是 $`1/32768`$。這個步長是幾 dBFS？（這大約就是 16-bit 的 noise floor 量級。）

## 練習 2 ★★☆（觀念 + 計算）參數平滑與 pan law

1. 用「時域相乘 = 頻域摺積」解釋（先複習 README 2.4 節開頭「振幅調變與 sideband」的直覺）：為什麼 gain 瞬間從 1 跳到 0.1 會產生 click，而且 click 是寬頻的？
2. 在 44.1 kHz 下做 50 ms 的 linear ramp，從 gain 1.0 走到 0.1：總共幾個 sample？每個 sample 前進多少？
3. one-pole exponential smoother：$`y[n] = y[n-1] + a\,(t - y[n-1])`$。在 44.1 kHz 下 τ = 20 ms，$`a`$ 是多少？輸出到達目標 99% 需要幾個 sample、幾 ms？
4. 分別寫出 linear 與 constant power pan law 在**中央**的 $`g_L`$、$`g_R`$（線性與 dB），以及 $`g_L^2 + g_R^2`$ 與 $`g_L + g_R`$ 的值。然後回答：為什麼廣播 / 影視業界偏好 linear 或 −4.5 dB，而不是純 constant power？
5. 一句話說明 **mono pan** 與 **stereo balance** 的差別；`dawlab::dsp::Panner::process()` 對立體聲 block 做的是哪一種？
6. 為什麼 `Gain` 的 polarity invert 不直接乘 −1，而是把 SmoothedValue 的目標設成 −g？切換的瞬間 gain 會經過哪個值？

## 練習 3 ★★☆（實作）one-pole exponential smoother 與時間常數量測

起始碼：`starter/ex3_exp_smoother.cpp`　解答：`../solutions/ex3_exp_smoother.cpp`　target：`l01_ex3_starter` / `l01_ex3_solution`

實作 `ExpSmoother`：

- `reset(sampleRate, timeConstantSeconds)`：算出 $`a = 1 - e^{-1/(\tau f_s)}`$（τ = 0 時 $`a = 1`$），並把 current 對齊 target。
- `getNextValue()`：套用差分方程式；當 $`|t - y| <`$ `kSnapThreshold`（1e-6）時直接把 current 設成 target（否則永遠到不了、`isSmoothing()` 永遠是 true，而且會產生 denormal）。
- `samplesToReach(s, fraction, maxSamples)`：對階梯 0 → 1 量測輸出第一次 ≥ fraction 的 sample index（從 1 起算）。

Self-check 會驗證：$`a`$ 的值、63.2% 在 ≈ τ、99% 在 ≈ 4.605 τ、99.9% 在 ≈ 6.908 τ、與解析解 $`1 - (1-a)^n`$ 的誤差、單調遞增、不超過目標、夠久之後真的停止平滑。

提示：起始碼的狀態刻意用 `double`。做完之後試著把 `current_` 改成 `float`，看看最後兩個 check（`!s.isSmoothing()`、`getCurrentValue() == 1.0f`）會發生什麼事，並想想為什麼（第 02 章的 IIR filter 會再遇到同一件事）。

## 練習 4 ★★☆（實作）fader taper：推桿位置 ↔ dB

起始碼：`starter/ex4_fader_taper.cpp`　解答：`../solutions/ex4_fader_taper.cpp`　target：`l01_ex4_starter` / `l01_ex4_solution`

分段點（位置 → dB）：`1.00 → +6`、`0.75 → 0`、`0.50 → −12`、`0.25 → −30`、`0.05 → −60`、`0.00 → −∞`。
0.05 以上每一段「dB 對位置線性」；最底段 [0, 0.05]「線性倍率對位置線性」，從 −60 dB 走到剛好 0。

實作：

- `faderToGain(position)`：位置 → 線性倍率（`faderToDb` 已經幫你包好）。
- `gainToFader(gain)`：反函式，UI 要把目前的 gain 畫回推桿位置時用。

Self-check 會驗證：每個分段點精確、段內中點（0.875 → +3 dB、0.625 → −6 dB）、底段倍率線性（0.025 → −60 dB 的一半倍率）、`faderToGain(0) == 0`、整條曲線單調遞增、反函式 round-trip 誤差 < 1e-4、超出範圍（−200 dB、+20 dB）會夾住。

## 練習 5 ★★☆（實作）multiplicative ramp：在 dB 上走直線的 fader automation

起始碼：`starter/ex5_multiplicative_ramp.cpp`　解答：`../solutions/ex5_multiplicative_ramp.cpp`　target：`l01_ex5_starter` / `l01_ex5_solution`

本章的 `dsp::SmoothedValue` 是 linear ramp：倍率對時間是直線。用它做長的 fade-out（例如 automation 把 fader 從 0 dB 拉到 −60 dB），聽起來會「一直很大聲、最後突然消失」，因為耳朵聽的是 dB。這一題實作另一種平滑器 `MultiplicativeRamp`：每個 sample **乘**同一個倍率

```math
r = \left(\frac{\text{target}}{\text{current}}\right)^{1/N},\qquad g[n] = g[0]\,r^n,\qquad \text{dB}[n] = \text{dB}[0] + n \cdot 20\log_{10} r
```

也就是在 dB 上走直線（JUCE 的 `SmoothedValue<float, ValueSmoothingTypes::Multiplicative>` 就是這個做法）。0 沒辦法用乘的到達、也乘不出任何東西，所以把 0 換成地板 `kFloorGain`（−100 dB），最後一個 sample 再 snap 成精確的目標值。

實作：

- `setTargetValue(value)`（TODO 1）：從**目前的值**出發（ramp 中途改目標也不會跳），0 換成地板，算出 `multiplier_`。
- `getNextValue()`（TODO 2）：`current_ *= multiplier_`（最後一步的 snap 已經寫好）。

Self-check（100 ms = 4800 samples @ 48 kHz，0 dB → −60 dB）會驗證：每個 sample 前進 −0.0125 dB；走到一半時 multiplicative = −30.00 dB、`dsp::SmoothedValue` 的 linear ramp = −6.01 dB；與 dB 直線的偏差 < 0.001 dB；ramp 期間在 −20 dB 以上的時間 multiplicative 1/3、linear 90.1%；相鄰 sample 最大 |ΔdB| multiplicative 0.0125 dB、linear > 1 dB；結尾精確等於目標。mute（→ 0）一半時 −50 dB、最後一個 sample 精確為 0；unmute（0 →）第一個 sample 約 −100 dB 而不是 0；ramp 走到 −15 dB 時改回 0 dB，下一個 sample 只前進 < 0.01 dB。

想一想：既然 multiplicative 這麼好，為什麼本章的 `Gain`（50 ms 的 fader 平滑）仍然用 linear ramp？從 −100 dB 地板 ramp 到 0 dB 時，前一半的時間都在 −50 dB 以下——對 unmute 來說這是優點還是缺點？狀態為什麼要用 `double`？

## 練習 6 ★★☆（實作）N 軌 mute / solo 解析邏輯

起始碼：`starter/ex6_mute_solo.cpp`　解答：`../solutions/ex6_mute_solo.cpp`　target：`l01_ex6_starter` / `l01_ex6_solution`

規則（第 14 章 DAW 的 mixer 用的是同一套規則，差別是那裡的 track 沒有 soloSafe 旗標，只有 FX bus 與 master 隱含 solo-safe）：

1. 只要有任何一軌 solo，就只有 solo 的軌出聲。
2. mute 永遠有效（solo + mute → 不出聲）。
3. soloSafe 的軌在 solo 模式下仍然出聲（但自己 mute 時一樣不出聲）。

實作：

- `resolveAudible(tracks)`：回傳每一軌「現在應該出聲嗎」。
- `audibleToGainDb(audible)`：出聲 → 0 dB，不出聲 → `kMinusInfinityDb`。

Self-check 有 8 組真值表案例，最後把結果餵給 `dsp::Gain`：在約 0.32 s（sample 15372）mute、約 0.64 s（sample 30756）unmute，驗證一階差分沒有變大（沒 click）、mute 期間 peak 是精確的 0、unmute 後訊號回來。這兩個時間點刻意落在 block 中間、1 kHz 正弦波的峰值上（不是零交越，否則就算不 ramp 也聽不到 click，見 README 2.4 節）；程式還會用 `setRampSeconds(0)` 跑一次「對照組」，確認不平滑時檢查真的會抓到 click。

## 練習 7 ★★★（實作）Mid/Side stereo width 與 stereo balance

起始碼：`starter/ex7_stereo_width.cpp`　解答：`../solutions/ex7_stereo_width.cpp`　target：`l01_ex7_starter` / `l01_ex7_solution`

```math
M = \tfrac{1}{2}(L + R),\quad S = \tfrac{1}{2}(L - R);\qquad L' = M + w\,S,\quad R' = M - w\,S
```

實作：

- `applyStereoWidth(stereo, width)`：in-place；$`w = 0`$ 變 mono、$`w = 1`$ 原樣、$`w = 2`$ 左右差異放大一倍。
- `applyBalance(stereo, balance)`：$`b > 0`$ 時左聲道乘 $`(1 - b)`$、右聲道不動；$`b < 0`$ 反之。

Self-check 會驗證：左 = 正弦波、右 = 0 的極端訊號在 $`w`$ = 0 / 1 / 2 下左右 peak 分別是 0.25/0.25、0.5/0、0.75/0.25（$`w = 2`$ 時右聲道出現反相訊號）；**任何 $`w`$ 下 $`(L' + R')/2`$ 都等於原本的 $`M`$**（mono 相容）；不相關雜訊 $`w = 2`$ 時 side +6.02 dB、mid 不變；balance +1 時左聲道精確為 0、右聲道一個 sample 都沒變；balance +0.5 時左 −6.02 dB、右 0 dB。

想一想：$`w > 1`$ 時 $`L'`$ 的 peak 可能超過原本的 peak（上面的例子 0.5 → 0.75），這對 headroom 有什麼影響？如果左右聲道完全相同（真正的 mono），任何 $`w`$ 都沒有效果——為什麼？

## 練習 8 ★☆☆（計算 + 觀念）同一軌人聲放在 mono track 與 stereo track 上

先讀 README 2.10 節。為了簡單，假設一軌 mono 人聲是常數 0.5。Mono track 用 2.9 節的 constant power pan law，stereo track 用線性 balance law $`g_L = \min(1, 1 - p)`$、$`g_R = \min(1, 1 + p)`$；fader 都在 0 dB，沒有任何 insert。

1. 寫出下面四種情況的 L 與 R 輸出值（以及 dBFS）：
   - (a) 放在 **mono track** 上，pan 在中央；
   - (b) 複製成 $`L = R`$ 放在 **stereo track** 上，balance 在中央；
   - (c) 放在 mono track 上，pan **全右**；
   - (d) 放在 stereo track 上，balance 在 **+0.5**。
2. (b) 的每一邊比 (a) 大幾 dB？這個差異從哪裡來？
3. 比較 (c) 與 (d)：兩者都是「往右」，但左喇叭分別還剩下什麼？
4. （觀念）在 track 上插一個有 stereo spread 的 chorus。為什麼在 (b) 聽起來很寬，在 (a) 卻不會？如果要讓 (a) 的人聲維持在 mono track 上、又讓 chorus 變寬，你會怎麼改？
