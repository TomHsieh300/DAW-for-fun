# 第 16 章 - 練習
[English](README.md) | **繁體中文**

[回到本章](../README.zh-TW.md) | [解答](../solutions/README.zh-TW.md)

難度：★☆☆ 簡單、★★☆ 中等、★★★ 困難。實作題在 `exercises/starter/` 有一個可以編譯、可以執行的 starter 檔；在你完成之前，它的自我檢查會印出 `FAIL`（這是預期的）。方括號裡是 build target 的名稱。這些練習都不需要 JUCE：

```powershell
.\scripts\build.ps1 -Only 16-automation
.\build\bin\Debug\l16_ex3_starter.exe
```

**練習 1-6** 是本章的練習，針對各個 step（不需要 JUCE）。**練習 7-9 是選做的專題**，用來擴充 DawLab 16。它們需要 JUCE build（`.\scripts\build.ps1 -Only 16-automation -Juce`）；和第 15 章一樣，它們是 console 程式，在 offline 模式下建立真正的 `MainComponent`，而且都不修改 app 本身。

練習 3 建立在 Step 1 的曲線上，練習 4 和 5 建立在 Step 4 的 model 上（`exercises/model.h`：Step 4 的 `minidaw.h`，edit 都已經寫好）。請先做完這些 step。

---

## 練習 1 - 誰可以碰曲線？ ★☆☆（概念）

每題用幾句話回答。

1. Step 2 從不修改 audio thread 可能正在讀的曲線：UI 改的是複本，然後交換指標。為什麼不在 point 外面包一個 `std::mutex`？`exchangeCurve()` 又為什麼把舊曲線傳回來，而不是直接刪掉？
2. Step 3 讓 audio thread 替使用者的值蓋上 timeline 位置。UI thread 也知道 transport 的位置——為什麼它不能自己蓋？想想 loop、seek 和很大的 buffer。
3. 在 Latch 模式下，在 fader 上快速點一下也必須 latch。Buffer 是 4096 samples 時，audio thread 為什麼可能錯過這一下？`touch()` 裡的計數器怎麼解決？Touch 模式為什麼不需要計數器？
4. 刪除 track 時，它的 lane 會在同一個 edit 裡一起帶走。這個版本不認識的參數，它的 lane 則被保留下來、不發聲。為什麼這兩種情況的處理差這麼多？
5. 一個 track 上有一個 lookahead 100 samples 的 limiter。它的 fader 曲線在第 9 小節陡升。如果在 transport 的位置讀 lane，聽眾會聽到什麼？Step 5 為什麼在 transport 位置減 100 的地方讀？
6. Strip 的 fader 每個 sample 一個值；effect 的參數每個 block 一個值。後者的代價是什麼？為什麼開關（choice 參數）反而需要它？

## 練習 2 - 用數字看 automation ★★☆（計算）

先手算（寫出公式），再執行 [`l16_ex2_solution`] 核對。(a) 和 (c) 的 gain，正規化值對應 -60..0 dB，在 dB 上是線性的。

- (a) 48 kHz 下從 0 到 1、為時 2 秒的 fade，每個 512-sample block 讀一次（第 15 章）。每秒幾個值？每個 block 移動多少——正規化值和 dB 各是多少？每個 sample 一個值的話呢？
- (b) Step 2 的 slew limit，`declickSamples = 64`。從 0.2 跳到 0.8 要幾個 sample、幾毫秒？從 0 跳到 1 呢？
- (c) 一段錄下的 pass，每 4800 samples 一點：值為 0.00、0.10、0.22、0.30、0.42、0.90、1.00。用 Step 3 的規則、tolerance 0.05 手動 thin：哪些點留下來？tolerance 多大時只剩兩端？Step 3 預設的 tolerance 0.002 在這個 gain 上是幾 dB？
- (d) 30 秒的 fader 動作，每個 block 一個 capture event。Thin 之前有幾個點？每點 16 bytes 是多少 bytes？Thin 之後，一個直線 fade 和一個緩慢的晃動，你預期各剩多少？

## 練習 3 - 帶 tension 的曲線段 ★★☆（實作）

[`l16_ex3_starter`，檔案 `exercises/starter/ex3_curve_shape.cpp`]

替 Step 1 的曲線加上第三種形狀：`Shape::Curve`，由該點的 tension $`k`$（-1..1）決定彎曲。$`t`$ 是在這一段裡的位置（0..1），$`e = 4^{|k|}`$：

```math
f(t) = \begin{cases} t^{e} & k \ge 0 \\ 1 - (1-t)^{e} & k < 0 \end{cases}
\qquad v = v_i + (v_{i+1} - v_i)\, f(t)
```

