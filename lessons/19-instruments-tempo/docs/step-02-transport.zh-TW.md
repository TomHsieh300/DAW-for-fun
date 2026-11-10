# Step 2 - Tempo map 上的 transport

[English](step-02-transport.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 1 - Tempo map](step-01-tempo-map.zh-TW.md) | 下一步：[Step 3 - 樂器的 latency](step-03-latency.zh-TW.md)

## 目標

播放一首 tempo 會變的歌——在 block 裡、跨過 loop 的繞回、以及使用者在播放時移動了 tempo 點——每個 tick 恰好播放一次、落在它的 sample 上，不論 block 多大。

## 設計

### 兩個時鐘：sample 和 tick

Engine 的時間軸仍然以 sample 為單位。Audio clip、automation、錄音和延遲補償都以 sample 計數，而 tempo 改變不能移動錄好的 take：它就是在那些 sample 上彈的。音符以 tick 為單位。第 14 章的 transport 把一個 block 描述成 **segment**，也就是時間軸 sample 的範圍；現在每個 segment 也說明它播放哪些 **tick**，$`[tickFrom, tickTo)`$，以及它們是否接續上一個 segment 的（`ticksJump`）。

通常一個 segment 的 tick 是 Step 1 的 $`[\mathrm{first}(s_0), \mathrm{first}(s_1))`$，而下一個 segment 從 $`s_1`$ 開始，所以它的 tick 從這裡結束的地方開始。Transport 把這當成規則，而不是巧合：在持續播放的一段裡，`tickFrom` 就是上一個 segment 的 `tickTo`。有兩個地方需要它。

### 以 tick 表示的 loop

使用者以小節設定 loop。如果存成 sample，tempo 改變時它會留在原地，不再從小節線開始。所以 loop 存成 **tick**，它的 sample 每個 block 都從 map 算出：$`[\mathrm{sampleOfTick}(start), \mathrm{sampleOfTick}(end))`$。

在它的邊界，取整成 sample 會出問題。在 999 BPM、8 kHz 時，兩個 tick 共用一個 sample，$`\mathrm{first}(\mathrm{sampleOfTick}(end))`$ 可能是 `end` 前一個 tick：只靠 sample 的話，每一圈會在結尾丟掉一個 tick，並在開頭重播一個。所以結束在 loop 終點的 segment 結束在 tick `end`，繞回後的 segment 從 tick `start` 開始——直接給定，而不是從 sample 再找一次。每一圈都恰好播放 loop 的 tick。

### 播放時換了 map

使用者拖動一個 tempo 點。播放頭該怎麼辦？它在某個小節某一拍，使用者預期它就停在那裡：保留的是**音樂上的位置**。用 Step 1 的 `tickAt()` 找出它在舊 map 下精確的、帶小數的 tick，播放頭移到這個 tick 在新 map 下的 sample。

對音訊來說這是一次跳躍——sample 跑到別的地方，就像 seek 之後一樣。對音符來說不是：tick 從上一個 segment 結束的地方繼續，`ticksJump` 維持 false，第 18 章的 MIDI player 會讓正在響的音繼續響。一個跨過這次改變而按住的音會繼續按住，以新的 tempo。

```text
    舊 map：   ...|  第 3 小節 |...         播放頭在第 3 小節第 2 拍（tick 7926）
    新 map：   ...|      第 3 小節      |...  同一個 tick，在它新的 sample；tick 繼續
```

Tempo 改變時會動的，是以 tick 為單位的東西：音符、loop、小節和拍子、節拍器。不會動的，是以 sample 為單位的東西：audio clip 和 automation。以 120 BPM 錄的鼓 loop 不會跟著歌加速——那需要 time-stretch，是另一個主題。

### 節拍器

`clicksIn()` 是第一個播放 map 的東西：在一個 segment 的 tick 裡每一拍一聲，每小節第一拍加重，落在該拍的 sample 上。它在 ramp 裡會加速，因為拍子加速了；而它正確的程度和 tick 一樣——繞回之後它從 loop 的第一拍開始，不會早一個 tick。

## 換你了

在 `steps/step02-transport/starter/step02.cpp` 裡寫出 `Transport::setMap()`、`Transport::advance()` 和 `clicksIn()`。建置並執行：

```powershell
.\scripts\build.ps1 -Only 19-instruments-tempo
.\build\bin\Debug\l19_step02_starter.exe
```

Self-check 會播放一首加速、跳躍、再減速的歌——一次用單一 block，一次用 1 到 1500 sample 的 block——在 tempo 變化上 loop、在兩個 tick 共用一個 sample 的地方 loop，並在播放時移動一個 tempo 點：

```text
Step 2: a transport on a tempo map
  8 bars that speed up, jump and slow down: 30720 ticks once each at their samples, 32 clicks; the same in blocks of 1-1500
    beats 1-4 at samples 0, 30736, 59194, 85687; beats 29-32 at 876026, 917169, 958312, 999455
  bars 2-4 looped 5 times: each pass the loop's 7680 ticks, the ticks jump at each wrap; the same in blocks of 1-1500
  two ticks a sample, ticks 960-1920 looped 7 times, both edges on a shared sample: every pass exactly its 961 ticks
  a tempo point put in while it plays: the playhead stays at tick 7926, no tick lost or played twice, no jump;
    a bar after it is 192000 samples later (it was 96000)
[PASS] l19_step02 (18 checks passed)
```

## 你學到了什麼

- 時間軸仍然是 sample；segment 也帶著它的 tick，而 transport 讓它們接續下去。
- 以小節設定的 loop 存成 tick，它的邊界以 tick 給定：取整成 sample 移動不了它們。
- Tempo 改變時保留播放頭音樂上的位置；對音訊來說是跳躍，對音符來說不是。
- 以 tick 為單位的東西跟著 tempo 走；以 sample 為單位的不會。
