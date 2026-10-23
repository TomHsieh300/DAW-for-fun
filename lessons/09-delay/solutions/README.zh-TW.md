# 第 09 章 解答：Delay 與 Echo

[English](README.md) | **繁體中文**

每一題都有完整解答。實作題的程式在本資料夾（`exN_xxx.cpp`，ctest label: solution），計算題 1、7 也有程式把數字實際跑一次：

```powershell
.\scripts\build.ps1 -Only 09-delay -TestLabel solution
cd build
.\bin\Debug\l09_ex1_solution.exe      # ex1、ex3、ex4、ex5、ex6、ex7
```

| 題號 | 類型 | 解答程式 | 輸出 WAV（`build\dawlab_output\`） |
|---|---|---|---|
| 1 | 計算 | `l09_ex1_solution`（`ex1_tempo_math.cpp`） | — |
| 2 | 觀念 | 第 2 小題的數字由 `l09_ex7_solution` 第 5 段驗證 | — |
| 3 | 實作 | `l09_ex3_solution`（`ex3_allpass_interpolation.cpp`） | `l09_ex3_allpass_noise.wav` |
| 4 | 實作 | `l09_ex4_solution`（`ex4_tape_delay.cpp`） | `l09_ex4_tape_dry.wav`、`l09_ex4_tape_wet.wav`、`l09_ex4_dub.wav` |
| 5 | 實作 | `l09_ex5_solution`（`ex5_multitap.cpp`） | `l09_ex5_multitap_dry.wav`、`l09_ex5_multitap_wet.wav` |
| 6 | 實作 | `l09_ex6_solution`（`ex6_crossfade_delay.cpp`） | `l09_ex6_crossfade.wav`、`l09_ex6_abrupt.wav`、`l09_ex6_ramp.wav` |
| 7 | 計算 + 觀念 | `l09_ex7_solution`（`ex7_comb_math.cpp`） | — |

---

## 練習 1：Tempo sync 與 echo 的衰減

公式：$t_\text{ms} = \text{beats} \times 60000 / \text{BPM}$，附點 × 1.5、三連音 × 2/3。

1. 120 BPM 一拍 = 500 ms：
   - 四分音符 = **500 ms**
   - 八分音符 = 0.5 × 500 = **250 ms**
   - 附點八分 = 0.75 × 500 = **375 ms**
   - 八分三連音 = (1/3) × 500 = **166.67 ms**
   - 十六分音符 = 0.25 × 500 = **125 ms**
2. 95 BPM 附點八分 = $0.75 \times 60000 / 95 = 473.684$ ms，在 48 kHz 下是 $473.684 \times 48 = $ **22 736.84 sample**。不是整數：要準確對齊 tempo 就需要讀「兩個 sample 之間」。（單一 echo 差 0.16 sample 聽不出來，但同樣的 delay line 也要支援 modulation 與平滑改變 delay time，見練習 2。）
3. - 140 BPM 四分三連音 = $(2/3) \times 60000/140 = $ **285.71 ms**
   - 90 BPM 附點四分 = $1.5 \times 60000/90 = $ **1000 ms**（剛好 1 秒）
   - 174 BPM 十六分音符 = $0.25 \times 60000/174 = $ **86.21 ms**（短到接近 slapback）
4. 第 $k$ 個 echo 的振幅是 $g^{k-1}$：1、0.5、0.25、0.125，即 **0、−6.02、−12.04、−18.06 dB**。（第一個 echo 是原始訊號直接延遲，還沒有經過 feedback。）
5. 每一圈衰減 $-20\log_{10} g$ dB，衰減 60 dB 要 $60/(-20\log_{10}g) = 3/(-\log_{10} g)$ 圈，每圈 $D$ 秒：

   $$T_{60} = \frac{3D}{-\log_{10} g}$$

   - $D = 0.375$、$g = 0.5$：$1.125 / 0.30103 = $ **3.737 s**（約 10 圈）
   - $D = 0.375$、$g = 0.7$：$1.125 / 0.15490 = $ **7.263 s**
   - $D = 0.25$、$g = 0.9$：$0.75 / 0.045757 = $ **16.39 s**——feedback 90% 的 echo 會拖很久，這就是為什麼 delay 的 feedback 旋鈕在最後 10% 特別「敏感」。
6. $g = 10^{-3D/T_{60}} = 10^{-3 \times 0.5/2} = 10^{-0.75} = $ **0.178**。

`l09_ex1_solution` 驗證：`noteToMs()` 的 9 個例子、impulse 經過 `Delay`（375 ms、fb 0.5、mix 1、filter 關閉）的前 8 個 echo 振幅（1、0.5、0.25……誤差 < 10⁻⁴，位置剛好在 $kD$）、三個 $T_{60}$ 與反算的 $g$，以及 `setFeedback(1.5)` 被 clamp 成 0.99。

## 練習 2：為什麼 feedback ≥ 1 會爆、為什麼需要插值

1. **Impulse response 的觀點**：一個 impulse 進去，輸出是 $D$ 處的 1、$2D$ 處的 $g$、$3D$ 處的 $g^2$……第 $k$ 個 echo 是 $g^{k-1}$。$|g| < 1$ 時幾何級數收斂，echo 越來越小；$|g| > 1$ 時每一圈都變大，任何輸入（包括極小的雜訊）最後都會長到無限大——在浮點數裡就是 Inf、然後 NaN。
   **Pole 的觀點**：$1 - g z^{-D} = 0 \Rightarrow z^D = g$，$D$ 個 pole 平均分布在半徑 $|g|^{1/D}$ 的圓上。穩定（BIBO）⇔ 全部 pole 在單位圓內 ⇔ $|g| < 1$。
   **$g = 1$**：pole 剛好在單位圓上，是「臨界穩定」（marginally stable）——impulse response 永遠不衰減（每 $D$ 個 sample 一個振幅 1 的 echo），但不會變大。可是它不是 BIBO 穩定：一個頻率剛好是 $k f_s/D$ 的正弦輸入會讓輸出線性成長到無限大（共振）。實務上等於不穩定，所以 `Delay` 把 feedback 限制在 0.99。
2. One-pole lowpass 的 $|H(f)| \le 1$（DC 處 = 1，其他頻率 < 1），迴路增益 $|g H(f)| \le 0.99 < 1$，**仍然穩定**。換成 Q = 4 的 biquad lowpass，cutoff 附近的增益約 $Q \approx 4$（+12 dB），迴路增益 $0.5 \times 4 = 2 > 1$：在 cutoff 附近、而且頻率剛好落在 $k f_s/D$ 的成分會每圈放大，**不穩定**。`l09_ex7_solution` 實際量：RBJ lowpass（fc = 1 kHz、Q = 4）在 fc 處 +12.04 dB（$= 20\log_{10}Q$），最大值 +12.11 dB @ 984 Hz，$g = 0.5$ 時迴路增益最大 2.02；把它和 $D = 240$（comb 的峰在 $k \times 200$ Hz，1000 Hz 剛好落在共振上）接成迴路、打一個 impulse，3 秒內就溢位成 inf。對照組 one-pole LP、$g = 0.99$ 在 2.5–3.0 s 的 peak 只剩 $3.07\times10^{-5}$。結論：迴路穩定的條件是「所有頻率」的迴路增益都 < 1，$|g| < 1$ 只有在迴路裡的其他東西增益都 ≤ 1 時才充分。
3. $\tanh$ 是有界的（$|\tanh u| < 1$），而且「斜率」隨振幅變小：小訊號時 $\tanh u \approx u$，迴路增益 1.1，echo 變大；echo 越大，tanh 壓得越多，等效增益越小。振幅會停在「等效（基頻）迴路增益剛好 = 1」的那一點。輸出振幅由 feedback、迴路 filter 在振盪頻率的增益與 drive 決定（練習 4 用 describing function 算出來）。輸出不會發散，但也不會自己消失——這就是 self-oscillation。
4. 至少三個情境：
   - **Modulation（chorus、flanger、vibrato）**：delay time 每個 sample 都在變。整數 delay 會讓讀取位置一格一格跳，每一跳都是波形的斷層 → 一連串細碎的 click / zipper noise，而不是平滑的 pitch 起伏。
   - **平滑改變 delay time**：使用者轉旋鈕時，平滑後的 delay 幾乎都不是整數；同樣會聽到 zipper。
   - **短 delay 的音高（comb、Karplus-Strong、physical modeling）**：音高是 $f_s/D$，整數 $D$ 只能做出離散的音高，高音時誤差很大（44.1 kHz 下 A4 只能做出 441 或 436.6 Hz，見練習 7）。
   - **Tempo sync**：delay 是 22 736.84 sample 而不是整數；誤差小，但在 feedback 迴路中每圈都相同方向偏移。
5. **對的情況**：delay time 固定不變、沒有 modulation、delay 夠長（> 數十 ms）的 echo。半個 sample 是 0.01 ms，耳朵完全分辨不出來，而整數讀取還沒有任何插值的高頻損失，其實音質最好。
   **錯的情況**：只要 delay time 會**變**——automation、使用者轉旋鈕、tempo 改變、LFO modulation——整數 delay 就會產生階梯狀的跳動與 click；以及短 delay 需要準確音高的時候。一個通用的 delay 效果器無法預知使用者會不會轉旋鈕，所以一律用 fractional delay（本章 `Delay` 用 cubic），delay 固定在整數時 cubic 的係數剛好是 (0, 1, 0, 0)，不會損失任何東西。

## 練習 3：Allpass interpolation

關鍵程式（`ex3_allpass_interpolation.cpp`）：

```cpp
void setDelay(float delaySamples) noexcept {
    const float delay = std::max(0.5f, delaySamples);
    d_ = static_cast<int>(std::floor(delay - 0.5f));
    const float frac = delay - static_cast<float>(d_); // ∈ [0.5, 1.5)
    eta_ = (1.0f - frac) / (1.0f + frac);
}

