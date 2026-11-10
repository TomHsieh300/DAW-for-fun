# 第 18 章 - 解答
[English](README.md) | **繁體中文**

[回到本章](../README.zh-TW.md) | [練習](../exercises/README.zh-TW.md)

用 `.\scripts\build.ps1 -Only 18-midi -TestLabel solution` 執行所有解答程式。每一個都以 `[PASS] l18_exN (... checks passed)` 結尾。

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
