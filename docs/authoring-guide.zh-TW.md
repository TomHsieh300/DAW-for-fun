# 教材撰寫規範（Authoring Guide）
[English](authoring-guide.md) | **繁體中文**

這份文件規定每一章的結構、程式風格與 API 合約。之後自己新增章節或練習時也照這份走，整套教材才會一致。

## 1. 讀者與語言

- 讀者：會基本 C++（class、template、std::vector、lambda），**沒學過 DSP**，目標是能自己寫 DAW。
- 本教材是**中英雙語**，**英文為主要語言**：
  - 英文版使用原檔名（例如 `README.md`、`exercises/README.md`），程式碼的註解與 `printf` / `std::cout` 輸出一律用自然的英文。
  - 繁體中文版放在同目錄、同名加上 `.zh-TW` 的檔案（例如 `README.zh-TW.md`、`exercises/README.zh-TW.md`），內容必須與英文版保持同步；中文版不可出現簡體字。
  - 每份 Markdown 的第一行（標題）之後有一列語言切換：英文檔是 `**English** | [繁體中文](xxx.zh-TW.md)`，中文檔是 `[English](xxx.md) | **繁體中文**`。英文檔的連結指向其他英文檔，中文檔的連結指向對應的 `.zh-TW.md`；指向程式碼、圖片與 LICENSE 的連結兩個版本相同。
  - 中文版的專有名詞與技術術語保留英文，不硬翻：sample rate、buffer、block、filter、cutoff、Q、biquad、gain、dB、envelope、attack、release、threshold、ratio、knee、lookahead、aliasing、oversampling、waveshaper、plugin、host、callback、thread、lock-free……第一次出現時可以在括號補中文說明。
  - 中文版引用的程式輸出保留程式實際印出的英文；README 引用的輸出一律是程式實際跑出來的結果。
  - **第 14 章不再是特例**：全書的程式（註解、UI 文字、輸出）都是英文，各章都使用同一套規則。`Check.h` 的 PASS/FAIL 輸出也是英文。
