# Chapter 14 - 解答

[English](README.md) | **繁體中文**

[回到本章](../README.zh-TW.md) | [練習題](../exercises/README.zh-TW.md)

用 `.\scripts\build.ps1 -Only 14-daw-8track -TestLabel solution` 執行所有解答程式（exercise 8-13 要加上 `-Juce`）。每一個都以 `[PASS] l14_exN (... checks passed)` 結束。

---

## Exercise 1 - 誰可以碰什麼？

1. **Mutex。** 當 UI thread 持有 lock 時——而加入 effect 意味著 allocate，這本身就可能取得 allocator 的 lock，或向 OS 要求 page in 記憶體——audio thread 會 block 在 `processBlock()` 中。UI thread 以一般優先權執行；在它仍持有 lock 的情況下，OS 可能隨時 preempt 它，持續任意長的時間。於是高優先權的 audio thread 在等待一個低優先權的 thread：**priority inversion**。如果等待超過 block 剩餘的時間預算（48 kHz 下 512 個 sample 為 10.67 ms，減去實際的處理時間），device 就會 underrun，你會聽到 click 或 dropout。它「在他的機器上能運作」是因為這個 race 很少發生——這讓情況更糟，而不是更好：它會在忙碌的系統上、在現場演出時失敗。

2. **為什麼需要 garbage queue。** Audio thread 在兩個 block 之間 unlink effect，但它自己不能呼叫 `delete`：destructor 會釋放記憶體（allocator、可能的 lock、可能沒有上限的時間——convolution reverb 會釋放好幾 MB）。UI thread 也不能在送出 `RemoveInsert` 之後立刻刪除它，因為它不知道 audio thread *何時*停止使用它：command 可能還在 queue 裡等待，或者 audio thread 可能正在那個 effect 的 `process()` 當中。Garbage queue 是 audio thread 表達「我用完它了」的方式——這是唯一可以安全刪除的時刻。

3. **Garbage queue 滿時 leak。** 另外的選擇是：在 audio thread 上釋放（禁止，見 2.），或等到有空間（禁止：等待）。Leak 一個物件是一個小的、有上限的、*有計數*的錯誤（`getGarbageLeaked()`）；dropout 則是聽得見的錯誤。它在實際上不會發生，因為每個 command 至多產生一張 garbage ticket，且兩個 queue 的容量相同（1024）：只有在套用了超過 1024 個結構性 command 卻一次 `collectGarbage()` 都沒呼叫時，garbage queue 才可能被填滿——以 30 Hz 的 UI timer 而言，那是每秒超過 30 000 次編輯。

4. **數值 vs 結構。** Fader 或參數值是一個數字：把它寫進 `std::atomic` 一次 store 就是完整且一致的，而且如果 UI 在兩個 block 之間寫了十個值，只有最新的有意義。加入 effect 需要記憶體（在 UI thread 上 allocate）、一次 `prepare()` 呼叫（會 allocate，可能計算 FFT），以及 chain 中好幾個必須讓 audio thread 一起看到的 pointer 移動——所以它必須是一個由 audio thread 在 block 邊界套用的訊息。

5. **被 mute 的 track 與 pre-fader send。** 你什麼都聽不到——既沒有 dry，也沒有經過 reverb。在這個 engine 中，mute/solo 是在 send 取樣點*之前*套用的（見 Step 6）。使用者期待「mute」讓 track 完全無聲；被 mute 的 track 還有 reverb tail 會令人意外，而且 solo 另一個 track 時，被 mute 的 track 的 reverb 也會漏出來。（有些 console 為了 monitor mix，會在 mute 之前取樣 pre-fader send；DAW 可以把它當作一個選項提供。）

## Exercise 2 - 手算 timing 與 latency

**(a)** $N_{\text{beat}} = 60 f_s / B = 60 \cdot 44100 / 96 = 27\,562.5$ 個 sample；
$N_{\text{bar}} = 3 \cdot 27\,562.5 = 82\,687.5$ 個 sample。
Bar 5、beat 2：$(5-1)\cdot 3 + (2-1) = 13$ 個 beat $= 13 \cdot 27\,562.5 = 358\,312.5$ 個 sample，四捨五入（half away from zero，如 `std::llround`）得 **358 313**，即 $13 \cdot 0.625 = 8.125$ s。
Sample 1 000 000：$1\,000\,000 / 27\,562.5 = 36.2812$ 個 beat；36 個整 beat = 12 個完整的 bar，所以是 bar **13**，beat $36 \bmod 3 + 1 = $ **1**，tick $\lfloor 0.2812 \cdot 960 \rfloor = $ **269**。

