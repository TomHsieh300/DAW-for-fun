# 第 10 章 練習解答

[English](README.md) | **繁體中文**

題目見 [`../exercises/README.zh-TW.md`](../exercises/README.zh-TW.md)。實作題的完整程式在本資料夾，每一支都用 `DAWLAB_CHECK` / `DAWLAB_CHECK_NEAR` 以數字驗證，執行後印出 `[PASS] l10_exN (...)`。

```powershell
.\scripts\build.ps1 -Only 10-modulation -TestLabel solution
```

---

## 練習 1：Chorus、flanger、vibrato 的本質差異

1. **延遲範圍**：chorus 5–30 ms（本章預設 centre 15 ms ± 3 ms），flanger 0.5–10 ms（預設 1–3 ms）。
   - **頻域**：兩者都是「原音 + 延遲」= comb filter，梳齒間距 $`1/d`$。flanger 的 $`d = 1`$ ms → 間距 1000 Hz，notch 在 500、1500、2500 Hz……，稀疏、落在耳朵最敏感的頻段，掃動時整排 notch 一起滑，聽起來就是「音色在被濾波」。chorus 的 $`d = 15`$ ms → 間距 66.7 Hz，梳齒密到一個臨界頻帶（critical band）裡有好幾個峰谷，耳朵分辨不出個別的 notch，染色感很弱。
   - **音高**：走音量 $`2\pi r D`$ 只和 depth × rate 有關，但兩者的典型值不同：chorus 的 depth 較大、rate 中等（3 ms × 0.8 Hz → ±26 cents），走音明顯，主要感受是「另一個稍微走音的人」；flanger 的 rate 很低（0.1–0.3 Hz），走音只有幾 cents，主要感受是梳子在滑。
   - 一句話：**chorus 讓你聽到「兩個聲音」，flanger 讓你聽到「一個被濾波的聲音」**。分界大約在 10–15 ms，也就是延遲短到梳齒間距大於耳朵的頻率解析度的地方。
2. Feedback 在 $`f = k/d`$ 製造共振。flanger $`d = 1`$ ms 時共振在 1、2、3 kHz……，這種「有音高的金屬共鳴」正是 flanger 想要的聲音。chorus 若 $`d = 20`$ ms，共振在 50、100、150 Hz……——一個 50 Hz 的嗡嗡低音（像對著水管唱歌），而且 feedback 會讓 20 ms 的延遲重複好幾次，開始聽成快速的 flutter echo，完全破壞「自然的合奏」感。所以 chorus 幾乎不用 feedback。
3. **mix**。Vibrato = mix 1（只有延遲路徑）。沒有原音就沒有「兩條路徑相加」，$`|H| = |e^{-j\omega d}| = 1`$，沒有任何峰谷，只剩延遲變化造成的音高漂移（練習 3 的 self-check：輸出 RMS 與輸入差 < 0.3 dB）。
4. 不會。notch 處 $`z^{-D} = -1`$：$`|H| = |(1 - m) - m| = |1 - 2m|`$。mix 0.2 → $`|0.8 - 0.2| = 0.6`$ → $20\log_{10}0.6 = $ **−4.44 dB**；peak 處 $`|0.8 + 0.2| = 1`$（0 dB）。只有 mix 0.5（兩條路徑等量）才能完全抵消，這就是 flanger / phaser 的 mix 預設 0.5 的原因。

## 練習 2：把公式算過一遍

1. $`d = 0.5`$ ms：notch $`f_k = (2k+1)/(2d) = (2k+1) \times 1000`$ Hz → **1000、3000、5000 Hz**；peak $`f_k = k/d`$ → **0、2000、4000 Hz**。
2. 第一個 notch $`f_0 = 1/(2d)`$ ⇒ $`d = 1/(2f_0)`$：$`f_0 = 1000`$ Hz → $`d = 0.5`$ ms；$`f_0 = 200`$ Hz → $`d = 2.5`$ ms。所以 **`setDelayMs(0.5f)`、`setDepthMs(2.0f)`**（延遲在 0.5–2.5 ms 掃）。注意 notch 與 $`d`$ 成反比：延遲 5 倍、notch 頻率也是 5 倍（約 2.3 個八度）。
3. $`H_{\text{peak}} = (1-m) + m/(1-g)`$、$`H_{\text{notch}} = (1-m) - m/(1+g)`$，$`m = 0.5`$：
   - $`g = +0.7`$：peak $`0.5 + 0.5/0.3 = 2.167`$ → **+6.72 dB**；notch $`0.5 - 0.5/1.7 = 0.206`$ → **−13.73 dB**。
   - $`g = -0.7`$：原 peak 位置 $`0.5 + 0.5/1.7 = 0.794`$ → **−2.00 dB**；原 notch 位置 $`0.5 - 0.5/0.3 = -1.167`$ → $`|{-1.167}|`$ → **+1.34 dB**。峰谷對調（範例 3 量到的 −0.7 那欄就是這組數字）。
