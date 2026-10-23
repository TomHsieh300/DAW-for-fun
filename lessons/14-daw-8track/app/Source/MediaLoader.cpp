#include "MediaLoader.h"

#include <dawlab/DspMath.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace dawlab::app {

// ---- resampling -----------------------------------------------------------------------------------

namespace {

// Kernel table: T[j] = sinc(u) * blackman(u / Z) for u = j / R, j = 0 .. Z*R (one side; the
// kernel is symmetric). u is measured in zero crossings of the sinc.
const std::vector<float>& sincTable() {
    static const std::vector<float> table = [] {
        constexpr int Z = ResamplerSettings::kZeroCrossings;
        constexpr int R = ResamplerSettings::kTableResolution;
        std::vector<float> t(static_cast<size_t>(Z * R + 2), 0.0f);
        for (int j = 0; j <= Z * R; ++j) {
            const double u = static_cast<double>(j) / R;
            const double sinc = (j == 0) ? 1.0 : std::sin(kPi * u) / (kPi * u);
            // Blackman window on [-Z, Z], evaluated at u: 0.42 + 0.5 cos(pi u/Z) + 0.08 cos(2 pi u/Z)
            const double x = kPi * u / Z;
            const double w = 0.42 + 0.5 * std::cos(x) + 0.08 * std::cos(2.0 * x);
            t[static_cast<size_t>(j)] = static_cast<float>(sinc * w);
        }
        return t;
    }();
    return table;
}

} // namespace

AudioBuffer resampleBuffer(const AudioBuffer& input, double fromRate, double toRate) {
    const int numCh = input.getNumChannels();
    const int n = input.getNumSamples();
    if (fromRate <= 0.0 || toRate <= 0.0 || fromRate == toRate || n == 0)
        return input;

    constexpr int Z = ResamplerSettings::kZeroCrossings;
    constexpr int R = ResamplerSettings::kTableResolution;
    const double ratio = toRate / fromRate;                                  // output samples per input sample
    const double fc = 0.5 * std::min(1.0, ratio) * ResamplerSettings::kCutoffScale; // cycles per input sample
    const double halfWidth = Z / (2.0 * fc);                                 // W, in input samples
    const double step = 1.0 / ratio;                                         // input samples per output sample
    // n * toRate / fromRate, computed in this order so that e.g. 44100 -> 48000 gives exactly 48000
    const int outLength = static_cast<int>(std::ceil(static_cast<double>(n) * toRate / fromRate - 1e-9));
    const std::vector<float>& table = sincTable();
    const double tableScale = 2.0 * fc * R; // input-sample distance -> table index

    AudioBuffer out(numCh, outLength);
    for (int ch = 0; ch < numCh; ++ch) {
        const float* x = input.getReadPointer(ch);
        float* y = out.getWritePointer(ch);
        for (int m = 0; m < outLength; ++m) {
            const double t = m * step;
            const int kFirst = std::max(0, static_cast<int>(std::ceil(t - halfWidth)));
            const int kLast = std::min(n - 1, static_cast<int>(std::floor(t + halfWidth)));
            double acc = 0.0;
            for (int k = kFirst; k <= kLast; ++k) {
                const double pos = std::abs(t - k) * tableScale;
                const int j = static_cast<int>(pos);
                if (j >= Z * R)
                    continue;
                const double frac = pos - j;
                const double g = table[static_cast<size_t>(j)] +
                                 frac * (table[static_cast<size_t>(j) + 1] - table[static_cast<size_t>(j)]);
                acc += x[k] * g;
            }
            // 2 fc is the DC gain of the kernel (the sinc's area in input samples).
            y[m] = static_cast<float>(acc * 2.0 * fc);
        }
    }
    return out;
}

// ---- file reading ---------------------------------------------------------------------------------

namespace {

// WAV and AIFF always; FLAC, Ogg Vorbis, Opus and MP3 when the JUCE_USE_xxx options are on
// (they are, in chapter 13's host library). Every format here can be read; MP3 cannot be written.
void registerReadableFormats(juce::AudioFormatManager& formats) { formats.registerBasicFormats(); }

} // namespace

juce::String MediaLoader::getWildcard() {
    juce::AudioFormatManager formats;
    registerReadableFormats(formats);
    return formats.getWildcardForAllFormats();
}

