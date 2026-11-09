# 第 17 章 - 解答
[English](README.md) | **繁體中文**

[回到本章](../README.zh-TW.md) | [練習](../exercises/README.zh-TW.md)

用 `.\scripts\build.ps1 -Only 17-recording -TestLabel solution` 執行所有解答程式。每一個都以 `[PASS] l17_exN (... checks passed)` 結束。

---

## 練習 1 - 為什麼要這麼小心？

1. **檔案。** `fwrite` 先把資料複製到 buffer，buffer 滿了才呼叫作業系統，而作業系統可能要等磁碟：正在啟動的硬碟、正在掃描新檔案的防毒軟體、網路資料夾、滿了的 page cache。這會花上幾毫秒甚至幾秒，而 audio thread 每個 block 只有幾毫秒，每個 block 都是。Ring 讓 audio thread 不論磁碟在做什麼都能不必等待地交出 frame；等待由 disk thread 負責。
2. **整個 block，以及靜音。** 如果放得下的部分先進去，take 會遺失 block 的中段，之後的一切都會提早遺失的 frame 數——take 的其餘部分都會和歌曲對不上拍。整塊或完全不進讓帳目保持簡單：一個空缺永遠是已知位置上若干完整的 frame。而空缺變成等長的靜音、不是什麼都沒有，理由相同：它之後的每個 frame 都留在當初演奏的位置。
3. **誰說 Idle。** UI 呼叫 `stop()` 的那一刻，audio thread 可能正在 `process()` 中間推送一個 block。如果 UI 宣告 Idle 並開始讀取或準備 ring，兩個 thread 就同時使用它：data race，最後的 frame 遺失，或寫進一個剛被重設的 ring。只有 audio thread 知道自己已經離開這個 take，所以只有它能說——用它的下一個 block。
4. **當機之後。** 大小為 0 和 `.recording` 這個名字都在說「還沒完成」：沒有程式會把這個檔案誤認為完成的 take，下次啟動也確切知道要復原哪些檔案。真正的長度來自檔案大小而不是檔頭，所以寫進去的東西一點也不會遺失。Sync 很慢——它要等到磁碟真的拿到資料——每次寫入都 sync 會讓 disk thread 落後。每秒一次把斷電時的損失限制在一秒以內；*程式*當機則只會遺失還在 ring 裡的部分，因為每次寫入都已經交給了作業系統，即使程式不在了，作業系統還是會把它寫出去。
5. **不切。** Transport 到達 punch-in 時進來的 block，裡面是 $`L = L_{in} + L_{out} + L_{mix}`$ 個 sample *之前*演奏的聲音。屬於 punch-in 的 frame 會晚 $`L`$ 才到，落在之後某個 block 的中間。在 transport 到達時的 block 切開，會早了 $`L`$，還被捨入到 block 邊界。
6. **靜音，而且保持靜音。** Feedback 是一個增益大於 1 的迴路。「稍微」調小聲可能還是大於 1，或在下一個大聲的音符又回來；只有靜音能確實打斷迴路。而迴路安靜*正是因為*被靜音了：變安靜就取消靜音的 guard 會讓嘯叫一再重來。使用者必須改變某件事——戴上耳機、移動麥克風——然後再重設。

## 練習 2 - 用數字看錄音

- **(a)** $`L = 480 + 480 + 240 = 1200`$ samples = 25 ms，所以 take 從 $`96000 - 1200 = 94800`$（1.975 秒）開始。真實 round trip 是 380 ms（18 240 samples）的話，take 會晚 $`18240 - 1200 = 17040`$ samples = **355 ms**：120 BPM 下 **0.71 拍**——不是微妙的 flam，而是明顯錯誤的錄音。從 0 開始錄的 take 會從 $`-1200`$ 開始：那些 frame 是歌曲開始前演奏的，所以 clip 跳過它們（`skipFrames = 1200`），從 0 開始。
- **(b)** $`48000 \cdot 2 \cdot 4 = 384\,000`$ bytes 每秒，每分鐘 **23.04 MB**，每小時 **1.38 GB**。`kMaxDataBytes` = $`2^{32} - 1 - 100`$ = 4 294 967 195 bytes：$`/ 384\,000 = 11\,185`$ 秒 = **3.11 小時**的立體聲（單聲道兩倍）。到了之後 recorder 會停下，留下一個完整的 take，就像磁碟滿了一樣。
- **(c)** 250 ms = 12 000 frames，再加一個 block，因為 block 是整塊進去的：**12 512 frames**，立體聲 $`12512 \cdot 2 \cdot 4`$ = **100 096 bytes**。DawLab 17 的 10 秒 ring 是 3.84 MB——不小，但磁碟停頓一兩秒並不罕見。
- **(d)** 直接：input + output = 960 samples = **20 ms**。經過 insert：再加 mix 的 240 = **25 ms**。兩邊都是 64 samples 時：128 samples = **2.67 ms**，比站在離喇叭一公尺遠還短。
- **(e)** Take 從 stream 的第 $`480000 + 1200 = 481\,200`$ 個 frame 開始；扣掉 48 000 的 preroll，**433 200 個 frame** 從未寫入：單聲道 **1.73 MB**。

