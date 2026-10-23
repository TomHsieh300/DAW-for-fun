# Chapter 14 - 練習題

[English](README.md) | **繁體中文**

[回到本章](../README.zh-TW.md) | [解答](../solutions/README.zh-TW.md)

難度：★☆☆ 簡單、★★☆ 中等、★★★ 困難。實作型練習在 `exercises/starter/` 中附有一個可編譯、可執行的 starter 檔；在你完成它之前，它的 self-check 會印出 `FAIL`（這是預期的）。Build target 名稱寫在方括號內。

本章的**練習題組是 Exercise 1-7**（概念、計算、設計，以及三個實作練習），內容關於 engine（stage 1，不用 JUCE）。**Exercise 8-13 是選修專案**，關於應用程式 DawLab 8（stage 2）：更大、更開放的延伸，給想在本章之後繼續打造這個 DAW 的讀者。它們需要 JUCE build（`.\scripts\build.ps1 -Only 14-daw-8track -Juce`）。JUCE 練習是 console 程式，連結 app 函式庫 `l14_daw_app`，並在 offline 模式下建出真正的 `MainComponent`，所以檢查它們既不需要 audio device，也不需要點擊。**Exercise 14** 是隨 mono / stereo track format 加入的：一個像 3-5 那樣的 engine 實作練習（不用 JUCE），放在最後。

---

## Exercise 1 - 誰可以碰什麼？★☆☆（概念）

每題用幾句話回答：

1. 一位同事用 `std::mutex` 保護 insert chain：UI 為了加入 effect 而鎖它，audio thread 在 `processBlock()` 中鎖它。在他的機器上能運作。說明可能出什麼問題，並指出這個現象的名稱。
2. 為什麼 `Engine::removeInsert()` 不是在 audio thread unlink effect 之後直接 `delete` 它，而是透過第二個 queue 送回來？
3. 如果 garbage queue 滿了，audio thread 會 *leak* 該物件而不是刪除它。為什麼這是正確的選擇？為什麼在這個 engine 中實際上不會發生？
4. 為什麼 fader 移動與 effect 參數不需要 command queue，而加入 effect 卻需要？
5. 一個 track 被 mute，但它通往 reverb bus 的 pre-fader send 是 0 dB。你會從那個 track 聽到什麼？我們為什麼選擇這種行為？

## Exercise 2 - 手算 timing 與 latency ★★☆（計算）

用手算（寫出公式），然後執行 [`l14_ex2_solution`] 來檢查：

- (a) 一個 44.1 kHz、96 BPM、3/4 拍的 project。一個 beat 與一個 bar 各有多少 sample？Bar 5、beat 2 在哪裡（以 sample 與秒表示）？Sample 1 000 000 是哪個 bar:beat:tick（每個 beat 960 ticks）？
- (b) Bus B 上的 delay 應該以 96 BPM 的附點八分音符重複。Delay time 是多少 ms，在 44.1 kHz 下是多少 sample？「每四分音符一個週期」的 LFO rate 是多少 Hz？
- (c) 在 48 kHz 下，track 1-4 的 insert latency 分別為 0、64、240 與 1024 個 sample（track 5-8 沒有），bus A 沒有，bus B 有一個 512 個 sample 的 convolution reverb，master 有一個 240 個 sample 的 limiter。給出每個補償 delay（track、bus、track sum）以及以 sample 與 ms 表示的總 latency。
- (d) Track fader 在 -6 dB，pan +0.5，constant-power law：左右 gain 各是多少，以線性與 dB 表示？如果 strip 是 balance 模式呢？
- (e) 每條 PDC delay line 最多容納 16 384 個 sample（+1），stereo，`float`。它們全部用掉多少記憶體（11 個 strip + track-sum 那條 line）？

## Exercise 3 - 音量與 pan automation ★★☆（實作）

[`l14_ex3_starter`，檔案 `exercises/starter/ex3_automation.cpp`]

實作一個 breakpoint envelope（"automation lane"），並在 engine 內以 sample 精度使用它：

