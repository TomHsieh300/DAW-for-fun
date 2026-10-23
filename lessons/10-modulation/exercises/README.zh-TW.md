# 第 10 章 練習題：Modulation —— Chorus、Flanger、Phaser、Tremolo

[English](README.md) | **繁體中文**

難度：★☆☆ 基礎 / ★★☆ 中等 / ★★★ 進階。
實作題（3–6、8）都有起始碼 `exercises/starter/exN_xxx.cpp`（可編譯、留 `// TODO`，執行會印出 FAIL——這是預期的）與完整解答 `solutions/exN_xxx.cpp`。
解答與解說（含觀念題、計算題）在 [`../solutions/README.zh-TW.md`](../solutions/README.zh-TW.md)，建議先自己做完再看。

建置與執行（Windows，在專案根目錄）：

```powershell
.\scripts\build.ps1 -Only 10-modulation -NoTest     # 預設 build 目錄是 build\
cd build
.\bin\Debug\l10_ex3_starter.exe      # 你的版本（改 exercises/starter/ex3_vibrato.cpp）
.\bin\Debug\l10_ex3_solution.exe     # 參考解答
```

改完 starter 之後重新執行 `build.ps1`（或在專案根目錄 `cmake --build build --config Debug --target l10_ex3_starter`）即可。
程式輸出的 WAV 會寫在「目前工作目錄」的 `dawlab_output/` 底下，所以先 `cd build` 再執行。
注意 starter 與 solution 輸出的 WAV 檔名相同（例如都是 `l10_ex3_vibrato.wav`），後執行的會覆蓋先執行的。
每支 starter 的 self-check 全部通過、印出 `[PASS]` 就算完成。

| 題號 | 類型 | 難度 | starter / solution target |
|---|---|---|---|
| 1 | 觀念 | ★☆☆ | — |
| 2 | 計算 | ★★☆ | — |
| 3 | 實作：Vibrato | ★★☆ | `l10_ex3_starter` / `l10_ex3_solution` |
| 4 | 實作：Ring modulator | ★★☆ | `l10_ex4_starter` / `l10_ex4_solution` |
| 5 | 實作：Stereo chorus 與相關係數 | ★★☆ | `l10_ex5_starter` / `l10_ex5_solution` |
| 6 | 實作：Tempo-synced tremolo | ★★☆ | `l10_ex6_starter` / `l10_ex6_solution` |
| 7 | 觀念 + 計算：Phaser 的 notch 間距 | ★★★ | — |
| 8 | 實作：Staggered phaser（每級不同 $f_c$） | ★★★ | `l10_ex8_starter` / `l10_ex8_solution` |

---

## 練習 1 ★☆☆（觀念）Chorus、flanger、vibrato 的本質差異

`Chorus` 和 `Flanger` 的程式幾乎一樣：都是「被 LFO 推動的延遲 + 原音」。

1. 兩者的延遲範圍各是多少？為什麼「延遲長短」會讓耳朵聽到完全不同的東西？請從**頻域**（comb filter 的梳齒間距 $1/d$）和**音高**（Doppler，$1 - d'(t)$）兩個角度各說一次。
2. Flanger 常常開 feedback，chorus 幾乎不用。為什麼？（提示：feedback 在 $f = k/d$ 製造共振，$d = 20$ ms 時這些共振頻率是多少？聽起來會像什麼？）
3. Vibrato 和 chorus 只差一個參數。是哪一個？為什麼 vibrato 沒有 comb filter 的峰谷？
4. 把 flanger 的 mix 從 0.5 改成 0.2，notch 還會到 −∞ dB 嗎？最深會到幾 dB？

## 練習 2 ★★☆（計算）把公式算過一遍

全部用 $f_s = 48$ kHz。

1. Flanger，延遲固定 0.5 ms、mix 0.5、feedback 0：前三個 notch 與前三個 peak（含 0 Hz）的頻率？
2. 想讓 flanger 的**第一個** notch 在 200 Hz 到 1000 Hz 之間掃，`setDelayMs` 與 `setDepthMs` 該設多少？（本章 `Flanger` 的延遲範圍是 $[\text{delay}, \text{delay} + \text{depth}]$。）
3. Flanger mix 0.5、feedback +0.7：peak 與原 notch 位置的增益各是幾 dB？feedback 改成 −0.7 呢？
4. Chorus depth 5 ms、rate 1 Hz：最大音高偏移幾 cents？如果 depth 固定 5 ms、希望最大偏移只有 ±10 cents，rate 最多可以設多少？
5. 一階 allpass，$f_c = 1$ kHz：係數 $a$ 是多少？500 Hz 的相位是幾度？
6. 4 stages phaser，所有 stage 的 $f_c = 1$ kHz、mix 0.5、feedback 0：兩個 notch 在哪裡？
7. Tremolo：1 kHz 正弦（振幅 0.5）、sine LFO 6 Hz、depth 0.6。輸出的頻譜有哪幾根線？各自振幅多少？
8. Tempo sync：128 bpm 的附點八分音符，LFO rate 是多少 Hz？一個週期幾 ms？

