# 第 09 章 練習題：Delay 與 Echo

難度：★☆☆ 基礎　★★☆ 中等　★★★ 進階

實作題（3、4、5、6）都有起始碼 `starter/exN_xxx.cpp`，可以直接編譯；執行時 self-check 會印出 FAIL，那是預期的——把 `// TODO` 補完、讓所有 check 變成 PASS 就完成了。解答在 [`../solutions/README.md`](../solutions/README.md) 與 `../solutions/exN_xxx.cpp`。計算題 1、7 也有解答程式（`l09_ex1_solution`、`l09_ex7_solution`）把答案實際跑一次——先自己算，再執行對答案。

建置與執行（在專案根目錄）：

```powershell
.\scripts\build.ps1 -Only 09-delay -NoTest
cd build
.\bin\Debug\l09_ex3_starter.exe      # 或 l09_ex4_starter、l09_ex5_starter、l09_ex6_starter
```

---

## 練習 1 ★☆☆（計算）Tempo sync 與 echo 的衰減

1. 120 BPM 下，四分音符、八分音符、附點八分、八分三連音、十六分音符各是幾 ms？
2. 95 BPM 的附點八分是幾 ms？在 48 kHz 下是幾個 sample？為什麼這個數字說明了 delay line 需要 fractional delay？
3. 140 BPM 的四分三連音、90 BPM 的附點四分、174 BPM（drum & bass）的十六分音符各是幾 ms？
4. delay 375 ms、feedback 0.5（迴路裡沒有 filter）。第 1 到第 4 個 echo 的振幅各是多少？各是幾 dB？
5. 推導 echo 衰減 60 dB 所需的時間 $T_{60}$（用 $D$ 與 $g$ 表示）。$D = 375$ ms 時 $g = 0.5$ 與 $g = 0.7$ 各是幾秒？$D = 250$ ms、$g = 0.9$ 呢？
6. 反過來：$D = 500$ ms，希望 echo 在 2 秒內衰減 60 dB，feedback 要設多少？

## 練習 2 ★☆☆（觀念）為什麼 feedback ≥ 1 會爆、為什麼需要插值

1. 用 impulse response 說明 feedback echo 的穩定條件。再用 $W(z) = z^{-D}/(1 - g z^{-D})$ 的 pole 位置說明一次。$g = 1$ 時會發生什麼事（它是「穩定」還是「不穩定」）？
2. 迴路裡加了 one-pole lowpass 之後，$g = 0.99$ 還穩定嗎？如果把 one-pole 換成 Q = 4 的 biquad lowpass（cutoff 處 +12 dB），$g = 0.5$ 還一定穩定嗎？
3. 為什麼迴路裡加了 $\tanh$ 就可以把 feedback 設到 1.1 而不爆？此時輸出振幅由什麼決定？
4. 舉出至少三個「整數 delay 不夠用」的情境，並說明每個情境若硬用整數 delay 會聽到什麼問題。
5. 有人說：「delay 400 ms 的 echo 根本不需要插值，差半個 sample 誰聽得出來？」這句話在什麼情況下是對的、什麼情況下是錯的？

## 練習 3 ★★☆（實作）Allpass interpolation

起始碼：`starter/ex3_allpass_interpolation.cpp`

完成 `AllpassInterpolator` 的 `setDelay()` 與 `processSample()`：

$$
H(z) = \frac{\eta + z^{-1}}{1 + \eta z^{-1}},\qquad \eta = \frac{1 - \Delta}{1 + \Delta},\qquad y[n] = \eta\,(v[n] - y[n-1]) + v[n-1]
$$

要求：

- delay = 10.5 時，100 Hz 到 20 kHz 的幅度響應都在 0 ± 0.05 dB；
- 相位延遲符合理論 $\tau(\omega) = d + 1 - \frac{2}{\omega}\arctan\frac{\eta\sin\omega}{1 + \eta\cos\omega}$（誤差 < 0.02 sample）；
- 100 Hz 時 delay 10.1、10.3、10.5、10.7、10.9 的相位延遲都精準（誤差 < 0.01）。

提示：把小數部分安排在 $[0.5, 1.5)$——`d = floor(delay − 0.5)`——$\eta$ 就不會接近 1。記得 flush denormal。

延伸思考：解答程式會印出 linear / cubic / allpass 三者的幅度與相位延遲表。用這張表說明各自適合的場合。

## 練習 4 ★★☆（實作）Tape-style delay

起始碼：`starter/ex4_tape_delay.cpp`

