//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.1.3 - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// File: tests/bench_latency.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.3 — tests/bench_latency.cpp
// Latency DISTRIBUTION (p50/p90/p99/p999/max/min), not just averages:
//   1. engine decision + UTF-16 encode per key
//   2. full pipeline simulation: capture → SPSC ring → consumer → engine
//      (the exact path the app uses for TSF-mode output)
//   3. cycles/key (rdtsc) on x86-64
//
//   g++ -std=c++23 -O2 -I src/core tests/bench_latency.cpp src/core/TextEngine.cpp -o bl
//   ./bl
//----------------------------------------------------------------------------
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "LockFreeQueue.hpp"
#include "TextEngine.hpp"

using namespace ok::text;
using namespace ok::lockfree;
using namespace std::chrono;

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#define HAVE_RDTSC 1
#endif

namespace {

//---------------------------------------------------------------------------
// percentile reporting
//---------------------------------------------------------------------------
struct Stats {
    double min, p50, p90, p99, p999, max, mean;
};
Stats compute(std::vector<std::uint64_t> v) {
    std::sort(v.begin(), v.end());
    auto at = [&](double q) {
        return static_cast<double>(v[static_cast<std::size_t>(q * (v.size() - 1))]);
    };
    double sum = 0;
    for (auto x : v) { sum += static_cast<double>(x); }
    return { static_cast<double>(v.front()), at(0.50), at(0.90), at(0.99), at(0.999),
             static_cast<double>(v.back()), sum / static_cast<double>(v.size()) };
}
void printStats(const char* label, const Stats& s, const char* unit) {
    std::printf("  %-34s min=%6.0f p50=%6.0f p90=%6.0f p99=%6.0f p999=%6.0f max=%6.0f  (mean %6.0f) %s\n",
                label, s.min, s.p50, s.p90, s.p99, s.p999, s.max, s.mean, unit);
}

// deterministic keystroke stream (mix of letters + tone keys)
std::vector<char32_t> keyStream(std::size_t n) {
    std::vector<char32_t> v;
    v.reserve(n);
    static const char32_t a[] = {U'a',U's',U'a',U'a',U'w',U'u',U'o',U'w',U'b',U'a',U'n',
                                 U's',U'x',U'i',U'n',U'c',U'h',U'a',U'o',U'f',U'r',U'j',
                                 U'z',U'd',U'd',U'e',U'f',U'q',U'u',U'o',U'c',U'k',U'y'};
    for (std::size_t i = 0; i < n; ++i) { v.push_back(a[i % std::size(a)]); }
    return v;
}

//---------------------------------------------------------------------------
// 1) engine-only: process + encode (scratch buffer — the production hot path)
//---------------------------------------------------------------------------
void benchEngine(std::size_t n) {
    EngineOptions o; o.inputMethod = InputMethod::Telex; o.codeTable = CodeTable::Unicode;
    TextEngine e(o);
    const auto keys = keyStream(n);
    std::vector<std::uint64_t> lat; lat.reserve(n);
    std::wstring scratch;

    for (char32_t ch : keys) {
        TextInput in; in.kind = InputKind::Char; in.ch = ch;
        const auto t0 = steady_clock::now();
        const EngineResult& r = e.process(in);
        if (r.consumed()) { e.replacementUtf16(r, scratch); }
        const auto t1 = steady_clock::now();
        lat.push_back(static_cast<std::uint64_t>(duration_cast<nanoseconds>(t1 - t0).count()));
    }
    printStats("engine decision+encode", compute(std::move(lat)), "ns/key");
}

//---------------------------------------------------------------------------
// 2) full pipeline: producer push → ring → consumer pop → engine+encode
//    (single-threaded simulation of the hook→consumer hop + engine)
//---------------------------------------------------------------------------
void benchPipeline(std::size_t n) {
    using Item = char32_t;
    SPSCRing<Item, 256> ring;
    EngineOptions o; o.inputMethod = InputMethod::Telex; o.codeTable = CodeTable::Unicode;
    TextEngine e(o);
    const auto keys = keyStream(n);
    std::vector<std::uint64_t> lat; lat.reserve(n);
    std::wstring scratch;

    for (char32_t ch : keys) {
        const auto t0 = steady_clock::now();
        while (!ring.try_push(ch)) { /* backpressure (never in practice) */ }
        Item out = 0;
        if (ring.try_pop(out)) {
            TextInput in; in.kind = InputKind::Char; in.ch = out;
            const EngineResult& r = e.process(in);
            if (r.consumed()) { e.replacementUtf16(r, scratch); }
        }
        const auto t1 = steady_clock::now();
        lat.push_back(static_cast<std::uint64_t>(duration_cast<nanoseconds>(t1 - t0).count()));
    }
    printStats("push→pop→engine→encode", compute(std::move(lat)), "ns/key");
}

//---------------------------------------------------------------------------
// 3) DELETE PATH — engine backspace-storm latency.
// The user-visible "delete delay" is the cost of the engine's delete
// decision (backspaceBranch + checkSpelling + checkGrammar) plus the
// pipeline hop. This measures the engine side under a sustained storm on a
// long word (incl. the longWordHelper_ restore shift path).
//---------------------------------------------------------------------------
void benchDeleteEngine(std::size_t n) {
    EngineOptions o; o.inputMethod = InputMethod::Telex; o.codeTable = CodeTable::Unicode;
    TextEngine e(o);
    std::wstring scratch; scratch.reserve(256);
    TextInput ch; ch.kind = InputKind::Char; ch.ch = U'a';
    TextInput bs; bs.kind = InputKind::Backspace;

    // Build a long word (longWordHelper_ active) so the delete path crosses
    // the overflow-restore branch.
    for (int i = 0; i < 80; ++i) {
        const EngineResult& r = e.process(ch);
        if (r.consumed()) { e.replacementUtf16(r, scratch); }
    }
    // Now storm backspaces, measuring each delete decision.
    std::vector<std::uint64_t> lat; lat.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto t0 = steady_clock::now();
        const EngineResult& r = e.process(bs);
        if (r.consumed()) { e.replacementUtf16(r, scratch); }
        const auto t1 = steady_clock::now();
        lat.push_back(static_cast<std::uint64_t>(duration_cast<nanoseconds>(t1 - t0).count()));
    }
    printStats("delete decision (backspace storm)", compute(std::move(lat)), "ns/key");
}