- `AutomationLane::valueAt(t)`：已排序的 breakpoint 之間做 linear interpolation；第一個點之前 / 最後一個點之後維持第一個 / 最後一個值；空的 lane 回傳它的預設值。
- `AutomationLane::render(start, out, n)`：對整個 block 做同樣的事，但只做一次 binary search，之後使用向前移動的 cursor，而不是每個 sample 搜尋一次。
- `VolumeAutomationEffect::processDsp()`：lane 的值是 dB；把每個 channel 乘以 $`10^{v/20}`$。（Effect 透過 `setPlayHead()` 接收 block 的 timeline segment；base class 已經把它們轉成每個 sample 一個值，所以 loop 也能運作。）

Self-check 讓一個 DC clip 經過一個從 0 dB（0.5 s）到 -24 dB（1.5 s）的 fade，並要求在 1.0 s 時剛好是 -12.00 dB、block size 512 與 37 的輸出相同、第二次 loop pass 相同，以及一個 constant-power pan sweep。

提示：為什麼要在 dB 而不是線性 gain 上做 automation？比較線性 gain ramp 1 -> 0.063 與 dB ramp 0 -> -24 dB 的中途，你聽到什麼。

## Exercise 4 - 以 JSON 儲存與載入 project ★★★（實作）

[`l14_ex4_starter`，檔案 `exercises/starter/ex4_project_json.cpp`]

Starter 給你一個最小的 JSON parser（`parseJson`、`Json`）與 writer helper（`jsonString`、`num`、`boolean`）。請寫出：

- `saveProject(engine, pool)`：transport（tempo、拍號、loop）、PDC flag，以及全部 11 個 strip 的：名稱、fader、pan、pan law、pan mode、mute、solo、send（level、pre/post）、insert（類型名稱、bypass、**以名稱**表示的每個參數），以及 track 的 format（Mono / Stereo，Step 4）與 clip（media id、start、length、offset、gain、fade、shape、muted）。Audio 不存進檔案：clip 引用 `MediaPool` 的 id。
- `loadProject(engine, text, pool, &error)`：反向操作，使用 `createEffect(name)`、`setParameterByName()`、`Engine::addInsert()` 與 `Engine::setTrackClips()`。track 的 format 要在 pan mode *之前*還原：`Track::setFormat()` 也會設定 pan mode。未知的參數名稱會被跳過；未知的 effect 類型或缺少的 media 是帶有訊息的錯誤。

Self-check 儲存一個 project，把它載入一個全新的 engine，再儲存一次，並要求**完全相同的 JSON 文字**以及**逐 bit 相同的重新 render**——所以想想一個 `float` 需要多少位數。

## Exercise 5 - 移動 clip 與 crossfade ★★☆（實作）

[`l14_ex5_starter`，檔案 `exercises/starter/ex5_clip_crossfade.cpp`]

- `snapToGrid(position, gridSamples)`：取最近的 grid 線（grid 為 0 時不做 snapping）。
- `moveClip(clips, index, newStart, gridSamples)`：帶 snapping 的移動，絕不早於 0，保持 vector 依 start 排序，回傳新的 index。
- `crossfade(a, b, overlap, shape)`：移動 clip `b`，使它與 `a` 的結尾重疊 `overlap` 個 sample，並給兩者相配的 fade。

Self-check 透過 engine 量測四種 crossfade。在執行之前先預測：對於*相同* audio 的兩份副本，以及兩個*獨立*的雜訊，Linear 與 EqualPower crossfade 中間的電平各是多少？

## Exercise 6 - 設計每個 track 的 input recording ★★☆（設計）

在紙上為 engine 加入 recording（不需要程式碼；歡迎 pseudo-code）。你的設計必須回答：

