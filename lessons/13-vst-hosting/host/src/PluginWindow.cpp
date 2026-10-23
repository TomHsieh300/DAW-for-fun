// PluginWindow：顯示 plugin 的 editor（VST3 的 IPlugView 由 JUCE 包成 AudioProcessorEditor）。
// 全部在 message thread 執行。

#include <dawlab/host/PluginHost.h>

namespace dawlab::host {

namespace {

juce::AudioProcessorEditor* createEditorFor(juce::AudioPluginInstance& instance, bool& usedGeneric) {
    usedGeneric = false;
    // createEditorAndMakeActive()：建立 editor 並讓 getActiveEditor() 記住它（JUCE 8.0.13 起不可直接呼叫 createEditor()）。
    // 對 VST3 而言，這一步會呼叫 IEditController::createView("editor") 取得 IPlugView，
    // 再把 plugin 的原生視窗 attach 到我們的 Component 裡。
    if (instance.hasEditor())
        if (auto* editor = instance.createEditorAndMakeActive())
            return editor;

    // 沒有 editor（或建立失敗）：JUCE 依參數列表自動產生滑桿介面
    usedGeneric = true;
    auto* generic = new juce::GenericAudioProcessorEditor(instance);
    generic->setResizeLimits(300, 200, 1600, 1600);
    return generic;
}

} // namespace

PluginWindow::PluginWindow(HostedPlugin& plugin)
    : juce::DocumentWindow(plugin.getName(),
                           juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
                           juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton),
      plugin_(plugin) {
    setUsingNativeTitleBar(true);
    auto* editor = createEditorFor(plugin_.getInstance(), usingGenericEditor_);
    // setContentOwned(..., true)：視窗擁有 editor，並依 editor 的大小調整視窗
    setContentOwned(editor, true);
    setResizable(editor->isResizable(), false);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

PluginWindow::~PluginWindow() {
    // 先刪 editor：AudioProcessorEditor 的解構會呼叫 processor.editorBeingDeleted()，
    // 所以 plugin 必須在這之後才能消失（這也是「先關視窗、再刪 plugin」規則的由來）。
    clearContentComponent();
}

void PluginWindow::closeButtonPressed() {
    if (onCloseRequested)
        onCloseRequested(); // 通常是擁有者 delete 這個視窗；之後不可以再碰 this
    else
        setVisible(false);
}

} // namespace dawlab::host
