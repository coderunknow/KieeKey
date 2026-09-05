//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.1 RC3 - refactored and completed logic
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
// File: tests/test_notifications.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
// Unit tests: notification framework + QuickTelex unwanted-correction
// detector + performance-profile strategy model (all pure std C++).
#include "Notifications.hpp"
#include "PerfProfile.hpp"
#include "TextEngine.hpp"

#include <chrono>
#include <cstdio>
#include <map>
#include <string>
#include <thread>

using namespace ok::notify;
using namespace ok::perf;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static void testCenterBasics() {
    std::map<int, bool> persisted;
    Store st;
    st.isSuppressed  = [&](Id id) { return persisted[static_cast<int>(id)]; };
    st.setSuppressed = [&](Id id, bool v) { persisted[static_cast<int>(id)] = v; };
    NotificationCenter c(st);
    std::uint64_t t = 1'000'000;

    // below threshold → nothing
    CHECK(!c.raise(Id::QuickTelexUnwanted, 50, 3, t));
    CHECK(!c.hasPending());
    // above threshold → pending
    CHECK(c.raise(Id::QuickTelexUnwanted, 80, 3, t));
    CHECK(c.hasPending());
    // dedup: same id, not stronger → rejected
    CHECK(!c.raise(Id::QuickTelexUnwanted, 80, 4, t + 1));
    // stronger replaces
    CHECK(c.raise(Id::QuickTelexUnwanted, 95, 5, t + 2));
    Notification n = c.takePending(t + 10);
    CHECK(n.valid() && n.id == Id::QuickTelexUnwanted && n.confidence == 95 && n.evidence == 5);
    CHECK(!c.hasPending());
    // cooldown: immediately again → rejected
    CHECK(!c.raise(Id::QuickTelexUnwanted, 99, 6, t + 1000));
    // different id inside the global gap → rejected (no spam)
    CHECK(!c.raise(Id::HookReinstalled, 90, 1, t + 1000));
    // after global gap, other id ok
    CHECK(c.raise(Id::HookReinstalled, 90, 1, t + 10 + NotificationCenter::kGlobalMinGapMs + 1));
    CHECK(c.takePending(t + 10 + NotificationCenter::kGlobalMinGapMs + 2).id == Id::HookReinstalled);
    // after cooldown → allowed again (allowRepeat)
    const std::uint64_t later = t + policyFor(Id::QuickTelexUnwanted).cooldownMs + 10;
    CHECK(c.raise(Id::QuickTelexUnwanted, 90, 3, later));
    n = c.takePending(later + 1);
    CHECK(n.valid());
    // Don't show again → persisted + never raised again
    CHECK(c.resolve(Id::QuickTelexUnwanted, Action::DontShowAgain) == Action::DontShowAgain);
    CHECK(persisted[static_cast<int>(Id::QuickTelexUnwanted)]);
    const std::uint64_t much = later + 24ull * 3600 * 1000;
    CHECK(!c.raise(Id::QuickTelexUnwanted, 100, 9, much));
    // a fresh center loads suppression from the store
    NotificationCenter c2(st);
    c2.loadSuppressions();
    CHECK(c2.isSuppressed(Id::QuickTelexUnwanted));
    CHECK(!c2.raise(Id::QuickTelexUnwanted, 100, 9, much));
    c2.resetSuppression(Id::QuickTelexUnwanted);
    CHECK(!persisted[static_cast<int>(Id::QuickTelexUnwanted)]);
    CHECK(c2.raise(Id::QuickTelexUnwanted, 100, 9, much));
    // session mute
    NotificationCenter c3;
    c3.setSessionMuted(true);
    CHECK(!c3.raise(Id::QuickTelexUnwanted, 100, 9, t));
    // once-per-session id
    NotificationCenter c4;
    CHECK(c4.raise(Id::BarrierTimeouts, 90, 1, t));
    CHECK(c4.takePending(t).valid());
    CHECK(!c4.raise(Id::BarrierTimeouts, 100, 1, t + 10ull * 3600 * 1000));
    // hourly rate limit (HookReinstalled: 3/h, 10 min cooldown) — six
    // attempts spaced 10 min apart inside one hour → exactly 3 shown.
    NotificationCenter c5;
    const std::uint64_t step = policyFor(Id::HookReinstalled).cooldownMs + 1;
    std::uint64_t tt = 1;
    int shown = 0;
    for (int i = 0; i < 6; ++i, tt += step) {
        if (c5.raise(Id::HookReinstalled, 90, 1, tt) && c5.takePending(tt).valid()) { ++shown; }
    }
    CHECK(shown == 3);
    // no UI polling → nothing accumulates (single slot), input path unaffected
    NotificationCenter c6;
    for (int i = 0; i < 100000; ++i) { c6.raise(Id::HookReinstalled, 90, 1, t + i); }
    CHECK(c6.raisedTotal() == 1);
    CHECK(c6.presentedTotal() == 0);
    std::printf("[notify] center basics: %s\n", g_fail ? "FAIL" : "ok");
}