**(b)** 四分音符持續 $60\,000 / 96 = 625$ ms；附點八分音符是它的 $0.75$：**468.75 ms** = $0.46875 \cdot 44100 = $ **20 671.875 個 sample**（一個小數 delay——chapter 09 的 `FractionalDelayLine` 處理 .875）。每四分音符一個週期：$96 / 60 = $ **1.6 Hz**。

**(c)** $\max_t L_t = 1024$，$\max_b L_b = 512$：

| 路徑 | latency $L$ | 補償 |
|---|---|---|
| track 1 | 0 | 1024 - 0 = **1024** |
| track 2 | 64 | **960** |
| track 3 | 240 | **784** |
| track 4 | 1024 | **0** |
| track 5-8 | 0 | 各 **1024** |
| bus A | 0 | 512 - 0 = **512** |
| bus B | 512 | **0** |
| track sum | - | $\max_b L_b$ = **512** |

總 latency $= 1024 + 512 + 240 = $ **1776 個 sample** $= 1776 / 48 = $ **37.0 ms**。

**(d)** Constant power：$\theta = \frac{\pi}{4}(p+1) = \frac{3\pi}{8} = 67.5°$；
$\cos 67.5° = 0.38268$，$\sin 67.5° = 0.92388$；fader $10^{-6/20} = 0.50119$。
$g_L = 0.50119 \cdot 0.38268 = $ **0.19180** $= -6 + 20\log_{10} 0.38268 = -6 - 8.34 = $ **-14.34 dB**；
$g_R = 0.50119 \cdot 0.92388 = $ **0.46304** $= -6 - 0.69 = $ **-6.69 dB**。
Balance 模式：$g_L = 0.50119 \cdot \min(1, 1 - 0.5) = $ **0.25059 (-12.02 dB)**，
$g_R = 0.50119 \cdot 1 = $ **0.50119 (-6.00 dB)**。

**(e)** 每條 delay line $(16384 + 1) \cdot 2 \text{ channels} \cdot 4 \text{ bytes} = $ **131 080 bytes**；12 條 line（8 個 track + 2 個 bus + master + track sum）：**1 572 960 bytes = 1.50 MiB**。小到足以讓固定、寬裕的上限，成為相對於 run time 調整大小的正確取捨。

`solutions/ex2_timing_and_pdc.cpp` [`l14_ex2_solution`] 針對 `Transport`、`DelayEffect::noteToMs`、帶有 `PureDelayEffect` 的真正 `Engine`，以及 `ChannelStrip::computeStripGains` 檢查以上全部。驗證過的輸出：

```
(a) samples per beat 27562.5, per bar 82687.5
    bar 5 beat 2 = sample 358313 = 8.1250 s
    sample 1000000 = 13:1:269
(b) dotted eighth at 96 BPM = 468.750 ms = 20671.875 samples at 44.1 kHz; quarter-note LFO = 1.600 Hz
(c) track compensation: 1024 960 784 0 | bus: 512 0 | track sum: 512 | total 1776 samples = 37.00 ms
(d) pan law : L 0.19180 (-14.34 dB)  R 0.46304 (-6.69 dB)
    balance : L 0.25059 (-12.02 dB)  R 0.50119 (-6.00 dB)
(e) PDC memory: 131080 bytes per delay line, 1572960 bytes in total (1.50 MiB)
[PASS] l14_ex2 (25 checks passed)
```

## Exercise 3 - 音量與 pan automation

解答：`solutions/ex3_automation.cpp` [`l14_ex3_solution`]。

- **`valueAt(t)`**：處理空的 lane 與兩端，然後 `std::upper_bound` 找到第一個時間 $> t$ 的點；以它的前一個點 $(t_k, v_k)$：$v = v_k + (v_{k+1} - v_k)\,\frac{t - t_k}{t_{k+1} - t_k}$（以 `double` 計算——timeline position 是很大的數字）。
- **`render(start, out, n)`**：對 `start` 做一次 `upper_bound`，然後對每個 sample，當 iterator 的時間 $\le t$ 時向前推進它。成本是 $O(\log P + n + P_{\text{block}})$，而不是 $O(n \log P)$，且沒有 allocation——在 audio thread 上是安全的。Self-check 在所有 segment 上以 5000 個 sample 與 `valueAt` 比較。
- **Sample 精度**：effect 自己不計算 sample。Mixer 傳入 block 的 timeline segment（`PlayHead`），所以 gain 是 timeline 的純函數：block size 512 與 37 給出相同的輸出，loop 的第二次 pass 等於第一次（兩個差異都精確為 0）。當 transport 停止時，最後一個值被保持。
- **為什麼用 dB**：從 1 到 0.063 的線性 gain ramp 在中途仍是 $0.53$（-5.5 dB）：fade 似乎什麼都沒做，然後在結尾才驟降。dB 上的 ramp 在感知上是均勻的：中途是 -12 dB。Self-check 印出：

