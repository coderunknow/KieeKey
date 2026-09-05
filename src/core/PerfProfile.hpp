//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.1 Stable - refactored and completed logic
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
// File: src/core/PerfProfile.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// PerfProfile — v1.2.1 RC2 Performance Preference Profiles.
//
// ONE centralized strategy model. Every tunable that trades latency against
// flicker against correctness lives in `Strategy`; the app, the shim
// pipeline bench and the unit tests all derive their behaviour from the SAME
// `resolveStrategy()` so a profile means one thing everywhere.
//
// Profiles (user-facing):
//   Fastest         — lowest input latency. Inline SendInput everywhere the
//                     policy allows, hot consumer (long spin), short ordering
//                     barrier, no extra engine correctness passes.
//   LeastFlicker    — TSF commits wherever the app supports them (no visible
//                     backspace/retype), larger edit batches, consumer kept
//                     hot so the TSF hop does not add a wake.
//   MaxCorrectness  — every safety net on: dictionary-backed restore, longest
//                     ordering barrier, per-key foreground/layout re-check,
//                     conservative batching.
//   Balanced        — the RC1 shipping behaviour (default). Auto output
//                     policy, adaptive spin, 1 ms barrier.
//   Adaptive        — Balanced that re-resolves from live telemetry: slow TSF
//                     commits push output inline; barrier timeouts lengthen
//                     the barrier; long idle drops the spin cap to save CPU;
//                     sustained bursts raise it.
//
// Hybrid combos: `HybridFlags` OR-ed on top of a base profile —
//   PreferInline / PreferTsf / ExtraCorrectness / LowCpu — for users who
//   want e.g. "Fastest but with the dictionary check" or "Least flicker on a
//   laptop" (LowCpu caps the spin).
//
// This header is pure std C++ (no Win32) so the resolution table is
// unit-tested on every platform (tests/test_perf_profiles.cpp).
//----------------------------------------------------------------------------
#pragma once

#include <algorithm>
#include <cstdint>

namespace ok::perf {

enum class Profile : std::uint8_t {
    Balanced = 0,      // default — RC1 behaviour
    Fastest,
    LeastFlicker,
    MaxCorrectness,
    Adaptive,
    kCount
};

enum HybridFlags : std::uint8_t {
    kHybridNone           = 0,
    kHybridPreferInline   = 1u << 0,   // output: inline SendInput unless forced TSF
    kHybridPreferTsf      = 1u << 1,   // output: TSF wherever the app allows
    kHybridExtraCorrect   = 1u << 2,   // engine: dictionary restore + long barrier
    kHybridLowCpu         = 1u << 3,   // consumer: short spin, idle-friendly
};

// Output routing preference. `Auto` = the per-app policy table (browsers /
// Office → TSF, everything else → inline) — the RC1 behaviour.
enum class OutputPreference : std::uint8_t { Auto = 0, Inline, Tsf };

struct Strategy {
    OutputPreference output       = OutputPreference::Auto;
    std::uint32_t consumerSpinCapUs   = 100;  // adaptive-spin upper bound
    std::uint32_t consumerSpinFloorUs = 2;    // adaptive-spin lower bound
    std::uint32_t barrierBudgetUs     = 1000; // ordering-barrier hard cap
    std::uint32_t barrierSpinIters    = 200;  // spin before blocking
    std::uint32_t editBatchMax        = 32;   // consumer TSF batch size
    bool deferInlineToConsumer        = false;// inline edits ride the consumer
    bool dictionaryRestore            = false;// engine: lexicon veto at breaks
    bool restoreIfWrongSpelling       = true; // engine: raw restore at breaks
    bool layoutRecheckEveryKey        = false;// hook: HKL re-check per key
    bool tsfSlowDowngrade             = true; // auto-downgrade slow TSF apps

