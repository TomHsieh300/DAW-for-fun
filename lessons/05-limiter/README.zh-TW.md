# 第 05 章：Limiter——lookahead brickwall limiter

[English](README.md) | **繁體中文**

> [教材首頁](../../README.zh-TW.md)　｜　上一章：[04 Compressor](../04-compressor/README.zh-TW.md)　｜　下一章：[06 Saturation](../06-saturation/README.zh-TW.md)

第 04 章的 compressor 會「減少」動態，但它永遠慢半拍：訊號變大之後它才開始壓，第一個峰值總是溜出去。這一章要做的是能給出**硬性保證**的元件——不管餵什麼進來，輸出的 sample peak 永遠不超過你設的 ceiling。達成這件事的代價是延遲（lookahead），而延遲又牽扯出 DAW 的 plugin delay compensation、true peak 與 inter-sample peak 這些 mastering 工程師每天在面對的問題。

## 1. 本章目標

讀完並做完練習之後，你能夠：

- 用「ratio → ∞、attack → 0」解釋 limiter 與 compressor 的關係，並用數字說明為什麼 fast-attack compressor 仍然會 overshoot。
- 推導 lookahead limiter 的演算法（需要的 gain → sliding window minimum → 移動平均 → release），並**證明**它保證輸出 ≤ ceiling。
- 實作 `DelayLine`（circular buffer）與 `Limiter`，用 monotonic deque 做 O(1) amortized 的視窗最小值。
- 正確回報 latency，並說明 DAW 如何用 plugin delay compensation（PDC）補償它。
- 解釋 inter-sample peak、dBTP、為什麼 ceiling 通常設 -1 dBTP；用 4x oversampling 量 true peak。
- 分辨 clipper 與 limiter 的取捨、知道 LUFS 是什麼（第 11 章會實作），以及 limiter 在 mastering chain 中的位置。

## 2. 原理

### 2.1 Limiter = ratio → ∞、attack → 0 的 compressor

回想第 04 章的靜態曲線（hard knee）：

$$
y_{\text{dB}} = \begin{cases} x_{\text{dB}} & x_{\text{dB}} \le T \\ T + \dfrac{x_{\text{dB}} - T}{R} & x_{\text{dB}} > T \end{cases}
$$

$`R \to \infty`$ 時第二條變成 $`y_{\text{dB}} = T`$：threshold 以上的輸入不管多大，輸出都停在 $`T`$。這條水平線就是「ceiling」。以線性值寫，輸入 $`|x|`$ 超過 ceiling $`c`$ 時需要的 gain 是

$$
g = \min\!\Bigl(1,\ \frac{c}{|x|}\Bigr)
$$

```
 輸出 (dB)
   ▲
   │            compressor 4:1  ╱
 T ┤─────────────────────────╱─────────  limiter（ratio ∞）= ceiling
   │                       ╱╱
   │                     ╱╱
   │                   ╱╱
   │                 ╱╱
   │               ╱  ← 1:1
   └───────────────┴──────────────────▶ 輸入 (dB)
                   T
```

但靜態曲線只是一半。第 04 章的 attack / release 平滑器決定 gain「多快」到位，而 limiter 要求的是「在峰值抵達輸出之前」就到位——attack 不只要快，還要**是負的**。

### 2.2 為什麼 fast-attack compressor 仍然會 overshoot（用數字）

拿第 04 章的 `Compressor` 盡量模擬 limiter：threshold -20 dB、ratio 100:1、hard knee、attack 1 ms。輸入是 1 kHz 的 tone burst，-30 dBFS 持續 0.4 秒後突然跳到 -6 dBFS。

目標 gain reduction：輸入 -6 dB 時輸出 $`= -20 + 14/100 = -19.86`$ dB，所以 $`GR_\infty = 13.86`$ dB。Compressor 的 attack 是 one-pole 平滑器（在 dB 上平滑 gain reduction），gain reduction 依 $`GR(t) = GR_\infty\,(1 - e^{-t/\tau})`$ 上升，$`\tau = 1`$ ms。1 kHz 正弦波從 0 相位開始變大，第一個波峰在 $`t = 0.25`$ ms 才出現，之後每 0.5 ms 一個；每個 1 ms 視窗裡最大的輸出是「該視窗的第一個波峰」（$`t = k + 0.25`$ ms，此時 gain 降得最少）：

| 視窗 | 波峰時間 $`t`$ | $`1 - e^{-t/\tau}`$ | 理論 GR | 理論輸出 peak |
|---|---|---|---|---|
| 0–1 ms | 0.25 ms | 0.221 | 3.07 dB | **-9.07 dBFS** |
| 1–2 ms | 1.25 ms | 0.713 | 9.89 dB | -15.89 dBFS |
| 2–3 ms | 2.25 ms | 0.895 | 12.40 dB | -18.40 dBFS |
| 3–4 ms | 3.25 ms | 0.961 | 13.32 dB | -19.32 dBFS |
| 5–6 ms | 5.25 ms | 0.995 | 13.79 dB | -19.79 dBFS |

範例 `l05_compressor_vs_limiter` 的實測（每 1 ms 視窗內的輸出 peak）：

```
Time after jump    Input (dBFS)     Compressor        Limiter
   0 ms ~ 1              -6.00          -7.99         -20.00
   1 ms ~ 2              -6.00         -15.54         -20.00
   2 ms ~ 3              -6.00         -18.27         -20.00
   3 ms ~ 4              -6.00         -19.27         -20.00
   5 ms ~ 6              -6.00         -19.77         -20.00
   8 ms ~ 9              -6.00         -19.84         -20.00
  50 ms ~ 51             -6.00         -19.85         -20.00

Overshoot relative to threshold/ceiling within 20 ms after the jump: Compressor +12.01 dB, Limiter +0.00 dB
```

（節錄；程式還會印出 12、20 ms 兩列，以及整段輸出的 peak 與 safety clamp 觸發次數。）

