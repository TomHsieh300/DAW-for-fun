#pragma once
// Chapter 20, Step 4: a time-stretcher.
//
// Step 3 follows the tempo and moves the pitch with it. To change the speed and KEEP the pitch, the audio is not
// read faster or slower: it is cut into short overlapping pieces, each played at its own speed 1, and the pieces
// are put down closer together or further apart than they were taken.
//
//   OLA     Overlap-add. A piece is a WINDOW of the input, `window` samples, faded in and out by a Hann curve.
//           A new piece starts every `window / 2` samples of the OUTPUT; two Hann halves that overlap add up to
//           1, so the pieces of an unchanged signal give the signal back. For output sample n the input that
//           should be heard is at positions[n] (Step 3 says where): the piece whose middle is at output sample c
//           is taken around the input position positions[c]. At speed 0.5 the pieces are taken 256 samples
//           apart and put down 512 apart: twice as long, and every piece still plays at its own pitch.
//           But the pieces no longer FIT. Where two of them overlap, the waves of one are somewhere in the
//           waves of the other; they cancel here and add there, and a steady tone comes out rough.
//   WSOLA   Waveform-similarity OLA: let every piece slide a little, up to `search` samples either way, to where
//           it fits. What would fit perfectly is known: the input that came right after the last piece's first
//           half - its NATURAL continuation. So: among the starts candidate - search .. candidate + search, take
//           the one whose first half looks most like the natural continuation,
//                 score(d) = sum a[i] b[i] / sqrt(sum a[i]^2),   a = input from candidate + d, b = natural
//           (the correlation, divided by the size of the candidate so that a loud place does not win for being
//           loud). The piece is then at most `search` samples from where the tempo wants it, and that error
//           does not add up: the next candidate comes from positions[] again.
//   Cost    A sound that happens ONCE - a drum hit - is in several pieces, and stretching puts those pieces
//           apart: the hit is heard twice, or smeared. Squeezing can drop one. Nothing in WSOLA knows what a
//           transient is. That is what Step 3 is still for, and what makes commercial stretchers long programs.
//
// No JUCE, no threads. Header-only: the starter and the solution define the functions marked YOUR TURN.

#include <dawlab/DspMath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace minidaw {

struct StretchSettings {
    int window = 1024; // samples of a piece (even); a new piece every window / 2 output samples. 21 ms at 48 kHz
    int search = 256;  // how far a piece may slide either way; 0: plain OLA
};

// ---- provided ---------------------------------------------------------------------------------------------

// A sample of the input, 0 outside it.
inline float inputAt(const std::vector<float>& x, std::int64_t i) noexcept {
    return i >= 0 && i < static_cast<std::int64_t>(x.size()) ? x[static_cast<size_t>(i)] : 0.0f;
}

// The Hann window of `window` samples: 0 at i = 0, 1 in the middle, and hann(i) + hann(i + window / 2) = 1.
inline double hann(int i, int window) noexcept {
    return 0.5 - 0.5 * std::cos(dawlab::kTwoPi * i / window);
}

// positions[] beyond its ends: the line of its last (or first) two values goes on.
inline double positionAt(const std::vector<double>& positions, std::int64_t n) noexcept {
    const std::int64_t size = static_cast<std::int64_t>(positions.size());
    if (size == 0)
        return 0.0;
    if (size == 1)
        return positions[0] + static_cast<double>(n);
    if (n < 0)
        return positions[0] + static_cast<double>(n) * (positions[1] - positions[0]);
    if (n >= size)
        return positions[static_cast<size_t>(size - 1)] +
               static_cast<double>(n - (size - 1)) * (positions[static_cast<size_t>(size - 1)] - positions[static_cast<size_t>(size - 2)]);
    return positions[static_cast<size_t>(n)];
}

// ---- YOUR TURN (starter/step04.cpp) -----------------------------------------------------------------------

// How far the piece that wants to start at input sample `candidate` should slide to fit the piece before it,
// whose natural continuation starts at input sample `natural`: the d in [-search, search] with the best score
// over the first window / 2 samples (the formula is above; a candidate of silence scores 0). Of equal scores the
// d nearest to 0 wins, and of d and -d the positive one. If candidate == natural the answer is 0: it IS the
// natural continuation.
int bestOffset(const std::vector<float>& x, std::int64_t candidate, std::int64_t natural, const StretchSettings& settings) noexcept;

// The stretched audio: positions.size() samples; output sample n sounds like the input around positions[n], at
// the input's own pitch. Pieces of settings.window samples start at the output samples -h, 0, h, 2h ... with
// h = window / 2, as long as they reach into the output. A piece that starts at output sample s has its middle
// at c = s + h and wants to start at the input sample round(positionAt(positions, c)) - h. The first piece
// starts there; every later one at that candidate plus bestOffset(candidate, the start of the piece before + h).
// Each piece is added to the output through the Hann window.
// Empty unless window is even and at least 4, and search is not negative.
std::vector<float> stretch(const std::vector<float>& x, const std::vector<double>& positions, const StretchSettings& settings);

} // namespace minidaw
