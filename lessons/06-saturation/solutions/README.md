# 第 06 章解答：Saturation 與 aliasing

每一支解答程式都用 `DAWLAB_CHECK` / `DAWLAB_CHECK_NEAR` 以數字驗證，全部通過時印出 `[PASS]` 並回傳 0（ctest label：`solution`）。

```powershell
.\scripts\build.ps1 -Only 06-saturation -TestLabel solution
```

---

## 練習 1：對稱、偶次諧波、DC 與 latency 對齊

**1. 為什麼 tanh 只有奇次諧波、沒有 DC？**

正弦波有半波對稱：$x(t + T/2) = -x(t)$。tanh 是奇函式：$\tanh(-u) = -\tanh(u)$。所以

$$
y(t + T/2) = \tanh\big(x(t+T/2)\big) = \tanh\big(-x(t)\big) = -\tanh\big(x(t)\big) = -y(t)
$$

輸出也半波對稱。把 $y$ 寫成 Fourier series $\sum_k c_k e^{jk\omega_0 t}$，平移半個週期後第 $k$ 項乘上 $e^{jk\pi} = (-1)^k$。要讓整體變號，只有 $(-1)^k = -1$ 的項可以不為 0——$k$ 必須是奇數。$k = 0$（DC）與所有偶數 $k$ 的係數都是 0。範例 `l06_harmonic_analysis` 的表格中，Tanh / Cubic / Arctan 的 H2、H4、H6、H8 全部低於 −99 dB、DC 為 0，正是這個結果。

**2. Asymmetric 為什麼一定有 DC？實際問題？**

把曲線拆成奇部與偶部：$f(x) = f_{\text{odd}}(x) + f_{\text{even}}(x)$。奇部只產生奇次諧波，所以 DC 只可能來自偶部。Asymmetric 的偶部是（$t = \tanh|x|$，用 $\tanh 2u = 2\tanh u/(1+\tanh^2 u)$）

$$
f_{\text{even}}(x) = \frac{f(x) + f(-x)}{2} = \frac{1}{2}\left(t - \frac{t}{1+t^2}\right) = \frac{t^3}{2(1+t^2)} \ge 0
$$

它處處非負、且只在 $x = 0$ 時為 0，所以只要輸入不是全零，平均值（DC）就一定是正的。小訊號時 $f_{\text{even}} \approx |x|^3/2$，DC $\approx 2A^3/(3\pi)$（範例 `l06_harmonic_analysis`：$A = 0.1$ 時近似 0.00021、量測 0.00021）。更直觀地說：正半波可以到 +1，負半波只能到 −0.5，平均值當然大於 0（$A = 1$ 時 +0.0748）。

注意這裡的偶部從 $|x|^3$ 開始，不是 $x^2$：兩邊在原點的斜率都是 1、曲率都是 0。只要偶部不是零函式而且不變號，DC 就不會是 0；$x^2$ 只是最簡單的例子。

實際問題（任兩個即可）：
- 吃掉 headroom，正半波提早撞到 0 dBFS。
- 後級的 compressor / limiter / gate 的 detector 把 DC 當成訊號，增益行為改變。
- track 開始、結束、mute / unmute 時 DC 瞬間出現或消失 → click。
- 串接多級非線性時偏壓越積越多，下一級更不對稱。
- 有些喇叭 / 耳機放大器遇到 DC 會保護或損壞（極端情況）。

**3. 為什麼 cutoff 用 5～20 Hz？**

- **太高（50 Hz）**：會削到真正的低頻音樂內容（kick 的基頻 40～60 Hz、bass 的 E1 = 41 Hz），而且一階 highpass 在 cutoff 附近有明顯的相位偏移，會改變 kick 的波形。
- **太低（1 Hz）**：時間常數 $\tau = 1/(2\pi f_c) = 159$ ms，DC 改變時（例如不對稱失真的量隨音量變化）要很久才被移除；衰減到 1% 約需 0.73 秒，期間偏壓仍然存在，也容易在 track 開頭留下一段緩慢的「漂移」。
- 10 Hz：$\tau$ = 16 ms、衰減到 1% 約 $\ln 0.01 / \ln R \approx 3516$ 個 sample = 73 ms（DC blocker 的實作與 step response 量測見第 02 章練習 2），20 Hz 處只衰減 0.97 dB，人耳幾乎聽不到差異，是常見的折衷。

**4. dry/wet 沒有對齊？**