```
Exercise 3: volume automation 0 dB @0.5 s -> -24 dB @1.5 s
  t = 0.25 s  level    0.00 dB
  t = 0.50 s  level    0.00 dB
  t = 0.75 s  level   -6.00 dB
  t = 1.00 s  level  -12.00 dB
  t = 1.25 s  level  -18.00 dB
  t = 1.50 s  level  -24.00 dB
  t = 2.00 s  level  -24.00 dB
  block 512 vs block 37: max |difference| = 0
  loop pass 1 vs pass 2: max |difference| = 0
  pan sweep: max deviation of L^2 + R^2 from 0.25 = 2.98e-08 (constant power)
```

- **Pan**：`PanAutomationEffect` 對每個 sample 使用 `dsp::Panner::computeGains`；整個 sweep 中 $L^2 + R^2$ 維持在 $0.5^2$（constant power）。
- **播放中編輯 lane**：lane 在 engine 內是 immutable 的。建立新的 lane、新的 effect，並用 `Engine::replaceInsert()` 換進去（一個 block 邊界，沒有 lock）。正式產品的設計會把 automation 放在 track 裡（而不是當作 insert），並像 clip 清單一樣透過 command queue 送出新的 lane。
- **Control-rate 的替代做法**：在每個 block 之前從外部設定 strip fader 也「行得通」，但 fader 的 50 ms smoother 此時表現得像 lag。每個 block ramp 都會重新開始，並涵蓋剩餘距離的 $\Delta/T$（$\Delta$ = block 長度，$T$ = 50 ms），而 envelope 移動 $r\Delta$（$r$ = 24 dB/s）；在平衡時，距離 $d$ 滿足 $d\,\Delta/T = r\Delta$，所以 $d = rT = 24 \cdot 0.05 = 1.2$ dB——與 block size 無關。解答用真正的 engine 量測它（每個 512 個 sample 的 block 設定一次 fader）：

  ```
    control-rate fader at 1.00 s: -10.73 dB, i.e. 1.27 dB behind the sample-accurate envelope
  ```

  （小的殘差來自 block 起點值形成的階梯，以及 smoother 是在 gain 上線性 ramp 而非在 dB 上；self-check 接受 1.2 +- 0.2 dB。）若來自 UI timer，結果還會取決於 timer jitter。在 audio thread 內的 sample 精度 automation 既精確又具確定性。

## Exercise 4 - 以 JSON 儲存與載入 project

解答：`solutions/ex4_project_json.cpp` [`l14_ex4_solution`]，輸出 `dawlab_output/l14_ex4_project.json`。

重點：

- **要存什麼**：使用者設定的一切，不存任何推導出來的東西。Latency、補償 delay 與 meter 都會重新計算；audio 存在以 id 引用的檔案中（這裡是 `MediaPool` map；真正的 DAW 則用相對於 project 資料夾的路徑）。Clip 的 media id 是透過在 pool 中反查它的 `shared_ptr` 找到的。
- **用名稱，不用 index**：effect 用 `getName()`，參數用 `ParameterInfo::name`。多了一個參數的較新 adapter 仍能載入舊的 project（缺少的參數保持預設值，未知的名稱被跳過）。`"version"` 欄位讓未來的 loader 可以轉換舊檔案。
- **精確的 float**：`%.9g` 印出足夠的有效位數，使任何 IEEE-754 `float` 讀回來都是同一個值（`float` 至多需要 9 位）。這就是為什麼 self-check 可以同時要求 save -> load -> save 之後有完全相同的 JSON 文字，以及逐 bit 相同的重新 render（`max |difference| = 0`）。
- **載入只使用 public API**：`createEffect(name)`、`setParameterByName()`、`setBypassed()`、`Engine::addInsert()`、`Engine::setTrackClips()`、strip 的 setter。所以載入進一個已經在播放的 engine 時運作方式相同（command 經由 queue）。
- **錯誤是訊息，不是 crash**：未知的 effect 類型 -> `unknown effect type 'Compresor'`，缺少的 media -> `missing media 'arp'`，語法錯誤則由 parser 連同位置回報。

驗證過的輸出（節錄）：

```
Exercise 4: project saved (5717 bytes) to dawlab_output/l14_ex4_project.json
{
  "format": "dawlab-project", "version": 1,
  "sampleRate": 48000,
  "transport": {"tempo": 124, "numerator": 4, "denominator": 4, "loopEnabled": false, "loopStart": 0, "loopEnd": 96000},
  "pdc": true,
  "strips": [
    {
      "kind": "track", "index": 0, "name": "Drums \"live\"",
      ...
  re-render of the loaded project: max |difference| = 0 (peak -2.94 dBFS)
  misspelled effect -> error: unknown effect type 'Compresor'
  missing media     -> error: missing media 'arp'
[PASS] l14_ex4 (18 checks passed)
```

