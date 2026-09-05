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
// File: tests/win32_shim.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v3.3.1 — tests/win32_shim.hpp
// Minimal Win32 surface shim for NATIVE (non-Windows) unit tests of the
// wrapper's platform-independent logic: InlineEmitter batching, the
// HookWatchdog state machine, and the Vyukov OutputRing.
//
// NOT compiled in production (guarded by OK_WRAP_NO_WIN32). The shim is
// deliberately faithful to the Win32 semantics the wrapper relies on:
//   * SendInput records every INPUT so tests assert exact batching (one
//     call per edit, backspaces before text, KEYEVENTF_UNICODE pairs,
//     self-tagged dwExtraInfo, chunked overflow).
//   * A controllable fake clock + fake last-input tick drive the watchdog.
//----------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

namespace okshim {

// ---- constants (mirror the real ones the wrapper uses) -------------------
using DWORD      = std::uint32_t;
using UINT       = std::uint32_t;
using UINT_PTR   = std::uintptr_t;
using WORD       = std::uint16_t;
using ULONG_PTR  = std::uintptr_t;

constexpr UINT INPUT_KEYBOARD          = 1;
constexpr DWORD KEYEVENTF_EXTENDEDKEY  = 0x0001;
constexpr DWORD KEYEVENTF_KEYUP        = 0x0002;
constexpr DWORD KEYEVENTF_UNICODE      = 0x0004;
constexpr WORD  VK_BACK                = 0x08;

struct KEYBDINPUT {
    WORD wVk;
    WORD wScan;
    DWORD dwFlags;
    DWORD time;
    ULONG_PTR dwExtraInfo;
};
struct INPUT {
    DWORD type;
    union { KEYBDINPUT ki; };   // anonymous union, like windows.h
};

struct LASTINPUTINFO {
    UINT cbSize;
    DWORD dwTime;
};

// ---- controllable test state ---------------------------------------------
struct State {
    // fake clock
    std::uint32_t tickMs = 1000;
    // fake system-wide last input tick (GetLastInputInfo)
    std::uint32_t lastInputTickMs = 1000;
    // recorded SendInput batches: one entry per CALL, each a list of INPUTs
    std::vector<std::vector<INPUT>> calls;
    // failure injection
    bool failSendInput = false;
    // rehook counter for the watchdog Env
    std::uint64_t rehooks = 0;
    bool failRehook = false;
};
inline State g_state;

inline void resetState() { g_state = State{}; }

// ---- API shims ------------------------------------------------------------
inline DWORD GetTickCount() noexcept { return g_state.tickMs; }
using BOOL_ = int;

inline BOOL_ GetLastInputInfo(LASTINPUTINFO* li) noexcept {
    li->dwTime = g_state.lastInputTickMs;
    return 1;
}

inline UINT SendInput(UINT n, const INPUT* inputs, int /*size*/) noexcept {
    if (g_state.failSendInput) { return 0; }
    g_state.calls.emplace_back();
    g_state.calls.back().assign(inputs, inputs + n);
    return n;
}

inline bool advanceTick(std::uint32_t deltaMs) noexcept {
    g_state.tickMs += deltaMs;
    return true;
}

} // namespace okshim

//----------------------------------------------------------------------------
// Global hoist — mirrors how <windows.h> declares these at global scope, so
// wrapper code compiles UNCHANGED under both the real and shim paths.
//----------------------------------------------------------------------------
using okshim::DWORD;
using okshim::UINT;
using okshim::UINT_PTR;
using okshim::WORD;
using okshim::ULONG_PTR;
using okshim::KEYBDINPUT;
using okshim::INPUT;
using okshim::LASTINPUTINFO;
using okshim::INPUT_KEYBOARD;
using okshim::KEYEVENTF_EXTENDEDKEY;
using okshim::KEYEVENTF_KEYUP;
using okshim::KEYEVENTF_UNICODE;
using okshim::VK_BACK;
using okshim::GetTickCount;
using okshim::GetLastInputInfo;
using okshim::SendInput;
using okshim::g_state;
