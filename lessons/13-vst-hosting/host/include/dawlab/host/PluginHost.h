#pragma once
// 第 13 章：可重用的 VST3 plugin hosting 函式庫（CMake target：dawlab_plugin_host）。
//
// 三個 class，由外而內：
//
//   PluginHost    ── 「找 plugin、生 plugin」：掃描 .vst3、維護 KnownPluginList（快取）、建立 instance。
//   HostedPlugin  ── 「用 plugin」：包住一個 juce::AudioPluginInstance，負責 prepare / process /
//                    參數（0..1 normalized）/ state 保存與還原。第 14 章的 DAW 在每個 insert slot 放一個。
//   PluginWindow  ── 「看 plugin」：一個 DocumentWindow，顯示 plugin 自己的 editor；
//                    plugin 沒有 editor 時改用 juce::GenericAudioProcessorEditor（自動產生的滑桿介面）。
//
// ─────────────────────────────────────────────────────────────────────────────
// Thread 規則（非常重要，第 14 章也照這套走）
// ─────────────────────────────────────────────────────────────────────────────
//   message thread（= GUI thread，JUCE 的 MessageManager 所在的 thread）：
//     - PluginHost 的所有函式（scan / loadFromFile / createInstance）。VST3 規格要求 plugin 的
//       建立、初始化、editor 開關都在 UI thread 上進行；很多 plugin 在別的 thread 上被建立會直接 crash。
//     - HostedPlugin::prepare / releaseResources / getState / setState / 建立 PluginWindow。
//       prepare / setState 必須在「audio thread 沒有同時呼叫 process()」時執行
//       （例如 audio device 停止時，或先把 plugin 從 audio callback 拿掉）。
//   audio thread（audio device 的 callback，或離線 render 的迴圈）：
//     - 只呼叫 HostedPlugin::process()。它不 allocate、不複製 audio、不等待 lock：
//       用 juce::AudioBuffer<float> 的 referencing constructor 直接包住 dawlab::AudioBlock 的指標；
//       plugin 的 callback lock 用 try-lock，拿不到（message thread 正在 suspendProcessing(true)）就輸出原音。
//       注意：try-lock 只在 suspendProcessing 期間生效；setState / prepare 不受它保護（JUCE 的 VST3 hosting
//       換 state 時拿的是 MessageManagerLock），所以才需要上面「audio thread 沒有同時呼叫 process()」的規則。
//       另外 JUCE 的 VST3PluginInstance::processBlock 內部會 spin 等待 processMutex（SpinLock），
//       prepareToPlay / releaseResources / reset / isBusesLayoutSupported 在 message thread 拿同一把鎖；
//       「不等待 lock」只在 host 遵守上述規則（這些呼叫不與 process() 同時進行）時才成立。
//   任意 thread：
//     - getParameterValue / setParameterValue / getLatencySamples / getNumParameters / getName。
//       JUCE 的 VST3 hosting 的 setValue() 只把值寫進 lock-free cache 並排進 dispatcher，
//       下一個 process() 再透過 IParameterChanges 送進 plugin，所以 audio thread 上做 automation
//       （每個 sub-block 設一次參數）是可行的。
//       細節：setParameterValue() 之後還會呼叫 sendValueChangedMessageToListeners() 讓開著的 editor 跟著動，
//       它會短暫拿參數的 listenerLock（CriticalSection）並同步呼叫 listener。平常沒有競爭、很快，
//       但嚴格說不是 lock-free；要求最嚴格的 host 可以在 audio thread 改呼叫
//       getInstance().getParameters()[i]->setValue(v)（不通知 listener），再由 message thread 的 timer 更新 UI。
//
// 生命週期（lifecycle）：
//   PluginHost::loadFromFile()  →  HostedPlugin(std::move(instance))  →  prepare()
//     →  process() × N  →  releaseResources()  →（先關掉 PluginWindow）→ 解構 HostedPlugin
//
// 依賴：JUCE（juce_audio_utils 以上，含 juce_audio_processors 的 VST3 hosting）與 dawlab::dsp。

