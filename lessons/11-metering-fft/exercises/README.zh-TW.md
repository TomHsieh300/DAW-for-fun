# 第 11 章 練習題

[English](README.md) | **繁體中文**

難度：★☆☆ 基礎、★★☆ 中等、★★★ 進階。實作題的起始碼在 `exercises/starter/`，可以直接編譯；
還沒完成 `// TODO` 之前執行會印出 `[FAIL]`，這是預期的。完成後 self-check 全部通過就會印出 `[PASS]`。

```powershell
.\scripts\build.ps1 -Only 11-metering-fft -NoTest
cd build
.\bin\Debug\l11_ex3_starter.exe          # 你的版本
.\bin\Debug\l11_ex3_solution.exe         # 參考解答
```

解答與解說：[`../solutions/README.zh-TW.md`](../solutions/README.zh-TW.md)。

---

## 練習 1（計算）★☆☆　bin 解析度與需要的 N

1. fs = 48 kHz、N = 2048。bin 寬是多少 Hz？一個 frame 涵蓋多少 ms？bin 100 對應幾 Hz？
2. 1 kHz 的正弦落在第幾個 bin（小數）？要讓 1 kHz 正好落在某個 bin 的中心，N 最小可以是多少（不限 2 的冪次）？
   如果限定 2 的冪次呢？
3. 你想在頻譜上把低音 E1（41.20 Hz）和 F1（43.65 Hz）分成兩個峰。用 Hann window 時，兩個正弦至少要相隔
   約 2 個 bin 才看得出兩個峰。fs = 48 kHz 時，N 至少要多大（取 2 的冪次）？這個 frame 有多長？
   用它來看鼓組的 transient 會有什麼問題？
4. 44.1 kHz、N = 4096：bin 寬與 frame 長度？
5. spectrum analyzer 想要每秒更新 30 次（30 fps），fs = 48 kHz、N = 4096。hop 是多少 sample？overlap 是多少 %？

提示：bin 寬 $\Delta f = f_s / N$；frame 長度 $N / f_s$；overlap $= 1 - \text{hop}/N$。

## 練習 2（觀念）★☆☆　為什麼要 window？leakage 是怎麼來的？

1. 用你自己的話解釋 spectral leakage：一個「純」的 1 kHz 正弦，為什麼 FFT 之後會在 1 kHz 以外的 bin 也有能量？
   提示：DFT 隱含地假設 frame 會「週期性重複」，想像把 frame 首尾接起來。
2. 為什麼「整數個週期剛好填滿 frame」時看不到 leakage？（`l11_window_leakage` 第二張表）
3. 加 window 之後，峰值的讀數為什麼變小了？要怎麼補償？（coherent gain）
4. 你要在 0 dBFS 的 1 kHz 旁邊找一個 -90 dB 的 1.2 kHz 雜訊（例如 hum 的諧波），該用哪個 window？
   如果你要分辨 1000 Hz 和 1010 Hz 兩個一樣大的正弦呢？
5. zero-padding（frame 後面補 0 再做更大的 FFT）能不能提高「頻率解析度」？它到底做了什麼？

## 練習 3（實作）★★☆　重用 Oversampler 做 true-peak meter，並與第 05 章的 windowed-sinc 版比較

起始碼：`exercises/starter/ex3_true_peak.cpp`（target `l11_ex3_starter`）

第 05 章練習 4 已經從零做過一個 4x true-peak meter（64 tap Hann windowed-sinc、4 個 phase）。這一題不再重做一次，
而是換兩個角度：

1. **重用**：第 06 章的 `dsp::Oversampler`（兩級 2x halfband）已經是現成、測過的 4x 升頻器。
   完成 `TruePeakMeter::process()`（TODO 1）：`upsample()` 每個 block，在 4 倍長的 view 上找 |x| 最大值。
   只量測、不輸出，所以不需要 `downsample()`。
2. **比較**：起始碼已經附上第 05 章練習 4 的 windowed-sinc 版（`WindowedSincTruePeak`，已完成）與 FFT「理想內插」，
   會把同一段訊號同時餵給兩種 meter。你要完成 `multipliesPerInputSample()`（TODO 2）：從 Oversampler 各級的
   `getNumOddTaps()` 算出每個輸入 sample 的乘法數。然後讀印出來的表格，回答下面的問題。

self-check 會驗證：
- 1 kHz 0 dBFS → 0 dBTP；fs/4、相位 45° 的正弦：sample peak = -3.01 dBFS，Oversampler 讀到 0 dBTP
- 相位掃描（fs/4 正弦、相位 0°～90°）：兩種 meter 最差都低估約 0.17 dB，但最差的相位不同
- 接近 Nyquist 的頻率響應：Oversampler 到 22 kHz 都在 0.02 dB 內，windowed-sinc 在 22 kHz 明顯低估
- hard clip 過的鼓組、撥弦素材：兩種 meter 與 FFT 理想內插的差距；normalize 到 -1 dBTP 後讀數正確
- 乘法數：Oversampler 56、windowed-sinc 64；另外印出實際計時（只印不檢查，數字依機器與建置設定而異）

