# 第 16 章 - 解答
[English](README.md) | **繁體中文**

[回到本章](../README.zh-TW.md) | [練習](../exercises/README.zh-TW.md)

用 `.\scripts\build.ps1 -Only 16-automation -TestLabel solution` 執行所有解答程式（練習 7-9 要加 `-Juce`）。每一個都以 `[PASS] l16_exN (... checks passed)` 結束。

---

## 練習 1 - 誰可以碰曲線？

1. **Mutex。** Audio thread 每個 block 都會讀曲線。如果 UI 在編輯時持有一個鎖（而插入一個點可能會配置記憶體），audio thread 就要等一個優先權較低的 thread：priority inversion，等待時間超過 block 剩下的時間就會斷音。曲線不可變的話，audio thread 從不等待：指標給它哪條曲線它就讀哪條，舊的新的都是完整的。它不刪除舊曲線，因為釋放記憶體可能在 allocator 裡取鎖，時間也沒有上限；它把指標交回去，由 UI 刪除（Step 5 透過 garbage queue 傳送）。
2. **誰來蓋章。** UI 只大概知道 transport 在哪裡：它讀到的位置是某個 block 當時的位置，而它聽到的音訊更晚才離開喇叭，block 也可能長達 85 ms。Block 內的 loop wrap 或 seek，會讓「那個位置」變成兩個位置。Audio thread 準確知道自己正在 render 哪些 timeline sample，逐段（Step 2 的 `TimelineSegment`）知道，所以它把值蓋上它所屬的那一段。一次 wrap 就會呈現為兩個 event，recorder 在那裡結束這個 pass。
3. **很短的點擊。** Audio thread 每個 block 讀一次 `touching`。在一個 4096-sample block（85 ms）內開始又結束的點擊，永遠不會被看到是 `true`。計數器只會往上加，所以下一個 block 看到不同的計數，就知道發生過一次 touch。Touch 不需要它：在 Touch 模式下使用者只在*碰著的時候*贏，已經結束的點擊沒有東西可贏。在 Latch 模式下，這一下本來應該接管到 transport 停止為止。
4. **不見了，或是不認識。** 被刪掉的 track 的 lane 指向一個在*這個*專案裡已經不存在的東西，而且是使用者刪的：edit 把它們一起帶走，反向操作再帶回來，沒有任何 lane 會指向空無。不認識的參數不同：target 存在，只是這個版本無法解讀——檔案多半來自效果器的新版本。刪掉 lane 等於銷毀程式看不懂的資料，也就是某人做的錄製。所以它留著、不發聲，存檔時原樣寫回。
5. **Latency。** Fader 處的音訊比 transport 的位置舊 100 samples：它經過了 limiter 的 lookahead。在 transport 的位置讀，曲線會比它所屬的音訊早 100 samples（2 ms）上升——一個應該大聲開始的鼓擊，它的 attack 還停留在舊的、較低的 gain。在 $`x_{\text{transport}} - L`$ 讀，曲線就跟著它的音訊移動。
6. **每個 block 一個值。** 代價：效果器的 automation 取決於 block 大小（變化在下一個 block 邊界才生效，512 時最多晚 512 samples），而且效果器若不做 smoothing，快速的掃動會變成階梯。開關需要它，是因為中間值不是一段 ramp：濾波器類型從「low pass」經過「high pass」30 個 sample 再變成「band pass」，會是聽得到的 glitch。開關必須一步、一次就切換。

## 練習 2 - 用數字看 automation

