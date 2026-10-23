# 授權

[English](LICENSING.md) | **繁體中文**

本專案依檔案類型採用兩種授權：

| 內容 | 授權 | 檔案 |
|---|---|---|
| 原始碼：`*.cpp`、`*.h`、`CMakeLists.txt`、`*.cmake`、腳本 | MIT | [LICENSE](LICENSE) |
| 教材文字與圖片：`*.md`（所有語言）、`docs/images/`、`lessons/*/docs/images/` | Creative Commons Attribution 4.0 International (CC BY 4.0) | [LICENSE-docs](LICENSE-docs) |

Copyright (c) 2026 Tom Hsieh.

重用教材文字（例如用於自己的課堂筆記或翻譯）時，CC BY 4.0 要求標示出處：註明「DawLab by Tom Hsieh」，附上本 repository 與授權的連結，並說明你是否修改過內容。

## 第三方軟體

第三方程式碼**不屬於**本 repository，各自保有原授權。建置時會從 `third_party/` 取得或使用：

- **JUCE**（僅第 13、14 章）。JUCE 9 modules 採雙重授權：[AGPLv3](https://www.gnu.org/licenses/agpl-3.0.en.html) 與商業的 [JUCE 9 End User Licence Agreement](https://juce.com/legal/juce-9-licence/)。本 repository 中 MIT 授權的程式碼可以與 JUCE 結合，但**若你散布以 JUCE 建置的 plugin 或應用程式（例如第 13 章的 plugin 或 DawLab 8），整個結合後的作品必須遵守 AGPLv3**，實務上代表必須以 AGPLv3 公開完整原始碼，除非你持有 JUCE 商業授權。
- **VST3 SDK**（內含於 JUCE）：MIT License，Copyright (c) 2025 Steinberg Media Technologies GmbH。使用「VST」名稱或標誌須遵守 Steinberg 的使用指引。
- 第 00–12 章與 DAW engine（`lessons/14-daw-8track/engine/`）僅使用 C++ standard library，不受 JUCE 授權影響。

本摘要僅供參考，不構成法律意見；授權條文以原文為準。
