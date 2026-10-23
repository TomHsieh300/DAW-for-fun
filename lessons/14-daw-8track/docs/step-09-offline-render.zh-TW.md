# Step 9 - Offline render / bounce 成 WAV

[English](step-09-offline-render.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 8](step-08-pdc.zh-TW.md) | 下一步：[Step 10 - Real-time safety](step-10-realtime-safety.zh-TW.md)

## 目標

以 CPU 允許的最快速度把一首歌 render 成 WAV 檔 - 「bounce」或「export mixdown」- 而且聽起來
與 real-time 播放完全一樣，包含 effect 的尾巴、移除 latency，並印出 mix 報告
（每個 strip 的 peak、RMS、loudness）。

## 設計

### 同樣的程式碼，不同的時鐘

offline render 就是少了音效卡的播放迴圈：render 迴圈一個 block 接著一個 block 地呼叫
`processBlock()`，寫進一個大 buffer 中連續的 sub-block。因為它
是*同一個*函式，所以「你聽到的就是你 bounce 的」天然成立 - 前提是
engine 不依賴 block size（Step 10 會測試這一點），而且沒有任何東西依賴 wall-clock
時間。

```
applyPendingCommandsOffline()   clips and inserts queued during setup are linked in now
requestReset()                  tails, meters and smoothers start from zero
seek(start); play()
render songSamples              (the block that crosses the song end is cut there)
stop()
render tail + latency samples   tracks are silent, reverb / delay keep ringing, delayed audio flushes out
drop the first `latency` samples
restore the transport           the user's play position and loop survive the bounce
```

### 長度的計算

歌曲長度為 $`N_s`$，尾巴 $`N_t = \mathrm{round}(T_{\text{tail}} f_s)`$，mix latency 為 $`L`$：

$$
N_{\text{render}} = N_s + N_t + L, \qquad \text{file} = \text{render}[L,\; L + N_s + N_t)
$$

所以檔案的 sample 0 就是 timeline 的 sample `startSample`，檔案長度恰好是 $`N_s + N_t`$。
以本章的歌曲為例：120 BPM 下 8 個小節 = $`8 \cdot 96\,000 = 768\,000`$ 個 sample，3 秒的尾巴 = 144 000，
latency 752，所以 render 了 912 752 個 sample，寫出 912 000 個（19.00 秒）。

### Thread 規則

render 在呼叫端的 thread 上執行，扮演 audio thread 的角色。當 audio device 也在同一個 engine 上
呼叫 `processBlock()` 時，它不可以執行。GUI app 要不是在 bounce 期間停止
device，就是用專案建立第二個 `Engine`（Exercise 4），並在 worker thread 上 bounce 它，
同時繼續播放。

### Mix 報告

因為 `requestReset()` 清除了每個 meter，所以 render 之後每個 strip 的精確統計值
（`ChannelStrip::getStats()`：最大 peak、整個 render 的 RMS、來自第 11 章
`LoudnessMeter` 的 integrated LUFS）所描述的，恰恰就是這次 bounce。

## 程式碼

`Renderer.h`：

```cpp
struct RenderOptions {
    int64_t startSample = 0;        // timeline position to start from
    int64_t numSamples = 0;         // 0 = up to the end of the last clip
    double tailSeconds = 0.0;
    int blockSize = 512;
    bool resetBeforeRender = true;
    bool compensateLatency = true;
    bool restoreTransport = true;
};
AudioBuffer renderOffline(Engine&, const RenderOptions&);
bool bounceToWav(Engine&, const std::string& path, const RenderOptions&, int bitDepth = 24,
                 AudioBuffer* renderedOut = nullptr);
```

`renderOffline()` 在每個 block 之後都會呼叫 `engine.collectGarbage()`，所以有 command
在途中的 render 永遠不會讓 garbage queue 滿載。

### 歌曲（`examples/render_song.cpp`）

