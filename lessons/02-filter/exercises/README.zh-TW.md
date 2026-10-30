# 第 02 章練習題：Filter
[English](README.md) | **繁體中文**

共 7 題：計算 2 題（第 1、5 題，第 5 題同時是實作）、實作 5 題（第 2–6 題）、觀念 1 題（第 7 題）。
實作題的起始碼在 `starter/`，可以直接編譯；還沒完成 TODO 時執行會印出 `[FAIL]`，這是預期的。完成後 self-check 應該全部 `[PASS]`。

```powershell
.\scripts\build.ps1 -Only 02-filter -NoTest      # 建置（含 starter）
cd build
.\bin\Debug\l02_ex2_starter.exe                  # 執行你的版本
.\bin\Debug\l02_ex2_solution.exe                 # 對照解答
```

解答與解說：[`../solutions/README.zh-TW.md`](../solutions/README.zh-TW.md)。

---

## 練習 1 ★☆☆（計算）：手算 one-pole 係數

$`f_s = 48000`$ Hz。用計算機（或 Python）回答：

1. exp 版 one-pole lowpass（$`a = e^{-2\pi f_c/f_s}`$），$`f_c = 1`$ kHz 時的 $`a`$ 與 $`1 - a`$ 是多少？$`f_c = 100`$ Hz 呢？
2. 承上，$`f_c = 1`$ kHz 時這個 filter 的時間常數 $`\tau`$ 是幾個 sample？幾 ms？（提示：$`a = e^{-1/\tau}`$，$`\tau`$ 以 sample 計。）
3. exp 版在 Nyquist（$`z = -1`$）的增益是多少 dB（$`f_c = 1`$ kHz）？為什麼它不是 $`-\infty`$？
4. 同樣 $`f_c = 1`$ kHz 的 bilinear 版：$`g = \tan(\pi f_c/f_s)`$、pole $`(1-g)/(1+g)`$、$`b_0 = b_1 = g/(1+g)`$ 各是多少？和 exp 版的 $`a`$ 差多少？
5. $`f_c = 10`$ kHz 的 exp 版，實際 −3 dB 點在哪裡？（提示：2.8 節的 $`\cos\omega_{3\text{dB}} = 1 - (1-a)^2/(2a)`$。）

**對答案：** 執行 `l02_filter_response`，看 [1] 的表格與「手算對照」那兩行。

---

## 練習 2 ★★☆（實作）：DC blocker

檔案：`starter/ex2_dc_blocker.cpp` → target `l02_ex2_starter`

實作一階 DC blocker：

```math
y[n] = x[n] - x[n-1] + R\,y[n-1],\qquad H(z) = \frac{1 - z^{-1}}{1 - R z^{-1}}
```

1. 完成 `processSample()`（更新 `x1`、`y1`）。
2. 完成 `magnitudeDb()`：把 $`z = e^{j\omega}`$ 代入 $`H(z)`$。
3. 驗證（starter 裡已寫好 self-check）：輸入「0.5 的 DC + 0.25 的 1 kHz 正弦」，輸出平均值 < 0.001、1 kHz 增益在 ±0.05 dB 內；$`R = 0.995`$ 時 −3 dB 點 ≈ $`f_s(1-R)/2\pi`$ ≈ 38.2 Hz，且實測（`measureGainDb`）也是 −3.01 dB。

**延伸思考：** $`R`$ 越接近 1 有什麼好處、什麼壞處？一個突然出現的 DC（step）要多久才會被「吃掉」？（解答檔會量測 step response。）

---

## 練習 3 ★★☆（實作）：兩級 biquad = 4 階 Butterworth lowpass

檔案：`starter/ex3_butterworth4.cpp` → target `l02_ex3_starter`

1. 算出 4 階 Butterworth 兩級的 Q（$`N`$ 為偶數時 $`Q_k = 1/(2\cos((2k+1)\pi/(2N)))`$，或直接用 `dawlab::dsp::butterworthQ(4, k)`）。
2. 在 `setCutoff()` 中用同一個 $`f_c`$、各自的 Q 設定兩級 `makeLowpass`。
3. `processSample()` 把兩級串起來；`magnitudeDb()` 把兩級的 dB 相加。
4. 驗證：cutoff 處 −3.01 dB、$`2f_c`$ 處約 −24.1 dB（容許 0.3 dB），實測與理論一致。

**延伸思考：** 如果兩級都用 Q = 0.707，cutoff 處會是幾 dB？這種 filter 叫什麼、用在哪裡？

