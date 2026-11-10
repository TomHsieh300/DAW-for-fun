# Step 6 - App：樂器

[English](step-06-app-instruments.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 5 - App：tempo](step-05-app-tempo.zh-TW.md)

## 目標

讓 track 的樂器成為專案的一部分：一個 undo 和專案檔都認得的內建 synth 音色面板，以及取代 synth 的 VST3 樂器——可以載入、存檔、undo，並和其他東西一樣有延遲補償。

## 設計

### 一個 snapshot，一個 edit

Track 用來播放音符的東西有兩樣：內建 synth 的設定，以及——如果有東西取代了 synth——一個 plugin，以它的檔案和它自己的狀態表示。`InstrumentSnapshot` 把兩者都當成值，就像第 15 章的 `InsertSnapshot` 把 effect 當成值。一個 edit `SetInstrumentSound` 取代 track 的 snapshot 並傳回舊的；track 自己的 snapshot（刪除它時保留的東西）也包含它，所以被刪除的 track 回來時會帶著它的音色。

`Session::setInstrumentSound()` 負責執行：

- synth 的設定立刻送到 engine（第 18 章那個 hook：atomic，下一個 block 取用）；
- **另一個 plugin 檔案**會建立那個 plugin 並交給 `Engine::setTrackInstrument()`；空的檔案把 synth 放回去，舊的 plugin 以 garbage 的形式回來；
- **同一個檔案**只套用狀態：undo plugin 內部的改變不會重新建立它。

如果 plugin 建立不起來，什麼都不變，這個 edit 也不套用——這是 History 從第 15 章以來的規則。

Plugin 取代 synth 的期間，synth 的設定會保留。拿掉 plugin，track 的聲音就和原來一樣。

### Synth 面板

給選取的 track 用的七個 slider（Track > Synth settings）。移動其中一個，會在 synth 播放時改變聲音——engine 不會停，audio thread 上也不配置記憶體。有三條規則，每一條都來自前面的章節：

- **一次拖曳是一個 undo 步驟。** 拖曳的每一步都帶著同一個 merge key（第 16 章的 automation 點）。
- **顯示不是修改。** Session 改變時——undo、選了另一個 track——slider 會在不發通知的情況下被設定。如果發了通知，面板會把值當成新的 edit 送回去：打開面板就會讓專案變成「已修改」，undo 之後還會跟著它自己的 redo。
- **面板每次都用 id 找它的 track。** 有 plugin 的 track、audio track、被刪除的 track：slider 會關掉，面板會說明原因。

### 把 plugin 當成樂器

`PluginInstrument` 是 app 裡的第 18 章練習 12：一個 `juce::AudioProcessor`——第 13 章的 host 為 `.vst3` 給的東西——放在 engine 的 `Instrument` 介面後面。Block 的事件依 offset 放進 `MidiBuffer`，processor 的輸出加到 track 上。Step 3 在這裡派上用場：wrapper 回報 processor 的 **latency**，track 的 latency 包含它，延遲補償把其他每個 track 和它對齊。載入 plugin 時 status line 會說明。

只接受能當樂器的東西：不收 MIDI 或沒有輸出的 plugin 會被拒絕並說明原因（smoke test 裡，第 13 章的 compressor 就是）。

沒做的：plugin 自己的 editor 視窗（insert 的有顯示，第 13 章），以及匯出時歌曲結尾之後 plugin 的尾音。

### 專案檔

```text
synth 17 300 0.9 0.005 0.2 0.6 0.3 1
plugininstrument 17 "C:/Plugins/Pianos.vst3" "<base64>"
```

仍然是格式 7：本章的格式。只有不是預設音色時才寫 `synth` 行，所以從沒打開過面板的專案讀起來和以前一樣。在沒有安裝那個 plugin 的地方開啟專案不是錯誤：track 會用它自己的設定播放內建 synth，使用者會得到一個指名該 plugin 的警告。

### 測試可以替換的 factory

Session 不認識第 13 章的 host。它有一個函式 `createPluginInstrument`，由 `MainComponent` 設成「載入檔案、確認它是樂器、包起來」——就像第 15 章為 VST3 insert 做的那樣。不能假設跑 CI 的機器上有 VST3 樂器，所以 smoke test 設了它自己的：一個 latency 為 256 sample、狀態就是它的 gain 的小 processor。Factory 之後的一切——edit、undo、track 的 snapshot、專案檔、補償——都是真正的程式碼。

## 檢查

```text
instruments: the synth's sound and plugin instruments
  the synth panel: a drag is one undo step, the engine follows, a deleted track comes back with its sound
  a plugin in the synth's place: 1 note on, mix latency 240 -> 496, the built-in synth silent
  its state saved, undone, redone, deleted with its track and opened again: gain 0.5 each time (6 plugins built);
    where it is not installed: "track "Synth 9": plugin instrument Test Synth.vst3 not loaded (no such plugin); it plays the built-in synth"
```

`tests/test_project_format.cpp` 會讀寫這兩種新的行，並拒絕超出範圍、重複，或 track 不存在的內容。

## 你要自己試的

CI 沒有 VST3 樂器，也沒有耳朵。

1. **面板。** 新增一個 instrument track，畫幾個音，播放，然後打開 Track > Synth settings。在播放時轉動 cutoff 和 release。Undo。
2. **VST3 樂器。** Track > Load VST3 instrument，用你手上任何一個樂器 plugin。播放同樣的音；arm 這個 track 並彈鍵盤。看 status line 上的 latency，和內建 synth 比較。
3. **存檔、關閉、開啟。** Plugin 和你在它裡面設定的東西應該都回來了。

## 你學到了什麼

- 可以是兩種東西的東西，是一個 snapshot 和一個 edit；沒在用的那個要保留，不要丟掉。
- 顯示一個值和修改它必須分清楚，否則每次顯示都會變成一次 edit。
- 藏在函式後面的相依，是測試可以代替的位置。
- 缺少的 plugin 是一個警告和一個能用的 track，不是錯誤。
