# 第 02 章練習解答
[English](README.md) | **繁體中文**

實作題的完整程式在本資料夾（`exN_xxx.cpp`），每一支都用 `DAWLAB_CHECK` / `DAWLAB_CHECK_NEAR` 以數字驗證，由 ctest（label `solution`）自動執行。下面引用的數字都是解答程式（或範例程式）實際印出的結果（$f_s = 48$ kHz）。

```powershell
.\scripts\build.ps1 -Only 02-filter -TestLabel solution
cd build
.\bin\Debug\l02_ex3_solution.exe
```

---

## 練習 1：手算 one-pole 係數

**1.** $f_c = 1$ kHz：

$$
\frac{2\pi f_c}{f_s} = \frac{2\pi\cdot1000}{48000} = 0.130900,\qquad
a = e^{-0.130900} = 0.877306,\qquad 1 - a = 0.122694
$$

$f_c = 100$ Hz：指數是 0.0130900，$a = e^{-0.0130900} = 0.986995$，$1 - a = 0.013005$。cutoff 降 10 倍，$1-a$ 也大約降 10 倍（低 cutoff 時 $1 - a \approx 2\pi f_c/f_s$）。

**2.** $a = e^{-1/\tau}$ ⇒ $\tau = -1/\ln a = f_s / (2\pi f_c) = 48000 / 6283.19 = 7.64$ samples $= 0.159$ ms。這和 analog RC 的 $\tau = 1/(2\pi f_c)$ 一模一樣——這正是 impulse invariance 的意思。

**3.** 把 $z = -1$ 代入 $H(z) = (1-a)/(1 - a z^{-1})$：

$$
H(-1) = \frac{1-a}{1+a} = \frac{0.122694}{1.877306} = 0.065357 \;\Rightarrow\; -23.69\text{ dB}
$$

它不是 $-\infty$，因為這個 $H(z)$ 沒有任何 zero 在 $z = -1$（分子是常數）。analog RC lowpass 要到「無限高頻」才會衰減到 0，而 impulse invariance 只是在取樣點上複製 impulse response，並沒有把「無限高頻」搬到 Nyquist。

**4.** bilinear 版：

$$
g = \tan\frac{\pi\cdot1000}{48000} = \tan(0.065450) = 0.065543,\quad
\text{pole} = \frac{1-g}{1+g} = 0.876976,\quad
b_0 = b_1 = \frac{g}{1+g} = 0.061512
$$

pole 和 exp 版的 $a$（0.877306）只差 0.00033，低 cutoff 時兩者幾乎一樣。主要差別在分子：bilinear 版是 $b_0(1 + z^{-1})$，多了一個在 $z = -1$ 的 zero，所以 Nyquist 是 $-\infty$ dB，DC 增益則是 $2b_0/(1 - \text{pole}) = 0.123024/0.123024 = 1$。

**5.** $f_c = 10$ kHz：$a = e^{-1.308997} = 0.270091$。

$$
\cos\omega_{3\text{dB}} = 1 - \frac{(1-a)^2}{2a} = 1 - \frac{0.532767}{0.540182} = 0.013727
\;\Rightarrow\; \omega = 1.55707 \text{ rad}
\;\Rightarrow\; f = \frac{\omega f_s}{2\pi} = 11895 \text{ Hz}
$$

比設定值高了 19%。

**對照：** `l02_filter_response` 的 [1] 印出 `a=exp(..)` 欄（0.986995、0.877306、…）、`exp -3dB` 欄（1001.4、11895.1 Hz…）以及「手算對照」兩行（$a$ = 0.877306、Nyquist −23.69 dB、$g$ = 0.065543、pole = 0.876976、$b_0$ = 0.061512），並以 self-check 驗證。

---

## 練習 2：DC blocker

解答：[`ex2_dc_blocker.cpp`](ex2_dc_blocker.cpp)（target `l02_ex2_solution`）

關鍵兩行：

```cpp
const double y = x - s.x1 + r_ * s.y1;   // y[n] = x[n] - x[n-1] + R y[n-1]
// 理論響應：H(e^{jω}) = (1 - e^{-jω}) / (1 - R e^{-jω})
return gainToDb(std::abs((1.0 - z1) / (1.0 - r_ * z1)));
```

結果（$R = 0.995$）：

