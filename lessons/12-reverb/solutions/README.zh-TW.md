# 第 12 章 解答：Reverb

[English](README.md) | **繁體中文**

題目見 [`../exercises/README.zh-TW.md`](../exercises/README.zh-TW.md)。實作題的完整程式在本資料夾，每支都有 self-check，執行後最後一行應該是 `[PASS]`：

```powershell
.\scripts\build.ps1 -Only 12-reverb -TestLabel solution
```

| 題號 | 解答檔 | target |
|---|---|---|
| 3 | `ex3_early_reflections.cpp` | `l12_ex3_solution` |
| 4 | `ex4_fdn_modulation.cpp` | `l12_ex4_solution` |
| 5 | `ex5_gated_reverb.cpp` | `l12_ex5_solution` |
| 6 | `ex6_rt60_estimator.cpp` | `l12_ex6_solution` |

---

## 練習 1：comb 的 feedback 與 RT60

**1.** D = 1557、fs = 48000：

$$
g = 10^{-3\cdot1557/(2.0\cdot48000)} = 10^{-0.048656} = 0.89401
$$

RT60 = 0.5 s：$`g = 10^{-3\cdot1557/24000} = 10^{-0.194625} = 0.63881`$。

兩個值都和範例 1（`l12_comb_rt60`）印出的 g 一致。RT60 縮短成 1/4，指數也變 4 倍：$`0.63881 = 0.89401^4`$。

**2.** 44.1 kHz、g = 0.84，$`\log_{10}0.84 = -0.075721`$：

$$
\mathrm{RT60}_{1116} = \frac{-3\cdot1116}{44100\cdot(-0.075721)} = 1.003\ \text{s},\qquad
\mathrm{RT60}_{1617} = \frac{-3\cdot1617}{44100\cdot(-0.075721)} = 1.453\ \text{s}
$$

同一個 g 下，RT60 與 D 成正比：最長的 comb 比最短的多 45%。所以 Freeverb 的 roomsize 只是「feedback 大小」，不是嚴格的 RT60：整體衰減的前段由所有 comb 一起決定，後段被最長（衰減最慢）的那幾條主導，衰減曲線會微微「下凹」（前快後慢）。要每條 comb 都同一 RT60，就要各自用 $`g_i = 10^{-3D_i/(\mathrm{RT60}f_s)}`$，這正是 FDN（`Reverb.h`）的做法。

**3.** $`\omega = 2\pi f/f_s`$，$`|\mathrm{LP}| = 0.5/\sqrt{1.25 - \cos\omega}`$，$`\mathrm{RT60} = -3\cdot1557 / (48000\log_{10}(0.89401\,|\mathrm{LP}|))`$：

| f | ω | $`\lvert\mathrm{LP}\rvert`$ | $`g\lvert\mathrm{LP}\rvert`$ | RT60 手算 | 範例量到 |
|---|---|---|---|---|---|
| 986.5 Hz | 0.1291 | 0.9838 | 0.8795 | 1.745 s | 1.745 s |
| 4007.7 Hz | 0.5246 | 0.8063 | 0.7209 | 0.685 s | 0.685 s |
| 8015.4 Hz | 1.0492 | 0.5767 | 0.5156 | 0.338 s | 0.339 s |

（範例取的是最接近 1 k / 4 k / 8 kHz 的 comb 諧波 $`k\cdot f_s/D`$，避免落在 notch 上。）d = 0.5 的 one-pole 在 8 kHz 只剩 0.58 倍，每圈都乘一次，所以高頻的 RT60 掉到低頻的 1/6。

---

## 練習 2：為什麼 delay 長度要互質

**1.** 1500 samples 的 comb，mode 在 $`48000/1500 = 32`$ Hz 的整數倍；2000 samples 的在 24 Hz 的整數倍。兩者共同的頻率是 lcm(32, 24) = 96 Hz 的整數倍 —— 也就是 $`f_s/\gcd(1500, 2000) = 48000/500`$。96、192、288 Hz……這些頻率兩個 comb 同時共振。

時域上，第一個 comb 的 echo 在 1500 的倍數、第二個在 2000 的倍數，兩者在 lcm(1500, 2000) = 6000 samples（125 ms）的倍數同時抵達，每 125 ms 一次「加倍大」的 echo → 規律的 flutter。

**2.** 1427 與 1783 互質：共同頻率只有 $`f_s/\gcd = 48000`$ Hz 的倍數，也就是 DC（與它在 fs 的週期副本），音訊頻帶內沒有重疊的 mode。echo 同時抵達的週期是 1427 × 1783 = 2,544,341 samples ≈ 53 秒，遠超過任何 RT60，等於不會發生。

