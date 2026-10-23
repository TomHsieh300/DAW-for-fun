# 第 12 章 練習題：Reverb

[English](README.md) | **繁體中文**

共 7 題：觀念題 2 題、計算題 1 題（第 7 題也含計算）、實作題 4 題。實作題的起始碼在 `exercises/starter/`，可以直接編譯；執行時 self-check 會印 `[FAIL]`（這是預期的），完成 `// TODO` 之後應該全部 `[PASS]`。完整解答在 [`../solutions/`](../solutions/README.zh-TW.md)。

建置與執行（PowerShell，在專案根目錄）：

```powershell
.\scripts\build.ps1 -Only 12-reverb -NoTest
cd build
.\bin\Debug\l12_ex3_starter.exe        # 你的版本
.\bin\Debug\l12_ex3_solution.exe       # 參考解答
```

| 題號 | 類型 | 難度 | 主題 | 檔案 / target |
|---|---|---|---|---|
| 1 | 計算 | ★☆☆ | comb 的 feedback 與 RT60 | （紙筆） |
| 2 | 觀念 | ★☆☆ | delay 長度為什麼要互質 | （紙筆） |
| 3 | 實作 | ★★☆ | early reflections：image-source + tapped delay | `starter/ex3_early_reflections.cpp` → `l12_ex3_starter` |
| 4 | 實作 | ★★★ | 為 FDN 加 modulation，量化 metallic ringing | `starter/ex4_fdn_modulation.cpp` → `l12_ex4_starter` |
| 5 | 實作 | ★☆☆ | gated reverb（第 08 章 Gate） | `starter/ex5_gated_reverb.cpp` → `l12_ex5_starter` |
| 6 | 實作 | ★★☆ | Schroeder backward integration 估 RT60 | `starter/ex6_rt60_estimator.cpp` → `l12_ex6_starter` |
| 7 | 觀念 + 計算 | ★★☆ | partitioned convolution 的 latency、CPU 與記憶體 | （紙筆） |

---

## 練習 1（計算 ★☆☆）：comb 的 feedback 與 RT60

fs = 48 kHz。

1. 一個 feedback comb 的 D = 1557 samples，要讓 RT60 = 2.0 s，g 是多少？如果 RT60 改成 0.5 s 呢？
2. Freeverb 在 44.1 kHz 時，最短的 comb 是 1116 samples、最長的是 1617 samples。roomsize 設定讓 g = 0.84 時，這兩個 comb 的 RT60 各是多少？這說明了 Freeverb 的 roomsize 和 RT60 的關係有什麼問題？
3. 範例 1 的 damping 實驗中，d = 0.5、g = 0.894、D = 1557。手算 1 kHz 與 8 kHz 的 RT60（用 README 2.3 節的公式），和範例輸出比較。

提示：$`g = 10^{-3D/(\mathrm{RT60}\,f_s)}`$、$`\mathrm{RT60} = -3D / (f_s\log_{10} g)`$、$`|\mathrm{LP}(e^{j\omega})| = (1-d)/\sqrt{1 - 2d\cos\omega + d^2}`$。

---

## 練習 2（觀念 ★☆☆）：為什麼 delay 長度要互質

1. 兩個並聯的 comb，長度 1500 與 2000 samples（fs = 48 kHz）。它們各自的 mode 落在哪些頻率？哪些頻率兩個 comb 同時共振？時域上它們的 echo 每隔多久會同時抵達一次？
2. 換成 1427 與 1783（都是質數）呢？
3. 為什麼「同時共振的 mode」在聽感上是個問題？除了選互質的長度，還有什麼方法可以減輕 metallic ringing？
4. `Reverb.h` 用 `nextPrime()` 讓每條線都是質數。「全部都是質數」是「兩兩互質」的充分條件還是必要條件？舉一組不是質數但兩兩互質的長度。

---

## 練習 3（實作 ★★☆）：early reflections

檔案：`exercises/starter/ex3_early_reflections.cpp`（target `l12_ex3_starter`）。

房間 6 × 4 × 3 m（牆面在 0 與 Lx/Ly/Lz）、牆面吸音係數 α = 0.3、聲源在 (2.0, 1.5, 1.6)、聽者在 (4.3, 2.6, 1.2)、c = 343 m/s、fs = 48 kHz。

1. 完成 `firstOrderReflections()`：對 6 面牆各算一個 image source（例如牆 x = 0 → (−sx, sy, sz)、牆 x = Lx → (2Lx − sx, sy, sz)），轉成 `TappedDelay::Tap`：
   - delay（samples）= round((|image − listener| − |source − listener|) / c · fs)
   - gain = √(1 − α) · |source − listener| / |image − listener|
   - 依 delay 由小到大排序。
2. 完成 `EarlyReflections::process()`：每個 channel 逐 sample 呼叫 `TappedDelay::processSample()`（只輸出反射，不含直達音）。左右耳各自一組 tap（耳朵在聽者位置 y ± 0.08 m）。
3. 把 ER 與 `Reverb`（late reverb，pre-delay = 最晚的一個 tap 的時間）串起來處理 `makePluckSequence()`，輸出 `dawlab_output/l12_ex3_er_only.wav`（dry + ER）與 `l12_ex3_er_late.wav`（dry + ER + late）。

Self-check：最早的反射（地板）在 169 samples、gain 0.570；最晚的（x = 0 那面牆）在 536 samples、gain 0.337；TappedDelay 的 impulse response 恰好有 6 個非零 sample。先手算地板反射驗證你的公式。

聽聽看：只加 ER 時，聲音變「近」而且有房間感，但沒有尾巴；加上 late 之後才像完整的 reverb。

---

## 練習 4（實作 ★★★）：為 FDN 加 modulation，比較 metallic ringing