```
DC：輸入平均 0.5000 → 輸出平均 -0.000000
1 kHz：輸入振幅 0.2500 → 輸出振幅 0.2504（0.015 dB）
-3 dB 點：理論近似 38.2 Hz，由 H(z) 算出 38.1 Hz
實測：38.1 Hz = -3.01 dB，20 Hz = -6.67 dB（理論 -6.67 dB）
step response：τ = 199 samples（4.2 ms），降到 1% 需要 918 samples（理論 ≈ 4.6 τ = 919）
```

**為什麼 −3 dB 點 ≈ $f_s(1-R)/2\pi$：** 在低頻 $e^{-j\omega} \approx 1 - j\omega$，所以 $H \approx \dfrac{j\omega}{(1-R) + jR\omega} \approx \dfrac{j\omega}{(1-R) + j\omega}$。這就是一個 cutoff 在 $\omega_c = 1-R$ 的一階 highpass，換成 Hz 就是 $f_s(1-R)/2\pi$。

**$R$ 的取捨：** $R$ 越接近 1，cutoff 越低，對低音（例如 40 Hz 的 kick）的影響越小；但 pole 越靠近 $z = 1$，時間常數 $\tau = -1/\ln R \approx 1/(1-R)$ 越長，一個突然出現的 DC 偏移要更久才會被「吃掉」（$R = 0.995$ 時 99% 需要約 $4.6\tau$ = 918 samples ≈ 19 ms；$R = 0.999$ 時 $\tau$ 約 1000 samples，就要長 5 倍）。實務上 $R$ 常取 0.995–0.999，或改成依 sample rate 換算：$R = e^{-2\pi f_c / f_s}$，讓 cutoff 不隨 sample rate 改變。

---

## 練習 3：4 階 Butterworth lowpass

解答：[`ex3_butterworth4.cpp`](ex3_butterworth4.cpp)（target `l02_ex3_solution`）

關鍵：兩級用**同一個** $f_c$、**不同的** Q：

```cpp
for (int k = 0; k < 2; ++k)
    stages_[k].setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, hz, butterworthQ(4, k)));
```

$Q_0 = 1/(2\cos(\pi/8)) = 1/(2 \times 0.92388) = 0.5412$，$Q_1 = 1/(2\cos(3\pi/8)) = 1/(2\times0.38268) = 1.3066$。直覺：低 Q 的那級在 cutoff 附近「下垂」，高 Q 的那級在 cutoff 附近「隆起」，兩者相加剛好在通帶最平坦。

**奇數階要小心：** 上面的角度公式 $(2k+1)\pi/(2N)$ 只適用 $N$ 為偶數。$N$ 為奇數時 pole 角度是 $(k+1)\pi/N$，再加一級 bilinear one-pole（README 2.13 節）：3 階 = one-pole + Q = 1.0 的 biquad。解答檔最後驗證了 `butterworthQ(3, 0)` = 1.0、`butterworthQ(5, k)` = 0.6180 / 1.6180，以及 3 階串接在 $f_c$ 是 −3.01 dB、500 Hz 是 −0.067 dB（與 analog 一致）。若誤用偶數公式，$N = 3$ 會得到 Q = 0.5774，$f_c$ 處變成 −7.78 dB。

```
  f (Hz)    analog BW4    2-stage dB      measured   single biquad
     500         -0.02         -0.02         -0.02           -0.26
    1000         -3.01         -3.01         -3.01           -3.01
    2000        -24.10        -24.25        -24.25          -12.37
    4000        -48.16        -48.92        -48.92          -24.48
    8000        -72.25        -75.59        -75.59          -37.80
```

- cutoff 處 −3.01 dB ✓；1 octave 外 −24.25 dB（analog 理論 $-10\log_{10}(1+2^8) = -24.10$ dB，多出的 0.15 dB 是 bilinear 的 cramping）；2 octaves 外 −48.92 dB（analog −48.16）。
- 通帶比單一 biquad 平坦得多：500 Hz 處只掉 0.02 dB（單一 biquad 掉 0.26 dB）。
- 兩級都用 Q = 0.707：cutoff 處是 $2 \times (-3.01) = -6.02$ dB。這是 **Linkwitz-Riley 4 階（LR4）**：LR4 的 LPF 與 HPF 在 cutoff 都是 −6 dB 且相位相同，相加後大小完全平坦，所以用於 crossover（多頻段處理、喇叭分音）。它不是 Butterworth。

