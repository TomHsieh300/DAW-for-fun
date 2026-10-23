// Exercise 3 (starter): volume and pan automation with breakpoint envelopes.
//
// Fill in the three TODOs: AutomationLane::valueAt(), AutomationLane::render() and the
// gain loop of VolumeAutomationEffect. Until then the self-check prints FAIL (expected).
// The solution is solutions/ex3_automation.cpp.
//
// Idea
//   An automation lane is a sorted list of breakpoints (time in timeline samples, value).
//   Between two breakpoints the value is interpolated linearly:
//
//       v(t) = v_k + (v_{k+1} - v_k) * (t - t_k) / (t_{k+1} - t_k),     t_k <= t < t_{k+1}
//
//   before the first point the first value holds, after the last point the last value holds.
//   Volume is automated in dB (a linear ramp in dB sounds like an even fade), pan in -1..+1.
//
//   The lane is rendered sample-accurately by an insert effect: the Mixer tells every insert
//   which timeline segments the current block covers (PlayHead), so the effect can compute
//   v(t) for every sample even when the block wraps around a loop. Because the gain is a pure
//   function of the timeline position, the result does not depend on the block size.
//
//   render() walks the segment with a cursor instead of searching for every sample: one
//   binary search per segment (std::upper_bound) + O(1) per sample.
//
// Self-check
//   - valueAt() at, between, before and after breakpoints
//   - a DC clip through a -24 dB fade: exactly -12 dB halfway, 0 dB before, -24 dB after
//   - block size 512 vs 37 give identical output (sample accuracy)
//   - looping over the fade replays it exactly
//   - pan automation: hard left -> hard right, constant power in between (L^2 + R^2 = const)

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

using namespace dawlab;
using namespace dawlab::engine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

struct AutomationPoint {
    int64_t time = 0; // timeline samples
    float value = 0.0f;
};

class AutomationLane {
public:
    explicit AutomationLane(float defaultValue = 0.0f) : default_(defaultValue) {}

    // UI thread: insert a point, keeping the list sorted by time (a point at an existing
    // time replaces it).
    void addPoint(int64_t time, float value) {
        auto it = std::lower_bound(points_.begin(), points_.end(), time,
                                   [](const AutomationPoint& p, int64_t t) { return p.time < t; });
        if (it != points_.end() && it->time == time)
            it->value = value;
        else
            points_.insert(it, {time, value});
    }

    int getNumPoints() const noexcept { return static_cast<int>(points_.size()); }

    float valueAt(int64_t t) const noexcept {
        // TODO: return the linearly interpolated value at timeline sample t.
        //   - no points: default_
        //   - t before the first point / after the last point: that point's value
        //   - otherwise find the two neighbours (std::upper_bound) and interpolate
        (void)t;
        return default_;
    }

    // out[i] = v(start + i), i = 0..n-1. Audio thread safe (no allocation): one search, then a
    // cursor that only moves forward.
    void render(int64_t start, float* out, int n) const noexcept {
        // TODO: out[i] = valueAt(start + i) for i = 0..n-1, but without a binary search per
        // sample: search once, then walk forward with an iterator ("cursor").
        for (int i = 0; i < n; ++i)
            out[i] = valueAt(start + i);
    }

private:
    std::vector<AutomationPoint> points_;
    float default_;
};

// Base for the two automation inserts: renders the lane for the current block into a
// pre-allocated buffer, honouring the timeline segments. When the transport is stopped the
// last value is held (like every DAW).
class AutomationEffectBase : public EffectBase {
public:
    // The lane is immutable once the effect is in the engine. To edit it, build a new effect
    // with the new lane and swap it in with Engine::replaceInsert().
    AutomationEffectBase(std::shared_ptr<const AutomationLane> lane, float hold)
        : EffectBase(nullptr, 0), lane_(std::move(lane)), hold_(hold) {}

    void prepare(double sr, int maxBlock, int nch) override {
        (void)nch;
        values_.assign(static_cast<size_t>(std::max(1, maxBlock)), hold_);
        finishPrepare(sr);
    }
    void reset() noexcept override {}
    void setPlayHead(const PlayHead& ph) noexcept override { playHead_ = ph; }

protected:
    void applyParameter(int, float) noexcept override {}

