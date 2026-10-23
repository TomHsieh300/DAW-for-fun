# 第 04 章 解答與解說

[English](README.md) | **繁體中文**

題目在 [`../exercises/README.md`](../exercises/README.zh-TW.md)。每一支解答程式都有 self-check，`.\scripts\build.ps1 -Only 04-compressor` 會以 ctest（label `solution`）執行它們；手動執行時先 `cd` 到 build 目錄，例如 `.\bin\Debug\l04_ex3_solution.exe`。下面引用的數字都是解答程式實際印出的結果（48 kHz、block size 512）。

| 題目 | 解答檔 | target |
|---|---|---|
| 練習 1 觀念題 | （本文） | — |
| 練習 2 計算題 | `ex2_calculations.cpp`（驗算手算答案） | `l04_ex2_solution` |
| 練習 3 parallel compression | `ex3_parallel.cpp` | `l04_ex3_solution` |
| 練習 4 sidechain HPF | `ex4_sidechain_hpf.cpp` | `l04_ex4_solution` |
| 練習 5 auto makeup | `ex5_auto_makeup.cpp` | `l04_ex5_solution` |
| 練習 6 feedback 拓樸 | `ex6_feedback.cpp` | `l04_ex6_solution` |
| 練習 7 RMS 視窗 vs pumping | `ex7_rms_window.cpp` | `l04_ex7_solution` |

---

## 練習 1：方塊圖與設計選擇

**1. 四個方塊各負責什麼？**

- **Detector**：把訊號變成「現在多大聲」的 level。Peak 模式直接取 $\lvert x \rvert$，RMS 模式取 $x^2$ 的移動平均再開根號。
- **Gain computer**：依靜態曲線（threshold、ratio、knee）把 level（dB）換成「應該壓幾 dB」的 GR 目標值。
- **Attack / release 平滑**：決定 GR 以多快的速度趨近目標——上升（壓下去）用 attack 時間常數、下降（放開）用 release 時間常數。
- **Makeup gain**：壓完之後整體變小聲，用固定增益補回來。

**沒有狀態的純函式是 gain computer**：同樣的輸入 dB 永遠得到同樣的輸出 dB，所以 `Compressor::computeOutputDb()` 是 `static`，UI 可以直接拿來畫 transfer curve。（Peak 模式的 detector 本身也沒有狀態；RMS 模式的 detector 有。）

**2. 為什麼在 log domain 平滑？**

- 在 log domain，平滑的對象是 GR 本身（dB），所以 attack / release 的時間曲線跟 threshold、ratio、訊號超過多少都無關：attack 10 ms 就是 GR 在 10 ms 走完 63.2%。在 linear domain 先平滑 envelope，再經過非線性的靜態曲線，GR 的時間曲線形狀會隨訊號大小而變。
- Release 形狀：linear domain 的 envelope 指數衰減，換成 dB 是**等速**下降（每 $\tau$ 掉 8.686 dB），GR 跟著直線掉、碰到 threshold 就突然停住（範例 2 的 B4：release 150 ms 後 GR 已經是 0）。Log domain 的 GR 本身指數衰減，越接近 0 越慢，尾巴拖長、比較平順自然（B1：150 ms 時還有 1.86 dB、400 ms 時 0.15 dB）。

**3. Branching 為什麼低估正弦波的 peak？Decoupled 怎麼解決？**

Peak detector 給的是瞬間 $\lvert x[n] \rvert$。正弦波每半個週期就經過 0，瞬間 level 掉到 -100 dB，GR 目標值跟著掉到 0。Branching 平滑在這些時刻會切換成 release 係數往下拉，一個週期內「往上拉一點、往下拉一點」，穩態平衡點落在靜態值之下（範例 2 的 B2：6.47 dB，靜態值 7.50 dB）。

Decoupled 先過一個「瞬間上升、以 release 衰減」的 peak-hold：$y_1 = \max(x, \alpha_R y_1 + (1-\alpha_R)x)$。在兩個波峰之間（0.5 ms）peak-hold 只以 100 ms 的 release 衰減一點點，等於把 ripple 填平，再交給 attack 濾波器，所以穩態 GR 幾乎精確等於靜態值（B1：7.49 dB）。

**4. 為什麼 stereo link 預設要開？**

不 link 時兩個 channel 各自壓，比較大聲的那一邊壓得多，左右音量差縮小，聲像就往小聲那邊漂，而且會隨音樂動態來回飄動。範例 4：L -6 dBFS、R -30 dBFS，unlinked 時左右差從 24 dB 縮成 13.52 dB；linked 時兩邊同樣壓 10.48 dB，左右差維持 24.00 dB。