```text
Exercise 2 - recording by the numbers
(a) L = 1200 samples = 25.0 ms; the take starts at 94800 (1.9750 s)
    with a real round trip of 18240: 17040 samples (355 ms) too late = 0.71 beats at 120 BPM
(b) 384000 bytes a second, 23.04 MB a minute, 1.3824 GB an hour; a WAV take ends after 11185 s = 3.11 h
(c) a 250 ms stall: 12000 frames + one block = 12512 frames = 100096 bytes stereo
    DawLab 17's ring of 10 s: 3.84 MB stereo
(d) monitoring: directly 20.0 ms, through the inserts 25.0 ms; at 64-sample buffers 2.67 ms
(e) the take starts at frame 481200 of the stream; 433200 frames = 1.73 MB of mono are not written
[PASS] l17_ex2 (15 checks passed)
```

## 練習 3 - 只寫 take 用得到的部分

[`solutions/ex3_preroll.cpp`](ex3_preroll.cpp)

第一個 take 從 `takesOf()` 會放的地方開始：在可見視窗起點被聽到的那個 stream frame，$`window.from - (stamped - L)`$。到它之前一段 preroll 為止的一切都可以丟掉：$`skip = \max(first - preroll, 0)`$。把 stamped position 移動 `skip`，`takesOf()` 裡什麼都不會變——每個被聽到的位置都移動同樣的量——除了一件事：`loopsFrom()` 問的是 stamped position 是否在 loop 結束之前。被移到它之後的位置就不再 loop 了，所以在 loop 裡 `skip` 最多到 $`loopEnd - 1 - stamped`$。空的視窗（loop 外的 punch）沒有第一個 take：沒什麼可省的。

`StreamGate` 倒數：`drop(n)` 傳回 $`\min(remaining, n)`$ 並扣掉。

```text
Exercise 3 - write only what a take can use
  a punch at 10 s, recorded from 0: 433200 frames (9.03 s) never written, the take at frame 48000 of the file
  20000 random recordings: 0 wrong (expected 0), 5 % of the frames not written
[PASS] l17_ex3 (23 checks passed)
```

`prerollFor()` 在 audio thread 上計算，也就是第一個 block 蓋上位置的時候，所以它不能配置記憶體——它沒有，只是幾個比較。Disk thread 必須在 take 的檔頭使用移動過的 stamped position，否則復原的 take 會早 `skip` 個 frame。

## 練習 4 - 量測 latency，也要知道何時不能信

[`solutions/ex4_measure_latency.cpp`](ex4_measure_latency.cpp)

對每個 chirp，計算 $`[-early, late]`$ 內每個 lag 的 correlation；取絕對值，因為麥克風可能反相。最大的那個給出 lag；所有值的中位數代表「這裡沒東西」的水準，所以高出它 8 倍的峰值就是 chirp。聽到的 lag 取中位數，可以抵抗某個 chirp 旁邊甩門之類的干擾；聽到的 lag 的分散程度則說明到底有沒有單一的 latency。

```text
Exercise 4 - measure the latency, and know when not to trust it
  through a cable                              heard 8 of 8, lag     37, spread     0: set the trim
  through the air, quiet                       heard 8 of 8, lag   1234, spread     0: set the trim
  the driver says too much                     heard 8 of 8, lag   -300, spread     0: set the trim
  the microphone inverts                       heard 8 of 8, lag     37, spread     0: set the trim
  one chirp lost                               heard 7 of 8, lag     37, spread     0: set the trim
  a door slams after the third chirp           heard 8 of 8, lag     37, spread     0: set the trim
    (the first loud frame says 2400)
  a latency that moves (Step 7's laptop)       heard 8 of 8, lag  17236, spread  4525: do not trust it
  a muted microphone                           heard 0 of 8, lag      0, spread     0: do not trust it
  three chirps of eight                        heard 3 of 8, lag     37, spread     0: do not trust it
  five chirps of eight                         heard 5 of 8, lag     37, spread     0: do not trust it
  six chirps of eight                          heard 6 of 8, lag     37, spread     0: set the trim
  lags 48 samples apart                        heard 8 of 8, lag     37, spread    48: set the trim
  lags 49 samples apart                        heard 8 of 8, lag     37, spread    49: do not trust it
[PASS] l17_ex4 (15 checks passed)
```

