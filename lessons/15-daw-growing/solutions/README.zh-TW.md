# 第 15 章 - 解答
[English](README.md) | **繁體中文**

[回到本章](../README.zh-TW.md) | [練習](../exercises/README.zh-TW.md)

用 `.\scripts\build.ps1 -Only 15-daw-growing -TestLabel solution` 執行所有解答程式。每一個都以 `[PASS] l15_exN (... checks passed)` 結束。

---

## 練習 1 - 被刪掉的 track 歸誰？

1. **Index。** Track Drums、Bass、Keys、Vox 是 0-3。使用者把 Vox 的 fader 從 -6 拖到 0 dB：undo 紀錄寫著「track 3 回到 -6 dB」。接著他刪掉 Bass：Vox 現在是 track 2。他新增一個 Pad，成為 track 3。Undo 把 *Pad* 設成 -6 dB，Vox 仍是 0 dB。Index 說的是 track *現在*在哪裡；紀錄卻是*之後*才用。`TrackId` 只發一次、永不重發，所以紀錄不管 Vox 在哪裡都找得到它；如果 Vox 已被刪除，就什麼都找不到（Step 1：查詢只會找到正確的 track 或什麼都沒有，絕不會是另一個）。
2. **誰釋放什麼。**
   - UI thread 在 `removeTrack()` 裡立刻釋放自己在 slot table 的那一筆：沒有別人讀那個 table。
   - Audio thread 上的 slot 永遠不釋放：它是 `prepare()` 時配置好的 `kMaxTracks` 個 slot 之一。Audio thread 處理 `RemoveTrack`（`Mixer::deactivateTrack()`）時，會重設 slot 自己的狀態——最重要的是 latency-compensation 的 delay line，讓下一個進到這個 slot 的 track 不會一開始就播出 Bass 的尾巴——然後把它標成空的。
   - Clip 清單和 insert 可能還在使用中：audio thread 可能正在播放它們，command 也可能還在 queue 裡。只有 audio thread 知道自己什麼時候不再使用，所以它透過 garbage queue 把它們交回來（一個 command 最多 `kMaxInserts + 1` 個物件），由 UI thread 在 `collectGarbage()` 裡刪除。Audio thread 從不釋放任何東西。
3. **立刻重用 slot。** Command queue 只有一條，而且保持順序。所以 audio thread 一定先看到 `RemoveTrack(Bass, slot 1)` 才看到 `AddTrack(Vox, slot 1)`，等 Vox 到的時候，slot 在它那邊也已經空了。如果有兩條 queue——例如結構性的 command 一條、數值一條——Vox 的 fader 值可能比 Vox 的 `AddTrack` 先到，而那時 Bass 還在 slot 1。Command 裡的 id 會擋下它（slot 裡是別的 id，command 什麼都不做），但這個值就遺失了，而不是延後。一條 queue 讓順序成為保證。
4. **Id 計數器。**
   - 如果 undo「新增 Pad（id 7）」讓計數器倒回 7，下一個新 track 會拿到 id 7。被 undo 的那次新增，它的 redo 紀錄還寫著「新增 id 7」；其他任何指名舊 Pad 的紀錄或曲線，現在都會找到新的 track。
   - 如果檔案沒存 `nextTrackId`，開啟時會從檔案裡最大的 id 加一開始。如果使用者存檔前刪掉了 id 最大的 track，那個 id 就會再發一次。第 16 章會保留 track 已不存在的 automation lane，所以新 track 會繼承被刪掉的 track 的 automation。
5. **進 history：**重新命名 track、solo 一個 track、移動 clip、拖曳 fader（算一筆）。**不進：**playhead 的位置、縮放、選取的 track、CPU 警告。
   - **Solo** 是最不明顯的。它看起來是一種「聆聽」的方式，但 DawLab 把 solo 存進專案檔，所以它會改變專案：使用者 solo 之後存檔、再開啟，會看到 solo 還在，而 undo 必須能把它拿回來。
   - **拖曳 fader** 會送出 50 個變更，但它是一個動作：它們被合併成一筆紀錄（Step 5 的 merge key），否則 Ctrl+Z 要按 50 次。

## 練習 2 - 手算 CPU budget

Block 的時間是 $`t = o + N(f + nc)`$，budget 是 $`T = N / f_s`$，load 是 $`t / T`$。