值得嘗試的改進：以標籤（"Peak"）而不是數字儲存 choice 參數；先把檔案寫到暫存檔名再 rename（被中斷的儲存永遠不會毀掉舊的 project）；把 convolution IR 的檔名保存在 effect state 中。

## Exercise 5 - 移動 clip 與 crossfade

解答：`solutions/ex5_clip_crossfade.cpp` [`l14_ex5_solution`]。

- **Snap**：$\text{round}(x / G) \cdot G$，其中 $G = N_{\text{beat}} \cdot \text{gridBeats}$（120 BPM 下的 1/16：$G = 6000$）：25 000 -> 24 000，27 001 -> 30 000。在 `double` 中做 snap，只轉換一次。
- **移動**：複製 clip、erase 它、clamp/snap start，依 start 在 `std::upper_bound` 處插入（start 相同時是 stable 的）。Engine 不需要排序過的 clip，但 arranger view 與 project 檔案需要。用 `setTrackClips()` 送出新清單。
- **Crossfade**：`b.start = a.end() - F`，`a.fadeOutSamples = b.fadeInSamples = F`，兩者使用相同的 shape。由於 Clip.h 對 fade 的定義，在重疊 index $k$ 處的兩個 fade 引數是 $\frac{F-k}{F+1}$ 與 $\frac{k+1}{F+1}$——它們在每個 sample 都加總為 1。

預測與量測（驗證過的輸出）：

```
  Linear     same audio  : level at the middle +0.00 dB, max deviation in the overlap 0.00 dB
  Linear     noise A->B : steady -16.77 dBFS, middle of the overlap -19.79 dBFS (-3.03 dB)
  EqualPower same audio  : level at the middle +3.01 dB, max deviation in the overlap 3.01 dB
  EqualPower noise A->B : steady -16.77 dBFS, middle of the overlap -16.84 dBFS (-0.07 dB)
```

| | 相同（相關）的 audio | 獨立的 audio |
|---|---|---|
| Linear（$g_a + g_b = 1$） | 振幅相加：$0.5 + 0.5 = 1$ -> **0 dB**，完美 | power 相加：$0.5^2 + 0.5^2 = 0.5$ -> **-3 dB** 的凹洞 |
| EqualPower（$g_a^2 + g_b^2 = 1$） | $2 \sin(\pi/4) = 1.414$ -> **+3 dB** 的凸起 | power 恆定 -> **0 dB**，完美 |

所以：Linear 用於把 clip 與它自己（的一部分）crossfade，或與同一來源、時間對齊的另一個 take crossfade（comping）；EqualPower 用於不相關的素材。許多 DAW 兩者都提供——現在你知道為什麼了。

## Exercise 6 - 設計每個 track 的 input recording

一個適合這個 engine 的設計：

1. **Input 進入 engine。** `processBlock()` 也會得到一個 input block（device callback 有一個）。每個 record-armed 的 track 把它的 input channel 複製進一個**預先配置的 lock-free FIFO**（例如 10 秒 audio 的 `SpscQueue<float>`，或固定大小 chunk 的 ring）——絕不複製進會增長的 `std::vector`。Audio thread 的工作只是每個 block 做一次 `memcpy` 等級的 push。
2. **Disk thread。** 一個獨立的 "disk writer" thread（一般優先權）從 FIFO pop，並附加到 WAV 檔以及一個在*它的* thread 上增長的記憶體 buffer。錄音停止時，該 thread 建出一個 immutable 的 `AudioBuffer`，用 `shared_ptr` 包起來，UI 建立 clip 並以 `setTrackClips()` 送出新的 clip 清單。
3. **擺放位置。** 音樂人在 timeline 位置 $T$ 聽到的東西，是在 engine render $T$ 之後 $L_{\text{pdc}} + L_{\text{out}}$ 個 sample 離開喇叭的；他們的演奏再過 $L_{\text{in}}$ 才抵達 engine。所以在 engine render 位置 $P$ 時抵達的 input sample，屬於 timeline 位置
   $$T = P - (L_{\text{in}} + L_{\text{out}} + L_{\text{pdc}})$$
   也就是說，錄下的 clip 必須比錄音開始時的 playhead 位置*早* $L_{\text{in}} + L_{\text{out}} + L_{\text{pdc}}$ 個 sample 擺放（JUCE 透過 `AudioIODevice::getInputLatencyInSamples()` / `getOutputLatencyInSamples()` 回報 device 的 latency）。用 loop-back 線驗證：錄下 metronome，檢查 click 是否落在 grid 上。
