# 第 03 章 解答與解說：Parametric EQ

實作題的完整程式在本資料夾，每支都用 `DAWLAB_CHECK` / `DAWLAB_CHECK_NEAR` 以數字驗證，最後印出 `[PASS] l03_exN (...)`。用 ctest 跑全部解答：

```powershell
.\scripts\build.ps1 -Only 03-eq -TestLabel solution
```

下面的數字都是解答程式實際印出的值（48 kHz）。

---

## 練習 1：手算 A、Q、bandwidth 與 RBJ 係數

**1. A 值。**

$$
A = 10^{g/40}:\quad A(+6) = 10^{0.15} = 1.4125,\qquad A(-12) = 10^{-0.3} = 0.5012
$$

+6 dB peak 的中心線性增益是 $A^2 = 10^{6/20} = 1.9953$（約 2 倍）。$A$ 是「中心增益的平方根」，同時也是頻寬邊緣的增益（README 2.5 節）。

**2. Q ↔ bandwidth。**

$$
Q(N = 2/3) = \frac{2^{1/3}}{2^{2/3} - 1} = \frac{1.2599}{0.5874} = 2.1449
$$

$$
N(Q = 2) = \frac{2}{\ln 2}\operatorname{asinh}\left(\frac{1}{4}\right) = 2.8854 \times 0.24747 = 0.7140\ \text{octave}
$$

**3. 頻寬邊緣。** $\frac{1}{2Q} = \frac{1}{2\sqrt2} = 0.35355$，$\sqrt{1 + \frac{1}{4Q^2}} = \sqrt{1.125} = 1.06066$：

$$
f_1 = 1000 \times (1.06066 - 0.35355) = 707.1\ \text{Hz},\qquad f_2 = 1000 \times (1.06066 + 0.35355) = 1414.2\ \text{Hz}
$$

剛好是 $f_0 / \sqrt2$ 與 $f_0 \cdot \sqrt2$（1 octave 寬，以 $f_0$ 為幾何中心）。+12 dB 時邊緣是中點增益 +6 dB（數位版實測 +5.989 / +5.978 dB，差異來自 bilinear warping）。

**4. RBJ 係數。** $\omega_0 = 2\pi \cdot 12000/48000 = \pi/2$，$\cos\omega_0 = 0$，$\sin\omega_0 = 1$，$\alpha = \frac{1}{2Q} = 0.5$，$A = 1.41254$：

$$
\begin{aligned}
b_0 &= 1 + \alpha A = 1.70627, & b_1 &= 0, & b_2 &= 1 - \alpha A = 0.29373\\
a_0 &= 1 + \alpha/A = 1.35397, & a_1 &= 0, & a_2 &= 1 - \alpha/A = 0.64603
\end{aligned}
$$

除以 $a_0$：$b_0 = 1.26019$、$b_2 = 0.21694$、$a_2 = 0.47713$、$b_1 = a_1 = 0$。

- DC（$z = 1$）：$\frac{b_0 + b_2}{1 + a_2} = \frac{1.47713}{1.47713} = 1$ → 0 dB。
- $f_0$（$z = j$，$z^{-2} = -1$）：$\frac{b_0 - b_2}{1 - a_2} = \frac{1.04325}{0.52287} = 1.9952$ → +6.00 dB（$= A^2$）。

`l03_ex2_solution` 最後一段「練習 1 對照」用程式驗證了以上所有數字。

**5. +6 dB 與 −6 dB 串接。** 整條曲線是**完全平的 0 dB**，相位也是 0。因為 RBJ peak 的類比原型 $H_{+g}(s) = \frac{s^2 + sA/Q + 1}{s^2 + s/(AQ) + 1}$，把 $A$ 換成 $1/A$ 剛好分子分母互換，$H_{-g} = 1/H_{+g}$；bilinear transform 是同一個代換，數位版也成立。`l03_ex3_solution` 驗證：20 Hz–20 kHz 最大偏差 $1.07 \times 10^{-13}$ dB（只剩浮點誤差）。

---

## 練習 2：Q ↔ bandwidth 換算與驗證

解答：[`ex2_q_bandwidth.cpp`](ex2_q_bandwidth.cpp)（target `l03_ex2_solution`）

```cpp
double qToBandwidthOctaves(double q) { return 2.0 / std::log(2.0) * std::asinh(1.0 / (2.0 * q)); }
double bandwidthOctavesToQ(double bw) { const double p = std::pow(2.0, bw); return std::sqrt(p) / (p - 1.0); }
```

`bandEdges` 是 README 2.4 節二次方程的兩個根乘上 $f_0$。輸出：

