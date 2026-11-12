#pragma once
// FollowRenderer: clips that follow the tempo, rendered ahead of time (chapter 20, Step 5).
//
// Reading through a sinc and stretching with WSOLA cost far more than playing a clip, and a stretcher works on
// pieces of 20 ms. On the audio thread that would be latency to report and a cost to pay in every block, for
// every clip, in the one thread that must never be late. So it is done before: a WORKER thread turns a
// FollowClip into a plain Clip (Warp.h), and the UI thread hands that clip to the engine like any other. The
// audio thread and renderClip() do not change at all.
//
//   UI thread                         worker thread                    audio thread
//   ---------                         -------------                    ------------
//   tempo map or a clip changes
//   FollowTrack::update()
//     placeholders to the engine  ----------------------------------->  plays them
//     FollowRenderer::request()  -->  renderFollowClip()
//   (a timer)                         ... done
//   FollowRenderer::collect()    <--  result
//   FollowTrack::take()
//     the renders to the engine  ------------------------------------>  plays them
//
// The price is the time in between. Until its render is ready a clip plays its PLACEHOLDER: the last render,
// moved to where the clip now starts. A request that a newer one overtakes is dropped - cancelled if the worker
// is at it - and its result, should it still arrive, is not taken: every request has a generation, and only the
// newest of a key counts. A render that cannot be made (longer than kMaxRenderSamples, or no memory for it) comes
// back as a Result that is not ok: its clip goes on playing the placeholder, and the UI can say so.
//
// The two classes here are not for the audio thread. FollowRenderer is safe to use from one thread at a time
// next to its own worker; FollowTrack belongs to the UI thread alone.

#include "Warp.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <new>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dawlab::engine {

class FollowRenderer {
public:
    using Key = std::uint64_t;

    struct Result {
        Key key = 0;
        std::uint64_t generation = 0;
        Clip clip;
        bool ok = true; // false: the render could not be made (longer than kMaxRenderSamples, or no memory for it)
    };

    FollowRenderer() : worker_([this] { run(); }) {}
    ~FollowRenderer() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
            cancel_.store(true, std::memory_order_relaxed);
        }
        wake_.notify_all();
        worker_.join();
    }
    FollowRenderer(const FollowRenderer&) = delete;
    FollowRenderer& operator=(const FollowRenderer&) = delete;

    // A key nobody else has: one per clip that is rendered.
    Key newKey() noexcept { return nextKey_.fetch_add(1, std::memory_order_relaxed); }

    // Asks for the render of `clip` under `map`. An older request of the same key that still waits is replaced,
    // one the worker is at is cancelled. Returns the request's generation.
    std::uint64_t request(Key key, FollowClip clip, const TempoMap& map, double sampleRate) {
        std::uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            generation = ++newest_[key];
            for (auto it = waiting_.begin(); it != waiting_.end();)
                it = it->key == key ? waiting_.erase(it) : it + 1;
            if (busy_ && current_ == key)
                cancel_.store(true, std::memory_order_relaxed);
            waiting_.push_back(Job{key, generation, std::move(clip), map, sampleRate});
        }
        wake_.notify_all();
        return generation;
    }

    // Nothing of this key is wanted any more (its clip is gone).
    void forget(Key key) {
        std::lock_guard<std::mutex> lock(mutex_);
        newest_.erase(key);
        for (auto it = waiting_.begin(); it != waiting_.end();)
            it = it->key == key ? waiting_.erase(it) : it + 1;
        if (busy_ && current_ == key)
            cancel_.store(true, std::memory_order_relaxed);
    }

    // The renders that are done and still the newest of their key. Each is given once.
    std::vector<Result> collect() {
        std::vector<Result> done;
        std::lock_guard<std::mutex> lock(mutex_);
        for (Result& r : finished_) {
            const auto it = newest_.find(r.key);
            if (it != newest_.end() && it->second == r.generation)
                done.push_back(std::move(r));
        }
        finished_.clear();
        return done;
    }

    // True if no request waits and the worker is not at one.
    bool isIdle() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return waiting_.empty() && !busy_;
    }

    // How many renders the worker has begun, and how many of them it finished (the others were cancelled).
    // What a user would notice as a fan that does not come on: for tests.
    int getNumRendersStarted() const noexcept { return started_.load(std::memory_order_relaxed); }
    int getNumRendersFinished() const noexcept { return completed_.load(std::memory_order_relaxed); }

    // Blocks until isIdle(): for tests, and for an export that must not go out with placeholders.
    void waitUntilIdle() {
        std::unique_lock<std::mutex> lock(mutex_);
        idle_.wait(lock, [this] { return waiting_.empty() && !busy_; });
    }