wet 經過 4x oversampler，延遲了 36 個 sample；dry 沒延遲。兩者相加 = 一個 feedforward comb filter：$y = (1-m)\,x[n] + m\,x[n - D]$。頻率 $f$ 時兩路的相位差是 $2\pi f D / f_s$，相位差為 $\pi$ 的奇數倍時互相抵消：

$$
f_{\text{notch}} = \frac{(2k+1) f_s}{2D} = \frac{(2k+1) \cdot 48000}{72} = 667,\ 2000,\ 3333,\ \ldots \text{ Hz}
$$

第一個 notch 約 667 Hz。解法：dry 延遲 `getLatencySamples()` 個 sample（`Saturation` 內部的 `pushAndReadDry()` 就是這樣做的）。

---

## 練習 2：手算 $x^2$、$x^3$ 與 cubic soft clip

解答程式：`solutions/ex2_cubic_harmonics.cpp`（target `l06_ex2_solution`）

**1.** $x^2 = A^2\sin^2\theta = \dfrac{A^2}{2} - \dfrac{A^2}{2}\cos 2\theta$：DC $= A^2/2$、二次諧波振幅 $A^2/2$，**沒有基頻**。程式用 $A = 0.8$：量到 DC 0.3200、H2 0.3200、H1 < 0.001。

**2.** $\sin^3\theta = \dfrac{3\sin\theta - \sin 3\theta}{4}$，所以 $x^3 = \dfrac{3A^3}{4}\sin\theta - \dfrac{A^3}{4}\sin 3\theta$。$H_3/H_1 = 1/3 \Rightarrow 20\log_{10}(1/3) = -9.54$ dB，**跟 $A$ 無關**（因為兩項都正比於 $A^3$）。程式在 $A = 0.5$ 與 $A = 1$ 都量到 −9.54 dB。

**3.** $y = x - x^3/3 = \left(A - \dfrac{A^3}{4}\right)\sin\theta + \dfrac{A^3}{12}\sin 3\theta$。

| $A$ | $H_1$ | $H_3$ | $H_3/H_1$ | THD |
|---|---|---|---|---|
| 0.5 | $0.5 - 0.03125 = 0.46875$ | $0.125/12 = 0.0104167$ | $20\log_{10}(0.022222) = -33.06$ dB | 2.222 % |
| 1.0 | $1 - 0.25 = 0.75$ | $1/12 = 0.08333$ | $20\log_{10}(0.11111) = -19.08$ dB | 11.111 % |

$|x| \le 1$ 時只有三次諧波，所以 THD $= H_3/H_1$。程式量到的值與理論完全一致（±0.05 dB 內）。

**4.** $y = A\sin\theta + kA^2\sin^2\theta = \dfrac{kA^2}{2} + A\sin\theta - \dfrac{kA^2}{2}\cos 2\theta$。DC $= kA^2/2$、$H_2/H_1 = kA/2$。$k = 0.5$、$A = 0.5$：$H_2/H_1 = 0.125 \Rightarrow -18.06$ dB，DC = 0.0625。注意 $H_2/H_1$ 正比於 $A$：**偶次失真隨音量線性增加**，這也是為什麼 tube 前級「彈大聲才髒」。

---

## 練習 3：alias 頻率

解答程式：`solutions/ex3_alias_frequencies.cpp`（target `l06_ex3_solution`）

**1.** $f_s = 44.1$ kHz、Nyquist 22.05 kHz：

| $k$ | $k f_0$ | 1x 折返到 |
|---|---|---|
| 3 | 15 kHz | 15 kHz（真正的諧波） |
| 5 | 25 kHz | 44.1 − 25 = **19.1 kHz** |
| 7 | 35 kHz | 44.1 − 35 = **9.1 kHz** |
| 9 | 45 kHz | 45 − 44.1 = **0.9 kHz** |
| 11 | 55 kHz | 55 − 44.1 = **10.9 kHz** |
| 13 | 65 kHz | 65 − 44.1 = **20.9 kHz** |
| 15 | 75 kHz | 88.2 − 75 = **13.2 kHz** |

**2.** 9 次諧波 → 900 Hz，振幅是基頻的 1/9：$20\log_{10}(1/9) = -19.08$ dB。程式用「正弦波 × 100 再 clamp 到 ±1」做出近似方波，量到 −19.10 dB（其他極高次諧波，例如第 873、891 次，也會折到 900 Hz，但振幅只有 1/9 的 1% 左右）。它特別難聽，因為：(a) 它比 5 kHz 低了兩個多八度；一個音的遮蔽效應主要往高頻延伸，低於基頻很多的成分幾乎不會被遮蔽，所以很突兀；(b) 它與 5 kHz 沒有整數倍關係，是不和諧的音；(c) 輸入音高改變時，它的移動方向和速度都跟音高對不上：這個 alias $= 9f_0 - f_s$，會以 **9 倍**的速度**同向**滑動（$f_0$ = 5010 Hz 時變成 $9 \times 5010 - 44100 = 990$ Hz，基頻只動了 10 Hz，它動了 90 Hz）；而 5 次、7 次的 alias（$f_s - kf_0$，19.1 kHz、9.1 kHz）則是**反向**滑動。程式另外量了 9.1 kHz（7 次的 alias），−16.91 dB vs 理論 −16.90 dB；以及對照組 800 Hz，低於量測地板。

