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
// Real-time aggregate typing analytics & intelligent coaching engine.
//
// DESIGN & ISOLATION CONTRACT:
//   * ZERO-ALLOCATION OBSERVATION: `observeKey()` records timestamped events
//     into a fixed-size ring buffer with atomic head/tail counters.
//   * STRICT FACT VS HEURISTIC SEPARATION: Coach suggestions explicitly
//     distinguish between measured mathematical facts and heuristic advice.
//   * MINIMUM SAMPLE SIZE: No advice is given until sufficient statistical
//     significance is gathered (default: 50+ keystrokes).
//----------------------------------------------------------------------------
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace ok::analytics {

struct KeyLogEntry {
    char32_t ch = 0;
    uint64_t timestampUs = 0;
    bool isBackspace = false;
    bool isVietnameseMark = false;
    bool isToneKey = false;
};

struct AnalyticsSnapshot {
    uint64_t totalKeystrokes = 0;
    uint64_t totalChars = 0;
    uint64_t totalBackspaces = 0;
    uint64_t sessionDurationMs = 0;

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
    uint32_t burstCount = 0;
    double avgBurstLength = 0.0;
    uint32_t pauseCount = 0;
    double avgPauseMs = 0.0;

    // Backspace & correction behavior
    double avgRecoveryLatencyMs = 0.0;

    // Vietnamese Telex patterns
    double avgToneDelayMs = 0.0;
    uint32_t toneKeyCount = 0;

    // Fatigue slope (negative = decaying speed over session)
    double fatigueSlope = 0.0;
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
    std::string measuredFact;     // Proven statistic
    std::string heuristicAdvice;  // Actionable tip
};

class TypingAnalyticsEngine {
public:
    static constexpr size_t kRingCapacity = 1024;
    static constexpr uint64_t kMinSamplesForCoach = 40;

    static TypingAnalyticsEngine& instance() noexcept;

    // Hot-path call: non-blocking, O(1), no memory allocations
    void observeKey(
        char32_t ch,
        uint64_t timestampUs,
        bool isBackspace,
        bool isVietnameseMark = false,
        bool isToneKey = false) noexcept;

    // Compute metrics snapshot
    AnalyticsSnapshot computeSnapshot();

    // Generate intelligent coaching advice (empty if sample count < kMinSamplesForCoach)
    std::vector<CoachRecommendation> generateCoachingAdvice();

    void reset() noexcept;

private:
    TypingAnalyticsEngine();
    ~TypingAnalyticsEngine() = default;

    std::array<KeyLogEntry, kRingCapacity> m_ring{};
    std::atomic<uint64_t> m_head{0};
    mutable std::mutex m_analysisMutex;
};

} // namespace ok::analytics