- **(a)** 每秒 $`48000 / 512 = 93.75`$ 個值。2 秒的 fade 每個 block 移動 $`1 / (2 \cdot 93.75) = 0.00533`$，也就是 $`0.00533 \cdot 60 = 0.32`$ dB：每秒 94 次、每次 0.32 dB 的階梯，就是 zipper noise。每個 sample 一個值時每次移動 $`60 / 96000 = 0.000625`$ dB。
- **(b)** 上限是每個 sample $`1/64`$。$`0.6 / (1/64) = 38.4`$，所以 ramp 需要 **39 個 sample**（0.8125 ms）：38 個完整的步伐到 0.79375，第 39 步落在 0.8。從 0 跳到 1 剛好 **64 個 sample**（1.333 ms）。
- **(c)** 從第一點到最後一點的直線是 $`v = x/6`$（$`x`$ 以 4800 為單位）。距離分別是 0.067、0.113、0.200、**0.247**、0.067：第 4 點最遠，且超過 0.05，所以留下。它的左邊，從 (0, 0) 到 (4, 0.42) 的直線斜率 0.105：距離 0.005、0.010、0.015——都低於 0.05，丟掉。它的右邊，從 (4, 0.42) 到 (6, 1.00) 的直線在 $`x = 5`$ 是 0.71：距離 0.19，留下第 5 點。**留下：0、4、5、6。** Tolerance 從 0.247 起只剩兩端（程式用 0.25）。0.002 在 60 dB 的 gain 上是 **0.12 dB**。
- **(d)** $`30 \cdot 93.75 = 2812.5`$，所以 **2813** 個點，**45 008 bytes**。直線 fade thin 之後**只剩兩端**。緩慢的晃動（0.5 Hz、±0.2）會留下幾百個點（0.002 時 365 個）：曲線在哪裡彎，就需要多少點才能維持在 tolerance 之內。

```text
Exercise 2 - automation by the numbers
(a) 93.75 values a second; a 2 s fade moves 0.005333 a block = 0.320 dB; per sample 0.000625 dB
(b) 0.2 -> 0.8: 39 samples = 0.8125 ms; 0 -> 1: 64 samples = 1.3333 ms
(c) kept: 0 4 5 6
    tolerance 0.002 on a 60 dB fader = 0.120 dB
(d) 30 s: 2813 points = 45008 bytes before thinning
    after thinning (0.002): a straight fade 2 points, a 0.5 Hz wobble 365 points
[PASS] l16_ex2 (13 checks passed)
```

## 練習 3 - 帶 tension 的曲線段

[`solutions/ex3_curve_shape.cpp`](ex3_curve_shape.cpp)

`curveFraction()` 用 NaN 必定不成立的比較來 clamp tension（和 Step 1 的 `clamp01()` 一樣），然後計算 `pow(t, e)` 或 `1 - pow(1 - t, e)`。$`k = 0`$ 時 $`e = 4^0 = 1`$，而 `pow(t, 1.0)` 正好就是 $`t`$，所以 tension 0 和 Linear 逐位元相同——公式 $`v_i + (v_{i+1} - v_i)\,f`$ 也沒有改。`between()` 就是 Step 1 的版本，在 `Shape::Curve` 時用 `f` 取代 `t`。

```text
Exercise 3 - a curved segment with a tension
  a fade with tension 0.5 is at 0.2500 halfway (a line: 0.5000), with -1 at 0.9375
[PASS] l16_ex3 (45 checks passed)
```

**在檔案裡。**最直接的寫法是 `point 1 48000 0.25 curve 0.3`。只認識 `linear` 和 `hold` 的 DawLab 16 會在 `parseShape()` 失敗，拒絕**整個專案**——使用者因為一個點的形狀就打不開一首歌。比較好的做法：點照舊寫（`point 1 48000 0.25 linear`），tension 另起一行（`tension 1 48000 0.3`），並升一個 minor 版本。舊版本會跳過這個未知的行（第 15 章練習 4）、保留它，並把那一段當直線播放：不完全一樣，但很接近，而且什麼都沒遺失。

## 練習 4 - Trim

[`solutions/ex4_trim.cpp`](ex4_trim.cpp)

1. 斷點：pass 的每一點，以及舊曲線上嚴格位於 pass 兩端之間的每一點，排序後各取一次。
2. 新曲線：`first` 之前的舊點、在 `first` 處舊直線的終點，接著每個斷點的和——在舊曲線跳變或 hold 結束的地方（`valueBefore() != valueAt()`）放兩次，讓跳變仍是跳變。然後在 `last` 回到舊值，接上 `last` 之後的舊點。

`AutomationCurve::add()` 會 clamp 每個值，並讓同一位置的點保持加入的順序：處理跳變只需要這些。