**3.** 2x 時先在 88.2 kHz 下摺：

| $k$ | $k f_0$ | 在 88.2 kHz 下 | 降頻後 |
|---|---|---|---|
| 5 | 25 kHz | 25 kHz | > 22.05 kHz → **濾掉**（嚴格說它還在 transition band 的尾端，但已被衰減約 45 dB，再折回 19.1 kHz 時只剩 −59 dB 左右，可視為擋掉） |
| 7 | 35 kHz | 35 kHz | 濾掉 |
| 9 | 45 kHz | 88.2 − 45 = 43.2 kHz | 濾掉 |
| 11 | 55 kHz | 33.2 kHz | 濾掉 |
| 13 | 65 kHz | 23.2 kHz | 在 transition band（44.1 kHz 時第 1 級約 18.7～25.7 kHz），**只被衰減約 15 dB**，降頻後折回 44.1 − 23.2 = **20.9 kHz**（相對基頻約 −22.3 − 14.6 ≈ −37 dB） |
| 15 | 75 kHz | 13.2 kHz | **仍然 alias** 到 13.2 kHz |

結論：2x 擋掉大部分，但更高次的諧波照樣會折回來（理想 brickwall filter 下的安全上限是 $2f_s - f_s/2 = 66.15$ kHz；實際的 halfband 有 transition band，所以 65 kHz 這種貼近上限的諧波也只被擋掉一部分）。程式的表格用本章 `Oversampler` 第 1 級的係數算出 transition band 內的實際衰減量。方波的諧波只以 $1/k$ 衰減，所以 hard clip 需要比 tanh 高得多的倍率，或搭配 ADAA（練習 6）。

---

## 練習 4：Chebyshev 諧波 exciter

解答程式：`solutions/ex4_chebyshev_exciter.cpp`（target `l06_ex4_solution`）

**關鍵思路**
- `chebyshev(n, x)` 用遞迴 $T_{n+1} = 2xT_n - T_{n-1}$；程式先驗證 $|T_n(\cos\theta) - \cos n\theta| < 10^{-5}$。
- `processSample()` 不必呼叫四次 `chebyshev()`：同一個遞迴從 $T_2$ 一路算到 $T_5$，每算出一個就乘上 $g_n$ 加進去。
- 一定要先 `clamp(x, -1, 1)`：$|x| > 1$ 時 $T_n$ 呈指數成長（$T_5(2) = 362$）。

**量測**（振幅 1、1 kHz）：H2 −20.00 dB、H3 −26.02 dB、H5 −33.98 dB，與 $20\log_{10} g_n$ 完全一致；H4、H6 為 −100 dB（不存在）。5 次多項式最多產生 5 次諧波，所以 6 次以上一定是 0。

**手算振幅 0.5**：只有 $T_2$ 產生偶次成分（$g_4 = 0$）。

$$
T_2(A\cos\theta) = 2A^2\cos^2\theta - 1 = A^2\cos 2\theta + (A^2 - 1)
$$

所以 DC $= g_2(A^2 - 1) = 0.1 \times (0.25 - 1) = -0.075$、H2 振幅 $= g_2 A^2 = 0.025$。程式量到 −0.0750 與 0.0250。這說明了 Chebyshev exciter 的限制：只在設計振幅下精確，振幅改變時諧波比例會變（H2/H1 從 0.1 變成 0.05），而且偶次項帶來 DC，後面要接 DC blocker。

---

## 練習 5：latency 對齊的 dry/wet mix 與 comb notch

解答程式：`solutions/ex5_aligned_dry_wet.cpp`（target `l06_ex5_solution`）

（DC blocker 的實作與量測在第 02 章練習 2，這裡不重複。）

