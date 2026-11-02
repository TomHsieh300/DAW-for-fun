# Step 5 - Undo 與 redo

[English](step-05-undo.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 4 - 存檔與開啟專案](step-04-project-files.zh-TW.md) | 下一步：Step 6 - App（規劃中）

## 目標

讓使用者能把專案的任何修改收回再重做：一次一個修改、一次 fader 拖動算一步、一次刪掉好幾個 track 也算一步，並且隨時知道專案和上次存檔時有沒有不同。

## 設計

### 編輯是值，每個編輯都回傳它的反向

做 undo 有兩種經典做法：

<div align="center">

| 做法 | undo 怎麼運作 | 代價 |
|---|---|---|
| 快照 | 每次修改前複製整個專案；undo 把複本放回去 | 記憶體和時間跟著專案大小成長，而不是跟著修改大小；拖一次 fader 就要複製所有 clip |
| **command** | 記錄改了什麼；undo 執行相反的修改 | 紀錄很小；每種修改都需要正確的反向操作 |

</div>

這一步用 command，再加上一個讓它們保持精簡、不容易寫錯的做法。`Edit` 是一個普通的值：`AddTrack`、`RemoveTrack`、
`SetGain`、`RenameTrack`、`MoveTrack` 的 `std::variant`，每一種都用 id 指定 track。一個函式負責執行編輯，並回傳能把它收回的編輯：

```cpp
std::optional<Edit> applyEdit(Project& p, const Edit& edit); // the inverse, or nullopt
```

反向編輯本身又是一個普通的 `Edit`。所以 undo 就是「套用反向」，而套用反向會回傳反向的反向，那正好就是 redo。每種編輯只有
一段程式碼，執行、undo、redo 都用它。

<div align="center">

| 編輯 | 修改前記住什麼 | 反向 |
|---|---|---|
| `AddTrack` | - | `RemoveTrack` |
| `RemoveTrack` | 這個 track、它的位置、它的 clip 和各自的位置 | 帶著這些資料的 `AddTrack` |
| `SetGain` | 原本的 gain | 用原本 gain 的 `SetGain` |
| `RenameTrack` | 原本的名稱 | 用原本名稱的 `RenameTrack` |
| `MoveTrack` | 原本的位置 | 移回原本位置的 `MoveTrack` |

</div>

`RemoveTrack` 會連同 track 的 clip 一起刪除，所以它的反向要把 clip 帶回來，而且每個 clip 都放回它在 clip 清單中的原位置，
這樣 undo 才能把專案還原得一模一樣，連順序都相同。

### id 讓歷史紀錄可靠

undo 紀錄寫的是「把 track 2 的 gain 設成…」，絕不是「第二個 track」。中間不管新增、刪除或移動了哪些 track，它都還找得到 Bass。
刪掉 Bass 再 undo，Bass 會以**同一個** id 回來，所以指向 Bass 的舊紀錄仍然有效。這就是 Step 1 從不重複使用 id 的原因。

### 刻意不 undo 的一件事：id 計數器

undo「新增 Keys（id 3）」會移除 Keys，但 `nextTrackId` 仍然是 4。如果 undo 把計數器調低，下一個新 track 就會拿到 id 3，而某筆
redo 紀錄、剪貼簿或存好的檔案裡仍然寫著「track 3」，就會找到錯的 track。所以「undo 之後的專案」指的是相同的 track 和 clip，
不是相同的計數器；自我檢查比較的正是這些，並另外檢查計數器從不倒退。一般原則：model 裡的東西不全是使用者狀態，分配器、
快取和計數器就留在原地。

### 一個使用者動作，一次 undo

`Change` 是屬於同一個動作的一串編輯，例如刪除三個選取的 track 是一個 change。`History::perform()` 以**全有或全無**的方式套用
change（`applyAll()`）：如果第三個編輯套用失敗，前兩個會被收回，專案和歷史紀錄都不變。undo 以相反順序套用反向編輯：
最後一個編輯最先收回，就像任何堆疊起來的東西一樣。

### 什麼要進歷史紀錄

<div align="center">

| 進歷史紀錄 | 不進歷史紀錄 |
|---|---|
| 新增、刪除、改名、移動 track | 播放、停止、定位、loop 開關 |
| gain、pan、mute 設定 | 捲動、縮放、選取哪一個 track |
| 新增、移動、剪切 clip | CPU meter、峰值 meter |
| | 打開 mixer 視窗 |

