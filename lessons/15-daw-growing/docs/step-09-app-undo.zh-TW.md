# Step 9 - 在 app 中 undo 與 redo

[English](step-09-app-undo.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 8 - 在 app 中存檔與開啟](step-08-app-files.zh-TW.md)

## 目標

在 DawLab 15 加上 Edit > Undo（Ctrl+Z）和 Redo（Ctrl+Shift+Z 或 Ctrl+Y）。選單會寫出這一步是什麼，例如「Undo Delete track Bass」。
有未儲存的變更時，視窗標題會顯示 `*`；New、Open、demo song 和 Quit 在丟掉變更之前會先詢問。Step 5 在一個小 model 上設計了
這套機制；這一步讓 application 能做的每一個變更都經過它。

## 設計

### Edit：會回傳反向操作的單純資料

和 Step 5 的想法完全一樣：一個 **edit** 是說明要改什麼的單純資料，一個函式負責執行它，*並且*回傳能把它還原的 edit。

```cpp
std::optional<Edit> Session::applyEdit(const Edit& edit);   // 執行變更，回傳它的反向操作
```

undo 就是套用反向操作，redo 就是套用反向操作的反向操作。不需要為每種 edit 另外寫 undo 的程式。app 需要十種
（`app/Source/Edits.h`）：

<div align="center">

| Edit | 改什麼 | 它的反向操作 |
|---|---|---|
| `AddTrack` | 從 `TrackSnapshot` 把 track 放回去（或新增一個） | `RemoveTrack` |
| `RemoveTrack` | 刪除 track 和它的 clip、insert | 帶著整個 track 的 `AddTrack` |
| `SetValue` | gain、pan、mute、solo、send 的大小 | 原本的值 |
| `Rename` | strip 的名稱 | 原本的名稱 |
| `SetFormat` | mono / stereo | 原本的格式 |
| `SetClips` | track 的 clip 清單（移動 clip、載入檔案） | 原本的清單 |
| `SetInsert` | 從 `InsertSnapshot` 新增、替換或移除 insert | 原本的 slot 的 snapshot |
| `SetParameter` | insert 的一個參數 | 原本的值 |
| `SetBypass` | insert 的 bypass | 原本的狀態 |
| `SetTempo` | tempo | 原本的 tempo |

</div>

哪些可以 undo，沿用 Step 5 的規則：專案檔（Step 8）有存的就可以 undo；只負責播放或顯示專案的就不行（transport、loop、選取、
縮放）。程式裡寫明了一個例外：在 VST3 plugin 自己的編輯視窗裡做的變更會直接送到 plugin。這些變更會跟著 plugin 的 state
存檔，但 app 看不到它們，所以無法 undo。

### 用名稱，不用位置

edit 用 `StripTarget` 指定 strip：track **用 id**，bus 用 index，或是 master。它從不指定 engine slot 或顯示位置。刪除 track
再 undo，它會帶著同樣的 id 回來（Step 1 的承諾），但可能在另一個 engine slot，其他 track 也可能已經移動過。較舊的 edit 仍然
找得到它，因為 edit 在套用時才用 id 查 track 在哪裡。

為了讓刪除的 track 帶著原本的 id 回來，`Session` 多了一種新增 track 的方式：用它以前發過的 id（`addTrackWithId`）。id 計數器
仍然只會往上數，所以 Step 7 的規則依然成立：一個 id 絕不會發給*別的* track。

### Snapshot：被刪除的東西原本是什麼

`RemoveTrack` 的反向操作必須重建整個 track。所以刪除之前，`Session` 會取一份 `TrackSnapshot`：名稱、格式、fader、pan、mute、
solo、send、四個 `InsertSnapshot`，以及 clip 清單。clip 共用它們的音訊 buffer，所以 snapshot 很便宜。

`InsertSnapshot` 足以重建同樣的效果器。內建效果器存它的類型和每個參數值。VST3 plugin 存 plugin 檔案和 plugin 回傳的
state，和 Step 8 一樣。重建 plugin 需要第 13 章的 `PluginHost`，而 `Session` 不認識它，所以由 `MainComponent` 交給它一個
factory：`Session::createVst3`。沒有這個 factory，這種 edit 就套用不了。

### 歷史紀錄

`History`（`app/Source/History.h/.cpp`）就是 Step 5 的 `History`。它透過 `Session` 實作的 `EditTarget` 介面套用 edit，並給
每一筆紀錄一個顯示在選單上的名稱：

- **全有或全無。** 由好幾個 edit 組成的變更（載入檔案：格式、clip、名稱）是一筆紀錄。其中一個 edit 套用不了，已經套用的
  就會被還原。
