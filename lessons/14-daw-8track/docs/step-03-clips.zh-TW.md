# Step 3 - Track 上的 clip：sample 精度的擺放、clip gain、fade

[English](step-03-clips.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 2](step-02-transport.zh-TW.md) | 下一步：[Step 4 - Mixer](step-04-mixer.zh-TW.md)

## 目標

把音訊放到 timeline 上。**Clip** 是位於某個位置的一段音訊；**track** 擁有一份 clip 清單，
並且對每個 block，把每個與該 block 重疊的 clip 加進自己的 buffer - 剛好落在
正確的 sample 上，並套用 clip gain 與 fade。UI 必須能在播放中替換 clip 清單，
而且不用 lock。

## 設計

### Clip 是一個視窗，而不是一份複製

```
timeline:  0 ........ start ........................ start+length .......
source:              [sourceOffset ............. sourceOffset+length)
```

```cpp
struct Clip {
    std::shared_ptr<const AudioBuffer> source; // immutable audio, shared
    int64_t start, length, sourceOffset;
    float gainDb;
    int fadeInSamples, fadeOutSamples;
    FadeShape fadeShape;                       // Linear or EqualPower
    bool muted;
    std::string name;                          // UI only
};
```

音訊是共享且 immutable 的：八份鼓 loop 就是八個指向同一個 buffer 的小型 `Clip` struct。
修剪 clip 的開頭只要 `start += d; sourceOffset += d;
length -= d;` - 沒有任何東西被複製。因為 buffer 是 `const`，在使用期間也永遠不會改變，
audio thread 不需任何同步就能讀取它。

### 重疊

對於 timeline segment $`[T_0, T_0 + N)`$ 與 clip $`[S, S + L)`$：

$$
\text{from} = \max(T_0, S), \qquad \text{to} = \min(T_0 + N, S + L)
$$

如果 $`\text{to} \le \text{from}`$，這個 clip 就不在這個 block 中。否則，對於
$`T \in [\text{from}, \text{to})`$：

$$
\text{dest}[T - T_0] \mathrel{+}= \text{src}[O + T - S] \cdot g_{\text{clip}} \cdot g_{\text{fade}}(T - S)
$$

全部都是整數：沒有捨入、沒有漂移，每種 block size 都得到同樣的結果。Mono source 會餵給兩個
channel；比 source 還長的 clip 則以靜音補足。

### Fade

對於 clip 內部的索引 $`i`$ 與剩餘 sample 數 $`r = L - 1 - i`$：

$$
g_{\text{fade}}(i) = \underbrace{\text{shape}\Big(\frac{i+1}{F_{\text{in}}+1}\Big)}_{i < F_{\text{in}}}
\cdot \underbrace{\text{shape}\Big(\frac{r+1}{F_{\text{out}}+1}\Big)}_{r < F_{\text{out}}},
\qquad \text{shape}(x) = \begin{cases} x & \text{Linear} \\ \sin(\pi x / 2) & \text{EqualPower} \end{cases}
$$

分子與分母的 $`+1`$ 表示 fade 區域內部永遠不會出現恰好為 0 或恰好為 1 的值
（4-sample 的 fade-in 是 0.2、0.4、0.6、0.8），同時也讓相同長度的 fade-out 與 fade-in
互補：在重疊索引 $`k`$ 處，兩個引數分別是 $`\frac{F-k}{F+1}`$ 與
$`\frac{k+1}{F+1}`$，加起來為 1。因此 linear crossfade 的和恰好為 1，而 equal-power
crossfade 滿足 $`\cos^2 + \sin^2 = 1`$（Exercise 5）。

即使只有幾毫秒的 fade 也很重要：在波形中間開始或結束的 clip，訊號上就是一個
階躍，而階躍包含所有頻率 - 也就是 click。

### 不用 lock 交換 clip 清單

track 持有 `std::atomic<const std::vector<Clip>*>`。UI 從不就地編輯清單：

1. UI thread：建立新的 `std::vector<Clip>`（這裡可以配置記憶體）；
2. 以 `ReplaceClips` command 把它送給 audio thread（Step 7）；
3. audio thread：`old = clips_.exchange(newList)` - 在兩個 block 之間做一次 atomic pointer 交換；
4. audio thread：把 `old` 推進 garbage queue；UI thread 稍後再刪除它。

因此 audio thread 永遠不會看到編輯到一半的清單，也永遠不會釋放記憶體。

## 程式碼

### `Clip.h`

| 函式 | 用途 |
|---|---|
| `Clip::fromBuffer(buffer, start, name)` | 播放整個 buffer 的 clip |
| `fadeGain(shape, t)` | $`t`$ 或 $`\sin(\pi t/2)`$ |
| `clipFadeAt(clip, i)` | clip sample $`i`$ 的 fade gain |
| `renderClip(clip, timelineStart, dest)` | 把重疊的部分加進 `dest`；real-time safe |

### `Track.h`

```cpp
class Track : public ChannelStrip {
public:
    const std::vector<Clip>* exchangeClips(const std::vector<Clip>* newClips) noexcept; // audio thread
    const std::vector<Clip>* getClips() const noexcept;                                  // any thread, see below
    int64_t getEndSample() const noexcept;                                               // song length
    void renderClips(AudioBlock dest, const TimelineSegment* segments, int numSegments) noexcept;
};
```

