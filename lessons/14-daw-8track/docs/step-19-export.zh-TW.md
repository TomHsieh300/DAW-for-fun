# Step 19 - Export mixdown

[English](step-19-export.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 18](step-18-file-loading.zh-TW.md) | 下一步：[Step 20](step-20-spectrum-loudness.zh-TW.md) | 回到[路線圖](../README.zh-TW.md#4-the-twenty-one-steps)

## 目標

"File > Export mixdown..."（Ctrl+E）把 song 加上 3 秒的 tail 寫成 24-bit WAV 檔，經過 latency 補償、比 real time 更快、不讓視窗凍結——而且結果必須就是使用者按下 Play 時聽到的東西。

## 設計

### 重複使用 engine 的 bounce

Step 9 建立了 `renderOffline()` / `bounceToWav()`：套用待處理的 command、reset、從頭開始播放、盡可能快地呼叫 `processBlock()`、在 song 結尾停止、render tail、丟棄最前面的 `getLatencySamples()` 個 sample。因為 `processBlock()` 也正是 device 所呼叫的東西，「你聽到的就是你 bounce 出來的」在結構上就成立。App 在它周圍加了兩件事：從 device 安全地交接，以及一個 thread。

### 規則 1：`processBlock()` 只能有一個呼叫者

Engine 是 single-consumer 物件：只有一個 thread 可以呼叫 `processBlock()`（並因此消耗 command）。Device 運轉時，那就是 audio thread。所以在 render 之前：

```cpp
audioHost_.setEngineAttached(false);   // AudioDeviceManager::removeAudioCallback(this)
```

`removeAudioCallback()` 會取得 JUCE 的 audio callback lock：它返回時，沒有任何 callback 正在執行，也不會有新的開始。Device 繼續運轉並輸出靜音。Export 之後，`setEngineAttached(true)` 重新加入 callback；`audioDeviceAboutToStart()` 看到相同的 sample rate 與 buffer size，便*跳過* `Engine::prepare()`（Step 11），所以沒有任何東西被無謂地重置。Renderer 自己會還原 transport（position、loop、play state）。

### 規則 2：render thread 在這段期間擁有 queue

`renderOffline()` 會套用 command queue 並呼叫 `collectGarbage()`——它是 command queue *以及* garbage queue 的 consumer。兩者都是 single-consumer queue。因此，在 export 執行期間：

- `MainComponent::tick()` 跳過 `collectGarbage()`；
- 一個 `ExportOverlay` 蓋住視窗並吞掉滑鼠點擊，`perform()` 忽略 command，而這些 command 會回報自己為 inactive（`setActive(false)`），所以沒有人能編輯 project；
- editor 視窗在 export 開始之前就被關閉。

數值（fader 位置……）仍可能透過 atomic 改變而無害，但沒有任何辦法接觸到它們。

### 帶 callback 的 thread

`ExportJob : juce::Thread` 執行 `Exporter::renderToFile()`，並以 `MessageManager::callAsync` 把 `ExportResult` post 回來，以 `alive` flag 保護，和 Step 18 的 loader 一樣。`MainComponent::finishExport()` 移除 overlay、重新 attach engine，並在狀態列顯示 "Exported ...: 19.00 s, peak -1.00 dBFS, -15.5 LUFS, rendered in ... s"。

檔案是用 JUCE 的 `WavAudioFormat` 寫出的（`Exporter::writeWavFile`，16 / 24-bit PCM 或 32-bit float）：`juce::File` 能處理 Windows 上的非 ASCII 路徑，而以 UTF-8 `std::string` 開啟的 `std::ofstream` 則不行。

### 結果中的數字

`ExportResult` 回報長度、實際經過的 render 時間、檔案的 sample peak、此次 render 的 master integrated loudness（meter 在 render 開始時被 reset，所以該值精確描述被 export 的 audio），以及被移除的 latency。

## 程式碼

- `app/Source/Exporter.h/.cpp`：`ExportOptions`、`ExportResult`、`Exporter::renderToFile()`、`Exporter::writeWavFile()`、`ExportJob`。
- `app/Source/MainComponent.cpp`：`exportMixdownInteractive()`（儲存對話框）、`exportMixdown()`、`finishExport()`、`ExportOverlay`。

## 驗證

`l14_app_smoke_test` 的第 3 節用同一個函式 export demo song 的前 2 秒：

```
3. export: ok, 2.00 s, peak -1.00 dBFS, RMS -20.3 dBFS, ...\dawlab_output\l14_app_smoke.wav
```

檢查項目：檔案存在、剛好 96 000 個 sample、沒有 NaN / Inf、peak 高於 -30 dBFS（不是靜音）且至多 -1.00 dBFS + 0.05 dB（master limiter 的 ceiling）、RMS 高於 -45 dBFS。第 4 節（Step 11）接著證明透過 device callback 播放會得到相同的 sample。

聽聽 `dawlab_output/l14_app_smoke.wav`：bar 1 的 drum、bass 與 pad，以及經過 convolution reverb 的 hit。

同一個測試的第 9 節走的是 GUI 自己的路徑：一個 offline 的 `MainComponent` 載入 demo song 並呼叫 `exportMixdown(file)`——overlay、`ExportJob` thread、透過 `MessageManager::callAsync` 回傳的結果、`finishExport()`。因為結果只能透過 message loop 抵達，smoke test *非同步*地結束：`runSmokeTest()` 在啟動 export 後返回，`DawLabApplication::initialise()` 返回，JUCE 的 message loop 開始運轉，而 callback 檢查結果並以 exit code 結束 application。

```
9. GUI export (ExportJob thread): ok, 19.00 s, peak -1.00 dBFS, -15.5 LUFS integrated, latency 752 removed;
   rendered in 14.0 s while the message loop kept running; status line "Exported ..."
```

檢查項目：在 `exportMixdown()` 之後立刻，job 正在執行，且編輯 command 回報自己被停用；之後 WAV 檔 `dawlab_output/l14_app_export.wav` 剛好有 $`19 \cdot 48000 = 912\,000`$ 個 stereo frame（16 秒的 song = 120 BPM 下的 8 bar，加上 3 秒 tail），peak 位於 limiter 的 ceiling，被移除的 latency 等於 `Engine::getLatencySamples()`，狀態列以 "Exported" 開頭，且 transport 回到 0、停止。-15.5 LUFS 與 stage 1 的 mix report（Step 9）相同：GUI export *就是* engine 的 bounce。Render 時間（Debug build）隨機器而異。

## 換你動手

<div align="center">

| | |
|---|---|
| 你要寫 | `app/Source/Exporter.cpp` 中的 `Exporter::renderToFile()`：從 `ExportOptions` 填入 `engine::RenderOptions`，呼叫 `engine::renderOffline()`（Step 9），填寫 `ExportResult`（latency、長度、peak、master 的 integrated LUFS），拒絕空的或含 NaN 的 render，寫出 WAV |
| 建置 + 檢查 | `.\scripts\build.ps1 -Only 14-daw-8track -Juce -TestLabel test`（`l14_app_smoke_test`） |
| 預期 | 第 3 節：`export: ok, 2.00 s, peak -1.00 dBFS`；第 9 節：整首 song、19.00 s、-15.5 LUFS integrated、移除 752 的 latency；`[PASS] l14_app_smoke_test` |

</div>

## 你學到了什麼

- Bounce 就是由另一個 driver 執行的播放程式碼——但同一時間只能有一個 driver。
- Single-consumer queue 讓「現在誰可以呼叫這個？」成為你在每次模式切換時都必須明確回答的問題。
- 長時間的工作放在 thread 上，結果透過 message thread 回來，以 modal overlay 取代凍結的視窗。
