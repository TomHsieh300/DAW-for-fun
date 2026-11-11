# 第 20 章 - 解答
[English](README.md) | **繁體中文**

[回到本章](../README.zh-TW.md) | [練習](../exercises/README.zh-TW.md)

用 `.\scripts\build.ps1 -Only 20-audio-tempo -TestLabel solution` 執行所有解答程式。每一個都以 `[PASS] l20_exN (... checks passed)` 結束。

---

## 練習 1 - 為什麼這樣做？

1. **存 tick，而不是移動 sample。**「正確的量」是從 clip 的 tick 算出來的——而 clip 已經沒有 tick 了。它得從自己的 sample 再找回 tick，而 sample 是取整過的：每次都偏一點，經過很多次改變誤差就累加起來（Step 2 量到改 1000 次後偏 15 個 sample）。存著 tick 的話，每次改變都只是從事實取整一次，永遠不會是取整再取整。這也讓 undo 不花任何代價：另一個 tempo 下的 clip list 會從它們的 tick 重新擺放。
2. **計數器。** 每次 **seek** 和 **loop 繞回**之後，都得告訴它從哪裡重新開始；而新的 tempo map 會改變它底下的位置：它反正得從兩個 map *修正*，那正是它原本想省掉的運算。而且計數器不知道 block 裡的 **knot**——速度改變的 tempo 點或 marker——所以整個 block 只用一個速度。（取整是第三件事：十分鐘的 ramp 之後偏 31 ms。）
3. **除以權重。** Window 讓某個小數位置的權重加起來比 1 多一點或少一點，換一個小數位置又不同。不除的話，穩定的音量會隨著小數位置變動，一個 sample 大一點、下一個小一點：這是起伏，也就是以小數位置重複的頻率出現的雜訊。讀得比較快時 sinc 會變窄——`sinc(c x)`、`c = 1/speed`——它的權重加起來大約是 `1/c`，也就是速度本身：不除的話，clip 會依速度變大聲。
4. **Natural continuation，除以候選段。** 上一段已經在 output 裡了；新的一段要接上的是 input 裡*緊接在它後面*的東西，因為如果什麼都沒切，播出來的就是那個——拿上一段本身來比，會找到早了一個 hop 的起點，也就是把那一段重複一次。除以候選段的大小，分數就是候選段的形狀和 continuation 之間的 cosine 乘上一個常數：大聲的候選段不會因為大聲而贏。如果改成除以候選段的能量（而不是它的平方根），小聲的那段會因為小聲而贏。
5. **各個 channel 各自對準。** 兩個 channel 會在不同的地方被切，相差幾個 sample，而左右之間的延遲會在每一段跳動。左右之間的延遲是我們聽出聲音方向的依據：stereo image 會每秒晃 94 次，或是糊成一片很寬、有相位感的聲音。
6. **每個算完的結果都依序交回。** 舊 tempo 的慢運算可能比最新 tempo 的快運算更晚完成。依序交回的話，它會取代較新的那個：歌已經是新的 tempo，clip 卻以舊的 tempo 播放，直到下一次改變——沒對上拍，而且沒有任何東西會修正它。
7. **目前這個 map 的 marker。** 它們說的是「它此刻播放的樣子就是對拍的」。對於此刻*沒有*對拍的 audio，這就是錯的：以另一個 tempo 放進來的 loop，或是在改 tempo 之前錄的 take。使用者應該先讓它對上：把 tempo 改回它製作時的樣子、打開「跟著」、再把 tempo 改回來——或者，長度已知的 loop 可以用練習 9（下一批會做）。

## 練習 2 - 用數字看

[`solutions/ex2_numbers.cpp`](ex2_numbers.cpp)

