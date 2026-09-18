//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: src/core/SeqRing.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — SeqRing.hpp
// Fixed-capacity, allocation-free, lock-free observation ring for the typing
// hot path (analytics telemetry and the personal-AI learner).
//
// WHY NOT A PLAIN ARRAY + INDEX:
//   v1.3.0-beta1 published observations with `ring[idx] = entry` from the hook
//   thread while the UI thread read the same slots — a data race that is
//   *undefined behaviour* (and a torn read in practice: a half-updated entry
//   with a fresh timestamp corrupts every derived interval metric).
//
// DESIGN:
//   * Single producer / single consumer (hook thread → UI thread).
//   * Every slot is a seqlock over two atomic words, so all accesses are
//     defined atomics: no mutex, no allocation, and clean under TSan/ASan.
//   * Payload packing keeps entries at 16 bytes so the whole ring stays in L2.
//   * Readers never block the producer: a slot that is being overwritten is
//     simply skipped for that snapshot (and the next snapshot sees it).
//----------------------------------------------------------------------------
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace ok {

class SeqRing {
public:
    static constexpr std::size_t kCapacity = 1024;

    SeqRing() noexcept = default;
    SeqRing(const SeqRing&) = delete;
    SeqRing& operator=(const SeqRing&) = delete;

    //---- producer -----------------------------------------------------------
    void push(std::uint64_t stamp, std::uint64_t payload) noexcept {
        const std::uint64_t index = m_head.load(std::memory_order_relaxed);
        Slot& slot = m_slots[static_cast<std::size_t>(index % kCapacity)];
        const std::uint32_t seq = slot.seq.load(std::memory_order_relaxed);
        slot.seq.store(seq + 1, std::memory_order_relaxed);        // odd: writing
        slot.stamp.store(stamp, std::memory_order_relaxed);
        slot.payload.store(payload, std::memory_order_relaxed);
        slot.seq.store(seq + 2, std::memory_order_release);        // even: stable
        m_head.store(index + 1, std::memory_order_release);
    }

    [[nodiscard]] std::uint64_t head() const noexcept {
        return m_head.load(std::memory_order_acquire);
    }

    //---- consumer -----------------------------------------------------------
    // Returns false when the slot was being written (retry on the next pass).
    [[nodiscard]] bool read(std::uint64_t index, std::uint64_t& stamp,
                            std::uint64_t& payload) const noexcept {
        const Slot& slot = m_slots[static_cast<std::size_t>(index % kCapacity)];
        const std::uint32_t first = slot.seq.load(std::memory_order_acquire);
        if ((first & 1u) != 0u) {
            return false;
        }
        stamp = slot.stamp.load(std::memory_order_relaxed);
        payload = slot.payload.load(std::memory_order_relaxed);
        const std::uint32_t second = slot.seq.load(std::memory_order_acquire);
        return first == second;
    }

    // Snapshot helper: reads up to `maxCount` newest entries into `out`
    // (oldest first) and returns the number of valid entries written.
    template <std::size_t N>
    [[nodiscard]] std::size_t snapshotNewest(std::array<std::uint64_t, N>& stamps,
                                             std::array<std::uint64_t, N>& payloads,
                                             std::size_t maxCount) const noexcept {
        const std::uint64_t total = head();
        std::size_t count = static_cast<std::size_t>(total < kCapacity ? total : kCapacity);
        if (count > maxCount) {
            count = maxCount;
        }
        const std::uint64_t first = total - count;   // inclusive
        std::size_t written = 0;
        for (std::uint64_t index = first; index < total && written < maxCount; ++index) {
            std::uint64_t stamp = 0;
            std::uint64_t payload = 0;
            if (read(index, stamp, payload)) {
                stamps[written] = stamp;
                payloads[written] = payload;
                ++written;
            }
        }
        return written;
    }

    void reset() noexcept {
        for (auto& slot : m_slots) {
            slot.seq.store(0, std::memory_order_relaxed);
            slot.stamp.store(0, std::memory_order_relaxed);
            slot.payload.store(0, std::memory_order_relaxed);
        }
        m_head.store(0, std::memory_order_release);
    }

private:
    struct Slot {
        std::atomic<std::uint32_t> seq{0};
        std::atomic<std::uint64_t> stamp{0};
        std::atomic<std::uint64_t> payload{0};
    };

    std::array<Slot, kCapacity> m_slots{};
    std::atomic<std::uint64_t> m_head{0};
};

//---------------------------------------------------------------------------
// Payload packing shared by the telemetry modules:
//   low 32 bits  : the character (char32_t)
//   bit 32       : isBackspace
//   bit 33       : isVietnameseMark
//   bit 34       : isToneKey
//---------------------------------------------------------------------------
[[nodiscard]] inline std::uint64_t packKeyPayload(std::uint32_t ch, bool isBackspace,
                                                 bool isVietnameseMark, bool isToneKey) noexcept {
    std::uint64_t payload = ch;
    payload |= static_cast<std::uint64_t>(isBackspace ? 1u : 0u) << 32;
    payload |= static_cast<std::uint64_t>(isVietnameseMark ? 1u : 0u) << 33;
    payload |= static_cast<std::uint64_t>(isToneKey ? 1u : 0u) << 34;
    return payload;
}

inline void unpackKeyPayload(std::uint64_t payload, std::uint32_t& ch, bool& isBackspace,
                             bool& isVietnameseMark, bool& isToneKey) noexcept {
    ch = static_cast<std::uint32_t>(payload & 0xFFFFFFFFu);
    isBackspace = ((payload >> 32) & 1u) != 0u;
    isVietnameseMark = ((payload >> 33) & 1u) != 0u;
    isToneKey = ((payload >> 34) & 1u) != 0u;
}

} // namespace ok
