// Step 5 (solution): parameter description, lock-free parameter hand-over, the insert chain.
//
// Build and run:  l14_step05_solution   (ctest, label "solution")
// Your version:   starter/step05.cpp -> l14_step05_starter

#include "../minidaw.h"

namespace minidaw {

float ParameterInfo::clamp(float v) const noexcept {
    if (!(v == v)) // NaN: one bad value from a UI or a file must never poison a DSP state
        v = defaultValue;
    v = std::clamp(v, min, max);
    if (kind != ParameterKind::Float)
        v = std::round(v);
    return v;
}

float ParameterInfo::toNormalized(float v) const noexcept {
    v = clamp(v);
    if (max <= min)
        return 0.0f;
    if (logarithmic && min > 0.0f)
        return static_cast<float>(std::log(v / min) / std::log(max / min));
    return (v - min) / (max - min);
}

float ParameterInfo::fromNormalized(float n) const noexcept {
    n = std::clamp(n, 0.0f, 1.0f);
    if (logarithmic && min > 0.0f)
        return clamp(static_cast<float>(min * std::pow(max / min, n)));
    return clamp(min + n * (max - min));
}

void EffectBase::setParameter(int index, float value) noexcept {
    if (!isValid(index))
        return;
    values_[static_cast<size_t>(index)].store(infos_[index].clamp(value), std::memory_order_relaxed);
    dirty_.fetch_or(1u << static_cast<unsigned>(index), std::memory_order_release); // publish
}

void EffectBase::applyPendingParameters() noexcept {
    if (!prepared_)
        return; // stays dirty; finishPrepare() applies everything
    uint32_t mask = dirty_.exchange(0u, std::memory_order_acquire);
    for (int i = 0; mask != 0u; ++i, mask >>= 1u)
        if (mask & 1u)
            applyParameter(i, values_[static_cast<size_t>(i)].load(std::memory_order_relaxed));
}

void InsertChain::process(AudioBlock block) noexcept {
    for (int i = 0; i < count_; ++i) {
        InsertEffect* e = slots_[static_cast<size_t>(i)];
        if (!e->isBypassed())
            e->process(block);
    }
}

} // namespace minidaw

#include "../check.h"