**關鍵思路**
- `DryDelay` 先寫再讀：`buf_[pos_] = x`，讀 `buf_[(pos_ - D + size) % size]`。長度 `maxDelay + 1` 才放得下 $x[n]$ 到 $x[n - D]$ 共 $D + 1$ 個 sample；先寫後讀才能讓 $D = 0$ 時輸出等於輸入。
- 小訊號時 $\tanh(gx)/g \approx x$，wet 就是 $x[n - L]$，所以整個系統是 feedforward comb：$y[n] = (1 - m)\,x[n - D] + m\,x[n - L]$。令 $\Delta = L - D$，相位差 $2\pi f\Delta/f_s = (2k + 1)\pi$ 時兩路反相：

$$
f_k = \frac{(2k + 1)\,f_s}{2\Delta}
$$

- notch 深度：兩路相量長度分別是 $1 - m$ 與 $m$，反相時剩 $|1 - 2m|$。$m = 0.5$ 時理論上無限深，$m = 0.3$ 時 $20\log_{10} 0.4 = -7.96$ dB。
- 對齊後 $|H| = |(1 - m) + m| = 1$，與 $m$ 無關。

**執行輸出**

```
4x oversampler latency L = 36 samples（0.750 ms）

[沒對齊] dry 不延遲，mix 50%：
  notch 0：預測   666.7 Hz，量到   -89.6 dB
  notch 1：預測  2000.0 Hz，量到   -96.1 dB
  notch 2：預測  3333.3 Hz，量到  -103.3 dB
  兩個 notch 中間的峰值（1333.3 Hz）：+0.000 dB
  掃頻 500～850 Hz：最深處 666 Hz（-56.1 dB）
  mix 30%：667 Hz -7.96 dB（理論 20·log10|1 - 2m| = -7.96 dB）

[對齊] dry 延遲 36 samples，mix 50%：
     666.7 Hz：+0.0003 dB
    2000.0 Hz：+0.0001 dB
    3333.3 Hz：-0.0001 dB
   10000.0 Hz：-0.0001 dB

[差 1 sample] dry 延遲 35 samples：
     1000 Hz：-0.018 dB（理論 -0.019 dB）
    10000 Hz：-2.011 dB（理論 -2.011 dB）
[PASS] l06_ex5 (14 checks passed)
```

- 預測的 notch 位置上量到 −90 dB 以下，深度只受 oversampler passband 誤差與 $\tanh$ 的微小非線性限制。掃頻的最深處是 666 Hz（−56 dB）而不是更深，是因為 2 Hz 的格點離真正的 666.67 Hz 還差 0.67 Hz——notch 非常窄，這也是 comb filter 聽起來「空洞」而不是「少了某個頻段」的原因：它是一整排細細的缺口。
- 對齊後四個頻率都在 ±0.001 dB 內，證明 FIR halfband 的 latency 是乾淨的整數延遲（README 2.6 節的線性相位）。
- 差一個 sample 時 $|H| = |\cos(\pi f/f_s)|$：1 kHz 幾乎沒差（−0.02 dB），10 kHz 已經 −2 dB，所以「差不多對齊」不夠——latency 必須精確到 sample。

**想一想的答案**：mix = 100% 時 dry 不參與混合，延遲與否都聽不到差別（但 plugin 仍要向 host 回報 latency，讓它跟其他 track 對齊）。IIR halfband 的相位不是線性的，wet 相對 dry 的「延遲」隨頻率改變，用任何整數 delay 都只能在某些頻率對齊，這就是 README 2.6 節表格裡 IIR 版「mix 會有 comb filtering 或相位偏移」的意思。

---

## 練習 6：一階 ADAA hard clip

解答程式：`solutions/ex6_adaa.cpp`（target `l06_ex6_solution`）

**1. 反導函式**

$$
F(x) = \begin{cases} x^2/2 & |x| \le 1 \\ |x| - 1/2 & |x| > 1 \end{cases}
$$

$|x| \le 1$ 時 $F' = x$；$x > 1$ 時 $F' = 1$；$x < -1$ 時 $F' = -1$。在 $x = \pm 1$ 處兩段的值都是 1/2，所以連續。程式用數值微分在 −3～3 之間檢查 $F' = f$。

**2. 實作要點**
- 全部用 `double`：$F(x[n]) - F(x[n-1])$ 是兩個接近的數相減。
- $|\Delta| \le 10^{-6}$ 時用中點 $f\big((x[n] + x[n-1])/2\big)$——這是「$f$ 在線段上的平均」在 $\Delta \to 0$ 時的極限，所以輸出連續。
- 狀態 `x1_` 跨 block 保留。

**3. 量測**（7 kHz、peak 2、48 kHz）

