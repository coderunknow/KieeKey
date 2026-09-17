//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/test_arcade.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "Arcade.hpp"

#include <cassert>
#include <iostream>
#include <vector>

using namespace ok::arcade;

void testSnake() {
    SnakeGame snake;
    snake.start();
    assert(!snake.isGameOver());
    assert(snake.getScore() == 0);

    // Initial direction is Right. Moving right should advance snake.
    snake.update(0.2); // One tick
    assert(!snake.isGameOver());

    // Stress test input queue
    std::vector<int> inputs = {0x26, 0x27, 0x28, 0x25}; // Up, Right, Down, Left
    snake.stressTestInputQueue(inputs);
    assert(!snake.renderText().empty());

    // Trigger wall collision to verify game over
    SnakeGame wallSnake;
    wallSnake.start();
    for (int i = 0; i < 30; ++i) {
        wallSnake.update(0.2);
    }
    assert(wallSnake.isGameOver());
    std::cout << "  [PASS] Snake tests\n";
}

void testTetris() {
    TetrisGame tetris;
    tetris.start();
    assert(!tetris.isGameOver());
    assert(tetris.getScore() == 0);
    assert(tetris.getLevel() == 1);

    // Move left and right
    tetris.handleKey(0, U'a', true);
    tetris.handleKey(0, U'd', true);
    // Rotate
    tetris.handleKey(0, U'w', true);
    // Soft drop
    tetris.handleKey(0, U's', true);
    assert(tetris.getScore() >= 1);

    // Hard drop
    tetris.handleKey(0x20, 0, true);
    assert(!tetris.renderText().empty());
    std::cout << "  [PASS] Tetris tests\n";
}

void testFishing() {
    FishingGame fish;
    fish.start();
    assert(!fish.isGameOver());

    // Upgrades
    assert(fish.getRodLevel() == 1);
    fish.upgradeRod();
    assert(fish.getRodLevel() == 2);

    fish.upgradeBait();
    assert(fish.getBaitLevel() == 2);

    fish.upgradeReel();
    assert(fish.getReelLevel() == 2);

    // Automation modes
    fish.setAutomationMode(AutomationMode::Assisted);
    assert(fish.getAutomationMode() == AutomationMode::Assisted);

    fish.setAutomationMode(AutomationMode::Automated);
    assert(fish.getAutomationMode() == AutomationMode::Automated);
    fish.update(0.3); // Automated typing ticks

    fish.setAutomationMode(AutomationMode::Manual);
    assert(!fish.renderText().empty());
    std::cout << "  [PASS] Fishing tests\n";
}

void testTypingRace() {
    TypingRaceGame race;
    race.start();
    assert(!race.isGameOver());
    assert(race.getProgressPercent() == 0.0);

    // Type first character
    race.handleKey(0, U'K', true);
    assert(race.getProgressPercent() > 0.0);
    race.update(1.0);
    assert(race.getLiveWpm() >= 0.0);
    assert(!race.renderText().empty());
    std::cout << "  [PASS] Typing Race tests\n";
}

void testWasdRace() {
    WasdRaceGame wasd;
    wasd.start();
    assert(!wasd.isGameOver());

    // Lane navigation: steer left and right
    wasd.handleKey(0, U'a', true);
    wasd.handleKey(0, U'd', true);
    wasd.handleKey(0, U'w', true); // accelerate
    wasd.handleKey(0, U's', true); // brake

    // Update with delta time
    wasd.update(1.0);
    assert(!wasd.renderText().empty());
    std::cout << "  [PASS] WASD Race tests\n";
}

void testRhythm() {
    RhythmTypingGame rhythm;
    rhythm.start();
    assert(!rhythm.isGameOver());
    assert(rhythm.getCombo() == 0);

    // Advance time close to first note (at 1.0s)
    rhythm.update(1.0);
    // Hit key 'd'
    rhythm.handleKey(0, U'd', true);
    assert(rhythm.getCombo() >= 1);
    assert(rhythm.getScore() > 0);
    assert(!rhythm.renderText().empty());
    std::cout << "  [PASS] Rhythm Typing tests\n";
}

void testNoMistake() {
    NoMistakeGame noMis;
    noMis.start();
    assert(!noMis.isGameOver());

    // Target starts with 'h', 'o', 'c'
    noMis.handleKey(0, U'h', true);
    assert(noMis.getCombo() == 1);
    noMis.handleKey(0, U'o', true);
    assert(noMis.getCombo() == 2);

    // Mistake!
    noMis.handleKey(0, U'z', true); // wrong key
    // Score/combo should drop
    assert(noMis.getCombo() == 0);
    assert(!noMis.renderText().empty());
    std::cout << "  [PASS] No-Mistake tests\n";
}

void testFlexing() {
    FlexingGame flex;
    flex.setPreloadedText(U"Hello World");
    flex.setGranularity(FlexGranularity::OneCharPerKey);
    flex.start();
    assert(!flex.isGameOver());

    // Press any key
    flex.handleKey(0x41, U'x', true);
    auto emitted = flex.popEmittedOutput();
    assert(emitted == U"H");
    assert(flex.getActualKeypresses() == 1);
    assert(flex.getGeneratedChars() == 1);

    // Switch to OneWordPerKey
    flex.setGranularity(FlexGranularity::OneWordPerKey);
    flex.handleKey(0x42, U'y', true);
    emitted = flex.popEmittedOutput();
    assert(emitted == U"ello ");

    assert(!flex.renderText().empty());
    std::cout << "  [PASS] Flexing tests\n";
}

void testArcadeManager() {
    auto& mgr = ArcadeManager::instance();
    assert(!mgr.isConsumingKeyboard());
    assert(mgr.getCurrentGameType() == GameType::None);

    mgr.launchGame(GameType::Snake);
    assert(mgr.isConsumingKeyboard());
    assert(mgr.getCurrentGameType() == GameType::Snake);

    mgr.update(0.1);
    mgr.handleKey(0x27, 0, true);

    mgr.launchGame(GameType::Tetris);
    assert(mgr.getCurrentGameType() == GameType::Tetris);

    mgr.stopGame();
    assert(!mgr.isConsumingKeyboard());
    assert(mgr.getCurrentGameType() == GameType::None);
    std::cout << "  [PASS] ArcadeManager tests\n";
}

int main() {
    std::cout << "=== Running Arcade Minigame Suite ===\n";
    testSnake();
    testTetris();
    testFishing();
    testTypingRace();
    testWasdRace();
    testRhythm();
    testNoMistake();
    testFlexing();
    testArcadeManager();
    std::cout << "=== ALL ARCADE TESTS PASSED ===\n";
    return 0;
}