**3.** 重疊的 mode 是兩條（或更多）路徑同時共振：那個頻率的峰值比別的 mode 高、而且一起衰減，在長尾巴裡會冒出一個「有音高的音」。耳朵對這種窄頻、慢衰減的共振非常敏感，聽起來像敲金屬管（metallic ringing）。其他減輕方法：
- 增加 delay 線數或總長度 → mode 密度更高，個別 mode 不突出；
- 用混合更徹底的回授矩陣（FDN）與 input diffusion；
- delay modulation（練習 4）讓 mode 頻率持續移動；
- damping：高頻的 ringing 衰減更快；
- 長度不要成簡單整數比、不要擠在一起（例如本章分布在 30–73 ms）。

**4.** 充分但不必要。只要兩兩的 gcd = 1 就夠了，不需要每個都是質數。例如 1024 (= 2¹⁰)、1125 (= 3²·5³)、1331 (= 11³)、1729 (= 7·13·19)：沒有任何兩個共用質因數，所以兩兩互質。很多經典設計就是用「不同質數的次方」來挑長度。取 `nextPrime()` 只是最省事的保證方法。

---

## 練習 3：early reflections（`ex3_early_reflections.cpp`）

**關鍵思路**

1. **Image source**：對牆面 x = 0 鏡射是 $`x \to -x`$，對 x = Lx 鏡射是 $`x \to 2L_x - x`$，其他軸同理。一階反射 = 6 個 image。
2. **相對直達音**：tap 的 delay 是「多走的路程 / c」，gain 是 $`\sqrt{1-\alpha}`$（能量吸收 α → 振幅反射係數）乘上距離比（1/r 衰減）。用「相對直達音」的好處是 ER 模組不需要知道直達音的絕對延遲。
3. **Stereo**：左右耳各算一組 tap（相差 16 cm），兩組 tap 的時間差就是雙耳時間差，讓 ER 有方向感。
4. **ER + late**：late reverb 的 pre-delay 設成兩耳中最晚的一個 ER（`er.getMaxDelaySamples()`：右耳 x = 0 那面牆，538 samples ≈ 11.2 ms；單一聽者位置時是 536），尾巴接在 ER 後面開始。

**手算地板反射**（listener (4.3, 2.6, 1.2)、source (2, 1.5, 1.6)）：

- 直達：$`\sqrt{2.3^2 + 1.1^2 + 0.4^2} = \sqrt{6.66} = 2.5807`$ m
- 地板 image (2, 1.5, −1.6)：$`\sqrt{2.3^2 + 1.1^2 + 2.8^2} = \sqrt{14.34} = 3.7868`$ m
- delay = (3.7868 − 2.5807) / 343 = 3.516 ms → × 48 = 168.8 → **169 samples**
- gain = $`\sqrt{0.7}\times 2.5807/3.7868 = 0.8367\times0.6815 =`$ **0.570**

全部 6 個（依 delay 排序；與程式輸出一致）：

| 牆面 | image | 距離 (m) | delay (ms) | delay (samples) | gain |
|---|---|---|---|---|---|
| 地板 z = 0 | (2, 1.5, −1.6) | 3.7868 | 3.516 | 169 | 0.5702 |
| 天花板 z = 3 | (2, 1.5, 4.4) | 4.0915 | 4.405 | 211 | 0.5277 |
| 牆 y = 4 | (2, 6.5, 1.6) | 4.5453 | 5.728 | 275 | 0.4750 |
| 牆 y = 0 | (2, −1.5, 1.6) | 4.7181 | 6.231 | 299 | 0.4576 |
| 牆 x = 6 | (10, 1.5, 1.6) | 5.8189 | 9.441 | 453 | 0.3711 |
| 牆 x = 0 | (−2, 1.5, 1.6) | 6.4078 | 11.158 | 536 | 0.3370 |

（程式印的 ms 是由取整後的 samples 換回來的，所以會差一點點，例如 169 samples = 3.521 ms。）

**驗證**：TappedDelay 的 impulse response 恰好 6 個非零 sample，位置與 gain 都符合（誤差 < 10⁻⁶）；第 0 個 sample 為 0（不含直達音）。處理 pluck sequence 後 RMS：dry 0.1034 → dry + ER 0.1538 → dry + ER + late 0.1615，peak 0.991（< 1）。