```
    Q =  0.707  ->  BW = 1.900 oct
    Q =  1.414  ->  BW = 1.000 oct
    Q =  4.319  ->  BW = 0.333 oct
    BW = 1.000 oct  ->  Q = 1.4142
    BW = 0.333 oct  ->  Q = 4.3185
  f0 = 1000 Hz, Q = sqrt(2)（1 octave）：邊緣 = 707.1 Hz / 1414.2 Hz
  peak +12 dB：|H| @ 707.1 = +5.989 dB, @ 1000 = +12.000 dB, @ 1414.2 = +5.978 dB
  bandpass    ：|H| @ 707.1 = -3.020 dB, @ 1000 = +0.000 dB, @ 1414.2 = -3.029 dB
  實測 peak @ 1414.2 Hz = +5.978 dB
```

**重點**：

- 同一組頻率上，peak 是「中點增益」、bandpass 是 −3 dB——這不是巧合，README 2.5 節證明了兩者是同一個條件 $|1 - \Omega^2| = \Omega/Q$。
- 常被誤記的一點：Q = 0.707 不是「1 octave」，而是約 1.9 octave；1 octave 是 Q = √2。
- 數位版的邊緣增益比 +6 dB 少 0.01–0.02 dB，是 bilinear transform 的頻率 warping；頻率越接近 Nyquist 差越多。

---

## 練習 3：驗證兩個 peak 串接 dB 相加

解答：[`ex3_cascade_sum.cpp`](ex3_cascade_sum.cpp)（target `l03_ex3_solution`）

`Cascade2::processSample` 就是 `b_.processSample(ch, a_.processSample(ch, x))`。輸出（節錄）：

```
  freq(Hz)   H1(dB)   H2(dB)   H1+H2   |H1·H2|   實測 p1->p2   實測 p2->p1
      500    +6.00    -0.07    +5.93    +5.93      +5.93         +5.93
     1414    +0.88    -1.33    -0.45    -0.45      -0.45         -0.45
     2000    +0.42    -4.00    -3.58    -3.58      -3.58         -3.58
```

**為什麼 dB 相加？** 串接 = 摺積（convolution）= 頻域相乘：$H = H_1 H_2$，$|H| = |H_1||H_2|$，取 $20\log_{10}$ 後乘法變加法。相位 $\angle H = \angle H_1 + \angle H_2$ 也相加。乘法可交換，所以正反兩種順序的實測完全一樣。

**兩顆一樣的 +6 dB 疊起來**：每個頻率的 dB 都加倍，中心 +12.00、1414 Hz +5.98（單顆 +2.99）。曲線在 dB 軸上被拉高兩倍，「中點增益」邊緣的位置不變。它跟單顆 +12 dB、Q = √2 的 peak **很接近，但不完全相同**：中心（+12 dB）與中點增益邊緣（707 / 1414 Hz 都約 +6 dB）一樣，skirt 卻比較窄——例如 500 / 2000 / 4000 Hz 處，兩顆 +6 dB 串接是 +2.26 / +2.24 / +0.42 dB，單顆 +12 dB 是 +2.51 / +2.49 / +0.51 dB（48 kHz、RBJ 數位公式）。原因是 RBJ peak（類比原型，README 2.5 節）的 $|H|^2 = \dfrac{x + A^2}{x + A^{-2}}$（$x = Q^2(1-\Omega^2)^2/\Omega^2$），兩顆串接是 $\left(\dfrac{x + A^2}{x + A^{-2}}\right)^2$，單顆 +12 dB 則是把 $A$ 換成 $A^2$：$\dfrac{x + A^4}{x + A^{-4}}$。兩者只在 $x = 0$（中心）與 $x = 1$（中點增益邊緣）相等，其他地方不同。想要「更寬」或「更窄」就得調 Q。

---

## 練習 4：10-band graphic EQ

解答：[`ex4_graphic_eq.cpp`](ex4_graphic_eq.cpp)（target `l03_ex4_solution`，輸出 `l03_ex4_graphic_smile.wav`）

`kQ = 2^(1/2) / (2^1 − 1) = √2`。輸出：

```
  只推 1 kHz +6 dB：
      500.0 Hz: 理論  +1.13 dB, 實測  +1.13 dB
      707.1 Hz: 理論  +2.99 dB, 實測  +2.99 dB
     1000.0 Hz: 理論  +6.00 dB, 實測  +6.00 dB
     2000.0 Hz: 理論  +1.12 dB, 實測  +1.12 dB
  全部 +6 dB：
    1 kHz 理論 +8.80 dB；125 Hz–4 kHz 之間 曲線在 +6.89 .. +8.83 dB（漣波 1.94 dB）
```

**討論**：

