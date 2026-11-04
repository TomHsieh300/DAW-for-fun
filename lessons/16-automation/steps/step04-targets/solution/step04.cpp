// Step 4 (solution): what a curve belongs to.
//
// Build and run:  l16_step04_solution   (ctest, label "solution")
// Your version:   starter/step04.cpp -> l16_step04_starter

#include "../minidaw.h"

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

void writeLanes(std::ostream& out, const Project& project) {
    int number = 0;
    for (const AutomationLaneState& lane : project.lanes) {
        ++number;
        out << "lane " << number << ' ' << channelText(lane.target.strip) << ' ' << lane.target.effect.value << ' '
            << quote(lane.target.parameter) << ' ' << modeText(lane.mode) << ' ' << lane.curve.defaultValue() << '\n';
        for (const AutomationPoint& point : lane.curve.points())
            out << "point " << number << ' ' << point.position << ' ' << point.value << ' ' << shapeText(point.shape) << '\n';
    }
}

bool parseLaneLine(const std::vector<Token>& tok, int line, std::vector<PendingLane>& lanes,
                   std::vector<PendingPoint>& points, std::string& error) {
    auto inRange = [](float v) { return v >= 0.0f && v <= 1.0f; };
    if (tok[0].text == "lane") {
        PendingLane l;
        l.line = line;
        float defaultValue = 0.0f;
        if (tok.size() != 7 || !parseNumber(tok[1], l.number) || l.number <= 0 || !parseChannel(tok[2], l.lane.target.strip) ||
            !parseNumber(tok[3], l.lane.target.effect.value) || !tok[4].quoted || tok[5].quoted)
            error = "lane needs a number, a channel, an effect id, a parameter in quotes, a mode and a default";
        else if (!parseMode(tok[5].text, l.lane.mode))
            error = "unknown mode \"" + tok[5].text + "\"";
        else if (!parseNumber(tok[6], defaultValue) || !inRange(defaultValue))
            error = "the default must be a number from 0 to 1";
        else {
            l.lane.target.parameter = tok[4].text;
            l.lane.curve = AutomationCurve(defaultValue);
            lanes.push_back(std::move(l));
            return true;
        }
        return false;
    }
    PendingPoint p;
    p.line = line;
    if (tok.size() != 5 || !parseNumber(tok[1], p.laneNumber) || tok[4].quoted)
        error = "point needs a lane number, a position, a value and a shape";
    else if (!parseNumber(tok[2], p.point.position) || p.point.position < 0)
        error = "a point's position must be 0 or more samples";
    else if (!parseNumber(tok[3], p.point.value) || !inRange(p.point.value))
        error = "a point's value must be a number from 0 to 1";
    else if (!parseShape(tok[4].text, p.point.shape))
        error = "unknown shape \"" + tok[4].text + "\"";
    else {
        points.push_back(p);
        return true;
    }
    return false;
}

bool attachLanes(Project& p, const std::vector<PendingLane>& lanes, const std::vector<PendingPoint>& points,
                 std::string& error, int& errorLine) {
    std::vector<AutomationLaneState> built;
    for (size_t i = 0; i < lanes.size(); ++i) {
        const PendingLane& l = lanes[i];
        errorLine = l.line;
        const TargetStatus status = resolve(p, l.lane.target);
        if (status == TargetStatus::NoStrip) {
            error = "lane on " + channelText(l.lane.target.strip) + ", which does not exist";
            return false;
        }
        if (status == TargetStatus::NoEffect) {
            error = "lane on effect " + std::to_string(l.lane.target.effect.value) + ", which is not on " +
                    channelText(l.lane.target.strip);
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (lanes[j].number == l.number) {
                error = "lane number " + std::to_string(l.number) + " used twice";
                return false;
            }
            if (lanes[j].lane.target == l.lane.target) {
                error = "two lanes for one parameter";
                return false;
            }
        }
        built.push_back(l.lane);
    }
    for (const PendingPoint& point : points) {
        size_t i = 0;
        while (i < lanes.size() && lanes[i].number != point.laneNumber)
            ++i;
        if (i == lanes.size()) {
            errorLine = point.line;
            error = "point for lane " + std::to_string(point.laneNumber) + ", which does not exist";
            return false;
        }
        built[i].curve.add(point.point); // in file order, so a jump keeps its order
    }
    for (AutomationLaneState& lane : built)
        insertLane(p, std::move(lane));
    return true;
}

void convertSampleRate(Project& project, double newRate) {
    if (newRate <= 0.0 || newRate == project.sampleRate)
        return;
    const double ratio = newRate / project.sampleRate;
    for (AutomationLaneState& lane : project.lanes)
        for (AutomationPoint& point : lane.curve.editPoints()) // rounding never reverses two positions
            point.position = std::llround(static_cast<double>(point.position) * ratio);
    project.sampleRate = newRate;
}

} // namespace minidaw

#include "../check.h"
