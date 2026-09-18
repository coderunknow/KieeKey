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
#include "Progression.hpp"

#include <cassert>
#include <iostream>

using namespace ok::progression;

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

    std::cout << "  [PASS] Level & XP deterministic formulas\n";
}

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

    // Achievements should have fired: FirstKey, FirstWord, 50Wpm, 80Wpm, 100Wpm
    assert(prog.isAchievementUnlocked(AchievementId::FirstKey));
    assert(prog.isAchievementUnlocked(AchievementId::FirstWord));
    assert(prog.isAchievementUnlocked(AchievementId::Century100Wpm));

    // Minigame score recording
    prog.recordSnakeScore(2500);
    assert(prog.isAchievementUnlocked(AchievementId::SnakeMaster));

    prog.recordFishCaught(true); // Legendary fish
    assert(prog.isAchievementUnlocked(AchievementId::LegendaryAngler));

    std::cout << "  [PASS] Typing accumulation & achievements\n";
}

void testResilienceAndCorruption() {
    auto& prog = ProgressionEngine::instance();
    std::string valid = prog.serialize();
    assert(!valid.empty());

    // Corrupted data (tampered payload or bad checksum)
    std::string corrupted = valid;
    corrupted[corrupted.size() - 2] = (corrupted[corrupted.size() - 2] == '0' ? '1' : '0');
    assert(!prog.deserialize(corrupted));

    // Garbage data
    assert(!prog.deserialize("NOT_KIEEKEY_DATA"));
    assert(!prog.deserialize(""));

    // Valid reload
    assert(prog.deserialize(valid));

    std::cout << "  [PASS] Progression corruption resilience\n";
}

int main() {
    std::cout << "=== Running Progression & Level Suite ===\n";
    testLevelFormulas();
    testTypingAccumulationAndAchievements();
    testResilienceAndCorruption();
    std::cout << "=== ALL PROGRESSION TESTS PASSED ===\n";
    return 0;
}
