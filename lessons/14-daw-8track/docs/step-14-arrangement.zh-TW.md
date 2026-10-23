# Step 14 - Arrangement view

[English](step-14-arrangement.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 13](step-13-transport-bar.zh-TW.md) | 下一步：[Step 15](step-15-mixer.zh-TW.md) | 回到[路線圖](../README.zh-TW.md#4-the-twenty-one-steps)

## 目標

八條 lane，每個 track 一條，顯示每個 clip 與其 waveform thumbnail；一條帶有 bar 編號與 loop range 的 ruler；一個跟隨播放的 playhead；點擊 ruler 即可 seek；拖曳 clip 即可移動它——對齊到 beat grid——並立刻聽到結果。

## 設計

### 單一座標系統

View 從 `viewStart`（以 sample 計）開始顯示 timeline，每個 pixel 對應 `samplesPerPixel` 個 sample，位於寬度 $`H`$ = 132 像素的 lane header 右側：

```math
x(s) = H + \frac{s - s_{\text{view}}}{\text{spp}}, \qquad
s(x) = s_{\text{view}} + (x - H)\cdot \text{spp}
```

Zoom 就是改變 spp；*以滑鼠為中心*的 zoom 則讓滑鼠下方的那個 sample 保持原位：

```math
s_m = s(x_m), \qquad \text{spp}' = \text{spp} / \text{factor}, \qquad s'_{\text{view}} = s_m - (x_m - H)\,\text{spp}'
```

`zoomToFit()` 選擇 spp，使整首 song（至少 4 bar）再加一個 bar 剛好填滿寬度。對於 120 BPM / 48 kHz、1400 像素視窗中的 8 bar demo song：$`9 \cdot 96000 / 1268 \approx 681`$ 個 sample 每 pixel，一個 beat = 35 像素。

Snapping 取最近的 beat，$`\hat s = \mathrm{round}(s / N_{\text{beat}})\,N_{\text{beat}}`$（按住 **Alt** 會停用它）。Exercise 8 重複使用 `sampleToX`、`xToSample` 與 `snapToBeat`，來繪製 arrangement 下方的 automation lane。

### Waveform thumbnail：peak cache

一個 12 秒的 clip 每個 channel 有 576 000 個 sample；以每 pixel 681 個 sample 計，天真的 `paint()` 每次重繪都得讀過每個 sample。每個 DAW 都會保留一份縮減過的副本（"peak file"）。`WaveformCache` 對每個 source buffer 只計算一次：每一段 `kSamplesPerBucket = 128` 個 sample 的最小值與最大值（跨所有 channel）。繪製一個 pixel 欄時，只需合併 $`\lceil \text{spp} / 128 \rceil`$ 個 bucket——預設 zoom 下約 6 個——並畫出一條從最小值到最大值的垂直線，再乘上 clip gain。由於 clip 共用 buffer（drum loop 是一個被四個 clip 使用的 buffer；96 個 shaker hit 使用兩個 buffer），cache 以 buffer pointer 為 key：demo song 的 140 個 clip 需要 28 個 cache entry。當 session 改變時，不再被任何 clip 使用其 buffer 的 entry 會被丟棄（`prune`）。

### Playhead 不必整個重繪

`update()` 以 30 Hz 執行。如果 playhead 的 x 移動了，它只重繪兩條 13 像素寬的條帶（舊位置與新位置），而不是整個 view。播放時，view 會*翻頁*：當 playhead 離開可見範圍時，它會像大多數 DAW 一樣跳一整頁，而不是每個 frame 都捲動。

### Playhead 顯示你聽到的

Playhead 繪製在 `getPlayheadPosition()` 處，那就是 `AudioHost::getAudiblePosition()`（Step 13），而不是 `Transport::getPosition()`。Transport position 是 engine 正在 *render* 的 sample；播放時，聽眾要在 mix latency（PDC，Step 8：demo song 中為 752 個 sample = 15.7 ms）加上 device 的 output latency 之後才聽到它：

```math
x_{\text{playhead}} = \text{sampleToX}\big(\max(0,\ \text{position} - L_{\text{mix}} - L_{\text{device}})\big)
```

沒有這項修正的話，那條線會跑在聲音前面——使用 20 ms 的 device 與 demo song 時，超前近 36 ms，超過 30 Hz 顯示的一個 frame，當你看著 drum hit 穿過那條線時非常明顯。翻頁使用同一個 position，所以 view 是在*聽到*的位置離開時才翻頁。`MainComponent` 把它的 `AudioHost` 傳給 constructor；沒有 `AudioHost` 時（exercise 的 test bench），view 顯示 transport position。

### 編輯 clip：GUI 副本與 engine 副本

Arrangement 從不碰 engine 的 clip 清單。它從 `Session::getClips(t)`——GUI 副本——繪製，而拖曳會呼叫 `Session::moveClip(track, index, newStart)`，它會：

1. 在 GUI 副本中移動 clip，並維持清單排序（`std::upper_bound`），
2. 用 `Engine::setTrackClips()` 把新清單的*副本*送給 engine——即 Step 7 的 lock-free pointer swap（舊清單作為垃圾回來，由 30 Hz timer 釋放），
3. 廣播一個 change message，讓各個 view 重繪。

拖曳期間，每次改變對齊後位置的滑鼠移動都會發生這件事——每秒幾十個 command，遠低於 queue 的 1024 個 slot。你在一個 audio block 之內就會聽到 clip 出現在新的位置。

### 滑鼠對應表

| 位置 | 動作 | 效果 |
|---|---|---|
| ruler | 點擊 / 拖曳 | `Transport::setPosition()`（對齊）——一個 pending seek |
| clip | 拖曳 | `Session::moveClip()`（對齊；Alt = 自由） |
| lane | 點擊 | 選取該 track（`Load audio...`、M / S 鍵作用於它） |
| 空白的 lane | 雙擊 | seek 到該處 |
| 任何地方 | 滾輪 / Ctrl + 滾輪 | 捲動 / 以滑鼠為中心 zoom |

## 程式碼

`app/Source/ArrangementView.h/.cpp`：`WaveformCache::get` / `prune`、`sampleToX`、`xToSample`、`zoom`、`snapToBeat`、`paintRuler`、`paintLane`、`paintClip`，以及各個滑鼠 handler。
`app/Source/Session.cpp`：`moveClip`。

Waveform 繪製的核心：

```cpp
for (int px = x0; px < x1; ++px) {
    const double t0 = xToSample(px) - clip.start + clip.sourceOffset;   // source sample at this pixel
    int b0 = floor(t0 / kSamplesPerBucket), b1 = ceil((t0 + samplesPerPixel_) / kSamplesPerBucket);
    float lo = 0, hi = 0;
    for (int b = b0; b < b1; ++b) { lo = min(lo, peaks.minimum[b]); hi = max(hi, peaks.maximum[b]); }
    g.drawVerticalLine(px, midY - hi * gain * halfH, midY - lo * gain * halfH);
}
```

## 驗證

- Snapshot（Step 12）顯示 demo song 的全部八條 lane 與它們的 waveform、位於 bar 2 beat 3 的 playhead，以及在 bar 3 進入的 arp。
- Exercise 8 的 self-check 使用同樣的對應：以 `setView(0, 192)`，一個 beat 剛好是 125 像素，而在 beat 2 旁 3 像素處的點擊會對齊到 sample 48 000。
- 在 app 中：在 loop 播放時把一個 drum clip 拖曳半個 bar——這個改變在下一輪就聽得到，而且沒有 click 或 dropout。

## 換你動手

| | |
|---|---|
| 你要寫 | `app/Source/ArrangementView.cpp` 中的 `ArrangementView::sampleToX()`、`xToSample()` 與 `snapToBeat()`（「單一座標系統」中的公式；snapping 取最近的 beat，且絕不回傳負的位置），以及 `app/Source/Session.cpp` 中的 `Session::moveClip()`（移動、維持清單依 start 排序、把新的 clip 清單推送給 engine） |
| 建置 + 檢查 | `.\scripts\build.ps1 -Only 14-daw-8track -Juce -TestLabel solution`：`l14_ex8_solution` 在*你的* time axis 上繪製它的 automation lane |
| 預期 | `[PASS] l14_ex8_solution`：以 `setView(0, 192)`，一個 beat 是 125 像素，而在 beat 2 旁 3 像素處的點擊會對齊到 sample 48 000；snapshot（`l14_app_snapshot`）顯示 clip 在原本的位置；在 app 中，被拖曳的 clip 會落在 beat 上，並在下一輪 loop 就在那裡被聽到 |

## 你學到了什麼

- 一對對應函式（及其反函式）就是 timeline 的全部幾何。
- Peak cache 讓 waveform 繪製與 clip 長度無關。
- GUI 編輯自己的資料副本，並把 immutable snapshot 送給 audio thread。
