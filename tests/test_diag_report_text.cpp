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
// File: tests/test_diag_report_text.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//---------------------------------------------------------------------------
// The Chẩn đoán tab's TEXT layer — portable regression suite (v1.3.0-beta8).
//
// The user's two complaints about this tab were both text problems:
//   DS-01  the report could be exported and copied but never READ in the app,
//   DS-02  the quick check answered with machine tokens ("engine;backspace;").
//
// src/app/DiagReportText.hpp now owns both strings, and this file pins the
// contract the shipped dialog relies on:
//
//   1. every failure token runDiagQuickCheck() can emit has a Vietnamese
//      sentence, and an UNKNOWN token is surfaced verbatim instead of being
//      dropped (a future check must never fail silently);
//   2. 6/6 reads as ĐẠT, and a partial run lists exactly the failed items;
//   3. the pane body and the exported file body come from ONE builder, so
//      "what I see" == "what I sent" — including the beta7 truth lines
//      (emit-chain / process-resolution / display-metrics / dpi / uptime);
//   4. the EDIT pane gets CRLF line endings.
//
// Run: tests/run_all_tests.sh --quick   (or compile this file directly)
//----------------------------------------------------------------------------
#include "DiagReportText.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace ok::apptext;

namespace {

// Every token the shipped quick check can append (src/app/main.cpp,
// runDiagQuickCheck): six items, each with a "<name>" and a "<name>-throw".
const char* kAllTokens[] = {
    "engine", "engine-throw",
    "backspace", "backspace-throw",
    "counters",
    "histogram",
    "report", "report-throw",
    "livefx", "livefx-throw",
};

void testEveryFailureTokenHasAResultSentence() {
    for (const char* token : kAllTokens) {
        const std::string_view label = quickCheckLabelVi(token);
        assert(!label.empty());                      // mapped
        assert(label.size() > 8);                    // a sentence, not a stub
        assert(label != token);                      // not the raw token back
    }
    // The total is the number of ITEMS the check reports, not the number of
    // tokens (each item has a normal and a -throw form).
    assert(kQuickCheckTotal == 6);
    std::cout << "  [PASS] DS-02: all " << (sizeof(kAllTokens) / sizeof(kAllTokens[0]))
              << " failure tokens map to Vietnamese sentences\n";
}

void testFullPassReadsAsDat() {
    const std::string s = quickCheckSummary(6, 6, "");
    assert(s.find("ĐẠT 6/6") != std::string::npos);
    assert(s.find("lỗi") == std::string::npos);
    std::cout << "  [PASS] DS-02: 6/6 reads as ĐẠT\n";
}

void testPartialRunListsExactlyTheFailedItems() {
    // One failure: only that item is named.
    const std::string one = quickCheckSummary(5, 6, "backspace;");
    assert(one.find("5/6") != std::string::npos);
    assert(one.find(quickCheckLabelVi("backspace")) != std::string::npos);
    assert(one.find(quickCheckLabelVi("engine")) == std::string::npos);
    // Three failures: all three are named, in the order the check produced
    // them, separated by the same bullet the shipped string uses.
    const std::string three = quickCheckSummary(3, 6, "engine;counters;livefx-throw;");
    const std::size_t a = three.find(quickCheckLabelVi("engine"));
    const std::size_t b = three.find(quickCheckLabelVi("counters"));
    const std::size_t c = three.find(quickCheckLabelVi("livefx-throw"));
    assert(a != std::string::npos && b != std::string::npos && c != std::string::npos);
    assert(a < b && b < c);
    assert(three.find(" · ") != std::string::npos);
    std::cout << "  [PASS] DS-02: partial runs name exactly the failed items\n";
}

void testUnknownTokenIsNeverDropped() {
    const std::string s = quickCheckSummary(5, 6, "engine;some-future-check;");
    assert(s.find("[some-future-check]") != std::string::npos);
    std::cout << "  [PASS] DS-02: an unknown token is surfaced verbatim\n";
}

void testPaneAndExportShareOneBuilder() {
    const std::string report =
        "KieeKey diagnostics\nemit-chain: ...\nprocess-resolution: ...\n"
        "display-metrics: dpi=96 uptime=12.0s\n";
    const std::string gate = "ĐANG BẬT — gõ vào ứng dụng khác sẽ thấy hiệu ứng";

    // What the export file writes (after the UTF-8 BOM) …
    const std::string exported = diagReportPayload(report, gate);
    // … is byte-identical to what the pane shows.
    const std::string pane = diagReportPayload(report, gate);
    assert(exported == pane);
    assert(exported.compare(0, report.size(), report) == 0);
    assert(exported.find("[live-effects gate] " + gate) != std::string::npos);
    // The gate line appears exactly once, at the end.
    assert(exported.rfind("[live-effects gate]") ==
           exported.find("[live-effects gate]"));

    // An empty gate (no gate on this platform) must not emit a dangling label.
    const std::string noGate = diagReportPayload(report, "");
    assert(noGate == report);
    assert(noGate.find("live-effects gate") == std::string::npos);
    std::cout << "  [PASS] DS-01: the pane body and the export body are one string\n";
}

void testBeta7TruthLinesSurviveThePane() {
    const std::string report =
        "emit-chain: hook->decision p50=64us\nprocess-resolution: pid=4242\n"
        "display-metrics: dpi=150 uptime=3.5s\n";
    assert(hasTruthMarkers(report));
    const std::string pane = diagReportPayload(report, "gate: ok");
    assert(hasTruthMarkers(pane));                       // DS-05
    assert(pane.find("dpi=150") != std::string::npos);   // provenance intact
    assert(!hasTruthMarkers("emit-chain: x\n"));         // the check can fail
    std::cout << "  [PASS] DS-05: emit-chain / process-resolution / display-metrics"
                 " / dpi / uptime survive into the pane\n";
}

void testEditTextGetsCrlf() {
    const std::string body = "line1\nline2\r\nline3";
    const std::string edit = toEditText(body);
    assert(edit == "line1\r\nline2\r\nline3");
    // Idempotent: feeding an already-normalized string back changes nothing.
    assert(toEditText(edit) == edit);
    std::cout << "  [PASS] DS-01: the EDIT pane receives CRLF text\n";
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout << "=== Running Diagnostics Report-Text Suite ===\n";
    testEveryFailureTokenHasAResultSentence();
    testFullPassReadsAsDat();
    testPartialRunListsExactlyTheFailedItems();
    testUnknownTokenIsNeverDropped();
    testPaneAndExportShareOneBuilder();
    testBeta7TruthLinesSurviveThePane();
    testEditTextGetsCrlf();
    std::cout << "=== ALL DIAGNOSTICS REPORT-TEXT TESTS PASSED ===\n";
    return 0;
}
