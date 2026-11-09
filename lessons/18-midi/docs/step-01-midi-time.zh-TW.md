# Step 1 - MIDI 事件與音樂時間

[English](step-01-midi-time.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 下一步：Step 2 - 播放 MIDI clip（即將推出）

## 目標

音符不是聲音。它是一個指令——「開始彈中央 C，這麼大聲」——在歌曲中的某個時刻抵達，而歌曲以拍子計數，audio thread 卻以 sample 計數。把這些指令表示出來，放到音樂的時間軸上，並在它所屬的那個 sample 精確地交給樂器，不論 audio block 是多大。

## 設計

### 三個 byte

MIDI 是 1983 年為 31250 baud 的線材設計的，它的訊息至今仍是三個 byte 以內。第一個 byte 是 *status*：最高位元永遠是 1，高 4 位元說明這是什麼訊息，低 4 位元指定 16 個 channel 之一。後面的資料 byte 最高位元是 0，所以每個帶 7 個位元，0 到 127。

<div align="center">

| Status | Data 1 | Data 2 | 訊息 |
|---|---|---|---|
| `0x8n` | note | velocity | note off |
| `0x9n` | note | velocity | note on——**velocity 為 0 時就是 note off** |
| `0xBn` | controller | value | control change：64 延音踏板、1 mod wheel、7 音量…… |
| `0xEn` | 低 7 位元 | 高 7 位元 | pitch bend，14 位元，8192 = 不彎音 |

</div>

有兩個細節會絆倒每個第一次寫 MIDI parser 的人。velocity 為 0 的 note on 就是 note off：大多數鍵盤都這樣送放鍵，因為這樣整段演奏都能沿用同一個 status byte（*running status*，Step 5）。而 pitch bend 是*低*位元先送：`(data2 << 7 | data1) - 8192` 得到 -8192 到 8191，範圍由樂器決定——通常是兩個半音。

`MidiMessage` 原封不動地保存三個 byte，需要時才用 `kind()` 解讀。Program change、aftertouch 和系統訊息在這裡都是 `Other`：本章的樂器用不到它們，也絕不能把它們當成音符。

### 用 tick，不用 sample

音符該存在哪裡？如果像 audio clip 一樣存成 sample，改變 tempo 時每個音符都會留在原地，音樂就散了。音符存在 *tick* 裡，也就是四分音符的分數：一個四分音符 960 個 tick，和第 14 章的 `Transport` 計數方式相同。只有在播放時，tick 才變成 sample：

```text
samplesPerTick = sampleRate x 60 / (bpm x 960)        120 BPM、48 kHz 時剛好是 25
sample(tick)   = round(tick x samplesPerTick)
```

這正是本章會一再回到的差別。自我檢查最後就測這個：120 BPM 時第 3 小節從 sample 192000 開始，同一個音符在 90 BPM 時是 256000。位於 sample 192000 的 audio clip 則留在原地。

這個公式每次都從 tick 本身算起。一個 tick 一個 tick 地累加 `samplesPerTick` 看起來一樣而且比較便宜，但每次相加都會捨入，誤差會累積：歌曲進行一小時後，音符就會晚一點才被聽到。`sampleOfTick(tick)` 沒有記憶，自我檢查會在 133 BPM（每個 tick 22.556 個 sample）下算到一小時之後，和精確的 long double 計算比對。

### 這個 block 裡有哪些事件？

Audio thread 一個 block 接一個 block 地問：哪些事件屬於 sample `[s0, s1)`？答案必須讓每個事件剛好出現一次，不論 block 怎麼切。確保這點的方法是，用 `sampleOfTick` 的精確反函數把 block 的邊界換成 tick：

```text
firstTickAtOrAfter(s) = 滿足 sampleOfTick(t) >= s 的最小 tick t
block [s0, s1) 包含的 tick 是 [firstTickAtOrAfter(s0), firstTickAtOrAfter(s1))
```

相鄰的 block 共用一個邊界，所以 tick 範圍也是相鄰的，沒有任何 tick 同時屬於兩個範圍或一個都不屬於。只算 `s / samplesPerTick` 不夠：捨入會把那個點之前的某些 tick 也送到 `s`（999 BPM、22.05 kHz 時每個 tick 1.38 個 sample，好幾個 tick 會落在同一個 sample 上）。所以 `firstTickAtOrAfter` 從 `ceil(s / samplesPerTick)` 開始——這個 tick 一定到得了 `s`——然後只要前一個 tick 也到得了就往回退。

知道 tick 範圍後，`std::lower_bound` 在排序好的序列中找到第一個事件，迴圈在第一個超出範圍的事件停下。Audio thread 從不從歌曲開頭走起，而且只要預先 `reserve()` 空間，就從不配置記憶體。

### 同一個 tick 上的順序

同一個 tick 上的兩個事件仍然有先後。一段旋律彈 C 再彈一次 C，第一個 C 的 note off 和第二個 C 的 note on 就在同一個 tick。如果 note on 先到，note off 會立刻結束*新的*音符：樂句中間出現一個無聲的音。所以 `sortEvents` 在每個 tick 上把 note off 排在前面，其他事件則保持原本給定的順序——*穩定*排序：同一個 tick 內 mod wheel 的三次移動必須依它們發生的順序抵達，而 `std::sort` 不保證這點。

### Sample 精確

樂器會產生音訊、接收事件。偷懶的做法是把一個 block 的所有事件都交給它，再算整個 block：每個音符都從它所在 block 的開頭開始。512 個 sample、48 kHz 時最多晚 10.7 ms，更糟的是誤差取決於使用者選的 buffer 大小——改了 buffer，律動就變了。所以 `renderBlock` 在每個事件處切開 block：

```text
render [0, offset1)  handle(event1)  render [offset1, offset2)  handle(event2) ... render [last, n)
```

同一個 offset 上有好幾個事件時，它們之間什麼都不算。第 16 章對 automation 做了一樣的事，測試方法也一樣：*block 不變性*。自我檢查用 `CountingInstrument` 算 300 個音符——它的輸出是按住的音符數，一個階梯，精確地顯示每個事件何時生效——分別用 1、7、64、512 和隨機大小的 block，在四種 tempo 和取樣率下執行，並把每一次都和逐個 sample 處理事件的參考結果比對。

## 換你了

在 `steps/step01-midi-time/starter/step01.cpp` 寫這七個函式：

1. `MidiMessage::kind()`：高 4 位元，以及 velocity 0。
2. `MidiMessage::pitchBendValue()`：data2 是高的那一半。
3. `sampleOfTick()`：捨入，從 tick 算起。
4. `firstTickAtOrAfter()`：從 ceiling 開始，往下退。
5. `sortEvents()`：穩定排序，同一個 tick 上 note off 在前。
6. `eventsInBlock()`：tick 範圍、`lower_bound`、offset。
7. `renderBlock()`：算到每個事件為止，再把它交出去。

完成後，輸出的結尾會像這樣：

```text
Step 1: MIDI events and musical time
  an hour at 133 BPM: tick 7660800 is sample 172800000
  20 runs of 600 events in blocks of 1, 7, 64, 512 and random sizes: 0 differ from the reference
  bar 3 starts at sample 192000 at 120 BPM, at 256000 at 90 BPM
[PASS] l18_step01 (53 checks passed)
```

## 你學到了什麼

- MIDI 訊息是一個 status byte 加上最多兩個 7 位元的資料 byte；velocity 為 0 的 note on 是 note off。
- 音符存在 tick 裡，會跟著 tempo 走；音訊存在 sample 裡，不會。
- 每次都從 tick 算出 sample：累加會漂移。
- 有了精確的反函數，相鄰的 block 會讓每個事件剛好出現一次。
- 同一個 tick 上 note off 先到，其他事件保持原有順序。
- 在每個事件處切開 block，否則時間點會取決於 buffer 大小。
