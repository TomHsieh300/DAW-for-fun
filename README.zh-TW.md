# DawLab：用 C++ 從零學 audio DSP，最後自製 8 軌 DAW
[English](README.md) | **繁體中文**

這是一套給「會基本 C++、但沒學過 DSP」的學習者的教材。本教材為**英文 / 繁體中文雙語**：英文版是主要版本（原檔名，例如 `README.md`），繁體中文版放在同目錄的 `.zh-TW.md` 檔（例如 `README.zh-TW.md`）；每份文件頂端都有語言切換連結，程式碼的註解與輸出皆為英文。從「聲音在電腦裡長什麼樣子」開始，一章做出一個（或一組）真正可用的 DSP 元件：gain、filter、EQ、compressor、limiter、saturation、distortion、gate、delay、modulation、meter、reverb……接著用 JUCE 把它們包成 VST3 plugin、寫出能載入 VST3 的 host，最後組成一台能編排 clip、混音、插 plugin、匯出 WAV 的 **8 軌 DAW「DawLab 8」**。

![DawLab 8：播放示範歌曲（第 2 小節第 3 拍）](docs/images/dawlab8.png)

*上圖不是螢幕截圖，而是 app 自己畫出來的：`"DawLab 8.exe" --snapshot <檔名.png>`（第 14 章 Step 12），每次建置時 ctest 都會跑一次。同一個指令還會另存一張參數面板（下圖，鼓組軌的 compressor）：面板上的旋鈕是依各效果器的參數描述自動產生的。*

![自動產生的參數面板](docs/images/dawlab8_panel.png)

## 這套教材的特色

- **每個數字都驗證過**：每章的範例與解答都用 `DAWLAB_CHECK` 以數字自我檢查（例如「cutoff 處是 -3.01 dB」、「limiter 輸出 peak 一定 ≤ ceiling」），失敗就回傳非 0。README 裡引用的數字都是程式實際跑出來的結果。
- **聽得到**：範例會把處理前、處理後的音訊寫成 WAV，讓你用耳朵確認公式的意義。
- **同一份 DSP 程式碼從頭用到尾**：`dsp/` 裡的元件是 header-only、不依賴 JUCE 的純 C++17，離線範例、VST3 plugin 與 DAW engine 共用同一份。
- **Real-time 觀念貫穿全書**：audio thread 上不 allocate、不拿 lock、參數要平滑、denormal、thread-safe 的 meter……每章都有一節「Real-time 注意事項」。

## 先備知識

