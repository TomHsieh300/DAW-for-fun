# Step 3 - Varispeed

[English](step-03-varispeed.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 2 - 起點跟著 tempo 的 clip](step-02-follow-start.zh-TW.md) | 下一步：[Step 4 - Time-stretcher](step-04-stretch.zh-TW.md)

## 目標

用另一個速度播放，讓 clip 裡的每樣東西都跟著 tempo，就像磁帶那樣：把 audio 讀快一點或慢一點。知道歌的每個 sample 該讀檔案的哪裡而且永不漂移、怎麼讀檔案兩個 sample 之間的值，以及讀得比較快會對頻譜的高頻做什麼。

## 設計

### 速度和音高

Step 1 給了速度：現在的 tempo 除以 audio 製作時的 tempo。以速度 $`r`$ 讀檔案，裡面的每樣東西都快了 $`r`$ 倍——也高了 $`r`$ 倍：

```math
semitones = 12 \log_2 r
```

快一倍就是高八度。在 120 BPM 做的 loop 用 126 播放，會高 0.84 個半音。鼓的 loop 沒有人會在意；歌手或鋼琴就和歌裡其他東西不合調了。Varispeed 適合鼓和小幅度的改變，其他的交給 Step 4。

### 位置來自歌，不是來自計數器

速度不是 1 的時候，檔案裡的位置是小數。最直覺的做法——從某處開始，每個 sample 加上速度——錯了兩次。一百萬個取整過的步伐加起來會漂移，而且 seek 或 loop 繞回之後，這個總和反正得重新找。所以位置是每個 block 都從歌的 sample、經過 Step 1 的兩個 map 算出來的：

```math
position(s) = \mathrm{warp.sourceAt}(\mathrm{map.tickAt}(s / rate))
```

Self-check 播放十分鐘、每小節都從 90 ramp 到 150 BPM 的歌。用每個 block 開頭的速度累加，結束時位置偏了 1484 個 sample（31 ms）。從歌自己的 sample 算，從頭到尾都不超過 0.003 個 sample。

### Block 裡面

每個 sample 都算 `position(s)`，每次要一個 binary search 和一個 `exp()`。其實不必：tempo 不變時，位置是它的 **knot**——map 的 tempo 點和 warp map 的 marker——之間的一條直線。所以一個 block 只在它的轉角算精確位置，中間畫直線：

- block 的第一個 sample，和最後一個 sample 的下一個，
- block 裡的每個 knot（knot 很少剛好在整數 sample 上），
- 以及歌裡每個是 64 的倍數的 sample。

最後一項是為了 ramp，在那裡位置處處都在彎。Step 1 的上界說明彎多少：跨 $`h`$ 個 sample 的弦最多偏 $`h^2/8 \cdot \max|position''|`$。在很陡的 ramp 上，一個 512 sample 的 block 中間偏了 0.7 個 sample——不多，但會讓音高在每個 block 的邊緣稍微折一下。64 個 sample 一段則接近 64 倍：在 self-check 的隨機 ramp 上最差是 0.016 個 sample。這個 grid 是從歌的 sample 0 算起，不是從 block 算起，所以轉角不取決於 block 剛好從哪裡開始。

### 兩個 sample 之間

在小數位置讀檔案的三種方法，依成本排列：

- **Linear**：兩個相鄰 sample 之間的直線。兩個 sample、一次乘法。
- **Hermite**：通過兩個相鄰 sample 的三次曲線，斜率取自*它們的*鄰居（Catmull-Rom）。四個 sample、幾次乘法。
- **Sinc**：sampling theorem 說 sample 之間的訊號*就是*這個——位置周圍 $`\sin(\pi x)/(\pi x)`$ 的總和。這個總和是無限的；這裡在兩邊各 16 個 zero crossing 處截斷，並用 Blackman window 淡出。大約 33 個 sample 和同樣多的 sine。

誤差隨頻率增加。對 5 kHz 的音，self-check 量出把音本身拿掉之後剩下的東西：

```text
linear    35 dB below the tone       audibly dull and dirty in the top octaves
Hermite   49 dB
sinc     100 dB and more             below what 16 bits can hold
```

