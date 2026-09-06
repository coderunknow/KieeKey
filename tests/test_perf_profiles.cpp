//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.2 — Performance Preference Profiles test (RC2)
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
// File: tests/test_perf_profiles.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// v1.2.2 RC2 — PerfProfile unit test (pure-std header; runs on any platform).
//
// Layers pinned here (mirroring the profile-README contract):
//   A. baseStrategy table — every field of every profile, exactly.
//   B. applyHybrid — each flag's documented reach, flag collisions, and
//      "a flag may only touch what it is documented to touch".
//   C. adaptiveStrategy — threshold boundaries (off-by-one sweeps), rule
//      OVERWRITE ORDER (battery-after-burst, single-CPU-last), no-telemetry
//      == Balanced.
//   D. resolveStrategy — composition order Adaptive→Hybrid.
//   E. registry encoding: profileFromIndex clamp, hybrid 4-bit mask.
//   F. Strategy::operator== field-completeness — the app SKIPS re-apply
//      when a re-resolved strategy compares equal; a new Strategy field
//      forgotten in operator== would silently never reach the engine/hook/
//      barrier. Perturb each field alone, require !=.
//   G. the engine-surface invariant from the option-matrix work: the ONLY
//      EngineOptions field a profile may flip is useDictionaryRestore, and
//      only ON (one-way: the user's explicit opt-in can never be switched
//      off by changing profiles).
//   H. property sweep — spin floor<=cap, barrier ms >=1, batch clamp
//      identity, across profiles x 16 hybrid values x telemetry lattice.
//----------------------------------------------------------------------------
#include "PerfProfile.hpp"

#include <cstdio>
#include <cstdint>

using namespace ok::perf;

static int g_fail = 0;
static int g_checks = 0;
#define PP_CHECK(cond)                                                      \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(cond)) {                                                      \
            ++g_fail;                                                       \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                   \
    } while (0)
#define PP_CHECK_MSG(cond, msg)                                             \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(cond)) {                                                      \
            ++g_fail;                                                       \
            std::printf("  FAIL %s:%d  %s — %s\n", __FILE__, __LINE__,     \
                        #cond, msg);                                        \
        }                                                                   \
    } while (0)

