#pragma once
// EffectMenu: the drop-down of an insert slot (Step 16).
//
//   (none)                       remove the effect
//   ---------
//   EQ, Filter, Compressor, Gate, Expander, De-esser, Transient, Saturation, Distortion,
//   Amp, Delay, Chorus, Flanger, Phaser, Tremolo, Reverb, Convolution, Limiter, Gain
//   ---------
//   VST3...                      choose a .vst3 file (Step 17)
//   DawLab Compressor (ch. 13)   only when chapter 13 was built in the same tree
//   DawLab Limiter (ch. 13)      ditto; reports a latency (PDC, Step 17)
//   ---------
//   Bypass / Open editor / Generic panel   only for an occupied slot
//
// The menu is built from data (builtinEntries()), so the order and the friendly labels are in
// one place, and Exercise 13's shortcut map can reuse them.

#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace dawlab::app {

namespace EffectMenu {

struct Entry {
    const char* label;          // what the user sees ("De-esser")
    engine::EffectType type;    // what the engine creates (EffectType::DeEsser)
};

// The 19 built-in effects in menu order.
const std::vector<Entry>& builtinEntries();

// "De-esser" for EffectType::DeEsser, etc.
juce::String labelFor(engine::EffectType type);

// Menu item IDs (PopupMenu result values).
enum ItemId : int {
    kRemove = 1,
    kFirstBuiltin = 100, // kFirstBuiltin + index into builtinEntries()
    kVst3File = 200,
    kChapter13Compressor = 201,
    kChapter13Limiter = 202,
    kToggleBypass = 300,
    kOpenEditor = 301,
    kOpenGenericPanel = 302,
};

// Builds the menu for a slot in state `slot`. `chapter13Compressor` / `chapter13Limiter` are the
// paths of chapter 13's DawLab Compressor / DawLab Limiter VST3 if they exist (empty: item not
// shown).
juce::PopupMenu build(const InsertSlotState& slot, const juce::File& chapter13Compressor,
                      const juce::File& chapter13Limiter = {});

// The chapter-13 plugin bundles compiled into this build (DAWLAB_L13_COMPRESSOR_VST3,
// DAWLAB_L13_LIMITER_VST3), if they exist on disk.
juce::File chapter13CompressorFile();
juce::File chapter13LimiterFile();

} // namespace EffectMenu

} // namespace dawlab::app
