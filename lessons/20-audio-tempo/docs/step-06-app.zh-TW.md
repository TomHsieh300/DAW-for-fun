# Step 6 - App

[English](step-06-app.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 5 - Engine](step-05-engine.zh-TW.md) | 下一步：[Step 7 - 本章的結尾](step-07-the-end.zh-TW.md)

## 目標

DawLab 20：在 audio clip 上按右鍵，指定 tempo 改變時它要怎麼做——留在原地、起點留在拍子上、像磁帶那樣跟著走，或是跟著走而且保持音高。要有 undo、要存進專案檔，而且不必為了它把整個 app 重寫一遍。

## 設計

### 先複製，而且已經能動

`app/` 一開始是第 19 章的原樣複本，只把名稱改成 DawLab 20（一個 commit）。接在第 20 章的 engine 上可以建置，smoke test 也原樣通過：還沒有任何 clip 會跟著 tempo。

### 資料放在哪裡

App 的 model 是一串串單純的 `engine::Clip`：session 每個 track 存一串，一個 edit（`SetClips`）帶著一整串，undo history 就是這些東西疊起來。Arrangement、take、錄音、專案檔和 sample rate 轉換，全都以 sample 讀 clip 的 `start` 和 `length`。

Step 5 的 `FollowClip` 是把 clip 包起來。如果把 app 裡的每個 clip 都包起來，上面那些全都得改。所以資料反過來放——**由 clip 自己帶著**：

```cpp
struct Clip {
    ...
    std::string name;                         // UI only; the audio thread never reads it
    std::shared_ptr<const ClipFollow> follow; // UI only: how the clip follows the tempo; null if it does not
};

struct ClipFollow {
    FollowMode mode;
    double startTick;                // Start
    std::int64_t sourceLength;       // Varispeed, Stretch: the clip plays [sourceOffset, sourceOffset + sourceLength)
    std::vector<WarpMarker> markers; // Varispeed, Stretch
};
```

這是本章在第 14 章的 `Clip.h` 裡加的唯一一行，就放在 `name` 旁邊——`name` 從那時起就是「只給 UI 用」的。現在每一串 clip、每個 edit、每個 undo 步驟都帶著 follow 資料，而它們自己並不知道。

### Clip 的 start 和 length 是推導出來的

對跟著 tempo 的 clip 來說，`start` 和 `length` 是**它在目前的 tempo map 下涵蓋的歌的 sample**——所以所有繪製、點選、拖曳 clip 的程式都繼續以 sample 運作。Clip *本身是什麼*，則在 `follow` 裡。Session 只在兩個地方讓兩者保持一致：

- `setClips()` 會把 list 裡的每個 clip 重新擺放（`placeUnder()`），不管 list 原本寫什麼。Undo 步驟保存的是另一個時間點的 list，那時 tempo 可能不同；它回來的時候，裡面的 clip 會被放到目前的 map 所決定的位置。
- `setTempoMap()` 會重新擺放並送出每個 track 的 clip。

這又是 Step 2 的規則——tick 才是事實，sample 是推導出來的——也是 undo 不需要任何新東西的原因：`SetTempoMap` 還原 map，clip 就跟著回去。

### Engine 拿到的是什麼

`Session::pushClips()` 以前把 track 的 clip 交給 engine。現在它交給該 track 的 `FollowTrack`（Step 5），再把拿回來的東西給 engine：不跟著 tempo 的就是 clip 本身，跟著的則是運算結果或 placeholder。視窗的 timer 會呼叫 `Session::pollRenders()`，把算完的結果接過來再交出去。`getPlayedClips()` 是 engine 手上那份的複本——arrangement 畫的就是它，所以被 stretch 的 clip 顯示的是它實際播放的波形。

匯出會先呼叫 `finishRenders()` 並等待：bounce 不能帶著 placeholder 出去。匯出運算期間，session 不接受任何編輯（`holdEdits()`）：否則那個編輯會從運算當時進行到的位置開始出現在檔案裡。

