# Step 21 - 鍵盤快捷鍵、menu 與 app 的自動化測試

[English](step-21-shortcuts-and-tests.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 20](step-20-spectrum-loudness.zh-TW.md) | 回到[路線圖](../README.zh-TW.md#4-the-twenty-one-steps)

## 目標

Space 播放與停止——永遠如此，不論滑鼠在哪裡。每個 menu 項目都顯示它的快捷鍵，而且每個動作在程式碼中恰好存在一次。最後：一個可以由 script 測試的 GUI 應用程式，讓 `ctest` 像守護 engine 一樣守護這個 app。

## 設計

### Command：一個動作，三種觸發方式

JUCE 的 `ApplicationCommandManager` 是 *command*（一個整數 ID，帶有名稱、描述、類別、預設按鍵與「active / ticked」狀態）與 *target*（執行它們的物件）的註冊表。`MainComponent` 是 app 所有 command 的 target（`AppCommands.h`）：

| 類別 | command | 預設按鍵 |
|---|---|---|
| File | New project / Load demo song / Load audio into selected track... / Export mixdown... / Audio settings... / Quit | Ctrl+N / Ctrl+D / Ctrl+O / Ctrl+E / Ctrl+, / Ctrl+Q |
| Transport | Play / Stop、Return to start、Loop、Back / Forward one bar | Space、Home、L、Left、Right |
| Track | Select previous / next track、Mute / Solo selected track | Up、Down、M、S |
| View | Zoom in / out、Keyboard shortcuts... | = 或 +、-、F1 |

（在 macOS 上 Ctrl 是 Cmd：`ModifierKeys::commandModifier`。）

```cpp
commandManager_.registerAllCommandsForTarget(this);     // asks getAllCommands + getCommandInfo
commandManager_.setFirstCommandTarget(this);
addKeyListener(commandManager_.getKeyMappings());        // keys -> commands
setApplicationCommandManagerToWatch(&commandManager_);   // the menu bar shows shortcuts / ticks
```

- Menu bar 的項目是 `PopupMenu::addCommandItem(&commandManager_, id)`：名稱、快捷鍵文字、tick（Play / Stop、Loop）與 enabled 狀態都來自 `getCommandInfo()`。
- 按鍵經過安裝為 `MainComponent` 的 key listener 的 `KeyPressMappingSet`。當沒有任何有 focus 的 child 消耗掉按鍵時，按鍵才會抵達 component 的 key listener；這就是為什麼每個按鈕、旋鈕與 fader 都呼叫 `setWantsKeyboardFocus(false)`，也是主視窗在啟動時抓取 focus 的原因。正在編輯的 label（track 名稱）*確實*會消耗按鍵——在名稱中輸入 "s" 不該讓 track solo。
- Transport 按鈕呼叫同樣的 `togglePlay()` / `returnToStart()` / `toggleLoop()`。
- 當 export 執行時，`getCommandInfo()` 將編輯類 command 標示為 inactive，而 `perform()` 會吞掉它們（Step 19）。

`getNextCommandTarget()` 回傳一個可選的額外 target（`setExtraCommandTarget()`），而 `onExtendMenu` 讓呼叫端可以為 menu 增加項目。App 自己並不使用它們：Exercise 11（project save / open）與 13（shortcut map）透過它們為執行中的 app 增加 command，而不需修改它。

### 測試 GUI 應用程式

一個帶有 GUI 與 audio device 的 DAW 看起來無法測試。兩個命令列模式讓它可以測試，因為 app 從 Step 11 起就是為此而建的（offline 模式、不需 device 的 render 路徑、public 的 `tick()`）：

**`DawLab 8.exe --smoke-test`**（ctest `l14_app_smoke_test`，label `test`）。沒有視窗，沒有 device。它建出真正的 class，並以 `DAWLAB_CHECK` 檢查它們：

1. resampler（長度、passband、alias 抑制、留在 transition band 中的 alias）——Step 18；
2. 透過 `Session::applyDemoSong()` 載入的 demo song：19 個 insert、0 個未知參數、GUI slot = engine chain、mix latency = 最大 track + 最大 bus + master 的 latency——Step 16、18；
3. 透過 `Exporter::renderToFile()` 的 2 秒 export：不是靜音、沒有 NaN、peak <= ceiling——Step 19；
4. device callback（`AudioHost::processOffline`、512 與 480 個 frame、3 個 output）與 bounce 完全相等——Step 11；
5. master 上帶有 latency 的 hosted plugin：PDC、逐 bit 相同的 bounce、參數、清理——Step 17；
   5a. 同一個 plugin 放在 mono 的 drum track 上：`VstInsert` 的 mono fold，bounce 不變 - Step 4、17；
   5b. 當 chapter 13 在同一棵 tree 中建置時：drum 上它真正的 DawLab Compressor VST3，以及最大 latency 的 track 上它的 DawLab Limiter VST3（latency 240）：mix latency +240，bounce 不變；
6. GUI offline：strip 數量、fader -> engine、solo 按鈕、track format 按鈕（Mono -> Stereo -> Mono，pan mode 跟著改變）、產生的面板 -> engine、透過 key mapping 的 Space / L、一旦聽到 beat 2 時的 transport 顯示與 playhead（經過 latency 補償：transport position 減去 mix 與 device latency）——Step 12-16、21；
7. 切換到 44.1 kHz 的 device 變更：`Engine::prepare()` + `Session::handleSampleRateChange()` 把一個 clip 從 96 000 移到 88 200 個 sample（新 rate 下的 bar 2），並對其 buffer 做 resampling——Step 11；
8. spectrum 顯示：位於 bin 中心的 sine 出現在它的頻率與電平，對數軸正確——Step 20；
9. 透過 `MainComponent::exportMixdown()` 的 File > Export mixdown：`ExportJob` thread、被停用的 command、透過 message loop 回傳的結果、19 秒的檔案——Step 19；
10. 播放中，透過 `MainComponent` 自己的接線，在 44.1 kHz 重新啟動 device：`AudioHost` -> `onEnginePrepared`（transport 立即停止）-> `callAsync` -> `startClipConversion()` -> loader thread 上的 `MediaLoader::convertBuffers()` -> `finishClipConversion()` -> `Session::applySampleRateConversion()`，播放恢復——Step 11。

它會寫出 `dawlab_output/l14_app_smoke.wav`、`l14_app_export.wav` 與 `l14_app_smoke_44k.wav`（第 1 節餵給 file loader 的 44.1 kHz mono WAV），並在所有檢查通過時以 0 結束。第 1-8 節在 `initialise()` 內同步執行；第 9 與 10 節需要 message loop，所以 `runSmokeTest()` 接受一個 completion callback，在啟動 export 後返回，而最後一個 callback（在 message thread 上，於 export 與 sample-rate 變更之後）設定 exit code 並呼叫 `quit()`。這是測試非同步 GUI 程式碼的常見形式：啟動操作、返回 event loop、在 completion handler 中檢查。

**`DawLab 8.exe --snapshot <file.png>`**（ctest `l14_app_snapshot`，label `example`）把視窗 render 成 demo song 位於 bar 2 beat 3 的畫面，以及 drum compressor 的面板（Step 12）。

兩種模式都在 `JUCEApplication::initialise()` 內執行：message manager 存在，可以建立並繪製 component，而 process 以 `setApplicationReturnValue(code); quit();` 結束。

## 程式碼

- `app/Source/AppCommands.h`——ID 與類別。
- `app/Source/MainComponent.cpp`——`getAllCommands`、`getCommandInfo`、`perform`、`getMenuBarNames`、`getMenuForIndex`、`setExtraCommandTarget`、`showKeyboardShortcuts`（F1：由 key mapping 產生的清單）。
- `app/Source/SmokeTest.h/.cpp`、`app/Source/Main.cpp`。
- `app/CMakeLists.txt`——兩個 `add_test()` 呼叫（working directory = build 目錄，與其他每個 chapter 相同）。

## 驗證

```
DawLab 8 smoke test (no audio device)

1. resampler: 1 kHz 44.1k->48k amplitude 0.5000 (0.5 expected, -0.000 dB), length 48000;
   20 kHz 48k->22.05k alias at 2050 Hz: -122.2 dB below the input
   22.3 kHz 48k->44.1k (transition band) alias at 21800 Hz: -54.3 dB relative to the input
   file 44.1 kHz mono -> 2 channels, 48000 samples at 48 kHz, 1 kHz amplitude 0.5000
   l14_app_smoke_44k.flac -> 2 channels at 48 kHz, 1 kHz amplitude 0.5000
   l14_app_smoke_44k.ogg -> 2 channels at 48 kHz, 1 kHz amplitude 0.5049
   readable formats: *.wav;*.bwf;*.aiff;*.aif;*.flac;*.ogg;*.opus;*.mp3;*.wmv;*.asf;*.wm;*.wma
2. demo song: 140 clips on 28 distinct buffers, 19 inserts in GUI slots, 0 unknown parameter names,
   mix latency 752 samples
3. export: ok, 2.00 s, peak -1.00 dBFS, RMS -20.3 dBFS, ...\dawlab_output\l14_app_smoke.wav
4. device callback, 512-sample blocks, 3 outputs: max |callback - bounce| = 0, CPU load 59.8%
4. device callback, 480-sample blocks, 3 outputs: max |callback - bounce| = 0, CPU load 60.2%
5. test plugin on the master (latency 100): mix latency 752 -> 852, |with - without| = 0, gain 0.25 -> -6.02 dB, 192 process calls
5a. test plugin on the mono drum track: |with - without| = 0
5b. chapter 13's DawLab Compressor not built in this tree: VST3 file test skipped
5b. chapter 13's DawLab Limiter not built in this tree: VST3 PDC test skipped
   format button: track 2 Mono -> Stereo -> Mono
   drag and drop: 2 of 3 dragged files accepted, dropped on track 3
6. GUI offline: 11 strips, position 1.2.000 (playhead 24000, transport 24752 = + mix latency 752),
   master peak -11.8 dBFS, status "Demo song loaded"
7. device change 48 -> 44.1 kHz: bass clip 3 moved 96000 -> 88200 samples (bar 2 = 88200)
8. spectrum view: 996.09 Hz sine at 0.5 -> peak at 996.09 Hz, -6.02 dBFS (-6.02 expected); 632.5 Hz at x = 0.500
9. GUI export (ExportJob thread): ok, 19.00 s, peak -1.00 dBFS, -15.5 LUFS integrated, latency 752 removed;
   rendered in 13.4 s while the message loop kept running; status line "Exported ..."
10. GUI device change 48 -> 44.1 kHz (loader thread): engine 44100 Hz, transport stopped at once: yes, bass clip 3 96000 -> 88200,
    converted in 4457 ms with 405 message-loop turns meanwhile, playback resumed: yes, status "Sample rate changed to 44100 Hz: clips converted"

[PASS] l14_app_smoke_test (121 checks passed)
```

（Debug build；CPU 數字與第 9 節的 render 時間隨機器而異；第 6 行的 master peak 與第 9 行的 loudness 取決於 chapter 01-12 的 DSP 實作。所有被精確檢查的東西——長度、latency、零差異——都不會變動。）Exercise 13 對 app 的 18 個 command 加上它自己的 10 個（共 28 個）增加了衝突檢查：預設的 map 沒有衝突。

## 換你動手

| | |
|---|---|
| 你要寫 | `app/Source/MainComponent.cpp` 中的 transport command：它們在 `getCommandInfo()` 中的 `case`（名稱、描述、類別 "Transport"、預設按鍵 Space、Home、L、Left 與 Right 方向鍵）以及在 `perform()` 中的 `case`（`playStop`、`returnToStart`、`toggleLoop`、`seekBackOneBar`、`seekForwardOneBar`） |
| 建置 + 檢查 | `.\scripts\build.ps1 -Only 14-daw-8track -Juce -TestLabel test`（`l14_app_smoke_test`） |
| 預期 | smoke test 的按鍵通過（Space 切換 transport、L 切換 loop，也包括透過 `invokeDirectly`）；`[PASS] l14_app_smoke_test`；在 app 中，Transport menu 與 F1 清單顯示你的名稱與按鍵 |

想更進一步的話，加入你自己的 command（在 `AppCommands.h` 中加一個 ID，並在 `getAllCommands()`、`getCommandInfo()` 與 `perform()` 中各加一個項目）。在執行 `l14_ex13_solution` 之前把它拿掉：Exercise 13 剛好計算 app 的 18 個 command 加上它自己的 10 個（它的 shortcut editor 中共 32 列）。

## 你學到了什麼

- 讓每個使用者動作都經過 command 註冊表；menu、按鍵與按鈕就會保持同步。
- 鍵盤 focus 決定按鍵去向——讓它遠離會吞掉 Space 的 widget。
- 為測試而設計 app：offline 模式、不需 device 的 render 路徑與命令列模式，把「到處點一點、聽一聽」變成 CI server 可以檢查的數字。
