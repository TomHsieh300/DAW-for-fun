# Step 2 - 精準到 sample 的播放

[English](step-02-playback.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 1 - Automation 曲線](step-01-curve.zh-TW.md) | 下一步：Step 3 - Automation 與使用者（規劃中）

## 目標

讓 audio thread 以每個 sample 一個值的方式跟著曲線走，過程中不配置記憶體；並且在使用者 seek、transport loop，或播放途中曲線被換掉時，
結果都還是對的。

## 設計

### 為什麼要每個 sample 一個值

第 15 章的參數每個 block 讀一次。用這個方式播一條一秒內從 0 到 1 的 ramp，在 48 kHz、每個 block 512 個 sample 的情況下，gain 每次跳
0.0107，一秒跳大約 94 次：這是聽得到的階梯，也就是 zipper noise。每個 sample 一個值，每步只移動 0.00002。

### 曲線是 immutable 的

audio thread 不能拿 lock、不能釋放記憶體，而且一直在讀曲線。所以編輯絕不會改動 audio thread 正在讀的那條曲線。這和第 14 章 track
的 clip list 是同樣的取捨：

```mermaid
flowchart LR
    A["UI：複製曲線，<br/>修改複本"] -->|"新的 pointer"| B["audio thread：<br/>exchangeCurve()"]
    B -->|"舊的 pointer"| C["UI：delete 舊曲線"]
```

`exchangeCurve()` 把舊曲線回傳，自己什麼都不釋放。這個 snapshot 沒有 thread：兩個角色都由 check 扮演。Step 5 才會讓 pointer 走
engine 的 command 和 garbage queue。

### 一個 block 是一串 segment

loop 在 block 中間折返時，一個 block 就由兩段以上的時間軸組成。transport 本來就是這樣描述 block 的（第 14 章），所以 lane 一次填一段：

```cpp
for (int s = 0; s < numSegments; ++s) {
    const TimelineSegment& segment = segments[s];
    curve->render(segment.timelineStart, out + segment.blockOffset, segment.numSamples, cursor_);
}
```

沒有 segment 代表 transport 停著。這時 block 裡是曲線在 playhead 位置的值，所以就算沒有在播放，畫面上的 fader 和聲音仍然會跟著 seek。

### cursor 是 hint，而且會自己檢查

Step 1 的 `render()` 每個 block 搜尋一次。多數 block 只是接著上一個 block 繼續，所以用 `CurveCursor` 記住那一段。明顯的危險是記住的 index
會過期：seek 之後、loop 折返之後，或換上一條點比較少的新曲線之後。一種解法是把這些事件全部通知 cursor，然後每次有人忘記通知就修一個 bug。

另一種解法是第 15 章對 track slot 選的那一種：**hint 不是事實**。我們要的答案由兩個比較定義，所以 `locate()` 直接比較：

```math
x_i \le \text{position} < x_{i+1}
```

如果記住的 `i` 滿足這兩個比較，那麼不管中間發生過什麼事，`i` 都是對的，因為點是排序過的。不滿足的話，`locate()` 就搜尋。沒有人需要讓
cursor 失效，seek 或換曲線也都沒有專屬的程式路徑。範圍檢查 `-1 <= i <= last` 也是同一個想法：這個 index 可能來自一條比較長的曲線。

一個 block 結束後，cursor 記住的是 block **最後**一個 sample 的那一段，不是第一個。這對每個 block 有好幾個點的錄製曲線很重要：如果記住
第一個 sample 的那一段，每個 block 都得搜尋。

這能省多少？十個點的曲線上省不了多少：binary search 只要四次比較。但在很密的錄製曲線、幾十條 lane 的情況下，它讓每個 block 不必走過散落
各處的記憶體。這是一個小小的最佳化，而它之所以可以接受，只因為它不可能出錯。

### 跳躍不能有 click

seek、loop 折返、換曲線和 Hold 的階梯都會讓值跳動，而會跳的 gain 就會產生 click。但第 15 章 50 ms 的 fader smoother 在這裡不適用：它會讓每條
曲線都晚 50 ms，還會把每個轉角磨圓。所以 lane 只限制值每個 sample 能移動多遠：

```math
y[i] = \begin{cases}
y[i-1] + s & x[i] - y[i-1] > s \\
y[i-1] - s & y[i-1] - x[i] > s \\
x[i] & \text{其他情況}
\end{cases}
```

預設 $s = 1/64$。比這個慢的曲線會**完全不變**地通過，而這句話有被測試：最後一行是直接指定 `x[i]`。看起來很自然的
`y += clamp(x - y, -s, s)` 幾乎一樣好：兩個接近的 float，差值是精確的，加回去也是精確的。只有兩者大小差很多時才會捨入，在隨機操作的 3084245
個值裡出現了 10 次。直接指定就完全沒有這種情況。跳躍則變成一段最多 64 個 sample（48 kHz 時是 1.3 ms）的直線 ramp，而且剛好從跳躍那個 sample 開始。

這個限制必須在 block 之間記住上一個值，否則發生在 block 邊界上的跳躍就看不到了。lane 渲染的第一個 sample 沒有上一個值，就直接採用：lane
不應該從 0 淡入。

