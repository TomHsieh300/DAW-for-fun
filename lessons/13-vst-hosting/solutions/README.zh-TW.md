# 第 13 章練習解答
[English](README.md) | **繁體中文**

題目在 [`../exercises/README.zh-TW.md`](../exercises/README.zh-TW.md)。實作題的完整程式在本資料夾，會在 `ctest -L solution` 自動執行並以數字驗證：

```powershell
.\scripts\build.ps1 -Only 13-vst-hosting -Juce -TestLabel solution
```

下面引用的輸出都是實際執行結果（Debug build、48 kHz）。

---

## 練習 1（計算）：normalized parameter 換算

公式：$`n = \left(\dfrac{v - s}{e - s}\right)^{k}`$，$`v = s + (e - s)\,n^{1/k}`$。

**1. Threshold −24 dB、Ratio 8:1**

- Threshold（線性，$`k = 1`$）：$`n = \dfrac{-24 - (-60)}{0 - (-60)} = \dfrac{36}{60} = 0.6`$
- Ratio（$`k = 0.5`$）：$`n = \left(\dfrac{8 - 1}{20 - 1}\right)^{0.5} = \sqrt{7/19} = \sqrt{0.36842} = 0.60698`$

**2. `setSkewForCentre(c)` 的 skew**

要求 $`n = 0.5`$ 時 $`v = c`$：

$$
0.5 = \left(\frac{c - s}{e - s}\right)^{k} \;\Longrightarrow\; \ln 0.5 = k \ln\frac{c - s}{e - s} \;\Longrightarrow\; k = \frac{\ln 0.5}{\ln\!\left(\frac{c - s}{e - s}\right)}
$$

- Attack（0.1 … 200 ms，centre 10）：$`\dfrac{9.9}{199.9} = 0.049525`$，$`\ln = -3.00527`$，$`k = \dfrac{-0.693147}{-3.00527} = 0.23064`$
- Release（5 … 2000 ms，centre 100）：$`\dfrac{95}{1995} = 0.047619`$，$`\ln = -3.04452`$，$`k = 0.22767`$

**3. Release 轉到 1/4（$`n = 0.25`$）**

$`n^{1/k} = (0.5^2)^{1/k} = \left(0.5^{1/k}\right)^2`$，而由定義 $`0.5^{1/k} = \dfrac{c - s}{e - s}`$，所以

$$
v = s + (e - s)\left(\frac{c - s}{e - s}\right)^2 = s + \frac{(c - s)^2}{e - s} = 5 + \frac{95^2}{1995} = 5 + 4.524 = 9.524\ \text{ms}
$$

同理，$`n = 0.5^m`$ 時 $`v = s + (e - s)\left(\frac{c-s}{e-s}\right)^m`$：旋鈕每往左「減半」，實際值（扣掉 $`s`$）就乘上 $`\frac{c-s}{e-s} \approx 0.0476`$——這就是 skew 讓小數值區域變寬的效果。

**驗證**：`l13_state_roundtrip` 請 plugin 自己把 normalized 值轉成顯示文字：

```
Exercise 1 check: Threshold 0.6 -> -24.0 dB, Ratio 0.60698 -> 8.00, Attack 0.4 -> 3.86 ms, Release 0.6 -> 216.6 ms, Release 0.25 -> 9.5 ms
```

（Attack 0.4 → $`0.1 + 199.9 \times 0.4^{1/0.23064} = 3.862`$ ms；Release 0.6 → $`5 + 1995 \times 0.6^{1/0.22767} = 216.6`$ ms。）`l13_offline_host` 也驗證了反方向：−30 dB → 0.5、4:1 → 0.3974、5 ms → 0.4251、80 ms → 0.4738，plugin 顯示的文字分別是 −30.0 dB、4.00 : 1、5.00 ms、80.0 ms。

**4. 為什麼只傳 0..1**

- host 不需要理解任何參數的意義：automation lane、MIDI learn、控制器、undo、「把參數畫成曲線」都能用同一套程式處理所有 plugin 的所有參數。
- 曲線（線性、對數、skew）與單位是 plugin 的設計決定，可以在新版本調整，而不破壞舊專案（只要 normalized 值的意義維持相容）。
- 離散參數（選單、開關）也能統一表示（$`k / N`$）。
- 數值範圍有界，host 畫 automation 時不必知道上下限；插值也有定義（在 0..1 之間線性插值，plugin 端再換算，所以 automation 在 skew 參數上的「線性」其實是旋鈕位置的線性，這也是使用者在畫面上看到的）。

---

## 練習 2（觀念）：thread、掃描與 crash

