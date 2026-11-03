# Step 3 - CPU 預算

[English](step-03-cpu-budget.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 2 - Track 池](step-02-track-pool.zh-TW.md) | 下一步：[Step 4 - 存檔與開啟專案](step-04-project-files.zh-TW.md)

## 目標

量測 audio thread 用掉了多少時間預算、計算有幾個 block 沒趕上，並把這兩者變成使用者看得懂、能採取行動的警告。
過程中你會看到：一台機器能播放幾個 track 是 CPU 時間的問題，而不是 track 數量的問題。

## 設計

### 一個 block 的預算

音訊裝置要一個 $`N`$ 個 sample 的 block，而且必須在它播完上一個 block 之前拿到。在 sample rate $`f_s`$ 下，
這留下

```math
T_{\text{budget}} = \frac{N}{f_s}
```

秒：48 kHz、64 個 sample 是 1.33 ms，1024 個 sample 是 21.3 ms。一個 block 的負載（load）就是 callback
實際花掉的時間除以這個預算：

```math
\text{load} = \frac{t_{\text{spent}}}{T_{\text{budget}}}
```

load 為 1.0 表示用掉了整個預算。超過 1.0 這個 block 就遲到了：裝置沒有東西可以播，聽的人會聽到喀一聲或一段空白。
這樣的 block 叫做 **overrun**。

### 是什麼限制了 track 數

Step 2 的 `kMaxTracks` 是記憶體上限：事先配置好 128 個 track 的空間。這些 track 能不能*播放*，取決於它們在做什麼。
自我檢查用 64 sample 的 block 餵給 meter 兩個假想的專案：

<div align="center">

| 專案 | 每個 block 的時間 | Load | 3 秒內的 overrun |
|---|---|---|---|
| 16 個只有 gain 的 track（每個 2 µs） | 32 µs | 2.4 % | 0 |
| 4 個有 convolution reverb 的 track（每個 400 µs） | 1.6 ms | 120 % | 每個 block |

</div>

4 個 track 就讓一台輕鬆跑 16 個 track 的機器超載。所以 DAW 不會拒絕第 17 個 track，而是顯示 CPU meter，讓使用者
自己決定：freeze 一個 track、把 buffer size 調大、換一個比較輕的 plug-in。這一步就是要做出這個 meter。

### 第 14 章的 meter 哪裡不對

DawLab 8 已經會顯示「CPU 12 %」（`AudioHost::getCpuLoad()`）。它量的東西是對的，但有兩個弱點：

1. **平滑化算的是 callback 次數，不是秒數。** 它每個 callback 往該次的 load 移動 10 %。64 sample 的 block 大約
   13 ms 就穩定下來，顯示會跳動；1024 sample 的 block 大約要 200 ms。同一個混音在不同 buffer size 下看起來不一樣。
2. **平均值會把真正要緊的 block 藏起來。** 穩定 40 %、中間只有一個 block 到 130 %，顯示仍然是 41.5 %。看起來
   一切正常，使用者卻已經聽到喀一聲。

### 以秒為單位平滑化

讓 one-pole filter 的係數跟著 block 本身的長度走，時間常數就是以秒為單位，不管 block 多大：

```math
a = e^{-T_{\text{budget}} / \tau}, \qquad \bar{\ell} \leftarrow \ell + a \, (\bar{\ell} - \ell)
```

其中 $`\ell`$ 是這個 block 的 load，$`\bar{\ell}`$ 是平滑後的 load，$`\tau = 0.3`$ 秒。經過一個時間常數，顯示值會
往新的穩定 load 走 63 %，64 sample 和 1024 sample 都一樣。自我檢查正好顯示這件事：80 % 負載 0.3 秒後是 0.506 和
0.504，而第 14 章的公式是 0.800 和 0.617。

### 另外計算 overrun

平滑後的 load 是給眼睛看的。耳朵聽到的是 overrun，所以 meter 會計算每一個 load 超過 1.0 的 block，存在一個只會往上數
的計數器裡。audio thread 是它唯一的寫入者。UI 從不把它歸零；monitor 記住上次看到的數字，回報兩者的差。這維持了
「每個值只有一個寫入者」（Step 2 的規則），還有第二個好處：晚啟動的 monitor（例如發生幾次 overrun 之後才打開的視窗）
不會把舊事當成新消息回報。

overrun 是*我們自己的*處理讓 block 遲到。音訊裝置也可能因為 callback 以外的原因斷音（別的程式、driver、電源管理）；
那是裝置的 xrun，JUCE 用 `AudioIODevice::getXRunCount()` 回報（driver 無法判斷時回傳 -1）。Step 7 的 app 顯示前者；再加上裝置的計數只是小小的擴充。

### 從數字到警告

transport bar 需要的是三種狀態之一，使用者不該自己解讀數字：

<div align="center">

| 狀態 | 條件 | 告訴使用者什麼 |
|---|---|---|
| **Overload** | 2 秒內發生過 overrun | 你已經（或即將）聽到斷音 |
| **High** | 平滑後的 load 偏高 | 接近極限：再加一個重的 plug-in 可能就太多了 |
| **Ok** | 其他情況 | |

</div>

「High」有**遲滯（hysteresis）**：75 % 時開啟，降到 65 % 以下才關閉。否則 load 在 75 % 附近徘徊時，警告會一直閃爍。
「Overload」會保持 2 秒，因為一次 overrun 只有幾毫秒，短到根本看不見。

### 邏輯保持純粹

