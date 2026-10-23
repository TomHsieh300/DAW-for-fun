# 第 03 章 練習題：Parametric EQ
[English](README.md) | **繁體中文**

難度：★☆☆ 基礎 ／ ★★☆ 中等 ／ ★★★ 進階。
實作題的起始碼在 [`starter/`](starter/)，可以直接編譯；執行時 self-check 會印出 `[FAIL]`，這是預期的——把 `// TODO` 補完，直到印出 `[PASS]`。完整解答在 [`../solutions/`](../solutions/)，解說在 [`../solutions/README.zh-TW.md`](../solutions/README.zh-TW.md)。

建置與執行（以第 03 章為例）：

```powershell
.\scripts\build.ps1 -Only 03-eq                      # 建置 + 跑範例與解答的測試
cd build
.\bin\Debug\l03_ex2_starter.exe                      # 跑你自己的起始碼
```

| 題號 | 類型 | 難度 | 起始碼 | starter target | 解答 target |
|---|---|---|---|---|---|
| 1 | 計算 | ★☆☆ | — | — | （數字在 `l03_ex2_solution` 裡驗證） |
| 2 | 實作 | ★☆☆ | `starter/ex2_q_bandwidth.cpp` | `l03_ex2_starter` | `l03_ex2_solution` |
| 3 | 實作 | ★☆☆ | `starter/ex3_cascade_sum.cpp` | `l03_ex3_starter` | `l03_ex3_solution` |
| 4 | 實作 | ★★☆ | `starter/ex4_graphic_eq.cpp` | `l03_ex4_starter` | `l03_ex4_solution` |
| 5 | 實作 | ★★☆ | `starter/ex5_tilt_eq.cpp` | `l03_ex5_starter` | `l03_ex5_solution` |
| 6 | 實作 | ★★★ | `starter/ex6_switchable_highcut.cpp` | `l03_ex6_starter` | `l03_ex6_solution` |
| 7 | 觀念 | ★★☆ | — | — | — |

---

## 練習 1：手算 A、Q、bandwidth 與 RBJ 係數（計算，★☆☆）

不要用電腦（計算機可以），寫出算式：

1. RBJ peak / shelf 的 $A$：gain = +6 dB 與 −12 dB 時 $A$ 各是多少？+6 dB 的 peak 中心的**線性**增益是多少？
2. 頻寬 2/3 octave 對應的 Q？Q = 2 對應幾個 octave？
3. $f_0 = 1$ kHz、Q = √2 的 peak，「中點增益」的兩個頻寬邊緣在哪裡？+12 dB 時邊緣的增益是多少 dB？
4. $f_s = 48$ kHz、$f_0 = 12$ kHz、Q = 1、gain = +6 dB 的 peak：算出正規化後的 $b_0, b_1, b_2, a_1, a_2$，並驗證 DC 增益是 0 dB、$f_0$ 處是 +6 dB。
5. 一顆 +6 dB 和一顆 −6 dB 的 peak（同為 1 kHz、Q = 1）串接，整條曲線是什麼？為什麼？

> 提示：README 2.4、2.5 節。第 4 小題 $\omega_0 = \pi/2$ 時 $\cos\omega_0 = 0$，計算會簡單很多；$f_0$ 處 $z = e^{j\pi/2} = j$，$z^{-1} = -j$、$z^{-2} = -1$。

---

## 練習 2：Q ↔ bandwidth 換算與驗證（實作，★☆☆）

起始碼：[`starter/ex2_q_bandwidth.cpp`](starter/ex2_q_bandwidth.cpp)

1. 實作三個函式：`qToBandwidthOctaves(q)`、`bandwidthOctavesToQ(bw)`、`bandEdges(f0, q, fLow, fHigh)`（公式見 README 2.4 節）。
2. self-check 會驗證：1 octave ↔ Q = √2、1/3 octave ↔ Q ≈ 4.3185、來回換算回到原點、$f_1 f_2 = f_0^2$、$f_2 - f_1 = f_0/Q$。
3. 完成 `main()` 裡的 TODO：用 `BiquadCoefficients::makePeak(48000, 1000, √2, 12)` 驗證邊緣增益是 +6 dB（中點增益），用 `makeBandpass` 驗證同樣的邊緣是 −3 dB。再用 `measureGainDb` 實際送正弦波量一次。

> 提示：`std::asinh`、`std::sinh` 在 `<cmath>` 裡。

---

## 練習 3：驗證兩個 peak 串接 dB 相加（實作，★☆☆）

起始碼：[`starter/ex3_cascade_sum.cpp`](starter/ex3_cascade_sum.cpp)

1. 完成 `Cascade2::processSample`：`y = H2(H1(x))`。
2. 兩顆 peak：+6 dB @ 500 Hz（Q 1）與 −4 dB @ 2 kHz（Q 2）。在 8 個頻率比較：各自的 dB、dB 之和、$|H_1 H_2|$ 的 dB（用 `response()` 複數相乘）、實測（`measureGainDb`）。
3. 把串接順序顛倒，確認實測結果不變。
4. 加分：兩顆 +6 dB、Q = √2 @ 1 kHz 串接，中心應為 +12 dB、1414 Hz 應為 +6 dB。

