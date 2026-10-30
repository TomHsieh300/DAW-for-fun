# 跟著做的 snapshot，第 15 章

[English](README.md) | **繁體中文**

> [第 15 章](../README.zh-TW.md)

每個資料夾都是一個小而完整的程式，內容恰好是該 step 之後所存在的東西。你在 `starter/` 裡寫出
該 step 的函式，self-check 會告訴你它們何時能正常運作。

<div align="center">

| Step | 資料夾 | 你要寫的 | 執行 |
|---|---|---|---|
| 1 | `step01-track-ids` | `TrackList::add`、`remove`、`indexOf`、`move` | `l15_step01_starter` |

</div>

每個資料夾都有相同的四個檔案：

- `minidaw.h` - 目前為止的 model / engine：前面 step 的部分已經解好，這個 step 的函式只有宣告
  （標記為 `YOUR TURN`）。
- `check.h` - 該 step 的 self-check（`main()`），放在下面兩個程式的結尾 include。它只測試這個
  step 新增的東西。
- `starter/stepNN.cpp` - 你的程式碼，帶有 TODO。編譯不會有 warning，在 step 完成之前會印出
  `[FAIL]`。
- `solution/stepNN.cpp` - 一份解答；ctest 以 `l15_stepNN_solution` 執行它（label `solution`）。

在 repository 根目錄用 `.\scripts\build.ps1 -Only 15-daw-growing` 建置（不需要 JUCE），然後在
`build` 資料夾執行程式，例如 `.\bin\Debug\l15_step01_starter.exe`。每個 step 文件
（`../docs/step-NN-*.md`）的「換你做」一節會說明任務並列出預期輸出。
