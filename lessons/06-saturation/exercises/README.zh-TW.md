# 第 06 章練習題：Saturation 與 aliasing

[English](README.md) | **繁體中文**

難度：★☆☆ 基礎　★★☆ 進階　★★★ 挑戰

實作題的起始碼在 `exercises/starter/`，可以直接編譯；完成 `// TODO` 之前執行會印出 `[FAIL]`（這是預期的）。完整解答在 `solutions/`，解說在 [`solutions/README.zh-TW.md`](../solutions/README.zh-TW.md)。

建置與執行（PowerShell，在專案根目錄）：

```powershell
.\scripts\build.ps1 -Only 06-saturation -NoTest
cd build
.\bin\Debug\l06_ex4_starter.exe      # 你的版本
.\bin\Debug\l06_ex4_solution.exe     # 解答
```

| 題號 | 類型 | 難度 | 起始碼 target | 解答 target |
|---|---|---|---|---|
| 1 | 觀念 | ★☆☆ | — | — |
| 2 | 計算 | ★☆☆ | — | `l06_ex2_solution` |
| 3 | 計算 | ★★☆ | — | `l06_ex3_solution` |
| 4 | 實作 | ★★☆ | `l06_ex4_starter` | `l06_ex4_solution` |
| 5 | 實作 | ★★☆ | `l06_ex5_starter` | `l06_ex5_solution` |
| 6 | 實作 | ★★★ | `l06_ex6_starter` | `l06_ex6_solution` |
| 7 | 實作 | ★★☆ | `l06_ex7_starter` | `l06_ex7_solution` |

---

## 練習 1（★☆☆，觀念）：對稱、偶次諧波、DC 與 latency 對齊

1. `tanh` 是奇函式。用「正弦波的半波對稱」說明為什麼 tanh 只會產生奇次諧波，而且不會產生 DC。
2. `Asymmetric` 曲線為什麼一定會產生 DC？舉出兩個 DC offset 在混音裡造成的實際問題。
3. 為什麼 DC blocker 的 cutoff 通常設在 5～20 Hz，而不是 50 Hz 或 1 Hz？各有什麼缺點？
4. 有人寫了一個 4x oversampling 的 saturation，dry/wet mix 時直接把「目前這個 block 的 dry」與「wet」相加，聽起來音色變空洞。原因是什麼？第一個 notch 大約在幾 Hz？（4x 的 latency = 36 samples @ 48 kHz）

**提示**：README 2.4、2.10 節。

---

## 練習 2（★☆☆，計算）：手算 $`x^2`$、$`x^3`$ 與 cubic soft clip 的諧波

設輸入 $`x = A\sin\theta`$。

1. 用三角恆等式展開 $`x^2`$。有哪些頻率成分？各自振幅多少？
2. 展開 $`x^3`$。三次諧波相對於基頻是幾 dB？這個比值跟 $`A`$ 有沒有關係？
3. 對 $`y = x - x^3/3`$（$`|x| \le 1`$），寫出 $`H_1`$、$`H_3`$ 的公式，並算出 $`A = 0.5`$ 與 $`A = 1`$ 時的 $`H_3/H_1`$（dB）與 THD（%）。
4. 對 $`y = x + k x^2`$，寫出 DC 與 $`H_2/H_1`$。$`k = 0.5`$、$`A = 0.5`$ 時是多少 dB？

**驗證**：解答程式 `l06_ex2_solution` 會用 Goertzel 量測每一個值。

---

## 練習 3（★★☆，計算）：alias 頻率

sample rate $`f_s = 44.1`$ kHz，一個 5 kHz 正弦波經過很重的 hard clip（近似方波：只有奇次諧波，第 $`k`$ 次振幅 ≈ 基頻的 $`1/k`$）。

1. 第 3、5、7、9、11、13、15 次諧波的頻率各是多少？哪些超過 Nyquist？折返後落在哪裡？
2. 9 次諧波折返後的頻率是多少？它相對於基頻是幾 dB？為什麼這個 alias 特別難聽？
3. 改成 2x oversampling（在 88.2 kHz 下做 hard clip，然後降回 44.1 kHz）。上面哪些諧波會在降頻時被濾掉？哪一個仍然會以 alias 的形式留下來？哪一個落在 halfband filter 的 transition band 裡、只被衰減一部分（降頻後又落在哪裡）？

**提示**：$`f_{\text{alias}} = \left| ((f + f_s/2) \bmod f_s) - f_s/2 \right|`$。2x 時先用 $`2f_s`$ 摺一次，摺完仍高於 22.05 kHz 的會被降頻 filter 擋掉。

**驗證**：`l06_ex3_solution` 會印出表格，並用真的 hard clip 量測 900 Hz 與 9.1 kHz 的 alias。

---

## 練習 4（★★☆，實作）：Chebyshev 諧波 exciter

起始碼：`exercises/starter/ex4_chebyshev_exciter.cpp`

