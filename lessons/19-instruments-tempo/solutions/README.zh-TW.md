# 第 19 章 - 解答
[English](README.md) | **繁體中文**

[回到本章](../README.zh-TW.md) | [練習](../exercises/README.zh-TW.md)

用 `.\scripts\build.ps1 -Only 19-instruments-tempo -TestLabel solution` 執行所有解答程式。每一個都以 `[PASS] l19_exN (... checks passed)` 結尾。

---

## 練習 1 - 為什麼這樣做？

1. **秒。** 存成 sample 的 map 在取樣率改變時得轉換，每個點都取整到整數個 sample——下次改變時再取整一次，於是 44.1 → 48 → 44.1 kHz 之後拿不回原來的 map。存成秒，map 就是歌本身的性質；取樣率到最後才進來，每個 tick 只取整一次。而且它不花什麼成本：audio thread 只是多乘一次。
2. **Loop 的邊界。** 當兩個 tick 落在同一個 sample（tempo 快、取樣率低），在 loop 結束的那個 *sample* 之後（含）的第一個 tick，可能是 loop 結束點的前一個 tick：從 sample 去找的話，一圈會少走一個 tick，下一圈又提早一個 tick 開始——那個 tick 上的音符每圈結尾都會丟掉，或在開頭重播一次。直接以 tick 給定，一圈就剛好是 loop 的那些 tick，不管 sample 怎麼落。
3. **音樂上的位置。** 另一種選擇是保留 *sample*：播放頭留在畫面時間軸上的原處，卻落在另一個小節和拍上——在新的 map 下，那個 sample 是音樂裡的另一個地方。音訊會不跳動地繼續播，但音符會跳過或重複，小節計數也會跳。保留音樂上的位置時，音符繼續走（按住的繼續響），跳的是 audio clip——這正是使用者拖動 tempo 點時要的：播放頭下的音樂，換一個速度。
4. **三個 buffer。** Atomic 只裝得下一個 word，不是 8 KB。Mutex 會讓 audio thread 等 UI thread，而 UI thread 可能在握著 lock 時被排程出去：dropout。Seqlock 的 reader 在 writer 可能正在寫的時候複製記憶體；版本變了它會丟掉那份複製，但讀取本身就是 data race，C++ 沒有定義它的行為（ThreadSanitizer 也會報），而且每次重試 audio thread 都要複製 8 KB。用三個 buffer，每個 buffer 同一時間只屬於一個 thread，交接只是一次 exchange。
5. **比別人早了它的補償量。** 錄音時，演奏者是跟著*聽到的*其他 track 彈，並盡快聽到自己的樂器；自己的聲音在 mix 裡早了幾毫秒，不是他們彈奏時依據的東西。錄下的音符是依歌被聽到的時間擺放，不是依樂器發聲的時間。回放時沒有人在彈：這一 take 必須和其他 track 完全對齊，而只有完整的補償計畫做得到。
6. **提早彈。** 音符落得早——落在按鍵相對於他們聽到的歌按下去的地方。對*音符*來說這是對的：音符是演奏者想要它出現的時間，換一個快的樂器，或同一個樂器在回放時被補償，就會在那裡發聲。回放時，計畫把其他每個 track 延遲 sampler 的 latency，所以 sampler 的聲音剛好出現在音符的時間：提早彈是演奏者在補償一個回放時已經不存在的延遲。聽起來會稍微搶拍——這就是為什麼演奏者應該用快的樂器來 monitor，也是 DAW 提供 low-latency monitoring 的原因。
7. **時間。** 它保住的是這一階持續多久：tempo 選成讓這一階的長度剛好等於 ramp 走過這些 tick 的時間。讀檔的程式拿 tempo 只做一件事，就是把 tick 換成時間，所以它看得到的只有每一階邊界的時間；用這一階中間的「真正」tempo，每一階的長度都會稍微不對，誤差會累加（練習 2 (e)：一拍之後 1.9 ms）。

## 練習 2 - 用數字看 tempo

[`solutions/ex2_tempo_numbers.cpp`](ex2_tempo_numbers.cpp)

