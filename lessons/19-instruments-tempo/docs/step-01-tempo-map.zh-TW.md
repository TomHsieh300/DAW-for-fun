# Step 1 - Tempo map

[English](step-01-tempo-map.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 下一步：Step 2 - Tempo map 上的 transport（即將推出）

## 目標

在 tempo 改變時把 tick 換成時間：從一個 tempo 跳到另一個，以及 ramp——漸快或漸慢，tempo 從一個點直線走到下一個點。兩個方向都要精確，而且快到每個 block 都能用。

## 設計

### 點、跳躍與 ramp

Tempo map 是一串點，每個點是一個 tick 和一個 tempo。從一個點開始，tempo 要嘛維持到下一個點（在下一個點跳躍），要嘛直線走到下一個點的 tempo（ramp）。最後一個點永遠維持。在 tick 0 之前——預備拍——維持第一個點的 tempo。

```text
BPM
140 |      ______                 一個 map：120 到第 2 小節，跳到 140，
    |     /      |                在第 3 小節 ramp 降到 100，
120 |____/       |_____           100 維持
100 |            '-----.____
    +----+----+----+----+----> 小節
```

Map 以**秒**為單位，而不是 sample。同一首歌在 44.1 和 48 kHz 下有同一個 map；sample 是 $`\mathrm{round}(seconds \cdot rate)`$，到最後才算。

### 一個 tick 的時間

Tempo 為 $`B`$ 時一個 tick 長 $`60 / (960 B)`$ 秒。跳躍時就這樣：一段裡的 $`n`$ 個 tick 是 $`n \cdot 60 / (960 B)`$ 秒。Ramp 時每個 tick 底下的 tempo 都在變，所以時間是一個積分。從 tick $`a`$ 的點開始 tempo 為 $`B(t) = B_0 + k (t - a)`$，到 tick $`c`$ 的下一個點，$`k = (B_1 - B_0) / (c - a)`$：

```math
s(t) = \frac{60}{960} \int_a^t \frac{d\tau}{B_0 + k(\tau - a)} = \frac{60}{960} \cdot \frac{\ln\left(1 + k (t - a) / B_0\right)}{k}
```

四個小節從 60 ramp 到 120 BPM 花 $`16 \ln 2 = 11.09`$ 秒——在 60 時會花的 16 秒和 120 時的 8 秒之間，而不是它們的平均 12 秒：慢的部分持續得比較久。

反函數也有解析式——把公式對 $`t`$ 解出來：

```math
t = a + \frac{B_0}{k}\left(e^{k \, s \cdot 960 / 60} - 1\right)
```

每個點的起點是它之前各段的總和，不取整（第 18 章的練習 4 示範了每段都取整的代價）。於是一個 tick 就是用二分搜尋找到它的段，再套一個公式。

### 位數

$`\ln(1 + x)`$ 和 $`e^x - 1`$ 在 $`x`$ 很小時，照字面寫會失去位數：$`1 + x`$ 會把 $`x`$ 捨入到 1 的精度。一個幾乎沒在 ramp 的 ramp——十萬個小節從 120 到 120.000001 BPM——在第 1000 小節時 $`x \approx 10^{-11}`$，`std::log((B0 + k t) / B0) / k` 在那裡的誤差就已經超過 self-check 允許的範圍。`std::log1p` 和 `std::expm1` 正好計算這兩個函數，而且不會失去位數。

### 一個 sample 的第一個 tick

和第 18 章一樣，audio thread 會問哪些 tick 落在 block $`[s_0, s_1)`$ 裡：它們是 $`[\mathrm{first}(s_0), \mathrm{first}(s_1))`$，其中 $`\mathrm{first}(s)`$ 是 sample 至少為 $`s`$ 的最小 tick。`tickAt()` 給出一個 sample 精確的、帶小數的 tick，`ceil()` 給出在它之後（含）的整數 tick。取整成 sample 仍可能把前一個 tick 拉到同一個 sample 上，所以 `firstTickAtOrAfter()` 會在這種情況下往回退——並且在它的 tick 的 sample 仍在 $`s`$ 之前時往前走。在第 18 章，乘法讓往前走不可能發生；換成對數和指數這兩個只在紙上互為反函數的函數，它是防最後一位數的保險。Self-check 從來用不到它，但還是保留。

## 換你了

在 `steps/step01-tempo-map/starter/step01.cpp` 裡寫出 `prepare()`、`bpmAt()`、`secondsAt()`、`tickAt()`、`sampleOfTick()` 和 `firstTickAtOrAfter()`。建置並執行：

```powershell
.\scripts\build.ps1 -Only 19-instruments-tempo
.\build\bin\Debug\l19_step01_starter.exe
```

Self-check 會和一個完全不懂對數的參考答案比較——在 long double 下對 $`1/B(t)`$ 用 Simpson 法則——涵蓋 150 個由跳躍和 ramp 組成的隨機 map，從秒換回 tick，並把一首每小節都在加速減速的歌用隨機大小的 block 播放：

```text
Step 1: a tempo map
  120, 60 and 240 BPM a bar each: bars at 0, 2, 6 and 7 s
  60 to 120 BPM over 4 bars: 11.090355 s (16 ln 2 = 11.090355), 90 BPM half way
  a ramp of 0.000001 BPM over 100 000 bars, at bar 1000: 1999.999999916667 s (reference 1999.999999916666)
  150 random maps of jumps and ramps: 0 times off the reference (worst 2.0e-11 s), 0 not back from seconds,
    0 samples whose first tick is wrong, 0 steps backwards
  a song that ramps up and down every bar, 1536 events in random blocks: 1536 found, 0 outside their block
[PASS] l19_step01 (24 checks passed)
```

## 你學到了什麼

- Tempo map 是由跳躍和 ramp 組成的點；ramp 的時間是一個有解析式的積分，它的反函數也有。
- 把 map 存成秒，讓它和取樣率無關。
- `std::log1p` 和 `std::expm1` 保住了 $`\ln(1+x)`$ 和 $`e^x-1`$ 在 $`x`$ 很小時會失去的位數。
- 在紙上互為反函數的兩個公式，在浮點數裡並不是：精確的反函數要檢查自己的結果。
