#pragma once
// SmokeTest: the command-line modes of DawLab 8 used for automated verification (Step 21).
//
//   "DawLab 8.exe" --smoke-test
//       No window, no audio device. Builds the engine and the session, loads the demo song
//       through the same code path as the GUI, checks the resampler, the device-callback path,
//       the VST3-insert path (with an in-process test plugin) and the GUI construction, renders
//       2 s offline to dawlab_output/l14_app_smoke.wav and checks it (no NaN, not silent, peak
//       below the limiter ceiling). Exit code 0 = all checks passed. Registered as a ctest.
//       The last two sections export the whole song through the GUI's own File > Export path
//       (ExportJob on a background thread, result posted back with MessageManager::callAsync)
//       and restart the "device" at 44.1 kHz (clip conversion, also posted with callAsync).
//       Those results can only arrive through the message loop, so the test finishes
//       asynchronously: runSmokeTest() returns after starting the export, and `onFinished` is
//       called later, on the message thread, with the exit code.
//
//   "DawLab 8.exe" --snapshot <file.png>
//       Builds the main window content offline, loads the demo song, plays it to bar 2 beat 3
//       through the simulated device callback so that meters, spectrum and playhead show
//       something, and saves a PNG with Component::createComponentSnapshot(). A second PNG
//       <file>_panel.png shows the auto-generated parameter panel of the drum compressor.
//
// Both run inside JUCEApplication::initialise() (Main.cpp), i.e. on the message thread.

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <memory>

namespace dawlab::app {

// Needs a running message loop after it returns (JUCEApplication::initialise() provides one).
void runSmokeTest(std::function<void(int exitCode)> onFinished);
int runSnapshot(const juce::File& pngFile);

// An in-process "plugin" for tests: a stereo delay of `latencySamples` with a gain parameter
// ("Gain", normalized 0..1, gain = 2 * value, default 0.5 = unity). It reports its latency like
// a lookahead plugin would, so it exercises the VST3 insert path and PDC without a VST3 file.
std::unique_ptr<juce::AudioPluginInstance> createTestDelayPlugin(int latencySamples);

// Number of test plugin instances alive / processBlock() calls so far (all instances).
int getTestPluginInstanceCount() noexcept;
long long getTestPluginProcessCount() noexcept;

} // namespace dawlab::app