    friend bool operator==(const Strategy& a, const Strategy& b) noexcept {
        return a.output == b.output && a.consumerSpinCapUs == b.consumerSpinCapUs &&
               a.consumerSpinFloorUs == b.consumerSpinFloorUs &&
               a.barrierBudgetUs == b.barrierBudgetUs &&
               a.barrierSpinIters == b.barrierSpinIters &&
               a.editBatchMax == b.editBatchMax &&
               a.deferInlineToConsumer == b.deferInlineToConsumer &&
               a.dictionaryRestore == b.dictionaryRestore &&
               a.restoreIfWrongSpelling == b.restoreIfWrongSpelling &&
               a.layoutRecheckEveryKey == b.layoutRecheckEveryKey &&
               a.tsfSlowDowngrade == b.tsfSlowDowngrade;
    }
    friend bool operator!=(const Strategy& a, const Strategy& b) noexcept { return !(a == b); }
};

// Live signals the Adaptive profile consumes. All are cheap counters the app
// already maintains; the resolver never blocks and never reads the OS.
struct Telemetry {
    std::uint64_t tsfSlowCommits    = 0;   // composer watchdog hits (session)
    std::uint64_t barrierTimeouts   = 0;   // ordering-barrier budget misses
    std::uint32_t recentKeysPerSec  = 0;   // typing rate over the last window
    std::uint32_t idleSeconds       = 0;   // since the last keystroke
    bool          onBattery         = false;
    std::uint32_t logicalCpus       = 2;
};

[[nodiscard]] constexpr const char* profileName(Profile p) noexcept {
    switch (p) {
        case Profile::Balanced:       return "Balanced";
        case Profile::Fastest:        return "Fastest";
        case Profile::LeastFlicker:   return "LeastFlicker";
        case Profile::MaxCorrectness: return "MaxCorrectness";
        case Profile::Adaptive:       return "Adaptive";
        default:                      return "?";
    }
}

[[nodiscard]] constexpr Profile profileFromIndex(std::uint32_t i) noexcept {
    return i < static_cast<std::uint32_t>(Profile::kCount) ? static_cast<Profile>(i)
                                                           : Profile::Balanced;
}

// Base table — the ONLY place profile semantics are defined.
[[nodiscard]] constexpr Strategy baseStrategy(Profile p) noexcept {
    Strategy s{};   // Balanced == RC1 defaults
    switch (p) {
        case Profile::Fastest:
            s.output                = OutputPreference::Inline;
            s.consumerSpinCapUs     = 200;   // stay hot across word gaps
            s.consumerSpinFloorUs   = 4;
            s.barrierBudgetUs       = 500;
            s.barrierSpinIters      = 400;   // resolve in-spin, avoid the kernel wait
            s.editBatchMax          = 8;     // shortest pass-through stall
            s.dictionaryRestore     = false;
            s.layoutRecheckEveryKey = false;
            break;
        case Profile::LeastFlicker:
            s.output                = OutputPreference::Tsf;
            s.consumerSpinCapUs     = 200;   // TSF hop must not add a wake
            s.consumerSpinFloorUs   = 4;
            s.barrierBudgetUs       = 2000;  // never let a key overtake a commit
            s.editBatchMax          = 64;    // one commit per burst
            s.deferInlineToConsumer = true;  // even inline edits serialize
            s.tsfSlowDowngrade      = false; // flicker-free beats speed here
            break;
        case Profile::MaxCorrectness:
            s.output                = OutputPreference::Auto;
            s.consumerSpinCapUs     = 100;
            s.barrierBudgetUs       = 4000;  // ordering is sacred
            s.barrierSpinIters      = 400;
            s.editBatchMax          = 1;     // one edit, one commit — no coalescing
            s.dictionaryRestore     = true;
            s.restoreIfWrongSpelling= true;
            s.layoutRecheckEveryKey = true;
            break;
        case Profile::Adaptive:   // resolved dynamically; base = Balanced
        case Profile::Balanced:
        default:
            break;
    }
    return s;
}

[[nodiscard]] constexpr Strategy applyHybrid(Strategy s, std::uint8_t flags) noexcept {
    if (flags & kHybridPreferInline) { s.output = OutputPreference::Inline; }
    if (flags & kHybridPreferTsf)    { s.output = OutputPreference::Tsf; s.deferInlineToConsumer = true; }
    if (flags & kHybridExtraCorrect) {
        s.dictionaryRestore = true;
        s.restoreIfWrongSpelling = true;
        s.barrierBudgetUs = std::max<std::uint32_t>(s.barrierBudgetUs, 2000);
    }
    if (flags & kHybridLowCpu) {
        s.consumerSpinCapUs   = std::min<std::uint32_t>(s.consumerSpinCapUs, 20);
        s.consumerSpinFloorUs = 2;
        s.barrierSpinIters    = std::min<std::uint32_t>(s.barrierSpinIters, 50);
    }
    return s;
}

// Adaptive resolution. Deterministic in (telemetry) so it is testable; the
// app re-runs it on a slow timer (settings-dialog tick / 1 s), never per key.
[[nodiscard]] constexpr Strategy adaptiveStrategy(const Telemetry& t) noexcept {
    Strategy s = baseStrategy(Profile::Balanced);
    // Slow TSF commits observed → this machine/app mix pays for TSF; go inline.
    if (t.tsfSlowCommits >= 3) { s.output = OutputPreference::Inline; }
    // Barrier misses → the consumer cannot keep up within 1 ms; give it more
    // budget and keep it hotter rather than letting keys overtake edits.
    if (t.barrierTimeouts >= 5) {
        s.barrierBudgetUs   = 2000;
        s.consumerSpinCapUs = 200;
    }
    // Sustained fast typing → keep the consumer hot (no wake per word).
    if (t.recentKeysPerSec >= 8) { s.consumerSpinCapUs = std::max<std::uint32_t>(s.consumerSpinCapUs, 200); }
    // Long idle or battery → do not burn CPU spinning.
    if (t.idleSeconds >= 30 || t.onBattery) {
        s.consumerSpinCapUs   = 20;
        s.consumerSpinFloorUs = 2;
    }
    // Single-core hosts: spinning steals the producer's core.
    if (t.logicalCpus <= 1) {
        s.consumerSpinCapUs = 4;
        s.barrierSpinIters  = 20;
    }
    return s;
}

[[nodiscard]] constexpr Strategy resolveStrategy(Profile p, std::uint8_t hybrid,
                                                 const Telemetry& t) noexcept {
    const Strategy base = (p == Profile::Adaptive) ? adaptiveStrategy(t) : baseStrategy(p);
    return applyHybrid(base, hybrid);
}

[[nodiscard]] inline Strategy resolveStrategy(Profile p) noexcept {
    return resolveStrategy(p, kHybridNone, Telemetry{});
}

} // namespace ok::perf
