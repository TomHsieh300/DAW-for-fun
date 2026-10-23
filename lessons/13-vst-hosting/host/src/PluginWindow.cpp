// PluginWindow: shows the plugin's editor (the VST3 IPlugView, wrapped by JUCE as an AudioProcessorEditor).
// Everything runs on the message thread.

#include <dawlab/host/PluginHost.h>

namespace dawlab::host {

namespace {

juce::AudioProcessorEditor* createEditorFor(juce::AudioPluginInstance& instance, bool& usedGeneric) {
    usedGeneric = false;
    // createEditorAndMakeActive(): creates the editor and lets getActiveEditor() remember it (since JUCE 8.0.13 createEditor() must not be called directly).
    // For VST3 this step calls IEditController::createView("editor") to obtain an IPlugView,
    // then attaches the plugin's native window inside our Component.
    if (instance.hasEditor())
        if (auto* editor = instance.createEditorAndMakeActive())
            return editor;

    // No editor (or creation failed): JUCE auto-generates a slider UI from the parameter list
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
    // setContentOwned(..., true): the window owns the editor and resizes itself to the editor's size
    setContentOwned(editor, true);
    setResizable(editor->isResizable(), false);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

PluginWindow::~PluginWindow() {
    // Delete the editor first: the AudioProcessorEditor destructor calls processor.editorBeingDeleted(),
    // so the plugin must not disappear before this point (this is where the rule "close the window first, then delete the plugin" comes from).
    clearContentComponent();
}

void PluginWindow::closeButtonPressed() {
    if (onCloseRequested)
        onCloseRequested(); // usually the owner deletes this window; never touch this afterwards
    else
        setVisible(false);
}

} // namespace dawlab::host
