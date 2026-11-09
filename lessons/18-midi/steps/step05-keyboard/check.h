#pragma once
// check.h - the self-check of Step 5 (a keyboard). Defines main(); included at the END of starter/step05.cpp
// and solution/step05.cpp, so both run exactly the same checks.
//
// The parser byte by byte, the FIFO on one thread and on two (build it with -fsanitize=thread as well), and
// the timing: a simulated keyboard against a simulated audio device, measuring how late each note is heard.

#include <dawlab/Common.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

namespace step05_check {

using namespace minidaw;

inline std::vector<MidiMessage> parse(MidiParser& p, const std::vector<int>& bytes) {
    std::vector<MidiMessage> out;
    MidiMessage m;
    for (const int b : bytes)
        if (p.feed(static_cast<std::uint8_t>(b), m))
            out.push_back(m);
    return out;
}

inline void checkParser() {
    MidiParser p;
    // Running status: one status byte, three notes - the last a note off as velocity 0.
    std::vector<MidiMessage> m = parse(p, {0x90, 0x3C, 0x64, 0x3E, 0x64, 0x40, 0x00});
    DAWLAB_CHECK(m.size() == 3);
    if (m.size() == 3) {
        DAWLAB_CHECK(m[0] == MidiMessage::noteOn(0, 60, 100) && m[1] == MidiMessage::noteOn(0, 62, 100));
        DAWLAB_CHECK(m[2].kind() == Kind::NoteOff && m[2].note() == 64);
    }
    // A clock byte inside a message, and a stop: nothing changes.
    m = parse(p, {0x91, 0x3C, 0xF8, 0x64, 0xFC, 0x3E, 0xF8, 0x50});
    DAWLAB_CHECK(m.size() == 2 && m[0] == MidiMessage::noteOn(1, 60, 100) && m[1] == MidiMessage::noteOn(1, 62, 80));
    // One data byte: program change, with running status.
    m = parse(p, {0xC2, 0x05, 0x06});
    DAWLAB_CHECK(m.size() == 2 && m[0].status == 0xC2 && m[0].data1 == 5 && m[0].data2 == 0 && m[1].data1 == 6);
    // Channel pressure has one data byte too; a controller two.
    m = parse(p, {0xD0, 0x40, 0xB0, 0x40, 0x7F});
    DAWLAB_CHECK(m.size() == 2 && m[0].status == 0xD0 && m[1] == MidiMessage::controller(0, 64, 127));
    const long before = p.skipped();
    // A SysEx dump of any length is skipped, and ends the running status: the data after it is skipped.
    m = parse(p, {0xF0, 0x7E, 0x00, 0x09, 0x01, 0xF7, 0x3C, 0x40});
    DAWLAB_CHECK(m.empty() && p.skipped() == before + 2);
    // A real-time byte inside a SysEx does not end it.
    m = parse(p, {0xF0, 0x41, 0xF8, 0x10, 0x42, 0xF7, 0x90, 0x30, 0x40});
    DAWLAB_CHECK(m.size() == 1 && m[0] == MidiMessage::noteOn(0, 48, 64) && p.skipped() == before + 2);
    // System common (an MTC quarter frame) ends the running status; its data byte is skipped.
    m = parse(p, {0x90, 0x3C, 0x40, 0xF1, 0x05, 0x3C, 0x40});
    DAWLAB_CHECK(m.size() == 1 && p.skipped() == before + 5);
    // A new status in the middle of a message starts over: the 0x3C before it is forgotten.
    m = parse(p, {0x90, 0x3C, 0x80, 0x3E, 0x00, 0x40, 0x00});
    DAWLAB_CHECK(m.size() == 2 && m[0] == MidiMessage::noteOff(0, 0x3E) && m[1] == MidiMessage::noteOff(0, 0x40));
    // A status byte ends a SysEx that never got its 0xF7 (a cable pulled, a dump cut short).
    m = parse(p, {0xF0, 0x7E, 0x01, 0x92, 0x3C, 0x40});
    DAWLAB_CHECK(m.size() == 1 && m[0] == MidiMessage::noteOn(2, 60, 64));
    // Data before any status at all.
    MidiParser fresh;
    m = parse(fresh, {0x3C, 0x40, 0x90, 0x3C, 0x40});
    DAWLAB_CHECK(m.size() == 1 && fresh.skipped() == 2);
}

inline TimedMidi numbered(long i, double t = 0.0) {
    return {t, {0x90, static_cast<std::uint8_t>(i % 128), static_cast<std::uint8_t>((i / 128) % 128)}};
}
inline long numberOf(const TimedMidi& m) { return m.message.data1 + 128L * m.message.data2; }

inline void checkFifo() {
    static MidiFifo f; // 1024 messages: not on the stack
    TimedMidi m;
    DAWLAB_CHECK(!f.pop(m) && f.peek() == nullptr);
    int accepted = 0;
    for (long i = 0; i < 1100; ++i)
        accepted += f.push(numbered(i, static_cast<double>(i))) ? 1 : 0;
    DAWLAB_CHECK(accepted == 1024 && f.dropped() == 76);
    DAWLAB_CHECK(f.peek() != nullptr && numberOf(*f.peek()) == 0);
    int inOrder = 0;
    for (long i = 0; i < 1024; ++i)
        inOrder += f.pop(m) && numberOf(m) == i && m.time == static_cast<double>(i) ? 1 : 0;
    DAWLAB_CHECK(inOrder == 1024 && !f.pop(m));
    // Round and round: the counters only grow, the slots are reused.
    int wrong = 0;
    for (long i = 0; i < 5000; ++i) {
        f.push(numbered(i));
        f.push(numbered(i + 1));
        wrong += f.pop(m) && numberOf(m) == i ? 0 : 1;
        wrong += f.pop(m) && numberOf(m) == i + 1 ? 0 : 1;
    }
    DAWLAB_CHECK(wrong == 0);
}

// The MIDI thread and the audio thread at once. The producer pushes numbered messages; the consumer must see
// them all, in order, when it keeps up - and, when it stalls and the producer does not retry, an in-order
// subset with every gap counted.
inline void checkThreads() {
    static MidiFifo keepUp, stall;
    const long n = 100000;
    long seenKeepUp = 0, wrongKeepUp = 0;
    std::thread producer([&] {
        for (long i = 0; i < n; ++i)
            while (!keepUp.push(numbered(i)))
                std::this_thread::yield();
    });
    for (long expected = 0; expected < n;) {
        TimedMidi m;
        if (keepUp.pop(m)) {
            wrongKeepUp += numberOf(m) == expected % (128 * 128) ? 0 : 1;
            ++expected;
            ++seenKeepUp;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();
    DAWLAB_CHECK(seenKeepUp == n && wrongKeepUp == 0);

    std::atomic<bool> done{false};
    std::thread burst([&] {
        for (long i = 0; i < 16000; ++i) { // numbered() counts to 16384
            stall.push(numbered(i)); // a keyboard does not wait
            if (i % 2000 == 0)
                std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        done.store(true);
    });
    long received = 0, outOfOrder = 0, last = -1;
    auto take = [&] {
        TimedMidi m;
        while (stall.pop(m)) {
            const long k = numberOf(m);
            outOfOrder += k > last ? 0 : 1;
            last = k;
            ++received;
        }
    };
    while (!done.load()) {
        take();
        std::this_thread::sleep_for(std::chrono::milliseconds(2)); // a slow audio thread
    }
    burst.join();
    take();
    std::printf("  two threads: %ld of %ld messages in order; a stalled consumer: %ld received, %ld dropped, %ld out of order\n",
                seenKeepUp, n, received, stall.dropped(), outOfOrder);
    DAWLAB_CHECK(received + stall.dropped() == 16000 && outOfOrder == 0);
}

// A keyboard played for 20 s against an audio device: block k is rendered at wall time k x B plus a random
// wait (callbacks are not punctual), and heard from (k + 1) x B + L. Every key press is pushed when it
// happens. Returns how late each note is heard, min and max, in ms.
struct Lateness {
    double minMs = 1e9, maxMs = -1e9;
    long late = 0;
    long notes = 0;
};

inline Lateness playKeyboard(Timing timing, int block) {
    const double rate = 48000.0, B = block / rate, L = 0.005;
    const double delay = 2.0 * B + L; // a press is seen at most two blocks before its block is heard, plus L
    std::mt19937 rng(55);
    std::uniform_real_distribution<double> gap(0.001, 0.12), wait(0.0, 0.5);
    std::vector<double> presses;
    for (double t = 0.01; t < 20.0; t += gap(rng))
        presses.push_back(t);
    static MidiFifo fifo;
    std::vector<BlockEvent> out;
    out.reserve(MidiFifo::kCapacity);
    Lateness r;
    size_t next = 0;
    for (long k = 0; next < presses.size() || fifo.peek() != nullptr; ++k) {
        const double renderAt = k * B + wait(rng) * B;
        while (next < presses.size() && presses[next] <= renderAt) {
            fifo.push({presses[next], MidiMessage::noteOn(0, 60 + static_cast<int>(next % 24), 100)});
            ++next;
        }
        const BlockClock clock{(k + 1) * B + L, block, rate};
        drain(fifo, clock, timing, delay, out, r.late);
        for (const BlockEvent& e : out) {
            // Which press this is: notes are numbered by their order.
            const size_t i = static_cast<size_t>(r.notes++);
            const double heard = clock.blockTime + e.offset / rate;
            const double ms = 1000.0 * (heard - presses[i]);
            r.minMs = std::min(r.minMs, ms);
            r.maxMs = std::max(r.maxMs, ms);
        }
        if (k > 100000)
            break;
    }
    return r;
}

inline void checkTiming() {
    const Lateness asap = playKeyboard(Timing::AsSoonAsPossible, 512);
    const Lateness sched = playKeyboard(Timing::Scheduled, 512);
    std::printf("  512-sample blocks, as soon as possible: heard %.1f to %.1f ms after the key (%.1f ms of jitter)\n", asap.minMs,
                asap.maxMs, asap.maxMs - asap.minMs);
    std::printf("  512-sample blocks, scheduled:           heard %.1f to %.1f ms after the key (%.2f ms of jitter)\n", sched.minMs,
                sched.maxMs, sched.maxMs - sched.minMs);
    DAWLAB_CHECK(asap.notes == sched.notes && asap.notes > 300);
    // As soon as possible: early on average, but anywhere within about a block and a half.
    DAWLAB_CHECK(asap.maxMs - asap.minMs > 10.0 && asap.minMs > 10.0);
    // Scheduled: always the same delay - 2 blocks + L = 26.3 ms - to within one sample (0.021 ms).
    const double delayMs = 1000.0 * (2.0 * 512 / 48000.0 + 0.005);
    DAWLAB_CHECK(sched.late == 0 && sched.minMs > delayMs - 0.0209 && sched.maxMs <= delayMs + 1e-6);
    // Smaller blocks: less of both.
    const Lateness small = playKeyboard(Timing::Scheduled, 64);
    DAWLAB_CHECK(small.late == 0 && small.maxMs - small.minMs < 0.021 && small.maxMs < 8.0);

    // By hand, at a sample rate of 65536 Hz, where 512 samples are exactly 1/128 s and nothing rounds.
    static MidiFifo f;
    std::vector<BlockEvent> out;
    out.reserve(16);
    long late = 0;
    const double block = 1.0 / 128.0, sample = 1.0 / 65536.0;
    f.push({1.0, MidiMessage::noteOn(0, 60, 100)});
    f.push({1.0 + 1.0 / 256.0, MidiMessage::noteOn(0, 62, 100)});
    f.push({1.0 + 2.0 * block + 64.0 * sample, MidiMessage::noteOn(0, 64, 100)});
    f.push({1.0 + 2.0 * block + 64.0 * sample, MidiMessage::noteOn(0, 67, 100)});
    // A delay that is too short: 60 and 62 are due before the block starts. They are late - still played, at
    // offset 0, and counted. 64 is due at offset 576: the next block.
    drain(f, {1.0 + block, 512, 65536.0}, Timing::Scheduled, 0.0, out, late);
    DAWLAB_CHECK(out.size() == 2 && late == 2 && out[0].offset == 0 && out[1].offset == 0);
    DAWLAB_CHECK(out.size() == 2 && out[0].message.note() == 60 && out[1].message.note() == 62);
    DAWLAB_CHECK(f.peek() != nullptr && f.peek()->message.note() == 64);
    drain(f, {1.0 + 2.0 * block, 512, 65536.0}, Timing::Scheduled, 0.0, out, late);
    DAWLAB_CHECK(out.size() == 2 && out[0].offset == 64 && out[1].offset == 64 && out[1].message.note() == 67 && late == 2);
    DAWLAB_CHECK(f.peek() == nullptr);
    // Due exactly at the block's end: the next block. Exactly at its start: offset 0, and not late. The delay
    // counts: due = time + delay.
    f.push({2.0, MidiMessage::noteOn(0, 60, 100)});
    drain(f, {2.0 - block, 512, 65536.0}, Timing::Scheduled, 0.0, out, late);
    DAWLAB_CHECK(out.empty());
    drain(f, {2.0 + block, 512, 65536.0}, Timing::Scheduled, block, out, late);
    DAWLAB_CHECK(out.size() == 1 && out[0].offset == 0 && late == 2);
    // As soon as possible: everything waiting, at 0, whatever the times say.
    f.push({9.0, MidiMessage::noteOn(0, 60, 100)});
    f.push({0.5, MidiMessage::noteOn(0, 61, 100)});
    drain(f, {1.0, 512, 65536.0}, Timing::AsSoonAsPossible, 0.0, out, late);
    DAWLAB_CHECK(out.size() == 2 && out[0].offset == 0 && out[1].offset == 0 && f.peek() == nullptr && late == 2);
}

} // namespace step05_check

int main() {
    using namespace step05_check;
    std::printf("Step 5: a keyboard\n");
    checkParser();
    checkFifo();
    if (dawlab::test::counters().failures > 0)
        return dawlab::test::summary("l18_step05"); // the rest needs a FIFO that works
    checkThreads();
    checkTiming();
    return dawlab::test::summary("l18_step05");
}
