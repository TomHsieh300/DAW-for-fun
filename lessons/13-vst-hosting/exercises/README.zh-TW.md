# 第 13 章練習題：VST3 plugin 與 hosting
[English](README.md) | **繁體中文**

難度：★☆☆ 基礎、★★☆ 中等、★★★ 進階。解答在 [`../solutions/README.zh-TW.md`](../solutions/README.zh-TW.md)。

實作題的起始碼在 `exercises/starter/`，都能編譯，但留有 `// TODO`；直接執行會印出 `[FAIL]`（這是預期的）。
完成後執行對應的 `l13_exN_starter`，全部 `[PASS]` 就代表做對了。解答檔 `l13_exN_solution` 會在 `ctest -L solution` 自動驗證。

```powershell
.\scripts\build.ps1 -Only 13-vst-hosting -Juce -NoTest
cd build                               # 你的 build 資料夾
.\bin\Debug\l13_ex4_starter.exe
```

> 所有實作題都會載入本章 build 出來的 `DawLabCompressor.vst3` / `DawLabLimiter.vst3`（路徑在 CMake 時編進程式），
> 所以要先完整 build 一次（`l13_*` target 已經設定好依賴，build 它們時會自動先 build plugin）。

---

## 練習 1 ★☆☆（計算）：normalized parameter 換算

DawLabCompressor 的參數範圍如下（`plugin/compressor/CompressorProcessor.cpp`）：

| 參數 | 範圍 | skew |
|---|---|---|
| Threshold | −60 … 0 dB | 1（線性） |
| Ratio | 1 … 20 | 0.5 |
| Attack | 0.1 … 200 ms | `setSkewForCentre(10)` |
| Release | 5 … 2000 ms | `setSkewForCentre(100)` |

JUCE 的換算公式：$`n = \left(\dfrac{v - s}{e - s}\right)^{k}`$，$`v = s + (e - s)\, n^{1/k}`$。

1. host 要把 Threshold 設成 −24 dB、Ratio 設成 8:1，各要送出多少 normalized value？
2. `setSkewForCentre(c)` 讓 $`n = 0.5`$ 剛好對到 $`v = c`$。推導 skew $`k`$ 的公式，並算出 Attack 與 Release 的 $`k`$。
3. 使用者把 Release 旋鈕轉到 1/4 的位置（$`n = 0.25`$），實際是幾 ms？（提示：$`0.25 = 0.5^2`$，答案有很漂亮的封閉形式。）
4. 為什麼 VST3 規定 host 與 plugin 之間只傳 0..1 的值，而不是直接傳「−24 dB」？

## 練習 2 ★★☆（觀念）：thread、掃描與 crash

1. 下列動作各應該在哪個 thread 執行？為什麼？
   (a) `PluginHost::scan()`　(b) `HostedPlugin::process()`　(c) `HostedPlugin::setState()`　(d) 建立 `PluginWindow`　(e) 在 automation 播放中每 32 samples 呼叫一次 `setParameterValue()`
2. `HostedPlugin::process()` 為什麼用 `ScopedTryLock` 而不是 `ScopedLock` 去拿 plugin 的 callback lock？拿不到時我們做了什麼？這樣做的代價是什麼？
3. 為什麼專業 DAW（例如 Reaper、Bitwig、Cubase）要在「另一個 process」裡掃描 plugin？本章的 `PluginHost::scan()` 是 in-process 的，如果某個 plugin 在掃描時 crash，會發生什麼事？請設計一個「out-of-process 掃描 + blacklist」的流程（文字或流程圖即可）。
4. `PluginWindow` 的註解說「一定要在 HostedPlugin 被解構之前刪掉這個視窗」。如果順序反過來會怎樣？

## 練習 3 ★★☆（計算）：latency 與 PDC

48 kHz 的專案有三條 track 匯到 master：

- Track A：DawLabLimiter（lookahead 5 ms）
- Track B：一個回報 latency = 1024 samples 的 linear-phase EQ
- Track C：沒有 plugin