- (a) 速度是現在的 tempo 除以 loop 製作時的 tempo：$`120/128 = 0.9375`$ 和 $`140/128 = 1.0938`$。換成半音是 $`12 \log_2 0.9375 = -1.117`$ 和 $`12 \log_2 1.0938 = +1.551`$。兩種情況下歌手都偏了超過一個半音；鼓的 loop 沒問題。
- (b) $`k = 60/15360`$ BPM／tick，慢的那一端 $`\max|f''| = \frac{60}{960} \cdot k / 60^2 = 6.78 \cdot 10^{-8}`$ s/tick²。$`n = \lceil 15360 \sqrt{6.78 \cdot 10^{-8} / (8 \cdot 0.001)} \rceil = 45`$ 段、每段 341 tick、46 個 marker，每段最多偏 $`h^2/8 \cdot \max|f''| = 0.988`$ ms。第一段在 ramp 最慢的地方，它的中點實際偏 0.966 ms：上界在那裡很緊。
- (c) 速度 1 時 sinc 在兩邊各涵蓋 16 個 sample：共 33 個。速度 2 時它被變窄成在一半的 Nyquist 頻率截止，所以兩邊各涵蓋 $`16/0.5 = 32`$ 個：共 65 個，工作量加倍。
- (d) 每秒 $`48000/512 = 93.75`$ 段，每段搜尋 $`2 \cdot 256 + 1 = 513`$ 個偏移、每個 $`512`$ 個 sample、每個 sample 兩次乘法（交叉項和能量）：output 每秒 $`93.75 \cdot 513 \cdot 512 \cdot 2 = 4920`$ 萬次。
- (e) 4 分鐘以速度 0.8 播放是 300 s：$`300 \cdot 48000 \cdot 2 \cdot 4`$ byte = 115.2 MB 的 float audio。位置是每個 output sample 一個 double：運算期間還要 $`300 \cdot 48000 \cdot 8`$ = 115.2 MB。有四十個這種 clip 的歌，手機裝不下；分段計算位置可以讓峰值減半。
- (f) 一拍半在 120 BPM 是 0.75 s、在 90 BPM 是 1.0 s。Clip 的起點留在第 2 小節，播放頭留在它的拍子上，所以 clip 裡的 audio 往前跳 0.25 s——12000 個 sample。不跟著 tempo 的 clip 跳得更遠：播放頭動了而它沒動。

```text
Exercise 2 - audio and tempo by the numbers
(a) at 120 BPM: speed 0.9375, -1.117 semitones; at 140 BPM: speed 1.0938, +1.551 semitones
(b) max|f''| = 6.782e-08 s/tick^2: 45 pieces of 341.3 ticks, 46 markers; one piece is off by at most 0.9877 ms
    the first piece at its middle: 0.9661 ms off
(c) the sinc reaches 16 samples to each side at speed 1 (33 in all), 32 at speed 2 (65 in all)
(d) 93.75 pieces a second, 49.2 million multiplications a second of output
(e) 300 s of output: 115.2 MB of audio, and 115.2 MB of positions while it is rendered
(f) 0.750 s into the clip before, 1.000 s after: the audio jumps 0.250 s (12000 samples) ahead
[PASS] l20_ex2 (12 checks passed)
```

## 練習 3 - Take 用最少的 marker

[`solutions/ex3_fewest_markers.cpp`](ex3_fewest_markers.cpp)

和第 19 章練習 5 是同一個程式，只是換了一條曲線。`fewest[j]` 是從第一個候選點到候選點 `j` 最少需要的 marker 數；它等於「從某個較早的候選點 `i` 畫直線到 `j`、中間每個候選點都在容許值內」的那些 `i` 當中，最好的 `fewest[i]` 再加一。從最後一個候選點往回走就得到清單。Greedy——一次又一次取最長的一段——不一定最少，理由和第 19 章一樣；和所有選法比對的檢查會抓到它。

