# 跟著做的 snapshot，第 20 章

[English](README.md) | **繁體中文**

> [第 20 章](../README.zh-TW.md)

每個資料夾都是一個小而完整的程式，內容正好是該 step 完成後存在的東西。你在 `starter/` 裡寫出該 step 的函式，self-check 會告訴你它們何時能正常運作。

<div align="center">

| Step | 資料夾 | 你要寫的 | 執行 |
|---|---|---|---|
| 1 | `step01-warp-map` | `WarpMap::sourceAt`、`tickAt`、`sourceAtSample`、`songSecondsOf`、`speedAt`、`markersForLoop`、`markersForTake` | `l20_step01_starter` |
| 2 | `step02-follow-start` | `setFollow`、`startUnder`、`retime`、`moveTo`、`moveToTick`、`endTick`、`gaps`、`jumpInClip` | `l20_step02_starter` |
| 3 | `step03-varispeed` | `semitonesOf`、`readAt`、`positionsFor`、`firstSample`、`endSample`、`renderWarped` | `l20_step03_starter` |

</div>

每個資料夾都有同樣的四個檔案：

- `minidaw.h` - 到目前為止的模型：前面的 step 已經解好，這個 step 的函式只有宣告（標記為 `YOUR TURN`）。
- `check.h` - 該 step 的 self-check（`main()`），放在下面兩個程式的結尾 include。它只測試這個 step 新增的東西。
- `starter/stepNN.cpp` - 你的程式碼，含 TODO。可以無警告地建置，在該 step 完成前會印出 `[FAIL]`。
- `solution/stepNN.cpp` - 一份解答；ctest 會以 `l20_stepNN_solution` 執行（label `solution`）。

資料夾旁邊的 `tempo_map.h` 是第 19 章解好的 tempo map：每個 step 都從它開始。`warp_map.h` 是解好的 Step 1，給 Step 3 和 4 用。

在 repository 根目錄用 `.\scripts\build.ps1 -Only 20-audio-tempo` 建置（不需要 JUCE），並從 `build` 資料夾執行程式，例如 `.\bin\Debug\l20_step01_starter.exe`。每個 step 文件（`../docs/step-NN-*.zh-TW.md`）的「換你了」一節會說明任務並列出預期的輸出。
