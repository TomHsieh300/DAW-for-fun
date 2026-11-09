# 第 17 章 - 練習
[English](README.md) | **繁體中文**

[回到本章](../README.zh-TW.md) | [解答](../solutions/README.zh-TW.md)

難度：★☆☆ 簡單、★★☆ 中等、★★★ 困難。實作題在 `exercises/starter/` 有一個可以編譯、可以執行的 starter 檔；在你完成之前，它的自我檢查會印出 `FAIL`（這是預期的）。方括號裡是 build target 的名稱。這些練習都不需要 JUCE：

```powershell
.\scripts\build.ps1 -Only 17-recording
.\build\bin\Debug\l17_ex3_starter.exe
```

練習 3 和 6 建立在 Step 4 的 model 上（`exercises/model.h`：Step 4 的 `minidaw.h`，函式都已經寫好）。練習 4 和 5 是獨立的。請先做完這些 step。

---

## 練習 1 - 為什麼要這麼小心？ ★☆☆（概念）

每題用幾句話回答。

1. 為什麼 audio thread 不能自己把 take 寫進檔案，即使是大多數時候只是把資料複製到 buffer 的 `fwrite` 也不行？
2. Step 1 讓一個 block 整塊進 ring，否則就完全不進；放不下的 block 在 take 裡會變成等長的靜音。為什麼不把放得下的部分先放進去？為什麼是靜音，而不是什麼都沒有？
3. Step 2 的 `stop()` 不會讓 recorder 變成 Idle；只有 audio thread 會。如果 UI 自己設成 Idle 然後去讀 ring，會出什麼錯？
4. Step 3 在錄音時讓 WAV 檔頭的大小保持 0，並把檔案命名為 `.recording`。當機之後，這兩件事各帶來什麼？為什麼每秒 sync 到磁碟一次，而不是每次寫入都 sync？
5. Step 4 不在 audio thread 上於 punch-in 處切開 take，雖然 audio thread 準確知道 transport 何時到達那裡。為什麼那樣切會切錯地方？
6. Step 5 的 feedback guard 會把 monitor 靜音，並一直靜音到使用者重設為止。為什麼不稍微調小聲就好？嘯叫消失之後為什麼不自動取消靜音？

## 練習 2 - 用數字看錄音 ★★☆（計算）

先手算（寫出公式），再執行 [`l17_ex2_solution`] 核對。全部都是 48 kHz。

- (a) 第一個 input block 到達時 transport 在 2 秒處。Driver 回報 input latency 480 samples、output 480，mix 有 240。Take 在 timeline 上從哪裡開始？Step 7 的筆電量到的真實 round trip 約 380 ms：它的 take 會偏多少，以 sample、ms 和 120 BPM 的拍數計？從 0 開始錄的 take，它最前面的 frame 會怎樣？
- (b) 32-bit float 的立體聲 take：每秒多少 bytes、每分鐘多少 MB、每小時多少 GB？`data` chunk 的大小只有 32 bits（`kMaxDataBytes`）：一個 take 最長可以多長？
- (c) 磁碟停頓 250 ms，同時 audio thread 推送 512 frames 的 block。Ring 至少要容納多少 frame 才不會遺失任何東西？立體聲時是多少記憶體？DawLab 17 的 10 秒 ring 呢？
- (d) 使用 Step 5 的直接 monitoring，以及經過 insert 時，樂手聽到自己的聲音各晚幾 ms？Input 和 output 的 buffer 都是 64 samples 時呢？
- (e) 搭配練習 3：punch 在 10 秒處、從 0 開始錄、preroll 1 秒、單聲道 take。有多少 frame 從未寫入？多少 MB？

## 練習 3 - 只寫 take 用得到的部分 ★★☆（實作）

[`l17_ex3_starter`，檔案 `exercises/starter/ex3_preroll.cpp`]

Step 4 從 transport 開始播放的那一刻起寫入所有東西，它的文件也留下一個問題：punch 在第 41 小節、transport 從第 1 小節開始，會寫入 80 秒沒有任何 take 會用到的內容。在 audio thread 上、進入 ring 之前就丟掉它們——但在第一個 take 之前保留一段 *preroll*，讓使用者還能移動它的左邊緣。

- `prerollFor(stamped, latencies, range, preroll)`：stream 最前面要丟掉幾個 frame，以及第一個保留下來的 frame 的 stamped position。丟掉 frame 等於 stream 晚一點才開始，所以 Step 4 的 `takesOf()` 保持不變——只是在 loop 裡有一個陷阱。
- `StreamGate`：在 audio thread 上逐個 block 丟掉它們。

自我檢查要求在 20 000 次隨機錄音（loop、punch、兩者都有、兩者都沒有、負的 trim）下，縮短後的 stream 產生完全相同的 take、每個都保有 preroll，並且不論 block 大小，gate 都剛好丟掉 `skip` 個 frame。

## 練習 4 - 量測 latency，也要知道何時不能信 ★★☆（實作）

[`l17_ex4_starter`，檔案 `exercises/starter/ex4_measure_latency.cpp`]