    // Fills values_[0..n) for the current block.
    const float* renderValues(int n) noexcept {
        float* v = values_.data();
        if (!playHead_.isPlaying() || !lane_) {
            std::fill_n(v, n, hold_);
            return v;
        }
        for (int s = 0; s < playHead_.numSegments; ++s) {
            const TimelineSegment& seg = playHead_.segments[s];
            const int len = std::min(seg.numSamples, n - seg.blockOffset);
            if (len > 0)
                lane_->render(seg.timelineStart, v + seg.blockOffset, len);
        }
        hold_ = v[n - 1];
        return v;
    }

private:
    std::shared_ptr<const AutomationLane> lane_;
    std::vector<float> values_;
    PlayHead playHead_;
    float hold_;
};

// Volume automation: lane values are dB.  g = 10^(dB/20)
class VolumeAutomationEffect final : public AutomationEffectBase {
public:
    explicit VolumeAutomationEffect(std::shared_ptr<const AutomationLane> lane)
        : AutomationEffectBase(std::move(lane), 0.0f) {}
    const char* getName() const noexcept override { return "VolumeAutomation"; }

protected:
    void processDsp(AudioBlock block) noexcept override {
        const int n = block.getNumSamples();
        const float* db = renderValues(n);
        // TODO: multiply every channel of sample i by dbToGain(db[i]).
        (void)db;
    }
};

// Pan automation: lane values are -1..+1, constant-power law (chapter 01 Panner::computeGains).
class PanAutomationEffect final : public AutomationEffectBase {
public:
    explicit PanAutomationEffect(std::shared_ptr<const AutomationLane> lane)
        : AutomationEffectBase(std::move(lane), 0.0f) {}
    const char* getName() const noexcept override { return "PanAutomation"; }

protected:
    void processDsp(AudioBlock block) noexcept override {
        if (block.getNumChannels() < 2)
            return;
        const int n = block.getNumSamples();
        const float* pan = renderValues(n);
        float* l = block.getChannelPointer(0);
        float* r = block.getChannelPointer(1);
        for (int i = 0; i < n; ++i) {
            float gl = 0.0f, gr = 0.0f;
            dsp::Panner::computeGains(pan[i], dsp::PanLaw::ConstantPower, gl, gr);
            l[i] *= gl;
            r[i] *= gr;
        }
    }
};

std::shared_ptr<const AudioBuffer> makeDc(float v, int n) {
    auto b = std::make_shared<AudioBuffer>(1, n);
    for (int i = 0; i < n; ++i)
        b->setSample(0, i, v);
    return b;
}

// Track 1: DC 1.0 for 3 s, volume fade 0 dB (0.5 s) -> -24 dB (1.5 s), centre pan.
// The strip's own pan is set to Balance mode so only the automation changes the level.
AudioBuffer renderFade(int blockSize, bool loop) {
    auto lane = std::make_shared<AutomationLane>();
    lane->addPoint(24000, 0.0f);
    lane->addPoint(72000, -24.0f);
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    e.setTrackClips(0, {Clip::fromBuffer(makeDc(1.0f, 144000), 0)});
    e.getMixer().getTrack(0).setFormat(TrackFormat::Stereo); // stereo track: balance, unity at the centre
    e.addInsert(ChannelRef::track(0), -1, std::make_unique<VolumeAutomationEffect>(lane));
    e.applyPendingCommandsOffline();
    if (!loop) {
        RenderOptions o;
        o.numSamples = 144000;
        o.blockSize = blockSize;
        return renderOffline(e, o);
    }
    // loop [0, 96000) twice
    e.getTransport().setLoopRange(0, 96000);
    e.getTransport().setLoopEnabled(true);
    e.getTransport().play();
    AudioBuffer out(2, 192000);
    for (int pos = 0; pos < 192000; pos += blockSize)
        e.processBlock(out.toBlock().getSubBlock(pos, std::min(blockSize, 192000 - pos)));
    return out;
}

} // namespace