1. 用遞迴 $`T_{n+1}(x) = 2x\,T_n(x) - T_{n-1}(x)`$ 實作 `chebyshev(n, x)`。
2. 實作 `ChebyshevExciter::processSample()`：$`y = x + \sum_{n=2}^{5} g_n T_n(\text{clamp}(x, -1, 1))`$。
3. 驗證：振幅 1 的 1 kHz 正弦波，設 $`g_2 = 0.1`$、$`g_3 = 0.05`$、$`g_5 = 0.02`$，量到的 H2 / H3 / H5 要等於 −20.00 / −26.02 / −33.98 dB（±0.1 dB），H4 與 H6 不存在。
4. 手算：輸入振幅改成 0.5 時，輸出的 DC 與 H2 振幅是多少？把答案填進 TODO 3。

**提示**：$`T_2(A\cos\theta) = 2A^2\cos^2\theta - 1`$，再用倍角公式。

---

## 練習 5（★★☆，實作）：latency 對齊的 dry/wet mix 與 comb notch

起始碼：`exercises/starter/ex5_aligned_dry_wet.cpp`

（DC blocker 的實作已經在[第 02 章練習 2](../../02-filter/exercises/README.zh-TW.md) 做過；`Saturation` 內部用的是第 02 章的 `OnePole` highpass，見 README 2.4 節。這一題專心處理 README 2.10 節的另一個陷阱：dry/wet 對齊。）

起始碼已經寫好一個 parallel saturator：wet 路徑是 4x `Oversampler` + $`\tanh(g x)/g`$（除以 drive，讓小訊號時 dry 與 wet 一樣大），dry 路徑經過一個可以設定延遲 $`D`$ 的 `DryDelay`。小訊號時整個系統是

$$
y[n] = (1 - m)\,x[n - D] + m\,x[n - L],\qquad |H(f)| = \left|(1 - m) + m\,e^{-j2\pi f (L - D)/f_s}\right|
$$

1. 實作 `DryDelay::processSample()`：長度 `maxDelay + 1` 的 ring buffer，先寫入 $`x[n]`$、再讀 $`x[n - D]`$；$`D = 0`$ 時輸出等於輸入（TODO 1）。
2. 實作 `predictedNotchHz(k, Δ, fs)`：$`\Delta = L - D`$ 時第 $`k`$ 個 notch 的頻率（TODO 2）。
3. 在 `ParallelSaturator::process()` 裡完成 $`(1 - m)\cdot\text{dry} + m\cdot\text{wet}`$（TODO 3）。
4. 驗證（−40 dBFS 正弦波、256-sample block）：
   - 4x 的 latency $`L = 36`$。
   - **沒對齊**（$`D = 0`$）、mix 50%：667 / 2000 / 3333 Hz 三個 notch 都低於 −40 dB；兩個 notch 中間的 1333 Hz 是 0 dB；在 500～850 Hz 每 2 Hz 掃頻，最深處落在 667 Hz ± 2 Hz。mix 改成 30% 時 667 Hz 只剩 $`20\log_{10}|1 - 2m| = -7.96`$ dB。
   - **對齊**（$`D = L`$）：667 Hz、2 kHz、3.33 kHz、10 kHz 都在 0 dB ± 0.02 dB 內。
   - **差一個 sample**（$`D = L - 1`$）：第一個 notch 在 24 kHz，但 10 kHz 已經掉 $`20\log_{10}|\cos(\pi f/f_s)| = -2.01`$ dB。

**想一想**：mix = 100% 時還需要延遲 dry 嗎？如果 wet 路徑的 latency 不是整數（例如 IIR halfband 的非線性相位），還能用整數 delay 完美對齊嗎？

---

## 練習 6（★★★，實作）：一階 ADAA hard clip

起始碼：`exercises/starter/ex6_adaa.cpp`

1. 寫出 hard clip $`f(x) = \text{clamp}(x, -1, 1)`$ 的反導函式 $`F(x)`$，確認 $`F' = f`$ 而且在 $`|x| = 1`$ 處連續。
2. 實作 $`y[n] = \dfrac{F(x[n]) - F(x[n-1])}{x[n] - x[n-1]}`$，在 $`|x[n] - x[n-1]|`$ 很小時改用 $`f\big((x[n] + x[n-1])/2\big)`$。
3. 7 kHz、peak 2 的正弦波（48 kHz），比較 naive 與 ADAA 的 alias 總能量。ADAA 要改善 6 dB 以上。
4. （思考）ADAA 在線性區等於什麼 filter？它對 7 kHz 的基頻造成多少衰減？延遲多少？

**加分**：把 ADAA 放進 `Oversampler` 的 2x 迴圈裡，跟 naive 2x 比較。

---

## 練習 7（★★☆，實作）：oversampling 的品質與成本

起始碼：`exercises/starter/ex7_oversampling_cost.cpp`

用 `dawlab::dsp::Saturation`（Tanh、drive +18 dB、output −6 dB）：

1. 實作 `aliasDb(log2)`：7 kHz、peak 0.5 的正弦波處理後，所有 alias 位置（1、3、5、…、23 kHz，扣掉 7 與 21 kHz）的功率總和相對於基頻的 dB。跳過開頭 0.1 秒。
2. 實作 `timeMs(log2, noise)`：處理 10 秒 stereo noise 的時間（`std::chrono::steady_clock`，跑 3 次取最小值；不要把複製 buffer 與 prepare 算進去）。
3. 印出 1x / 2x / 4x / 8x 的表格。從品質 / 成本的角度，你會選幾倍？如果曲線換成 hard clip，答案會變嗎？

**提示**：為什麼 alias 都在奇數 kHz？見 README 2.5 節。
