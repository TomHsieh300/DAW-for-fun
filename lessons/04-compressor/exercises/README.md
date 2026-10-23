# 第 04 章 練習題

難度：★☆☆ 基礎 / ★★☆ 中等 / ★★★ 進階。實作題的起始碼在 `starter/`，可以直接編譯；執行時 self-check 會印出 FAIL，完成 `// TODO` 之後全部 PASS 就算完成。解答在 [`../solutions/README.md`](../solutions/README.md)。

建置與執行（以練習 3 為例）：

```powershell
.\scripts\build.ps1 -Only 04-compressor -NoTest
cd build; .\bin\Debug\l04_ex3_starter.exe      # 你的版本
.\bin\Debug\l04_ex3_solution.exe               # 參考解答
```

---

## 練習 1（觀念題，★☆☆）：方塊圖與設計選擇

回答下列問題，每題 2–4 句即可：

1. Compressor 的四個方塊（detector、gain computer、attack/release 平滑、makeup gain）各自負責什麼？哪一個方塊是「沒有狀態的純函式」？
2. 為什麼 `Compressor` 把 attack / release 平滑放在 gain computer **之後**（log domain），而不是先平滑 envelope 再丟進靜態曲線（linear domain）？release 的形狀差在哪裡？
3. Branching 平滑對正弦波這種週期訊號會「低估」peak，為什麼？Decoupled 怎麼解決？
4. 為什麼 stereo link 預設要開？什麼情況會想關掉？
5. 一台 attack 標示 1 ms 的 compressor，為什麼抓不住鼓的尖峰？要抓住需要什麼機制？

---

## 練習 2（計算題，★★☆）：靜態曲線與時間常數

不要寫程式，手算（可以用 `l04_transfer_curve` 的輸出對答案；`l04_ex2_solution` 會把解答的每個數字用程式驗算一遍）：

1. $T = -24$ dB、$R = 3$、hard knee。輸入 -30、-24、-12、0 dB 時的輸出 dB 與 gain reduction 各是多少？
2. 同樣的 $T$、$R$，改成 soft knee $W = 12$ dB。knee 的範圍是哪一段？輸入 -24 dB（threshold 本身）與 -20 dB 時的輸出各是多少？
3. 用 2. 的公式驗證：在 knee 上緣 $x = T + W/2 = -18$ dB，二次曲線的值與 hard knee 直線的值相同。
4. $f_s = 44.1$ kHz，attack 5 ms、release 200 ms。求 $\alpha_A$、$\alpha_R$（保留 6 位小數）。
5. 同上，attack 到 90% 與到 99% 各要幾 ms？release 到 10%（也就是衰減 90%）要幾 ms？
6. 某台類比 compressor 的手冊說「attack 時間定義為到達最終值 90% 所需時間」，面板寫 10 ms。如果要用本章的 one-pole（63.2% 定義）模仿它，`setAttackMs()` 應該設多少？

---

## 練習 3（實作題，★★☆）：Parallel（New York）compression

起始碼：`starter/ex3_parallel.cpp`（target `l04_ex3_starter`）

1. 完成 `compressBlocks()`：逐 block（512 samples）呼叫 `Compressor::process()`，並記下整段最大的 `getGainReductionDb()`。
2. 算出 makeup gain，讓 wet 的 peak 與 dry 的 peak 相同。
3. 完成 `parallelMix()`：$y = (1 - \text{mix})\,\text{dry} + \text{mix}\,\text{wet}$，mix 要 clamp 到 0..1。
4. 觀察不同 mix 下的 crest factor。

提示：wet 用「極快 attack」（0.02 ms），原因見 README 範例 3 的 attack 掃描。Self-check 要求 mix = 0 時完全等於 dry、mix = 1 時完全等於 wet、mix = 0.5 的 crest factor 介於兩者之間。

---

## 練習 4（實作題，★★☆）：Sidechain high-pass filter

起始碼：`starter/ex4_sidechain_hpf.cpp`（target `l04_ex4_starter`）