Compressor 在第一毫秒就讓 **12 dB 的 overshoot** 通過（-7.99 dBFS 比 threshold 高 12.01 dB），之後幾毫秒才慢慢壓到 -19.85 dB 的穩態。實測比上表的理論值「壓得少一點」（例如 -7.99 對 -9.07），因為第 04 章用的是 peak detector：$`|x|`$ 只有在波峰附近才到 -6 dB，其餘時間 gain reduction 的目標比 $`GR_\infty`$ 小，attack 等於只有一部分時間在「往下推」。那把 attack 設成 0 呢？第 04 章的 compressor 是 feedforward、detector 與 gain 作用在同一個 sample，attack = 0 時就不會再有 overshoot，輸出只比 threshold 高 $`(x-T)/R`$（這裡 $`14/100 = 0.14`$ dB，ratio → ∞ 時才剛好等於 threshold）——但 gain 會在**一個 sample 之內**從 0 dB 跳到 -13.86 dB，等於在波形上剪一刀：不連續的 gain 跳變就是 click；release 也很短的話，gain 還會跟著每個波形週期上下，行為退化成 clipper（2.9 節），產生諧波失真。我們要的是「gain 平滑地下降」**而且**「峰值到的時候已經降好」，也就是 gain 必須在峰值抵達**之前**就開始動。這不是第 04 章的實作有問題，而是**因果律**：沒有延遲的系統在時間 $`n`$ 只看得到 $`x[0..n]`$，不可能為還沒看到的峰值提前準備。

### 2.3 Brickwall 的要求與 lookahead

「brickwall」的意思是：對**任何**輸入，輸出的每一個 sample 都滿足 $`|y[n]| \le c`$。要同時做到「平滑的 gain 曲線」（不然會有失真）與「絕對不超過」，gain 必須在峰值抵達輸出之前就降到 $`\le c/|x|`$。既然在時間 $`n`$ 看不到未來，那就**把音訊延後**：

```
                    ┌──────────────────────┐
  x[n] ──┬─────────►│  DelayLine（延遲 L）  ├────────► × ──► y[n] = x[n-L] · s[n]
         │          └──────────────────────┘          ▲
         │   ┌──────────┐  ┌──────────┐  ┌─────────┐  │
         └──►│ gain 計算 ├─►│ window   ├─►│ 平滑     ├──┘
             │ g = c/|x| │  │ min (L+1)│  │ avg (L+1)│   s[n]
             └──────────┘  └──────────┘  └─────────┘
```

音訊走的那條路晚了 $`L`$ 個 sample，gain 那條路沒有延遲——相對於音訊，gain「提前」了 $`L`$ 個 sample，有 $`L`$ 個 sample 的時間可以慢慢降下來。這就是 lookahead。$`L`$ 由 `setLookaheadMs()` 決定：$`L = \mathrm{round}(\text{ms}\cdot f_s / 1000)`$，5 ms @ 48 kHz 就是 240 個 sample。

### 2.4 演算法：每個 sample 六個步驟

令 $`x_s[n] = x[n]\cdot g_{\text{in}}`$（input gain 之後的訊號），$`c`$ 是 ceiling 的線性值，$`L`$ 是 lookahead 的 sample 數。stereo 時所有 channel 共用同一個 gain（stereo link），$`|x_s[n]|`$ 取各 channel 的最大值。

1. **需要的 gain**：$`g[n] = \min\!\bigl(1,\ c / |x_s[n]|\bigr)`$。只看這一個 sample，它需要多小的 gain 才不會超過 ceiling。
2. **release**：$`gr[n] = \min\!\bigl(g[n],\ gr[n-1] + (1 - gr[n-1])\,c_{\text{rel}}\bigr)`$，$`c_{\text{rel}} = 1 - e^{-1/(\tau f_s)}`$。gain 往下可以瞬間跟上（取 min），往上只能以 one-pole 的速度慢慢回到 1。
3. **視窗最小值**：$`m[n] = \min\bigl(gr[n-L],\ \dots,\ gr[n]\bigr)`$——長度 $`L+1`$ 的 sliding window minimum。
4. **移動平均**：$`s[n] = \dfrac{1}{L+1}\sum_{k=0}^{L} m[n-k]`$——長度 $`L+1`$ 的 box filter。
5. **輸出**：$`y[n] = x_s[n-L]\cdot s[n]`$。
6. **safety clamp**：$`y[n] \leftarrow \mathrm{clamp}(y[n], -c, c)`$。理論上永遠不會動作（下一節證明），它只是為了浮點捨入與「播放中改參數」這類例外，並且會計數讓你除錯。

為什麼是「min 再 average」，而不是直接 average？看時間軸。假設 $`gr`$ 在 $`n_0`$ 突然從 1 掉到 $`g`$：

```
 index:        n0-L        n0          n0+L        n0+2L
                 │          │           │            │
 gr[n]   1 1 1 1 1 1 1 1 1 1│g g g g g g g g g g g g g g g g      ← 瞬間下降
                            │
 m[n]    1 1 1 1 1 1 1 1 1 1│g g g g g g g g g g g g g g g g      ← min：和 gr 同時下降，
                            │                                        但之後會「撐住」L 個 sample
 s[n]    1 1 1 1 1 1 1 1 1 1 ╲                                    ← avg：從 n0 開始線性下降，
                               ╲                                      在 n0+L 剛好到 g
                                 ╲____ g g g g g g g g
                            │           │
 音訊          ...x[n0-L]...│...........│x[n0] 在 n0+L 才從 delay line 出來 ←┘ 這時 s 剛好 = g
```

- 移動平均把「瞬間下降」變成「長度 $`L+1`$ 的線性 ramp」：這就是 attack，而且 ramp 長度剛好等於 lookahead——峰值 $`x[n_0]`$ 在 $`n_0 + L`$ 抵達輸出時，$`s`$ 剛好降到 $`g`$，不早不晚。
- 如果沒有 min、直接對 $`gr`$ 做平均，當 $`gr`$ 只有一個 sample 很低（一個尖峰）時，平均會把它「稀釋」掉：$`s[n_0+L] = (L\cdot 1 + g)/(L+1) \gg g`$，峰值就超過 ceiling 了。min 讓那個很低的值在視窗內「撐」滿 $`L+1`$ 個 sample，平均起來才會真的到 $`g`$。
- 為什麼視窗是 $`L+1`$ 而不是 $`L`$？因為平均的 $`L+1`$ 項要**全部**都 $`\le g[n_0]`$，而這些項對應到 $`m[n_0], \dots, m[n_0+L]`$；$`m[n_0+L]`$ 的視窗是 $`[n_0, n_0+L]`$，剛好還包含 $`n_0`$。少一個就會差一個 sample（典型的 off-by-one，fuzz test 會抓到）。

### 2.5 證明：輸出永遠不超過 ceiling

固定任意時間 $`n`$。