這一步留給後面的兩個限制：開關和選項不能經過中間值（Step 5 會每個 block 讀一次），而 64 這個數字應該由 sample rate 算出來（也是 Step 5）。

### 沒有曲線：knob

沒有曲線時，lane 用 knob 的值填滿 block，一樣經過這個限制。所以播放途中移除曲線時，值會滑到 knob 的位置，不會有 click。

## 測試

`check.h`（22 項檢查）。上面那條 ramp 的全部 48000 個 sample 都必須等於 `valueAt()`。在起點 800 的 block 裡，位於 sample 1000 的跳躍必須從
offset 200 開始移動，走 32 步 1/64 後到達。同一段播放用 1、64、300、512 和隨機大小的 block 跑，數字必須完全相同：block 大小不可以改變聲音。
另外還有：512 個 sample 的 block 裡有 100 個 sample 的 loop、播放中和停止時的 seek、knob、換成點比較少的曲線、一段播放要搜尋幾次，以及用計數的
**operator new** 跑 2000 個 block。

接著用 200 段隨機操作（seek、loop、停止、換曲線、動 knob）和一個一次只處理一個 sample 的參考實作比較：位置每次加一並且折返、`valueAt()`，
再加上一個 limiter。

<div align="center">

| 故意弄壞的地方 | 被誰抓到 |
|---|---|
| 不比較位置就相信 hint | seek、隨機操作 |
| hint 沒有範圍檢查 | 比較短的曲線（crash，或 AddressSanitizer） |
| 不檢查下一個點是否在位置之後 | 直接呼叫 `locate()`、密集曲線 |
| 永遠搜尋 | 搜尋次數 |
| cursor 記住 block 第一段 | 密集曲線（1000 次搜尋，而不是 40 次） |
| 忽略 `blockOffset` | block 大小、隨機操作 |
| 停止時用位置 0 的值，不是 playhead 的值 | 停止時的 seek、隨機操作 |
| 沒有曲線時忽略 knob | knob、移除曲線、隨機操作 |
| block 之間忘記上一個值 | block 大小、seek、移除曲線 |
| 第一個 sample 從 0 開始 ramp | 第一個 sample、knob、隨機操作 |
| 用 `y += clamp(x - y, -s, s)`，而不是 `y = x` | 隨機操作（3084245 個值裡有 10 個） |
| 完全沒有限制 | 跳躍、loop、seek、隨機操作 |
| `render()` 裡有 `std::vector` | 配置次數 |

</div>

其中兩項一開始是通過的。少了「下一個點」的檢查時，`render()` 往前走的迴圈會把答案修正回來，所有值都對，只是多走一段路；少了 cursor 的更新時，
hint 從 block 開頭留下來，稀疏的曲線幾乎察覺不到。兩者都不會讓聲音出錯，但都違背了關於成本的承諾。所以 check 加上了直接呼叫 `locate()` 和一條
密集曲線。一個通過的反向測試，告訴你的是你的測試還沒說清楚什麼。

### GCC 又幫了一次忙

配置計數器一開始是用一行包住 `malloc` 的方式取代 `operator new`。GCC 在 `-O1` 發出 `-Wmismatched-new-delete` 警告：它把取代的函式 inline 之後，
看到的是對 `operator new` 的記憶體呼叫 `free()`。MSVC 沒有說話。現在計數器和第 14 章 Step 10 一樣經過 helper 函式，GCC 從 `-O0` 到 `-O3` 都
沒有警告。

## 動手做

建置並執行 starter（不需要 JUCE）：

```powershell
.\scripts\build.ps1 -Only 16-automation
.\build\bin\Debug\l16_step02_starter.exe
```

在 `steps/step02-playback/starter/step02.cpp` 寫出四個函式：

1. `CurveCursor::locate()`：hint 正確就用它；否則搜尋、計數、記住。
2. 有 cursor 的 `AutomationCurve::render()`：Step 1 的 `render()`，用 `locate()` 取代搜尋，最後把最後一段存回 cursor。
3. `AutomationLane::render()`：knob、停止，或每個 segment 一次 `render()`。然後套用限制。
4. `AutomationLane::limitSlew()`：上面的公式，跨 block 有效。

完成後，輸出的最後會是：

```text
  667 blocks through 10 points: 4 searches (expected at most 11); 20 seeks: 7 more (at most 20)
  1000 blocks through a point every 100 samples: 40 searches (expected 40)
  locate() with a hint far behind the position: 7 (expected 7)
  2000 blocks with seeks, a loop and a swap: 0 allocations (expected 0)
  200 random sessions (seeks, loops, stops, swaps, knob moves), 3084245 values: 0 differ from
  the reference (expected 0)
[PASS] l16_step02 (22 checks passed)
```

## 你學到了

- 每個 block 一個值是階梯；會動的參數需要每個 sample 一個值。
- immutable 資料加上 pointer 交換，讓 audio thread 不需要 lock，也不需要 `delete`。
- 會自己驗證的快取 index 不需要失效機制：seek、loop 和換曲線完全不需要專屬的程式碼。
- 限制值的移動速度，而不是把值平滑化：慢的動作維持精確，跳躍不再有 click。
- block 大小不可以改變結果。這很容易測試，而且能找到真正的 bug。
