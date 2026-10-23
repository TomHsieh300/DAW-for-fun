# Step 2 - Transport：play、stop、position、loop、tempo、bars:beats

[English](step-02-transport.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 1](step-01-skeleton.zh-TW.md) | 下一步：[Step 3 - Clips](step-03-clips.zh-TW.md)

## 目標

給 engine 一個時鐘。transport 知道我們是否正在播放、playhead 在哪裡、tempo 與
拍號，以及 loop。audio thread 每個 block 會問它一次：「這個 block 涵蓋 timeline 的哪一段？」
而答案必須精確到 sample，即使 loop 的結尾落在 block 的中間也一樣。

## 設計

### Sample 才是真相，音樂時間是推導出來的

position 是一個 `int64_t` 的 sample 數。音樂時間是從它計算出來的：

```math
N_{\text{beat}} = \frac{60\, f_s}{B}, \qquad N_{\text{bar}} = n \cdot N_{\text{beat}}
```

其中 tempo $`B`$ 以每分鐘拍數計，sample rate 為 $`f_s`$，分子為 $`n`$。一拍是
分母所指的音符時值（在 7/8 中，一拍是八分音符），這也是 DAW 顯示的方式。

位置和螢幕上一樣從 1 起算，每拍 960 個 tick（常見的 PPQ 解析度；PPQ = pulses per quarter note，也就是
sequencer 把一拍切成的 timing tick 數）：

```math
\text{beats}(b, k, t) = (b-1)\,n + (k-1) + \frac{t}{960}, \qquad
\text{samples} = \mathrm{round}\big(\text{beats} \cdot N_{\text{beat}}\big)
```

反向轉換時對拍數取 floor；一個很小的 epsilon（$`10^{-9}`$ 拍）可避免剛好落在網格上的
位置因為浮點數捨入而變成「前一拍的 tick 959」。

範例（120 BPM、4/4、48 kHz）：第 3 小節、第 2 拍、tick 480 是
$`(2 \cdot 4 + 1 + 0.5) \cdot 24000 = 228\,000`$ 個 sample = 4.75 秒。

### Thread safety：atomic 與 pending seek

每個 setter（`play`、`stop`、`setTempo`、`setLoopRange`、……）都寫入一個 `std::atomic`，所以 UI 可以
隨時呼叫。**Seek** 比較特別：如果 audio thread 在處理一個 block 的途中看到 position 改變，
一半的 track 會播放舊的位置，另一半播放新的位置。
所以 `setPosition()` 寫入的是一個 *pending* 值，而 `advance()` - audio thread 處理 block 時
做的第一件事 - 才把它換進來：

```cpp
const int64_t seek = pendingSeek_.exchange(-1, std::memory_order_acq_rel);
if (seek >= 0) position_.store(seek, std::memory_order_relaxed);
```

如果有 pending 值，`getPosition()` 就回傳它，所以 UI 會立刻顯示新的位置。

### Timeline segment 與 loop

`advance(numSamples, segments, samplesCovered)` 會回傳一份 **segment** 清單：「block 的 sample
`[blockOffset, blockOffset + numSamples)` 是 timeline 的 sample `[timelineStart, ...)`」。通常
只有一個 segment。當 loop 結尾落在 block 之內時：

```
loop [1000, 1300), position 1256, block of 100 samples

block:     |0 ............ 43|44 ............. 99|
timeline:   1256 ...... 1299   1000 ......... 1055
segments:  {1256, offset 0, 44} {1000, offset 44, 56}
position after the block: 1056
```

track 會分別 render 每個 segment，所以 loop 是無縫且精確到 sample 的。比 `block / 8` 更短的 loop
會需要超過 `kMaxSegmentsPerBlock = 8` 個 segment；這時 `advance()`
只涵蓋 block 的一部分，用 `samplesCovered` 回報涵蓋了多少，而 `Engine::processChunk()`
只要再呼叫它處理剩下的部分。短到只有一個 sample 的任何 loop 長度都能運作。

