# Step 6 - 錄 MIDI

[English](step-06-recording.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 5 - 鍵盤](step-05-keyboard.zh-TW.md) | 下一步：Step 7 - 有樂器的 engine（即將推出）

## 目標

把鍵盤上彈的東西錄進 clip：每個音符都在它被彈奏的 tick 上，loop 一圈又一圈地錄，有兩種模式，還能 quantize——對齊 grid、依強度、加上 swing——而且永遠不會失去原本彈的內容。

## 設計

### 演奏者當時在哪裡？

演奏者是跟著他*聽到*的東西按鍵。第 17 章把錄下的音訊 take 放在

```text
P - (L_in + L_out + L_mix)
```

因為聲音也要經過 input 才回來，所以晚了。MIDI 訊息幾乎沒有 input latency：Step 5 的 driver 在訊息*到達*時就蓋上時間戳——在 USB 鍵盤上，是按鍵後一毫秒。所以只有歌曲和演奏者耳朵之間的東西才算數，而 audio thread 對每個 block 都有這兩個數字：

- `blockTime`：這個 block 的第一個 sample 會被聽到的時間（Step 5）——裝置的輸出 latency 已包含在內；
- `mixLatency`：plugin delay compensation（第 14 章）——在歌曲 sample `S` 算出來的東西，要晚這麼多才離開 mix。

所以演奏者在時間 `t` 聽到的歌曲 sample 是

```text
songSample + (t - blockTime) x sampleRate - mixLatency
```

而且可以用同一段連續播放中的*任何* block 來算：歌曲以取樣率前進，而時間戳和 `blockTime` 共用的是同一個時鐘。離那個 sample 最近的 tick（`tickAt`）就是音符要放的位置。自我檢查會完全跟著聽到的東西彈 200 個音符，經過 12.3 ms 的輸出 latency 和 752 個 sample 的 mix latency（第 14 章的 demo 歌曲），從任意的 block 去量，並要求每個音符都在它的 tick 上。

Step 5 的 thru 延遲在這裡完全不參與。排程的 thru 讓演奏者晚 26 ms 才聽到*自己的*音符，但音符的位置是依按鍵按下的時間，而不是它的聲音出來的時間。

### Recorder

`NoteRecorder` 接收按鍵按下和放開及它們的 tick，並把它們配對成音符。鍵盤可能讓它意外：

- velocity 為 0 的 note on 是放開（Step 1）；
- 一個仍按著的鍵又來一個 note on，第一個音符就在那裡結束；
- 沒有按著的鍵（在錄音開始前就按下了）的放開會被忽略；
- 停止錄音時仍按著的鍵在那裡結束。

音高*和* channel 一起決定一個鍵：分割的鍵盤會在兩個 channel 上送出同一個音符編號。

### Loop：pass、疊加與取代

Loop 錄音會一再播放同樣的小節，同時演奏者在錄音。Recorder 收到的是*未繞回*的 tick——演奏時的歌曲時間，會一直超過 loop 的結尾——由它自己繞回：pass 0 從錄音開始的地方到 loop 結尾，pass 1 是第一次完整繞一圈，依此類推。按著跨過 loop 結尾的音符屬於它開始的那個 pass，並在 loop 結尾結束。錄音停止時，recorder 知道每個 pass *涵蓋*了哪些範圍：最後一個 pass 可能只涵蓋 loop 的一部分。

Clip 留下什麼由錄音模式決定：

- **Overdub（疊加）** 全部保留：clip 原本的音符和每一個 pass。一圈一圈地疊出鼓的 pattern——先大鼓，再小鼓，再 hi-hat。
- **Replace（取代）** 讓每個 pass 抹掉它彈過的地方。一個音符會留下，除非有*之後的* pass 涵蓋了它的起點：clip 的舊音符（當作 pass -1）除非任何 pass 涵蓋了它，pass `p` 的音符除非有 `p` 之後的 pass 涵蓋了它。一直彈一個樂句直到對了再停下；每個地方留下的就是在那裡的最後一次嘗試。停在半途的最後一個 pass 只會抹掉它彈過的那一半。

沒有 loop 時，同樣的規則讓一般的 take 取代從開始錄音到停止之間的音符，其他都不動。

### Quantize，並保留原本彈的

Quantize 把音符往 grid 移：

```text
n      = start / grid，四捨五入到最近的整數              最近的 grid 線
target = n x grid，若 n 是奇數再加上 swing x grid        swing 延後每隔一條的線
start  = start + strength x (target - start)              strength 0..1：往那裡移多少
```

240 個 tick 的 grid 是十六分音符。Strength 1 把一切放到 grid 上；0.5 把每段距離減半，保留一些手感。Swing 1/3 把每隔一個十六分音符延後三分之一個十六分音符：很多律動中的三連音 shuffle。

最重要的性質不在公式裡：`quantize()` 是*原本彈的音符的函式*。Clip 保留原本彈的內容，quantize 後的音符是從它算出來的——所以 strength、grid 和 swing 可以隨時改、改幾次都行，而「取消 quantize」就是 strength 0。覆寫起點的破壞性 quantize 永遠回不到原本的時間點，連做兩次 quantize 還會疊加。第 16 章的 automation thinning 為了同樣的理由做了同樣的選擇。

## 換你了

在 `steps/step06-recording/starter/step06.cpp` 寫這七個函式：

1. `songSampleAt()`：anchor、時間、mix latency。
2. `tickAt()`：最近的 tick。
3. `NoteRecorder::noteOn()`：velocity 0、按兩次的鍵、新開的音符。
4. `NoteRecorder::noteOff()`：開著的音符、它的 pass、它在 pass 內的結尾。
5. `NoteRecorder::stop()`：關閉按著的鍵，再算每個 pass 涵蓋的範圍。
6. `mergeTake()`：疊加，或依之後的涵蓋範圍取代。
7. `quantize()`：最近的線、swing、strength，從原本彈的音符算。

完成後，輸出的結尾會像這樣：

```text
Step 6: recording MIDI
  200 notes played in time with what was heard (latency 12.3 ms, mix 752 samples): 0 off their tick
[PASS] l18_step06 (39 checks passed)
```

## 你學到了什麼

- MIDI 音符依按鍵按下的時間擺放：輸出和 mix latency 算數，input latency 不算。
- 任何 block 的 anchor 都能把時間對應到歌曲的 sample。
- Recorder 把按鍵配對成音符，而且撐得過送出怪東西的鍵盤。
- Loop 錄音會錄下多個 pass；疊加全部保留，取代保留每個地方最後涵蓋它的那一個。
- Quantize 從原本彈的音符算出來，所以隨時可以改或取消。
