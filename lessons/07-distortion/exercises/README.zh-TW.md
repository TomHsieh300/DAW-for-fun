# 第 07 章 練習題：Distortion

[English](README.md) | **繁體中文**

難度：★☆☆ 基礎 / ★★☆ 中等 / ★★★ 進階。
實作題（3、4、5）都有起始碼 `exercises/starter/exN_xxx.cpp`（可編譯、留 `// TODO`，執行會印出 FAIL——這是預期的）與完整解答 `solutions/exN_xxx.cpp`。
解答與解說（含觀念題、計算題、設計題）在 [`../solutions/README.md`](../solutions/README.zh-TW.md)，建議先自己做完再看。

建置與執行（Windows，在專案根目錄）：

```powershell
.\scripts\build.ps1 -Only 07-distortion -NoTest
cd build
.\bin\Debug\l07_ex3_starter.exe      # 你的版本（改 exercises/starter/ex3_wavefolder.cpp）
.\bin\Debug\l07_ex3_solution.exe     # 參考解答
```

改完 starter 之後回到專案根目錄重新執行 `build.ps1`（或在 build 目錄裡執行 `cmake --build . --config Debug --target l07_ex3_starter`）即可。
每支 starter 的 self-check 全部通過、印出 `[PASS]` 就算完成。輸出的 WAV 在你執行時所在目錄的 `dawlab_output/` 底下。

<div align="center">

| 題號 | 類型 | 難度 | 主題 | 檔案 |
|---|---|---|---|---|
| 1 | 計算 | ★☆☆ | N bits 的 quantization step 與 SNR | — |
| 2 | 觀念 | ★☆☆ | full-wave rectify 為什麼高八度 | — |
| 3 | 實作 | ★★☆ | wavefolder（三角 folder、sine folder、oversampling） | `starter/ex3_wavefolder.cpp` |
| 4 | 實作 | ★★☆ | 用 biquad 做簡易 cabinet simulator | `starter/ex4_cab_sim.cpp` |
| 5 | 實作 | ★★☆ | auto gain compensation（輸出 RMS = 輸入 RMS） | `starter/ex5_auto_gain.cpp` |
| 6 | 計算 | ★★☆ | hard clip 與 sample & hold 的 alias 落在哪裡 | — |
| 7 | 設計 | ★★★ | amp chain 各級的順序為什麼重要 | — |

</div>

---

## 練習 1 ★☆☆（計算）Quantization step 與 SNR

使用 README 2.6.1 節的 $`\Delta = 2/2^N`$ 與 $`\text{SNR} \approx 6.02N + 1.76 + 20\log_{10}A`$ dB。

1. 算出 $`N = 4, 8, 12, 16, 24`$ 時的 step $`\Delta`$ 與滿幅正弦（$`A = 1`$）的理論 SNR。
2. 8 bit 量化一個 −20 dBFS 的正弦，理論 SNR 是多少？跟範例 3 印出的數字比較。
3. 一段素材的 peak 大約在 −12 dBFS。要讓它的量化 SNR 至少 60 dB，最少要幾 bit？
4. 4 bit mid-tread 量化器的最大正輸出是多少？把 0 dBFS 的正弦送進去，正峰會發生什麼事？這對範例 3 中 4 bit 的「量測比理論低 2.6 dB」有什麼影響？
5. `Distortion::setBitDepth()` 接受小數。`bits = 5.5` 時 step 是多少？理論 SNR 是多少？這在聲音上代表什麼（為什麼 plugin 要允許小數 bit）？
6. 從 $`\sigma_e^2 = \Delta^2/12`$ 出發，自己推一次 6.02 與 1.76 這兩個數字是怎麼來的。

## 練習 2 ★☆☆（觀念）為什麼 full-wave rectify 會讓音高高八度？

1. 用「週期」的角度解釋：$`|\sin\omega t|`$ 的週期是多少？
2. 用 Fourier 級數的角度解釋：$`|\sin\omega t|`$ 有哪些頻率成分？基頻（$`\omega`$）的係數是多少？
3. half-wave rectify（$`\max(\sin\omega t, 0)`$）為什麼**不會**高八度？它比原本的正弦多了哪些成分？
4. 如果輸入在 rectify 之前帶有 DC offset（例如 $`0.3 + \sin\omega t`$），full-wave rectify 之後音高還是完全高八度嗎？為什麼？
5. 為什麼 octave fuzz 在單音 solo 很好聽、彈和弦卻常常很難聽？

## 練習 3 ★★☆（實作）Wavefolder

打開 `exercises/starter/ex3_wavefolder.cpp`，完成：

- (a) `float foldTriangle(float u, float threshold)`：超過 ±threshold 的部分折回來，可以折任意多次（週期 $`4T`$ 的三角波，$`|u| \le T`$ 時 $`y = u`$）。`threshold = 1` 時必須與 `Distortion::foldback()` 完全一致（self-check 會在 $`u \in [-10, 10]`$ 比對）。
- (b) `float foldSine(float u)`：$`y = \sin(\pi u / 2)`$。
- (c) `class Wavefolder`：標準介面 + `setDriveDb` + `setThreshold`，用第 06 章的 `Oversampler`（4x）在高取樣率做 `foldTriangle`。

self-check：(a)(b) 的數值點；drive 0 dB、振幅 0.25 時 THD < 0.1%（$`|u| < T`$ 完全透明）；drive 18 dB 時 THD > 20%、peak < 1.1、沒有 NaN；latency > 0。

