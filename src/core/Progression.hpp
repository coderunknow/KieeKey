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
// File: src/core/Progression.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — Progression.hpp
// Global Level & Typing Progression Engine.
//
// Tracks user achievements, total characters/words/keystrokes, session time,
// deterministic XP & leveling, and minigame records.
//
// GUARANTEES:
//   * DETERMINISTIC: Level formulas and XP accumulation are 100% deterministic
//     and free of floating-point overflow (uint64 counters).
//   * RESILIENT: Corrupted profile files are rejected safely, falling back
//     to pristine state without crashing the IME.
//   * NON-BLOCKING: All stats persistence is separate from the typing hot path.
//----------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ok::progression {

enum class AchievementId : std::uint16_t {
    FirstKey = 0,
    FirstWord = 1,
    Speedster50Wpm = 2,
    Speedster80Wpm = 3,
    Century100Wpm = 4,
    TenThousandKeys = 5,
    HundredThousandKeys = 6,
    MillionKeys = 7,
    SnakeNovice = 8,
    SnakeMaster = 9,
    TetrisLover = 10,
    TetrisGrandMaster = 11,
    FirstFish = 12,
    LegendaryAngler = 13,
    RhythmStreak50 = 14,
    NoMistakeCenturion = 15,
    Count = 16,
};

struct AchievementInfo {
    AchievementId id;
    const char* title;
    const char* description;
    uint32_t xpReward;
};

struct ProgressionStats {
    uint64_t totalCharacters = 0;
    uint64_t totalWords = 0;
    uint64_t totalKeystrokes = 0;
    uint64_t totalSessions = 0;
    uint64_t typingTimeSeconds = 0;

    double bestWpm = 0.0;
    double bestAccuracy = 0.0;

    uint32_t currentStreakDays = 0;
    uint32_t maxStreakDays = 0;

    uint64_t totalXp = 0;
    uint32_t currentLevel = 1;

    // Minigame records
    int64_t snakeHighScore = 0;
    int64_t tetrisHighScore = 0;
    uint64_t fishCaughtCount = 0;
    uint64_t legendaryFishCount = 0;
    double typingRaceBestWpm = 0.0;
    int64_t wasdRaceHighScore = 0;
    int64_t rhythmHighScore = 0;
    uint64_t noMistakeMaxCombo = 0;

    // Unlocked achievements bitmask (up to 64 achievements)
    uint64_t achievementsUnlocked = 0;

    void reset() noexcept;
};

class ProgressionEngine {
public:
    static ProgressionEngine& instance() noexcept;

    ProgressionStats getStats() const;
    void recordTypingSession(
        uint64_t chars,
        uint64_t words,
        uint64_t keystrokes,
        uint64_t durationSeconds,
        double wpm,
        double accuracy);

    void addXp(uint64_t xp);
    void recordSnakeScore(int64_t score);
    void recordTetrisScore(int64_t score);
    void recordFishCaught(bool isLegendary);
    void recordTypingRace(double wpm);
    void recordWasdRaceScore(int64_t score);
    void recordRhythmScore(int64_t score, uint32_t combo);
    void recordNoMistakeCombo(uint64_t combo);

    bool isAchievementUnlocked(AchievementId id) const noexcept;
    std::vector<AchievementInfo> getUnlockedAchievements() const;
    static AchievementInfo getAchievementInfo(AchievementId id) noexcept;

    // Deterministic formula calculations
    static uint32_t calculateLevel(uint64_t xp) noexcept;
    static uint64_t xpRequiredForLevel(uint32_t level) noexcept;
    static uint64_t xpRemainingToNextLevel(uint64_t currentXp) noexcept;

    // Serialization & resilience
    std::string serialize() const;
    bool deserialize(std::string_view data);
    void reset();

    // File persistence
    bool saveToFile(std::string_view path) const;
    bool loadFromFile(std::string_view path);

private:
    ProgressionEngine();
    ~ProgressionEngine() = default;

    void checkAchievementsLocked();

    mutable std::mutex m_mutex;
    ProgressionStats m_stats{};
};

} // namespace ok::progression