時鐘只在一個地方讀取：`Engine::process()`。`CpuMeter::addBlock()` 把花掉的時間當參數，`CpuMonitor::poll()` 把現在
的時間當參數。所以幾乎所有檢查都餵它們編出來的時間，在每台機器、Debug 和 Release 下結果都完全一樣，而不是一個會因為
電腦忙不忙而時過時不過的測試。量測要用 `std::chrono::steady_clock`：它不會跳（牆上時鐘會），讀取它也不會上鎖或配置記憶體。

### Thread

<div align="center">

| 值 | 寫入者 | 讀取者 |
|---|---|---|
| `smoothed_` | audio thread | audio thread |
| `load_`、`overruns_`、`blocks_`（atomic） | audio thread | UI thread |
| `seenOverruns_`、`lastOverrunAt_`、`high_` | UI thread | UI thread |
| `sampleRate_` | `prepare()`，在 audio thread 啟動之前 | audio thread |

</div>

這裡用 relaxed atomic 就夠了：每個值都只是一個拿來顯示的數字，沒有其他資料透過它發布。自我檢查的最後一部分會在 UI
thread poll 的同時跑一個真正的 audio thread。用 `--sanitize thread` 建置時會通過；把 `load_` 改成普通的 `float`，
ThreadSanitizer 就會回報 data race，程式以 exit code 66 結束。

## 程式碼

`steps/step03-cpu-budget/minidaw.h`（節錄）：

```cpp
class CpuMeter {                       // audio thread writes, UI thread reads
public:
    static constexpr double kTimeConstantSeconds = 0.3;
    void prepare(double sampleRate) noexcept;
    void addBlock(int numSamples, double secondsSpent) noexcept; // YOUR TURN
    float load() const noexcept;                 // smoothed; 1.0 = the whole budget
    std::uint32_t overruns() const noexcept;     // only counts up
private:
    double sampleRate_ = 0.0;
    double smoothed_ = 0.0;                      // audio thread only
    std::atomic<float> load_{0.0f};
    std::atomic<std::uint32_t> overruns_{0};
    std::atomic<std::uint64_t> blocks_{0};
};

class CpuMonitor {                     // UI thread
public:
    explicit CpuMonitor(const CpuMeter& meter) noexcept; // starts from the current count
    CpuStatus poll(double nowSeconds) noexcept;          // YOUR TURN
private:
    std::uint32_t seenOverruns_;
    double lastOverrunAt_ = -1.0e30;
    bool high_ = false;
};

void Engine::process(float* out, int numSamples) noexcept {
    const auto start = std::chrono::steady_clock::now();
    // ... commands and render, as in Step 2 ...
    const std::chrono::duration<double> spent = std::chrono::steady_clock::now() - start;
    meter_.addBlock(numSamples, spent.count());
}
```

## 換你寫

`steps/step03-cpu-budget/minidaw.h` 裡有已完成的 Step 1 與 Step 2、含資料成員的 `CpuMeter` 和 `CpuMonitor`，
`Engine::process()` 也已經會讀時鐘。

<div align="center">

| | |
|---|---|
| 你要寫 | `CpuMeter::addBlock()` 和 `CpuMonitor::poll()` |
| 檔案 | `steps/step03-cpu-budget/starter/step03.cpp` |
| 執行 | `l15_step03_starter`；完成前會印出 `[FAIL] l15_step03 (16 of 25 checks failed)` |
| 解答 | `steps/step03-cpu-budget/solution/step03.cpp`（ctest `l15_step03_solution`） |

</div>

在 `addBlock()` 裡你是每個成員唯一的寫入者，所以 `overruns_.store(overruns_.load() + 1)` 就夠了。在 `poll()` 裡用
unsigned 減法算新的 overrun：計數器在 $`2^{32}`$ 次 overrun 後繞回來時，結果仍然正確。

預期輸出（已驗證；最後兩行取決於機器）：

```
Step 3: a CPU budget (build-along)
  10 ms budget: blocks taking 5, 12 and 9.9 ms -> 1 overrun(s) (expected 1), 3 blocks
  0.3 s at 80 % load, then read the meter:  64-sample blocks 0.506, 1024-sample blocks 0.504 (expected 0.506 both)
  (chapter 14's 10 % per callback would show 0.800 and 0.617)
  steady 40 %, then one block at 130 %: load 0.415, 1 new overrun -> Overload; 0.9 s later Overload;
  2.4 s later Ok (expected below 0.5, 1, Overload, Overload, Ok - the average hides the spike)
  load 70, 80, 70, 60, 70 %: Ok, High, High, Ok, Ok (expected Ok, High, High, Ok, Ok)
  3 overruns before the monitor existed: first poll 0 new, Ok (expected 0, Ok)
  64-sample blocks (1.33 ms): 16 cheap tracks -> 2.4 %, 0 overruns; 4 heavy tracks -> 120 %, 2250 overruns
  (expected 2.4 %, 0; 120 %, 2250)
  the engine, 200 blocks of 256 samples on this machine: 200 blocks measured, load 0.0006 %
  2 threads: 5000 blocks measured while the UI polled 31248 times; overruns seen by the UI 0, counted 0
[PASS] l15_step03 (25 checks passed)
```

## 你學到了

- $`N`$ 個 sample 的 block 有 $`N / f_s`$ 秒；load 是花掉的時間除以這個預算，超過 1.0 的 block 就是 overrun，也就是斷音。
- 限制一個專案的是 track 的成本，不是 track 的數量；DAW 顯示成本，讓使用者決定。
- 以秒為單位平滑化，而不是以 callback 為單位，否則 meter 在每個 buffer size 下表現都不同。
- 平均值會藏住尖峰：overrun 要另外算，存在只有一個寫入者、只往上數的計數器裡。
- 時鐘留在邊界，邏輯保持純粹，測試在每台機器上都會完全一致。
