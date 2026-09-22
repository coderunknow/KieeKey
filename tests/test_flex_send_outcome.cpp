//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
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
// File: tests/test_flex_send_outcome.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//---------------------------------------------------------------------------
// FT-02 (v1.3.0-beta8) — "Gõ chữ Flexing ra app" must never fail silently.
//
// beta7 returned a bare 0 from every path, so these three real causes were
// indistinguishable from each other AND from "queued": no external target,
// a refused activation, a rejected chunk. The Lab said nothing.
//
// This suite pins the replacement contract:
//   1. every outcome has a non-empty Vietnamese reason;
//   2. the reasons are distinct (no copy-paste hiding a case);
//   3. exactly the five failure outcomes are failures — Queued/Sent are not,
//      and Empty is user guidance, not an error;
//   4. only a refused activation raises the one-time dialog;
//   5. the status line marks warnings, and every failure line survives being
//      shown in a 720-px-wide STATIC (the Lab's authored row) in one line.
//---------------------------------------------------------------------------
#include "FlexSendOutcome.hpp"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

using ok::flexsend::Outcome;

namespace {

int g_checks = 0;
#define CHECK(cond) do { ++g_checks; assert(cond); } while (0)

const Outcome kAll[] = {
    Outcome::Empty, Outcome::NoTarget, Outcome::NoEmitter, Outcome::ActivationDenied,
    Outcome::EmitFailed, Outcome::TargetLost, Outcome::Queued, Outcome::Sent,
};
constexpr std::size_t kAllCount = sizeof(kAll) / sizeof(kAll[0]);

void testEveryOutcomeHasAReason() {
    for (Outcome o : kAll) {
        const std::wstring reason = ok::flexsend::reasonVi(o);
        CHECK(!reason.empty());
        // A reason, not a code: at least a few words of Vietnamese.
        CHECK(reason.size() >= 20);
        const std::wstring line = ok::flexsend::statusLineVi(o);
        CHECK(!line.empty());
        CHECK(line.size() > reason.size());          // the marker prefix
    }
    std::cout << "  [PASS] FT-02: every outcome carries a Vietnamese reason\n";
}

void testReasonsAreDistinct() {
    for (std::size_t i = 0; i < kAllCount; ++i) {
        for (std::size_t j = i + 1; j < kAllCount; ++j) {
            CHECK(std::wstring(ok::flexsend::reasonVi(kAll[i])) !=
                  std::wstring(ok::flexsend::reasonVi(kAll[j])));
        }
    }
    std::cout << "  [PASS] FT-02: the eight reasons are pairwise distinct\n";
}

void testFailureClassification() {
    CHECK(ok::flexsend::isFailure(Outcome::NoTarget));
    CHECK(ok::flexsend::isFailure(Outcome::NoEmitter));
    CHECK(ok::flexsend::isFailure(Outcome::ActivationDenied));
    CHECK(ok::flexsend::isFailure(Outcome::EmitFailed));
    CHECK(ok::flexsend::isFailure(Outcome::TargetLost));
    // The success/queued paths must never be dressed as errors — a false alarm
    // trains users to ignore the row.
    CHECK(!ok::flexsend::isFailure(Outcome::Queued));
    CHECK(!ok::flexsend::isFailure(Outcome::Sent));
    CHECK(!ok::flexsend::isFailure(Outcome::Empty));
    std::cout << "  [PASS] FT-02: five failure outcomes, three non-failures\n";
}

void testFailureOutcomesExplainThemselvesInADialog() {
    for (Outcome o : kAll) {
        const std::wstring detail = ok::flexsend::detailVi(o);
        if (ok::flexsend::isFailure(o)) {
            // Every failure that reaches the dialog must carry real advice —
            // and the dialog text must be longer than the one-line reason.
            CHECK(!detail.empty());
            CHECK(detail.size() > std::wstring(ok::flexsend::reasonVi(o)).size());
        } else {
            // Nothing to interrupt for: no dialog text at all.
            CHECK(detail.empty());
        }
    }
    std::cout << "  [PASS] FT-02: failures carry dialog advice, successes carry none\n";
}

void testOnlyActivationDenialAlerts() {
    int alerts = 0;
    for (Outcome o : kAll) {
        if (ok::flexsend::needsAlert(o)) { ++alerts; }
    }
    CHECK(alerts == 1);
    CHECK(ok::flexsend::needsAlert(Outcome::ActivationDenied));
    std::cout << "  [PASS] FT-02: only the refused activation interrupts\n";
}

void testStatusLineMarksWarnings() {
    const std::wstring failure = ok::flexsend::statusLineVi(Outcome::NoTarget);
    const std::wstring success = ok::flexsend::statusLineVi(Outcome::Sent);
    CHECK(failure.rfind(L"⚠", 0) == 0);
    CHECK(success.rfind(L"•", 0) == 0);
    CHECK(failure != success);
    std::cout << "  [PASS] FT-02: the status line distinguishes warning from confirmation\n";
}

// The Lab authors this row as a 720-unit STATIC at 96 DPI, one line tall — a
// reason longer than the row would clip (the class of bug WS-A exists for).
void testReasonsFitTheAuthoredRow() {
    constexpr std::size_t kRowWidthPx = 720;
    constexpr std::size_t kAdvancePx = 6;        // ~Segoe UI 13 px, conservative
    constexpr std::size_t kMaxChars = kRowWidthPx / kAdvancePx;
    for (Outcome o : kAll) {
        ++g_checks;
        const std::wstring line = ok::flexsend::statusLineVi(o);
        const bool fits = line.size() <= kMaxChars;
        assert(fits);
    }
    std::cout << "  [PASS] FT-02: every status line fits the authored 720-unit row\n";
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout << "=== Running Flexing Send-Outcome Suite (FT-02) ===\n";
    testEveryOutcomeHasAReason();
    testReasonsAreDistinct();
    testFailureClassification();
    testFailureOutcomesExplainThemselvesInADialog();
    testOnlyActivationDenialAlerts();
    testStatusLineMarksWarnings();
    testReasonsFitTheAuthoredRow();
    std::cout << "=== ALL FLEXING SEND-OUTCOME TESTS PASSED (" << g_checks
              << " checks) ===\n";
    return 0;
}