1. Input audio 從哪裡進入 `processBlock()`，以及當長度未知的 take 持續增長時，你如何避免 allocate？
2. 哪個 thread 寫 WAV 檔，audio 如何在沒有 lock 的情況下抵達它？
3. Interface 有 input latency $`L_{\text{in}}`$ 與 output latency $`L_{\text{out}}`$，而 mix 有 PDC latency $`L_{\text{pdc}}`$。音樂人是跟著他聽到的東西演奏的。錄下的 clip 必須放在 timeline 的哪個位置？
4. 如果磁碟在一段時間內太慢，audio thread 上會發生什麼？
5. Input monitoring（透過 track 的 insert 聽到自己）如何與 PDC 互動？

## Exercise 7 - Tempo 變化與 time-stretching ★★★（設計）

到目前為止 clip 都是以 sample 計，所以改變 tempo 會移動 grid 但不會移動 audio：以 120 BPM 錄製的 drum loop 在 100 BPM 就不再對得上。

1. Clip 必須改為（或額外）儲存什麼才能跟隨 tempo 變化？區分「musical」（以 beat 為基礎）與「absolute」（以 sample 為基礎）的 clip——哪種素材需要哪一種？
2. 為什麼 resampling（較快/較慢地播放）對大多數素材不可接受，而 time-stretch 演算法改為做什麼？舉出兩種做法及其典型的 artefact。
3. Time-stretching 很昂貴而且有 latency。你會在哪裡執行它——在 audio thread 上每個 block 做，還是在別處？勾勒一個仍然 real-time safe、且在使用者於播放中改變 tempo 時仍能反應的設計。
4. 有了 tempo *map*（tempo 隨時間變化），你如何把 timeline 的 sample 轉換成 beat 位置？什麼資料結構能讓這件事很快？

---

# 選修專案（Exercise 8-13）：應用程式（JUCE）

這六個專案不屬於本章的核心練習題組；挑你有興趣的做即可。每個都有 starter、附 self-check 的完整解答，以及解答中的說明文章。

## Exercise 8 - GUI 中的 automation lane ★★★（實作）

[`l14_ex8_starter`，檔案 `exercises/starter/ex8_automation_lane.cpp`]

Exercise 3 從程式碼 automate 一個 fader。現在換使用者來畫 automation。請完成：

- `AutomationLaneData::valueAt(t)`：已排序的 breakpoint（dB 值）之間做 linear interpolation；範圍外維持第一個 / 最後一個值。
- `AutomatedGainEffect::process()`：根據 PlayHead segment 所給的 timeline position，求出每個 sample 的 gain（把新 lane 交給 audio thread 的交接機制已經寫好——去讀它：一個 atomic pointer 進去，一個 `SpscQueue` 回來，在 UI thread 上刪除）。
- `AutomationLaneComponent`：`valueToY` / `yToValue`（頂端 +6 dB，底端 -48 dB，在 dB 上線性）、`pointFromPosition`（x 透過 arrangement 的 `xToSample()` 與 `snapToBeat()`）、`hitTest`（6 像素半徑），以及三種手勢：點擊 = 選取或新增，拖曳 = 移動但絕不越過鄰居，右鍵 = 移除。

Self-check 建出一個真正的 `ArrangementView`（一個 beat = 125 px），用座標執行這些手勢，並讓一個 DC clip 經過該 effect：在 0 dB 與 -24 dB 兩點之間的中途剛好是 -12.00 dB、block size 512 與 37 的輸出相同，且在播放期間 publish 200 次後沒有 lane 物件被 leak。提示：為什麼拖曳必須把點 clamp 在它的鄰居之間，而不是重新排序清單？

## Exercise 9 - 將 audio input 錄進 track ★★★（實作）

[`l14_ex9_starter`，檔案 `exercises/starter/ex9_recording.cpp`]

把 Exercise 6 的設計變成程式碼。`RecordingCallback` 是一個 `juce::AudioIODeviceCallback`，它像 `AudioHost` 一樣 render engine，並把 device 的 input 交給 `InputRecorder`：

- `pushInput()`（audio thread）：把 block 複製進預先配置好的 `juce::AbstractFifo` ring（wrap 時分成兩個區域）。如果沒有足夠的空間：計一次 overrun 並丟棄該 block——絕不等待。
- `drain()`（disk thread）：把所有就緒的東西移進持續增長的 take。
- `clipStart()`：給定 device 的 input / output latency 與 mix 的 PDC latency，take 在 timeline 上的位置。