4. **磁碟太慢。** FIFO 吸收短暫的停頓（這就是它那 10 秒的用途）。如果它仍然滿了，audio thread 不能等待：它丟棄該 input block，設定一個 atomic 的 "overrun" flag/計數器，由 UI 告訴使用者（"disk too slow - recording damaged at 1:23"）。播放永遠不會因為錄音而出現 glitch。
5. **Monitoring 與 PDC。** 透過 track 的 insert 做 monitoring，會讓音樂人聽到*自己*的聲音加上完整的 PDC delay——Exercise 2 的 project 中是 37 ms，無法演奏。選項：直接 monitor dry input（零 latency 的 "direct monitoring"），或在錄音時讓 engine 進入低 latency 模式（bypass 或停用有 latency 的 plugin，對被 monitor 的路徑使用 `setLatencyCompensationEnabled(false)`）。

## Exercise 7 - Tempo 變化與 time-stretching

1. **Musical clip** 以 **beat**（或 tick）儲存位置與長度，並儲存 audio 的 "source tempo"；**absolute clip** 則儲存 sample。Drum loop、arpeggio，任何鎖定在 grid 上的東西都必須是 musical 的；旁白、與影片同步的音效，或 timing 自由的現場錄音則應維持 absolute。Tempo 改變時，engine 重新計算 musical clip 的 sample 位置（start = beatsToSamples(startBeats)）以及所需的 stretch ratio $r = \frac{\text{sourceTempo}}{\text{projectTempo}}$（duration factor）。
2. **Resampling** 同時改變長度*與*音高（$r = 1.2$ 也會移調 $12 \log_2 1.2 = 3.2$ 個半音）——對 "vari-speed" 效果沒問題，對必須維持在同一調的音樂則是錯的。**Time-stretching** 只改變長度，作法是把訊號切成短而重疊的 grain 並重新安排它們的間距：**time-domain** 方法（OLA / **WSOLA**：overlap-add，grain 位置以 cross-correlation 挑選使波形吻合）——適合語音與單音素材，artefact：transient 上的重複 / 結巴、"warble"；**frequency-domain** 方法（**phase vocoder**：以 chapter 11 的 FFT 做 STFT，依新的 hop size 對每個 bin 推進 phase）——適合複音素材，artefact："phasiness" / transient 被塗抹、帶有 reverb 感的聲音。正式的 stretcher 會結合兩者並搭配 transient detection。
3. **在哪裡執行。** 好的 stretch 需要數十 ms 的分析視窗（latency）以及大量 CPU。Real-time safe 的設計：由一個**背景 thread 在 playhead 前方預先 render 被 stretch 過的 audio**，放進 cache（每個 clip、每個 tempo），而 audio thread 只播放 cache 中的 buffer（immutable，像 clip 清單一樣換入）。Tempo 改變時，UI 送出 "new tempo" 訊息；在新的 cache 就緒之前，engine 繼續播放舊的（或以廉價的 real-time stretch 作為 fallback），然後在 block 邊界以短 crossfade 切換。Offline bounce 則直接等待高品質的 render。
4. **Tempo map。** 依時間排序儲存 tempo segment：每次變化一筆 $(t_i^{\text{samples}}, \text{beat}_i, \text{bpm}_i)$。對 timeline sample $t$：對 $t_i \le t$ 的最後一個 segment 做 binary search，然後 $\text{beat} = \text{beat}_i + (t - t_i) \cdot \text{bpm}_i / (60 f_s)$（segment 內 tempo 恆定；ramp 則改為對 tempo 積分）。反向運算是在 `beat` 欄上做同樣的搜尋。排序過的 `std::vector` + `std::upper_bound` 是 $O(\log n)$，而且因為 playhead 向前移動，在 block 之間快取一個 cursor 可以讓它變成 $O(1)$——與 Exercise 3 的 `render()` 是同樣的技巧。

---

# 選修專案（Exercise 8-13）：應用程式（JUCE）

六個解答都是連結 app 函式庫的 JUCE console 程式；它們在 offline 模式下建出真正的 `MainComponent`（沒有 device、沒有視窗），並以數字檢查一切。以下輸出來自作者的 Debug build。

## Exercise 8 - GUI 中的 automation lane

解答：[`solutions/ex8_automation_lane.cpp`](ex8_automation_lane.cpp)（`l14_ex8_solution`）。