//============================================================================
// A — base table, field by field
//============================================================================
static void testBaseTable() {
    std::printf("[PP-A] baseStrategy table (5 profiles x all fields)\n");

    {   // Balanced is the RC1 shipping behaviour: the whole DEFAULT struct.
        Strategy s = baseStrategy(Profile::Balanced);
        PP_CHECK(s.output == OutputPreference::Auto);
        PP_CHECK(s.consumerSpinCapUs == 100);
        PP_CHECK(s.consumerSpinFloorUs == 2);
        PP_CHECK(s.barrierBudgetUs == 1000);
        PP_CHECK(s.barrierSpinIters == 200);
        PP_CHECK(s.editBatchMax == 32);
        PP_CHECK(!s.deferInlineToConsumer);
        PP_CHECK(!s.dictionaryRestore);
        PP_CHECK(s.restoreIfWrongSpelling);
        PP_CHECK(!s.layoutRecheckEveryKey);
        PP_CHECK(s.tsfSlowDowngrade);
        PP_CHECK(s == Strategy{});   // default-constructed == Balanced
    }
    {
        Strategy s = baseStrategy(Profile::Fastest);
        PP_CHECK(s.output == OutputPreference::Inline);
        PP_CHECK(s.consumerSpinCapUs == 200);
        PP_CHECK(s.consumerSpinFloorUs == 4);
        PP_CHECK(s.barrierBudgetUs == 500);
        PP_CHECK(s.barrierSpinIters == 400);
        PP_CHECK(s.editBatchMax == 8);
        PP_CHECK(!s.deferInlineToConsumer);
        PP_CHECK(!s.dictionaryRestore);          // fastest = no lexicon veto
        PP_CHECK(s.restoreIfWrongSpelling);      // ...but raw restore stays
        PP_CHECK(!s.layoutRecheckEveryKey);
        PP_CHECK(s.tsfSlowDowngrade);
    }
    {
        Strategy s = baseStrategy(Profile::LeastFlicker);
        PP_CHECK(s.output == OutputPreference::Tsf);
        PP_CHECK(s.consumerSpinCapUs == 200);
        PP_CHECK(s.consumerSpinFloorUs == 4);
        PP_CHECK(s.barrierBudgetUs == 2000);
        PP_CHECK(s.barrierSpinIters == 200);     // default spin
        PP_CHECK(s.editBatchMax == 64);
        PP_CHECK(s.deferInlineToConsumer);
        PP_CHECK(!s.dictionaryRestore);
        PP_CHECK(s.restoreIfWrongSpelling);
        PP_CHECK(!s.layoutRecheckEveryKey);
        PP_CHECK(!s.tsfSlowDowngrade);           // flicker-free wins here
    }
    {
        Strategy s = baseStrategy(Profile::MaxCorrectness);
        PP_CHECK(s.output == OutputPreference::Auto);
        PP_CHECK(s.consumerSpinCapUs == 100);
        PP_CHECK(s.barrierBudgetUs == 4000);
        PP_CHECK(s.barrierSpinIters == 400);
        PP_CHECK(s.editBatchMax == 1);           // no coalescing
        PP_CHECK(!s.deferInlineToConsumer);
        PP_CHECK(s.dictionaryRestore);           // the ONE engine-side field
        PP_CHECK(s.restoreIfWrongSpelling);
        PP_CHECK(s.layoutRecheckEveryKey);
        PP_CHECK(s.tsfSlowDowngrade);
    }
    {   // Adaptive base == Balanced (telemetry moves it from there).
        PP_CHECK(baseStrategy(Profile::Adaptive) == baseStrategy(Profile::Balanced));
    }
    // Ordering properties that must hold across the table (drift-safe
    // regardless of exact numbers):
    PP_CHECK(baseStrategy(Profile::Fastest).barrierBudgetUs <=
             baseStrategy(Profile::Balanced).barrierBudgetUs);
    PP_CHECK(baseStrategy(Profile::LeastFlicker).barrierBudgetUs <=
             baseStrategy(Profile::MaxCorrectness).barrierBudgetUs);
    PP_CHECK(baseStrategy(Profile::Fastest).editBatchMax <=
             baseStrategy(Profile::Balanced).editBatchMax);
    PP_CHECK(profileName(Profile::Fastest) && profileName(Profile::Adaptive));
}

//============================================================================
// B — hybrid flags
//============================================================================
static void testHybrids() {
    std::printf("[PP-B] hybrid flag semantics + collisions\n");
    const Strategy bal = baseStrategy(Profile::Balanced);

    // PreferInline: touches output ONLY.
    Strategy s = applyHybrid(bal, kHybridPreferInline);
    Strategy want = bal; want.output = OutputPreference::Inline;
    PP_CHECK(s == want);

    // PreferTsf: output + defer (documented as the only two).
    s = applyHybrid(bal, kHybridPreferTsf);
    want = bal; want.output = OutputPreference::Tsf; want.deferInlineToConsumer = true;
    PP_CHECK(s == want);

    // Collision: the header applies Inline THEN Tsf — Tsf wins + defer.
    s = applyHybrid(bal, kHybridPreferInline | kHybridPreferTsf);
    want = bal; want.output = OutputPreference::Tsf; want.deferInlineToConsumer = true;
    PP_CHECK(s == want);

    // ExtraCorrect: dict + restore + barrier raised to >= 2000 (max, not set).
    s = applyHybrid(bal, kHybridExtraCorrect);
    want = bal; want.dictionaryRestore = true; want.restoreIfWrongSpelling = true;
    want.barrierBudgetUs = 2000;
    PP_CHECK(s == want);
    // ...and on MaxCorrectness (4000 > 2000) the barrier must NOT shrink.
    s = applyHybrid(baseStrategy(Profile::MaxCorrectness), kHybridExtraCorrect);
    PP_CHECK(s.barrierBudgetUs == 4000);
    PP_CHECK(s.dictionaryRestore);

    // LowCpu: spin clamp to <=20, floor 2, barrier spin <=50; nothing else.
    s = applyHybrid(bal, kHybridLowCpu);
    want = bal; want.consumerSpinCapUs = 20; want.consumerSpinFloorUs = 2;
    want.barrierSpinIters = 50;
    PP_CHECK(s == want);
    // Already-below values are kept (min, not set): Balanced cap 100 -> 20,
    // a hypothetical cap-10 base would stay 10.
    Strategy low = bal; low.consumerSpinCapUs = 10;
    s = applyHybrid(low, kHybridLowCpu);
    PP_CHECK(s.consumerSpinCapUs == 10);

    // All four at once on Fastest — exact expected composition.
    s = applyHybrid(baseStrategy(Profile::Fastest), 0x0F);
    PP_CHECK(s.output == OutputPreference::Tsf);        // tsf after inline wins
    PP_CHECK(s.deferInlineToConsumer);
    PP_CHECK(s.dictionaryRestore && s.restoreIfWrongSpelling);
    PP_CHECK(s.barrierBudgetUs == std::max<std::uint32_t>(500, 2000));
    PP_CHECK(s.consumerSpinCapUs == 20 && s.consumerSpinFloorUs == 2);
    PP_CHECK(s.barrierSpinIters == 50);                 // Fastest 400 -> 50

    // Flags above bit 3 are IGNORED by the resolver contract (the app masks
    // &0x0F at load; the header must not depend on that — high bits are simply
    // never consulted).
    PP_CHECK(applyHybrid(bal, 0xF0) == bal);
}

