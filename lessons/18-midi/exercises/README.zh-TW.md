# 第 18 章 - 練習
[English](README.md) | **繁體中文**

[回到本章](../README.zh-TW.md) | [解答](../solutions/README.zh-TW.md)

難度：★☆☆ 簡單、★★☆ 中等、★★★ 困難。實作題在 `exercises/starter/` 裡有一個可以編譯執行的 starter 檔；完成之前，它的 self-check 會印出 `FAIL`（這是預期的）。建置 target 名稱寫在方括號裡。它們都不需要 JUCE：

```powershell
.\scripts\build.ps1 -Only 18-midi
.\build\bin\Debug\l18_ex3_starter.exe
```

**練習 1-7** 是本章的練習，關於各個 step（不需要 JUCE）。**練習 8-12 是選做專題**，延伸 DawLab 18（需要 JUCE 建置）；它們之後才會加上。

練習 2 和 3 建立在 Step 1 的 model 上（`exercises/model.h`：Step 1 的 `minidaw.h`，函式都已解好）。練習 4 是獨立的。請先完成各個 step。

---

## 練習 1 - 處理音符為什麼要這麼小心？★☆☆（概念）

每題用幾句話回答。

1. Audio clip 以 sample 存放，MIDI clip 以 tick 存放（Step 1）。如果音符存成 sample，使用者改 tempo 時會出什麼錯？還有，Step 1 為什麼每次都從 tick 算出 `sampleOfTick()`，而不是把 `samplesPerTick` 一直加上去？
2. 在同一個 tick 上，Step 1 先播 note off 再播 note on，Step 2 把 controller 播在兩者之間。對這兩條規則，各舉一個順序反過來時聽得出錯的例子。
3. Seek 之後，Step 2 會 chase controller（踏板、bend），但不 chase 音符。DAW 要正確地 chase 一個音符得做什麼——以及使用者為什麼可能根本不想要它？
4. Step 4 用 2 ms 的淡出來 steal 一個 voice，所以新的音晚了 2 ms。為什麼不立刻切掉舊的音？為什麼不在所有 voice 都在用時乾脆丟掉新的音？
5. Step 5 可以讓鍵盤的事件盡快播放，或是排程播放：在到達後一個固定的延遲。排程播放總是比較晚。為什麼它仍然是比較好的預設？什麼時候你會選擇盡快播放？
6. 錄音訊（第 17 章）會扣掉 input latency；錄 MIDI（Step 6）不會。為什麼？它扣掉哪些 latency，又為什麼是這些？
7. Step 6 的 quantize 從不改變原本彈的音符，而是算出它們在哪裡播放。如果它直接移動音符本身，會失去什麼？

## 練習 2 - 用數字看 MIDI ★★☆（計算）

先手算（寫出公式），再執行 [`l18_ex2_solution`] 來檢查。

- (a) 一首 97 BPM、44.1 kHz、一個四分音符 960 tick 的歌：一個 tick 是多少 sample？第 33 小節（4/4）從哪個 sample 開始？有個程式每個 tick 把 `samplesPerTick` 加到一個 `float` 上，加一小時：猜猜它最後差多少，再讓解答告訴你。
- (b) MIDI 線每秒傳 31 250 bit，每個 byte 10 bit。一個 note on 要多久？一個 10 個音的和弦，用和不用 running status 各要多久？在 48 kHz 下，第一個音和最後一個音之間隔了多少 sample？
- (c) 48 kHz 下每個 block 512 sample，輸出 latency 512。「盡快播放」會加上多少 jitter？Step 5 兩個 block 加上輸出 latency 的排程延遲有多長？128 sample 時呢？
- (d) 48 kHz 下，120 BPM 和 200 BPM 時一個 tick 是多少 sample、多少毫秒？120 BPM 時一個 1/16 音符是多少 sample？
- (e) 48 kHz 下一個 C7（MIDI note 96）的 naive 鋸齒波。第一個超過 Nyquist 的是第幾個諧波，它摺回到哪裡？第一個低於 10 kHz 的 alias 是哪一個，它和基音比起來有多大聲？
- (f) Step 6 的 quantize，1/8 grid（480 tick），swing 0.2：每隔一條的 grid 線晚了多少 tick、多少 sample、120 BPM 時多少毫秒？Strength 0.5 會把 tick 990 的音移到哪裡？加上這個 swing 且 strength 為 1 時，tick 1430 的音會移到哪裡？

## 練習 3 - Standard MIDI File ★★★（實作）

[`l18_ex3_starter`，檔案 `exercises/starter/ex3_smf.cpp`]

每個 DAW 都會讀寫 `.mid` 檔。為 format 0 和 1 寫出兩個方向（header 完整說明了這個格式）：

- `writeVlq()` 和 `readVlq()`：每個 delta time 和長度都用到的可變長度數字。
- `writeSmf()`：把一首歌——一個 tempo、一個拍號、有名字的事件 track——寫成 format 1、一個四分音符 960 tick 的檔案，有 conductor track 和 running status。Self-check 會比對每一個 byte。
- `readSmf()`：把 format 0 或 1、任何 division 的檔案讀回成以 960 tick 為單位的歌。Running status、meta event、SysEx、未知的 chunk，以及長度超過 6 byte 的 header 都必須能處理。

檔案是不可信的輸入。Self-check 會餵給 reader 23 個壞掉的檔案、一個好檔案的每一種截斷，以及 20 000 個隨機損壞的檔案：它必須拒絕讀不了的東西並說明原因，而且絕不能讀超過結尾。也用 AddressSanitizer 建置它（在 Linux 上用 `-DDAWLAB_SANITIZE=address,undefined`），或用 MSVC 的 Debug 建置，它的 checked iterator 會抓到同樣的錯。

## 練習 4 - Tempo map ★★☆（實作）

[`l18_ex4_starter`，檔案 `exercises/starter/ex4_tempo_map.cpp`]

DawLab 18 只有一個 tempo。給它一個 tempo map——一串變化，每個是一個 tick 和一個 tempo——並寫出 audio thread 需要的兩個轉換：`sampleOfTick()` 和它的精確反函數 `firstTickAtOrAfter()`，和 Step 1 一樣，但要跨過 tempo 變化。`prepare()` 算出每個變化從哪個 sample 開始。

Self-check 會用 300 個隨機 map 和參考答案比對，要求一首每拍都漸慢的歌的每一個 tick 都恰好落在一個隨機大小的 block 裡，還會查出你有沒有把變化的起點取整：2000 個變化之後，那會差 480 個 sample（11 ms）。
