# DawLab engine - API reference and integration guide

[Back to the chapter overview](../README.md)

This page is for whoever writes a front end for the engine - in particular the stage-2 JUCE app
(`app/`). Everything is header-only in `engine/include/dawlab/engine/`, namespace
`dawlab::engine`; link the CMake target `dawlab::engine` and include `<dawlab/engine/All.h>`.

## 1. Threading model in one table

| API | UI / message thread | audio thread | notes |
|---|---|---|---|
| `Engine::prepare()` | yes, device stopped | no | allocates; re-prepares existing inserts |
| `Engine::processBlock()` | only offline (no device running) | **yes** | real-time safe |
| `Transport` setters / getters | yes | (advance only) | atomics |
| `ChannelStrip` parameter setters / getters, meters | yes | (read in `beginBlock`) | atomics |
| `InsertEffect::setParameter()`, `get...`, `setBypassed()`, `getMeterDb()` | yes | yes | atomics |
| `Engine::addInsert / replaceInsert / removeInsert / moveInsert / setTrackClips` | yes (single producer!) | no | commands |
| `Engine::getInsert()`, `getNumInserts()`, `Track::getClips()` | yes | - | see "observer pointers" |
| `Engine::collectGarbage()` | yes (the same single thread) | no | ~30 Hz timer |
| `renderOffline()`, `bounceToWav()`, `applyPendingCommandsOffline()` | yes, no device running on that engine | no | |
| `InsertEffect::prepare()` | yes (before handing over) | no | `Engine::addInsert` calls it |

"Single producer": all structural calls (`addInsert`, ..., `setTrackClips`, `pushCommand`) must come
from one thread at a time - the message thread. The command queue is single-producer.

**Observer pointers**: `addInsert()` / `replaceInsert()` / `getInsert()` return `InsertEffect*`.
Such a pointer stays valid until the effect has been removed (or replaced) **and** the thread that
runs `collectGarbage()` has collected it. So hold them only on the message thread, and drop them
when you remove the effect.

## 2. Public API

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

class Track : public ChannelStrip {
public:
    const std::vector<Clip>* exchangeClips(const std::vector<Clip>* newClips) noexcept; // audio thread (Engine)
    const std::vector<Clip>* getClips() const noexcept;
    int getNumClips() const noexcept;
    int64_t getEndSample() const noexcept;
    void renderClips(AudioBlock dest, const TimelineSegment* segments, int numSegments) noexcept;
};
```

### ChannelStrip.h

```cpp
enum class PanMode { PanLaw, Balance };            // tracks: PanLaw; buses and master: Balance
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

(const overloads of the getters exist as well.) PDC limit: every compensator holds at most
`getMaxPdcSamples()` (= `prepare()`'s `maxPdcSamples`) samples. When the largest track or bus
latency is larger, the mixer aligns on `min(max, limit)`, `getMaxTrackLatencySamples()` /
`getMaxBusLatencySamples()` / `getTotalLatencySamples()` report the delay actually applied, a path
slower than the target arrives late, and `hasPdcOverflow()` is true (Step 8). Default names: "Track 1".."Track 8", "Bus A",
"Bus B", "Master".

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

### Renderer.h, SpscQueue.h, Commands.h, LatencyCompensator.h

See Steps 9, 7 and 8. `RenderOptions`, `renderOffline()`, `bounceToWav()`; `SpscQueue<T>` (`push`,
`pop`, `size`, `empty`, `capacity`); `ChannelKind`, `ChannelRef` (`track(i)`, `bus(i)`, `master()`),
`CommandType`, `Command`, `Garbage`; `LatencyCompensator` (`prepare`, `reset`, `setDelaySamples`,
`getDelaySamples`, `getMaxDelaySamples`, `process`).

### DemoSong.h (not included by All.h)

```cpp
struct DemoInsert { ChannelRef channel; EffectType type; std::vector<std::pair<const char*, float>> parameters; };
struct DemoSong {   // the 8-track song of l14_render_song (Step 9), as data
    double sampleRate, bpm; int beatsPerBar, bars;
    std::array<std::string, kNumTracks> trackNames;  std::array<std::string, kNumBuses> busNames;
    std::array<std::vector<Clip>, kNumTracks> clips;  std::vector<DemoInsert> inserts;
    std::array<float, kNumTracks> trackGainDb, trackPan;
    std::array<std::array<float, kNumBuses>, kNumTracks> sendDb;  std::array<float, kNumBuses> busGainDb;
    int64_t songEndSample() const noexcept;
};
DemoSong makeDemoSong(double sampleRate);                                       // any non-audio thread; allocates
std::unique_ptr<InsertEffect> createDemoEffect(const DemoInsert&, int* unknown = nullptr);
int applyDemoSong(Engine& engine, const DemoSong& song);                       // producer thread; returns unknown parameter names
```

`l14_test_demo_song` checks that it loads without unknown parameters and reproduces the
`l14_render_song` report.

## 3. Integration recipe for a JUCE app

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

Further notes for stage 2 (all of them are implemented in `app/`, see Steps 11-21):

- **Sample-rate / buffer-size change**: stop the device, call `engine.prepare()` again (inserts are
  re-prepared, clips and settings stay), restart.
- **Mono / multichannel devices**: the engine renders stereo. For a mono device pass a 1-channel
  block (you get the left channel) or render into a scratch stereo buffer and sum.
- **Bounce while playing**: build a second `Engine` from the project (Exercise 4) on a worker thread,
  or stop the device and bounce the live engine.
- **VST3 inserts (chapter 13)**: wrap the hosted `juce::AudioPluginInstance` in a class derived from
  `InsertEffect`: `prepare` -> `prepareToPlay`, `process` -> `processBlock` on a
  `juce::AudioBuffer<float>` that refers to the `AudioBlock`'s channel pointers,
  `getLatencySamples` -> `getLatencySamples()`, parameters -> `getParameters()` (names, ranges,
  normalized values). Prepare it on the message thread, hand it over with `addInsert()`.
- **Report latency to the user**: `engine.getLatencySamples()` - for a live input monitoring path you
  may want to disable PDC (`Mixer::setLatencyCompensationEnabled(false)`).
