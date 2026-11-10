# 第 19 章：樂器與 tempo - tempo map、樂器的 latency，以及 app 裡的樂器

[English](README.md) | **繁體中文**

> **狀態：進行中。** 6 個 step 中的第 1 個。

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
| 2 | Tempo map 上的 transport | 跨 tempo 變化的 block、loop 和 seek；tempo 改變時什麼會動（音符）什麼不會（音訊、automation） | 規劃中 |
| 3 | 樂器的 latency | 樂器的 latency 算進 track 的、第 14 章的補償、鍵盤與錄音 | 規劃中 |
| 4 | Engine | 第 18 章 engine 的複本，加上 tempo map 和樂器 latency | 規劃中 |
| 5 | App：tempo | 第 18 章 app 的複本：tempo lane、undo、專案檔、帶 tempo 變化的 MIDI 檔 | 規劃中 |
| 6 | App：樂器 | undo 和專案檔都認得的 synth 面板、track 上的 VST3 樂器 | 規劃中 |

</div>

## 3. 本章的起點

DawLab 18 的 `Transport` 只有一個 tempo，`sampleOfTick()` 是一個乘法。它的 engine 有第 18 章 Step 7「本章之後加上的」那個 hook：取代 synth 的 `Instrument`，以及在 synth 播放時改變它的設定。第 18 章的練習 4、3、8、9 和 12 做出了 tempo map、MIDI 檔、synth 面板和 plugin 樂器的第一個版本；本章把它們做進 DawLab。

## 4. 下一步

從 [Step 1 - Tempo map](docs/step-01-tempo-map.zh-TW.md) 開始。
