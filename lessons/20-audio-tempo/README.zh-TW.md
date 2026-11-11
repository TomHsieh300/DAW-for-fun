# 第 20 章：跟著 tempo 走的 audio - warp marker、varispeed 和 time-stretcher

[English](README.md) | **繁體中文**

> **狀態：完成。** 全部 7 個 step。在 loop 底下拖動 tempo 仍然要親手試過：見 Step 6 的「你要自己試的」。

在 DawLab 19（第 19 章）裡 tempo 可以改變，音符會跟著走：它們是以 tick 存的。Audio clip 則是以 sample 存的，留在原地——拖一下 tempo 點，錄好的吉他就和鼓對不上了。第 19 章的練習 6 在紙上設計了解法。本章把它做出來：一個知道自己的 audio 屬於音樂哪個位置的 clip，依 tempo 的要求播得快一點或慢一點——像磁帶那樣，或是不改變音高。

## 如何使用本章

和第 15 到 19 章一樣，每個 step 都是 `steps/stepNN-*/` 裡一個小而完整的 snapshot：你在 `starter/` 裡寫出該 step 的函式，self-check 會告訴你它們何時能正常運作。見 [steps/README.zh-TW.md](steps/README.zh-TW.md)。

```powershell
.\scripts\build.ps1 -Only 20-audio-tempo          # 不需要 JUCE
.\build\bin\Debug\l20_step01_starter.exe
```

在 Linux / macOS 上：`./scripts/build.sh --only 20-audio-tempo`，然後執行 `./build/bin/l20_step01_starter`。

## 1. 目標

完成本章後，你可以

- 用 warp marker 說明 clip 的 audio 屬於音樂的哪個位置，並為 loop 和 take 做出 marker；
- 讓 clip 只用起點跟著 tempo，或是讓裡面的每樣東西都跟著；
- 用 resampling 以另一個速度播放 audio，而且位置永遠不漂移；
- 用 time-stretcher 改變速度而不改變音高，並量出它的代價；
- 事先算好這樣的 audio，再交給負責播放的 engine。

## 2. 各個 step

<div align="center">

| Step | 文件 | 新增的內容 | 狀態 |
|---|---|---|---|
| 1 | [Warp map](docs/step-01-warp-map.zh-TW.md) | 把檔案的 sample 綁到歌的 tick 的 marker，雙向換算；tempo 要求的速度；loop 和 take 的 marker，以及 ramp 需要幾個 | 完成 |
| 2 | [起點跟著 tempo 的 clip](docs/step-02-follow-start.zh-TW.md) | 起點存成 tick、sample 由它推導，拖到 sample 和拖到 grid，長度會怎樣，接縫的空隙和重疊，播放中改變 tempo | 完成 |
| 3 | [Varispeed](docs/step-03-varispeed.zh-TW.md) | 速度和音高；每個 block 都從歌的 sample 算檔案裡的位置，在 knot 和 grid 上精確；linear、Hermite 和 windowed sinc 的讀法與量測；讀得比較快時的 aliasing；clip 是 source 的範圍，在任何 tempo 下都相接 | 完成 |
| 4 | [Time-stretcher](docs/step-04-stretch.zh-TW.md) | Hann window 的 overlap-add、小段為什麼接不起來、WSOLA 對 natural continuation 的搜尋和它的分數；在單音、noise 和 click 上量測：保住了什麼、代價是什麼 | 完成 |
| 5 | [Engine](docs/step-05-engine.zh-TW.md) | 第 19 章 engine 的複本加上兩個新 header：clip 和它跟著 tempo 的方式、算成一般 clip、事先運算的 worker thread、用 generation 和取消處理過期的 request、結果算好之前的 placeholder | 完成 |
| 6 | [App](docs/step-06-app.zh-TW.md) | 第 19 章 app 的複本：由 clip 自己帶著 follow 資料、start 和 length 在兩個地方推導、經由 session 的運算結果和 placeholder、clip 選單裡的模式、移動和裁切、專案檔第 8 版 | 完成 |
| 7 | [本章的結尾](docs/step-07-the-end.zh-TW.md) | clip 可以做的四件事、每一塊放在哪裡、檢查量出來的數字，以及留下什麼沒做 | 完成 |

</div>

## 3. 本章從哪裡開始

DawLab 19 的 tempo map 把 tick 換成秒、再換回來，經過跳躍和 ramp 都是精確的（`steps/tempo_map.h` 裡有解好的版本）。它的 transport 會給每個 block 它的 tick。它的 audio clip 有以 sample 計的起點和長度，一個 sample 一個 sample 地播放。

## 4. 練習

見 [exercises/README.zh-TW.md](exercises/README.zh-TW.md)，解答在 [solutions/README.zh-TW.md](solutions/README.zh-TW.md)。六題，都不需要 JUCE：為什麼本章這樣做（概念）、用數字看 audio 和 tempo（計算）、take 用最少的 marker、對準到 sample 小數位置的 time-stretcher、播放頭跳動時的 fade，以及保住鼓擊的 stretcher（設計）。三個選做專題延伸 DawLab 20（需要 `-Juce`）：照錄音時的樣子跟著 tempo 的 take、audio quantize，以及不知道 tempo 也能對拍的 loop。

## 5. 接下來

從 [Step 1 - Warp map](docs/step-01-warp-map.zh-TW.md) 開始。