完成 `TapeDelay::feedbackPath()`：wet → highpass → lowpass → $\tanh(\text{drive}\cdot v)/\text{drive}$ → × feedback。（也可以寫成第 06 章的 `Saturation::shape(SaturationCurve::Tanh, drive * v) / drive`，結果相同——`shape(Tanh, x)` 本身就是 `std::tanh(x)`，所以 drive 的乘與除要自己做。）

self-check 會驗證：

- (a) delay 200 ms、fb 0.7、LP 2 kHz、HP 100 Hz：第二個 echo 與第一個 echo 的比值在 500 Hz ≈ $g\,|H_{HP}||H_{LP}|$ ≈ 0.67、在 8 kHz ≈ 0.17；
- (b) feedback = 1.1 時輸出仍然有界（peak < 1.2），而且穩態振幅接近 describing function 的預測值（describing function 的直覺見主講義 2.8 節，完整推導在解答）。

問題：為什麼 tanh 要「先乘 drive、再除以 drive」？drive 越大，自激振盪的振幅會變大還是變小？

## 練習 5 ★★☆（實作）Multi-tap delay

起始碼：`starter/ex5_multitap.cpp`

一條 mono delay line、最多 8 個 tap，每個 tap 有自己的 delay、gain、pan（constant-power：$g_L = \cos\theta$、$g_R = \sin\theta$、$\theta = (\text{pan} + 1)\pi/4$）。feedback 從最後一個 tap 回到輸入。完成 `process()` 與 `updateTap()`。

self-check 會用 120 BPM 的三個 tap（附點八分偏左、四分偏右、附點四分置中）驗證：

- impulse 進去，每個 tap 剛好在正確的 sample 出現，振幅 = gain × pan 係數，前後一個 sample 都是 0；
- 總能量 = $\sum \text{gain}^2$（constant-power pan 不改變能量）；
- 加 feedback 0.5 之後，tap0 會在 $D_\text{last} + D_0$ 再出現一次，振幅乘 0.5。

問題：multi-tap delay 和「三台 delay 串接」有什麼不同？（從 echo 的時間位置、CPU、插值造成的高頻損失三方面比較。）

## 練習 6 ★★★（實作）用 crossfade 切換 delay time

起始碼：`starter/ex6_crossfade_delay.cpp`

實作 2.7 節的第二種策略：兩個讀取頭，收到新的 delay time 時把「沒在用的那個」放到新位置，用 raised-cosine（$g_B = \tfrac12(1 - \cos\pi t)$、$g_A = 1 - g_B$）在 20 ms 內 crossfade，結束後交換角色；fade 期間又收到新值要排隊。

self-check 會用 440 Hz 正弦在 t = 1 s 把 delay 從 10 ms 切到 250 ms，驗證：

- 切換後 100 ms 內，相鄰 sample 的最大差值 < 正弦本身最大斜率的 1.5 倍（沒有 click）；
- 切換前後 ±50 ms 量到的頻率接近 440 Hz（沒有 Doppler）；
- fade 結束後輸出與 $x[n - 12000]$ 完全相同；
- fade 期間再改值會排隊：10 → 100 ms 的 fade 進行中又改成 300 ms，fade 結束後會接著做下一次，最後穩定在 $x[n - 14400]$，整個過程沒有 click。

問題：crossfade 期間，兩個讀取頭讀到的是同一個 440 Hz 正弦的不同相位。合成訊號的振幅在 fade 中間最低會掉到多少？（提示：兩個頭相差 240 ms，是 105.6 個週期。）這對真實音樂素材來說嚴重嗎？

## 練習 7 ★★☆（計算 + 觀念）Comb filter

1. $D = 240$ sample（48 kHz）、$g = 0.8$ 的 feedback comb：峰在哪些頻率、高幾 dB？谷在哪些頻率、低幾 dB？
2. 同樣的 $D$，若是 feedforward comb（$y = x + 0.8\,x[n - D]$）呢？
3. 把 $g$ 改成 −0.8，峰谷的位置會怎麼變？聽起來的音高會怎麼變？
4. 在 44.1 kHz 下想讓 feedback comb 的音高是 A4 = 440 Hz。$D$ 應該是多少？若只能用整數 $D$，最接近的兩個選擇會偏離幾 cents？
5. 為什麼白雜訊經過 feedback comb 會聽到音高？這和 `Signals.h` 的 `makePluck()`（Karplus-Strong）有什麼關係？
6. 第 12 章的 Schroeder reverb 把好幾個 feedback comb 並聯。為什麼要並聯「好幾個」、而且 delay time 要選互質的數字？
