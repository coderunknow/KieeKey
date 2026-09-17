//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/test_analytics.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "TypingAnalytics.hpp"

#include <cassert>
#include <iostream>

using namespace ok::analytics;

void testObservationAndSnapshot() {
    auto& engine = TypingAnalyticsEngine::instance();
    engine.reset();

    auto emptySnap = engine.computeSnapshot();
    assert(emptySnap.totalKeystrokes == 0);

    // Simulate typing 60 keystrokes with 100ms interval
    uint64_t baseTimeUs = 100000000;
    for (int i = 0; i < 60; ++i) {
        bool isBs = (i == 20 || i == 35);
        bool isTone = (i == 10 || i == 45);
        engine.observeKey(
            isBs ? U'\b' : (U'a' + (i % 26)),
            baseTimeUs + i * 100000, // 100ms
            isBs,
            false,
            isTone);
    }

    auto snap = engine.computeSnapshot();
    assert(snap.totalKeystrokes == 60);
    assert(snap.totalBackspaces == 2);
    assert(snap.totalChars == 58);
    assert(snap.accuracyPercent > 90.0);
    assert(snap.meanIkiMs > 80.0 && snap.meanIkiMs < 120.0);
    assert(snap.p50IkiMs > 0.0);

    std::cout << "  [PASS] Analytics observation & snapshot\n";
}

void testCoachRecommendations() {
    auto& engine = TypingAnalyticsEngine::instance();
    engine.reset();

    // Below minimum threshold: no coaching advice
    for (int i = 0; i < 15; ++i) {
        engine.observeKey(U'a', 1000000 + i * 100000, false);
    }
    assert(engine.generateCoachingAdvice().empty());

    // Provide enough samples (>= 50)
    for (int i = 15; i < 65; ++i) {
        engine.observeKey(U'a' + (i % 26), 1000000 + i * 100000, (i % 10 == 0));
    }
    auto recs = engine.generateCoachingAdvice();
    assert(!recs.empty());
    for (const auto& r : recs) {
        assert(!r.measuredFact.empty());
        assert(!r.heuristicAdvice.empty());
    }

    std::cout << "  [PASS] Coach recommendations & threshold guard\n";
}

int main() {
    std::cout << "=== Running Typing Analytics & Coach Suite ===\n";
    testObservationAndSnapshot();
    testCoachRecommendations();
    std::cout << "=== ALL ANALYTICS TESTS PASSED ===\n";
    return 0;
}