- **merge key。** 拖動 fader 時，每次滑鼠移動都會執行一個 edit。key 相同的移動（「track 17 的 gain」）都合併成一筆。每個
  控制項在拖動**開始**和**結束**時都會結束合併，所以拖兩次就是兩個 undo 步驟。按一下或選一個選項沒有拖動：它在自己之前和
  之後都結束合併，所以它自己就是一步。
- **序號。** 每筆紀錄都有一個永不重用的序號。存檔時記住最新紀錄的序號；最新紀錄換成別筆時，專案就算修改過。所以 undo
  可以讓專案回到未修改的狀態，redo 又會讓它變成修改過。
- **New、開檔、demo song、換音訊裝置** 都會重新開始歷史紀錄。換裝置會把每個 clip 轉到新的 rate，而舊紀錄裡存的是舊 rate
  的 clip 清單。它忘掉的是紀錄（`forgetEntries()`），不是「有沒有未存的變更」：之前編輯過，轉換後專案仍然算修改過；之前
  存過檔，轉換本身不算變更。
- **沒有紀錄的變更**——當機後救回的 take（第 17 章）——會呼叫 `markModified()`：在下次存檔前都算未存，不管 undo、redo 怎麼做。

### 每個變更都經過 `perform()`

直接修改 session 的函式（`setClips`、`removeTrack`……）仍然存在。`applyEdit()` 就是用它們組成的，File > New、demo song 和
開檔也是。但**使用者**做的每個變更現在都經過 `Session::perform(edits, name, mergeKey)`：

<div align="center">

| 在哪裡 | 什麼 |
|---|---|
| mixer strip | fader、pan、send 旋鈕、mute、solo、名稱、mono/stereo、master 的 ceiling |
| arrangement | 拖動 clip（`Session::moveClip`） |
| 效果器面板 | 每個旋鈕、開關和選項，以及 bypass（`ParameterPanel::onParameterChange`） |
| transport bar | tempo（`TransportBar::onTempoChange`） |
| 指令 | 新增 / 刪除 track、選取 track 的 mute / solo、slot 選單裡的 insert |
| 載入檔案 | 格式、clip 和名稱合成一筆紀錄 |

</div>

面板和 transport bar 只透過 callback 回報變更；由 `MainComponent` 把它轉成 edit。沒有 callback 時，它們仍然直接寫入
效果器或 transport，所以單獨使用時也能運作。

一個 undo 步驟在哪裡結束，由控制項所在的地方決定。拖一次旋鈕是一步：面板在拖動開始和結束時呼叫 `onGesture`（它會結束
merge）。在清單裡選一個選項或按一下開關沒有拖動，每一次都是自己的一步：面板在修改之前和之後都呼叫 `onGesture`
（`ParameterPanel::changeAsOneGesture`）。少了這一步，緊接在第一個選項之後的第二個選項會帶著同一個 merge key 併進去，
一次 undo 就會把兩個都收回。

### 丟掉變更之前先詢問

New、Open、demo song 和 Quit 都會經過 `MainComponent::askToSaveChanges(then)`。歷史紀錄顯示專案修改過時，會跳出
**Save / Don't save / Cancel**。選 Save 時，如果專案還沒有檔案，會先詢問檔案，存檔成功後才繼續。選 Cancel 就什麼都不做。
app 的 `systemRequestedQuit()` 也用同樣的方式詢問，所以關閉視窗也不會丟失變更。

## 測試

smoke test 的 6e 和 6f 段：

- **刪除後 undo。** 用指令刪除 bass。標題出現 `*`，Undo 選項寫著「Undo Delete track Bass」。undo 後，bass 帶著 id 10、16 個
  clip 和 2 個 insert 回到第 2 個 lane，整個專案和原本的文字（Step 8 的格式）相同，標題也回到未修改。redo 會再刪除一次。
- **手勢。** 兩次 fader 拖動，分別移動 3 次和 2 次，得到兩筆紀錄。第一次 undo 回到 -14 dB（第一次拖動的終點），第二次回到 -3 dB。
- **效果器面板。** compressor 的 threshold 旋鈕從 -20 轉到 -35，undo 回到 -20。Saturation 的 curve 連續選兩次、EQ 某個
  band 的開關連按兩次，各是兩步：一次 undo 只收回第二次。
