# 第 05 章 練習題：Limiter

難度：★☆☆ 基礎　★★☆ 中等　★★★ 進階

實作題（3、4、5、6）都有起始碼 `starter/exN_xxx.cpp`，可以直接編譯；執行時 self-check 會印出 FAIL，那是預期的——把 `// TODO` 補完、讓所有 check 變成 PASS 就完成了。解答在 [`../solutions/README.md`](../solutions/README.md) 與 `../solutions/exN_xxx.cpp`。

建置與執行（在專案根目錄）：

```powershell
.\scripts\build.ps1 -Only 05-limiter -NoTest
cd build
.\bin\Debug\l05_ex3_starter.exe      # 或 l05_ex4_starter、l05_ex5_starter、l05_ex6_starter
```

---

## 練習 1 ★☆☆（觀念）Limiter 與 Compressor、Clipper 的差別

1. 用「ratio」與「attack」兩個參數說明：limiter 是 compressor 的哪種極端情況？為什麼一台 ratio 100:1、attack 1 ms 的 compressor 仍然不能稱為 brickwall limiter？（提示：範例 `l05_compressor_vs_limiter` 的表格——變大後的第 1 毫秒輸出是多少？）
2. 「brickwall」這個保證（輸出 sample peak 永遠 ≤ ceiling）在數學上需要什麼條件？為什麼「零延遲」的 limiter 做不到這件事（除非它變成 clipper）？
3. Clipper 與 limiter 都能保證輸出不超過上限，它們「付出的代價」分別是什麼？什麼情況下你會選 clipper？

## 練習 2 ★☆☆（計算）Lookahead、latency 與 gain reduction

1. lookahead 1.5 ms、5 ms、10 ms 在 44.1 kHz、48 kHz、96 kHz 下各是幾個 sample？（用 `round(ms × fs / 1000)`，注意 44.1 kHz × 5 ms 是 220.5 要怎麼取整。）
2. 一條 master chain 上串了兩個 lookahead 5 ms 的 limiter（48 kHz）。host 從兩個 plugin 的 `getLatencySamples()` 各讀到多少？整條 chain 的總延遲是幾 ms？
3. 本章的平滑用「長度 L + 1 的移動平均」。L = 240 時平均幾個值？對一個突然下降的 gain，這個移動平均產生的線性 ramp 持續幾個 sample、幾毫秒？
4. 範例 `l05_drum_limiting` 的 drum loop 原始 peak 是 -2.92 dBFS。推 +12 dB 的 input gain 後，限制在 ceiling -1 dBFS 需要的最大 gain reduction 是多少 dB？（對照範例輸出的 `max GR` 欄位。）
5. ceiling 設 -1 dBTP，線性值是多少？一個振幅 A 的正弦波、頻率剛好是 fs/4、取樣相位偏 45°，它的 sample peak 比真正的峰值低幾 dB？

## 練習 3 ★★☆（實作）O(1) amortized 的 sliding window minimum

起始碼：`starter/ex3_sliding_window_min.cpp`

Limiter 第 4 步要在 lookahead 視窗（L + 1 個值）內取最小值。暴力法每個 sample 掃一遍視窗是 O(L)——lookahead 10 ms @ 96 kHz 就是每個 sample 961 次比較。請用 **monotonic deque** 完成 `MonotonicWindowMin::push()`，讓它：

- 和暴力法 `BruteForceWindowMin` 在所有輸入上回傳完全相同的值（包含大量重複值的情況——想清楚 `>=` 還是 `>`）；
- 每個元素最多進出 deque 一次 → N 個 sample 總共 O(N)；
- deque 用預先配置的環形陣列實作，`push()` 內不 allocate。

起始碼會比對兩種做法在 9 種視窗長度下的結果，並印出耗時與加速倍率。

提示：deque 內只保留「還有機會成為最小值」的元素——由舊到新 index 遞增、value 嚴格遞增。新值 v 進來時，尾端所有 ≥ v 的元素都可以丟掉（為什麼？）。

## 練習 4 ★★★（實作）簡易 4x oversampling true-peak meter

起始碼：`starter/ex4_true_peak_meter.cpp`

ITU-R BS.1770 量 true peak 的做法是把訊號升頻 4 倍再量 sample peak。windowed-sinc 與 polyphase 的完整理論要到第 06 章 2.6 節才教，所以動手之前先讀 README 2.8 節的「升頻 4 倍怎麼做？（直覺版）」：零插入 → image → lowpass 插值 → polyphase 只算非零項。讀完應該能回答：為什麼要乘 4？為什麼 lowpass 是 $\operatorname{sinc}(n/4)$？為什麼 phase p 用的是 `h[4j + p]`？請完成 `TruePeakMeter`：

1. `designFilter()`：設計 64 個 tap 的 windowed-sinc lowpass（cutoff = 原本的 fs/2、Hann window、總和正規化成 4），並做 polyphase 分解：phase p 的第 j 個 tap 是 `h[4j + p]`。
2. `process()`：每個輸入 sample 只算 4 個內積 `y[4n + p] = Σ_j h[4j + p] · x[n − j]`，更新 `truePeak_`。