為什麼用 sweep 而不是 click：click 的能量分散在所有頻率，而筆電喇叭只播得出其中一部分；sweep 把所有能量放在喇叭和麥克風能運作的頻段，而且和自己做 correlation 只會有一個尖銳的峰值。

## 練習 5 - 告訴使用者 input 出了什麼問題

[`solutions/ex5_input_health.cpp`](ex5_input_health.cpp)

- 每個 channel 把目前滿刻度的連續段記成一個帶號的計數：+1 的段為正，-1 的段為負。另一個符號的 sample 開始新的一段；較小聲的則結束它。因為計數存在物件裡，跨過 block 邊緣的段會繼續——自我檢查用隨機的 block 大小餵入同一個訊號來證明這點。
- 一段在長度達到 3 個 sample 時算一次；最長的記下來放進訊息。
- 一段全為 0 的 frame，只有在之前聽到過聲音、*而且*之後有聲音結束它時才算中斷。所以第一個音之前和最後一個音之後的靜音永遠不算。
- 全是 0 的 take 只得到一句話：其他關於它的話都是雜訊。

```text
Exercise 5 - tell the user what is wrong with the input
  muted:   Every sample was exactly 0: the input is muted or blocked. Check the system's microphone settings and privacy settings.
  quiet:   The input is very quiet: its peak is -63.0 dBFS. Raise the input gain.
  hot:     The input clipped 2000 times, the longest for 13 samples: lower the input gain.
  gap:     The input stopped for 0.200 s in the middle of the take: a cable or an interface?
[PASS] l17_ex5 (14 checks passed)
```

**Noise gate。**介面裡的 gate 在音符之間輸出剛好為 0 的資料，所以 0.1 秒以上的停頓會觸發「中斷」規則——誤報。被靜音的 input 不一樣：*每個* sample 都是 0。要分辨 gate 和中斷，看 0 之前是什麼：gate 是在訊號淡到幾乎沒有之後才關上，中斷則是在中途、以完整音量被切斷。記住每段 0 之前最後幾個 sample 的音量就夠了。

## 練習 6 - 兩個時鐘

一種設計；也有其他可行的做法。

1. **差多少。** $`48000 \cdot 600 \cdot 50 \cdot 10^{-6} = 1440`$ samples = 10 分鐘後 **30 ms**——一個開頭對拍的 take，結尾出現 flam。開始時什麼都沒有，之後線性增加。
2. **Ring。** 如果麥克風送來的 frame 比 callback 消耗的多，它們會堆在 driver 的 buffer 或 ring 裡，直到某處溢出：大約每 3.5 分鐘遺失一個 512-frame 的 block，每次在 take 裡留下一個爆音。比較慢的麥克風則會用完：出現空缺。有些系統會把它藏起來——Windows 的 shared mode 和 macOS 的聚集裝置可以做帶漂移修正的 resample——這時時間是對的，但品質取決於它們的 resampler。
3. **什麼時候修正。**
   - *錄音時*：input 上的自適應 resampler，比例由某個 buffer 的滿度慢慢推動（一個緩慢的控制迴路，PLL）。Monitoring 保持正確、take 寫下時就是對的，但它在 audio thread 上耗 CPU，也會多一點 latency。
   - *之後*：照原樣錄下 input，在 seal 時依量到的比例伸縮完成的 take。精確又便宜，audio thread 上什麼都不做，但錄音期間即時波形和 monitor 會漂移。檔案必須存下這個比例（在 `dlab` chunk 裡，和 latency 放在一起），讓復原的 take 用同樣的方式伸縮。
4. **怎麼量。**在 audio thread 上數收到的 input frame 和 render 的 output frame，連同一個時鐘（JUCE 每次 callback 都會傳入 host time）。兩個計數在一段長時間窗內的比例就是漂移。50 ppm 在 48 kHz 是每秒 2.4 個 frame，所以要量得準需要幾十秒——這沒關係，因為它只隨溫度改變。
5. **使用者。** 當 input 和 output 是不同裝置時，直接說清楚：「錄音可能相對歌曲每分鐘漂移最多 3 ms。」對音樂來說真正的解法是 input 和 output 用同一個介面——同一個時鐘——或是有漂移修正的聚集裝置，或把裝置鎖到共用的 word clock。

---

[回到本章](../README.zh-TW.md) | [練習](../exercises/README.zh-TW.md)
