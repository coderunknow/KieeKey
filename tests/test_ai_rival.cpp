//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/test_ai_rival.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "AiRival.hpp"

#include <cassert>
#include <iostream>

using namespace ok::ai;

void testOptInAndPrivacy() {
    auto& ai = AiRivalEngine::instance();
    ai.resetProfile();
    ai.setOptIn(false);
    assert(!ai.isOptIn());

    // Observing when not opt-in must be a no-op
    ai.observeKeystroke(U'a', 1000000, false, false);
    ai.observeKeystroke(U's', 1100000, false, true);
    ai.trainBatch();
    assert(ai.getProfile().sampleCount == 0);

    // Opt-in enabled
    ai.setOptIn(true);
    assert(ai.isOptIn());
    for (int i = 0; i < 20; ++i) {
        ai.observeKeystroke(U'a' + (i % 26), 2000000 + i * 120000, false, (i % 5 == 0));
    }
    ai.trainBatch();
    assert(ai.getProfile().sampleCount > 0);

    // Opt-out immediately wipes profile
    ai.setOptIn(false);
    assert(ai.getProfile().sampleCount == 0);
    std::cout << "  [PASS] AI Opt-in & Privacy tests\n";
}

void testSimulationAndGhost() {
    auto& ai = AiRivalEngine::instance();
    ai.setOptIn(true);

    // Train with some simulated user keystrokes
    for (int i = 0; i < 30; ++i) {
        ai.observeKeystroke(U'x', 1000000 + i * 100000, (i == 10), (i == 15));
    }
    ai.trainBatch();

    // Generate simulated typing run
    std::u32string target = U"Tieng Viet giau dep";
    auto sim = ai.simulateTypingRun(target, 42);
    assert(!sim.empty());

    // Record ghost run
    std::vector<uint32_t> offsets = {0, 120, 240, 360, 500};
    ai.recordGhostRun(offsets);
    auto yesterday = ai.getYesterdayGhost();
    assert(yesterday.size() == offsets.size());
    assert(yesterday[2] == 240);

    // Serialization round trip
    std::string serialized = ai.serializeProfile();
    assert(!serialized.empty());

    AiRivalEngine otherEngine;
    otherEngine.setOptIn(true);
    assert(otherEngine.deserializeProfile(serialized));
    assert(otherEngine.getProfile().sampleCount == ai.getProfile().sampleCount);

    ai.setOptIn(false);
    std::cout << "  [PASS] AI Simulation & Ghost tests\n";
}

int main() {
    std::cout << "=== Running AI Personal Rival Suite ===\n";
    testOptInAndPrivacy();
    testSimulationAndGhost();
    std::cout << "=== ALL AI RIVAL TESTS PASSED ===\n";
    return 0;
}