- 推一個 band，相鄰 band 的中心（1 octave 外）也被帶起約 +1.1 dB——peak 的 skirt 很寬。
- 全部推 +6 dB，1 kHz 處是 **+8.80 dB** 而不是 +6：每個頻率都同時落在好幾個 band 的 skirt 上，dB 一起相加。band 中心之間（例如 707 Hz）又比中心低一點，形成約 1.9 dB 的漣波。所以 graphic EQ 的推桿形狀 ≠ 實際曲線。
- 改善方向：（1）把 Q 調大，漣波變大但互相影響變小；（2）本題的設計本來就是 constant-Q（Q 固定為 √2，不隨 gain 改變）；若改用 proportional Q（README 2.11 節），小 gain 時 band 較寬、互相影響更大，大 gain 時較窄、互相影響較小，兩者各有取捨，但都不會讓疊加問題消失；（3）更進階的設計會解一個小的線性方程，讓「實際曲線在 band 中心的值」剛好等於推桿值（把 band 之間的交互作用反解回去）。
- 笑臉曲線（低高頻提、中頻挖）讓 drum loop 的 peak 從 −2.92 升到 +0.05 dBFS——超過 0 dBFS！這是 graphic EQ 常見的陷阱，記得降 output gain。

---

## 練習 5：Tilt EQ

解答：[`ex5_tilt_eq.cpp`](ex5_tilt_eq.cpp)（target `l03_ex5_solution`，輸出 `l03_ex5_tilt.wav`）

```cpp
low_.setCoefficients(BiquadCoefficients::makeLowShelf(sampleRate_, pivotHz_, kShelfQ, -0.5 * tiltDb));
high_.setCoefficients(BiquadCoefficients::makeHighShelf(sampleRate_, pivotHz_, kShelfQ, 0.5 * tiltDb));
```

輸出（pivot 1 kHz、tilt +6 dB）：

```
    freq(Hz)   理論(dB)   實測(dB)
          30     -3.00      -3.00
         500     -2.64      -2.64
        1000     -0.00      +0.00
        2000     +2.65      +2.65
       16000     +3.00      +3.00
```

**為什麼 pivot 剛好是 0 dB？** RBJ shelf 在 $f_0$ 處的增益是中點 $A$（gain 的一半，README 2.6 節）。low shelf 在 pivot 貢獻 $-t/4$，high shelf 貢獻 $+t/4$，相加 0。

**為什麼反對稱？** low shelf 的 $s$ 換成 $1/s$ 就是 high shelf（在 log 頻率軸上以 $f_0$ 為鏡面），gain 又剛好相反，所以 $\text{dB}(f_0 \cdot 2^k) = -\text{dB}(f_0 \cdot 2^{-k})$。高頻端有一點點 bilinear warping，self-check 在 2 octave 處容許 0.15 dB。

**為什麼要 Q = 0.707？** S = 1，兩顆 shelf 各自單調，加起來也單調、不會有駝峰。Q 越小轉折越緩，tilt 的「傾斜」越平均；可以自己試 Q = 0.5 看看曲線。

**平滑**：`setTiltDb` 只設 `SmoothedValue` 的目標；`process()` 每 32 個 sample `skip(32)` 一次、用目前值重算兩顆 shelf 的係數——跟 `ParametricEq` 同一招。WAV 在 2 秒處從亮切到暗，聽不到 click。

---

## 練習 6：可切換 12 / 24 dB/oct 的 high cut

解答：[`ex6_switchable_highcut.cpp`](ex6_switchable_highcut.cpp)（target `l03_ex6_solution`，輸出 `l03_ex6_highcut_switch.wav`）

輸出（fc = 1 kHz）：

```
  12 dB/oct：  1000 Hz -3.01 dB，2000 Hz -12.37 dB，4000 Hz -24.48 dB（類比理想 -12.30 / -24.10）
  24 dB/oct：  1000 Hz -3.01 dB，2000 Hz -24.25 dB，4000 Hz -48.92 dB（類比理想 -24.10 / -48.16）
  4 階的兩顆在 fc：Q=0.5412 -> -5.33 dB，Q=1.3066 -> +2.32 dB，相加 -3.01 dB
  1 kHz 正弦波（= fc）中途 12 -> 24 dB/oct：最大 sample 跳變
    直接切換        0.3505（正弦波本身最多 0.0654）
    20 ms crossfade 0.0461
```

**重點**：

