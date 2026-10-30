# 第 15 章：讓 DAW 長大 - 可以新增的 track、可以存檔的專案、可以 undo 的編輯

[English](README.md) | **繁體中文**

> **狀態：進行中。** Step 1 已完成；Step 2-6 還在規劃中，列在下面讓你看到本章接下來的方向。

第 14 章刻意把 engine 做成 *static*：8 個 track、2 個 FX bus，所有容器都在 `prepare()` 一次配置
好，audio thread 永遠不用擴充任何東西。這讓 real-time 的規則很容易遵守。但一個真的能拿來用的 DAW
需要更多：可以新增和刪除的 track、可以存檔再打開的專案，還有 undo 按鈕。

本章一步一步加上這些功能，而且**不放棄第 14 章的 real-time 規則**。每個 step 先說明當固定大小
變成可變大小時哪裡會壞掉，再把它修好。第 14 章本身保持不變：它是起點，比較兩章的差異也是課程的
一部分。

## 如何使用本章

跟第 14 章跟著做的路線一樣，每個 step 都是 `steps/stepNN-*/` 裡一個小而完整的 snapshot：你在
`starter/` 裡寫出該 step 的函式，self-check 會告訴你它們何時能正常運作。請見
[steps/README.zh-TW.md](steps/README.zh-TW.md)。

```powershell
.\scripts\build.ps1 -Only 15-daw-growing          # no JUCE needed
.\build\bin\Debug\l15_step01_starter.exe
```

在 Linux / macOS：`./scripts/build.sh --only 15-daw-growing`，然後執行 `./build/bin/l15_step01_starter`。

## 1. 學習目標

完成本章後，你可以

- 解釋為什麼一旦 track 可以刪除或移動，index 就不能再用來指稱 track，並用穩定的 id 取代它；
- 讓使用者在 audio thread 持續運作時新增和刪除 track，而且 audio thread 上依然不配置記憶體、
  不上鎖；
- 判斷 track 數量真正受限於什麼（記憶體還是 CPU），並在電腦快撐不住時告訴使用者；
- 把專案存成檔案再打開，即使是用之後版本的程式打開；
- 在 engine 已經在用的 command 之上實作 undo / redo。

## 2. 各個 step

<div align="center">

| Step | 文件 | 新增內容 | 狀態 |
|---|---|---|---|
| 1 | [穩定的 track ID](docs/step-01-track-ids.zh-TW.md) | `TrackId`、發出永不重複 id 的 track list；為什麼 index 會出錯 | 完成 |
| 2 | Track 池 | `kMaxTracks` 個預先配置的 slot、新增 / 刪除 track 的 command、id -> slot 對應；用 ThreadSanitizer 檢查 | 規劃中 |
| 3 | CPU 預算 | callback 負載與 xrun 次數；為什麼真正的上限是 CPU 時間，而不是 track 數 | 規劃中 |
| 4 | 存檔與開啟專案 | 以 track id 為基礎、帶版本的專案檔；在不同 sample rate 下開啟 | 規劃中 |
| 5 | Undo 與 redo | 可以反轉的 command、undo 歷史、哪些操作應該（或不應該）可以 undo | 規劃中 |
| 6 | App | 可以捲動的 arrangement 與 mixer、GUI 中新增 / 刪除 track、File 選單 | 規劃中 |

</div>

## 3. 本章的起點

第 14 章裡所有指向 track 的東西都用 0 到 7 的 index：`Mixer` 裡的 `std::array<Track, kNumTracks>`、
command 裡的 `ChannelRef::track(i)`、`Session::clips_`、`ArrangementView` 的 lane。`kNumTracks`
出現在第 14 章的 34 個檔案中。Step 1 在動到這些容器之前，先處理問題的根源 - 到底該怎麼指稱一個 track。

## 4. 下一步

從 [Step 1 - 穩定的 track ID](docs/step-01-track-ids.zh-TW.md) 開始。
