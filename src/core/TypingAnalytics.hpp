//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey - refactored and completed logic
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
// File: src/core/TypingAnalytics.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — TypingAnalytics.hpp
// Real-time typing telemetry + fact/heuristic coaching.
//
// v1.3.0 fixes:
//   * `observeKey()` bumped an atomic index and then wrote a plain struct while
//     `computeSnapshot()` read the same struct from the UI thread: a data race
//     (torn entries, garbage intervals) despite the "no allocations" comment.
//     The ring is now a `SeqRing` (defined atomics only, lock-free).
//   * `computeSnapshot()` allocated three vectors per call; snapshots are now
//     computed straight from the ring into fixed member scratch buffers.
//   * A per-session view was impossible (there was only `reset()`), so the
//     coach could not report "this session". `beginSession()` exists now.
//----------------------------------------------------------------------------
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "SeqRing.hpp"

namespace ok::analytics {

struct KeyLogEntry {
    char32_t ch = 0;
    std::uint64_t timestampUs = 0;
    bool isBackspace = false;
    bool isVietnameseMark = false;
    bool isToneKey = false;
};

struct AnalyticsSnapshot {
    std::uint64_t totalKeystrokes = 0;
    std::uint64_t totalChars = 0;
    std::uint64_t totalBackspaces = 0;
    std::uint64_t sessionDurationMs = 0;
    double netWpm = 0.0;
    double rawWpm = 0.0;
    double accuracyPercent = 100.0;
    // Inter-key intervals (milliseconds)
    double meanIkiMs = 0.0;
    double stddevIkiMs = 0.0;
    double p50IkiMs = 0.0;
    double p90IkiMs = 0.0;
    double p99IkiMs = 0.0;
    // Bursts and pauses
    std::uint32_t burstCount = 0;
    double avgBurstLength = 0.0;
    std::uint32_t pauseCount = 0;
    double avgPauseMs = 0.0;
    // Backspace & correction behaviour
    double avgRecoveryLatencyMs = 0.0;
    // Vietnamese Telex patterns
    double avgToneDelayMs = 0.0;
    std::uint32_t toneKeyCount = 0;
    // Fatigue slope (negative = decaying speed over the window)
    double fatigueSlope = 0.0;
    // True when at least one observation was skipped because the producer
    // overwrote the slot mid-read (diagnostic only; never a correctness issue).
    bool sampleSkipped = false;
};

enum class CoachItemCategory : std::uint8_t {
    Pacing,
    Accuracy,
    Telex,
    Fatigue,
    Burst,
};

struct CoachRecommendation {
    CoachItemCategory category;
    std::string measuredFact;      // proven statistic
    std::string heuristicAdvice;   // actionable tip
};

class TypingAnalyticsEngine {
public:
    static constexpr std::size_t kRingCapacity = ok::SeqRing::kCapacity;
    static constexpr std::uint64_t kMinSamplesForCoach = 40;

    static TypingAnalyticsEngine& instance() noexcept;

    // Hot-path call: lock-free, O(1), no allocations, no locks.
    void observeKey(char32_t ch, std::uint64_t timestampUs, bool isBackspace,
                    bool isVietnameseMark = false, bool isToneKey = false) noexcept;

    // Aggregate metrics over the whole ring.
    [[nodiscard]] AnalyticsSnapshot computeSnapshot() const;
    // Metrics restricted to the current session (since beginSession()).
    [[nodiscard]] AnalyticsSnapshot computeSessionSnapshot() const;

    // Coaching advice (empty while the sample count is below the threshold).
    [[nodiscard]] std::vector<CoachRecommendation> generateCoachingAdvice() const;

    // Resets the whole telemetry window.
    void reset() noexcept;
    // Marks the start of a new "session" window without dropping history.
    void beginSession() noexcept;
    [[nodiscard]] std::uint64_t totalObserved() const noexcept { return m_ring.head(); }

private:
    TypingAnalyticsEngine();
    ~TypingAnalyticsEngine() = default;
    TypingAnalyticsEngine(const TypingAnalyticsEngine&) = delete;
    TypingAnalyticsEngine& operator=(const TypingAnalyticsEngine&) = delete;

    AnalyticsSnapshot computeRange(std::uint64_t firstIndex, std::uint64_t total) const;

    mutable SeqRing m_ring;
    mutable std::atomic<std::uint64_t> m_sessionStartIndex{0};
    // Scratch buffers for the (single-threaded) analysis path: sized once, so
    // snapshotting allocates nothing.
    mutable std::mutex m_analysisMutex;
    mutable std::array<double, kRingCapacity> m_ikiScratch{};
    mutable std::array<std::uint64_t, kRingCapacity> m_stampScratch{};
    mutable std::array<std::uint64_t, kRingCapacity> m_payloadScratch{};
};

} // namespace ok::analytics
