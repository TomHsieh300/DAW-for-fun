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

## 練習 5 - 把 tempo 點變稀疏

[`solutions/ex5_thin_tempo.cpp`](ex5_thin_tempo.cpp)

把歌畫成一條曲線：橫軸是 tick，縱軸是秒。一串跳躍點是一條在每個點都有轉角的折線，把它變稀疏就是畫一條轉角比較少的折線，從舊折線的一個轉角連到另一個轉角，而且和舊折線的距離永遠不超過容許值。從點 $`i`$ 到點 $`j`$ 的一段*合格*的條件是：兩點之間的直線——也就是單一的 tempo $`\frac{60}{960} \cdot \frac{tick_j - tick_i}{t_j - t_i}`$——經過中間每個點時都在容許值內。只需要看轉角：兩個轉角之間，兩條線都是直的。

最少的段數是一個最短路徑問題。`fewest[j]` 是從第一個點到點 $`j`$ 最少要幾段：

```math
fewest_j = \min_{i < j,\ (i, j)\ fits} fewest_i + 1
```

`from[j]` 記住勝出的那個 $`i`$，用來從最後一個點走回去。最後一個點再原樣加上去。一共 $`n^2 / 2`$ 對，每對最多 $`n`$ 個點——250 拍是兩百五十萬次比較，只做一次，而且在 message thread 上。

為什麼不每次都拿合格的最長一段？因為「合格」不會繼承：一段合格，不代表它從較晚的起點到同一個終點的那一部分也合格。在穩定 tempo 上下 ±9 ms 的晃動，從一個平穩的拍到另一個平穩的拍，在 10 ms 的容許值內是合格的；但從晚了 9 ms 的那一拍出發，直線一開始就偏了 9 ms，早到的那些拍離它就有 18 ms。貪心的那一段在它不得不停的地方結束，也許正好停在這樣的拍上，代價由下一段來付。Self-check 的 400 個短 list 抓得到貪心的解法。

```text
Exercise 5 - thin a tempo list
  beats of 0.50, 0.52, 0.48, 0.50 s within 15 ms: 117.65 BPM, 122.45 from beat 3; the worst tick is 10.0 ms off
  400 random lists of up to 12 points against every choice: 0 refused, 0 that move a tick too far, 0 not the fewest (240 got shorter)
  250 beats within  0.0 ms: 250 points (worst tick 0.00 ms off); every 1. point instead: 0 ms off
  250 beats within  1.0 ms: 166 points (worst tick 0.98 ms off); every 2. point instead: 71 ms off
  250 beats within  5.0 ms:  44 points (worst tick 4.80 ms off); every 6. point instead: 394 ms off
  250 beats within 20.0 ms:  27 points (worst tick 19.63 ms off); every 10. point instead: 643 ms off
[PASS] l19_ex5 (23 checks passed)
```

最後一欄是 DawLab 19 的匯入：同樣的點數，它偏了幾十倍，因為留下的點把自己的 tempo 用在被拿掉的那些點的拍上。練習 9 會用 `thinTempo()` 取代它。

## 練習 6 - 跟著 tempo 走的 audio

1. **兩種意思。** *起點跟著走*：clip 的起點是 tick，不是 sample；map 改變時 clip 移動，並以它自己的速度播放。它只需要那個 tick，適合本身沒有拍、或不能被改變的東西：一句口白、第 17 小節上的音效、一聲 cymbal。*全部跟著走*：clip 裡的每一拍都留在歌的那一拍上，所以 audio 要播得快一點或慢一點。這需要知道 audio 裡的拍在哪裡，還需要 time-stretcher；適合 loop 和樂團的 take。第三種選擇便宜而誠實：像錄音帶那樣用 resample 播快一點——音高跟著速度走。鼓沒問題，歌手就不行。
2. **Clip 存什麼。** 它的起點和長度（以 tick 表示），以及一串*（檔案裡的 sample，歌裡的 tick）*配對——warp marker。兩個 marker 之間，audio 平均攤在那些 tick 上。已知 tempo 的 loop 只需要兩個 marker；在 map 上錄的 take，在它底下 map 的每個點都放一個 marker（ramp 則放足夠多個：練習 5 說明了在某個容許值下可以少到多少）。只存「它是在 120 BPM 錄的」是兩個 marker 的特例，描述不了在 ramp 上錄的 take。
3. **Stretcher 在哪裡跑。** *在 audio thread 上*：新的 tempo 馬上聽得到，但 stretcher 是以幾十毫秒的 window 在工作，所以它有 latency——track 必須回報它，就像 Step 3 的樂器——而且它的成本每個 block、每個 clip 都要付，付在最付不起的那個 thread 上。它的比例必須能每個 block 改變。*事先算好*：worker thread 把拉伸後的 audio render 到 buffer，engine 用一個 command 拿到它，和拿到其他東西的方式一樣；audio thread 播的是普通的 clip，沒有 latency，也沒有額外成本。代價是：tempo 編輯之後，在 render 完成前 clip 是過時的（先播移到新起點的舊 render，或把 clip 靜音，好了再換上去），而且第二份複本要佔記憶體。對 DawLab 來說，事先算好比較合適：`ClipPlayer` 不用改，交接也是 engine 已經有的那一種。能讓你邊聽邊拖 tempo 的 DAW 需要 real-time 的那種，通常再加上 render 結果的 cache。
4. **不漂移。** Segment 帶著它的 tick（Step 2）。對每個 block，player 向 marker 問 `tickFrom` 和 `tickTo` 各對應檔案裡的哪個 sample，然後剛好餵那麼多給 stretcher——位置每個 block 都從 tick 算出來，從不用比例一路累加。於是 clip 裡的 ramp 沒什麼特別：只是每個 block 的比例不一樣。
5. **其餘的。** *錄音*：take 拿到它錄製時那個 map 的 marker，所以錄完當下它照原樣播放（比例 1），之後 map 改變時再相對於它拉伸。關掉 metronome 錄的 take 沒有 marker，只有起點跟著走。*專案檔*：每個 clip 存模式和 marker——新的格式編號，舊檔讀進來是「不跟著走」。*Undo*：tempo 這邊沒有新東西：`SetTempoMap` 這個 edit 本來就會還原 map，而 clip 是以 tick 存的，所以會跟著回去。改變 clip 的模式或 marker 是另外一個 edit。Render 不存檔也不 undo：它們是 cache，從檔案、marker 和 map 重新做出來。