1. 完成 `OnePoleHighpass`：$lp[n] = lp[n-1] + b\,(x[n] - lp[n-1])$，$b = 1 - e^{-2\pi f_c / f_s}$，$hp[n] = x[n] - lp[n]$。self-check 會用 `measureGainDb` 驗證 400 Hz 處約 -3 dB、50 Hz 處低於 -15 dB。
2. 完成 `compressWithSidechainHpf()`：逐 block 把主訊號複製到預先配置的 sidechain buffer、過 HPF、再呼叫 `Compressor::process(block, sidechain)`。
3. 用 50 Hz @ -6 dBFS + 3 kHz @ -24 dBFS 的訊號比較：沒有 HPF 時 3 kHz 被低音拖下去多少 dB？加了 400 Hz HPF 之後呢？

提示：`hpf == nullptr` 代表「不加 HPF」，sidechain 就是原始訊號的複本。

---

## 練習 5（實作題，★☆☆）：Auto makeup gain

起始碼：`starter/ex5_auto_makeup.cpp`（target `l04_ex5_starter`）

1. 完成 `autoMakeupDb()`：讓 0 dBFS 的輸入經過靜態曲線後回到 0 dBFS 所需的 makeup（dB）。
2. 完成 `steadyOutputPeakDb()`：建立 `Compressor`（attack 1 ms、release 100 ms、`setMakeupGainDb()`），逐 block 跑 1 秒正弦波，回傳最後 200 ms 的 peak。
3. 驗證：$T = -20$、$R = 4$ 時 makeup = 15 dB；0 dBFS 正弦進 → 約 0 dBFS 出；-6 dBFS 進 → -1.5 dBFS 出。

想一想：$T = -6$、$R = 4$、$W = 20$ 時 0 dBFS 落在 knee 裡面，makeup 是多少？（先手算再跑。）

---

## 練習 6（實作題，★★★）：Feedback 拓樸 compressor

起始碼：`starter/ex6_feedback.cpp`（target `l04_ex6_starter`）

1. 完成 `FeedbackCompressor::processSample()`：detector 看**上一個輸出 sample**（不是輸入），gain computer 用斜率 $s$ 參數化：$\text{GR}_{\text{target}} = \max(0,\; s\,(L_{\text{dB}} - T))$，經 `AttackReleaseFilter` 平滑後套用。
2. 推導並填入理論穩態 GR：$\text{GR} = \frac{s}{1 + s}(X - T)$，有效 ratio $R_{\text{eff}} = 1 + s$。
3. 用 tone bursts 比較 $s = 0.75, 3, 9$ 的穩態 GR，並確認 $s = 3$（$R_{\text{eff}} = 4$）的 attack t63 比 feedforward 4:1 快。

提示：$s > 1$ 的迴路靠 attack 平滑才穩定，attack 固定 10 ms；attack 設 0 會震盪。

---

## 練習 7（實作題，★★☆）：RMS 視窗長度 vs pumping

起始碼：`starter/ex7_rms_window.cpp`（target `l04_ex7_starter`）

1. 完成 `run()`：`setDetectorMode(DetectorMode::Rms)`、`setRmsWindowMs(w)`，逐 block 壓縮鼓組 loop，跳過前 0.5 秒暖機後記錄每個 block 的 `getGainReductionDb()` 最大值與最小值，算出「GR 擺動幅度」= max − min。
2. 掃描視窗 1、5、10、30、100、300、1000 ms，觀察擺動幅度與輸出 crest factor。
3. Self-check：擺動幅度隨視窗變長單調下降；1 ms 視窗的行為接近 peak detector（擺動差 < 3 dB）；1 ms 的擺動至少是 1000 ms 的 3 倍。

聽聽看 `l04_ex7_rms_1ms.wav`、`l04_ex7_rms_100ms.wav`：短視窗每一下鼓都讓整體「呼吸」，長視窗像是整體被拉平。
