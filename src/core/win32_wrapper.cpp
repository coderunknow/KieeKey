//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.0 - refactored and completed logic
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
// File: src/core/win32_wrapper.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v3.3.1 — win32_wrapper.cpp
// Transport-layer implementation: batched synthetic input (InlineEmitter)
// and the assembled pipeline (Win32Wrapper). See win32_wrapper.hpp for the
// architecture and the v3.3 contract this layer preserves.
//
// Layer discipline: NO typing logic may live here. Everything Vietnamese
// (composition, restoration, lexicon arbitration, tone-style conversion) is
// core TextEngine; this file only moves bytes and manages the hook.
//----------------------------------------------------------------------------
#include "win32_wrapper.hpp"

#include <cstring>

namespace ok::wrap {

//===========================================================================
// InlineEmitter — ONE SendInput call per edit (zero heap, stack batches).
//
// Layout of the batch (legacy-compatible order the apps expect):
//   [ Backspace down, Backspace up ] × backspace
//   [ Unicode down, Unicode up ]     × len
// A payload larger than one batch chunks into consecutive calls IN ORDER —
// never reordered, never interleaved. Every INPUT is tagged with the
// self-injection magic so our own LL hook skips it, and stamps the
// watchdog's self-injection tick (GetLastInputInfo counts our injections —
// the stamp tells the watchdog this input was ours).
//===========================================================================
void InlineEmitter::sendEdit(std::size_t backspace, const wchar_t* text,
                             std::size_t len) noexcept {
    // NOTE: intentionally NOT zero-initialized as a whole — every pushed
    // INPUT field is assigned explicitly below, and SendInput only reads the
    // first `c` entries. On the hot path this saves a ~7.8 KB memset per
    // edit (measurable at burst rates).
    INPUT inputs[kMaxInlineInputs];
    std::size_t c = 0;

    const auto flush = [&inputs, &c, this]() {
        if (c != 0) {
            stampInjected();
            ::SendInput(static_cast<UINT>(c), inputs, sizeof(INPUT));
            sendInputCalls_.fetch_add(1, std::memory_order_relaxed);
            inputsInjected_.fetch_add(c, std::memory_order_relaxed);
            c = 0;
        }
    };
    const auto push = [&inputs, &c, &flush](const INPUT& i) {
        inputs[c++] = i;
        if (c == kMaxInlineInputs) { flush(); }
    };

    std::size_t bs = backspace;
    while (bs > 0) {
        const std::size_t batch = bs < (kMaxInlineInputs / 2) ? bs : (kMaxInlineInputs / 2);
        for (std::size_t i = 0; i < batch; ++i) {
            INPUT ki{};
            ki.type = INPUT_KEYBOARD;
            ki.ki.wVk = VK_BACK;
            ki.ki.dwExtraInfo = kSelfInjectedExtraInfo;   // hook skips our own events
            push(ki);
            ki.ki.dwFlags = KEYEVENTF_KEYUP;
            push(ki);
        }
        bs -= batch;
        // Flush each backspace slab before the text so a chunked payload
        // never reorders deletions after insertions.
        if (bs > 0) { flush(); }
    }

    std::size_t off = 0;
    while (off < len) {
        const std::size_t room = kMaxInlineInputs / 2;
        const std::size_t batch = (len - off) < room ? (len - off) : room;
        for (std::size_t i = 0; i < batch; ++i) {
            INPUT key{};
            key.type = INPUT_KEYBOARD;
            key.ki.wVk = 0;
            key.ki.wScan = static_cast<WORD>(text[off + i]);   // UTF-16 unit
            key.ki.dwFlags = KEYEVENTF_UNICODE;
            key.ki.dwExtraInfo = kSelfInjectedExtraInfo;
            push(key);
            key.ki.dwFlags |= KEYEVENTF_KEYUP;
            push(key);
        }
        off += batch;
        if (off < len) { flush(); }
    }

    flush();   // the one SendInput call for the common (non-chunked) case
}

#if OK_WRAP_HAS_WIN32
//===========================================================================
// Win32Wrapper — watchdog wiring (priority policy + start live in the
// header; both are trivial and must stay inlined with the member layout).
//===========================================================================
void Win32Wrapper::enableSelfHealing() noexcept {
    if (watchdog_) { return; }   // idempotent
    watchdogEnv_ = std::make_unique<PumpEnv>(hook_);
    // Bind the watchdog to the hook's heartbeat atomics through the PUBLIC
    // const-reference accessors (kbEventTickRef / mouseEventTickRef) — the
    // members themselves are private to ModernKeyHook (encapsulation).
    watchdog_ = std::make_unique<HookWatchdog>(
        *watchdogEnv_, HookWatchdog::Config{},
        hook_.kbEventTickRef(), hook_.mouseEventTickRef(), selfInjectTickMs_);
    // Arm the pump tick: the watchdog heartbeat runs inside the hook
    // message pump (WM_TIMER @ 5 ms + timeBeginPeriod(1)) — a confirmed
    // silent unhook is repaired in place within 5–15 ms.
    hook_.setPumpTickIntervalMs(5);
    auto* wd = watchdog_.get();
    hook_.setPumpTick([wd, this]() {
        if (wd->check()) {
            rehookCount_.store(wd->rehookCount(), std::memory_order_relaxed);
        }
    });
}
#endif // OK_WRAP_HAS_WIN32

} // namespace ok::wrap
