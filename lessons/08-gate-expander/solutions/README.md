# 第 08 章 解答

題目見 [`../exercises/README.md`](../exercises/README.md)。實作題的完整程式在本資料夾的 `exN_*.cpp`，每支都有 self-check，用 `ctest -L solution` 或直接執行 `l08_exN_solution` 驗證。

---

## 練習 1：Expander 的輸出 dB

### 1. Hard knee（$T = -40$、$R = 2$、range $-30$）

低於 threshold 時 $g = (R - 1)(x - T)$，再取 $\max(\text{range}, g)$：

| 輸入 | 靜態 gain $(R-1)(x-T)$ | 套用 range 後 | 輸出 |
|---:|---:|---:|---:|
| -30 | 0（在 threshold 以上） | 0 | **-30** |
| -45 | $1 \times (-5) = -5$ | -5 | **-50** |
| -60 | $1 \times (-20) = -20$ | -20 | **-80** |
| -80 | $1 \times (-40) = -40$ | **-30**（被 range 限制） | **-110** |

只有 -80 dB 被 range 限制住：沒有 range 時輸出會是 -120 dB。

### 2. Soft knee $W = 10$ dB

knee 範圍 $[T - W/2,\ T + W/2] = [-45,\ -35]$，knee 內 $y = x + \frac{(1 - R)(x - T - W/2)^2}{2W} = x - \frac{(x + 35)^2}{20}$：

| 輸入 | 計算 | 輸出 |
|---:|---|---:|
| -35 | 上緣，$(x + 35)^2 = 0$ | **-35.00** |
| -40 | $-40 - 25/20$ | **-41.25** |
| -43 | $-43 - 64/20$ | **-46.20** |
| -45 | 下緣，$-45 - 100/20 = -50$；直線 $-40 + 2(-5) = -50$ 也相同 | **-50.00** |

-40 dB 的結果也可以用 README 的捷徑 $y(T) = T + (1 - R)W/8 = -40 - 1.25$ 得到。所有 gain 都在 range 之內。

### 3. 求 ratio

$-65 = -40 + R(-50 + 40) \Rightarrow -25 = -10R \Rightarrow R = 2.5$（1:2.5）。gain = -15 dB，在 range -30 之內。

### 4. Gate attack 斜率

$$
\Delta_a = \frac{|\text{range}|}{t_a f_s} = \frac{80}{0.002 \times 48000} = \frac{80}{96} \approx 0.833\ \text{dB/sample}
$$

0.5 ms = 24 samples → 爬了 $24 \times 0.833 = 20$ dB → gain = $-80 + 20 = -60$ dB。注意 0.5 ms 時線性 gain 只有 0.001——attack 的前半段幾乎還是靜音，這就是「頭被吃掉」的來源（練習 6）。

---

## 練習 2：Hysteresis 門檻怎麼設

### 1. Threshold

- 必須**高於** hi-hat 串音的 peak（-30 dB），否則 hi-hat 會把門打開。
- 必須**低於** snare 的 peak（-6 dB），而且最好低很多：門檻越低，snare 起音越早觸發，頭越完整。
- 加 4 dB 安全邊際：**threshold = -26 dB**。

### 2. Hysteresis

先驗算題目給的漣漪：README 2.4.1 節的估算式 $8.686\,(1 - 2/\pi)/(2f\tau_r) = 8.686 \times 0.363 / (2 \times 185 \times 0.003) \approx 2.8$ dB，和題目的 3 dB 一致（若偵測器 release 是本章 `Gate` 預設的 20 ms，只有約 0.43 dB，範例 1 的 [4] 實測 0.41 dB）。

漣漪峰對峰 3 dB，hysteresis 必須 **> 3 dB**，envelope 的波峰與波谷才不會同時跨過兩條門檻。取 6 dB（一倍的餘裕）→ **關門門檻 = -26 − 6 = -32 dB**。

### 3. 開始 release 與完全關上的時間

envelope 從 -6 dB 掉到 -32 dB：$\frac{26\ \text{dB}}{60\ \text{dB/s}} \approx 0.433$ s → 進入 Hold；hold 50 ms → **約 0.483 s 開始 release**；本章 `Gate` 的 release 時間是「從 0 dB 降到 range」的時間，所以再 100 ms → **約 0.583 s 完全關上**。

### 4. 關門門檻低於 hi-hat 串音

