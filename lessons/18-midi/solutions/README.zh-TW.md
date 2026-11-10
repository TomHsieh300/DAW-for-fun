# 第 18 章 - 解答
[English](README.md) | **繁體中文**

[回到本章](../README.zh-TW.md) | [練習](../exercises/README.zh-TW.md)

用 `.\scripts\build.ps1 -Only 18-midi -TestLabel solution` 執行所有解答程式（練習 8-12 要加上 `-Juce`）。每一個都以 `[PASS] l18_exN (... checks passed)` 結尾。

---

## 練習 1 - 處理音符為什麼要這麼小心？

1. **Tick。** 存成 sample 的音符在 tempo 改變時會留在原地：鼓（以 tick 存放，或跟著 grid）移動了，音符卻沒有，歌就散了。要跟上，每個音符都得重寫——而且每次都要再取整一次，於是 tempo 來回改幾次之後，音符就跑到別的地方去了。以 tick 存放時，tempo 改變只動到轉換而已。這個轉換每次都從 tick 算起，因為累加會漂移：每次加法都會取整，誤差會累積起來。練習 2 (a) 量了一個 `float` 累加一小時的結果：差了四分鐘。
2. **Note off、controller、note on。** 一個音在它結束的地方再彈一次：如果新的 note on 先來，synth 會開始這個音，然後舊音的 note off 立刻把它結束——該有音的地方變成無聲。Controller 放在中間：鋼琴家在兩個和弦*之間*換踏板，而在錄音裡，舊和弦的結束、踏板和新和弦常常在同一個 tick。先處理 note off 就結束了舊和弦；踏板踩下；新和弦在踏板下開始。如果踏板先來，舊和弦會被延續到新和弦裡——一片混濁。在音符開頭重設 pitch bend 也一樣：如果放在 note on 之後，這個音最開頭的 sample 會帶著舊的 bend。
3. **Chase 一個音符。** 這個音得從中間開始——但 synth 每個音都從 attack 開始播，所以從第 3 小節「chase」到第 5 小節的音，聽起來會像第 5 小節有一個新的音，而那裡根本沒人彈。要做對，樂器得從音的開頭靜音地 render 到該位置，這很昂貴；而取樣鋼琴或鼓聲根本不該重新開始。因此許多 DAW 把 note chasing 做成選項，給長的 pad 和弦樂用；Step 2 沒有做。
4. **Steal。** 立刻切掉聲音是波形上的一個跳躍，而跳躍就是每個人都聽得到的爆音，能量遍佈所有頻率。新的音晚 2 ms 遠低於任何人聽得出的程度。丟掉新的音是最糟的選擇：那正是演奏者剛彈、正在聽的音，而被搶走的是最不會被想念的——已放開的，或最舊的。
5. **排程播放。** 固定的延遲是可以學會的：演奏者會適應一個總是晚 10 ms 回應的樂器，就像管風琴師適應管風琴一樣。Jitter 學不會：兩個剛好同時彈的音出來時會分開，穩定的節奏出來時會不均勻。而且音與音之間的時間完全保持彈的樣子。Block 小時盡快播放比較好——64 或 128 sample 時，jitter（48 kHz 下 1.3 到 2.7 ms）低於任何人聽得出的程度，而延遲更重要——或是用在慢的 pad 上，根本沒人聽得出起音的時間。
6. **沒有 input latency。** 音符的時間是它的事件到達的時間，大約在按鍵按下後一毫秒（練習 2 (b)）：MIDI 沒有轉換器，也沒有 input buffer。音訊帶著 input latency 進來，是因為聲音在程式看到它之前，先在轉換器和 driver 的 buffer 裡等待。要扣掉的是演奏者*聽到*的：他們跟著歌彈，而歌在 render 之後，要經過輸出 latency 和 mix latency 才到達他們的耳朵。所以音符屬於他們聽到時歌所在的位置，而不是 engine 在那一刻 render 的位置。
7. **保留原本彈的。** 移動音符會永遠失去原始的版本：沒辦法回到「關」、沒辦法試另一個 grid，而且第二次 strength 0.5 的 quantize 會和第一次疊加（變成 0.75 的距離）。它也會失去感覺——演奏者刻意的 swing 或搶拍——而 strength 低於 1 本來就是要保留這些。從原本彈的音符算出 quantize 後的音符，只在 clip 改變時花一點時間，audio thread 上則完全不花。

## 練習 2 - 用數字看 MIDI

