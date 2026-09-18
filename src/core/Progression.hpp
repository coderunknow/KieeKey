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
// Global level, lifetime statistics, achievements and daily streaks
// ("📈 Level tăng vì gõ nhiều").
//
// v1.3.0 fixes:
//   * The hot path used `recordTypingSession(1, 0, 1, 0, 0.0, 100.0)` per
//     keystroke from the hook thread — a mutex + full stats rewrite *per key*,
//     and `totalSessions` grew by one for every character typed. Keystroke
//     accounting is now lock-free (`recordKeystroke`) and merged into the
//     persisted stats by `flushStats()` on the UI thread.
//   * `totalWords` was never fed, so the "First word" achievement was
//     unreachable; words are counted now.
//   * `typingTimeSeconds` and the day streak never advanced. Both are tracked.
//   * `loadFromFile()` could not distinguish "no file yet" from "corrupt file",
//     and a corrupt file was silently ignored; there is a `LoadOutcome` now
//     plus a best-effort `salvageDeserialize()` for recoverable damage.
//----------------------------------------------------------------------------
#pragma once

#include <atomic>
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
    std::uint32_t xpReward;
};

struct ProgressionStats {
    std::uint64_t totalCharacters = 0;
    std::uint64_t totalWords = 0;
    std::uint64_t totalKeystrokes = 0;
    std::uint64_t totalSessions = 0;
    std::uint64_t typingTimeSeconds = 0;
    double bestWpm = 0.0;
    double bestAccuracy = 0.0;
    std::uint32_t currentStreakDays = 0;
    std::uint32_t maxStreakDays = 0;
    std::uint64_t totalXp = 0;
    std::uint32_t currentLevel = 1;
    // Minigame records
    std::int64_t snakeHighScore = 0;
    std::int64_t tetrisHighScore = 0;
    std::uint64_t fishCaughtCount = 0;
    std::uint64_t legendaryFishCount = 0;
    double typingRaceBestWpm = 0.0;
    std::int64_t wasdRaceHighScore = 0;
    std::int64_t rhythmHighScore = 0;
    std::uint64_t noMistakeMaxCombo = 0;
    // Progression accounting
    std::uint32_t lastActiveDayIndex = 0;      // days since 1970-01-01 (UTC)
    std::uint64_t achievementsUnlocked = 0;    // bitmask (up to 64 achievements)

    void reset() noexcept;
};

enum class LoadOutcome : std::uint8_t {
    Ok = 0,
    FileMissing = 1,
    Corrupt = 2,
    Recovered = 3,   // parsed with the checksum failing (salvage path)
};

class ProgressionEngine {
public:
    static ProgressionEngine& instance() noexcept;

    [[nodiscard]] ProgressionStats getStats() const;

    //---- hot path (hook thread) -------------------------------------------
    // Lock-free, allocation-free: only relaxed atomics are touched. The values
    // are merged into the persisted stats by flushStats().
    void recordKeystroke(bool isBackspace = false, std::uint32_t chars = 1,
                         std::uint32_t words = 0) noexcept;
    // Active typing time (hook thread, lock-free) — feeds typingTimeSeconds.
    void recordActiveTimeMs(std::uint64_t ms) noexcept;

    // Merge the pending lock-free counters into the stats and re-evaluate
    // achievements/level. Call from the UI timer (e.g. twice a second).
    void flushStats() noexcept;
    [[nodiscard]] std::uint64_t pendingKeystrokes() const noexcept;

    //---- session-level accounting (UI thread) ------------------------------
    void recordTypingSession(std::uint64_t chars, std::uint64_t words, std::uint64_t keystrokes,
                             std::uint64_t durationSeconds, double wpm, double accuracy);
    // Ends a live session: counts it once, records best WPM/accuracy.
    void endSession(double wpm, double accuracy) noexcept;

    void addXp(std::uint64_t xp);

    //---- minigame results ---------------------------------------------------
    void recordSnakeScore(std::int64_t score);
    void recordTetrisScore(std::int64_t score);
    void recordFishCaught(bool isLegendary);
    void recordTypingRace(double wpm);
    void recordWasdRaceScore(std::int64_t score);
    void recordRhythmScore(std::int64_t score, std::uint32_t combo);
    void recordNoMistakeCombo(std::uint64_t combo);
    // Generic entry point used by the Arcade Hub result pump.
    void recordArcadeRun(int gameTypeId, std::int64_t score, std::uint32_t maxCombo,
                         double wpm);

    [[nodiscard]] bool isAchievementUnlocked(AchievementId id) const noexcept;
    [[nodiscard]] std::vector<AchievementInfo> getUnlockedAchievements() const;
    [[nodiscard]] static AchievementInfo getAchievementInfo(AchievementId id) noexcept;
    [[nodiscard]] static std::vector<AchievementInfo> getAllAchievements();

    //---- day streaks --------------------------------------------------------
    [[nodiscard]] static std::uint32_t dayIndexFromUnixSeconds(std::uint64_t unixSeconds) noexcept;
    void touchDailyStreak(std::uint32_t dayIndex) noexcept;

    //---- level-up signal (consumed by the UI) ------------------------------
    [[nodiscard]] bool consumeLevelUp(std::uint32_t& newLevel) noexcept;

    //---- deterministic formulas --------------------------------------------
    [[nodiscard]] static std::uint32_t calculateLevel(std::uint64_t xp) noexcept;
    [[nodiscard]] static std::uint64_t xpRequiredForLevel(std::uint32_t level) noexcept;
    [[nodiscard]] static std::uint64_t xpRemainingToNextLevel(std::uint64_t currentXp) noexcept;
    [[nodiscard]] static std::uint64_t xpIntoCurrentLevel(std::uint64_t currentXp) noexcept;
    [[nodiscard]] static std::uint64_t xpSpanOfCurrentLevel(std::uint64_t currentXp) noexcept;

    //---- serialization & resilience ----------------------------------------
    [[nodiscard]] std::string serialize() const;
    bool deserialize(std::string_view data);
    // Best-effort parse that ignores a failing checksum (used after
    // `deserialize()` reported Corrupt).
    bool salvageDeserialize(std::string_view data);

    bool saveToFile(std::string_view path) const;
    bool loadFromFile(std::string_view path, LoadOutcome* outcome = nullptr);
    void reset();

private:
    ProgressionEngine();
    ~ProgressionEngine() = default;
    ProgressionEngine(const ProgressionEngine&) = delete;
    ProgressionEngine& operator=(const ProgressionEngine&) = delete;

    void checkAchievementsLocked();
    void applyLevelLocked();
    static bool parsePayload(std::string_view payload, ProgressionStats& out);
    static bool applyField(ProgressionStats& stats, const std::string& key, const std::string& value);

    mutable std::mutex m_mutex;
    ProgressionStats m_stats{};

    // Lock-free pending counters (hook thread → flushStats()).
    std::atomic<std::uint64_t> m_pendingChars{0};
    std::atomic<std::uint64_t> m_pendingWords{0};
    std::atomic<std::uint64_t> m_pendingKeystrokes{0};
    std::atomic<std::uint64_t> m_pendingBackspaces{0};
    std::atomic<std::uint64_t> m_pendingTimeMs{0};
    // Sub-second remainder of the accumulated typing time. Without it every
    // flush truncated `ms / 1000`, so a session that reported time in small
    // batches (the normal case: a flush every UI tick) lost up to a second per
    // flush and the "typing time" statistic ran far behind reality.
    std::uint64_t m_timeCarryMs = 0;
    std::atomic<std::uint32_t> m_pendingLevelUp{0};
};

} // namespace ok::progression