4. $`2\pi r D = 2\pi \times 1 \times 0.005 = 0.0314`$ → 向上 $1200\log_2(1.0314) = $ **+53.6 cents**，向下 $1200\log_2(0.9686) = $ **−55.3 cents**（超過半個半音，很明顯的走音；log 曲線向下彎（concave），所以向下的絕對值略大）。
   ±10 cents：以向上偏移計，$`1 + 2\pi r D = 2^{10/1200} = 1.005793`$ ⇒ $r = 0.005793 / (2\pi \times 0.005) = $ **0.184 Hz**；若要求向下也不超過 10 cents，$`1 - 2\pi r D = 2^{-10/1200}`$ ⇒ $`r = 0.183`$ Hz，差異很小。depth 大的時候 rate 要很慢，chorus 才不會「暈」。
5. $`t = \tan(\pi \cdot 1000/48000) = \tan(0.06545) = 0.065543`$，$a = (t-1)/(t+1) = -0.934457/1.065543 = $ **−0.87698**。
   500 Hz：$\varphi = -2\arctan\big(\tan(\pi \cdot 500/48000)/t\big) = -2\arctan(0.032737/0.065543) = -2\arctan(0.49946) = $ **−53.08°**（範例 4 的表：−53.08°）。注意 500 Hz 是 $`f_c`$ 的一半，但相位不是 −45°：arctan 不是線性的。
6. $`\tan(\pi f_k/f_s) = t \cdot \tan\frac{(2k+1)\pi}{8}`$：
   - $`k = 0`$：$`0.065543 \times \tan(22.5°) = 0.065543 \times 0.414214 = 0.027149`$ → $f = \arctan(0.027149) \times 48000/\pi = $ **414.7 Hz**。
   - $`k = 1`$：$`0.065543 \times \tan(67.5°) = 0.065543 \times 2.414214 = 0.158236`$ → **2397.8 Hz**。
   （範例 4 量到 414.7 / 2397.8 Hz。）
7. $`g(t) = (1 - 0.3) + 0.3\sin(2\pi \cdot 6t)`$：
   - 1000 Hz：$0.5 \times (1 - D/2) = 0.5 \times 0.7 = $ **0.35**；
   - 994 Hz 與 1006 Hz：$0.5 \times D/4 = 0.5 \times 0.15 = $ **0.075**（各一根）。
   6 Hz 本身**不會**出現在輸出（輸入是 1 kHz，乘法只會在 1 kHz 兩側產生 sideband）。
8. $r = 128 / (60 \times 0.75) = $ **2.844 Hz**；週期 $= 0.75 \times 60/128 = $ **351.6 ms**。

## 練習 3：Vibrato

完整解答：[`ex3_vibrato.cpp`](ex3_vibrato.cpp)（target `l10_ex3_solution`）。

關鍵：

```cpp
// prepare：唯一 allocate 的地方
delay_.prepare(dawlab::msToSamples(kMaxCentreMs + kMaxDepthMs, sampleRate) + 8, numChannels_);

// advanceModulation：每個時間點一次（channel 0），所有 channel 共用
const float ms = std::max(centreMs_ + depth * lfo_.getNextValue(), 0.05f);
currentDelaySamples_ = std::clamp(ms * fs / 1000, 2.0f, maxDelay - 3);

// process：push 再讀，輸出「只有」延遲的訊號
delay_.push(ch, d[i]);
d[i] = delay_.readCubic(ch, currentDelaySamples_);
```

- **centre 必須 ≥ depth**，否則 $`d(t)`$ 會變負（讀到「未來」）。centre 本身只是固定的 latency（5 ms），耳朵聽不出來。
- 用 `readCubic`：延遲一直在變，linear 插值會讓高頻隨 LFO 起伏（README 2.8 節）。
- 實測：1 kHz、5 Hz、1 ms → 968.6 .. 1031.4 Hz（理論相同，+53.6 / −55.3 cents），平均 1000.51 Hz，RMS −0.00 dB；depth 0 時固定 1000.00 Hz。
- 歌手的自然顫音約 5–6 Hz、±20–50 cents。解答輸出用 5.5 Hz、0.6 ms → $1200\log_2(1 \pm 2\pi \cdot 5.5 \cdot 0.0006) = $ +35.5 / −36.3 cents。