**聽感**：`l12_ex3_er_only.wav` 只多了 6 個很早的反射，聲音變得「在房間裡、離你一點距離」，但沒有尾巴；`l12_ex3_er_late.wav` 才有完整的空間。延伸：加入二階反射（對 image 再鏡射，共 18 個）會更真實；把每個 tap 加上依距離的 lowpass（空氣吸收）也有幫助。

---

## 練習 4：FDN 的 modulation 與 metallic ringing（`ex4_fdn_modulation.cpp`）

**關鍵思路**

1. **先讀後寫的 −1**：`FractionalDelayLine` 的慣例是 push 之後 `read(0)` 是剛寫入的值。在 `processSample()` 裡我們先讀、最後才 push，所以讀 `L − 1` 才得到 L 個 sample 前寫入的值，迴路長度正好是 L，$`g_i`$ 公式才精確。
2. **Modulation**：每條線的 LFO 相位差 90°（`i·π/2`），避免 4 條線「一起伸縮」造成整體音高晃動；用 `readCubic` 讓非整數 delay 的高頻損失比 linear 少。
3. **Damping 要能真正關掉**：就算 cutoff = 20 kHz，one-pole 在 Nyquist 仍有 −1.3 dB，每圈都乘一次，高頻 RT60 會被明顯縮短。所以 `setDampingHz(≥ 20000)` 時直接 bypass。如果不 bypass，這個 4 線 FDN 量到的 RT60 只有約 1.66 s 而不是 2 s。
4. **Roughness 指標**：mode 分明的頻譜像一排尖峰（峰谷差很大），dB 的標準差大；mode 被抹平後，頻譜像雜訊，dB 標準差趨近白雜訊 magnitude（Rayleigh 分布）在 dB 上的理論值約 5.6 dB。

**實際輸出**

```
Delay-line lengths (samples): 1123, 1567, 1949, 2251 (all prime)
lossless: RMS @1.0s = 0.0390, @2.5s = 0.0391 (difference +0.01 dB, should be close to 0)
RT60 set to 2.0 s -> measured 2.021 s
Std. dev. of the tail spectrum in dB (from 0.4 s, 16384-point FFT, 100 Hz..6 kHz): static 8.85 dB, modulated 5.89 dB
Measured RT60 of the modulated version: 1.863 s
depth 4 ms: spectrum std. dev. 5.60 dB (flatter, but the maximum pitch deviation is 3.3% = 56 cents -> audible pitch wobble)
```

**思考題**

- 4 線 FDN 的 mode 密度 = (1123 + 1567 + 1949 + 2251) / 48000 = 0.14 個 / Hz（平均每 7 Hz 一個 mode）；8 線的 `Reverb` 是 0.41 個 / Hz（每 2.5 Hz 一個）。mode 越稀疏，每個 mode 越容易被單獨聽出來。依 Schroeder 與 Logan 的「mode 密度 ≥ 0.15 × RT60」經驗法則（見 README 2.6 節），RT60 = 2 s 至少要 0.3 個 / Hz，4 線版本明顯不足 —— modulation 正好補救這點。
- Lossless 測試證明 Householder 矩陣是正交的：沒有 $`g_i`$ 與 damping 時，系統能量 1.5 秒內完全不變。如果把矩陣寫錯（例如 1/N 而不是 2/N），這個測試會立刻看到 RMS 持續上升或下降。
- 調變 delay 會造成 Doppler 式的 pitch 變化：瞬時頻率比 $`f'/f = 1 - dL/dt`$（L 以秒計），正弦調變 $`L(t) = L_0 + D\sin(2\pi f_{\mathrm{LFO}}t)`$ 的最大偏移是 $`2\pi f_{\mathrm{LFO}}D`$。depth 1 ms、1.3 Hz → 0.82%（約 14 cents，幾乎聽不出來）；depth 4 ms → 3.3%（約 56 cents，超過四分之一全音），尾巴聽起來會「晃」，像 chorus 開太深。modulation 要在「抹平 ringing」與「聽不出 pitch 晃動」之間取捨，常見是 0.3–1 ms、0.3–2 Hz。
- Modulation 讓量到的 RT60 稍微變短（1.86 s）。可能原因之一：cubic 插值在非整數位置有一點高頻損失（整數位置則完全無損），每繞一圈累積一次；另外單次 IR 的 RT60 量測本身也有幾 % 的起伏。這也是為什麼有些 reverb 設計只調變少數幾條線、或改用 allpass 插值。

---

## 練習 5：gated reverb（`ex5_gated_reverb.cpp`）

**關鍵思路**