> 提示：$\log(ab) = \log a + \log b$。

---

## 練習 4：10-band graphic EQ（實作，★★☆）

起始碼：[`starter/ex4_graphic_eq.cpp`](starter/ex4_graphic_eq.cpp)

做一個 10-band graphic EQ：中心頻率 $31.25 \times 2^k$ Hz（$k = 0..9$，即 31.25、62.5、125 … 16000 Hz），每個 band 頻寬 1 octave，使用者只能推 gain（±12 dB）。

1. 用練習 2 的公式把「1 octave」換成 `kQ`。
2. 完成 `setGainDb`（重算該 band 的係數）、`getMagnitudeDb`（各 band dB 相加）、`processSample`、`process`。
3. 觀察 self-check 印出的數字：只推 1 kHz +6 dB 時，500 Hz 和 2 kHz 各被帶起多少？**全部** band 推 +6 dB 時，1 kHz 處是多少？為什麼不是 +6？
4. 把「笑臉曲線」套在 drum loop 上，輸出 `l03_ex4_graphic_smile.wav`。

> 提示：相鄰 band 的「尾巴」會疊加（README 2.11 節）。

---

## 練習 5：Tilt EQ（實作，★★☆）

起始碼：[`starter/ex5_tilt_eq.cpp`](starter/ex5_tilt_eq.cpp)

Tilt EQ 只有兩個參數：pivot 頻率與 tilt（dB）。曲線像翹翹板：pivot 處 0 dB，低頻端 → $-t/2$，高頻端 → $+t/2$。

1. 用一顆 low shelf（gain $-t/2$）加一顆 high shelf（gain $+t/2$），兩顆都放在 pivot、Q = 0.707。
2. 完成 `getMagnitudeDb` 與 `updateCoefficients`。
3. self-check 會驗證：pivot 處 0 dB、兩端 ∓3 dB（tilt = +6）、以 pivot 為中心反對稱、整條曲線單調、$-t$ 是 $+t$ 的鏡像。
4. 加分：`setTiltDb` 用 `SmoothedValue` 平滑，`process()` 每 32 個 sample 重算一次係數；pluck sequence 在 2 秒處從 +6 切到 −6 dB，不可以 click。輸出 `l03_ex5_tilt.wav`。

> 提示：為什麼 pivot 剛好是 0 dB？README 2.6 節「shelf 的 $f_0$ 是中點增益」。

---

## 練習 6：可切換 12 / 24 dB/oct 的 high cut（實作，★★★）

起始碼：[`starter/ex6_switchable_highcut.cpp`](starter/ex6_switchable_highcut.cpp)

1. 12 dB/oct = 一顆 Butterworth lowpass（Q = 1/√2）；24 dB/oct = 兩顆 lowpass 串接，Q 用 `butterworthQ(4, 0)` 與 `butterworthQ(4, 1)`（第 02 章 `Biquad.h`）。
2. 實作數位 Butterworth 的理論值 $|H|^2 = 1/\left(1 + \left(\frac{\tan(\pi f/f_s)}{\tan(\pi f_c/f_s)}\right)^{2N}\right)$，並驗證兩種 slope 的實測響應都符合，且在 $f_c$ 都是 −3.01 dB。
3. 切換 slope 時不可以 click：兩條鏈同時跑，用 `SmoothedValue` 做 20 ms crossfade。self-check 會比較「直接切換」與「crossfade」在 1 kHz（= $f_c$）正弦波上的最大 sample 跳變。
4. 試聽：drum loop、high cut 2 kHz，前 2 秒 12 dB/oct、後 2 秒 24 dB/oct，輸出 `l03_ex6_highcut_switch.wav`。

> 提示：為什麼直接切換會 click？想想 $f_c$ 處 2 階與 4 階 Butterworth 的相位各是多少。

---

## 練習 7：EQ 與相位（觀念，★★☆）

用自己的話回答（每題幾句話即可）：

1. 為什麼一個改變 magnitude 的 IIR EQ 一定會改變相位？什麼是 minimum phase？
2. +6 dB @ 1 kHz、Q = 1 的 peak，在 500 Hz 和 2 kHz 的相位大約是多少？方向（領先 / 落後）？如果改成 −6 dB 呢？（可以用範例 `l03_eq_phase_cramping` 的輸出佐證）
3. 單一軌上用 IIR EQ，相位偏移通常聽不出來；什麼情況下它會變成問題？
4. Linear-phase EQ 的代價有哪些？一個 4097-tap 的 linear-phase FIR 在 48 kHz 下 latency 多少 ms？
5. 什麼是 pre-ringing？為什麼它對 kick、snare 特別明顯？
6. 你在 mastering 時想在 40 Hz 做一個很窄的 cut，用 linear-phase 還是 minimum-phase？說說你的取捨。