**1. 各動作的 thread**

| 動作 | thread | 理由 |
|---|---|---|
| (a) `scan()` | message thread（或專門的 scanner process / thread，但要依格式規定） | VST3 要求 factory 與 instance 在 UI thread 建立；掃描會載入 DLL、可能很慢，正式產品會放到子 process |
| (b) `process()` | audio thread | 只有它在 audio callback 裡；不 allocate、不等 lock |
| (c) `setState()` | message thread，而且最好在 audio 停止時（或先把 plugin 從 callback 拿掉） | plugin 可能在裡面重建資料結構、allocate；與 `process()` 同時跑可能造成 data race |
| (d) 建立 `PluginWindow` | message thread | 所有 GUI 操作都只能在 message thread |
| (e) 每 32 samples `setParameterValue()` | audio thread（跟 `process()` 同一個 thread，在每個 sub-block 之前） | JUCE 的 VST3 hosting 把參數值寫進 lock-free cache，下一個 `process` 才送給 plugin；參數變化必須跟音訊在同一條時間軸上，所以要在 audio thread 上、緊接著對應的 sub-block 設定（練習 4）。小細節：`setParameterValue` 另外通知 listener 時會短暫拿 `listenerLock`，嚴格的 real-time host 可改用不通知 listener 的 `setValue()` |

**2. 為什麼用 `ScopedTryLock`**

`AudioProcessor::getCallbackLock()` 是一個 `CriticalSection`。在 JUCE 裡，message thread 只有在 `suspendProcessing(true)` 時會拿著它（換 state、`prepareToPlay`、改 bus layout 都**不會**拿這把 lock）。如果 audio thread 用 `ScopedLock` 等待，就要等 message thread 做完（可能幾十 ms，甚至因為 message thread 優先權低而被延後更久——priority inversion），audio callback 錯過 deadline → dropout（爆音）。

try-lock 拿不到就立刻放棄：我們讓那一段 block 維持原音（等同瞬間 bypass）。代價：那幾 ms 的聲音沒經過 plugin，可能聽到音色瞬間改變（對 compressor 來說是一小段沒壓縮的聲音；對 latency 不為 0 的 plugin，原音與處理後的訊號時間不同，可能有 click）。更好的做法是 host 在換 state 之前自己先把 plugin 從 audio graph 拿掉、或用 crossfade bypass（練習 7），讓 try-lock 失敗幾乎不會發生。

注意 try-lock 保護的範圍很小：它**只**在 `suspendProcessing(true)` 期間生效。`setState()` 與 `prepare()` 完全不受它保護——JUCE 的 VST3 hosting 換 state 時拿的是 `MessageManagerLock`，不碰 callback lock——所以必須由 host 自己保證這時 audio thread 沒有同時呼叫 `process()`（`PluginHost.h` 的 thread 規則就是這樣要求的，見 (c)）。另外，JUCE 的 `VST3PluginInstance::processBlock()` 內部會無條件 spin 等待一把 `processMutex`（`SpinLock`），而 `prepareToPlay`、`releaseResources`、`reset`、`isBusesLayoutSupported` 都在 message thread 拿同一把鎖；只要 host 遵守「這些呼叫與 `process()` 不同時進行」的規則，這把鎖永遠不會有競爭，`process()` 才真的「不等待 lock」。

**3. Out-of-process 掃描**

plugin 的程式碼跑在 host 的 process 裡；掃描會執行 DLL 的初始化與 factory（舊 plugin 甚至要 instantiate 才查得到資訊）。in-process 掃描時任何一個 plugin crash → 整個 DAW 結束；更糟的是下次啟動又掃到同一個檔案，又 crash，使用者完全打不開 DAW。

設計：

```
主 process                                         scanner 子 process（同一個執行檔加參數 --scan）
1. 列出所有 .vst3（只看檔名，不載入）
2. 跳過 blacklist 裡的、跳過 KnownPluginList 快取中修改時間沒變的
3. 對每個待掃描檔案：
     送「scan <path>」給子 process  ─────────────►  載入 DLL、findAllTypesForFile
     等回覆，最多 T 秒                ◄─────────────  回傳 PluginDescription（XML 序列化）
     - 收到結果：加進 KnownPluginList
     - 子 process 結束（crash）或逾時：把 path 寫進 blacklist、重開子 process、繼續
4. 把 KnownPluginList 與 blacklist 存檔；UI 顯示「哪些 plugin 被停用」，讓使用者手動重試
```