會想關掉的情況：左右聲道其實是兩個不相干的訊號（例如 dual-mono 的兩支麥克風、左右各一把吉他），或是故意要讓立體聲「變寬 / 變動」的創意用法。

**5. 為什麼 attack 1 ms 抓不住鼓的尖峰？**

One-pole 的 GR 在第 $n$ 個 sample 只走了 $1 - \alpha^n$。attack 1 ms @ 48 kHz 時 $\alpha = e^{-1/48}$，第一個 sample 只到目標的 $1 - e^{-1/48} = 2.06\%$。鼓組的 peak 是 snare 雜訊、hi-hat 這種只有幾個 sample 寬的尖峰，GR 還沒升起來尖峰就過去了（範例 3：attack 1 ms 時最大 GR 12.53 dB，但 peak 只降 5.48 dB）。

要「保證」抓住，需要 **lookahead**：把主訊號延遲幾 ms，讓 detector 先看到未來的尖峰、提前把 gain 降好。代價是延遲（latency）。這正是第 05 章 limiter 的做法。

---

## 練習 2：靜態曲線與時間常數

`l04_ex2_solution` 會把以下每一個答案和 `computeOutputDb()`、`attackReleaseCoefficient()` 對答案（17 checks 全部 PASS）。

**1. $T = -24$、$R = 3$、hard knee**：threshold 以上 $y = T + (x - T)/R$。

| 輸入 | 輸出 | GR |
|---|---|---|
| -30 | -30（在 threshold 以下，不動） | 0 |
| -24 | -24（剛好在 threshold） | 0 |
| -12 | $-24 + 12/3 = -20$ | 8 |
| 0 | $-24 + 24/3 = -16$ | 16 |

**2. Soft knee $W = 12$**：knee 範圍是 $T \pm W/2 = -30 \ldots -18$ dB。knee 內 $y = x + \frac{(1/R - 1)(x - T + W/2)^2}{2W}$，其中 $1/R - 1 = -2/3$、$2W = 24$：

- $x = -24$：$x - T + W/2 = 6$，$y = -24 + (-2/3)(36/24) = -24 - 1 = -25$ dB（threshold 處已有 1 dB GR；也可以用 $y(T) = T + (1/R - 1)W/8 = -24 - 1$ 驗算）。
- $x = -20$：$x - T + W/2 = 10$，$y = -20 + (-2/3)(100/24) = -20 - 2.7778 = -22.7778$ dB（hard knee 是 -22.667，soft knee 壓得多一點，因為在 knee 裡斜率從 1 慢慢變成 1/3，平均起來比「一刀切」多壓一些）。

**3. 上緣 $x = -18$**：$x - T + W/2 = 12 = W$，二次式 $y = -18 + (-2/3)(144/24) = -18 - 4 = -22$；hard knee 直線 $y = -24 + 6/3 = -22$。兩者相等。一般式在本章 README 2.3 節：上緣值 $= T + \frac{W}{2R}$，這裡 $-24 + 12/6 = -22$。解答程式也用數值微分確認 knee 內側的斜率 ≈ 0.334 ≈ 1/R。

**4. 係數**（$\alpha = e^{-1/(\tau f_s)}$）：

- $\tau_A f_s = 0.005 \times 44100 = 220.5$，$\alpha_A = e^{-1/220.5} = 0.995475$
- $\tau_R f_s = 0.2 \times 44100 = 8820$，$\alpha_R = e^{-1/8820} = 0.999887$

**5. 到達比例的時間** $t_p = -\tau \ln(1 - p)$：

- attack 到 90%：$5 \ln 10 = 11.51$ ms；到 99%：$5 \ln 100 = 23.03$ ms
- release 衰減到 10%（= 走完 90%）：$200 \ln 10 = 460.5$ ms

（解答程式用 step response 模擬量到 11.52、23.04、460.5 ms，差的 0.01 ms 是「取第一個超過的 sample」造成的量化。）

**6. 「10 ms 到 90%」換算**：要 $\tau \ln 10 = 10$ ms，所以 $\tau = 10 / \ln 10 = 4.343$ ms，`setAttackMs(4.343f)`。模擬驗證：到 90% 的時間 10.02 ms。一般規則：**90% 定義的數字除以 2.303 就是本章的 τ**。

---

## 練習 3：Parallel（New York）compression

解答：`ex3_parallel.cpp`。