## 練習 4：Ring modulator

完整解答：[`ex4_ringmod.cpp`](ex4_ringmod.cpp)（target `l10_ex4_solution`）。

```cpp
void advance() noexcept {
    mix_ = mixSm_.next();
    carrierValue_ = carrier_.getNextValue();   // sine Lfo，rate = 載波頻率
}
float processSample(int channel, float x) noexcept {
    if (channel == 0) advance();
    return (1.0f - mix_) * x + mix_ * x * carrierValue_;
}
```

- 載波就是一個 rate 很高的 sine `Lfo`。`Lfo` 的相位是 double，150 Hz 或 500 Hz 的載波頻率都很準。
- 數學：$`A\sin(a)\sin(b) = \frac{A}{2}[\cos(a-b) - \cos(a+b)]`$ → 850 / 1150 Hz 各 $`A/2 = 0.25`$，1000 Hz 與 150 Hz 都消失。實測 0.2500 / 0.2500，1000 Hz 與 150 Hz 都在 −100 dB（量測下限）。
- mix 0.5：乾訊號 $`0.5 \times 0.5 = 0.25`$ 留在 1000 Hz；濕訊號減半，sideband 各 $`0.125`$。實測完全吻合。
- **30 Hz 載波為什麼是「每秒 60 次」起伏**：ring mod 的「gain」是 $`\sin(2\pi \cdot 30t)`$，它的絕對值每個週期過零兩次（正半週一次、負半週一次），而耳朵聽不出正負號（反相），只聽得到 $`|{\sin}|`$ 的起伏 → 60 次 / 秒。從頻譜看：每個頻率被拆成 $`f \pm 30`$ Hz 兩根線，間距 60 Hz，兩者拍頻（beating）就是 60 Hz。
- 和 tremolo 的差別：tremolo 的 gain 有 DC 偏移（$`1 - D/2`$），所以原頻率保留；ring mod 沒有 DC，原頻率被抵消。

## 練習 5：Stereo chorus 與左右相關性

完整解答：[`ex5_stereo_chorus.cpp`](ex5_stereo_chorus.cpp)（target `l10_ex5_solution`）。

```cpp
void reset() noexcept {
    delay_.reset();
    lfo_[0].setPhase(0.0f);
    lfo_[1].setPhase(phaseOffset_);           // 兩個 LFO 永遠相差 phaseOffset 個週期
    for (Lfo& l : lfo_) l.reset();
}
// process（每個 sample、每個 channel）：
delay_.push(ch, x);
const float delayMs = centreMs_ + depthMs_ * lfo_[ch].getNextValue();
const float wet = delay_.readCubic(ch, std::clamp(delayMs * toSamples, 2.0f, maxRead));
d[i] = (1.0f - mix_) * x + mix_ * wet;
```

實測（白雜訊，左右相同的輸入）：

| phase offset | mix | $`\rho`$ |
|---|---|---|
| 0° | 1.0 | 1.0000 |
| 90° | 1.0 | 0.0012 |
| 90° | 0.5 | 0.5502 |
| 180° | 1.0 | 0.0057 |