static void testQuickTelexDetector() {
    NotificationCenter c;
    QuickTelexDetector d;
    std::uint64_t t = 10'000;
    // Three clean expansions with commits → no notification, score decays to 0.
    for (int i = 0; i < 3; ++i) {
        d.observeExpansion(U'C', t); t += 100;
        d.observeWordCommitted(t);   t += 500;
    }
    CHECK(d.confidence() == 0);
    CHECK(!d.maybeRaise(c, t));
    // One undo (pp → backspace → pp raw) → below threshold.
    d.observeExpansion(U'P', t); t += 200;
    d.observeBackspace(t);       t += 50;   // depth -1 → into the expansion
    d.observeBackspace(t);       t += 50;
    d.observeRawChar(t);         t += 100;  // replaced with something else
    CHECK(d.undos() == 1);
    CHECK(!d.maybeRaise(c, t));
    // Two more undos on DIFFERENT pairs (general heuristic) → raise.
    d.observeExpansion(U'T', t); t += 200; d.observeBackspace(t); t += 50; d.observeExpansion(U'T', t); t += 300;
    d.observeWordCommitted(t);  // the re-expansion itself is accepted this time
    d.observeExpansion(U'G', t); t += 200; d.observeRestore(t); t += 300;
    CHECK(d.undos() == 3);
    CHECK(d.distinctLettersInWindow(t) == 3);
    CHECK(d.confidence() >= policyFor(Id::QuickTelexUnwanted).minConfidence);
    CHECK(d.maybeRaise(c, t));
    const Notification n = c.takePending(t);
    CHECK(n.id == Id::QuickTelexUnwanted && n.evidence == 3);
    // A backspace long after the expansion (outside the undo window) is not an undo.
    QuickTelexDetector d2;
    d2.observeExpansion(U'K', t); t += QuickTelexDetector::kUndoWindowMs + 1;
    d2.observeBackspace(t); d2.observeRawChar(t);
    CHECK(d2.undos() == 0);
    // A later typo fix ("chung" → ⌫ → "g") never reaches into the expansion.
    QuickTelexDetector d3;
    d3.observeExpansion(U'C', t); t += 10;
    for (int i = 0; i < 3; ++i) { d3.observeRawChar(t); t += 10; }
    d3.observeBackspace(t); t += 10; d3.observeRawChar(t); t += 10; d3.observeWordCommitted(t);
    CHECK(d3.undos() == 0 && d3.accepted() == 1);
    // Undos outside the 10-minute window age out.
    QuickTelexDetector d4;
    for (int i = 0; i < 3; ++i) { d4.observeExpansion(U'C', t); t += 10; d4.observeRestore(t); t += 10; }
    CHECK(d4.undosInWindow(t) == 3);
    CHECK(d4.undosInWindow(t + QuickTelexDetector::kWindowMs + 1) == 0);
    std::printf("[notify] quick-telex detector: %s\n", g_fail ? "FAIL" : "ok");
}

