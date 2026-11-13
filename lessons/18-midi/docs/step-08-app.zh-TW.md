# Step 8 - 有樂器的 app

[English](step-08-app.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 7 - 有樂器的 engine](step-07-engine.zh-TW.md)

## 目標

把本章帶進 DawLab：arrangement 裡的 instrument track、可以畫和移動的 MIDI clip、編輯音符的 piano roll、彈奏 synth 的鍵盤、有疊加和取代的 MIDI 錄音、quantize——全部都會存檔，全部都能 undo。

## 設計

### 先複製，再修改

和第 15 到 17 章一樣，這個 step 的第一個 commit 是第 17 章的 app，只把名字改成 DawLab 18，好讓之後的 commit 顯示樂器改了什麼。它們分成幾部分，每部分都先建置並測試過才進行下一部分：

1. model：session 裡的 instrument track 和 MIDI clip、兩個新的 edit、專案檔；
2. arrangement 和 piano roll；
3. 鍵盤和 MIDI 錄音；

中間還有一個 engine 的修正，是 app 找到的（見下文）。

### Clip 保留原本彈的

`MidiModel.h` 是 app 的 MIDI clip：engine 的 clip（音符上的視窗，Step 2），加上**原本彈的**音符以及它們如何被 quantize。Engine 拿到的是 quantize 後的音符——Step 6 的 `quantizeNotes()`——每次 clip 改變時重新計算：

```cpp
engine::MidiClip toEngine() const { ...; c.notes = quantizeNotes(played, quantize); ... }
```

所以 quantize 是 clip 的設定，而不是對音符的修改：選單可以再把它關掉、專案檔存的是原本彈的、第二次 quantize 不會和第一次疊加。Clip 的 id 是 engine 的播放器在編輯之間認得它的方式（Step 2）；session 負責發 id、絕不重複使用，而 clip 的編輯會保留它的 id。

### 兩個 edit，以及完整回來的 track

MIDI 的一切都能透過第 15 章那些 edit 旁新增的兩個 edit 來 undo：`SetMidiClips` 取代一個 track 的整串 clip（畫一個音符、移動一個 clip、一次錄音、一次 quantize——每個都是對這串清單的編輯），`SetInstrument` 把 track 變成樂器或變回來。Track 的 snapshot——刪除時為 undo 保留的東西——現在包含它的旗標和它的 clip，所以被刪除的 instrument track 回來時會帶著它的 synth 和它的 clip，而且 clip id 不變。

### 專案檔，格式 6

```text
instrument 17
midiclip 17 1 3840 15360 0 0 "Bass" 240 1 0
note 1 0 480 36 100 0
cc 1 0 64 127 0
```

全部都以 tick 表示。以另一個取樣率開啟的專案會轉換它的音訊位置（第 14 章），而音符不動：它們跟著 tempo，而不是跟著 sample。`tests/test_project_format.cpp` 會讀寫它，並拒絕每一行超出範圍的內容，超過 2^40 的 tick 也一樣。

### Arrangement 和 piano roll

Instrument track 的 lane 會顯示它的 MIDI clip，音符以橫槓畫在 quantize 後播放的位置。在空白處雙擊會建立一個一小節的 clip 並開啟它；拖曳移動 clip（一個 undo 步驟，對齊拍子）；雙擊在 **piano roll** 中開啟 clip；右鍵有選單：編輯、quantize（關、1/4、1/8、1/16、一半強度、swing）、mute、刪除。Track > Add instrument track（Ctrl+Alt+T）新增一個。

Piano roll 編輯的是原本彈的音符，並把 quantize 讓它們播放的位置畫成外框：點一下在 grid 上加入音符並拖出長度、拖曳音符移動它、拖曳它的尾端、按住 Alt 拖曳改 velocity、右鍵刪除。每個手勢是一個 undo 步驟：拖曳的每一步都帶著同一個 merge key，就像第 16 章的 automation 點一樣。

滑鼠處理函式只把事件轉成 `press()`、`dragTo()` 和 `release()`。Smoke test 直接呼叫這三個——畫、在時間和音高上移動、改長度、velocity、刪除，六個 undo 步驟回到空的 clip——所以 piano roll 不用滑鼠、也不用視窗就能測試。

### 鍵盤

Arm 一個 instrument track 會開啟 MIDI input，而不是音訊 input。Driver 在它自己的 thread 上 callback，帶著 JUCE 的訊息，已經解析好了（Step 5 的 parser 做的是同樣的事），並以 `Time::getMillisecondCounterHiRes()` 讀取的時鐘蓋上時間戳。`AudioHost` 把它推進**兩個** FIFO：

- audio thread 在每個 block 之前排空其中一個，而且是*排程*的（Step 5）：在它到達後固定的 delay——兩個 block 加上輸出 latency——所以每個音符都晚同樣的時間，而不是看 block 週期怎麼落；
- UI thread 讀另一個，用來錄音。