float processSample(float x) noexcept {
    line_.push(0, x);
    const float v0 = line_.readInteger(0, d_);     // v[n]   = x[n − d]
    const float v1 = line_.readInteger(0, d_ + 1); // v[n−1] = x[n − d − 1]
    float y = eta_ * (v0 - y1_) + v1;              // y[n] = η(v[n] − y[n−1]) + v[n−1]
    if (std::abs(y) < 1.0e-20f) y = 0.0f;          // flush denormal
    y1_ = y;
    return y;
}
```

思路：

- 把 delay 拆成「整數部分 $d$（用 circular buffer 直接讀）」與「小數部分 $\Delta$（交給 allpass）」。一階 allpass 本身在低頻有 $(1-\eta)/(1+\eta)$ 的延遲，令它等於 $\Delta$ 就解出 $\eta = (1 - \Delta)/(1 + \Delta)$。
- 為什麼 $\Delta \in [0.5, 1.5)$：$\Delta \to 0$ 時 $\eta \to 1$，pole（$z = -\eta$）貼近單位圓，impulse response 拖很長；delay 改變時的 transient 也很長。把 $\Delta$ 往上平移 0.5，$\eta$ 就落在 $(-0.2, 1/3]$。
- 遞迴的 $y[n-1]$ 要 flush denormal。

實測（delay 10.5，$d = 10$、$\eta = 1/3$）：

| freq (Hz) | linear (dB) | cubic (dB) | allpass (dB) |
|---|---|---|---|
| 1000 | −0.02 | −0.00 | −0.00 |
| 8000 | −1.25 | −0.23 | +0.00 |
| 12000 | −3.01 | −1.07 | −0.00 |
| 20000 | −11.74 | −8.41 | +0.00 |

相位延遲（sample）：linear 全頻都是 10.500（$\Delta = 0.5$ 時是對稱 FIR，線性相位）；allpass 在 100 Hz 是 10.500、4 kHz 10.509、8 kHz 10.537，與理論完全一致。delay 10.0 到 11.0 各種小數部分在 100 Hz 量到的延遲都精準到 0.001。

適用場合：

- **linear**：最便宜。適合 delay time 不太變、或對高頻不挑剔的地方；不適合 modulation（高頻衰減量會跟著 $\Delta$ 起伏）。
- **cubic**：多 2 次讀取、十幾次乘加。8 kHz 以下幾乎平坦、沒有狀態，可以任意快速地改變 delay → chorus、flanger、vibrato、本章 `Delay` 的預設。
- **allpass**：幅度完全平坦、計算量最低（一次乘法），但有狀態、延遲隨頻率變化。適合固定或極慢變化的 fractional delay：Karplus-Strong / waveguide 的調音、reverb 內部的 delay。

## 練習 4：Tape-style delay

關鍵程式：

```cpp
float feedbackPath(int ch, float wet) noexcept {
    float v = highpass_.processSample(ch, wet);
    v = lowpass_.processSample(ch, v);
    if (saturate_)
        v = std::tanh(drive_ * v) / drive_;   // 小訊號 ≈ v；|v| 大時被壓到 ±1/drive
    return feedback_ * v;
}
```

（`Saturation::shape(SaturationCurve::Tanh, drive_ * v) / drive_` 是等價的寫法。）

**(a) 每一圈都更暗。** 第一個 echo 直接從 delay line 出來，沒有經過迴路裡的 filter（實測峰值 1.000，位置 n = 9600）。第二個 echo 是第一個 echo 經過 HP → LP → × g，所以兩者的比值就是迴路的頻率響應 $g\,|H_{HP}(f)|\,|H_{LP}(f)|$。量法：用 `goertzelAmplitude` 在以 echo 為中心、長度 $D$ 的視窗量 500 Hz 與 8 kHz 的成分。實測 500 Hz：0.665（理論 0.666）、8 kHz：0.177（理論 0.178）。理論值由 `getLoopGainAt()` 用數位 `OnePole` 的實際頻率響應算出；若用類比 one-pole 的公式 $1/\sqrt{1+(f/f_c)^2}$ 近似，8 kHz 會得到 0.170，高頻差約 4%。高頻每一圈只剩 18%，低頻還有 66%——echo 一次比一次暗。

**(b) feedback 1.1。** 迴路的線性增益（200 Hz 處）是 $1.1 \times |H_{HP}(200)| \times |H_{LP}(200)| \approx 1.1 \times 0.995 \times 0.995 \approx 1.089 > 1$（20 Hz 的 HP 仍在迴路裡，200 Hz 處影響很小但不是 0）。線性版本 2.5–3.0 秒的 peak 已經長到 15.36 而且還在長；tanh 版本整體 peak 1.02、尾巴穩定在 0.305。

**預測振幅：describing function。** 迴路裡繞的近似是一個正弦。設 tanh 的輸入是 $X\sin\theta$，輸出的基頻振幅是

$$
F_1(X) = \frac{2}{\pi}\int_0^\pi \tanh(X\sin\theta)\sin\theta\,d\theta
$$

小訊號時 $F_1(X) \approx X$，大訊號時趨近 $4/\pi$。繞一圈的基頻增益 $= G \cdot F_1(X)/X$（$G$ = 線性迴路增益）。穩定振盪 ⇔ 這個增益 = 1 ⇔ $F_1(X)/X = 1/G$。$F_1(X)/X$ 從 1 單調遞減，用二分法解出 $X$，迴路訊號（= wet）的峰值就是 $\text{fb}\cdot\tanh(X)/\text{drive}$。程式算出 0.298，實測 0.305（差異來自 3 次諧波也在迴路裡繞，並不是純正弦）。

**問題：為什麼先乘 drive 再除以 drive？** $\tanh(\text{drive}\cdot v)/\text{drive}$ 在小訊號時 ≈ $v$，迴路的小訊號增益**和 drive 無關**，echo 的衰減時間只由 feedback 決定；drive 只決定「從多大的振幅開始被壓」（約 $1/\text{drive}$）。若不除回去，加 drive 等於同時把 feedback 乘上 drive，echo 會突然變長、甚至自激。
**drive 越大，自激振幅越小**：解 $F_1(X)/X = 1/G$ 得到的 $X$ 只和 $G$ 有關，峰值 $= \text{fb}\tanh(X)/\text{drive} \propto 1/\text{drive}$。實測（`l09_ex4_solution`）：drive 2 → 0.305（預測 0.298），drive 1 → 0.608（預測 0.595），兩者比值 2.00。

**(c) 音樂素材**：375 ms、fb 0.6、LP 2.5 kHz、HP 120 Hz、drive 1.5、mix 0.35。peak：dry 0.551、tape 0.402。該聽到：echo 越來越暗、越來越「遠」，比範例 2 的 ping-pong 更有類比的厚度；`l09_ex4_dub.wav` 是 200 Hz 的音只響 0.3 秒，之後迴路自己唱下去、振幅固定。

## 練習 5：Multi-tap delay

關鍵程式：

```cpp
for (int t = 0; t < numTaps_; ++t) {
    const TapState& s = state_[t];
    const float v = line_.readCubic(0, s.delaySamples - 1.0f); // 先讀再 push → −1
    wetL += s.gainL * v;
    wetR += s.gainR * v;
    last = v;                    // feedback 取最後一個 tap 的「原始」延遲訊號
}
line_.push(0, mono + feedback_ * last);
```

```cpp
void updateTap(int i) noexcept {
    s.delaySamples = std::clamp(t.delayMs * 0.001f * sampleRate, 2.0f, maxDelaySamples);
    panGains(t.pan, gl, gr);     // θ = (pan + 1)·π/4，gL = cos θ，gR = sin θ
    s.gainL = t.gain * gl;
    s.gainR = t.gain * gr;
}
```

思路：只有一條 delay line，每個 tap 只是一個讀取位置。pan 與 gain 合併成兩個係數在 setter 裡預先算好，`process()` 裡只有乘加。

實測（impulse，L = R = 1 → mono 1）：

| tap | delay | sample | gain | pan | L（預期 / 量到） | R（預期 / 量到） |
|---|---|---|---|---|---|---|
| 0 | 375 ms | 18000 | 0.60 | −0.6 | 0.5706 / 0.5706 | 0.1854 / 0.1854 |
| 1 | 500 ms | 24000 | 0.45 | +0.6 | 0.1391 / 0.1391 | 0.4280 / 0.4280 |
| 2 | 750 ms | 36000 | 0.30 | 0.0 | 0.2121 / 0.2121 | 0.2121 / 0.2121 |

總能量 0.6525 = 0.6² + 0.45² + 0.3²（constant-power：$g_L^2 + g_R^2 = 1$）。feedback 0.5 時，tap0 在 36000 + 18000 = 54000 再出現一次，振幅 0.2853 = 0.5 × 0.5706。

**問題：multi-tap vs 三台 delay 串接。**

- **echo 的時間位置**：multi-tap 的 echo 就在你指定的 $D_0, D_1, D_2$；串接的話第二台的 echo 是在第一台輸出的「再延遲」，會出現 $D_1$、$D_2$、$D_1 + D_2$、……各種組合（再加上各自的 feedback），節奏很快就變得混亂。
- **CPU / 記憶體**：multi-tap 一條 delay line + N 次讀取；串接是 N 條 delay line（記憶體 N 倍）。
- **插值的高頻損失**：multi-tap 每個 echo 只經過一次插值；串接時訊號經過幾台就被插值幾次，高頻損失累積（`l09_ex5_solution` 實測：一次 cubic @ $\Delta = 0.5$ 在 12 kHz 是 −1.07 dB，三次串接是 −3.22 dB）。

## 練習 6：用 crossfade 切換 delay time

關鍵思路（完整程式見 `ex6_crossfade_delay.cpp`）：

- 狀態：`headDelay_[2]`、`active_`（目前在用的頭）、`fadePos_`（−1 = 沒有在 fade）、`pending_`。
- `setDelayMs()`：正在 fade → `pending_ = true`；否則把 `headDelay_[1 − active_]` 設成新值、`fadePos_ = 0`。
- `process()` 每個 sample：$t = \text{fadePos}/\text{fadeLength}$，$g_B = \tfrac12(1 - \cos\pi t)$，$g_A = 1 - g_B$，`wet = gA·readCubic(headA) + gB·readCubic(headB)`；fade 走完就 `active_ = 1 − active_`、`fadePos_ = −1`，若 `pending_` 而且目標和目前不同就馬上開始下一次 fade。
- 兩個讀取頭的 delay 都**固定**，所以各自沒有 Doppler；切換是兩段訊號的 crossfade，而 raised-cosine 的權重是平滑的，所以沒有 click。
- `reset()` 要把兩個頭直接對齊目前的目標值（buffer 已清空，不需要 fade），否則 `prepare() → setDelayMs() → reset()` 這種常見順序會丟掉還沒開始的 fade、停在舊的 delay。
- 實務細節：測試把 block 在切換點（48000，不是 512 的倍數）切開，模擬 sample-accurate 的 automation。

實測（440 Hz，t = 1 s 時 10 → 250 ms，crossfade 20 ms）：

```
Largest adjacent-sample difference within 100 ms after the switch: crossfade 0.0288, direct jump 0.2701
Frequency measured over ±50 ms around the switch: crossfade 444.2 Hz (expected 440)
Delay smooth transition (20 ms ramp), measured during the ramp: 4840.8 Hz (d(tau)/dt = 12 -> theoretical |f_out| = 4840 Hz, a negative sign means reverse playback)
Largest error between y[n] and x[n - 12000] after the fade ends: 0.00e+00
Changing the delay again mid-fade (100 -> 300 ms): it finally settles at 300 ms, error 1.72e-13
```

444 Hz 而不是 440 Hz 的原因見下一段的相位分析；和平滑過渡的 4840 Hz（倒放）相比，可以說完全沒有 Doppler。

**問題：fade 中間的振幅。** 兩個頭相差 240 ms = 105.6 個週期，相位差 0.6 週期 = 216°（等效 −144°）。合成訊號是 $g_A e^{j0} + g_B e^{j\varphi}$，振幅 $\sqrt{1 - 2g_Ag_B(1 - \cos\varphi)}$，在 $g_A = g_B = 0.5$ 時最小：$|\cos(\varphi/2)| = |\cos 108°| = $ **0.309**，也就是原本振幅的 31%（−10.2 dB）。`l09_ex6_solution` 在 fade 中點量到 peak 0.1547（原振幅 0.5），比值 0.309，與理論一致；合成訊號的相位也在這 20 ms 內從 A 滑到 B（約 0.4 週期），攤在 100 ms 的量測窗裡就是 +4 Hz 的偏差。
真實音樂素材中，兩個頭讀到的是 240 ms 前後兩段**不相關**的內容，平均來說功率是 $g_A^2 + g_B^2$，中間點是 0.5（−3 dB），而且只持續 20 ms，通常聽不出來。若要更平順，可以改用 equal-power crossfade（$g_A = \cos(\pi t/2)$、$g_B = \sin(\pi t/2)$），但它在兩頭**相關**時（例如很小的 delay 改變）中間會凸起 +3 dB——取捨和第 01 章的 pan law 一樣。解答程式把白雜訊（白雜訊在不同延遲下彼此不相關）送進真正的 `CrossfadeDelay`，從 10 ms 切到 250 ms；為了讓量測窗內 $g_A, g_B$ 幾乎固定在 0.5，把 crossfade 拉長到 1 秒、只量中點 ±20 ms 的 RMS，再用 8 個 seed 平均。equal-power 那一行只是算式對照（本練習沒有實作 equal-power，所以不算 check）：

```
Level at the crossfade midpoint (relative to the input, measured on CrossfadeDelay):
  raised cosine (0.5 + 0.5), uncorrelated material: -2.98 dB (theory 10·log10(0.5² + 0.5²) = -3.01)
  (Formula comparison) equal-power midpoint gA = gB = cos45°: uncorrelated +0.00 dB, correlated +3.01 dB