- **存檔與否。** 存檔後標題是乾淨的。mute 加上 `*`，undo 讓它變乾淨，redo 又加上。
- **VST3。** 刪除一個有 VST3 insert 的 track（用測試 plugin 代替檔案）再 undo：plugin 透過 `createVst3` 重建。
- **隨機操作。** 150 個隨機 edit，涵蓋測試用的全部 14 種（新增、刪除、有拖動和沒拖動的 gain、pan、mute、solo、send、改名、
  格式、移動 clip、insert、參數、bypass、tempo）；其中 129 個套用成功。全部 129 個 undo 後，專案文字和原本相同。全部 129 個
  redo 後，得到最後的狀態。沒有任何 leak。

### 一個 float 的差距：加總的順序

隨機操作全部 undo 之後，專案的文字相同，但聲音差了 `1.79e-07`，大約是 float 的一個捨入單位。測試計算了哪些 track 回到了
另一個 **engine slot**：8 個中有 3 個。engine 依 slot 順序把 track 加總，而 float 用不同順序相加，捨入的結果就不同。差異
完全來自這裡，所以測試現在要求：沒有 track 換 slot 時必須完全相同，有換 slot 時差異要小於 `1e-6`（-120 dB）。全部 redo 之後
聲音又完全相同，因為那時 slot 也一致。

### 什麼都沒印出來的 stack overflow

新的測試段第一次執行時，以 `0xC00000FD` 結束，什麼都沒印出來，連測試的第一行都沒有：

- **為什麼什麼都沒印。** 寫進 pipe 的 stdout 有緩衝，程式當掉時緩衝也跟著消失。smoke test 現在不緩衝 stdout，當機不會
  再把印出來的東西藏起來。
- **為什麼當掉。** 一個 `MainComponent` 帶著有 128 個 track slot 的 engine，有好幾 MB。Debug 建置會一次為函式裡*每一個*
  區塊保留 stack，兩個新的測試段讓 `runSmokeTest()` 超過了 1 MB 的 stack。新測試段的視窗現在放在 heap 上。

### 故意弄壞

<div align="center">

| 故意弄壞的地方 | 被誰抓到 |
|---|---|
| track snapshot 沒有存 insert | 6e：bass 回來時沒有 insert；6f：專案不同 |
| undo 把刪除的 track 放到最後面，而不是原本的位置 | 6e 和 6f：專案不同 |
| fader 從不結束合併 | 6e：兩次拖動變成一筆紀錄 |
| 效果器面板直接寫入效果器 | 6e：undo 沒有把 threshold 改回來 |
| 選項或開關沒有結束它的合併 | 6e：選兩次或按兩次變成一筆紀錄 |
| 存檔時沒有標記歷史紀錄 | 6e：存檔後標題仍然有 `*` |

</div>

有兩個反向測試一開始沒被抓到，兩個都是測試本身不夠嚴謹，不是程式碼剛好能動：

- **fader 的反向測試當掉了。** 測試呼叫 fader 的 `onDragStart` 前沒有檢查它是否存在。現在測試和真正的拖動一樣，只在
  callback 存在時才呼叫。
- **存檔的反向測試通過了。** 存檔那一刻，測試已經把所有變更 undo 掉，歷史紀錄是空的，乾淨的標題什麼也證明不了。現在
  測試會先幫一個 track 改名，讓存檔真的有東西可存。

## 動手試試

```powershell
.\scripts\build.ps1 -Only 15-daw-growing -Juce
& ".\build\bin\Debug\DawLab 15.exe"
```

刪除幾個 track、移動 clip、轉動效果器面板裡的旋鈕，然後一直按 Ctrl+Z，直到選單說沒有東西可以 undo，再用 Ctrl+Shift+Z 全部
做回來。存檔，改點東西，看 `*` 隨著 undo 和 redo 出現又消失。在有未儲存變更時關閉視窗。

## 這一步的限制

- 在 VST3 plugin 自己的編輯視窗裡做的變更無法 undo（見上文）。
- 歷史紀錄保留 200 筆，不會跟著專案存檔。
- undo 刪除時，track 的 plugin 是從存下來的 state 重建的。已經沒有安裝的 plugin 回不來，那一個 undo 步驟就套用不了。

## 你學到了

- undo 就是「套用反向操作」：每種 edit 一個函式，既負責修改，也描述回去的路，歷史紀錄只保存這些值。
- 在 edit 裡用永遠不變的東西（id）指定對象，到套用時才查它在哪裡。
- 被刪除的東西用值的 snapshot 保存。只有別的層能建立的東西（plugin），就由那一層傳進來的 factory 建立。
- 一個手勢就是一步：在手勢開始和結束的地方結束合併。
- 「修改過」是歷史紀錄的性質（存檔時的序號），而不是每個變更都得記得設定的旗標。
- 結果幾乎相等時，先找出原因，再放寬容許誤差。這裡的原因是 float 加總的順序，而測試現在明確說出什麼時候允許差異。