[`solutions/ex2_midi_numbers.cpp`](ex2_midi_numbers.cpp)

- **(a)** $`\frac{44100 \cdot 60}{97 \cdot 960} = 28.414948`$ sample 一個 tick。第 33 小節從 tick $`32 \cdot 4 \cdot 960 = 122\,880`$ 開始，也就是 sample $`\mathrm{round}(122880 \cdot 28.414948) = 3\,491\,629`$（79.18 s）。一小時是 $`97 \cdot 960 \cdot 60 = 5\,587\,200`$ 個 tick，正好是 sample 158 760 000。`float` 有 24 bit 的 mantissa：累加超過 $`2^{24} \approx 1680`$ 萬之後，加上 28.4 會被取整成 2 的倍數，然後是 4、8、16——解答量到差了 **1060 萬個 sample（240 s）**。`double` 好得多，但仍然不精確；從 tick 算起就只取整一次，永遠如此。
- **(b)** $`10 / 31250 = 320`$ µs 一個 byte，所以一個 note on（3 byte）要 **0.96 ms**。10 個音的和弦：用 running status 是 $`3 + 9 \cdot 2 = 21`$ byte = **6.72 ms**，不用是 30 byte = 9.6 ms。最後一個音在第一個音開始後 $`19 \cdot 320`$ µs = 6.08 ms = **291.84 sample** 開始。5-pin 線傳來的「和弦」其實是 6 ms 的琶音；USB MIDI 會把這些音放在同一個封包裡送。
- **(c)** 一個 block 是 $`512 / 48000`$ = **10.67 ms**：「盡快播放」會讓事件晚 0 到 10.67 ms，取決於它落在 block 週期的哪裡。排程播放：$`2 \cdot 512 + 512 = 1536`$ sample = **32 ms**，永遠如此。128 sample 時：0 到 **2.67 ms** 的 jitter，或排程的 **8 ms**。
- **(d)** 120 BPM 時：$`48000 \cdot 60 / (120 \cdot 960) = `$ 一個 tick **25 sample = 0.52 ms**；200 BPM 時 **15 sample = 0.31 ms**。120 BPM 時一個 1/16 是 240 tick = **6000 sample**（125 ms）。Tick 比任何人彈得還細；Step 6 的錄音取整到 tick，沒有人聽得出來。
- **(e)** C7 = $`440 \cdot 2^{27/12}`$ = **2093.0 Hz**。第 12 個諧波是第一個超過 24 kHz 的（25 116 Hz），摺回到 $`48000 - 25116 = `$ **22 884 Hz**。第一個低於 10 kHz 的 alias 是**第 19 個諧波**（39 767 Hz），在 $`48000 - 39767 = `$ **8233 Hz**，根本不是 C7 的諧波。鋸齒波第 $`k`$ 個諧波的振幅是 $`1/k`$：比基音低 **25.6 dB**，清楚可聞——這就是 PolyBLEP 的用途（Step 3）。
- **(f)** 480 tick 上的 swing 0.2：$`\mathrm{round}(0.2 \cdot 480) = `$ **96 tick = 2400 sample = 50 ms**（120 BPM）。Tick 990 最接近第 2 條線（tick 960，偶數線，不 swing）；strength 0.5 把它移一半：$`990 + \mathrm{round}(0.5 \cdot (960 - 990)) = `$ **975**。Tick 1430 最接近第 3 條線（1440，奇數），swing 到 $`1440 + 96 = `$ **1536**。

## 練習 3 - Standard MIDI File

[`solutions/ex3_smf.cpp`](ex3_smf.cpp)

Writer 先建好每個 track 的資料，之後才寫它的 chunk，因為 chunk 的長度在資料之前。Running status 只是一個變數：最後寫出的 status；meta event 會取消它，但 writer 在事件之間不寫任何 meta event。Reader 一個事件一個事件地跟著檔案走：

- **每個長度都先和剩下的部分比較**，然後才讀任何東西——而且寫成 `length > end - p`，絕不寫 `p + length > end`，因為損壞的檔案可能帶著接近 $`2^{32}`$ 的長度，那樣會溢位。
- **Meta event 和 SysEx 會取消 running status**，所以緊接在它們後面的 data byte 是錯誤，而不是上一個音的延續。
- **Tick 從絕對 tick 轉換**，以檔案的 division：$`\mathrm{round}(tick \cdot 960 / division)`$，用整數算。逐個轉換 delta 會讓每一個都取整：division 7 時，相隔一個 tick 的七個事件會結束在 959 而不是 960。
- **沒有 channel message 的 track** 是 format 1 的 conductor（或是空的 track）：它可能帶著 tempo 和拍號，但不是歌的 track。