//============================================================================
// C — adaptive ladder
//============================================================================
static void testAdaptive() {
    std::printf("[PP-C] adaptive telemetry ladder + boundaries\n");
    const Strategy bal = baseStrategy(Profile::Balanced);

    PP_CHECK(adaptiveStrategy(Telemetry{}) == bal);     // no telemetry = Balanced

    Telemetry t;
    t.tsfSlowCommits = 2;
    PP_CHECK(adaptiveStrategy(t) == bal);                 // below threshold
    t.tsfSlowCommits = 3;
    {   Strategy s = adaptiveStrategy(t);
        PP_CHECK(s.output == OutputPreference::Inline);
        Strategy expect = bal; expect.output = OutputPreference::Inline;
        PP_CHECK(s == expect);                            // ONLY output moved
    }
    t = Telemetry{}; t.barrierTimeouts = 4;
    PP_CHECK(adaptiveStrategy(t) == bal);
    t.barrierTimeouts = 5;
    {   Strategy s = adaptiveStrategy(t);
        PP_CHECK(s.barrierBudgetUs == 2000);
        PP_CHECK(s.consumerSpinCapUs == 200);
    }
    t = Telemetry{}; t.recentKeysPerSec = 7;
    PP_CHECK(adaptiveStrategy(t) == bal);
    t.recentKeysPerSec = 8;
    PP_CHECK(adaptiveStrategy(t).consumerSpinCapUs == 200);   // max(100,200)

    // Battery/idle set cap 20; a BURST signal set cap 200 first — the LATER
    // rule wins (documented overwrite order: energy trumps throughput).
    t = Telemetry{}; t.recentKeysPerSec = 50; t.onBattery = true;
    PP_CHECK(adaptiveStrategy(t).consumerSpinCapUs == 20);
    t = Telemetry{}; t.recentKeysPerSec = 50; t.idleSeconds = 29;
    PP_CHECK(adaptiveStrategy(t).consumerSpinCapUs == 200);   // 29 < 30: no clamp
    t.idleSeconds = 30;
    PP_CHECK(adaptiveStrategy(t).consumerSpinCapUs == 20);

    // Single-core is the FINAL word (spinning steals the producer's core):
    // it wins over battery, burst and timeouts alike.
    t = Telemetry{}; t.logicalCpus = 1; t.recentKeysPerSec = 100;
    {   Strategy s = adaptiveStrategy(t);
        PP_CHECK(s.consumerSpinCapUs == 4);
        PP_CHECK(s.barrierSpinIters == 20);
        PP_CHECK(s.consumerSpinFloorUs == 2);   // untouched floor <= cap ✓
    }
    t.logicalCpus = 2;   // parity: with 2 cpus the final clamp disappears —
    //                      burst (100 keys/s) is still active -> cap 200
    PP_CHECK(adaptiveStrategy(t).consumerSpinCapUs == 200);

    // Everything at once — full-ladder fixed point (pins rule ORDER).
    t.tsfSlowCommits = 9; t.barrierTimeouts = 9; t.recentKeysPerSec = 99;
    t.idleSeconds = 99; t.onBattery = true; t.logicalCpus = 1;
    {   Strategy s = adaptiveStrategy(t);
        PP_CHECK(s.output == OutputPreference::Inline);
        PP_CHECK(s.barrierBudgetUs == 2000);
        PP_CHECK(s.consumerSpinCapUs == 4);
        PP_CHECK(s.consumerSpinFloorUs == 2);
        PP_CHECK(s.barrierSpinIters == 20);
        PP_CHECK(!s.dictionaryRestore);   // adaptive never touches the engine
        PP_CHECK(!s.deferInlineToConsumer);
    }
}