檔案：`exercises/starter/ex4_fdn_modulation.cpp`（target `l12_ex4_starter`）。

起始碼已經寫好一個 4 線 FDN 的骨架 `Fdn4`（delay 約 23–47 ms，取質數；Householder 矩陣；one-pole damping；$`g_i = 10^{-3L_i/(\mathrm{RT60}f_s)}`$）。

1. 完成 `Fdn4::processSample()`：
   - LFO 相位前進；讀 4 條線 $`v_i`$ = `lines_.readCubic(i, L_i − 1 + depth·sin(φ + i·π/2))`（先讀後寫，所以 −1）；
   - Householder：$`u_i = v_i - \frac{2}{N}\sum_j v_j`$；需要時過 damping；
   - 寫回 $`g_i u_i + x`$；輸出 $`v_0 - v_1 + v_2 - v_3`$。
2. 完成 `spectralRoughnessDb()`：對尾巴（0.4 s 起）取 16384 點、Hann window 的 FFT，算 100 Hz – 6 kHz 各 bin 的 magnitude（dB）的**標準差**。mode 越分明（metallic），頻譜越「尖」，標準差越大。
3. 比較 modulation 關閉 vs depth 1 ms / 1.3 Hz，以及 depth 4 ms。

Self-check：lossless 模式（g = 1、無 damping）下 1.0 s 與 2.5 s 的 RMS 差 < 1 dB（驗證 Householder 無損）；RT60 量測 ≈ 2 s；modulation 開啟後標準差至少降低 1 dB，RT60 不明顯改變。

思考：4 線 FDN 的 mode 密度是多少個 / Hz？為什麼它比 `Reverb.h` 的 8 線版本更容易聽到 ringing？depth 4 ms 有什麼副作用？

---

## 練習 5（實作 ★☆☆）：gated reverb

檔案：`exercises/starter/ex5_gated_reverb.cpp`（target `l12_ex5_starter`）。

1980 年代的經典鼓聲：把很長的 reverb 用 gate 硬生生砍斷，聽起來又大又緊。

```
 snare ──┬──────────────────────────────────────────► dry
         └─► Reverb（RT60 3 s, mix 1）──► Gate（sidechain = dry snare）──► wet
```

1. 設定第 08 章的 `Gate`：threshold −30 dB、hysteresis 6 dB、range −80 dB、attack 0.5 ms、hold 100 ms、release 30 ms。
2. 逐 block 呼叫 `gate.process(main, sidechain)`：main = reverb 的 wet、sidechain = dry snare。
3. 思考：為什麼偵測訊號用 dry snare，而不是 reverb 自己的輸出？

Self-check：門開時（0.02–0.10 s）gated ≈ ungated（差 < 1 dB）；門關後（0.40–0.58 s）gated 比 ungated 低 40 dB 以上；下一擊（0.6 s）門要再打開。解答輸出 `l12_ex5_dry.wav`、`l12_ex5_reverb.wav`、`l12_ex5_gated.wav`；起始碼只輸出 `l12_ex5_gated.wav`（dry + gated wet）。

---

## 練習 6（實作 ★★☆）：IR 的 RT60 估計工具（Schroeder backward integration）

檔案：`exercises/starter/ex6_rt60_estimator.cpp`（target `l12_ex6_starter`）。

`measureRt60()` 用 windowed RMS 的 envelope 做回歸，但單一次 impulse response 的 envelope 起伏很大。Schroeder（1965）的方法是對 IR 的平方**從尾巴往回積分**：

$$
\mathrm{EDC}[n] = \sum_{k=n}^{N-1} h[k]^2,\qquad
\mathrm{EDC}_{\mathrm{dB}}[n] = 10\log_{10}\frac{\mathrm{EDC}[n]}{\mathrm{EDC}[0]}
$$

1. 完成 `schroederDecayDb()`：一次 O(N) 的迴圈（從尾巴往回累加）。
2. 完成 `estimateRt60()`：在 EDC_dB 介於 −5 與 −25 dB 的區段，對 (t, dB) 做最小平方法線性回歸，RT60 = −60 / slope（T20）。
3. 用三種 IR 驗證：理想指數衰減正弦（RT60 1 s）、`makeSyntheticImpulseResponse()`（0.5 s 與 1.5 s）、（解答另外加了）`Reverb` 的 IR。

Self-check：指數衰減正弦的 EDC 在 t = 0.5 s 時是 −30 dB（±0.5）；RT60 估計誤差 3% 以內；合成 IR 誤差 10% 以內。

思考：為什麼對指數衰減的訊號，EDC_dB 是一條斜率等於 −60/RT60 的直線？真實錄音的 IR 尾巴有背景雜訊時，EDC 會怎麼變形？

---

## 練習 7（觀念 + 計算 ★★☆）：partitioned convolution 的 latency 與成本

fs = 48 kHz，IR 長 3.0 s（mono），使用本章的 `ConvolutionReverb`（uniform partition，N = 2P，overlap-save）。

1. P = 512 時，partition 數 K 是多少？IR 頻譜佔多少記憶體（每個 bin 是 `complex<float>` = 8 bytes）？加上一個 channel 的 FDL 呢？
2. 每個輸出 sample 平均要多少次 complex 乘加？跟直接卷積比，大約省幾倍？
3. P 改成 64 呢？latency 與每個 sample 的 complex 乘加數各變成多少？
4. 「uniform partitioned convolution 的 latency = P」這個 latency 從哪裡來？如果 host 每次 callback 剛好給 512 個 sample 並且與 partition 對齊，能不能做到額外 latency = 0？本章的實作為什麼沒有這麼做？
5. 如果想要 0 latency、又不想付 P = 64 的 CPU 成本，可以怎麼做？