- **對應。** $y(v) = (1 - p)\,h$，其中 $p = (v - v_{\min}) / (v_{\max} - v_{\min})$，其反函數為 $v(y) = v_{\max} - (y/h)(v_{\max} - v_{\min})$。X 軸*不是* lane 自己的：lane 位於 arrangement 之下，並使用 `ArrangementView::xToSample()` / `sampleToX()` / `snapToBeat()`，所以畫在 bar 線上的點，在 audio 中也就在那條 bar 線上。
- **手勢。** 點擊首先做 hit-test（到每個點中心的距離至多 6 px）；命中只會選取。否則新點會在 `std::upper_bound` 處插入——清單保持排序，因此 `valueAt()` 可以對它做 binary search。拖曳時把時間 clamp 在 $[t_{k-1}, t_{k+1}]$ 之間：拖曳中重新排序會讓被選取的 index 在使用者的滑鼠底下跳到另一個點，使「移動」變成「推擠其他點」。每次編輯都 publish 一份新的 immutable `AutomationLaneData`。
- **Audio。** `valueAt()` 在 breakpoint 之間*以 dB* 做 linear interpolation，所以在 0 與 -24 dB 的中途，gain 剛好是 $10^{-12/20}$。Effect 以 `segment.timelineStart + i` 逐 sample 求值，所以 loop 與 seek 不需要特殊程式碼。
- **交接。** UI 把新的 lane exchange 進 `pending_`，並刪除換回來的東西（audio thread 從未見過它）。Audio thread 把 `pending_` 與 `nullptr` exchange，把它目前的 lane 退役進一個 `SpscQueue`，並保留新的；由 UI 刪除退役的 lane。沒有 lock、audio thread 上沒有 `delete`、也沒有任何 leak。

```
lane 0 dB @0, -24 dB @24000, -24 dB @72000: level at 12000 = -12.00 dB, at 60000 = -24.00 dB
block size 512 vs 37: max difference 0
after removing the middle point: level at 36000 = -12.00 dB
lane objects alive after 200 publishes: 1 (1 = only the one in use)
[PASS] l14_ex8 (18 checks passed)
```

它也會寫出 `dawlab_output/l14_ex8_lane.png`。要在 app 中使用它，把一個 `AutomationLaneComponent` 放在 `ArrangementView` 之下（共用它的寬度），並把一個 `AutomatedGainEffect` 加到 track 的第一個 slot。

## Exercise 9 - 將 audio input 錄進 track

解答：[`solutions/ex9_recording.cpp`](ex9_recording.cpp)（`l14_ex9_solution`）。

- `pushInput()`：`AbstractFifo::prepareToWrite(n, start1, size1, start2, size2)` 回傳至多兩個區域（ring wrap 時才有第二個）；把 `size1` 個 sample 複製到 `start1`，接下來的 `size2` 個複製到 `start2`，然後 `finishedWrite(size1 + size2)`。先檢查 `getFreeSpace() < n`，使決定成為全有或全無：只放一部分 block 會在 block *內部*造成缺口，而丟棄整個 block 至少能讓 take 以整個 block 為單位保持對齊（而 overrun 計數器會告訴使用者）。大小為 $N$ 的 `AbstractFifo` 容納 $N - 1$ 個 sample，所以 12 000 個 sample 的 FIFO 可以放 46 個 256 的 block；剩下的 $282 - 46 = 236$ 次 push 都是 overrun——測試正是檢查這一點。
- `drain()`：`prepareToRead(getNumReady(), ...)`，把兩個區域附加到 take（一個可以增長的 `std::vector`——在 disk thread 上），`finishedRead()`。
- 第一個錄到的 sample 的 **timeline 位置**，是在 audio thread 上、從錄音開始那個 block 的 transport position 擷取的（pending seek 已由 `Transport::getPosition()` 計入）。若去問 UI thread「我們在哪裡？」，會有最多一個 block 加上排程 jitter 的誤差。
- `clipStart = recordStart - (L_in + L_out + L_pdc)` = 96 000 - (64 + 96 + 240) = 95 600（推導見 Exercise 6）。240 個 sample 的 PDC 是 master limiter 5 ms 的 lookahead。

```
recorded 72000 of 72000 samples, 0 overruns, max |take - input| = 0
recording started at 96000, latencies in 64 + out 96 + PDC 240 -> clip at 95600
without a disk thread: 282 blocks pushed, 236 overruns (expected 236)
[PASS] l14_ex9 (9 checks passed)
```

完整的 recorder 會在 take 增長時於 disk thread 上把它寫入 WAV 檔（`juce::AudioFormatWriter`）、在錄音時顯示一個持續增長的 clip，並從 `AudioIODevice::getInputLatencyInSamples()` / `getOutputLatencyInSamples()` 取得 latency。

## Exercise 10 - 帶簡單 synthesizer 的 MIDI track

解答：[`solutions/ex10_midi_synth.cpp`](ex10_midi_synth.cpp)（`l14_ex10_solution`）。

