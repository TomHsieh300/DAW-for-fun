# 第 00 章：數位音訊基礎與開發環境

[English](README.md) | **繁體中文**

> [教材首頁](../../README.zh-TW.md)　｜　下一章：[01 Gain 與 Pan](../01-gain/README.zh-TW.md)

這是整套教材的起點。讀完這一章，你會知道「聲音在電腦裡長什麼樣子」、會算 dB、知道 audio callback 是什麼，而且能編譯、執行、試聽本教材的每一支程式。後面 14 章的所有 DSP 元件都建立在這一章的觀念上。

## 整套教材的路線圖

章節依編號順序前進，一條直線走到底：

```
00 → 01 → 02 → 03 → 04 → 05 → 06 → 07
  → 08 → 09 → 10 → 11 → 12 → 13 → 14
```

| 章 | 主題 | 你會做出什麼 |
|---|---|---|
| 00 | 數位音訊基礎、開發環境 | 看懂 sample、dB、block、latency；能建置與試聽 |
| 01 | gain、pan、參數平滑 | `Gain`、`Panner`、`SmoothedValue` |
| 02 | filter（one-pole、biquad） | `OnePole`、`Biquad`（RBJ cookbook） |
| 03 | parametric EQ | 6 段 `ParametricEq` |
| 04 | compressor | `EnvelopeFollower`、`Compressor` |
| 05 | limiter 與 lookahead | `DelayLine`、`Limiter` |
| 06 | saturation 與 oversampling | `Oversampler`、`Saturation` |
| 07 | distortion | `Distortion`（hard clip、foldback、bitcrush…） |
| 08 | gate / expander / de-esser / transient shaper | 四種動態處理器 |
| 09 | delay | `FractionalDelayLine`、`Delay`（ping-pong、tempo sync） |
| 10 | modulation | `Lfo`、`Chorus`、`Flanger`、`Phaser`、`Tremolo` |
| 11 | metering 與 FFT | `Fft`、`LevelMeter`、`LoudnessMeter`（LUFS）、`SpectrumAnalyzer` |
| 12 | reverb | algorithmic `Reverb` 與 `ConvolutionReverb` |
| 13 | VST3 plugin 與 hosting（JUCE） | 把 compressor 包成 VST3，並寫一個能載入 VST3 的 host |
| 14 | 自製 8 軌 DAW | 把以上全部組成一個能編排 clip、混音、插 VST3 plugin、匯出 WAV 的 DAW（錄音是延伸練習） |

---

## 0. 開發環境

### 0.1 Windows：Visual Studio 2022