輸出 WAV：`l02_ex3_noise_butterworth4.wav`、`l02_ex3_noise_biquad2.wav`。白雜訊的 RMS 只差 0.3 dB（−30.07 vs −30.38 dB，因為大部分剩下的能量在通帶裡），但仔細聽，4 階版本 1 kHz 以上的「嘶」聲更乾淨。

---

## 練習 4：HPF + LPF 組成 bandpass

解答：[`ex4_bandpass_cascade.cpp`](ex4_bandpass_cascade.cpp)（target `l02_ex4_solution`）

```cpp
hp_.setCoefficients(BiquadCoefficients::makeHighpass(sampleRate_, lowHz, BiquadCoefficients::kButterworthQ));
lp_.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, highHz, BiquadCoefficients::kButterworthQ));
// 串接 = 響應相乘 = dB 相加
```

```
  f (Hz)    cascade dB      measured    RBJ BPF dB
   125.0        -24.11        -24.11        -14.57
   500.0         -3.03         -3.03         -3.02
  1000.0         -0.52         -0.52          0.00
  2000.0         -3.03         -3.03         -3.04
  8000.0        -25.69        -25.69        -15.39
```

- **邊界約 −3 dB**：HPF 在 500 Hz 是 −3.01 dB，LPF 在 500 Hz 只貢獻 $-10\log_{10}(1 + (500/2000)^4) = -0.017$ dB，合計 −3.03 dB。
- **中央不是 0 dB**：1 kHz 處 HPF 與 LPF 各自都還沒完全「放開」：各 $-10\log_{10}(1 + 0.5^4) = -0.26$ dB，合計約 −0.53 dB（數位實測 −0.52 dB）。兩個邊界越近，中央凹得越深；邊界很近時就該改用 RBJ bandpass 或 peak。
- **斜率**：串接版兩側都是 12 dB/oct（邊界外 2 octaves 約 −24 dB；8 kHz 那側因 cramping 是 −25.7 dB）；RBJ 二階 bandpass 兩側各只有 6 dB/oct（−14.6 / −15.4 dB，analog 理論 −14.56 dB）。

**優缺點：** 串接版可以獨立設定上下邊界、斜率陡一倍（還可以再串更多級），適合「電話音效」「只留人聲頻段」；代價是兩個 biquad、中央不是精準 0 dB。RBJ bandpass 只要一個 biquad、中心精準 0 dB、用一個 Q 控制寬度，適合需要掃動中心頻率的場合（wah、de-esser 的偵測）。

輸出 WAV：`l02_ex4_noise_cascade.wav`、`l02_ex4_noise_rbj_bpf.wav`（RMS −28.37 vs −27.27 dB：斜率陡的版本留下的能量更少，聽起來更「窄」）。

---

## 練習 5：由係數算 pole，判斷穩定性

**手算：** $p = \dfrac{-a_1 \pm \sqrt{a_1^2 - 4a_2}}{2}$

| | 判別式 $a_1^2 - 4a_2$ | pole | $\max\lvert p\rvert$ | 三角形條件 | 結論 |
|---|---:|---|---:|---|---|
| (a) $-1.8, 0.81$ | $3.24 - 3.24 = 0$ | 0.9（重根） | 0.9 | $0.81 < 1$，$1.8 < 1.81$ ✓ | 穩定 |
| (b) $-1.9, 1.0$ | $3.61 - 4 = -0.39$ | $0.95 \pm 0.3122j$ | $\sqrt{a_2} = 1$ | $\lvert a_2\rvert = 1$，不滿足 $< 1$ | 臨界（不衰減的振盪器） |
| (c) $-2.1, 1.1025$ | $4.41 - 4.41 = 0$ | 1.05（重根） | 1.05 | $\lvert a_2\rvert > 1$ ✗ | 不穩定 |
| (d) $0.5, -0.6$ | $0.25 + 2.4 = 2.65$ | $0.5639$、$-1.0639$ | 1.0639 | $0.5 < 0.4$？✗ | 不穩定 |

(b) 很有趣：pole 剛好在單位圓上，impulse response 是一個永不衰減的正弦，頻率 $\theta f_s/2\pi = \arccos(0.95)\cdot 48000/2\pi \approx 2.43$ kHz——這就是「數位正弦振盪器」的原理。(d) 是負實數 pole 跑到圓外：每個 sample 正負號翻轉、振幅乘 1.064，在 Nyquist 附近爆掉。