關鍵思路：

1. `compressBlocks()`：複製一份 buffer，用 `AudioBlock::getSubBlock()` 逐 512 個 sample 呼叫 `process()`，每個 block 之後讀一次 `getGainReductionDb()` 取最大值——這就是 GR meter 在 UI 上做的事。
2. Wet 用「重壓」設定：$T = -30$、10:1、knee 3 dB、attack **0.02 ms**、release 60 ms。makeup = dry peak dB − wet peak dB（實測 +20.64 dB，最大 GR 24.37 dB）。Attack 要極快，理由見範例 3 的掃描：attack ≥ 1 ms 時尖峰溜過去，wet 的 crest 反而變大，parallel 就失去意義。
3. `parallelMix()`：先 `std::clamp(mix, 0, 1)`，再逐 sample $y = (1 - \text{mix})\,d + \text{mix}\,w$。

實測：

```
crest factor: dry 14.51 dB, wet 11.03 dB
mix 0.00: peak  -2.92 dBFS, crest 14.51 dB
mix 0.25: peak  -3.67 dBFS, crest 13.04 dB
mix 0.50: peak  -4.50 dBFS, crest 11.33 dB
mix 0.75: peak  -4.67 dBFS, crest 10.22 dB
mix 1.00: peak  -2.92 dBFS, crest 11.03 dB
```

觀察：

- mix 0.5 的 crest（11.33 dB）介於 dry 與 wet 之間，self-check 通過。
- **mix 0.75 的 crest（10.22 dB）比純 wet 還低**，peak 也比 dry、wet 都低（-4.67 dBFS）。原因是 dry 與 wet 的尖峰位置不完全對齊：wet 的 peak 被壓扁、出現在別的地方，混合後兩邊的 peak 不會同時出現，而 RMS（身體）兩邊都有，加起來就「peak 低、能量高」。這就是 parallel compression 好用的原因——它不只是 dry 和 wet 的內插。
- 聽感（`l04_ex3_parallel_50.wav` 對 `l04_ex3_parallel_wet.wav`）：wet 很扁、房間感與尾巴被拉起來；50% mix 保留了 dry 的鼓頭衝擊，同時有 wet 的厚度。

---

## 練習 4：Sidechain HPF

解答：`ex4_sidechain_hpf.cpp`。

**HPF**：$lp[n] = lp[n-1] + b\,(x[n] - lp[n-1])$、$b = 1 - e^{-2\pi f_c / f_s}$、$hp = x - lp$。這是一階 lowpass 的「互補」，所以在 $f_c$ 附近是 -3 dB、低於 $f_c$ 以每 octave 6 dB 衰減。實測（用 `measureGainDb`）：50 Hz -18.36 dB、400 Hz -3.24 dB、3 kHz -0.30 dB。（400 Hz 處是 -3.24 dB 而不是 -3.01 dB。lowpass 本身在 $f_c$ 是精確的 -3.01 dB（$\lvert H_{lp}\rvert = 0.7074$），偏差來自 $hp = x - lp$ 這個互補結構：令 $a = 1 - b$，lowpass 是 $H_{lp}(z) = \dfrac{b}{1 - a z^{-1}}$，所以
$$H_{hp}(z) = 1 - H_{lp}(z) = \frac{a\,(1 - z^{-1})}{1 - a z^{-1}}$$
它在高頻（$z = -1$，Nyquist）的增益只到 $2a/(1+a) \approx 0.974$（約 -0.23 dB），而不是類比一階 HPF 的 1，相位也和類比版不同，所以整條 HPF 曲線略低一點：$f_c$ 處 -3.24 dB、3 kHz 處 -0.30 dB（類比一階 HPF 在 3 kHz 只會是 -0.08 dB）。對 sidechain 用途完全足夠。）

**流程**：sidechain buffer 在迴圈外配置一次（模擬 `prepare()`），每個 block：`sc.copyFrom(block)` → `hpf->process(sc)` → `comp.process(block, sc)`。gain 永遠套在**原始**訊號上，HPF 只影響 detector「聽到什麼」。

**數字**：輸入 50 Hz @ -6 dBFS + 3 kHz @ -24 dBFS，峰值約 $20\log_{10}(0.501 + 0.063) = -4.97$ dBFS。$T = -20$、4:1、hard knee：

