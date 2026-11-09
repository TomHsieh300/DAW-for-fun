#pragma once
// model.h - the model of Step 4, solved, for Exercise 5.
//
// Step 4's minidaw.h (the project with its tracks, effects and lanes, the edits, the History) with
// resolve() and applyEdit() defined here, exactly as in steps/step04-targets/solution/step04.cpp.
// The file functions of Step 4 are not needed and not defined. Include it in ONE source file per
// program.

#include "../steps/step04-targets/minidaw.h"

namespace minidaw {

TargetStatus resolve(const Project& p, const AutomationTarget& target) {
    const std::vector<EffectState>* effects = effectsOf(p, target.strip);
    if (effects == nullptr)
        return TargetStatus::NoStrip;
    std::vector<std::string> parameters = stripParameters();
    if (target.effect.isValid()) {
        const int slot = slotOf(*effects, target.effect); // on THIS strip
        if (slot < 0)
            return TargetStatus::NoEffect;
        parameters = effectParameters((*effects)[static_cast<size_t>(slot)].type);
    }
    return std::find(parameters.begin(), parameters.end(), target.parameter) != parameters.end() ? TargetStatus::Live
                                                                                                 : TargetStatus::NoParameter;
}

namespace {

// Takes every lane `belongs` says yes to out of the project.
template <typename Belongs>
std::vector<AutomationLaneState> takeLanes(Project& p, Belongs belongs) {
    std::vector<AutomationLaneState> taken;
    auto keep = p.lanes.begin();
    for (auto it = p.lanes.begin(); it != p.lanes.end(); ++it) {
        if (belongs(*it))
            taken.push_back(std::move(*it));
        else {
            if (keep != it)
                *keep = std::move(*it);
            ++keep;
        }
    }
    p.lanes.erase(keep, p.lanes.end());
    return taken;
}

std::optional<Edit> applyKind(Project& p, const SetLane& e) {
    const TargetStatus status = resolve(p, e.target);
    if (status == TargetStatus::NoStrip || status == TargetStatus::NoEffect)
        return std::nullopt;
    SetLane inverse{e.target, std::nullopt};
    for (auto it = p.lanes.begin(); it != p.lanes.end(); ++it)
        if (it->target == e.target) {
            inverse.content = LaneContent{it->mode, std::move(it->curve)};
            p.lanes.erase(it);
            break;
        }
    if (e.content)
        insertLane(p, AutomationLaneState{e.target, e.content->mode, e.content->curve});
    return inverse;
}

std::optional<Edit> applyKind(Project& p, const AddEffect& e) {
    std::vector<EffectState>* effects = effectsOf(p, e.strip);
    if (effects == nullptr || !e.effect.id.isValid() || findEffect(p, e.effect.id))
        return std::nullopt;
    const int index = std::clamp(e.index, 0, static_cast<int>(effects->size()));
    effects->insert(effects->begin() + index, e.effect);
    for (const AutomationLaneState& lane : e.lanes)
        insertLane(p, lane);
    p.nextEffectId = std::max(p.nextEffectId, e.effect.id.value + 1); // up only
    return RemoveEffect{e.effect.id};
}

std::optional<Edit> applyKind(Project& p, const RemoveEffect& e) {
    const std::optional<EffectPlace> place = findEffect(p, e.id);
    if (!place)
        return std::nullopt;
    std::vector<EffectState>* effects = effectsOf(p, place->strip);
    AddEffect inverse{place->strip, place->slot, (*effects)[static_cast<size_t>(place->slot)], {}};
    effects->erase(effects->begin() + place->slot);
    inverse.lanes = takeLanes(p, [&e](const AutomationLaneState& lane) { return lane.target.effect == e.id; });
    return inverse;
}

std::optional<Edit> applyKind(Project& p, const MoveEffect& e) {
    const std::optional<EffectPlace> place = findEffect(p, e.id);
    if (!place)
        return std::nullopt;
    std::vector<EffectState>& effects = *effectsOf(p, place->strip);
    const int to = std::clamp(e.index, 0, static_cast<int>(effects.size()) - 1);
    EffectState effect = std::move(effects[static_cast<size_t>(place->slot)]);
    effects.erase(effects.begin() + place->slot);
    effects.insert(effects.begin() + to, std::move(effect));
    return MoveEffect{e.id, place->slot};
}

std::optional<Edit> applyKind(Project& p, const AddTrack& e) {
    if (!e.track.id.isValid() || indexOf(p, e.track.id) >= 0)
        return std::nullopt;
    for (const EffectState& effect : e.track.effects)
        if (findEffect(p, effect.id))
            return std::nullopt;
    const int index = std::clamp(e.index, 0, static_cast<int>(p.tracks.size()));
    p.tracks.insert(p.tracks.begin() + index, e.track);
    for (const AutomationLaneState& lane : e.lanes)
        insertLane(p, lane);
    p.nextTrackId = std::max(p.nextTrackId, e.track.id.value + 1); // up only
    return RemoveTrack{e.track.id};
}

std::optional<Edit> applyKind(Project& p, const RemoveTrack& e) {
    const int index = indexOf(p, e.id);
    if (index < 0)
        return std::nullopt;
    const StripRef strip = StripRef::ofTrack(e.id);
    AddTrack inverse{p.tracks[static_cast<size_t>(index)], index, {}};
    inverse.lanes = takeLanes(p, [&strip](const AutomationLaneState& lane) { return lane.target.strip == strip; });
    p.tracks.erase(p.tracks.begin() + index);
    return inverse;
}

} // namespace

std::optional<Edit> applyEdit(Project& p, const Edit& edit) {
    return std::visit([&p](const auto& e) { return applyKind(p, e); }, edit);
}

} // namespace minidaw
