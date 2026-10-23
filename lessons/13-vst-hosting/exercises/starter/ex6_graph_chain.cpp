// Exercise 6 (starter): chain two plugins with juce::AudioProcessorGraph and measure the latency of the whole chain
//
//   [audio in] --> DawLabCompressor (makeup +12 dB, deliberately pushed to clip) --> DawLabLimiter (ceiling -1 dB, lookahead 5 ms) --> [audio out]
//
// AudioProcessorGraph is JUCE's "modular host": every node is an AudioProcessor (it can be a plugin instance),
// and a connection is (node, channel) -> (node, channel). At prepareToPlay the graph works out the processing order (the render sequence)
// and does PDC automatically: when parallel paths have different latencies, a delay is inserted in the shorter path; the latency of the whole graph = the sum along the longest path.
//
// Complete the TODO in buildChain(). Currently the graph has no nodes, so the self-check prints FAIL (this is expected).
//
// Verification:
//   1. graph.getLatencySamples() = compressor (0) + limiter (5 ms = 240 samples);
//   2. Feed an impulse in; the output peak appears exactly latency samples later (measured value = reported value);
//   3. Drum loop: through the compressor alone the peak is > 0 dBFS (clipped), and the output peak of the whole chain is <= -1 dBFS.
// Output: dawlab_output/l13_ex6_comp_only.wav, l13_ex6_chain.wav

#include "../../examples/L13Common.h"

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

// Sets a parameter directly on an AudioProcessor (the plugin has been handed to the graph, so there is no HostedPlugin to use)
void setByName(juce::AudioProcessor& p, const char* name, float normalized) {
    for (auto* param : p.getParameters())
        if (param->getName(64) == name)
            param->setValue(normalized);
}

std::unique_ptr<juce::AudioPluginInstance> load(dawlab::host::PluginHost& host, const juce::File& file) {
    juce::String error;
    auto instance = host.loadFromFile(file, kSampleRate, kBlockSize, error);
    if (instance == nullptr)
        std::printf("Load failed: %s\n", error.toRawUTF8());
    return instance;
}

// Feeds the input into any AudioProcessor (a graph or a single plugin) in blocks of kBlockSize
dawlab::AudioBuffer renderProcessor(juce::AudioProcessor& processor, const dawlab::AudioBuffer& input) {
    dawlab::AudioBuffer out = input;
    juce::AudioBuffer<float> block(2, kBlockSize);
    juce::MidiBuffer midi;
    for (int start = 0; start < out.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, out.getNumSamples() - start);
        block.setSize(2, n, false, false, true); // n <= kBlockSize: no reallocation
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom(ch, 0, out.getReadPointer(ch) + start, n);
        processor.processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            std::copy_n(block.getReadPointer(ch), n, out.getWritePointer(ch) + start);
    }
    return out;
}

// Builds the graph in -> compressor -> limiter -> out (already prepared when returned)
std::unique_ptr<juce::AudioProcessorGraph> buildChain(dawlab::host::PluginHost& host, bool withLimiter) {
    auto comp = load(host, l13::defaultCompressorVst3());
    auto lim = withLimiter ? load(host, l13::defaultLimiterVst3()) : nullptr;
    if (comp == nullptr || (withLimiter && lim == nullptr))
        return nullptr;

    setByName(*comp, "Threshold", l13::toNormalized(-20.0f, -60.0f, 0.0f));
    setByName(*comp, "Makeup", l13::toNormalized(12.0f, -12.0f, 24.0f)); // +12 dB: deliberately pushed above 0 dBFS
    if (lim != nullptr) {
        setByName(*lim, "Ceiling", l13::toNormalized(-1.0f, -24.0f, 0.0f));
        setByName(*lim, "Lookahead", l13::toNormalized(5.0f, 0.0f, 20.0f));
    }

    auto graph = std::make_unique<juce::AudioProcessorGraph>();
    graph->setPlayConfigDetails(2, 2, kSampleRate, kBlockSize); // the graph itself: 2 in, 2 out
    // TODO:
    //   1. add the input and output nodes: graph->addNode(std::make_unique<IOProcessor>(IOProcessor::audioInputNode))->nodeID
    //      (use IOProcessor::audioOutputNode for the output);
    //   2. graph->addNode(std::move(comp)), and add the limiter too if there is one;
    //   3. use graph->addConnection({{source nodeID, channel}, {destination nodeID, channel}}) to connect both the left and right
    //      channels of in -> comp -> (lim ->) out.
    // Currently the graph is empty: the output is always 0 and the latency is also 0.
    juce::ignoreUnused(comp, lim);

    graph->prepareToPlay(kSampleRate, kBlockSize); // works out the render sequence and computes the latency
    return graph;
}

} // namespace

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    dawlab::host::PluginHost host;

    auto chain = buildChain(host, true);
    auto compOnly = buildChain(host, false);
    if (chain == nullptr || compOnly == nullptr)
        return 1;

    // ---- 1. The reported latency ----------------------------------------------------
    const int reported = chain->getLatencySamples();
    const int expectedLatency = dawlab::msToSamples(5.0, kSampleRate); // 240
    std::printf("Latency reported by the graph: %d samples (%.2f ms); compressor-only graph: %d\n", reported,
                1000.0 * reported / kSampleRate, compOnly->getLatencySamples());

    // ---- 2. Measure with an impulse --------------------------------------------------
    const int impulseAt = 1000;
    dawlab::AudioBuffer impulse = dawlab::makeImpulse(8192, 2, impulseAt, 0.25f);
    const auto impulseOut = renderProcessor(*chain, impulse);
    int peakIndex = 0;
    for (int i = 0; i < impulseOut.getNumSamples(); ++i)
        if (std::abs(impulseOut.getSample(0, i)) > std::abs(impulseOut.getSample(0, peakIndex)))
            peakIndex = i;
    const int measured = peakIndex - impulseAt;
    std::printf("Impulse in at %d, out at %d -> measured latency = %d samples\n", impulseAt, peakIndex, measured);

    // ---- 3. Drum loop: compressor only vs the whole chain --------------------------------
    chain->reset();
    const dawlab::AudioBuffer drums = dawlab::makeDrumLoop(kSampleRate, 2);
    const auto compOut = renderProcessor(*compOnly, drums);
    const auto chainOut = renderProcessor(*chain, drums);
    std::printf("peak: input %.2f dBFS, compressor only %.2f dBFS, compressor -> limiter %.2f dBFS\n",
                l13::peakDb(drums), l13::peakDb(compOut), l13::peakDb(chainOut));
    // Only a 32-bit float WAV keeps overs above 0 dBFS (24-bit integer would clip them)
    dawlab::writeWav(dawlab::outputPath("l13_ex6_comp_only.wav"), compOut, kSampleRate, 32);
    dawlab::writeWav(dawlab::outputPath("l13_ex6_chain.wav"), chainOut, kSampleRate);

    chain->releaseResources();
    compOnly->releaseResources();

    DAWLAB_CHECK(reported == expectedLatency);
    DAWLAB_CHECK(compOnly->getLatencySamples() == 0);
    DAWLAB_CHECK(measured == reported);
    DAWLAB_CHECK(l13::peakDb(compOut) > 0.0f);
    DAWLAB_CHECK(l13::peakDb(chainOut) <= -1.0f + 0.01f);
    return dawlab::test::summary("l13_ex6_graph_chain");
}