- **(a)** $`k = 60 / 15360`$ BPM／tick，所以 ramp 需要 $`\frac{60}{960} \cdot \frac{\ln(120/60)}{k} = 16 \ln 2 = `$ **11.09 s**。中間那條小節線，tempo 是 90：$`16 \ln 1.5 = `$ **6.49 s**——比一半的時間晚，因為前半段是慢的那一半。時間過一半（5.545 s）是在 $`\ln(B/60) = \frac{1}{2}\ln 2`$ 的地方：$`B = 60\sqrt{2} = `$ **84.85 BPM**，在 tick $`15360(\sqrt{2} - 1) = `$ **6362**（第 2 小節，進去 2.6 拍）。
- **(b)** 120 BPM 的兩小節是 4 s；90 的兩小節是 $`8 \cdot 60 / 90 = 5.33`$ s：第 5 小節在 **9.333 s** 開始，48 kHz 下是 sample **448 000**，44.1 kHz 下是 **411 600**。一個 map，兩種取樣率。
- **(c)** 總量是最大的那個，**4096**；各 track 分別等 4096、3840、4000 和 0。在第一個 track 上按一個鍵：$`1024 + 0 + 4096 + 512 = 5632`$ sample = **117 ms**。第一個 track arm 起來並開 low-latency monitoring：$`1024 + 0 + 0 + 512 = 1536`$ = **32 ms**，而且它比其他 track 早 4096 sample（85 ms）。
- **(d)** $`60\,000\,000 / 97.5 = 615\,384.6`$，存成 **615 385**，也就是 97.499 939 BPM。十分鐘後：$`600 \cdot (97.5 / 97.499939 - 1) = `$ 晚 **0.375 ms**。很小——但同一首歌的兩個檔，從不同的 tempo 轉換而來，一個小時下來就對不齊了。
- **(e)** $`\frac{60}{960} \cdot \frac{\ln(60/120)}{-60/960} = \ln 2 = `$ **0.6931 s**。用中間的 tempo 112.5、97.5、82.5 和 67.5：$`15 \cdot (1/112.5 + 1/97.5 + 1/82.5 + 1/67.5) = 0.6912`$ s，早了 **1.93 ms**：一階的時間是 $`1/B`$ 在這一階上的平均，不是它中間的 $`1/B`$。

## 練習 3 - Meter map

[`solutions/ex3_meter_map.cpp`](ex3_meter_map.cpp)

`prepare()` 把每個變化之前的小節加起來：$`start_i = start_{i-1} + (bar_i - bar_{i-1}) \cdot ticksPerBar_{i-1}`$。`tickOfBar()` 用 `std::upper_bound` 找到這個小節之前（含）的最後一個變化；`positionOf()` 用以 tick 表示的起點找到變化，然後除兩次——除以小節的長度，再除以拍的長度。

唯一的陷阱是除法。C++ 往零取整：$`-1 / 3840`$ 是 0，tick -1 就會變成「第 0 小節、第 0 拍、tick -1」。往下取整的 `floorDiv` 把它放在第 -1 小節、第 3 拍、tick 959。

```text
Exercise 3 - a meter map
  one meter: 4/4 bars of 3840 ticks, 6/8 bars of 2880; a tick before 0 is in bar -1
  4/4, 4/4, 2/4, then 6/8, 7/16 from bar 11: bar lines at 0, 3840, 7680, 9600, 12480 ... 29760
  300 random maps (1-21 changes, 1/2 to 12/16): 0 bar lines wrong, 0 positions that do not come back, 0 outside their
    bar, 0 out of order
[PASS] l19_ex3 (16 checks passed)
```

在 DawLab 裡，meter map 會和 tempo map 並排放在 transport 裡：ruler 向它要小節線，再向 tempo map 問這些 tick 各在哪個 sample。兩者從不需要對方。

## 練習 4 - 從拍點做出 tempo map

[`solutions/ex4_beats_to_map.cpp`](ex4_beats_to_map.cpp)

第 $`k`$ 拍和第 $`k + 1`$ 拍之間有 960 個 tick 和 $`d_k = t_{k+1} - t_k`$ 秒，所以那裡的 tempo 是 $`60 / d_k`$：在 tick $`960k`$ 的一個跳躍。Tempo 和前一拍相同的拍不需要點——穩定的樂團是一個點，不是兩百個。

`tapTempo()` 把間隔排序後取中間那個。漏敲一下時，間隔是 0.5、0.5、1.0、0.5、0.5：平均值說 100 BPM，中位數說 120。

```text
Exercise 4 - a tempo map from beats
  beats 0.6, 0.6, 0.5, 0.5, 0.4 s apart: 100 BPM, 120 from beat 3, 150 from beat 5; every beat within 0.0e+00 s
  300 random bands of up to 200 beats: 0 refused, 0 with a beat off its time, 0 with a point where the tempo did not change
  five taps with one missed: the median says 120.0 BPM, the mean would say 100.0
[PASS] l19_ex4 (22 checks passed)
```

真實樂團的每一拍都差一點點，於是每一拍都變成一個點：練習 5 會在不讓任何一拍移動超過容許值的前提下，把這樣的 map 變稀疏。