- `notesFromMidi()`：在 `updateMatchedPairs()` 之後，每個 note-on 都知道它的 note-off（`noteOffObject`）。Tick 轉 sample：$s = \operatorname{round}(\text{ticks} / 960 \cdot N_{\text{beat}})$，只在最後四捨五入一次（stage 1，Step 2）。在 120 BPM / 48 kHz 下，beat 2 上的 note 從 24 000 開始。
- `noteOn()`：第一個處於 Off 階段的 voice；若沒有，則偷走 start 最小（最舊）的 voice。被偷走的 voice 從 phase 0 與 level 0 重新開始（真正的 synth 會在幾 ms 內把它淡出以避免 click）。
- `envelope()`：attack 步進 $1 / (t_A f_s)$，decay 步進 $(1 - S) / (t_D f_s)$，release 步進 $\ell / (t_R f_s)$，由 note-off 時的 level $\ell$ 計算，所以 release 永遠持續 $t_R$。
- Sample 精度：`processDsp()` 在每個 PlayHead segment 內 render 到下一個 note 起點，在剛好那個 sample 啟動 note，然後繼續。當 timeline sample 到達 voice 的 note-off 時間時，該 voice 開始 release。
- 跳躍：當某個 segment 沒有延續前一個（seek、loop 重新開始）時，所有 voice 都會 release，並以 binary search 重新定位 note cursor。沒有這一步，橫跨 loop 結尾而被按住的 note 會永遠懸著（它們的 note-off 位於跳躍「之後方」）。*Chasing* 的 DAW 還會啟動那些在跳躍目標之前開始、且在那裡仍在發聲的 note；我們不會（對 drum 簡單而且足夠，對長 pad 則是錯的）。

```
MIDI -> 15 notes; first: note 69 at 24000 for 24000 samples
first non-zero output sample: 24001 (note-on at 24000)
A4: 440 Hz -15.0 dB, 466 Hz -72.1 dB
voices during the triad: 3
block size 512 vs 100: max difference 0
loop of bar 1: 440 Hz in pass 1 0.177, pass 2 0.177
[PASS] l14_ex10 (11 checks passed)
```

第一個非零 sample 是 24 001，不是 24 000：sine 從 phase 0 開始，而 sin(0) = 0。聽聽 `dawlab_output/l14_ex10_synth.wav`（saw wave，300 ms release）。PolyBLEP 在 naive saw / square 的每個不連續處減去一個兩 sample 的多項式修正，移除了 naive saw 會折返到 Nyquist 以下的大部分 aliasing（chapter 06 解釋了 aliasing）。

## Exercise 11 - 從 File menu 儲存與開啟 project

解答：[`solutions/ex11_project_file.cpp`](ex11_project_file.cpp)（`l14_ex11_solution`）。

- **格式。** `<DawLabProject version sampleRate tempo ...>`，帶有 `<Media id file>` child，以及每個 strip（mixer 順序）一個 `<Strip>`，其中包含 `<Send>`、`<Insert>`（帶 `<Param name value>`，或 VST3 的 `file` + base64 `state`）與 `<Clip>` child。參數**以名稱**儲存：effect 的後續版本可能增加或重新排列參數，名稱能撐過這種變動；載入時未知的名稱會被跳過。
- **精確性。** Audio：32-bit float WAV 逐 bit 精確地儲存每個 `float` sample（16 / 24-bit PCM 會量化）。數字：`%.9g`——9 位有效十進位數字足以讓任何 IEEE float 來回轉換不失真（`FLT_DECIMAL_DIG` = 9），並以 `strtof` 解析回來。位置以文字形式存為 `int64`。如此一來，save + open 之後的重新 render 是逐 bit 相同的，再次儲存也得到相同的文字。
- **載入順序。** 先載入 media（如果檔案缺失，目前的 project 不受影響），然後 `newProject()` 並清除每個 slot，再處理各個 strip。以另一個 sample rate 儲存的 project 在載入後以 `Session::handleSampleRateChange()` 轉換——這是 device 變更所執行轉換的同步形式（兩者都結束於 `Session::applySampleRateConversion()`；device 變更會先在 loader thread 上 resample，Step 11）。
- **Menu。** `ProjectCommands` 是一個 `ApplicationCommandTarget`；`setExtraCommandTarget()` 註冊它的 command（讓 Ctrl+S / Ctrl+Shift+O 成為 key mapping）並把它串接在 `MainComponent` 之後；`onExtendMenu` 把這兩個項目放在 File menu 的頂端。這些 command 會開啟 `juce::FileChooser`；測試則直接呼叫 `saveTo()` / `openFrom()`。

```
saved ...\dawlab_output\l14_ex11_project.dawlab (32148 bytes of XML, 28 media files)
re-render after save + open: 414508 samples, max difference 0
saving the opened project gives identical XML
[PASS] l14_ex11 (14 checks passed)
```

（28 個 media 檔：demo song 的 140 個 clip 使用 28 個不同的 buffer。414 508 個 sample = 118 BPM 下的 4 bar 加 0.5 s 的 tail。）

## Exercise 12 - 第三個 FX bus

解答：[`solutions/ex12_third_bus.cpp`](ex12_third_bus.cpp)（`l14_ex12_solution`）。

