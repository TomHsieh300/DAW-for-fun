#include "Exporter.h"

#include <dawlab/Analysis.h>
#include <dawlab/DspMath.h>
#include <dawlab/engine/Renderer.h>

#include <juce_events/juce_events.h>

namespace dawlab::app {

bool Exporter::writeWavFile(const juce::File& file, const AudioBuffer& buffer, double sampleRate, int bitDepth,
                            juce::String& error) {
    if (bitDepth != 16 && bitDepth != 24 && bitDepth != 32) {
        error = "Bit depth must be 16, 24 or 32";
        return false;
    }
    file.getParentDirectory().createDirectory();
    // Written next to the file and moved over it only when complete: a write that fails half-way (a full disk)
    // leaves the file that was there - the last export, a project's audio - as it was.
    juce::TemporaryFile tmp(file);
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(tmp.getFile());
    if (!static_cast<juce::FileOutputStream*>(stream.get())->openedOk()) {
        error = "Cannot write " + file.getFullPathName();
        return false;
    }
    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(sampleRate)
                             .withNumChannels(buffer.getNumChannels())
                             .withBitsPerSample(bitDepth)
                             .withSampleFormat(bitDepth == 32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                              : juce::AudioFormatWriterOptions::SampleFormat::automatic);
    std::unique_ptr<juce::AudioFormatWriter> writer = wav.createWriterFor(stream, options);
    if (writer == nullptr) {
        error = "The WAV writer refused the format";
        return false;
    }
    // AudioFormatWriter wants an array of const float* (non-interleaved), like AudioBlock.
    std::vector<const float*> channels;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        channels.push_back(buffer.getReadPointer(ch));
    const bool samplesWritten = writer->writeFromFloatArrays(channels.data(), buffer.getNumChannels(), buffer.getNumSamples());
    writer.reset(); // writes the header and closes the file; a failure there shows only in the file's size
    const juce::int64 dataBytes = static_cast<juce::int64>(buffer.getNumSamples()) * buffer.getNumChannels() * (bitDepth / 8);
    if (!samplesWritten || tmp.getFile().getSize() < dataBytes) {
        error = "Writing the samples failed (disk full?)";
        return false;
    }
    if (!tmp.overwriteTargetFileWithTemporary()) {
        error = "Cannot replace " + file.getFullPathName();
        return false;
    }
    return true;
}

ExportResult Exporter::renderToFile(engine::Engine& engine, const ExportOptions& options, AudioBuffer* renderedOut) {
    ExportResult result;
    result.file = options.file;
    if (!engine.isPrepared()) {
        result.error = "The engine is not prepared";
        return result;
    }
    engine::RenderOptions ro;
    ro.numSamples = options.numSamples;
    ro.tailSeconds = options.tailSeconds;
    ro.blockSize = options.blockSize;

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    AudioBuffer mix = engine::renderOffline(engine, ro); // also applies queued commands first
    result.renderSeconds = (juce::Time::getMillisecondCounterHiRes() - t0) * 0.001;
    result.latencySamples = engine.getLatencySamples();
    result.seconds = mix.getNumSamples() / engine.getSampleRate();
    result.peakDb = gainToDb(peakAbs(mix));
    result.integratedLufs = engine.getMixer().getMaster().getIntegratedLufs();

    if (mix.getNumSamples() == 0) {
        result.error = "Nothing to export: the project has no clips";
        return result;
    }
    if (hasNanOrInf(mix)) {
        result.error = "The render contains NaN or Inf samples";
        return result;
    }
    if (!writeWavFile(options.file, mix, engine.getSampleRate(), options.bitDepth, result.error))
        return result;
    result.ok = true;
    if (renderedOut)
        *renderedOut = std::move(mix);
    return result;
}

ExportJob::ExportJob(engine::Engine& engine, ExportOptions options, std::function<void(const ExportResult&)> onDone)
    : juce::Thread("DawLab export"), engine_(engine), options_(std::move(options)), onDone_(std::move(onDone)) {}

ExportJob::~ExportJob() {
    alive_->store(false);
    stopThread(-1); // the render cannot be interrupted half-way; wait for it
}

void ExportJob::run() {
    auto result = std::make_shared<ExportResult>(Exporter::renderToFile(engine_, options_));
    std::weak_ptr<std::atomic<bool>> alive = alive_;
    auto onDone = onDone_;
    juce::MessageManager::callAsync([alive, result, onDone] {
        const auto flag = alive.lock();
        if (flag != nullptr && flag->load() && onDone)
            onDone(*result);
    });
}

} // namespace dawlab::app
