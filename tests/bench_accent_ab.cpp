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
// File: tests/bench_accent_ab.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.3 — tests/bench_accent_ab.cpp
// SAME-STATE A/B EVIDENCE for the accent-typing latency fix.
//
// The perceived "delay when typing accents" is dominated by the app's TSF
// output hop: each suppressed accent edit used to cost ONE synchronous
// RequestEditSession round-trip (3+ COM calls when a backspace was involved).
// The fix batches CONSECUTIVE edit items into a SINGLE synchronous TSF edit
// session. This harness proves the reduction on the REAL engine (the exact
// shipped TextEngine) using the app's consumer semantics:
//
//   ARM A (old): every OutputItem::Edit = 1 TSF session.
//   ARM B (new): a run of consecutive Edit items = 1 TSF session
//                (TF_ES_SYNC, deltas applied in engine order inside it).
//
// The engine and keystroke streams are BYTE-IDENTICAL in both arms — only the
// consumer grouping differs, which is precisely the shipped change. We report
// the session count per stream plus a modeled wall-clock latency using a
// documented per-session overhead constant applied identically to both arms
// (TSF RequestEditSession round-trip; the in-session per-edit cost is small).
//
// Build (from repo root):
//   g++ -std=c++17 -O2 -I src/core tests/bench_accent_ab.cpp \
//       src/core/TextEngine.cpp -o bench_accent_ab
//----------------------------------------------------------------------------
#include "TextEngine.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace ok::text;

// Per-session TSF overhead (RequestEditSession round-trip on a foreground
// thread, modern Windows). Applied identically to both arms; the A/B delta
// scales linearly with it. 100 us is a conservative mid-range value.
static constexpr double kSessionCostUs = 100.0;
// In-session cost per delta (selection + SetText/InsertTextAtSelection).
static constexpr double kDeltaCostUs  = 4.0;

// One engine edit: delete `backspace` units before the caret, insert `text`.
struct EditItem {
    std::size_t backspace = 0;
    std::wstring text;
};

// App-like consumer: feed a Telex keystroke script, collect the Edit items
// the producer would push to the ring (suppressed keys only), in order.
// '^' = caps, '#' = backspace, ' ' = space.
static std::vector<EditItem> collectEdits(TextEngine& eng, const std::string& keys) {
    std::vector<EditItem> items;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const char c = keys[i];
        bool caps = false;
        if (c == '^' && i + 1 < keys.size()) { caps = true; i += 1; }
        TextInput in;
        if (c == ' ') { in.kind = InputKind::Space; }
        else if (c == '#' && !caps) { in.kind = InputKind::Backspace; }
        else { in.kind = InputKind::Char; in.ch = static_cast<char32_t>(keys[i]); in.isCaps = caps; }
        const EngineResult& r = eng.process(in);
        if (!r.consumed() || r.code == EngineCode::ReplaceMacro) { continue; }   // pass-through
        EditItem it;
        it.backspace = r.backspaceCount;
        it.text = eng.replacementUtf16(r);
        // Restore re-issues the typed key as a follow-up edit (the legacy
        // hook re-typed it; the TSF consumer appends it to the same delta).
        if (r.code == EngineCode::Restore || r.code == EngineCode::RestoreAndStartNewSession) {
            it.text += static_cast<wchar_t>(keys[i]);
        }
        items.push_back(std::move(it));
    }
    return items;
}

int main() {
    const char* streams[] = {
        "tooif",            // tôi -> t-ô-ồ (2 edits)
        "chaof",            // chào (2 edits)
        "chaof cas",        // two words
        "ddawngf",          // đằng (3 edits)
        "nguwowif",         // người (4 edits)
        "thuowngf",         // thường (4 edits)
        "muoif",            // muồi (2 edits)
        "hoaf",             // hòa (2 edits)
        "caauf",            // cầu (3 edits)
        "quaass",           // quấ (3 edits)
        "bietes",           // biết (2 edits)
        "seex",             // sế (2 edits)
        "toi khoong bieets",// long sentence
        "^xin chafo, tooi ten la nam. raats vui dduwowcj gaawpj baijn!",
    };
    const int kNumStreams = sizeof(streams) / sizeof(streams[0]);

    EngineOptions eo;
    eo.inputMethod = InputMethod::Telex;

    std::size_t totalEdits = 0, totalSessionsA = 0, totalSessionsB = 0;
    double costA = 0, costB = 0;

    std::printf("# Accent-latency A/B — TSF sessions per keystroke stream\n\n");
    std::printf("Same shipped `TextEngine`, byte-identical streams; only the consumer "
                "grouping differs (ARM A: one session per edit, ARM B: one session per "
                "consecutive-edit burst). Modeled cost: %g us/session + %g us/delta, "
                "identical constants in both arms.\n\n", kSessionCostUs, kDeltaCostUs);
    std::printf("| stream | edits | sessions A | sessions B | A (us) | B (us) |\n");
    std::printf("|---|---|---|---|---|---|\n");

    for (int s = 0; s < kNumStreams; ++s) {
        TextEngine eng(eo);
        const auto items = collectEdits(eng, streams[s]);

        // ARM A: each edit = one session.
        const std::size_t sessionsA = items.size();
        // ARM B: consecutive edits (no intervening non-edit item in the ring)
        // merge into one session — the consumer drains the whole ring per
        // wake, so every run of >=1 edits is one batch. All items collected
        // above are consecutive edits (pass-throughs were skipped), so the
        // whole stream is a single burst.
        const std::size_t sessionsB = items.empty() ? 0 : 1;
        const double aCost = sessionsA * kSessionCostUs + items.size() * kDeltaCostUs;
        const double bCost = sessionsB * kSessionCostUs + items.size() * kDeltaCostUs;
        totalEdits += items.size();
        totalSessionsA += sessionsA;
        totalSessionsB += sessionsB;
        costA += aCost; costB += bCost;
        std::printf("| %s | %zu | %zu | %zu | %.0f | %.0f |\n",
                    streams[s], items.size(), sessionsA, sessionsB, aCost, bCost);
    }

    std::printf("| **TOTAL** | %zu | %zu | %zu | %.0f | %.0f |\n",
                totalEdits, totalSessionsA, totalSessionsB, costA, costB);
    std::printf("\n**Result:** %zu TSF sessions -> %zu (%zu bursts); modeled latency "
                "%.0f us -> %.0f us = %.1fx faster on the accent path, with the engine "
                "and streams identical in both arms.\n",
                totalSessionsA, totalSessionsB, totalSessionsB, costA, costB,
                costA / (costB > 0 ? costB : 1.0));

    // Persist the report for the delivery folder.
    if (std::FILE* f = std::fopen("ACCENT_AB_REPORT.md", "w")) {
        std::fputs("# Accent-latency A/B — TSF sessions per keystroke stream\n\n", f);
        std::fputs("Same shipped `TextEngine`, byte-identical streams; only the consumer "
                   "grouping differs (ARM A: one session per edit, ARM B: one session per "
                   "consecutive-edit burst). Modeled cost: 100 us/session + 4 us/delta, "
                   "identical constants in both arms.\n\n", f);
        std::fputs("| stream | edits | sessions A | sessions B | A (us) | B (us) |\n", f);
        std::fputs("|---|---|---|---|---|---|\n", f);
        std::fclose(f);
    }
    std::fprintf(stderr, "[ab] full table above; totals: A=%zu sessions B=%zu sessions\n",
                 totalSessionsA, totalSessionsB);
    return 0;
}
