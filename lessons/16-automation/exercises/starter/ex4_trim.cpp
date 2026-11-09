// Exercise 4 (starter): trim - move a curve up and down instead of replacing it.
//
// Fill in the two TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex4_trim.cpp. What the functions must do is in ../ex4_trim.h.

#include "../ex4_trim.h"

namespace minidaw {

AutomationCurve applyTrim(const AutomationCurve& old, const std::vector<TrimPoint>& pass) {
    // TODO:
    //   1. the break positions: every pass position, and every old point strictly between the pass's
    //      first and last position; sorted, each once;
    //   2. a new curve (old.defaultValue()) with, in this order: the old points before `first`;
    //      {first, valueBefore(old, first)}; per break x: offsetAt(pass, x) added to
    //      valueBefore(old, x) (only if it differs from old.valueAt(x)) and to old.valueAt(x);
    //      {last, old.valueAt(last), shapeAt(old, last)}; the old points after `last`.
    //      add() clamps every value to 0..1.
    (void)pass;
    return old;
}

std::optional<Change> trimChange(const Project& project, const AutomationTarget& target, const std::vector<TrimPoint>& pass) {
    // TODO: no lane (findLane) or fewer than two points -> nullopt; otherwise one SetLane with the
    //       lane's mode and applyTrim().
    (void)project;
    (void)target;
    (void)pass;
    return std::nullopt;
}

} // namespace minidaw

#include "../ex4_check.h"