**實作：** [`ex5_pole_stability.cpp`](ex5_pole_stability.cpp)（target `l02_ex5_solution`）

```cpp
const std::complex<double> disc = std::sqrt(std::complex<double>(a1 * a1 - 4.0 * a2, 0.0));
return {(-a1 + disc) * 0.5, (-a1 - disc) * 0.5};
```

用 `std::complex` 的 `sqrt`，判別式為負時自動得到虛數，不用分情況。程式輸出：

```
case                                       pole 1                 pole 2   |p|max  stable?  tail/head
LPF 1 kHz Q=0.707                     0.9077   +0.0845j     0.9077   -0.0845j   0.9116      yes          0
LPF 1 kHz Q=8                         0.9834   +0.1292j     0.9834   -0.1292j   0.9919      yes          0
a1=-1.8  a2=0.81 (double real pole)   0.9000   +0.0000j     0.9000   +0.0000j   0.9000      yes   2.05e-30
a1=-1.9  a2=1.0  (|p| = 1, marginal)  0.9500   +0.3122j     0.9500   -0.3122j   1.0000       NO          1
a1=-2.1  a2=1.1025 (p = 1.05)         1.0500   +0.0000j     1.0500   +0.0000j   1.0500       NO        inf
a1=0.5   a2=-0.6                      0.5639   +0.0000j    -1.0639   +0.0000j   1.0639       NO        inf
```

（為了排版，case 名稱略有縮短。）tail/head 欄完全符合預期：穩定的衰減到 0，臨界的維持 1，不穩定的變成 inf。程式還在 $(a_1, a_2)$ 平面上掃了 5861 個點，三角形條件與「直接算 pole」的結論全部一致。

**延伸思考：** 共軛 pole $re^{\pm j\theta}$ 中，**角度 $\theta$ 決定共振頻率**（$f \approx \theta f_s / 2\pi$），**半徑 $r$ 決定衰減速度**（包絡 $r^n$，$\tau = -1/\ln r$ samples）。1 kHz、Q = 8 的 LPF：$r = \sqrt{a_2} = 0.991875$、$\theta = 0.13065$ rad → 998.1 Hz、$\tau = 123$ samples（2.6 ms）。實測 1000 個 sample 後 impulse response 衰減 −70.33 dB，理論 $20\log_{10}r^{1000} = -70.86$ dB。高 Q 時有個好用的近似：$r \approx e^{-\omega_0/(2Q)}$（$0.1309/16 = 0.00818$，$e^{-0.00818} = 0.99185$）——Q 越高，pole 越貼近單位圓、ringing 越久。

---

## 練習 6：FIR moving average vs one-pole

解答：[`ex6_moving_average.cpp`](ex6_moving_average.cpp)（target `l02_ex6_solution`）

running sum 的核心：

```cpp
sum_[ch] += x - h[idx];   // 加新的、減掉 N 個 sample 前的
h[idx] = x;
idx = (idx + 1) % length_;
return static_cast<float>(sum_[ch] / length_);
```

（累加器用 double：float 的 running sum 長時間執行後會累積誤差，因為「加進去」與「減出來」的捨入不會完全抵銷。）

結果（$N = 8$，one-pole 的 cutoff 設在 moving average 的實際 −3 dB 點 2676 Hz）：

```
   f (Hz)     MA theory   MA measured      one-pole
     1000         -0.39         -0.39         -0.56
     2676         -3.01         -3.01         -2.97
     4000         -7.57         -7.57         -5.00
     6000       -100.00       -100.00         -7.58
     9000        -12.96        -12.96        -10.40
    12000       -100.00       -100.00        -12.34
    20000        -19.01        -19.01        -14.93
```