在 snare 尾音期間門是開的，這時只要 hi-hat 串音的 envelope 還在 -32 dB 以上，就**不會**觸發關門（Open 狀態只在 envelope < -32 dB 時離開；Hold 期間也會因為 ≥ -26 dB 才重置——-30 dB 的串音不會重置 Hold，但會讓 Open 延後結束）。結果是 hi-hat 串音「搭便車」：它無法自己開門，卻能讓已經開的門晚一點關，snare 尾音後面會多一段 hi-hat。

這不是 bug，而是 hysteresis 的本質：**開門由 $T_\text{open}$ 決定，維持開啟由 $T_\text{close}$ 決定**。如果不想要：

- 讓 $T_\text{close} > -30$：hysteresis < $-26 - (-30) = 4$ dB。又要 > 3 dB（漣漪），所以只剩 3–4 dB 的窄窗（例如 3.5 dB），幾乎沒有餘裕。
- 更好的做法：hysteresis 保持 3–4 dB，chattering 的剩餘風險交給 **hold**（時間）處理；或在 sidechain 加 lowpass / bandpass（只聽 snare 本體的 150–250 Hz），讓 hi-hat 串音在偵測訊號裡變得更小。

### 5. 沒有 hysteresis 時的 chattering

envelope 的平均值以 60 dB/s 下降，平均值落在 $[T - 1.5,\ T + 1.5]$ 的期間，漣漪都可能跨過門檻：

$$
\frac{3\ \text{dB}}{60\ \text{dB/s}} = 50\ \text{ms}
$$

漣漪來自 185 Hz 全波整流的波峰，每秒 370 個，50 ms 內約 **18 個**波峰，所以最多可能開關約 18 次——聽起來像尾音末端有一段 370 Hz 的「嗡」或碎裂聲。範例 1 的鼓組 loop 實測：hysteresis 0 + hold 0 有 70 次 state 轉換，hysteresis 6 dB + hold 20 ms 只有 43 次。

---

## 練習 3：Chattering 的成因與三種解法

### 1. 成因

Chattering 是 gate 在門檻附近快速重複開關。偵測器的 envelope 不是平滑曲線：peak detector 在每個波峰被 attack 拉上去，兩個波峰之間按 release 往下掉，形成漣漪。當訊號（例如鼓的尾音）**平均** level 慢慢經過門檻時，漣漪的波峰在門檻上、波谷在門檻下，門就跟著漣漪的頻率開開關關。每次開關都是 80 dB 的增益變化，所以聽起來是碎裂、嗶剝或嗡嗡聲，而不是平順的衰減。

kick 比 hi-hat 更容易 chattering：漣漪大小取決於「兩個波峰之間的時間 / 偵測器 release」。kick 是 45–120 Hz，波峰間隔 4–11 ms；hi-hat 是數 kHz，間隔不到 0.2 ms。相同的偵測器 release 下，kick 的 envelope 漣漪大得多：漣漪 ≈ $8.686\,(1 - 2/\pi)/(2f\tau_r)$ dB 和頻率成反比（README 2.4.1 節；範例 1 的 [4] 實測 release 20 ms 時 45 Hz 是 1.56 dB、1 kHz 只有 0.08 dB）；而且 kick 的尾音長、以低頻為主，經過門檻的時間也長。

### 2. 三種不同機制的解法

| 解法 | 機制 | 原理 | 代價 |
|---|---|---|---|
| **Hysteresis** | 判斷規則 | 開門門檻 $T$、關門門檻 $T - H$（Schmitt trigger）。只要 $H$ > 漣漪峰對峰值，漣漪就不可能同時跨過兩條線 | 關門門檻較低，門會開得比較久；串音可以「讓已開的門晚點關」（練習 2 第 4 小題） |
| **Hold** | 時間 | 低於關門門檻後先等 hold 毫秒才 release，期間訊號回來就取消。漣漪造成的「短暫低於門檻」只有幾毫秒，被 hold 吸收 | 訊號真的結束後，雜訊還會多留 hold 毫秒；hold 太長在快速段落會讓門「關不起來」 |
| **偵測器平滑** | 偵測器 | 拉長偵測器 release、改用 RMS 偵測、或在 sidechain 先 lowpass，讓 envelope 本身的漣漪變小 | 偵測變慢：門晚開（頭被吃掉，可用 lookahead 補）、晚關；RMS 對 transient 的反應比 peak 小 |