| 方法 | 基頻 (dBFS) | alias 總和 |
|---|---|---|
| naive 1x | 1.72 | −24.6 dB |
| ADAA 1x | 1.32 | −42.1 dB |
| naive 2x | 1.72 | −36.8 dB |
| ADAA + 2x | 1.62 | −56.0 dB |
| naive 4x | 1.71 | −49.2 dB |
| naive 8x | 1.71 | −61.7 dB |

ADAA 在 1x 就改善 17.5 dB，比 naive 2x 還乾淨；與 2x 結合再改善 19.3 dB。最後兩列是對照組：只靠 oversampling 的 hard clip，倍率每翻一倍只改善 12～13 dB（諧波以 $1/k$ 衰減），8x 還有 −61.7 dB；「ADAA + 2x」用少得多的運算就到了同一個量級（程式檢查兩者差距 < 10 dB）。

**4. 線性區等於什麼？** $|x| \le 1$ 時 $y[n] = \dfrac{x[n]^2/2 - x[n-1]^2/2}{x[n] - x[n-1]} = \dfrac{x[n] + x[n-1]}{2}$——2-tap 平均（moving average）。頻率響應 $H(e^{j\omega}) = e^{-j\omega/2}\cos(\omega/2)$：**延遲半個 sample**，幅度 $\cos(\omega/2)$。7 kHz 時 $\cos(\pi \cdot 7/48) = 0.897 \Rightarrow -0.94$ dB；程式量到 naive 與 ADAA 的基頻差 0.40 dB（clip 區的行為不同於線性區，所以不完全等於 −0.94 dB）。在 2x 下同一個 7 kHz 只衰減 $\cos(\pi \cdot 7/96)$ ≈ −0.23 dB，表中 ADAA + 2x 的基頻損失只剩 0.10 dB。

半個 sample 的延遲在 plugin 裡通常忽略不計（或與 dry 路徑用分數延遲對齊），但如果把 ADAA 放在回授迴路裡就必須考慮。

---

## 練習 7：oversampling 的品質與成本

解答程式：`solutions/ex7_oversampling_cost.cpp`（target `l06_ex7_solution`）

**關鍵思路**
- `aliasDb()`：與範例 `l06_aliasing_demo` 相同的量測，跳過開頭 4800 samples（latency + 參數 ramp）。
- `timeMs()`：每個倍率跑 3 次取最小值（最小值最接近「沒有被作業系統打擾」的真實成本）；複製 buffer 與 `prepare()` 都在計時之外。
- CPU 佔用 = 處理時間 / 音訊長度。

**作者機器上的實測**（`-Config Release` 建置；時間會隨機器變動，alias 數字則不會）：

| 倍率 | latency | alias 總和 | 耗時 | 相對 1x | CPU 佔用 |
|---|---|---|---|---|---|
| 1x | 0 | −17.9 dB | 11.9 ms | 1.00 | 0.12 % |
| 2x | 31 | −38.3 dB | 37.3 ms | 3.15 | 0.37 % |
| 4x | 36 | −85.2 dB | 70.3 ms | 5.93 | 0.70 % |
| 8x | 38 | −111.5 dB | 124.3 ms | 10.49 | 1.24 % |

Debug 建置慢 3～4 倍，相對倍率在幾次執行之間跳動（2x 約 3.8～4.7、4x 約 6.6～8.4、8x 約 12～15）。所以比較成本時一定要用 Release，並跑多次取最小值。

**怎麼選？** 對 tanh 這種平滑曲線：2x 改善 20 dB，但 −38 dB 的 alias 在高頻素材上仍聽得到；4x 降到 −85 dB，已經接近 16-bit 的底噪（約 −96 dB）、低於大部分素材的雜訊；8x 的 −111 dB 聽不出差別，卻多花了約 1.8 倍的 CPU。所以 **4x 是甜蜜點**，很多商用 saturation 也預設 2x～4x，並讓使用者在 mixdown 時切到更高倍率。

**換成 hard clip 呢？** Hard clip 的諧波只以 $1/k$～$1/k^2$ 衰減（尖角 → 頻譜收斂慢），即使 8x 也還有明顯的 alias（練習 3 的分析：每個倍率都有一批更高次的諧波會折回來；練習 6 的量測：naive hard clip 8x 仍有 −61.7 dB）。這時更划算的做法是 ADAA + 2x～4x（練習 6），或者把 hard clip 換成 soft clip。

另外注意 1x → 2x 的成本跳得特別多：1x 的 Oversampler 只是複製，而 2x 開始要跑 32-tap 的 halfband FIR（升、降各一次），再加上兩倍的 `tanh` 呼叫。之後每多一級，增加的是「更高 sample rate 下的 `tanh`」加上較短的 FIR。
