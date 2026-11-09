# 第 15 章 - 練習
[English](README.md) | **繁體中文**

[回到本章](../README.zh-TW.md) | [解答](../solutions/README.zh-TW.md)

難度：★☆☆ 簡單、★★☆ 中等、★★★ 困難。實作題在 `exercises/starter/` 有一個可以編譯、可以執行的 starter 檔；在你完成之前，它的自我檢查會印出 `FAIL`（這是預期的）。方括號裡是 build target 的名稱。這些練習都不需要 JUCE：

```powershell
.\scripts\build.ps1 -Only 15-daw-growing
.\build\bin\Debug\l15_ex3_starter.exe
```

**練習 1-6** 是本章的練習，針對各個 step（不需要 JUCE）。**練習 7-9 是選做的專題**，用來擴充 DawLab 15 這個 app。它們需要 JUCE build（`.\scripts\build.ps1 -Only 15-daw-growing -Juce`）；和第 14 章的 JUCE 練習一樣，它們是 console 程式，在 offline 模式下建立真正的 `MainComponent`，所以檢查時不需要音訊裝置、也不用點滑鼠，而且都不修改 app 本身。

練習 3 和 5 建立在 Step 5 的 model 上（`exercises/model.h`：Step 5 的 `minidaw.h`，函式都已經寫好），練習 4 建立在 Step 4 的 model 上。請先做完這些 step。

---

## 練習 1 - 被刪掉的 track 歸誰？ ★☆☆（概念）

每題用幾句話回答。

1. 第 14 章用 index（0-7）來稱呼 track。舉出一串具體的使用者操作，讓一筆寫著「設定 track 3 的 gain」的 undo 紀錄改到錯的 track。為什麼 `TrackId` 在同樣的操作下不會出錯？
2. 使用者刪掉 Bass。哪個 thread 釋放什麼：UI 在 slot table 裡的那一筆、audio thread 上的 slot、Bass 的 clip source？每一樣什麼時候釋放才安全，由誰決定？
3. `removeTrack()` 立刻把 slot 1 交給下一次 `addTrack()`，這時 audio thread 還沒處理那個 remove。為什麼不用等回覆也安全？如果 command 走兩條 queue 而不是一條，會壞掉什麼？
4. Undo「新增 track」會移除那個 track，但 id 計數器不會倒退；開啟專案時會從檔案讀取 `nextTrackId`。各舉一個例子：如果 undo 之後計數器倒退了，或者檔案沒有存計數器，會出什麼錯？
5. Step 5 說：存在專案檔裡的東西可以 undo，只是顯示或播放專案的東西不行。把下列項目分類，並為最不明顯的兩項說明理由：重新命名 track、playhead 的位置、縮放 arrangement、solo 一個 track、移動 clip、選取的 track、CPU meter 的警告、拖曳 fader。

## 練習 2 - 手算 CPU budget ★★☆（計算）

一台 48 kHz 的機器，處理 $`n`$ 個 track、長度 $`N`$ samples 的 block 需要

```math
t = o + N\,(f + n\,c), \qquad o = 50\ \mu\text{s},\ f = 0.40\ \mu\text{s},\ c = 0.15\ \mu\text{s}
```

（$`o`$ 是 callback 本身，$`f`$ 是 master 和 bus 每個 sample 的成本，$`c`$ 是每個 track 連同它的 insert 每個 sample 的成本）。先手算（寫出公式），再執行 [`l15_ex2_solution`] 核對：

- (a) 64、256 和 1024 samples 的 block 各有多少 budget。
- (b) 20 個 track 在這三種大小下的 load。每個 sample 的成本都一樣，為什麼 load 不一樣？
- (c) 每種大小下，最多幾個 track 能維持在 `CpuMonitor` 的 High 門檻（75 %）以下。
- (d) 每 200 個 block 有一個要花 2.2 倍的時間（一次 page fault、一個 plug-in 在清理）。100 個 track、256 samples 時會怎樣：平均 load、慢的那個 block 的 load、每分鐘幾次 overrun，以及 `CpuMonitor` 會顯示什麼（它會把 Overload 保持 2 秒）？幾個 track 以下永遠不會 overrun？
- (e) 穩定的 40 % load，然後一個 130 % 的 block。在 64 和 256 samples 時，`CpuMeter` 緊接著會顯示多少（$`\tau = 0.3`$ s）？使用者從 meter 得知什麼，又從 overrun 次數得知什麼？

## 練習 3 - 複製 track ★★☆（實作）

[`l15_ex3_starter`，檔案 `exercises/starter/ex3_duplicate_track.cpp`]

在 Step 5 的 model 上寫「Duplicate Track」指令，分成兩個函式：