- mix 0.5 時理論 $`\rho`$：$`L = 0.5x + 0.5w_L`$，若 $`x, w_L, w_R`$ 互不相關、功率都是 $`\sigma^2`$：$`E[LR] = 0.25\sigma^2`$、$`E[L^2] = 0.5\sigma^2`$ → $`\rho = 0.5`$。實測 0.55，略高的原因是插值：延遲一直在變，cubic（Catmull-Rom）插值器是一個時變的 lowpass，對白雜訊平均只留下 $`\alpha \approx 0.814`$ 倍的功率（約 −0.9 dB），所以濕訊號功率是 $`\alpha\sigma^2`$ 而不是 $`\sigma^2`$。代回去：$`\rho = 0.25/(0.25 + 0.25\alpha) = 0.551`$，和實測吻合（主教材 2.4 節的 0.711 也是同一個原因）。
- **mono 相容性（−1.25 dB 從哪裡來）**：$`P(L) = 0.25\sigma^2 + 0.25\sigma^2 = 0.5\sigma^2`$；$`(L+R)/2 = 0.5x + 0.25w_L + 0.25w_R`$，功率 $`0.25 + 0.0625 + 0.0625 = 0.375\sigma^2`$；比值 $`0.375/0.5 = 0.75`$ → $10\log_{10}0.75 = $ **−1.25 dB**。實測 −1.11 dB：把插值的功率因子 $`\alpha`$ 算進去，比值 $`= (0.25 + 0.125\alpha)/(0.25 + 0.25\alpha)`$，$`\alpha = 0.814`$ 時為 −1.10 dB，正好解釋這個差距。損失不大，因為乾訊號（左右相同）在 mono 相加時完整保留。
- **相位差要另外驗證**：上表 90° 和 180° 的 $`\rho`$ 都接近 0，差不到 0.05——對白雜訊來說，任何非 0 的相位差都讓濕訊號幾乎不相關，所以 $`\rho`$ 分辨不出 offset 是 0.25、0.5 還是 0.05。解答多加一項對相位敏感的 self-check：rate 設 0（LFO 停在起始相位）、輸入 impulse，輸出 impulse 的位置就是延遲 $`d = \text{centre} + \text{depth}\cdot\sin(2\pi p)`$。實測左 15.000 ms、右 18.000 ms（$`p = 0.25`$ → $`\sin 90° = 1`$）；若 offset 寫成 0.5，右聲道會在 15 ms，寫成 0.05 則在 15.93 ms，都會 FAIL。
- 和 `dsp::Chorus`（1 voice、mix 1、spread 1）的 $`\rho`$：0.0012 vs 自己寫的 0.0012，一致（`Chorus` 的 spread 1 = 右聲道相位 +0.25，和這題相同）。
- **為什麼常用 90°？** 對白雜訊這種寬頻素材，延遲只要差一點點，左右就幾乎完全不相關，90° 和 180° 的 $`\rho`$ 都接近 0——數字上沒差。差別在動態：180° 時左右的延遲永遠一個最長、一個最短，走音方向完全相反，聲像在左右之間對稱地拉扯；90° 時左右輪流領先，聽起來是比較均勻、會「旋轉」的寬度。很多硬體 chorus 用 90°（正交，quadrature）LFO，是聽感與傳統的選擇，不是數學上的最佳解。

## 練習 6：Tempo-synced tremolo

完整解答：[`ex6_tempo_tremolo.cpp`](ex6_tempo_tremolo.cpp)（target `l10_ex6_solution`）。

```cpp
float getRateHz() const noexcept { return Lfo::tempoToRateHz(bpm_, beats_); }   // bpm / 60 / beats

void process(dawlab::AudioBlock block, double ppqAtBlockStart) noexcept {
    tremolo_.setRateHz(getRateHz());
    const double cycles = ppqAtBlockStart / beats_;               // 從歌曲開頭經過幾個 LFO 週期
    const double phase = cycles - std::floor(cycles) + phaseOffset_;
    tremolo_.setLfoPhase(static_cast<float>(phase - std::floor(phase)));
    tremolo_.process(block);
}
```

- **為什麼每個 block 都要重新對齊**：如果只設 rate，LFO 從「按下 play 的瞬間」開始算相位。從歌曲中間（例如 ppq 3.3，在兩個八分音符拍點之間）開始播放，tremolo 的峰值會落在拍點之間——跟鼓完全對不上。每個 block 用 ppq 算相位，跳到任何位置、或 tempo 改變後都立刻正確；正常播放時重新算出的相位和 LFO 自己累加的相位一樣（只差捨入誤差），所以不會有跳變。
- 實測：拍點上 gain 1.00000、兩拍中間 0.00000、2 秒內 8 個谷；從 ppq 3.3 開始播放與從頭播放的最大差 $`2.4 \times 10^{-7}`$（float 捨入等級）；120 → 90 bpm 後谷的間距 0.3333 s。
- 量測技巧：輸入 DC 1.0，輸出就是 gain 曲線本身。找「谷」時不要用「比左右鄰居小」——谷底附近 float 的量化會造成好幾個假的局部最小值；解答改用「連續低於 0.05 的區間算一個谷」。
- 真實 host（第 13、14 章）的 ppq 由 transport 提供（JUCE 的 `AudioPlayHead::PositionInfo::getPpqPosition()`）；這裡用 `ppq += n / fs × bpm / 60` 模擬。

## 練習 7：為什麼 phaser 的 notch 不是等間距？

