# Step 3 - 一個 voice

[English](step-03-one-voice.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 2 - 播放 MIDI clip](step-02-midi-clips.zh-TW.md) | 下一步：Step 4 - 複音（即將推出）

## 目標

讓音符發出聲音：一個不會 aliasing 的鋸齒波 oscillator、一個 filter，以及一個絕不會爆音的 envelope——在 audio thread 上算，不配置記憶體，不論 block 多大結果都一樣。

## 設計

### 減法合成

經典的類比 synthesizer，至今仍是每個人做的第一台，是串成一列的三個部分：

```text
oscillator  ->  filter  ->  amplifier
（所有泛音）    （削掉高頻）  （envelope：隨時間變化的音量）
```

鋸齒波包含每一個泛音，第 `k` 個泛音是基音的 `1/k`：明亮、帶嗡聲，很適合讓 filter 去塑形。這裡的 filter 是第 02 章的 `StateVariableFilter`，當作 lowpass 使用；新的部分是 oscillator 和 envelope。兩者各有一種會被聽出來的失敗方式。

### 從音符編號到頻率

十二平均律：一個八度 12 個音，每個音相差 2^(1/12) 倍，以 A4 = MIDI 音符 69 = 440 Hz 為基準：

```text
f = 440 x 2^((note - 69 + bend) / 12)
```

`bend` 以半音為單位，也可以是小數。Step 4 會用 Step 1 的 pitch bend 滾輪來餵它。

### Aliasing 與 PolyBLEP

最直接的鋸齒波是 `2 x phase - 1`，phase 每個 sample 增加 `f / sampleRate`，到 1 就繞回。它從 +1 跳到 -1 的那一下無限陡峭，而無限陡峭的跳躍有永無止盡的泛音。數位訊號裝不下高於取樣率一半的東西：2489 Hz 音符的第 10 個泛音在 24.9 kHz，它會*折回*到 48 - 24.9 = 23.1 kHz。第 30 個泛音在 74.7 kHz，折回到 21.3 kHz，依此類推——而且因為折疊會來回反彈，有些會落到很低的幾 kHz。這些 *alias* 不屬於任何泛音列。它們聽起來像第二個、錯的音，而且 pitch bend 時會往反方向移動。

頻寬受限的鋸齒波在 Nyquist 之上沒有泛音。用正弦波疊出來是精確的，但每個泛音要一個正弦。*PolyBLEP*（polynomial band-limited step）是每個軟體 synth 都在用的便宜近似：天真的跳躍和平滑跳躍之間的差，是一個只在跳躍旁那幾個 sample 才重要的小殘差，所以只在那裡減掉它的多項式近似——每個週期兩個 sample，其他地方什麼都不做：

```text
t < dt:       x = t / dt          residual = 2x - x^2 - 1     （剛跳過之後）
t > 1 - dt:   x = (t - 1) / dt    residual = x^2 + 2x + 1     （正要跳之前）
saw = 2 x phase - 1 - residual
```

自我檢查會量出這換來了什麼。它分析每個 alias 相對於基音的音量：

<div align="center">

| | 天真的 | PolyBLEP |
|---|---|---|
| A4（440 Hz），最大的 alias | 低 40.6 dB | 低 71.3 dB |
| D#7（2489 Hz），10 kHz 以下的 alias | 低 24.1 dB | 低 52.3 dB |
| D#7（2489 Hz），剛好低於 24 kHz 的 alias | 低 20.0 dB | 低 28.4 dB |

</div>

PolyBLEP 在重要的地方非常好——折回到很低、會被聽成錯音的那些 alias——而在剛好低於 Nyquist 的地方只是普通，那些 alias 在 20 kHz 以上，oscillator 後面的 lowpass filter 反正也會把它們去掉。更高階的 BLEP 或 oversampling（第 06 章）在頂端做得更好，代價是更多 CPU。

### 絕不跳躍的 envelope