```text
Exercise 3 - Standard MIDI Files
  10 of 10 numbers of the standard's table written and read back
  a song of one track, 3/4 at 100 BPM: 82 bytes, exactly the expected ones (running status, note offs first)
  a format-0 file at 96 a quarter with SysEx, an unknown chunk and running status: read
  division 7, a tick apart: ticks 137, 274, ... 960 (summed deltas would end on 959)
  23 of 23 broken files refused, each with a reason
  2000 of 2000 random songs (every channel message, names, empty tracks) came back the same
  82 files cut short: 82 refused; 20000 damaged files: no crash (1416 still readable)
[PASS] l18_ex3 (52 checks passed)
```

它沒做的：第一個之後的 tempo 變化（計在 `tempoChanges` 裡，好讓 app 能發出警告）、format 2、SMPTE 時間，以及調號、歌詞和 marker。練習 9 會把它帶進 DawLab 18，在那裡事件會變成音符。

## 練習 4 - Tempo map

[`solutions/ex4_tempo_map.cpp`](ex4_tempo_map.cpp)

`prepare()` 用 `double` 把各段加起來，不取整：$`start_i = start_{i-1} + (tick_i - tick_{i-1}) \cdot spt_{i-1}`$。`sampleOfTick()` 用 `std::upper_bound` 找到 tick 之前（含）的最後一個變化，只取整一次：$`\mathrm{round}(start_i + (t - tick_i) \cdot spt_i)`$。0 之前的 tick 用第一個變化。

`firstTickAtOrAfter()` 依各段以 sample 計的起點找到 sample 所在的段，然後在段內做 Step 1 做的事：`ceil()`，再往回退，只要前一個 tick 仍然取整到這個 sample。在變化處，從前一段算出的 tick 和從後一段算出的 tick 一致，因為一段的起點和前一段的終點是完全相同的算式。

```text
Exercise 4 - a tempo map
  one tempo: the same as Step 1 (bar 33 at 97 BPM, 44.1 kHz: sample 3491629)
  120, 60 and 240 BPM a bar each: bars at samples 0, 96000, 288000, 336000
  300 random maps (1-41 changes, 20-300 BPM): 0 ticks wrong, 0 samples whose first tick is wrong, 0 steps backwards
  a ritardando into every beat, 2048 events in random blocks: 2048 found, 0 outside their block
  2000 changes later: sample 49504480, exactly 49504480; rounding every change would give 49504000
[PASS] l18_ex4 (19 checks passed)
```

2000 個變化之後，取整的起點早了 480 個 sample——不是因為某次取整很大，而是因為每次都一樣：1001 個 tick 的一段在 97 BPM 時是 28 443.36 sample，在 131 BPM 時是 21 061.12，兩者都被向下取整——每對差 0.48，1000 對之後就是 480。不會互相抵消的誤差會累積起來。

要把 map 帶進 engine，`Transport` 會保存它而不是一個 BPM，所有呼叫 `sampleOfTick()` 的地方——clip player、recorder、grid——都改問 map。音符完全不用變：這正是它們存成 tick 的原因。

## 練習 5 - Arpeggiator

[`solutions/ex5_arpeggiator.cpp`](ex5_arpeggiator.cpp)

`process()` 一步一步走過 block。各步是 $`[firstTickAtOrAfter(s_0), firstTickAtOrAfter(s_1))`$ 裡 `rate` 的倍數——Step 1 的半開區間，所以每一步恰好屬於一個 block。每一步依序：

1. 如果我們的音的 off tick 到了就結束它（`offTick <= step`：gate 為 1 時正好在這一步結束，在下一個音之前）；
2. 取到這一步的 offset（含）為止的輸入：鍵按下和放開，其他的直接通過；
3. 播放這一步：沒有按住的鍵時 pattern 從頭開始，否則播 pattern 的下一個音。

最後一步之後，如果 note off 落在 block 裡就送出，再處理剩下的輸入。輸出靠從後面插入來保持順序——note off 會移到同一個 offset 的其他事件之前——只花幾次交換，不配置記憶體；`std::stable_sort` 會需要一個 buffer。