- Reverb 本身照常（RT60 3 s，mix 1，純 wet），在它**後面**放 gate，**sidechain 用 dry snare**。每一擊 snare 都讓門瞬間打開（attack 0.5 ms），維持 hold 100 ms，然後 30 ms 內關到 −80 dB。
- 為什麼不用 reverb 自己的輸出當偵測訊號：reverb 的尾巴是慢慢衰減的，門會在「尾巴低於 threshold」才關 —— 關門時間取決於 reverb 音量、RT60 與 threshold 的交互作用，很難控制，也不會有「突然被砍斷」的效果。用 dry 偵測，門的開關時間只跟鼓擊有關，hold 就直接決定尾巴的長度。
- 記得在設好參數後 `reverb.reset()`：讓 mix = 1 從第一個 sample 起就生效，否則前 0.1 秒會漏出 dry。

**實際輸出**

```
window                  ungated(dB)    gated(dB)
open   0.02..0.10 s           -21.9        -21.9
closed 0.40..0.58 s           -31.2       -100.0
Final gate state: closed, gain reduction 80.0 dB
```

門開時完全不影響（差 0.0 dB）；門關之後沒有 gate 的尾巴只掉了約 9 dB（RT60 3 s → 每秒 20 dB），gated 版本則已經是 −100 dB（低於量測下限）。0.6 s 的下一擊門會再打開（self-check 驗證 0.62–0.70 s 兩者差 < 1.5 dB）。

**聽感**：`l12_ex5_reverb.wav` 的尾巴拖很長、把每一擊連成一片；`l12_ex5_gated.wav` 每一擊都「很大很寬」，但 100 ms 後戛然而止 —— Phil Collins〈In the Air Tonight〉那種 80 年代的鼓聲。

---

## 練習 6：Schroeder backward integration（`ex6_rt60_estimator.cpp`）

**關鍵思路**

```cpp
double acc = 0.0;
for (int i = n - 1; i >= 0; --i) {   // 從尾巴往回累加：EDC[i] = EDC[i+1] + h[i]²
    acc += double(h[i]) * h[i];
    edc[i] = acc;
}
// 再轉 dB：10·log10(edc[i] / edc[0])（能量用 10·log10）
```

回歸：只收 EDC_dB 在 [−25, −5] dB 之間的點，最小平方法 slope = (nΣty − ΣtΣy) / (nΣt² − (Σt)²)，RT60 = −60 / slope。

**為什麼 EDC 是直線**：若 $`h(t) = A e^{-kt}\cdot w(t)`$（w 是單位功率的雜訊或正弦），$`h^2`$ 的平均是 $`A^2 e^{-2kt}`$，

$$
\mathrm{EDC}(t) = \int_t^\infty A^2 e^{-2k\tau}d\tau = \frac{A^2}{2k}e^{-2kt}
\;\Longrightarrow\;
\mathrm{EDC}_{\mathrm{dB}}(t) = 10\log_{10}e^{-2kt} = -\frac{20k}{\ln 10}t
$$

RT60 的定義 $`e^{-k\cdot\mathrm{RT60}} = 10^{-3}`$ → $`k = 3\ln10/\mathrm{RT60}`$，代入得 $`\mathrm{EDC}_{\mathrm{dB}}(t) = -60\,t/\mathrm{RT60}`$：斜率正好是 −60/RT60，跟振幅 envelope 的 dB 斜率相同，但**平滑得多**。Schroeder 證明了：這個積分等於「用同一個房間做無限多次雜訊中斷量測再平均」的結果，所以一次 IR 就能得到 ensemble average。

**實際輸出**

```
EDC of the exponentially decaying sine: t=0.25 s -> -15.00 dB (theory -15), t=0.5 s -> -30.00 dB (theory -30)

IR                             truth(s)       T20(s)       T30(s) windowed-RMS(s)
decaying sine (RT60 1.0)          1.000        1.000        1.000          1.000
synthetic noise IR                0.500        0.502        0.501          0.501
synthetic noise IR                1.500        1.505        1.497          1.499
FDN Reverb (RT60 2.0)             2.000        1.991        1.998          2.004

EDC of the FDN IR (every 0.25 s):
  t = 0.00 s :    0.00 dB
  t = 0.25 s :   -6.24 dB
  t = 0.50 s :  -13.73 dB
  t = 1.00 s :  -28.75 dB
  t = 2.00 s :  -58.77 dB
  ...（節錄）
[PASS] l12_ex6 (11 checks passed)
```