Amplifier 依循 ADSR envelope：*attack* 時上升，*decay* 時降到 *sustain* 的音量，按住鍵時維持，note off 之後在 *release* 中降到無聲。音量的跳躍會爆音——跳躍就是一個寬頻的脈衝——所以 envelope 有三條規則：

- **沒有任何階段短於 1 ms。** 0 ms 的 attack 就是跳躍。1 ms 在 48 kHz 是 48 個 sample，短到聽起來是瞬間，又長到不會爆音。
- **音符從目前的音量開始。** 在 release 中再彈一次的音符不會先降到 0：attack 從它碰到的音量繼續往上。
- **Release 從目前的音量開始，並花上 release 時間。** 在 attack 一半時放開的音符就從那裡 release。斜率是 `level / releaseSamples`，所以不論從什麼音量開始，每次 release 都持續相同的時間。

各階段都是線性的，這讓檢查可以精確：attack 一半時是 0.5、480 個 sample 後是 1.0，從 0.5 開始的 release 花 9600 個 sample。真正的 synth 常用指數曲線，對 decay 聽起來更自然；規則不變。

自我檢查也會在所有時間都設為 0 的情況下隨機觸發 note on 和 off，並要求任兩個 sample 之間的變化都不超過 1/48——一個 1 ms 斜坡的量。

### Voice

`Voice` 把它們組合起來：

```text
out[i] += filter(saw.next()) x envelope.next() x velocityGain(velocity)
```

- 它是*加*到 buffer 上，因為 Step 4 會把很多 voice 混成一個。
- 閒置的 voice 什麼都不加，也不花任何成本：envelope 閒置時迴圈就停止。
- `velocityGain` 是平方律 `(v / 127)^2`：velocity 64 大約是 127 的四分之一（-12 dB），大多數 synth 都是這種手感。
- 在無聲時開始的 voice 會把 saw 重設到 phase 0、filter 重設到靜止狀態，所以每個音符的開頭都一樣。在*仍有聲音時*再被彈一次的 voice 則兩者都保留：重設 phase 會讓波形在聲音中間跳一下——爆音。

這一切都是每個 sample 單純的算術，沒有跟 block 有關的狀態，所以檢查會用 1、7、64、512 和 1000 個 sample 的 block 算同一個音符，並要求輸出完全相同。

## 換你了

在 `steps/step03-one-voice/starter/step03.cpp` 寫這九個函式：

1. `noteFrequency()`：公式，包含 bend。
2. `polyBlep()`：兩個多項式，其他地方為 0。
3. `Saw::next()`：天真的值減去殘差，然後更新 phase。
4. `Adsr::noteOn()`：attack，從目前的音量開始。
5. `Adsr::noteOff()`：release 和它的斜率，除非已閒置。
6. `Adsr::next()`：目前階段的一步，在目標處截住。
7. `Voice::start()`：只重設無聲的 voice，然後 pitch、gain、頻率、attack。
8. `Voice::release()`：envelope 的 release。
9. `Voice::render()`：作用中時加上 filter x envelope x gain。

完成後，輸出的結尾會像這樣：

```text
Step 3: one voice
  A4: worst alias 40.6 dB down naive, 71.3 dB with PolyBLEP
  D#7: aliases below 10 kHz 24.1 dB down naive, 52.3 dB with PolyBLEP (just below 24 kHz: 20.0, 28.4)
  A3 at velocity 127 is 3.94 times as loud as at 64 (11.9 dB)
[PASS] l18_step03 (43 checks passed)
```

## 你學到了什麼

- 音符編號就是頻率：440 x 2^((n - 69) / 12)。
- 天真的鋸齒波會 aliasing；PolyBLEP 修正每次跳躍旁的兩個 sample，去掉大部分聽得到的部分。
- 量測 oscillator 的 alias；不要用波形判斷它。
- Envelope 絕不跳躍：沒有短於 1 ms 的階段，每個階段都從目前的音量開始。
- Voice 加進混音、閒置時不花成本，再被彈一次時保留它的 phase。