測試從第二個 thread 以 440 Hz 的音調呼叫該 callback（不需要 device），同時由一個 `juce::Thread` 進行 drain。要求：take 在經過多次 wrap 的 FIFO 之後逐 sample 等於 input、零 overrun、clip 位於 96 000 - (64 + 96 + 240)，以及當沒人 drain 一個 0.25 s 的 FIFO 時 overrun 的精確次數。

## Exercise 10 - 帶簡單 synthesizer 的 MIDI track ★★★（實作）

[`l14_ex10_starter`，檔案 `exercises/starter/ex10_midi_synth.cpp`]

`SimpleSynth` 是一個 *instrument insert*：它位於 track 的第一個 slot，並把它的 voice 加進（空的）track 中。它的參數（Waveform、Attack、Decay、Sustain、Release、Gain）像任何 effect 一樣在 app 中得到自動產生的面板。請完成：

- `notesFromMidi()`：以 tick 為單位（每個 beat 960）的 `juce::MidiMessageSequence` -> 以 sample 為單位的 `NoteEvent`，note-on 與其 note-off 配對（`updateMatchedPairs`）。
- `noteOn()`：voice 配置——有空閒的 voice 就用，否則偷走最舊的。
- `envelope()`：線性 ADSR。

Event loop（PlayHead segment 內 sample 精度的 note 起點、loop 跳躍時的 release）與 PolyBLEP oscillator 已提供。Self-check 要求：beat 2 上的 note 的第一個非零 sample 在 24 000 或 24 001、A4 的 440 Hz 比 466 Hz 至少高 30 dB、和弦（triad）期間有三個 voice、release 之後靜音、block size 512 與 100 的輸出相同，以及同一個 note 在 loop 的兩次 pass 中相同。問題：為什麼跳躍（seek、loop）必須 release 所有 voice？對於在 seek 點之前就開始的 note，一個會「chase」的 DAW 會怎麼處理？

## Exercise 11 - 從 File menu 儲存與開啟 project ★★☆（實作）

[`l14_ex11_starter`，檔案 `exercises/starter/ex11_project_file.cpp`]

Exercise 4 把 *engine* 存成 JSON。現在要儲存*應用程式*的 project，並讓它成為真正的 menu command：

- `ProjectFile::stripToTree()` / `stripFromTree()`：每個 strip 一個 `juce::ValueTree`（名稱、fader、pan、pan law、track format 與 pan mode、mute、solo、send、帶有所有以名稱表示的參數的 insert，或一個 VST3 檔案 + base64 state、clip）。Media 的寫出（每個不同的 buffer 一個 32-bit float WAV）與檔案的 framing 已提供。
- `ProjectCommands`：作為額外的 `ApplicationCommandTarget`（`MainComponent::setExtraCommandTarget`）的 command 的 "Save project..."（Ctrl+S）與 "Open project..."（Ctrl+Shift+O），透過 `MainComponent::onExtendMenu` 加進 File menu——不修改 app。

Self-check 在 File menu 中找到這兩個項目，並在 key mapping 中找到 Ctrl+S，儲存一個修改過的 demo song，在全新的 `MainComponent` 中開啟它，要求 4 bar 的 render **逐 bit 相同**，且再次儲存必須得到**完全相同的 XML 文字**。為什麼用 32-bit float WAV，以及為什麼 float 用 9 位有效數字？

## Exercise 12 - 第三個 FX bus ★★☆（實作 + 概念）

[`l14_ex12_starter`，檔案 `exercises/starter/ex12_third_bus.cpp`，連結 `l14_daw_app_3bus`]

Bus 的數量是 compile-time 常數 `engine::kNumBuses`（`-DDAWLAB_ENGINE_NUM_BUSES=3` 會建出 app 函式庫的第二份副本，見 `app/CMakeLists.txt`）。

