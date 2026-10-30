# DawLab engine - API 參考與整合指南

[English](engine-api.md) | **繁體中文**

[回到本章總覽](../README.zh-TW.md)

這一頁是寫給任何要為 engine 撰寫 front end 的人 - 特別是第 2 階段的 JUCE app
（`app/`）。一切都是 header-only，位於 `engine/include/dawlab/engine/`，namespace 為
`dawlab::engine`；請 link CMake target `dawlab::engine` 並 include `<dawlab/engine/All.h>`。

## 1. 一張表看懂 threading model

<div align="center">

| API | UI / message thread | audio thread | 備註 |
|---|---|---|---|
| `Engine::prepare()` | 是，device 已停止 | 否 | 會配置記憶體；重新 prepare 既有的 insert |
| `Engine::processBlock()` | 僅限 offline（沒有 device 在執行） | **是** | real-time safe |
| `Transport` setter / getter | 是 | （只有 advance） | atomic |
| `ChannelStrip` 參數 setter / getter、meter | 是 | （在 `beginBlock` 中讀取） | atomic |
| `Track::setFormat()` / `getFormat()` | 是 | （在 `Mixer::process` 中每個 block 讀取一次） | atomic；同時設定 pan mode |
| `InsertEffect::setParameter()`、`get...`、`setBypassed()`、`getMeterDb()` | 是 | 是 | atomic |
| `Engine::addInsert / replaceInsert / removeInsert / moveInsert / setTrackClips` | 是（single producer！） | 否 | command |
| `Engine::getInsert()`、`getNumInserts()`、`Track::getClips()` | 是 | - | 見「observer pointer」 |
| `Engine::collectGarbage()` | 是（同一條 thread） | 否 | ~30 Hz timer |
| `renderOffline()`、`bounceToWav()`、`applyPendingCommandsOffline()` | 是，該 engine 上沒有 device 在執行 | 否 | |
| `InsertEffect::prepare()` | 是（交接之前） | 否 | `Engine::addInsert` 會呼叫它 |

</div>

「Single producer」：所有結構性呼叫（`addInsert`、……、`setTrackClips`、`pushCommand`）一次只能來自
一條 thread - message thread。command queue 是 single-producer 的。

**Observer pointer**：`addInsert()` / `replaceInsert()` / `getInsert()` 會回傳 `InsertEffect*`。
這樣的 pointer 在 effect 被移除（或被取代）**並且**執行 `collectGarbage()` 的 thread 已回收它之前，
都保持有效。所以只在 message thread 上持有它們，並在移除 effect 時放掉它們。

## 2. 公開 API

### Constants.h

```cpp
constexpr int kNumTracks = 8, kNumBuses = 2, kNumChannels = 2, kMaxInserts = 8;
constexpr int kDefaultMaxPdcSamples = 16384, kCommandQueueCapacity = 1024;
constexpr float kMaxFaderDb = 12.0f;   // faders: [kMinusInfinityDb (-100) .. +12] dB
```

### Engine.h

```cpp
class Engine {
public:
    explicit Engine(std::size_t queueCapacity = kCommandQueueCapacity);
    void prepare(double sampleRate, int maxBlockSize, int maxPdcSamples = kDefaultMaxPdcSamples);
    bool isPrepared() const noexcept;
    double getSampleRate() const noexcept;
    int getMaxBlockSize() const noexcept;
    static std::array<ChannelRef, kNumTracks + kNumBuses + 1> allChannels() noexcept;

    Transport& getTransport() noexcept;             const Transport& getTransport() const noexcept;
    Mixer& getMixer() noexcept;                     const Mixer& getMixer() const noexcept;
    int getLatencySamples() const noexcept;         // total mix latency (PDC), report to the host
    bool hasPdcOverflow() const noexcept;            // an insert latency exceeds maxPdcSamples (Step 8)
    void requestReset() noexcept;                   // clear tails/meters at the next block

    // structural changes (message thread)
    bool pushCommand(const Command& c);
    InsertEffect* addInsert(ChannelRef channel, int slot, std::unique_ptr<InsertEffect> effect); // slot -1 = append
    InsertEffect* replaceInsert(ChannelRef channel, int slot, std::unique_ptr<InsertEffect> effect);
    bool removeInsert(ChannelRef channel, int slot);
    bool moveInsert(ChannelRef channel, int fromSlot, int toSlot);
    bool setTrackClips(int track, std::vector<Clip> clips);
    InsertEffect* getInsert(ChannelRef channel, int slot) const noexcept;
    int getNumInserts(ChannelRef channel) const noexcept;
    int collectGarbage();
    void applyPendingCommandsOffline() noexcept;     // offline only

    // statistics (any thread)
    uint64_t getBlocksProcessed() const noexcept;  uint64_t getCommandsApplied() const noexcept;
    uint64_t getCommandsDropped() const noexcept;  uint64_t getGarbageLeaked() const noexcept;
    std::size_t getPendingCommands() const noexcept; std::size_t getPendingGarbage() const noexcept;

    void processBlock(AudioBlock out) noexcept;     // audio thread; any block size; stereo out
};
```