1. 兩條路徑相加，notch 出現在「相位差 = π 的奇數倍」的頻率。
   - Flanger：相位差 $`= 2\pi f d`$，對 $`f`$ **線性**，每增加 $`1/d`$ Hz 就多轉 $`2\pi`$，所以 notch 等間距（$`1/d`$）。
   - Phaser：相位差 $`= N\varphi(f) = -2N\arctan\big(\tan(\pi f/f_s)/\tan(\pi f_c/f_s)\big)`$，是 arctan 形狀：**以對數頻率（八度）來看**，相位在 $`f_c`$ 附近轉得最快，離 $`f_c`$ 越多個八度轉得越慢，所以 notch 在對數軸上以 $`f_c`$ 為中心對稱、在線性 Hz 軸上低頻密高頻疏（例如 4 stages 的 414.7 / 2397.8 Hz）。注意這是「每八度」的說法：在線性 Hz 軸上，每 Hz 的相位變化 $`|d\varphi/d\omega| = (1+u^2)\,t/(t^2+u^2)`$（$`u = \tan(\omega/2)`$、$`t = \tan(\omega_c/2)`$）在 DC 最大（約 $`1/t`$），到 $`f_c`$ 只剩一半左右。總相位只有 $`N\pi`$，所以 notch 數有限（$`N/2`$），不像 comb 一路延伸到 Nyquist。
