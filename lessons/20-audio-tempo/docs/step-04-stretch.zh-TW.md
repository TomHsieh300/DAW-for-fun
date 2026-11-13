# Step 4 - Time-stretcher

[English](step-04-stretch.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 3 - Varispeed](step-03-varispeed.zh-TW.md) | 下一步：[Step 5 - Engine](step-05-engine.zh-TW.md)

## 目標

改變 audio 的速度而保持音高。做出最簡單而能用的 stretcher——WSOLA——並量出它做得好和做不好的地方。

## 設計

### 是一段一段，不是一個個 sample

Varispeed 改變每個 sample 經過的速度，而音高就是 sample 經過的速度。要保持音高，sample 就得以自己的速度經過。所以把 audio 切成互相重疊的小段，每一段照原樣播放，再把這些小段擺得比取出時更近或更遠。

```text
input    [  A  ]                      pieces taken every 256 samples ...
            [  B  ]
               [  C  ]

output   [  A  ]                      ... and put down every 512: twice as long,
                [  B  ]                   every piece at its own pitch
                       [  C  ]
```

這就是 **overlap-add**（OLA）。一段是 1024 個 sample 的 window，用 Hann 曲線淡入淡出，output 每 512 個 sample 開始新的一段。兩個重疊的 Hann 半邊加起來正好是 1，所以速度為 1 時——取的間隔也是 512 個 sample——output 就是 input。

每一段從哪裡取，來自 Step 3：中點在 output sample $`c`$ 的那一段，取自 input 位置 `positions[c]` 附近。Stretcher 不知道 tempo map 或 marker；它拿到一串位置，每個 output sample 一個，而這串位置的斜率可以在任何地方改變。

### 這些小段接不起來

在其他任何速度下，相鄰的兩段在 input 裡並不相鄰。它們重疊的地方，一段的波形落在另一段波形的某處。在這個重疊處互相抵消，在下一個又相加，穩定的音出來就變成別的東西：self-check 的 440 Hz 經過單純的 OLA 之後，「不是這個音」的部分比這個音本身還多。

### 讓小段滑動：WSOLA

一段不必正好取在位置所說的地方。早幾毫秒或晚幾毫秒，在音樂上是同一個時刻——而在這個範圍裡，某處是接得上的。

什麼東西能完美接上是已知的：input 裡緊接在上一段前半之後的那些 sample，也就是它的 **natural continuation**。所以在候選位置前後 `search` 個 sample 內的起點當中，取前半最像它的那一個：

```math
score(d) = \frac{\sum_i a_i \, b_i}{\sqrt{\sum_i a_i^2}} \qquad a = \text{input from } candidate + d, \quad b = \text{the natural continuation}
```

分子是 correlation。除以候選段的大小，讓分數只看形狀：不除的話，大聲的地方會因為大聲而贏；改成除以完整的能量，則會讓幾乎無聲的地方贏。分數相同時取最近的。

這就是 **waveform-similarity overlap-add**。有兩件事讓它是安全的：

- 一段離 tempo 要它在的位置最多 `search` 個 sample（這裡是 5 ms），而且這個誤差不會累加：下一個候選位置又是從位置算出來的，不是從上一段最後落在哪裡算的。
- 速度為 1 時，候選位置*就是* natural continuation，什麼都不滑動，output 仍然是 input。

### 它做得好的

單音。Self-check 把 440 Hz 拉到一半和兩倍的速度：音高維持在百分之一個半音內，音量在 0.01 dB 內，而「不是這個音」的部分比它低 31 到 40 dB。剩下這些的原因很單純：一段只能以整數 sample 滑動，所以只能對準到半個 sample 以內，而 440 Hz 的半個 sample 在每個接合處都是一小步相位。對準到 sample 的小數位置（Step 3 知道怎麼讀那裡）可以再壓得更低。

### 它的代價

只發生**一次**的聲音。一聲鼓擊存在於涵蓋它的每一段裡——速度為 1 時有兩段，它們把鼓擊放回同一個 output sample。拉長到兩倍時，含有鼓擊的那幾段被擺得比取出時更遠，鼓擊落在兩個地方：聽到兩次，變成 flam。壓縮到一半時，每一段相隔一整個 window 才取一次；靠近段落邊緣的鼓擊被 window 淡掉，self-check 的 8 個 click 掉了 4 個。