// End-to-end: drive the REAL engine with quickTelex ON through the shipped
// hook-side observation rules (mirrors main.cpp's observeEngineDecision) and
// confirm the pp/tt/cc undo pattern trips the detector while normal
// Vietnamese typing with quick-Telex accepted does not.
static void testDetectorAgainstEngine() {
    using namespace ok::text;
    EngineOptions o; o.quickTelex = true;
    TextEngine e(o);
    QuickTelexDetector d;
    NotificationCenter c;
    std::uint64_t t = 1;
    char32_t lastRaw = 0;
    auto feed = [&](char32_t ch) {
        TextInput in;
        if (ch == U' ') { in.kind = InputKind::Space; }
        else if (ch == U'\b') { in.kind = InputKind::Backspace; }
        else { in.kind = InputKind::Char; in.ch = ch; }
        const EngineResult& r = e.process(in);
        t += 50;
        // ---- the observation rules (kept identical in main.cpp) ----
        if (in.kind == InputKind::Backspace) { d.observeBackspace(t); lastRaw = 0; return; }
        if (in.kind == InputKind::Space)     { d.observeWordCommitted(t); lastRaw = 0; return; }
        const char32_t up = (ch >= U'a' && ch <= U'z') ? ch - 32 : ch;
        if (r.code == EngineCode::WillProcess && r.backspaceCount == 1 && r.newCharCount == 2 &&
            up == lastRaw && kQuickTelex.find(static_cast<std::uint16_t>(up)) != kQuickTelex.end()) {
            d.observeExpansion(up, t); lastRaw = 0; return;
        }
        if (r.code == EngineCode::Restore || r.code == EngineCode::RestoreAndStartNewSession) {
            d.observeRestore(t);
        } else {
            d.observeRawChar(t);
        }
        lastRaw = up;
    };
    auto type = [&](const char32_t* s) { for (; *s; ++s) feed(*s); };
    // Accepted quick-telex usage: "cc" → ch, committed.
    type(U"ccung ttoi ");
    CHECK(d.expansions() >= 2);
    CHECK(d.undos() == 0);
    // Quick-Telex fires only at WORD START (index_ == 1), so the real-world
    // unwanted-correction pattern is an English/brand token beginning with
    // a double consonant: "ppt", "ttyl", "cc:" … The user types "pp" → sees
    // "ph" → backspaces twice → retypes "pp". On the retype the engine
    // expands AGAIN (same word start) — a second undo cycle — until the
    // user gives up or turns the option off. Each cycle is one confirmed
    // undo (backspace while armed + Restore/raw double).
    type(U"pp");              // expands to "ph"
    CHECK(d.expansions() >= 3);
    type(U"\b\bp");           // undo; a single p is raw again
    type(U"p");               // expands again ("ph") — second cycle
    type(U"\b\bpt ");         // give up: "pt "
    const auto undosAfterPp = d.undos();
    CHECK(undosAfterPp >= 1);
    type(U"tt\b\bt");         // "ttyl" attempt
    type(U"tyl ");
    type(U"cc\b\bc");         // "cc:" attempt
    type(U"c: ");
    CHECK(d.undos() >= undosAfterPp + 2);
    CHECK(d.undosInWindow(t) >= QuickTelexDetector::kMinUndos);
    CHECK(d.maybeRaise(c, t));
    std::printf("[notify] engine-driven: expansions=%llu undos=%llu accepted=%llu conf=%u\n",
                (unsigned long long)d.expansions(), (unsigned long long)d.undos(),
                (unsigned long long)d.accepted(), d.confidence());
    // The heuristic must NOT fire on the accepted-only stream.
    d.reset(); NotificationCenter c0; lastRaw = 0;
    e.startNewSession();
    type(U"ccung ttoi kkhong nngay qquas ppho ");
    CHECK(d.undos() == 0);
    CHECK(!d.maybeRaise(c0, t));
}

