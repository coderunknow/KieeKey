//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.1 Stable - refactored and completed logic
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
// File: tests/test_ringbuffer.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.3 — test_ringbuffer.cpp
// Verifies the lock-free SPSC ring: order preservation, overflow policy,
// batch drain, and MPMC cross-thread correctness.
//----------------------------------------------------------------------------
#include "LockFreeQueue.hpp"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

using ok::lockfree::SPSCRing;
using ok::lockfree::MPMCQueue;

namespace {

int failures = 0;
// Plain `if` — `if constexpr` would require a constant expression and break
// every runtime CHECK (ill-formed C++). C4127 is disabled project-wide.
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    ++failures; } } while (0)

void test_spsc_order() {
    constexpr std::size_t N = 1'000'000;
    SPSCRing<std::uint32_t, 4096> q;

    std::thread producer([&] {
        for (std::uint32_t i = 0; i < N; ++i) {
            while (!q.try_push(i)) { /* bounded spin — test only */ }
        }
    });
    std::uint64_t sum = 0, count = 0, expected = static_cast<std::uint64_t>(N) * (N - 1) / 2;
    std::uint32_t last = 0;
    bool first = true;
    std::uint32_t v;
    while (count < N) {
        if (q.try_pop(v)) {
            if (!first) { CHECK(v == last + 1); }   // strict order
            first = false;
            last = v;
            sum += v;
            ++count;
        }
    }
    producer.join();
    CHECK(sum == expected);
    CHECK(count == N);
    CHECK(q.empty());
}

void test_spsc_overflow() {
    // Tiny ring → exercise full-path without blocking.
    SPSCRing<int, 4> q;
    int v = 0;
    CHECK(q.try_push(1));
    CHECK(q.try_push(2));
    CHECK(q.try_push(3));
    CHECK(q.try_push(4));
    CHECK(!q.try_push(5));          // full → producer drops (DropNewest policy)
    CHECK(q.try_pop(v) && v == 1);  // FIFO order kept
    CHECK(q.try_push(5));           // slot freed
    CHECK(q.try_pop(v) && v == 2);
    CHECK(q.try_pop(v) && v == 3);
    CHECK(q.try_pop(v) && v == 4);
    CHECK(q.try_pop(v) && v == 5);
    CHECK(!q.try_pop(v));           // empty
}

void test_spsc_batch() {
    SPSCRing<int, 8> q;
    for (int i = 0; i < 8; ++i) { CHECK(q.try_push(i)); }
    int out[8];
    std::size_t n = q.try_pop_batch(out, 8);
    CHECK(n == 8);
    for (int i = 0; i < 8; ++i) { CHECK(out[i] == i); }
    CHECK(q.empty());
}

void test_mpmc_stress() {
    constexpr std::size_t Producers = 4;
    constexpr std::size_t ItemsPerProducer = 200'000;
    constexpr std::size_t Total = Producers * ItemsPerProducer;
    MPMCQueue<std::uint32_t, 4096> q;
    std::atomic<std::uint64_t> received{0};

    std::vector<std::thread> producers;
    for (std::uint32_t p = 0; p < Producers; ++p) {
        producers.emplace_back([&q, p] {
            for (std::uint32_t i = 0; i < ItemsPerProducer; ++i) {
                while (!q.try_push((p << 20) | i)) { /* spin */ }
            }
        });
    }
    std::thread consumer([&] {
        std::uint32_t v;
        while (received.load(std::memory_order_relaxed) < Total) {
            if (q.try_pop(v)) {
                received.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });
    for (auto& t : producers) { t.join(); }
    consumer.join();
    CHECK(received.load() == Total);
    CHECK(q.empty());
}

} // namespace

int main() {
    test_spsc_order();
    test_spsc_overflow();
    test_spsc_batch();
    test_mpmc_stress();

    if (failures == 0) {
        std::printf("ALL RINGBUFFER TESTS PASSED\n");
        return 0;
    }
    std::printf("%d RINGBUFFER TEST(S) FAILED\n", failures);
    return 1;
}
