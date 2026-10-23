# Step 10 - Real-time safety 稽核

[English](step-10-realtime-safety.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 9](step-09-offline-render.zh-TW.md) | 下一步：[Step 11 - Audio device](step-11-audio-device.zh-TW.md)（第 2 階段：app）| 回到[路線圖](../README.zh-TW.md#4-二十一個-step)

## 目標

「audio thread 從不配置記憶體、從不加 lock，並且能處理任何 block size」這句話很容易宣稱，
也很容易被一個無害的 `push_back` 破壞。把這些宣稱變成自動化測試，當有人破壞它們時，
讓 build 失敗。

## 設計

### 這裡所說的 real-time 是什麼意思

audio device 每 $`N / f_s`$ 秒要求 $`N`$ 個 sample（48 kHz 下 512 個：每 10.67 ms 一次）。如果
`processBlock()` *有一次*遲到，聽者就會聽到 click 或空隙。平均速度無關緊要；
**最壞情況**才算數。因此，持續時間無上限或不可預測的操作
在 audio thread 上是被禁止的：

| 禁止 | 原因 | engine 的替代做法 |
|---|---|---|
| `new` / `delete` / `malloc`（也包括藏在 `std::vector::push_back`、`std::string`、`std::function`、`make_shared` 裡的） | allocator 可能取得 lock，或向 OS 要求 page | 在 `prepare()` 中配置；擁有權隨 command 抵達 |
| mutex、condition variable、`join` | priority inversion | atomic、wait-free 的 `SpscQueue` |
| 檔案 / console I/O（`printf`、logging） | system call、lock | 統計值放在 atomic，由另一條 thread 印出 |
| exception | 配置 + unwinding 的成本 | 一切都是 `noexcept` |
| 等待另一條 thread | 無上限 | audio thread 只會 *poll* queue |

### 稽核 1 - 計算配置次數

C++ 允許程式取代全域的 `operator new` / `operator delete`。測試這麼做了（所有
變體：array、nothrow、sized、aligned），並在 `thread_local` 旗標被設定時計算呼叫次數：

```cpp
thread_local bool tCounting = false;
void* operator new(std::size_t n) { if (tCounting) ++gAllocs; return std::malloc(n ? n : 1); ... }
void operator delete(void* p) noexcept { if (p && tCounting) ++gFrees; std::free(p); }
...
{
    CountScope scope;          // tCounting = true for this thread only
    engine.processBlock(block);
}                              // tCounting = false
```

`thread_local` 很重要：UI thread 在同一時間會自由地配置記憶體（新的 effect、clip list），
而那些配置不能被計入。`processBlock()` 內部的任何配置或釋放 - 不論來自
engine 或任何第 01..12 章的 DSP class - 都會表現為非零的計數。

### 稽核 2 - 每個 effect 單獨檢查

同一個計數器會包住每一種 effect 類型的 1 秒 `process()` 呼叫（每個 block 變更一次參數），共
20 種 effect 類型（19 個 adapter 加上 Step 8 的 `PureDelayEffect` 測試用儀器），所以
失敗時能指出元兇。

### 稽核 3 - 受到全面攻擊的整個 engine

一個 8 軌專案（每個 track 兩個 insert，循環使用所有 effect 類型，加上 send、一條 reverb 與一條
delay bus、master 上的 convolution + limiter、一個 loop）在 `std::thread` 上以 1..2048 的隨機 block
size 執行 12 秒的音訊。同時，主 thread 盡可能快地（有 0.1 ms 的
暫停）做隨機的 UI 工作：fader、pan、mute、solo、send level 與 pre/post、透過 observer pointer 變更的 effect
參數、bypass、新增 / 移除 / 移動 / 取代 insert（任何 effect
類型）、新的 clip list、seek、loop 範圍、tempo、play/stop、master fader、`collectGarbage()`。
audio thread 計算配置次數，並檢查每個 block 是否有 NaN / inf。

一個很好的額外習題，是用 thread sanitizer 來 build 這個測試（Clang/GCC：
`-fsanitize=thread`；MSVC 沒有），它會回報 data race - 也就是 atomic 與 queue 本來要
防止的那些 bug。（本章的測試只用 MSVC 執行過。）

### 稽核 4 - Block-size invariance

`test_block_invariance` 以 block size 512、64 以及兩組 1..1500 的隨機大小序列 render 一個專案（compressor、EQ、gate、delay bus、limiter、PDC、fade、
send，以及一個刻意從 sample 777 開始的 clip），並要求最大差異低於 $`10^{-5}`$。

### 模擬的 device

`l14_realtime_sim` 是稽核 3 給人類看的友善版本：audio thread 像真正的 device 一樣被控制節奏（`sleep_until` 上一個 block「播完」的時間），
UI 做一個隨機動作然後睡 5 ms（OS 的 timer 粒度會把這個時間拉長：在 Windows 上 6 秒內約 380 個動作，也就是
每秒 60 個），而程式會回報每個 block 相對於其 deadline 的 CPU 負載。

## 程式碼

| 檔案 | 內容 |
|---|---|
| `tests/test_realtime_safety.cpp` | operator new/delete 的取代、稽核 2 與 3 |
| `tests/test_block_invariance.cpp` | 稽核 4 |
| `examples/realtime_sim.cpp` | 帶有負載統計的、控制節奏的模擬 |

## Your turn

`steps/step10-realtime-safety/minidaw.h` 是你完整的 Step 9 engine 加上三個稽核工具：
`audit::counters()`、`audit::countingOnThisThread()`（一個 `thread_local` 旗標）與
`audit::CountScope`。這裡沒有新的 engine 程式碼 - 被稽核的就是 engine 本身。

| | |
|---|---|
| 你要寫的 | 被取代的全域 `operator new` / `operator delete` 中的計數（只在 `countingOnThisThread()` 為 true 時），以及 `audit::renderWithBlockSizes()`（從 0 開始播放，block size 循環使用一份清單） |
| 檔案 | `steps/step10-realtime-safety/starter/step10.cpp`（這些取代函式已經用 `malloc` / `free` 配置；只是還沒有計數） |
| 執行 | `l14_step10_starter`；在你完成之前它會印出 `[FAIL] l14_step10 (7 of 11 checks failed)` |
| Solution | `steps/step10-realtime-safety/solution/step10.cpp`（ctest `l14_step10_solution`） |

這個檢查會在你的迷你 engine 上執行這三項稽核（8 個帶 fade 的 track、track、bus 與 master 上的 Gain / Compressor /
PureDelay insert、send、PDC、一個 loop）。預期輸出（已驗證，
Debug build）：

```
Step 10: real-time safety audit (build-along)
  counter: new/delete + new[]/delete[] inside a CountScope -> 2 allocations, 2 frees (expected 2, 2)
  another thread allocating 1000 times meanwhile: 0 counted (expected 0)
  audio thread: 3043228 samples, random blocks 1..2048; UI thread: 400 actions, 165 commands applied
  allocations + frees inside processBlock(): 0 + 0 (expected 0), blocks with NaN/inf: 0
  max |difference| 512 vs 64: 0, 512 vs random 1..1500: 0 (expected 0), peak 0.463
[PASS] l14_step10 (11 checks passed)
```

第三行的 sample 與 command 數量每次執行都不同（thread 的時序）；零則不能變。前兩行
與零同樣重要：如果計數器壞了而導致稽核什麼都沒計到，它就會「通過」- 所以這個檢查先證明
計數器看得見這條 thread 的配置，並忽略另一條 thread 的。

## Verify（參考路線）

```powershell
.\scripts\build.ps1 -Only 14-daw-8track       # runs all tests
cd build; .\bin\Debug\l14_test_realtime_safety.exe; .\bin\Debug\l14_realtime_sim.exe
```

在作者的機器上驗證的輸出（Debug build）：

```
  audio: 577111 samples in 1398 chunks, max block time 53.38 ms
  ui: 603 operations, 191 commands applied, 0 dropped, 0 garbage leaked
  allocations + frees on the audio thread: 0, blocks with NaN/inf: 0
[PASS] l14_test_realtime_safety (25 checks passed)

  max |difference| 512 vs 64: 0, 512 vs random: 0, 512 vs random(2): 0
[PASS] l14_test_block_invariance (5 checks passed)
```

（sample 數量比 12 秒多一點，因為最後一個隨機 block 超出了。「max block
time」是 Debug build 中一個 2048-sample block 的值；它不是 real-time 的量測。Release
build 渲染 12 秒的速度快到 UI thread 幾乎輪不到執行，所以 audio thread 會
持續進行，直到 UI thread 至少呼叫了 200 次：在 Release 中 sample 數量會更大。）

```
Steps 7 + 10: simulated audio device, random block sizes 32..1024
  audio      : 6.00 s in 537 callbacks (537 engine chunks)
  CPU load   : average 49.4 %, worst block 633.3 % of its deadline (a 24-sample block), 22 deadline misses
  UI         : 381 actions, 135 commands applied, 0 dropped
  garbage    : 44 objects freed on the UI thread, 0 leaked
  NaN blocks : 0
```

在 Debug build 中，平均負載約為 50 %，而且有些 block 錯過了 deadline - 模擬本身仍然正確，
但你會聽到 dropout。Debug 程式碼（未最佳化、帶有檢查式 STL
iterator）比最佳化後的程式碼慢好幾倍，所以請 build Release
（`.\scripts\build.ps1 -Only 14-daw-8track -Config Release`）來看真正的 headroom。在
作者的機器上量測（Release：三次執行）：

| build | 平均負載 | 最差的 block | deadline 錯過次數 | `l14_render_song`（19 秒音訊） |
|---|---|---|---|---|
| Debug | 49.4 % | 633 %（24 個 sample） | 22 | 13.14 秒（1.4 倍 real time） |
| Release | 10.0 - 10.1 % | 69 - 109 %（24 個 sample） | 0 - 1 | 1.38 秒（13.8 倍 real time） |

注意*哪個* block 是最差的：最後一個，被切成 24 個 sample，讓這次執行剛好在
6 秒結束。它的 deadline 只有 0.5 ms，而 engine 每個 block 的許多工作並不會隨 block 縮小
（11 個帶 meter 與 smoother 的 strip、PDC 的記帳、每個 insert 的每 block 設定）。
相對於其時間長度，小 block 是昂貴的 - 這就是為什麼低 latency 的設定（32 或 64
個 sample）需要比 512 多得多的 CPU headroom，也是為什麼平均負載對 dropout 說明不了什麼。
Release 中偶爾的 miss 也來自排程：這條「audio thread」是一條普通的
`std::thread`，而真正的 driver 是在高優先權的 thread 上 callback。動作與
command 的數量每次執行都不同（thread 的時序）；零（dropped、leaked、NaN）則不能變。

兩種 block size 的 render 之間，差異恰好是 0：每個 smoother 都逐
sample 前進，clip 與 PDC 是整數運算，而 DSP class 會在 block 之間保留它們的狀態。

## 你學到了什麼

- Real-time 意味著有界的最壞情況時間；禁止清單由此推得。
- 在測試中取代 `operator new`，就能*證明*沒有配置 - 針對每條 thread。
- 在 audio thread 執行時，從第二條 thread 對 API 做 fuzz，並檢查輸出有沒有 NaN。
- 用不同的 block size render 並比較：這是在音訊程式碼中，以最低成本找出最多 bug 的測試。
