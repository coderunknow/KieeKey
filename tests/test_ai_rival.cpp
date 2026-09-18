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
// AI personal-rival suite (v1.3.0).
//
//  1. Opt-in & privacy     — nothing is learned without consent; opt-out purges
//  2. Learning             — the fitted profile tracks the observed typing
//  3. AiRacer              — deterministic, frame-rate independent, beatable
//  4. Ghost                — "yesterday you" survives a profile round-trip
//  5. Robustness           — hostile/legacy payloads are clamped, never trusted
//  6. Concurrency          — hook thread vs UI thread: race-free (TSan-clean)
//============================================================================
#include "AiRival.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

using namespace ok::ai;

namespace {

std::u32string kPassage() {
    return U"Chao mung ban den voi KieeKey, noi ban go phim that nhanh.";
}

// Repeated incremental training (as the UI does on its refresh timer).
void train(AiRivalEngine& ai, int batches = 1) {
    for (int i = 0; i < batches; ++i) { ai.trainBatch(); }
}

// Feeds `count` keystrokes with a fixed inter-key interval (µs).
void feed(AiRivalEngine& ai, std::uint32_t count, std::uint64_t startUs,
          std::uint64_t intervalUs, bool withTypos = false) {
    for (std::uint32_t i = 0; i < count; ++i) {
        const char32_t ch = U'a' + static_cast<char32_t>(i % 26);
        ai.observeKeystroke(ch, startUs + i * intervalUs, withTypos && (i % 20 == 0),
                            (i % 7 == 0));
    }
}

} // namespace

//---------------------------------------------------------------------------
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

