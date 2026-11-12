# Step 5 - Engine

[English](step-05-engine.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 4 - Time-stretcher](step-04-stretch.zh-TW.md) | 下一步：[Step 6 - App](step-06-app.zh-TW.md)

## 目標

把 Step 1 到 4 放進 DawLab 的 engine，而且不讓它的 audio thread 做其中任何一件事：跟著 tempo 的 clip 在 worker thread 上事先算好，再交給一個只播放一般 clip 的 engine——它一直都是這樣。

## 設計

### 先複製，再修改

`engine/` 和 `tests/` 一開始是第 19 章的原樣複本（一個 commit，什麼都沒改，15 個測試通過）。這個 step 接著加了兩個 header 和一個測試——而且除了 include 它們的 `All.h` 之外，**沒有**改動任何既有的檔案。`scripts/check_copies.py` 可以證明：其他每個 engine 檔案仍然和第 19 章的相同。

### Stretcher 在哪裡跑

Stretcher 有兩個地方可以放。

**在 audio thread 上**，新的 tempo 馬上聽得到。但 WSOLA 是以 20 ms 的小段工作的，所以有 latency，track 得像樂器那樣回報它（第 19 章 Step 3）。它的搜尋在 output 每秒要花約五千萬次乘法——每個 clip、每個 block 都要，而且是在那個絕對不能遲到的 thread 上。

**事先算好**，由 worker thread 做出新的 buffer，engine 像收其他東西一樣用 command 收到它。Audio thread 播的是一般的 clip：沒有 latency、沒有額外成本，`renderClip()` 也不用改。代價是算的那段時間——算好之前要播什麼？——以及多一份 audio 的記憶體。

DawLab 選擇事先算好。這正是 engine 已經有的交接方式（`setTrackClips()`、command queue、給舊 list 用的 garbage queue），而且讓運算可以用 Step 3 的 sinc 和 Step 4 的搜尋，品質不打折。如果是那種邊聽邊拖 tempo、手一動就聽到它跟著伸縮的 DAW，就需要另一種做法，通常背後還有這一種當 cache。

### Clip 現在是什麼

```cpp
enum class FollowMode { Off, Start, Varispeed, Stretch };

struct FollowClip {
    Clip clip;                       // as chapter 14's: what plays when it does not follow
    FollowMode mode;
    double startTick;                // Start: where the clip belongs (Step 2)
    std::vector<WarpMarker> markers; // Varispeed, Stretch: where the audio belongs (Step 1)
};
```

`renderFollowClip()` 把一個 `FollowClip` 和一個 tempo map 變成實際播放的一般 `Clip`：

- **Off**：clip 本身。
- **Start**：clip 放在它的 tick 所在的 sample。
- **Varispeed**、**Stretch**：一個新的 buffer，正好涵蓋位置落在 clip 的 source 範圍 `[sourceOffset, sourceOffset + length)` 內的那些歌的 sample——所以切開的 take 的兩半在任何 tempo 下仍然相接。

離線運算時，**每一個**歌的 sample 的位置都精確算出來。Step 3 那種 knot 之間畫直線的做法是給必須很快的 player 用的；在這裡，一秒的 varispeed 大約花 40 ms（每個 sample 一次 `tickAt()` 加一次加窗的 sinc），而且沒有任何東西會彎。

Stretch 時兩個 channel 一起滑動：搜尋在它們的平均值上進行，每個 channel 都在同一個地方切。如果左右各自找最適合的位置，stereo image 會晃動。

### Worker

`FollowRenderer` 擁有一個 thread、一個 request 的 queue 和一個 result 的 list，全都在一個 mutex 後面。這裡用 mutex 沒有問題：只有 UI thread 和 worker 會碰它，兩者都不是 audio thread。

```text
UI thread                         worker thread                    audio thread
tempo map or a clip changes
FollowTrack::update()
  placeholders to the engine  ----------------------------------->  plays them
  FollowRenderer::request()  -->  renderFollowClip()
(a timer)                         ... done
FollowRenderer::collect()    <--  result
FollowTrack::take()
  the renders to the engine  ------------------------------------>  plays them
```

### 過期的結果

使用者拖動一個 tempo 點：每幾毫秒就有一個新的 map，每一個都要求一次比這更久的運算。三條規則讓這件事保持有序。