```text
Exercise 3 - the fewest markers for a take
  150 small takes: 150 with as few markers as the best of every choice, 150 well formed, 150 within the tolerance
  a take of 60 to 120 BPM within 1.0 ms:  33 markers, Step 1's equal pieces  46 (the worst sample 0.9864 ms off, 0.9865 between candidates)
  a take of 60 to 120 BPM within 0.1 ms: 102 markers, Step 1's equal pieces 143 (the worst sample 0.0951 ms off, 0.0951 between candidates)
  a take of up and down   within 1.0 ms:  59 markers, Step 1's equal pieces  87 (the worst sample 0.9911 ms off, 0.9916 between candidates)
  a take of up and down   within 0.1 ms: 187 markers, Step 1's equal pieces 268 (the worst sample 0.1000 ms off, 0.1000 between candidates)
[PASS] l20_ex3 (11 checks passed)
```

每次都少約 30%。省下的都在 ramp 快的地方：曲線以 $`1/B^2`$ 彎曲，所以在 120 BPM 下，同樣的誤差一段可以比在 60 BPM 下長一倍，而依慢的那一端決定長度的等分就浪費了這個差距。在候選點之間（這裡相隔 5 ms），誤差最多比候選點多 0.0005 ms。

值得用 dynamic programming 嗎？它的成本是候選點數的平方；Step 1 的公式只有一行，而且根本不需要候選點。對於專案檔裡幾個在 ramp 上錄的 take，少 30% 的行數無關緊要。對於手動編輯的 warp——每個 marker 都是畫面上的一個把手——就有關係了。

## 練習 4 - 對準到 sample 的小數位置

[`solutions/ex4_fine_fit.cpp`](ex4_fine_fit.cpp)

分數在它最好的偏移附近是平滑的，所以三個值就足以找出頂點：通過它們的拋物線。新的起點是小數，由此帶出三件事：下一段的 natural continuation 也是小數（搜尋之前先用 Hermite 讀出來），小段本身用 Hermite 讀，而「它是不是已經就是 natural continuation？」這個判斷要用容許值比較——速度為 1 時候選位置是整數 sample，continuation 是前一個起點加 512，也是整數，而且不能滑動。

```text
Exercise 4 - a fit to a fraction of a sample
  noise at speed 1: at most 0 from the input
  220 Hz at speed 0.50: what is not the tone  96.4 dB below it (whole samples: 36.9 dB), +0.000 cents
  220 Hz at speed 2.00: what is not the tone  96.6 dB below it (whole samples: 43.3 dB), -0.000 cents
  440 Hz at speed 0.50: what is not the tone  91.0 dB below it (whole samples: 30.9 dB), -0.000 cents
  440 Hz at speed 2.00: what is not the tone  90.4 dB below it (whole samples: 39.6 dB), +0.000 cents
  the fit to a fraction is at least 51 dB better on these 8 tones
  1 kHz at speed 0.80 (a period of 48 samples): whole samples 155 dB, a fraction 83 dB
[PASS] l20_ex4 (19 checks passed)
```

從 31–43 dB 到 90–97 dB：Step 4 所說的「它的」誤差，幾乎全是那半個 sample。現在剩下的是用三次曲線讀 sample 之間的值的誤差——Step 3 量到 5 kHz 時是 49 dB，頻率越低誤差越小，所以 220 Hz 比 440 Hz 好。

最後一行是代價。週期剛好是整數 sample 的音——48 kHz 下的 1 kHz 是 48 個——用整數偏移就能*精確*對準；拋物線的頂點和整數差了一點點，開始在 sample 之間讀取，結果是 83 dB：仍然遠低於聽得到的程度，但比原本差。改用 Step 3 的 sinc 來讀而不是 Hermite，可以拉回來，代價是 sinc 的成本。大多數真實的 audio 不是週期剛好整數 sample 的單音，所以對準到小數位置還是贏。

## 練習 5 - 跳動時的 fade

[`solutions/ex5_declick.cpp`](ex5_declick.cpp)

這個 class 在呼叫之間保留兩個數——讀取原本會往哪裡走，以及它原本的速度——fade 期間再為舊路徑保留同樣的兩個數。第一個位置離第一個數太遠就是跳動。接下來的 `fadeSamples` 個 output sample 兩條路徑都要讀；新的乘上 $`\sin(t\pi/2)`$、舊的乘上 $`\cos(t\pi/2)`$，兩者的功率相加是 $`\sin^2 + \cos^2 = 1`$，這對錄音裡兩段互不相關的部分是正確的總和。Fade 可能跨好幾次呼叫，所以它的進度是 member，而不是迴圈變數。