- **(a)** $`T = 64/48000 = 1.3333`$ ms、$`256/48000 = 5.3333`$ ms、$`1024/48000 = 21.333`$ ms。
- **(b)** $`n = 20`$ 時，$`f + nc = 3.4\ \mu`$s／sample：

  <div align="center">

  | Block | 每個 block 的時間 | Load |
  |---|---|---|
  | 64 | $`50 + 64 \cdot 3.4 = 267.6\ \mu`$s | 20.07 % |
  | 256 | $`50 + 256 \cdot 3.4 = 920.4\ \mu`$s | 17.26 % |
  | 1024 | $`50 + 1024 \cdot 3.4 = 3531.6\ \mu`$s | 16.55 % |

  </div>

  每個 sample 的工作一樣，但每次 callback 的 50 µs 不一樣：它占 64 samples budget 的 3.75 %，卻只占 1024 samples budget 的 0.23 %。小 buffer 除了 latency 低，也比較耗 CPU。
- **(c)** 對 $`n`$ 解 $`o + N(f + nc) \le 0.75\,T`$：

  ```math
  n \le \frac{(0.75\,T - o)/N - f}{c}
  ```

  64：$`(1000 - 50)/64 = 14.844`$，$`(14.844 - 0.4)/0.15 = 96.3`$ → **96** 個 track；256：$`(4000 - 50)/256 = 15.430`$ → $`100.2`$ → **100**；1024：$`(16000 - 50)/1024 = 15.576`$ → $`101.2`$ → **101**。
- **(d)** 100 個 track、256 samples：$`50 + 256 \cdot 15.4 = 3992.4\ \mu`$s，load **74.86 %**，剛好在 High 以下。慢的 block 要 $`2.2 \times 74.86 = 164.7`$ %：overrun。每分鐘有 $`48000 \cdot 60 / 256 = 11250`$ 個 block，所以**每分鐘 56.25 次 overrun**，每 1.07 秒一次。`CpuMonitor` 每次 overrun 後把 Overload 保持 2 秒，所以它**永遠離不開 Overload**——雖然平均值低於 High。這就是把 overrun 分開計算的用意：平均值看起來沒事，使用者卻每秒聽到一次爆音。不 overrun 代表 $`2.2 \cdot \text{load} \le 1`$，load 最多 45.45 %：**59 個 track**。真正的上限是最差的 block，不是平均。
- **(e)** $`\bar\ell \leftarrow \ell + a(\bar\ell - \ell)`$，$`a = e^{-T/\tau}`$：64 時 $`a = 0.995565`$，meter 顯示 $`1.3 + 0.995565 \cdot (0.4 - 1.3) = 40.40`$ %；256 時 $`a = 0.982379`$：**41.59 %**。兩種情況 meter 幾乎都沒動——一個 block 只是 300 ms time constant 裡的 1.3 ms 或 5.3 ms。Meter 告訴使用者機器在一段時間內有多忙；只有 overrun 次數（1）告訴他有東西被聽到了。

## 練習 3 - 複製 track

[`solutions/ex3_duplicate_track.cpp`](ex3_duplicate_track.cpp)

- `copyName()` 找結尾的 `" ("`、數字和 `")"`，把前面的部分當作基本名稱；然後依序試 `base (2)`、`base (3)`……直到沒有 track 用這個名字。
- `duplicateTrack()` 在 `index + 1` 建一個 `AddTrack`，id 是 `nextTrackId`，原 track 的 clip 放在 `clips.size()`、`clips.size() + 1`……的位置。

**為什麼不需要新的 edit 和新的 undo 程式：**複本*就是*一個剛好有內容的新增 track。`AddTrack` 本來就帶著一個 track 和它的 clip（undo 就是這樣把刪掉的 track 帶回來的），`applyEdit()` 也早就會回傳它的反向操作 `RemoveTrack`。指令只是一個*描述*變更的函式；其餘交給 Step 5。

```text
Exercise 3 - duplicate a track
[PASS] l15_ex3 (41 checks passed)
```

## 練習 4 - 格式 3，以及來自新版本的行

[`solutions/ex4_format3.cpp`](ex4_format3.cpp)：Step 4 的解答加上三處修改，每處都標著「Exercise 4」：

- `parseVersion()` 在小數點處切開檔頭的版本。兩部分都必須是整數，所以 `3.`、`.2`、`3.x` 和 `3.1.1` 都被拒絕。
- 格式 3 的 track 行有 6 個 token，mute 旗標必須是 0 或 1。
- 只有在 `major == 3` 且 `minor > 0` 時才保留未知 keyword；這時原始的行（去掉 `\r`）放進 `keptLines`，minor 版本放進 `keptMinor`。寫檔時把保留的行接在後面，有保留的行就寫成 `3.<keptMinor>`。

```text
Exercise 4 - format 3, and lines from a newer version
  line 3: "colour" is from DawLab format 3.2, kept as it is
  line 5: "marker" is from DawLab format 3.2, kept as it is
[PASS] l15_ex4 (40 checks passed)
```

