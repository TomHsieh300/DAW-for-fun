# Step 15 - Mixer

[English](step-15-mixer.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 14](step-14-arrangement.zh-TW.md) | 下一步：[Step 16](step-16-inserts-panels.zh-TW.md) | 回到[路線圖](../README.zh-TW.md#4-the-twenty-one-steps)

## 目標

八個 track strip、FX bus strip 與一個 master 區段，每個 strip 都有：名稱、Load 按鈕（track 上還有 Mono / Stereo format 按鈕）、四個 insert slot、send 旋鈕、pan、帶 dB 刻度的垂直 fader、mute / solo，以及 stereo peak meter。每個控制項都直接寫入 engine 的 atomic；每個顯示都讀取它們。

## 設計

### Strip 由 engine 的常數產生

`MixerView` 針對 `stripRefAt(0 .. kNumStrips-1)`（`Session.h`）的每個項目建立一個 `ChannelStripView`：track 1-8、bus A..(kNumBuses)、master。每個 track strip 會得到 `kNumBuses` 個 send 旋鈕。GUI 裡沒有任何地方寫著「2 個 bus」：Exercise 12 以三個 bus 重新編譯 engine 與 app，mixer 就直接有十二個 strip。

| strip 種類 | Load | insert | send | pan | fader | M | S | 額外 |
|---|---|---|---|---|---|---|---|---|
| track | 有 | 4 | 每個 bus 一個（預設為 post-fader；pre-fader send 會顯示 "A pre"，並在 tooltip 中說明） | mono track：pan law（constant power），stereo track：balance | 有 | 有 | 有 | format 按鈕（Mono / Stereo） |
| bus | - | 4 | - | balance | 有 | 有 | -（solo-safe） | - |
| master | - | 4 | - | balance | 有 | - | - | limiter ceiling、spectrum、LUFS |

這些差異來自 stage 1：mono track 使用 constant-power pan law，stereo track、bus 與 master 使用 balance（Step 4 的「Mono 與 stereo track」，以及 debugging 表中的「Everything 3 dB too quiet」）；bus 是 solo-safe，所以沒有 solo 按鈕（Step 6）。

### Format 按鈕與 Pan / Bal 標籤

Track strip 在 Load 旁邊有一個小的切換按鈕，顯示 track 的 format：**Mono** 或 **Stereo**（`ChannelStripView::format_`）。點一下會呼叫 `Session::setTrackFormat()`，它透過 `Track::setFormat()` 寫入 track 的 format atomic - 不需要 engine command，audio thread 每個 block 讀一次 - 同時也設定 pan mode（Step 4）。pan 的位置維持不變，改變的只是它的意義。載入檔案時會依檔案的 channel 數自動設定 format（Step 18），所以這個按鈕主要是用來覆寫它，例如把 mono source 放到 stereo track 上，讓會加寬聲像的 insert 保持寬度（Step 5）。

pan 旋鈕下方的標籤會跟著 format 改變，因為這個旋鈕做的是兩件不同的事：

| format | 標籤 | tooltip | 旋鈕置中時 |
|---|---|---|---|
| Mono | `Pan` | "Pan (constant-power law: centre -3 dB per side)" | 每側 x0.7071 |
| Stereo | `Bal` | "Balance (centre: unity; turning right lowers the left side)" | 每側 x1 |

按鈕文字、切換狀態、標籤與 tooltip 都在 `refreshStructure()` 中設定，它會在每次 `Session` 的 change message 之後執行 - 所以由 loader、專案檔或 exercise 設定的 format，顯示方式都和點擊一樣。

### Fader law

Fader 的行程應該把大部分長度花在混音真正發生的區間（-20 .. +6 dB），而不是 -60 .. -40。App 以平方律把位置 $`p \in [0, 1]`$ 對應到 dB：

```math
\text{dB}(p) = 12 - 72\,(1 - p)^2, \qquad p(\text{dB}) = 1 - \sqrt{\frac{12 - \text{dB}}{72}}
```

| dB | +12 | +6 | 0 | -6 | -12 | -30 | -60（最底端 = $`-\infty`$） |
|---|---|---|---|---|---|---|---|
| $`p`$ | 1.000 | 0.711 | 0.592 | 0.500 | 0.423 | 0.236 | 0 |

所以 0 dB 位於行程的 59 % 處，-6 dB 剛好在正中間；最底端的位置代表靜音（`FaderLaw::toEngineDb` 把 -60 對應到 $`-\infty`$ = `kMinusInfinityDb`）。畫在 fader 旁的刻度也使用 `dbToPosition()`，所以數字與 cap 對得上。Fader 寫入 `ChannelStrip::setGainDb()`——一個 atomic；strip 會在 50 ms 內 ramp 到新的 gain（Step 4），所以不論滑鼠事件多粗糙，拖曳 fader 都不會產生 zipper noise。

### 跟隨 engine 的控制項

Strip 的控制項不是 source of truth；engine 才是。`ChannelStripView::update()`（30 Hz）會從 `ChannelStrip` 讀回 gain、pan、send、mute 與 solo，並移動控制項——除非滑鼠正在它們上面。這就是為什麼 project load、鍵盤快捷鍵（對選取的 track 按 M / S，Step 21）或直接呼叫 `setGainDb()` 的 exercise 程式，全都能在不需要任何通知程式碼的情況下顯示在 GUI 中。

### Meter

每個 strip 的 `MeterBar` 顯示 `getPeakDb(ch)`（chapter 11 的 `LevelMeter`：instant attack，以 dB/秒衰減）與 `getPeakHoldDb(ch)`。在 -60 到 +6 dBFS 之間以 dB 線性呈現，低於 -18 為綠色，到 -6 為黃色，其上為紅色，0 dBFS 處有一道刻線：

```math
\text{position} = \frac{\text{dB} + 60}{66}
```

Meter 只有在某個值移動超過 0.1 dB 所對應的 pixel 數（高度的 1/660）時才重繪——11 個 meter 大多數時候大多是靜止的。

### Strip *不*做的事

它從不呼叫 DSP 函式、從不碰 insert chain，也從不代表 audio thread 做 allocation。Insert 透過 `Session`（Step 16）處理。Load 按鈕只呼叫 `onLoadRequested(track)`；`MainComponent` 開啟 file chooser，並把檔案交給 loader thread（Step 18）。

## 程式碼

`app/Source/MixerView.h/.cpp`：`FaderLaw`、`ChannelStripView`（constructor 把每個控制項接到 strip 的 setter）、`update()`、`refreshStructure()`（`Session` 變更後的名稱與 slot 標籤）、`MixerView::resized()`（98 像素的 strip、bus 與 master 之前有間隔、其餘寬度留給 spectrum 與 loudness，Step 20）。

```cpp
fader_.onValueChange = [this] {
    const float db = FaderLaw::positionToDb(static_cast<float>(fader_.getValue()));
    strip().setGainDb(FaderLaw::toEngineDb(db)); // atomic; the engine ramps over 50 ms
    gainLabel_.setText(FaderLaw::format(db), juce::dontSendNotification);
};
```

## 驗證

`l14_app_smoke_test`：

- `FaderLaw::dbToPosition(0) = 0.5918`，且 `positionToDb(dbToPosition(-12)) = -12`；
- 把 track 1 的 fader 移到 -6 dB 的位置，會讓 `ChannelStrip::getGainDb()` 變成 -6.00；
- 點擊 track 1 的 S 會在 engine 中將它 solo；
- 11 個 strip，每個 track strip 有 `kNumBuses` 個 send 旋鈕；
- 點擊 track 2 的 format 按鈕會讓它 Mono -> Stereo -> Mono 切換，engine 的 pan mode 也跟著改變（mono 為 pan law，stereo 為 balance）；測試會印出 `format button: track 2 Mono -> Stereo -> Mono`。

Snapshot 顯示 demo song 的混音：fader 位於 song 的設定值（drum -2 dB、wind -13 dB……）、format 按鈕（Pad、Wind 與 Hits 是 stereo，標籤為 `Bal`）、track 送往 reverb 與 delay bus 之處的 send 旋鈕（橘色）、有訊號的 meter。

## 換你動手

| | |
|---|---|
| 你要寫 | `app/Source/MixerView.cpp` 中的 `FaderLaw::positionToDb()` 與 `FaderLaw::dbToPosition()`（「Fader law」的平方律；位於最底端的 position 代表 $`-\infty`$） |
| 建置 + 檢查 | `.\scripts\build.ps1 -Only 14-daw-8track -Juce -TestLabel test`（`l14_app_smoke_test`） |
| 預期 | 本頁的 fader 檢查通過：`dbToPosition(0) = 0.5918`、`positionToDb(dbToPosition(-12)) = -12`、把 track 1 的 fader 移到 -6 dB 位置會讓 engine 設為 -6.00 dB；`[PASS] l14_app_smoke_test` |

## 你學到了什麼

- 從 engine 的常數而不是字面值來產生 GUI 結構。
- 好的 fader law 就是一條公式；用它的反函式來畫刻度。
- 讓 GUI 重新讀取 engine，而不是保留第二份需要同步的值。
- 一個旋鈕代表兩件事（pan law 或 balance）時，讓它的標籤說出是哪一個。