1. **哪些東西跟隨 `kNumBuses`：** `ChannelStrip`（send level、pre/post flag 與 send smoother 都是 `std::array<..., kNumBuses>`）、`Mixer`（bus strip、bus buffer、每個 block 的 bus block 陣列）、`Engine::allChannels()`（大小為 `kNumTracks + kNumBuses + 1`）、`ChannelRef::bus()` 的有效性、`DemoSong`（bus 名稱、send 表），以及 app 中的 `kNumStrips`（`Session.h`）、每個 strip 的 send 旋鈕與 `MixerView` 中的 bus strip。Compile-time 的大小讓這些全部都能是只 allocate 一次的固定陣列——沒有東西要增長，audio thread 讀取時也沒有東西要重新配置。Run-time 的 bus 數量則需要在 UI thread 上重建所有這些東西，並透過 command 換入（像 clip 清單一樣）：新的 routing 表、每個 strip 新的 send 陣列、舊的那些變成 garbage——可行，但它會一次碰到每個 strip。
2. **`configureThirdBus()`：** 命名，`setBuiltinInsert(bus, 0, Chorus)`，Mix 1 / Depth 6 / Voices 3，`setBuiltinInsert(bus, 1, ParametricEq)`，band 1（low cut）在 200 Hz，然後對 Pad 與 Lead 執行 `setSendLevelDb(2, -6)` 與 `setSendPreFader(2, true)`。

```
mixer strips: 12 (8 tracks + 3 buses + master), send knobs per track: 3
bus C only: master RMS -30.3 dBFS, bus C RMS -32.3 dBFS, Pad strip (fader -inf) peak -100.0 dBFS
sends off: master peak -100.0 dBFS
[PASS] l14_ex12 (13 checks passed)
```

Pad 與 Lead 的 fader 在 $-\infty$ 時，它們自己的 strip 是無聲的（-100 dBFS 是 meter 的下限），然而 bus C 仍帶有約 -32 dBFS RMS：send 是在 fader *之前*取樣。關掉 send 後，master 剛好是 0。`dawlab_output/l14_ex12_third_bus.png` 顯示 12 個 strip 的 mixer。

## Exercise 13 - 鍵盤快捷鍵 map

解答：[`solutions/ex13_shortcut_map.cpp`](ex13_shortcut_map.cpp)（`l14_ex13_solution`）。

- `findConflicts()`：對每一對 $(i < j)$ 以及 command $i$ 的每個按鍵，檢查 command $j$ 是否也有它——對 $n = 28$ 個 command 是 $O(n^2 k)$，瞬間完成。
- Nudge 走到上一個 / 下一個 beat *線*：向前是 $\lfloor b \rfloor + 1$，向後是 $\lceil b \rceil - 1$，其中 $b$ 是以 beat 計的位置（一個極小的 epsilon 避免剛好落在線上的位置被算作「介於兩線之間」）。從 30 000 個 sample（1.25 個 beat）向前是 48 000；從那裡向後兩次是 0。
- `createXml(true)` 只寫出與預設值的差異——P 的一個 `MAPPING` 與 Space 的一個 `UNMAPPING`——所以 app 的後續版本可以更改其他預設值，而不會被使用者的檔案覆蓋。`restoreFromXml()` 先重新套用預設值，再套用差異。
- **為什麼旋鈕與按鈕不要鍵盤 focus：** key mapping 監聽在 `MainComponent` 上。有 focus 的 child 會先拿到按鍵；有 focus 的 `TextButton` 會把 Space 當作對自己的點擊，有 focus 的 slider 會隨方向鍵移動。拒絕 focus 保證 Space 永遠代表 Play / Stop。

```
28 commands, 0 conflicts in the default map
conflict found: 'L' is on both "Play / Stop" and "Loop"
shortcut editor: 32 rows (4 categories + 28 commands)
saved the user's shortcuts to ...\dawlab_output\l14_ex13_shortcuts.xml:
<KEYMAPPINGS basedOnDefaults="1">
  <MAPPING commandId="2101" description="Start or stop playback" key="P"/>
  <UNMAPPING commandId="2101" description="Start or stop playback" key="spacebar"/>
</KEYMAPPINGS>
[PASS] l14_ex13 (14 checks passed)
```

`dawlab_output/l14_ex13_editor.png` 是同一組 mapping set 的 JUCE `KeyMappingEditorComponent`，P 位於 Play / Stop。有兩個細節讓它能在 console 程式中 render：editor 只有在取得 parent 時（`parentHierarchyChanged()`）才填入它的 tree，所以把它放進一個 holder component；而 `TreeView` 透過 message loop 非同步地排版它的 row，這裡 message loop 並未執行——`setRootItem(nullptr)` 接著 `setRootItem(root)` 讓它立即排版。`rows == 32`（4 個類別 + 28 個 command）這項檢查防止產生空白的圖片。