## 練習 3 ★★☆（實作）Vibrato

起始碼：`exercises/starter/ex3_vibrato.cpp`

用第 09 章的 `FractionalDelayLine` 與本章的 `Lfo` 寫一個 `Vibrato`：延遲 $d(t) = \text{centre} + \text{depth}\cdot m(t)$，輸出**只有**延遲後的訊號。完成三個 TODO：

1. `prepare()` 裡配置 delay line（最長延遲 = `kMaxCentreMs + kMaxDepthMs`，換成 sample，多留幾個給插值）。
2. `advanceModulation()` 算出這個 sample 的延遲（sample），clamp 在合法範圍。
3. `processSample()` / `process()`：push、再用 `readCubic` 讀出來，**不要**混原音。

Self-check：

- 1 kHz 正弦、rate 5 Hz、depth 1 ms → 瞬時頻率最大 / 最小 = $1000(1 \pm 2\pi \cdot 5 \cdot 0.001) = 1031.4 / 968.6$ Hz（±2 Hz）。
- 平均頻率 1000 Hz（±1 Hz）；輸出 RMS 與輸入相同（±0.3 dB）。
- depth 0 → 頻率固定 1000 Hz。

輸出 `l10_ex3_vibrato.wav`（撥弦琶音 + vibrato）。聽聽看 depth、rate 怎麼搭配才像歌手的自然顫音（提示：5–6 Hz、±20–50 cents）。

## 練習 4 ★★☆（實作）Ring modulator

起始碼：`exercises/starter/ex4_ringmod.cpp`

$y = (1 - \text{mix})\,x + \text{mix}\cdot x \cdot c(t)$，載波 $c(t) = \sin(2\pi f_c t)$ 用一個跑在可聽頻率範圍的 `Lfo`（sine）產生。

Self-check：

- 1000 Hz 正弦（$A = 0.5$）× 150 Hz 載波、mix 1：850 與 1150 Hz 振幅 ≈ 0.25（±0.005）；1000 Hz 與 150 Hz 都低於 −60 dB。
- 載波 0 Hz、相位 0.25（$\sin = 1$）→ 輸出 = 輸入。
- mix 0.5：1000 Hz 剩 0.25、850 Hz 為 0.125。

輸出 `l10_ex4_ringmod_30hz.wav`、`l10_ex4_ringmod_500hz.wav`。比較 30 Hz（像很深的 tremolo）和 500 Hz（金屬鐘聲）的差別，並說明為什麼 30 Hz 載波聽起來是「每秒 60 次」的起伏。

## 練習 5 ★★☆（實作）Stereo chorus 與左右相關性

起始碼：`exercises/starter/ex5_stereo_chorus.cpp`

寫一個只有 1 個 voice、但左右各有一個 LFO 的 `StereoChorus`，右聲道的 LFO 起始相位 = `phaseOffset`（0.25 = 90°）。完成 TODO 後量測 Pearson 相關係數 $\rho = \sum LR / \sqrt{\sum L^2 \sum R^2}$。

Self-check：

- phase offset 0 → $\rho > 0.999$。
- phase offset 0.25、mix 1 → $\rho < 0.5$。
- phase offset 0.25、mix 0.5 → $\rho$ 介於上面兩者之間。
- 相位差真的是 90°：rate 0、impulse 輸入 → 左聲道的 impulse 出現在 15 ms（centre）、右聲道在 18 ms（centre + depth·sin 90°）。白雜訊的 $\rho$ 只看得出「左右有沒有錯開」，看不出錯開多少，所以需要這一項。
- mono 相容性：$(L+R)/2$ 的 RMS 相對單一聲道 RMS ≈ −1.25 dB（±0.5），且不低於 −3 dB。先自己推導 −1.25 dB 這個數字從哪裡來。
- 和 `dawlab::dsp::Chorus`（1 voice、mix 1、`setStereoSpread(1)`）的 $\rho$ 相差 < 0.05。

延伸思考：白雜訊在 90° 和 180° 的 $\rho$ 都接近 0，那 stereo chorus 為什麼常用 90°？

## 練習 6 ★★☆（實作）Tempo-synced tremolo

起始碼：`exercises/starter/ex6_tempo_tremolo.cpp`

`TempoSyncTremolo` 包住本章的 `Tremolo`，每個 block 由「host」給你 block 開頭的歌曲位置 `ppq`（四分音符拍數）。完成兩個 TODO：

