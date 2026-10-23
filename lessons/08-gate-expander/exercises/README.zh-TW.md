# 第 08 章 練習題

[English](README.md) | **繁體中文**

難度：★☆☆ 基本、★★☆ 需要思考、★★★ 挑戰。解答在 [`../solutions/README.md`](../solutions/README.zh-TW.md)。

實作題（練習 4–8）都有起始碼 `exercises/starter/exN_*.cpp`：可以直接編譯，裡面留了 `// TODO`，self-check 已經寫好，所以**還沒完成時執行會印出 FAIL（這是預期的）**；填完後應該印出 PASS。完整解答在 `solutions/exN_*.cpp`。

```powershell
.\scripts\build.ps1 -Only 08-gate-expander          # 建置 + 跑範例與解答的測試
cd build
.\bin\Debug\l08_ex4_starter.exe                       # 跑你的起始碼
```

| 題號 | 類型 | 起始碼 target | 解答 target |
|---|---|---|---|
| 1 | 計算 | — | — |
| 2 | 計算 | — | — |
| 3 | 觀念 | — | — |
| 4 | 實作 | `l08_ex4_starter`（`starter/ex4_sidechain_gate.cpp`） | `l08_ex4_solution`（`solutions/ex4_sidechain_gate.cpp`） |
| 5 | 實作 | `l08_ex5_starter`（`starter/ex5_upward_expander.cpp`） | `l08_ex5_solution`（`solutions/ex5_upward_expander.cpp`） |
| 6 | 實作 | `l08_ex6_starter`（`starter/ex6_lookahead_gate.cpp`） | `l08_ex6_solution`（`solutions/ex6_lookahead_gate.cpp`） |
| 7 | 實作 | `l08_ex7_starter`（`starter/ex7_splitband_deesser.cpp`） | `l08_ex7_solution`（`solutions/ex7_splitband_deesser.cpp`） |
| 8 | 實作 | `l08_ex8_starter`（`starter/ex8_transient_shaper.cpp`） | `l08_ex8_solution`（`solutions/ex8_transient_shaper.cpp`） |

---

## 練習 1（計算題，★☆☆）：Expander 的輸出 dB

一個 downward expander：threshold $T = -40$ dB、ratio 1:2（`setRatio(2)`）、range $-30$ dB。以下的「輸入」都是偵測器穩態的 level（dB）。

1. Hard knee：輸入 $-30$、$-45$、$-60$、$-80$ dB 時，輸出各是多少 dB？哪一個被 range 限制住了？
2. 改成 knee $W = 10$ dB：輸入 $-35$、$-40$、$-43$、$-45$ dB 時的輸出？（用 README 2.2 節的三段式，注意 knee 的範圍是 $[T - W/2,\ T + W/2]$。）
3. 混音師希望 $-50$ dB 的串音被壓到 $-65$ dB，threshold 不變、hard knee，ratio 要設多少？
4. Gate 的 range $-80$ dB、attack 2 ms、$f_s = 48$ kHz。本章 `Gate` 在 dB 域等速爬升，每個 sample 爬幾 dB？開門後 0.5 ms 時 gain 是多少 dB？

## 練習 2（計算題，★★☆）：Hysteresis 門檻怎麼設

你在 snare 麥克風上放一個 gate。量測得知：

- snare 打擊的 envelope peak 約 $-6$ dBFS，之後以約 60 dB/s 的速度衰減；
- hi-hat 串音的 envelope peak 約 $-30$ dBFS；
- 偵測器的 envelope 在尾音上有約 ±1.5 dB 的漣漪（峰對峰 3 dB），漣漪主要來自 snare 本體 185 Hz 的全波整流波峰（這個偵測器的 release 設得很短，約 3 ms；用 README 2.4.1 節的估算式驗算看看）。

1. 開門門檻（threshold）最高可以多高、最低必須多低？如果要求比 hi-hat 串音高 4 dB 的安全邊際，threshold 設多少？
2. hysteresis 最少要多少才能避免 chattering？取 6 dB 時關門門檻是多少？
3. 承上（threshold 與 hysteresis 用第 1、2 小題的值），hold 50 ms、release 100 ms。從 snare 的 peak 開始，大約多久門會開始 release？多久完全關上？
4. 取 6 dB hysteresis 時，關門門檻比 hi-hat 串音的 peak 還低。這會造成什麼現象？這是 bug 嗎？如果不想要，hysteresis 最多能設多少？
5. 如果 hysteresis = 0、hold = 0，snare 尾音的 envelope 平均值經過 threshold 時，大約有多長的時間落在「漣漪會跨越門檻」的區間？這段時間裡門最多可能開關幾次？