重點：(1) 每次寫檔「正在掃描哪個檔案」，即使主 process 也 crash，下次啟動也知道是誰；(2) 逾時處理（有些 plugin 會跳出授權對話框卡住）；(3) JUCE 的 `KnownPluginList::CustomScanner` 就是掛這個流程的介面，`extras/AudioPluginHost` 的 `CustomPluginScanner` 是完整範例。本章的 `PluginHost::scan()` 是 in-process 的：某個 plugin 在掃描時 crash，`l13_scan_plugins` 就會直接結束（ctest 判定失敗），而且 KnownPluginList 還沒存檔，下次執行同樣會 crash。

**4. 銷毀順序**

editor（`AudioProcessorEditor`）持有 processor 的參考；它的解構子會呼叫 `processor.editorBeingDeleted(this)`，VST3 的 editor 還要對 plugin 的 `IPlugView` 呼叫 `removed()` 並釋放它（view 屬於 plugin 的 edit controller）。如果 plugin 先被刪掉，editor 解構時就在使用已經釋放的物件（use-after-free）→ 隨機 crash，而且通常發生在「關閉程式」這種難以除錯的時候。所以 `PluginWindow` 的註解規定：先刪視窗，再刪 `HostedPlugin`；`l13_gui_host` 的 `loadPlugin()` 與解構子都照這個順序。

---

## 練習 3（計算）：latency 與 PDC

**1.** DawLabLimiter：$`5\ \text{ms} \times 48\ \text{kHz} = 240`$ samples。linear-phase EQ：1024 samples = $`1024 / 48000 = 21.33`$ ms。Track C：0。

**2.** $`L_{\max} = 1024`$，$`d_i = L_{\max} - L_i`$：

| track | plugin latency $`L_i`$ | host 額外延遲 $`d_i`$ |
|---|---|---|
| A | 240 | 784 samples（16.33 ms） |
| B | 1024 | 0 |
| C | 0 | 1024 samples（21.33 ms） |

整個專案的總 latency = 1024 samples = 21.33 ms（播放時 host 會提早 21.33 ms 讀取 timeline，所以聽起來仍然對拍）。

**3.** 沒有 PDC：Track A 比 C 晚 240 samples = 5 ms。兩個相同的 kick 相加：$`y[n] = x[n] + x[n - D]`$，$`|H(f)| = 2\left|\cos\!\left(\dfrac{\pi f D}{f_s}\right)\right|`$，零點在 $`\dfrac{\pi f D}{f_s} = \dfrac{\pi}{2} + k\pi`$，即

$$
f_k = \frac{(2k+1) f_s}{2D} = \frac{(2k+1) \times 48000}{480} = 100,\ 300,\ 500,\ \dots\ \text{Hz}
$$

kick 的基頻（約 45–120 Hz，見 `makeDrumLoop`）正好落在 100 Hz 的零點附近：低頻被大量抵消，kick 變薄。（練習 6 用 impulse 實際量到 DawLabLimiter 的 240 samples。）

**4.** 錄音時，輸入訊號經過有 latency 的 plugin 監聽（monitoring），耳機裡會聽到延遲 $`L_{\max}`$ 的自己（再加上音訊裝置的 buffer latency），21 ms 以上就明顯影響演奏。DAW 的做法：錄音 / 監聽中的 track 走「低 latency 模式」（Cubase 的 Constrain Delay Compensation、Logic 的 Low Latency Mode 等），暫時 bypass 會造成大 latency 的 plugin，或讓監聽路徑不套用 PDC；另外錄進來的音訊要依 latency 回推時間位置，才會跟其他 track 對齊。

---

## 練習 4（實作）：host 端的 automation —— 切 sub-block

解答：[`ex4_automation.cpp`](ex4_automation.cpp)（`l13_ex4_solution`）

關鍵是 block 裡再切一層：

```cpp
for (int start = 0; start < total; start += kBlockSize) {            // audio callback（512）
    const int blockLen = std::min(kBlockSize, total - start);
    for (int sub = 0; sub < blockLen; sub += kSubBlock) {             // sub-block（32）
        const int len = std::min(kSubBlock, blockLen - sub);
        const float db = thresholdAt(start + sub, total);
        plugin->setParameterValue(thresholdIndex, l13::toNormalized(db, -60.0f, 0.0f));
        plugin->process(all.getSubBlock(start + sub, len));           // AudioBlock 的 sub-block 是零複製的 view
    }
}
```

`thresholdAt()` 把值 snap 到 0.1 dB——跟 plugin 的 interval 一致，參考答案才會完全對得上。

實際輸出：

