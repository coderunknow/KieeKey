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
// File: src/app/DiagReportText.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//---------------------------------------------------------------------------
// v1.3.0-beta8 (bugs DS-01 / DS-02 / DS-05) — the diagnostics TEXT layer.
//
// WHY THIS HEADER IS PORTABLE (and why it exists at all)
//   The Chẩn đoán tab used to be a dead end: ok::diag::report() could leave the
//   app (file / clipboard) but never be READ inside it, and the quick check
//   answered with machine tokens ("engine;backspace;") that a user cannot act
//   on. Both problems are pure STRING problems, so they live here — no HWND,
//   no <windows.h> — which lets tests/test_diag_report_text.cpp pin the exact
//   contract on every platform, including the one CI runs on Linux.
//
//   The one that matters most is diagReportPayload(): the pane and the export
//   file are composed by ONE builder, so "what I see" and "what I sent" can
//   never drift apart again (DS-05: the beta7 truth lines — emit-chain,
//   process-resolution, display-metrics — must survive in both).
//---------------------------------------------------------------------------
#ifndef KIEEKEY_APP_DIAG_REPORT_TEXT_HPP
#define KIEEKEY_APP_DIAG_REPORT_TEXT_HPP

#include <cstddef>
#include <string>
#include <string_view>

namespace ok::apptext {

//---------------------------------------------------------------------------
// DS-02 — the quick check's failure tokens, in Vietnamese.
//
// runDiagQuickCheck() (src/app/main.cpp) appends "<token>;" for every failed
// item. The tokens are stable machine identifiers (they stay in the report
// file); these are the sentences the user reads in the dialog.
//---------------------------------------------------------------------------
struct QuickCheckLabel {
    std::string_view token;     // as produced by runDiagQuickCheck()
    std::string_view labelVi;   // what the user reads
};

inline constexpr int kQuickCheckTotal = 6;

inline constexpr QuickCheckLabel kQuickCheckLabels[] = {
    {"engine", "lõi gõ: round-trip âm tiết (booj → bộ)"},
    {"engine-throw", "lõi gõ: NÉM LỖI khi soạn âm tiết"},
    {"backspace", "backspace ở biên từ: phải là no-op nhưng không phải"},
    {"backspace-throw", "backspace ở biên từ: NÉM LỖI"},
    {"counters", "bộ đếm sự kiện bàn phím: NÉM LỖI"},
    {"histogram", "biểu đồ độ trễ: NÉM LỖI"},
    {"report", "báo cáo/health verdict: rỗng hoặc quá ngắn"},
    {"report-throw", "báo cáo: NÉM LỖI"},
    {"livefx", "chuỗi quyết định hiệu ứng trực tiếp: không áp dụng được"},
    {"livefx-throw", "chuỗi quyết định hiệu ứng trực tiếp: NÉM LỖI"},
};

[[nodiscard]] inline std::string_view quickCheckLabelVi(std::string_view token) noexcept {
    for (const QuickCheckLabel& item : kQuickCheckLabels) {
        if (item.token == token) { return item.labelVi; }
    }
    return {};
}

// "Kiểm tra nhanh: ĐẠT 6/6 hạng mục" / "Kiểm tra nhanh: 4/6 — lỗi: …"
//
// A token the table does not know is NEVER dropped: it is shown verbatim in
// brackets, so a future check cannot fail silently (that was the whole point
// of DS-02 — the beta7 status line printed "lỗi: engine;backspace;").
[[nodiscard]] inline std::string quickCheckSummary(int passed, int total,
                                                  std::string_view failDetail) {
    std::string out = "Kiểm tra nhanh: ";
    if (passed >= total && total > 0) {
        out += "ĐẠT ";
        out += std::to_string(passed);
        out += "/";
        out += std::to_string(total);
        out += " hạng mục ✓";
        return out;
    }
    out += std::to_string(passed);
    out += "/";
    out += std::to_string(total);
    out += " — lỗi: ";
    bool first = true;
    std::size_t pos = 0;
    while (pos < failDetail.size()) {
        const std::size_t end = failDetail.find(';', pos);
        const std::string_view token = failDetail.substr(
            pos, (end == std::string_view::npos ? failDetail.size() : end) - pos);
        pos = (end == std::string_view::npos) ? failDetail.size() : end + 1;
        if (token.empty()) { continue; }
        if (!first) { out += " · "; }
        first = false;
        const std::string_view label = quickCheckLabelVi(token);
        if (label.empty()) {
            out += "[";
            out += token;
            out += "]";
        } else {
            out += label;
        }
    }
    if (first) { out += "(không rõ nguyên nhân)"; }
    return out;
}

//---------------------------------------------------------------------------
// DS-01 / DS-05 — ONE builder for the report body.
//
// The export writes a UTF-8 BOM and this string; the in-app pane shows this
// string. `liveGateLine` is the same text the gate readout shows
// (liveGateStatusText() in main.cpp) and may be empty on a platform that has
// no gate at all.
//---------------------------------------------------------------------------
[[nodiscard]] inline std::string diagReportPayload(std::string_view report,
                                                   std::string_view liveGateLine) {
    std::string out(report);
    if (!liveGateLine.empty()) {
        out += "\n[live-effects gate] ";
        out += liveGateLine;
        out += "\n";
    }
    return out;
}

// The lines the beta7 "truth" release made mandatory in every report — the
// pane must carry them too, or it is showing the user a different document
// than the one they email to support.
inline constexpr std::string_view kDiagTruthMarkers[] = {
    "emit-chain", "process-resolution", "display-metrics", "dpi", "uptime",
};

[[nodiscard]] inline bool hasTruthMarkers(std::string_view text) noexcept {
    for (const std::string_view marker : kDiagTruthMarkers) {
        if (text.find(marker) == std::string_view::npos) { return false; }
    }
    return true;
}

// A Win32 EDIT control wants CRLF between lines (a bare LF renders as a box on
// older common-control builds). Only '\n' is expanded; existing CRLF stays.
[[nodiscard]] inline std::string toEditText(std::string_view text) {
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) {
            out += "\r\n";
        } else {
            out += text[i];
        }
    }
    return out;
}

} // namespace ok::apptext

#endif // KIEEKEY_APP_DIAG_REPORT_TEXT_HPP