## 練習 3（觀念題，★☆☆）：Chattering 的成因與三種解法

1. 用自己的話解釋什麼是 gate 的 chattering、它聽起來像什麼、為什麼「門檻附近的 envelope 漣漪」會造成它。為什麼 kick 比 hi-hat 更容易 chattering？
2. 列出三種**不同機制**的解法，說明各自的原理與代價（提示：判斷規則、時間、偵測器）。
3. 為什麼 downward expander（ratio 1:2）對同樣的漣漪幾乎不會產生 chattering 的聽感？
4. lookahead 能解決 chattering 嗎？為什麼？

## 練習 4（實作題，★★☆）：Sidechain —— kick 控制 pad

起始碼：`exercises/starter/ex4_sidechain_gate.cpp`（target `l08_ex4_starter`）。測試素材是一段持續的 pad（和弦）與每拍一下的 kick。

- (a) **Ducking**：完成 `Ducker::nextGain()`。偵測 sidechain（kick）的 envelope，高於 threshold（-20 dB）時把 main（pad）壓低 `duckDb`（12 dB）；gain reduction 在 **dB 域**用第 04 章 `AttackReleaseFilter` 平滑（attack 5 ms、release 50 ms）。
- (b) **Trance gate**：在 `main()` 裡用 `Gate::process(main, sidechain)`，讓 pad 只在 kick 出現時通過（threshold -20、hysteresis 6、range -80、attack 1 ms、hold 50 ms、release 50 ms）。

self-check：ducking 期間（kick 後 15–60 ms）pad 比原本低 12 ± 1 dB；kick 之間（kick 後 350–490 ms）回到原本位準 ± 1 dB；trance gate 在 kick 後 20–100 ms 幾乎不動、350–490 ms 低於 -60 dBFS。

輸出：`dawlab_output/l08_ex4_pad.wav`、`l08_ex4_kick.wav`、`l08_ex4_ducked.wav`、`l08_ex4_trance_gate.wav`。

## 練習 5（實作題，★★☆）：Upward expander

起始碼：`exercises/starter/ex5_upward_expander.cpp`（target `l08_ex5_starter`）。

把 threshold **以上**的訊號推得更大。靜態曲線（$R \ge 1$、knee 寬 $W$）：

$$
y(x) = \begin{cases}
x, & x \le T - W/2 \\
x + \dfrac{(R - 1)(x - T + W/2)^2}{2W}, & T - W/2 < x < T + W/2 \\
T + R\,(x - T), & x \ge T + W/2
\end{cases}
$$

1. 完成 `UpwardExpander::computeOutputDb()`。
2. 完成 `nextGain()`：envelope → dB → 靜態曲線 → boost = $\min(\text{range},\ y - x)$ → 線性 gain（只有 boost 改變時才算 `dbToGain`）。

self-check：knee 兩端的值與斜率連續（數值微分，斜率 1 與 $R$）；tone bursts {-30, -20, -10, -40} dB、$T = -30$、$R = 1.5$、range +12 dB 時，輸出應為 {-30, -15, 0, -40} dB（± 0.5 dB）。

延伸思考：為什麼 upward expander 一定要有 range（最大 boost）？起始碼的偵測器設成 `SmoothingMode::Decoupled`，改成 Branching 會發生什麼事？

## 練習 6（實作題，★★★）：Lookahead gate

起始碼：`exercises/starter/ex6_lookahead_gate.cpp`（target `l08_ex6_starter`）。

完成 `LookaheadGate::process()`：偵測器看「現在」的 sample（`gate_.nextGain(det)`），主訊號寫進長度 `maxDelay_` 的環形緩衝，讀出 `delaySamples_` 之前的值再乘 gain。`setLookaheadMs()` 與 `getLatencySamples()` 已寫好。

測試訊號：0.5 秒靜音後突然出現 997 Hz、-6 dBFS 的 burst。gate 的 attack 設成 10 ms（故意很長）。self-check：

- 沒有 lookahead 時，burst 開頭 2 ms 的 RMS 比輸入低 10 dB 以上（頭被 attack 削掉）；
- lookahead 10 ms 時，把輸出對齊延遲後，burst 開頭 2 ms 的 RMS 與輸入相差 < 0.5 dB，而且逐 sample 比對 `output[burstStart + L + k]` 與 `input[burstStart + k]`（$0 \le k <$ 2 ms）的誤差 < 0.01；
- burst 到達輸入後的前 $L$ 個 sample（transient 還在延遲線裡），輸出 RMS < -60 dB；
- 穩態區間 `output[i + L] == input[i]`（延遲量正確）。