1. **每個 request 都有 generation**，每個 clip 各自計數。`collect()` 只交回最新要求的那個 generation 的結果；舊 map 的結果即使已經算完也會被丟掉。
2. **還在等的 request 會被**同一個 clip 較新的 request **取代**，worker 正在算的那個則會被**取消**：運算每算一段就看一次旗標，然後放棄。測試要求了 23 次，只開始了 4 次運算。
3. **沒有變的東西不重算**。新的 gain、fade、名稱或 mute 不需要新的 buffer，而在同一個 map 下沒有改變的 clip 會保留它的結果。

測試連續送出 120 個 map：途中只有一兩個結果算完，而最後播放的是最後一個 map 的結果。

### 這段期間播什麼

在結果算好之前，clip 播放的是 **placeholder**：它上一次的結果，移到 clip 現在開始的位置。這是暫時的 Step 2 行為——clip 的起點在拍子上，之後則是舊的速度。如果還沒有任何結果（clip 才剛建立），placeholder 就是 clip 自己的 audio。

匯出不能帶著 placeholder 出去：`FollowRenderer::waitUntilIdle()` 會等到所有運算都完成。Step 6 會在 bounce 之前呼叫它。

### 它沒有做的事

- 運算結果不會存檔。它們是 cache：source、marker 和 map 可以重新做出它們。
- 在 48 kHz 下超過 93 分鐘的 render 會被拒絕，記憶體不夠時也一樣。它會以一個 `ok` 為 false 的 `Result` 回來（cancel 永遠不會這樣回來：cancel 之後一定還有更新的 request），clip 繼續播放它的 placeholder，`FollowTrack::getNumFailed()` 會把它算進去。App 會在狀態列說明，而且不會 export。
- Stretch 時，切開的 take 的兩半是分開處理的：各自最多可以滑動 5 ms，所以它們的接縫不像 varispeed 那樣每個 sample 都精確。在切點做一小段 crossfade 可以蓋掉。
- Stretch 的一段最多會讀到 clip 的 source 範圍之外 5 ms：被修剪掉的邊緣外面的 audio，在邊緣處可能聽得到。在邊緣做 fade 同樣可以蓋掉。

## 檢查

`tests/test_warp.cpp` 不需要 JUCE，也會在 ThreadSanitizer 和 AddressSanitizer 下執行：

```text
l20_test_warp: audio clips that follow the tempo
  150 takes over random maps: 0 refused, the worst sample 0.488 ms from where it was recorded (7005 markers)
  varispeed, a 440 Hz loop at half and at twice its tempo: 220 Hz (the rest 109 dB below) and 880 Hz (151 dB)
  stretch, the same loop: 440 Hz at both tempos (+0.72 and +0.25 cents, the rest 31 and 40 dB below)
  varispeed, 15 kHz at twice its tempo: what is left is 75 dB below the tone it was
  23 requests, 20 of them overtaken: 4 renders begun, 3 finished
  12 clicks recorded at 120 BPM, through the engine over 100, 150 and 75 BPM: 12 heard, 0 off their beats (the last at 6.400 s)
  120 maps in a row: 1 renders taken on the way, the render at the end is the last map's (392505 samples, wanted 392504)
  an audio thread next to a UI thread that changes the map and a worker that renders: 16 renders handed over in 16034 blocks,
    0 allocations on the audio thread
[PASS] l20_test_warp (87 checks passed)
```

運算次數和 block 數取決於機器的速度；檢查本身不受影響。

它的最後一部分讓真正的 audio thread 和 UI thread、worker 同時執行：結果送達時 audio thread 沒有任何配置（這個檔案替換了 `operator new` 並計數），ThreadSanitizer 也沒有看到 race。舊的 buffer 在第 14 章安排的地方釋放：跟著舊的 clip list，在 UI thread 的 `collectGarbage()` 裡。

## 你學到了什麼

- 又重又有 latency 的工作，如果結果可以事先做好，就不該放在 audio thread 上。
- 只播放一般 clip 的 engine 可以在不知情的情況下播放 warp 過的 clip：運算結果只是另一個 buffer。
- 在背景製作的 cache 需要 generation：同一個東西只有最新的 request 算數。
- Cache 過期時要播什麼是一個決定，而最便宜又誠實的做法是把上一次的結果移到新的位置。
