# Step 5 - 鍵盤

[English](step-05-keyboard.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 4 - 複音](step-04-polyphony.zh-TW.md) | 下一步：Step 6 - 錄 MIDI（即將推出）

## 目標

用鍵盤彈 synth：把 driver 送來的 byte 變成訊息，在 MIDI thread 和 audio thread 之間傳遞而不讓任何一方等待，並決定每個訊息在哪個 sample 播放——在 latency 和 jitter 之間取捨。

## 設計

### 是 byte，不是訊息

MIDI 連線——5 針的線，或假裝成那條線的 USB——傳的是一串 byte。Step 1 的三 byte 訊息是它們加起來的結果，但真實裝置有三個習慣會造成阻礙：

- **Running status。** 彈和弦的鍵盤會送出 `90 3C 64 40 64 43 64`：一個 status byte，然後三個音符。沒有 status 的資料 byte 沿用上一個 status。配合 velocity 為 0 的 note off（Step 1），這在 31250 baud 的線上省下三分之一的頻寬，每個鍵盤都這麼做。
- **Real-time byte 隨處可見。** Clock（`F8`）、start、stop 各只有一個 byte，而且可能出現在*另一個訊息的 byte 之間*：`90 3C F8 64` 是一個 note on，中間夾著一個 clock tick。它們不會改變周圍的訊息。
- **System exclusive。** `F0 ... F7` 是廠商自訂、任意長度的訊息——一個音色 dump 可以有好幾 KB。它的資料 byte 不是音符。

`MidiParser::feed()` 一次接收一個 byte，並在一個 channel 訊息完整時告知。它記著 running status、已收集的資料 byte 數，以及是否有 SysEx 開著。Status byte——除了 real time 以外的任何一個——都會重新開始：它結束沒完成的訊息，甚至結束一個沒等到 `F7` 的 SysEx。System common 訊息（`F1`-`F6`）會結束 running status，這是 MIDI 規格的規定，而沒有 status 可歸屬的資料 byte 會被跳過並計數。

### 從 MIDI thread 到 audio thread

Byte 到達時，driver 會在它自己的 thread 上 callback。那個 thread 不能等 audio thread（按鍵會被延遲），而 audio thread 完全不能等。所以這又是第 17 章的 ring，只是裝的是訊息：一個大小固定的單一生產者、單一消費者 FIFO，有兩個只會增加的計數器 `written` 和 `read`。

- `push()` 把項目寫進 `written & (N - 1)` 這個格子，*然後*用 **release** 存入 `written + 1`。
- `pop()` 用 **acquire** 讀取 `written`，複製項目，*然後*用 **release** 存入 `read + 1`。

Release/acquire 這兩組配對就是整個設計：消費者絕不會看到比它所描述的項目還超前的 `written`，生產者也絕不會重用消費者還在複製的格子。在 x86 上處理器本來就會替這些 store 排好順序，所以用 `relaxed` 的版本在 PC 上能通過每個功能測試。ThreadSanitizer 模擬的是 C++ 記憶體模型而不是處理器，它會把這兩個錯誤都回報出來——這就是 CI 在 TSan 下執行這個 step 的原因，也是檢查會跑一個真正的生產者和消費者的原因：100000 個訊息必須完整且依序到達，而一個卡住的消費者所遺失的訊息必須全部被計數。

FIFO 滿的時候，`push()` 會丟掉訊息並計數。你無法叫鍵盤等一下。

### 每個訊息都有時間

Driver 會為每個訊息蓋上到達的時間，用的是 audio callback 也讀得到的時鐘（JUCE：`Time::getMillisecondCounterHiRes()`）。Audio callback 會提前算好第 `k` 個 block，而它的第一個 sample 會在一個已知的時間被聽到，`blockTime`。這兩個時鐘之間，就是這個 step 的選擇。

**盡快（as soon as possible）。** 把 FIFO 裡的東西都拿出來，放在正在算的 block 的 offset 0 播放。剛好在 callback 執行前按下的鍵，會在一個 block 加上輸出 latency 之後被聽到；剛好在 callback 之後按下的鍵，要等下一次 callback。延遲取決於鍵在 *block 週期中的哪個時間點*按下：**jitter**。

**排程（scheduled）。** 每個訊息都在它到達後固定的 `delay` 播放，位置在

```text
offset = floor((time + delay - blockTime) x sampleRate)
```

到期時間在 block 結束之後的訊息，留在 FIFO 給下一個 block（`peek()` 只看不拿）。這個 delay 必須涵蓋最壞的情況：一個訊息最多在它的 block 被聽到之前兩個 block 週期被看到，再加上輸出 latency。這樣一來，每個音符都剛好在按鍵後 `delay` 被聽到，精確到 sample。來得太晚、趕不上到期時間的訊息（delay 設太短、callback 執行得非常晚）會放在 offset 0 播放並計數。

自我檢查會對一個 callback 不準時（最多晚半個 block）的模擬裝置彈 20 秒的按鍵，並為每個音符量測它在按鍵後多久被聽到：

<div align="center">

| 48 kHz 下 512 個 sample | 按鍵後被聽到 | jitter |
|---|---|---|
| 盡快 | 11.0 到 25.6 ms | 14.6 ms |
| 排程（delay 26.3 ms） | 26.3 ms | 0.02 ms（一個 sample） |

</div>

15 毫秒的 jitter 在打擊墊或快速樂段上聽得出來；大多數演奏者寧可要固定的 26 ms——他們會不自覺地補償——也不要飄忽不定的延遲。用 64 個 sample 的 block，排程的 delay 會降到 8 ms 以下。大多數 DAW 採用排程；有些提供不排程的「低 latency」設定。

## 換你了

在 `steps/step05-keyboard/starter/step05.cpp` 寫這五個函式：

1. `MidiParser::feed()`：real time、system、status、資料——依這個順序。
2. `MidiFifo::push()`：滿了沒；先項目，再用 release 存 `written`。
3. `MidiFifo::pop()`：空了沒（acquire）；先複製，再用 release 存 `read`。
4. `MidiFifo::peek()`：和 `pop()` 一樣，但不拿走。
5. `drain()`：全部放在 offset 0，或排程的 offset、block 的結尾、遲到的訊息。

完成後，輸出的結尾會像這樣（卡住的消費者那幾個數字每次執行都不同）：

```text
Step 5: a keyboard
  two threads: 100000 of 100000 messages in order; a stalled consumer: 7708 received, 8292 dropped, 0 out of order
  512-sample blocks, as soon as possible: heard 11.0 to 25.6 ms after the key (14.6 ms of jitter)
  512-sample blocks, scheduled:           heard 26.3 to 26.3 ms after the key (0.02 ms of jitter)
[PASS] l18_step05 (31 checks passed)
```

## 你學到了什麼

- MIDI 串流有 running status、夾在訊息中間的 real-time byte，以及任意長度的 SysEx。
- Status byte 會重新開始；system common 會結束 running status。
- SPSC FIFO 需要在發布資料的 store 上用 release、在讀取的 load 上用 acquire；x86 會掩蓋錯誤，ThreadSanitizer 不會。
- 「盡快」比較早但有 jitter；用固定 delay 排程比較晚但精確。
- Delay 必須涵蓋最壞情況，否則訊息會遲到。