提示：

- `std::fmod` 對負數回傳負的餘數，記得補回 $`[0, 4T)`$。
- 想一想 (b) 的 sine folder 在折點的**導數**是否連續，對諧波衰減速度有什麼影響（解答會印出兩者的諧波表）。

## 練習 4 ★★☆（實作）用 biquad 做簡易 cabinet simulator

打開 `exercises/starter/ex4_cab_sim.cpp`，完成 `CabSim`：

```math
H_\text{cab} = H_\text{HPF}(70\text{ Hz}, Q = \tfrac{1}{\sqrt 2}) \cdot H_\text{peak}(100\text{ Hz}, Q = 1.5, +4\text{ dB}) \cdot H_\text{LPF}(4.5\text{ kHz}, Q = \tfrac{1}{\sqrt 2})^2
```

四顆都是第 02 章的 `Biquad`（`BiquadCoefficients::makeHighpass` / `makePeak` / `makeLowpass`）。還要寫 `double magnitudeDb(double freq) const`，把各級的 `magnitudeDb()` 相加。

self-check：

- 實際處理（`measureGainDb`）與分析式（`magnitudeDb`）在 9 個頻率上差 < 0.2 dB。
- 4.5 kHz 處 −6.02 ± 0.3 dB（LR4 的特徵）。
- 9 kHz 處要符合 **bilinear transform 的 frequency warping** 修正後的值（不是類比 LR4 的 −24.6 dB）。
- 1 kHz 在 ±0.5 dB 內、100 Hz 在 +2.5~+4.5 dB、30 Hz < −12 dB、15 kHz < −40 dB。

延伸思考：為什麼 9 kHz 處數位 filter 比類比原型掉得多？（提示：$`\tan(\pi f / f_s)`$。）

## 練習 5 ★★☆（實作）Auto gain compensation

打開 `exercises/starter/ex5_auto_gain.cpp`，完成 `AutoGain::process(const AudioBlock& dry, AudioBlock wet)`：

```math
\overline{x^2}[n] = a\,\overline{x^2}[n-1] + (1-a)\,x^2[n], \qquad a = e^{-1/(\tau f_s)}
```

對 dry 與 wet 各算一個 mean square（$`\tau`$ = 100 ms），然後

```math
g[n] = \mathrm{clamp}\left(\sqrt{\frac{\overline{x_\text{dry}^2}[n]}{\max(\overline{x_\text{wet}^2}[n], 10^{-12})}},\; 0.01,\; 100\right), \qquad y[n] = g[n] \cdot x_\text{wet}[n]
```

self-check：

1. −20 dBFS 正弦、HardClip 30 dB：穩態後輸出 RMS 與輸入 RMS 差 < 0.2 dB，增益約 −22.4 ± 0.5 dB（請先自己估算這個數字：被 clip 的正弦，RMS 比純方波的 1 小一點）。
2. pluck sequence、Fuzz 30 dB：沒補償時 RMS 至少大 6 dB；有補償時與輸入差 < 1 dB。
3. 全靜音輸入：輸出必須全是 0、沒有 NaN。

延伸思考：時間常數 $`\tau`$ 太短（例如 1 ms）會發生什麼事？太長（例如 2 s）呢？

## 練習 6 ★★☆（計算）Alias 會落在哪裡？

取樣率 48 kHz。

1. 一個 7 kHz 的正弦經過 hard clip（只有奇次諧波）。列出第 3、5、7、9、11、13、15 次諧波的頻率，以及它們在 48 kHz 下實際出現的頻率（$`f_\text{alias} = |f - k f_s|`$，$`k = \mathrm{round}(f/f_s)`$）。哪些是「真的諧波」、哪些是 alias？
2. 同一個訊號改在 4x oversampling（192 kHz）下做 hard clip。第 27 次諧波會折到哪裡？假設 hard clip 已經接近方波，它相對於基頻大約幾 dB？
3. 一個 3 kHz 正弦經過 bitcrusher 的 sample & hold，downsample factor = 4。等效取樣率 $`f_s'`$ 是多少？3 kHz 本身會 alias 嗎？輸出在 3 kHz 與 9 kHz 的成分相對於原本的 3 kHz 各是幾 dB？（用 ZOH 的 sinc：$`|\sin(\pi f/f_s') / (\pi f/f_s')|`$。）
4. 為什麼第 3 小題的情況「不需要也不應該」用 oversampling 來處理？

## 練習 7 ★★★（設計）Amp chain 裡各級的順序為什麼重要？

`AmpChain` 的順序是：input HPF → pre-EQ → preamp（tanh）→ tone stack → power amp（hard clip）→ cabinet → output gain。

1. 用一句數學說明：為什麼「兩個線性 filter」可以交換順序，但「filter 與 waveshaper」不行？
2. 如果把 input HPF 移到 preamp **之後**，彈低音弦的和弦會聽到什麼差別？為什麼？（關鍵字：intermodulation。）
3. tone stack 放在 preamp 與 power amp **之間**，跟放在整條鏈的**最後面**，bass 旋鈕的作用有什麼不同？
4. 如果把 cabinet 移到 power amp **前面**，會發生什麼事？
5. noise gate（第 08 章）應該放在哪裡？為什麼不放在 cabinet 後面？
6. 設計題：你要做一個「bass guitar 用」的 distortion，希望低頻保持乾淨飽滿、只讓中高頻失真。請畫出訊號流程（可以用平行路徑），並說明每一級的參數大概怎麼設。