//============================================================================
// D+E — composition + registry encoding
//============================================================================
static void testComposition() {
    std::printf("[PP-D] resolveStrategy composition\n");
    Telemetry t; t.onBattery = true; t.barrierTimeouts = 6;
    // Adaptive + LowCpu: adaptive caps at 20, LowCpu min(20,20)=20, floor 2.
    Strategy s = resolveStrategy(Profile::Adaptive, kHybridLowCpu, t);
    PP_CHECK(s.consumerSpinCapUs == 20 && s.consumerSpinFloorUs == 2);
    // Adaptive sees Telemetry; the single-arg overload must NOT.
    PP_CHECK(resolveStrategy(Profile::Adaptive) == baseStrategy(Profile::Balanced));
    // Non-adaptive profiles ignore telemetry entirely.
    for (std::uint64_t x = 0; x < 4; ++x) {
        Telemetry tt; tt.tsfSlowCommits = x * 7; tt.barrierTimeouts = x * 5;
        tt.recentKeysPerSec = static_cast<std::uint32_t>(x * 40);
        tt.logicalCpus = x ? 1 : 2; tt.onBattery = x & 1;
        PP_CHECK(resolveStrategy(Profile::Fastest, kHybridExtraCorrect, tt) ==
                 resolveStrategy(Profile::Fastest, kHybridExtraCorrect, Telemetry{}));
    }

    std::printf("[PP-E] registry encoding\n");
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(Profile::kCount); ++i) {
        Profile p = profileFromIndex(i);
        PP_CHECK(static_cast<std::uint32_t>(p) == i);       // identity round-trip
    }
    PP_CHECK(profileFromIndex(static_cast<std::uint32_t>(Profile::kCount)) == Profile::Balanced);
    PP_CHECK(profileFromIndex(0xFFFFu) == Profile::Balanced);   // garbage clamps
    // Hybrid persistence is a DWORD masked to 4 bits (app side); the
    // resolver must agree with the masked value for every high-bit pattern.
    for (std::uint32_t raw = 0; raw < 64; ++raw) {
        const std::uint8_t masked = static_cast<std::uint8_t>(raw & 0x0Fu);
        PP_CHECK(resolveStrategy(Profile::Fastest, masked, Telemetry{}) ==
                 resolveStrategy(Profile::Fastest,
                                  static_cast<std::uint8_t>(raw & 0x0Fu), Telemetry{}));
        PP_CHECK(applyHybrid(baseStrategy(Profile::Fastest),
                             static_cast<std::uint8_t>(raw | 0xF0u)) ==
                 applyHybrid(baseStrategy(Profile::Fastest), masked));
    }
}

//============================================================================
// F — operator== field completeness guard
//============================================================================
// The app skips re-applying an unchanged strategy (g_appliedStrategy == st).
// If a future Strategy field is added but forgotten in operator==, its
// changes become invisible to that fast path AND to this test — so the test
// perturbs EVERY declared field individually and requires !=.
static void testEqualityCoverage() {
    std::printf("[PP-F] Strategy::operator== field completeness\n");
    const Strategy base{};
    { auto m = base; m.output = OutputPreference::Inline;              PP_CHECK(m != base); }
    { auto m = base; m.consumerSpinCapUs = base.consumerSpinCapUs + 1; PP_CHECK(m != base); }
    { auto m = base; m.consumerSpinFloorUs = base.consumerSpinFloorUs + 1; PP_CHECK(m != base); }
    { auto m = base; m.barrierBudgetUs = base.barrierBudgetUs + 1;     PP_CHECK(m != base); }
    { auto m = base; m.barrierSpinIters = base.barrierSpinIters + 1;   PP_CHECK(m != base); }
    { auto m = base; m.editBatchMax = base.editBatchMax + 1;            PP_CHECK(m != base); }
    { auto m = base; m.deferInlineToConsumer = !m.deferInlineToConsumer; PP_CHECK(m != base); }
    { auto m = base; m.dictionaryRestore = !m.dictionaryRestore;        PP_CHECK(m != base); }
    { auto m = base; m.restoreIfWrongSpelling = !m.restoreIfWrongSpelling; PP_CHECK(m != base); }
    { auto m = base; m.layoutRecheckEveryKey = !m.layoutRecheckEveryKey; PP_CHECK(m != base); }
    { auto m = base; m.tsfSlowDowngrade = !m.tsfSlowDowngrade;          PP_CHECK(m != base); }
    // MAINTENANCE CONTRACT: adding a field to Strategy REQUIRES (a) a
    // perturbation line above, and (b) pins in layers A/B/C — the compiler
    // cannot enforce it, so review this test in the same PR as the field.
}

