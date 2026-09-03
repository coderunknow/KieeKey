//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.1.0 - refactored and completed logic
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
// File: tests/stress_queue.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.0 — tests/stress_queue.cpp
// EXTREME lock-free queue test: SPSC correctness under random pauses,
// MPMC multi-producer integrity, overflow edge, throughput measurement.
// Recommended: run under -fsanitize=thread for the concurrency checks.
//
//   g++ -std=c++23 -O2 -Wno-interference-size -I src/core tests/stress_queue.cpp -o sq
//   ./sq [items]
//
// Exit 0 = ALL CHECKS PASSED.
//----------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "LockFreeQueue.hpp"

using namespace ok::lockfree;
using namespace std::chrono;

namespace {

int g_failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { ++g_failures; std::printf("  [FAIL] %s\n", what); }
    else     { std::printf("  [ ok ] %s\n", what); }
}

void pauseRandom(std::uint64_t* s) {
    // xorshift
    *s ^= *s << 13; *s ^= *s >> 7; *s ^= *s << 17;
    if ((*s & 0x3F) == 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(*s % 200));
    }
}

//---------------------------------------------------------------------------
// 1) SPSC correctness: N items, random pauses, order + checksum preserved
//---------------------------------------------------------------------------
bool spscTest(std::uint64_t n) {
    SPSCRing<std::uint64_t, 4096> q;
    std::atomic<bool> producerDone{false};
    std::atomic<std::uint64_t> popped{0};
    std::atomic<std::uint64_t> sum{0};
    std::atomic<std::uint64_t> last{-1ULL};
    std::atomic<bool> orderOk{true};

    std::thread producer([&] {
        std::uint64_t s = 1;
        std::uint64_t v = 0;
        while (v < n) {
            if (q.try_push(v)) { ++v; }
            else { pauseRandom(&s); }   // back off on full
        }
        producerDone.store(true, std::memory_order_release);
    });
    std::thread consumer([&] {
        std::uint64_t s = 2;
        std::uint64_t prev = 0;
        bool first = true;
        while (!producerDone.load(std::memory_order_acquire) || !q.empty()) {
            std::uint64_t v = 0;
            if (q.try_pop(v)) {
                sum.fetch_add(v, std::memory_order_relaxed);
                popped.fetch_add(1, std::memory_order_relaxed);
                if (first) { first = false; }
                else if (v != prev + 1) { orderOk.store(false, std::memory_order_relaxed); }
                prev = v;
                last.store(v, std::memory_order_relaxed);
            } else {
                pauseRandom(&s);
            }
        }
    });
    producer.join();
    consumer.join();

    const std::uint64_t expectSum = n * (n - 1) / 2;
    const bool ok = popped.load() == n && sum.load() == expectSum && orderOk.load();
    std::printf("  SPSC  %llu items: popped=%llu sum=%s order=%s\n",
                (unsigned long long)n, (unsigned long long)popped.load(),
                sum.load() == expectSum ? "OK" : "MISMATCH",
                orderOk.load() ? "OK" : "BROKEN");
    return ok;
}

