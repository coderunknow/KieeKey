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
// File: src/core/AiRival.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — AiRival.hpp
// Personal AI typing rival: learns YOUR dynamics (inter-key intervals, pauses,
// bursts, tone-key reaction, typo rate) and then races you with them.
//
// v1.3.0 fixes:
//   * The hot path (`observeKeystroke`) took a mutex and pushed into a growing
//     std::vector — i.e. the "O(1), lock-free" claim in the header comment was
//     false and the hook thread could block on the UI thread's allocation.
//     Observations now go through a `SeqRing` (allocation-free, lock-free).
//   * `pauseThresholdMs` and `speedCurveSlope` were never updated, so two of
//     the eight "learned" fields were constants. Both are now fitted.
//   * Deserialization accepted NaN/negative/huge values straight into the
//     profile (a hand-edited file could produce an infinite WPM rival); all
//     fields are now clamped to sane ranges.
//   * The "Yesterday you" ghost was lost on restart; it is part of the
//     serialized profile now.
//   * NEW: `AiRacer` — an actual head-to-head opponent. The rival replays your
//     learned timing through the passage and (with `aggression > 1`) pushes to
//     beat you, which is what "AI học chính m rồi đòi đua thắng m" describes.
//----------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "SeqRing.hpp"

namespace ok::ai {

struct AiProfile {
    std::uint64_t sampleCount = 0;
    double meanIkiMs = 150.0;         // ~80 WPM default baseline
    double stddevIkiMs = 35.0;
    double avgBurstLength = 6.0;
    double pauseThresholdMs = 350.0;
    double errorRate = 0.025;         // 2.5% natural typo rate
    double backspaceRecoveryMs = 260.0;
    double toneDelayMs = 175.0;       // reaction time for Vietnamese accents
    double speedCurveSlope = 0.0;     // ms added per 100 characters typed (fatigue)

    void reset() noexcept {
        sampleCount = 0;
        meanIkiMs = 150.0;
        stddevIkiMs = 35.0;
        avgBurstLength = 6.0;
        pauseThresholdMs = 350.0;
        errorRate = 0.025;
        backspaceRecoveryMs = 260.0;
        toneDelayMs = 175.0;
        speedCurveSlope = 0.0;
    }
    [[nodiscard]] double expectedWpm() const noexcept {
        const double iki = (meanIkiMs > 1.0) ? meanIkiMs : 1.0;
        return 60000.0 / (5.0 * iki);
    }
};

struct SimulatedKeystroke {
    char32_t ch = 0;
    std::uint32_t delayMs = 0;
    bool isTypo = false;
    bool isBackspaceCorrection = false;
};

//---------------------------------------------------------------------------
// Race configuration: how hard the rival tries.
//---------------------------------------------------------------------------
struct AiRaceConfig {
    double aggression = 1.05;    // 1.0 = exactly your pace, 1.2 = 20 % faster
    double typoFactor = 1.0;     // scales the learned typo rate (0 = flawless)
    std::uint32_t seed = 0;      // 0 = derive from the profile
    bool allowTypos = true;
    [[nodiscard]] double effectiveSpeedMultiplier() const noexcept {
        return aggression <= 0.0 ? 1.0 : aggression;
    }
};

//---------------------------------------------------------------------------
// AiRacer — deterministic, frame-rate independent rival progress
//---------------------------------------------------------------------------
class AiRacer {
public:
    AiRacer() = default;

    void reset(std::u32string_view passage, const AiProfile& profile, const AiRaceConfig& config);
    void update(double dt);

    [[nodiscard]] std::size_t getCharIndex() const noexcept { return m_charIndex; }
    [[nodiscard]] double getProgress() const noexcept;      // 0.0 .. 1.0
    [[nodiscard]] double getElapsedSec() const noexcept { return m_elapsedSec; }
    [[nodiscard]] double getWpm() const noexcept;
    [[nodiscard]] bool isFinished() const noexcept { return m_finished; }
    [[nodiscard]] double getFinishTimeSec() const noexcept { return m_finishTimeSec; }
    [[nodiscard]] std::uint32_t getTypoCount() const noexcept { return m_typos; }
    [[nodiscard]] std::size_t getPassageLength() const noexcept { return m_passageLength; }
    [[nodiscard]] std::uint32_t getScheduleEntryCount() const noexcept {
        return static_cast<std::uint32_t>(m_scheduleMs.size());
    }

private:
    std::vector<std::uint32_t> m_scheduleMs;   // cumulative ms per emitted character
    std::size_t m_passageLength = 0;
    std::size_t m_charIndex = 0;
    double m_elapsedSec = 0.0;
    double m_finishTimeSec = 0.0;
    std::uint32_t m_typos = 0;
    bool m_finished = false;
};

//---------------------------------------------------------------------------
// AiRivalEngine
//---------------------------------------------------------------------------
class AiRivalEngine {
public:
    static AiRivalEngine& instance() noexcept;

    // Explicit opt-in controls (off by default; opting out purges everything).
    void setOptIn(bool optIn) noexcept;
    [[nodiscard]] bool isOptIn() const noexcept {
        return m_optIn.load(std::memory_order_relaxed);
    }

    // Input-path notification: lock-free, allocation-free, O(1).
    void observeKeystroke(char32_t ch, std::uint64_t timestampUs, bool isBackspace,
                          bool isToneKey) noexcept;

    // Fit the model from the accumulated observations (UI thread).
    void trainBatch();

    [[nodiscard]] AiProfile getProfile() const;
    void resetProfile();

    // Generate a simulated typing run through `targetText`.
    std::vector<SimulatedKeystroke> simulateTypingRun(std::u32string_view targetText,
                                                     std::uint32_t seed = 0) const;

    // "Yesterday you" ghost (character completion offsets in ms).
    void recordGhostRun(const std::vector<std::uint32_t>& charTimeOffsetsMs);
    [[nodiscard]] std::vector<std::uint32_t> getYesterdayGhost() const;

    // Profile persistence (includes the ghost + the learned counters).
    [[nodiscard]] std::string serializeProfile() const;
    bool deserializeProfile(std::string_view data);

    // Convenience: build a rival opponent for `passage`.
    [[nodiscard]] AiRacer makeRacer(std::u32string_view passage,
                                    const AiRaceConfig& config) const;

    [[nodiscard]] std::size_t pendingObservationCount() const noexcept {
        return static_cast<std::size_t>(m_ring.head());
    }

    AiRivalEngine();
    ~AiRivalEngine() = default;
    AiRivalEngine(const AiRivalEngine&) = delete;
    AiRivalEngine& operator=(const AiRivalEngine&) = delete;

private:
    std::atomic<bool> m_optIn{false};
    mutable std::mutex m_profileMutex;         // guards m_profile + m_ghost
    AiProfile m_profile{};
    std::vector<std::uint32_t> m_ghost;
    SeqRing m_ring;                            // producer: hook thread
    std::atomic<std::uint64_t> m_lastObservedStamp{0};
    std::atomic<std::uint64_t> m_observedCount{0};
    // Ring index up to which the profile has already been fitted. Without it,
    // every `trainBatch()` (called once per UI refresh) re-fitted the *same*
    // samples: `sampleCount` inflated without bound and each observation was
    // learned many times over, biasing the running average.
    std::atomic<std::uint64_t> m_trainedIndex{0};
};

} // namespace ok::ai