- 沒有 HPF：靜態估計 GR = $(−4.97 + 20) \times 0.75 = 11.27$ dB；實測 GR 11.23 dB，3 kHz 成分被拖下去 **11.08 dB**。小聲的高頻被大聲的低頻「綁架」了。
- HPF 400 Hz：sidechain 裡 50 Hz 掉了約 18 dB，剩下 -24 dBFS 左右，加上 3 kHz 也只稍微超過 threshold，GR 只剩 **1.21 dB**，3 kHz 只少 1.19 dB。

聽感：`l04_ex4_no_hpf.wav` 的高音隨著 50 Hz 的每個波峰被「調變」（50 Hz 的 GR ripple 也會產生失真）；`l04_ex4_with_hpf.wav` 高音穩定。實務上 sidechain HPF 常設 60–150 Hz（讓 kick、bass 不要主導 bus compressor），本題用 400 Hz 是為了讓差異明顯。

---

## 練習 5：Auto makeup gain

解答：`ex5_auto_makeup.cpp`。

$$
\text{makeup} = -\,y(0) = -\,\text{computeOutputDb}(0, T, R, W)
$$

| 設定 | 算式 | makeup |
|---|---|---|
| $T=-20$、4:1、hard | $-(-20 + 20/4)$ | 15 dB |
| $T=-20$、4:1、$W=10$ | 0 dBFS 在 knee（-25..-15）之外，同上 | 15 dB |
| $T=-30$、2:1、hard | $-(-30 + 30/2)$ | 15 dB |
| $T=-10$、10:1、hard | $-(-10 + 10/10)$ | 9 dB |
| $T=-6$、4:1、$W=20$（「想一想」） | knee 是 -16..+4，0 dBFS 在 knee 內：$y = 0 + (-0.75)(0 + 6 + 10)^2/40 = -4.8$ | **4.8 dB** |

最後一列是陷阱：如果直接用 hard knee 公式 $-(−6 + 6/4) = 4.5$ dB 會差 0.3 dB，因為 soft knee 在 threshold 以上一點點的地方比 hard knee 多壓。

實跑（attack 1 ms、release 100 ms、1 kHz 正弦）：0 dBFS 進 → **0.01 dBFS** 出；-6 dBFS 進 → **-1.49 dBFS** 出（預期 $-20 + 14/4 + 15 = -1.5$）；不加 makeup 時 0 dBFS 進 → -14.99 dBFS。0.01 dB 的差異來自 decoupled peak-hold 在兩個波峰之間的微小衰減（同範例 4 的 10.48 對 10.5）。

為什麼很多 plugin 只補一半（7.5 dB）？auto makeup 假設訊號一直頂在 0 dBFS，但真實素材的 peak 通常在 -6 ~ -12 dBFS，那裡的 GR 比 0 dBFS 小，補滿 15 dB 會讓輸出明顯比輸入大聲，A/B 比較時會被「比較大聲就比較好聽」騙。比較嚴謹的作法是依實際素材的平均 GR 補，或用 loudness matching（第 11 章 `LoudnessMeter`）。

---

## 練習 6：Feedback 拓樸

解答：`ex6_feedback.cpp`。

**實作重點**：`FeedbackCompressor` 保存上一個輸出 sample `lastOutput_`；每個 sample：

```cpp
const float levelDb  = gainToDb(std::abs(lastOutput_));                 // 看輸出，不看輸入
const float grTarget = std::max(0.0f, slope_ * (levelDb - thresholdDb_)); // 斜率 s 的 hard knee
const float gr       = smoother_.process(grTarget);                      // AttackReleaseFilter（decoupled）
lastOutput_ = x * dbToGain(-gr);
```

一個 sample 的延遲在 48 kHz 下無關緊要，而且這正是「detector 只能看到已經算出來的輸出」的本質。

**推導**（README 2.6 節）：穩態時 $Y = X - \text{GR}$、$\text{GR} = s(Y - T)$ ⇒ $\text{GR} = \frac{s}{1+s}(X - T)$，$R_{\text{eff}} = 1 + s$。

**實測**（tone burst -30 → -10 → -30 dBFS，$X - T = 10$ dB，attack 10 ms、release 100 ms）：

```
feedforward R = 4 (s = 0.75): steady-state GR 7.49 dB (theory 7.50), t63 = 10.77 ms
feedback s = 0.75 (R_eff = 1.75): steady-state GR 4.28 dB (theory s/(1+s)·10 = 4.29), t63 = 5.27 ms
feedback s = 3.00 (R_eff = 4.00): steady-state GR 7.50 dB (theory s/(1+s)·10 = 7.50), t63 = 2.79 ms
feedback s = 9.00 (R_eff = 10.00): steady-state GR 9.00 dB (theory s/(1+s)·10 = 9.00), t63 = 1.79 ms
```