```text
Exercise 5 - an arpeggiator
  Up, C E G held, 1/16 at half gate:                   yes
  Down:                                                yes
  UpDown:                                              yes
  UpDown, one key:                                     yes
  UpDown, two keys:                                    yes
  AsPlayed, G C E:                                     yes
  AsPlayed, C pressed twice, then E:                   yes
  Up, two octaves of C E:                              yes
  UpDown, two octaves of C E:                          yes
  Down, three octaves of 110 (134 is no note):         yes
  Up, two octaves of 115 (127 is the top note):        yes
  17 keys: the oldest is pushed out:                   yes
  keys at a step and a sample after it:                yes
  all keys up, then a new chord at a step:             yes
  gate 1: the note off first at the next step:         yes
  a tiny gate is still one tick long:                  yes
  1/8 at 97 BPM, 44.1 kHz, blocks of 333: 44 events, every one on its exact sample: yes
  a controller and a bend pass through:                yes
  a controller after a note off, in one block:         yes
  a controller before a note off, in one block:        yes
  a jump: the note ends, the pattern starts again:     yes
  400 random songs (keys at and between steps, every mode, rate and gate) in blocks of 1 to 700 samples:
    0 differ from one block, 0 leave a note hanging, 0 play two notes at once, 0 made `out` grow,
    0 put an event out of its block or out of order
[PASS] l18_ex5 (26 checks passed)
```

Block 測試找到的錯最多：第一步用 `from / rate`（向下取整）而不是向上取整來算，或是在 block 裡後面沒有下一步時忘了送 note off。這些錯在某些 block 裡能運作，在另一些 block 裡就失敗。

## 練習 6 - Mono、legato 與 glide

[`solutions/ex6_mono_synth.cpp`](ex6_mono_synth.cpp)

鍵存在一個陣列裡，最舊的在前：新的鍵放到最後，放開的鍵不論在哪裡都取出，只有它是最後一個時聲音才會改變。`sound()` 做每個新音都要做的事：目標和 gain，然後滑音（當這個音和另一個重疊時）或直接跳過去，然後是 envelope——從無聲開始時起 attack，或 legato 關掉時；legato 開著時什麼都不做。

滑音是一個 sample 數和每個 sample 的一步，$`(target - pitch) / samples`$。把步長一直加上去會留下取整誤差，所以最後一步把音高精確地設成目標。以半音為單位的滑音在 hertz 上是一條曲線：鋸齒波的頻率每個 sample 都從音高算出，$`440 \cdot 2^{(p - 69)/12}`$。

```text
Exercise 6 - mono, legato and glide
  one note: starts at its pitch with an attack, sounds (peak 0.25), releases to silence
  legato: E over a held C keeps the envelope and slides 4 semitones in exactly 2400 samples
  back to the key still down, a new slide from mid-slide, an octave as long as a semitone: done
  last-note priority: C E G, E up changes nothing, G up returns to C, C up releases
  legato off: every new note restarts the attack from the level it is at, and still glides
  the sound: 55 cycles in a quarter second at A3, 110 at A4
[PASS] l18_ex6 (36 checks passed)
```

固定*時間*的 glide（這裡的做法）對每個音程聽起來都一樣。許多 synth 改用固定*速率*的 glide——每秒若干個半音——讓八度比一步花更久。只要把 sample 數改成 $`|target - pitch| \cdot samplesPerSemitone`$ 就行了。

## 練習 7 - Track 上的 VST3 樂器

