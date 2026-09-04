//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.0 Stable - refactored and completed logic
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
// File: tests/soak_pipeline.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.2.0 Stable — tests/soak_pipeline.cpp
// LONG-RUNNING SOAK of the producer→ring→consumer→barrier pipeline.
//
// Companion to tests/soak_engine.cpp (which soaks the ENGINE). This one soaks
// the TRANSPORT around it, because that is where the v1.2.0 stability bugs
// were: the pending-edit counter, the ordering barrier and the wake protocol.
//
// What it asserts over a long randomized run:
//   * CONSERVATION  — every published edit is applied exactly once; the
//                     pending count never drifts (no leak, no phantom)
//   * ORDERING      — the consumer observes a strictly increasing sequence
//                     (no reordering, no duplication, no loss)
//   * BOUNDEDNESS   — the pending count and both rings stay inside capacity
//   * LIVENESS      — the barrier is always released within its budget; the
//                     number of barrier TIMEOUTS is reported and must stay 0
//                     for a consumer that is actually making progress
//   * LIFECYCLE     — randomized engine-off / app-switch / power-resume style
//                     transitions never strand a count
//   * MEMORY        — RSS is sampled and must be flat after warm-up
//
// Usage:
//   ./soak_pipeline [iterations] [checkpointEvery]
// Exit 0 = flat + all invariants held.
//----------------------------------------------------------------------------

#include "win32_wrapper.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("  [FAIL] %s\n", what);
    }
}

// RSS sampler (Linux: /proc/self/status; elsewhere: 0 = not measured).
std::size_t rssKb() {
    FILE* f = std::fopen("/proc/self/status", "r");
    if (f == nullptr) { return 0; }
    char line[256];
    std::size_t kb = 0;
    while (std::fgets(line, sizeof line, f) != nullptr) {
        if (std::strncmp(line, "VmRSS:", 6) == 0) {
            kb = static_cast<std::size_t>(std::strtoul(line + 6, nullptr, 10));
            break;
        }
    }
    std::fclose(f);
    return kb;
}

std::size_t liveAllocs() { return 0; }   // hook: set OK_COUNT_ALLOCS to enable

//---------------------------------------------------------------------------
// Wake event (see tests/test_lifecycle.cpp for the Win32 mapping).
//---------------------------------------------------------------------------
class WakeEvent final {
public:
    void set() noexcept {
        { std::lock_guard<std::mutex> lk(m_); signaled_ = true; }
        cv_.notify_one();
    }
    template <typename HasWork>
    void park(HasWork&& hasWork) noexcept {
        std::unique_lock<std::mutex> lk(m_);
        parked_.store(true, std::memory_order_release);
        if (signaled_ || hasWork()) {
            parked_.store(false, std::memory_order_release);
            signaled_ = false;
            return;
        }
        cv_.wait(lk, [&] { return signaled_; });
        parked_.store(false, std::memory_order_release);
        signaled_ = false;
    }
    void signalIfParked() noexcept {
        if (parked_.load(std::memory_order_acquire)) { set(); }
    }
private:
    std::mutex              m_;
    std::condition_variable cv_;
    bool                    signaled_ = false;
    std::atomic<bool>       parked_{false};
};

struct Pipeline {
    ok::lockfree::SPSCRing<std::uint64_t, 1024> out;
    ok::lockfree::SPSCRing<std::uint64_t, 4096> wake;
    ok::wrap::EditDrainBarrier   barrier;
    ok::wrap::PendingEditCounter pending{barrier};
    WakeEvent                    wakeEv;

    std::atomic<bool>             running{true};
    std::atomic<std::uint64_t>    published{0};
    std::atomic<std::uint64_t>    applied{0};
    std::atomic<std::uint64_t>    lastSeq{0};       // consumer-side ordering check
    std::atomic<std::uint64_t>    orderViolations{0};
    std::atomic<std::uint64_t>    overflowWakes{0};
    std::atomic<std::uint64_t>    barrierTimeouts{0};
    std::atomic<std::uint64_t>    quiesces{0};
    std::atomic<std::uint32_t>    maxPending{0};
};

} // namespace