//---------------------------------------------------------------------------
// 4) DELETE PATH — real two-thread pipeline: producer → SPSC ring → consumer
// (wake + batch drain, same shape as ModernKeyHook::consumerThreadMain) —
// the exact hop a correction's backspace+insert takes in TSF mode.
//---------------------------------------------------------------------------
void benchDeletePipeline(std::size_t n) {
    // Item carries its own push-time stamp, so the consumer measures pure
    // queue-residence + wake latency (no separate array, no cross-thread
    // race). The timestamp is refreshed on every failed push attempt, so a
    // producer that outruns the consumer does not inflate the measurement
    // with its own backpressure wait — that wait is a benchmark saturation
    // artifact, not pipeline latency.
    struct Item { std::uint64_t pushedAtNs; };
    SPSCRing<Item, 256> ring;
    std::atomic<bool> running{true};
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<std::uint64_t> lat; lat.reserve(n);

    // NOTE on the signaling model: the PRODUCTION consumer parks on a
    // MANUAL-RESET event (ModernKeyHook::SetEvent/ResetEvent) with NO
    // predicate — a sticky signal cannot lose a wakeup, and the producer
    // signals only on the empty→non-empty transition (wake-on-demand; the
    // majority of events — KeyUps and pass-through keys — never signal at
    // all). A condvar + queue-predicate simulation of that exact pattern can
    // lose a wakeup on a momentarily stale queue view, so this bench models
    // the WAKE COST (identical either way: the consumer parks between bursts
    // and the first push of the next burst pays one OS wake) with the
    // self-healing notify-every-push form. The producer-side syscall savings
    // of wake-on-demand are measured separately (see bench_producer-style
    // micro-benchmark in the report).
    std::thread consumer([&] {
        Item batch[64];
        while (running.load(std::memory_order_acquire) || !ring.empty()) {
            const std::size_t k = ring.try_pop_batch(batch, std::size(batch));
            if (k != 0) {
                const auto t1 = static_cast<std::uint64_t>(
                    duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
                for (std::size_t i = 0; i < k; ++i) {
                    lat.push_back(t1 - batch[i].pushedAtNs);
                }
                continue;
            }
            // pause-spin then block (mirror of the production consumer:
            // 4096 _mm_pause iterations, then wait on the wake event)
            for (int s = 0; s < 4096; ++s) {
                if (!ring.empty()) { break; }
            }
            if (ring.empty() && running.load(std::memory_order_acquire)) {
                std::unique_lock<std::mutex> lk(mtx);
                cv.wait(lk, [&] { return !ring.empty() || !running.load(std::memory_order_acquire); });
            }
        }
    });

    std::thread producer([&] {
        // Realistic typing: short bursts (a word at a time) with inter-word
        // pauses, so the consumer parks between bursts — the exact regime the
        // TSF-mode consumer sees. (A fully saturated producer would peg the
        // consumer at 100% CPU for the whole bench; the multi-ms tail that
        // produces is CFS timeslicing of a saturated thread, not pipeline
        // latency — the app never runs that regime: human typing is ~10³/s,
        // the consumer processes ~10⁷/s.)
        std::uint64_t seq = 0;
        while (seq < n) {
            const int burst = 3 + static_cast<int>(seq % 13);   // 3..15 keys
            for (int b = 0; b < burst && seq < n; ++b) {
                const auto t0 = static_cast<std::uint64_t>(
                    duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
                if (ring.try_push(Item{t0})) {
                    cv.notify_one();          // self-healing form (see NOTE above)
                    ++seq;
                } else {
                    std::this_thread::yield();   // bench-only: never hot-spin on a full ring
                }
            }
            if (seq < n) {
                // inter-word pause (~human cadence, 300–800 µs)
                std::this_thread::sleep_for(std::chrono::microseconds(300 + (seq % 5) * 125));
            }
        }
        running.store(false, std::memory_order_release);
        cv.notify_one();
    });

    producer.join();
    consumer.join();

    // A correction's latency = ring hop + consumer wake (+ engine cost, which
    // benchDeleteEngine measures separately).
    std::sort(lat.begin(), lat.end());
    auto at = [&](double q) {
        return static_cast<double>(lat[static_cast<std::size_t>(q * (lat.size() - 1))]);
    };
    double sum = 0;
    for (auto x : lat) { sum += static_cast<double>(x); }
    Stats s{ static_cast<double>(lat.front()), at(0.50), at(0.90), at(0.99), at(0.999),
             static_cast<double>(lat.back()), sum / static_cast<double>(lat.size()) };
    printStats("correction hop (ring+wake), 2 threads", s, "ns/edit");
}

//---------------------------------------------------------------------------
// 5) cycles/key (rdtsc) for the engine path
//---------------------------------------------------------------------------
#ifdef HAVE_RDTSC
void benchCycles(std::size_t n) {
    EngineOptions o; o.inputMethod = InputMethod::Telex; o.codeTable = CodeTable::Unicode;
    TextEngine e(o);
    const auto keys = keyStream(n);
    std::vector<std::uint64_t> cyc; cyc.reserve(n);
    std::wstring scratch;
    for (char32_t ch : keys) {
        TextInput in; in.kind = InputKind::Char; in.ch = ch;
        const auto c0 = __rdtsc();
        const EngineResult& r = e.process(in);
        if (r.consumed()) { e.replacementUtf16(r, scratch); }
        const auto c1 = __rdtsc();
        cyc.push_back(c1 - c0);
    }
    printStats("engine (rdtsc)", compute(std::move(cyc)), "cycles/key");
}
#endif

} // namespace

int main() {
    constexpr std::size_t kKeys = 2'000'000;
    std::printf("KieeKey — latency distribution (ns), %zu keys each:\n\n", kKeys);

    benchEngine(kKeys);
    benchPipeline(kKeys);
    benchDeleteEngine(200'000);
    benchDeletePipeline(200'000);
#ifdef HAVE_RDTSC
    benchCycles(kKeys);
#endif

    std::printf("\nReference: human 200 WPM = ~17 keys/s = ~58,000,000 ns/key.\n");
    return 0;
}