1. **事件。** JUCE 的 plugin 拿到一個 `juce::MidiBuffer`：在呼叫內部 sample 位置上的訊息。每個 chunk 的 track 已經有它的 `MidiBlock`——clip 的事件、chase 過的 controller、即時鍵盤——所以 host 把它們複製進一個在 `prepare()` 準備好的 `MidiBuffer`（先 `ensureSize`，之後只 `clear()` 和 `addEvent()`：不配置記憶體）。JUCE 的 VST3 wrapper 把音符變成 VST3 的 note event，把 controller 和 pitch bend 經由 plugin 的 `IMidiMapping` 變成同一個 sample 上的*參數變化*。沒有把任何參數對應到 controller 64 的 plugin 就沒有延音踏板；host 修不了，只能顯示出來。
2. **卡住的音。** 那個表仍然需要——比以前更需要：plugin 不可能知道 transport 跳了，只有 host 知道。停止、seek 或繞回時，host 在 chunk 的 offset 為表裡的每個音送出 note off，音會照常 release。要硬停止（panic 按鈕）時，它呼叫 plugin 的 `reset()`，切掉它的 voice 和尾巴。Track 取消 arm 時仍按著的即時鍵盤按鍵，也會收到它們的 note off。
3. **Latency。** Plugin 的 latency $`L`$ 是 track latency 的一部分，第 14 章的補償會把其他每個 track 都延遲這麼多，讓歌保持整齊。從鍵盤來的音不能提早送——它還沒發生——所以演奏者會比用內建 synth 晚 $`L`$ 聽到。錄下的音符依演奏者聽到的來擺放，而 Step 6 的 mix latency 已經包含補償，所以只要 anchor 用 engine 的總 latency，結果就是對的。
4. **尾巴。** 不論停止與否，engine 每個 block 都繼續呼叫 plugin，沒有事件時就給空的 buffer——release、reverb、delay 都繼續。Mute 的 track 仍然會被處理（它的 note off 必須送到），只是輸出不混音。Bypass 的樂器輸出無聲（`processBlockBypassed`），但仍然收到 note off。Offline render 在歌曲結尾之後繼續 `getTailLengthSeconds()` 那麼久，或者——當它說「無限」時——直到輸出低於 -90 dB 持續一秒。
5. **狀態。** 專案保存 plugin 的身分（格式、檔案、unique id）和 `getStateInformation()` 的 blob，以 base64 寫在一行文字裡。開啟專案時在 message thread 上建立 instance、在那裡呼叫 `setStateInformation()`，之後才透過 command queue 交給 engine，就像任何 insert 一樣（第 15 章）。沒有安裝的 plugin 會把它的 blob 保留在一個無聲的佔位裡，這樣存檔時才不會遺失。
6. **信任。** 什麼都證明不了：plugin 可能配置記憶體、上鎖、寫 log 到檔案，或當掉。Host 改為量測——每次呼叫的時間對照 block 的期限、dropout、每個 plugin 的 CPU 表——謹慎的 host 會把 plugin 放在另一個 process 裡跑，讓當掉只損失 plugin 而不是整首歌。DawLab 的 engine 測試繼續用內建的 synth；plugin 則用第 13 章自己的那個來測，它的行為是已知的。

## 練習 8 - Synth 設定面板

[`solutions/ex8_synth_panel.cpp`](ex8_synth_panel.cpp)

`showTrack()` 以 `juce::dontSendNotification` 把 synth 的值寫進 slider：顯示一個 track 不是修改它；如果帶著通知，面板會把設定送回去——對值本身無害，但對 audio thread 來說是一個新版本，更糟的是會養成「打開面板就是一次編輯」的習慣。`sliderMoved()` 每次都用 track 的 id 重新找 synth；check 會在面板底下刪掉一個 track，再加一個佔用同一個 slot 的新 track。

文字格式先用 `%.15g` 印出每個值，只有 15 位讀回來不是同一個 double 時才試 16 和 17 位——所以預設音色寫成 `resonance=0.9` 而不是 `0.90000000000000002`，而每個隨機值仍然精確讀回。讀取用 `std::strtod`，它會正確捨入；JUCE 快速的 `getDoubleValue()` 不一定，500 組隨機設定的來回測試就會找到這一點。

```text
Exercise 8 - a synth settings panel
  the default sound as text: cutoff=4000 resonance=0.9 attack=0.005 decay=0.2 sustain=0.6 release=0.3 level=0.25
  500 random settings back exactly from their text; 10 of 10 bad lines refused
  one long note, cutoff 8000 Hz then 200 Hz from the panel: brightness 0.00619, then 0.00019
  level 0 while the note sounds: silent, the note still held; back to 0.25: heard again
  an audio track, a deleted track, no track: the sliders off, and why
[PASS] l18_ex8 (16 checks passed)
```

它沒做的：設定既不能 undo 也不會存檔——那需要一個新的 edit 和專案檔裡的一行，而那是對 app 的修改。文字格式就是那一行。

## 練習 9 - DawLab 18 裡的 MIDI 檔

[`solutions/ex9_midi_files.cpp`](ex9_midi_files.cpp)

匯出走的是 `notes()`——engine 播放的、quantize 之後的音——而不是 `played`，並依 Step 2 的規則擺放每個音：clip 素材的 tick $`t`$ 在 $`start + t - offset`$ 播放，而且只在 $`[offset, offset + length)`$ 之內。匯入用每個鍵和 channel 一個佇列來配對音符，最舊的優先：檔案無法說明一個 off 屬於同一個鍵的兩個重疊音裡的哪一個，而先進先出是大多數程式的假設。