#include <dawlab/AudioBuffer.h>

#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>
#include <memory>
#include <vector>

namespace dawlab::host {

// ===========================================================================
// PluginHost：掃描與建立 plugin（全部在 message thread 呼叫）
// ===========================================================================
class PluginHost {
public:
    // 建立 juce::AudioPluginFormatManager 並註冊 VST3 格式（含 editor 支援的 juce::VST3PluginFormat）。
    PluginHost();
    ~PluginHost();

    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    // 掃描一個 .vst3（檔案或 bundle 資料夾）或一個資料夾（找出底下所有 .vst3）。
    // 每個找到的 plugin 會加進 KnownPluginList（快取：同一個檔案沒被修改就不會重新載入）。
    // 注意：VST3 的掃描必須真的把 DLL 載進來、呼叫 factory，所以有問題的 plugin 可能讓整個程式 crash。
    // 專業 DAW 會在另一個 process 裡掃描（out-of-process scanning），本函式庫為了教學簡潔是 in-process。
    juce::Array<juce::PluginDescription> scan(const juce::File& fileOrFolder, bool recursive = true);

    // 掃描 vst3File，取第一個 plugin 建立 instance。失敗回傳 nullptr，原因寫進 error。
    std::unique_ptr<juce::AudioPluginInstance> loadFromFile(const juce::File& vst3File, double sampleRate,
                                                            int blockSize, juce::String& error);

    // 依 PluginDescription（例如從 KnownPluginList 或存檔讀回來的）建立 instance。
    std::unique_ptr<juce::AudioPluginInstance> createInstance(const juce::PluginDescription& desc, double sampleRate,
                                                              int blockSize, juce::String& error);

    juce::AudioPluginFormatManager& getFormatManager();

    // ---- 額外功能（合約之外） ---------------------------------------------------------
    // 系統預設的 VST3 資料夾（Windows：%LOCALAPPDATA%\Programs\Common\VST3 與
    // C:\Program Files\Common Files\VST3，以及環境變數 VST3_PATH）。
    juce::FileSearchPath getDefaultSearchPaths();

    // 只列出資料夾底下的 .vst3 路徑，不載入任何 DLL（很快、不會 crash）。
    juce::StringArray findPluginFiles(const juce::File& folder, bool recursive = true);

    // 掃描結果的快取。DAW 啟動時讀回來，就不必每次重新載入所有 plugin。
    juce::KnownPluginList& getKnownPluginList();
    bool saveKnownPluginList(const juce::File& xmlFile) const;
    bool loadKnownPluginList(const juce::File& xmlFile);

    // 上一次 scan() 裡「是 .vst3 但載不出任何 plugin」的檔案。
    const juce::StringArray& getLastScanFailures() const;

private:
    juce::AudioPluginFormat* findVst3Format() const;

    juce::AudioPluginFormatManager formatManager_;
    juce::KnownPluginList knownPlugins_;
    juce::StringArray lastFailures_;
};

// ===========================================================================
// HostedPlugin：包住一個 plugin instance
// ===========================================================================
class HostedPlugin {
public:
    explicit HostedPlugin(std::unique_ptr<juce::AudioPluginInstance> instance);
    ~HostedPlugin();

    HostedPlugin(const HostedPlugin&) = delete;
    HostedPlugin& operator=(const HostedPlugin&) = delete;

    // message thread（或 audio 停止時）。唯一會 allocate 的地方：
    //   1. 跟 plugin 協商 bus layout：主輸入 / 主輸出都試著設成 numChannels（1 = mono、2 = stereo），
    //      plugin 不接受就沿用它的預設 layout；
    //   2. 呼叫 plugin 的 prepareToPlay(sampleRate, maxBlockSize)；
    //   3. 預先配置 process() 需要的東西：channel 指標陣列、多出來的 channel 用的暫存 buffer、MidiBuffer。
    // 可以重複呼叫（例如換 sample rate）；會先 releaseResources()。
    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void releaseResources();
    bool isPrepared() const noexcept { return prepared_; }