起始碼的 TODO 註解把每一步「為什麼這樣做」拆開說明。建議先只做第 1 步：self-check (0) 不跑訊號、只檢查係數（總和 = 4、每個 phase 的總和 ≈ 1、左右對稱），它 PASS 了再做第 2 步。

Self-check (1)～(4) 會驗證：1 kHz 正弦波的 true peak ≈ sample peak；fs/4、相位 45° 的正弦波 sample peak 低估 3.01 dB，true peak 則量到 -6.19 dBTP 附近（比真正的 -6.02 低約 0.17 dB——為什麼？提示：偶數長度濾波器的插值點落在哪裡）；hard clip 的 drum loop true peak 高於 sample peak（約 +4.5 dB）；本章 Limiter 輸出的 sample peak ≤ -1 dBFS，但 true peak 高於 sample peak、也明顯高於 -1 dBTP（約高 2 dB）。

思考：量出來的差距告訴你，想保證 -1 dBTP 時 sample-peak limiter 的 ceiling 該怎麼設？

（第 11 章練習 3 會改用第 06 章的 `Oversampler` 實作 true-peak meter，並拿這一題的版本當對照組，比較兩者的讀數誤差與 CPU 成本。）

## 練習 5 ★★☆（實作）Soft clipper，並與 limiter 比較

起始碼：`starter/ex5_soft_clipper.cpp`

完成 `softClip(x, t)`：

$$
y = \begin{cases} x & |x| \le t \\ \operatorname{sign}(x)\,\bigl[\,t + (1-t)\tanh\!\frac{|x|-t}{1-t}\,\bigr] & |x| > t \end{cases}
$$

要求：線性區完全不動、在 $t$ 處連續且斜率連續（C¹）、$|y| \le 1$、奇對稱、單調遞增。起始碼會用 1 kHz +6 dBFS 的正弦波比較 clipper 與 limiter 的 THD 與諧波（H2–H5），再用 drum loop +12 dB 比較 peak / RMS，輸出 WAV 給你聽。

思考：為什麼 clipper 的 H2、H4 幾乎是 -100 dB？limiter 的 THD 為什麼接近 0？

## 練習 6 ★★★（實作）Fuzz test：驗證 ceiling 保證

起始碼：`starter/ex6_fuzz_test.cpp`

README 2.5 節證明了演算法不會超過 ceiling，但程式碼可能有 off-by-one、環形緩衝繞回、block 邊界等 bug。請寫一個 fuzz test：

- 隨機訊號（noise / sine / bursts / impulses / DC / 方波 / 混合，振幅最高 +60 dB）；
- 隨機參數（sample rate、channel 數、`setMaxLookaheadMs`、`setLookaheadMs`（故意超出範圍）、ceiling、release、input gain）；
- 隨機 block 長度（1..1024）餵 `process()`；
- 檢查：輸出 peak ≤ ceiling（線性值精確比較）、沒有 NaN / Inf、`getSafetyClipCount() == 0`、`getLatencySamples()` 與理論值一致；
- 至少要有 case 真的「頂到」ceiling（最小 margin < 0.01 dB），否則測試沒測到東西。

進階：在 block 之間隨機改 release / input gain（保證仍應成立）；播放中把 ceiling 往下調（會發生什麼？為什麼 `getSafetyClipCount()` 會 > 0？）；在輸入裡塞 NaN / Inf；模擬 plugin host「每個 block 都把同一個 input gain 再設一次」，檢查 20 ms 的 ramp 是否仍然準時走完。

## 練習 7 ★★☆（觀念）DAW 如何補償 latency

1. 一個 session 有兩軌：vocal 軌插了 lookahead 5 ms 的 limiter，guitar 軌什麼都沒插。如果 DAW 不做任何事，兩軌混在一起會發生什麼？DAW 的 plugin delay compensation（PDC）具體要對哪一軌、做什麼事、延遲多少？
2. 承上，如果 limiter 是插在 master bus 上呢？PDC 還需要延遲誰？播放時的「播放頭位置」要怎麼修正，畫面上的游標才會跟聽到的聲音對齊？
3. 為什麼錄音時「即時監聽」（software monitoring）的路徑上通常不建議放有 lookahead 的 plugin？
4. 本章 Limiter 的 `setLookaheadMs()` 會改變 `getLatencySamples()`。Plugin 應該在什麼時機、用什麼方式告訴 host 延遲變了？（提示：JUCE 的 `AudioProcessor::setLatencySamples()`；想想 host 收到通知後要做什麼、為什麼不建議在播放中改。）
5. 兩個 plugin 回報的 latency 分別是 240 與 64 samples，串在同一軌上；另一軌沒有 plugin。PDC 要在哪裡插入多少 sample 的延遲？
