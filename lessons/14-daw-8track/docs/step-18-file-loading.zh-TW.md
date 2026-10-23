# Step 18 - 載入檔案與 demo song

[English](step-18-file-loading.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 17](step-17-vst3-inserts.zh-TW.md) | 下一步：[Step 19](step-19-export.zh-TW.md) | 回到[路線圖](../README.zh-TW.md#4-the-twenty-one-steps)

## 目標

Track strip 上的 "Load..."（或對選取的 track 按 Ctrl+O）會讀取一個 WAV 或 AIFF 檔——任何 sample rate、mono 或 stereo——把它轉換成 engine 的 sample rate，並作為 clip 放在 bar 1。"File > Load demo song"（Ctrl+D）會建出 Step 9 的 8-track demo song。兩者都不能讓 GUI 凍結，也都不能碰 audio thread，除非透過一份已完成的 clip 清單。

## 設計

### 三個 thread，單一方向

```
message thread                   loader thread (juce::ThreadPool, 1 worker)       audio thread
--------------                   ---------------------------------------------    ------------
Load... -> FileChooser
loader_.loadFile(t, file, fs) -> read with juce::AudioFormatReader
                                 mono -> stereo, resampleBuffer(fileRate -> fs)
                                 shared_ptr<const AudioBuffer>
on the message thread  <-------- MessageManager::callAsync(...)
Session::setTrackAudio()  -----------------------------------------------------> setTrackClips
                                                                                  (pointer swap)
```

Loader 從不碰 session 或 engine；它只產生一個 immutable 的 buffer，並把一個 closure post 給 message thread。如果到那時 GUI 已經不在了（檔案載入中 app 被關閉），一個共用的 `alive` flag 透過 `std::weak_ptr` 檢查後，會丟棄結果。`MediaLoader` 的 destructor 會等待正在執行的 job。

`Session::setTrackAudio()` 為 clip 加上 5 ms 的 fade（檔案很少剛好在 zero crossing 開始與結束；Step 3 解釋了 click），並以檔名為 track 命名。

### Resampling：band-limited interpolation

44.1 kHz 的檔案由 48 kHz 的 engine 播放，會快 8.8 %、高 1.5 個半音（$12\log_2(48000/44100) = 1.47$）。它必須被轉換。輸出 sample $m$ 位於輸入位置 $t_m = m \cdot f_{\text{in}} / f_{\text{out}}$，介於兩個輸入 sample 之間。Band-limited 訊號的理想重建（sampling theorem）是 sinc 函數之和；我們用加窗、低通過的 sinc kernel 在 $t_m$ 處求值：

$$
y[m] = \sum_k x[k]\; g(t_m - k), \qquad
g(x) = 2f_c\,\operatorname{sinc}(2 f_c x)\; w\!\left(\frac{x}{W}\right), \qquad
\operatorname{sinc}(u) = \frac{\sin \pi u}{\pi u}
$$

- $f_c$ 是以「每個輸入 sample 的 cycle 數」計的 cutoff，$f_c = 0.5 \cdot \min(1, f_{\text{out}}/f_{\text{in}}) \cdot 0.92$：剛好低於兩個 Nyquist frequency 中*較低*者。Downsample（$f_{\text{out}} < f_{\text{in}}$）時，這會移除原本會 alias 的成分；upsample 時，它移除 spectral image。"移除" 只是近似：加窗的 kernel 在 $f_c$ 附近有約 $5.5 / (2W) = 0.23 f_c$ 的 transition band，所以它的 stopband 在 $1.115 f_c = 0.513 f_{\text{out}}$ 附近才開始，剛好高於輸出的 Nyquist frequency。介於 $0.5$ 與 $0.513 f_{\text{out}}$ 之間的成分（44.1 kHz 輸出時為 22.05-22.6 kHz）仍會折返到 21.5-22.05 kHz（頻帶最上方的 2.6 %），至少約低 54 dB（README 第 3.10 節推導了這些數字）。
- $W = Z / (2 f_c)$ 是以輸入 sample 計的半寬，其中兩側各有 $Z = 24$ 個 sinc zero crossing；$w$ 是定義在 $[-1, 1]$ 上的 Blackman window。
- $2 f_c$ 把 kernel 的 DC gain 正規化為 1。

Kernel 只製表一次（每個 zero crossing 512 個點，點與點之間用 linear interpolation）；從 44.1 轉到 48 kHz 時，每個輸出 sample 約花 $2W \approx 52$ 次 multiply-add。這對 audio thread 來說太慢，但在 loader thread 上無關緊要。（這是教學用的 resampler；正式的函式庫使用 polyphase filter——同樣的數學，不同的記帳方式。）

### 把 demo song 當成資料

`engine/include/dawlab/engine/DemoSong.h` 描述的是與 `l14_render_song`（Step 9）手工建出的同一首 8-track song，只是以資料表示：`makeDemoSong(sampleRate)` 產生所有 audio 與 clip 清單、insert 清單（effect 類型 + *以名稱表示*的參數）、fader、pan 與 send——一個 pure function，在 loader thread 上執行（在 48 kHz 下會 allocate 約 40 MB）。接著 message thread 呼叫 `Session::applyDemoSong(song)`，它把每個 insert 放進其 strip 下一個空閒的 GUI slot，並把一切送給 engine。同一個 header 中的 `applyDemoSong(engine, song)` 則在沒有 GUI 的情況下做同樣的事（供 console 程式使用）。

因為 song 是以 engine 的 sample rate 產生的，44.1 kHz 的 device 會得到 44.1 kHz 的 song——不需要 resampling。如果 device 在 song 產生期間改變，結果會被丟棄並重新產生。檔案也受到同樣的保護：`loadAudioFile()` 會要求 session 的 *clip rate*（`Session::getClipSampleRate()`，通常就是 engine 的 rate，Step 11），而它的 callback 在結果抵達時，會比較 `media.targetSampleRate` 與 clip rate。如果 device 在這期間改變了 rate，檔案就直接依新的 rate 重新讀取——否則以舊 rate 表示的 audio 會進到 project 裡，永遠以錯誤的音高播放。

## 程式碼

- `app/Source/MediaLoader.h/.cpp`：`resampleBuffer()`、`MediaLoader::readAudioFile()`（同步的部分，可從測試使用）、`loadFile()`、`loadDemoSong()`、`convertBuffers()`（同一個 resampler 在 loader thread 上執行，當 device 的 sample rate 改變時使用，Step 11）。
- `app/Source/Session.cpp`：`setTrackAudio()`、`applyDemoSong()`、`applySampleRateConversion()`、`handleSampleRateChange()`（Step 11）。
- `app/Source/MainComponent.cpp`：`chooseAudioFileForTrack()`、`loadAudioFile()`、`loadDemoSongAsync()`（啟動時與 Ctrl+D）、`loadDemoSongNow()`（同步，供測試使用）。

## 驗證

`l14_app_smoke_test`，第 1 節：

```
1. resampler: 1 kHz 44.1k->48k amplitude 0.5000 (0.5 expected, -0.000 dB), length 48000;
   20 kHz 48k->22.05k alias at 2050 Hz: -122.2 dB below the input
   22.3 kHz 48k->44.1k (transition band) alias at 21800 Hz: -54.3 dB relative to the input
   file 44.1 kHz mono -> 2 channels, 48000 samples at 48 kHz, 1 kHz amplitude 0.5000
```

- 44.1 kHz 的 1 秒剛好變成 48 000 個 sample，而 1 kHz sine 維持它的振幅（passband 誤差 < 0.05 dB）。
- 20 kHz 的 sine 不可能存在於 22.05 kHz（Nyquist 11.025 kHz）。沒有 lowpass 的話，它會折返到 $22050 - 20000 = 2050$ Hz；用 Goertzel 在 2050 Hz 量測，它比輸入低 122 dB（檢查要求超過 60 dB）。
- 從 48 轉到 44.1 kHz 的 22.3 kHz 落在 kernel 的 transition band 裡，剛好高於新的 Nyquist frequency 22.05 kHz：它以 -54.3 dB 折返到 $44100 - 22300 = 21800$ Hz。這是這個 kernel 誠實的極限（檢查要求 -50 到 -80 dB，所以 kernel 的任何改動都會被看出來）。
- `MediaLoader::readAudioFile()`——loader thread 的函式——讀取一個 mono 44.1 kHz 的 WAV（由測試寫出），並回傳 48 kHz 下剛好 48 000 個 sample 的 stereo buffer，且音調完好。

第 2 節透過 `Session::applyDemoSong()`（GUI 的路徑）載入 demo song：19 個 insert、0 個未知的參數名稱、mix latency 752 個 sample——與 `l14_render_song` 的數字相同。

## 換你動手

| | |
|---|---|
| 你要寫 | `app/Source/MediaLoader.cpp` 中的 `resampleBuffer()`：輸出長度、cutoff $f_c = 0.46 \min(1, f_{\text{out}}/f_{\text{in}})$、「Resampling：band-limited interpolation」中的 windowed-sinc kernel（保留 `MediaLoader.h` 的 `ResamplerSettings::kZeroCrossings` 與 `kCutoffScale`） |
| 建置 + 檢查 | `.\scripts\build.ps1 -Only 14-daw-8track -Juce -TestLabel test`（`l14_app_smoke_test`） |
| 預期 | 第 1 節印出本頁的四行 resampler 輸出：1 kHz 振幅 0.5000 且長度 48 000、20 kHz alias 低於輸入超過 60 dB（作者：122.2 dB）、22.3 kHz transition-band alias 介於 -50 與 -80 dB 之間（作者：-54.3 dB）；`[PASS] l14_app_smoke_test` |

## 你學到了什麼

- 慢的工作交給 worker thread；結果透過 message thread 以 immutable 物件的形式回來；audio thread 永遠只看到完成的資料。
- Resampling 就是 filtering：cutoff 跟隨較低的 Nyquist frequency，而有限長度的 kernel 需要 transition band——去量測它在哪裡結束，而不是假設它是 brick wall。
- 把內容描述成資料，讓同一份描述既能由 GUI 套用，也能由測試套用。
