#pragma once
// DeviceTest: what the smoke test cannot do, on a real audio device (chapter 17, Step 7).
//
//   "DawLab 18.exe" --device-test record [seconds]   records the default input for a few seconds and
//                                                   checks the take: there, sealed, the right length,
//                                                   not digital silence, nothing dropped
//   "DawLab 18.exe" --device-test hold <folder>      records into <folder> until the process is killed;
//                                                   prints "RECORDING" once 3 s are in
//   "DawLab 18.exe" --device-test recover <folder>   what the next start does after that kill: seals
//                                                   the take and puts it on a track
//   "DawLab 18.exe" --device-test latency [buffer]   plays clicks through the speaker into the
//                                                   microphone and measures where they land: the trim
//                                                   that the driver's latencies still miss. LOUD.
//
// The window is never shown: the main component is built offline and the device is opened by hand,
// so the takes go to the folder given here and not to the user's takes folder, and no demo song plays.
// Exit code 0 = passed.

#include <juce_core/juce_core.h>

namespace dawlab::app {

int runDeviceTest(const juce::StringArray& args);

} // namespace dawlab::app