```text
Exercise 5 - a fade over a jump
  120 to 90 BPM while a 440 Hz clip whose start follows plays: the largest step from one sample to the next 0.953 without a fade, 0.029 with one
    (the tone's own: 0.029)
[PASS] l20_ex5 (16 checks passed)
```

沒有 fade 時，波形從一個 sample 到下一個跳了將近整個峰對峰值（1.0 裡的 0.95）：這就是 click。有 10 ms 的 fade 時，沒有任何一步比單音本身的更大。

有兩件事值得一看：

- **誰會跳。** 用 varispeed 或 stretch 跟著的 clip 不會：它的位置是 tick 的函數，而播放頭保住了 tick。檢查在用「起點跟著」的 clip 製造跳動之前，先用一行證明了這點。Step 2 的兩個 clip 裡，完全不跟著的那個跳得更遠。
- **它該放在哪裡。** 在 DawLab 裡，audio thread 一個 sample 一個 sample 地讀 clip（`renderClip()`），跳動發生在 block 之間——seek、loop 繞回、tempo 改變時。在 audio thread 上為每個 clip 放一個 Declicker，正是 DAW 的做法；跳動後的 10 ms 要讀兩次，狀態只有幾個數，不需要配置記憶體。第 14 章的 engine 沒有這樣做，所以 Step 2 說「seek 仍然會有 click」。

## 練習 6 - Transient

1. **找到敲擊。** 在很短的 window（5-10 ms）裡，看出現了多少*新的*能量——能量的上升，或者更好的 spectral flux：各頻帶增加量的總和，這樣即使敲擊被很大聲的貝斯蓋住也看得到。Onset 就是這條曲線超過門檻的峰值。固定的門檻會找到大聲段落的每一下，卻找不到小聲段落的任何一下：改用自適應的門檻（曲線移動中位數的某個倍數），並加上 onset 之間的最小距離（30-50 ms），這樣一下帶有嘎嘎聲的敲擊不會變成三下。
2. **一下敲擊一個小段。** 在 onset 處切小段，而不是每半個 window 切一次。從每個 onset 起，把 attack（最初 10-30 ms）以速度 1 複製，並準確放在 onset 的 tick 現在所在的位置——這樣敲擊就在拍子上，而且只聽到一次。所有的伸縮都發生在這一段剩下的部分，直到下一個 onset：在那裡用 WSOLA，比例更大，因為 attack 沒有分擔任何伸縮。
3. **拉長和壓縮。** 拉長時，這一段的 decay 變長——WSOLA 會重複持續部分的小段；如果沒有持續部分（很乾的小鼓），下一下之前就是安靜，這正是鼓手打慢一點時會發生的事。壓縮時，decay 被截短：這一段可能比它的 attack 還短，那麼 attack 就在下一下開始處淡出。沒有任何一下被丟掉，每一下都完整；代價是 cymbal 的尾巴。
4. **好幾支麥克風。** 它們必須共用 onset——在 clip 的總和（或最大聲的那個）上找一次，套用到全部——以及切點：否則近距離麥克風和 overhead 裡的大鼓會被彼此錯開幾毫秒，鼓組聽起來會變空（comb filtering）。這和 Step 5 裡 stereo clip 的兩個 channel 一起滑動是同一個理由。
5. **在哪裡、什麼時候。** 每個 clip 一次、事先進行，在 clip 被設成跟著 tempo 的時候：audio 不會變，會變的只有 tempo。它對 clip 長度是 O(n)，每個 window 一次 FFT——比 stretch 便宜——結果和 clip 存在一起：一串 source sample。每個 onset 都是天生的 warp marker，因為敲擊應該落在拍子上：把 onset 當成候選點，練習 3 就能選出 marker，練習 8（之後會做）則把它們 quantize。
