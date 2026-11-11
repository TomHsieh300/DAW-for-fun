# 第 20 章 - 練習
[English](README.md) | **繁體中文**

[回到本章](../README.zh-TW.md) | [解答](../solutions/README.zh-TW.md)

難度：★☆☆ 簡單、★★☆ 中等、★★★ 困難。實作題在 `exercises/starter/` 裡有一個可以編譯執行的 starter 檔；完成之前，它的 self-check 會印出 `FAIL`（這是預期的）。建置 target 名稱寫在方括號裡。這些練習都不需要 JUCE：

```powershell
.\scripts\build.ps1 -Only 20-audio-tempo
.\build\bin\Debug\l20_ex3_starter.exe
```

練習 3 建立在 Step 1 上（`steps/warp_map.h` 裡有解好的版本），練習 4 建立在 Step 4 上（`exercises/wsola.h` 裡有解好的版本）。請先完成各個 step。

---

## 練習 1 - 為什麼這樣做？★☆☆（概念）

每題用幾句話回答。

1. 起點跟著 tempo 的 clip 存的是 tick，sample 是推導出來的（Step 2）。為什麼不存 sample，然後每次 tempo 改變時把它移動正確的量？
2. 檔案裡的位置每個 block 都從歌的 sample 算出來（Step 3）。除了取整之外，用速度累加的位置還會在哪兩件事上出錯？
3. Sinc 把它的總和除以權重的總和（Step 3）。不除的話，穩定的訊號會怎樣？為什麼 sinc 讀得比 1 快時更糟？
4. WSOLA 拿候選段去和上一段的 *natural continuation* 比，而不是和上一段本身比，而且只除以候選段的大小（Step 4）。這兩件事各是為什麼？
5. Stereo stretch 時，兩個 channel 滑動同樣的量（Step 5）。如果各自找最適合的位置，你會聽到什麼？
6. 被較新的 request 取代的運算結果，即使已經算完也不會交回（Step 5）。如果 `collect()` 把每個算完的結果都依完成順序交回，使用者可能會聽到什麼？
7. 打開「跟著」時，clip 拿到的是「在目前的 map 上錄下的 take」的 marker（Step 6）。什麼時候這是錯的？那時使用者可以怎麼做？

## 練習 2 - 用數字看 ★★☆（計算）

先手算（寫出公式），再執行 [`l20_ex2_solution`] 來檢查。

- (a) 一個在 128 BPM 做的兩小節鼓 loop，放在 120 BPM 和 140 BPM 的歌裡。它以什麼速度播放？varispeed 讓它移動幾個半音？
- (b) 四小節內從 60 ramp 到 120 BPM 的 take，要維持在 1 ms 內。$`\max|f''|`$ 是多少？Step 1 給出幾個等分、幾個 marker？一段最多偏多少？第一段在它的中點實際偏多少？
- (c) Step 3 的 sinc 在速度 1 和速度 2 時，每個 output sample 各讀幾個 sample？
- (d) Step 4 的搜尋在 48 kHz、小段 1024 個 sample、搜尋範圍 256 時，output 每秒要做幾次乘法？
- (e) 一個 4 分鐘的 stereo clip 在 48 kHz 下被 stretch 到速度 0.8：它的運算結果佔多少記憶體？運算期間 Step 5 的位置清單又要多少？
- (f) 起點跟著 tempo 的 clip 在第 2 小節。歌以 120 BPM 播放，進入 clip 一拍半時 tempo 變成 90 BPM。Clip 裡的 audio 跳了多少？

## 練習 3 - Take 用最少的 marker ★★★（實作）

[`l20_ex3_starter`，檔案 `exercises/starter/ex3_fewest_markers.cpp`]

Step 1 把 ramp 切成等分，份數由 ramp 慢的那一端的弦誤差上界決定：永遠不會錯，但在 ramp 快的地方很浪費。第 19 章的練習 5 用 dynamic programming 找出最少的 tempo 點。寫出 `fewestMarkers()`：從一組候選 marker（take 每 `gridSamples` 一個，以及它的 tempo 點）裡，選出最少的幾個，讓每個候選點的播放時間都在錄音時間的容許值內。

Self-check 會對 150 個小的 take 和所有可能的選法比對，並在兩個 ramp 上和 Step 1 的等分比較數量。

## 練習 4 - 對準到 sample 的小數位置 ★★★（實作）

[`l20_ex4_starter`，檔案 `exercises/starter/ex4_fine_fit.cpp`]

Step 4 的小段以整數 sample 滑動，只能對準到半個 sample 以內：stretch 後的單音和其餘部分之間只有 31 到 40 dB，就是這樣來的。用通過三個整數偏移的拋物線找出分數在它們之間的頂點（`refine()`），並在小數位置讀取小段（`readHermite()`、`stretchFine()`）。

Self-check 會在四個速度下 stretch 220 和 440 Hz 的單音，並和 Step 4 比較——還有一個週期剛好是整數 sample 的 1 kHz 單音。