- `copyName(project, name)`：「Bass」→「Bass (2)」，如果已經有人用了就是「Bass (3)」。結尾的「 (n)」（n 是數字）是編號，不是名字的一部分，所以「Bass (2)」的複本是「Bass (3)」，不是「Bass (2) (2)」。用最小的空號。
- `duplicateTrack(project, id)`：複製這個 track 的 `Change`；沒有這個 track 就什麼都不回傳。它是一個 `AddTrack`：下一個 id、複本的名字、相同的 gain 和 pan、緊接在原本的 track 後面，並把原 track 每個 clip 的複本加到 clip 清單的最後。

自我檢查會複製 Bass 兩次、再複製一次它的複本，undo 和 redo，刪掉原本的 track 並要求複本保有自己的 clip，最後全部 undo：專案和原來一樣，只是沒有任何 id 回來。

開始之前想想：為什麼複製不需要新的 edit 種類，也不需要新的 undo 程式？

## 練習 4 - 格式 3，以及來自新版本的行 ★★★（實作）

[`l15_ex4_starter`，檔案 `exercises/starter/ex4_format3.cpp`]

Starter 是 Step 4 的讀檔和寫檔程式，改用 `exercises/ex4_format3.h` 的型別。把它改成：

- **格式 3** 在每個 track 加上 mute 旗標（`track 2 "Bass" -6 0.25 1`）。格式 1 和 2 的檔案仍然能開，所有 track 都沒有 mute；程式寫出的是格式 3。
- 版本可以有 **minor 號碼**：`DawLabProject 3.2`。Minor 版本只會增加舊程式可以跳過的行。較新 minor 版本的檔案可以開啟：每一行未知 keyword 的行都原封不動地保留（附上註明行號的警告），存檔時寫回去，放在已知的行之後。這時檔案要再寫成 `DawLabProject 3.2`，否則這個程式會拒絕它自己寫出的檔案。
- 較新的 **major** 版本（4、4.0）仍然拒絕；這個程式完全認識的版本（3、3.0、2）裡出現未知 keyword 也拒絕：那個檔案是壞掉，不是比較新。

自我檢查會開啟格式 1、2、3 和 3.2 的檔案（含 CRLF 換行，以及保留的行裡有註解），存檔後再開一次，並拒絕 `3.`、`.2`、`3.1.1` 這類錯誤的檔頭。

想一想：3.2 保留下來的一行寫著 `colour 2 "#ff8800"`，是 track 2 的顏色。使用者在這個舊版本裡刪掉 track 2 並存檔。新版本會看到什麼，又該怎麼做？這告訴你 minor 版本可以增加哪些種類的行？

## 練習 5 - 用記憶體限制 undo history ★★★（實作）

[`l15_ex5_starter`，檔案 `exercises/starter/ex5_byte_budget.cpp`]

Step 5 的 history 保留最後 100 筆，不管裡面是什麼。但刪除 track 的反向操作握著它的 clip，而在 app 裡 clip 把音訊放在記憶體中：只要 undo 紀錄還能把它帶回來，那段音訊就不能釋放。寫一個 `BudgetHistory`，在 byte budget 之內保留紀錄：

- `bytes()` 是所有 undo 和 redo 紀錄的 `changeBytes()`（已提供）總和，隨紀錄增減即時更新；
- 超過 budget 時，先丟掉最舊的 undo 紀錄；沒有 undo 紀錄了，再丟掉最遠的 redo 紀錄；
- 最後剩下的一筆永遠不丟，即使它自己就超過 budget。

Starter 是沒有計算 byte 的 Step 5 history。自我檢查會在 10 MB 的 budget 下刪掉 4 MB 的 track、做一千個都放得下的小編輯、合併一次 fader 拖曳、undo 一次新增（這會讓 redo 紀錄比原本的 undo 紀錄**更大**），並執行 4000 次隨機的編輯、undo 和 redo，每次都拿 `bytes()` 和完整重算的結果比較。

想一想：budget 計算的是被刪掉的 clip 的音訊。如果同一段音訊也被專案裡還在的 clip 使用呢？

## 練習 6 - 存檔時什麼都不遺失 ★★☆（設計）

Step 4 透過暫存檔和 rename 來存檔。用文字設計 app 在此之外如何保護使用者的成果，包括：

1. **Autosave。** 寫到哪裡（不覆蓋使用者的檔案——為什麼不行？）、多久一次、在哪個 thread 上。寫出一個含 VST3 state 和音訊路徑的專案要花時間：使用者繼續編輯的同時，autosave 可以從 session 讀取什麼？
2. **當機之後。** 下次啟動時，app 怎麼知道 autosave 比專案檔新？它要問使用者什麼？
3. **音訊。** 專案檔會指名音訊檔；錄好或匯入的檔案可能還沒放到它旁邊。在把指名它的專案檔 rename 到定位之前，磁碟上必須已經有什麼？
4. **開啟時缺檔。** Step 8 開啟音訊檔不見的專案時會給警告。App 應該保留什麼，才不會在下次存檔時永遠失去那個 clip？

## 練習 7 - 拖曳重排 track ★★☆（實作，JUCE）

