// Step 4 (starter): what a curve belongs to.
//
// Build and run:  l16_step04_starter   (prints [FAIL] until all six TODOs are done)
// Reference:      ../solution/step04.cpp, docs/step-04-targets.md ("Your turn")
//
// The model, the lookups (effectsOf, slotOf, findEffect, findLane, insertLane), the history and the
// rest of the file format are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

TargetStatus resolve(const Project& p, const AutomationTarget& target) {
    // TODO 1: no strip: NoStrip. If target.effect is valid and not on THIS strip: NoEffect. Then look
    //         for the name in effectParameters(type of the effect), or in stripParameters() for no
    //         effect: Live, or NoParameter.
    (void)p;
    (void)target;
    return TargetStatus::NoStrip;
}

std::optional<Edit> applyEdit(Project& p, const Edit& edit) {
    // TODO 2: one case per kind of edit (std::visit with one overload each, as in chapter 15, Step 5).
    //         Each returns its inverse, or nullopt with p unchanged.
    //   SetLane       strip or effect missing: nullopt. Inverse: SetLane with the old content, or none.
    //   AddEffect     strip missing, id invalid or in use: nullopt. Insert the effect, insertLane() its
    //                 lanes, nextEffectId = max(nextEffectId, id + 1).   Inverse: RemoveEffect.
    //   RemoveEffect  findEffect(). Take the effect AND every lane with target.effect == id.
    //                                                                    Inverse: AddEffect, same place.
    //   MoveEffect    findEffect(). Reorder the strip's effects. The lanes do not change.
    //                                                                    Inverse: MoveEffect back.
    //   AddTrack      id invalid or in use, or one of its effect ids in use: nullopt. Insert it, insert
    //                 its lanes, nextTrackId = max(nextTrackId, id + 1). Inverse: RemoveTrack.
    //   RemoveTrack   Take the track AND every lane on its strip.        Inverse: AddTrack, same place.
    (void)p;
    (void)edit;
    return std::nullopt;
}

void writeLanes(std::ostream& out, const Project& project) {
    // TODO 3: number the lanes 1, 2, ... For each:
    //           lane <number> <channelText> <effect id> <quote(parameter)> <modeText> <default>
    //         then for each point, in the order of the curve:
    //           point <number> <position> <value> <shapeText>
    (void)out;
    (void)project;
}

bool parseLaneLine(const std::vector<Token>& tok, int line, std::vector<PendingLane>& lanes,
                   std::vector<PendingPoint>& points, std::string& error) {
    // TODO 4: tok[0] is "lane" or "point". Check the number of tokens and each value (parseNumber,
    //         parseChannel, parseMode, parseShape; the parameter in quotes; lane number > 0; position
    //         >= 0; default and value from 0 to 1, NOT clamped). On a problem: set error, return false.
    //         Otherwise push a PendingLane or PendingPoint, with `line`.
    (void)tok;
    (void)line;
    (void)lanes;
    (void)points;
    (void)error;
    return false;
}

bool attachLanes(Project& p, const std::vector<PendingLane>& lanes, const std::vector<PendingPoint>& points,
                 std::string& error, int& errorLine) {
    // TODO 5: for each lane: resolve() NoStrip or NoEffect is an error (NoParameter is NOT); its number
    //         or its target used by an earlier lane is an error. For each point: find its lane by
    //         number or fail, and add() it to that lane's curve, in file order. Set errorLine to the
    //         line of what is wrong. Only when all is right, insertLane() the lanes into p.
    (void)p;
    (void)lanes;
    (void)points;
    (void)error;
    (void)errorLine;
    return true;
}

void convertSampleRate(Project& project, double newRate) {
    // TODO 6: newRate <= 0 or unchanged: nothing. Otherwise every point's position (editPoints()) *=
    //         newRate / sampleRate, rounded with std::llround, and project.sampleRate = newRate.
    (void)project;
    (void)newRate;
}

} // namespace minidaw

#include "../check.h"