（另外，把 gate 的 attack / release 斜坡拉長也能讓 chattering「比較不刺耳」，但它沒有消除開關的決策本身，只是把每次開關的增益變化變小，屬於治標。）

### 3. 為什麼 expander 不太會 chattering

expander 的 gain **隨 level 連續變化**：漣漪讓 level 抖 3 dB，ratio 1:2 的 gain 只跟著抖 $(R - 1) \times 3 = 3$ dB，是一個小小的振幅調變（而且被偵測器的 attack / release 進一步平滑）。gate 則是**不連續**的：門檻兩側的 gain 差 80 dB，同樣 3 dB 的漣漪會被放大成 80 dB 的跳動。ratio 越大，expander 就越接近 gate，chattering 的風險也越高——這也是有些 expander 也提供 hysteresis 參數的原因。

### 4. Lookahead 不能解決 chattering

lookahead 只是把主訊號相對於偵測器延遲 $L$：偵測器看到的 envelope、做出的開關決策序列完全一樣，只是套用時間往後挪了 $L$。開關的次數不變，所以 chattering 一樣存在。lookahead 解決的是 transient 的頭被 attack 削掉的問題（練習 6）。

---

## 練習 4：Sidechain —— kick 控制 pad

解答：[`ex4_sidechain_gate.cpp`](ex4_sidechain_gate.cpp)（target `l08_ex4_solution`）。

**(a) Ducker 的關鍵**：

```cpp
float nextGain(float sidechainSample) noexcept {
    const float envDb = gainToDb(detector_.processSample(sidechainSample));
    const float targetGr = envDb >= thresholdDb_ ? duckDb_ : 0.0f;   // 「∞:1 的 gate 式」靜態曲線
    const float gr = smoother_.process(targetGr);                      // dB 域平滑（AttackReleaseFilter）
    return dbToGain(-gr);
}
```

- 偵測器吃 sidechain（kick），gain 乘到 main（pad）：這就是 sidechain 的全部意義——偵測和套用是兩條不同的訊號。
- 平滑放在 dB 域（gain reduction 上）而不是 envelope 上：attack 5 ms 決定「kick 來時壓下去多快」、release 50 ms 決定「pad 回來多快」，與第 04 章 compressor 的 log-domain 平滑同一個概念。也可以把靜態曲線換成比例式（例如 4:1 compressor 的曲線），就是正規的 sidechain compression。

**(b) Trance gate**：直接 `gate.process(padBlock, kickBlock)`。`Gate` 的 sidechain 版本允許 channel 數不同（mono kick 控制 stereo pad）。

**實測結果**：

```
[a] ducking：pad 原本 -13.94 dBFS；kick 後 15–60 ms -25.85 dBFS（應低 12 dB）；kick 後 350–490 ms -14.10 dBFS（應回到原本）
[b] trance gate：kick 後 20–100 ms -14.06 dBFS（應 ≈ -14.06）；kick 後 350–490 ms -94.01 dBFS（應 < -60）
[PASS] l08_ex4_sidechain_gate (5 checks passed)
```

ducking 量 = 25.85 − 13.94 = 11.9 dB（目標 12 ± 1）。trance gate 關門期間 -94 dBFS = pad 的 -14 dB 加上 range -80 dB。

聽 `l08_ex4_ducked.wav`：pad 在每拍 kick 處「吸」下去再回來（EDM 的 pumping）；`l08_ex4_trance_gate.wav`：pad 被切成跟著 kick 的節奏短音。

---

## 練習 5：Upward expander

解答：[`ex5_upward_expander.cpp`](ex5_upward_expander.cpp)（target `l08_ex5_solution`）。

**靜態曲線**是 README 2.7 節三段式的直接翻譯；錨點放在 knee **下緣**（$x - T + W/2$），所以下緣接 $y = x$、上緣斜率 $1 + 2\frac{R - 1}{2W} W = R$。

**nextGain**：`boost = min(range, y − x)`，boost ≥ 0。

**實測**（節錄）：