| Track | 素材（Signals.h） | Insert | Fader / pan | Send |
|---|---|---|---|---|
| 1 Drums | `makeDrumLoop` 2 小節 x 4 個 clip | TransientShaper、Compressor | -2 dB / C | reverb -18 |
| 2 Bass | `makePluck` A1 F1 C2 G1，16 個 clip | ParametricEQ（low cut 35 Hz，120 Hz 處 +3 dB）、Saturation | -3 / C | - |
| 3 Arp | 從第 3 小節開始的 `makePluckSequence`，1 小節 fade-in | Gain -3、Phaser | -2 / L 0.35 | reverb -14、delay -8 |
| 4 Pad | detuned 的和聲和弦 Am F C G | ParametricEQ（150 Hz low cut、5 kHz high cut）、Chorus | -4 / C | reverb -8 |
| 5 Wind | 經過 low-pass 的 noise，第 5-8 小節 | Tremolo（8 Hz square）、Gate | -10 / R 0.5 | reverb -12 |
| 6 Lead | 帶 vibrato 的 sine 旋律，第 5-8 小節 | Distortion（fuzz）、Flanger | -5 / R 0.2 | reverb -12、delay -6 |
| 7 Shaker | 經過 high-pass 的 noise burst，16 分音符 | Expander、DeEsser | -8 / R 0.6 | reverb -20 |
| 8 Hits | 在第 1、3、5、7 小節的 noise burst | ConvolutionReverb | -6 / L 0.6 | - |
| Bus A | | Reverb（100 % wet，2.2 秒） | -2 | |
| Bus B | | Delay（100 % wet，附點 1/8 = 375 ms，ping-pong） | -4 | |
| Master | | Compressor（-12 dB 處 2:1）、Limiter（-1 dBFS，5 ms） | 0 | |

有 17 種 effect 類型出現一次或多次 - 除了 Filter 與 AmpChain 之外全部，那兩個是在這首歌寫好之後
才加進 catalog（Step 5）的。

## Your turn

`steps/step09-offline-render/minidaw.h` 是你的 Step 8 engine 加上 `Engine::requestReset()`、
`Engine::applyPendingCommandsOffline()`、`Mixer::getSongEndSample()`、`RenderOptions`（沒有
完成版 engine 的 `resetBeforeRender` / `restoreTransport` 開關：snapshot 永遠會 reset
並還原）以及 `bounceToWav()`。這是完整的迷你 engine。

| | |
|---|---|
| 你要寫的 | `renderOffline()`：長度計算、儲存 / 還原 transport、reset、render 歌曲（在結尾處切斷）、stop、render 尾巴 + latency、丟掉前 `latency` 個 sample |
| 檔案 | `steps/step09-offline-render/starter/step09.cpp` |
| 執行 | `l14_step09_starter`；在你完成之前它會印出 `[FAIL] l14_step09 (11 of 14 checks failed)` |
| Solution | `steps/step09-offline-render/solution/step09.cpp`（ctest `l14_step09_solution`） |

測試用的歌曲長 1 秒（在 track 1 的 sample 0 與 24 000 處分別有 0.5 與 0.25 的 impulse），
master 上有一個 240-sample 的 delay，用來代替 lookahead limiter。預期輸出（已驗證）：

```
Step 9: offline render (build-along)
  rendered 72000 samples (expected 48000 + 24000 = 72000), latency 240 (expected 240)
  file[0] = 0.3536, file[24000] = 0.1768 (expected 0.3536, 0.1768: the latency is gone)
  transport after the bounce: position 12345, loop on, stopped (expected 12345, on, stopped)
  wrote dawlab_output/l14_step09.wav: 72000 samples at 48000 Hz
  bounce vs live playback shifted by the latency: max difference 0 (expected 0)
  render from 24000: 24000 samples (expected 24000), file[0] = 0.1768 (expected 0.1768)
  tail: 60000 samples (expected 60000), the echo at 52900 = 0.3536 (expected 0.3536)
[PASS] l14_step09 (14 checks passed)
```

尾巴那一行使用的是一個未補償的 5000-sample delay，作用在一個於歌曲結尾結束的 clip 上：它的
「回聲」會晚 5000 個 sample 出現，位於尾巴之內，因為 insert 在
transport 停止之後仍持續執行。這個檢查也會以 37-sample 的 block 做 render，並要求產生相同的檔案。