</div>

規則是：會存進專案檔（Step 4）的東西可以 undo；只是用來顯示或播放專案的東西不行。undo「設定 gain」的人，不會希望播放位置也跟著跳回去。

### 拖一次 fader 是一步

拖動 fader 時，每次滑鼠移動都會送出一次 gain 修改，一個手勢就有 50 次。沒有人想按 50 次 Ctrl+Z。每次修改都帶一個
**merge key**（「track 2 的 fader」）來執行；和最新紀錄同一個 key 的修改會被套用，但不另外記錄，因為最新紀錄的反向本來就會
回到拖動前的 gain。merge 在以下情況結束：

- 放開滑鼠（`endMerge()`）
- 執行了其他修改
- 使用了 undo 或 redo
- 存檔（`markSaved()`）：存檔後繼續拖動要算成一次修改

### 新的編輯會清掉 redo

undo 兩步之後，redo 堆疊裡有這兩步。如果使用者這時做了新的修改，那兩步就屬於一個已經不存在的未來：新修改套用的專案，和它們
當初記錄時的專案不一樣了。`perform()` 會清掉 redo 堆疊。（有些程式改用 undo *樹*；為了一個很少用的功能，那需要多得多的機制。）

### 上限、存檔，以及繞過歷史紀錄的修改

- **上限。** 歷史紀錄保留最新的 100 筆（建構子參數），丟掉最舊的。用 `std::deque` 從前面丟掉很便宜。
- **存檔後是否修改過。** 每筆紀錄都有一個不會重複使用的序號，undo 和 redo 時紀錄保留自己的序號。`markSaved()` 記住最新紀錄的
  序號；最新紀錄換成別筆時，專案就算修改過。這在 undo、redo、新編輯和丟掉舊紀錄之後都正確，也是 Step 6 視窗標題加上 `*` 的依據。
- **繞過歷史紀錄的修改。** 如果有東西沒經過歷史紀錄就改了專案（別處的 bug），舊紀錄可能就套用不上了。這時 `undo()` 會拒絕，
  讓專案保持原樣（又是全有或全無），並丟掉那筆紀錄，而不是只套用一半。

### C++ 陷阱：`apply` 與 `std::apply`

這一步的第一版把函式叫做 `apply(Project&, const Edit&)`，結果編譯不過：`Edit` 是 `std::variant`，所以 argument-dependent lookup
也會到 `std` namespace 找，找到 `<tuple>` 裡的 `std::apply(F&&, Tuple&&)`。那個 template 對任何引數都是完全符合，贏了 overload
resolution，然後在標準函式庫深處失敗。引數型別來自 `std` 的函式，不應該取一個 `std` 已經拿來當 template 的名字，所以改叫 `applyEdit`。

### 和 JUCE 的對應

`juce::UndoManager` 儲存帶有 virtual `perform()` 和 `undo()` 的 `juce::UndoableAction` 物件，用 `beginNewTransaction()` 把它們
分組成 transaction，並用 `UndoableAction::createCoalescedAction()` 合併。概念是一樣的：一個動作、一個使用者動作對應一個
transaction、合併、上限。Step 6 可以把 `History` 包成那個介面，也可以直接用 `UndoManager` 搭配裝著 `Edit` 的 action。這一步自己
寫一個小小的歷史紀錄，是為了讓第 15 章在 app 之前都不依賴 JUCE，也因為 150 行就能看到整個機制。

## 程式碼

`steps/step05-undo/minidaw.h`（節錄）：

```cpp
struct AddTrack    { TrackState track; int index; std::vector<std::pair<int, ClipState>> clips; };
struct RemoveTrack { TrackId id; };
struct SetGain     { TrackId id; float gainDb; };
struct RenameTrack { TrackId id; std::string name; };
struct MoveTrack   { TrackId id; int index; };
using Edit = std::variant<AddTrack, RemoveTrack, SetGain, RenameTrack, MoveTrack>;
using Change = std::vector<Edit>;                       // one user action

AddTrack newTrack(const Project& p, std::string name);  // provided: uses nextTrackId
std::optional<Edit> applyEdit(Project& p, const Edit& edit);           // YOUR TURN
bool applyAll(Project& p, const Change& change, Change& inverses);     // provided: all or nothing

class History {
public:
    bool perform(Project& p, const Change& change, std::uint32_t mergeKey = 0); // YOUR TURN
    bool undo(Project& p);                                                     // YOUR TURN
    bool redo(Project& p);                                                     // YOUR TURN
    void endMerge();  void markSaved();  bool isModified() const;              // provided
private:
    struct Entry { Change edits; std::uint32_t mergeKey; std::uint64_t serial; };
    std::deque<Entry> undo_;   // oldest first
    std::vector<Entry> redo_;  // the next redo last
    ...
};
```