1. （概念）列出 engine 與 GUI 中每一個大小跟隨 `kNumBuses` 的地方（提示：`ChannelStrip`、`Mixer`、`Engine::allChannels`、`Session`、`MixerView`、`DemoSong`）。為什麼 compile-time 常數對一個不得在 audio thread 上 allocate 的 engine 是合理的選擇？Run-time 的 bus 數量會需要什麼？
2. （實作）`configureThirdBus()`：把 bus C 做成 "Doubler"——100 % wet 的 Chorus 加上一個 low-cut EQ——由 Pad 與 Lead 透過 -6 dB 的 **pre-fader** send 餵入。

Self-check 計算 12 個 strip 與每個 track 3 個 send 旋鈕，移動 C 旋鈕並讀取 engine，mute 除了 doubler 路徑以外的一切，把 Pad 與 Lead 的 fader 拉到 $`-\infty`$ 卻仍聽得到 bus C（pre-fader！），然後關閉 send 並要求完全靜音。

## Exercise 13 - 鍵盤快捷鍵 map ★☆☆（實作）

[`l14_ex13_starter`，檔案 `exercises/starter/ex13_shortcut_map.cpp`]

- `findConflicts()`：每一對共用同一個按鍵的 command。
- `ShortcutCommands`：按鍵 1-8 選取 track 1-8；Ctrl+Left / Ctrl+Right 把 playhead 移到上一個 / 下一個 beat *線*（不是「從這裡起算一個 beat」）。
- `saveShortcuts()` / `loadShortcuts()`：只儲存使用者的變更（`KeyPressMappingSet::createXml(true)`）並還原它們。

Self-check 要求 28 個 command 之間沒有衝突、偵測到一個刻意製造的衝突、按下新的按鍵、將 Play/Stop 從 Space 重新對應到 P、儲存、把檔案還原到一個全新的 app 中，並檢查 P 會播放而 Space 什麼都不做。它也會把同一組 mapping set 的 JUCE `KeyMappingEditorComponent` render 到 `dawlab_output/l14_ex13_editor.png`。問題：為什麼 DawLab 8 的旋鈕與按鈕拒絕鍵盤 focus？

## Exercise 14 - 手寫 mono 與 stereo track ★★☆（實作）

[`l14_ex14_starter`，檔案 `exercises/starter/ex14_track_format.cpp`；只用 engine，不用 JUCE]

把 track 中與 format 有關的部分（Step 4 的「Mono 與 stereo track」；Step 5 的「Mono track 上的 insert」）寫成兩個獨立函式，不需要外圍的 engine：

- `trackPanGains(format, pan, left, right)`：Mono -> constant-power pan law，$`p_L = \cos\theta`$、$`p_R = \sin\theta`$，其中 $`\theta = \frac{\pi}{4}(p+1)`$；Stereo -> balance，$`p_L = \min(1, 1-p)`$、$`p_R = \min(1, 1+p)`$。
- `processTrackBlock(block, format, insert, gainDb, pan)`，就地處理一個 stereo block：Mono -> fold 成 $`(L+R)/2`$，只在 `block.getChannelSubset(0, 1)` 上執行 `insert`（可能為 null），再把 channel 0 複製到 channel 1；Stereo -> 在兩個 channel 上執行 `insert`。接著把每一側乘上 $`10^{\text{dB}/20}`$ 再乘上它的 pan gain。

Self-check 會把你的 gain 與 `ChannelStrip::computeStripGains()` 比較；把一個 mono 檔案（L = R = 0.5）放到兩種 format 上，要求中央相差 **3.01 dB**；要求 stereo source（0.6 | 0.2）在 balance 下保留 L != R（+0.5 時為 0.3 | 0.2），在 mono track 上 fold 成 0.4 x 0.7071；以 Stereo Spread 開到最大的 chorus 執行，要求 mono track 上 L = R，而 stereo track 上輸出是寬廣的；最後把你的結果與真正的 engine 對兩種 format render 出的結果比較。執行之前先預測：同一個 mono 檔案在哪一種 format 上中央比較大聲，大聲多少？為什麼？
