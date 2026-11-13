#pragma once
// MediaLoader: everything that is too slow for the message thread - reading audio files,
// resampling them to the engine's sample rate, generating the demo song (Step 18).
//
//   message thread                    loader thread (juce::ThreadPool, 1 worker)
//   --------------                    ------------------------------------------
//   loadFile(track, file, 48000, cb)  -> read the file (juce::AudioFormatReader)
//                                     -> resampleBuffer(44100 -> 48000)
//                                     -> std::shared_ptr<const AudioBuffer>
//   cb(result)  <- MessageManager::callAsync -+
//   Session::setTrackAudio(...)  -> Engine::setTrackClips (command queue) -> audio thread
//
// The callback always runs on the message thread, and never after the MediaLoader has been
// destroyed (a shared "alive" flag is checked first).
//
// Resampling: a windowed-sinc interpolator (band-limited interpolation, Smith & Gossett 1984).
// To produce output sample m we evaluate the input signal "between the samples" at
// t = m * fromRate / toRate input samples:
//
//   y[m] = sum_k x[k] * g(t - k),   g(x) = 2 fc * sinc(2 fc x) * w(x / W)
//
// where fc (cycles per input sample) is just below the lower of the two Nyquist frequencies:
// fc = 0.5 * min(1, toRate / fromRate) * kCutoffScale. w is a Blackman window over
// |x| <= W = kZeroCrossings / (2 fc). The kernel is not a brick wall: its transition band is
// about 5.5 / (2W) = 0.23 fc wide, centred on fc, so with kCutoffScale = 0.92 it ends near
// 0.513 * toRate when downsampling - just above the output Nyquist frequency. Aliasing is
// therefore confined to 0.487 .. 0.5 * toRate (21.5 - 22.05 kHz at 44.1 kHz) and attenuated by
// at least about 54 dB (README 3.10; the smoke test measures it).
// The kernel is tabulated (kTableResolution points per zero crossing) and linearly
// interpolated, so each output sample costs about 2W multiply-adds.

#include <dawlab/AudioBuffer.h>
#include <dawlab/engine/DemoSong.h>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace dawlab::app {

// Resamples every channel of `input` from `fromRate` to `toRate`. The output has
// ceil(N * toRate / fromRate) samples. Equal rates return a copy. Any thread; allocates.
AudioBuffer resampleBuffer(const AudioBuffer& input, double fromRate, double toRate);

struct ResamplerSettings {
    static constexpr int kZeroCrossings = 24;     // kernel half-width in zero crossings
    static constexpr int kTableResolution = 512;  // table points per zero crossing
    static constexpr double kCutoffScale = 0.92;  // fc = 0.92 * Nyquist of the lower rate (~0.88: no alias at all)
};

// What an audio file may declare. MediaLoader::readChecked() checks it before it allocates anything:
// a header is not proof of anything, and a damaged or hostile one must not cost gigabytes.
struct MediaLimits {
    static constexpr double kMinSampleRate = 8000.0;   // the rates audio files really have
    static constexpr double kMaxSampleRate = 768000.0;
    static constexpr double kMaxSeconds = 20.0 * 60.0; // at the file's own rate
    // 20 minutes at 192 kHz: at most 1.8 GB as stereo float, whatever rate the header claims.
    static constexpr std::int64_t kMaxFrames = static_cast<std::int64_t>(kMaxSeconds) * 192000;
};

// Result of a background sample-rate conversion: old buffer -> the same audio at the new rate.
// The key is a shared_ptr (compared by address) so the old buffer stays alive as long as the
// map exists: its address cannot be reused by a new buffer and mistaken for it.
using ConvertedBuffers = std::map<std::shared_ptr<const AudioBuffer>, std::shared_ptr<const AudioBuffer>>;

struct LoadedMedia {
    engine::TrackId track;                     // where the clip goes (chapter 15: by id)
    juce::String name;                         // file name, for the clip and the track
    std::shared_ptr<const AudioBuffer> audio;  // at the target sample rate; nullptr on error
    double fileSampleRate = 0.0;
    int fileNumChannels = 0;                   // 1 -> the track becomes mono, 2+ -> stereo
    double targetSampleRate = 0.0;
    int nonFiniteSamples = 0;                  // NaN or Inf in the file, set to 0 (see readChecked)
    juce::String error;
};

class MediaLoader {
public:
    MediaLoader();
    ~MediaLoader(); // waits for a running job; pending callbacks are dropped

    MediaLoader(const MediaLoader&) = delete;
    MediaLoader& operator=(const MediaLoader&) = delete;

    // Reads `file` (WAV / AIFF / FLAC / Ogg Vorbis / Opus / MP3) on the loader thread, converts it to stereo float at
    // `targetSampleRate` and calls `onDone` on the message thread.
    void loadFile(engine::TrackId track, const juce::File& file, double targetSampleRate,
                  std::function<void(const LoadedMedia&)> onDone);

    // Generates the demo song (engine::makeDemoSong) on the loader thread.
    void loadDemoSong(double sampleRate, std::function<void(std::shared_ptr<const engine::DemoSong>)> onDone);

    // Resamples every buffer of `sources` from `fromRate` to `toRate` on the loader thread and
    // calls `onDone` on the message thread (device sample-rate change, Step 11). The shared_ptrs
    // keep the sources alive while the job runs; nothing else is touched by the loader thread.
    void convertBuffers(std::vector<std::shared_ptr<const AudioBuffer>> sources, double fromRate, double toRate,
                        std::function<void(const ConvertedBuffers&)> onDone);

    bool isBusy() const noexcept { return pending_.load() > 0; }

    // The synchronous part of loadFile(), usable from any thread (tests, the smoke test).
    static LoadedMedia readAudioFile(const juce::File& file, double targetSampleRate);

    // The one place where the program reads a whole audio file (readAudioFile, and ProjectIO for a
    // project's own WAVs). It refuses what MediaLimits does not allow and a WAV or AIFF that is cut off
    // (shorter than its header declares; a cut-off compressed file loads, silent where it ends). It
    // reads at most 2 channels straight into `out`: 2 channels when `stereo` (a mono file duplicated),
    // else the file's 1 or 2. Every sample that is NaN or Inf becomes 0 and is counted in
    // `nonFiniteSamples`. False, with `error`, when nothing could be read.
    static bool readChecked(juce::AudioFormatReader& reader, bool stereo, AudioBuffer& out, juce::String& error,
                            int& nonFiniteSamples);

    // "3 samples were NaN or Inf, set to 0": how the program reports nonFiniteSamples.
    static juce::String describeNonFinite(int count);

    // File patterns the reader understands, for the file chooser ("*.wav;*.aif;...;*.mp3").
    static juce::String getWildcard();

    // True when the file's extension belongs to a format the reader understands (used to accept
    // files dragged in from the file browser; the content is only checked when it is read).
    static bool canRead(const juce::File& file);

private:
    juce::ThreadPool pool_{juce::ThreadPoolOptions{}.withThreadName("DawLab loader").withNumberOfThreads(1)};
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    std::atomic<int> pending_{0};
};

} // namespace dawlab::app
