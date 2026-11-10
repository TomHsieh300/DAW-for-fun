# Step 4 - Engine

[English](step-04-engine.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 3 - 樂器的 latency](step-03-latency.zh-TW.md) | 下一步：[Step 5 - App：tempo](step-05-app-tempo.zh-TW.md)

## 目標

把 Step 1 到 3 帶進 DawLab 的 engine：transport 裡的 tempo map、帶著自己 tick 的 segment、跟著它們走的 MIDI player，以及延遲補償會等待其 latency 的樂器——而第 18 章的測試不改一行仍然通過。

## 設計

### 先複製，再修改

和第 15 到 18 章一樣，這個 step 的第一個 commit 是第 18 章原封不動的 engine 和測試；第二個 commit 才修改它們。`scripts/check_copies.py` 現在也涵蓋第 19 章，所以這個 step 沒改的檔案會和第 18 章的保持一致。

### Map：一個值，三個 buffer

`TempoMap.h` 是 engine 需要的那種 Step 1 和 2 的 map：一個固定大小的值——最多 256 個點放在 `std::array` 裡，它們以秒計的起點放在旁邊——所以複製它不會配置記憶體。`setTempo(bpm)` 現在是一個只有一個點的 map，所有以前用「那個」tempo 換算的東西，現在都經過 map 換算。

UI thread 隨時會給 transport 一個新的 map；audio thread 用一個 map 播放一個 block。一個 map 有 8 KB：沒有 atomic 裝得下。Seqlock 可以，但它的讀者會讀寫入者正在寫的記憶體，C++ 稱之為 data race（ThreadSanitizer 也會回報）。所以 transport 保存**三個** map 並把它們傳來傳去：

```text
   UI thread                         audio thread
   寫它的 BACK map                   播放它的 FRONT map
   exchange(back <-> middle | new)   在 block 開頭：如果 middle 是新的，exchange(front <-> middle)
```

每個 map 同一時間只屬於一個 thread；交接是一次 atomic exchange。沒有人會讀到正在被寫的 map，audio thread 上也不會複製任何東西。UI 自己還留了第四份，用於它自己的換算（小節和拍子、grid）。這四個 map 放在 heap 上：多出的 32 KB 會讓 Engine 放不進測試的 stack——這是測試第一次執行時發現的，就像第 18 章為它的樂器發現的那樣。

### Transport 播放 Step 2

`Transport::advance()` 給每個 `TimelineSegment` 它的 tick，`tickFrom`、`tickTo` 和 `ticksJump`，就像 Step 2 的 transport 那樣。新的 map 在 block 開頭被取用，播放頭保持在它音樂上的位置。Loop 現在有兩種設定方式：

- `setLoopRange(samples)`——和以前一樣：tempo 改變時它留在原地，邊界的 tick 從 sample 找出來。第 14 到 18 章的每個測試都這樣設定 loop，而它們不改一行就通過。
- `setLoopRangeTicks()` 和 `setLoopRangeBars()`——以 tick 設定：loop 跟著 tempo 走，邊界是精確的。

`samplesToBarsBeats()` 現在找出在某個 sample 播放的 tick——sample 不在它之後的最後一個 tick——而不是除以一個 tempo 再加一個 epsilon：拍子會落在它的 tick 上，不論取整做了什麼。

### MIDI player 跟著 tick 走

第 18 章的 `ClipPlayer` 從 sample 找出它的 tick，並把不是從上一個結束處開始的 segment 視為跳躍。現在它直接採用 segment 的 tick，在 `ticksJump` 這麼說時才跳躍。這正是讓按住的音能撐過 tempo 改變的原因：sample 跳了，tick 沒有，player 沒有理由結束這個音。剛被 reset 的 player 仍然會先 chase。

### 補償計畫裡的樂器 latency

`Instrument::getLatencySamples()`（除非樂器另有說明，否則為 0）加入 track 的 latency，`Track::getLatencySamples()` = insert + 樂器。第 14 章的 `Mixer::updateLatencies()` 用它算出最大的 track latency 和每個 track 的補償，automation lane 的位移也用它。打開 `Mixer::setLowLatencyMonitoring(true)` 時，arm 的 track 會跳過補償，和 Step 3 一樣。

## 檢查

第 18 章的 14 個 engine 測試不改一行就通過。`tests/test_tempo.cpp` 是新的：

```text
l19_test_tempo: a tempo map and instrument latency in the engine
  32 beats over a map that speeds up, jumps and slows down: every note on at its sample in blocks of 512, random and 7: yes
  a loop of ticks 960-1920 where two ticks share a sample, 7 passes in random blocks: 7 and 7 note ons, 0 misplaced;
    the note at 959 before it once: 1
  a tempo change half way through a held note: 1 note on, 1 note off, at 335872 (wanted 335872)
  a sampler 1000 samples late next to an instant synth: mix latency 1000, both heard at 25000
  the sampler armed, low-latency monitoring: mix latency 0, the synth at 24000, the sampler on its own at 25000
  the synth armed next to the sampler, low-latency monitoring: the synth at 24000, the sampler at 25000
  an insert's play head through a ramp from 60 to 120 BPM: 60.0 at the start, 120.0 at the end, never down
  29 new maps while a loop plays: 0 allocations on the audio thread
  N maps of up to 200 points from the UI thread while the audio thread plays 2001 blocks: 0 torn
[PASS] l19_test_tempo (18 checks passed)
```

## 你學到了什麼

- 大的值靠 triple buffer 跨 thread：每份屬於一個 thread，交接是一次 exchange。
- 舊的介面保留它們的意義（以 sample 設定的 loop、`setTempo`），新的介面有自己的名字。
- 相信 transport 的 tick 的 MIDI player，能讓音符撐過 tempo 改變。
- 樂器的 latency 是 track 的一部分，第 14 章的補償計畫不需要其他修改。