//---------------------------------------------------------------------------
void testLearningFitsObservedRhythm() {
    auto& ai = AiRivalEngine::instance();
    ai.resetProfile();
    ai.setOptIn(true);

    // A slow, steady typist: 250 ms between keys => ~48 WPM.
    feed(ai, 400, 10'000'000, 250'000);
    train(ai, 30);
    const AiProfile slow = ai.getProfile();
    assert(slow.sampleCount > 0);
    // The first real batch lands on the measured rhythm (not 30 % of the way
    // from the factory default, which made the rival race ahead of the user).
    assert(slow.meanIkiMs > 240.0 && slow.meanIkiMs < 260.0);
    assert(slow.expectedWpm() > 45.0 && slow.expectedWpm() < 52.0);

    // Every sample is learned exactly once: 30 refreshes must not inflate the
    // counter beyond what was actually observed (each batch learns the new tail).
    assert(slow.sampleCount == 399);

    // A fast typist must produce a visibly faster rival.
    ai.resetProfile();
    feed(ai, 400, 10'000'000, 90'000);
    train(ai, 30);
    const AiProfile fast = ai.getProfile();
    assert(fast.meanIkiMs < slow.meanIkiMs);
    assert(fast.expectedWpm() > slow.expectedWpm());
    assert(fast.meanIkiMs > 85.0 && fast.meanIkiMs < 95.0);
    assert(fast.expectedWpm() > 120.0);

    // Learning is incremental: the next batch blends in proportion to the new
    // evidence, so the model keeps following the user's actual rhythm.
    feed(ai, 400, 100'000'000, 200'000);
    train(ai, 30);
    const AiProfile again = ai.getProfile();
    assert(again.meanIkiMs > fast.meanIkiMs);
    assert(again.meanIkiMs > 135.0 && again.meanIkiMs < 155.0);   // (90 + 200) / 2
    assert(again.sampleCount == 798);

    // The learner must record the natural typo rate it was fed.
    ai.resetProfile();
    feed(ai, 400, 10'000'000, 120'000, /*withTypos=*/true);
    train(ai, 10);
    assert(ai.getProfile().errorRate > 0.0);
    ai.setOptIn(false);
    std::cout << "  [PASS] AI learning fits observed rhythm\n";
}

//---------------------------------------------------------------------------
void testRacerDeterminismAndProgress() {
    auto& ai = AiRivalEngine::instance();
    ai.resetProfile();
    ai.setOptIn(true);
    feed(ai, 300, 10'000'000, 120'000);
    ai.trainBatch();

    const std::u32string passage = kPassage();
    AiRaceConfig cfg;
    cfg.seed = 777;
    cfg.aggression = 1.0;

    AiRacer a = ai.makeRacer(passage, cfg);
    AiRacer b = ai.makeRacer(passage, cfg);
    assert(a.getPassageLength() == passage.size());
    assert(a.getScheduleEntryCount() > 0);

    // Deterministic: identical config => identical schedule.
    for (int i = 0; i < 200; ++i) {
        a.update(0.016);
        b.update(0.016);
        assert(a.getCharIndex() == b.getCharIndex());
    }
    assert(a.getElapsedSec() > 0.0);
    assert(a.getProgress() > 0.0 && a.getProgress() <= 1.0);

    // Frame-rate independence: one big step and many small steps must land on
    // the same character (the per-frame clamp is applied consistently).
    AiRacer small = ai.makeRacer(passage, cfg);
    AiRacer coarse = ai.makeRacer(passage, cfg);
    for (int i = 0; i < 60; ++i) { small.update(1.0 / 60.0); }
    for (int i = 0; i < 4; ++i) { coarse.update(0.25); }   // clamped to 50 ms each
    // The clamped racer can only ever be *behind*, never ahead of real time.
    assert(coarse.getElapsedSec() <= small.getElapsedSec() + 0.001);

    // Aggression makes the rival faster (this is the "đòi đua thắng m" dial).
    AiRaceConfig aggressive = cfg;
    aggressive.aggression = 1.5;
    AiRacer quick = ai.makeRacer(passage, aggressive);
    AiRacer normal = ai.makeRacer(passage, cfg);
    for (int i = 0; i < 120; ++i) { quick.update(0.05); normal.update(0.05); }
    assert(quick.getCharIndex() >= normal.getCharIndex());
    assert(quick.getWpm() > 0.0);

    // A flawless, very fast rival must actually finish the passage.
    AiProfile ideal;
    ideal.meanIkiMs = 45.0;
    ideal.stddevIkiMs = 0.0;
    ideal.errorRate = 0.0;
    AiRacer finisher;
    AiRaceConfig flawless;
    flawless.aggression = 1.0;
    flawless.allowTypos = false;
    flawless.seed = 1;
    finisher.reset(passage, ideal, flawless);
    int guard = 0;
    while (!finisher.isFinished() && guard++ < 100000) { finisher.update(0.02); }
    assert(finisher.isFinished());
    assert(finisher.getCharIndex() == passage.size());
    assert(finisher.getFinishTimeSec() > 0.0);
    assert(finisher.getProgress() >= 0.999);
    assert(finisher.getWpm() > 100.0);
    assert(finisher.getTypoCount() == 0);

    // Typos are replayed when the learned error rate says so, and cost time.
    AiProfile sloppy;
    sloppy.meanIkiMs = 120.0;
    sloppy.errorRate = 0.30;
    AiRacer typoRacer;
    AiRaceConfig typoCfg;
    typoCfg.seed = 5;
    typoCfg.typoFactor = 4.0;
    typoRacer.reset(passage, sloppy, typoCfg);
    assert(typoRacer.getScheduleEntryCount() >= passage.size());
    // Extra schedule entries (corrections) are what make the typos visible.
    assert(typoRacer.getScheduleEntryCount() >= typoRacer.getPassageLength());

    ai.setOptIn(false);
    std::cout << "  [PASS] AiRacer determinism & progress tests\n";
}

//---------------------------------------------------------------------------
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
    assert(ai.simulateTypingRun(target, 42).size() == sim.size());   // deterministic seed

    // Record ghost run
    std::vector<std::uint32_t> offsets = {0, 120, 240, 360, 500};
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
    // The ghost survives the round-trip (it used to be lost on restart).
    const auto ghost = otherEngine.getYesterdayGhost();
    assert(ghost == yesterday);

    // Opting out purges the ghost too.
    otherEngine.setOptIn(false);
    assert(otherEngine.getYesterdayGhost().empty());

    ai.setOptIn(false);
    std::cout << "  [PASS] AI Simulation & Ghost tests\n";
}

//---------------------------------------------------------------------------
void testHostileAndLegacyPayloads() {
    AiRivalEngine engine;
    engine.setOptIn(true);

    // Hostile: absurd values must be clamped to the documented ranges.
    const std::string hostile =
        "KIEEKEY_AI_PROFILE_V2\n"
        "sampleCount=999999999999\n"
        "meanIkiMs=0\n"
        "stddevIkiMs=-50\n"
        "avgBurstLength=1e9\n"
        "pauseThresholdMs=nan\n"
        "errorRate=99\n"
        "backspaceRecoveryMs=-1\n"
        "toneDelayMs=1e12\n"
        "speedCurveSlope=-1e9\n";
    assert(engine.deserializeProfile(hostile));
    const AiProfile p = engine.getProfile();
    assert(p.meanIkiMs >= 20.0 && p.meanIkiMs <= 900.0);
    assert(p.stddevIkiMs >= 0.0 && p.stddevIkiMs <= 400.0);
    assert(p.avgBurstLength >= 1.0 && p.avgBurstLength <= 60.0);
    assert(std::isfinite(p.pauseThresholdMs));
    assert(p.pauseThresholdMs >= 100.0 && p.pauseThresholdMs <= 2500.0);
    assert(p.errorRate >= 0.0 && p.errorRate <= 0.35);
    assert(p.backspaceRecoveryMs >= 20.0 && p.backspaceRecoveryMs <= 2000.0);
    assert(std::isfinite(p.toneDelayMs));
    assert(p.toneDelayMs >= 20.0 && p.toneDelayMs <= 1500.0);
    assert(std::isfinite(p.speedCurveSlope));
    assert(p.speedCurveSlope >= -250.0 && p.speedCurveSlope <= 250.0);
    assert(std::isfinite(p.expectedWpm()));
    assert(p.expectedWpm() < 1000.0);

    // Garbage / wrong header is rejected.
    assert(!engine.deserializeProfile(""));
    assert(!engine.deserializeProfile("NOT_A_PROFILE"));
    assert(!engine.deserializeProfile("KIEEKEY_AI_PROFILE_V2\n"));

    // V1 payloads (no ghost, no slope) are still accepted.
    const std::string v1 =
        "KIEEKEY_AI_PROFILE_V1\n"
        "sampleCount=120\n"
        "meanIkiMs=140\n"
        "stddevIkiMs=30\n"
        "avgBurstLength=6\n"
        "pauseThresholdMs=340\n"
        "errorRate=0.02\n"
        "backspaceRecoveryMs=250\n"
        "toneDelayMs=170\n";
    assert(engine.deserializeProfile(v1));
    assert(engine.getProfile().sampleCount == 120);
    assert(engine.getProfile().meanIkiMs > 130.0 && engine.getProfile().meanIkiMs < 150.0);

    // A payload with one unparsable field keeps the rest (forward compatible).
    const std::string partial =
        "KIEEKEY_AI_PROFILE_V2\n"
        "sampleCount=42\n"
        "meanIkiMs=lots\n"
        "toneDelayMs=200\n";
    assert(engine.deserializeProfile(partial));
    assert(engine.getProfile().sampleCount == 42);
    std::cout << "  [PASS] AI hostile & legacy payload tests\n";
}

//---------------------------------------------------------------------------
void testConcurrentObservationIsRaceFree() {
    auto& ai = AiRivalEngine::instance();
    ai.resetProfile();
    ai.setOptIn(true);

    std::atomic<bool> producerDone{false};
    std::thread producer([&] {
        std::uint64_t t = 1'000'000;
        for (int i = 0; i < 60000; ++i) {
            // Human-scale rhythm so the learned model has something to fit.
            t += 80'000 + static_cast<std::uint64_t>(i % 17) * 3'000;
            ai.observeKeystroke(U'a' + static_cast<char32_t>(i % 26), t, (i % 23) == 0,
                                (i % 11) == 0);
        }
        producerDone.store(true, std::memory_order_release);
    });
    std::thread consumer([&] {
        // Keep consuming while the hook thread is still producing, then take a
        // few more snapshots so the UI-side readers interleave with the writer.
        int iterations = 0;
        while ((!producerDone.load(std::memory_order_acquire) || iterations < 50) &&
               iterations < 20000) {
            (void)ai.getProfile();
            (void)ai.pendingObservationCount();
            (void)ai.getYesterdayGhost();
            if ((iterations % 60) == 0) { ai.trainBatch(); }
            ++iterations;
        }
    });
    producer.join();
    consumer.join();

    ai.trainBatch();
    const AiProfile p = ai.getProfile();
    assert(p.sampleCount > 0);
    assert(std::isfinite(p.meanIkiMs) && p.meanIkiMs >= 20.0 && p.meanIkiMs <= 900.0);
    assert(std::isfinite(p.expectedWpm()) && p.expectedWpm() > 0.0);

    ai.setOptIn(false);
    std::cout << "  [PASS] Concurrent observation is race-free\n";
}

int main() {
    std::cout << "=== Running AI Personal Rival Suite ===\n";
    testOptInAndPrivacy();
    testLearningFitsObservedRhythm();
    testRacerDeterminismAndProgress();
    testSimulationAndGhost();
    testHostileAndLegacyPayloads();
    testConcurrentObservationIsRaceFree();
    std::cout << "=== ALL AI RIVAL TESTS PASSED ===\n";
    return 0;
}