在 440 Hz，Hermite 已經低了 100 dB。要用哪一個是預算問題：Step 5 事先算好，sinc 在那裡的成本無關緊要。

### 比 1 快就是重新取樣

用兩倍速讀檔案，等於用一半的 rate 重新取樣。原本高於新 Nyquist 頻率的東西不會消失：它會折回來變成 alias。15 kHz 的音用兩倍速讀應該是 30 kHz，48 kHz 的串流裝不下——它會在 18 kHz 被聽到，音量幾乎不減。

Sinc 是 low-pass，可以當自己的 anti-alias filter：依速度拉寬之後，它在 $`Nyquist / speed`$ 而不是 Nyquist 處截止（涵蓋範圍也變成 $`speed`$ 倍，所以更貴）。Hermite 和 linear 做不到。Self-check 的結果：用 Hermite 時 alias 只比原音低 3 dB，用 sinc 低 78 dB。

Sinc 的總和要除以權重的總和。Window 會讓某個小數位置的權重加起來比 1 多一點或少一點，不除的話，穩定的訊號會隨小數位置起伏。

### Clip 是 source 的一段範圍

Warp 過的 clip 播放 source 的一段範圍 `[sourceFrom, sourceTo)`，涵蓋位置落在這段範圍內的那些歌的 sample——兩個 map 把它們放在哪裡就在哪裡。同一個 take 的兩半共用切點：一個結束的 source 位置就是另一個開始的位置。所以在任何 tempo 下，後半的第一個 sample 都是前半最後一個 sample 的下一個。Step 2 的 4 秒空隙和重疊都消失了，而且切開的 take 和沒切的那個，每個 sample 都相同。

## 換你了

在 `steps/step03-varispeed/starter/step03.cpp` 裡寫出 `semitonesOf()`、`readAt()`、`positionsFor()`、`firstSample()`、`endSample()` 和 `renderWarped()`。建置並執行：

```powershell
.\scripts\build.ps1 -Only 20-audio-tempo
.\build\bin\Debug\l20_step03_starter.exe
```

```text
Step 3: varispeed
  a loop made at 120 BPM played at 126: 0.84 semitones up; at 60: -12
  5 kHz read at speed 0.7 and 1.3, linear : the error is  35.1 and  35.1 dB below the tone
  5 kHz read at speed 0.7 and 1.3, Hermite: the error is  48.9 and  48.9 dB below the tone
  5 kHz read at speed 0.7 and 1.3, sinc   : the error is 100.4 and 116.3 dB below the tone
  15 kHz read twice as fast: Hermite leaves an alias at -3.0 dB, the sinc -78.3 dB
  600 blocks of up to 512 samples over maps of jumps  : the worst position 2.8e-09 samples off, 0 blocks not exact at their start
  600 blocks of up to 512 samples over maps with ramps: the worst position 0.016 samples off, 0 blocks not exact at their start
  ten minutes of ramps in blocks: the worst position 0.003 samples off; counted up from the speed instead: 1484 samples (31 ms)
  300 clips over random maps: 0 with a first or a last sample that is not the range's
  a take cut in two at bar 3, at 60 BPM: no gap (the halves meet at 8.0 s), the cut is 0 away from the uncut take,
    and what plays is 220 Hz, an octave down (everything else 102 dB below)
  16 clicks recorded at 120 BPM, played over 100, 150 and a ramp from 70 to 130: 16 on their beats (the last at 7.861 s, was 7.500 s)
[PASS] l20_step03 (32 checks passed)
```

換一個 compiler 的 math library，這些數字的最後幾位可能稍有不同。

## 你學到了什麼

- Varispeed 讓 clip 裡的每樣東西都跟著 tempo，音高則移動速度的 $`12 \log_2`$。
- 檔案裡的位置每個 block 都從歌的 sample 算出來；計數器會漂移。
- Block 裡的位置是精確轉角之間的直線：block 的兩端、knot，以及給 ramp 用的 grid。
- Linear、Hermite 和 sinc 是同一個問題的三種價格；只有 sinc 還能在讀得比 1 快時擋住 aliasing。
- 同一個 source 的幾段範圍做成的 clip，在任何 tempo 下都會相接。
