#pragma once
// Exercise 5 - Tell the user what is wrong with the input.
//
// The first real recording of DawLab 17 (Step 7, "On a real device") was 3 seconds of exact zeros: the
// microphone was muted in Windows. The take was there, in place, sealed - and silent, and nothing said
// so. After unmuting, the input peaked at -64 dBFS. Both are things a DAW should say, and so is the
// opposite: an input that clips.
//
// InputHealth watches what the disk thread writes (it sees every frame, and it may take its time):
//
//   - every sample exactly 0, the whole take       the input is muted or blocked
//   - a stretch of exact zeros of 0.1 s or more    the input stopped in the middle (a cable, a USB
//     in a take that is not all zeros               interface that went away)
//   - a peak below -50 dBFS                         the input gain is far too low
//   - kClipRun or more samples in a row at full     clipping: the converter ran out of range. One
//     scale (|x| >= 0.999), of one sign, on one      sample at full scale is a loud peak, not a clip,
//     channel                                        and a run may cross the edge of a block
//
// add() runs on the disk thread for every block it writes: no allocation, no locks. warnings() is for
// the UI, after the take.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ex5 {

class InputHealth {
public:
    static constexpr int kMaxChannels = 64;
    static constexpr float kFullScale = 0.999f;
    static constexpr int kClipRun = 3;
    static constexpr float kQuietDb = -50.0f;
    static constexpr double kGapSeconds = 0.1;

    // Before a take.
    void reset(double sampleRate) noexcept;
    // ---- YOUR TURN (starter/ex5_input_health.cpp) -----------------------------------------------
    // Disk thread: the next n frames of the take, numChannels channels (at most kMaxChannels are looked at).
    void add(const float* const* channels, int numChannels, int n) noexcept;
    // UI: what to tell the user, one sentence each, in the order of the list above. Empty: all is well.
    // The sentences must contain: "exactly 0" / "stopped" and the gap in seconds with 3 decimals ("0.200 s")
    // / "quiet" and the peak in dBFS with one decimal ("-64.4 dBFS") / "clipped", the number of clips and
    // the longest one in samples.
    std::vector<std::string> warnings() const;

    // ---- provided: what add() found -------------------------------------------------------------
    std::int64_t frames() const noexcept { return frames_; }
    float peak() const noexcept { return peak_; }
    std::int64_t zeroFrames() const noexcept { return zeroFrames_; }     // frames with every channel exactly 0
    std::int64_t longestGap() const noexcept { return longestGap_; }     // the longest run of such frames, not at
                                                                         // the take's start or end
    int clips() const noexcept { return clips_; }                         // runs of kClipRun or more
    int longestClip() const noexcept { return longestClip_; }

private:
    double sampleRate_ = 48000.0;
    std::int64_t frames_ = 0;
    float peak_ = 0.0f;
    std::int64_t zeroFrames_ = 0;
    std::int64_t zeroRun_ = 0;      // the current run of all-zero frames
    bool heardSomething_ = false;   // a frame that was not all zeros came already
    std::int64_t longestGap_ = 0;
    int clips_ = 0;
    int longestClip_ = 0;
    std::array<int, kMaxChannels> run_{};   // per channel: the current run at full scale, signed by its sign
};

} // namespace ex5
