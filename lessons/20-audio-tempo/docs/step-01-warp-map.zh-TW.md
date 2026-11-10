# Step 1 - Warp map

[English](step-01-warp-map.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 下一步：[Step 2 - 起點跟著 tempo 的 clip](step-02-follow-start.zh-TW.md)

## 目標

說明 clip 的 audio 屬於音樂的哪個位置，讓它能跟著 tempo 走：檔案的哪個 sample 在歌的哪個 tick 上。再從這個和 tempo map 找出 player 需要的東西——現在要播哪個 sample，以及多快。

## 設計

### Marker

音符會跟著 tempo，因為它是以 tick 存的。一個 audio clip 有成千上萬個 sample，每一個都屬於音樂的某個位置——但沒有人會為每個 sample 存一個 tick。**Warp map** 只存幾組數對，也就是 **warp marker**：

```text
(source, tick)        「檔案的這個 sample 屬於歌的這個 tick」
```

兩個 marker 之間，audio 平均分布在 tick 上。第一個 marker 之前和最後一個之後，沿用最近那一對的直線，所以被修剪或延長的 clip 仍然知道自己在哪裡。

```text
source
(samples)                         . (96000, bar 3)
   |                         .
   |                    .           a loop of one bar, put on bar 2:
   |               .                two markers, one straight line
   |          .
   +-----o-----------------------> ticks
      (0, bar 2)
```

Source 位置和 tick 都是小數。Marker 很少剛好落在整數 tick 上，而且在 120 BPM、48 kHz 下一個 tick 是 25 個 sample——用來擺放 audio 太粗了。

### 兩個 map，兩個問題

Warp map 把檔案的 sample 綁到 tick。第 19 章的 tempo map 把 tick 綁到時間。兩者合起來回答 player 的問題：

```text
song sample --tempo map--> tick --warp map--> source sample       現在要播什麼？
source sample --warp map--> tick --tempo map--> seconds           這個何時被聽到？
```

兩個 map 互不認識。改了 tempo map，同樣的 marker 會把同樣的 audio 放在同樣的拍子上，只是時間變了：這就是「跟著 tempo 走」。

### 速度

讀檔案的速度是兩個斜率的乘積：warp map 的每 tick 幾個 source sample，和 tempo map 的每個 song sample 幾個 tick。

```math
speed = \frac{\Delta source}{\Delta tick} \cdot \frac{B \cdot 960}{60 \cdot rate}
```

在 120 BPM 做的 loop，48 kHz 下每個 tick 有 25 個 sample。歌在 120 BPM 時每個 sample 是 1/25 tick，速度是 1：loop 照原樣播放。60 BPM 時速度是 0.5。速度就是現在的 tempo 和 audio 製作時的 tempo 的比值——Step 3 和 4 是用不等於 1 的速度播放的兩種方法。

### Loop 的 marker

已知長度是四拍的 loop 需要兩個 marker：它的第一個 sample 在它被放上去的 tick，最後一個 sample 的下一個在四拍之後。它自己的 tempo $`beats \cdot 60 \cdot rate / length`$ 從來不用存：兩個 marker 已經說明了。

### Take 的 marker

Take 是在某個 tempo map 上錄的，從歌的某個 sample 開始。Map 不變時它應該完全照錄的樣子播放，之後 map 改變時則要跟著走。所以它的 marker 複製了錄音時的 map：take 的 sample $`n`$ 是在 $`(start + n) / rate`$ 秒被聽到的，而 tempo map 會說那是哪個 tick。

Tempo 不變的時候，take 的 tick 是它的 sample 的一條直線。這時在 take 的起點、take 底下的每個 tempo 點，以及它的終點各放一個 marker 就是**精確的**：它們之間的直線就是 map 自己的。

在 ramp 上這條線是曲線，直線段只能逼近。有多接近是個經典結果：把曲線 $`f`$ 在寬度 $`h`$ 的一段上換成它的弦，誤差最多是

```math
\frac{h^2}{8} \max |f''|
```

這裡 $`f(t)`$ 是 ramp 上 tick $`t`$ 的時間，$`f' = \frac{60}{960} / B(t)`$、$`f'' = -\frac{60}{960} \, k / B(t)^2`$，其中 $`k`$ 是 ramp 的斜率（每 tick 的 BPM）。$`|f''|`$ 在 ramp 慢的那一端最大。把 $`T`$ 個 tick 的 ramp 切成 $`n`$ 等份，$`h = T / n`$，所以以秒計的容許值 $`\varepsilon`$ 需要

```math
n = \left\lceil T \sqrt{\frac{\max |f''|}{8 \, \varepsilon}} \right\rceil
```

份。好消息是那個平方根：十倍的精確度大約只要三倍的 marker。四小節內從 60 到 120 BPM 的 ramp，要維持在 1 ms 內需要 46 個 marker，0.1 ms 需要 143 個。

Marker 的誤差不會沿著 take 累加：每個 marker 都是直接從 tempo map 算出來的，不是從前一個 marker 算的。

## 換你了

在 `steps/step01-warp-map/starter/step01.cpp` 裡寫出 `WarpMap::sourceAt()` 和 `tickAt()`、`sourceAtSample()`、`songSecondsOf()`、`speedAt()`、`markersForLoop()` 和 `markersForTake()`。建置並執行：

```powershell
.\scripts\build.ps1 -Only 20-audio-tempo
.\build\bin\Debug\l20_step01_starter.exe
```

Self-check 會把一個 loop 放在第 2 小節並讓歌在它底下變慢，在 200 個隨機 warp map 上雙向換算，在隨機 tempo map 上做出 take 並檢查每個 sample 是否在錄音時的位置，然後改掉 take 底下的 map：

```text
Step 1: a warp map
  a loop of one bar on bar 2: its third beat at 3.0 s at 120 BPM, at 4.0 s with 60 BPM from bar 2 (speed 0.50)
  200 random warp maps: 0 ticks not back from their source, 0 steps backwards
  200 takes over maps of jumps: 0 refused, 0 with a marker too many or too few, 0 not where they were recorded
    (worst 4.3e-14 s), 0 places with a speed other than 1
  a take of a ramp from 60 to 120 BPM over 4 bars within 10.00 ms:  16 markers, the worst sample 8.3299 ms off
  a take of a ramp from 60 to 120 BPM over 4 bars within  1.00 ms:  46 markers, the worst sample 0.9661 ms off
  a take of a ramp from 60 to 120 BPM over 4 bars within  0.10 ms: 143 markers, the worst sample 0.0985 ms off
  a take of a ramp from 60 to 120 BPM over 4 bars within  0.01 ms: 449 markers, the worst sample 0.0099 ms off
  150 takes over maps of jumps and ramps within 0.5 ms: 0 refused, 0 off by more, 0 with more markers than needed (8871 markers in all)
  100 takes, then a new map: 0 refused, 0 samples not at half their time at twice the tempo,
    0 places where the speed is not new tempo / old tempo
[PASS] l20_step01 (22 checks passed)
```

每個 ramp 最差的那個 sample 都很接近它的容許值，而不是遠低於它：這個上界在 ramp 慢的地方很緊，而等分的做法在 ramp 快的地方用了比需要更多的 marker。第 19 章的練習 5 可以找出最少的；這個公式只有一行，而且永遠不會錯。

## 你學到了什麼

- Warp map 是幾組「檔案的 sample、歌的 tick」數對，中間以直線相連。
- Warp map 和 tempo map 互不認識；新的 tempo map 會移動 audio，卻不用動任何 marker。
- 速度是兩個斜率的乘積：現在的 tempo 除以 audio 製作時的 tempo。
- 在跳躍上錄的 take，在 tempo 點放 marker 就是精確的；ramp 需要 $`\sqrt{1/\varepsilon}`$ 個。
