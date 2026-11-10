# Step 2 - 起點跟著 tempo 的 clip

[English](step-02-follow-start.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 1 - Warp map](step-01-warp-map.zh-TW.md) | 下一步：[Step 3 - Varispeed](step-03-varispeed.zh-TW.md)

## 目標

跟著 tempo 最便宜的做法：clip 只用它的**起點**保住在音樂裡的位置，並以自己的速度播放。找出這需要什麼、適合什麼，以及到哪裡就不夠用了。

## 設計

### Tick 才是事實

跟著 tempo 的 clip 存著它所屬的 tick，也就是 `startTick`；每次 tempo map 改變，它以 sample 計的起點都從這個 tick **推導**出來：

```math
start = \mathrm{round}(\mathrm{secondsAt}(startTick) \cdot rate)
```

絕對不要反過來。以 sample 計的起點是取整過的，而從取整過的 sample 找回來的 tick，已經不完全是原來那個 tick。做一次沒有人會發現；每次 tempo 改變都做一次，clip 就會慢慢離開它的小節線。Self-check 把一個在第 9 小節的 clip 送過 1000 個 tempo map 再回來：存著 tick 的 clip 回到原位，一個 sample 都不差；每次都重新找 tick 的那個偏了 15 個 sample。

這就是第 18 章對音符、第 19 章對以小節設定的 loop 用的規則，現在用在 audio 上：以拍子計數的東西要存成 tick。

這個 tick 是小數，和 Step 1 的 marker 一樣。Clip 很少剛好在整數 tick 上，而打開「跟著」時不能移動任何東西：`startTick` 就是 clip 目前所在 sample 的精確 tick。

### 兩種移動方式

自由拖曳把 clip 放到某個 sample；它的 tick 從那個 sample 找出來，只找一次。Snap 到 grid 的拖曳把它放到某個 tick，而且原樣保留這個 tick——不是 grid 線取整後那個 sample 的 tick。不跟著 tempo 的 clip 只拿到那個 tick 的 sample，tick 本身不留。

### 長度不變

只有起點跟著走。Clip 的長度就是它的 audio 以 sample 計的長度，所以它在音樂裡**結束**的位置取決於 tempo：兩秒在 120 BPM 是一小節，在 60 BPM 是半小節。

```text
120 BPM   bar 2            bar 3            bar 4
          [=== clip ======]
 60 BPM   bar 2                             bar 3
          [=== clip ======]                              the same 2 s, half a bar
```

這對本身沒有拍子的 audio 是對的：第 17 小節的一聲 cymbal、一句口白、一個音效。

### 接縫

對被切開的 audio 就不對了。同一個 take 的兩半首尾相接；兩半都跟著 tempo 而 tempo 變慢時，後半會移開，留下一段無聲的空隙。Tempo 變快時它會移進前半，而 engine 會把重疊的 clip 相加（第 14 章用這個做 crossfade），所以重疊的部分會被聽到兩次。

```text
120 BPM   [== first half ==][== second half ==]
 60 BPM   [== first half ==]                  [== second half ==]      a gap of 4 s
240 BPM   [== first half ==]
                   [== second half ==]                                 2 s heard twice
```

樂團的 take 或是 loop，每一小節裡都有拍子。對它們來說只有起點是不夠的：clip 裡的每樣東西都得跟著走，這表示要用另一個速度播放。Step 1 的 `speedAt()` 會說是哪個速度。

### 播放中

歌在播放時 map 改變，播放頭保留它音樂上的位置（第 19 章 Step 2），sample 則跳開。跟著 tempo 的 clip 也跟著它的起點跳。但是 clip 裡的播放位置不會落在原本正在播的那段 audio 上：從 clip 起點到播放頭的時間，在另一個 tempo 下是另一個長度。

在 120 BPM 下，進入 clip 一拍是它的 audio 的半秒。換成 60 BPM，進入 clip 一拍是一整秒：audio 往前跳了半秒。不跟著 tempo 的 clip 跳得更遠，這裡是 2.5 s，因為播放頭動了而 clip 沒動。不論哪一種，對 audio 來說都是一次 seek，也和任何 seek 一樣可能有 click：DawLab 的 engine 在播放頭跳動時不做 fade。

## 換你了

在 `steps/step02-follow-start/starter/step02.cpp` 裡寫出 `setFollow()`、`startUnder()`、`retime()`、`moveTo()`、`moveToTick()`、`endTick()`、`gaps()` 和 `jumpInClip()`。建置並執行：

```powershell
.\scripts\build.ps1 -Only 20-audio-tempo
.\build\bin\Debug\l20_step02_starter.exe
```

```text
Step 2: a clip whose start follows the tempo
  two clips on bar 2, then 60 BPM instead of 120: the one that follows starts at 4.0 s, the other at 2.0 s
  following turned on for 400 clips on random maps: 0 moved
  a clip on bar 9 through 1000 tempo maps and back: 0 samples from where it was, 0 times off its bar;
    with its tick found again from its sample each time: 15 samples
  300 clips moved to a tick and to a sample: 0 wrong
  8 clicks on 8 bars, then a map with a ramp: 8 of the clips that follow are on their bars (bar 8 at 18.806 s);
    of those that do not, 8 are where they were (bar 8 was at 14.000 s)
  a take cut in two at bar 3, both halves follow: at 60 BPM a gap of 4.0 s, at 240 BPM 2.0 s heard twice
  120 to 60 BPM while beat 2 of bar 2 plays: the audio of a clip that follows jumps by 0.5 s, of one that does not by 2.5 s
[PASS] l20_step02 (23 checks passed)
```

（忘記 tick 的那個 clip 偏的 15 個 sample 取決於 `log1p` 和 `expm1` 的最後幾位數；別的 compiler 的 library 可能給出稍微不同的數字。存著 tick 的 clip 的 0 則不會變。）

## 你學到了什麼

- 跟著 tempo 的 clip 存的是 tick，它的 sample 每次都從 tick 推導出來。
- 打開「跟著」不會移動任何東西，而 snap 保留的是 grid 自己的 tick。
- 只有起點跟著走時，clip 的長度仍然以 sample 計：適合單一的聲音，不適合切開的 take 和 loop——它們會分開或重疊。
- 播放中改變 tempo，對每個 audio clip 來說都是 clip 內部的一次 seek，不論它跟不跟。
