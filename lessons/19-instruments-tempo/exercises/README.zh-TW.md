# 第 19 章 - 練習
[English](README.md) | **繁體中文**

[回到本章](../README.zh-TW.md) | [解答](../solutions/README.zh-TW.md)

難度：★☆☆ 簡單、★★☆ 中等、★★★ 困難。實作題在 `exercises/starter/` 裡有一個可以編譯執行的 starter 檔；完成之前，它的 self-check 會印出 `FAIL`（這是預期的）。建置 target 名稱寫在方括號裡。它們都不需要 JUCE：

```powershell
.\scripts\build.ps1 -Only 19-instruments-tempo
.\build\bin\Debug\l19_ex3_starter.exe
```

**練習 1-6** 是本章的練習，關於各個 step（不需要 JUCE）；第 5、6 題之後才會加上。**練習 7-9 會是選做專題**，延伸 DawLab 19。

練習 2 和 4 建立在 Step 1 的 tempo map 上（`steps/step02-transport/minidaw.h` 裡有解好的版本）。練習 3 是獨立的。請先完成各個 step。

---

## 練習 1 - 為什麼這樣做？★☆☆（概念）

每題用幾句話回答。

1. Step 1 把 tempo map 存成秒，而不是 sample。如果 map 存成 sample，音訊裝置換取樣率時它得發生什麼事——代價又是什麼？
2. 以小節設定的 loop 存成 tick（Step 2），而且它的兩個邊界是以 tick *直接給*各個 segment，而不是從 sample 找出來。否則每一圈會丟掉或重播哪個 tick，在什麼情況下？
3. 歌在播放時 tempo map 改變，播放頭保留它音樂上的位置，sample 則跳開。說出另一種選擇，並說明兩種做法下使用者各會看到和聽到什麼。
4. Step 4 用三個 buffer 把 map 從 UI thread 傳到 audio thread。為什麼不用 atomic、為什麼不用 mutex，而 seqlock 在這裡雖然「能動」，問題出在哪裡？
5. 開著 low-latency monitoring 時，arm 的 track 比其他 track 早了它的補償量。為什麼錄音時沒人在意，而回放時補償又必須恢復完整？
6. 錄下的音符依歌被聽到的位置擺放，不論彈它的樂器 latency 多大（Step 3）。彈慢 sampler 的演奏者會提早彈來彌補。他們的音符會落在哪裡，這樣對嗎？
7. 匯出 MIDI 檔時，Step 5 把 ramp 寫成階梯，而每一階的 tempo *不是* ramp 在這一階裡任何一處的 tempo。它保住的是什麼，為什麼那才是讀檔程式在意的？

## 練習 2 - 用數字看 tempo ★★☆（計算）

先手算（寫出公式），再執行 [`l19_ex2_solution`] 來檢查。

- (a) 四個 4/4 小節從 60 ramp 到 120 BPM。要多久？中間那條小節線何時到？時間過一半時是哪個 tick，那裡的 tempo 是多少？
- (b) 120 BPM，從第 3 小節起 90。第 5 小節何時開始，在 48 kHz 和 44.1 kHz 下各是哪個 sample？
- (c) 48 kHz 下，latency 為 0、256、64 + 32 和 4096 sample 的 track。總量是多少，每個 track 各等多久？鍵盤排程晚 1024 sample、輸出 latency 512 時，在第一個 track 上按一個鍵：開和不開 low-latency monitoring，各在何時聽到？
- (d) MIDI 檔把 tempo 存成每拍整數個微秒。97.5 BPM 會變成什麼，十分鐘後歌漂移了多少？
- (e) 一拍內從 120 ramp 到 60 BPM，寫成四階、每階一個十六分音符。這一拍有多長？每一階都用它中間的 tempo 時又是多長？

## 練習 3 - Meter map ★★☆（實作）

[`l19_ex3_starter`，檔案 `exercises/starter/ex3_meter_map.cpp`]

本章給了 tempo 一個 map，拍號卻只有一個。也給拍號一個 map：一串變化，每個是一個小節和從那個小節起生效的拍號。寫出 `prepare()`、`tickOfBar()`、`positionOf()`（一個 tick 所在的小節、拍，以及拍內的 tick）和它的反函數 `tickOf()`。

全部都是整數——沒有東西要取整——但 6/8 的小節不是六個四分音符長、一拍是分母指定的那個音符，而 0 之前的 tick 在第 -1 小節，不是倒著數的第 0 小節。Self-check 會用 300 個隨機 map 和一個逐小節數過去的參考答案比對。

## 練習 4 - 從拍點做出 tempo map ★★☆（實作）

[`l19_ex4_starter`，檔案 `exercises/starter/ex4_beats_to_map.cpp`]

樂團沒有跟 click 錄音，grid 哪裡都對不上。反過來做：從拍子*實際*所在的時間——跟著敲出來，或由 beat detector 找到——建立把 grid 放到那裡的 tempo map。

- `mapFromBeats()`：在每一拍跳到讓下一拍準時到的 tempo——但 tempo 沒變的地方不放點。它會拒絕不是一個接一個的拍點、map 裝不下的 tempo，以及超過 map 容量的點數。
- `tapTempo()`：從幾次 tap 得到一個 tempo，用間隔的中位數，這樣漏敲一下不會讓它減半。

Self-check 會為 300 個隨機樂團建立 map，並問 Step 1 的 map 每一拍在何時播放：和它被聽到的時間相差不到十分之一微秒。