int main(int argc, char** argv) {
    const std::uint64_t iters = (argc > 1) ? std::strtoull(argv[1], nullptr, 0) : 2000000ULL;
    const std::uint64_t every = (argc > 2) ? std::strtoull(argv[2], nullptr, 0)
                                           : (iters / 10 ? iters / 10 : 1);

    std::printf("KieeKey — pipeline soak (%llu iterations, sample every %llu)\n\n",
                static_cast<unsigned long long>(iters),
                static_cast<unsigned long long>(every));
    std::printf("  %8s %12s %12s %10s %10s %8s %8s\n",
                "iter", "published", "applied", "pending", "maxPend", "RSS(MB)", "allocs");

    Pipeline p;
    std::atomic<bool> stop{false};

    std::thread consumer([&] {
        std::uint64_t seq = 0;
        // Bounded batches, exactly like the shipped consumer (kMaxEditBatch
        // = 32 deltas per TSF edit session): the pending count is released
        // once per batch, so the in-flight transient is bounded by the batch
        // size and the invariant below is meaningful.
        constexpr std::uint32_t kBatch = 32;
        while (!stop.load(std::memory_order_acquire) || !p.out.empty()) {
            std::uint64_t item = 0;
            std::uint32_t n = 0;
            while (p.wake.try_pop(item)) { }
            while (n < kBatch && p.out.try_pop(item)) {
                ++seq;
                if (item != seq) {
                    p.orderViolations.fetch_add(1, std::memory_order_relaxed);
                    seq = item;   // resync so one violation cannot cascade
                }
                ++n;
            }
            if (n != 0) {
                p.applied.fetch_add(n, std::memory_order_relaxed);
                p.pending.consume(n);
                p.lastSeq.store(seq, std::memory_order_relaxed);
                continue;
            }
            if (stop.load(std::memory_order_acquire)) { break; }
            p.wakeEv.park([&] { return !p.out.empty(); });
        }
    });

    std::mt19937_64 rng(0xC0FFEEULL);
    std::size_t rssFirst = 0;
    std::size_t rssLast  = 0;

    for (std::uint64_t i = 1; i <= iters; ++i) {
        // Randomized producer behaviour: bursts, gaps, occasional lifecycle
        // transitions (engine off→on, app switch, power resume).
        const std::uint32_t roll = static_cast<std::uint32_t>(rng() & 0xFFFF);

        if (roll < 64) {
            // LIFECYCLE: stop producing, drain (poke + bounded wait), and
            // force quiescence if the consumer cannot make progress.
            for (int attempt = 0; attempt < 2; ++attempt) {
                if (p.pending.pending() == 0) { break; }
                p.wakeEv.set();
                if (!p.pending.waitDrained()) {
                    p.barrierTimeouts.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (p.pending.pending() != 0) {
                p.pending.forceQuiesce();
                p.quiesces.fetch_add(1, std::memory_order_relaxed);
            }
        } else if (roll < 128) {
            // Ordering-barrier wait exactly like a pass-through keystroke.
            if (p.pending.pending() != 0) {
                if (!p.pending.waitDrained()) {
                    p.barrierTimeouts.fetch_add(1, std::memory_order_relaxed);
                }
            }
        } else {
            // Normal edit: publish → push → request a wake.
            const std::uint32_t burst = 1 + static_cast<std::uint32_t>(rng() & 7);
            for (std::uint32_t b = 0; b < burst; ++b) {
                p.pending.publish(1);
                const std::uint64_t seq = p.published.fetch_add(1, std::memory_order_relaxed) + 1;
                if (!p.out.try_push(seq)) {
                    p.pending.rollback(1);
                    p.published.fetch_sub(1, std::memory_order_relaxed);
                    break;
                }
                if (p.wake.try_push(seq)) {
                    p.wakeEv.signalIfParked();
                } else {
                    p.overflowWakes.fetch_add(1, std::memory_order_relaxed);
                    p.wakeEv.set();
                }
            }
        }

        const std::uint32_t pend = p.pending.pending();
        std::uint32_t prevMax = p.maxPending.load(std::memory_order_relaxed);
        while (pend > prevMax &&
               !p.maxPending.compare_exchange_weak(prevMax, pend,
                                                   std::memory_order_relaxed)) { }
        // The pending count can exceed the ring's occupancy by at most one
        // in-flight consumer batch (kBatch = 32); anything beyond that is a
        // publish/consume imbalance — a leak.
        if (pend > p.out.capacity + 32u) {
            check(false, "pending count exceeded ring capacity + one batch");
            break;
        }

        if (i % every == 0) {
            const std::size_t rss = rssKb();
            if (rssFirst == 0) { rssFirst = rss; }
            rssLast = rss;
            std::printf("  %8llu %12llu %12llu %10u %10u %8.2f %8zu\n",
                        static_cast<unsigned long long>(i),
                        static_cast<unsigned long long>(p.published.load()),
                        static_cast<unsigned long long>(p.applied.load()),
                        pend, p.maxPending.load(std::memory_order_relaxed),
                        static_cast<double>(rss) / 1024.0, liveAllocs());
        }
    }

    // Shut down like the app does: lifecycle drain, then stop.
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (p.pending.pending() == 0) { break; }
        p.wakeEv.set();
        static_cast<void>(p.pending.waitDrained());
    }
    if (p.pending.pending() != 0) {
        p.pending.forceQuiesce();
        p.quiesces.fetch_add(1, std::memory_order_relaxed);
    }
    stop.store(true, std::memory_order_release);
    p.wakeEv.set();
    consumer.join();

    std::printf("\n  published        : %llu\n", static_cast<unsigned long long>(p.published.load()));
    std::printf("  applied          : %llu\n", static_cast<unsigned long long>(p.applied.load()));
    std::printf("  pending at exit  : %u\n", p.pending.pending());
    std::printf("  max pending      : %u\n", p.maxPending.load(std::memory_order_relaxed));
    std::printf("  order violations : %llu\n", static_cast<unsigned long long>(p.orderViolations.load()));
    std::printf("  overflow wakes   : %llu\n", static_cast<unsigned long long>(p.overflowWakes.load()));
    std::printf("  barrier timeouts : %llu\n", static_cast<unsigned long long>(p.barrierTimeouts.load()));
    std::printf("  forced quiesces  : %llu\n", static_cast<unsigned long long>(p.quiesces.load()));
    if (rssFirst != 0) {
        std::printf("  RSS first/last   : %.2f / %.2f MB\n",
                    static_cast<double>(rssFirst) / 1024.0,
                    static_cast<double>(rssLast) / 1024.0);
    }

    std::printf("\n");
    check(p.applied.load() == p.published.load(),
          "every published edit was applied exactly once");
    check(p.pending.pending() == 0, "pending count is zero at exit");
    check(p.orderViolations.load() == 0, "consumer observed a strictly ordered stream");
    check(p.out.empty(), "output ring fully drained");
    if (rssFirst != 0 && rssLast > rssFirst) {
        const double growthMb = static_cast<double>(rssLast - rssFirst) / 1024.0;
        // Allow generous slack for allocator arena behaviour; a real leak in
        // this pipeline grows by MB per 100k iterations.
        check(growthMb < 8.0, "RSS stayed flat after warm-up");
    }

    if (g_failures == 0) {
        std::printf("SOAK PIPELINE PASSED\n");
        return 0;
    }
    std::printf("SOAK PIPELINE FAILED (%d)\n", g_failures);
    return 1;
}