### Transport.h

```cpp
struct TimeSignature { int numerator = 4, denominator = 4; };
struct BarsBeats { int bar = 1, beat = 1, tick = 0; };            // 1-based, 960 ticks per beat
struct TimelineSegment { int64_t timelineStart; int blockOffset; int numSamples; };

class Transport {
public:
    static constexpr int kTicksPerBeat = 960, kMaxSegmentsPerBlock = 8;
    static constexpr double kMinTempo = 20.0, kMaxTempo = 999.0;
    void prepare(double sampleRate) noexcept;       double getSampleRate() const noexcept;
    void setTempo(double bpm) noexcept;             double getTempo() const noexcept;
    void setTimeSignature(int numerator, int denominator) noexcept;  TimeSignature getTimeSignature() const noexcept;
    void play() noexcept; void stop() noexcept; void setPlaying(bool) noexcept; bool isPlaying() const noexcept;
    void setPosition(int64_t samples) noexcept;     void setPositionSeconds(double) noexcept;
    void setPositionBarsBeats(const BarsBeats&) noexcept;
    int64_t getPosition() const noexcept;           double getPositionSeconds() const noexcept;
    BarsBeats getPositionBarsBeats() const noexcept;
    void setLoopRange(int64_t start, int64_t end) noexcept;  void setLoopRangeBars(int firstBar, int numBars) noexcept;
    void setLoopEnabled(bool) noexcept;  bool isLoopEnabled() const noexcept;
    int64_t getLoopStart() const noexcept;  int64_t getLoopEnd() const noexcept;
    double getSamplesPerBeat() const noexcept;  double getSamplesPerBar() const noexcept;
    double beatsToSamples(double) const noexcept;  double samplesToBeats(double) const noexcept;
    double toSeconds(int64_t) const noexcept;  int64_t fromSeconds(double) const noexcept;
    int64_t barsBeatsToSamples(const BarsBeats&) const noexcept;
    BarsBeats samplesToBarsBeats(int64_t) const noexcept;
    int advance(int numSamples, TimelineSegment* segments, int& samplesCovered) noexcept; // audio thread
};
```

### Clip.h / Track.h

```cpp
enum class FadeShape { Linear, EqualPower };
struct Clip {
    std::shared_ptr<const AudioBuffer> source;
    int64_t start = 0, length = 0, sourceOffset = 0;
    float gainDb = 0.0f;
    int fadeInSamples = 0, fadeOutSamples = 0;
    FadeShape fadeShape = FadeShape::Linear;
    bool muted = false;
    std::string name;
    int64_t end() const noexcept;
    static Clip fromBuffer(std::shared_ptr<const AudioBuffer> buffer, int64_t startSample, std::string clipName = {});
};
float fadeGain(FadeShape shape, float t) noexcept;
float clipFadeAt(const Clip& clip, int64_t i) noexcept;
void renderClip(const Clip& clip, int64_t timelineStart, AudioBlock dest) noexcept;

enum class TrackFormat { Mono, Stereo };             // Step 4; default Mono
const char* trackFormatName(TrackFormat) noexcept;  // "Mono" / "Stereo"

class Track : public ChannelStrip {
public:
    void setFormat(TrackFormat) noexcept;            // any thread; Mono -> PanMode::PanLaw, Stereo -> PanMode::Balance
    TrackFormat getFormat() const noexcept;
    int getNumProcessingChannels() const noexcept;   // 1 (Mono) or 2 (Stereo): channels the inserts see
    const std::vector<Clip>* exchangeClips(const std::vector<Clip>* newClips) noexcept; // audio thread (Engine)
    const std::vector<Clip>* getClips() const noexcept;
    int getNumClips() const noexcept;
    int64_t getEndSample() const noexcept;
    void renderClips(AudioBlock dest, const TimelineSegment* segments, int numSegments) noexcept;
};
```

### ChannelStrip.h

