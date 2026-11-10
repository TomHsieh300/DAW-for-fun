# 第 19 章：樂器與 tempo - tempo map、樂器的 latency，以及 app 裡的樂器

[English](README.md) | **繁體中文**

> **狀態：完成。** 全部 6 個 step。自己畫的 tempo、別處來的 MIDI 檔和真正的 VST3 樂器仍然要親手試過：見 Step 5 和 6 的「你要自己試的」。

DawLab 18（第 18 章）以單一 tempo、用它自己的 synth 播放音符，而它的 engine 已經可以讓另一個樂器取代 synth——但只有練習用到這一點。本章完成第 18 章留下的事：會改變的 tempo，有跳躍也有 ramp，音符跟著它走；latency 有補償的樂器；以及 app 這一側——tempo lane、帶 tempo 變化的 MIDI 檔、undo 和專案檔都認得的 synth 設定面板，以及 track 上的 VST3 樂器。

## 如何使用本章

和第 15 到 18 章一樣，每個 step 都是 `steps/stepNN-*/` 裡一個小而完整的 snapshot：你在 `starter/` 裡寫出該 step 的函式，self-check 會告訴你它們何時能正常運作。請見 [steps/README.zh-TW.md](steps/README.zh-TW.md)。

```powershell
.\scripts\build.ps1 -Only 19-instruments-tempo          # no JUCE needed
.\build\bin\Debug\l19_step01_starter.exe
```

在 Linux / macOS：`./scripts/build.sh --only 19-instruments-tempo`，然後執行 `./build/bin/l19_step01_starter`。

## 1. 學習目標

完成本章後，你可以

- 在由 tempo 跳躍和 ramp 組成的 map 上，於 tick 和時間之間精確地雙向轉換；
- 播放 tempo 在 block 內改變的歌，經過 loop 和 seek；
- 像第 14 章補償 effect 那樣補償樂器的 latency；
- 給 app 讓這些能用的東西：tempo lane、帶 tempo 變化的 MIDI 檔、能 undo 和存檔的樂器設定，以及 plugin 樂器。

## 2. 各個 step

<div align="center">

| Step | 文件 | 新增的內容 | 狀態 |
|---|---|---|---|
| 1 | [Tempo map](docs/step-01-tempo-map.zh-TW.md) | 跳躍和 ramp 的點、ramp 的時間是積分及其解析反函數、`log1p` 和 `expm1`、一個 sample 的第一個 tick | 完成 |
| 2 | [Tempo map 上的 transport](docs/step-02-transport.zh-TW.md) | 帶著 tick 的 segment、以 tick 表示且邊界精確的 loop、播放時換 map 仍保留音樂上的位置、節拍器；什麼跟著 tempo 動、什麼不動 | 完成 |
| 3 | [樂器的 latency](docs/step-03-latency.zh-TW.md) | 樂器的 latency 算進 track 的、第 14 章的補償、現在彈的鍵的代價、low-latency monitoring、不會移動的錄音 | 完成 |
| 4 | [Engine](docs/step-04-engine.zh-TW.md) | 第 18 章 engine 的複本：以 triple buffer 跨 thread 傳遞的 tempo map、帶 tick 的 segment、tempo 改變時不切斷音符的 MIDI player、計入補償的樂器 latency、low-latency monitoring | 完成 |
| 5 | [App：tempo](docs/step-05-app-tempo.zh-TW.md) | 第 18 章 app 的複本：session 裡的 map 和它的一個 edit、跟著它走的 ruler、grid 和 loop、專案檔格式 7、tempo lane、帶 tempo 變化進出的 MIDI 檔 | 完成 |
| 6 | [App：樂器](docs/step-06-app-instruments.zh-TW.md) | track 的樂器用一個 snapshot 和一個 edit、播放時改變音色的 synth 面板、取代 synth 且 latency 有補償的 VST3 樂器，能存檔也能 undo | 完成 |

</div>

## 3. 本章的起點

DawLab 18 的 `Transport` 只有一個 tempo，`sampleOfTick()` 是一個乘法。它的 engine 有第 18 章 Step 7「本章之後加上的」那個 hook：取代 synth 的 `Instrument`，以及在 synth 播放時改變它的設定。第 18 章的練習 4、3、8、9 和 12 做出了 tempo map、MIDI 檔、synth 面板和 plugin 樂器的第一個版本；本章把它們做進 DawLab。

## 4. 練習

見 [exercises/README.zh-TW.md](exercises/README.zh-TW.md)，解答在 [solutions/README.zh-TW.md](solutions/README.zh-TW.md)。前四題已經有了，都不需要 JUCE：本章為什麼這樣做（概念）、用數字看 tempo（計算）、meter map，以及從拍點的時間做出 tempo map。另外兩題和三個延伸 DawLab 19 的選做專題之後才會加上。

## 5. 下一步

從 [Step 1 - Tempo map](docs/step-01-tempo-map.zh-TW.md) 開始。