private:
    struct Job {
        Key key = 0;
        std::uint64_t generation = 0;
        FollowClip clip;
        TempoMap map;
        double sampleRate = 48000.0;
    };

    void run() {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            wake_.wait(lock, [this] { return stop_ || !waiting_.empty(); });
            if (stop_)
                return;
            Job job = std::move(waiting_.front());
            waiting_.pop_front();
            busy_ = true;
            current_ = job.key;
            cancel_.store(false, std::memory_order_relaxed);
            started_.fetch_add(1, std::memory_order_relaxed);
            lock.unlock();

            // A render that is not done was cancelled - a newer request follows, or nobody wants it - or it cannot
            // be made: that comes back too, as one that failed, or its track would wait for it for ever. An
            // exception must not leave this thread: it would end the program.
            Result result{job.key, job.generation, {}};
            bool done = false;
            try {
                done = renderFollowClip(job.clip, job.map, job.sampleRate, result.clip, &cancel_);
                result.ok = done || cancel_.load(std::memory_order_relaxed);
            } catch (const std::bad_alloc&) {
                result.ok = false;
            }
            job = Job{}; // the clip's buffers are let go outside the lock

            lock.lock();
            busy_ = false;
            if (done)
                completed_.fetch_add(1, std::memory_order_relaxed);
            if ((done || !result.ok) && !stop_)
                finished_.push_back(std::move(result));
            if (waiting_.empty())
                idle_.notify_all();
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable wake_, idle_;
    std::deque<Job> waiting_;
    std::vector<Result> finished_;
    std::unordered_map<Key, std::uint64_t> newest_; // the newest generation asked for, by key
    bool busy_ = false;
    bool stop_ = false;
    Key current_ = 0;
    std::atomic<bool> cancel_{false};
    std::atomic<Key> nextKey_{1};
    std::atomic<int> started_{0}, completed_{0};
    std::thread worker_; // last: it starts in the constructor, when everything above exists
};

// One track's clips as the user means them, and what the engine plays for them (UI thread).
class FollowTrack {
public:
    explicit FollowTrack(FollowRenderer& renderer) : renderer_(renderer) {}
    ~FollowTrack() {
        for (const Entry& e : entries_)
            renderer_.forget(e.key);
    }
    FollowTrack(const FollowTrack&) = delete;
    FollowTrack& operator=(const FollowTrack&) = delete;

    // The track's clips, or a new tempo map (or both): Off and Start clips are placed at once; a warped clip
    // gets its placeholder and a request for its render. A warped clip that is the same as before (the same
    // source, range, mode and markers, in the same place of the list) keeps its last render as the placeholder -
    // and if the map is the same too, nothing is rendered again: a new gain or fade costs nothing.
    void update(std::vector<FollowClip> clips, const TempoMap& map, double sampleRate) {
        const bool mapIsTheSame = hasMap_ && sampleRate == sampleRate_ && sameMap(map, map_);
        std::vector<Entry> next;
        next.reserve(clips.size());
        for (size_t i = 0; i < clips.size(); ++i) {
            Entry e;
            const bool same = i < entries_.size() && sameAudio(entries_[i].clip, clips[i]);
            if (same) {
                e = std::move(entries_[i]);
                entries_[i].key = 0; // taken over
            } else {
                e.key = renderer_.newKey();
            }
            e.clip = std::move(clips[i]);
            if (e.clip.isWarped() && e.clip.clip.source) {
                if (same && mapIsTheSame && (e.waiting || e.hasRender || e.failed)) {
                    carryOver(e.clip.clip, e.playing); // the render, the request on its way, or the failure still holds
                } else {
                    e.playing = placeholderFor(e.clip, e.hasRender ? &e.rendered : nullptr, map, sampleRate);
                    carryOver(e.clip.clip, e.playing);
                    e.generation = renderer_.request(e.key, e.clip, map, sampleRate);
                    e.waiting = true;
                    e.failed = false;
                }
            } else {
                renderFollowClip(e.clip, map, sampleRate, e.playing); // Off, Start: no buffer to make
                e.waiting = false;
                e.hasRender = false;
                e.failed = false;
            }
            next.push_back(std::move(e));
        }
        for (const Entry& old : entries_)
            if (old.key != 0)
                renderer_.forget(old.key);
        entries_ = std::move(next);
        map_ = map;
        sampleRate_ = sampleRate;
        hasMap_ = true;
    }

