# Step 1 - 穩定的 track ID

[English](step-01-track-ids.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[第 14 章](../../14-daw-8track/README.zh-TW.md) | 下一步：Step 2 - Track 池（規劃中）

## 目標

給每個 track 一個身分，讓它在新增、刪除、重新排序 track 之後依然不變。這樣所有指向 track 的
東西 - undo 紀錄、正在送往 audio thread 的 command、GUI 的選取狀態、存好的專案檔 - 都會一直
指向**同一個** track。

## 設計

### 為什麼 index 會失效

在第 14 章，track **就是**它的 index：mixer 持有 `std::array<Track, 8>`，而 `ChannelRef`、
`Session::clips_`、arrangement 的 lane 和 command 都說「track 1」。因為什麼都不會移動，index
是個完全合格的名字。

現在讓使用者刪除 track。四個 track，以及在它們都還在時記下的兩筆編輯（可以想成 undo 歷史裡的兩筆
紀錄，或是 queue 裡還沒處理的兩個 command）：

<div align="center">

| | Drums | Bass | Keys | Vox |
|---|---|---|---|---|
| 刪除前的 index | 0 | 1 | 2 | 3 |
| id | 1 | 2 | 3 | 4 |
| 刪除 Drums 後的 index | - | 0 | 1 | 2 |

</div>

這兩筆編輯是「Bass 調到 -6 dB」和「Vox 調到 -3 dB」。如果用 index 記，它們就是「track 1 調到
-6 dB」和「track 3 調到 -3 dB」。刪除之後，track 1 變成 *Keys*，track 3 不存在：一筆改錯了
track，另一筆直接消失。這兩種錯誤都不會發出怪聲，也不會當機 - 只是某個 track 莫名其妙變小聲。
這正是 index 的 bug 那麼難找的原因。

### 指稱 track 的四種方式

<div align="center">

| 用什麼指稱 track | 刪除 / 重排後還對嗎？ | 問題 |
|---|---|---|
| index | 不對 | 上面的 track 被刪除或移動時就會位移 |
| 指標 | 只在 track 還活著時 | 刪除後變成 dangling；`std::vector` 擴充時可能整批搬家；無法寫進檔案 |
| 名稱 | 對 | 不唯一（兩個「Audio 1」），而且使用者會改名 |
| **id** | **對** | 沒有 - 它只是個數字，所以需要查表 |

</div>

id 是 track list 在建立 track 時發出的一個普通數字，之後永遠不會改變。因為它只是個整數，可以
複製進 command、寫進專案檔、跨 thread 比較。

### 永遠不要重複使用 id

計數器只會往上數。如果 list 重複使用已刪除 track 的 id，舊的參照（例如刪掉的 Vox，id 4，留在
undo 歷史裡的紀錄）就會突然找到**新的**、拿到 id 4 的 track，然後改到它 - 跟 index 一樣的
無聲 bug，只是更少見，所以更糟。用 32 bits、每秒新增一個 track，計數器也能用 136 年。

`TrackId{}`（值為 0）代表「沒有 track」；用它查詢一定失敗，所以預設建構的參照永遠不會碰到真的
track。

### 身分、slot 與位置

id 回答的是**哪一個** track。本章還會出現另外兩個數字，把這三者分清楚正是這一步的重點：

- **id** - 這是哪一個 track。永遠不變。audio thread 以外的所有東西都用它。
- **位置**（顯示用的 index）- track 畫在哪裡，0 = 最上面。每次移動或刪除都會變。只有 GUI 需要
  關心。
- **slot**（Step 2）- track 預先配置好的 audio 物件，放在 engine 池子裡的哪一格。在 track 存在
  期間固定不變，所以 audio thread 可以直接使用、不必查表。

audio thread 永遠不搜尋 id：UI thread 先把 id 轉成 slot，再送出 slot。id 給 model 用，slot
給 engine 用。

## 程式碼

`steps/step01-track-ids/minidaw.h`：

```cpp
struct TrackId {
    std::uint32_t value = 0;                       // 0 = no track
    constexpr bool isValid() const noexcept { return value != 0; }
};

class TrackList {
public:
    TrackId add(std::string name);                 // id = nextId_++, append
    bool remove(TrackId id);                       // erase; the counter is not touched
    int indexOf(TrackId id) const noexcept;        // display position, -1 if gone
    bool move(TrackId id, int newIndex);           // reorder; the id stays
    TrackInfo* find(TrackId id) noexcept;          // never returns a *different* track
    ...
private:
    std::vector<TrackInfo> tracks_;                // in display order
    std::uint32_t nextId_ = 1;                     // only ever counts up
};
```

`indexOf()` 用線性搜尋，這是刻意的：track list 在 UI thread 上，最多幾百個 track，而且只在使用者
操作時才搜尋。從 id 到位置的 hash map 在每次移動或刪除後反正都得重建。

`move(id, newIndex)` 把 track 拿出來再插回去，所以 `newIndex` 是**不含**被移動 track 的 list
中的位置；這樣移動後 `indexOf(id) == newIndex` 才會成立。超出範圍的目標會被 clamp，就像把 track
拖到最後一個下面時，它會變成最後一個。

## 換你做

`steps/step01-track-ids/minidaw.h` 已經有 `TrackId`、`TrackInfo`、`TrackList` 的資料成員和
輔助函式，以及兩種記下來的編輯：`applyEdits()`（用 id）和 `applyIndexedEdits()`（用 index，
第 14 章的做法）。

<div align="center">

| | |
|---|---|
| 你要寫的 | `TrackList::add()`、`remove()`、`indexOf()` 和 `move()` |
| 檔案 | `steps/step01-track-ids/starter/step01.cpp` |
| 執行 | `l15_step01_starter`；完成之前它會印出 `[FAIL] l15_step01 (18 of 25 checks failed)` |
| 解答 | `steps/step01-track-ids/solution/step01.cpp`（ctest `l15_step01_solution`） |

</div>

預期輸出（已驗證）- 先是上面的範例，接著是 id 重複使用、重新排序，以及 2000 次隨機操作與
參考模型的比對：

```
Step 1: stable track IDs (build-along)
  ids: Drums 1, Bass 2, Keys 3, Vox 4 (expected 1, 2, 3, 4)
  after deleting Drums: Bass, Keys, Vox (expected Bass, Keys, Vox)
  by index: 1 of 2 applied, Bass 0.0 dB, Keys -6.0 dB, Vox 0.0 dB (the chapter-14 way: wrong)
  by id:    2 of 2 applied, Bass -6.0 dB, Keys 0.0 dB, Vox -3.0 dB (expected -6.0, 0.0, -3.0)
  delete Vox (id 4), add Pad: Pad gets id 5 (expected 5, not 4); Drums found: no, Vox found: no (expected no, no)
  move Pad to 0, Bass to 99, Keys to -5: Keys, Pad, Bass (expected Keys, Pad, Bass); Bass still -6.0 dB
  2000 random add / remove / move: 0 mismatches, 0 reused ids, 289 tracks left (expected 0, 0)
[PASS] l15_step01 (25 checks passed)
```

## 你學到了什麼

- index 指的是一個**位置**，不是一個 track；一旦 track 可以刪除或移動，它就會出錯。
- track id 只發一次、永遠不變、永遠不重複使用；0 代表「沒有 track」。
- 用 id 查詢，結果不是找到正確的 track，就是什麼都找不到 - 絕不會找到別的 track。
- 分清楚三個數字：id（哪一個 track）、位置（畫在哪裡）、slot（它的 audio 物件放在哪裡，Step 2）。