int main() {
    // ---- the lane on its own ----
    AutomationLane lane;
    DAWLAB_CHECK_NEAR(lane.valueAt(123), 0.0, 1e-9); // empty -> default
    lane.addPoint(1000, -6.0f);
    lane.addPoint(0, 0.0f); // out of order on purpose: stays sorted
    lane.addPoint(3000, -6.0f);
    lane.addPoint(4000, 6.0f);
    DAWLAB_CHECK(lane.getNumPoints() == 4);
    DAWLAB_CHECK_NEAR(lane.valueAt(-50), 0.0, 1e-6);   // before the first point
    DAWLAB_CHECK_NEAR(lane.valueAt(500), -3.0, 1e-6);  // halfway 0 -> -6
    DAWLAB_CHECK_NEAR(lane.valueAt(2000), -6.0, 1e-6); // flat segment
    DAWLAB_CHECK_NEAR(lane.valueAt(3250), -3.0, 1e-6); // a quarter of -6 -> +6
    DAWLAB_CHECK_NEAR(lane.valueAt(9999), 6.0, 1e-6);  // after the last point
    std::vector<float> block(5000);
    lane.render(-100, block.data(), 5000);
    int mismatches = 0;
    for (int i = 0; i < 5000; ++i)
        if (std::abs(block[static_cast<size_t>(i)] - lane.valueAt(i - 100)) > 1e-5f)
            ++mismatches;
    DAWLAB_CHECK(mismatches == 0); // the cursor walk equals the per-sample search

    // ---- volume automation through the engine ----
    const AudioBuffer a = renderFade(512, false);
    const auto levelDb = [&](const AudioBuffer& b, int i) { return gainToDb(std::abs(b.getSample(0, i))); };
    std::printf("Exercise 3: volume automation 0 dB @0.5 s -> -24 dB @1.5 s\n");
    for (double t : {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0})
        std::printf("  t = %.2f s  level %7.2f dB\n", t, static_cast<double>(levelDb(a, static_cast<int>(t * kSampleRate))));
    DAWLAB_CHECK_NEAR(levelDb(a, 12000), 0.0, 1e-3);
    DAWLAB_CHECK_NEAR(levelDb(a, 48000), -12.0, 1e-3); // exactly halfway in dB
    DAWLAB_CHECK_NEAR(levelDb(a, 96000), -24.0, 1e-3);

    const AudioBuffer b = renderFade(37, false); // odd block size
    float maxDiff = 0.0f;
    for (int i = 0; i < a.getNumSamples(); ++i)
        maxDiff = std::max(maxDiff, std::abs(a.getSample(0, i) - b.getSample(0, i)));
    std::printf("  block 512 vs block 37: max |difference| = %.3g\n", static_cast<double>(maxDiff));
    DAWLAB_CHECK(maxDiff < 1e-7f);

    const AudioBuffer looped = renderFade(512, true);
    float loopDiff = 0.0f;
    for (int i = 0; i < 96000; ++i)
        loopDiff = std::max(loopDiff, std::abs(looped.getSample(0, i) - looped.getSample(0, 96000 + i)));
    std::printf("  loop pass 1 vs pass 2: max |difference| = %.3g\n", static_cast<double>(loopDiff));
    DAWLAB_CHECK(loopDiff < 1e-7f);

    // ---- pan automation: -1 at 0 s -> +1 at 1 s ----
    auto panLane = std::make_shared<AutomationLane>();
    panLane->addPoint(0, -1.0f);
    panLane->addPoint(48000, 1.0f);
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    e.setTrackClips(0, {Clip::fromBuffer(makeDc(0.5f, 60000), 0)});
    e.getMixer().getTrack(0).setFormat(TrackFormat::Stereo); // inserts on both channels, balance
    e.addInsert(ChannelRef::track(0), -1, std::make_unique<PanAutomationEffect>(panLane));
    RenderOptions o;
    o.numSamples = 60000;
    const AudioBuffer p = renderOffline(e, o);
    DAWLAB_CHECK_NEAR(p.getSample(0, 0), 0.5, 1e-5); // hard left
    DAWLAB_CHECK_NEAR(p.getSample(1, 0), 0.0, 1e-5);
    DAWLAB_CHECK_NEAR(p.getSample(0, 24000), 0.5 * 0.70711, 1e-4); // centre
    DAWLAB_CHECK_NEAR(p.getSample(1, 48000), 0.5, 1e-5);            // hard right
    float worst = 0.0f;
    for (int i = 0; i < 48000; i += 1000) {
        const float l = p.getSample(0, i), r = p.getSample(1, i);
        worst = std::max(worst, std::abs(l * l + r * r - 0.25f));
    }
    std::printf("  pan sweep: max deviation of L^2 + R^2 from 0.25 = %.3g (constant power)\n", static_cast<double>(worst));
    DAWLAB_CHECK(worst < 1e-5f);
    return dawlab::test::summary("l14_ex3");
}