1. 下載 [Visual Studio 2022](https://visualstudio.microsoft.com/)：**Build Tools**（純命令列，體積小）或 **Community**（含 IDE，免費）都可以。
2. 安裝時勾選工作負載 **「使用 C++ 的桌面開發」（Desktop development with C++）**。它會一併裝好 MSVC 編譯器、Windows SDK 與 **CMake**（本教材的腳本會自動找到 VS 內附的 CMake，不需要另外裝）。
3. 打開 PowerShell，切到專案根目錄，執行：

```powershell
.\scripts\build.ps1 -Only 00-basics        # 設定 + 建置 + 測試第 00 章
.\scripts\build.ps1                        # 全部純 C++ 章節
.\scripts\build.ps1 -Only 00-basics -NoTest             # 只建置
.\scripts\build.ps1 -Only 00-basics -TestLabel example  # 只跑範例（或 solution）
.\scripts\build.ps1 -Only 00-basics -Config Release     # Release 建置
.\scripts\build.ps1 -Only 14-daw-8track -Juce          # JUCE 章節（13、14）要加 -Juce
```

`-Only` 後面接的是 `lessons/` 底下的資料夾名稱（例如 `00-basics`、`14-daw-8track`）。名稱打錯（`lessons/` 底下沒有那個資料夾）時，那個名稱會被默默略過，只會建出共用工具的 `common_selftest`；建完如果發現沒有那一章的執行檔，先檢查拼字。要一次建多章，用逗號分隔：`-Only 13-vst-hosting,14-daw-8track`。

如果 PowerShell 說「無法載入指令碼」，用 `powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Only 00-basics`。

腳本做的事等同於下面三行（VS 內附的 cmake 在 `C:\Program Files (x86)\Microsoft Visual Studio\2022\<版本>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\`）：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DDAWLAB_ONLY=00-basics
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

建好的執行檔在 `build\bin\Debug\`（Release 在 `build\bin\Release\`）。**手動執行範例時請先 `cd build`**，因為程式把 WAV 寫在「目前工作目錄」底下的 `dawlab_output\`：

```powershell
cd build
.\bin\Debug\l00_test_signals.exe
.\bin\Debug\l00_wav_info.exe dawlab_output\l00_drum_loop.wav
```

### 0.2 其他方式：VS Code、Visual Studio IDE

- **VS Code + CMake Tools 擴充套件**：開啟專案資料夾 → 下方狀態列選 kit「Visual Studio Build Tools 2022 Release - amd64」（或 Community）→ 按「Build」。要只建一章，在 `.vscode/settings.json` 加 `"cmake.configureSettings": { "DAWLAB_ONLY": "00-basics" }`。狀態列的「Run CTest」會跑測試；要手動執行某支程式，記得終端機的工作目錄要在 build 資料夾。
- **Visual Studio 2022 IDE**：「開啟本機資料夾」選專案根目錄，VS 會自動偵測 `CMakeLists.txt`。在「專案 → CMake 設定」裡加 CMake 變數 `DAWLAB_ONLY=00-basics` 可以只建一章。目標執行檔從工具列的「選取啟動項目」挑，例如 `l00_db_math.exe`。

### 0.3 Linux / macOS

本教材已在 **Ubuntu 24.04**（GCC 13.3、CMake 3.28）上驗證；macOS 尚未測試。安裝 CMake ≥ 3.22 與 C++17 編譯器（Ubuntu：`sudo apt install build-essential cmake git`；macOS：`xcode-select --install` 加 `brew install cmake`），然後使用 `scripts/build.sh`，它是 `build.ps1` 的對應腳本：

```bash
./scripts/build.sh --only 00-basics                         # configure + build + test 第 00 章
./scripts/build.sh --only 00-basics --test-label example    # 只跑範例（或 "solution"）
./scripts/build.sh --only 14-daw-8track --juce              # JUCE 章節（13、14）需要 --juce
```

它做的事等同於：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DDAWLAB_ONLY=00-basics
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

執行檔在 `build/bin/`（單一組態的 generator 沒有 `Debug/` 子資料夾，也沒有 `.exe`）。和 Windows 一樣，手動執行範例前先 `cd build`：

```bash
cd build
./bin/l00_test_signals
./bin/l00_wav_info dawlab_output/l00_drum_loop.wav
```

在本教材的其他部分，請把 `.\scripts\build.ps1 -Only X` 讀成 `./scripts/build.sh --only X`，`-Juce` 讀成 `--juce`，`.\bin\Debug\name.exe` 讀成 `./bin/name`。

Linux 上只有 JUCE 章節（13、14）需要額外套件（ALSA、X11、freetype 等；完整的 `apt install` 指令在最上層 README 的「環境安裝」）；本章不需要。

### 0.4 輸出的 WAV 在哪裡、怎麼試聽

所有程式都用 `dawlab::outputPath("檔名")` 寫到 `<build 目錄>/dawlab_output/`。用 ctest 跑時工作目錄就是 build 目錄，所以也在同一個地方。

強烈建議安裝免費的 [Audacity](https://www.audacityteam.org/)：

- **波形**：直接拖 WAV 進去。用滑鼠滾輪 + Ctrl 放大到看得見一顆一顆的 sample——那就是 $x[n]$。
- **頻譜圖**：音軌名稱旁的下拉選單 → 「Spectrogram」。正弦波是一條水平線、掃頻是往上彎的曲線、白雜訊是整片均勻的雜訊、脈衝是一條垂直線。
- **單一時間點的頻譜**：選取一段 → 「分析 → 繪製頻譜」。看 `l00_clipped_sine.wav` 可以清楚看到 1、3、5 kHz 的奇次諧波。
- **檢查 clip**：「View → Show Clipping」（中文介面的選單名稱依版本而異）會把 clip 的 sample 標成紅色。
- 聽 `l00_aliasing_sweep.wav` 時請把音量調小：接近 Nyquist 的高頻很刺耳。

---

## 1. 本章目標

讀完這一章，你應該能：

- 說明聲音如何變成一串數字：sampling、sample rate、bit depth，以及為什麼 DSP 程式裡一律用 `float` 且範圍是 $[-1, 1]$。
- 用 Nyquist 定理算出「某個頻率在某個 sample rate 下會 alias 到幾 Hz」，並解釋 aliasing 聽起來像什麼。
- 熟練 dB 的數學：$20\log_{10}$、+6 dB ≈ 兩倍振幅、相同訊號相加 +6 dB vs 不相關訊號相加 +3 dB、quantization 的 SNR ≈ 6.02N + 1.76 dB。
- 分辨 peak、RMS、crest factor，知道 clipping 在數字上長什麼樣子。
- 解釋 channel、interleaved / non-interleaved、buffer、block processing、audio callback、latency = blockSize / sampleRate。
- 說出 real-time audio thread 的四大禁忌（allocation、lock、I/O、無界迴圈 / GC）以及**為什麼**。
- 在 Windows（或 macOS / Linux）上建置整套教材、跑測試、找到輸出的 WAV 並用 Audacity 看波形與頻譜。
- 看懂 `common/include/dawlab/` 裡的共用工具：`AudioBuffer` / `AudioBlock`、`WavFile`、`Signals`、`Analysis`、`Check`。

---

## 2. 原理

### 2.1 聲波與取樣（sampling）

聲音是空氣壓力隨時間的微小變化。麥克風把壓力變成電壓，電壓是一條**連續**的曲線 $x(t)$：任何時刻都有值，而且值可以是任意實數。電腦存不了連續的東西，所以 ADC（analog-to-digital converter）做兩件事：

1. **取樣（sampling）**：每隔固定的時間 $T$ 量一次電壓。$T$ 叫 sampling period，它的倒數 $f_s = 1/T$ 叫 **sample rate**（取樣率），單位 Hz。48000 Hz 代表每秒量 48000 次，每個 sample 相隔 $1/48000 \approx 20.8\ \mu s$。
2. **量化（quantization）**：把量到的電壓四捨五入到有限個刻度之一（見 2.3）。

結果就是一串數字 $x[n] = x(nT)$，$n = 0, 1, 2, \ldots$。這一串數字就是「聲音在電腦裡的樣子」——沒有別的了。一秒鐘的立體聲 48 kHz 音訊就是 2 × 48000 = 96000 個數字。

```
連續的聲波 x(t)                      取樣後的 x[n]（每格一個 sample）
      ╭──╮                                  •
     ╱    ╲                               •   •
    ╱      ╲        ╭──                 •       •         •
───╱────────╲──────╱───── t     ───•─────────────•─────•───•──── n
  ╱          ╲    ╱                                •   •
 ╱            ╲──╯                                   •
                                 |←T→|   T = 1 / fs
```

一個很常見的誤解：「sample 之間的聲音不見了，所以數位音訊是階梯狀的、有損的」。只要訊號裡沒有超過 $f_s/2$ 的成分，這些點**唯一地**決定了原本那條平滑曲線（這就是下一節的 Nyquist 定理）；DAC 輸出時會用一個 reconstruction filter 把平滑曲線還原回來，不是輸出階梯。

「超過 $f_s/2$ 的成分」是什麼意思？要回答這個問題，得先講清楚頻率、正弦波與頻譜。

#### 頻率、正弦波與頻譜

**頻率（frequency）** 是「每秒振動幾次」，單位 Hz。它對應到我們聽到的**音高**：鋼琴中央 C 上方的 A（A4）是 440 Hz，高八度的 A5 是 880 Hz（頻率加倍 = 高八度）。人耳大約聽得到 20 Hz～20 kHz，年紀越大上限越低。

**正弦波（sine wave）** 是最「純」的聲音，只有單一頻率，聽起來像音叉或測試音的「嗶——」：

$$
x(t) = A \sin(2\pi f t)
$$

$A$ 是振幅（決定多大聲），$f$ 是頻率（決定音高）。取樣後就是 $x[n] = A\sin(2\pi f n / f_s)$——`Signals.h` 的 `makeSine()` 做的就是這件事。

**任何聲音都可以拆成很多正弦波疊加**。這是 Fourier（傅立葉）的核心觀念：一把吉他、一句人聲、一個鼓聲，都等於「很多個不同頻率、不同振幅的正弦波加在一起」。把「每個頻率的正弦波有多大」畫成圖，就叫**頻譜（spectrum）**：橫軸是頻率，縱軸是振幅。

```
振幅                                    振幅
 │  █                                    │  █
 │  █                                    │  █   █
 │  █                                    │  █   █   █
 │  █                                    │  █   █   █   █   ▄   ▂
 └──┴──────────────────── 頻率           └──┴───┴───┴───┴───┴───┴─── 頻率
   440                                     110 220 330 440 550 660
 正弦波：只有一根柱子                    撥弦（基頻 110 Hz）：一根基頻 + 一排諧波
```

真實樂器的頻譜通常像右圖：最低的那根叫**基頻（fundamental）**，決定我們聽到的音高；上面一排頻率剛好是基頻**整數倍**（2、3、4…倍）的成分叫**諧波（harmonics）**，第 $k$ 次諧波的頻率是 $k \cdot f_0$。基頻本身算第 1 次諧波（H1），2 倍的是 H2，以此類推；頻率是基頻奇數倍的叫**奇次諧波**（H3、H5…），偶數倍的叫**偶次諧波**（H2、H4…）。

**諧波的組成決定音色（timbre）**：同樣是 110 Hz 的 A，鋼琴、吉他、小提琴聽起來不一樣，就是因為各次諧波的大小不同。之後談 clipping、saturation、distortion 時會一直說「產生了新的諧波」，意思就是：處理之後的頻譜多出了原本沒有的柱子，所以音色變了。

親耳對照：`l00_test_signals` 輸出的 `l00_sine_440.wav` 是單一正弦波，聽起來乾淨但單調、像測試音；`l00_pluck_110.wav` 是 110 Hz 的撥弦，音高比較低，但因為有一整排諧波，聽起來「像樂器」、明亮而有質感，而且高次諧波隨時間衰減得比較快，所以聲音越來越悶。用 Audacity 的「分析 → 繪製頻譜」（見 0.4）看這兩個檔案，就是上面兩張圖。

### 2.2 sample rate、Nyquist 定理與 aliasing

**Nyquist–Shannon 取樣定理**：如果訊號最高頻率是 $f_{max}$，那麼只要 $f_s > 2 f_{max}$，就可以從 sample 完美重建原訊號。反過來說，在 sample rate $f_s$ 之下，能正確表示的最高頻率是

$$
f_{Nyquist} = \frac{f_s}{2}
$$

人耳聽覺上限約 20 kHz，所以 CD 用 44.1 kHz（Nyquist 22.05 kHz），專業音訊常用 48 kHz（Nyquist 24 kHz）、96 kHz、192 kHz。

**那超過 Nyquist 的頻率會怎樣？** 它不會消失，而是「偽裝」成一個較低的頻率——這叫 **aliasing**（混疊）。直覺：想像你每秒拍 48 張照片去記錄一個每秒轉 30 圈的風扇。每張照片之間風扇轉了 30/48 = 0.625 圈；但是「轉了 0.625 圈」和「倒轉了 0.375 圈」拍出來的照片一模一樣。所以你會看到風扇以每秒 18 圈倒轉。

把它寫成公式：一個頻率 $f_{in}$ 的正弦波取樣後，和下列所有頻率產生**完全相同**的 sample：

$$
f_{alias} = \left| f_{in} - k \cdot f_s \right|,\quad k = 0, \pm 1, \pm 2, \ldots
$$

其中落在 $[0, f_s/2]$ 內的那一個，就是你會「聽到」的頻率。

**計算：30 kHz 在 48 kHz 取樣下會變成幾 Hz？**

$$
f_{alias} = |30000 - 1 \times 48000| = 18000\ \text{Hz}
$$

30 kHz 超過 Nyquist（24 kHz）6 kHz，就「折返」到 Nyquist 以下 6 kHz 的位置，也就是 18 kHz。可以用三角函數驗證（$n$ 是整數）：

$$
\sin\!\left(2\pi \tfrac{30000}{48000} n\right)
= \sin\!\left(2\pi n - 2\pi \tfrac{18000}{48000} n\right)
= -\sin\!\left(2\pi \tfrac{18000}{48000} n\right)
$$

兩組 sample 除了正負號完全相同——電腦根本無法分辨它們。範例 `l00_db_math` 用 Goertzel（一個只量「單一頻率振幅」的演算法，見 3.4）量測一個 30 kHz 正弦波，在 18 kHz 量到的振幅就是完整的 0.5：

```
== Aliasing: f_in = 30 kHz, fs = 48 kHz ==
    at 18 kHz (|30000 - 48000|) the amplitude is 0.5000  <- all the energy has folded here
```

```
振幅
 │                       Nyquist = 24 kHz
 │                            │
 │                            │   30 kHz（原本）
 │   18 kHz（alias）          │        ╎
 │        █                   │        ╎
 │        █                   │        ╎
 │        █   ◄───── 折返 ────┼────────╎
 └────────┴───────────────────┴────────┴──── 頻率
          18k                24k      30k
```

**為什麼要在乎？** 兩個地方會產生超過 Nyquist 的成分：

1. **ADC 之前**：真實世界的聲音有超音波成分，所以 ADC 前面一定有 anti-aliasing lowpass filter 把它們濾掉。這是硬體的事，我們不用管。
2. **我們自己的 DSP**：任何**非線性**處理（saturation、distortion、clipping，甚至 compressor 的快速 gain 變化）都會產生新的諧波。1.1 kHz 的訊號經過 hard clip 會產生 3.3、5.5、7.7、… kHz 的奇次諧波，一路到無限高；超過 24 kHz 的部分全部 alias 回來，例如第 23 次諧波 25.3 kHz 會折到 $|25300 - 48000| = 22.7$ kHz、第 25 次 27.5 kHz 折到 20.5 kHz——這些位置都**不是** 1.1 kHz 的整數倍，變成跟原本音高無關的刺耳雜音。（例外：如果 $f_s$ 剛好是基頻的整數倍，例如 1 kHz @ 48 kHz，alias 會剛好落回諧波的格點上，例如 25 kHz 折到 23 kHz = H23。這是巧合，一般的音高不會這樣。）這就是第 06 章要用 oversampling 的原因：先把 sample rate 升到 4 倍（192 kHz，Nyquist 96 kHz），做完非線性處理，濾掉 24 kHz 以上的成分，再降回來。

聽感上，aliasing 是「金屬感」、「數位感」、「音高往反方向跑」。`l00_db_math` 輸出的 `l00_aliasing_sweep.wav` 是一段從 20 Hz 掃到 40 kHz 的 sweep（刻意超過 Nyquist）：音高一路往上，碰到 24 kHz 之後反而往下掉，就是這個現象。

### 2.3 bit depth 與 quantization

每個 sample 要用多少 bit 儲存，叫 **bit depth**。N bit 的整數 PCM 有 $2^N$ 個刻度：16 bit 有 65536 個（$-32768 \ldots 32767$），24 bit 有 16777216 個。注意正負不對稱：負端可以到 $-2^{N-1}$，正端最多只到 $2^{N-1}-1$。換成 $[-1, 1]$ 的 float 時除以 $2^{N-1}$，所以 16 bit 能存的最大正值是 $32767/32768 \approx 0.99997$，略小於 1.0；`l00_db_math` 的 `quantize()` 在四捨五入後會 clamp 到這個合法範圍。

量化就是把真實的值四捨五入到最近的刻度，誤差 $e[n]$ 介於 $\pm \Delta/2$，$\Delta$ 是刻度間距。對大多數訊號，這個誤差像是均勻分布的隨機雜訊（quantization noise），功率是

$$
P_{noise} = \frac{\Delta^2}{12}
$$

（均勻分布在 $[-\Delta/2, \Delta/2]$ 的變異數。）把全幅（full-scale）正弦波的功率 $P_{signal} = A^2/2$ 除以它，就得到最好情況的 **SNR（signal-to-noise ratio）**。若刻度範圍是 $[-1, 1]$，則 $\Delta = 2 / 2^N = 2^{1-N}$，$A = 1$：

$$
\text{SNR} = 10\log_{10}\frac{1/2}{2^{2-2N}/12}
= 10\log_{10}\left(1.5 \cdot 2^{2N}\right)
= 20N\log_{10}2 + 10\log_{10}1.5
\approx 6.02N + 1.76\ \text{dB}
$$

**直覺：每多 1 bit，雜訊就低 6 dB。** 16 bit 約 98 dB、24 bit 約 146 dB。`l00_db_math` 實際把正弦波量化後量 SNR，跟公式幾乎一樣：

```
  bits     measured      formula
     8     49.12 dB     49.92 dB
    12     73.80 dB     74.00 dB
    16     98.06 dB     98.08 dB
    20    122.16 dB    122.16 dB
    24    146.24 dB    146.24 dB
```

8 bit 量到的值比公式低約 0.8 dB：0 dBFS 正弦波的正半週頂端超過了最大合法碼 $127/128$，被 clamp 住多了一點誤差；bit 數越多，這一階佔的比例越小，差異就消失了。

注意 SNR 是以**全幅訊號**算的。訊號小 20 dB，SNR 也少 20 dB（同一個範例：8 bit 在 -20 dBFS 只剩 29.78 dB）。這就是為什麼錄音時要有足夠的位準。聽 `l00_quantized_8bit.wav`，正弦波底下有明顯的嘶嘶聲與失真；`l00_quantized_16bit.wav` 則幾乎聽不出來。16 bit 的底噪（最小刻度約 -90 dBFS）只有在極安靜的段落、而且把監聽音量開得很大時才可能察覺；這也是錄音和混音內部常用 24 bit / float 的原因。（把高解析度的音訊轉成 16 bit 時，實務上會先加一點極小的隨機雜訊再量化，叫 **dither**，讓量化誤差變成均勻的嘶聲，而不是跟訊號相關的失真；第 07 章 bitcrusher 會再提到。）

### 2.4 為什麼 DSP 用 float，以及 0 dBFS

雖然檔案與硬體用整數，但 DSP 內部一律用 **32-bit float**，範圍約定為 $[-1.0, 1.0]$：

- **不用擔心溢位**：16 bit 整數相加兩次就可能 overflow（繞到負數，爆音），float 可以放心超過 1.0，最後再處理。
- **動態範圍極大**：float 有 24 bit 的 mantissa，相對精度不隨音量改變。-60 dB 的小訊號還是有 24 bit 的精度；整數在 -60 dB 時只剩 6 bit（16 bit 的情況）。
- **乘法自然**：gain 就是乘一個數，不用做定點數的 shift。

$[-1, 1]$ 這個範圍對應整數 PCM 的「滿刻度」（full scale）。振幅剛好 1.0 的訊號叫 **0 dBFS**（dB relative to Full Scale）；所有以 dBFS 標示的位準都是負數（或 0），例如 -6 dBFS 的正弦波振幅是 0.5。這是 DAW meter 上看到的刻度。

float 可以超過 1.0 而不壞，但寫成整數 WAV 或送到 DAC 時，超過的部分會被**截斷**（clipping，見 2.7）。`WavFile.h` 寫 16/24-bit 時會 clamp 到 ±1；寫 32-bit float 時原樣保留。

### 2.5 dB 數學

人耳對音量的感覺是對數的：從 0.1 到 0.2 跟從 0.5 到 1.0 聽起來「差不多大一步」。所以音訊幾乎都用 **dB（decibel）**：

$$
L_{dB} = 20\log_{10}\frac{A}{A_{ref}} \qquad\Longleftrightarrow\qquad \frac{A}{A_{ref}} = 10^{L_{dB}/20}
$$

這是**振幅**（amplitude）的公式。如果比較的是**功率**（power，正比於振幅平方），係數是 10 而不是 20：$10\log_{10}(P/P_{ref}) = 10\log_{10}(A^2/A_{ref}^2) = 20\log_{10}(A/A_{ref})$。兩個公式算出來一樣，只是看你手上拿的是振幅還是功率。

幾個一定要記住的數字（來自 `l00_db_math` 的輸出）：

| dB | 倍率 | 記法 |
|---:|---:|---|
| +20 | ×10 | 每 +20 dB 多一個零 |
| +12 | ×3.98 ≈ ×4 | |
| +6 | ×1.995 ≈ ×2 | **+6 dB ≈ 兩倍振幅** |
| +3 | ×1.41 ≈ √2 | **+3 dB ≈ 兩倍功率** |
| 0 | ×1 | |
| -3 | ×0.708 ≈ 1/√2 | |
| -6 | ×0.501 ≈ ×0.5 | |
| -12 | ×0.251 ≈ ×0.25 | |
| -20 | ×0.1 | |
| -40 | ×0.01 | |
| -60 | ×0.001 | |

精確值：×2 = +6.0206 dB、√2 = +3.0103 dB。教材裡寫 6.02 / 3.01。

**相加：+6 dB 還是 +3 dB？** 這是初學者最常搞混的地方，關鍵在「兩個訊號相關不相關」：

- **完全相同的訊號**（例如同一軌複製兩份）：每個 sample 都是 $x + x = 2x$，振幅 ×2 → **+6.02 dB**。
- **不相關的訊號**（例如兩段不同的雜訊、兩把不同的吉他）：每個 sample 有時同號有時異號，振幅不會直接相加；相加的是**功率**：$P = P_1 + P_2 = 2P_1$，振幅 ×√2 → **+3.01 dB**。

```
== Summing two -12 dBFS signals ==
  Identical sines: RMS rises by +6.02 dB (amplitude ×2)
  Uncorrelated white noise: RMS rises by +3.04 dB (power ×2, amplitude ×√2)
```

推廣：$N$ 條相同的軌混在一起 +20log₁₀N dB，$N$ 條不相關的軌 +10log₁₀N dB。8 軌不相關的 -18 dBFS 訊號混起來約 -9 dBFS；但如果它們相同，就是 +18 dB → 剛好 0 dBFS，再多一點就 clip。這會直接影響你第 14 章 DAW 的 mix bus 要留多少 headroom。

`DspMath.h` 提供 `dbToGain()` 與 `gainToDb()`。低於 `kMinusInfinityDb`（-100 dB）一律視為靜音，避免 $\log_{10}(0) = -\infty$。

### 2.6 peak、RMS 與 crest factor

一段訊號「多大聲」有兩種量法：

- **Peak**：最大的 $|x[n]|$。它決定會不會 clip。
- **RMS（root mean square）**：$\sqrt{\frac{1}{N}\sum x[n]^2}$，等於功率的平方根。它比較接近人耳感受到的「平均音量」。

兩者的比值叫 **crest factor**（波峰因數），通常用 dB 表示：$\text{crest} = 20\log_{10}(\text{peak}/\text{RMS})$。它告訴你訊號「尖不尖」：

| 波形 | peak / RMS | crest factor |
|---|---|---|
| 方波 | 1 | 0 dB |
| 正弦波 | √2 | 3.01 dB |
| 均勻分布白雜訊 | √3 | 4.77 dB |
| 單一脈衝（0.5 秒內 1 個 sample） | √24000 | 43.8 dB |
| 鼓組 loop（本教材的合成鼓） | ≈ 5.3 | 14.5 dB |

正弦波的推導：$x = A\sin\theta$，$\overline{x^2} = A^2 \cdot \overline{\sin^2\theta} = A^2/2$，所以 RMS = $A/\sqrt 2$。均勻分布在 $[-A, A]$ 的雜訊：$\overline{x^2} = \int_{-A}^{A} \frac{x^2}{2A}dx = A^2/3$，RMS = $A/\sqrt 3$。

`l00_test_signals` 印出的 peak / RMS / crest 就是這些數字：

```
sine 440 Hz      ...  peak  -6.02 dBFS  RMS  -9.03 dBFS  crest  3.01 dB
white noise      ...  peak -12.04 dBFS  RMS -16.81 dBFS  crest  4.77 dB
drum loop        ...  peak  -2.92 dBFS  RMS -17.43 dBFS  crest 14.51 dB
```

鼓組 loop 的 peak 只有 -2.9 dBFS，RMS 卻只有 -17.4 dBFS：鼓聲的能量集中在很短的瞬間（transient），其餘時間很安靜。這就是「動態大」的數字意義，也是第 04、05 章 compressor / limiter 要處理的東西——把 crest factor 壓小，RMS 才能推大。

### 2.7 clipping

當 $|x[n]| > 1.0$ 的訊號被轉成整數（寫 WAV、送到 DAC），超過的部分會被**硬切**在 ±1.0：

$$
y[n] = \begin{cases} 1 & x[n] > 1 \\ x[n] & -1 \le x[n] \le 1 \\ -1 & x[n] < -1 \end{cases}
$$

```
  1.0 ┤  ╭───────╮            ╭───   ← 被削平的部分
      │ ╱         ╲          ╱
  0.0 ┼╱───────────╲────────╱──────
      │             ╲      ╱
 -1.0 ┤              ╰────╯
```

波形被削平，等於加入了原本沒有的高次諧波——聽起來刺耳、「破掉」。`l00_db_math` 把振幅 1.5 的 1 kHz 正弦波硬切，量到：

```
  clipped samples = 26000 / 48000 (54.2%; theory: the fraction with |sin| >= 2/3 ≈ 54%)
  THD = 15.22%; harmonics relative to the fundamental: H1 +0.0 dB  H2 -100.0 dB  H3 -16.5 dB  H4 -100.0 dB  H5 -35.7 dB
```

**THD（Total Harmonic Distortion，總諧波失真）** 是衡量「多出來的諧波總共有多少」的單一數字：

$$
\text{THD} = \frac{\sqrt{H_2^2 + H_3^2 + H_4^2 + \cdots}}{H_1} \times 100\%
$$

$H_k$ 是第 $k$ 次諧波的振幅。也就是「新增的諧波總共佔基頻多少比例」：0% 代表完全乾淨（只有原本的正弦波），數字越大失真越重。`Analysis.h` 的 `thdPercent()` 用 Goertzel 量 H2～H10（低於 Nyquist 的部分）再套這個公式。上面的 15.22% 主要來自 H3（-16.5 dB ≈ 0.15 倍）。

對稱的 clip 只產生**奇次**諧波（3、5、7…），偶次諧波為 0——這個規律第 06 章會再回來談。（這裡用 1 kHz @ 48 kHz 是為了讓 clip 的 sample 數是整齊的 26000；如 2.2 所說，這個頻率剛好讓 alias 回來的高次諧波也落在奇次諧波的位置上，所以 H2、H4 量起來是乾淨的 -100 dB。換成 1.1 kHz，alias 回來的成分會散落在各種非整數倍的位置，量 H2、H4 時可能會碰到一些 alias 的能量，但對稱 clip 本身仍然不產生偶次諧波。）數 clip 的 sample 數是練習 4 的題目之一。

### 2.8 channel 與 interleaved / non-interleaved

立體聲有左右兩個 channel，每個 channel 是獨立的一串 sample。同一個時間點上所有 channel 的 sample 合稱一個 **frame**。兩個 channel 在記憶體裡有兩種排法：

```
non-interleaved（planar）：每個 channel 一段連續記憶體
   L: [L0 L1 L2 L3 L4 L5 ...]
   R: [R0 R1 R2 R3 R4 R5 ...]

interleaved：所有 channel 交錯在同一段記憶體
   [L0 R0 L1 R1 L2 R2 L3 R3 ...]
```

- **WAV 檔案、大多數 audio driver API（WASAPI、CoreAudio 的部分模式）** 用 interleaved。
- **DSP 程式、JUCE、VST3** 用 non-interleaved：處理「一個 channel 的一段訊號」只要一個指標加一個迴圈，SIMD 也好做。

本教材的 `AudioBuffer` 是 non-interleaved。`WavFile.h` 在讀寫時負責轉換；練習 7 要你自己實作 interleave / deinterleave。

### 2.9 buffer、block processing 與 audio callback

真正的 audio 程式**不會**「把整首歌讀進來、一次處理完」。audio driver 每隔一小段時間就把一塊 **block**（也叫 buffer）的 sample 交給你，要你在**下一塊到來之前**處理完交回去。這個被 driver 呼叫的函式叫 **audio callback**：

```
          ┌────────────┐   每隔 blockSize / fs 秒呼叫一次
          │ audio      │ ────────────────────────────────┐
          │ driver     │                                 ▼
          │ (硬體計時) │ ◄──────────────────  void audioCallback(float** in, float** out, int n)
          └────────────┘   在期限內交回 n 個   {
                           sample，否則爆音       // 你的 DSP：gain、filter、compressor…
                                                }
```

```
時間 ──────────────────────────────────────────────────────►
block 0   │ block 1   │ block 2   │ block 3   │ ...
 處理中   │ 處理中    │ 處理中    │ 處理中    │
          ▲           ▲           ▲
          driver 取走 block 0，送給 DAC；同時交給你 block 1
```

因為你拿到一塊 sample 之後，最快也要等整塊處理完才能播出去，**延遲**（latency）至少是

$$
\text{latency} = \frac{\text{blockSize}}{f_s}
$$

`l00_block_processing` 印出的表：

```
     block      44100 Hz      48000 Hz      96000 Hz
        32       0.73 ms       0.67 ms       0.33 ms
        64       1.45 ms       1.33 ms       0.67 ms
       128       2.90 ms       2.67 ms       1.33 ms
       256       5.80 ms       5.33 ms       2.67 ms
       512      11.61 ms      10.67 ms       5.33 ms
      1024      23.22 ms      21.33 ms      10.67 ms
      2048      46.44 ms      42.67 ms      21.33 ms
```

這是單程的最小值。實際上輸入與輸出各有一塊，driver 還可能多加一層 buffer，所以錄音時聽到自己聲音的 **round-trip latency** 通常是 2～3 倍：512 @ 48 kHz 約 21～32 ms，彈樂器時會明顯感覺「慢半拍」；128 以下才會覺得「即時」。

**block size 小 → latency 低，但 callback 被呼叫得更頻繁**，每次處理的固定開銷（function call、參數更新）比例變大，CPU 壓力變高、更容易來不及。這是個 trade-off，所以 DAW 都讓使用者自己選。

**為什麼每支範例都用 block size 512 逐塊處理？** 因為所有 DSP 元件都必須在「只看到一小塊」的情況下正確運作：filter 的記憶（上一個 sample 的值）、envelope 的狀態、delay line 的內容都要跨 block 保留。如果整段一次處理，你永遠不會發現「忘了保存狀態」的 bug。`l00_block_processing` 示範了分塊處理 gain 這種「沒有狀態」的操作，結果和整段處理完全相同（最大差異 = 0）；之後的章節會看到有狀態的元件怎麼處理。

### 2.10 Real-time 的四大禁忌

audio callback 跑在一個高優先權的 **audio thread** 上，有硬性的期限（deadline）：512 samples @ 48 kHz 就是 10.67 ms，一次錯過就是聽得見的 click / dropout。所以 callback 裡絕對不可以做任何「**不知道要花多久**」的事。四大禁忌：

| 禁忌 | 為什麼 |
|---|---|
| **Allocation**（`new`、`malloc`、`std::vector::push_back`、`std::string`…） | 記憶體配置器內部有 lock，而且可能要向 OS 要新的 page（system call）。偶爾一次就要幾毫秒，deadline 直接爆掉。解法：所有記憶體在 `prepare()` 配好。 |
| **Lock**（`std::mutex`、任何會 block 的同步） | 如果持有 lock 的是低優先權的 UI thread，而它剛好被 OS 換掉，audio thread 就得等它——這叫 **priority inversion**。解法：`std::atomic`、lock-free queue（第 11 章 `SpectrumAnalyzer` 會用到）。 |
| **I/O**（檔案、console `printf`、網路、log） | 系統呼叫，時間完全不可預期（磁碟可能在休眠）。解法：audio thread 把資料丟進 lock-free FIFO，由另一個 thread 寫檔。 |
| **無界迴圈 / GC** | 迴圈次數不固定（例如「一直算到收斂」）就無法保證在期限內結束。有 garbage collector 的語言（Java、C#、Python）隨時可能暫停所有 thread 幾十毫秒——這就是為什麼 audio engine 幾乎都是 C / C++。 |

另外要避免：throw exception、呼叫不知道內部做什麼的第三方函式、denormal float（極小的數字會讓 CPU 慢 10～100 倍，第 02 章講 filter 時會處理）。

本教材每個 DSP 元件都遵守同一個介面：`prepare()` 是**唯一**可以 allocate 的地方，`process()` 裡只有算術。`l00_block_processing` 的 `audioCallback()` 示範了這個紀律——連統計結果的 `std::vector` 都是在進入迴圈前就配好的。

---

## 3. 從公式到程式碼

本章沒有新的 DSP 演算法要實作，所以「從公式到程式碼」分成兩部分：3.1～3.6 導覽 `common/` 共用工具，3.7～3.11 逐支解說範例程式。

本章沒有新的 `dsp/` header；所有範例只用 `common/include/dawlab/` 的工具。它們是之後 14 章每一支程式的基礎，值得花 20 分鐘把 header 讀一遍。一行 `#include <dawlab/Common.h>` 就全部引入。

### 3.1 `AudioBuffer.h`：`AudioBuffer` 與 `AudioBlock`

```cpp
dawlab::AudioBuffer buffer(2, 48000);      // 2 ch × 48000 samples，配置並清 0（會 allocate）
float* left = buffer.getWritePointer(0);   // non-interleaved：每個 channel 一段連續記憶體
buffer.setSample(1, 100, 0.5f);
buffer.applyGain(0.5f);

dawlab::AudioBlock block(buffer);          // 不擁有記憶體的 view（不 allocate）
dawlab::AudioBlock part = block.getSubBlock(512, 512);      // 第 512..1023 個 sample
dawlab::AudioBlock right = block.getChannelSubset(1, 1);    // 只有右聲道
part.multiplyBy(0.5f);                     // 直接改到 buffer 裡
```

- **`AudioBuffer` 擁有記憶體**（owning）。`setSize()` 會 allocate，所以只能在 `prepare()` 或離線程式裡呼叫，絕對不可以在 audio callback 裡呼叫。
- **`AudioBlock` 是 non-owning view**：只存 `float* const*`（每個 channel 的指標）、channel 數、sample 數與起始位移。複製它只是複製幾個整數和指標，不碰音訊資料。

**為什麼要有 view？** 兩個理由：

1. **切子區塊不用複製**：`getSubBlock()` 只是把起始位移往後移，`l00_block_processing` 就是這樣把 192000 個 sample 切成 375 塊 512 的 block，零複製。
2. **跟 JUCE / VST3 接軌**：它們也是用 `float**` 傳 channel 指標。第 13、14 章只要 `AudioBlock(juceBuffer.getArrayOfWritePointers(), numCh, numSamples)` 就能把同一份 DSP 程式碼用在 plugin 與 DAW 上，不用改一行。

所以本教材所有 DSP 元件的 `process()` 都吃 `AudioBlock`，而且 `AudioBuffer` 可以隱式轉成 `AudioBlock`，`processor.process(buffer)` 直接寫就對了。

### 3.2 `WavFile.h`：`readWav`、`writeWav`、`outputPath`

```cpp
dawlab::WavData wav;
std::string error;
if (!dawlab::readWav("in.wav", wav, &error)) { /* error 裡是失敗原因的說明文字 */ }
// wav.buffer（AudioBuffer，已轉成 float [-1,1]）、wav.sampleRate、wav.bitDepth

dawlab::writeWav(dawlab::outputPath("out.wav"), wav.buffer, wav.sampleRate);      // 預設 24-bit PCM
dawlab::writeWav(dawlab::outputPath("out32.wav"), wav.buffer, wav.sampleRate, 32); // 32-bit float，保留超過 ±1 的值
```

讀取支援 8/16/24/32-bit PCM、32/64-bit float 與 WAVE_FORMAT_EXTENSIBLE；寫入支援 16/24-bit PCM 與 32-bit float。整數格式寫出時會把超過 ±1.0 的值 clamp（這就是 2.7 的 clipping）。`outputPath()` 回傳 `dawlab_output/<檔名>` 並確保資料夾存在。

### 3.3 `Signals.h`：測試訊號

每一章都用這些 deterministic（同樣參數 → 同樣結果）的訊號當輸入，不需要準備音檔：

| 函式 | 用途 |
|---|---|
| `makeSine(freq, sr, seconds, amp, nch)` | 最單純的訊號，只有一個頻率。量 gain、量 THD 的基準。 |
| `makeLogSweep(f0, f1, sr, seconds, amp, nch)` | 頻率對數上升。用耳朵「聽」filter / EQ 的頻率響應。 |
| `makeWhiteNoise(sr, seconds, amp, nch, seed)` | 所有頻率能量平均；左右聲道用不同 seed 所以**不相關**。 |
| `makeImpulse(numSamples, nch, position, amp)` | 一個 sample = 1，其餘 0。送進線性系統，輸出就是 impulse response（第 12 章 convolution 會用）。 |
| `makeToneBursts(freq, sr, {levelsDb...}, segSeconds, nch)` | 音量階梯，例如 -30 → -10 → -30 dBFS。看 compressor 的 attack / release。 |
| `makeDrumLoop(sr, bars, bpm, amp, nch)` | 合成 kick / snare / hi-hat，crest factor 高。dynamics 章節的主要素材。 |
| `makePluck(freq, sr, seconds, ...)` | Karplus-Strong 撥弦，諧波豐富且高頻隨時間衰減。EQ、saturation 用。 |
| `makePluckSequence(sr, seconds, amp, nch)` | A 小調琶音，「像音樂」的素材。 |
| `NoiseGenerator` | xorshift32，輸出 $[-1, 1)$，給你自己合成訊號用。 |
| `secondsToSamples(sec, sr)` | 秒 → sample 數（`DspMath.h` 另有 `msToSamples`、`samplesToMs`）。 |

### 3.4 `Analysis.h`：用數字驗證

| 函式 | 用途 |
|---|---|
| `peakAbs(buffer)` / `peakAbs(ptr, n)` | 最大 $\|x\|$ |
| `rms(buffer, ch)` / `rms(ptr, n)` | RMS |
| `hasNanOrInf(buffer)` | 抓 DSP 寫錯造成的 NaN / Inf |
| `goertzelAmplitude(ptr, n, freq, sr)` | 訊號中**單一頻率**的正弦振幅 |
| `measureGainDb(processFn, freq, sr)` | 任何「一次一個 sample」的系統在某頻率的增益（dB） |
| `harmonicLevelsDb(...)`、`thdPercent(...)` | 諧波分析、THD |

**Goertzel 的直覺**：FFT 一次算出所有頻率，但如果你只想知道「這段訊號裡 1 kHz 有多大」，Goertzel 更便宜：它是一個刻意把共振頻率調在目標頻率的二階 IIR filter，把整段訊號餵過去，最後從 filter 的兩個狀態變數算出該頻率的振幅。餵進振幅 $A$ 的正弦波，回傳值 ≈ $A$。前面加了 Hann window，減少訊號頭尾不連續造成的洩漏（leakage）。IIR filter、window、leakage、FFT 這些名詞會在第 02 章（IIR filter）和第 11 章（FFT、window、leakage）詳談；現在只要知道：**給它一段訊號和一個頻率，它回傳那個頻率的正弦振幅**。

`measureGainDb()` 是第 02、03 章驗證 filter 的主力：丟一個 lambda 進去，它自動送正弦波、跳過 transient、量輸出對輸入的比值。

### 3.5 `Check.h`：寫 self-check

```cpp
DAWLAB_CHECK(peak <= 1.0f);
DAWLAB_CHECK_NEAR(gainDb, -3.01, 0.05);         // |a - b| <= tol
return dawlab::test::summary("l00_ex4");          // 全過回傳 0（ctest PASS），否則 1
```

失敗時印出運算式、實際值、預期值與檔案行號。每題練習的解答都以這個方式驗證，而不是「用耳朵聽起來對」。本章的 `l00_db_math` 也用它驗證 README 的每一個數值宣稱。

### 3.6 `DspMath.h`、`Console.h`

`kPi`、`kTwoPi`（MSVC 沒有 `M_PI`）、`dbToGain`、`gainToDb`、`lerp`、`msToSamples`、`samplesToMs`。`Console.h` 讓 Windows console 用 UTF-8 顯示（例如範例輸出裡的 ×、→、√ 等符號），任何引入 `dawlab/` header 的程式都會自動設定。

### 3.7 範例程式總覽

| target | 原始碼 | 示範 | 輸出 WAV |
|---|---|---|---|
| `l00_test_signals` | `examples/test_signals.cpp` | 產生全部測試訊號、印 peak / RMS / crest | `l00_sine_440.wav`、`l00_log_sweep.wav`、`l00_white_noise.wav`、`l00_impulse.wav`、`l00_tone_bursts.wav`、`l00_drum_loop.wav`、`l00_pluck_110.wav`、`l00_pluck_sequence.wav` |
| `l00_block_processing` | `examples/block_processing.cpp` | 模擬 audio callback、latency 表、每個 block 的 peak / RMS | `l00_block_input.wav`、`l00_block_processed.wav` |
| `l00_wav_info` | `examples/wav_info.cpp` | 讀 WAV（`argv[1]` 或自製）印資訊、normalize 到 -1 dBFS | `l00_wav_info_input.wav`、`l00_wav_info_normalized.wav` |
| `l00_db_math` | `examples/db_math.cpp` | 用數字驗證 2.2～2.7 的每個宣稱（self-check） | `l00_sum_identical.wav`、`l00_sum_uncorrelated.wav`、`l00_aliasing_30k.wav`、`l00_aliasing_sweep.wav`、`l00_quantized_8bit.wav`、`l00_quantized_16bit.wav`、`l00_clipped_sine.wav` |

### 3.8 `l00_test_signals`

把 `Signals.h` 的每一種訊號各做一段，寫成 WAV，並印出長度、peak、RMS、crest factor。這支程式的重點是**建立你對數字的直覺**：

```
sine 440 Hz      2 ch    96000 samples (2.00 s)  peak  -6.02 dBFS  RMS  -9.03 dBFS  crest  3.01 dB
log sweep        2 ch   192000 samples (4.00 s)  peak  -6.02 dBFS  RMS  -9.03 dBFS  crest  3.01 dB
white noise      2 ch    96000 samples (2.00 s)  peak -12.04 dBFS  RMS -16.81 dBFS  crest  4.77 dB
impulse          1 ch    24000 samples (0.50 s)  peak   0.00 dBFS  RMS -43.80 dBFS  crest 43.80 dB
tone bursts      2 ch    72000 samples (1.50 s)  peak -10.00 dBFS  RMS -17.70 dBFS  crest  7.70 dB
drum loop        2 ch   192000 samples (4.00 s)  peak  -2.92 dBFS  RMS -17.43 dBFS  crest 14.51 dB
pluck 110 Hz     2 ch    96000 samples (2.00 s)  peak  -6.03 dBFS  RMS -25.19 dBFS  crest 19.16 dB
pluck sequence   2 ch   192000 samples (4.00 s)  peak  -5.18 dBFS  RMS -19.71 dBFS  crest 14.53 dB
```

對照 2.5、2.6：振幅 0.5 = -6.02 dBFS；正弦波 RMS 比 peak 低 3.01 dB；白雜訊振幅 0.25 = -12.04 dBFS，crest 4.77 dB；脈衝 $\text{crest} = 10\log_{10}(24000) = 43.8$ dB。

**該聽到什麼**：用 Audacity 開頻譜圖。sine 是一條水平線；sweep 是往上彎的曲線（對數掃頻在對數頻率軸上是直線）；white noise 整片均勻；impulse 是一條垂直線（所有頻率同時出現）；drum loop 看得到 kick 在低頻、hi-hat 在高頻。

### 3.9 `l00_block_processing`

先印 2.9 的 latency 表，然後把 4 秒的鼓組 loop 切成 375 塊 512 samples 的 block，逐塊呼叫一個模擬的 `audioCallback()`：套 -6 dB gain、量該 block 的 peak 與 RMS（這就是 DAW meter 的雛型），最後驗證「分塊處理」與「整段處理」逐 sample 完全相同：

```
== Simulated audio callback: 192000 samples, block size 512 -> 375 blocks (last block 512 samples) ==
   block   time(ms)  peak dBFS   RMS dBFS
       0        0.0      -9.31     -14.01
      23      245.3     -19.13     -28.83
     ...
     138     1472.0    -100.00    -100.00     <- 這段是靜音（-100 dB = kMinusInfinityDb）
     ...
Input peak -2.92 dBFS -> output peak -8.92 dBFS (largest block peak -8.92 dBFS)
Max difference between block-wise and whole-signal processing = 0 (gain has no state, so it should be 0)
```

注意程式結構：`stats` 這個 `std::vector` 在進迴圈前就配好大小，`audioCallback()` 本身是 `noexcept` 而且只有乘法與比較——這就是 2.10 說的紀律。

**該聽到什麼**：`l00_block_processed.wav` 比 `l00_block_input.wav` 小聲一半（-6.02 dB），而且完全聽不出 375 個「塊」的接縫，因為 gain 沒有跨 block 的狀態。

### 3.10 `l00_wav_info`

不帶參數時自己產生一段撥弦琶音、寫成 24-bit WAV、再讀回來（示範 `readWav` 的完整流程）；帶參數時分析你給的檔案。印出 channel 數、sample rate、bit depth、長度、記憶體用量、clip 的 sample 數、每個 channel 的 peak / RMS / crest / DC offset，然後 normalize 到 -1 dBFS 並以 32-bit float 寫出：

```
File: dawlab_output\l00_wav_info_input.wav
  channels    = 2
  sample rate = 48000 Hz
  bit depth   = 24 bit
  frames      = 192000 (4.000 s)
  memory      = 1500.0 KB (float, 2 ch × 192000 samples × 4 bytes)
  clipped     = 0 samples (|x| >= 1.0)
Before processing:
  channel   peak dBFS   RMS dBFS   crest dB    DC offset
  0             -7.68     -22.21      14.53    -0.013856
Normalize to -1.0 dBFS: applied +6.68 dB (× 2.1571)
After processing:
  0             -1.00     -15.53      14.53    -0.029889
```

normalize 是「找到 peak，算出讓 peak 剛好等於目標的 gain，整段乘上去」：

$$
g = \frac{10^{-1/20}}{\text{peak}} \quad\Rightarrow\quad \text{新 peak} = g \cdot \text{peak} = 10^{-1/20} = 0.891 = -1\ \text{dBFS}
$$

注意 crest factor 不變（peak 與 RMS 同時加 6.68 dB），DC offset 也被乘了 2.157 倍。DC offset 是訊號的平均值，理想上是 0；這段撥弦有一點點負的 DC，是 Karplus-Strong 初始亂數的平均不是零造成的，實務上會用第 02 章的 highpass 把它濾掉。

試試看：`.\bin\Debug\l00_wav_info.exe dawlab_output\l00_drum_loop.wav`，或拿你自己的 WAV（16/24/32-bit 都可以）。

### 3.11 `l00_db_math`

這支範例沒有示範新的 API，它的工作是**用 `DAWLAB_CHECK` 驗證本章每一個數值宣稱**：dB 對照表、相同 vs 不相關相加（+6.02 / +3.04 dB）、四種波形的 crest factor、30 kHz → 18 kHz 的 aliasing、8～24 bit 的 quantization SNR 對 6.02N + 1.76、clipping 的 sample 數與諧波。輸出以 `[PASS] l00_db_math (17 checks passed)` 結尾。讀者可以改數字（例如把 30 kHz 改成 50 kHz）看看會發生什麼。

**該聽到什麼**：`l00_aliasing_sweep.wav` 的音高過了 24 kHz 後往下掉；`l00_quantized_8bit.wav` 有嘶嘶聲；`l00_clipped_sine.wav` 比乾淨的正弦波「亮」而刺耳。

---

## 4. Real-time 注意事項

本章沒有 DSP 元件，但這裡先立下之後每一章都遵守的規矩：

- **`prepare(sampleRate, maxBlockSize, numChannels)` 是唯一可以 allocate 的地方**。所有 `std::vector`、delay line、暫存 buffer 都在這裡配到最大需要的尺寸。`process()` 收到的 block 永遠 ≤ `maxBlockSize`。
- **`process(AudioBlock)` 裡只有算術**：沒有 `new`、沒有 `std::vector::push_back`、沒有 mutex、沒有 `printf`、沒有 exception。範例程式的 `printf` 都在模擬的 callback **之外**。
- **參數從 UI thread 來**：setter 寫入的值會被 audio thread 讀，用 `std::atomic<float>` 或至少保證是單一 word 的寫入；而且參數變化要平滑（第 01 章 `SmoothedValue`），否則會有 zipper noise。
- **量測值給 UI 讀**（meter、gain reduction）：用 `std::atomic<float>` relaxed 存，UI thread 定時讀。
- **不要在 audio thread 讀寫檔案**。本教材的範例是離線程式，所以 `writeWav` 沒關係；到了第 13、14 章有真正的 audio device 時，錄音要透過 lock-free FIFO 交給另一個 thread 寫。

---

## 5. 常見錯誤與除錯

| 症狀 | 可能原因 | 怎麼查 |
|---|---|---|
| 輸出 WAV 聽起來破掉、Audacity 一片紅 | 超過 ±1.0 就寫成 16/24-bit → clipping | `peakAbs()` > 1？用 `l00_wav_info` 看 clipped 數；寫 32-bit float 看原始值 |
| 音量比預期小/大 6 dB | 把 +6 dB 當成兩倍功率，或把兩個相同訊號相加當成 +3 dB | 回到 2.5；`gainToDb(2.0f)` 是 6.02 |
| 每隔固定時間一個 click | block 邊界處狀態沒保存（filter 記憶、envelope…） | 用 `l00_block_processing` 的方法比較「分塊」與「整段」結果 |
| 高頻聽起來「金屬感」、音高跑反方向 | aliasing：非線性處理沒有 oversampling，或產生了超過 Nyquist 的訊號 | 聽 `l00_aliasing_sweep.wav` 熟悉這個聲音；第 06 章 |
| 很小聲但有嘶嘶聲 | bit depth 太低或訊號位準太低 | 2.3：SNR 隨位準下降 |
| 左右聲道合併後某些聲音消失 | 左右反相（L = -R）→ downmix 相消 | 練習 5 |
| 輸出全是 0 或 NaN | 除以 0（例如 normalize 靜音 buffer）、log(0) | `hasNanOrInf()`；normalize 前檢查 peak > 0 |
| 程式找不到輸出檔 | 工作目錄不在 build 資料夾 | `outputPath()` 寫到 `<工作目錄>/dawlab_output/`；先 `cd build` |
| printf 輸出的非 ASCII 符號（×、→、√）變亂碼 | 沒有引入 `dawlab/Console.h`（`Common.h` 已含）或 terminal 不是 UTF-8 | PowerShell 可用 `chcp 65001` |
| MSVC `M_PI` 未定義 | MSVC 預設沒有 | 用 `dawlab::kPi` |
| `/W4` 警告：double → float、signed/unsigned | 混用型別 | 明確 `static_cast<float>()`、index 用 `size_t` |

---

## 6. 練習題

見 [`exercises/README.zh-TW.md`](exercises/README.zh-TW.md)。7 題：3 題計算 / 觀念（dB 換算、latency、aliasing）、4 題實作（normalize 與 clipping 計數、stereo↔mono、linear vs equal-power fade、interleave/deinterleave），每題實作都有 `exercises/starter/` 的起始碼與 `solutions/` 的完整解答。解答與解說在 [`solutions/README.zh-TW.md`](solutions/README.zh-TW.md)。

---

## 7. 延伸閱讀

- Julius O. Smith III, *Mathematics of the Discrete Fourier Transform (DFT) with Audio Applications*（線上免費，CCRMA）—— sampling、aliasing、dB 的嚴謹推導。
- Udo Zölzer (ed.), *DAFX: Digital Audio Effects*, 2nd ed., Wiley, 2011 —— 整套教材會反覆引用的效果器聖經。
- Will Pirkle, *Designing Audio Effect Plugins in C++*, 2nd ed., Routledge, 2019 —— 從 DSP 到 plugin 的實作導向書。
- Ross Bencina, "Real-time audio programming 101: time waits for nothing"（部落格文章，2011）—— 2.10 四大禁忌最常被引用的出處。
- Timur Doumler, "C++ in the Audio Industry"（CppCon 2015 演講）—— real-time C++ 的實務。
- Bob Katz, *Mastering Audio: The Art and the Science*, 3rd ed., Focal Press, 2014 —— peak、RMS、crest factor、headroom 的聽感面。
- ITU-R BS.1770（loudness 與 true-peak 的量測演算法：K-weighting、gating）、EBU R 128（loudness 正規化的建議書）與 EBU Tech 3341（loudness meter 規格：momentary / short-term / integrated）—— 第 11 章 `LoudnessMeter` 用。EBU Tech 3343 則是依 R 128 製作節目的實務指引，可當補充讀物。

---

## 8. 下一章

[第 01 章：gain](../01-gain/README.zh-TW.md) —— 從「乘一個數」開始，做出第一個真正的 DSP 元件 `Gain`，並學會用 `SmoothedValue` 避免參數變化時的 zipper noise，以及 `Panner` 的三種 pan law。