**被刪掉的 track 的顏色。**新版本開啟檔案時，看到 `colour 2 "#ff8800"`，但 track 2 並不存在。它不能拒絕這個檔案——使用者沒做錯任何事——而是把這一行當成過期的：附上警告後丟掉，就像 Step 8 處理音訊不見的 clip 一樣。對格式的啟示是：minor 版本只能增加這樣的行：(1) 跳過它不會改變已知各行的意思，而且 (2) 新版本讀到時能再檢查一次，因為舊版本可能已經改變了它所指的東西。會改變另一行意思的行（「gain 現在換了單位」）需要新的 major 版本。

## 練習 5 - 用記憶體限制 undo history

[`solutions/ex5_byte_budget.cpp`](ex5_byte_budget.cpp)

每筆紀錄存著自己的大小，所以丟掉一筆時不必重算。每個增加或移除紀錄的地方都用那個大小調整 `bytes_`：`perform()` 清空 redo stack、新的 undo 紀錄，以及 `undo()` 和 `redo()` 裡的兩筆紀錄。每次增加紀錄之後都執行 `trim()`：

```cpp
while (bytes_ > byteLimit_ && undo_.size() + redo_.size() > 1)
    有 undo_.front() 就丟它，否則丟 redo_.front()
```

容易漏掉的情況是 `undo()` 讓 history **變大**。Undo 一次新增，會把小小的「移除 track 3」紀錄變成 redo 紀錄「新增 track 3 和它 4 MB 的 clip」。所以 `undo()` 也要 trim，而且它可能丟掉最舊的 undo 紀錄來替 redo 騰出空間。

```text
Exercise 5 - an undo history limited by memory
  3 deleted tracks of 4 MB, a budget of 10 MB: 2 entries, 8000376 bytes
  undoing an add of 4 MB next to 8 MB: 1 undo, 1 redo, 8000370 bytes
  4000 random edits, undos and redos: 0 broken (expected 0)
[PASS] l15_ex5 (1090 checks passed)
```

（Byte 數取決於 `sizeof(Edit)`，所以不同 compiler 會有一點差異。）

**共用的音訊。**在 app 裡，clip 透過 `std::shared_ptr` 持有音訊。如果被刪掉的 clip 的音訊也被專案裡還在的 clip 使用（一個複本、一個被切成兩半的 clip），釋放 undo 紀錄並不會釋放任何東西，budget 就把那段音訊多算了一次。一筆紀錄真正的成本，只有*沒有別人*持有的音訊——而這會隨著專案的每次編輯改變。精確的 budget 必須在專案每次改變時重算。多算是安全的一邊：history 會稍早一點丟掉紀錄，但絕不會讓超過 budget 的記憶體繼續存活。

## 練習 6 - 存檔時什麼都不遺失

一種設計；也有其他可行的做法。

1. **Autosave。**
   - 寫到專案旁邊的另一個檔案（`Song.dawlab.autosave`），永遠不覆蓋使用者的檔案。專案檔什麼時候改變由使用者決定：他可能只是在試東西，打算不存檔就關掉。
   - 大約一分鐘一次，而且只在 history 顯示專案自上次 autosave 後有修改時。
   - 寫檔在背景 thread 上進行。它必須處理一份*複本*：message thread 取一份 session 的快照（`writeProject()` 的文字，或它需要的 model 值）交出去，背景 thread 只負責寫 byte。使用者編輯時，它不能去讀活的 session。
2. **當機之後。**比較 autosave 和專案檔的修改時間；更好的做法是寫檔時把 history 的 serial（或一個計數器）寫進兩個檔案。如果 autosave 比較新，就問：「DawLab 沒有正常關閉。要開啟 14:32 自動儲存的版本嗎？」使用者回答之前兩個檔案都保留。正常存檔或正常結束之後刪掉 autosave。
3. **音訊。**專案指名的每個音訊檔，都必須在指名它的專案檔被 rename 到定位*之前*完整寫到磁碟上。新的錄音或匯入的檔案先複製到 media 資料夾並 flush，再寫專案檔並 rename。中間當機只會留下一個沒用到的音訊檔，絕不會留下指名不存在檔案的專案。
4. **開啟時缺檔。**把 clip 保留為一個佔位符：位置、長度和檔名，以灰色、無聲顯示，並附上警告。存檔時原樣寫回。這樣使用者之後還能找到那個檔案（練習 9 讓他重新選擇）。像 Step 8 那樣直接略過這種 clip，會讓一顆不見的硬碟在下次存檔時變成資料遺失。

---

[回到本章](../README.zh-TW.md) | [練習](../exercises/README.zh-TW.md)
