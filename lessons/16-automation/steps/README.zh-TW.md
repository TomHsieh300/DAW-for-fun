# 跟著做的 snapshot，第 16 章

[English](README.md) | **繁體中文**

> [第 16 章](../README.zh-TW.md)

每個資料夾都是一個小而完整的程式，內容剛好是做完那一步之後存在的東西。你在 `starter/` 寫出這一步的函式，自我檢查會告訴你
什麼時候完成。

<div align="center">

| Step | 資料夾 | 你要寫的 | 執行 |
|---|---|---|---|
| 1 | `step01-curve` | `AutomationCurve::add`、`remove`、`valueAt`、`render` | `l16_step01_starter` |
| 2 | `step02-playback` | `CurveCursor::locate`、有 cursor 的 `AutomationCurve::render`、`AutomationLane::render`、`limitSlew` | `l16_step02_starter` |

</div>

每個資料夾都有同樣的四個檔案：

- `minidaw.h` - 到目前為止的 model：前面的 step 已經完成，這一步的函式只有宣告（標記為 `YOUR TURN`）。
- `check.h` - 這一步的自我檢查（`main()`），會被 include 在下面兩個程式的最後。它只測這一步加入的東西。
- `starter/stepNN.cpp` - 你的程式碼，留有 TODO。可以無警告地建置，在這一步完成之前會印出 `[FAIL]`。
- `solution/stepNN.cpp` - 一份完成的解答；ctest 以 `l16_stepNN_solution` 執行它（label `solution`）。

在 repository 根目錄用 `.\scripts\build.ps1 -Only 16-automation` 建置（不需要 JUCE），然後在 `build` 資料夾執行程式，例如
`.\bin\Debug\l16_step01_starter.exe`。每一步文件（`../docs/step-NN-*.md`）的「動手做」一節會說明任務並列出預期的輸出。