`renderClips()` 會走訪 Step 2 的 segment，並對每個 clip 呼叫 `renderClip()`，把
segment 的 `timelineStart` 對應到相應的 sub-block。有 $`C`$ 個 clip 時，每個 block 的成本是 $`O(C)`$；
有成千上萬個 clip 的真正 arranger 會讓清單保持排序，並用 binary search 找出第一個
候選者。

只要執行 `collectGarbage()` 的就是同一條 thread，從 UI thread 呼叫 `getClips()` 就是安全的：
它讀取的清單只可能由它自己釋放。

## Your turn

`steps/step03-clips/minidaw.h` 是你的 Step 2 engine 加上 `Clip`、`fadeGain()` 與一個
逐 segment render 其 clip 的 `Track`。engine 現在有 8 個 track，而不是磁帶，
輸出是它們單純的總和 - 在 Step 4 之前沒有 fader 也沒有 pan，所以 0.5 的 clip sample 會剛好以
0.5 輸出。在 Step 7 之前，track 把 clip 放在一個單純的 `std::vector` 中，只在 engine 沒有執行時
才會被替換（上面的 lock-free 交換是 Step 7 的工作）。

| | |
|---|---|
| 你要寫的 | `clipFadeAt()`（兩條 fade 公式）與 `renderClip()`（重疊、source 索引、clip gain、fade、mono -> 所有 channel、`+=`） |
| 檔案 | `steps/step03-clips/starter/step03.cpp` |
| 執行 | `l14_step03_starter`；在你完成之前它會印出 `[FAIL] l14_step03 (24 of 27 checks failed)` |
| Solution | `steps/step03-clips/solution/step03.cpp`（ctest `l14_step03_solution`） |

預期輸出（已驗證）：

```
Step 3: clips (build-along)
  impulse clip at 12345, block size  512: found at 12345, 1 non-zero samples
  impulse clip at 12345, block size   64: found at 12345, 1 non-zero samples
  impulse clip at 12345, block size    1: found at 12345, 1 non-zero samples
  impulse clip at 12345, block size 1000: found at 12345, 1 non-zero samples
  trimmed clip: out[999] 0.0000, out[1000] 0.0500, out[1299] 0.1995, out[1300] 0.0000 (expected 0, 0.0500, 0.1995, 0)
  fade-in  : 0.20 0.40 0.60 0.80 1.00 (expected 0.20 0.40 0.60 0.80 1.00)
  fade-out : 0.80 0.60 0.40 0.20 (expected 0.80 0.60 0.40 0.20)
  crossfade over 100 samples: max |gOut + gIn - 1| (linear) 0, max |gOut^2 + gIn^2 - 1| (equal power) 1.2e-07
[PASS] l14_step03 (27 checks passed)
```

被修剪的 clip 是一個 ramp source（sample $`i`$ = $`i/1000`$），`sourceOffset = 100`、
`length = 300`，clip gain 為 -6.02 dB：$`0.100 \cdot 0.5`$ 與 $`0.399 \cdot 0.5`$。這個檢查也
測試了 stereo source、比 source 還長的 clip、重疊的 clip，以及被 mute 的 clip。

## Verify（參考路線）

`l14_test_timeline`（已驗證，全部通過）：

- timeline 12 345 處的 impulse clip 會出現在輸出 sample 12 345 - 且不會出現在其他地方 - block
  size 為 512、64、1 與 1000 時皆然；
- 修剪：`sourceOffset = 100`、`length = 300`、ramp source 上的 clip gain -6.02 dB：第一個
  輸出 = 0.100 x 0.5 x 0.7071，最後一個 = 0.399 x 0.5 x 0.7071，緊接著是靜音；
- stereo clip 保持它的 channel；比 source 長的 clip 在 source 之外是靜音；
- 重疊的 clip 會相加；
- fade：0.2、0.4、0.6、0.8、1；linear crossfade 的 gain 總和為 1，equal-power 的平方和為 1。

`l14_transport_clips` 用 16 個 clip 建立一段兩小節的琶音（4 個共享的 pluck buffer、每個八分音符一個 clip、clip 長度 = 一個八分音符、25 ms 的 fade-out、除了第 1 拍之外都是 -6 dB），並檢查每個 clip 的
起點：在它的起始 sample $`S`$ 處，輸出必須恰好等於 `source[0] x clipGain x 0.7071`
（前一個 clip 剛結束，而 pluck 一開始是隨機 noise，所以只要有一個 clip 偏了一個 sample，
就會顯示出不同的值）。已驗證的輸出：

```
Clip placement (first 6 of 16):
   clip  0 at 1:1:000 = sample      0, length 12000, gain 0 dB
   clip  1 at 1:1:480 = sample  12000, length 12000, gain -6 dB
   ...
Rendered once: 240000 samples, peak -9.04 dBFS, misplaced clips: 0
```

聽聽看：`l14_transport_clips.wav` - 均勻的八分音符，每小節第一個音符有重音，多虧了 fade-out，音符之間沒有 click。

## 你學到了什麼

- clip 是進入共享、immutable 音訊的一個視窗；編輯 clip 從不複製音訊。
- 當所有位置都是整數，且重疊是逐 segment 計算時，sample 精度是自然得到的。
- fade 的設計讓 crossfade 彼此互補。
- audio thread 讀取的清單是被替換的，而不是被編輯的：建立新的、交換 pointer、稍後在另一條
  thread 上釋放舊的。
