# Step 3 - 樂器的 latency

[English](step-03-latency.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 2 - Tempo map 上的 transport](step-02-transport.zh-TW.md) | 下一步：[Step 4 - Engine](step-04-engine.zh-TW.md)

## 目標

讓一首歌的音符在樂器回應得晚時仍然一起出來——預先讀取的 sampler、以自己的 block 運作的 synth——並知道這對現場彈奏的演奏者有什麼代價，以及它不會改變錄音的什麼。

## 設計

### 樂器的 latency 就是 track 的 latency

第 18 章的 synth 在音符進來的那個 sample 就回應。許多樂器不是這樣，而會回報一個 latency：它們對一個音的聲音晚那麼多個 sample 才出來。這和 effect 的延遲是同一種，第 14 章已經有答案——plugin 延遲補償。現在 track 的 latency 是它的樂器加上它的 insert：

```math
L_t = L_{instrument} + L_{inserts}, \qquad total = \max_t L_t, \qquad compensation_t = total - L_t
```

每個 track 再多等 $`compensation_t`$，所以任何 track 上在 sample $`S`$ 的音都在 $`S + total`$ 被聽到：全部一起，整個混音晚同樣的量。對一首歌來說這就夠了——第 14 章的播放頭本來就顯示聲音所在的位置，而不是 engine render 的位置。

| Track | 樂器 | Insert | $`L_t`$ | 等待 |
|---|---|---|---|---|
| Synth | 0 | 0 | 0 | 4096 |
| Sampler | 256 | 0 | 256 | 3840 |
| Synth + limiter + filter | 64 | 32 | 96 | 4000 |
| 串流 sampler，已 arm | 4096 | 0 | 4096 | 0 |

### 補償做不到的：現在彈的鍵

從鍵盤來的音，在它之前的 block 被 render 時還沒發生。它要經過全部：

```math
heard = delay_{scheduled} + L_t + compensation_t + L_{output}
```

在串流 sampler 上是 $`1024 + 4096 + 0 + 512 = 5632`$ 個 sample，117 ms——那是它自己的 latency，除了換樂器沒有辦法。在 synth 上同樣是 5632 個 sample，雖然 synth 立刻回應：synth 在*等待*另一個 track 上的 sampler，因為補償把每個 track 都延遲到最慢的那個。彈 synth 的演奏者感受到一個不屬於他們的 latency。

### Low-latency monitoring

所以 DAW 讓 arm 的 track 在演奏者彈奏時跳過補償：它的補償為 0，也不計入 `total`。只 arm synth、sampler 不動，synth 的鍵在 $`1024 + 0 + 0 + 512 = 1536`$ 個 sample、32 ms 之後被聽到。這時 arm 的 track 比其他 track 早了它原本的補償量——跟著彈的時候沒有人聽得出來。Take 回放時，補償又是完整的。改成 arm sampler，low-latency monitoring 則幫了其他 track：`total` 從 4096 降到 256，整首歌不再為了一個樂器等 85 ms。

### 錄音不會移動

錄下的音符屬於演奏者按鍵時聽到*歌*的位置（第 18 章 Step 6）。歌在 render 之後 $`total + L_{output}`$ 才出來，所以在 engine render $`R`$ 時按下的鍵屬於

```math
R - total - L_{output}
```

Arm 的樂器自己的 latency 不在裡面。它讓演奏者*感覺*音晚了，而演奏者會用提早彈來適應晚出來的聲音——但音符擺在他們聽到拍子的地方，不論那是哪裡。Self-check 把一個正好在拍子上彈的音，在 arm 的 sampler latency 為 0、256、4096 和 20000 個 sample 時各擺一次：永遠在拍子上。

## 換你了

在 `steps/step03-latency/starter/step03.cpp` 裡寫出 `planLatency()`、`heardLive()`、`placeRecorded()`、`Mix::setTracks()` 和 `Mix::render()`。`LatentInstrument`（每個 note on 之後 `latency` 個 sample 的脈衝）和 `DelayLine` 已經提供。建置並執行：

```powershell
.\scripts\build.ps1 -Only 19-instruments-tempo
.\build\bin\Debug\l19_step03_starter.exe
```

```text
Step 3: instrument latency
  latencies 0, 256, 64+32 and 4096 (armed): total 4096, waits 4096 3840 4000 0; with low-latency monitoring: total 256, waits 256 0 160 0
  a key to the ear on the sampler: 5632 samples (117 ms) either way; on the synth 5632 (117 ms), with low-latency monitoring 1536 (32 ms)
  a note played on the beat it heard: at 96000 whatever the armed sampler's latency (low-latency total 256)
  a chord on the four tracks at 10000: one impulse of 4 at 14096; with low-latency monitoring 3 at 10256 and the armed one at 14096
  100 random sessions in random blocks: 0 where the tracks did not arrive together at the note + total
[PASS] l19_step03 (18 checks passed)
```

## 你學到了什麼

- 樂器的 latency 是它 track 的 latency 的一部分，第 14 章的補償把 track 對齊。
- 補償對一首歌是免費的，對現在彈的鍵則很昂貴：每個 track 都等最慢的那個。
- Low-latency monitoring 在演奏者彈奏時把 arm 的 track 移出補償。
- 錄下的音符依歌被聽到的時間擺放；樂器自己的 latency 不會移動它。