    // Results of FollowRenderer::collect(): takes those that are this track's and still wanted. True if what the
    // engine should play has changed - then hand playable() to Engine::setTrackClips() - or a render failed: the
    // clip then goes on playing its placeholder, and getNumFailed() counts it.
    bool take(const std::vector<FollowRenderer::Result>& results) {
        bool changed = false;
        for (const FollowRenderer::Result& r : results)
            for (Entry& e : entries_) {
                if (e.key != r.key || !e.waiting || e.generation != r.generation)
                    continue;
                e.waiting = false;
                e.failed = !r.ok;
                changed = true;
                if (!r.ok)
                    continue;
                e.rendered = r.clip;
                e.hasRender = true;
                e.playing = r.clip;
                carryOver(e.clip.clip, e.playing);
            }
        return changed;
    }

    // What the engine plays now: renders where they are ready, placeholders where they are not.
    std::vector<Clip> playable() const {
        std::vector<Clip> clips;
        clips.reserve(entries_.size());
        for (const Entry& e : entries_)
            clips.push_back(e.playing);
        return clips;
    }

    // True if no clip of the track plays a placeholder.
    bool isUpToDate() const noexcept {
        for (const Entry& e : entries_)
            if (e.waiting)
                return false;
        return true;
    }

    // Clips whose render under the map of now could not be made: they play their placeholder.
    int getNumFailed() const noexcept {
        int failed = 0;
        for (const Entry& e : entries_)
            failed += e.failed ? 1 : 0;
        return failed;
    }

    int getNumClips() const noexcept { return static_cast<int>(entries_.size()); }
    const FollowClip& getClip(int i) const noexcept { return entries_[static_cast<size_t>(i)].clip; }

private:
    struct Entry {
        FollowClip clip;
        FollowRenderer::Key key = 0;
        std::uint64_t generation = 0;
        bool waiting = false;   // a render is on its way; `playing` is a placeholder
        bool hasRender = false; // `rendered` is a render of this clip under some map
        bool failed = false;    // its render under the map of now could not be made; `playing` is a placeholder
        Clip rendered;
        Clip playing;
    };

    // The same audio, read the same way: a render of one is a fair placeholder for the other.
    static bool sameAudio(const FollowClip& a, const FollowClip& b) noexcept {
        return a.clip.source == b.clip.source && a.clip.sourceOffset == b.clip.sourceOffset && a.clip.length == b.clip.length && a.mode == b.mode &&
               a.markers == b.markers;
    }
    static bool sameMap(const TempoMap& a, const TempoMap& b) noexcept {
        if (a.size() != b.size())
            return false;
        for (int i = 0; i < a.size(); ++i)
            if (a.point(i).tick != b.point(i).tick || a.point(i).bpm != b.point(i).bpm || a.point(i).rampToNext != b.point(i).rampToNext)
                return false;
        return true;
    }
    // What does not need a new render: gain, fades, mute and name are the clip's of now.
    static void carryOver(const Clip& from, Clip& to) {
        to.gainDb = from.gainDb;
        to.fadeInSamples = from.fadeInSamples;
        to.fadeOutSamples = from.fadeOutSamples;
        to.fadeShape = from.fadeShape;
        to.muted = from.muted;
        to.name = from.name;
    }

    FollowRenderer& renderer_;
    std::vector<Entry> entries_;
    TempoMap map_; // the map of the last update()
    double sampleRate_ = 0.0;
    bool hasMap_ = false;
};

} // namespace dawlab::engine
