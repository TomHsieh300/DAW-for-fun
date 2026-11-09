# 第 17 章：Recording - 從音訊 input 到時間軸上的 clip

[English](README.md) | **繁體中文**

> **狀態：完成。** 全部 7 個 step。真正的錄音、latency 和 monitoring 仍然要用真的裝置試過：見 Step 7 的「你要自己試的」。

DawLab 16（第 16 章）可以播放、混音並 automation 已經存在的音訊，但它的 audio callback 會忽略 input。
DAW 也必須能錄音，而錄音會把前面各章分開處理的東西全部放在一起：不能等待的 real-time thread、可能卡住
的硬碟，以及一條每個 sample 都有固定位置的時間軸，儘管聲音到達電腦時已經晚了。

[第 14 章的 Exercise 6 和 9](../14-daw-8track/exercises/README.zh-TW.md) 勾勒了基本做法。本章不靠 JUCE 從
頭開始，再往下處理真正錄音需要的東西：硬碟卡住時保持時間位置、撐得過當機的 take、punch in/out，以及演
奏時聽得到自己。

## 如何使用本章

和第 15、16 章一樣，每個 step 都是 `steps/stepNN-*/` 裡一個小而完整的 snapshot：你在 `starter/` 裡寫出該
step 的函式，self-check 會告訴你它們何時能正常運作。請見 [steps/README.zh-TW.md](steps/README.zh-TW.md)。

```powershell
.\scripts\build.ps1 -Only 17-recording          # no JUCE needed
.\build\bin\Debug\l17_step01_starter.exe
```

在 Linux / macOS：`./scripts/build.sh --only 17-recording`，然後執行 `./build/bin/l17_step01_starter`。

## 1. 學習目標

完成本章後，你可以

- 不等待、不配置記憶體地把音訊交出 audio thread，並在硬碟跟不上時保持錄音的時間位置；
- 根據裝置和混音的 latency，算出 take 在時間軸上的位置；
- 把 take 寫進硬碟，讓當機或硬碟滿了都不會失去它；
- 在 block 中間 punch in/out，並把 loop 的每一圈保留成一個 take；
- 監聽 input，並解釋為什麼聽到的不等於錄到的。

## 2. 各個 step

<div align="center">

| Step | 文件 | 新增內容 | 狀態 |
|---|---|---|---|
| 1 | [從 audio thread 到 disk thread](docs/step-01-input-ring.zh-TW.md) | 只會增加的 counter 組成的 ring、整個放入或完全不放的 block、保持時間位置的 gap；ThreadSanitizer | 完成 |
| 2 | [Take 放在哪裡](docs/step-02-placement.zh-TW.md) | 由 audio thread 蓋章的 `P - (L_in + L_out + L_mix)`、模擬的 loop-back、量測來回延遲、兩個 thread 都能結束的 take | 完成 |
| 3 | [把 take 寫進硬碟](docs/step-03-take-on-disk.zh-TW.md) | 邊錄邊變長、撐得過當機或硬碟滿的 WAV，只有一種結束 take 的方式 | 完成 |
| 4 | [Punch 與 loop](docs/step-04-punch-and-loop.zh-TW.md) | 一個檔案一條 stream，take 由它算出：精確的 punch 點、loop 的每一圈 | 完成 |
| 5 | [Input monitoring](docs/step-05-input-monitoring.zh-TW.md) | 聽到的與錄到的、經過 insert 的 latency、防 feedback 的 guard | 完成 |
| 6 | [有錄音的 engine](docs/step-06-engine.zh-TW.md) | 第 16 章 engine 的複本：`processBlock` 接收 input、track 可以 arm、經過真正 mixer 的 loop-back；ThreadSanitizer | 完成 |
| 7 | [有錄音的 app](docs/step-07-app.zh-TW.md) | 第 16 章 app 的複本：錄音鍵、arm、需要時才開的 input、錄音中的 take、取代舊內容的 take、loop 的 take、undo、存檔、當機後復原 | 完成 |

</div>

## 3. 本章的起點

第 16 章的 `AudioHost::audioDeviceIOCallbackWithContext` 收到裝置的 input 後就丟掉，
`AudioHost::openDefaultDevice` 預設不要求任何 input channel，`Engine::processBlock` 也只有 output。engine
的其他部分完全不知道聲音可以來自外部。Step 1 還不會動到這些，它先處理錄音要做的第一件事。

## 4. 練習

見 [exercises/README.zh-TW.md](exercises/README.zh-TW.md)，解答在 [solutions/README.zh-TW.md](solutions/README.zh-TW.md)。
共七題，都不需要 JUCE：錄音為什麼要這麼小心（概念）、用數字看錄音（計算）、只寫 take 用得到的部分、
量測 latency 也要知道何時不能信、告訴使用者 input 出了什麼問題、把 loop 各圈最好的部分 comp 起來，以及兩個裝置、兩個時鐘（設計）。另有四個選做專題延伸 DawLab 17（需要 `-Juce`）：「Measure Latency」按鈕、take 靜音、太小聲或 clipping 時的警告、在 track 上 comping，以及 pre-roll。

## 5. 下一步

從 [Step 1 - 從 audio thread 到 disk thread](docs/step-01-input-ring.zh-TW.md) 開始。
