# Step 7 - 本章的結尾

[English](step-07-the-end.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 6 - App](step-06-app.zh-TW.md)

這個 step 沒有程式碼。它說明本章做了什麼、每一塊放在哪裡，以及——同樣直白地——留下了什麼沒做。

## 做了什麼

第 19 章結束時，歌裡的音符會跟著 tempo，audio 不會。現在 tempo 改變時，clip 可以做四件事之一：

<div align="center">

| Clip…… | 什麼跟著走 | 音高 | 代價 | 適合 |
|---|---|---|---|---|
| 留在原地 | 沒有 | 和錄的一樣 | 沒有 | tempo 固定時的任何東西 |
| 起點留在拍子上（Step 2） | 它的起點 | 和錄的一樣 | 沒有；切開的 take 會分開或重疊 | cymbal、口白、音效 |
| 用 varispeed 跟著（Step 3） | 裡面的每樣東西 | 隨速度移動：速度的 $`12 \log_2`$ 個半音 | 事先運算一次 | 鼓，以及小幅度的 tempo 變化 |
| 用 stretch 跟著（Step 4） | 裡面的每樣東西 | 不變（一個 cent 以內） | 事先運算一次；單一的敲擊會變兩下或消失 | 有音高、沒有尖銳敲擊的東西 |

</div>

底下只有一個想法，就是第 18 章和第 19 章的那一個：**以拍子計數的東西要存成 tick**。音符從一開始就是 tick。Loop 的邊界在第 19 章變成 tick。在這裡，clip 的起點變成了 tick，然後它的 audio 的每個 sample 也透過幾個 warp marker 得到了自己的 tick。

## 每一塊在哪裡

<div align="center">

| 想法 | Step | 在 engine 裡 | 在 app 裡 |
|---|---|---|---|
| warp marker、速度 | 1 | `Warp.h`：`WarpMap`、`markersForLoop()`、`markersForTake()` | - |
| tick 才是事實 | 2 | `Warp.h`：`placeUnder()`、`movedTo()` | `Session::setClips()`、`setTempoMap()` |
| 讀 sample 之間的值 | 3 | `Warp.h`：`readSinc()` | - |
| WSOLA | 4 | `Warp.h`：`stretchAlong()` | - |
| 事先運算 | 5 | `Warp.h`：`renderFollowClip()`；`FollowRenderer.h` | `Session::pushClips()`、`pollRenders()`、`finishRenders()` |
| 由 clip 帶著資料 | 6 | `Clip.h`：`Clip::follow`；`Warp.h`：`ClipFollow`、`withFollowMode()`、`cutToSongRange()` | clip 選單、專案檔第 8、9 版 |

</div>

Audio thread 沒有得到任何新工作。`Track.h`、`Transport.h`、`Mixer.h` 和 `LatencyCompensator.h` 是第 19 章的檔案，一個 byte 都沒變（`scripts/check_copies.py` 每次 push 都會確認），而 `Clip.h` 只多了一個 audio thread 不讀的 member。本章的一切都在 audio thread 看到 clip 之前就做完了。

## 量出來的數字

- 在只有跳躍的 tempo map 上錄的 take，每個 tempo 點放一個 marker 就是**精確的**；四小節內從 60 到 120 BPM 的 ramp，要維持在 1 ms 內需要 46 個 marker（Step 1）。
- 每次 tempo 改變都從取整過的 sample 重新找 tick 的 clip，改 1000 次之後偏了 15 個 sample；存著 tick 的 clip 偏 0 個（Step 2）。
- 用速度累加出來的位置，經過十分鐘的 ramp 之後偏了 31 ms；從歌自己的 sample 算出來的偏 0.003 個 sample（Step 3）。
- 在 sample 之間讀一個 5 kHz 的音，用直線留下比它低 35 dB 的誤差，用三次曲線是 49 dB，用 windowed sinc 是 100 dB；用兩倍速讀 15 kHz 會留下 -3 dB 的 alias，用會變窄的 sinc 則是 -78 dB（Step 3）。
- WSOLA 把單音維持在 0.72 cent 和 0.01 dB 以內，音和其餘部分之間有 31 到 40 dB；8 個 click 在一半速度時全部被聽到兩次，在兩倍速度時掉了 4 個（Step 4）。
- 23 個互相取代的運算要求，只開始了 4 個（Step 5）。

每一項都是由你可以自己執行的檢查印出來的。

## 留下了什麼沒做

各自寫在所屬的 step 裡，這裡再集中說一次：

- **沒有處理 transient。** WSOLA 不知道鼓擊是什麼。知道的 stretcher——或是在頻譜上處理的那種——是另一個長得多的程式。
- **小段只能對準到半個 sample。** Stretch 後的單音那 31 到 40 dB 就是這樣來的；對準到 sample 的小數位置可以再壓低。
- **沒有 beat detection，也沒有可以拖曳的 marker。** 打開「跟著」時，clip 播放的樣子就被當成是對拍的。*沒有*對拍演奏的 audio，沒辦法變成對拍。
- **錄音不會讓 take 自動跟著 tempo。** 要事後打開，而且要在 tempo 改變之前。
- **不是邊聽邊算。** Tempo 的改變會馬上以 placeholder 聽到——clip 的起點在拍子上、速度是舊的——運算完成後才正確。
- **運算結果是 cache。** 它們不在專案檔裡，匯出時會等它們算完。
- **Seek 仍然會有 click。** 第 14 章的 engine 在播放頭跳動時不做 fade，而播放中改變 tempo 對每個 audio clip 來說就是這樣的跳動（Step 2）。
- **只有一個拍號。** 和第 19 章一樣，一首歌裡的拍號不會改變。

## 你現在會做的事

- 說明 audio 屬於音樂的哪個位置，並從兩個互不認識的 map 算出該播什麼、何時播；
- 在起點、磁帶式、stretch 三種跟著 tempo 的方式之間做選擇，並說出每一種會對某個聲音做什麼；
- 用三種價格讀 sample 小數位置的 audio，並在讀得比較快時擋住 aliasing；
- 做出保持音高的 time-stretcher，並量出它破壞了什麼；
- 把沉重的工作從 audio thread 移到事先運算再交接的地方，並用 generation 處理過期的結果；
- 在 model 是單純 list 的 app 裡加功能：讓 list 的元素自己帶著它。

## 接下來

[練習](../exercises/README.zh-TW.md)處理了其中一部分。各個 step 留下了很明顯的題目：ramp 用最少的 marker（把第 19 章的練習 5 用在 Step 1）、Step 4 對準到 sample 的小數位置、tempo 改變時的 fade，以及用 take 錄音當時的 map 做 marker。
