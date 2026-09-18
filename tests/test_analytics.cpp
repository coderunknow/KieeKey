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
// Typing analytics & coach suite (v1.3.0).
//
//  1. Observation & snapshot — counting, WPM, accuracy, percentiles
//  2. Rhythm metrics         — bursts, pauses, tone delay, correction latency
//  3. Ring rollover          — bounded memory, oldest samples dropped cleanly
//  4. Session window         — beginSession() isolates "this session" from history
//  5. Coach recommendations  — threshold guard, fact + advice for every item
//  6. Concurrency            — hook thread vs UI thread: torn samples impossible
//============================================================================
#include "TypingAnalytics.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <thread>

using namespace ok::analytics;

namespace {

constexpr std::uint64_t kUs = 1000;                 // µs per ms
constexpr std::uint64_t kBase = 100'000'000ull;     // arbitrary epoch (µs)

void feed(std::uint64_t baseUs, std::uint64_t stepMs, int count, bool withBackspaces,
          bool withTones) {
    auto& engine = TypingAnalyticsEngine::instance();
    for (int i = 0; i < count; ++i) {
        const bool isBs = withBackspaces && ((i % 20) == 10);
        const bool isTone = withTones && ((i % 7) == 3);
        engine.observeKey(isBs ? U'\b' : static_cast<char32_t>(U'a' + (i % 26)),
                          baseUs + static_cast<std::uint64_t>(i) * stepMs * kUs, isBs, false,
                          isTone);
    }
}

} // namespace

//---------------------------------------------------------------------------
void testObservationAndSnapshot() {
    auto& engine = TypingAnalyticsEngine::instance();
    engine.reset();

    auto emptySnap = engine.computeSnapshot();
    assert(emptySnap.totalKeystrokes == 0);
    assert(emptySnap.accuracyPercent == 100.0);   // nothing typed is not "0 % accurate"

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
    assert(snap.accuracyPercent <= 100.0);
    assert(snap.meanIkiMs > 80.0 && snap.meanIkiMs < 120.0);
    assert(snap.p50IkiMs > 0.0);
    assert(snap.p50IkiMs <= snap.p90IkiMs);
    assert(snap.p90IkiMs <= snap.p99IkiMs);
    assert(snap.stddevIkiMs >= 0.0);
    assert(snap.sessionDurationMs > 5000 && snap.sessionDurationMs < 7000);
    assert(snap.rawWpm > snap.netWpm);   // backspaces drag the net rate down

    // Observation count is a plain counter; it keeps growing past the ring size.
    assert(engine.totalObserved() == 60);

    engine.reset();
    assert(engine.computeSnapshot().totalKeystrokes == 0);
    assert(engine.totalObserved() == 0);
    std::cout << "  [PASS] Analytics observation & snapshot\n";
}

//---------------------------------------------------------------------------
void testRhythmMetrics() {
    auto& engine = TypingAnalyticsEngine::instance();
    engine.reset();

    // 30 keys at 100 ms, a 2 s thinking pause, then 30 more keys at 100 ms.
    feed(kBase, 100, 30, false, false);
    feed(kBase + 29 * 100 * kUs + 2000 * kUs, 100, 30, false, false);

    const auto snap = engine.computeSnapshot();
    assert(snap.totalKeystrokes == 60);
    assert(snap.totalBackspaces == 0);
    assert(snap.pauseCount == 1);
    assert(snap.avgPauseMs > 1900.0 && snap.avgPauseMs < 2100.0);
    assert(snap.burstCount == 2);
    assert(snap.avgBurstLength > 25.0 && snap.avgBurstLength < 31.0);

    // Tone keys: the interval that precedes them is recorded as the tone delay.
    engine.reset();
    feed(kBase, 250, 40, /*withBackspaces=*/false, /*withTones=*/true);
    const auto toneSnap = engine.computeSnapshot();
    assert(toneSnap.toneKeyCount > 0);
    assert(toneSnap.avgToneDelayMs > 200.0 && toneSnap.avgToneDelayMs < 300.0);

    // Backspace latency: the interval that ends with the correction key.
    engine.reset();
    feed(kBase, 400, 40, /*withBackspaces=*/true, /*withTones=*/false);
    const auto bsSnap = engine.computeSnapshot();
    assert(bsSnap.totalBackspaces == 2);
    assert(bsSnap.avgRecoveryLatencyMs > 300.0 && bsSnap.avgRecoveryLatencyMs < 500.0);

    // A slowing session must produce a negative fatigue slope, a steady one ~0.
    engine.reset();
    for (int i = 0; i < 80; ++i) {
        const std::uint64_t step = (i < 40) ? 80u : 200u;
        engine.observeKey(U'a', kBase + static_cast<std::uint64_t>(i) * step * kUs, false);
    }
    const auto fatigued = engine.computeSnapshot();
    assert(fatigued.fatigueSlope < -15.0);

    engine.reset();
    for (int i = 0; i < 80; ++i) {
        engine.observeKey(U'a', kBase + static_cast<std::uint64_t>(i) * 100 * kUs, false);
    }
    const auto steady = engine.computeSnapshot();
    assert(std::fabs(steady.fatigueSlope) < 1.0);

    // Out-of-order timestamps (clock adjustment) must not corrupt the metrics.
    engine.reset();
    engine.observeKey(U'a', kBase + 1000 * kUs, false);
    engine.observeKey(U'b', kBase, false);              // goes backwards
    engine.observeKey(U'c', kBase + 1200 * kUs, false);
    const auto ooo = engine.computeSnapshot();
    assert(ooo.totalKeystrokes == 3);
    assert(ooo.meanIkiMs >= 0.0 && std::isfinite(ooo.meanIkiMs));

    engine.reset();
    std::cout << "  [PASS] Rhythm metric tests\n";
}