```

## 練習 7：Comb filter

1. Feedback comb，$D = 240$、$g = 0.8$、$f_s/D = 200$ Hz：
   - 峰在 $k \times 200$ Hz（0、200、400……），$|H| = 1/(1 - 0.8) = 5$，**+13.98 dB**。
   - 谷在 $(k + \tfrac12) \times 200$ Hz（100、300、500……），$|H| = 1/1.8 = 0.556$，**−5.11 dB**。
   - `l09_ex7_solution` 在 200、400、1000、5000 Hz 量到 +13.98 dB，在 300、500、1100、5100 Hz 量到 −5.11 dB。
2. Feedforward comb：$|H|^2 = 1 + g^2 + 2g\cos(\omega D)$。峰在同樣的 $k \times 200$ Hz，$1 + 0.8 = 1.8$，**+5.11 dB**；谷在 $(k+\tfrac12) \times 200$ Hz，$1 - 0.8 = 0.2$，**−13.98 dB**。和 feedback comb 剛好「上下顛倒」：feedforward 是深而窄的谷，feedback 是高而窄的峰。
3. $g = -0.8$：$-2g\cos(\omega D)$ 變號，峰移到 $\cos(\omega D) = -1$ 的位置，即 **100、300、500 Hz……**（$f_s/2D$ 的奇數倍），谷移到 200、400 Hz。實測 100/300/500 Hz +13.98 dB、200/400 Hz −5.11 dB。聽感：基頻從 200 Hz 掉到 100 Hz（低八度），而且只有奇次諧波，音色像一端封閉的管子（類似 clarinet 那種空洞感）。
4. $D = 44100/440 = $ **100.227 sample**。整數選擇：
   - $D = 100$ → $441.0$ Hz，$1200\log_2(441/440) = $ **+3.9 cents**
   - $D = 101$ → $436.63$ Hz，$1200\log_2(436.63/440) = $ **−13.3 cents**（明顯走音）
   - 實測：整數 100 峰在 441.0 Hz、101 峰在 436.6 Hz、用 cubic 的 100.227 峰在 440.0 Hz。（程式掃描峰值的解析度是 0.1 Hz；拿四捨五入後的 436.6 Hz 去算 cents 會得到 −13.4，所以 `l09_ex7_solution` 的 cents 用理論頻率 $f_s/D$ 計算。）音越高，$D$ 越小，整數誤差的 cents 越大——這就是 Karplus-Strong 需要 fractional delay（通常用 allpass，練習 3）的原因。
5. Feedback comb 的峰排在 $f_0, 2f_0, 3f_0, \ldots$（$f_0 = f_s/D$），這正是一個週期性訊號的諧波結構。白雜訊經過它，能量集中在這些諧波附近，耳朵就聽到基頻 $f_0$ 的音高。從時域看：輸出 = 輸入 + 每 $D$ 個 sample 重複一次的回聲，越來越接近週期為 $D$ 的訊號。`makePluck()` 就是一個 feedback comb：ring buffer 長度 = $D$ = 週期，迴路裡用「相鄰兩點平均 × 0.996」當作 lowpass + 衰減，所以高頻衰減得比低頻快，聽起來像撥弦（Karplus-Strong）。
6. 單一 comb 的 echo 間距固定，在時域上是「噠、噠、噠」的週期回聲（flutter echo），在頻域上是等間距的峰（一個明確的音高）——很金屬。好幾個 comb 並聯，echo 的密度變成好幾倍；delay time 選**互質**，各 comb 的 echo 幾乎不會在同一時刻重疊、頻率響應的峰也幾乎不會落在同一頻率，峰的分布更密、更不規則，耳朵就不再聽到單一音高，而是像房間的殘響。再串幾個 allpass 把每個 echo 打散（增加 echo 密度但不改變整體頻譜），就是 Schroeder reverb。每個 comb 的 $g$ 用練習 1 的 $T_{60}$ 公式決定，第 12 章會照這個思路做。