1. 每個 plugin 的 latency 是幾個 sample、幾 ms？
2. 為了讓三條 track 在 master 對齊，host 要在每條 track 額外延遲多少 sample？整個專案的總 latency 是多少？
3. 如果 host 沒做 PDC，Track A 的 kick 跟 Track C 的 kick 會差幾 ms？兩個相同的 kick 疊在一起、但差了 240 samples，在哪些頻率會完全抵消？（提示：comb filter，$`f_k = \dfrac{(2k+1)\, f_s}{2D}`$）
4. 錄音時（monitoring 輸入訊號）PDC 會帶來什麼問題？DAW 通常怎麼處理？

## 練習 4 ★★☆（實作）：host 端的 automation —— 切 sub-block

起始碼：`exercises/starter/ex4_automation.cpp`（target `l13_ex4_starter`）

讓 DawLabCompressor 的 Threshold 在 8 秒的 drum loop 內從 0 dB 線性降到 −40 dB。plugin 每次 `processBlock` 只讀一次參數，所以把每個 512-sample 的 block 切成 32-sample 的 sub-block，每段開頭用 `setParameterValue()` 送一次新值。

self-check：結果跟「直接呼叫 `dsp::Compressor`、每 32 samples 改一次 threshold」的參考答案差 < 1e-4；開頭 0.5 秒 RMS 幾乎不變（> −1 dB），最後 1 秒被壓 6 dB 以上。

延伸思考：sub-block 越小時間解析度越好，但代價是什麼？

## 練習 5 ★★☆（實作）：preset 存成檔案再載回

起始碼：`exercises/starter/ex5_preset_file.cpp`（target `l13_ex5_starter`）

實作 `savePreset()` / `loadPreset()`：把 `HostedPlugin::getState()` 的 binary blob 用 base64 存進自訂的 XML 檔（`dawlab_output/l13_ex5_preset.dawpreset`），附上 plugin 名稱與 uid；載入時檢查是不是同一個 plugin、檔案有沒有壞掉。

self-check：載回後所有參數誤差 < 1e-3、處理結果與存檔前逐 sample 相同（< 1e-6）；把 compressor 的 preset 載進 DawLabLimiter、或載入壞掉的檔案時要回傳 false。

## 練習 6 ★★★（實作）：用 AudioProcessorGraph 串兩個 plugin，並量測 latency

起始碼：`exercises/starter/ex6_graph_chain.cpp`（target `l13_ex6_starter`）

用 `juce::AudioProcessorGraph` 建立 `audio in → DawLabCompressor（makeup +12 dB）→ DawLabLimiter（ceiling −1 dB）→ audio out`。

self-check：graph 回報的 latency = 240 samples（5 ms）；用 impulse 量到的延遲 = 回報值；只有 compressor 時 peak > 0 dBFS，整條鏈的 peak ≤ −1 dBFS。

## 練習 7 ★★★（實作）：沒有 click 的 bypass（latency 補償 + crossfade）

起始碼：`exercises/starter/ex7_bypass_crossfade.cpp`（target `l13_ex7_starter`）

完成 `CrossfadeBypass::process()`：dry 訊號先延遲 plugin 的 latency（用第 05 章的 `dsp::DelayLine`），再用 10 ms 的 `SmoothedValue` 在 dry 與 wet 之間 crossfade。bypass 時 plugin 仍繼續處理。

self-check（DawLabLimiter，ceiling −20 dB，440 Hz 正弦波）：bypass 穩定後輸出 = 輸入延遲 240 samples；crossfade 版最大 sample-to-sample 跳動 < 0.025；沒有補償的 hard bypass 跳動 > 0.05。

延伸：DawLabCompressor 的 Mix 參數（parallel compression）其實就是同一招——為什麼它不需要延遲 dry？如果 compressor 有 lookahead，Mix 要怎麼改？