```
Parameter changes: 12000 (once every 32 samples, time resolution 0.67 ms)
RMS change in the first 0.5 s: +0.00 dB; in the last 1 s: -17.05 dB
Max difference from the reference (dsp::Compressor): 8.94e-08
[PASS] l13_ex4_automation (5 checks passed)
```

8.94e-08 不是 0，是因為 threshold 經過「dB → normalized（float）→ VST3 的 double → plugin 換回 dB 再 snap」的來回，最後一位元的捨入可能不同；差異在 −140 dB 以下，聽不到。

**延伸思考**：sub-block 越小，plugin 被呼叫次數越多（這裡是 512/32 = 16 倍），每次呼叫的固定開銷（VST3 的 `process` 包裝、參數 queue 處理、DSP 的 per-block 設定）跟著放大；有些 plugin 在極小的 block 上效率很差，甚至有 FFT 類 plugin 內部本來就要湊滿固定長度才處理。所以 DAW 通常讓使用者選擇 automation 的解析度，或只在參數真的有變化的地方才切 block。真正 sample-accurate 的做法是 host 填 VST3 的 parameter queue（帶 sampleOffset），由 plugin 自己決定怎麼內插。

---

## 練習 5（實作）：preset 存成檔案再載回

解答：[`ex5_preset_file.cpp`](ex5_preset_file.cpp)（`l13_ex5_solution`）

思路：

- **資料就是 state blob**：`plugin.getState()` → `MemoryBlock::toBase64Encoding()` 放進 `<STATE>`。host 不解讀它，所以這個格式對任何 plugin 都適用。
- **身分辨識**：存 `pluginName` 與 `uid`（`PluginDescription::uniqueId`）。不要用 `createIdentifierString()`：它包含檔案路徑的 hash（例如 `VST3-DawLabCompressor-b2512fcf-dd419ed8` 的中間那段；`l13_scan_plugins` 會印出你機器上的值，build 資料夾不同，這段就不同），plugin 換個資料夾就認不得了。
- **防呆**：解析失敗、名稱或 uid 不符、base64 解不開或長度 0 → 回傳 false 與錯誤訊息，**不要** 把垃圾送進 `setState()`（很多 plugin 不會檢查，可能 crash）。
- `<PARAMS>` 只是給人看的（方便用文字比較工具 diff 兩個 preset），載入時不使用。

實際輸出：

```
preset -> ...dawlab_output\l13_ex5_preset.dawpreset (1615 bytes)
After loading back: max parameter error 0, max sound difference 0
Load into DawLabLimiter: rejected (preset belongs to DawLabCompressor)
Load the corrupted file: rejected (missing or corrupted STATE)
[PASS] l13_ex5_preset_file (6 checks passed)
```

---

## 練習 6（實作）：用 AudioProcessorGraph 串兩個 plugin，並量測 latency

解答：[`ex6_graph_chain.cpp`](ex6_graph_chain.cpp)（`l13_ex6_solution`）

建 graph 的步驟：

```cpp
auto graph = std::make_unique<juce::AudioProcessorGraph>();
graph->setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);            // graph 本身 2 進 2 出
const auto in  = graph->addNode(std::make_unique<IOProcessor>(IOProcessor::audioInputNode))->nodeID;
const auto out = graph->addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode))->nodeID;
const auto c   = graph->addNode(std::move(comp))->nodeID;              // graph 擁有 plugin instance
const auto l   = graph->addNode(std::move(lim))->nodeID;
for (int ch = 0; ch < 2; ++ch) {
    graph->addConnection({{in, ch}, {c, ch}});
    graph->addConnection({{c, ch}, {l, ch}});
    graph->addConnection({{l, ch}, {out, ch}});
}
graph->prepareToPlay(kSampleRate, kBlockSize);                         // 排出 render sequence、算 latency
```

注意 plugin instance 交給 graph 之後就沒有 `HostedPlugin` 可用，所以參數直接對 `AudioProcessor::getParameters()` 設定（`setByName`）。

latency 量測：送一個振幅 0.25 的 impulse 在第 1000 個 sample，找輸出絕對值最大的位置。compressor 的 attack 10 ms 對單一 sample 幾乎沒反應，makeup +12 dB 讓它變成約 1.0，limiter 再把它壓到 −1 dBFS——峰值的「位置」不受影響，只會延後 lookahead 的長度。

實際輸出：

```
Latency reported by the graph: 240 samples (5.00 ms); compressor-only graph: 0
Impulse in at 1000, out at 1240 -> measured latency = 240 samples
peak: input -2.92 dBFS, compressor only 7.25 dBFS, compressor -> limiter -1.00 dBFS
[PASS] l13_ex6_graph_chain (5 checks passed)
```

