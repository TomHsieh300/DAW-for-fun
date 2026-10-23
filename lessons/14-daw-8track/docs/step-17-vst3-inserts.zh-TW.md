# Step 17 - 以 VST3 plugin 作為 insert

[English](step-17-vst3-inserts.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 16](step-16-inserts-panels.zh-TW.md) | 下一步：[Step 18](step-18-file-loading.zh-TW.md) | 回到[路線圖](../README.zh-TW.md#4-the-twenty-one-steps)

## 目標

在任何 insert slot 中選擇 "VST3..."，挑選一個 `.vst3` bundle，plugin 就會像內建 effect 一樣在該 slot 中處理 audio：有它自己的 editor 視窗、可作為替代的通用參數面板、bypass，以及由 engine 的 PDC（Step 8）補償的 latency。

## 設計

### Chapter 13 負責 hosting；我們寫一個 adapter

Chapter 13 的函式庫 `dawlab_plugin_host` 提供：

- `PluginHost::loadFromFile(file, sampleRate, blockSize, error)`——掃描 bundle，建立一個 `juce::AudioPluginInstance`（message thread）；
- `HostedPlugin`——prepare / process(`AudioBlock`) 無需複製 / normalized 參數 / state / latency；
- `PluginWindow`——在視窗中顯示 plugin 的 editor（或 JUCE 的 `GenericAudioProcessorEditor`）。

Engine 對這些一無所知；它只認得 `InsertEffect`。`VstInsert` 就是 adapter：

| `InsertEffect` | `VstInsert` 做的事 | thread |
|---|---|---|
| `prepare(sr, block, ch)` | `HostedPlugin::prepare()`（bus layout、`prepareToPlay`） | message（在 `Engine::addInsert` 內） |
| `process(block)` | `HostedPlugin::process(block)` | audio |
| `getLatencySamples()` | `AudioProcessor::getLatencySamples()` | audio，每個 block |
| `getNumParameters()` ... | 最多 64 個參數，作為 Float 0..1（normalized，VST3 慣例），名稱複製進 `std::string` | any |
| `setParameter(i, v)` | `HostedPlugin::setParameterValue(i, v)` | any |
| `reset()` | 什麼都不做（見下文） | audio |
| `getName()` | plugin 的名稱 | any |

包裝完成後，VST3 plugin 走的是和內建 effect *完全相同*的路徑：`Session::setVstInsert()` -> `Engine::addInsert()` -> command queue -> `InsertChain`；移除時它作為垃圾回來，在 message thread 上被刪除——這是 VST3 的要求。

### Latency 與 PDC 免費到手

Mixer 在每個 block 開頭向每個 insert 詢問 `getLatencySamples()`（Step 8），並重新計算補償 delay。因此帶有 lookahead 的 plugin 在被插入的那一刻就得到補償，而當它改變 latency 時（許多 plugin 在你改變品質或 lookahead 設定時會這樣）也會再次補償——在一個 block 之內完成，app 裡不需要任何程式碼。

### 為什麼 `reset()` 什麼都不做

`Engine::requestReset()` 會在 audio thread 上對每個 insert 呼叫 `reset()`。對 VST3 plugin 而言，JUCE 的 `reset()` 會經過 `setActive(false)` / `setActive(true)`，這可能 allocate，且不是 real-time safe。我們不去動 plugin 的 state：在 bounce 或「新 project」之後，它的 tail 會自然響完，而不是被切斷。（需要 hard reset 的 host 會在 plugin 脫離 chain 時，於 message thread 上停用並重新 prepare 它。）

### Editor 視窗的生命週期

Plugin 的 editor 必須在 plugin *之前*刪除。`VstInsert` 擁有它的 `PluginWindow`，並把它宣告在 `HostedPlugin` *之後*，所以 destructor 的順序是視窗先、plugin 後。關閉視窗時會非同步刪除它（視窗不能在自己的 close callback 內刪除自己）。`Session::onInsertWillBeRemoved` 在 slot 被清空後立刻關閉它，遠早於 garbage collector 刪除 plugin。

### 手邊沒有 VST3？用測試 plugin

自動化測試不能依賴某個 VST3 檔案。`createTestDelayPlugin(latency)`（SmokeTest.cpp）是一個 in-process 的 `juce::AudioPluginInstance`：一個 `latency` 個 sample 的 stereo delay，帶有一個參數 "Gain"（gain $= 2 \cdot$ value，預設 0.5 = unity）與 `setLatencySamples(latency)`。包進 `HostedPlugin` 與 `VstInsert` 之後，它走的程式碼路徑與真正的 plugin 相同。

當 chapter 13 在同一棵 tree 中建置時（`build.ps1 -Only 13-vst-hosting,14-daw-8track -Juce`），它的兩個 plugin 會直接出現在 insert 選單中："DawLab Compressor (chapter 13)"（latency 0）與 "DawLab Limiter (chapter 13)"（一個把 lookahead 回報為 latency 的 lookahead limiter：預設在 48 kHz 下為 240 個 sample = 5 ms）。`app/CMakeLists.txt` 把它們的 bundle 路徑以 `DAWLAB_L13_COMPRESSOR_VST3` / `DAWLAB_L13_LIMITER_VST3` 編譯進來（從 target 的 artefact 資料夾重新組出，因為 chapter 13 只在它自己的 directory scope 中設定 `L13_COMPRESSOR_VST3` / `L13_LIMITER_VST3`），並讓 `DawLab8` 依賴這兩個 bundle target，所以 smoke test 執行時它們已經存在。`EffectMenu::chapter13LimiterFile()` 只有在 bundle 存在於磁碟上時才回傳它；否則該項目不會顯示。

## 程式碼

- `app/Source/VstInsert.h/.cpp`——adapter。
- `app/Source/MainComponent.cpp`——`chooseVst3For()`（接受 bundle 資料夾與單一檔案的 file chooser）、`loadVst3Into()`：

```cpp
auto instance = pluginHost_.loadFromFile(file, engine_.getSampleRate(), engine_.getMaxBlockSize(), error);
if (instance == nullptr)
    return false;
auto vst = std::make_unique<VstInsert>(std::make_unique<host::HostedPlugin>(std::move(instance)));
if (session_.setVstInsert(ref, slot, std::move(vst)) == nullptr) { ... }
openInsertEditor(ref, slot, false);   // -> VstInsert::openEditor() -> host::PluginWindow
```

- `app/Source/SmokeTest.cpp`——`TestDelayPlugin`。

## 驗證

`l14_app_smoke_test`，第 5 節：latency 為 100 個 sample 的測試 plugin 被放進 master 的 slot 4（在 master limiter 之後）：

```
5. test plugin on the master (latency 100): mix latency 752 -> 852, |with - without| = 0, gain 0.25 -> -6.02 dB, 192 process calls
```

- Mix latency 剛好增加 100 個 sample（PDC 接手了）。
- 以 unity gain 帶著 plugin 的 1 秒 bounce，與*不帶* plugin 的 bounce 逐 bit 相同：renderer 移除了整個 mix latency（Step 9），所以純 delay 是看不見的。
- 透過 `InsertEffect::setParameter()` 把它的參數設為 0.25，會讓輸出降低 6.02 dB（$20\log_{10} 0.5$）。
- 在 `clearInsert()` + `collectGarbage()` 之後，沒有任何測試 plugin 的實例仍然存活，且 mix latency 回到 752。

當 chapter 13 是建置的一部分時，smoke test 也會載入真正的 VST3 檔案（第 5b 節）；以 `build.ps1 -Only 13-vst-hosting,14-daw-8track -Juce`，作者得到

```
5b. VST3 DawLabCompressor on the drums: 8 parameters, latency 0, 1 s render peak -1.00 dBFS
5b. VST3 DawLabLimiter on track 8 (insert latency now 752): plugin latency 240, mix latency 752 -> 992,
    |with - without| = 0 (the two compared 240 samples apart: 1.33)
[PASS] l14_app_smoke_test (105 checks passed)
```

Compressor 沒有 latency，所以它只證明真正的 bundle 能載入、處理，並在 message thread 上被刪除。Limiter 才是真正的 PDC 測試——再次是第 5 節，但用的是會自行計算並回報 latency 的 plugin：

- 它被放進 insert latency 已是最大的那個 track 的 slot 4（track 8，convolution reverb：512 個 sample），所以該 track 的路徑變成 $512 + 240 = 752$，其他每個 track 現在都必須再多 delay 240 個 sample。Mix latency 必須剛好增加 plugin 透過 `AudioProcessor::getLatencySamples()` 所回報的量：$752 \to 992$，$+240$。
- 它的 ceiling 設為 0 dB（normalized 1.0），高於該 track 的電平，所以對這個訊號而言，limiter 是 240 個 sample 的純 delay。帶著它的 1 秒 bounce 因此與不帶它的 bounce 逐 bit 相同：PDC 對齊了全部八個 track，而 renderer 移除了較大的 mix latency。把同樣的兩個 bounce 錯開 240 個 sample 比較——也就是未補償的 plugin 會對該 track 造成的結果——差異為 1.33，所以這項檢查能抓出缺少的補償。
- 在 `clearInsert()` 之後，mix latency 回到 752。

（105 = stand-alone build 的 92 項檢查（它針對第 5b 節印出兩行 "not built in this tree"），加上 compressor 的 5 項與 limiter 的 8 項。測試 plugin 的 192 次 process 呼叫，是兩次 1 秒的 render，每次 $48\,000 + 852$ 個 sample，以 512 個 sample 的 block 處理，$2 \lceil 48\,852 / 512 \rceil = 2 \cdot 96$。）

在 app 中：把 chapter 13 的 DawLab Compressor 插在 drum 上，開啟它的 editor，把 threshold 拉低——plugin 自己 GUI 裡的 meter 會隨著 song 移動。把 DawLab Limiter 插在 Hits track 上，並觀察 mixer 下方的 "Mix latency (PDC)"：752 -> 992 個 sample；轉動它的 lookahead 旋鈕，這個數字會在一個 block 之內跟著變動，同時所有 track 都保持對拍。（在 bass 上，它的 insert 回報 31 個 sample，$31 + 240 = 271$ 仍低於 Hits track 的 512：bass 只是被 delay 得較少，mix latency 並不改變。）

## 換你動手

| | |
|---|---|
| 你要寫 | `app/Source/VstInsert.cpp` 中的 `VstInsert::process()` 與 `VstInsert::getLatencySamples()`（轉發給 hosted plugin；負的 latency 視為 0） |
| 建置 + 檢查 | `.\scripts\build.ps1 -Only 13-vst-hosting,14-daw-8track -Juce -TestLabel test`（連同 chapter 13，讓第 5b 節也載入真正的 VST3 limiter） |
| 預期 | 第 5 節：`mix latency 752 -> 852, |with - without| = 0`；第 5b 節：`mix latency 752 -> 992`；`[PASS] l14_app_smoke_test`——PDC 跟隨你的 adapter 所回報的 latency |

## 你學到了什麼

- 只需要在兩個 interface 之間放一個 adapter，就能在 engine 中 host 外來的程式碼。
- 每個 block 都重新計算的 PDC，能處理 latency 會改變的 plugin。
- Plugin API 提供的並非全都是 real-time safe：逐個呼叫決定它可以在哪裡執行。
- In-process 的假 plugin 讓 hosting 路徑在不需要檔案的情況下也能測試。