WSOLA 裡沒有任何東西知道 transient 是什麼。真正的 stretcher 會先找出 transient 另外處理，或是改在頻譜而不是波形上處理，所以它們都是很長的程式。對 DawLab 來說，選擇是每個 clip 各自決定：**varispeed** 給鼓和所有音高不重要的東西，**stretch** 給有音高的東西。兩者跟著 tempo 的精確度都和 Step 1 的 marker 一樣。

搜尋要花時間：每一段要試 513 個偏移、每個 512 次乘法，output 每秒約五千萬次。事先算好的話這不算什麼，但要在 audio thread 上為每個 clip 做就太多了——這是 Step 5 的事。

Window 和 search 是 48 kHz 下的長度。但一段代表的是一段時間：同樣 1024 個 sample 在 96 kHz 下只有一半長，裝不下低音的週期，55 Hz 的音出來低了一個八度。Engine 的 stretcher（`stretchSettingsFor()`，Step 5）會依 sample rate 等比放大它們，讓同一個音在任何 sample rate 下聽起來都一樣——而且在任何 sample rate 下，都能保住約 60 Hz 以上的音的完整週期。比這更低的音，一段滑動的距離不夠遠，找不到下一個週期，bass 線條就會走樣。所以 engine 會先看 clip 播的內容：如果有四分之一以上的能量在 80 Hz 以下，它的每一段就用 40 Hz 的音所需要的長度（search 一個週期 25 ms，window 兩個週期）。這要花四倍的工夫，鼓擊也會更糊，所以只有這種 clip 才這樣做。

## 換你了

在 `steps/step04-stretch/starter/step04.cpp` 裡寫出 `bestOffset()` 和 `stretch()`。建置並執行：

```powershell
.\scripts\build.ps1 -Only 20-audio-tempo
.\build\bin\Debug\l20_step04_starter.exe
```

在 self-check 的輸出裡，「plain OLA」是同一個 stretcher 把 search 設為 0，而 dB 是負數表示「不是這個音」的部分比這個音還大聲。

```text
Step 4: a time-stretcher
  noise at speed 1: at most 0 from the input
  440 Hz at speed 0.50: 4.0 s long, +0.72 cents, what is not the tone 30.9 dB below it, the level -0.00 dB (plain OLA: -25.2 dB, -2.17 dB)
  440 Hz at speed 0.80: 2.5 s long, +0.29 cents, what is not the tone 38.8 dB below it, the level +0.00 dB (plain OLA:  -1.4 dB, -0.08 dB)
  440 Hz at speed 1.25: 1.6 s long, -0.36 cents, what is not the tone 36.9 dB below it, the level -0.00 dB (plain OLA: -13.4 dB, -0.63 dB)
  440 Hz at speed 2.00: 1.0 s long, +0.25 cents, what is not the tone 39.6 dB below it, the level +0.00 dB (plain OLA: -19.4 dB, -1.79 dB)
  at speed 0.6 the level of noise changes by -0.56 dB, of a chord by -0.30 dB (plain OLA: -1.01 dB)
  at speed 1.5 the level of noise changes by -0.63 dB, of a chord by -0.28 dB (plain OLA: -1.49 dB)
  speed 0.75 for a second, then 1.5: 440 Hz in both (+0.36 and -0.72 cents, 36.9 and 30.9 dB)
  8 clicks at speed 0.5: 8 heard, 8 of them twice, the loudest copy at most 480 samples from its place
  8 clicks at speed 0.9: 8 heard, 0 of them twice, at most 53 samples off
  8 clicks at speed 2.0: 4 heard, 0 of them twice, at most 112 samples off
[PASS] l20_step04 (22 checks passed)
```

## 你學到了什麼

- 要保持音高，audio 是一段一段搬動的，每一段都以速度 1 播放；重疊的 Hann window 加起來是 1。
- 從別處取來的小段接不起來；讓每一段滑到看起來像上一段的 natural continuation 的地方，就接得上。
- 滑動有上限而且不會累加，所以拉長後的 audio 仍然對得上 tempo。
- WSOLA 適合單音，不適合單一的敲擊：拉長時會變成兩下，壓縮時可能消失。