```cpp
enum class PanMode { PanLaw, Balance };            // mono tracks: PanLaw; stereo tracks, buses, master: Balance
struct StripStats { float peakDb, rmsDb, integratedLufs; };

class ChannelStrip {
public:
    static constexpr double kFaderRampSeconds = 0.05, kMuteRampSeconds = 0.01;
    void prepare(double sampleRate, int maxBlockSize, int maxPdcSamples = kDefaultMaxPdcSamples);
    void reset() noexcept;
    void setName(std::string);  const std::string& getName() const noexcept;      // UI thread only
    void setGainDb(float) noexcept;  float getGainDb() const noexcept;
    void setPan(float) noexcept;  float getPan() const noexcept;                  // -1 .. +1
    void setPanLaw(dsp::PanLaw) noexcept;  dsp::PanLaw getPanLaw() const noexcept;
    void setPanMode(PanMode) noexcept;  PanMode getPanMode() const noexcept;
    void setMute(bool) noexcept;  bool isMuted() const noexcept;
    void setSolo(bool) noexcept;  bool isSoloed() const noexcept;                 // tracks only
    void setSendLevelDb(int bus, float db) noexcept;  float getSendLevelDb(int bus) const noexcept;
    void setSendPreFader(int bus, bool) noexcept;  bool isSendPreFader(int bus) const noexcept;
    static void computeStripGains(float gainDb, float pan, dsp::PanLaw, PanMode, float& left, float& right) noexcept;
    InsertChain& getInserts() noexcept;  LatencyCompensator& getCompensator() noexcept;
    int getInsertLatencySamples() const noexcept;
    // meters (any thread)
    const dsp::LevelMeter& getLevelMeter() const noexcept;  const dsp::LoudnessMeter& getLoudnessMeter() const noexcept;
    float getPeakDb(int ch) const noexcept;  float getPeakHoldDb(int ch) const noexcept;  float getRmsDb(int ch) const noexcept;
    float getMomentaryLufs() const noexcept;  float getShortTermLufs() const noexcept;  float getIntegratedLufs() const noexcept;
    float getMaxPeakLinear() const noexcept;  float getMaxPeakDb() const noexcept;  float getIntegratedRmsDb() const noexcept;
    StripStats getStats() const noexcept;
    // audio thread (called by Mixer)
    void beginBlock(bool audible) noexcept;  void processInserts(AudioBlock) noexcept;
    void applyMuteSolo(AudioBlock) noexcept;  void applyFaderAndPan(AudioBlock) noexcept;
    void addSend(int bus, AudioBlock dest, const AudioBlock& src) noexcept;  void meter(const AudioBlock&) noexcept;
};
```

### Mixer.h

```cpp
class Mixer {
public:
    void prepare(double sampleRate, int maxBlockSize, int maxPdcSamples = kDefaultMaxPdcSamples);
    void reset() noexcept;
    Track& getTrack(int i) noexcept;          ChannelStrip& getBus(int i) noexcept;   ChannelStrip& getMaster() noexcept;
    ChannelStrip* getStrip(ChannelRef) noexcept;  const ChannelStrip* getStrip(ChannelRef) const noexcept;
    bool isAnyTrackSoloed() const noexcept;   bool isTrackAudible(int t) const noexcept;
    int64_t getSongEndSample() const noexcept;
    void setLatencyCompensationEnabled(bool) noexcept;  bool isLatencyCompensationEnabled() const noexcept;
    int getTotalLatencySamples() const noexcept;  int getMaxTrackLatencySamples() const noexcept;
    int getMaxBusLatencySamples() const noexcept;  int getTrackSumCompensationSamples() const noexcept;
    int getMaxPdcSamples() const noexcept;        bool hasPdcOverflow() const noexcept;
    void updateLatencies() noexcept;                                                         // audio thread
    void process(AudioBlock out, const TimelineSegment*, int numSegments, double bpm = 120.0) noexcept; // audio thread
};
```

（getter 也都有 const 版本的 overload。）PDC 上限：每個 compensator 最多容納
`getMaxPdcSamples()`（= `prepare()` 的 `maxPdcSamples`）個 sample。當最大的 track 或 bus
latency 比它更大時，mixer 會以 `min(max, limit)` 對齊，`getMaxTrackLatencySamples()` /
`getMaxBusLatencySamples()` / `getTotalLatencySamples()` 回報實際套用的 delay，比目標更慢的
路徑會晚到，而且 `hasPdcOverflow()` 為 true（Step 8）。對每個 track，`process()` 依照
track 的 format 處理：**mono** track 把 clip fold 成 $`(L+R)/2`$，在單一 channel 的 block
（`getChannelSubset(0, 1)`）上執行 insert 與 PDC delay，再把結果複製到右 channel，然後才是
mute/solo、send 與 fader x pan；**stereo** track 則在兩個 channel 上執行它們。panner 之後的一切
（send、bus、master）都是 stereo。因此 mono track 上的 `InsertEffect` 雖然是以 `kNumChannels`
prepare，仍必須接受單一 channel 的 block；所有內建 effect 都做得到（`l14_test_track_format`）。預設名稱："Track 1".."Track 8"、"Bus A"、
"Bus B"、"Master"。

