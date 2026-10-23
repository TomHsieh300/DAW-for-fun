// 練習 6（起始碼）：用 juce::AudioProcessorGraph 串接兩個 plugin，並量測整條鏈的 latency
//
//   [audio in] ──► DawLabCompressor（makeup +12 dB，故意推爆）──► DawLabLimiter（ceiling −1 dB，lookahead 5 ms）──► [audio out]
//
// AudioProcessorGraph 是 JUCE 的「模組化 host」：每個 node 是一個 AudioProcessor（可以是 plugin instance），
// connection 是 (node, channel) → (node, channel)。graph 在 prepareToPlay 時排出處理順序（render sequence），
// 並自動做 PDC：平行路徑的 latency 不同時，在較短的路徑插入 delay；整個 graph 的 latency = 最長路徑的總和。
//
// 完成 buildChain() 裡的 TODO。目前 graph 沒有任何節點，self-check 會印出 FAIL（這是預期的）。
//
// 驗證：
//   1. graph.getLatencySamples() = compressor（0）+ limiter（5 ms = 240 samples）；
//   2. 送一個 impulse 進去，輸出 peak 出現的位置剛好晚了 latency 個 sample（量測值 = 回報值）；
//   3. drum loop：只經過 compressor 時 peak > 0 dBFS（爆了），整條鏈的輸出 peak ≤ −1 dBFS。
// 輸出：dawlab_output/l13_ex6_comp_only.wav、l13_ex6_chain.wav

#include "../../examples/L13Common.h"

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

// 直接對 AudioProcessor 設參數（plugin 已交給 graph 擁有，沒有 HostedPlugin 可用）
void setByName(juce::AudioProcessor& p, const char* name, float normalized) {
    for (auto* param : p.getParameters())
        if (param->getName(64) == name)
            param->setValue(normalized);
}

std::unique_ptr<juce::AudioPluginInstance> load(dawlab::host::PluginHost& host, const juce::File& file) {
    juce::String error;
    auto instance = host.loadFromFile(file, kSampleRate, kBlockSize, error);
    if (instance == nullptr)
        std::printf("載入失敗：%s\n", error.toRawUTF8());
    return instance;
}

// 把 input 以 kBlockSize 一塊一塊送進任意 AudioProcessor（graph 或單一 plugin）
dawlab::AudioBuffer renderProcessor(juce::AudioProcessor& processor, const dawlab::AudioBuffer& input) {
    dawlab::AudioBuffer out = input;
    juce::AudioBuffer<float> block(2, kBlockSize);
    juce::MidiBuffer midi;
    for (int start = 0; start < out.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, out.getNumSamples() - start);
        block.setSize(2, n, false, false, true); // n ≤ kBlockSize：不會重新配置
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom(ch, 0, out.getReadPointer(ch) + start, n);
        processor.processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            std::copy_n(block.getReadPointer(ch), n, out.getWritePointer(ch) + start);
    }
    return out;
}

// 建立 in → compressor → limiter → out 的 graph（回傳時已 prepare）
std::unique_ptr<juce::AudioProcessorGraph> buildChain(dawlab::host::PluginHost& host, bool withLimiter) {
    auto comp = load(host, l13::defaultCompressorVst3());
    auto lim = withLimiter ? load(host, l13::defaultLimiterVst3()) : nullptr;
    if (comp == nullptr || (withLimiter && lim == nullptr))
        return nullptr;

    setByName(*comp, "Threshold", l13::toNormalized(-20.0f, -60.0f, 0.0f));
    setByName(*comp, "Makeup", l13::toNormalized(12.0f, -12.0f, 24.0f)); // +12 dB：故意推到 0 dBFS 以上
    if (lim != nullptr) {
        setByName(*lim, "Ceiling", l13::toNormalized(-1.0f, -24.0f, 0.0f));
        setByName(*lim, "Lookahead", l13::toNormalized(5.0f, 0.0f, 20.0f));
    }

    auto graph = std::make_unique<juce::AudioProcessorGraph>();
    graph->setPlayConfigDetails(2, 2, kSampleRate, kBlockSize); // graph 本身：2 進 2 出
    // TODO：
    //   1. 加入輸入、輸出節點：graph->addNode(std::make_unique<IOProcessor>(IOProcessor::audioInputNode))->nodeID
    //      （輸出用 IOProcessor::audioOutputNode）；
    //   2. graph->addNode(std::move(comp))，有 limiter 的話也加進去；
    //   3. 用 graph->addConnection({{來源 nodeID, channel}, {目的 nodeID, channel}}) 把
    //      in → comp →（lim →）out 的左右兩個 channel 都接起來。
    // 目前 graph 是空的：輸出永遠是 0，latency 也是 0。
    juce::ignoreUnused(comp, lim);

    graph->prepareToPlay(kSampleRate, kBlockSize); // 排出 render sequence、計算 latency
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

    // ---- 1. 回報的 latency ----------------------------------------------------------
    const int reported = chain->getLatencySamples();
    const int expectedLatency = dawlab::msToSamples(5.0, kSampleRate); // 240
    std::printf("graph 回報的 latency：%d samples（%.2f ms）；compressor-only graph：%d\n", reported,
                1000.0 * reported / kSampleRate, compOnly->getLatencySamples());

    // ---- 2. 用 impulse 量測 ----------------------------------------------------------
    const int impulseAt = 1000;
    dawlab::AudioBuffer impulse = dawlab::makeImpulse(8192, 2, impulseAt, 0.25f);
    const auto impulseOut = renderProcessor(*chain, impulse);
    int peakIndex = 0;
    for (int i = 0; i < impulseOut.getNumSamples(); ++i)
        if (std::abs(impulseOut.getSample(0, i)) > std::abs(impulseOut.getSample(0, peakIndex)))
            peakIndex = i;
    const int measured = peakIndex - impulseAt;
    std::printf("impulse 在 %d 進去，在 %d 出來 → 量測 latency = %d samples\n", impulseAt, peakIndex, measured);

    // ---- 3. drum loop：compressor only vs 整條鏈 ----------------------------------------
    chain->reset();
    const dawlab::AudioBuffer drums = dawlab::makeDrumLoop(kSampleRate, 2);
    const auto compOut = renderProcessor(*compOnly, drums);
    const auto chainOut = renderProcessor(*chain, drums);
    std::printf("peak：輸入 %.2f dBFS，只有 compressor %.2f dBFS，compressor → limiter %.2f dBFS\n",
                l13::peakDb(drums), l13::peakDb(compOut), l13::peakDb(chainOut));
    // 32-bit float WAV 才保留 0 dBFS 以上的 overs（24-bit 整數會被截斷）
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