bool MediaLoader::canRead(const juce::File& file) {
    juce::AudioFormatManager formats;
    registerReadableFormats(formats);
    return file.getFileExtension().isNotEmpty() &&
           formats.findFormatForFileExtension(file.getFileExtension()) != nullptr;
}

LoadedMedia MediaLoader::readAudioFile(const juce::File& file, double targetSampleRate) {
    LoadedMedia result;
    result.name = file.getFileName();
    result.targetSampleRate = targetSampleRate;

    juce::AudioFormatManager formats;
    registerReadableFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) {
        result.error = "Cannot read " + file.getFullPathName() + " (WAV, AIFF, FLAC, Ogg Vorbis, Opus or MP3 expected)";
        return result;
    }
    const auto length = reader->lengthInSamples;
    const double maxSeconds = 20.0 * 60.0;
    if (length <= 0 || static_cast<double>(length) > maxSeconds * reader->sampleRate) {
        result.error = "The file is empty or longer than 20 minutes";
        return result;
    }
    result.fileSampleRate = reader->sampleRate;

    // The engine is stereo: mono files are duplicated, extra channels are dropped.
    const int fileChannels = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> temp(std::max(1, fileChannels), static_cast<int>(length));
    reader->read(&temp, 0, static_cast<int>(length), 0, true, true);

    AudioBuffer stereo(2, static_cast<int>(length));
    for (int ch = 0; ch < 2; ++ch)
        std::copy_n(temp.getReadPointer(std::min(ch, temp.getNumChannels() - 1)), static_cast<int>(length),
                    stereo.getWritePointer(ch));

    AudioBuffer converted = resampleBuffer(stereo, reader->sampleRate, targetSampleRate);
    result.audio = std::make_shared<const AudioBuffer>(std::move(converted));
    return result;
}

// ---- the loader thread ----------------------------------------------------------------------------

MediaLoader::MediaLoader() = default;

MediaLoader::~MediaLoader() {
    alive_->store(false);
    pool_.removeAllJobs(true, 60000); // wait for a running job (it may be resampling)
}

void MediaLoader::loadFile(int track, const juce::File& file, double targetSampleRate,
                           std::function<void(const LoadedMedia&)> onDone) {
    ++pending_;
    std::weak_ptr<std::atomic<bool>> alive = alive_;
    pool_.addJob([this, alive, track, file, targetSampleRate, onDone = std::move(onDone)] {
        auto media = std::make_shared<LoadedMedia>(readAudioFile(file, targetSampleRate));
        media->track = track;
        juce::MessageManager::callAsync([this, alive, media, onDone] {
            const auto flag = alive.lock();
            if (flag == nullptr || !flag->load())
                return; // the loader (and probably the GUI) is gone
            --pending_;
            if (onDone)
                onDone(*media);
        });
    });
}

void MediaLoader::loadDemoSong(double sampleRate, std::function<void(std::shared_ptr<const engine::DemoSong>)> onDone) {
    ++pending_;
    std::weak_ptr<std::atomic<bool>> alive = alive_;
    pool_.addJob([this, alive, sampleRate, onDone = std::move(onDone)] {
        auto song = std::make_shared<const engine::DemoSong>(engine::makeDemoSong(sampleRate));
        juce::MessageManager::callAsync([this, alive, song, onDone] {
            const auto flag = alive.lock();
            if (flag == nullptr || !flag->load())
                return;
            --pending_;
            if (onDone)
                onDone(song);
        });
    });
}

void MediaLoader::convertBuffers(std::vector<std::shared_ptr<const AudioBuffer>> sources, double fromRate,
                                 double toRate, std::function<void(const ConvertedBuffers&)> onDone) {
    ++pending_;
    std::weak_ptr<std::atomic<bool>> alive = alive_;
    pool_.addJob([this, alive, sources = std::move(sources), fromRate, toRate, onDone = std::move(onDone)] {
        auto converted = std::make_shared<ConvertedBuffers>();
        for (const auto& src : sources)
            if (src != nullptr && converted->count(src) == 0)
                converted->emplace(src, std::make_shared<const AudioBuffer>(resampleBuffer(*src, fromRate, toRate)));
        juce::MessageManager::callAsync([this, alive, converted, onDone] {
            const auto flag = alive.lock();
            if (flag == nullptr || !flag->load())
                return;
            --pending_;
            if (onDone)
                onDone(*converted);
        });
    });
}

} // namespace dawlab::app
