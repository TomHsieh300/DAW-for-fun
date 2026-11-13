# 第 18 章 - 練習
[English](README.md) | **繁體中文**

[回到本章](../README.zh-TW.md) | [解答](../solutions/README.zh-TW.md)

難度：★☆☆ 簡單、★★☆ 中等、★★★ 困難。實作題在 `exercises/starter/` 裡有一個可以編譯執行的 starter 檔；完成之前，它的 self-check 會印出 `FAIL`（這是預期的）。建置 target 名稱寫在方括號裡。它們都不需要 JUCE：

```powershell
.\scripts\build.ps1 -Only 18-midi
.\build\bin\Debug\l18_ex3_starter.exe
```

**練習 1-7** 是本章的練習，關於各個 step（不需要 JUCE）。**練習 8-12 是選做專題**，延伸 DawLab 18。它們需要 JUCE 建置（`.\scripts\build.ps1 -Only 18-midi -Juce`）；和第 17 章的一樣，它們是在 offline 模式下建立真正 `MainComponent` 的 console 程式（`exercises/app_rig.h`），都不會修改 app。練習 8 和 12 用到 engine 在本章之後加上的 hook：把另一個樂器放到 synth 的位置，以及在 synth 播放時改變它的設定（Step 7「本章之後加上的」）。

練習 2、3 和 5 建立在 Step 1 的 model 上（`exercises/model.h`：Step 1 的 `minidaw.h`，函式都已解好），練習 6 建立在 Step 4 已解好的 voice 元件（`Saw`、`Adsr`）上。練習 4 是獨立的。請先完成各個 step。

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

這個練習的歌都很短，也只有一個可用的 tempo，所以它的 writer 可以保留兩個 app 的 `MidiFile.cpp`（第 19 章）沒有的簡化：4 個 byte 的 delta time 最多只能表示 2^28 - 1 個 tick，更長的間隔需要填充事件；而 0 或 NaN 的 tempo 換算不出每拍的微秒數。第 19 章兩者都處理。

## 練習 4 - Tempo map ★★☆（實作）

[`l18_ex4_starter`，檔案 `exercises/starter/ex4_tempo_map.cpp`]

DawLab 18 只有一個 tempo。給它一個 tempo map——一串變化，每個是一個 tick 和一個 tempo——並寫出 audio thread 需要的兩個轉換：`sampleOfTick()` 和它的精確反函數 `firstTickAtOrAfter()`，和 Step 1 一樣，但要跨過 tempo 變化。`prepare()` 算出每個變化從哪個 sample 開始。

Self-check 會用 300 個隨機 map 和參考答案比對，要求一首每拍都漸慢的歌的每一個 tick 都恰好落在一個隨機大小的 block 裡，還會查出你有沒有把變化的起點取整：2000 個變化之後，那會差 480 個 sample（11 ms）。

## 練習 5 - Arpeggiator ★★★（實作）

[`l18_ex5_starter`，檔案 `exercises/starter/ex5_arpeggiator.cpp`]

Arpeggiator 是鍵盤和樂器之間的 MIDI effect：它把按住的和弦一次一個音地播出來，落在音樂時間的 grid 上——Up、Down、UpDown 或依按鍵的順序，跨最多四個八度，每個音長是它那一步的 `gate`。寫出 `process()`，把一個 block 的鍵盤事件變成這個 block 的音符，以及給停止或跳躍用的 `reset()`（header 寫了每一條規則）。

這是換了形狀的 Step 1 和 Step 2。每一步在任何 tempo 下都落在它精確的 sample 上；在某一步自己的 sample 上按下的鍵屬於那一步；一個音即使它的鍵已經放開，也在它的 gate 結束，gate 為 1 時在下一個音之前結束；controller 直接通過。而且它跑在 audio thread 上：輸出在寫入時就保持順序，不用會配置記憶體的排序。Self-check 會把 400 首隨機的歌各播兩次，一次用單一 block，一次用 1 到 700 sample 的 block：輸出不能改變，也不能有音卡住。

## 練習 6 - Mono、legato 與 glide ★★☆（實作）

[`l18_ex6_starter`，檔案 `exercises/starter/ex6_mono_synth.cpp`]

Bass line 或 lead 常常用一個 voice 來彈。用 Step 3 的鋸齒波和 envelope 寫出 `MonoSynth`：

- **最後一個音優先：** 最新的鍵發聲；它放開時，仍按著的鍵裡最新的那個再次發聲。
- **Legato：** 和另一個音重疊的音不會重新開始 envelope（關掉 legato 時會，從它當下的音量開始）。
- **Glide：** 音高以半音為單位直線滑到新的音，不論音程多大都花一樣的時間——但只在重疊的音之間。

Self-check 會逐個 sample 追蹤音高：4 個半音的 glide 走到一半時，必須正好高了 2 個半音，而且必須正好在 2400 個 sample 時到達，一個都不能早。

## 練習 7 - Track 上的 VST3 樂器 ★★☆（設計）

DawLab 18 的樂器是它自己的 synth（Step 7）。第 13 章掛載 VST3 *effect*；練習 12 會把 VST3 *樂器* 放到 synth 的位置。在那之前，先用文字設計它。涵蓋：