每次改 map 都會一口氣替很多 track 送出新的 clip 清單，而 engine 的 command queue 可能是滿的（沒有 device 在取，或 audio thread 落後了）。Session 送出的永遠是一整份狀態——一個 track 的 clip、它的 MIDI clip、一條 lane 的曲線——所以被丟掉的清單只要重送最新的那份就好：視窗的 timer 會呼叫 `catchUpEngine()`。沒有 device 時，timer 會自己套用這些 command。

`FollowTrack` 是靠 clip 的 audio 認出它，不是靠它在清單裡的位置：別的 clip 被拖到它前面時，它保留自己的運算結果；被移動的 clip 在新的結果算好之前，會在新的起點播放上一次的結果。

### 打開它

`setClipFollow(track, clip, mode)` 是一個 undo 步驟，而且不移動任何東西（`withFollowMode()`）：

- **Start**：`startTick` 是 clip 目前起點的 tick。
- **Varispeed、Stretch**：clip 拿到的 marker，是「在目前的 map 上、在 clip 目前的位置錄下的 take」的 marker。用白話說：*它此刻播放的樣子就是對拍的*。這對剛錄好的 take 是對的，對用它原本的 tempo 放進來的 loop 也是對的。
- 已經 warp 過的 clip 在 varispeed 和 stretch 之間切換時，保留原本的 marker。
- **Off**：clip 留在原地，並再次以自己的速度播放它的 source。
- 對 clip 設定它原本就有的模式，不會改變任何東西，也不算一個 undo 步驟：起點的 tick 不會再從 sample 反推一次，否則每次都會因捨入誤差移動一點。

Clip 選單記住的是打開它時的那個 clip，不是它的 index（`chooseFromClipMenu()`）：選單開著時如果清單變了，選項仍然作用在那個 clip 上；如果那個 clip 本身變了，就什麼都不做。

### 移動和裁切

被拖曳的 clip 會帶著它的 tick 或 marker 一起走（`movedTo()`）：marker 移動的 tick 數和起點移動的一樣多。

錄音落在某個 clip 上時，第 17 章會把舊的 clip 沿著新的 clip 切開。對 warp 過的 clip，這一刀是用它自己的方式切的（`cutToSongRange()`）：切出來的片段保留 marker，只是播放的 source 變少。前一段的結尾和後一段的開頭，都剛好落在新 clip 邊緣的那個 sample 上。如果一個 source sample 涵蓋歌曲的很多個 sample（take 播放得比錄製時慢很多），邊緣會落在某個 source sample 的中間：切出來的片段會記住它頭尾那兩個 sample 實際播放的比例（`ClipFollow::headCut`、`tailCut`），由第 9 版存檔。

### 專案檔，第 8 和第 9 版

跟著 tempo 的 clip 在它的 `clip` 行底下有幾行：

```text
clip 1 384000 256000 "Follow_media/000.wav" 0 0 0 linear 0 0 "clicks"
follow stretch 192000
marker 0 11520
marker 192000 19200
```

（在 120 BPM 做的兩小節 audio，放在第 4 小節，歌是 90 BPM。）起點模式是 `follow start <tick>`；其他模式是 `follow varispeed|stretch <source 裡的長度>` 加上至少兩行 `marker <source> <tick>`。Tick 和 source 位置以 17 位數字寫出，檔案裡每個 double 都是如此（sample rate 和 tempo 也是）：它們是小數，讀回來必須是同一個數。`clip` 行的 `start` 和 `length` 是存檔當時 clip 所在的位置；開檔時會依檔案的 tempo map 重新擺放。第 1 到 7 版的檔案沒有這些行，照舊開啟。

第 9 版在切出來的片段的 marker 之後多一行：`cut <head> <tail>`，表示它頭尾那兩個 source sample 中不播放的比例，各自從 0 到 1（不含 1）。沒有被切過的 clip 沒有這一行，所以沒有切過的專案在第 20 章照舊能讀；第 19 章則會把第 9 版當成較新的 DawLab 做的檔案而拒絕。