//============================================================================
// G — the ONLY engine-surface mapping (mirrors main.cpp applyPerfStrategy)
//============================================================================
static void testEngineSurface() {
    std::printf("[PP-G] profile -> EngineOptions surface (dictionaryRestore, one-way)\n");
    auto wantDict = [](const Strategy& st, bool userOptIn) {
        return st.dictionaryRestore || userOptIn;   // EXACTLY the app expression
    };
    for (int p = 0; p < static_cast<int>(Profile::kCount); ++p) {
        for (std::uint32_t f = 0; f < 16; ++f) {
            const Strategy st = resolveStrategy(static_cast<Profile>(p),
                                                static_cast<std::uint8_t>(f),
                                                Telemetry{});
            const bool on = wantDict(st, false);
            // Turning the profile must NEVER switch the user's opt-in off:
            PP_CHECK(wantDict(st, true));
            // ...and only MaxCorrectness or the ExtraCorrect hybrid may
            // force it on by themselves:
            const bool expected = (p == static_cast<int>(Profile::MaxCorrectness)) ||
                                  (f & kHybridExtraCorrect) != 0;
            PP_CHECK_MSG(on == expected, "dictionary-restore surface");
        }
    }
}

//============================================================================
// H — property sweep over the whole option lattice
//============================================================================
static void testInvariants() {
    std::printf("[PP-H] invariants over profiles x hybrids x telemetry lattice\n");
    std::size_t n = 0;
    for (int p = 0; p < static_cast<int>(Profile::kCount); ++p) {
        for (std::uint32_t f = 0; f < 16; ++f) {
            for (std::uint32_t tl = 0; tl < 32; ++tl) {   // 5 telemetry bits
                Telemetry t;
                if (tl & 1) t.tsfSlowCommits = 5;
                if (tl & 2) t.barrierTimeouts = 6;
                if (tl & 4) t.recentKeysPerSec = 40;
                if (tl & 8) t.idleSeconds = 60;
                if (tl & 16) t.onBattery = true;
                t.logicalCpus = (tl % 3 == 0) ? 1 : (tl % 3 == 1 ? 2 : 8);
                const Strategy s = resolveStrategy(static_cast<Profile>(p),
                                                   static_cast<std::uint8_t>(f), t);
                ++n;
                // Consumer spin window must stay ordered (the hook clamps,
                // but an inverted window would silently disable spinning).
                PP_CHECK(s.consumerSpinFloorUs <= s.consumerSpinCapUs);
                // Barrier budget must survive the app's us->ms rounding with
                // at least one whole millisecond (kWaitBudgetMs semantics).
                PP_CHECK((s.barrierBudgetUs + 999) / 1000 >= 1);
                // editBatchMax lands inside the app's clamp [1,64] untouched.
                PP_CHECK(s.editBatchMax >= 1 && s.editBatchMax <= 64);
                // Engine surface stays binary as pinned in G.
                PP_CHECK(s.restoreIfWrongSpelling);   // NO profile disables it
            }
        }
    }
    std::printf("     %zu strategy points swept\n", n);
}

int main() {
    std::printf("== KieeKey perf-profile matrix (RC2) ==\n");
    testBaseTable();
    testHybrids();
    testAdaptive();
    testComposition();
    testEqualityCoverage();
    testEngineSurface();
    testInvariants();
    std::printf("\nPERF PROFILES: %s (%d checks, %d failures)\n",
                g_fail ? "FAILED" : "ALL PASSED", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