1. `getRateHz()`：$r = \text{bpm} / 60 / \text{beats}$。
2. `process()`：每個 block 開頭把 LFO 相位設成 $\operatorname{frac}(\text{ppq}/\text{beats} + \text{phaseOffset})$。

Self-check：

- `getRateHz()`：120 bpm 八分 → 4 Hz；附點八分 → 2.6667 Hz；90 bpm 四分 → 1.5 Hz。
- 輸入 DC 1.0（輸出 = gain 曲線）、sine、depth 1、phaseOffset 0.25：每個八分音符拍點 gain > 0.999、兩拍中間 gain < 0.001，2 秒內 8 個谷。
- 從 ppq = 3.3 開始播放，和從頭播放到同一位置的 gain 最大差 < $10^{-3}$。
- tempo 120 → 90 bpm 之後，谷的間距 = 0.3333 s（±2 ms）。

想一想：如果只設 rate、不在每個 block 重新對齊相位，從歌曲中間開始播放會發生什麼事？

## 練習 7 ★★★（觀念 + 計算）為什麼 phaser 的 notch 不是等間距？

1. Flanger 的 notch 等間距（間距 $1/d$），phaser 的不是。從「兩條路徑的相位差對頻率的關係」解釋原因。
2. 證明：$N$ 級、同一個 $f_c$ 的 phaser，在低頻近似 $\tan(\pi f/f_s) \approx \pi f/f_s$ 下，notch 成對滿足 $f_k \cdot f_{N/2-1-k} \approx f_c^2$。（提示：$\tan\theta \cdot \tan(\pi/2 - \theta) = 1$。）
3. 用範例 4 印出的 6 stages notch（268.3 / 1000.0 / 3665.4 Hz）驗證第 2 小題。為什麼乘積不是剛好 $10^6$？
4. 如果把 4 級 allpass 的 $f_c$ 設成**不同**的值（例如 300、600、1200、2400 Hz），總相位變成什麼？notch 數會變嗎？notch 位置大概往哪裡移？
5. 如果希望 phaser 的 notch「等間距」，有什麼辦法？代價是什麼？

第 4 小題的答案可以用練習 8 實際量出來驗證。

## 練習 8 ★★★（實作）Staggered phaser：每一級 allpass 有自己的 $f_c$

起始碼：`exercises/starter/ex8_staggered_phaser.cpp`

本章的 `Phaser` 讓所有 stage 共用同一個 $f_c$，notch 位置有封閉解（README 2.6）。這題寫一個 `StaggeredPhaser`：每一級有自己的基準 $f_c$，LFO 把所有 $f_c$ 乘上同一個比例 $2^{\text{depth}\cdot\text{octaves}\cdot m(t)}$（各級之間的比例不變）。每級 $f_c$ 不同時沒有封閉解，但總相位仍是各級相加：$\Phi(f) = \sum_i \varphi_i(f)$，從 0 單調降到 $-N\pi$，notch 在 $\Phi(f) = -(2k+1)\pi$。完成三個 TODO：

1. `theoryPhase(f, scale)`：$\sum_i \varphi_i(f)$，第 $i$ 級的 $f_c$ = scale × 基準 $f_c$（可直接用 `Phaser::allpassPhase`）。
2. `predictNotchHz(k, scale)`：在 $[1, f_s/2 - 1]$ Hz 之間用二分法解 $\Phi(f) = -(2k+1)\pi$。
3. `advanceModulation()`：算出這個 sample 的比例，替每一級設定**各自**的一階 allpass 係數（`Phaser::makeFirstOrderAllpass`）。

Self-check（mix 0.5、無 feedback、LFO 停住，掃頻 50 Hz–16 kHz 自動找 notch）：

- 4 級都設 1000 Hz：`predictNotchHz` 和 `Phaser::notchFrequencyHz` 的封閉解一致（414.7 / 2397.8 Hz，±0.01 Hz）；白雜訊經過 `StaggeredPhaser` 和 `dawlab::dsp::Phaser`（rate 0、depth 0、feedback 0、mix 0.5）的輸出逐 sample 相同（最大差 < $10^{-5}$）。
- 300 / 600 / 1200 / 2400 Hz：量到的 notch 仍是 **2 個**，位置和 `predictNotchHz` 相差 < 0.5%，深度 < −30 dB。
- 驗證練習 7 第 4 小題的預測：第一個 notch 比 4 × 1000 Hz 的 414.7 Hz 低、第二個比 2397.8 Hz 高。
- depth 1、octaves 1、LFO 停在 +1（rate 0、相位 0.25）→ 所有 $f_c$ 加倍，量到的 notch = `predictNotchHz(k, 2)`（±0.5%）。

輸出 `l10_ex8_uniform.wav`、`l10_ex8_staggered.wav`（白雜訊、rate 0.2 Hz、±1.5 個八度）。比較兩者掃動的聽感：notch 分得更開之後有什麼不同？
