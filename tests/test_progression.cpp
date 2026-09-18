//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/test_progression.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
// Progression & level suite (v1.3.0).
//
//  1. Level/XP formulas   — deterministic, monotonic, no overflow
//  2. Hot path            — lock-free accumulation + flush semantics
//  3. Achievements        — table integrity, unlock rules, level-up events
//  4. Streaks             — consecutive-day bookkeeping
//  5. Minigame records    — per-game highscores feed the right counters
//  6. Persistence         — checksum, salvage, LoadOutcome reporting
//  7. Concurrency         — hook thread vs UI thread: no lost keystrokes
//============================================================================
#include "Progression.hpp"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <thread>

using namespace ok::progression;

namespace {

std::string readAll(const std::string& path) {
    std::ifstream ifs(path);
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

void writeAll(const std::string& path, const std::string& data) {
    std::ofstream ofs(path, std::ios::trunc);
    ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
}

} // namespace

//---------------------------------------------------------------------------
void testLevelFormulas() {
    // Level 1 starts at 0 XP
    assert(ProgressionEngine::calculateLevel(0) == 1);
    assert(ProgressionEngine::calculateLevel(50) == 1);

    // XP requirements must monotonically increase
    uint64_t prev = 0;
    for (uint32_t lvl = 2; lvl <= 50; ++lvl) {
        uint64_t req = ProgressionEngine::xpRequiredForLevel(lvl);
        assert(req > prev);
        prev = req;
        assert(ProgressionEngine::calculateLevel(req) == lvl);
    }

    // Helper consistency: level base + progress + remaining == next threshold.
    for (std::uint64_t xp : {0ull, 1ull, 99ull, 100ull, 5000ull, 250000ull, 9999999ull}) {
        const std::uint32_t level = ProgressionEngine::calculateLevel(xp);
        const std::uint64_t base = ProgressionEngine::xpRequiredForLevel(level);
        const std::uint64_t into = ProgressionEngine::xpIntoCurrentLevel(xp);
        const std::uint64_t span = ProgressionEngine::xpSpanOfCurrentLevel(xp);
        const std::uint64_t left = ProgressionEngine::xpRemainingToNextLevel(xp);
        assert(xp >= base);
        assert(into + left == span);
        assert(into <= span);
    }

    // Absurd XP must saturate instead of overflowing / looping forever.
    const std::uint64_t huge = ~0ull;
    const std::uint32_t topLevel = ProgressionEngine::calculateLevel(huge);
    assert(topLevel > 1);
    assert(ProgressionEngine::calculateLevel(ProgressionEngine::xpRequiredForLevel(topLevel)) >=
           topLevel - 1);

    std::cout << "  [PASS] Level & XP deterministic formulas\n";
}

//---------------------------------------------------------------------------
void testHotPathAccumulation() {
    auto& prog = ProgressionEngine::instance();
    prog.reset();
    assert(prog.pendingKeystrokes() == 0);

    // The hook-thread API must be fire-and-forget: nothing is visible in the
    // aggregate until flushStats() runs on the UI side.
    for (int i = 0; i < 10; ++i) {
        prog.recordKeystroke(false, 1, (i % 6 == 0) ? 1u : 0u);
    }
    prog.recordKeystroke(/*isBackspace=*/true);
    prog.recordActiveTimeMs(4000);
    assert(prog.pendingKeystrokes() == 11);
    assert(prog.getStats().totalKeystrokes == 0);

    prog.flushStats();
    auto stats = prog.getStats();
    assert(stats.totalCharacters == 10);
    assert(stats.totalKeystrokes == 11);
    assert(stats.typingTimeSeconds == 4);
    assert(stats.totalWords >= 1);
    assert(prog.pendingKeystrokes() == 0);

    // A second flush with nothing pending must not double-count.
    prog.flushStats();
    assert(prog.getStats().totalKeystrokes == 11);
    assert(prog.getStats().totalCharacters == 10);

    // XP is awarded per character, +50 % when accuracy stays >= 95 %.
    // (Both runs below unlock the same achievements, so the *delta* isolates
    // the typing XP from the one-off achievement rewards.)
    prog.reset();
    for (int i = 0; i < 100; ++i) { prog.recordKeystroke(); }
    prog.flushStats();
    const std::uint64_t cleanXp = prog.getStats().totalXp;
    assert(cleanXp >= 150);   // 100 typing XP + 50 % accuracy bonus

    // Accuracy below the threshold: no bonus.
    prog.reset();
    for (int i = 0; i < 100; ++i) { prog.recordKeystroke(); }
    for (int i = 0; i < 10; ++i) { prog.recordKeystroke(true); }
    prog.flushStats();
    assert(prog.getStats().totalCharacters == 100);
    assert(prog.getStats().totalKeystrokes == 110);
    assert(cleanXp - prog.getStats().totalXp == 50);   // exactly the lost bonus

    // 50 chars + 20 backspaces is a 71 % accuracy run: character XP only.
    prog.reset();
    for (int i = 0; i < 50; ++i) { prog.recordKeystroke(); }
    for (int i = 0; i < 20; ++i) { prog.recordKeystroke(true); }
    prog.flushStats();
    assert(prog.getStats().totalCharacters == 50);
    assert(prog.getStats().totalKeystrokes == 70);

    prog.reset();
    std::cout << "  [PASS] Hot-path accumulation & flush semantics\n";
}

//---------------------------------------------------------------------------
void testTypingAccumulationAndAchievements() {
    auto& prog = ProgressionEngine::instance();
    prog.reset();
    assert(prog.getStats().totalKeystrokes == 0);

    // Record typing session
    prog.recordTypingSession(500, 100, 520, 60, 105.0, 96.0);
    auto stats = prog.getStats();
    assert(stats.totalCharacters == 500);
    assert(stats.totalWords == 100);
    assert(stats.bestWpm == 105.0);
    assert(stats.bestAccuracy == 96.0);
    assert(stats.totalXp > 0);
    assert(stats.totalSessions == 1);
    assert(stats.typingTimeSeconds == 60);

    // A slower session must not lower the bests.
    prog.recordTypingSession(100, 20, 100, 10, 40.0, 80.0);
    stats = prog.getStats();
    assert(stats.bestWpm == 105.0);
    assert(stats.bestAccuracy == 96.0);
    assert(stats.totalSessions == 2);

    // Achievements should have fired: FirstKey, FirstWord, 50Wpm, 80Wpm, 100Wpm
    assert(prog.isAchievementUnlocked(AchievementId::FirstKey));
    assert(prog.isAchievementUnlocked(AchievementId::FirstWord));
    assert(prog.isAchievementUnlocked(AchievementId::Century100Wpm));
    assert(!prog.isAchievementUnlocked(AchievementId::MillionKeys));

    // Unlocked achievements carry their reward XP and are unique.
    const auto unlocked = prog.getUnlockedAchievements();
    assert(!unlocked.empty());
    const auto all = ProgressionEngine::getAllAchievements();
    assert(all.size() == static_cast<std::size_t>(AchievementId::Count));
    for (const auto& info : all) {
        assert(info.title != nullptr && info.title[0] != '\0');
        assert(info.description != nullptr && info.description[0] != '\0');
        assert(info.xpReward > 0);
    }
    for (std::size_t i = 0; i < all.size(); ++i) {
        for (std::size_t j = i + 1; j < all.size(); ++j) {
            assert(all[i].id != all[j].id);   // no duplicate ids in the table
        }
    }

    // Minigame score recording
    prog.recordSnakeScore(2500);
    assert(prog.isAchievementUnlocked(AchievementId::SnakeMaster));
    assert(prog.getStats().snakeHighScore == 2500);
    prog.recordSnakeScore(10);                       // lower: keeps the record
    assert(prog.getStats().snakeHighScore == 2500);

    prog.recordFishCaught(true); // Legendary fish
    assert(prog.isAchievementUnlocked(AchievementId::LegendaryAngler));
    assert(prog.getStats().fishCaughtCount == 1);
    assert(prog.getStats().legendaryFishCount == 1);

    // Achievements are awarded once: XP must not grow on a repeat unlock.
    const std::uint64_t xpBefore = prog.getStats().totalXp;
    prog.recordFishCaught(true);
    assert(prog.getStats().totalXp > xpBefore);      // fish XP, not achievement XP
    const std::uint64_t xpAfterSecondFish = prog.getStats().totalXp;
    prog.recordFishCaught(true);
    assert(prog.getStats().totalXp - xpAfterSecondFish == 500);   // exactly one fish reward

    std::cout << "  [PASS] Typing accumulation & achievements\n";
}

//---------------------------------------------------------------------------
void testLevelUpEvents() {
    auto& prog = ProgressionEngine::instance();
    prog.reset();

    std::uint32_t level = 0;
    assert(!prog.consumeLevelUp(level));            // nothing pending at start

    // Cross the level-2 threshold (100 XP at level 2).
    prog.addXp(ProgressionEngine::xpRequiredForLevel(2));
    assert(prog.consumeLevelUp(level));
    assert(level == 2);
    assert(!prog.consumeLevelUp(level));            // delivered exactly once
    assert(prog.getStats().currentLevel == 2);

    // A big jump must report the *final* level, not every intermediate one.
    prog.reset();
    prog.addXp(ProgressionEngine::xpRequiredForLevel(6));
    assert(prog.consumeLevelUp(level));
    assert(level == 6);
    assert(!prog.consumeLevelUp(level));

    prog.reset();
    assert(prog.getStats().currentLevel == 1);
    assert(prog.getStats().totalXp == 0);
    std::cout << "  [PASS] Level-up event tests\n";
}

//---------------------------------------------------------------------------
void testDailyStreaks() {
    auto& prog = ProgressionEngine::instance();
    prog.reset();

    assert(ProgressionEngine::dayIndexFromUnixSeconds(0) == 0);
    assert(ProgressionEngine::dayIndexFromUnixSeconds(86399) == 0);
    assert(ProgressionEngine::dayIndexFromUnixSeconds(86400) == 1);
    // 2026-09-18 00:00:00 UTC => day 20714
    assert(ProgressionEngine::dayIndexFromUnixSeconds(1789689600ull) == 20714);
    assert(ProgressionEngine::dayIndexFromUnixSeconds(1789775999ull) == 20714);

    prog.touchDailyStreak(20714);
    assert(prog.getStats().currentStreakDays == 1);
    prog.touchDailyStreak(20714);                   // same day: idempotent
    assert(prog.getStats().currentStreakDays == 1);
    prog.touchDailyStreak(20715);
    assert(prog.getStats().currentStreakDays == 2);
    prog.touchDailyStreak(20716);
    assert(prog.getStats().currentStreakDays == 3);
    prog.touchDailyStreak(20720);                   // gap: back to 1
    assert(prog.getStats().currentStreakDays == 1);
    assert(prog.getStats().maxStreakDays == 3);
    prog.touchDailyStreak(0);                       // "unknown": ignored
    assert(prog.getStats().currentStreakDays == 1);

    std::cout << "  [PASS] Daily streak tests\n";
}

//---------------------------------------------------------------------------
void testMinigameRecords() {
    auto& prog = ProgressionEngine::instance();
    prog.reset();

    prog.recordArcadeRun(/*snake=*/1, 3000, 0, 0.0);
    assert(prog.getStats().snakeHighScore == 3000);
    prog.recordArcadeRun(/*tetris=*/2, 12000, 0, 0.0);
    assert(prog.getStats().tetrisHighScore == 12000);
    prog.recordArcadeRun(/*typingRace=*/4, 0, 0, 88.5);
    assert(prog.getStats().typingRaceBestWpm > 88.0 && prog.getStats().typingRaceBestWpm < 89.0);
    prog.recordArcadeRun(/*wasdRace=*/5, 4200, 0, 0.0);
    assert(prog.getStats().wasdRaceHighScore == 4200);
    prog.recordArcadeRun(/*rhythm=*/6, 9000, 60, 0.0);
    assert(prog.getStats().rhythmHighScore == 9000);
    prog.recordArcadeRun(/*noMistake=*/7, 0, 120, 0.0);
    assert(prog.getStats().noMistakeMaxCombo == 120);
    assert(prog.isAchievementUnlocked(AchievementId::NoMistakeCenturion));

    // Flexing Mode (id 8) is deliberately excluded from progression.
    const auto before = prog.getStats();
    prog.recordArcadeRun(/*flexing=*/8, 999999, 999, 999.0);
    const auto after = prog.getStats();
    assert(after.totalXp == before.totalXp);
    assert(after.noMistakeMaxCombo == before.noMistakeMaxCombo);

    // Unknown ids are ignored rather than corrupting a record.
    prog.recordArcadeRun(99, 1, 1, 1.0);
    assert(prog.getStats().tetrisHighScore == 12000);

    // Achievements: 10k keys + combo thresholds.
    prog.reset();
    for (int i = 0; i < 12000; ++i) { prog.recordKeystroke(); }
    prog.flushStats();
    assert(prog.isAchievementUnlocked(AchievementId::TenThousandKeys));
    assert(!prog.isAchievementUnlocked(AchievementId::HundredThousandKeys));

    std::cout << "  [PASS] Minigame record routing tests\n";
}

//---------------------------------------------------------------------------
void testPersistenceAndCorruption() {
    auto& prog = ProgressionEngine::instance();
    prog.reset();
    prog.recordTypingSession(1234, 240, 1300, 120, 72.5, 97.5);
    prog.recordArcadeRun(1, 777, 0, 0.0);
    prog.touchDailyStreak(20714);
    prog.recordNoMistakeCombo(64);

    const std::string valid = prog.serialize();
    assert(!valid.empty());
    assert(valid.rfind("KIEEKEY_PROGRESSION_V1", 0) == 0);
    assert(valid.find("checksum=") != std::string::npos);
    assert(valid.find("totalWords=") != std::string::npos);
    assert(valid.find("typingTimeSeconds=") != std::string::npos);
    assert(valid.find("noMistakeMaxCombo=") != std::string::npos);
    assert(valid.find("lastActiveDayIndex=") != std::string::npos);

    // Corrupted data (tampered payload or bad checksum)
    std::string corrupted = valid;
    corrupted[corrupted.size() - 2] = (corrupted[corrupted.size() - 2] == '0' ? '1' : '0');
    assert(!prog.deserialize(corrupted));

    // Garbage data
    assert(!prog.deserialize("NOT_KIEEKEY_DATA"));
    assert(!prog.deserialize(""));
    assert(!prog.deserialize("KIEEKEY_PROGRESSION_V1\n"));
    assert(!prog.deserialize("KIEEKEY_PROGRESSION_V2\ntotalXp=10\n"));

    // Valid reload
    assert(prog.deserialize(valid));

    // Salvage path: the payload is readable even though the checksum is wrong.
    prog.reset();
    assert(prog.getStats().totalXp == 0);
    assert(!prog.deserialize(corrupted));
    assert(prog.salvageDeserialize(corrupted));
    const auto salvaged = prog.getStats();
    assert(salvaged.totalWords == 240);
    assert(salvaged.snakeHighScore == 777);
    assert(salvaged.noMistakeMaxCombo == 64);
    assert(salvaged.currentLevel == ProgressionEngine::calculateLevel(salvaged.totalXp));

    // File round-trip, including the LoadOutcome contract.
    const std::string path = "/tmp/kieekey_progression_test.txt";
    std::remove(path.c_str());
    LoadOutcome outcome = LoadOutcome::Ok;
    assert(!prog.loadFromFile(path, &outcome));
    assert(outcome == LoadOutcome::FileMissing);

    assert(prog.saveToFile(path));
    prog.reset();
    assert(prog.loadFromFile(path, &outcome));
    assert(outcome == LoadOutcome::Ok);
    assert(prog.getStats().totalWords == 240);

    // A file whose checksum was tampered with is recovered, not silently trusted.
    std::string onDisk = readAll(path);
    onDisk[onDisk.size() - 2] = (onDisk[onDisk.size() - 2] == '0' ? '1' : '0');
    writeAll(path, onDisk);
    prog.reset();
    assert(prog.loadFromFile(path, &outcome));
    assert(outcome == LoadOutcome::Recovered);
    assert(prog.getStats().snakeHighScore == 777);

    // Unreadable payload: reported as corrupt, machine stays usable.
    writeAll(path, "KIEEKEY_PROGRESSION_V1\n!!not a field!!\n");
    prog.reset();
    assert(!prog.loadFromFile(path, &outcome));
    assert(outcome == LoadOutcome::Corrupt);
    assert(prog.getStats().totalXp == 0);

    std::remove(path.c_str());
    prog.reset();
    std::cout << "  [PASS] Persistence, checksum & salvage tests\n";
}

//---------------------------------------------------------------------------
void testResilienceAndCorruption() {
    // Kept as the legacy entry point (v1.2.0 suite referenced it): same
    // invariants as testPersistenceAndCorruption.
    auto& prog = ProgressionEngine::instance();
    std::string valid = prog.serialize();
    assert(!valid.empty());
    std::string corrupted = valid;
    corrupted[corrupted.size() - 2] = (corrupted[corrupted.size() - 2] == '0' ? '1' : '0');
    assert(!prog.deserialize(corrupted));
    assert(!prog.deserialize("NOT_KIEEKEY_DATA"));
    assert(!prog.deserialize(""));
    assert(prog.deserialize(valid));
    std::cout << "  [PASS] Progression corruption resilience\n";
}

//---------------------------------------------------------------------------
void testConcurrentFlushLosesNothing() {
    auto& prog = ProgressionEngine::instance();
    prog.reset();

    constexpr int kKeys = 40000;
    std::atomic<bool> producerDone{false};
    std::thread producer([&] {
        for (int i = 0; i < kKeys; ++i) {
            if ((i % 40) == 3) {
                prog.recordKeystroke(true);
            } else {
                prog.recordKeystroke(false, 1, (i % 10) == 0 ? 1u : 0u);
            }
            prog.recordActiveTimeMs(50);
        }
        producerDone.store(true, std::memory_order_release);
    });
    std::thread consumer([&] {
        int iterations = 0;
        while ((!producerDone.load(std::memory_order_acquire) || iterations < 25) &&
               iterations < 100000) {
            prog.flushStats();
            (void)prog.getStats();
            (void)prog.serialize();
            ++iterations;
        }
    });
    producer.join();
    consumer.join();
    prog.flushStats();

    const auto stats = prog.getStats();
    assert(stats.totalKeystrokes == kKeys);                 // not one keystroke lost
    assert(stats.totalCharacters == kKeys - (kKeys / 40));  // every 40th is a backspace
    assert(stats.typingTimeSeconds == static_cast<std::uint64_t>(kKeys) * 50 / 1000);
    assert(stats.totalXp > 0);

    // The persistence path must stay coherent under the same interleaving.
    const std::string payload = prog.serialize();
    assert(prog.deserialize(payload));
    assert(prog.getStats().totalKeystrokes == kKeys);

    prog.reset();
    std::cout << "  [PASS] Concurrent flush loses nothing\n";
}

int main() {
    std::cout << "=== Running Progression & Level Suite ===\n";
    testLevelFormulas();
    testHotPathAccumulation();
    testTypingAccumulationAndAchievements();
    testLevelUpEvents();
    testDailyStreaks();
    testMinigameRecords();
    testPersistenceAndCorruption();
    testResilienceAndCorruption();
    testConcurrentFlushLosesNothing();
    std::cout << "=== ALL PROGRESSION TESTS PASSED ===\n";
    return 0;
}