    // audio thread：in-place 處理 block。不 allocate、不複製 audio。
    //   - block 比 maxBlockSize 長：自動切成多段依序處理；
    //   - block 的 channel 比 plugin 少（例如 mono block 進 stereo plugin）：多出來的輸入 channel
    //     用 channel 0 的複本餵進去，對應的輸出丟掉；
    //   - block 的 channel 比 plugin 多：多出來的 channel 原封不動（plugin 看不到它們）。
    //   - 還沒 prepare、或 plugin 正被 suspend：block 原封不動（等同 bypass）。
    void process(dawlab::AudioBlock block) noexcept;

    // plugin 回報的延遲（sample 數），host 要拿來做 plugin delay compensation（PDC）。
    int getLatencySamples() const;

    // ---- 參數：一律用 0..1 的 normalized value（VST3 的規定） ---------------------------
    int getNumParameters() const;
    juce::String getParameterName(int index) const;
    // 依名稱（不分大小寫）或 parameter ID 找參數；找不到回傳 -1。
    int findParameter(const juce::String& name) const;
    float getParameterValue(int index) const;
    void setParameterValue(int index, float normalizedValue);
    // 額外：把目前的值或任意 normalized 值轉成 plugin 自己的顯示文字（例如 "-20.0 dB"）。
    juce::String getParameterText(int index) const;
    juce::String getParameterText(int index, float normalizedValue) const;
    // 額外：parameter ID（VST3 hosting 下是 plugin 內部的 ParamID 數字字串）。
    juce::String getParameterId(int index) const;

    // ---- state：plugin 自訂格式的 binary blob，host 只負責保存，不解讀 ----------------------
    juce::MemoryBlock getState();
    void setState(const juce::MemoryBlock& state);

    juce::String getName() const;
    bool hasEditor() const;
    juce::AudioPluginInstance& getInstance();
    const juce::AudioPluginInstance& getInstance() const;

    // 額外：prepare 後實際生效的設定
    double getSampleRate() const noexcept { return sampleRate_; }
    int getMaxBlockSize() const noexcept { return maxBlockSize_; }
    int getNumPluginChannels() const noexcept { return numPluginChannels_; }
    juce::String describeBusLayout() const;

private:
    juce::AudioProcessorParameter* getParam(int index) const;
    void processChunk(const dawlab::AudioBlock& block, int start, int length) noexcept;

    std::unique_ptr<juce::AudioPluginInstance> instance_;
    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;
    int numPluginChannels_ = 0; // max(總輸入 channel, 總輸出 channel)
    bool prepared_ = false;

    std::vector<float*> channelPtrs_;         // 預先配置的 channel 指標陣列（process 時只填值）
    juce::AudioBuffer<float> scratch_;        // block 沒有的 channel 用這裡的記憶體
    juce::MidiBuffer midi_;                   // 效果器不吃 MIDI，但 processBlock 介面需要一個
};

// ===========================================================================
// PluginWindow：顯示 plugin 的 editor（message thread）
// ===========================================================================
class PluginWindow : public juce::DocumentWindow {
public:
    // 建立並顯示視窗。plugin 有 editor 就用 createEditorAndMakeActive()，否則用 GenericAudioProcessorEditor。
    // 視窗不擁有 plugin：一定要在 HostedPlugin 被解構「之前」刪掉這個視窗（editor 必須先於 plugin 消失）。
    explicit PluginWindow(HostedPlugin& plugin);
    ~PluginWindow() override;

    // 按下關閉鈕時：有設定 onCloseRequested 就交給它（通常是擁有者把這個視窗 delete 掉）；
    // 沒有設定就只把視窗隱藏。兩種情況都不會刪除 plugin。
    std::function<void()> onCloseRequested;

    bool isUsingGenericEditor() const noexcept { return usingGenericEditor_; }

    void closeButtonPressed() override;

private:
    HostedPlugin& plugin_;
    bool usingGenericEditor_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWindow)
};

} // namespace dawlab::host