## Verify（參考路線）

```powershell
cd build; .\bin\Debug\l14_render_song.exe
```

作者執行的報告（Debug build；確切的 LUFS 數值取決於第 01-12 章的
實作，但結構與 limiter 的 -1.00 dBFS 不會變）：

```
Step 9: offline bounce of an 8-track song (8 bars at 120 BPM + 3 s tail)
  length 19.00 s, rendered in 13.14 s (1.4x real time), mix latency 752 samples (15.67 ms)

  strip     peak     RMS     LUFS   fader   pan    inserts
            dBFS    dBFS      (I)
  Drums     -3.3   -27.4    -24.8    -2.0 dB  0.00   TransientShaper, Compressor
  Bass      -8.9   -22.6    -16.8    -3.0 dB  0.00   ParametricEQ, Saturation
  Arp      -11.9   -33.9    -27.0    -2.0 dB -0.35   Gain, Phaser
  Pad      -19.6   -33.5    -30.5    -4.0 dB  0.00   ParametricEQ, Chorus
  Wind     -20.3   -42.3    -34.7   -10.0 dB  0.50   Tremolo, Gate
  Lead     -12.6   -27.7    -21.4    -5.0 dB  0.20   Distortion, Flanger
  Shaker   -17.9   -47.1    -38.8    -8.0 dB  0.60   Expander, DeEsser
  Hits     -16.6   -46.0    -31.9    -6.0 dB -0.60   ConvolutionReverb
  Reverb   -19.6   -36.9    -33.8    -2.0 dB  0.00   Reverb
  Delay    -20.7   -39.7    -34.3    -4.0 dB  0.00   Delay
  Master    -1.0   -20.2    -15.5     0.0 dB  0.00   Compressor, Limiter

  file: peak -1.00 dBFS, RMS -20.2 dBFS, integrated loudness -15.5 LUFS
  wrote dawlab_output/l14_render_song.wav
  drums solo: peak -1.56 dBFS, wrote dawlab_output/l14_render_song_drums_solo.wav
  silent strips in the solo bounce: Bass -100 dBFS, Arp -100 dBFS (silenced by the solo)
[PASS] l14_render_song (7 checks passed)
```

自我檢查：沒有 NaN；長度恰好是 768 000 + 144 000 個 sample；檔案 peak $`\le`$ -0.95 dBFS（limiter
的天花板）；歌曲不是靜音；solo 的 bounce 會讓其他 strip 靜音。

如何閱讀這份報告：bass 在各 track 中擁有最高的 loudness（-16.8 LUFS），儘管它的
peak 中等 - 這是低頻、持續的能量。shaker 的 peak 對 RMS 比（crest factor）接近
30 dB：短促的 burst。master 的 integrated loudness -15.5 LUFS 落在典型
串流目標（-14 LUFS）的範圍內；它的 RMS -20.2 dBFS、peak -1 dBFS，crest factor 為 19 dB -
master 的 compressor 與 limiter 作用得很輕柔。

同一首歌也以*資料*的形式存在於 `engine/include/dawlab/engine/DemoSong.h`
（`makeDemoSong()` + `applyDemoSong()`），第 2 階段的 app 會在 worker thread 上載入它（Step 18）。
`l14_test_demo_song` 會 bounce 它，並檢查它重現了這份報告：912 000 個 sample、
752 個 sample 的 latency、peak -1.00 dBFS、master -15.5 LUFS、鼓 -3.3 / -27.4 dBFS、bass
-16.8 LUFS，並且資料中的每個參數名稱都存在於它的 effect 中。

聽聽 `l14_render_song.wav`。結尾的尾巴會持續 3 秒：reverb bus、
在左右之間彈跳的 ping-pong delay，以及 hits 的 convolution 房間。

## 你學到了什麼

- bounce 就是把時鐘從 device 手中拿走的播放迴圈 - 僅此而已。
- 先 reset、render 尾巴、render latency、切掉 latency、還原 transport。
- 第 11 章的 meter 把一次 render 變成數值化的 mix 報告。