1. 由第 2 步，對所有 $`k`$：$`gr[k] \le g[k]`$（min 的結果不會比其中一項大）。
2. 由第 3 步，$`m[n-k]`$ 是 $`\{gr[n-k-L], \dots, gr[n-k]\}`$ 的最小值。對 $`k = 0, 1, \dots, L`$，這個範圍都包含 index $`n-L`$（因為 $`n-k-L \le n-L \le n-k`$）。所以
   $$m[n-k] \le gr[n-L] \le g[n-L],\qquad k = 0,\dots,L.$$
3. 由第 4 步，$`s[n]`$ 是這 $`L+1`$ 個 $`m`$ 的平均，每一項都 $`\le g[n-L]`$，所以 $`s[n] \le g[n-L]`$。又每一項都在 $`[0, 1]`$，所以 $`0 \le s[n] \le 1`$。
4. 由第 5 步，$`|y[n]| = |x_s[n-L]|\cdot s[n] \le |x_s[n-L]|\cdot g[n-L]`$。
   - 若 $`|x_s[n-L]| > c`$：$`g[n-L] = c/|x_s[n-L]|`$，乘起來 $`= c`$。
   - 若 $`|x_s[n-L]| \le c`$：$`g[n-L] = 1`$，乘起來 $`= |x_s[n-L]| \le c`$。

兩種情況都得到 $`|y[n]| \le c`$。∎

注意證明**沒有用到** release 的時間常數、也沒有用到 input gain 怎麼變——所以這兩個參數可以在播放中隨意改（練習 6 的 fuzz test 第 (2) 段驗證了這件事）。證明用到的是：$`gr \le g`$、視窗長度 $`= L+1`$、平均長度 $`= L+1`$、音訊延遲 $`= L`$。這四個數字任何一個錯了，保證就破了。

至於浮點：$`c/|x| \cdot |x|`$ 在 float 下可能比 $`c`$ 大 1 個 ulp（unit in the last place：在這個數值附近，float 能表示的兩個相鄰數字的間距），移動平均用 running sum 也會累積捨入誤差。所以程式碼把平均 clamp 在 $`[0, 1]`$、最後再加一道 safety clamp；只有「明顯」超過（相對誤差 $`> 10^{-6}`$）才會被計數為異常。running sum 用 `double` 累積，誤差遠小於這個容差，正常使用下 safety clamp 的計數永遠是 0。

注意 clamp 的下限是 0 而不是 $`m[n]`$。直覺上「平均不會小於視窗最小值」，但這裡平均的是 $`m[n-L..n]`$ 這 $`L+1`$ 個**不同時間**的視窗最小值，不是同一個視窗裡的值。release 期間 $`gr`$ 一路往上，$`m[n] = gr[n-L]`$，而較舊的 $`m[n-L..n-1]`$ 都比它小，所以平均 $`< m[n]`$。若把下限設成 $`m[n]`$，算出來的就變成 $`\max(\text{平均}, m[n])`$：保證仍然成立（因為 $`m[n] \le gr[n-L] \le g[n-L]`$），但 release 那一側的 box 平滑等於被拿掉，release 很短時 gain 會在一個 sample 內跳回去（click）。

### 2.6 Release：gain 怎麼回來

峰值過去之後，$`g[n]`$ 回到 1，但 $`gr[n]`$ 只能以 one-pole 的速度回去：

$$
gr[n] = gr[n-1] + (1 - gr[n-1])\,c_{\text{rel}},\qquad c_{\text{rel}} = 1 - e^{-1/(\tau f_s)}
$$

$`\tau`$ 就是 `setReleaseMs()`：gain 在 $`\tau`$ 之後回復了「剩餘距離」的 63.2 %，$`2.3\tau`$ 之後 90 %。本章在**線性 gain** 上做 one-pole（第 04 章的 compressor 是在 dB 上做），好處是簡單、而且 $`gr \le g`$ 的不等式一眼就看得出來。

回升的 $`gr`$ 一樣會經過第 3、4 步的 min 與長度 $`L+1`$ 的移動平均，所以就算 release 設成 0（$`gr`$ 一個 sample 就跳回 1），$`s`$ 也是用 $`L+1`$ 個 sample 的線性 ramp 回到 1，不會瞬間跳回去。

聽感：
- release 太短（< 10 ms）：gain 會跟著低頻波形的每個週期上下——gain 以音訊速率調變，等於把訊號乘上一個週期性的波形 → 產生 intermodulation 失真，bass 會變「毛」。
- release 太長（> 500 ms）：一個大峰值之後整段都變小聲、慢慢回來——pumping（抽吸感）。
- 常見的折衷：50–200 ms；很多商用 limiter 用「auto release」（短 + 長兩段）或依 program 動態調整。

### 2.7 Latency 與 plugin delay compensation

`getLatencySamples()` 回傳 $`L`$。範例 `l05_latency_impulse` 用 impulse 驗證：

```
sampleRate    lookahead  getLatencySamples       impulse at     match?
     44100       5.0 ms                221              221        yes
     48000       5.0 ms                240              240        yes
     96000      10.0 ms                960              960        yes
```

（節錄；程式會印出 1.5 / 5 / 10 ms × 3 種 sample rate 共 9 列。44.1 kHz × 5 ms = 220.5，`std::lround` 進位成 221。）

同一支範例的 null test：把低於 ceiling 的訊號（不需要動作）送進 limiter，輸出往前移 240 個 sample 後與輸入相減，殘差 = 0——limiter 不動作時是**完全透明**的，只是晚了 $`L`$ 個 sample。

DAW 怎麼處理這個延遲？Plugin 透過 host API 回報 latency（JUCE 是 `AudioProcessor::setLatencySamples()`，VST3 是 `IAudioProcessor::getLatencySamples()`），host 做 **plugin delay compensation（PDC）**：

```
 vocal  ──► [limiter, 240] ─────────────────────┐
                                                 ├──► mix bus
 guitar ──► [沒有 plugin] ──► [PDC delay 240] ───┘
```

找出所有路徑的最大 latency，對其他路徑插入補償延遲，讓所有訊號抵達 mix bus 時對齊；代價是整個 mix 的輸出晚了 240 samples。第 14 章的 engine 會實作這套 PDC（[Step 8](../14-daw-8track/docs/step-08-pdc.zh-TW.md)），並用 `Engine::getLatencySamples()` 回報整個 mix 的 latency。