| 比較項目 | moving average（FIR） | one-pole（IIR） |
|---|---|---|
| −3 dB 點 | 2676 Hz（近似 $0.443 f_s/N$ = 2658） | 設成相同 |
| 通帶 | 較平（1 kHz −0.39 dB） | 較早開始下降（−0.56 dB） |
| 阻帶 | 在 $kf_s/N$ 有完全的 null（6、12 kHz），null 之間有旁瓣（峰值約 8.6 kHz、−12.8 dB；表中 9 kHz 處 −12.96 dB） | 單調下降，沒有 null 也沒有旁瓣 |
| step response | 剛好 $N = 8$ 個 sample 到位 | 只能「趨近」：到 99% 要 14 samples（$\approx 4.6\tau$） |
| impulse response | 8 個 0.125，然後全是 0 | $0.296, 0.208, 0.147, \dots$ 永遠不為 0 |
| 相位 | linear phase：固定延遲 $(N-1)/2 = 3.5$ samples（實測 3.492） | 非線性相位 |
| 成本 | running sum O(1)，但要存 $N$ 個 sample | O(1)，只存 1 個狀態 |

**聽感：** 白雜訊通過兩者的 RMS 是 −25.79 dB 與 −24.40 dB。moving average 的頻譜有規律的「洞」，聽起來帶一點「梳狀」的空洞感；one-pole 是平順的變暗。moving average 的強項是「時域」：step response 在確定的時間內完成、沒有 overshoot，所以常用在 RMS 偵測視窗（第 04、11 章）而不是音色處理。

輸出 WAV：`l02_ex6_noise_moving_average.wav`、`l02_ex6_noise_one_pole.wav`。

---

## 練習 7：為什麼用 Transposed Direct Form II？

**1. 狀態數與意義。**
- DF-I：4 個（$x[n-1], x[n-2], y[n-1], y[n-2]$），就是真實的過去輸入與輸出。
- DF-II：2 個（$w[n-1], w[n-2]$），$w$ 是「只經過 pole（feedback）部分」的中間訊號。
- TDF-II：2 個（$s_1, s_2$），是「尚未加進輸出的部分和」（partial sums），量級和輸出相近。

**2. DF-II 的問題。** 低 cutoff 的 lowpass，pole 非常靠近 $z = 1$，pole 部分的 DC 增益 $1/(1 + a_1 + a_2)$ 很大（之後再由很小的 $b$ 係數縮回來）。`l02_direct_forms` 的 30 Hz LPF：$w[n]$ 峰值 18908，是輸出 0.289 的 65422 倍。在 **fixed-point** 下這直接溢位（數值被截斷或繞回，輸出全毀）；在 **floating-point** 下不會溢位，但大數加小數會吃掉輸入的低位元，而且換係數時會出問題（見 3）。

**3. 換係數時誰會 click。** DF-II：輸出 $y = b_0 w + b_1 w_1 + b_2 w_2$，$w$ 是放大了數萬倍的量，$b$ 一換，乘積就跳一大步——實測換係數瞬間的跳動是正常的 983 倍。DF-I 的狀態是真實的輸入輸出，TDF-II 的狀態和輸出同量級，換係數時都只有正常的 1.6 / 2.3 倍。

**4. 精度的關鍵是 double 係數 + double 狀態。** 實測 float 下三種結構的 round-off 都在 −70 ~ −74 dB（TDF-II 甚至稍差一點）。只做一半不夠：以 DF-I 為例，float 係數 + double 狀態是 −89.93 dB（係數 round 成 float 時 pole 就偏了，算的是另一個 filter），double 係數 + float 狀態是 −83.77 dB；係數與狀態都用 double，和 double TDF-II 參考（double 輸出）的差異降到 −242.69 dB——兩種 double 實作幾乎完全吻合，round-off 遠低於可聽範圍。所以 `dawlab::dsp::Biquad` 的係數與狀態都用 double，只有輸入輸出是 float；它實測的 −151.20 dB 誤差，正好等於「把 double 輸出 round 成 float」本身的量化底線。TDF-II 的貢獻是「2 個狀態 + 沒有內部放大 + 換係數平順」，double 的貢獻是「精度」，兩者合起來才是好的 biquad。

**5. 每個 sample 都調變 cutoff。** 選 TPT 的 state-variable filter（`StateVariableFilter.h`）：每個 sample 更新只要一次 `tan`（不需要 RBJ 的 `sin` / `cos` 與 normalize），狀態是積分器的物理量，調變時行為良好，而且同時給出 LP / BP / HP / notch。`l02_cutoff_modulation` 的 [B2] 顯示「每 block 更新一次」的 biquad 與理想平滑調變差了 −2.97 dB，per-sample SVF 只差 −25.69 dB；[C] 顯示兩者的靜態響應完全相同。