//---------------------------------------------------------------------------
// 2) MPMC: 8 producers × items, single consumer; every item seen exactly once
//---------------------------------------------------------------------------
bool mpmcTest(std::uint64_t perProducer) {
    constexpr int kProducers = 8;
    MPMCQueue<std::uint64_t, 65536> q;
    std::atomic<int> done{0};
    std::atomic<std::uint64_t> seen{0};
    std::atomic<std::uint64_t> sum{0};
    // Bitmap for duplicates (item values are unique: pid * perProducer + k)
    const std::uint64_t total = kProducers * perProducer;
    std::vector<std::atomic<bool>> seenBitmap(total);
    for (auto& b : seenBitmap) { b.store(false, std::memory_order_relaxed); }
    std::atomic<bool> dup{false};

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p] {
            std::uint64_t s = p + 100;
            std::uint64_t v = static_cast<std::uint64_t>(p) * perProducer;
            const std::uint64_t end = v + perProducer;
            while (v < end) {
                if (q.try_push(v)) { ++v; }
                else { pauseRandom(&s); }
            }
            done.fetch_add(1, std::memory_order_release);
        });
    }
    std::thread consumer([&] {
        std::uint64_t it = 0;
        while (done.load(std::memory_order_acquire) < kProducers || !q.empty()) {
            std::uint64_t v = 0;
            if (q.try_pop(v)) {
                sum.fetch_add(v, std::memory_order_relaxed);
                seen.fetch_add(1, std::memory_order_relaxed);
                bool expected = false;
                if (!seenBitmap[v].compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
                    dup.store(true, std::memory_order_relaxed);
                }
                ++it;
                if ((it & 0x3F) == 0) { std::this_thread::yield(); }
            }
        }
    });
    for (auto& t : producers) { t.join(); }
    consumer.join();

    // Every unique value 0..total-1 is produced exactly once.
    const std::uint64_t expectSumAll = total * (total - 1) / 2;
    const bool ok = seen.load() == total && sum.load() == expectSumAll && !dup.load();
    std::printf("  MPMC  %d x %llu: seen=%llu sum=%s dup=%s\n",
                kProducers, (unsigned long long)perProducer,
                (unsigned long long)seen.load(),
                sum.load() == expectSumAll ? "OK" : "MISMATCH",
                dup.load() ? "YES" : "no");
    return ok;
}

//---------------------------------------------------------------------------
// 3) Overflow edge: fill to capacity, push fails, existing items intact
//---------------------------------------------------------------------------
bool overflowTest() {
    SPSCRing<std::uint64_t, 64> q;   // small ring
    std::uint64_t i = 0;
    for (; i < 128; ++i) {           // 64 fits, 64..127 must fail
        if (!q.try_push(i)) { break; }
    }
    const std::uint64_t fits = i;
    std::uint64_t v = 0;
    bool orderOk = true;
    std::uint64_t expect = 0;
    while (q.try_pop(v)) {
        if (v != expect) { orderOk = false; }
        ++expect;
    }
    const bool ok = fits == 64 && expect == 64 && orderOk;
    std::printf("  Overflow: %llu items stored, %llu drained, order=%s\n",
                (unsigned long long)fits, (unsigned long long)expect,
                orderOk ? "OK" : "BROKEN");
    return ok;
}

//---------------------------------------------------------------------------
// 4) Throughput (steady-state pipeline, no pauses): push+pop ops/sec
//---------------------------------------------------------------------------
void throughputTest(std::uint64_t n) {
    SPSCRing<std::uint64_t, 4096> q;
    const auto t0 = steady_clock::now();
    std::uint64_t v = 0;
    std::uint64_t popped = 0;
    // Pipelined steady state: the producer stays up to `capacity` items ahead
    // of the consumer, exactly like the real hook→consumer topology. A push
    // only blocks (returns false) when the ring is full, and the pop drains
    // in the same loop, so the queue never overflows and never starves.
    while (popped < n) {
        if (v < n && q.try_push(v)) { ++v; }
        std::uint64_t tmp = 0;
        if (q.try_pop(tmp)) { ++popped; }
    }
    const auto t1 = steady_clock::now();
    const double opsPerSec = 2.0 * static_cast<double>(n) /
        duration_cast<duration<double>>(t1 - t0).count();
    std::printf("  Throughput (SPSC pipelined push+pop): %.0f M ops/s (%llu items)\n",
                opsPerSec / 1e6, (unsigned long long)n);
}

} // namespace

int main(int argc, char** argv) {
    const std::uint64_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 0) : 1'000'000;
    std::printf("KieeKey — lock-free queue stress test (%llu items)\n",
                (unsigned long long)n);

    std::printf("\n-- SPSC correctness --\n");
    check(spscTest(n), "SPSC order+integrity");
    std::printf("\n-- MPMC correctness --\n");
    check(mpmcTest(200'000), "MPMC 8x200k no dup/no loss");
    std::printf("\n-- Overflow edge --\n");
    check(overflowTest(), "capacity respected, order preserved");
    std::printf("\n-- Throughput --\n");
    throughputTest(n);

    std::printf("\n%s\n", g_failures == 0 ? "ALL QUEUE STRESS CHECKS PASSED" : "QUEUE STRESS FAILED");
    return g_failures == 0 ? 0 : 1;
}
