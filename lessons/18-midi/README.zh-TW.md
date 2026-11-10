# 第 18 章：MIDI - 音樂時間裡的音符、樂器與鍵盤

[English](README.md) | **繁體中文**

> **狀態：完成。** 全部 8 個 step。用真的鍵盤彈奏和錄音仍然要親手試過：見 Step 8 的「你要自己試的」。

DawLab 17（第 17 章）會錄音也會播放音訊：已經存在的聲音，以 sample 擺放。DAW 也會播放*音符*——給樂器的指令，以拍子擺放，只有在播放時才變成聲音——並從鍵盤把它們錄下來。音符有它自己的問題：沒有結束的音符會永遠響下去、樂器的 voice 數量有限、鍵盤從另一個 thread 送出事件而且有自己的時間觀，而錄下的演奏常常希望比彈的時候更整齊一點。

## 如何使用本章

和第 15 到 17 章一樣，每個 step 都是 `steps/stepNN-*/` 裡一個小而完整的 snapshot：你在 `starter/` 裡寫出該 step 的函式，self-check 會告訴你它們何時能正常運作。請見 [steps/README.zh-TW.md](steps/README.zh-TW.md)。

```powershell
.\scripts\build.ps1 -Only 18-midi          # no JUCE needed
.\build\bin\Debug\l18_step01_starter.exe
```

在 Linux / macOS：`./scripts/build.sh --only 18-midi`，然後執行 `./build/bin/l18_step01_starter`。

## 1. 學習目標

完成本章後，你可以

- 表示 MIDI 訊息，把音符放在音樂時間裡，並在精確的 sample 上播放每個事件；
- 播放 MIDI clip 經過 loop、seek 和停止時，不留下任何卡住的音；
- 做出一個複音樂器：沒有 aliasing 的 oscillator、envelope、voice 與 voice stealing；
- 把鍵盤的事件從它的 thread 送到 audio thread，並在 latency 和 jitter 之間做取捨；
- 把演奏錄在彈奏的位置，並在不失去原始演奏的情況下 quantize。

## 2. 各個 step

<div align="center">

| Step | 文件 | 新增的內容 | 狀態 |
|---|---|---|---|
| 1 | [MIDI 事件與音樂時間](docs/step-01-midi-time.zh-TW.md) | 三個 byte 的訊息、存在 tick 裡的音符、block 邊界的精確反函數、note off 在前、在每個事件處切開的樂器 | 完成 |
| 2 | [播放 MIDI clip](docs/step-02-midi-clips.zh-TW.md) | clip 是音符上的視窗；記下啟動過的音符，停止、seek、繞回、mute、刪除後都不留卡住的音；跳躍後 chase controller | 完成 |
| 3 | [一個 voice](docs/step-03-one-voice.zh-TW.md) | 用 PolyBLEP 的鋸齒波並量測它的 alias；絕不跳躍的 ADSR；filter、velocity；加進混音的 voice | 完成 |
| 4 | [複音](docs/step-04-polyphony.zh-TW.md) | voice 池、同一個音回到自己的 voice、不爆音地搶最不會被想念的 voice、延音踏板、pitch bend、all notes off | 完成 |
| 5 | [鍵盤](docs/step-05-keyboard.zh-TW.md) | byte parser（running status、real time、SysEx）、來自 MIDI thread 的 SPSC FIFO、把時間戳換成 sample offset：jitter 或固定延遲；ThreadSanitizer | 完成 |
| 6 | [錄 MIDI](docs/step-06-recording.zh-TW.md) | 依按鍵按下的時間擺放音符（輸出與 mix latency，沒有 input latency）、loop 的 pass、疊加或取代、有 strength 和 swing 且保留原始演奏的 quantize | 完成 |
| 7 | [有樂器的 engine](docs/step-07-engine.zh-TW.md) | 第 17 章 engine 的複本：synth 在 insert 之前加入 track 的 instrument track、經 command queue 送入的 MIDI clip、給 armed track 的即時 MIDI；ThreadSanitizer | 完成 |
| 8 | [有樂器的 app](docs/step-08-app.zh-TW.md) | 第 17 章 app 的複本：instrument track、MIDI clip、piano roll、鍵盤輸入、錄音、quantize、存檔、undo | 完成 |

</div>

## 3. 本章的起點

DawLab 17 裡沒有任何東西認得音符。它的 track 播放 audio clip，clip 是 sample，而它的 `Transport` 計數 tick（一個四分音符 960 個）只是為了顯示小節和拍子。Step 1 從音符是什麼開始。

## 4. 練習

見 [exercises/README.zh-TW.md](exercises/README.zh-TW.md)，解答在 [solutions/README.zh-TW.md](solutions/README.zh-TW.md)。
共七題，都不需要 JUCE：音符為什麼需要這麼小心（概念）、用數字看 MIDI（計算）、讀寫 Standard MIDI File、tempo map、arpeggiator、有 legato 和 glide 的 mono synth，以及 track 上的 VST3 樂器（設計）。另有五個選做專題延伸 DawLab 18（需要 `-Juce`）：synth 設定面板、MIDI 檔的匯入匯出、把電腦鍵盤當 MIDI 鍵盤、一次編輯很多音符，以及 track 上的 VST3 樂器。

## 5. 下一步

從 [Step 1 - MIDI 事件與音樂時間](docs/step-01-midi-time.zh-TW.md) 開始。讀完本章後，接著讀[第 19 章 - 樂器與 tempo](../19-instruments-tempo/README.zh-TW.md)。