```
[1] 靜態曲線 T=-30 R=1.5 knee=10：knee 下緣 -35 dB → -35.000；上緣 -25 dB → -22.500
    knee 下緣斜率 1.000（應 ≈ 1）、上緣斜率 1.500（應 ≈ 1.5）
[2] tone bursts（T = -30、R = 1.5、最大 boost 12 dB）
    段落  輸入    輸出實測  理論
       1   -30.0    -30.00   -30.0
       2   -20.0    -15.01   -15.0
       3   -10.0     -0.01     0.0
       4   -40.0    -40.00   -40.0

[3] 對照：偵測器改用 Branching（其他設定相同）
    段落  輸入    Decoupled  Branching
       1   -30.0     -30.00     -30.00
       2   -20.0     -15.01     -15.58
       3   -10.0      -0.01      -0.58
       4   -40.0     -40.00     -40.00
    Branching 在 -20 dB 段比理論低 0.58 dB
[PASS] l08_ex5_upward_expander (13 checks passed)
```

上緣的值：$T + R \cdot W/2 = -30 + 1.5 \times 5 = -22.5$ ✓。-10 dB 段：$-30 + 1.5 \times 20 = 0$ dB，boost 10 dB < 12 dB 上限，所以輸出 0 dB（剛好碰到 0 dBFS——這就是為什麼一定要有 range：輸入再大 1 dB 就會超過 0 dBFS，而且靜態曲線本身沒有上限）。

**延伸思考：Decoupled vs Branching**。偵測器若用 Branching 模式，穩態 1 kHz 正弦波的 envelope 在每個波峰之間按 release 下降、再被 5 ms 的 attack 慢慢拉回，平均起來比真正的 peak 低約 1.2 dB。輸出是「真正的輸入 $x$ + boost」，boost $= (R - 1)(L - T)$ 只透過偵測到的 level $L$ 受影響，所以輸出的誤差是 $(R - 1)\delta = 0.5 \times 1.16 \approx 0.58$ dB。解答的 [3] 用同樣設定改成 Branching 實測：-20 dB 段 -15.58 dB、-10 dB 段 -0.58 dB，超出 self-check 的 ±0.5 dB。Decoupled 先做 peak-hold（瞬間上升）再用 attack 平滑，穩態 level 就等於 peak。本章的 `Expander` 也採用 Decoupled（README 2.3 節）。

---

## 練習 6：Lookahead gate

解答：[`ex6_lookahead_gate.cpp`](ex6_lookahead_gate.cpp)（target `l08_ex6_solution`）。

**關鍵程式**：

```cpp
const float g = gate_.nextGain(det);                       // 偵測器看「現在」
const int readPos = (writePos_ - delaySamples_ + maxDelay_) % maxDelay_;
for (int ch = 0; ch < nch; ++ch) {
    ring[writePos_] = d[i];                                // 先寫
    d[i] = (delaySamples_ > 0 ? ring[readPos] : d[i]) * g; // 再讀 L 個 sample 之前的值
}
writePos_ = (writePos_ + 1) % maxDelay_;
```

環形緩衝的長度是「最大 lookahead + 1」：因為是先寫後讀，若延遲 $L$ 等於緩衝長度，讀取位置會和寫入位置重疊，讀到的是剛寫進去的值（等於沒有延遲，卻回報了 $L$ 的 latency）。這是環形緩衝最常見的 off-by-one。

**實測**（節錄）：

```
lookahead 10 ms = 480 samples（getLatencySamples）
burst 前 2 ms 的 RMS：輸入 -9.00 dB；無 lookahead -79.16 dB（頭被削掉 70.2 dB）；lookahead -9.00 dB
burst 前 2 ms 逐 sample：|output[burstStart + L + k] − input[burstStart + k]| 最大 1.24e-03；[burstStart, burstStart + L) 的輸出 RMS -120.0 dB
穩態區間 output[i + L] 與 input[i] 的最大誤差：0.00e+00
[PASS] l08_ex6_lookahead_gate (9 checks passed)
```

（-6 dBFS 正弦波的 RMS 約 -9.0 dB。）沒有 lookahead 時，attack 10 ms 的斜坡在前 2 ms 只從 -80 dB 爬到 -64 dB，所以頭幾乎完全被削掉。