//---------------------------------------------------------------------------
void testRingRollover() {
    auto& engine = TypingAnalyticsEngine::instance();
    engine.reset();

    constexpr int kPushes = (int)TypingAnalyticsEngine::kRingCapacity * 3;
    for (int i = 0; i < kPushes; ++i) {
        engine.observeKey(U'a' + static_cast<char32_t>(i % 26),
                          kBase + static_cast<std::uint64_t>(i) * 100 * kUs, (i % 50) == 7);
    }
    const auto snap = engine.computeSnapshot();
    // The window is bounded by the ring; nothing is out of range or NaN.
    assert(snap.totalKeystrokes == TypingAnalyticsEngine::kRingCapacity);
    assert(snap.totalBackspaces + snap.totalChars == snap.totalKeystrokes);
    assert(snap.accuracyPercent >= 0.0 && snap.accuracyPercent <= 100.0);
    assert(snap.meanIkiMs > 90.0 && snap.meanIkiMs < 110.0);   // only recent samples
    assert(engine.totalObserved() == static_cast<std::uint64_t>(kPushes));

    // The newest sample is the one that survives: a jump in rhythm right at the
    // end must dominate the window.
    engine.reset();
    for (int i = 0; i < (int)TypingAnalyticsEngine::kRingCapacity; ++i) {
        engine.observeKey(U'a', kBase + static_cast<std::uint64_t>(i) * 50 * kUs, false);
    }
    const auto fast = engine.computeSnapshot();
    assert(fast.meanIkiMs > 45.0 && fast.meanIkiMs < 55.0);

    engine.reset();
    std::cout << "  [PASS] Ring rollover tests\n";
}

//---------------------------------------------------------------------------
void testSessionWindow() {
    auto& engine = TypingAnalyticsEngine::instance();
    engine.reset();

    feed(kBase, 100, 50, false, false);           // history
    engine.beginSession();
    feed(kBase + 50 * 100 * kUs, 100, 20, false, false);   // current session

    const auto whole = engine.computeSnapshot();
    const auto session = engine.computeSessionSnapshot();
    assert(whole.totalKeystrokes == 70);
    assert(session.totalKeystrokes == 20);
    assert(session.totalKeystrokes < whole.totalKeystrokes);
    assert(session.meanIkiMs > 80.0 && session.meanIkiMs < 120.0);

    // A session that starts before the ring window is clamped to the window.
    engine.reset();
    engine.beginSession();
    for (int i = 0; i < (int)TypingAnalyticsEngine::kRingCapacity * 2; ++i) {
        engine.observeKey(U'a', kBase + static_cast<std::uint64_t>(i) * 100 * kUs, false);
    }
    const auto clamped = engine.computeSessionSnapshot();
    assert(clamped.totalKeystrokes == TypingAnalyticsEngine::kRingCapacity);

    engine.reset();
    std::cout << "  [PASS] Session window tests\n";
}