播放游標也要跟著修正。engine 的 transport position 是「正在**算**的那個 sample」，那個 sample 的聲音要先晚 PDC latency（這裡 240）才離開 engine，再晚音訊裝置的輸出 latency（driver 與 DA converter 的 buffer）才從喇叭出來。所以第 14 章 app 的 TransportBar（小節／時間計數器）與 ArrangementView（播放游標）顯示的是

$$
\text{顯示位置} = \max\bigl(0,\ \text{transport position} - \text{engine 的 PDC latency} - \text{裝置輸出 latency}\bigr)
$$

（播放中才扣；停止時沒有聲音，直接顯示下次開始播放的位置），畫面才會和耳朵聽到的對齊。實作在 `AudioHost::getAudiblePosition()`，說明見第 14 章的 [Step 13](../14-daw-8track/docs/step-13-transport-bar.zh-TW.md) 與 [Step 14](../14-daw-8track/docs/step-14-arrangement.zh-TW.md)。這也是為什麼**不建議在播放中改 lookahead**：latency 變了 host 得重算整個 session 的 PDC，通常會打斷播放。本章的 `setLookaheadMs()` 會重設平滑器狀態，請在停止播放時呼叫。

### 2.8 True peak、inter-sample peak 與 dBTP

Sample peak ≤ ceiling 只保證「這些取樣點」不超過。DAC 重建出來的連續波形在兩個 sample 之間可能更高——**inter-sample peak**。極端例子：頻率 $`f_s/4`$ 的正弦波，每個週期只有 4 個取樣點，如果相位偏 45°，每個 sample 都落在 $`\pm 0.707A`$：

```
  真正的波形           取樣點（都在 ±0.707A）
      ╭─╮                  •       •
     ╱   ╲                ╱ ╲     ╱ ╲
 ───╱─────╲─────╱──    ──•───•───•───•──
           ╲   ╱                ╲ ╱
            ╰─╯                  •
  峰值 = A           sample peak = 0.707A（低估 3.01 dB）
```

ITU-R BS.1770-4 定義的 **true peak** 是把訊號升頻 4 倍（48 kHz → 192 kHz，用 lowpass 插值）之後量 sample peak，單位 **dBTP**。

#### 升頻 4 倍怎麼做？（直覺版）

這一小節只給做練習 4 需要的直覺；完整理論（halfband filter、Kaiser window、latency、多級升頻）在[第 06 章 2.6 節](../06-saturation/README.zh-TW.md)，到那裡再推導一次。整件事分四步：

**(1) 零插入。** 在每兩個 sample 之間插 3 個 0，sample rate 變成 $`4f_s`$：

```
 原始（fs）        x[0]            x[1]            x[2]
 零插入（4fs）     x[0]  0  0  0   x[1]  0  0  0   x[2]  0  0  0
 想要的（4fs）     x[0]  ·  ·  ·   x[1]  ·  ·  ·   x[2]  ·  ·  ·     ← · = 連續波形在 1/4、1/2、3/4 處的值
```

我們要的是最後一列：原始 sample 不動，中間三格填上「DAC 重建出來的連續波形」在那些時間點的值。零插入只是先把格子空出來。

**(2) image。** 把「想要的」4x 訊號記成 $`v[m]`$。零插入的結果 $`u[m]`$ 其實就是 $`v[m]`$ 乘上一串「1, 0, 0, 0, 1, 0, 0, 0, …」的脈衝 $`p[m]`$（在 4 的倍數位置 $`v = x`$，其他位置被乘成 0）。這串脈衝可以寫成幾個餘弦的和：

$$
p[m] = \tfrac14\bigl[\,1 + 2\cos(\tfrac{\pi}{2}m) + \cos(\pi m)\,\bigr]
$$

（代入 $`m = 0, 1, 2, 3`$ 驗算：1、0、0、0。）在 $`4f_s`$ 的取樣率下，$`\cos(\tfrac{\pi}{2}m)`$ 是頻率 $`f_s`$ 的餘弦、$`\cos(\pi m)`$ 是 $`2f_s`$。所以

$$
u = v\cdot p = \tfrac14 v + \tfrac12 v\cos(2\pi f_s t) + \tfrac14 v\cos(2\pi\cdot 2f_s t)
$$

乘上一個餘弦等於把頻譜搬到那個頻率的兩側（和第 06 章「非線性產生新頻率」的三角恆等式是同一件事）。結果：$`u`$ 裡有一份振幅只剩 **1/4** 的 $`v`$（baseband），外加在 $`f_s`$、$`2f_s`$ 兩側的複本——**image**。例如 48 kHz 的 10 kHz 正弦，零插入之後在 192 kHz 下同時出現 10、38、58、86 kHz（$`48 \mp 10`$、$`96 - 10`$）。零插入那種「尖刺 + 一堆 0」的波形，就是這些高頻 image 疊出來的。

**(3) lowpass 插值。** 既然 $`u = \tfrac14 v + \text{image}`$，只要用一個 cutoff 在原本 $`f_s/2`$（新取樣率的 1/8）的 lowpass 把 image 濾掉、再乘 4，就得到 $`v`$。理想 lowpass 的 impulse response 是 sinc；把 cutoff 與增益 4 代進去，剛好是

$$
h[n] = \mathrm{sinc}\!\Bigl(\frac{n}{4}\Bigr) = \frac{\sin(\pi n/4)}{\pi n/4}
$$

它在 $`n = \pm 4, \pm 8, \dots`$ 時是 0：每個原始 sample 經過濾波後，在**其他原始 sample 的位置**貢獻 0、在中間的空格貢獻一點點——也就是「原始 sample 不動、只把 0 填起來」，這正是插值。理想 sinc 無限長，實作時只取 64 個 tap，並乘上 Hann window 讓截斷的邊緣變平滑（不然頻率響應會有漣波；window 的取捨在第 11 章 2.9 節）。

**(4) polyphase：只算非零項。** 直接在 $`4f_s`$ 下跑 64 tap FIR，每個輸出要 64 次乘法、每個原始 sample 有 4 個輸出，共 256 次——但 $`u`$ 有 3/4 是 0，絕大部分乘法都是「× 0」。輸出寫成 $`m = 4n + p`$（$`p = 0..3`$），只有 $`u[m-k]`$ 落在 4 的倍數的項會留下，也就是 $`k = 4j + p`$：

$$
y[4n+p] = \sum_j h[4j+p]\; x[n-j]
$$