```text
Exercise 4 - trim
  a trim over a hold and a jump: off by 6.0e-08 inside, 6.0e-08 outside
[PASS] l16_ex4 (20 checks passed)
```

**點之間的 clamp。**舊曲線在一段裡從 0.5 到 0.9，trim 加上 0.3：兩端變成 0.8 和 $`\min(1.2, 1) = 1.0`$，新直線從 0.8 到 1.0。但真正的 $`\text{clamp}(\text{old} + \text{offset}) = \min(0.8 + 0.4t, 1)`$ 在一半處就到 1.0 並停在那裡。中間的直線太低了。要做到精確，就在和越過 0 或 1 的地方加一點——也就是 $`v_a + (v_b - v_a)\,t = 1`$ 的位置——轉角就會落在正確的地方。

## 練習 5 - 複製和貼上 automation

[`solutions/ex5_clipboard.cpp`](ex5_clipboard.cpp)

- `copyRange()` 需要兩個光靠點給不出來的值：曲線在 `start` 的值（可能落在某段的中間），以及曲線從左邊到達 `end` 時的值（`valueBefore()`：那裡如果有跳變，取跳變之前的值，不是之後的）。在 0 的那一點沿用它所在那一段的形狀，所以從 hold 中間開始複製的片段也會 hold。
- `pasteChange()` 依序建立新曲線：`at` 之前的舊點；舊直線在 `at` 的終點；clip；舊曲線在 $`at + length`$ 的值（和形狀）；之後的舊點。同一位置加入的點保持順序，所以值不同時兩個邊緣都會成為跳變。沒有 lane 的 target 會得到一條 Read 模式的新 lane，預設值取自 clip。

```text
Exercise 5 - copy and paste automation
  pasted after the end: off by 0.0e+00 outside, 6.0e-08 inside
  pasted over the middle: off by 0.0e+00 outside, 6.0e-08 inside
  pasted at 0: off by 0.0e+00 outside, 6.0e-08 inside
  pasted onto the compressor's threshold: off by 0.0e+00 outside, 6.0e-08 inside
[PASS] l16_ex5 (52 checks passed)
```

範圍內那一點點差異，來自從某段中間的新點開始內插，而不是從原本的點：同一條直線，只是捨入不同。

## 練習 6 - Automate 一個 VST3 plugin

一種設計；也有其他可行的做法。

1. **Sample accuracy——三種方式。**
   - *切開 block*：在每次變化處切開，每一片用新值呼叫 plugin 一次。對 Hold 的階梯是精確的，但很多次小呼叫代價很高（每次呼叫都有固定成本），而 ramp 仍然需要每隔幾個 sample 切一次。
   - *把點傳進去*：透過 plugin 自己的參數 queue（帶 sample offset 的 `IParameterChanges`）。這正是 VST3 的設計用途，不需額外成本，但 JUCE 的 `AudioProcessor` wrapper 沒有把它開放給 host：host 必須自己直接和 VST3 介面打交道。
   - *每個 block 一個值*，讓 plugin 自己 smoothing，就像 DawLab 的做法：便宜又簡單，但結果取決於 block 大小，也取決於每個 plugin 怎麼 smoothing。
2. **從曲線到點。**Block 第一個 sample 處的值；接著 block 內曲線每一點的值（跳變處兩個：之前和之後）；以及 block 最後一個 sample 處的值。這些點之間曲線是直線，在 queue 點之間線性內插的 plugin 聽到的就正是這條曲線。Hold 段需要在階梯之前重複一次它的結束值，否則 plugin 會把它內插成斜坡。
3. **Plugin 自己的視窗。**VST3 plugin 會宣告一個 gesture：`beginEdit()`、`performEdit()` 的值、`endEdit()`。JUCE 把它們轉成 `AudioProcessorParameter::Listener::parameterGestureChanged()` 和 `parameterValueChanged()`。Host 把 begin 對應到 `touch()`、每個值對應到 `setKnobValue()`、end 對應到 `release()`——和 DawLab 自己的 fader 完全一樣——所以 Touch、Latch 和錄製都照樣運作。
4. **Latency。**位於 2000-sample lookahead 之後的 plugin，聽到的音訊是 2000 samples（42 ms）之前的。沒有平移的 lane 會提早 42 ms 移動參數：應該在鼓擊時打開的濾波器掃頻，會在鼓擊之前就打開。Lane 應該在 transport 位置減去 chain 中它前面所有東西的 latency（再加上 track 的 PDC，和 fader 一樣）的地方讀取，錄下的 gesture 也要同樣平移。
5. **身分。**VST3 參數有一個 `ParamID`，plugin 承諾在各版本之間保持不變的數字——不像名稱，可能被翻譯或改名。效果器要存 plugin 的 class id，lane 要存 `ParamID` 和名稱：用 id 找到參數，plugin 不見時用名稱讓使用者看到一點東西。