### InsertEffect.h / EffectBase.h / Effects.h

```cpp
enum class ParameterKind { Float, Int, Bool, Choice };
struct ParameterInfo {
    const char* name; const char* unit; float min, max, defaultValue; ParameterKind kind;
    bool logarithmic; const char* const* choices; int numChoices;
    float clamp(float) const noexcept; float toNormalized(float) const noexcept; float fromNormalized(float) const noexcept;
};
int formatParameterValue(const ParameterInfo&, float value, char* buffer, int size) noexcept; // UI thread
struct PlayHead { const TimelineSegment* segments; int numSegments; double bpm;
                  bool isPlaying() const noexcept; int64_t blockStart() const noexcept; };

class InsertEffect {          // see Step 5 for the full listing
    virtual const char* getName() const noexcept = 0;
    virtual void prepare(double, int, int) = 0;  virtual void reset() noexcept = 0;
    virtual void process(AudioBlock) noexcept = 0;
    virtual int getLatencySamples() const noexcept;  // audio thread / setup only
    virtual void setPlayHead(const PlayHead&) noexcept;
    int getReportedLatencySamples() const noexcept;   // any thread (atomic copy, <= 1 block old)
    void publishLatency() noexcept;                   // audio thread / setup: refresh that copy
    virtual void applyPendingParameters() noexcept;
    virtual int getNumParameters() const noexcept = 0;  virtual ParameterInfo getParameterInfo(int) const noexcept = 0;
    virtual float getParameter(int) const noexcept = 0;  virtual void setParameter(int, float) noexcept = 0;
    virtual bool hasMeter() const noexcept;  virtual float getMeterDb() const noexcept;
    void setBypassed(bool) noexcept;  bool isBypassed() const noexcept;
    int findParameter(const char*) const noexcept;  bool setParameterByName(const char*, float) noexcept;
    float getParameterNormalized(int) const noexcept;  void setParameterNormalized(int, float) noexcept;
    void resetParametersToDefault() noexcept;
};
class InsertChain { /* reset, applyPendingParameters, setPlayHead, process, getLatencySamples,
                       insertAt, removeAt, replaceAt, moveTo (audio thread); getNumInserts, getInsert (any) */ };

constexpr int kMaxEffectParameters = 32;
class EffectBase : public InsertEffect {   // derive your own effects from this
protected:
    EffectBase(const ParameterInfo* infos, int count) noexcept;
    virtual void applyParameter(int index, float value) noexcept = 0;
    virtual void processDsp(AudioBlock block) noexcept = 0;
    void finishPrepare(double sampleRate) noexcept;   // call at the end of prepare()
};
class PureDelayEffect;   // delays by N samples, reports N (tests)

enum class EffectType { Gain, ParametricEq, Compressor, Limiter, Saturation, Distortion, Gate, Expander,
                        DeEsser, TransientShaper, Delay, Chorus, Flanger, Phaser, Tremolo, Reverb,
                        ConvolutionReverb, Filter, AmpChain, PureDelay };
constexpr int kNumEffectTypes = 20, kNumUserEffectTypes = 19;
const char* effectTypeName(EffectType) noexcept;
bool effectTypeFromName(const char*, EffectType&) noexcept;
std::unique_ptr<InsertEffect> createEffect(EffectType);
std::unique_ptr<InsertEffect> createEffect(const char* name);
// adapters: GainEffect, ParametricEqEffect (+ setBand(), paramIndex()), CompressorEffect, LimiterEffect,
// SaturationEffect, DistortionEffect, GateEffect, ExpanderEffect, DeEsserEffect, TransientShaperEffect,
// DelayEffect (+ noteToMs()), ChorusEffect, FlangerEffect, PhaserEffect, TremoloEffect, ReverbEffect,
// ConvolutionReverbEffect (+ setImpulseResponse()), FilterEffect (chapter 02 SVF), AmpChainEffect
// (chapter 07); each has `enum Param { ..., kCount }`.
```

### Renderer.h、SpscQueue.h、Commands.h、LatencyCompensator.h