| 輸出 | 用到的 tap | 乘上的輸入 |
|---|---|---|
| $`y[4n+0]`$ | $`h[0], h[4], h[8], \dots, h[60]`$ | $`x[n], x[n-1], \dots, x[n-15]`$ |
| $`y[4n+1]`$ | $`h[1], h[5], h[9], \dots, h[61]`$ | 同上 |
| $`y[4n+2]`$ | $`h[2], h[6], h[10], \dots, h[62]`$ | 同上 |
| $`y[4n+3]`$ | $`h[3], h[7], h[11], \dots, h[63]`$ | 同上 |

64 tap 的原型被拆成 4 個 16 tap 的子濾波器（**phase**），4 個 phase 吃同一段最近 16 個原始輸入。每個原始 sample 只要 $`4 \times 16 = 64`$ 次乘法，而且完全不必真的做出 4 倍長的零插入 buffer。每個 phase 的係數總和約為 1（輸入是常數時，4 個插值點都要等於那個常數），所以整個原型的總和是 4——這就是 (2) 的「乘 4」。

這就是練習 4 要實作的全部。第 06 章會用另一種設計（每次只升 2 倍的 halfband filter，串兩級得到 4 倍）做出 `Oversampler`；第 11 章練習 3 會直接重用它來做 true-peak meter，並和練習 4 這一版比較讀數誤差與 CPU 成本。

練習 4 的實測：

| 訊號 | sample peak | true peak |
|---|---|---|
| fs/4 正弦波、相位 45°（真正峰值 -6.02） | -9.03 dBFS | -6.18 dBTP |
| drum loop +12 dB、hard clip 到 0 dBFS | 0.00 dBFS | +4.54 dBTP |
| drum loop +12 dB → 本章 Limiter（ceiling -1 dBFS） | -1.00 dBFS | +1.02 dBTP |

第一列量到 -6.18 而不是 -6.02：64 tap 的插值濾波器是偶數長度，4 個插值點落在兩個原始 sample 之間的 1/8、3/8、5/8、7/8 處，而這個訊號的峰值剛好在正中間（1/2），最近的插值點差 1/8 sample，最多低估 0.17 dB。4x oversampling 的 true-peak meter 本來就只是「很接近」真正的峰值，不是精確值（練習 4 解答有完整說明）。

為什麼要在意？因為訊號離開你的 DAW 之後還會被重建：DAC 的 reconstruction filter（DAC 把離散 sample 還原成連續波形時用的 lowpass）、sample rate conversion、MP3 / AAC 編碼都會把那些 inter-sample peak「還原」出來，接著在只有 0 dBFS headroom 的整數路徑上 clip。所以 EBU R128 建議節目的 true peak 不超過 **-1 dBTP**，串流平台多半也要求 -1 dBTP（有些建議 -2 dBTP 給 lossy codec 留餘地）。實務上：

- 用 sample-peak limiter（本章）時把 ceiling 再往下留 1–2 dB；
- 或者做 **true-peak limiter**：sidechain 裡先升頻 4 倍再算 $`g`$（練習 4 的濾波器放進第 1 步就是了）；
- 不管用哪種，最後用 true-peak meter 確認。

### 2.9 Clipper vs limiter

Clipper 是最簡單的「保證不超過」：$`y = \mathrm{clamp}(x, -c, c)`$（hard）或一條平滑曲線（soft）。它零延遲、無狀態，但它改的是**波形**而不是增益——正弦波的頂被削平 → 產生諧波。練習 5 的實測（1 kHz +6 dBFS）：soft clipper THD 20.9 %（H3 -13.8 dB、H5 -27.6 dB，奇對稱曲線只有奇次諧波）；limiter THD 0.0000 %——穩態下整段乘同一個 gain，波形不變。

| | Clipper | Limiter |
|---|---|---|
| 延遲 | 0 | lookahead（1–10 ms） |
| 改變的東西 | 波形 | 增益 |
| 副作用 | 諧波失真（持續音很難聽） | pumping、transient 變鈍 |
| 適合 | 削極短的尖峰、要「破」的音色、硬體 / live | mastering、broadcast、任何需要乾淨的場合 |

很多 mastering 工程師兩個都用：limiter 前放一個 soft clipper 削 1–2 dB 的鼓尖峰（短到聽不出失真），limiter 就不用做那麼多 gain reduction。

### 2.10 Loudness（LUFS）簡介

Peak 只告訴你「最大的那一個 sample」，跟人耳感受的「大聲」關係不大。EBU R128 / ITU-R BS.1770 用 **LUFS**（Loudness Units relative to Full Scale）量 loudness：訊號先經過 K-weighting（一個高頻 shelf 加一個 highpass，模擬人耳），取 400 ms 視窗的均方值，$`L_K = -0.691 + 10\log_{10}\sum_i G_i\,z_i`$，再做 gating（-70 LUFS 絕對門檻、相對門檻 -10 LU）得到 integrated loudness。廣播的目標是 -23 LUFS，串流平台多半把節目正規化到 -14 LUFS 左右——這表示「把 limiter 推到爆」在串流上只會被轉小聲、白白損失動態。第 11 章會實作 `LoudnessMeter`。

本章範例 `l05_drum_limiting` 用 RMS 當作 loudness 的粗略代理：input gain +12 dB → ceiling -1 dBFS，peak 被釘在 ceiling（只從 dry 的 -2.92 升到 -1.00 dBFS，多 1.92 dB），RMS 卻增加 7.2 dB，crest factor 從 14.51 dB 縮小到 9.25 dB（少了約 5.3 dB）。這就是 limiter「變大聲」的原理：**降低 crest factor**（peak 與 RMS 的距離）。

### 2.11 Mastering chain 中的位置

```
 mix ──► EQ ──► compressor（glue）──► saturation / stereo ──► [soft clip] ──► LIMITER ──► dither ──► 輸出
```

Limiter 幾乎永遠是最後一個（dither 除外；dither 是降低位元深度、例如轉成 16-bit 之前加入的極小雜訊，讓量化誤差變成平滑的底噪而不是跟著訊號走的失真，它的振幅只有 1 LSB 左右，對 peak 幾乎沒有影響），因為它的工作是「對最終輸出的 peak 做保證」；後面再放任何會改變增益的東西，保證就失效了。它的 input gain（或 threshold）決定最終 loudness，ceiling 設 -1 dBTP，gain reduction 表最好只在最大的 transient 上偶爾跳 2–4 dB——持續壓 6 dB 以上，鼓的 punch 就沒了。

