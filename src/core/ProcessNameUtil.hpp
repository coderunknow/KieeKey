//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
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
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// File: src/core/ProcessNameUtil.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — ProcessNameUtil.hpp (v1.3.0-beta5, bug B4)
// PURE models behind ProcessMonitor's two reported failures:
//
//   1. "Ứng dụng hiện tại: unknown" — beta4 resolved the foreground process
//      name through ONE query (OpenProcess + QueryFullProcessImageNameW +
//      GetProcessTimes, all-or-nothing: a GetProcessTimes failure discarded
//      an already-successful path query) and displayed the literal string
//      "unknown" on ANY failure — no fallback, no reason, no PID. The Win32
//      fallback chain now lives in ProcessMonitor.cpp; the pure pieces that
//      decide WHAT TO DISPLAY are here and unit-tested on every platform
//      (tests/test_process_monitor.cpp): exe-name extraction, UWP AUMID →
//      friendly name, and the honest "pid N (lỗi X)" label of last resort.
//
//   2. The elevation probe conflated "cannot query" with "is elevated" for
//      the NAME query too. The probe DECISION itself is correct and stays
//      (an OpenProcess/OpenProcessToken ACCESS_DENIED really does mean the
//      target outranks us — UIPI blocks both output paths, pass-through is
//      the only safe behavior), but it is now separated from name
//      resolution and pinned as a pure decision table here, so the two
//      concerns can never silently merge again: a failed NAME query must
//      never exclude an app, and a failed TOKEN query must never show a
//      wrong name.
//
// No windows.h in this header — that is the point.
//----------------------------------------------------------------------------
#pragma once

#include <string>
#include <string_view>

namespace ok::monitor {

// Last path segment ("C:\Dir\App.exe" → "App.exe"; also handles '/' and a
// trailing separator, which QueryFullProcessImageNameW never emits but a
// native-format path can: "\Device\HarddiskVolume3\Dir\App.exe").
[[nodiscard]] inline std::wstring_view exeNameFromPath(std::wstring_view path) noexcept {
    const std::size_t pos = path.find_last_of(L"\\/");
    std::wstring_view name = (pos == std::wstring_view::npos)
                                 ? path
                                 : path.substr(pos + 1);
    // A trailing separator (e.g. "C:\Dir\") has an empty last segment —
    // fall back to the whole string rather than displaying nothing.
    if (name.empty() && !path.empty()) { return path; }
    return name;
}

// UWP Application User Model ID → friendly display name.
// "Microsoft.WindowsCalculator_8wekyb3d8bbwe!App" → "Microsoft.WindowsCalculator".
// Structure: <PackageFamilyName>!<AppId>, PackageFamilyName =
// <PackageFullName-name>_<13-char base32 hash>. The hash is noise for a
// human; strip "!AppId" and the trailing "_hash" when it is recognizable.
[[nodiscard]] inline std::wstring_view friendlyNameFromAumid(std::wstring_view aumid) noexcept {
    std::wstring_view name = aumid;
    const std::size_t bang = name.find(L'!');
    if (bang != std::wstring_view::npos) { name = name.substr(0, bang); }
    // Strip the package-family hash suffix: "_<exactly 13 chars, no sep>".
    if (name.size() > 14) {
        const std::size_t us = name.rfind(L'_');
        if (us != std::wstring_view::npos && name.size() - us - 1 == 13 &&
            name.find_first_of(L"!\\/", us) == std::wstring_view::npos) {
            name = name.substr(0, us);
        }
    }
    return name.empty() ? aumid : name;
}

// The honest label of last resort: never the bare word "unknown" — the PID
// still identifies the process for a bug report, and the Win32 error says
// WHY the name could not be read (5 = ACCESS_DENIED: the target outranks us
// or is protected; 87/299 = protected-process query refusal; …).
[[nodiscard]] inline std::wstring unknownLabelW(unsigned long pid,
                                                unsigned long err) noexcept(false) {
    return L"pid " + std::to_wstring(pid) + L" (lỗi " + std::to_wstring(err) + L")";
}

//---------------------------------------------------------------------------
// Elevation-probe DECISION TABLE (pure). Inputs are the outcomes of the three
// Win32 steps; the output is what ProcessMonitor publishes as `elevated`.
//
// Contract (bug B4): "cannot query" is treated as elevated ONLY where UIPI
// semantics justify it —
//   * OpenProcess fails            → the target outranks us / is protected →
//                                    BOTH output paths are blocked → treat as
//                                    elevated (pass-through, never swallow).
//   * OpenProcessToken fails       → same reasoning (integrity barrier).
//   * GetTokenInformation fails    → we DID open the process and its token;
//                                    nothing indicates an integrity barrier,
//                                    so do NOT disable the IME on a read
//                                    hiccup (beta4 already returned false
//                                    here; the table pins it against drift).
// A failed name query is NOT an input to this table — name resolution and
// exclusion are separate concerns and must stay that way.
//---------------------------------------------------------------------------
[[nodiscard]] constexpr bool treatAsElevated(bool openProcessOk,
                                             bool openTokenOk,
                                             bool getTokenInfoOk,
                                             bool elevationFlag) noexcept {
    if (!openProcessOk)  { return true; }
    if (!openTokenOk)    { return true; }
    if (!getTokenInfoOk) { return false; }
    return elevationFlag;
}

} // namespace ok::monitor
