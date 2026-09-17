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
// Personal AI Typing Rival: Learning typing biometrics and rival simulation.
//
// PRIVACY & RESOURCE PRINCIPLES:
//   * EXPLICIT OPT-IN: Disabled by default. No data is gathered unless user
//     grants permission.
//   * 100% LOCAL: All computations run on-device. No network access.
//   * OFFLINE & ASYNCHRONOUS: Hot path only enqueues raw timestamps; all
//     model fitting occurs off the critical input path.
//   * INSTANT PURGE: User can completely reset or wipe their AI profile anytime.
//----------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ok::ai {

struct AiProfile {
    uint64_t sampleCount = 0;
    double meanIkiMs = 150.0;         // ~80 WPM default baseline
    double stddevIkiMs = 35.0;
    double avgBurstLength = 6.0;
    double pauseThresholdMs = 350.0;
    double errorRate = 0.025;         // 2.5% natural typo rate
    double backspaceRecoveryMs = 260.0;
    double toneDelayMs = 175.0;       // Reaction time for Vietnamese accents
    double speedCurveSlope = 0.0;

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
};

struct SimulatedKeystroke {
    char32_t ch = 0;
    uint32_t delayMs = 0;
    bool isTypo = false;
    bool isBackspaceCorrection = false;
};

class AiRivalEngine {
public:
    static AiRivalEngine& instance() noexcept;

    // Explicit opt-in controls
    void setOptIn(bool optIn) noexcept;
    [[nodiscard]] bool isOptIn() const noexcept {
        return m_optIn.load(std::memory_order_relaxed);
    }

    // Input path notification (O(1), lock-free atomic/ring)
    void observeKeystroke(
        char32_t ch,
        uint64_t timestampUs,
        bool isBackspace,
        bool isToneKey) noexcept;

    // Fit model from accumulated observations
    void trainBatch();

    // Access current learned model
    AiProfile getProfile() const;
    void resetProfile();

    // Generate simulated typing keystrokes mimicking the player's profile
    std::vector<SimulatedKeystroke> simulateTypingRun(
        std::u32string_view targetText,
        uint32_t seed = 0) const;

    // "Yesterday You" ghost tracking
    void recordGhostRun(const std::vector<uint32_t>& charTimeOffsetsMs);
    std::vector<uint32_t> getYesterdayGhost() const;

    // Profile persistence
    std::string serializeProfile() const;
    bool deserializeProfile(std::string_view data);

    AiRivalEngine();
    ~AiRivalEngine() = default;

private:
    std::atomic<bool> m_optIn{false};

    mutable std::mutex m_mutex;
    AiProfile m_profile{};

    struct RawObs {
        char32_t ch;
        uint64_t timestampUs;
        bool isBackspace;
        bool isToneKey;
    };
    std::vector<RawObs> m_pendingObs;

    std::vector<uint32_t> m_yesterdayGhost;
};

} // namespace ok::ai