- `curveFraction(t, tension)`：$`f(t)`$，tension 要 clamp，NaN 當作 0；
- `AutomationCurve::between()`：Step 1 的版本，加上新形狀。`valueAt()` 和 `render()` 都呼叫它。

自我檢查要求 $`f(0) = 0`$、$`f(1) = 1`$、曲線只升不降、鏡像規則 $`f_{-k}(t) = 1 - f_k(1-t)`$、tension 0 和 Linear **逐位元相同**、值永遠不離開該段的範圍、跳變之後接曲線，以及 block 大小為 1、37、512、4096 samples 時 `render()` 都等於 `valueAt()`。

想一想：Step 4 的檔案把一個點寫成 `point 1 48000 0.25 linear`。你會怎麼寫一個帶 tension 的 curve？只認識 `linear` 和 `hold` 的 DawLab 16 遇到這種檔案會怎樣——它原本可以怎麼做（第 15 章練習 4）？

## 練習 4 - Trim ★★★（實作）

[`l16_ex4_starter`，檔案 `exercises/starter/ex4_trim.cpp`]

Touch、Latch 和 Write 會用手的動作取代曲線。混音師喜歡某段人聲的 fader 動作，只是想讓第二段副歌高 0.2，他需要第五種模式 **Trim**：fader 變成一個 offset，錄下的 pass 是*加*到曲線上，而不是 punch 進去。在 Step 4 的曲線上寫出這個合併：

- `applyTrim(old, pass)`：從 pass 的第一個位置到最後一個位置，值是 $`\text{clamp}_{0..1}(\text{old}(p) + \text{offset}(p))`$；範圍外是舊曲線。在兩點之間，舊曲線和 offset 都是直線，所以新曲線在任一方有點的地方都需要一點，在舊曲線跳變或 hold 結束的地方需要兩點。
- `trimChange(project, target, pass)`：同樣的事，包成一個可以 undo 的 `SetLane`。

自我檢查會跨過一段 hold 和一個跳變做 trim（offset 平坦處 hold 仍然平坦、跳變仍是跳變），在舊曲線的點上和跳變上開始與結束，把超出範圍的 trim clamp 住，並 undo 一次 trim。

想一想：trim 超過 1 時，自我檢查只要求點上的值正確。為什麼兩個被 clamp 的點之間，曲線不一定等於 $`\text{clamp}(\text{old} + \text{offset})`$？要怎麼做才會相等？

## 練習 5 - 複製和貼上 automation ★★☆（實作）

[`l16_ex5_starter`，檔案 `exercises/starter/ex5_clipboard.cpp`]

選取曲線的第 5-8 小節、複製、在第 17 小節貼上——或貼到另一個參數。

- `copyRange(curve, start, end)`：曲線在 $`[start, end)`$ 上的部分，成為一個 `CurveClip`：在 0 的一點是 `start` 處的值，範圍內的點移動 $`-start`$，在長度處的一點是曲線到達 `end` 時的值。
- `pasteChange(project, target, clip, at)`：把 clip 放到 `at` 的 `Change`（一個 `SetLane`）。`at` 之前和 $`at + length`$ 之後，舊曲線保持不變——所以貼上時兩端可能都需要跳變，也可能需要在舊曲線某段的中間加一點。還不存在的 lane 會被建立。

自我檢查會複製一段裡面有 hold 和跳變的曲線，貼在結尾之後、蓋在中間、貼在 0，以及貼到原本沒有 lane 的 compressor threshold 上，並逐 sample 把結果和範圍外的舊曲線、範圍內的來源比較。每次貼上都是一個 undo 步驟，undo 會把專案完全還原。

## 練習 6 - Automate 一個 VST3 plugin ★★☆（設計）

DawLab 16 還沒有把 VST3 insert 的參數當成 target（Step 6，「這一步留下的問題」），內建效果器則每個 block 一個值（Step 5）。用文字設計正確的 plugin automation 需要什麼，包括：

1. **Sample accuracy。** VST3 plugin 以一串帶有 block 內 sample offset 的點來接收參數變化（`IParameterChanges`）。JUCE 的 `AudioProcessor` 介面則是每個 block 設一個值。有哪三種方式可以更接近 sample-accurate 的 automation？各自的代價是什麼？
2. **從曲線到點。** 一條 lane 在一個 block 內的曲線由 Linear 和 Hold 段組成。如果 plugin 在點之間線性內插，哪些點必須放進它的 queue，它聽到的才會是同一條曲線？
3. **從 plugin 自己的視窗錄製。** 使用者轉的是 plugin 編輯器裡的旋鈕，不是 DawLab 的。Host 怎麼知道一次 touch 開始和結束，讓 Touch 和 Latch 能運作？
4. **Latency。** Step 5 會依 fader 之前的 latency 平移它的 lane，但不動效果器的 lane。如果一個 plugin 在 chain 的後段，前面有 2000 samples 的 lookahead，會出什麼問題？
5. **身分。** Step 4 用名稱指稱參數。VST3 plugin 提供了什麼替代方案？專案檔應該存什麼？

