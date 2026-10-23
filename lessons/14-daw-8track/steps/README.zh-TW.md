# 跟著做的 snapshot，Step 1-10

[English](README.md) | **繁體中文**

> [第 14 章](../README.zh-TW.md) | 兩條路線如何運作：[如何使用本章](../README.zh-TW.md#如何使用本章)

每個資料夾都是一個小而完整的 engine，內容恰好是該 step 之後所存在的東西。你
在 `starter/` 裡寫出該 step 的函式，self-check 會告訴你它們何時能正常運作。

| Step | 資料夾 | 你要寫的 | 執行 |
|---|---|---|---|
| 1 | `step01-skeleton` | `Engine::processBlock`、`Engine::processChunk` | `l14_step01_starter` |
| 2 | `step02-transport` | `Transport::barsBeatsToSamples`、`samplesToBarsBeats`、`advance` | `l14_step02_starter` |
| 3 | `step03-clips` | `clipFadeAt`、`renderClip` | `l14_step03_starter` |
| 4 | `step04-mixer` | `ChannelStrip::computeStripGains`、`Mixer::isTrackAudible`、`Mixer::processTrack` | `l14_step04_starter` |
| 5 | `step05-insert-effects` | `ParameterInfo::clamp` / `toNormalized` / `fromNormalized`、`EffectBase::setParameter` / `applyPendingParameters`、`InsertChain::process` | `l14_step05_starter` |
| 6 | `step06-sends-buses` | `ChannelStrip::addSend`、帶 send 取樣點的 `Mixer::processTrack` | `l14_step06_starter` |
| 7 | `step07-commands` | `SpscQueue::push` / `pop`、`Engine::applyCommands` | `l14_step07_starter` |
| 8 | `step08-pdc` | `LatencyCompensator::process`、`Mixer::updateLatencies` | `l14_step08_starter` |
| 9 | `step09-offline-render` | `renderOffline` | `l14_step09_starter` |
| 10 | `step10-realtime-safety` | 計算 `operator new` / `delete` 次數、`audit::renderWithBlockSizes` | `l14_step10_starter` |

每個資料夾都有相同的四個檔案：

- `minidaw.h` - 到目前為止的迷你 engine：先前的 step 已解完，這個 step 的函式只有宣告
  （標記為 `YOUR TURN`）。
- `check.h` - 該 step 的 self-check（`main()`），被 include 在下面兩支程式的結尾。它
  只測試這個 step 新增的東西。
- `starter/stepNN.cpp` - 你的程式碼，帶有 TODO。可無 warning 地 build，在
  step 完成前會印出 `[FAIL]`。
- `solution/stepNN.cpp` - 一份示範解答；ctest 會把它當作 `l14_stepNN_solution` 執行
  （label 為 `solution`）。

從 repository 根目錄用 `.\scripts\build.ps1 -Only 14-daw-8track` build（不需要 JUCE），並
從 `build` 資料夾執行程式，例如 `.\bin\Debug\l14_step03_starter.exe`。每份 step 文件
（`../docs/step-NN-*.zh-TW.md`）的「Your turn」章節會說明任務並展示
預期輸出。
