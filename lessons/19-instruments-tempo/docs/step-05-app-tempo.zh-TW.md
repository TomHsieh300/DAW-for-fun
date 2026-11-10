# Step 5 - App：tempo

[English](step-05-app-tempo.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 4 - Engine](step-04-engine.zh-TW.md) | 下一步：[Step 6 - App：樂器](step-06-app-instruments.zh-TW.md)

## 目標

把 tempo map 帶進 DawLab：一條可以畫和編輯它的 lane、跟著它走的 ruler 和 grid、undo、專案檔，以及帶著 tempo 變化進出的 MIDI 檔。

## 設計

### 先複製，而它已經能動

這個 step 的第一個 commit 是第 18 章的 app，只把名字改成 DawLab 19，跑在第 19 章的 engine 上。它能建置，smoke test 原封不動就通過：對 app 來說，一個 tempo 就是只有一個點的 tempo map，`setTempo()` 的意思也沒變。這是 Step 4 小心對待舊介面的回報。

### Model：多一個 edit

關於 map 的一切都能用一個 edit 來 undo：`SetTempoMap`，它取代全部的點——就像第 18 章的 `SetMidiClips` 取代一個 track 的 clip。Transport bar 的 tempo 欄位保留它的 edit `SetTempo`：它現在設定*第一個*點的 tempo，其他的點不動。`Session::sampleOfTick()` 和 `tickOfSample()` 經過 map，所以所有本來就問 session 的東西——arrangement 裡的 MIDI clip、piano roll 的播放頭、錄音——都跟著 map 走，而不需要知道。

### 原本用 sample 而不該用的地方

有三個地方用「每拍多少 sample」來算，而這個數字已經不存在了：

- **Ruler 和 grid。** 小節和拍子是 tick；每一個都畫在 map 給它的 sample 上。歌比較快的地方，拍子靠得比較近。
- **Snap。** Clip 對齊到最近的拍子：滑鼠精確的 tick，取整到拍子，再換回 sample。
- **Loop。** 以小節設定的 loop 現在存成 tick（`setLoopRangeBars()`），所以 tempo 改變時它仍然在它的小節上。取樣率改變不會動到以 tick 存的 loop。

還有一個地方需要真正想清楚——**在 loop 中錄 MIDI**。第 18 章把按鍵的時間展開成一個穿過 loop 各圈一直往下數的 sample，再除以 tempo 得到 tick。有了 map，超過 loop 終點的 sample 會用 loop *之後*的 tempo 來讀。所以要先把 sample 繞回它的那一圈，透過 map 找出它的 tick，再以 tick 把圈數加上去。

### 專案檔，格式 7

```text
tempo 120 4 4
loop 3840 11520 0 ticks
tempopoint 0 120 0
tempopoint 3840 60 1
tempopoint 7680 180 0
```

只有一個點的 map 不寫 `tempopoint`：單一 tempo 的歌，檔案看起來和以前一樣。以小節設定的 loop 會標上 `ticks`。Tempo 以 17 位數字寫出，讀回來才會是同一個數。`tests/test_project_format.cpp` 會讀寫它，並拒絕不是 map 的內容：第一個點不在 0、tick 往回走、tempo 超出範圍、超過 256 個點。

### Tempo lane

Arrangement 上方的一條，用同一個時間軸：tempo 維持的地方是水平線，ramp 是斜線，每個點上有一個圓點。

| 手勢 | 作用 |
|---|---|
| 在空白處雙擊 | 在最近的拍子上新增一個點，tempo 取自點擊的高度 |
| 拖曳一個點 | 在它的鄰居之間移動它（Shift：任意 tick、任意 tempo）；第一個點留在 tick 0 |
| Alt + 點擊一個點 | 它的 tempo ramp 到下一個點，或恢復維持 |
| 右鍵點擊一個點 | 刪除它（第一個不行） |

每個手勢是一個 undo 步驟。和第 18 章的 piano roll 一樣，滑鼠處理函式只呼叫 `press()`、`dragTo()` 和 `release()`，smoke test 也呼叫它們。

有一件事是新的。時間軸以 sample 為單位，所以移動一個 tempo 點會移動畫面上它之後的每一拍——包括滑鼠底下的那個點。如果透過*目前的* map 來讀，每走一步滑鼠就會落在另一拍上，點會從滑鼠底下跑掉。所以拖曳時是透過**拖曳開始時的 map** 來讀滑鼠：點會去到當時滑鼠所在的那一拍。