static void testProfiles() {
    const Strategy bal = resolveStrategy(Profile::Balanced);
    const Strategy fast = resolveStrategy(Profile::Fastest);
    const Strategy flick = resolveStrategy(Profile::LeastFlicker);
    const Strategy corr = resolveStrategy(Profile::MaxCorrectness);
    // Balanced == RC1 shipping defaults
    CHECK(bal.output == OutputPreference::Auto && bal.consumerSpinCapUs == 100 &&
          bal.barrierBudgetUs == 1000 && bal.editBatchMax == 32 && !bal.dictionaryRestore);
    // Every profile is distinct and has REAL behavioural effects
    CHECK(fast != bal && flick != bal && corr != bal && fast != flick && fast != corr && flick != corr);
    CHECK(fast.output == OutputPreference::Inline && fast.barrierBudgetUs < bal.barrierBudgetUs);
    CHECK(flick.output == OutputPreference::Tsf && flick.editBatchMax > bal.editBatchMax && flick.deferInlineToConsumer);
    CHECK(corr.dictionaryRestore && corr.layoutRecheckEveryKey && corr.editBatchMax == 1 &&
          corr.barrierBudgetUs > flick.barrierBudgetUs);
    // Adaptive with quiet telemetry == Balanced
    CHECK(resolveStrategy(Profile::Adaptive, kHybridNone, Telemetry{}) == bal);
    // Adaptive reacts
    Telemetry tm; tm.tsfSlowCommits = 5;
    CHECK(resolveStrategy(Profile::Adaptive, kHybridNone, tm).output == OutputPreference::Inline);
    tm = Telemetry{}; tm.barrierTimeouts = 10;
    CHECK(resolveStrategy(Profile::Adaptive, kHybridNone, tm).barrierBudgetUs == 2000);
    tm = Telemetry{}; tm.idleSeconds = 120;
    CHECK(resolveStrategy(Profile::Adaptive, kHybridNone, tm).consumerSpinCapUs == 20);
    tm = Telemetry{}; tm.logicalCpus = 1;
    CHECK(resolveStrategy(Profile::Adaptive, kHybridNone, tm).consumerSpinCapUs == 4);
    // Hybrids compose on any base
    const Strategy fastCorrect = resolveStrategy(Profile::Fastest, kHybridExtraCorrect, Telemetry{});
    CHECK(fastCorrect.output == OutputPreference::Inline && fastCorrect.dictionaryRestore &&
          fastCorrect.barrierBudgetUs == 2000);
    const Strategy flickLow = resolveStrategy(Profile::LeastFlicker, kHybridLowCpu, Telemetry{});
    CHECK(flickLow.output == OutputPreference::Tsf && flickLow.consumerSpinCapUs == 20);
    const Strategy corrInline = resolveStrategy(Profile::MaxCorrectness, kHybridPreferInline, Telemetry{});
    CHECK(corrInline.output == OutputPreference::Inline && corrInline.dictionaryRestore);
    // Index round-trip / names
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(Profile::kCount); ++i) {
        CHECK(static_cast<std::uint32_t>(profileFromIndex(i)) == i);
        CHECK(std::string(profileName(profileFromIndex(i))) != "?");
    }
    CHECK(profileFromIndex(99) == Profile::Balanced);
    std::printf("[profiles] strategy model: %s\n", g_fail ? "FAIL" : "ok");
}

// Latency guard: raise()+takePending() must be in the tens of nanoseconds
// (never on the input path budget). Loose bound so CI VMs pass.
static void testLatency() {
    NotificationCenter c;
    QuickTelexDetector d;
    const auto t0 = std::chrono::steady_clock::now();
    constexpr int N = 2'000'000;
    std::uint64_t now = 1;
    for (int i = 0; i < N; ++i) {
        d.observeExpansion(U'P', now); d.observeBackspace(now + 1); d.observeBackspace(now + 2); d.observeRawChar(now + 3);
        d.maybeRaise(c, now + 3);
        now += 7;
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    const double perOp = static_cast<double>(ns) / N;
    std::printf("[notify] detector+center per observe-cycle: %.1f ns\n", perOp);
    CHECK(perOp < 2000.0);
}

int main() {
    testCenterBasics();
    testQuickTelexDetector();
    testDetectorAgainstEngine();
    testProfiles();
    testLatency();
    if (g_fail) { std::printf("[notify] VERDICT: FAIL (%d)\n", g_fail); return 1; }
    std::printf("[notify] VERDICT: PASS\n");
    return 0;
}
