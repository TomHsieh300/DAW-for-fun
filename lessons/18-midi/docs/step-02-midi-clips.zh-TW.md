# Step 2 - 播放 MIDI clip

[English](step-02-midi-clips.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 1 - MIDI 事件與音樂時間](step-01-midi-time.zh-TW.md) | 下一步：[Step 3 - 一個 voice](step-03-one-voice.zh-TW.md)

## 目標

在會 loop、會跳躍、會停止的 transport 下播放音符 clip，而且使用者還會 mute 和刪除 clip——要讓每個 note on 都剛好有一個 note off，並且不論從哪裡開始播放，controller 都是正確的值。

## 設計

### 音符有起點和長度

Step 1 播放的是一串事件。編輯器顯示的不是事件，而是音符——有起點和長度的一條橫槓——clip 以它自己的時間保存音符，從 clip 的零點算起：

```text
Note            start, length（從 clip 零點算起的 tick）, pitch, velocity, channel
ControllerPoint tick, number, value, channel
MidiClip        start（時間軸上的 tick）, length, offset, muted, notes, controllers
```

Clip 是素材上的一扇*視窗*，和第 14 章的 audio clip 一樣：它在時間軸 tick `start` 的位置，顯示素材從 `offset` 到 `offset + length` 的部分。把 clip 的左緣往右拖，`offset` 和 `start` 會一起增加；音符在歌曲中的位置不變。`MidiClip::place()` 決定音符在哪裡播放：

- 起點在視窗內的音符，在 `start + note.start - offset` 播放；
- 比視窗長的音符，在視窗結束處結束；
- 起點在視窗之前——在被修剪掉的開頭之前——的音符完全不播放。

最後一條是一個選擇。音符的開頭被切掉了，而少了起音的鋼琴音已經不是同一個音。大多數 DAW 提供照樣播放這種音符的選項（「chase notes」）；本章保持簡單。

### 誰來結束音符？

一串事件裡每個 note on 都有一個 note off。播放時就不是這樣了。以下情況會結束一個依序列來說仍在響的音符：

<div align="center">

| 發生的事 | note off 放在哪裡 |
|---|---|
| 音符比它的 clip 長 | clip 結束處 |
| loop 繞回、使用者 seek | 跳躍發生的 sample |
| transport 停止 | 停止的那個 block |
| clip 被 mute 或刪除 | 下一個 block 的開頭 |

</div>

漏掉任何一種，樂器就會永遠彈著那個音——*卡住的音*，這是最古老的 MIDI bug（每台 synth 都有一個 panic 鍵專門對付它）。解法是記帳：播放器保存一張它啟動過的音符表——clip、channel、pitch，以及它將結束的 tick——並自己結束這些音符。播放器沒有啟動過的音符，絕不送出 note off，所以被修剪或 mute 隱藏的音符，不可能結束另一個 clip 的音符。

這張表的大小固定，128 個音符：`process()` 在 audio thread 上執行。表滿了的時候，新的音符不播放，由 `droppedNotes()` 計數——相反的選擇（結束最舊的音符）是 voice 分配器的事，那是 Step 4。

音符響著的時候 clip 被編輯，音符保留它開始時的結束位置。這張表用一個在編輯時保持不變的 id 認得 clip，所以被移動或修剪的 clip 仍是同一個 clip，而被刪除的——或被另一個新 id 的 clip 取代的——就是不見了。

### Controller 要 chase

Controller 不是有起點和終點的事件。它是一個值，維持到下一次變動為止。在 filter sweep 進行到一半時停止，seek 回開頭，synth 的 filter 仍停在 sweep 留下的位置：從頭播放的內容在 clip 下一次變動之前都是錯的。

所以每次跳躍之後，播放器都會 *chase* controller：對新位置底下的每個 clip，送出每個 controller 在該位置之前的最後一個值——也包括視窗開頭之前的點，它們仍描述著素材在那裡聽起來的樣子——而且在其他所有事件之前送出。剛好在新位置上的點不 chase；從那裡開始的 segment 會送出它，送兩次雖然無害但不整齊。