//---------------------------------------------------------------------------
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

    // Every advice item must be backed by a measured fact: a fast-but-sloppy
    // typist gets the accuracy advice with their own numbers in it.
    engine.reset();
    for (int i = 0; i < 120; ++i) {
        const bool bs = (i % 8) == 5;                 // 12.5 % error rate
        engine.observeKey(U'a', 1000000 + static_cast<std::uint64_t>(i) * 60 * kUs, bs);
    }
    const auto advice = engine.generateCoachingAdvice();
    assert(!advice.empty());
    bool hasPacingOrAccuracy = false;
    for (const auto& r : advice) {
        if (r.category == CoachItemCategory::Accuracy || r.category == CoachItemCategory::Burst ||
            r.category == CoachItemCategory::Pacing) {
            hasPacingOrAccuracy = true;
        }
        assert(r.measuredFact.find("WPM") != std::string::npos ||
               r.measuredFact.find("%") != std::string::npos ||
               r.measuredFact.find("ms") != std::string::npos);
    }
    assert(hasPacingOrAccuracy);

    // A clean, steady typist still gets exactly one (positive) suggestion.
    engine.reset();
    for (int i = 0; i < 120; ++i) {
        engine.observeKey(U'a', 1000000 + static_cast<std::uint64_t>(i) * 100 * kUs, false);
    }
    const auto clean = engine.generateCoachingAdvice();
    assert(clean.size() == 1);
    assert(clean[0].category == CoachItemCategory::Pacing);

    engine.reset();
    std::cout << "  [PASS] Coach recommendations & threshold guard\n";
}

//---------------------------------------------------------------------------
void testConcurrentObservationIsRaceFree() {
    auto& engine = TypingAnalyticsEngine::instance();
    engine.reset();

    std::atomic<std::uint64_t> produced{0};
    std::atomic<bool> producerDone{false};
    std::thread producer([&] {
        std::uint64_t t = kBase;
        for (int i = 0; i < 120000; ++i) {
            t += 60 + static_cast<std::uint64_t>(i % 31) * 7;   // µs
            engine.observeKey(U'a' + static_cast<char32_t>(i % 26), t, (i % 23) == 0, false,
                              (i % 11) == 0);
            produced.fetch_add(1, std::memory_order_relaxed);
        }
        producerDone.store(true, std::memory_order_release);
    });
    std::thread consumer([&] {
        int iterations = 0;
        while ((!producerDone.load(std::memory_order_acquire) || iterations < 40) &&
               iterations < 20000) {
            const auto snap = engine.computeSnapshot();
            const auto session = engine.computeSessionSnapshot();
            // Invariants that a torn read would break immediately.
            assert(snap.totalBackspaces + snap.totalChars == snap.totalKeystrokes);
            assert(snap.accuracyPercent >= 0.0 && snap.accuracyPercent <= 100.0);
            assert(snap.meanIkiMs >= 0.0 && std::isfinite(snap.meanIkiMs));
            assert(snap.stddevIkiMs >= 0.0 && std::isfinite(snap.stddevIkiMs));
            assert(snap.p50IkiMs <= snap.p90IkiMs && snap.p90IkiMs <= snap.p99IkiMs);
            // (The session snapshot is taken *after* the whole-window one, so
            // it may legitimately contain more samples.)
            assert(session.totalBackspaces + session.totalChars == session.totalKeystrokes);
            assert(session.totalKeystrokes <= TypingAnalyticsEngine::kRingCapacity);
            assert(snap.totalKeystrokes <= TypingAnalyticsEngine::kRingCapacity);
            (void)engine.generateCoachingAdvice();
            if ((iterations % 25) == 0) { engine.beginSession(); }
            ++iterations;
        }
    });
    producer.join();
    consumer.join();

    assert(engine.totalObserved() == produced.load());
    const auto finalSnap = engine.computeSnapshot();
    assert(finalSnap.totalKeystrokes == TypingAnalyticsEngine::kRingCapacity);
    assert(finalSnap.accuracyPercent > 90.0);

    engine.reset();
    std::cout << "  [PASS] Concurrent observation is race-free\n";
}

int main() {
    std::cout << "=== Running Typing Analytics & Coach Suite ===\n";
    testObservationAndSnapshot();
    testRhythmMetrics();
    testRingRollover();
    testSessionWindow();
    testCoachRecommendations();
    testConcurrentObservationIsRaceFree();
    std::cout << "=== ALL ANALYTICS TESTS PASSED ===\n";
    return 0;
}