輸出：`dawlab_output/l08_ex6_input.wav`、`l08_ex6_no_lookahead.wav`、`l08_ex6_lookahead.wav`。

延伸思考：lookahead 設成和 attack 一樣長，為什麼門剛好在 transient 到達時開完？為什麼 plugin 必須回報 `getLatencySamples()`？為什麼測試訊號用 997 Hz 而不是 1 kHz（提示：1 kHz 的週期是幾個 sample？和 $L = 480$ 有什麼關係？）

## 練習 7（實作題，★★★）：LR4 分頻的 split-band de-esser

起始碼：`exercises/starter/ex7_splitband_deesser.cpp`（target `l08_ex7_starter`）。

1. 完成 `LinkwitzRileyCrossover`：`setCrossoverHz()` 用 `BiquadCoefficients::makeLowpass / makeHighpass(sampleRate, fc, 1/√2)` 設定 LP、HP 各兩級；`processSample()` 輸出 `low = lp2(lp1(x))`、`high = hp2(hp1(x))`。
2. 完成 `SplitBandDeEsser::process()`：分頻（分頻點 5 kHz）→ 只把高頻段交給第 04 章的 `Compressor`（threshold -30、8:1、attack 1 ms、release 40 ms）→ 低頻段 + 壓過的高頻段。

self-check：

- 100 Hz–15 kHz 之間 $|LP + HP|$ 在 0 dB ± 0.1 dB（相加平坦）；
- 分頻點 5 kHz 處 LP、HP 各為 -6.02 dB ± 0.2 dB；
- 齒音期間 220 Hz 母音的位準變化 < 0.3 dB，7 kHz 齒音被壓 ≥ 8 dB。

輸出：`dawlab_output/l08_ex7_input.wav`、`l08_ex7_output.wav`。

延伸思考：如果只用一級 2 階 Butterworth 當 LP / HP（不串兩級），$|LP + HP|$ 在分頻點會是多少？把 HP 反相呢？

## 練習 8（實作題，★★☆）：Transient shaper 與 level independence

起始碼：`exercises/starter/ex8_transient_shaper.cpp`（target `l08_ex8_starter`）。起始碼裡的 `MyTransientShaper` 已經準備好 fast（0.5 / 30 ms）與 slow（10 / 150 ms）兩個 `EnvelopeFollower`、平滑係數與 `process()`。

1. 完成 `mapToGainDb(d, attackDb, sustainDb)`：README 2.6.2 節的映射，$d > 0$ 用 attackDb、$d \le 0$ 用 sustainDb，$|d|$ 達到 6 dB 時飽和。
2. 完成 `nextGain()`：兩個 envelope → $d = 20\log_{10}(e_\text{fast}/e_\text{slow})$ → 映射 → 0.5 ms one-pole 平滑（dB 域）→ 線性 gain。slow envelope 低於 −80 dBFS 時目標 gain 是 0 dB。

self-check：

- 映射：$d = +3$ 時 attack +12 → +6 dB；$d = +20$ → +12 dB（飽和）；$d = -3$ 時 sustain -12 → -6 dB；$d = 0$ → 0 dB。
- 與 `dawlab::dsp::TransientShaper` 逐 sample 比對，誤差 < $10^{-5}$。
- **level-independent 測試**：同一段 `makeDrumLoop()` 分別正規化到 peak −6 dBFS 與 −26 dBFS。attack +6 dB 時，兩者的 peak 增量要大於 3 dB 且彼此相等（± 0.3 dB）；sustain −12 dB 時，兩者的尾音 RMS（每個八分音符格子的 120–240 ms）都要下降 3 dB 以上，且下降量相等（± 0.3 dB）。
- 對照組：同樣兩個 level 送進 compressor（threshold −20 dB、4:1），peak 變化相差 3 dB 以上——compressor 看絕對 level，transient shaper 不看。

輸出：`dawlab_output/l08_ex8_{loud,quiet}_{dry,attack6,sustain-12}.wav`。

延伸思考：為什麼 $d$ 只跟「比例」有關，整段訊號乘上常數 $k$ 時 $d$ 完全不變（提示：`EnvelopeFollower` 是線性的嗎）？什麼情況下 level independence 會被破壞（提示：−80 dBFS 的 floor）？
