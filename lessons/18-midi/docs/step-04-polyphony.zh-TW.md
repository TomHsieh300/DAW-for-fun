# Step 4 - 複音

[English](step-04-polyphony.md) | **繁體中文**

[總覽](../README.zh-TW.md) | 上一步：[Step 3 - 一個 voice](step-03-one-voice.zh-TW.md) | 下一步：Step 5 - 鍵盤（即將推出）

## 目標

彈和弦：固定數量的 voice、一個決定哪個 voice 彈哪個音符的分配器——以及沒有 voice 可用時該怎麼辦——再加上延音踏板、pitch bend 和「all notes off」。

## 設計

### 固定數量的 voice

每個 voice 在發聲期間都要花 CPU，而 audio thread 不能配置記憶體，所以 synth 有一個固定的池子：8、16、64 個 voice。音符來來去去；voice 一直都在。哪個 voice 彈哪個音符是 *voice 分配器*的決定，它有三種情況：

```text
1. 這個音高已經在某個 voice 上響著    在那個 voice 上再彈一次
2. 有閒置的 voice                     用那一個
3. 沒有閒置的                         搶一個
```

**同一個音再彈一次。** 鋼琴的琴鍵在弦還在響時再敲一次，敲的是同一根弦；它不會多出第二根。同一個音高用兩個 voice，聽起來會像一個太大聲的音，而且因為相位不同，還會有緩慢的相位抹糊。所以仍在發聲的音符——按著、被延音、或正在 release——會在它自己的 voice 上再彈一次。Step 3 的 `Voice::start()` 本來就會保留發聲中 voice 的相位，所以這是無縫的。

**搶 voice（stealing）。** 所有 voice 都忙時，新的音符會拿走一個。拿哪一個？最不會被想念的那一個：

1. 已經放開的音符（鍵已放開、踏板沒有延住、正在淡出）——放開最久的那個，因為它已經淡得最多；
2. 如果每個音符都還按著，就拿最早開始的那個：和弦中最舊的音衰減得最多，旋律也已經離開它了。

其他 synth 有其他選擇——最小聲的 voice、絕不搶最低音——但記帳方式相同：每個 voice 有一個 slot，記著鍵的狀態和兩個時間戳記 `started` 與 `released`，來自一個只會往上數的計數器。

Note off 依音高找到它的 voice。因此被搶走的音符的 note off 找不到 voice：那個 voice 現在是別的音高，絕不能結束新的音符。

### 搶 voice 不能爆音

一個 voice 在波形中途從一個音跳到另一個音會爆音。所以 `Voice::steal()` 會在 2 ms（48 kHz 下 96 個 sample）內把舊音淡出，再在下一個 sample 從靜止狀態開始新音。新音晚了 2 ms；沒有人聽得出 2 ms，但每個人都聽得出爆音。在這 2 ms 內到達的 note off 會取消新音，「all notes off」也會。

### 延音踏板與 pitch bend

延音踏板是 controller 64：64 以上是踩下，以下是放開。踩著時，note off 不會 release 音符，而是把它標成*被延音*。踏板放開時，每個被延音的音符都會 release——但仍按著鍵的音符繼續發聲。在踏板下再敲一次的鍵，會在同一個 voice 上再彈它的音，現在由鍵而不是踏板按住它。

Pitch bend 以 Step 1 的 14 位元值送來，-8192 到 8191。這台 synth 在兩端彎 +-2 個半音，`value / 8192 x 2`，所以完全往下剛好是兩個半音，完全往上則差一點點。每個發聲中的 voice 立刻跟上，新的音符也從彎過的音高開始。

Controller 123，「all notes off」，會 release 每個 voice 並忘掉踏板：Step 2 那些卡住的音的 panic 鍵，以訊息的形式。

### 一如往常，sample 精確

`Synth::render()` 清空 buffer，讓每個 voice 把自己加上去，並和 Step 1 的 `renderBlock()` 一樣在每個事件處切開。自我檢查在 6 個 voice 上播放 800 個隨機事件——音符、velocity 0 的 note off、踏板、bend、panic——長 26 秒，用 1、64、512 和隨機大小的 block，要求輸出逐位元相同、同一個音高絕不由兩個按著的 voice 發聲，而且最後歸於無聲。

## 換你了

在 `steps/step04-polyphony/starter/step04.cpp` 寫這三個函式：

1. `Synth::allocate()`：同一個音高、閒置的 voice、最早放開的已放開 voice、最舊的音符。
2. `Synth::handle()`：note on（start 或 steal）、note off（release 或延音）、踏板、all notes off、pitch bend。
3. `Synth::render()`：清空，然後每個 voice 算到每個事件為止，處理它，繼續。

完成後，輸出的結尾會像這樣：

```text
Step 4: polyphony
  stealing a voice of a 4-note chord: the largest step around it is 0.650, 1.075 before it
  a random performance of 800 events on 6 voices, 25.9 s: the same in blocks of 1, 64, 512 and random: yes
[PASS] l18_step04 (53 checks passed)
```

## 你學到了什麼

- Synth 有固定的 voice 池；分配器把音符對應到 voice 上。
- 再彈一次的音符回到它自己的 voice。
- 搶最不會被想念的 voice：先已放開的、再按著的，最舊的優先。
- 被搶的 voice 先在 2 ms 內淡出再彈新音：晚一點，但不爆音。
- Note off 依音高找 voice，所以被搶走的音符的 note off 不會結束別的音符。
- 延音踏板把 note off 變成「被延音」；放開踏板會讓它們離開，但仍按著的鍵不會。