## 3. 從公式到程式碼

本章新增兩個 header：`dsp/include/dawlab/dsp/DelayLine.h` 與 `dsp/include/dawlab/dsp/Limiter.h`（後者也包含 `SlidingWindowMin`）。

### 3.1 `DelayLine.h`：circular buffer

延遲 $`D`$ 個 sample 就是 $`y[n] = x[n-D]`$，需要記住最近 $`D`$ 個輸入。Circular buffer 讓 push 與 read 都是 O(1)、不搬資料：

```cpp
void push(int channel, float x) noexcept {
    int& w = writeIndex_[channel];
    channelData(channel)[w] = x;
    if (++w >= size_) w = 0;            // 到尾端就繞回開頭
}

float read(int channel, int delaySamples) const noexcept {
    const int d = std::clamp(delaySamples, 0, maxDelay_);
    int idx = writeIndex_[channel] - 1 - d;
    if (idx < 0) idx += size_;          // 等價於 mod size，但避免負數取餘數的陷阱
    return channelData(channel)[idx];
}
```

`prepare(maxDelaySamples, numChannels)` 配置 `maxDelay + 1` 格（要能同時存「現在」與「maxDelay 之前」）。`read(ch, 0)` 是剛 push 的那個，`read(ch, L)` 是 L 次 push 之前的。每個 channel 有自己的 write index。第 09 章的 `FractionalDelayLine` 會沿用同樣的設計，加上小數 delay 的插值。

### 3.2 `SlidingWindowMin`：monotonic deque

第 3 步要每個 sample 都算「最近 $`L+1`$ 個值的最小值」。暴力法每個 sample 掃一遍是 O(L)（10 ms @ 96 kHz = 961 次比較 / sample）。Monotonic deque 的想法：deque 裡只保留「還有機會成為最小值」的 (value, index)，由舊到新 value 嚴格遞增。

```cpp
float push(float v) noexcept {
    const int64_t n = sampleIndex_++;
    // (a) 尾端所有 >= v 的元素丟掉：它們比 v 早過期、又不比 v 小，永遠不可能再當最小值
    while (count_ > 0 && values_[back()] >= v) --count_;
    // (b) 前端若已離開視窗 [n - window + 1, n] 就丟掉
    while (count_ > 0 && indices_[head_] <= n - window_) { head_ = (head_ + 1) % capacity_; --count_; }
    // (c) 把 v 放到尾端（此時元素數 <= window - 1，一定放得下）
    const int slot = (head_ + count_) % capacity_;
    values_[slot] = v; indices_[slot] = n; ++count_;
    return values_[head_];              // 前端就是最小值
}
```

每個元素最多 push 一次、pop 一次 → N 個 sample 總共 O(N)，平均每個 sample O(1)。deque 用預先配置的環形陣列，`push()` 不 allocate。練習 3 會讓你自己寫一遍並與暴力法比對、計時：deque 版快數十倍，而且視窗越長差距越大——暴力法的時間跟著 L 變長，deque 版幾乎不變，這就是 O(L) 對 O(1) 的差別（Debug 建置的實測：視窗 241 約 20–30 倍、961 約 70–100 倍；確切倍率依機器與負載而異，Release 下差距更大）。

### 3.3 `Limiter.h`：六個步驟

```cpp
void process(AudioBlock block) noexcept {
    for (int i = 0; i < numSamples; ++i) {
        // (1) input gain（線性 ramp）→ 推進 delay line → 取所有 channel 的 |peak|（stereo link）
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) {
            const float xs = block.getSample(ch, i) * currentInputGain_;
            delay_.push(ch, xs);
            peak = std::max(peak, std::abs(xs));
        }
        // (2) 需要的 gain：g = min(1, ceiling / peak)
        const float g = (peak > ceiling) ? ceiling / peak : 1.0f;
        // (3) release：往上只能以 one-pole 的速度回到 1；往下瞬間跟上
        float released = releasedGain_ + (1.0f - releasedGain_) * cRel;
        releasedGain_ = std::min(g, released);
        // (4) lookahead 視窗（長度 L+1）內的最小值
        const float m = windowMin_.push(releasedGain_);
        // (5) 長度 L+1 的移動平均（running sum）→ 線性 attack ramp
        averageSum_ += m - oldest; oldest = m; ...
        const float s = std::clamp(float(averageSum_ * invLen), 0.0f, 1.0f);
        // (6) 輸出 = 延遲後的音訊 × 平滑後的 gain；safety clamp 兜底
        for (int ch = 0; ch < numChannels; ++ch) {
            float y = delay_.read(ch, L) * s;
            if (!(std::abs(y) <= ceiling)) { ++clips（若明顯超過）; y = clamp(y, -ceiling, ceiling); }
            block.setSample(ch, i, y);
        }
    }
    gainReductionDb_.store(-gainToDb(minSmoothedGain));   // 給 UI 的 GR meter
}
```

幾個設計細節：

- **移動平均用 running sum**：每個 sample 只做一加一減，O(1)。sum 用 `double` 累積，再 clamp 到 $`[0, 1]`$ 擋掉捨入漂移（下限不能用 $`m`$，理由見 2.5 節最後一段）。
- **channel 數**：`process()` 收到的 block 的 channel 數不可超過 `prepare()` 時的 `numChannels`。多出來的 channel 沒有 delay line、也沒參與 peak 偵測，Debug 建置會 assert；Release 建置把它們清成 0（寧可靜音，也不讓沒被限制的訊號通過）。沒呼叫 `prepare()` 就 `process()` 也一樣：Debug 會 assert，Release 輸出全 0。
- **safety clamp 用 `!(|y| <= ceiling)`** 而不是 `|y| > ceiling`：NaN 的任何比較都是 false，這樣寫 NaN 也會被攔住換成 0。
- **input gain 用約 20 ms 的線性 ramp**（`setInputGainDb()`），避免轉旋鈕時的 zipper noise；`reset()` 會讓它直接就位。
- **lookahead 的上限在 `prepare()` 決定**：`setMaxLookaheadMs()`（預設 20 ms）要在 `prepare()` 之前呼叫，`prepare()` 依它配置 delay line、deque 與平均環。`setLookaheadMs()` 只記住你要求的值，生效的值是 $`\min(\text{要求}, \text{上限})`$，所以兩個 setter 的呼叫順序不影響結果；`getLookaheadMs()` 回傳生效的值，prepare 之後永遠與 `getLatencySamples()` 一致。
- **`getGainReductionDb()`** 回傳上一個 block 內最大的 gain reduction（正值 dB），`std::atomic<float>` relaxed，UI thread 可以直接讀。
- **`getSafetyClipCount()`** 是除錯用的計數器：正常使用下永遠是 0；只有播放中把 ceiling 調低、或輸入含 NaN / Inf 才會增加。

