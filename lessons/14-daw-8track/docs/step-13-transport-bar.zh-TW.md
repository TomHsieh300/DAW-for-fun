# Step 13 - Transport bar

[English](step-13-transport-bar.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 12](step-12-main-window.zh-TW.md) | 下一步：[Step 14](step-14-arrangement.zh-TW.md) | 回到[路線圖](../README.zh-TW.md#4-the-twenty-one-steps)

## 目標

Play / Stop、Return to start、Loop 開/關、以 bars.beats.ticks 與 m:ss.mmm 顯示的位置、tempo、帶有 LUFS 的 master peak meter，以及 CPU load——全部放在一條 58 像素的長條裡。每個控制項都必須能用滑鼠*以及*鍵盤操作，而且每個顯示都必須跟隨 audio thread，卻絕不 block 它。

## 設計

### 讀取 audio thread 的狀態：以 30 Hz polling atomic

Transport（stage 1，Step 2）把 play state、position、loop range 與 tempo 存在 atomic 裡。GUI 從不向 audio thread 詢問任何事；它在 message thread 上以 `juce::Timer` 每秒 30 次*讀取*這些 atomic：

```
audio thread (every 10.7 ms)                    message thread (every 33 ms)
transport.advance() -> position_.store()  ....  TransportBar::update(): getPositionBarsBeats()
LevelMeter::process() -> peak_.store()    ....  master.getPeakDb(0), getPeakHoldDb(0)
LoudnessMeter::process() -> lufs_.store() ....  getMomentaryLufs(), getShortTermLufs(), ...
AudioHost::render() -> cpuLoad_.store()   ....  audioHost.getCpuLoad()
```

30 Hz 對眼睛已經足夠（電影以 24 fps 播放），而且不花任何成本。這些值可能是一個 block 之前的，沒有人看得出來。沒有 queue、沒有 lock，也沒有來自 audio thread 的訊息。

### 寫入：以同樣的 atomic 反向操作

- **Play / Stop**：`Transport::setPlaying(!isPlaying())`。
- **Return to start**：`Transport::setPosition(0)`——一個 *pending seek*（Step 2）：audio thread 在下一個 block 開頭才套用它，絕不在 block 進行到一半時套用。
- **Loop**：`setLoopEnabled()`；loop range 顯示在 arrangement 的 ruler 上（Step 14）。
- **Tempo**：一個 `LinearBar` 樣式的 `juce::Slider`（20 .. 300 BPM，步進 0.1），呼叫 `Transport::setTempo()`。Clip 是以 sample 為單位定位的，所以 tempo 變更移動的是 audio 底下的 bar grid，而不是 audio 本身（stage 1 的 Exercise 7 討論了原因）。

按鈕自己不呼叫 transport：它們呼叫 `onPlayStop`、`onReturnToStart`、`onToggleLoop`，由 `MainComponent` 把它們接到 menu 與鍵盤 command 所用的同一批函式（Step 21）。一個動作，一份實作。

### 格式化位置

`Transport::getPositionBarsBeats()`（stage 1）負責計算；顯示端只做格式化：

```cpp
juce::String TransportBar::formatBarsBeats(const engine::BarsBeats& p) {
    return juce::String(p.bar) + "." + juce::String(p.beat) + "." + juce::String(p.tick).paddedLeft('0', 3);
}
// 120 BPM, 48 kHz: sample 24000 -> "1.2.000", sample 132000 -> "2.2.480"
```

而 `formatTime()` 以毫秒的整數運算把秒數轉成 `m:ss.mmm`，所以顯示永遠不會出現 "0:60.000"。

### 顯示聽到的，而不是 render 出的

`Transport::getPosition()` 是 engine *現在*正在 render 的 sample。播放時，那個 sample 會稍後才到達喇叭：

```
engine renders sample P ──> PDC delays every path by L_mix ──> device buffers, driver, DAC: L_device ──> ear
```

- $L_{\text{mix}}$ = `Engine::getLatencySamples()`，即 Step 8 的 plugin delay compensation。Bounce 會把它移除（Step 9）；即時播放則不能：它是真實的延遲。在 demo song 中它是 752 個 sample（track 8 的 convolution reverb 512 + master limiter 240）= 15.7 ms。
- $L_{\text{device}}$ = driver 針對所開啟 device 回報的值（`juce::AudioIODevice::getOutputLatencyInSamples()`，在 `audioDeviceAboutToStart()` 中讀取），通常是 5..30 ms；沒有 device 時為 0。

忽略它們的位置顯示，會比聲音超前 $L_{\text{mix}} + L_{\text{device}}$。`AudioHost::getAudiblePosition()` 修正了這一點，這裡的計數器與 playhead（Step 14）都使用它：

$$\text{shown} = \begin{cases}\max(0,\ P - L_{\text{mix}} - L_{\text{device}}) & \text{playing}\\ P & \text{stopped}\end{cases}$$

Transport 停止時沒有任何聲音，所以顯示的是播放將開始的位置（因此你按下 Stop 的瞬間，它會向前跳 $L_{\text{mix}} + L_{\text{device}}$）。這個公式是一個 static 函式，`AudioHost::compensatePosition(position, playing, mixLatency, deviceLatency)`，所以 smoke test 可以用任意的 latency 來檢查它。（在 loop wrap 處，顯示會短暫地呈現 loop 起點之前的位置——持續 $L$ 個 sample，約為 30 Hz 顯示的半個 frame。）

### 圖示

按鈕是一個小型的 `IconButton : juce::Button`，以 `juce::Path` 繪製它的圖示（三角形、方形、長條 + 三角形、圓形箭頭）。沒有圖片檔，在任何 DPI 下都銳利。Play 按鈕在 `update()` 中於 Play 與 Stop 圖示之間切換；loop 按鈕在 loop 開啟時亮起橘色。它們全都呼叫 `setWantsKeyboardFocus(false)`：如果按鈕在被點擊後保有鍵盤 focus，下一個 Space 會再次「點擊」那個按鈕，而不是傳到 Play/Stop command。

### Master meter 與 loudness

水平的 `MeterBar`（Step 20 解釋它的刻度）顯示 master 各 channel 的 peak 與 peak hold；`LoudnessDisplay` 則顯示來自 master strip 的 `dsp::LoudnessMeter`（chapter 11）的 **M**omentary（400 ms）、**S**hort-term（3 s）與 **I**ntegrated loudness。每當 engine 被 reset（新的 project、一次 bounce），integrated 值就會重新開始。

## 程式碼

`app/Source/TransportBar.h/.cpp`。`update()`：

```cpp
void TransportBar::update() {
    const engine::Transport& tr = engine_.getTransport();
    const int64_t audible = audioHost_.getAudiblePosition();   // minus mix + device latency while playing
    barsBeats_.setText(formatBarsBeats(tr.samplesToBarsBeats(audible)), juce::dontSendNotification);
    time_.setText(formatTime(tr.toSeconds(audible)), juce::dontSendNotification);
    playButton_->setIcon(tr.isPlaying() ? IconButton::Icon::Stop : IconButton::Icon::Play);
    loopButton_->setToggleState(tr.isLoopEnabled(), juce::dontSendNotification);
    if (!tempo_.isMouseButtonDown() && std::abs(tempo_.getValue() - tr.getTempo()) > 1e-6)
        tempo_.setValue(tr.getTempo(), juce::dontSendNotification);
    const engine::ChannelStrip& master = engine_.getMixer().getMaster();
    masterMeter_.setLevels(master.getPeakDb(0), master.getPeakDb(1), master.getPeakHoldDb(0), master.getPeakHoldDb(1));
    loudness_.setValues(master.getMomentaryLufs(), master.getShortTermLufs(), master.getIntegratedLufs());
    cpu_.setText("CPU " + juce::String(juce::roundToInt(100.0f * audioHost_.getCpuLoad())) + "%", juce::dontSendNotification);
}
```

注意到處都用 `dontSendNotification`：更新顯示不能觸發控制項自己的 callback（否則 tempo 顯示會每秒 30 次把 tempo 寫回 engine，並和使用者的拖曳互相拉扯）。`isMouseButtonDown()` 讓使用者按住控制項時不去動它。

## 驗證

Smoke test 透過模擬的 callback 播放 demo song，直到 beat 2（120 BPM 下的 24 000 個 sample）被*聽見*：24 000 + 752 個 sample，因為這首 song 的 mix latency 是 752 個 sample，且沒有 device（device latency 為 0）。接著它檢查各個顯示：

```
6. GUI offline: 11 strips, position 1.2.000 (playhead 24000, transport 24752 = + mix latency 752),
   master peak -11.8 dBFS, status "Demo song loaded"
```

- 計數器顯示 "1.2.000"，而 arrangement 的 playhead 位於 sample 24 000，比 transport 位置 24 752 *少*——剛好落後 mix latency；
- 同時帶有 device latency 的 `compensatePosition()`：position 24 752、mix latency 752、device 256 -> 23 744；position 500 -> 0（絕不早於起點）；stopped -> 不變；
- Stop 之後，計數器跳到 transport 位置："1.2.030"（752 個 sample = 30 個 tick）。

`l14_app_smoke_test` 也會透過 key mapping 按下 Space 並檢查 transport 是否切換，以及用 `L` 切換 loop。在 app 中：Space 播放、Home 回到 bar 1、L 切換 loop，tempo slider 可以拖曳，或雙擊後輸入數值。

## 換你動手

| | |
|---|---|
| 你要寫 | `app/Source/AudioHost.cpp` 中的 `AudioHost::compensatePosition()`（播放時：position 減去 mix latency 再減去 device latency，絕不低於 0；停止時：不變）與 `app/Source/TransportBar.cpp` 中的 `TransportBar::formatBarsBeats()`（"bar.beat.tick"，tick 為三位數） |
| 建置 + 檢查 | `.\scripts\build.ps1 -Only 14-daw-8track -Juce -TestLabel test`（`l14_app_smoke_test`） |
| 預期 | 第 6 節印出 `position 1.2.000 (playhead 24000, transport 24752 = + mix latency 752)`；本頁的三項 `compensatePosition()` 檢查（23 744、0、不變）與 Stop 之後的 "1.2.030" 通過；`[PASS] l14_app_smoke_test` |

## 你學到了什麼

- GUI 與 audio thread 在 atomic 相遇；GUI 以自己的步調 polling。
- 顯示更新使用 `dontSendNotification`，並跳過使用者正按住的控制項。
- 把按鈕、menu 項目與按鍵導向每個動作各一個函式。