問題：
1. 相位掃描的表格裡，Oversampler 在哪些「峰值位置」量得準、windowed-sinc 在哪些位置量得準？用兩者的插值點
   落在原始 sample 之間的哪裡（README 2.5 節）解釋。為什麼兩者的最差誤差一樣大？
2. 22 kHz 時第 06 章說第 1 級 halfband 單趟衰減 0.65 dB，為什麼 Oversampler 的讀數幾乎沒有誤差？
   （提示：第 06 章 2.6 節「Passband 邊緣」那一段的 $|H(f)| + |H(f_s - f)| = 1$。）windowed-sinc 為什麼沒有這個性質？
3. 乘法數只差 56 對 64（約 14%），實際計時卻差好幾倍。比較兩者的內迴圈：累加用 float 還是 double？
   歷史緩衝怎麼取值（鏡像 ring buffer 的連續記憶體 vs 每個 tap 都要判斷繞回）？哪一種寫法編譯器比較容易向量化？
4. 如果你要在 DAW 的 master bus 上即時顯示 true peak，會選哪一個？如果要做第 05 章 2.8 節說的 true-peak limiter
   （sidechain 裡升頻），latency 又會是哪一個比較小？（提示：halfband 的 latency 在第 06 章 2.6 節；
   64 tap FIR 的群延遲是 31.5 個 4x sample。）

## 練習 4（實作）★★☆　VU meter 的 ballistics

起始碼：`exercises/starter/ex4_vu_meter.cpp`（target `l11_ex4_starter`）

VU meter 是一支有質量、有阻尼的指針：突然出現的 0 VU 正弦，指針要在 300 ms 到達最終讀數的 99%，
過衝 1~1.5%。用二階系統模擬：

$$\ddot p + 2\zeta\omega_n \dot p + \omega_n^2 p = \omega_n^2\, u(t),\qquad u = |x| \cdot \text{inputScale}$$

完成兩個 TODO：每個 sample 的 semi-implicit Euler 積分，以及「-18 dBFS 正弦 = 0 VU」的校準倍率。

self-check 會驗證：300 ms 時 = 最終讀數的 99% ± 0.5%、過衝 1~1.5%、-24 dBFS → -6 VU、
10 ms 的短促 burst 推不動指針（< -12 VU）。

## 練習 5（實作）★★★　Loudness Range（LRA，EBU Tech 3342）

起始碼：`exercises/starter/ex5_loudness_range.cpp`（target `l11_ex5_starter`）

LRA = short-term loudness 分布的第 95 百分位數 − 第 10 百分位數（先過 -70 LUFS absolute gate 與
-20 LU relative gate）。起始碼已經用本章的 `LoudnessMeter` 每 100 ms 取一個 short-term 值放進直方圖；
你要完成 `computeLra()`。

self-check 會驗證 EBU Tech 3342 風格的測試訊號：前 20 s / 後 20 s 兩種位準的正弦，
-20/-30 dBFS → 10 LU、-20/-15 → 5 LU、-40/-20 → 20 LU（各 ±1 LU）；固定位準 → 0 LU。

## 練習 6（實作）★★★　用 FFT 做 overlap-add 快速卷積

起始碼：`exercises/starter/ex6_fft_convolution.cpp`（target `l11_ex6_starter`）

卷積定理：時域卷積 = 頻域相乘。把每個輸入 block 補 0 到 $N \ge B + L - 1$，做 FFT、乘上 IR 的頻譜、IFFT，
再把超出 block 的尾巴疊加到下一個 block（overlap-add）。完成 `OverlapAddConvolver::process()`。

self-check 會驗證：與直接卷積的誤差 < 1e-5（block 長度不固定也要對）、impulse 的輸出 = IR 本身、
大 block 時比直接卷積快。這是第 12 章 convolution reverb 的基礎。

## 練習 7（實作）★★☆　stereo correlation meter

起始碼：`exercises/starter/ex7_correlation_meter.cpp`（target `l11_ex7_starter`）

$$r = \frac{E[L R]}{\sqrt{E[L^2]\,E[R^2]}}$$

用 exponential moving average（時間常數 300 ms）估計三個期望值。完成 `process()`。

self-check 會驗證：L = R → +1；L = −R → −1；sin / cos → 0；左右獨立的雜訊 → 約 0；
共同成分 : 獨立成分 = 1 : 1 → 0.5；從 mono 突然變反相時，越過 0 的時間 = τ ln 2 ≈ 208 ms。
