//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/test_soak_arcade.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "Arcade.hpp"
#include "ChaosEngine.hpp"
#include "AiRival.hpp"
#include "Progression.hpp"
#include "TypingAnalytics.hpp"

#include <cassert>
#include <iostream>
#include <vector>

using namespace ok::arcade;
using namespace ok::chaos;
using namespace ok::ai;
using namespace ok::progression;
using namespace ok::analytics;

void testArcadeLifecycleSoak(int cycles) {
    auto& mgr = ArcadeManager::instance();
    GameType types[] = {
        GameType::Snake,
        GameType::Tetris,
        GameType::Fishing,
        GameType::TypingRace,
        GameType::WasdRace,
        GameType::Rhythm,
        GameType::NoMistake,
        GameType::Flexing,
    };

    for (int i = 0; i < cycles; ++i) {
        GameType t = types[i % 8];
        mgr.launchGame(t);
        assert(mgr.isConsumingKeyboard());
        mgr.update(0.016); // 1 frame
        mgr.handleKey(0x41, U'a', true);
        mgr.stopGame();
        assert(!mgr.isConsumingKeyboard());
    }
    std::cout << "  [PASS] Arcade lifecycle soak (" << cycles << " cycles)\n";
}

void testHighThroughputKeyStress(int keys) {
    auto& mgr = ArcadeManager::instance();
    mgr.launchGame(GameType::Snake);

    int vkPool[] = {0x25, 0x26, 0x27, 0x28, 0x57, 0x41, 0x53, 0x44};
    for (int i = 0; i < keys; ++i) {
        int vk = vkPool[i % 8];
        mgr.handleKey(vk, 0, true);
        if (i % 100 == 0) {
            mgr.update(0.01);
        }
    }
    mgr.stopGame();
    std::cout << "  [PASS] High-throughput key stress (" << keys << " keys)\n";
}

void testChaosAndAiSoak(int cycles) {
    auto& chaos = ChaosEngine::instance();
    auto& ai = AiRivalEngine::instance();

    ChaosConfig cfg;
    cfg.reset();
    ai.setOptIn(true);

    std::u32string testText = U"KieeKey Arcade and AI soak testing string";

    for (int i = 0; i < cycles; ++i) {
        // Toggle chaos
        cfg.masterEnabled = (i % 2 == 0);
        cfg.randomCaseEnabled = true;
        cfg.glyphTransformEnabled = true;
        cfg.glyphMode = static_cast<GlyphTransformMode>((i % 6) + 1);
        chaos.setConfig(cfg);

        auto cRes = chaos.processCase(testText, i + 1);
        auto gRes = chaos.getVisualDisplayString(testText, i + 1);
        (void)cRes; (void)gRes;

        // Feed AI observations
        ai.observeKeystroke(U'a' + (i % 26), 1000000 + i * 50000, false, (i % 4 == 0));
        if (i % 50 == 0) {
            ai.trainBatch();
            auto sim = ai.simulateTypingRun(testText, i);
            (void)sim;
        }
    }

    ai.resetProfile();
    ai.setOptIn(false);
    cfg.reset();
    chaos.setConfig(cfg);
    std::cout << "  [PASS] Chaos and AI soak (" << cycles << " cycles)\n";
}

int main(int argc, char** argv) {
    int cycles = 500;
    int stressKeys = 100000;
    if (argc > 1) {
        cycles = std::atoi(argv[1]);
        if (cycles < 10) cycles = 10;
    }
    std::cout << "=== Running Arcade & Subsystem Soak Suite ===\n";
    testArcadeLifecycleSoak(cycles);
    testHighThroughputKeyStress(stressKeys);
    testChaosAndAiSoak(cycles);
    std::cout << "=== ALL SOAK TESTS PASSED ===\n";
    return 0;
}