音符不 chase（見上文），這正是兩者的差別：值可以在任何時候設定，音符的開頭卻不能事後補上。

### 一個 block，兩段

Transport 把一個 block 交出來時是一或兩個 *segment*：loop 在 block 內繞回時就是兩個。`MiniTransport` 會把不接續前一段的 segment 標為 `jump`——在 `play()`、`seek()` 和繞回之後。`process()` 依序處理每個 segment：

```text
jump?                        所有音符 off，接著 chase，都在 segment 的 offset
被 mute 或不見的 clip 的音符  在 segment 的 offset off
segment 的 tick              [firstTickAtOrAfter(start), firstTickAtOrAfter(end))   （Step 1）
  note on                    在這裡開始的音符，加進表中
  note off                   在這裡結束的表中項目——包括剛開始的音符
  controller                 這裡的點，在視窗內
排序                         依 tick；同一個 tick 上 off、然後 controller、然後 on
```

同一個 tick 上的順序有兩處很重要。剛結束處再彈一次的音符需要先送 note off，和 Step 1 一樣。而在音符開始的同一個 tick 改變的 controller 必須先到達樂器，否則音符會以舊的值開始。

為了在不配置記憶體的情況下排好一個 segment 的事件，它們被收集在一個只預留一次的清單裡，每個都帶著 tick、代表種類的 rank，以及代表發現順序的序號，再用 `std::sort` 依這三者排序：不可能出現相等的 key，所以結果和穩定排序一樣。這個清單的大小也是固定的，而且永遠為每個響著的音符保留一個 note off 的空間：滿了的時候丟掉（並計數）的是 controller，絕不是 note off。

### 自我檢查怎麼知道

兩類檢查：

- **完全等於 clip。** 三個隨機 clip——被修剪的開頭、比視窗長的音符、只有一個 tick 的音符、在結束處再彈一次的同一個音高——手動展開成事件，再用 64、512、1000 和隨機大小的 block 從頭播到尾。播放器必須在同樣的 sample 給出同樣的事件。
- **絕不卡住音符。** `StrictInstrument` 依 channel 和 pitch 計算正在響的音，對沒有 on 的音符收到 note off 會抱怨。三十個各 2500 個 block 的隨機 session，隨機地停止、播放、seek、loop、mute、刪除和編輯；最後 transport 停止，不能有任何聲音。

兩者之間，還有手動的測試：seek 到小節中間（兩個音符都結束、mod wheel 被 chase 到那裡的值、長音符不會重新開始）、停止、mute、取消 mute、刪除、在 block 內繞回的 loop、修剪、滿了的表，以及塞爆的清單。

## 換你了

在 `steps/step02-midi-clips/starter/step02.cpp` 寫這四個函式：

1. `MidiClip::place()`：在視窗內與否，以及在視窗結束處截斷的結尾。
2. `ClipPlayer::allNotesOff()`：表中每一項一個 note off，然後清空表。
3. `ClipPlayer::chase()`：每個 controller 在該位置之前的最後一個值，用固定大小的陣列。
4. `ClipPlayer::process()`：上面那張表，一個 segment 一個 segment 地做。

完成後，輸出的結尾會像這樣：

```text
Step 2: playing a MIDI clip
  a loop of 36100 samples, 3 wraps: 6 note offs and 6 note ons at the wrap samples
  24 straight runs (2048 events) in blocks of 64, 512, 1000 and random sizes: 0 differ from the clips
  30 random sessions, 1532 note ons: 0 unmatched or misplaced events, 0 notes left sounding (expected 0, 0)
[PASS] l18_step02 (33 checks passed)
```

## 你學到了什麼

- Clip 是素材上的視窗；起點在視窗外的音符不播放。
- 播放會結束序列沒有結束的音符：在 clip 結束處、跳躍、停止、mute、刪除時。
- 記下你啟動過的東西，並且只結束那些。
- Controller 是值：跳躍後要 chase。音符不 chase。
- 同一個 tick 上：note off、controller、note on。
- 在 audio thread 上排序時不配置記憶體，而且絕不丟掉 note off。
