# Step 6 - Send / return bus

[English](step-06-sends-buses.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 5](step-05-insert-effects.zh-TW.md) | 下一步：[Step 7 - Commands](step-07-commands.zh-TW.md)

## 目標

加入兩條 FX bus（例如一條 reverb 與一條 delay），每個 track 都能透過自己的 send
level、在 fader 之前或之後送訊號給它們，並把它們 return 到 master。

## 設計

### 為什麼用 bus，而不是八個 reverb？

在每個 track 上都放一個 reverb insert，會消耗八個 reverb 的 CPU，而且各 track 會處在
八個不同的「房間」。**Send** 會以可調的 level 把 track 的一份複本分接進一條 **bus**；
bus 擁有一個 100 % wet 的 reverb；它的輸出（**return**）會和乾的 track 一起混進
master。一個房間、一個 reverb，每個 track 的 dry/wet 比例就只是它的 send
level。

### Pre-fader 或 post-fader

```math
\text{bus}_b[n] = \sum_{t=1}^{8} s_{t,b}[n]\cdot
\begin{cases} g_t[n]\,x_t[n] & \text{post-fader (default)} \\ x_t[n] & \text{pre-fader} \end{cases}
```

- **Post-fader**（在 fader *以及* pan 之後）：把 fader 拉低，reverb 也跟著變小；
  wet/dry 比例保持不變。這是 effect 所需要的。
- **Pre-fader**：send 忽略 fader。用於 monitor / 耳機 mix，或是在把 dry
  訊號 fade out 後只留下它的 reverb。

send level $`s_{t,b}`$ 和 fader 一樣會做 ramp（50 ms）。-100 dB（`kMinusInfinityDb`）代表關閉 -
這是每個 send 的預設值 - 而且不花成本：當 send gain 為 0 且沒有在 ramp 時，
`addFrom` 會被略過。

### Mute 與 solo 作用在哪裡

Mute/solo 套用在兩個 send 取樣點**之前**。結果是：

- 被 mute 的 track 什麼也不送 - 連 pre-fader 也不送（否則「mute」之後它的 reverb
  還會繼續播放，這會讓每個人都很意外）；
- solo 某個 track 也會讓*其他* track 的 send 靜音，但 bus 本身是
  **solo-safe** 的：它們沒有 solo，也永遠不會因為 track 的 solo 而被靜音，所以被 solo 的 track 會
  *連同*它的 reverb 一起被聽到。

### Bus strip

bus 是一個 `PanMode::Balance` 的 `ChannelStrip`（它承載 stereo）：inserts -> PDC -> mute ->
fader x balance -> meters -> master。bus 原則上可以互相送訊號；我們不允許
這麼做（沒有 feedback loop、沒有順序問題）- mixer graph 是固定的：
track -> bus -> master。

## 程式碼

`ChannelStrip.h`：

```cpp
void setSendLevelDb(int bus, float db) noexcept;   // any thread; <= -100 dB = off
void setSendPreFader(int bus, bool preFader) noexcept;
void addSend(int bus, AudioBlock dest, const AudioBlock& src) noexcept;   // dest += src * ramped gain
```

`Mixer::process()`：

```cpp
track.applyMuteSolo(tb);
for (int b = 0; b < kNumBuses; ++b)
    if (track.isSendPreFader(b)) track.addSend(b, bus[b], tb);       // pre-fader tap
track.applyFaderAndPan(tb);
for (int b = 0; b < kNumBuses; ++b)
    if (!track.isSendPreFader(b)) track.addSend(b, bus[b], tb);      // post-fader tap
...
for (int b = 0; b < kNumBuses; ++b) {                                // after all tracks
    strip.beginBlock(!strip.isMuted());
    strip.processInserts(bus[b]);   strip.applyMuteSolo(bus[b]);
    strip.applyFaderAndPan(bus[b]); strip.meter(bus[b]);
    master.addFrom(bus[b]);
}
```

即使 send 是關閉的（或正被關閉），每個 block 也都會呼叫它，這樣它的 smoother 才會一直
朝目標移動 - 否則被關掉的 send 會凍結在舊的 gain 上，重新打開時就會跳動。

