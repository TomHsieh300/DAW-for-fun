# Step 20 - Spectrum analyzer 與 loudness 顯示

[English](step-20-spectrum-loudness.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 19](step-19-export.zh-TW.md) | 下一步：[Step 21](step-21-shortcuts-and-tests.zh-TW.md) | 回到[路線圖](../README.zh-TW.md#4-the-twenty-one-steps)

## 目標

在 master strip 旁邊：master 輸出的即時 spectrum（20 Hz - 20 kHz，對數刻度）與 EBU R 128 loudness——momentary、short-term、integrated——外加 mix latency。全部使用 chapter 11 的 class，全部由 audio thread 無 lock 地餵入。

## 設計

### 兩個量測點，一條規則

| 顯示 | 量測者 | 位置 | GUI 如何取得 |
|---|---|---|---|
| strip meter、master meter | 每個 `ChannelStrip` 中的 `dsp::LevelMeter` | audio thread，`ChannelStrip::meter()` | atomic，30 Hz poll |
| LUFS M / S / I | 每個 `ChannelStrip` 中的 `dsp::LoudnessMeter` | audio thread | atomic，30 Hz poll |
| spectrum | `AudioHost` 中的 `dsp::SpectrumAnalyzer` | audio thread 推入 sample | message thread 上的 `computeSpectrum()` |

Chapter 11 的規則：audio thread 只*寫入*（sample 寫進 lock-free FIFO，結果寫進 atomic）；所有昂貴的或會 allocate 的事都發生在讀取端。

### Spectrum：推入 mono sample，在 GUI 端計算

`Engine::processBlock()` 之後，device callback 在 device buffer 裡就有 master 輸出。`AudioHost::render()` 在預先配置好的 scratch buffer 中形成 mono 總和 $m[n] = \tfrac12(L[n] + R[n])$，並呼叫 `SpectrumAnalyzer::pushSamples(m, n)`——複製進 analyzer 的 ring buffer。在 message thread 上，`SpectrumView::update()` 呼叫 `computeSpectrum(magnitudesDb)`，它對最新的 4096 個 sample 加窗、執行 chapter 11 的 FFT，並回傳以 dB 表示的 $N/2 + 1 = 2049$ 個 bin（若沒有新資料抵達則回傳 `false`，所以停止的 transport 不花任何成本）。以 `prepare(sampleRate, 12)`，FFT 有 $2^{12} = 4096$ 個點：bin 間距為 $f_s / N = 48000 / 4096 = 11.7$ Hz，足以區分 bass 音符（A1 = 55 Hz、F1 = 43.7 Hz）。

### 在對數軸上繪製

音樂人聽的是 octave，所以 x 軸是對數的：

$$
x(f) = \frac{\ln(f / 20)}{\ln(20000 / 20)}, \qquad f(x) = 20 \cdot 1000^{x}
$$

在低頻端，一個 pixel 涵蓋不到一個 bin（對 snapshot 中 285 像素寬的 view，約在 480 Hz 以下，README 第 3.11 節）；在高頻端，一個 pixel 涵蓋約 20 個 bin。因此 `SpectrumView::paint()` 逐個 pixel 欄走訪，取頻率落在該欄內的所有 bin 的**最大值**（讓窄的 peak 不會遺失在 pixel 之間），而當某一欄不含任何 bin 時，重複使用其上的下一個 bin——這就是為什麼 trace 在低頻端看起來像階梯。在 analyzer 自己的平滑之上再加顯示 ballistics：數值可以立即上升，但每個 frame 最多下降 1.5 dB（30 Hz 下為 45 dB/s）——平穩，但對 transient 永不延遲。Y 軸涵蓋 0 .. -90 dB，每 20 dB 一條線。標籤（頂端邊緣的 0 dB，然後是 -20、-40、……以及各頻率）最後才畫，底下襯深色底，讓 trace 永遠不會遮住它們。

### Loudness

Master strip 的 `dsp::LoudnessMeter`（BS.1770 K-weighting、400 ms momentary、3 s short-term、經 gate 的 integrated）發佈三個 atomic。`LoudnessDisplay` 以一位小數顯示它們（低於 -70 LUFS，也就是 absolute gate，顯示 "-inf"）。它只有在某個值改變 0.05 LU（loudness unit；1 LU 是 LUFS 中 1 dB 的差異）時才重繪。Integrated 值從上一次 engine reset（新 project、demo song、export）起累積。串流平台會 normalize 到約 -14 LUFS；demo song 的 master 約在 -15.5 LUFS integrated（stage 1 的 mix report，Step 9）。

### Mix latency

在 loudness 之下，mixer 以 sample 與毫秒顯示 `Engine::getLatencySamples()`——demo song 為 752 個 sample = 15.67 ms（track 8 的 convolution reverb 512 + master limiter 5 ms lookahead 的 240）。插入一個帶 latency 的 plugin（Step 17）並觀察它增加。如果某個 track 或 bus 上的 insert 加總超過 PDC 上限（16 384 個 sample，Step 8），該行會變紅並加上 "PDC limit exceeded, not aligned!"（`Engine::hasPdcOverflow()`）。

## 程式碼

- `app/Source/AudioHost.cpp`：`render()` 中的 analyzer feed、`prepareInternal()`（在 device 停止時以 FFT order 12 prepare analyzer；`isAnalyzerReady()` 保護 GUI 端）。
- `app/Source/Meters.h/.cpp`：`MeterBar`、`LoudnessDisplay`、`SpectrumView`（`frequencyToProportion`、`proportionToFrequency`、`update`、`paint`）。
- `app/Source/MixerView.cpp`：master 區段的 layout、`update()`。

## 驗證

- `l14_app_smoke_test` 的第 8 節以數字檢查顯示路徑：一個剛好落在 4096 點 FFT 的 bin 85 上的 sine（$85 \cdot 48000 / 4096 = 996.09$ Hz），振幅 0.5，必須在 `SpectrumView` 中出現在該頻率，且為 $20 \log_{10} 0.5 = -6.02$ dBFS（analyzer 除以 window 的 coherent gain，chapter 11），而對數軸必須把兩端的幾何平均 $\sqrt{20 \cdot 20000} = 632.5$ Hz 剛好放在正中央：

  ```
  8. spectrum view: 996.09 Hz sine at 0.5 -> peak at 996.09 Hz, -6.02 dBFS (-6.02 expected); 632.5 Hz at x = 0.500
  ```

  它也會在 device callback 模擬（第 4 節）之後檢查 `isAnalyzerReady()`。
- Snapshot（Step 12）顯示 demo song 播放 1.5 bar 之後的 spectrum。此時只有 drum、bass、pad 與第一個 hit 的 reverb tail 在播放（arp 與 shaker 從 bar 3 開始）：能量從 40-50 Hz（kick 與 bass）延伸到約 2 kHz，然後朝 10 kHz 下降，那裡只剩下 drum loop 的雜訊 hat 與 snare。Snapshot 會印出它所顯示的 loudness：bar 2 beat 3 時 M -17.2、S -15.8、I -15.9 LUFS（這些數字取決於 chapter 01-12 的 DSP 實作）。
- 在 app 中 solo bass：spectrum 會塌縮到 300 Hz 以下；solo shaker：只剩高頻。

## 換你動手

| | |
|---|---|
| 你要寫 | `app/Source/Meters.cpp` 中的 `SpectrumView::frequencyToProportion()` 與 `proportionToFrequency()`（20 Hz 到 20 kHz 之間的對數軸，兩端皆做 clamp） |
| 建置 + 檢查 | `.\scripts\build.ps1 -Only 14-daw-8track -Juce -TestLabel test`（`l14_app_smoke_test`） |
| 預期 | 第 8 節：`peak at 996.09 Hz, -6.02 dBFS (-6.02 expected); 632.5 Hz at x = 0.500`；`[PASS] l14_app_smoke_test`；在 snapshot 中，spectrum 的格線（100 Hz、1 kHz、10 kHz）位於原本的位置 |

## 你學到了什麼

- 量測在 audio thread 上產生，並在別處*顯示*；交接方式是 sample 的 FIFO 或一組 atomic。
- 對數頻率顯示需要逐 pixel 聚合（取 max），而不是逐 bin 繪製。
- Ballistics 是 meter 設計的一部分，而不是事後補上的東西。