**為什麼 self-check 要逐 sample 比對，而且用 997 Hz**：只比「頭 2 ms 的 RMS」分不出有沒有延遲。attack 10 ms 的門在 burst 開始後 480 samples 剛好開完，所以一個**完全沒有延遲線**、只做 `d[i] *= g` 的錯誤實作，在 `burstStart + L` 之後量到的 RMS 也是 -9.0 dB。若測試訊號是 1 kHz（週期 48 samples，整除 $L = 480$），`input[i + L] == input[i]`，連逐 sample 比對也分不出來。997 Hz 在 480 samples 內走 9.97 個週期，差了 0.03 週期，沒有延遲的實作逐 sample 誤差約 0.094；延遲差 1 sample（例如環形緩衝的 off-by-one）也會有約 0.065 的誤差。正確實作的頭部誤差是 1.24e-3：偵測器在 burst 的第 2 個 sample 才越過門檻，attack 晚了幾個 sample 才開始爬，所以輸出頭 2 個 sample 的 gain 還差 0.33 dB、0.17 dB 沒開完（第 3 個 sample 起才是 0 dB），而這兩個 sample 本身振幅很小（0 與 0.065）。所以門檻設 0.01（約為 peak 的 -34 dB）。另外再檢查 $[\text{burstStart}, \text{burstStart} + L)$ 這段輸出仍是靜音（transient 還在延遲線裡），沒有延遲的實作在這裡會量到 -21.9 dB。

**延伸思考**：

- **lookahead = attack 為什麼剛好**：transient 在 $t_0$ 到達偵測器，偵測器 0.1 ms 內超過門檻、開始 attack，gain 在 $t_0 + t_a$ 爬到 0 dB。主訊號延遲 $L = t_a$，transient 在 $t_0 + t_a$ 才到達乘法器——剛好碰上全開的門。$L < t_a$ 時頭仍會被削一部分；$L > t_a$ 時門會「提早」開，transient 前面會多一小段雜訊（通常聽不出來）。
- **為什麼用 997 Hz**：1 kHz 在 48 kHz 下的週期是 48 samples，$L = 480 = 10 \times 48$，延遲 $L$ 之後的波形和原本一模一樣，延遲多少都比不出差別。測試延遲時，訊號的週期不能整除待測的延遲量（或乾脆用白雜訊）。
- **為什麼要回報 latency**：這個 gate 的輸出比輸入晚 $L$ samples。DAW 會用各 plugin 回報的 latency 做 plugin delay compensation（PDC）：把其他軌延遲相同的量，讓所有軌在 mix bus 對齊。不回報的話，這軌會比其他軌晚 10 ms，和鼓組其他麥克風產生 comb filtering 或節奏拖拍。

---

## 練習 7：LR4 分頻的 split-band de-esser

解答：[`ex7_splitband_deesser.cpp`](ex7_splitband_deesser.cpp)（target `l08_ex7_solution`）。

**分頻器**：LP、HP 各兩級相同的 Butterworth（RBJ cookbook，$Q = 1/\sqrt2$）。**de-esser**：每個 channel 逐 sample 分頻寫進 `low_`、`high_`（在 `prepare()` 配置好的 `AudioBuffer`），把 `high_` 包成 `AudioBlock` 交給第 04 章 `Compressor::process()`，最後逐 sample 相加。偵測就用高頻段本身（compressor 的內部 detector），所以不需要另外的 bandpass。

**實測**（節錄）：

```
[1] LR4 @ 5 kHz：|LP + HP|、|LP|、|HP|（dB）
    頻率(Hz)   LP+HP      LP       HP
         100   -0.000    -0.00  -100.00
        1000    0.000    -0.01   -57.15
        3500    0.000    -1.75   -14.79
        5000    0.000    -6.02    -6.02
        7000   -0.000   -14.73    -1.76
       15000   -0.000   -51.57    -0.02
[2] de-esser：最大 GR 14.21 dB
    220 Hz 母音：純母音段 輸入 -18.08 / 輸出 -18.08；齒音期間 輸入 -18.08 / 輸出 -18.08 dB
    7 kHz 齒音：輸入 -42.31 / 輸出 -50.85 dB（壓了 8.54 dB）
[PASS] l08_ex7_splitband_deesser (15 checks passed)
```

- $|LP + HP| = 0.000$ dB 在整個頻帶成立，證實 README 2.5.3 節的推導：LR4 的 LP + HP 是 allpass。
- 分頻點 LP、HP 各 -6.02 dB（$20\log_{10}0.5$），兩者同相，相加為 0 dB。
- 7 kHz 只被壓 8.54 dB，而 compressor 的 GR 最大 14.21 dB：7 kHz 在 5 kHz 分頻點上方不遠，LP 段在 7 kHz 還有 -14.73 dB 的成分沒被壓到（範例 3 的內建 `DeEsser` 也有同樣現象）。