API（依教材合約）：

| Method | 說明 |
|---|---|
| `setMaxLookaheadMs(float)` | lookahead 上限，`prepare()` 前呼叫（下一次 prepare 生效），預設 20 ms |
| `prepare(double sr, int maxBlock, int numCh)` | 配置記憶體（唯一會 allocate 的地方）；`process()` 前必須呼叫 |
| `reset()` | 清空 delay line、視窗、平均、release 狀態 |
| `setCeilingDb(float)` | 預設 -1 dB |
| `setInputGainDb(float)` | 預設 0 dB，20 ms ramp；值沒變時不動作（可每個 block 重複呼叫） |
| `setReleaseMs(float)` | 預設 100 ms |
| `setLookaheadMs(float)` | 預設 5 ms；生效值夾在 [0, max]（與呼叫順序無關）；會重設平滑狀態 |
| `float getLookaheadMs() const` | 生效的 lookahead（ms） |
| `int getLatencySamples() const` | = lookahead 的 sample 數 |
| `float getGainReductionDb() const` | 正值、thread-safe |
| `uint64_t getSafetyClipCount() const` | 除錯用 |
| `process(AudioBlock)` | in-place，stereo-linked；block 的 channel 數不可超過 `prepare()` 時的 `numCh` |

### 3.4 範例

建置與執行（在專案根目錄；ctest 會自動跑所有範例與解答）：

```powershell
.\scripts\build.ps1 -Only 05-limiter
cd build
.\bin\Debug\l05_compressor_vs_limiter.exe
```

WAV 都輸出到 `build\dawlab_output\`。

| Target | 示範什麼 | 輸出 WAV |
|---|---|---|
| `l05_compressor_vs_limiter` | 2.2 節的 overshoot 對照：同一段 tone burst 過 Compressor（100:1、attack 1 ms）與 Limiter（lookahead 2 ms），印出變大後每 1 ms 的 peak | `l05_cmp_vs_lim_input.wav`、`l05_cmp_vs_lim_compressor.wav`、`l05_cmp_vs_lim_limiter.wav` |
| `l05_drum_limiting` | drum loop 推 +6 / +12 dB 後 limit（ceiling -1 dBFS），印出 peak、RMS、crest factor、max GR；對照 hard clip。可選 `argv[1]` 給自己的 WAV | `l05_drums_dry.wav`、`l05_drums_limited_6dB.wav`、`l05_drums_limited_12dB.wav`、`l05_drums_clipped_12dB.wav` |
| `l05_latency_impulse` | impulse 在 44.1 / 48 / 96 kHz 下的延遲 = `getLatencySamples()`；null test；gain 提前下降的表格 | `l05_latency_impulse.wav`、`l05_latency_burst.wav`（左 = 輸入、右 = 輸出，拉進 DAW 看 ramp） |
| `l05_ceiling_stress` | 2.5 節保證的壓力測試：+40 dB 雜訊、±10（+20 dBFS）的 50 Hz 方波、DC、±1000 脈衝、±10⁴ 交替……配隨機 block 長度與隨機參數，檢查輸出 peak 與 safety clamp 次數（`clamp#` 欄必須是 0）；最後驗證 block 512 / 1 / 隨機 block 的輸出逐 sample 相同 | 無（純數字） |

`l05_drum_limiting` 的實測：

```
input gain      in peak     out peak      out RMS     RMS gain       max GR
     +0 dB      -2.92 dB      -2.92 dB     -17.43 dB      +0.00 dB       0.00 dB
     +6 dB       3.08 dB      -1.00 dB     -13.38 dB      +4.05 dB       4.08 dB
    +12 dB       9.08 dB      -1.00 dB     -10.25 dB      +7.18 dB      10.08 dB
+12 dB clip       9.08 dB       0.00 dB      -7.55 dB      +9.88 dB          -
```

該聽到：`limited_6dB` 比 `dry` 明顯大聲、幾乎聽不出壓縮；`limited_12dB` 更大聲，但 kick 的「衝擊感」變鈍、hi-hat 跟著 kick 一起被壓下去（pumping）；`clipped_12dB` 的 RMS 最高，但 kick / snare 有明顯的沙沙破音。max GR 10.08 dB = 9.08 − (−1)，不多不少——lookahead 讓 gain 剛好到位（程式會檢查 max GR = max(0, 輸入 peak − ceiling)，誤差超過 0.05 dB 就失敗）。

`l05_latency_impulse` 第 (3) 段（tone burst -30 → 0 dBFS、ceiling -6 dB、L = 240）：

```
 Output pos. rel. to step+L    gain (dB)  output (dBFS)
           -240 samples        -0.12         -30.12      ← gain 從這裡開始下降（比峰值早 L 個 sample）
           -120 samples        -2.63         -32.63      ← 線性 ramp 走到一半（線性 gain 0.75）
             +0 samples        -6.00          -6.00      ← 峰值抵達輸出，gain 剛好 = ceiling / 峰值
           +240 samples        -6.00          -6.00
```

（節錄；程式還會印出 -480、-180、-60 等位置。）線性 gain 在 241 個 sample 內從 1 線性降到 0.5：理論上 -240 處是 $`(240 + 0.5)/241 = 0.998`$（-0.02 dB），-120 處是 0.75（-2.50 dB）。表中的數字略低（-0.12、-2.63 dB），是因為範例用「0.5 ms（24 個 sample）視窗內的輸出 peak ÷ 輸入 peak」來量 gain，而 gain 在這 24 個 sample 內還在繼續往下掉。

## 4. Real-time 注意事項