1. **事件。** Step 7 交給 synth 一個逐 chunk 切開的 `MidiBlock`。Plugin 拿到的是什麼，clip 的音符、chase 過的 controller 和即時的鍵盤又怎麼進到裡面？在沒有 controller 事件的 VST3 裡，controller 或 pitch bend 會變成什麼？
2. **不留卡住的音。** Step 2 記著它啟動過的音符。Plugin 有自己的 voice 時還需要它嗎？既然 VST3 沒有「all notes off」，在停止、seek 或 loop 繞回時，host 要送什麼？
3. **Latency。** 預先讀取的 sampler，或有 look-ahead 的 synth，會回報 latency。它在第 14 章的延遲補償裡放在哪裡？它對鍵盤的延遲（Step 5）以及錄下的音符擺放的位置（Step 6）有什麼影響？
4. **尾巴。** Transport 停了，plugin 還在 release 一個音。Engine 要繼續呼叫它嗎？Mute 的 track、bypass 的 track，以及 offline render 的結尾呢？
5. **狀態。** 專案檔要保存什麼，怎麼在第一個音之前還原，哪個 thread 可以呼叫 `setStateInformation()`？
6. **信任。** Step 7 的測試證明 synth 絕不在 audio thread 上配置記憶體。Host 能對 plugin 證明什麼，又改做什麼？

## 練習 8 - Synth 設定面板 ★★☆（實作，JUCE）

[`l18_ex8_starter`，檔案 `exercises/starter/ex8_synth_panel.cpp`]

Synth 原本只有一種音色。本章之後，engine 為它加了一個 hook（Step 7「本章之後加上的」）：synth 播放時，可以在 UI thread 呼叫 `Engine::getTrackSynth(track)->setSettings()`，下一個 block 會把值交給 voice。做出它前面的面板：

- `SynthPanel::showTrack()` 用七個 slider 顯示一個 track 的 cutoff、resonance、envelope 和 level——顯示時什麼都不送出。不是樂器的 track，或已經不在的 track，會關掉 slider 並說明原因。
- 移動 slider 會把整組設定送到那個 track 的 synth。面板每次都用 track 的 id 找 synth：被刪除的 track 的 slot 屬於下一個 track，面板不能改到它的音色。
- `toText()` 和 `fromText()`：設定寫成一行，`cutoff=4000 resonance=0.9 ...`，用能精確讀回的最少位數——專案檔會保存的東西。每個 key 一次，每個值都在它 slider 的範圍內。

Self-check 會播一個長音，把 cutoff 從 8000 移到 200 Hz：聲音必須暗超過十倍；而 level 0 必須從下一個 block 起讓它無聲，同時那個音仍在響。

## 練習 9 - DawLab 18 裡的 MIDI 檔 ★★☆（實作，JUCE）

[`l18_ex9_starter`，檔案 `exercises/starter/ex9_midi_files.cpp`]

練習 3 讀寫的是事件；DawLab 把音符存在 clip 裡。寫出兩者之間的那一步（練習 3 的 reader 和 writer 已解好，放在 `exercises/smf_model.h`）：

- `songOf()`：專案播放時的樣子——每個有東西可播的 instrument track，每個沒有 mute 的 clip，quantize 之後落在它視窗裡的聲音，在 clip 結尾切掉。
- `importSong()`：檔案的每個 track 變成一個新的 instrument track，帶一個 clip。Note off 結束同一個鍵和 channel 最先開始的那個音；從沒結束的音隨 track 結束；pitch bend 和 program change 只計數，不保留。檔案的 tempo 變成專案的。全部是一個 undo 步驟「Import MIDI」。

Self-check 會匯出一個有 offset 的 quantize clip 和一個 mute 的複本、匯入同一個鍵重疊的音和一個從沒結束的音、用一次 undo 全部收回，並把 198 個隨機的音經由檔案送進另一個專案：它們必須原樣回來。

## 練習 10 - 把電腦鍵盤當 MIDI 鍵盤 ★☆☆（實作，JUCE）

[`l18_ex10_starter`，檔案 `exercises/starter/ex10_computer_keyboard.cpp`]

`a w s e d f t g y h u j k o l p` 彈 C4 到 D#5，`z` 和 `x` 換八度，`c` 和 `v` 改 velocity，音符送到 MIDI 鍵盤送的地方：`AudioHost::pushMidi()`。電腦鍵盤在三方面不是 MIDI 鍵盤，忘了任何一個都會留下卡住的音：按住的鍵會重複、JUCE 不會告訴你哪個鍵放開了，而視窗在背景時放開的鍵永遠不會被看到。還有，換了八度之後放開的鍵，必須結束它當初開始的那個音。

## 練習 11 - 一次編輯很多音符 ★★☆（實作，JUCE）

[`l18_ex11_starter`，檔案 `exercises/starter/ex11_note_editor.cpp`]

用矩形選取 clip 的音符；移調、移動、在它們後面複製、刪除——每個都是一個 undo 步驟——以及這一切的按鍵。難的是選取：它屬於音符，所以被排序移到清單另一個位置的音仍然被選取；而它屬於編輯器最後看到的 clip，所以 undo 會清掉它，而不是讓它留在別的音符上。

## 練習 12 - Track 上的 VST3 樂器 ★★★（實作，JUCE）

[`l18_ex12_starter`，檔案 `exercises/starter/ex12_plugin_instrument.cpp`]

把練習 7 的設計做出來：`PluginInstrument` 把一個 JUCE `AudioProcessor`——第 13 章的 `PluginHost` 為 VST3 檔給的就是它——包成一個 `engine::Instrument`，而 `Engine::setTrackInstrument()` 把它放到 track 的 synth 的位置。Block 的事件依 offset 放進 `MidiBuffer`，processor 的輸出加到 track 上，狀態可以存成 base64 文字。`render()` 跑在 audio thread 上：self-check 會計算其中的記憶體配置次數，必須是零。

Self-check 的樂器是它自己的一個小 processor，因為不能假設跑 check 的地方有 VST3 樂器。想聽真的，就在你自己的程式裡用 `PluginInstrument::load()` 載入你有的 VST3 樂器，或把它接到 DawLab 18 的 track 選單。