---

## 練習 4 ★★☆（實作）：HPF + LPF 組成 bandpass

檔案：`starter/ex4_bandpass_cascade.cpp` → target `l02_ex4_starter`

用一個 Butterworth highpass（$`f_\text{low} = 500`$ Hz）與一個 Butterworth lowpass（$`f_\text{high} = 2`$ kHz）串接，做出「只留 500 Hz–2 kHz」的 bandpass。

1. 完成 `setBand()`、`processSample()`、`magnitudeDb()`。
2. 驗證：兩個邊界處約 −3 dB；通帶中央（1 kHz）約 −0.53 dB（為什麼不是 0 dB？）；邊界外 2 個 octave（125 Hz）約 −24 dB。
3. 和 RBJ 的二階 `makeBandpass`（中心 $`\sqrt{500\cdot2000} = 1`$ kHz、$`Q = f_0/\text{BW} = 1000/1500`$）比較：兩者的斜率差多少？各自的優缺點？

---

## 練習 5 ★★☆（計算 + 實作）：由係數算 pole，判斷穩定性

**手算部分：** 下面幾組分母係數（$`1 + a_1z^{-1} + a_2z^{-2}`$），算出 pole、$`|p|`$，判斷穩定與否；再用三角形條件 $`|a_2| < 1`$ 且 $`|a_1| < 1 + a_2`$ 檢查一次。

<div align="center">

| | $`a_1`$ | $`a_2`$ |
|---|---:|---:|
| (a) | −1.8 | 0.81 |
| (b) | −1.9 | 1.0 |
| (c) | −2.1 | 1.1025 |
| (d) | 0.5 | −0.6 |

</div>

**實作部分：** 檔案 `starter/ex5_pole_stability.cpp` → target `l02_ex5_starter`

1. `computePoles()`：用 `std::complex<double>` 解 $`z^2 + a_1 z + a_2 = 0`$。
2. `isStable()`：兩個 pole 都 $`|p| < 1`$。
3. `tailToHeadRatio()`：送 impulse 進 `Biquad`，回傳「最後 10% sample 的峰值 ÷ 最前 10% 的峰值」——穩定的應該趨近 0。
4. 驗證：與 `BiquadCoefficients::poles()` / `isStable()` 結果一致。

**延伸思考：** 一對共軛 pole 的半徑 $`r`$ 與角度 $`\theta`$ 分別決定了什麼？用 1 kHz、Q = 8 的 LPF 算算看共振頻率與衰減時間常數。

---

## 練習 6 ★★☆（實作）：FIR moving average vs one-pole

檔案：`starter/ex6_moving_average.cpp` → target `l02_ex6_starter`

1. 用 **running sum** 實作 $`N`$ 點 moving average：每個 sample「加上新的、減掉 $`N`$ 個 sample 前的」，成本與 $`N`$ 無關。
2. `magnitudeDb()`：$`|H| = \left|\dfrac{\sin(N\omega/2)}{N\sin(\omega/2)}\right|`$。
3. 驗證（$`N = 8`$）：−3 dB 點 ≈ $`0.443 f_s/N`$；$`f_s/N = 6`$ kHz 處是 null；$`1.5f_s/N`$（9 kHz）處約 −12.96 dB（接近第一旁瓣的峰值：峰值在約 8.6 kHz、−12.8 dB）；step response 剛好 $`N`$ 個 sample 到位。
4. 和同一個 −3 dB 點的 one-pole 比較：通帶、null、旁瓣、step response、相位各有什麼不同？聽感上呢？

---

## 練習 7 ★☆☆（觀念）：為什麼用 Transposed Direct Form II？

用自己的話回答（可以參考 `l02_direct_forms` 的輸出）：

1. DF-I、DF-II、TDF-II 各需要幾個狀態變數？它們的狀態「代表」什麼量？
2. 為什麼 DF-II 在低 cutoff 的 lowpass 會有問題？在 fixed-point 與 floating-point 下的後果分別是什麼？
3. 在 block 之間換係數時，哪一種結構會 click？為什麼？
4. `dawlab::dsp::Biquad` 除了選 TDF-II，還做了哪個決定讓 30 Hz lowpass 的誤差從約 −70 dB 降到 −130 dB 以下？只做一半（例如只把狀態換成 double）夠不夠？
5. 如果 cutoff 要每個 sample 都調變（例如合成器的 filter envelope），你會選什麼結構？為什麼？
