// Step 5 (starter): parameter description, lock-free parameter hand-over, the insert chain.
//
// Build and run:  l14_step05_starter   (prints [FAIL] until the six TODOs are done)
// Reference:      ../solution/step05.cpp, docs/step-05-insert-effects.md ("Your turn")
//
// InsertEffect, EffectBase (storage, finishPrepare), GainEffect, CompressorEffect, the factory
// and the rest of InsertChain are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

float ParameterInfo::clamp(float v) const noexcept {
    // TODO 1: NaN (v != v) -> defaultValue; std::clamp to [min, max]; round when kind != Float.
    return v;
}

float ParameterInfo::toNormalized(float v) const noexcept {
    // TODO 2: v = clamp(v); 0 if max <= min; log taper (logarithmic && min > 0):
    //         ln(v / min) / ln(max / min); otherwise (v - min) / (max - min).
    (void)v;
    return 0.0f;
}

float ParameterInfo::fromNormalized(float n) const noexcept {
    // TODO 3: n = clamp to [0, 1]; log taper: min * (max / min)^n; otherwise min + n (max - min);
    //         pass the result through clamp().
    (void)n;
    return min;
}

void EffectBase::setParameter(int index, float value) noexcept {
    // TODO 4: ignore invalid indices (isValid); values_[index].store(infos_[index].clamp(value));
    //         dirty_.fetch_or(1u << index, std::memory_order_release).
    (void)index;
    (void)value;
}

void EffectBase::applyPendingParameters() noexcept {
    // TODO 5: return if !prepared_; mask = dirty_.exchange(0u, std::memory_order_acquire);
    //         for every set bit i: applyParameter(i, values_[i].load()).
}

void InsertChain::process(AudioBlock block) noexcept {
    // TODO 6: run slots_[0 .. count_) in order, skipping effects whose isBypassed() is true.
    (void)block;
}

} // namespace minidaw

#include "../check.h"
