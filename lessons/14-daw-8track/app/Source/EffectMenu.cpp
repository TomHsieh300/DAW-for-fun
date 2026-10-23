#include "EffectMenu.h"

#ifndef DAWLAB_L13_COMPRESSOR_VST3
#define DAWLAB_L13_COMPRESSOR_VST3 ""
#endif
#ifndef DAWLAB_L13_LIMITER_VST3
#define DAWLAB_L13_LIMITER_VST3 ""
#endif

namespace dawlab::app::EffectMenu {

using engine::EffectType;

const std::vector<Entry>& builtinEntries() {
    static const std::vector<Entry> entries = {
        {"EQ", EffectType::ParametricEq},
        {"Filter", EffectType::Filter},
        {"Compressor", EffectType::Compressor},
        {"Gate", EffectType::Gate},
        {"Expander", EffectType::Expander},
        {"De-esser", EffectType::DeEsser},
        {"Transient", EffectType::TransientShaper},
        {"Saturation", EffectType::Saturation},
        {"Distortion", EffectType::Distortion},
        {"Amp", EffectType::AmpChain},
        {"Delay", EffectType::Delay},
        {"Chorus", EffectType::Chorus},
        {"Flanger", EffectType::Flanger},
        {"Phaser", EffectType::Phaser},
        {"Tremolo", EffectType::Tremolo},
        {"Reverb", EffectType::Reverb},
        {"Convolution", EffectType::ConvolutionReverb},
        {"Limiter", EffectType::Limiter},
        {"Gain", EffectType::Gain},
    };
    return entries;
}

juce::String labelFor(EffectType type) {
    for (const Entry& e : builtinEntries())
        if (e.type == type)
            return e.label;
    return engine::effectTypeName(type);
}

namespace {
juce::File existingFile(const char* compiledInPath) {
    const juce::String path = juce::String::fromUTF8(compiledInPath);
    if (path.isEmpty())
        return {};
    const juce::File f(path);
    return f.exists() ? f : juce::File();
}
} // namespace

juce::File chapter13CompressorFile() { return existingFile(DAWLAB_L13_COMPRESSOR_VST3); }
juce::File chapter13LimiterFile() { return existingFile(DAWLAB_L13_LIMITER_VST3); }

juce::PopupMenu build(const InsertSlotState& slot, const juce::File& chapter13Compressor,
                      const juce::File& chapter13Limiter) {
    juce::PopupMenu menu;
    const bool occupied = !slot.isEmpty();
    menu.addItem(kRemove, "(none)", occupied, !occupied);
    menu.addSeparator();
    const auto& entries = builtinEntries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const bool ticked = occupied && slot.kind == SlotKind::Builtin && slot.type == entries[i].type;
        menu.addItem(kFirstBuiltin + static_cast<int>(i), entries[i].label, true, ticked);
    }
    menu.addSeparator();
    menu.addItem(kVst3File, "VST3...");
    if (chapter13Compressor != juce::File())
        menu.addItem(kChapter13Compressor, "DawLab Compressor (chapter 13)");
    if (chapter13Limiter != juce::File())
        menu.addItem(kChapter13Limiter, "DawLab Limiter (chapter 13)");
    if (occupied) {
        menu.addSeparator();
        menu.addItem(kToggleBypass, "Bypass", true, slot.effect->isBypassed());
        menu.addItem(kOpenEditor, slot.kind == SlotKind::Vst3 ? "Open plugin editor" : "Open parameter panel");
        if (slot.kind == SlotKind::Vst3)
            menu.addItem(kOpenGenericPanel, "Open generic parameter panel");
    }
    return menu;
}

} // namespace dawlab::app::EffectMenu