- **C++**：class、template、`std::vector`、lambda、reference 與指標。不需要會 JUCE；第 13 章會從頭介紹。
- **數學**：高中程度的三角函數、指數與對數。複數、轉移函數、FFT 等需要的工具在用到的那一章推導。
- **不需要任何 DSP 或音訊背景**。有一副耳機、一個能看波形與頻譜的軟體（推薦免費的 [Audacity](https://www.audacityteam.org/)）會很有幫助。

## 環境安裝

### Windows（主要支援平台）

1. 安裝 [Visual Studio 2022](https://visualstudio.microsoft.com/)：**Build Tools**（純命令列、體積小）或 **Community**（含 IDE、免費）都可以。
2. 安裝時勾選工作負載 **「使用 C++ 的桌面開發」（Desktop development with C++）**。它會一起裝好 MSVC、Windows SDK 與 CMake；本專案的腳本會自動找到 VS 內附的 CMake，系統 PATH 上不需要有 `cmake`。
3. 第 13、14 章需要 JUCE：
   - 完整的專案資料夾已在 `third_party/JUCE` 附上 JUCE **9.0.4**。**若你是用 git 取得本專案，`third_party/` 不在版本控制內**（列在 `.gitignore`），需要自己 clone JUCE 9.0.4 到 `third_party/JUCE`，或直接跳到下一點讓 CMake 用 FetchContent 下載：
     ```powershell
     git clone --depth 1 --branch 9.0.4 https://github.com/juce-framework/JUCE.git third_party/JUCE
     ```
   - 不 clone 也可以：`third_party/JUCE` 不存在時，CMake 會用 FetchContent 自動從 GitHub 下載 `DAWLAB_JUCE_TAG`（預設 9.0.4）指定的版本（需要網路與 git）。也可以用 `-DDAWLAB_JUCE_DIR=<路徑>` 指向你自己的 JUCE。
   - VST3 SDK 已內含在 JUCE 裡，不需要另外下載。

### Linux（已在 Ubuntu 24.04 驗證）

所有章節，包括 JUCE 的第 13、14 章，都已在 **Ubuntu 24.04 LTS、GCC 13.3、CMake 3.28** 上建置成功並通過全部測試（第 00–12 章加上第 14 章純 C++ 部分共 149 個測試，第 13–14 章的 JUCE 建置共 44 個測試，包含 `DawLab 8` 的 smoke test 與 snapshot）。

1. 安裝編譯器與 CMake：
   ```bash
   sudo apt install build-essential cmake git
   ```
2. 第 13、14 章需要 JUCE 的系統相依套件（ALSA / JACK、X11、freetype、fontconfig、OpenGL、webkit2gtk、curl；見 `third_party/JUCE/docs/Linux Dependencies.md`）：
   ```bash
   sudo apt install libasound2-dev libjack-jackd2-dev ladspa-sdk libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libglu1-mesa-dev mesa-common-dev libcurl4-openssl-dev libwebkit2gtk-4.1-dev
   ```
3. JUCE 本身的取得方式和 Windows 相同：clone 到 `third_party/JUCE`（指令見上），或讓 CMake 用 FetchContent 下載。

`scripts/build.sh` 是 `build.ps1` 在 Linux / macOS 上的對應腳本（選項相同，寫成小寫的 `--long-option` 風格）；見下一節。

### macOS（未驗證）

第 00–12 章是純 C++17 + CMake，在 macOS（`xcode-select --install`、`brew install cmake`）上應該可以用 `./scripts/build.sh` 同樣建置。macOS 尚未測試過，指令可能需要自行調整。

## 建置、執行、測試

所有指令都在專案根目錄的 PowerShell 執行（`scripts/build.ps1` 會依序 configure → build → ctest）：

```powershell
.\scripts\build.ps1                                   # 全部純 C++ 章節（00–12），Debug，並跑所有測試
.\scripts\build.ps1 -Only 02-filter                   # 只建第 02 章
.\scripts\build.ps1 -Only 02-filter,03-eq             # 多章以逗號分隔
.\scripts\build.ps1 -Juce                             # 連同第 13、14 章（JUCE）一起建置
.\scripts\build.ps1 -Only 14-daw-8track -Juce         # 只建第 14 章
.\scripts\build.ps1 -Config Release                   # Release 組態（另有 RelWithDebInfo）
.\scripts\build.ps1 -Only 05-limiter -NoTest          # 只建置、不跑測試
.\scripts\build.ps1 -Only 05-limiter -TestLabel example   # 只跑範例（或 solution、test）
.\scripts\build.ps1 -BuildDir build-release -Config Release   # 指定 build 資料夾（預設 build）
```

PowerShell 說「無法載入指令碼」時，改用 `powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 ...`。第一次加 `-Juce` 時會先建 JUCE 的工具 `juceaide`，完整建置需要數分鐘到十幾分鐘。

在 **Linux / macOS** 上，於終端機的專案根目錄執行 `scripts/build.sh`，選項相同：

```bash
./scripts/build.sh                                    # 全部純 C++ 章節（00–12），Debug，並跑所有測試
./scripts/build.sh --only 02-filter,03-eq             # 多章以逗號分隔
./scripts/build.sh --only 14-daw-8track --juce        # 只建第 14 章，連同 JUCE 應用程式
./scripts/build.sh --config Release --no-test         # Release，只建置
./scripts/build.sh --only 05-limiter --test-label example   # 只跑範例（或 solution、test）
./scripts/build.sh --build-dir build-juce --juce      # 指定 build 資料夾（預設 build）
```

它使用單一組態的 generator（有安裝 Ninja 就用 Ninja，否則用 Makefiles），所以組態在每個 build 資料夾內是固定的：Debug 與 Release 請使用不同的 `--build-dir`。

**在 Linux / macOS 上讀各章 README**：各章寫的是 Windows 指令，請這樣對照：`.\scripts\build.ps1 -Only 02-filter` → `./scripts/build.sh --only 02-filter`，`-Juce` → `--juce`，`.\bin\Debug\l02_filter_response.exe` → `./bin/l02_filter_response`（沒有 `Debug/` 子資料夾，也沒有 `.exe`）。

**輸出位置**（以預設的 `build` 資料夾為例）：

| 東西 | 位置 |
|---|---|
| 範例、解答、起始碼的執行檔 | `build\bin\Debug\lNN_*.exe`（Release 在 `build\bin\Release\`） |
| 程式輸出的 WAV / PNG | 目前工作目錄底下的 `dawlab_output\`（ctest 的工作目錄就是 build 資料夾，所以是 `build\dawlab_output\`） |
| 第 13 章的 VST3 plugin | `build\lessons\13-vst-hosting\plugin\DawLabCompressor_artefacts\Debug\VST3\DawLabCompressor.vst3`（`DawLabLimiter` 同理） |
| 第 14 章的 DAW | `build\bin\Debug\DawLab 8.exe` |
| 在 Linux / macOS 上 | `build/bin/lNN_*` 與 `build/bin/DawLab 8`（沒有 `Debug/` 子資料夾，也沒有 `.exe`）；VST3 bundle 在 `build/lessons/13-vst-hosting/plugin/DawLabCompressor_artefacts/VST3/DawLabCompressor.vst3` |

手動執行範例時，**請先 `cd` 到 build 資料夾**，WAV 才會出現在 `build\dawlab_output\`：

```powershell
cd build
.\bin\Debug\l02_filter_response.exe
& ".\bin\Debug\DawLab 8.exe"                          # 開啟 DAW（需要音訊裝置）
& ".\bin\Debug\DawLab 8.exe" --snapshot dawlab_output\dawlab8.png   # 不開視窗，把畫面存成 PNG
```

**ctest 的 label**：`example`（範例，不帶參數就能跑完）、`solution`（練習解答，以數字自我檢查）、`test`（元件與 engine 的回歸測試）。`exercises/starter/` 的起始碼只建置、不登記成測試——它們留有 `// TODO`，執行時印出 FAIL 是預期的。

## 目錄結構

```
DAW/
├── README.md                 本頁（英文；中文版是 README.zh-TW.md）
├── CMakeLists.txt            根建置檔（DAWLAB_ONLY、DAWLAB_WITH_JUCE、DAWLAB_JUCE_DIR 等選項）
├── cmake/DawLab.cmake        dawlab_add_example / _solution / _starter / _test
├── scripts/build.ps1         Windows 建置 + 測試腳本
├── scripts/build.sh          Linux / macOS 建置 + 測試腳本（選項相同）
├── docs/
│   ├── authoring-guide.md    教材撰寫規範與 DSP API 合約（另有 .zh-TW.md）
│   └── images/               本頁的截圖
├── common/include/dawlab/    共用工具：AudioBuffer / AudioBlock、WAV 讀寫、測試訊號、分析、self-check
├── dsp/include/dawlab/dsp/   各章做出來的 DSP 元件（header-only；清單見 dsp/include/dawlab/dsp/README.md）
├── lessons/
│   └── NN-name/
│       ├── README.md         講義（主教材；另有 README.zh-TW.md）
│       ├── examples/         範例程式
│       ├── exercises/        練習題（README.md）與起始碼（starter/）
│       ├── solutions/        所有題目的解答與解說，以及實作題的完整程式
│       └── tests/            （部分章節）元件回歸測試
└── third_party/JUCE/         JUCE 9.0.4（第 13、14 章）
```

所有 DSP 元件的一覽與用途見 [dsp/include/dawlab/dsp/README.zh-TW.md](dsp/include/dawlab/dsp/README.zh-TW.md)；統一介面與 API 合約見 [docs/authoring-guide.zh-TW.md](docs/authoring-guide.zh-TW.md)。

## 章節路線圖

章節依編號順序前進，每一章都會用到前面章節的元件。

| 章 | 主題 | 你會學到 | 做出的元件 |
|---|---|---|---|
| [00](lessons/00-basics/README.zh-TW.md) | 數位音訊基礎與開發環境 | sample、sample rate 與 Nyquist、aliasing、bit depth、dB、peak / RMS、clipping、block processing 與 audio callback | （使用 `common/` 的共用工具） |
| [01](lessons/01-gain/README.zh-TW.md) | Gain、參數平滑與 Pan | 為什麼瞬間改 gain 會 click、linear ramp、pan law、mono pan 與 stereo balance、混音的 headroom | `SmoothedValue`、`Gain`、`Panner` |
| [02](lessons/02-filter/README.zh-TW.md) | Filter：從 one-pole 到 biquad | 差分方程、頻率響應、pole / zero、RBJ cookbook、direct form 與數值精度、cutoff 調變 | `OnePole`、`Biquad`、`StateVariableFilter` |
| [03](lessons/03-eq/README.zh-TW.md) | Parametric EQ | shelf / peak / cut、Q 與頻寬、串接、相位、參數平滑與 click、Nyquist 附近的 cramping | `ParametricEq` |
| [04](lessons/04-compressor/README.zh-TW.md) | Compressor | envelope detector、gain computer、knee、attack / release、log-domain 平滑、stereo link | `EnvelopeFollower`、`Compressor` |
| [05](lessons/05-limiter/README.zh-TW.md) | Limiter | lookahead、brickwall 保證、滑動視窗最小值、latency 回報 | `DelayLine`、`Limiter` |
| [06](lessons/06-saturation/README.zh-TW.md) | Saturation 與 aliasing | waveshaper 與諧波、非線性造成的 aliasing、halfband FIR oversampling | `Oversampler`、`Saturation` |
| [07](lessons/07-distortion/README.zh-TW.md) | Distortion | hard clip、foldback、fuzz、rectify、bitcrush、amp 模擬鏈 | `Distortion`、`AmpChain` |
| [08](lessons/08-gate-expander/README.zh-TW.md) | Gate、Expander、De-esser、Transient Shaper | hysteresis 與 hold、downward expansion、sidechain、split-band、transient 偵測 | `Gate`、`Expander`、`DeEsser`、`TransientShaper` |
| [09](lessons/09-delay/README.zh-TW.md) | Delay 與 Echo | circular buffer、fractional delay 與插值、feedback 穩定性、ping-pong、tempo sync、Doppler | `FractionalDelayLine`、`Delay` |
| [10](lessons/10-modulation/README.zh-TW.md) | Modulation | LFO、chorus、flanger、phaser、tremolo / autopan | `Lfo`、`Chorus`、`Flanger`、`Phaser`、`Tremolo` |
| [11](lessons/11-metering-fft/README.zh-TW.md) | Metering 與 FFT | peak / RMS meter、LUFS（BS.1770 / R128）、FFT、window 與 leakage、lock-free 頻譜分析 | `Fft`、`LevelMeter`、`LoudnessMeter`、`SpectrumAnalyzer` |
| [12](lessons/12-reverb/README.zh-TW.md) | Reverb | RT60、comb / allpass、FDN、partitioned FFT convolution | `ReverbBuildingBlocks`、`Reverb`、`ConvolutionReverb` |
| [13](lessons/13-vst-hosting/README.zh-TW.md) | VST3 plugin 與 hosting（JUCE） | plugin 架構與生命週期、參數與 state、latency 與 PDC、掃描與載入第三方 VST3 | `DawLabCompressor` / `DawLabLimiter`（VST3）、`dawlab_plugin_host` 函式庫 |
| [14](lessons/14-daw-8track/README.zh-TW.md) | **Capstone：8 軌 DAW** | transport 與兩種時鐘、clip、mixer 與 FX bus、lock-free command queue、PDC、offline bounce、real-time safety 測試、JUCE GUI、VST3 insert | 純 C++ 的 `dawlab::engine` 與 JUCE app「DawLab 8」 |

> 英文是主要語言：每份文件的英文版用原檔名，繁體中文版是同名的 `.zh-TW.md`，兩者內容同步；中文版的專有名詞保留英文。

## 建議的學習方法

每一章都照這個順序走：

1. **讀 `README.md`**：先看「本章目標」，再讀原理。推導可以拿紙筆跟著算一次。
2. **跑範例並試聽**：用 `build.ps1 -Only NN-name` 建置，`cd build` 後執行 `bin\Debug\lNN_*.exe`。對照 README 的數字看輸出，用耳機聽 `dawlab_output\` 裡處理前 / 處理後的 WAV，並用 Audacity 看波形與頻譜。
3. **做練習**：`exercises/README.md` 有觀念題、計算題與實作題（標示難度 ★☆☆～★★★）。觀念題、計算題先自己寫下答案。
4. **跑自己的 starter**：實作題的起始碼在 `exercises/starter/`，完成 `// TODO` 後重新建置並執行對應的 `lNN_exN_starter.exe`；self-check 從 FAIL 變成全部 PASS 才算完成。
5. **對照解答**：`solutions/README.md` 有每一題的答案與解說，`solutions/*.cpp` 是完整的參考實作。即使你的版本已經 PASS，也值得比較兩者的寫法與註解。

遇到怪聲音、NaN 或 click 時，先翻該章的「常見錯誤與除錯」。

## 授權說明

本專案依檔案類型採用兩種授權，完整說明見 [LICENSING.zh-TW.md](LICENSING.zh-TW.md)：

- **程式碼**（`*.cpp`、`*.h`、`CMakeLists.txt`、`*.cmake`、腳本）：[MIT License](LICENSE)。
- **教材文字與圖片**（所有語言的 `*.md`、`docs/images/`、`lessons/*/docs/images/`）：[CC BY 4.0](LICENSE-docs)；重用教材文字時須標示出處。
- **JUCE**（只有第 13、14 章需要，不包含在本 repository 內）：採雙授權，[AGPLv3](https://www.gnu.org/licenses/agpl-3.0.en.html) 或商業的 JUCE EULA。以 AGPLv3 使用 JUCE 並散佈你做出來的 plugin 或 app 時，整個原始碼也必須以 AGPLv3 公開；不想公開就要依 JUCE EULA 取得授權。VST3 SDK（內含於 JUCE）為 MIT License，使用「VST」商標或 logo 須遵守 Steinberg 的使用規範。
- 只用到第 00–12 章（`common/`、`dsp/` 與這些章節的程式）時不會連結 JUCE，不受 JUCE 授權影響。

這些摘要僅供參考，不是法律意見；第 13 章 3.7 節有更完整的說明。打算散佈用本教材程式碼做出的 plugin 或 app 前，請自行閱讀授權條款，必要時諮詢法律意見。