2. notch 條件 $`\tan(\pi f_k/f_s) = \tan(\pi f_c/f_s)\tan\theta_k`$，$`\theta_k = (2k+1)\pi/(2N)`$。成對的 $`k`$ 與 $`k' = N/2 - 1 - k`$：$`\theta_{k'} = \frac{(N - 2k - 1)\pi}{2N} = \frac{\pi}{2} - \theta_k`$。所以
   $$\tan\frac{\pi f_k}{f_s}\cdot\tan\frac{\pi f_{k'}}{f_s} = \tan^2\frac{\pi f_c}{f_s}\cdot\tan\theta_k\tan\!\left(\frac{\pi}{2} - \theta_k\right) = \tan^2\frac{\pi f_c}{f_s}$$
   低頻時 $`\tan x \approx x`$，兩邊消去 $`(\pi/f_s)^2`$ 得 $`f_k f_{k'} \approx f_c^2`$。也就是說 notch 在**對數頻率軸**上以 $`f_c`$ 為中心對稱，但相鄰的比值並不固定（8 stages：3.36、2.23、3.26 倍），所以既不等差也不等比。
3. $`268.3 \times 3665.4 = 983{,}427 \approx 10^6`$（中間那個 1000.0 Hz 自己和自己配對：$`1000^2 = 10^6`$）。不剛好等於 $`10^6`$，是因為 $`\tan x \approx x`$ 只是近似：3665 Hz 已經不算「低頻」，bilinear transform 把高頻往下壓（frequency warping）。用精確式驗證：$`\tan(\pi \cdot 268.3/48000)\cdot\tan(\pi \cdot 3665.4/48000) = 0.0042959`$，$`\tan^2(\pi \cdot 1000/48000) = 0.0042959`$，到第五位有效數字都相同（剩下的差異只是 268.3、3665.4 四捨五入到 0.1 Hz 造成的；範例 4 用未捨入的 4 stages notch 代入，到小數第十位都相等）。
4. 總相位 $`= \sum_{i=1}^{4}\varphi_i(\omega)`$，每一項仍然從 0 單調降到 $`-\pi`$，總和仍然從 0 降到 $`-4\pi`$，所以 **notch 數不變（仍然 2 個）**。位置：$`f_c`$ 散開之後，總相位在低頻（300 Hz 那級先轉）和高頻（2400 Hz 那級最後轉）都更早 / 更晚開始變化，曲線比較平緩、跨越的頻率範圍更寬 → 第一個 notch 往低頻移、第二個往高頻移，兩個 notch 分得更開、更寬。很多類比 phaser（例如以 JFET 或 OTA 組成的 4 級電路）就是用「各級電容不同」來調整 notch 分布，這是它們各自音色的來源之一。
5. 要等間距，相位差必須對頻率線性 = 純延遲 → 那就是 flanger 了。也可以用很多級 allpass 去近似一段延遲（allpass 鏈的 group delay 在低頻近似常數），但需要很多級（CPU）、而且結果就變成 comb filter 的金屬聲，失去 phaser 柔和的特色。比較實際的作法是接受不等間距，用不同的 $`f_c`$ 分布（第 4 小題）調整 notch 的位置。

## 練習 8：Staggered phaser

完整解答：[`ex8_staggered_phaser.cpp`](ex8_staggered_phaser.cpp)（target `l10_ex8_solution`）。

- **TODO 1**：`theoryPhase` 就是把每一級的 `Phaser::allpassPhase(f, scale · fc_i, fs)` 加起來。一階 allpass 的振幅處處是 1，串接後的響應 = 各級響應相乘，相位就相加。
- **TODO 2**：每一項 $`\varphi_i`$ 都從 0 單調降到 $`-\pi`$，總和 $`\Phi`$ 從 0 單調降到 $`-N\pi`$，所以 $`\Phi(f) = -(2k+1)\pi`$ 恰好有一個解，二分法一定收斂：$`\Phi(\text{mid})`$ 比目標大（還不夠負）就把 `lo` 移到 mid，否則移 `hi`。區間 24 kHz 二分 60 次，誤差遠小於 1 Hz。
- **TODO 3**：所有 stage 共用同一個比例 $`s = 2^{\text{depth}\cdot\text{octaves}\cdot m}`$，第 $`i`$ 級設 $`f_c = s \cdot f_{c,i}`$。這樣整組 notch 在對數頻率軸上一起平移，各級之間的分布（音色）不變。注意是「每一級各自」呼叫 `makeFirstOrderAllpass`——和 `Phaser` 算一次係數、套到所有 stage 不同。

量測結果（`l10_ex8_solution` 的輸出）：

```
4 x 1000 Hz: predictNotchHz vs Phaser::notchFrequencyHz (closed form)
  k = 0: 414.7042 Hz vs 414.7042 Hz
  k = 1: 2397.7862 Hz vs 2397.7862 Hz
  White noise through StaggeredPhaser vs dawlab::dsp::Phaser: largest difference 0.00e+00 (should be 0)

300 / 600 / 1200 / 2400 Hz: found 2 notches (expected 2)
   k   measured(Hz)  predicted(Hz)   error      depth
   0        289.1         289.1   +0.002%    -92.4 dB
   1       2481.5        2481.5   -0.001%   -100.0 dB

  4 x 1000 Hz:           414.7 /  2397.8 Hz, ratio 5.78x (2.53 octaves)
  300-2400 Hz spread:    289.1 /  2481.5 Hz, ratio 8.58x (3.10 octaves)
     total phase   4 x 1000 Hz    300-2400 Hz
       414.7 Hz     -180.0°        -235.0°
      1000.0 Hz     -360.0°        -389.3°
      2397.8 Hz     -540.0°        -535.0°

LFO = +1 (fc x 2 = 600 / 1200 / 2400 / 4800 Hz): found 2 notches (expected 2)
   k   measured(Hz)  predicted(Hz)   error      depth
   0        580.5         580.5   -0.001%    -97.4 dB
   1       4895.9        4895.8   +0.001%    -94.2 dB
```

解讀：

- **notch 數不變**：總相位仍是 0 到 $`-4\pi`$，只有 $`-\pi`$ 與 $`-3\pi`$ 兩個解，和練習 7 第 4 小題的推論一致。
- **兩個 notch 分得更開**（2.53 → 3.10 個八度）：300 Hz 那級很早開始轉，所以 414.7 Hz 時總相位已經是 −235°（超過 −180°），第一個 notch 提早在 289.1 Hz 出現；2400 Hz 那級很晚才轉完，2397.8 Hz 時還只到 −535°，第二個 notch 延後到 2481.5 Hz。注意兩個 notch 的位移並不對稱（低頻那個移了 0.52 個八度、高頻那個只移了 0.05 個八度）：這組 $`f_c`$ 的幾何平均是 $`300 \cdot 2^{1.5} \approx 848.5`$ Hz，不是 1000 Hz，整體重心本來就偏低。
- **LFO 掃動**：所有 $`f_c`$ 加倍之後，第一個 notch 從 289.1 移到 580.5 Hz（約 2 倍），第二個從 2481.5 移到 4895.9 Hz（1.97 倍，不到 2 倍是 bilinear 在高頻的頻率扭曲）。
- 聽感：`l10_ex8_staggered.wav` 兩個 notch 距離較遠，掃動時「低頻那個」和「高頻那個」比較像兩個獨立的凹陷在移動；`l10_ex8_uniform.wav` 的兩個 notch 比較靠近，聽起來是一團。類比 phaser 的各級元件本來就不完全相同，這也是它們音色各異的原因之一。