`Session::makeNewTrack()` 會給出下一個 id 但不會佔用它，所以呼叫兩次會得到同一個 id；匯入自己從第一個開始為 track 編號，套用 AddTrack edit 時計數器會移到它們之後。所有 edit 和 SetTempo 都交給同一個 `perform()`，所以一次 undo 就收回整個匯入。

```text
Exercise 9 - MIDI files in DawLab 18
  a quantized clip with an offset, a muted copy, an empty track: 3 notes on one track "Bass" at 96 BPM
  two tracks, overlapping notes of one key, a note that never ends, a bend and a program: 5 notes, 2 dropped
  undo, redo, and a song with no track refused
  198 random notes to l18_ex9_round_trip.mid (1708 bytes) and back into another project: the same
[PASS] l18_ex9 (24 checks passed)
```

## 練習 10 - 把電腦鍵盤當 MIDI 鍵盤

[`solutions/ex10_computer_keyboard.cpp`](ex10_computer_keyboard.cpp)

每個按住的鍵都記下它的 key code 和它開始的音高。重複時找到自己的 key code 就什麼都不做；`keyStateChanged()` 逐一詢問每個按住的鍵是否仍按著，結束那些已放開的，用它們當初的音高；`allNotesOff()` 結束全部。App 會從擁有鍵盤焦點的 component 的 `focusLost()`，以及視窗的 `activeWindowStatusChanged()` 呼叫它。

```text
Exercise 10 - the computer keyboard as a MIDI keyboard
  a: C4 at velocity 100, at the host's time:       yes
  a held: the repeats send nothing:                yes
  w, k and p:                                      yes
  a up:                                            yes
  z, a, then w up:                                 yes
  v v c, the octave up to +3, then j:              yes
  Ctrl+s, Alt+d, q, 1:                             yes
  focus lost with k, p, a and j held:              yes
  the armed instrument track plays h (A at octave -3), and stops when it goes up
[PASS] l18_ex10 (27 checks passed)
```

## 練習 11 - 一次編輯很多音符

[`solutions/ex11_note_editor.cpp`](ex11_note_editor.cpp)

每個操作都建立「音符與它是否被選取」的配對，`write()` 先排序這些配對——旗標就跟著它的音符走——再把音符交給 `Session::replaceMidiClip()`。`sync()` 把 clip 和編輯器最後看到的音符比較；任何其他改變 clip 的事都會清掉選取。

```text
Exercise 11 - edit many notes at once
  the chord up a whole tone:                   yes
  undo; the selection is cleared:              yes
  74 moved to the start, still selected:       yes
  then only it a tone down:                    yes
  duplicate the chord and 72@960:              yes
  duplicate on a grid of a bar grows the clip to two bars; remove takes the copies away
  Up, Shift+Up, Right, Right, Down:            yes
  Ctrl+D, Delete, Ctrl+A, Backspace: the clip is empty
  everything undone:                           yes
  the clip deleted: every operation refused
[PASS] l18_ex11 (36 checks passed)
```

## 練習 12 - Track 上的 VST3 樂器

[`solutions/ex12_plugin_instrument.cpp`](ex12_plugin_instrument.cpp)

`prepare()` 為最壞的 block 預留空間：buffer 是 block 的大小，`MidiBuffer` 容得下 512 個事件。之後 `render()` 只縮小 buffer 的可見範圍（`setSize(..., avoidReallocating = true)`）、依 byte 加入事件、再處理——不配置記憶體，check 用替換 `operator new` 來計數。Processor 的 channel 對應到 track 的 channel；mono 的對應到兩邊。

```text
Exercise 12 - a VST3 instrument on a track
  events at their samples (a note, a pedal, a bend, a program change), the sound added, reset: done
  allocations while rendering: 0
  a mono processor on both channels; the state as "4....f9." and back
  on an instrument track: the note on at sample 24000, its end at 48000, heard in the mix
  (chapter 13's plugin is not built here: loading a real VST3 is not checked)
[PASS] l18_ex12 (18 checks passed)
```

Plugin 的 latency 沒有補償：預先讀取的 sampler 會晚它的 latency 那麼多。練習 7 第 3 題說明了它該放在哪裡——在 track 的 latency 裡，交給第 14 章的補償。