## 練習 7 - Automation lane 的畫線工具 ★★☆（實作，JUCE）

[`l16_ex7_starter`，檔案 `exercises/starter/ex7_line_tool.cpp`]

DawLab 16 的 automation 只能一點一點畫。加上一個畫線工具：在 fade 開始處按下、在結束處放開，中間的內容就被一條直線取代。

- `drawLine(old, x0, v0, x1, v1)`：在那個範圍以直線取代的曲線（兩個方向都可以；同一個位置什麼都不畫）。Step 3 的 `engine::punch()` 已經做好了困難的部分。
- `drawLineInLane(session, lane, ...)`：同樣的事，算一個 undo 步驟「Draw line Gain」。
- `LineTool`，一個蓋在 `ArrangementView` 上的透明 component。啟用時它只在 automation lane 上接收滑鼠：在標頭、clip 和 ruler 上 `hitTest()` 都說不，所以它們照樣收到點擊。位置和 arrangement 自己的點一樣對齊拍子（Alt = 不對齊），線的終點也會留在它開始的那條 lane 裡。

自我檢查會由左到右、由右到左、按著 Alt，以及拖出 lane 外各畫一條線，每次都把 lane 和對原曲線呼叫 `drawLine()` 的結果比較，要求每條線一個 undo 步驟、點擊則不留步驟，最後全部 undo。

想一想：為什麼要用一個有自己 `hitTest()` 的 component 蓋在上面，而不像第 15 章練習 7 那樣在 arrangement 上掛一個 mouse listener？

## 練習 8 - 碰到控制項，就看到它的 automation ★☆☆（實作，JUCE）

[`l16_ex8_starter`，檔案 `exercises/starter/ex8_follow_touch.cpp`]

抓住 compressor 的 threshold，arrangement 就在它的 track 下方顯示 threshold 的 lane。

- `LaneFollower::attach(panel, ref)`：包裝 `ParameterPanel` 的 `onTouch`。App 自己的處理函式仍然要先執行——Touch 和 Latch 就是靠它知道有手放在 lane 上。
- `LaneFollower::touched(address)`：在 track 下方顯示 lane 並選取 track（這會把它捲進畫面）。Bus 和 master 沒有列：什麼都不做。
- `LaneFollower::addToMenu(menu)`：「Show Touched Automation」，開啟時打勾，放進 View 選單。

自我檢查會把各列撐高，讓那個 track 一開始不在畫面內，接著抓三個參數、放開一個控制項、碰 track 的 Gain、一個 bus 和 master，再從選單關掉跟隨——app 的處理函式無論如何都繼續執行。

## 練習 9 - Lane 選單：thin、清除、移除 ★★★（實作，JUCE）

[`l16_ex9_starter`，檔案 `exercises/starter/ex9_lane_menu.cpp`]

- `thinCurve(curve, tolerance)`：Step 3 thin 的是一個 pass，它的位置嚴格遞增。整條曲線有 hold 和跳變，它們必須原封不動。所以有些點是永遠保留的錨點——第一點和最後一點、每個 Hold 點和它的下一點、每個跳變的兩個點（不論多小）——只有兩個錨點之間純粹的直線段才交給 `engine::thin()`。
- `clearRange(curve, start, end)`：兩者之間嚴格不留任何點，從 `start` 的值到曲線到達 `end` 時的值畫一條直線。
- `addLaneItems(menu, session, lane)`：「Thin Points (n -> m)」、「Clear Between Locators」（loop 的 locator）和「Remove Lane」，各為一個 undo 步驟，只有在會改變東西時才啟用。

自我檢查會 thin 一條曲線，裡面有晃動、一段 hold、緊接在 hold 之後剛好落在直線上的一點、一個 0.001 的跳變，以及一段 49 點的直線 ramp；要求每個 sample 都在 tolerance 之內、hold 和兩個跳變都精確保留、ramp 只剩兩端。接著執行三個選單項目，並逐一 undo。

---

[回到本章](../README.zh-TW.md) | [解答](../solutions/README.zh-TW.md)