## Your turn

`steps/step06-sends-buses/minidaw.h` 是你的 Step 5 engine 加上 `ChannelStrip` 中的 send level（atomic + 每條 bus 一個 ramp）
與 pre/post 開關，以及兩個 balance 模式的 bus strip，它們的 return 由
`Mixer::process()` 在所有 track 之後加進 master。

<div align="center">

| | |
|---|---|
| 你要寫的 | `ChannelStrip::addSend()`（`dest += src * ramped gain`，每個 sample 一次 `getNextValue()`），以及再次寫 `Mixer::processTrack()`，這次在 `applyFaderAndPan()` 之前加入 pre-fader 取樣點、之後加入 post-fader 取樣點 |
| 檔案 | `steps/step06-sends-buses/starter/step06.cpp` |
| 執行 | `l14_step06_starter`；在你完成之前它會印出 `[FAIL] l14_step06 (9 of 10 checks failed)` |
| Solution | `steps/step06-sends-buses/solution/step06.cpp`（ctest `l14_step06_solution`） |

</div>

預期輸出（已驗證）- 先是上面的算例，接著是 mute、兩條 bus、solo 與 ramp：

```
Step 6: sends and buses (build-along)
  direct 0.5 g + bus A (0.5 g x 0.5 post + 0.25 pre) : out 0.7803 (expected 0.7803), bus A 0.4268 (expected 0.4268)
  bus A muted: 0.3536 (expected 0.3536); track 2 muted: 0.5303 (expected 0.5303)
  one track, send A 0 dB + send B -6 dB: 0.8839 (expected 0.3536 x 2.5 = 0.8839)
  solo track 1, both tracks send to bus A: 0.7071 (expected 0.5 g + 0.5 g = 0.7071)
  send switched on while playing: max step 0.000147 (expected < 0.001), ends at 0.7071 (expected 0.7071)
[PASS] l14_step06 (10 checks passed)
```

## Verify（參考路線）

`l14_test_mixer`，`testSends()`（已驗證）。Track 1：DC 0.5，以 -6.02 dB 做 post-fader send 到 bus A。
Track 2：DC 0.25，fader 在 $`-\infty`$，以 0 dB 做 pre-fader send 到 bus A。其中 $`g = 0.7071`$（track
置中 pan），bus / master 為 unity balance：

```math
\text{direct} = 0.5\,g = 0.3536,\qquad
\text{bus A} = \underbrace{0.5\,g \cdot 0.5}_{\text{post}} + \underbrace{0.25 \cdot 1}_{\text{pre}} = 0.4268,\qquad
\text{out} = 0.3536 + 0.4268 = 0.7803
```

測試檢查了 `out`、bus A 的 meter（`getMaxPeakLinear() = 0.4268`）、mute bus A 之後只剩
`direct`、mute track 2 也會移除它的 pre-fader 貢獻，以及一個 track
同時送給兩條 bus 時會加上兩個 return。`testSoloKeepsReverbBus()` 在兩個 track 都
送給 bus A 的情況下 solo track 1：輸出只有 track 1 加上*它自己的* reverb return（0.5 g + 0.5 g）。

在 `l14_render_song` 中，arp 與 lead 餵給 bus B（ping-pong delay，附點八分音符 = 120 BPM 下的 375 ms），
大部分 track 餵給 bus A（reverb，2.2 秒）。報告中的 bus meter（已驗證）：

```
  Reverb   -19.6   -36.9    -33.8    -2.0 dB  0.00   Reverb
  Delay    -20.7   -39.7    -34.3    -4.0 dB  0.00   Delay
```

而 `l14_render_song_drums_solo.wav` 是帶有自己 reverb 的鼓 solo。

## 你學到了什麼

- 共用的 effect 屬於 bus；track 透過 send 到達它們。
- Post-fader send 保持 wet/dry 比例；pre-fader send 忽略 fader。
- 在 send 取樣點之前做 mute/solo + solo-safe 的 bus = 使用者所預期的行為。
- 即使 send 是關閉的也要對它的 gain 做 ramp，這樣它才不會跳動。