`l13_ex6_comp_only.wav` 用 32-bit float 寫出，才保留 +7.25 dBFS 的 overs（24-bit 整數 WAV 會被截斷在 0 dBFS）；在播放軟體裡聽會 clip。`l13_ex6_chain.wav` 的 peak 剛好 −1.00 dBFS，聽起來響但不破。

延伸：如果把 graph 改成「compressor 與 limiter 並聯、兩路相加」，graph 會自動在 compressor 那一路插入 240 samples 的延遲（PDC），兩路才對得上——可以自己接接看，用同樣的 impulse 方法確認輸出只有一個峰值。

---

## 練習 7（實作）：沒有 click 的 bypass（latency 補償 + crossfade）

解答：[`ex7_bypass_crossfade.cpp`](ex7_bypass_crossfade.cpp)（`l13_ex7_solution`）

```cpp
void process(dawlab::AudioBlock block, dawlab::host::HostedPlugin& plugin) noexcept {
    // 1) dry 對齊：dry[n] = x[n − L]
    for (int ch …) for (int i …) { delay_.push(ch, x[i]); d[i] = delay_.read(ch, latency_); }
    // 2) wet：plugin 永遠在跑（bypass 時也是，保持內部狀態連續）
    plugin.process(block);
    // 3) y = dry + g·(wet − dry)，g 用 10 ms 線性 ramp 在 0 與 1 之間移動
}
```

為什麼兩件事都要做：

- 只有 crossfade、沒有延遲 dry：fade 的過程中把 $`x[n]`$ 與 $`x[n-L]`$ 相加，正是練習 3 的 comb filter（10 ms 內聽到一個「咻」的音色變化），而且 bypass 之後整條 track 提早 $`L`$ 個 sample；
- 只有延遲、沒有 crossfade：時間對齊了，但 wet（被 limiter 壓到 0.1）與 dry（0.3）的振幅不同，瞬間切換仍是一個不連續點。

實際輸出：

```
plugin latency: 240 samples
Sine's own maximum sample-to-sample change: 0.0173
crossfade + compensation: max jump 0.0173; hard bypass: max jump 0.2680
Max difference between the bypassed output and "input delayed by 240": 0; peak when active: 0.1000 (ceiling 0.1)
plugin Bypass parameter: impulse 1000 -> 1240 (0.2500), reported latency 240; max jump while toggling 0.0173, max difference of the bypass region from "input delayed by L" 0, active-region peak 0.1000
[PASS] l13_ex7_bypass_crossfade (11 checks passed)
```

crossfade 版的最大跳動 = 正弦波本身的最大斜率 $`0.3 \times 2\pi \times 440 / 48000 = 0.0173`$——也就是說切換過程沒有加入任何額外的不連續。hard bypass 的 0.268 幾乎等於正弦波的振幅（0.3），是正弦波本身最大斜率 0.0173 的 15 倍以上，聽得到很清楚的「喀」聲（`l13_ex7_hard.wav` 每 0.5 秒一次；`l13_ex7_crossfade.wav` 沒有）。

**plugin 自己的 Bypass 參數也要遵守同一個規則**：JUCE 的 VST3 wrapper 會自動幫 plugin 加一個 `Bypass` 參數，打開時改呼叫 `processBlockBypassed()`。它的預設實作是直接 pass-through（延遲 0），可是 plugin 回報的 latency 仍是 $`L`$、host 的 PDC 也仍在補償 $`L`$——bypass 的訊號就會提早 $`L`$ 個 sample，跟其他 track 錯位。所以 DawLabLimiter override 了 `processBlockBypassed()`，在 plugin 內部做本題的同一招（dry 延遲 $`L`$ + 10 ms crossfade，見 `plugin/limiter/LimiterProcessor.cpp`）。解答的最後一段驗證它：Bypass = 1 時，位置 1000 的 impulse 在 $`1000 + 240 = 1240`$ 出來、而且就是原音 0.25；播放中每 0.5 秒切換 Bypass 參數，最大跳動仍是 0.0173。

**延伸題**：DawLabCompressor 的 Mix 是同一個公式 $`y = (1-m)\,x + m\,\text{wet}`$。compressor 沒有 lookahead，wet 與 dry 天生對齊，所以 Mix 不需要延遲 dry。如果 compressor 加了 $`L`$ samples 的 lookahead，Mix 就必須先把 dry 延遲 $`L`$（像本題的 `DelayLine`），否則 Mix 在 0..100 % 之間的每個位置都會產生 comb filter；plugin 也要用 `setLatencySamples(L)` 回報，讓 host 做 PDC。