請見 Step 9、7 與 8。`RenderOptions`、`renderOffline()`、`bounceToWav()`；`SpscQueue<T>`（`push`、
`pop`、`size`、`empty`、`capacity`）；`ChannelKind`、`ChannelRef`（`track(i)`、`bus(i)`、`master()`）、
`CommandType`、`Command`、`Garbage`；`LatencyCompensator`（`prepare`、`reset`、`setDelaySamples`、
`getDelaySamples`、`getMaxDelaySamples`、`process`）。

### DemoSong.h（不被 All.h include）

```cpp
struct DemoInsert { ChannelRef channel; EffectType type; std::vector<std::pair<const char*, float>> parameters; };
struct DemoSong {   // the 8-track song of l14_render_song (Step 9), as data
    double sampleRate, bpm; int beatsPerBar, bars;
    std::array<std::string, kNumTracks> trackNames;  std::array<std::string, kNumBuses> busNames;
    std::array<std::vector<Clip>, kNumTracks> clips;  std::vector<DemoInsert> inserts;
    std::array<TrackFormat, kNumTracks> trackFormat;   // Mono / Stereo, applied before the faders
    std::array<float, kNumTracks> trackGainDb, trackPan;
    std::array<std::array<float, kNumBuses>, kNumTracks> sendDb;  std::array<float, kNumBuses> busGainDb;
    int64_t songEndSample() const noexcept;
};
DemoSong makeDemoSong(double sampleRate);                                       // any non-audio thread; allocates
std::unique_ptr<InsertEffect> createDemoEffect(const DemoInsert&, int* unknown = nullptr);
int applyDemoSong(Engine& engine, const DemoSong& song);                       // producer thread; returns unknown parameter names
```

`l14_test_demo_song` 會檢查它載入時沒有未知的參數，並且能重現
`l14_render_song` 的報告。

## 3. JUCE app 的整合食譜

```cpp
// 1. Audio callback (juce::AudioIODeviceCallback)
void audioDeviceAboutToStart(juce::AudioIODevice* d) override {
    engine.prepare(d->getCurrentSampleRate(), d->getCurrentBufferSizeSamples()); // device stopped here
}
void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* out, int numOut,
                                      int numSamples, const juce::AudioIODeviceCallbackContext&) override {
    juce::ScopedNoDenormals noDenormals;
    dawlab::AudioBlock block(out, std::min(numOut, 2), numSamples);
    engine.processBlock(block);
    for (int ch = 2; ch < numOut; ++ch) juce::FloatVectorOperations::clear(out[ch], numSamples);
}

// 2. A 30 Hz juce::Timer on the message thread
void timerCallback() override {
    engine.collectGarbage();
    // poll meters: engine.getMixer().getTrack(t).getPeakDb(0), getMomentaryLufs(), fx->getMeterDb() ...
    // poll the playhead: engine.getTransport().getPositionBarsBeats()
}

// 3. Knobs generated from the parameter description
InsertEffect* fx = engine.addInsert(ChannelRef::track(t), -1, createEffect(EffectType::Compressor));
for (int i = 0; i < fx->getNumParameters(); ++i) {
    const ParameterInfo p = fx->getParameterInfo(i);   // name, unit, range, kind, choices, log taper
    // slider.onValueChange = [fx, i, &slider] { fx->setParameterNormalized(i, (float) slider.getValue()); };
}
```

第 2 階段的補充說明（全部都已在 `app/` 中實作，見 Step 11-21）：

- **Sample-rate / buffer-size 變更**：停止 device，再次呼叫 `engine.prepare()`（insert 會
  重新 prepare，clip 與設定都會保留），然後重新啟動。
- **Mono / 多聲道 device**：engine 輸出 stereo。對 mono device，請傳入 1 channel 的
  block（你會得到左聲道），或先 render 到一個 stereo scratch buffer 再加總。
- **播放時 bounce**：在 worker thread 上用專案建立第二個 `Engine`（Exercise 4），
  或先停止 device 再 bounce 這個執行中的 engine。
- **VST3 insert（第 13 章）**：把被 host 的 `juce::AudioPluginInstance` 包進一個衍生自
  `InsertEffect` 的 class：`prepare` -> `prepareToPlay`，`process` -> 對一個指向 `AudioBlock`
  channel pointer 的 `juce::AudioBuffer<float>` 呼叫 `processBlock`，
  `getLatencySamples` -> `getLatencySamples()`，參數 -> `getParameters()`（名稱、範圍、
  normalized 數值）。在 message thread 上 prepare 它，再用 `addInsert()` 交接。
- **向使用者回報 latency**：`engine.getLatencySamples()` - 對於即時 input monitoring 路徑，
  你可能想停用 PDC（`Mixer::setLatencyCompensationEnabled(false)`）。
