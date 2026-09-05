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
// File: src/core/LockFreeQueue.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.3 — LockFreeQueue.hpp
// Bounded, non-blocking, wait-free producers / lock-free consumers.
//
//   * SPSCRing : single-producer single-consumer (Vyukov sequence-slot ring).
//                This is the CORRECT model for the keyboard pipeline:
//                WH_KEYBOARD_LL / WH_MOUSE_LL / SetWinEventHook callbacks are
//                all delivered, serialized, on the single hook pump thread,
//                so the hook thread is the one and only producer.
//   * MPMCQueue: bounded multi-producer multi-consumer (Vyukov) — provided for
//                future topologies (e.g. multiple input sources) and for the
//                unit-test harness.
//
// No allocations, no locks, no blocking in the producer path: the low-level
// hook callback may call try_push and must always return in O(1).
//----------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ok::lockfree {

// Fixed 64-byte cacheline: x86-64 L1 line size is 64 B on every target this
// ships for, and the value must be ABI-stable (std::hardware_*_interference_
// size is explicitly allowed to change between compilers/-mtune, which
// triggers -Werror=interference-size on GCC 14).
inline constexpr std::size_t cacheline = 64;

//---------------------------------------------------------------------------
// SPSCRing<T, Capacity>  —  capacity MUST be a power of two, T trivially
// copyable and cheap to assign. Sequence-number slots (Dmitry Vyukov SPSC).
//---------------------------------------------------------------------------
template <typename T, std::size_t Capacity>
class SPSCRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(Capacity >= 2, "Capacity must be >= 2");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

public:
    static constexpr std::size_t capacity = Capacity;

    // Vyukov sequence-slot ring: each slot i is pre-armed with seq == i.
    SPSCRing() noexcept {
        for (std::size_t i = 0; i < Capacity; ++i) {
            slots_[i].seq.store(static_cast<std::intptr_t>(i), std::memory_order_relaxed);
        }
    }
    SPSCRing(const SPSCRing&)            = delete;
    SPSCRing& operator=(const SPSCRing&) = delete;

    // Non-blocking enqueue. Returns false when full (caller decides policy:
    // drop, or spin — never block inside a hook callback).
    [[nodiscard]] bool try_push(const T& item) noexcept {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t idx = h & (Capacity - 1);
        // If the slot's sequence is not h, the consumer has not consumed
        // item at index `idx` yet -> buffer is full.
        if (slots_[idx].seq.load(std::memory_order_acquire) != static_cast<std::intptr_t>(h)) {
            return false;
        }
        slots_[idx].data = item;
        slots_[idx].seq.store(static_cast<std::intptr_t>(h + 1), std::memory_order_release);
        head_.store(h + 1, std::memory_order_relaxed);
        return true;
    }

    // Non-blocking dequeue. Returns false when empty.
    [[nodiscard]] bool try_pop(T& out) noexcept {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        const std::size_t idx = t & (Capacity - 1);
        if (slots_[idx].seq.load(std::memory_order_acquire) != static_cast<std::intptr_t>(t + 1)) {
            return false;
        }
        out = slots_[idx].data;
        slots_[idx].seq.store(static_cast<std::intptr_t>(t + Capacity), std::memory_order_release);
        tail_.store(t + 1, std::memory_order_relaxed);
        return true;
    }

    // Drain up to `maxItems` items; returns number drained. Reduces per-item
    // atomic traffic on the consumer side (batch latency is amortized).
    std::size_t try_pop_batch(T* out, std::size_t maxItems) noexcept {
        std::size_t n = 0;
        while (n < maxItems && try_pop(out[n])) { ++n; }
        return n;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        const std::size_t h = head_.load(std::memory_order_acquire);
        const std::size_t t = tail_.load(std::memory_order_acquire);
        return h - t;
    }
    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

private:
    struct alignas(cacheline) Slot {
        std::atomic<std::intptr_t> seq{0};   // 0 = never used
        T data{};
    };
    std::array<Slot, Capacity> slots_{};

    alignas(cacheline) std::atomic<std::size_t> head_{0};  // producer-owned
    alignas(cacheline) std::atomic<std::size_t> tail_{0};  // consumer-owned
};

//---------------------------------------------------------------------------
// MPMCQueue<T, Capacity> — bounded multi-producer / multi-consumer (Vyukov).
// Same non-blocking API. Used by the test harness and future multi-source
// topologies.
//---------------------------------------------------------------------------
template <typename T, std::size_t Capacity>
class MPMCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

public:
    static constexpr std::size_t capacity = Capacity;

    MPMCQueue() noexcept {
        for (std::size_t i = 0; i < Capacity; ++i) {
            slots_[i].seq.store(static_cast<std::intptr_t>(i), std::memory_order_relaxed);
        }
    }
    MPMCQueue(const MPMCQueue&)            = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;

    [[nodiscard]] bool try_push(const T& item) noexcept {
        while (true) {
            std::size_t pos = head_.load(std::memory_order_relaxed);
            Slot& s = slots_[pos & (Capacity - 1)];
            const std::intptr_t seq = s.seq.load(std::memory_order_acquire);
            const std::intptr_t diff = seq - static_cast<std::intptr_t>(pos);
            if (diff == 0) {
                // Claim this slot (CAS the position counter; on failure `pos`
                // is refreshed to the current head and we retry).
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    s.data = item;
                    s.seq.store(seq + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;   // queue full
            }
            // slot owned by another producer → retry
        }
    }

    [[nodiscard]] bool try_pop(T& out) noexcept {
        while (true) {
            std::size_t pos = tail_.load(std::memory_order_relaxed);
            Slot& s = slots_[pos & (Capacity - 1)];
            const std::intptr_t seq = s.seq.load(std::memory_order_acquire);
            const std::intptr_t diff = seq - static_cast<std::intptr_t>(pos + 1);
            if (diff == 0) {
                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    out = s.data;
                    s.seq.store(pos + Capacity, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;   // queue empty
            }
            // slot owned by another consumer → retry
        }
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

private:
    struct alignas(cacheline) Slot {
        std::atomic<std::intptr_t> seq{0};
        T data{};
    };
    std::array<Slot, Capacity> slots_{};
    alignas(cacheline) std::atomic<std::size_t> head_{0};
    alignas(cacheline) std::atomic<std::size_t> tail_{0};
};

// Latency/stats helpers shared by producers and consumers.
struct QueueStats {
    std::atomic<std::uint64_t> pushed{0};
    std::atomic<std::uint64_t> popped{0};
    std::atomic<std::uint64_t> droppedOverflow{0};
    std::atomic<std::int64_t>  lastLatencyUs{0};   // consumer-measured E2E latency
};

} // namespace ok::lockfree