- **4 階 Butterworth 不是兩顆 Q = 0.707**：那樣 $f_c$ 處是 −6 dB。正確的 Q 由極點位置決定（README 2.3 節），兩顆各自在 $f_c$ 是 −5.33 與 +2.32 dB，相加才是 −3.01。
- **比類比更陡**：數位 Butterworth 的 $(f/f_c)^{2N}$ 被 $\tan(\pi f/f_s)/\tan(\pi f_c/f_s)$ 取代，越接近 Nyquist 衰減越多（Nyquist 處是零點，−∞ dB）。
- **為什麼直接切換會 click**：在 $f_c$ 處 2 階 Butterworth 的相位是 −90°、4 階是 −180°。兩條鏈輸出振幅相同（都 −3 dB，振幅 0.354）但相差 90°，差值 $y_{24} - y_{12} = 0.5\cos(\theta + 45°)$。在第 48 個 block（0.512 s）切換時 $\theta = 0°$，差值約 0.35，所以輸出瞬間跳 0.35，是正弦波自然變化量的 5 倍以上。
- **crossfade 的做法**：兩條鏈**一直同時在跑**（狀態永遠是新鮮的），只有輸出用 mix 混合：$y = y_{12} + m(y_{24} - y_{12})$，$m$ 以 20 ms 線性 ramp 從 0 走到 1。代價是切換期間兩條鏈都要算。`ParametricEq` 的 `setSlope()` 用的是另一種做法（先淡出到 bypass 再淡入新的 slope），不需要同時維護兩條鏈，但會短暫經過「沒有 EQ」的狀態。

---

## 練習 7：EQ 與相位

**1. 為什麼 IIR EQ 一定會改變相位？** IIR filter 靠回授（極點）在某些頻率「累積」能量來造成 boost / cut；累積需要時間，所以不同頻率的延遲不同 → 相位偏移。數學上，因果、穩定的 rational filter 若極點和零點都在單位圓內（或上），稱為 **minimum phase**；這類系統的 log-magnitude 與 phase 是一對 Hilbert transform，**magnitude 一旦決定，相位也就決定了**，而且是所有同 magnitude 的因果系統裡延遲最小的那一個。所以「只改 magnitude、不改相位」的 IIR EQ 不存在。

**2. 相位的數值**（`l03_eq_phase_cramping` 的 A 段）：+6 dB @ 1 kHz、Q = 1，500 Hz 處 +18.00°（領先）、2 kHz 處 −17.97°（落後）、中心 0°。改成 −6 dB 時相位剛好反號（500 Hz −18°、2 kHz +18°；程式印出 −6 dB 在 707 Hz 是 −18.39°），因為 $H_{-g} = 1/H_{+g}$，相位取負。

**3. 什麼時候相位會變成問題？** 單軌的相位偏移只有十幾度、群延遲零點幾 ms，耳朵幾乎聽不出來。問題在**同一個聲音的兩個版本相加**時：

- 多支麥克風收同一個來源（鼓組的 kick in / kick out、overhead），只對其中一支 EQ → 兩者相位關係改變，相加後某些頻率抵消（comb filtering）。
- Parallel processing（dry + 處理過的 wet）或 multiband 的頻段重組。
- 對 stereo 的 L / R 套不同的 EQ → 影像（imaging）偏移。

**4. Linear-phase 的代價。**（a）latency：對稱 FIR 延遲 $(N-1)/2$ samples，4097 taps → 2048 samples → $2048/48000 = 42.7$ ms；要解析低頻就需要長 FIR，latency 跟著變長，DAW 必須做 plugin delay compensation，也不適合即時監聽。（b）pre-ringing（下一題）。（c）CPU：長 FIR 要用 FFT 分段摺積（第 12 章）。（d）改參數時要重新設計整個 FIR，平滑也比較麻煩。

**5. Pre-ringing。** Linear-phase filter 的 impulse response 是對稱的：主峰前後各有一半的振盪。所以在一個 transient（例如鼓的起音）**之前**就會聽到 filter 的振盪——像是聲音「還沒打就先吸一口氣」。在自然界，聲音的共鳴只會發生在激發**之後**（post-ringing），所以 pre-ringing 聽起來特別不自然；kick、snare 這類尖銳 transient 加上窄而深的 EQ（Q 大 → 振盪久）最明顯。

**6. 40 Hz 窄 cut 的取捨。** 窄（Q 大）、低頻 → linear-phase 版需要非常長的 FIR（低頻 + 窄頻寬 = 長 impulse response），latency 大、pre-ringing 也拉得很長，而且就落在 kick / bass 的基頻上，最容易被聽到。minimum-phase 版只會在 40 Hz 附近有一點相位偏移與 post-ringing，通常比較安全。除非這個 cut 會影響和其他平行訊號的相位關係（例如 multiband 處理），一般選 minimum-phase；有疑慮就兩種都試，**音量對齊**後盲聽比較。