符合常見 DAW 行為的規則：

- loop 在啟用**且** `end > start` 時才有效；
- 如果開啟 loop 時 playhead 已經超過 loop 結尾，它會繼續線性播放；
- 停止時，`advance()` 不回傳任何 segment，但仍會套用 pending seek。

## 程式碼

`engine/include/dawlab/engine/Transport.h`，逐段說明：

| 區段 | 功能 |
|---|---|
| `TimeSignature`、`BarsBeats`、`TimelineSegment` | 單純的 struct；`BarsBeats` 從 1 起算 |
| tempo 與 meter | `setTempo()` 會 clamp 到 20..999 BPM；`setTimeSignature()` |
| 播放狀態 | `play()`、`stop()`、`setPlaying()`、`isPlaying()` |
| seek | `setPosition()`、`setPositionSeconds()`、`setPositionBarsBeats()`；在 `advance()` 之前都是 pending |
| loop | `setLoopRange()`、`setLoopRangeBars(firstBar, numBars)`、`setLoopEnabled()` |
| 轉換 | `getSamplesPerBeat()`、`barsBeatsToSamples()`、`samplesToBarsBeats()`、`toSeconds()` |
| `advance()` | 唯一的 audio-thread 方法：seek、segment、position 更新 |

`advance()` 的核心：

```cpp
while (offset < numSamples && count < kMaxSegmentsPerBlock) {
    int64_t n = numSamples - offset;
    if (loopActive && pos < loopEnd)
        n = std::min<int64_t>(n, loopEnd - pos);     // stop exactly at the loop end
    segments[count++] = {pos, offset, static_cast<int>(n)};
    pos += n;
    offset += static_cast<int>(n);
    if (loopActive && pos == loopEnd)
        pos = loopStart;                             // wrap
}
samplesCovered = offset;
```

engine 這一側（`Engine::processChunk`）：

```cpp
while (done < n) {
    TimelineSegment segments[Transport::kMaxSegmentsPerBlock];
    int covered = 0;
    const int numSegments = transport_.advance(n - done, segments, covered);
    mixer_.process(out.getSubBlock(done, covered), segments, numSegments, transport_.getTempo());
    done += covered;
}
```

## Your turn

在 Step 2 的 snapshot 裡親手做出 transport。`steps/step02-transport/minidaw.h` 是你的
Step 1 engine（已解完）加上帶有 atomic 的 `Transport` class；engine 的
`processChunk()` 已經會向 transport 要 segment，並在那些 timeline
位置播放「磁帶」。有三個函式只有宣告：

| | |
|---|---|
| 你要寫的 | `Transport::barsBeatsToSamples()`、`Transport::samplesToBarsBeats()`、`Transport::advance()`（pending seek、在 loop 結尾切開 segment、`samplesCovered`） |
| 檔案 | `steps/step02-transport/starter/step02.cpp` |
| 執行 | `l14_step02_starter`（從 build 目錄：`.\bin\Debug\l14_step02_starter.exe`）；在你完成之前它會印出 `[FAIL] l14_step02 (27 of 31 checks failed)` |
| Solution | `steps/step02-transport/solution/step02.cpp`（ctest `l14_step02_solution`） |

這個檢查只涵蓋 Step 2：網格（包含 7/8，以及 96 BPM 在 44.1 kHz 下一拍是
27 562.5 個 sample 的情況）、仍會套用 seek 的停止中 transport、上面算過的 loop 切分、
需要超過 8 個 segment 的 10-sample loop，以及兩次使用「ramp tape」的 engine render
（sample $`i`$ 的值是 $`i`$，因此每個輸出 sample 都能說明播放的是哪個 timeline 位置）-
跨越 loop 結尾的 441-sample block，以及搭配隨機 block size 的 7-sample loop。
預期輸出（已驗證）：

