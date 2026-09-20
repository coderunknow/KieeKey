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
// File: tests/test_process_monitor.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// v1.3.0-beta5 (bug B4) — portable tests for the ProcessMonitor models.
//
// The tester's diagnostics tab showed "Ứng dụng hiện tại: unknown" and a wall
// of zeros. The Win32 query chain itself cannot run on Linux, so its DECISION
// models live in the pure header src/core/ProcessNameUtil.hpp and are pinned
// here: exe-name extraction (including native-format paths), UWP AUMID →
// friendly name, the honest "pid N (lỗi X)" fallback label, and the
// elevation-probe decision table (the "cannot query → treat elevated"
// semantics that must NEVER be conflated with name resolution again).
//----------------------------------------------------------------------------
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>

#include "ProcessNameUtil.hpp"

using namespace ok::monitor;

namespace {

void testExeNameFromPath() {
    // Win32 format.
    assert(exeNameFromPath(LR"(C:\Program Files\App\Code.exe)") == L"Code.exe");
    assert(exeNameFromPath(LR"(C:\Windows\explorer.exe)") == L"explorer.exe");
    // Native format (PROCESS_NAME_NATIVE fallback of the Win32 chain).
    assert(exeNameFromPath(LR"(\Device\HarddiskVolume3\Windows\notepad.exe)")
           == L"notepad.exe");
    // Forward slashes (defensive; never emitted by Windows but cheap).
    assert(exeNameFromPath(L"C:/Dir/app.exe") == L"app.exe");
    // No separator at all → the whole string.
    assert(exeNameFromPath(L"svchost.exe") == L"svchost.exe");
    // Trailing separator → fall back to the whole string, never empty.
    assert(exeNameFromPath(LR"(C:\Dir\)") == LR"(C:\Dir\)");
    assert(exeNameFromPath(L"").empty());
    // Unicode names survive untouched.
    assert(exeNameFromPath(LR"(D:\Ứng dụng\Bộ Gõ.exe)") == L"Bộ Gõ.exe");
    std::cout << "  [PASS] exeNameFromPath: win32 + native formats, unicode,"
                 " trailing-separator fallback\n";
}

void testFriendlyNameFromAumid() {
    // Canonical store-app AUMID: <Name>_<13-char hash>!<AppId>.
    assert(friendlyNameFromAumid(
               L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App")
           == L"Microsoft.WindowsCalculator");
    assert(friendlyNameFromAumid(
               L"Microsoft.WindowsTerminal_8wekyb3d8bbwe!App")
           == L"Microsoft.WindowsTerminal");
    // Third-party package with a longer name and an AppId containing dots.
    assert(friendlyNameFromAumid(
               L"48683LeoNguyen.VietKey_9x2kh2p3s4t5v!VietKey.App")
           == L"48683LeoNguyen.VietKey");
    // No '!': treat the whole string as the package family name.
    assert(friendlyNameFromAumid(L"Some.Package_8wekyb3d8bbwe") == L"Some.Package");
    // Hash suffix that is NOT 13 chars: keep it (do not guess).
    assert(friendlyNameFromAumid(L"Pkg.Short!App") == L"Pkg.Short");
    // No hash suffix at all.
    assert(friendlyNameFromAumid(L"ClassicApp!Main") == L"ClassicApp");
    // Degenerate inputs never yield an empty label.
    assert(friendlyNameFromAumid(L"!App") == L"!App");
    assert(friendlyNameFromAumid(L"").empty());
    std::cout << "  [PASS] friendlyNameFromAumid: strips !AppId + 13-char"
                 " package hash, never empty for non-empty input\n";
}

void testUnknownLabel() {
    // The honest fallback: PID + the Win32 error, never the bare "unknown".
    const std::wstring a = unknownLabelW(4242, 5);
    assert(a == L"pid 4242 (lỗi 5)");
    const std::wstring b = unknownLabelW(88, 299);
    assert(b == L"pid 88 (lỗi 299)");
    // Regression guard: the label must not BE "unknown" (the beta4 display).
    assert(a != L"unknown" && b != L"unknown");
    std::cout << "  [PASS] unknownLabelW: honest 'pid N (lỗi X)' — never bare"
                 " 'unknown'\n";
}

void testElevationProbeDecisionTable() {
    // Happy path: the token says it all.
    assert(treatAsElevated(true, true, true, true)  == true);
    assert(treatAsElevated(true, true, true, false) == false);
    // OpenProcess refused (elevated target or protected process): UIPI blocks
    // BOTH output paths — treat as elevated, pass keystrokes through.
    assert(treatAsElevated(false, false, false, false) == true);
    // Token open refused with the process handle open: same integrity-barrier
    // reasoning.
    assert(treatAsElevated(true, false, false, false) == true);
    // Token open SUCCEEDED but the information read failed: no integrity
    // barrier exists (we opened an elevated-inaccessible token? we did not) —
    // a read hiccup must NOT disable the IME.
    assert(treatAsElevated(true, true, false, false) == false);
    assert(treatAsElevated(true, true, false, true)  == false);
    std::cout << "  [PASS] elevation probe decision table: ACCESS_DENIED means"
                 " elevated; a token-READ failure does not\n";
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout << "=== Running ProcessMonitor Model Suite (bug B4) ===\n";
    testExeNameFromPath();
    testFriendlyNameFromAumid();
    testUnknownLabel();
    testElevationProbeDecisionTable();
    std::cout << "=== ALL PROCESS MONITOR TESTS PASSED ===\n";
    return 0;
}