- 程式碼識別字用英文。
- 數學用 GitHub 支援的 LaTeX：行內公式寫成 ``$`...`$``，獨立公式放在 ` ```math ` 區塊裡（兩者都讓 TeX 不經過 Markdown 處理；否則 `\\`、`\,`、`\;` 這類反斜線會被吃掉，或底線會跨公式被配成斜體）。在清單項目（list item）裡，GitHub 會把 ` ```math ` 區塊顯示成一般程式碼，所以那裡的獨立公式改成單獨一行的 ``$`\displaystyle ...`$``。避免 GitHub 不接受的巨集，例如 `\operatorname`（改用 `\mathrm`）。
- 圖：訊號流程、方塊圖、state machine、時序圖用 ` ```mermaid ` 區塊（標籤用該版本的語言）；曲線（頻率響應、transfer curve、envelope、波形）用 SVG 圖，由該章的 `images/make_figures.py` 透過 `docs/tools/plotstyle.py` 依公式計算產生。不要在程式碼區塊裡用框線字元、箭頭或中文字畫圖：在 GitHub 上會歪掉。用 `python docs/tools/check_mermaid.py` 檢查每個 Mermaid 區塊都能正確渲染。

## 2. 目錄結構

| 路徑 | 內容 |
|---|---|
| `common/include/dawlab/` | 共用工具（已提供，不要改 API） |
| `common/include/dawlab/AudioBuffer.h` | `AudioBlock` / `AudioBuffer`（non-interleaved float） |
| `common/include/dawlab/WavFile.h` | `readWav` / `writeWav` / `outputPath` |
| `common/include/dawlab/Signals.h` | `makeSine` / `makeLogSweep` / `makeWhiteNoise` / `makeImpulse` / `makeToneBursts` / `makeDrumLoop` / `makePluck` / `makePluckSequence` / `NoiseGenerator` |
| `common/include/dawlab/Analysis.h` | `peakAbs` / `rms` / `hasNanOrInf` / `goertzelAmplitude` / `measureGainDb` / `harmonicLevelsDb` / `thdPercent` |
| `common/include/dawlab/Check.h` | `DAWLAB_CHECK` / `DAWLAB_CHECK_NEAR` / `dawlab::test::summary` |
| `common/include/dawlab/DspMath.h` | `kPi` / `kTwoPi` / `dbToGain` / `gainToDb` / `lerp` / `msToSamples` |
| `common/include/dawlab/Common.h` | 一次 include 以上全部 |
| `dsp/include/dawlab/dsp/` | 各章逐步長出來的 DSP 元件（header-only，namespace `dawlab::dsp`） |
| `lessons/NN-name/README.md` | 原理講義（主教材，英文）；README.zh-TW.md 為繁體中文版 |
| `lessons/NN-name/CMakeLists.txt` | 本章的建置檔 |
| `lessons/NN-name/examples/*.cpp` | 範例程式（每支都能直接執行，ctest label: example） |
| `lessons/NN-name/exercises/README.md` | 練習題（只有題目與提示；另有 README.zh-TW.md） |
| `lessons/NN-name/exercises/starter/*.cpp` | 實作題的起始碼（可編譯，留 TODO；只建置、不跑測試） |
| `lessons/NN-name/solutions/README.md` | 所有題目的解答與解說（含觀念題、計算題；另有 README.zh-TW.md） |
| `lessons/NN-name/solutions/*.cpp` | 實作題的完整解答（含 self-check，ctest label: solution） |
| `lessons/NN-name/tests/*.cpp` | （可選）本章 dsp 元件的回歸測試（ctest label: test） |

## 3. 每章 README.md 的結構

1. **本章目標**：讀完能做到什麼（條列）。
2. **原理**：從直覺 → 數學 → 圖解。要推導關鍵公式，不只丟結論；說明每個參數在聽感上的意義。
3. **從公式到程式碼**：逐段解說 `dsp/include/dawlab/dsp/` 裡本章新增的 header，以及每支範例在示範什麼、怎麼執行、輸出哪些 WAV、該聽到什麼。
4. **Real-time 注意事項**：本章元件在 audio thread 上要注意的事（allocation、denormal、參數平滑、thread safety……）。
5. **常見錯誤與除錯**：初學者最常踩的坑與症狀（爆音、zipper noise、NaN、aliasing……）。
6. **練習題**：連到 `exercises/README.md`。
7. **延伸閱讀**：書、論文、規格（只列真實存在、你確定的資料）。
8. **下一章**：一句話連到下一章。

## 4. 程式碼規範

### 4.1 DSP 元件（`dsp/include/dawlab/dsp/*.h`）

- header-only、`#pragma once`、`namespace dawlab::dsp`，只依賴標準函式庫與 `dawlab/` 共用工具（以及前面章節的 dsp header）。**不可依賴 JUCE**，這樣離線測試、plugin、DAW 都能共用同一份程式。
- 統一介面：

```cpp
class Xxx {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels); // 唯一可以 allocate 的地方
    void reset() noexcept;                                              // 清除內部狀態（filter memory、envelope…）
    void setSomething(float value) noexcept;                            // 參數 setter：可在 block 之間呼叫
    float processSample(int channel, float x) noexcept;                 // 逐 sample 的效果才提供
    void process(AudioBlock block) noexcept;                            // in-place 處理整個 block
};
```

- `process()` 內**不可**：allocate / free、拿 mutex、檔案或 console I/O、throw exception。
- 參數改變要避免 zipper noise / click：用 `SmoothedValue`（第 01 章）或係數插值。
- 遞迴結構（IIR filter、envelope）要處理 denormal：在 `reset()` 歸零，必要時加極小 offset 或把極小值 flush 成 0。
- 需要給 UI 讀的量測值（例如 gain reduction）用 `std::atomic<float>`（relaxed 即可）。
- 註解解釋「為什麼」，公式旁邊寫出對應的數學式。

### 4.2 範例程式（`examples/*.cpp`）

- `#include <dawlab/Common.h>` 與本章的 dsp header。
- **不帶參數就能執行並成功結束（return 0）**：用 `Signals.h` 產生輸入訊號。可選擇接受 `argv[1]` 作為輸入 WAV 路徑。
- 用 `dawlab::writeWav(dawlab::outputPath("lNN_xxx.wav"), buffer, sampleRate)` 輸出「處理前 / 處理後」的 WAV 供試聽，並用 printf 印出有意義的數字（peak、RMS、頻率響應表、gain reduction……）。
- 預設 sample rate 48000 Hz、block size 512（模擬 audio callback 一塊一塊處理），不要整段一次處理。
- 每支程式開頭有一段註解：示範什麼、怎麼執行、會輸出哪些檔案、該聽到什麼。

### 4.3 練習題

- 每章 5–8 題（第 08 章有 8 題，讓 Gate、Expander、De-esser、Transient shaper 各有實作題；例外：第 14 章為 7 題 engine 練習 + 6 個可選的 application project，標為 Exercises 1–7 與 optional Projects 8–13），混合：**觀念題**、**計算題**（手算 dB、係數、時間常數……）、**實作題**（至少 3 題）。標示難度 ★☆☆ / ★★☆ / ★★★。
- 實作題提供 `exercises/starter/exN_xxx.cpp`：可編譯、留 `// TODO`，執行時會因為 self-check 失敗而印出 FAIL（這是預期的）。
- `solutions/exN_xxx.cpp`：完整解答，用 `DAWLAB_CHECK` / `DAWLAB_CHECK_NEAR` **以數字驗證**結果（例如用 `measureGainDb` 驗證 cutoff 處是 -3 dB），最後 `return dawlab::test::summary("lNN_exN");`。
- `solutions/README.md`：每一題都有答案與解說（觀念題寫完整說明，計算題寫出算式，實作題講解關鍵思路並指向解答檔）。
- 解答檔可以自帶本題需要的 class（例如練習「自己寫一個 RMS detector」），不一定要放進 `dsp/`。

### 4.4 CMake

每章的 `CMakeLists.txt` 只用 `cmake/DawLab.cmake` 的函式，target 名稱以章節編號為前綴，避免衝突：

```cmake
dawlab_add_example(l02_biquad_response   examples/biquad_response.cpp)
dawlab_add_starter(l02_ex3_starter       exercises/starter/ex3_highpass.cpp)
dawlab_add_solution(l02_ex3_solution     solutions/ex3_highpass.cpp)
dawlab_add_test(l03_test_parametric_eq  tests/parametric_eq_test.cpp)   # 可選：元件回歸測試
```

### 4.5 編譯與測試（Windows）

```powershell
.\scripts\build.ps1                                  # 全部純 C++ 章節
.\scripts\build.ps1 -Only 02-filter                  # 只建第 02 章
.\scripts\build.ps1 -Only 13-vst-hosting -Juce       # JUCE 章節
.\scripts\build.ps1 -Only 13-vst-hosting,14-daw-8track -Juce   # 多個章節用逗號分隔
```

警告等級是 MSVC `/W4`；新程式碼不應該產生 warning。

### 4.6 編譯與測試（Linux / macOS）

```bash
./scripts/build.sh                                   # 全部純 C++ 章節
./scripts/build.sh --only 02-filter                  # 只建第 02 章
./scripts/build.sh --only 13-vst-hosting,14-daw-8track --juce   # JUCE 章節
```

GCC / Clang 使用 `-Wall -Wextra -Wpedantic`；新程式碼在這些編譯器上同樣不應該產生 warning（唯一預期會出現的，是起始碼檔案裡未使用的輔助函式，那是留給學習者使用的）。本教材已在 Ubuntu 24.04 + GCC 13.3 上驗證；修改內容時，請確認在 MSVC 與 GCC 上都仍能建置並通過測試。

## 5. DSP API 合約

後面的章節（尤其第 13 章 plugin 與第 14 章 DAW）會直接使用這些 class，**名稱與下列 method 必須存在且語意相同**。可以額外增加 method 或 class。

| 章 | Header | 必須提供 |
|---|---|---|
| 01 | `SmoothedValue.h` | `class SmoothedValue`：`reset(double sampleRate, double rampSeconds)`、`setCurrentAndTargetValue(float)`、`setTargetValue(float)`、`float getNextValue()`、`float getCurrentValue() const`、`float getTargetValue() const`、`bool isSmoothing() const`、`void skip(int numSamples)`（linear ramp） |
| 01 | `Gain.h` | `class Gain`：標準介面 + `setGainDb(float)`、`setGainLinear(float)`、`float getGainDb() const`、`setRampSeconds(double)`（預設約 0.05 s） |
| 01 | `Panner.h` | `enum class PanLaw { Linear, ConstantPower, Minus4_5dB }`；`class Panner`：標準介面 + `setPan(float)`（-1 = 左，0 = 中，+1 = 右）、`setPanLaw(PanLaw)`、`static void computeGains(float pan, PanLaw law, float& left, float& right)`；`process()` 處理立體聲 block（mono 輸入的情況要在文件說明；`process()` 對立體聲 block 套用的是 pan law，中間 -3 dB，已經是立體聲的 bus 通常要用 balance，見第 01 章） |
| 02 | `OnePole.h` | `class OnePole`：`enum class Type { Lowpass, Highpass }`、標準介面 + `setType(Type)`、`setCutoff(float hz)`、`processSample(int ch, float x)` |
| 02 | `Biquad.h` | `struct BiquadCoefficients { double b0, b1, b2, a1, a2; }`（a0 已正規化為 1），static factory（RBJ Audio EQ Cookbook）：`makeLowpass(sr, f, q)`、`makeHighpass(sr, f, q)`、`makeBandpass(sr, f, q)`（0 dB peak）、`makeNotch(sr, f, q)`、`makeAllpass(sr, f, q)`、`makePeak(sr, f, q, gainDb)`、`makeLowShelf(sr, f, q, gainDb)`、`makeHighShelf(sr, f, q, gainDb)`；成員 `std::complex<double> response(double f, double sr) const`、`double magnitudeDb(double f, double sr) const`。`class Biquad`：標準介面 + `setCoefficients(const BiquadCoefficients&)`、`getCoefficients()`、`processSample(int ch, float x)`（Transposed Direct Form II） |
| 03 | `ParametricEq.h` | `enum class EqBandType { LowCut, LowShelf, Peak, HighShelf, HighCut, Notch }`；`struct EqBand { EqBandType type; float frequency; float q; float gainDb; bool enabled; }`；`class ParametricEq`：`static constexpr int kNumBands = 6`、標準介面 + `setBand(int, const EqBand&)`、`const EqBand& getBand(int) const`、`double getMagnitudeDb(double freq) const`（整條 EQ 曲線，給 UI 畫圖）。參數變化不可有 click |
| 04 | `EnvelopeFollower.h` | `enum class DetectorMode { Peak, Rms }`、`enum class SmoothingMode { Branching, Decoupled }`（預設 Branching）；`class EnvelopeFollower`：`prepare(double sr)`、`reset()`、`setAttackMs`、`setReleaseMs`、`setMode`、`setSmoothingMode(SmoothingMode)`（第 08 章 Expander 使用 Decoupled）、`float processSample(float x)`（輸入原始 sample，回傳線性振幅 envelope） |
| 04 | `Compressor.h` | `class Compressor`：標準介面 + `setThresholdDb`、`setRatio`、`setKneeDb`、`setAttackMs`、`setReleaseMs`、`setMakeupGainDb`、`setStereoLink(bool)`、`float getGainReductionDb() const`（**正值**，例如 6 代表正在壓 6 dB；thread-safe）、`static float computeOutputDb(float inputDb, float thresholdDb, float ratio, float kneeDb)`（靜態 gain computer） |
| 05 | `DelayLine.h` | `class DelayLine`：`prepare(int maxDelaySamples, int numChannels)`、`reset()`、`void push(int ch, float x)`、`float read(int ch, int delaySamples) const` |
| 05 | `Limiter.h` | `class Limiter`：標準介面 + `setCeilingDb`、`setInputGainDb`、`setReleaseMs`、`setLookaheadMs`（上限在 prepare 決定；預設上限 20 ms，可在 prepare 前用 `setMaxLookaheadMs(float)` 改變）、`int getLatencySamples() const`、`float getGainReductionDb() const`（正值、thread-safe）。保證：任何輸入下，輸出 sample peak ≤ ceiling |
| 06 | `Oversampler.h` | `class Oversampler`：`explicit Oversampler(int factorLog2 = 1)`（倍率 = 2^factorLog2）、`prepare(double sr, int maxBlockSize, int numChannels)`、`reset()`、`int getFactor() const`、`AudioBlock upsample(const AudioBlock& input)`（回傳內部高取樣率 buffer 的 view）、`void downsample(AudioBlock output)`、`int getLatencySamples() const`（以原始取樣率計）；`static constexpr int kMaxFactorLog2 = 3`（最高 8×） |
| 06 | `Saturation.h` | `enum class SaturationCurve { Tanh, Cubic, Arctan, Asymmetric }`；`class Saturation`：標準介面 + `setCurve`、`setDriveDb`、`setMix(float 0..1)`、`setOutputGainDb`、`setOversamplingFactorLog2(int)`（在 prepare 之前呼叫）、`static float shape(SaturationCurve, float x)`、`int getLatencySamples() const` |
| 07 | `Distortion.h` | `enum class DistortionType { HardClip, Foldback, Fuzz, Rectify, Bitcrush }`；`class Distortion`：標準介面 + `setType`、`setDriveDb`、`setToneHz`（後級 lowpass）、`setMix`、`setOutputGainDb`、`setBitDepth(float)`、`setDownsampleFactor(int)`、`setOversamplingFactorLog2(int)`（在 prepare 之前呼叫，預設 2 = 4×）、`int getOversamplingFactor() const`（prepare 之後回報實際生效的倍率）、`int getLatencySamples() const` |
| 08 | `Gate.h` | `class Gate`：標準介面 + `setThresholdDb`、`setHysteresisDb`（關門門檻 = threshold − hysteresis）、`setRangeDb`（關門時的衰減，負值，例如 -80）、`setAttackMs`、`setHoldMs`、`setReleaseMs`、`process(AudioBlock)`、`process(AudioBlock main, const AudioBlock& sidechain)`（以 sidechain 偵測）、`float getGainReductionDb() const`（正值、thread-safe）、`bool isOpen() const` |
| 08 | `Expander.h` | `class Expander`（downward expander）：標準介面 + `setThresholdDb`、`setRatio`（2 代表低於 threshold 的每 1 dB 變成 2 dB）、`setKneeDb`、`setRangeDb`、`setAttackMs`、`setReleaseMs`、`float getGainReductionDb() const`、`static float computeOutputDb(float inputDb, float thresholdDb, float ratio, float kneeDb)` |
| 08 | `DeEsser.h` | `class DeEsser`：標準介面 + `setFrequency(float hz)`、`setThresholdDb`、`setRangeDb`（最大衰減，正值）、`setSplitBand(bool)`（true = 只壓高頻段）、`setListen(bool)`（監聽偵測訊號）、`float getGainReductionDb() const` |
| 08 | `TransientShaper.h` | `class TransientShaper`：標準介面 + `setAttackDb(float)`（-24..+24，加強或減弱 transient）、`setSustainDb(float)` |
| 09 | `FractionalDelayLine.h` | `class FractionalDelayLine`：`prepare(int maxDelaySamples, int numChannels)`、`reset()`、`void push(int ch, float x)`、`float readLinear(int ch, float delaySamples) const`、`float readCubic(int ch, float delaySamples) const`（3 階 Lagrange 或 Hermite）、`int getMaxDelaySamples() const`。讀取慣例：`push()` 之後 `read*(ch, 0)` 是剛寫入的 sample，`read*(ch, D)` 是 D 次 push 以前的 sample；`readCubic` 的延遲小於 1 時 clamp 到 1 |
| 09 | `Delay.h` | `class Delay`（stereo echo）：`setMaxDelayMs(float)`（在 prepare 前呼叫，預設 2000）、標準介面 + `setDelayMs(float)`（平滑變化）、`setFeedback(float 0..0.99)`、`setMix(float 0..1)`、`setPingPong(bool)`、`setLowpassHz(float)`、`setHighpassHz(float)`（feedback 迴路內的濾波）、`static float noteToMs(double bpm, double beats)`（beats = 0.75 代表附點八分音符） |
| 10 | `Lfo.h` | `enum class LfoShape { Sine, Triangle, Saw, Square, SampleAndHold }`；`class Lfo`：`prepare(double sampleRate)`、`reset()`、`setShape(LfoShape)`、`setRateHz(float)`、`setPhase(float 0..1)`、`float getNextValue()`（輸出 -1..1） |
| 10 | `Chorus.h` | `class Chorus`：標準介面 + `setRateHz`、`setDepthMs`、`setCentreDelayMs`、`setVoices(int)`、`setMix`、`setStereoSpread(float 0..1)` |
| 10 | `Flanger.h` | `class Flanger`：標準介面 + `setRateHz`、`setDepthMs`、`setDelayMs`（基礎延遲）、`setFeedback(float -0.99..0.99)`、`setMix` |
| 10 | `Phaser.h` | `class Phaser`：標準介面 + `setRateHz`、`setDepth(float 0..1)`、`setCentreHz`、`setFeedback`、`setStages(int)`（2..12 的偶數）、`setMix` |
| 10 | `Tremolo.h` | `class Tremolo`：標準介面 + `setRateHz`、`setDepth(float 0..1)`、`setShape(LfoShape)`、`setAutoPan(bool)`（true 時左右反相 = autopan） |
| 11 | `Fft.h` | `enum class WindowType { Rectangular, Hann, Hamming, BlackmanHarris }`；`class Fft`：`explicit Fft(int order)`（size = 2^order，建構時預先算好 twiddle）、`int getSize() const`、`void forward(std::complex<float>* data) const`、`void inverse(std::complex<float>* data) const`（in-place，inverse 含 1/N）、`void forwardReal(const float* input, std::complex<float>* output) const`（輸出 N/2+1 個 bin）、`void inverseReal(const std::complex<float>* input, float* output) const`、`static void makeWindow(std::vector<float>& window, int size, WindowType type)`。注意：`forwardReal` / `inverseReal` 使用內部 scratch buffer，同一個 `Fft` 物件不可同時被兩個 thread 使用 |
| 11 | `LevelMeter.h` | `class LevelMeter`：`prepare(double sampleRate, int numChannels)`、`reset()`、`setPeakHoldMs`、`setDecayDbPerSecond`、`setRmsWindowMs`、`void process(const AudioBlock&)`（audio thread）、`float getPeakDb(int ch) const`、`float getRmsDb(int ch) const`、`float getPeakHoldDb(int ch) const`（thread-safe）、`void resetPeakHold()` |
| 11 | `LoudnessMeter.h` | `class LoudnessMeter`（ITU-R BS.1770 / EBU R128，K-weighting）：`prepare(double sampleRate, int numChannels)`、`reset()`、`void process(const AudioBlock&)`（不可 allocate）、`float getMomentaryLufs() const`（400 ms）、`float getShortTermLufs() const`（3 s）、`float getIntegratedLufs() const`（含 -70 LUFS absolute 與 -10 LU relative gating）；getter 皆 thread-safe |
| 11 | `SpectrumAnalyzer.h` | `class SpectrumAnalyzer`：`prepare(double sampleRate, int fftOrder = 11)`、`void pushSamples(const float* x, int n)`（audio thread，lock-free）、`bool computeSpectrum(std::vector<float>& magnitudesDb)`（UI thread；回傳 N/2+1 個 bin 的 dB，含 window 與平滑；沒有新資料時回傳 false）、`double getBinFrequency(int bin) const` |
| 12 | `Reverb.h` | `class Reverb`（algorithmic，FDN 或 Freeverb 架構擇一並說明）：標準介面 + `setDecaySeconds`（RT60）、`setPreDelayMs`、`setDampingHz`、`setSize(float 0..1)`、`setWidth(float 0..1)`、`setMix(float 0..1)`、`setFreeze(bool)`。教學用 building blocks（`CombFilter`、`AllpassDiffuser` 等）可放在 `ReverbBuildingBlocks.h` |
| 12 | `ConvolutionReverb.h` | `class ConvolutionReverb`（uniform partitioned FFT convolution，使用第 11 章 `Fft`）：`prepare(double sampleRate, int maxBlockSize, int numChannels)`、`reset()`、`void setImpulseResponse(const AudioBuffer& ir)`（非 audio thread，可 allocate）、`setMix(float 0..1)`、`process(AudioBlock)`、`int getLatencySamples() const`、`static AudioBuffer makeSyntheticImpulseResponse(double sampleRate, double rt60Seconds, int numChannels, uint32_t seed = 1)` |

第 13 章（JUCE）會提供：一個用 `dawlab::dsp::Compressor` 包成的 VST3 plugin（`DawLabCompressor`），以及可重用的 plugin hosting 程式碼（給第 14 章的 DAW 用來在 track 上插 VST3）。第 14 章把 engine（純 C++、可離線測試）和 JUCE app（audio device、GUI、VST3 insert）分開，並提供兩條路線從零做出 8 軌 DAW：build-along 路線讓讀者逐步自己寫——Steps 1–10 在 `steps/stepNN-*/` 各有一個只含到該步為止功能、可獨立編譯的 engine 快照（`minidaw.h`；`starter/` 留 TODO、執行會 FAIL，`solution/` 以 `DAWLAB_CHECK` 只驗證該步功能，ctest 名稱為 `l14_stepNN_solution`，後一步以前一步的 solution 為基礎），Steps 11–21 則在每份 step 文件的「Your turn」一節指定要在 `app/` 中刪掉重寫的函式，以及重寫後必須通過的檢查（`l14_app_smoke_test` 的對應段落、`--snapshot` 圖片或練習解答）；reference 路線則直接閱讀並執行 `engine/`、`app/` 的完成品。