- **只有 `prepare()` 會 allocate**：delay line、deque、平均環的大小都由 `setMaxLookaheadMs()` 與 sample rate 決定。`process()` 內沒有 allocate、沒有 lock、沒有 I/O。
- **`setLookaheadMs()` 不是 real-time safe 的「平滑」操作**：它會重設視窗與平均（否則長度不一致會破壞保證），而且改變 latency 需要通知 host 重算 PDC。在停止播放時改。
- **`setCeilingDb()` 在播放中往下調**會讓 pipeline 內已算好 gain 的 L 個 sample 超過新 ceiling，由 safety clamp 兜底（會有 clip）。要做「可以在播放中轉的 ceiling 旋鈕」，應該把 ceiling 用 `SmoothedValue` 平滑、或在 ramp 期間同時降低 input gain。
- **參數平滑**：input gain 內建 20 ms 線性 ramp；release 係數改變不需要平滑（只影響回復速度）。
- **Setter 要能被「每個 block 重複呼叫」**：plugin wrapper（第 13 章）通常在每個 `processBlock()` 開頭把所有參數再設一次。`setInputGainDb()` 若每次都依「剩下的距離」重算 ramp step，ramp 會越走越慢、永遠到不了目標；所以本章的 `setInputGainDb()` / `setReleaseMs()` 在值沒變時直接 return（練習 6 解答第 (5) 段驗證：每個 block 都設 +6 dB，25 ms 後輸出剛好是目標值）。
- **Denormal**：release 的 one-pole 是往 1 收斂、不是往 0，所以不會產生 denormal；程式還是在 `released > 1 - 10^{-6}` 時直接設成 1，避免永遠差一點點。
- **Thread safety**：`getGainReductionDb()` / `getSafetyClipCount()` 是 relaxed atomic，UI thread 隨時可讀。setter 與 `process()` 要在同一個 thread、或由 host 保證在 block 之間呼叫（跟第 04 章一樣）。
- **Block 大小無關**：所有狀態都是逐 sample 維護的，block 長度 1 或 1024、每次都不一樣，結果完全相同（`l05_ceiling_stress` 最後一段把同一段 drum loop 用 block 512、block 1、隨機 block 各跑一次，輸出最大差異 = 0；fuzz test 也全程用隨機 block 長度）。
- **Stereo link**：所有 channel 共用一個 gain。若左右各自 limit，當只有一邊觸發時立體聲像會瞬間往另一邊偏。

## 5. 常見錯誤與除錯

| 症狀 | 可能原因 |
|---|---|
| 輸出 peak 還是超過 ceiling（`getSafetyClipCount()` > 0） | 音訊沒有延遲、或延遲 ≠ L；視窗 / 平均長度用了 L 而不是 L + 1；平均沒有先做 min；播放中改了 lookahead 或 ceiling |
| 「喀」一聲的 click | 平均長度太短（gain 瞬間跳）；`setLookaheadMs()` 在播放中呼叫；input gain 沒有 ramp |
| Bass 變毛、有 intermodulation | release 太短，gain 跟著波形週期抖動。拉長 release，或加「hold」（gain 降下去之後先維持一段時間、例如一個低頻週期以上，才開始 release） |
| 鼓的 punch 不見了、整首歌隨 kick 一起呼吸 | gain reduction 太多（input gain 推太猛）或 release 太長。mastering 上通常只讓 GR 在 transient 上跳 2–4 dB |
| 兩軌混起來有 comb filtering / 相位感 | latency 沒回報或回報錯（host 沒做 PDC）。用 `l05_latency_impulse` 的方法量：impulse 出來的位置就是 latency |
| 用 true-peak meter 看超過 -1 dBTP | 這是 sample-peak limiter 的正常現象（2.8 節）。ceiling 再往下留 1–2 dB，或做 true-peak 偵測 |
| 左右聲像在大聲時往一邊偏 | 沒有 stereo link |
| 輸出全 0 | `prepare()` 沒呼叫，或 block 的 channel 數比 `prepare()` 時多（Debug 建置會在 assert 停下；Release 建置把沒有 delay line 的 channel 清成 0，沒 prepare 時就是整個 block） |
| 某些 sample 變成 0 | 輸入含 NaN / Inf：本章的 safety clamp 會把乘出來的 NaN 換成 0（`getSafetyClipCount()` 會增加），但上游該修 |
| GR meter 顯示 -0.00 | `-gainToDb(1)` 是 -0.0；本章用 `std::max(0.0f, …)` 處理 |

除錯技巧：把 `l05_latency_burst.wav` 拉進 DAW，左聲道是輸入、右聲道是輸出，放大看峰值前 L 個 sample 的 gain ramp；用 `l05_ceiling_stress`（看 `clamp#` 欄）和練習 6 的 fuzz test 當 regression test——任何改動之後跑一次。注意只看「輸出 peak ≤ ceiling」是不夠的：最後那道 safety clamp 不管演算法對不對都會把輸出夾回 ceiling，所以本章所有範例與練習 6 都同時要求 `getSafetyClipCount() == 0`，這樣視窗長度、平均長度或延遲差一個 sample 的 bug 才抓得到。

## 6. 練習題

見 [`exercises/README.zh-TW.md`](exercises/README.zh-TW.md)（7 題：觀念 2、計算 1、實作 4），解答在 [`solutions/README.zh-TW.md`](solutions/README.zh-TW.md)。

## 7. 延伸閱讀

- D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design—A Tutorial and Analysis", *Journal of the Audio Engineering Society*, 60(6), 2012。第 04、05 章的 attack / release 結構都來自這篇；其中也討論 lookahead。
- ITU-R BS.1770-4, "Algorithms to measure audio programme loudness and true-peak audio level"（Annex 2 是 4x oversampling true-peak meter 的規格）。
- EBU R 128, "Loudness normalisation and permitted maximum level of audio signals"；EBU Tech 3341（loudness metering 的 EBU mode）。
- U. Zölzer (ed.), *DAFX: Digital Audio Effects*, 2nd ed., Wiley, 2011——第 4 章 dynamics processing（含 limiter）。
- J. D. Reiss, A. McPherson, *Audio Effects: Theory, Implementation and Application*, CRC Press, 2014——第 6 章 dynamics processing。
- W. Pirkle, *Designing Audio Effect Plugins in C++*, 2nd ed., Routledge, 2019——dynamics 處理器與 plugin latency 回報。
- JUCE 說明文件：`AudioProcessor::setLatencySamples()` / `getLatencySamples()`（plugin 如何向 host 回報 latency）。

## 8. 下一章

[06 Saturation](../06-saturation/README.zh-TW.md)：這一章我們刻意不改變波形；下一章反過來，用 waveshaper 刻意產生諧波——並用 oversampling 解決隨之而來的 aliasing。