這些行來自外部，所以在讀進來的地方就檢查（ProjectFormat.cpp 的 `followProblem()`）。一個檔案如果會把 clip 放到 DawLab 從來不會放的位置，就會連同行號一起被拒絕：起點 tick 小於 0；tick 或位置超過 2^40；兩個 marker 之間的關係不是任何 tempo 做得出來的（每個 source sample 對應的 tick 數不在 2^-20 到 2^20 之間）；或 markers 沒有涵蓋 clip 播放的那段 source（容許一個 sample 的捨入誤差）。Warped clip 的 `length` 可以是 0：在很快的 tempo 下，一兩個 sample 長的片段蓋不到歌曲的任何一個 sample。另外，切 clip 時如果某個片段裡完全沒有 source 的內容，那個片段就不保留（`Session::withClipOnTop()`）。

`clip` 行沿用第 15 到 19 章的上限，放在它們共用的同一個檔案裡（`ProjectLimits.h`）：sample rate 在 8000 到 768000 Hz 之間，每個位置和長度最多 2^40，每個 fade 在檔案的 rate 下最多 60 秒。Fade 不受它的 clip 長度限制：較快的 tempo 讓 warped clip 變短時，它的 fade 保持不變。

### 它沒有做的事

- **錄音不會讓 take 自動跟著 tempo。** Take 錄下來是不跟著 tempo 的 clip；之後你再打開，它會拿到當下那個 map 的 marker。請在改 tempo 之前打開。
- **要先選好 take，clip 才跟著 tempo。** Clip 選單只在 clip 不跟著 tempo 時列出 take。
- **Marker 不能編輯。** 沒有 beat detection，也沒有可以拖曳的 marker：打開「跟著」的那一刻，clip 播放的樣子就被當成是對拍的。
- **運算結果不在專案檔裡。** 開檔之後，跟著 tempo 的 clip 會在運算所需的那一小段時間內播放 placeholder。
- 很長的 clip 做 stretch 需要時間（Step 4：每秒 audio 約五千萬次乘法），而匯出前的 `finishRenders()` 會讓視窗卡住這麼久。

## 檢查

Smoke test 多了一個部分，格式測試多了一節：

```text
audio clips that follow the tempo
  a take of 8 clicks made at 120 BPM, by varispeed at 60: 8 clicks, on their beats yes, the clip 8 s long
  start, varispeed and stretch: set, moved, cut, saved (format 9), opened, at another sample rate, and off again
```

```text
clips that follow: a start in ticks, varispeed and stretch with their markers written and read back exactly
[PASS] l20_test_project_format (220 checks passed)
```

`tests/test_warp.cpp` 多了第七部分，測試 clip 的輔助函式，不需要 JUCE：

```text
  a clip that carries how it follows: its mode changed, placed, moved and cut - the pieces of 200 cuts tile its source
```

## 你要自己試的

1. **Loop。** 把一個鼓的 loop 以它自己的 tempo 放到 track 上。按右鍵：*Follow, pitch changes (varispeed)*。播放時上下拖動第一個 tempo 點：loop 馬上移動（placeholder），片刻之後就對上拍子。
2. **換成 stretch。** 對同一個 loop 選 *Follow, pitch stays (stretch)*：聽聽 tempo 減半時鼓擊變成什麼樣子。再用一個持續音試試。
3. **Cymbal。** 對第 5 小節的單一敲擊選 *Keep its start on the beat*；改它前面的 tempo。
4. **Undo。** 先 undo tempo 的改變，再 undo 模式：clip 兩次都會回去。

## 你學到了什麼

- Model 的每個部分都得帶著的資料，放在 model 本來就在傳遞的東西裡面最便宜。
- 從事實推導出來的值，只要在事實或情境改變的少數幾個地方重算——這裡是兩個。
- 以完整狀態組成的 undo history，對於隨狀態一起移動的資料不需要新的 edit。
- 背景運算在 app 裡只剩一個問題：算完之前，使用者看到和聽到的是什麼？