**延伸思考**：只用一級 2 階 Butterworth 時，$H_{LP} + H_{HP} = \frac{1 + s^2}{s^2 + \sqrt2 s + 1}$，在 $s = j$（分頻點）分子為 0——**完全抵消的 notch**（LP 相位 -90°、HP +90°，差 180°）。把 HP 反相：$H_{LP} - H_{HP} = \frac{1 - s^2}{s^2 + \sqrt2 s + 1}$，在 $s = j$ 為 $\frac{2}{j\sqrt2}$，大小 $\sqrt2$ → **+3 dB 的凸起**。兩者都不平坦，這就是 LR4（平方）的價值。

---

## 練習 8：Transient shaper 與 level independence

解答：[`ex8_transient_shaper.cpp`](ex8_transient_shaper.cpp)（target `l08_ex8_solution`）。

**映射**是 README 2.6.2 節的直接翻譯：

```cpp
static float mapToGainDb(float d, float attackDb, float sustainDb) noexcept {
    if (d > 0.0f)
        return attackDb * std::min(d / kFullScaleDb, 1.0f);   // 起音：d > 0
    return sustainDb * std::min(-d / kFullScaleDb, 1.0f);     // 延音：d ≤ 0
}
```

**`nextGain()`**：兩個 follower 吃同一個偵測值（各 channel 的 $\max|x|$）；slow 高於 −80 dBFS 才計算 $d = \text{gainToDb}(e_\text{fast}) - \text{gainToDb}(e_\text{slow})$ 並映射，否則目標 0 dB；最後 $g_\text{dB} \leftarrow t + \alpha(g_\text{dB} - t)$，$\alpha = e^{-1/(0.5\,\text{ms}\cdot f_s)}$。和 `dsp::TransientShaper::nextGain()` 一行一行對應，所以逐 sample 比對的誤差是 0。

**實測**：

```
[3] level independence（同一段鼓，peak -6 dBFS 與 -26 dBFS）
                         loud(-6)   quiet(-26)
    attack +6：peak 增量    +5.83      +5.83 dB
    sustain -12：尾音 RMS   -8.66      -8.66 dB
[4] 對照組：compressor（threshold -20、4:1）的 peak 變化
    loud  -5.13 dB、quiet  +0.00 dB → 差 5.13 dB：compressor 只壓得到大聲的那一份
[PASS] l08_ex8_transient_shaper (14 checks passed)
```

**為什麼兩個 level 的結果完全相同**：`EnvelopeFollower` 的 peak 偵測（Branching）是 $e[n] = |x[n]| + a\,(e[n-1] - |x[n]|)$，$a$ 用 attack 還是 release 係數只看 $|x[n]|$ 與 $e[n-1]$ 的**大小關係**；輸入整段乘上 $k > 0$ 時，大小關係不變、每一步都是線性運算，所以 $e_\text{fast}$、$e_\text{slow}$ 都變成 $k$ 倍（只差浮點捨入），比值 $e_\text{fast}/e_\text{slow}$ 不變，$d$ 與 gain 也就逐 sample 相同。peak 增量 +5.83 dB 而不是 +6 dB：鼓的 peak 出現在起音後很短的時間內，那時 $d$ 雖然已經超過 6 dB（飽和），但 gain 的 0.5 ms one-pole 平滑還沒完全追上 +6 dB。

**對照組**：compressor 的 gain computer 比較的是 level 與**固定的** threshold（−20 dB）。−6 dBFS 的鼓超過 threshold 14 dB，被壓了 5 dB；−26 dBFS 的鼓從頭到尾都在 threshold 以下，一點都沒被壓。這就是 README 2.6 節說的「compressor 看絕對音量、transient shaper 只看變化快慢」。

**延伸思考**：level independence 只在 $e_\text{slow} > -80$ dBFS 時成立。把鼓壓到 −70 dBFS 以下，尾音的 slow envelope 會先掉到 floor 以下而停止處理（目標 0 dB），sustain −12 的效果就會比大聲時弱；floor 是為了不讓底噪的隨機起伏被當成 transient 放大而付出的代價。
