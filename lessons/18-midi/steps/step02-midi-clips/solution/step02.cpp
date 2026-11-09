// Chapter 18, Step 2 (solution): playing a MIDI clip.
//
// Build and run:  l18_step02_solution   (ctest, label "solution")
// Your version:   starter/step02.cpp -> l18_step02_starter

#include "../minidaw.h"

namespace minidaw {

bool MidiClip::place(const Note& n, std::int64_t& on, std::int64_t& off) const noexcept {
    if (n.start < offset || n.start >= offset + length)
        return false;
    on = timelineTick(n.start);
    off = std::min(timelineTick(n.start + n.length), end());
    return true;
}

void ClipPlayer::allNotesOff(int offset, std::vector<BlockEvent>& out) noexcept {
    for (int i = 0; i < numSounding_; ++i)
        out.push_back({offset, MidiMessage::noteOff(sounding_[static_cast<size_t>(i)].channel, sounding_[static_cast<size_t>(i)].pitch)});
    numSounding_ = 0;
}

void ClipPlayer::chase(const std::vector<MidiClip>& clips, std::int64_t tick, int offset, std::vector<BlockEvent>& out) const noexcept {
    for (const MidiClip& clip : clips) {
        if (clip.muted || tick < clip.start || tick >= clip.end())
            continue;
        const std::int64_t material = tick - clip.start + clip.offset;
        std::array<std::int16_t, 16 * 128> last; // per channel and number: the value, or -1
        last.fill(-1);
        for (const ControllerPoint& p : clip.controllers) {
            if (p.tick >= material)
                break; // sorted; a point at `tick` itself is the segment's to send
            last[static_cast<size_t>((p.channel & 15) * 128 + (p.number & 127))] = static_cast<std::int16_t>(p.value);
        }
        for (size_t i = 0; i < last.size(); ++i)
            if (last[i] >= 0)
                out.push_back({offset, MidiMessage::controller(static_cast<int>(i / 128), static_cast<int>(i % 128), last[i])});
    }
}

void ClipPlayer::process(const std::vector<MidiClip>& clips, const Block& block, const Tempo& tempo, std::vector<BlockEvent>& out) noexcept {
    out.clear();
    if (!block.playing) {
        allNotesOff(0, out); // nothing if nothing sounds
        return;
    }
    for (int s = 0; s < block.numSegments; ++s) {
        const Segment& seg = block.segments[s];
        if (seg.jump) {
            allNotesOff(seg.offset, out);
            chase(clips, firstTickAtOrAfter(seg.timelineSample, tempo), seg.offset, out);
        }
        // Notes whose clip has gone or been muted end now.
        for (int i = 0; i < numSounding_;) {
            const Sounding& n = sounding_[static_cast<size_t>(i)];
            const auto clip = std::find_if(clips.begin(), clips.end(), [&n](const MidiClip& c) { return c.id == n.clipId; });
            if (clip == clips.end() || clip->muted) {
                out.push_back({seg.offset, MidiMessage::noteOff(n.channel, n.pitch)});
                sounding_[static_cast<size_t>(i)] = sounding_[static_cast<size_t>(--numSounding_)];
            } else {
                ++i;
            }
        }

        const std::int64_t t0 = firstTickAtOrAfter(seg.timelineSample, tempo);
        const std::int64_t t1 = firstTickAtOrAfter(seg.timelineSample + seg.numSamples, tempo);
        scratch_.clear();
        int seq = 0;
        // Room is kept for a note off of every sounding note: a note off may never be lost.
        auto room = [this] { return scratch_.size() + kMaxSounding < scratch_.capacity(); };
        auto pend = [this, &seq](std::int64_t tick, int rank, MidiMessage m) { scratch_.push_back({tick, rank, seq++, m}); };
        for (const MidiClip& clip : clips) {
            if (clip.muted || clip.end() <= t0 || clip.start >= t1)
                continue;
            // The notes are sorted by start: begin at the first one that can start in this segment.
            const std::int64_t first = t0 - clip.start + clip.offset;
            auto it = std::lower_bound(clip.notes.begin(), clip.notes.end(), first, [](const Note& n, std::int64_t t) { return n.start < t; });
            for (; it != clip.notes.end() && clip.timelineTick(it->start) < t1; ++it) {
                std::int64_t on = 0, off = 0;
                if (!clip.place(*it, on, off))
                    continue;
                if (numSounding_ == kMaxSounding || !room()) {
                    ++dropped_;
                    continue;
                }
                sounding_[static_cast<size_t>(numSounding_++)] = {clip.id, it->channel, it->pitch, off};
                pend(on, 2, MidiMessage::noteOn(it->channel, it->pitch, it->velocity));
            }
            for (const ControllerPoint& p : clip.controllers) {
                const std::int64_t t = clip.timelineTick(p.tick);
                if (p.tick < clip.offset || p.tick >= clip.offset + clip.length || t < t0 || t >= t1)
                    continue;
                if (room())
                    pend(t, 1, MidiMessage::controller(p.channel, p.number, p.value));
                else
                    ++droppedEvents_;
            }
        }
        // Note offs in this segment, also of the notes just started.
        for (int i = 0; i < numSounding_;) {
            const Sounding& n = sounding_[static_cast<size_t>(i)];
            if (n.offTick < t1) {
                pend(std::max(n.offTick, t0), 0, MidiMessage::noteOff(n.channel, n.pitch));
                sounding_[static_cast<size_t>(i)] = sounding_[static_cast<size_t>(--numSounding_)];
            } else {
                ++i;
            }
        }
        std::sort(scratch_.begin(), scratch_.end(), [](const Pending& a, const Pending& b) {
            if (a.tick != b.tick)
                return a.tick < b.tick;
            if (a.rank != b.rank)
                return a.rank < b.rank;
            return a.seq < b.seq;
        });
        for (const Pending& p : scratch_)
            out.push_back({static_cast<int>(sampleOfTick(p.tick, tempo) - seg.timelineSample) + seg.offset, p.message});
    }
}

} // namespace minidaw

#include "../check.h"