Step 2 的 `measureRoundTrip()` 用第一個大聲的 frame 找出一個 click——用線路接時沒問題。透過空氣就不行了：Step 7 的 device test 發現一台筆電，它的 latency 在同一個 take 裡會跳動幾十毫秒。寫出那個測試的核心：

- 對每個在已知位置播放的 chirp，在 $`[-early, +late]`$ 內找出 chirp 和錄音之間 $`|\text{correlation}|`$ 最大的 lag；
- 當那個峰值超過 median 的 8 倍時算是*聽到*；
- 聽到的 chirp 的 lag 中位數（要設定的 trim）、它們的分散程度，以及一個判斷：只有在聽到至少 3/4 的 chirp、而且它們彼此相差不超過 1 ms 時才可信。

自我檢查會模擬線路、有回聲的安靜房間、回報太多的 driver、反相的麥克風、遺失的 chirp、在 chirp 旁邊甩上的門（Step 2 的方法會找到門）、Step 7 的筆電、被靜音的麥克風、八個 chirp 只聽到三、五、六個，以及相差 48 和 49 samples 的情況。

## 練習 5 - 告訴使用者 input 出了什麼問題 ★★☆（實作）

[`l17_ex5_starter`，檔案 `exercises/starter/ex5_input_health.cpp`]

DawLab 17 第一個真實的 take 是 3 秒剛好為 0 的資料——麥克風被靜音了——而什麼都沒說。寫一個 `InputHealth`，由 disk thread 把它寫出的每個 block 餵給它：

- 每個 sample 都剛好是 0：input 被靜音或封鎖了；
- 有聲音之間出現 0.1 秒以上剛好為 0 的一段：input 中斷了；
- 峰值低於 -50 dBFS：太小聲了；
- clipping：同一個 channel 上連續 3 個以上、同號、滿刻度的 sample——跨過 block 邊緣也算。

`warnings()` 把這些變成狀態列上的句子。自我檢查會餵入 Step 7 被靜音和太小聲的 take、振幅 1.5 的 sine（2000 次 clip，任何 block 大小都相同）、不算 clip 的滿刻度 sample、兩個 channel、0.2 秒的中斷，以及不算中斷的 take 前後靜音。

想一想：音訊介面裡的 noise gate 在音符之間輸出剛好為 0 的資料。它會觸發哪一條規則？你要怎麼把它和被靜音的 input 分辨開來？

## 練習 6 - Comping ★★★（實作）

[`l17_ex6_starter`，檔案 `exercises/starter/ex6_comping.cpp`]

一個 loop 錄下了一段 solo 的四圈。開頭幾小節第 2 圈最好，中段第 4 圈最好，結尾第 1 圈最好。*Comp* 說明哪一圈在哪裡播放，並變成 Step 4 那一個 take 檔上的 clip：

- `chooseTake(comp, from, to, take)`：在視窗的 $`[from, to)`$ 內改用 `take` 的 comp——必要處切開 segment，相鄰且 take 相同的合併。Comp 是一個值，所以 undo 就是保留上一個（`CompHistory`，已提供）。
- `compClips(comp, takes, fade)`：每個 segment 一個 clip，播放它自己那一圈，在每個邊界以邊界為中心做 `fade` 個 frame 的線性交叉淡化——segment 太短或那一圈沒有更多 frame 時就短一點，而且永遠是偶數，讓兩個 clip 都剛好越過邊界一半。

自我檢查會對照一個簡單的 model 做 3000 次隨機選擇，接著逐 frame 混合各個 clip，要求 gain 在每個地方的總和都剛好是 1、每個 clip 都只播放自己那一圈——包括 960 frame 的交叉淡化、300 frame 的 segment 周圍、提早結束的那一圈的尾端、奇數和 0。

## 練習 7 - 兩個時鐘 ★★☆（設計）

DawLab 17 會拒絕無法以同一個取樣率運作的 input 和 output（Step 7）。但 USB 麥克風和電腦的喇叭可以都跑在 48 kHz，卻仍然不同步：每個裝置都有自己的晶振。用文字設計 DAW 該怎麼處理，包括：

1. **差多少。** 麥克風的時鐘比喇叭快 50 ppm。10 分鐘的 take 結束時，和 timeline 相差多少 sample、幾毫秒？
2. **Ring。** 兩個裝置共用一個 audio callback 時，總有一個必須多給或少給 frame。如果沒人修正，Step 1 的 ring 或 driver 會怎樣？
3. **什麼時候修正。** 錄音時（input 上的自適應 resampler，依 buffer 的滿度調整），還是之後（把完成的 take 依量到的比例伸縮）？各有什麼代價？Take 的檔案又必須存什麼？
4. **怎麼量。** 在哪個 thread 上數哪些數字，可以得到這個比例？
5. **使用者。** App 應該說什麼？對錄音樂的人來說，真正的解法是什麼？

---

[回到本章](../README.zh-TW.md) | [解答](../solutions/README.zh-TW.md)