解讀：

- 把 feedforward 的 4:1 斜率（0.75）直接搬到 feedback，只得到 1.75:1（4.28 dB）。要 feedback 做到 4:1 必須 $s = 3$。
- **同樣是 4:1、同樣的 attack 10 ms**，feedback 的 t63 只有 2.79 ms，比 feedforward 的 10.77 ms 快很多。原因：attack 剛開始時輸出還沒被壓，detector 看到超過 10 dB，GR 目標值是 $3 \times 10 = 30$ dB（遠大於穩態的 7.5），平滑器朝 30 dB 衝，很快就越過 7.5 dB 的 63%；隨著輸出被壓下來，目標值跟著降低，最後收斂在 7.5。所以 feedback 的 attack 是 **program-dependent**：超過越多，attack 前段越快。
- $s$ 越大越快（1.79 ms），也越接近 limiter 的行為；但 $R_{\text{eff}} = 1 + s$ 永遠有限，feedback 做不出 ∞:1，也無法 lookahead。
- 穩定性：$s > 1$ 時迴路增益大於 1，如果 attack = 0（GR 每個 sample 立即跟著目標跳），GR 會在「壓太多 → 輸出變小 → 放開 → 輸出變大」之間來回震盪。attack 平滑等於在迴路裡加了 lowpass，所以穩定。

聽感：`l04_ex6_feedback.wav` 的第二段開頭 overshoot 持續的時間明顯比範例 2 的 `l04_step_out.wav` 短（GR 在約 3 ms 內就壓下 63%，feedforward 要約 11 ms），「頭」比較快被收住。

---

## 練習 7：RMS 視窗長度 vs pumping

解答：`ex7_rms_window.cpp`。

**實作重點**：`setDetectorMode(DetectorMode::Rms)` + `setRmsWindowMs(w)`；跳過前 0.5 秒暖機（長視窗的 RMS detector 需要時間「填滿」，否則 min GR 會被初始的 0 污染）；每個 block 後讀 `getGainReductionDb()` 更新 max / min。

實測（$T = -24$、4:1、knee 3、attack 1 ms、release 50 ms）：

```
Peak detector (reference): GR max 15.46 / min 0.04, swing 15.41 dB, output crest 18.98 dB
RMS window     1 ms: GR max 14.20 / min 0.00, swing 14.19 dB, output crest 19.69 dB
RMS window     5 ms: GR max 12.65 / min 0.00, swing 12.65 dB, output crest 20.21 dB
RMS window    10 ms: GR max 11.75 / min 0.00, swing 11.75 dB, output crest 20.61 dB
RMS window    30 ms: GR max 10.59 / min 0.00, swing 10.59 dB, output crest 20.72 dB
RMS window   100 ms: GR max 8.99 / min 0.00, swing 8.99 dB, output crest 20.29 dB
RMS window   300 ms: GR max 7.54 / min 0.45, swing 7.09 dB, output crest 19.00 dB
RMS window  1000 ms: GR max 5.97 / min 1.96, swing 4.00 dB, output crest 17.80 dB
```

解讀：

- **擺動幅度隨視窗單調下降**（14.19 → 4.00 dB），1 ms 是 1000 ms 的 3.5 倍；1 ms 的 RMS 與 peak detector 只差 1.22 dB——視窗短到比鼓聲本身還短時，RMS 就退化成 peak。這三點都是 self-check。
- 短視窗：每一下 kick / snare 都讓 GR 衝到 14 dB 又放掉，整個 loop 跟著節拍「呼吸」——這就是 **pumping**。有時是想要的效果（EDM 的 sidechain pumping），但用在 bus 上通常是缺點。
- 長視窗：detector 看到的是「這一秒的平均能量」，GR 幾乎固定在 2–6 dB，壓縮變成緩慢的整體音量控制，鼓聲本身的動態被保留。
- Crest factor 先升後降：中等視窗（10–30 ms）時 GR 會在鼓頭之後才升起來，壓到的是鼓身，crest 反而最大（同範例 3 的「慢 attack」現象）；視窗再長，GR 不再跟著單一鼓聲動，crest 回落。這說明 RMS 視窗在效果上和 attack 時間是互相疊加的。

聽感：`l04_ex7_rms_1ms.wav` 明顯在每拍之間「吸氣」；`l04_ex7_rms_100ms.wav` 比較平穩、像是整體被往下壓了一層。