## 練習 7 - Automation lane 的畫線工具

[`solutions/ex7_line_tool.cpp`](ex7_line_tool.cpp)

`drawLine()` 就是用兩點的 pass 呼叫 `engine::punch()`：punch 本來就會保留範圍外的曲線，並在需要時於兩端加上跳變。`drawLineInLane()` 複製 lane 的內容（或從 `LaneContent{}` 開始，和 `Session::addPoint()` 一樣）、修改曲線，再執行一個 `SetLane`。工具記住按下的位置，拖曳時把終點限制在 lane 的範圍內，放開時把兩端轉成 sample（`snapToBeat(xToSample(x), !Alt)`）和值（`yToValue()`）。

```text
Exercise 7 - a line tool for automation lanes
  a line from 120000 (0.08) to 384000 (0.92): 4 points, undo "Draw line Gain"
[PASS] l16_ex7 (31 checks passed)
```

**為什麼蓋在上面。**Mouse listener 會*和* arrangement *一起*收到事件：在 automation lane 上按下，會在那裡新增一點（arrangement 自己的行為），同時又開始一條線。蓋在上面、有 `hitTest()` 的 component 則是*取代* arrangement 接收事件——但只在它說好的地方，所以 clip、標頭和 ruler 的行為都和原本一樣。

## 練習 8 - 碰到控制項，就看到它的 automation

[`solutions/ex8_follow_touch.cpp`](ex8_follow_touch.cpp)

`attach()` 把舊的 `onTouch` 移進新的 lambda，先呼叫它，在 touch（不是放開）時以參數的位址呼叫 `touched()`。`touched()` 除非跟隨已開啟、位址是存在的 track，否則提早返回；`showAutomation()` 取代 track 原本顯示的 lane，`setSelectedTrack()` 選取它並捲進畫面（這個解答的第一版還多呼叫了 `scrollToTrack()`；把它拿掉的 mutant 什麼都沒改變，就是這樣發現它是多餘的）。

```text
Exercise 8 - touch a control, see its automation
  touched Threshold on track 7: its lane is shown at y 287..359 of 359
[PASS] l16_ex8 (22 checks passed)
```

## 練習 9 - Lane 選單：thin、清除、移除

[`solutions/ex9_lane_menu.cpp`](ex9_lane_menu.cpp)

`thinCurve()` 走過所有點一次。每個錨點結束一段從前一個錨點開始的 run；這段 run 交給 `engine::thin()`（它的第一點和最後一點就是那兩個錨點），保留下來的點接在第一點之後加入。Hold 點之後的那一點必須是錨點：hold 在那裡以跳變結束，如果那一點剛好落在從 hold 到下一點的直線上，thinning 會把它丟掉，hold 就會持續太久。比 tolerance 小的跳變也會被 thinning 丟掉；但那是使用者畫的，所以要保留。

選單在打開時就計算好 thin 之後的曲線，所以項目可以寫出「154 -> 67」，在什麼都不會少時停用。

```text
Exercise 9 - a lane menu: thin, clear, remove
  thinned 154 points to 67, off by at most 0.00199 (tolerance 0.002)
  menu: "Thin Points (154 -> 67)"
[PASS] l16_ex9 (29 checks passed)
```

---

[回到本章](../README.zh-TW.md) | [練習](../exercises/README.zh-TW.md)