### 帶著 tempo 的 MIDI 檔

第 18 章的練習 3 和 9 成為 app 的一部分。`MidiFile.h` 讀寫 byte（不需要 JUCE，每次建置都測試）；`MidiFileIO.h` 把專案變成歌、把歌變成 track；File > Import MIDI file 和 Export MIDI file 把它們放進選單。

- **匯出。** 每個 tempo 點是一個 tempo 事件。檔案沒有 ramp，所以 ramp 變成一個十六分音符一階的階梯——每一階的 tempo 讓這一階的長度正好等於 ramp 在這段的長度。用這一階中間的 tempo 是直覺的選擇，但從 120 到 60 BPM 一拍之後就差了 2 ms；正確的那個讓每個十六分音符都準時。
- **匯入。** Tempo 事件可能在任何 track、順序錯亂、同一個 tick 有兩個：會先排序，同一個 tick 留最後一個。它們變成 tempo map 的跳躍。檔案的 tempo 可能超過 map 的 256 個點；每 k 個留一個會讓歌漂移好幾秒，所以 `TempoThinning.h` 留下*最少*的點，讓任何 tick 的移動都不超過容許值——先用 0，這只會拿掉重複的 tempo，再用 0.5 ms、1 ms、2 ms……之中放得下的最小那個——status line 會說明用了哪一個（練習 5 和 9 就是自己寫出這個）。Track 和 tempo 是**一個** undo 步驟。

## 檢查

Smoke test 有兩個新的部分，另有三個測試不需要 JUCE：

```text
the tempo map
  120 then 60 BPM from bar 2: bar 3 at sample 288000, a note at bar 2 beat 2 sounds from 144000
  the lane: a point added and dragged, a point that stops at its neighbour, a ramp, a delete - each one undo step
MIDI files
  2 tracks and a map with a ramp out and in again: 6 tempo points, the ramp's sixteenths and bar 3 off by at most 0.0002 ms
  a file with 700 tempos in: 179 tempo points, the worst beat 3.99 ms off ("the file had more than the tempo map holds: no beat moved by more than 4.0 ms")
```

```text
l19_test_midi_file: Standard MIDI Files with tempo changes
  a song with three tempos: 90 bytes, exactly the expected ones, and read back
  tempos in two tracks, out of order, two at one tick, none at 0: 3 tempos in order
  1000 of 1000 random songs (tempos, names, every kind of message) came back the same
  14 of 14 broken files refused, 75 files cut short refused, 20000 damaged files: no crash (1338 still readable)
[PASS] l19_test_midi_file (10 checks passed)
```

```text
l19_test_tempo_thinning: the fewest tempo points within a timing tolerance
  400 of 400 random lists: as few points as the best of every choice (678 points dropped)
  700 tempos into 256 points: 179 points, the worst beat 3.99 ms off (within 4.0 ms); every k-th: 234 points, 222 ms off
  700 tempos into  64 points:  44 points, the worst beat 63.79 ms off (within 64.0 ms); every k-th:  64 points, 2557 ms off
  700 tempos into  16 points:  12 points, the worst beat 506.31 ms off (within 512.0 ms); every k-th:  16 points, 6067 ms off
  700 tempos into   2 points:   2 points, the worst beat 16888.37 ms off (within 32768.0 ms); every k-th:   2 points, 49216 ms off
[PASS] l19_test_tempo_thinning (49 checks passed)
```

## 你要自己試的

1. **畫一個 tempo。** 在 tempo lane 的第 3 小節雙擊，在歌播放時上下拖曳這個點：音符會跟著走，audio clip 不會。
2. **Ramp。** Alt + 點擊它前面的那個點，聽你的音符漸漸加速。
3. **MIDI 檔。** File > Import MIDI file，選網路上任何一個 `.mid`：它的 track，以及 lane 裡它的 tempo 變化。再匯出，用另一個程式打開。

## 你學到了什麼

- 只向一個地方要換算結果的 app，不用知道就能跟上新的 model。
- 以拍子計數的東西必須存成 tick：ruler、snap、以小節設定的 loop。
- 會改變自己所在座標軸的拖曳，要透過拖曳開始時的座標軸來讀滑鼠。
- Ramp 要變成保持時間的階梯，而不是看起來像 ramp 的階梯。