## 換你寫

<div align="center">

| | |
|---|---|
| 你要寫 | `applyEdit()`、`History::perform()`、`History::undo()` 和 `History::redo()` |
| 檔案 | `steps/step05-undo/starter/step05.cpp` |
| 執行 | `l15_step05_starter`；完成前會印出 `[FAIL] l15_step05 (17 of 22 checks failed)` |
| 解答 | `steps/step05-undo/solution/step05.cpp`（ctest `l15_step05_solution`） |

</div>

把 `applyEdit()` 寫成每種編輯一個小小的 `applyKind()` overload，再用 `std::visit` 分派。每個 overload 先檢查，只有在編輯套用得上
時才修改專案。在 `undo()` 裡，把紀錄的編輯複製一份並反轉順序、用 `applyAll()` 套用，再把拿回來的反向編輯反轉回去，這樣 redo
紀錄才會是原本的順序。

自我檢查的最後一部分會隨機做 3000 個操作（修改、fader 拖動、undo、redo、放開滑鼠），每做一個就和一個參考 model 比較；那個
model 單純保存每一個狀態的複本。它也會檢查每條路徑真的都走過：一個從來沒有合併過的隨機測試，對合併什麼也證明不了。

預期輸出（已驗證）：

```
Step 5: undo and redo (build-along)
  add Keys, Bass to -6 dB, delete Drums: Bass, Keys; 1 clip(s) left (expected Bass, Keys; 1)
  undo: Drums, Bass, Keys, Drums is id 1 again, clips in the old order: yes (expected Drums, Bass, Keys; 1; yes)
  undo, undo: back to the start yes, nextTrackId still 4 (expected yes, 4); a 4th undo: nothing to undo
  redo x3: the same as before the undos yes, Keys has id 3 again (expected yes, 3)
  undo the add of Keys, then add Pad: Pad's id 4 (expected 4); redo still possible: no (expected no)
  6 kinds of edit undone by their inverse: 6 of 6; 6 edits that do not apply: refused and harmless yes
  a fader drag of 50 steps: 1 undo entry, a second drag: 2; undo -> -10.0 dB, undo -> 0.0 dB
  (expected 1, 2, -10.0, 0.0)
  drag, undo, drag the same fader again: 2 undo entries; undo -> Drums -5.0 dB, Bass 0.0 dB
  (expected 2, -5.0, 0.0)
  delete Drums and Bass in one action, one undo: both back yes; a change with a failing edit: refused, gain 0.0 dB,
  history unchanged yes (expected yes, refused, 0.0, yes)
  history limit 10, 25 edits: 10 undos possible, then the state after edit 15: yes (expected 10, yes)
  modified: new no, edit yes, save no, undo yes, redo no, drag on after a save yes
  (expected no, yes, no, yes, no, yes)
  undo after Bass was deleted outside the history: refused, project unchanged yes, entry dropped yes
  (expected refused, yes, yes)
  3000 random operations (2733 changes, 1256 merged, 294 refused, 621 undos, 153 redos): 0 mismatches with the
  reference model, id counter went down 0 times (expected 0, 0)
[PASS] l15_step05 (22 checks passed)
```

## 你學到了

- 讓編輯成為普通的值，並讓套用編輯回傳它的反向：undo 和 redo 就成了同一個操作，每種編輯只需要一段程式碼。
- 每筆紀錄都用 id 指定 track；被刪除的 track 要以同一個 id 回來，它的 clip 也回到原位。
- 不是所有東西都是可以 undo 的狀態：id 計數器絕不能倒退。
- 一個使用者動作要全有或全無地套用，undo 時最後一個編輯最先收回。
- 把一次拖動合併成一筆紀錄，並在放開滑鼠、其他修改、undo、redo 和存檔時結束合併。
- 新的編輯會清掉 redo；每筆紀錄的序號可以判斷專案在上次存檔後是否修改過。
- 用一個保存每個狀態的 model 測試 undo，並確認隨機測試真的走過每一條路徑。