T20 / T30 都是 Schroeder backward integration 的結果，只差回歸區段（−5..−25 dB 與 −5..−35 dB）。FDN 的 EDC 從 0.25 s 起每 0.25 s 掉約 7.5 dB（= 60 dB / 2 s × 0.25 s），是一條漂亮的直線；最前面 0–0.25 s 只掉 6.2 dB，因為那段是 echo 正在建立（build-up），能量還沒開始「純衰減」。

**背景雜訊的影響**：真實錄音的 IR 尾巴最後是固定位準的雜訊，不再衰減。backward integration 會把這段雜訊的能量也積進去 → EDC 的尾端「翹起來」變平，回歸區段若延伸到那裡，RT60 會被高估。實務做法（ISO 3382 系列與 Lundeby 等人的方法）：先估出 noise floor，把積分的上限截在 IR 碰到 noise floor 的時間點（並補償被截掉的能量），或在回歸前先從 $`h^2`$ 減掉雜訊功率。這也是為什麼常用 T20 / T30 而不是直接量到 −60 dB。

---

## 練習 7：partitioned convolution 的 latency 與成本

fs = 48 kHz、M = 3.0 × 48000 = 144000 samples。

**1.** P = 512：$`K = \lceil 144000/512\rceil = \lceil 281.25\rceil = 282`$。N = 1024，每個頻譜 N/2 + 1 = 513 個 bin。
IR 頻譜：282 × 513 × 8 = 1,157,328 bytes ≈ **1.10 MiB**（時域 IR 只要 576,000 bytes：頻譜存法大約是 2 倍，因為補零到 2P）。一個 channel 的 FDL 也是 282 個頻譜，同樣 1.10 MiB，合計約 2.21 MiB。

**2.** 每 P = 512 個輸出 sample 做 K × 513 次 complex 乘加：282 × 513 / 512 ≈ **282.6 次 complex MAC / sample**（≈ 1130 次實數乘法）。另外每 512 個 sample 做兩次 1024 點 FFT，radix-2 約 (N/2)·log₂N = 5120 個 butterfly，攤提下來每 sample 約 20 個 butterfly，相對很小。
直接卷積每 sample 144000 次乘加 → 144000 / 1130 ≈ **127 倍**（以實數乘法計；就是 README 的 P/4 ≈ 128）。

**3.** P = 64：K = 144000 / 64 = 2250，N = 128、65 個 bin。每 sample 2250 × 65 / 64 ≈ **2285 次 complex MAC**，是 P = 512 的 8 倍；latency 從 512 samples（10.67 ms）降到 **64 samples（1.33 ms）**。記憶體幾乎不變（2250 × 65 × 8 = 1,170,000 bytes ≈ 1.12 MiB）。結論：uniform partition 的 latency 與 CPU 成反比，無法兩全。

**4.** Latency 的來源：overlap-save 的每個 frame 需要**完整的 P 個新輸入**才能做 FFT。如果輸入逐 sample 到來，block 中第一個 sample 必須等到第 P 個 sample 到了才能被處理，它的輸出最早也只能在那時候產生 —— 為了讓輸出是連續、等間隔的串流，整體必須延遲 P 個 sample。

如果 host 每次剛好給 P = 512 個 sample、而且和 partition 邊界對齊：callback 開始時這 512 個輸入**都已經在手上**，可以立刻 FFT、乘加、IFFT，並在同一個 callback 裡把這 512 個 sample 的輸出寫回去 —— 額外 latency 是 0（「等湊滿」的時間已經包含在 host 本來就有的 buffer latency 裡）。

本章的 `ConvolutionReverb` 沒這麼做，因為 host 的 block 長度不保證固定（JUCE、VST3 都允許 callback 給比 maxBlockSize 小、甚至每次不同的 sample 數，DAW 在自動化或 loop 邊界常會切小塊）。用 FIFO 固定延遲 P，就能在任何 block 長度下得到正確結果，而且 latency 是固定值，方便 host 做延遲補償。代價是多了 P 個 sample 的 latency。

**5.** 混合 / non-uniform partition：
- IR 的最前面一段（例如前 64 或 128 個 sample）用**時域直接卷積**（FIR），零 latency、而且因為很短，CPU 很便宜；
- 接下來的部分用小 partition（例如 P = 128），其輸出本來就需要延遲 ≥ 128 才用得到，所以它的 latency 被「藏」在 IR 的時間偏移裡；
- 更後面用越來越大的 partition（1024、8192……），大 FFT 每個 sample 的成本低，而且有足夠的時間（可以分散到多個 callback 或背景 thread）去算。

這就是 Gardner（1995）的 zero-latency 方法的精神。總成本接近大 partition 的效率，latency 是 0。