### 錄音：演奏者當時在哪裡？

Step 6 用 anchor 擺放音符：一個 block 何時被聽到、它算出哪個歌曲 sample、mix latency。Audio thread 每個 block 都透過 **seqlock** 發布一個（寫入時版本號是奇數；讀者會一直重試，直到讀到一個沒有變過的偶數版本），所以 UI thread 拿到的永遠是一致的一個 block。

Loop 帶來 Step 6 沒有的問題：UI 以 30 Hz 讀取，所以它可能永遠看不到錄音的第一個 block，而之後的 block 的歌曲位置已經繞回了。所以 anchor 也會說播放中已經算了多少個 sample——一個只會增加的計數，繞回時也一樣——而錄音會記下它開始時的這個計數：

```text
unwrapped = startSample + (anchor.renderedBefore - renderedAtStart) + (time - anchor.blockTime) x rate - mix
```

從錄音的任何一個 block 來算，這都是按鍵按下的位置，一直往下數而不繞回；Step 6 的 recorder 再把它繞回成 pass。停止時，每個 armed 的 instrument track 的 take 都變成一個涵蓋所錄範圍的新 clip：

- **Overdub（疊加）** 保留每一個 pass 和舊的 clip；
- **Replace（取代）**（Transport > MIDI Loop Replaces）抹掉之後的 pass 彈過的地方——在 take 自己較早的 pass 裡，以及在舊的 clip 裡。

所有 track 的 clip 在一個 undo 步驟「Record MIDI」中改變。Armed 的 instrument track 錄的是 MIDI，不是音訊：engine 現在只在 armed 的 audio track 上啟動音訊 Recorder，而兩種可以同時錄。

### App 找到的

Smoke test 的第一次執行找到了 Step 7 engine 的一個 bug：移除 track 時，audio thread 會清掉它的樂器旗標。但 UI 會立刻重用 slot，等到 audio thread 拿到移除指令時，UI 已經把這個 slot 交給一個新的 instrument track——結果移除指令把它變回了 audio track。新增 instrument track 後 undo 再 redo，得到一個無聲的 track；開啟專案時也一樣。這個旗標是 UI 的值，就像 fader 一樣：現在由 `Engine::addTrack()` 重設它，audio thread 從不碰它。Step 7 的文件說明了經過，它的測試也涵蓋了這個情況。

## 檢查

Smoke test（`DawLab 18.exe --smoke-test`）有三個新的部分：

- **Model：** instrument track 和 clip 都是 undo 步驟；synth 會播放 clip；quantize 改變 engine 播放的內容而不改變 clip 保留的內容；樂器關掉再打開；被刪除的 track 完整回來；存檔和開檔。
- **編輯：** 新增並拖曳 clip（一個 undo 步驟），arrangement 在它播放的位置找到它；piano roll 的手勢如上；從選單 quantize 和 mute；被刪除的 clip 的 piano roll。
- **錄鍵盤：** 一位演奏者用測試時鐘對著 offline 裝置彈鍵盤。四個音符——其中一個不在拍子上——被錄在精確的 tick 上；錄音時聽得到 synth；只有 instrument track 被 arm，所以不會產生音訊 take。一段在舊 clip 上錄 2.5 圈的 loop，兩種模式都測，還有 undo。

```text
instruments and MIDI clips: recording the keyboard
  4 notes played against a device in 512-sample blocks: recorded at ticks 0, 960, 1920, 3001
```

## 你要自己試的

CI 沒有鍵盤也沒有耳朵。這些要你來：

1. **彈。** 新增一個 instrument track（Ctrl+Alt+T），arm 它（標頭的 R），彈 MIDI 鍵盤：status line 會列出它找到的鍵盤。延遲應該感覺是固定的；buffer 大時會比較長，但仍然固定。
2. **畫。** 在 track 的 lane 裡雙擊，在 piano roll 裡畫音符，播放。
3. **錄。** 按 R，彈兩小節，再按 R。然後錄一段 loop，MIDI Loop Replaces 開和關各一次。
4. **Quantize。** 在 clip 上按右鍵，選 1/16，聽聽看，再選 Off。
5. **存檔再開啟**，然後把你做過的一切 undo。

## 你學到了什麼

- 保留原本彈的內容的 clip，可以 quantize、取消 quantize、存檔，都不會遺失。
- 兩個對整串清單的 edit 讓每個 MIDI 修改都能 undo，snapshot 讓 track 完整回來。
- 透過滑鼠處理函式所呼叫的函式來測試元件，就不需要滑鼠。
- Seqlock 不用鎖、也不會讀到一半，就能把幾個值從 audio thread 交給 UI。
- 只會增加的計數，能替只看到部分 block 的讀者把 loop 展開。
- App 是 engine 最好的測試：它找到了 engine 自己的測試沒找到的 slot 重用 bug。