[`l15_ex7_starter`，檔案 `exercises/starter/ex7_reorder_tracks.cpp`]

DawLab 15 能新增和刪除 track，卻不能移動。從 app 外面把它加上：

- `moveTrack(session, id, index)`：把 track 移到顯示位置 `index`（會 clamp），算**一個** undo 步驟「Move Track」。`Session` 沒有移動用的 edit，但 `RemoveTrack` 加上一個把 track 快照（`Session::snapshotTrack`）放回新位置的 `AddTrack`，合起來就是移動，而 history 早就知道這兩者的反向操作。
- `TrackDragger`，`ArrangementView` 的一個 `juce::MouseListener`：在 track 標頭按下、上下拖曳、放開。少於 4 pixel 算是點擊（會選取）；拖到 lane 上方就代表「移到最上面」。拖曳時 `getDropIndex()` 告訴你那條指示線該畫在哪裡。

自我檢查會移動一個 track，要求 id、名稱、gain、insert 和 clip（同樣的音訊 buffer）都不變、只有一筆 undo 紀錄、undo 和 redo 都正常，而且什麼都沒移動時不留紀錄。接著它送出滑鼠事件：跨過 lane 邊緣的一次點擊、從 lane 0 拖到 lane 2（選取跟著 track 走）、往上拖進 ruler，以及在 clip 上按下。

想一想：「移除再加回來」在 audio thread 上有什麼成本，是真正的移動 edit 不會有的？什麼時候你會改成在 `Session` 裡加一個 `MoveTrack` edit？

## 練習 8 - 最近開過的專案和 autosave ★★☆（實作，JUCE）

[`l15_ex8_starter`，檔案 `exercises/starter/ex8_recent_and_autosave.cpp`]

練習 6 做了設計，現在實作其中兩部分：

- `RecentProjects`：最近 8 個專案，最新的在前，存在 app 的設定（`juce::PropertiesFile`）裡，而且立刻寫入。UI timer 呼叫 `follow(main.getProjectFile())`，就能記下每個新開啟或新存檔的專案，不必改動 Open 或 Save。`addToMenu()` 加上子選單「Open Recent」（每個還存在的檔案一項，接著是「Clear Menu」；或是一項停用的「No recent projects」），給 `MainComponent::onExtendMenu` 用。
- `Autosaver::poll(now)`：最多一分鐘一次，只要 history 顯示專案有修改、而且內容和上次 autosave 不同，就用 `ProjectIO::save` 把專案寫到 `Song.dawlab` 旁邊的 `Song.autosave.dawlab`（或 app 某個資料夾裡的 `Untitled.autosave.dawlab`），絕不覆蓋使用者的檔案，也不把它標成已存檔。專案不再處於修改狀態時——存檔了，或 undo 回去了——就刪掉 autosave。`newerAutosave()` 找出比專案新的 autosave：那是當機留下來的。

自我檢查會把清單塞超過 8 個、刪掉一個檔案、重新讀取設定、透過選單項目的 action 開啟專案，並讓 autosave 經歷一分鐘的 poll、一次當機（autosave 開起來和當時的專案一樣）、一次存檔、一次 undo 回到存檔狀態，以及一個沒有檔名的專案。

## 練習 9 - 開啟前找回不見的音訊 ★★★（實作，JUCE）

[`l15_ex9_starter`，檔案 `exercises/starter/ex9_relink_audio.cpp`]

Step 8 開啟音訊檔不見的專案時，會略過那些 clip；之後一存檔，它們就永遠消失了。寫出 DAW 在開啟前會做的事：

- `findMissingAudio(project, state)`：不在原處的各個檔案，每個附上它的 clip 需要的 frame 數（最大的 `sourceOffset + length`），以及有幾個 clip 用到它。
- `checkCandidate(missing, file, rate)`：一個檔案為什麼不能替代——不是音訊檔，或在它自己的取樣率下太短——可以替代就什麼都不回傳。
- `relink(project, ask)`：每個不見的檔案，先到之前找到檔案的資料夾裡找（media 資料夾被改名：找到一個，其他的就跟著找到），找不到才 `ask()` 使用者；不合格的候選檔附上理由拒絕；最後再到找到的資料夾裡試一次；然後把舊檔留成 `Song.dawlab.bak`，寫入新的路徑（在專案資料夾內用相對路徑，在外面用絕對路徑）。

自我檢查會把示範歌曲的 media 資料夾（23 個檔案）改名、取消其中一個、提供一個太短的檔案，並要求在一次正確回答之後找回其餘所有檔案；之後專案開啟時每個 clip 都在。它也會把音訊移到專案資料夾外（絕對路徑），並給一個壞掉的專案檔。

想一想：這個檢查只比較長度。名稱和長度都對的檔案，仍然可能是錯的。relinker 要怎麼分辨？（看看 Step 8 怎麼替 media 檔命名。）

---

[回到本章](../README.zh-TW.md) | [解答](../solutions/README.zh-TW.md)