```
Step 2: the transport (build-along)
  120 BPM, 4/4, 48 kHz: 24000 samples per beat, 96000 per bar
     bar:beat:tick    samples    back
      1:1:000            0    1:1:000
      1:2:000        24000    1:2:000
      1:2:480        36000    1:2:480
      2:1:000        96000    2:1:000
      3:2:480       228000    3:2:480
      9:1:000       768000    9:1:000
  7/8 at 90 BPM: bar 2 = 224000 samples (expected 224000)
  96 BPM at 44.1 kHz: beat 2 = 27563, bar 2 = 110250 (expected 27563, 110250)
  loop [1000, 1300), position 1256, 100-sample block: 2 segments {1256, offset 0, 44} {1000, offset 44, 56}, position after 1056
  10-sample loop, 512-sample block: 8 segments cover 80 samples (expected 8, 80)
  engine, loop [1000, 1300) from 900 with 441-sample blocks: 0 misplaced samples (expected 0)
  engine, 7-sample loop with random blocks 1..600: 0 misplaced of 10000 (expected 0)
[PASS] l14_step02 (31 checks passed)
```

## Verify（參考路線）

像 ctest 那樣從 build 目錄執行這些程式，這樣 WAV 檔才會輸出到
`build\dawlab_output\`：

```powershell
cd build                                       # or: .\scripts\build.ps1 -Only 14-daw-8track
.\bin\Debug\l14_test_timeline.exe
.\bin\Debug\l14_transport_clips.exe
```

`l14_test_timeline` 檢查了（其中包括）：120 BPM 時每拍 24 000 個 sample；第 3 小節第 2 拍 tick 480
= 228 000 並能轉回來；90 BPM 的 7/8 = 每拍 32 000 個 sample、每小節 224 000 個；停止中的 transport
不回傳 segment；上面的切分 `{1256, 44} {1000, 56}`；512 block 中的 10-sample loop 以 8 個 segment 涵蓋
80 個 sample；以及以隨機 block size 播放的 7-sample loop，在 10 000 個輸出 sample 的每一個上，
都重現出預期的 timeline sample。

`l14_transport_clips` 會印出網格（已驗證的輸出）：

```
Musical grid at 120 BPM, 4/4, 48 kHz: 24000 samples per beat, 96000 per bar
   bar:beat:tick    samples    seconds
     1:1:000           0     0.000    (back: 1:1:000)
     1:2:000       24000     0.500    (back: 1:2:000)
     1:2:480       36000     0.750    (back: 1:2:480)
     2:1:000       96000     2.000    (back: 2:1:000)
     3:2:480      228000     4.750    (back: 3:2:480)
     9:1:000      768000    16.000    (back: 9:1:000)
7/8 at 90 BPM (an eighth note is the beat): 32000 samples per beat, 224000 per bar
```

之後它以 441-sample block 對第 1-2 小節做 loop（192 000 不是 441 的倍數）：

```
   wrap 1 in the block at output 191835..192275: segment 1 = 165 samples (191835..191999), segment 2 = 276 samples from 0
   wrap 2 in the block at output 383670..384110: segment 1 = 330 samples (191670..191999), segment 2 = 111 samples from 0
   wrap 3 in the block at output 575946..575999: one segment of  54 samples (191946..191999) ending exactly at the loop end; the position wraps to 0
Looped 3 x 192000 samples, 3 wraps, max |pass 2 - pass 1| = 0
```

第三次 wrap 是 block 剛好結束在 loop 結尾的特殊情況：這個 block 只需要
一個 segment，而 position 會在*下一個* block 時 wrap 回 loop 的起點。第二輪 loop 與第一輪 bit-identical。聽聽 `l14_transport_loop.wav`：在 loop 點沒有空隙，也沒有 click。

## 你學到了什麼

- 用整數 sample 保存時間；推導出小節與拍；只 round 一次。
- UI 的寫入進到 atomic；seek 由 audio thread 在 block 邊界套用。
- 一個 block 不一定是 timeline 上連續的一段：timeline segment 讓 loop 在任何 block size 與任何 loop 長度下
  都精確到 sample。
