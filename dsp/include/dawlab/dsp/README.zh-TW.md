# dawlab::dsp
[English](README.md) | **繁體中文**

各章節逐步實作的 DSP 元件：header-only、`namespace dawlab::dsp`、只依賴標準函式庫與 `common/` 的共用工具（**不依賴 JUCE**），所以離線範例、第 13 章的 VST3 plugin 與第 14 章的 DAW engine 都共用同一份程式。

- 統一介面（`prepare` / `reset` / setter / `processSample` / `process`）、real-time 規則與各 class 的 API 合約見 [docs/authoring-guide.zh-TW.md](../../../../docs/authoring-guide.zh-TW.md) 第 4、5 節。
- 在 CMake 裡連結 `dawlab::dsp` 即可使用（它會一併帶入 `dawlab::common`）；程式裡 `#include <dawlab/dsp/Xxx.h>`。
- 每個 header 的原理、推導與逐段解說都在對應章節的 README。

## 依章節列出

<div align="center">

| 章 | Header | 用途 |
|---|---|---|
| [01](../../../../lessons/01-gain/README.zh-TW.md) | `SmoothedValue.h` | 參數的 linear ramp 平滑，避免改參數時的 click 與 zipper noise |
| 01 | `Gain.h` | 以 dB 或倍率設定、帶平滑的 gain 元件 |
| 01 | `Panner.h` | 三種 pan law（linear / constant power / −4.5 dB）的 mono pan 與立體聲 balance |
| [02](../../../../lessons/02-filter/README.zh-TW.md) | `OnePole.h` | 一階 lowpass / highpass（DC blocker、平滑、tone 控制的基礎） |
| 02 | `Biquad.h` | RBJ Audio EQ Cookbook 係數與 Transposed Direct Form II 的二階 IIR filter |
| 02 | `StateVariableFilter.h` | TPT（zero-delay feedback）state variable filter，cutoff 快速調變也穩定 |
| [03](../../../../lessons/03-eq/README.zh-TW.md) | `ParametricEq.h` | 6-band parametric EQ（low cut、shelf、peak、high cut、notch），參數平滑且可算整條曲線給 UI |
| [04](../../../../lessons/04-compressor/README.zh-TW.md) | `EnvelopeFollower.h` | Peak / RMS envelope 偵測器與 attack / release 平滑器 |
| 04 | `Compressor.h` | feedforward、log-domain 的 compressor（threshold、ratio、knee、makeup、stereo link） |
| [05](../../../../lessons/05-limiter/README.zh-TW.md) | `DelayLine.h` | 整數 sample 的 circular buffer 延遲線 |
| 05 | `Limiter.h` | lookahead brickwall limiter，保證輸出 sample peak 不超過 ceiling |
| [06](../../../../lessons/06-saturation/README.zh-TW.md) | `Oversampler.h` | 2× / 4× / 8× polyphase halfband FIR oversampling（降低非線性處理的 aliasing） |
| 06 | `Saturation.h` | tanh / cubic / arctan / asymmetric 的 waveshaper 飽和效果，內建 oversampling |
| [07](../../../../lessons/07-distortion/README.zh-TW.md) | `Distortion.h` | hard clip、foldback、fuzz、rectify、bitcrush 五種 distortion，含 tone filter 與 mix |
| 07 | `AmpChain.h` | 簡易吉他 amp 模擬鏈（HPF → pre-EQ → preamp saturation → tone stack → power amp distortion → cabinet） |
| [08](../../../../lessons/08-gate-expander/README.zh-TW.md) | `Gate.h` | 有 hysteresis、hold、range 與 sidechain 的 noise gate |
| 08 | `Expander.h` | downward expander（低於 threshold 的訊號依 ratio 再壓低） |
| 08 | `DeEsser.h` | 偵測齒音頻段並壓低（wideband 或 split-band，可監聽偵測訊號） |
| 08 | `TransientShaper.h` | 以快 / 慢兩條 envelope 的差分別加強或減弱 attack 與 sustain |
| [09](../../../../lessons/09-delay/README.zh-TW.md) | `FractionalDelayLine.h` | 支援非整數延遲的 circular buffer（linear / cubic Hermite / Lagrange 插值） |
| 09 | `Delay.h` | 立體聲 echo：平滑 delay time、feedback 迴路內的濾波、ping-pong、tempo sync |
| [10](../../../../lessons/10-modulation/README.zh-TW.md) | `Lfo.h` | 低頻振盪器（sine、triangle、saw、square、sample & hold） |
| 10 | `Chorus.h` | 多 voice、LFO 調變的短延遲，加上立體聲展開 |
| 10 | `Flanger.h` | 很短的調變延遲加 feedback，形成會掃動的 comb filter |
| 10 | `Phaser.h` | 串接一階 allpass、以 LFO 掃動 notch 的 phaser |
| 10 | `Tremolo.h` | LFO 振幅調變，可切換成 autopan |
| [11](../../../../lessons/11-metering-fft/README.zh-TW.md) | `Fft.h` | radix-2 FFT / IFFT、real FFT 與 window function |
| 11 | `LevelMeter.h` | sample peak（含 peak hold 與 decay）與滑動視窗 RMS meter，getter thread-safe |
| 11 | `LoudnessMeter.h` | ITU-R BS.1770 / EBU R128 loudness（momentary、short-term、integrated LUFS） |
| 11 | `SpectrumAnalyzer.h` | audio thread 以 lock-free FIFO 推 sample、UI thread 算出平滑後的頻譜 |
| [12](../../../../lessons/12-reverb/README.zh-TW.md) | `ReverbBuildingBlocks.h` | 教學用的殘響積木：`CombFilter`、`AllpassDiffuser`、`TappedDelay`、RT60 量測 `measureRt60`、`nextPrime` |
| 12 | `Reverb.h` | 8 線 FDN algorithmic reverb（decay、pre-delay、damping、size、width、freeze） |
| 12 | `ConvolutionReverb.h` | uniform partitioned FFT convolution reverb，可產生合成 impulse response |

</div>

第 13 章（JUCE plugin 與 hosting）與第 14 章（8 軌 DAW）沒有新增這個資料夾的 header：第 13 章的程式在 `lessons/13-vst-hosting/plugin/` 與 `host/`，第 14 章的 engine 在 `lessons/14-daw-8track/engine/include/dawlab/engine/`。
