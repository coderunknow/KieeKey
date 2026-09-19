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
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// File: src/core/HookCounters.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — HookCounters.hpp (v1.3.0-beta3)
// Per-SOURCE event counters for the low-level hook.
//
// THE BUG THIS REPLACES
//   beta2 exposed ONE number in the diagnostics tab, labelled "Sự kiện bàn phím
//   đã xử lý" (keyboard events processed), whose source was
//   `ModernKeyHook::pushed()` — the SPSC ring's push counter. Three different
//   things incremented it:
//
//     1. every keyboard event (KeyDown AND KeyUp, so one physical press = 2),
//     2. every mouse button/wheel event (`countPassThrough` in mouseProc —
//        a click or a scroll notch is an implicit word break),
//     3. every EVENT_SYSTEM_FOREGROUND (alt-tab, clicking another window, a
//        balloon taking focus).
//
//   So the "keyboard" counter climbed while the user only moved and clicked the
//   mouse, and roughly doubled for every key they did press. That is the
//   reported "sự kiện phím đã xử lí tăng dù chưa gõ phím".
//
//   The counter is not wrong because it is incremented — a word-break mouse
//   event and a foreground change ARE processed. It is wrong because ONE number
//   with a keyboard label was fed by three sources. These counters keep the
//   sources separate, so the UI can say exactly what moved:
//
//     keyboardEvents()  == KeyDown + KeyUp + SysKeyDown + SysKeyUp
//                          (physical AND third-party-injected, but NOT our own
//                          SendInput — see keySelfInjected)
//     mouseEvents()     == buttons + wheel (moves are counted separately and
//                          are NEVER part of any "processed" total: they only
//                          refresh the watchdog heartbeat)
//     foregroundEvents()== window activations
//
// PORTABILITY
//   No windows.h: the semantics ("what may be summed into a keyboard total")
//   are unit-tested on every platform (tests/test_hook_counters.cpp) instead of
//   being reviewable only by reading the hook callback.
//
// COST
//   Relaxed atomics, one increment per event on a path that already performs
//   several. `enabled` lets the app turn the whole set off (Level::Off) so a
//   user who wants zero diagnostics overhead gets literally zero increments.
//----------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <cstdint>

namespace ok::hook {

struct HookCounters {
    // --- keyboard ---------------------------------------------------------
    std::atomic<std::uint64_t> keyDown{0};
    std::atomic<std::uint64_t> keyUp{0};
    std::atomic<std::uint64_t> sysKeyDown{0};
    std::atomic<std::uint64_t> sysKeyUp{0};
    std::atomic<std::uint64_t> keySelfInjected{0};    // our own SendInput
    std::atomic<std::uint64_t> keyThirdPartyInjected{0};
    std::atomic<std::uint64_t> keySuppressed{0};      // returned 1 from the hook
    std::atomic<std::uint64_t> keyPassedThrough{0};
    std::atomic<std::uint64_t> keyIgnoredOwnWindow{0};// bypassed: our UI had focus
    std::atomic<std::uint64_t> keyComposedOwnField{0};// …but a text field had focus
    std::atomic<std::uint64_t> keyIgnoredDisabled{0}; // IME off / excluded app
    std::atomic<std::uint64_t> keyAutoRepeat{0};      // same vk still held down

    // --- mouse (never part of a keyboard total) ---------------------------
    std::atomic<std::uint64_t> mouseButton{0};
    std::atomic<std::uint64_t> mouseWheel{0};
    std::atomic<std::uint64_t> mouseMove{0};          // heartbeat only

    // --- window events ----------------------------------------------------
    std::atomic<std::uint64_t> foregroundChanged{0};

    // --- consumer wake-ups -------------------------------------------------
    // Ring pushes/drops stay in ok::lockfree::QueueStats (ModernKeyHook::pushed()
    // / dropped()): ONE owner per number, no mirrored counter that can drift.
    std::atomic<std::uint64_t> consumerWakeups{0};    // the consumer left its park
    std::atomic<std::uint64_t> setEventSyscalls{0};   // SetEvent actually issued

    // Master switch (Level::Off stops every increment below).
    std::atomic<bool> enabled{true};

    void add(std::atomic<std::uint64_t>& counter, std::uint64_t delta = 1) noexcept {
        if (!enabled.load(std::memory_order_relaxed)) { return; }
        counter.fetch_add(delta, std::memory_order_relaxed);
    }

    // The ONLY quantity that may be labelled "sự kiện bàn phím đã xử lý".
    [[nodiscard]] std::uint64_t keyboardEvents() const noexcept {
        return keyDown.load(std::memory_order_relaxed) +
               keyUp.load(std::memory_order_relaxed) +
               sysKeyDown.load(std::memory_order_relaxed) +
               sysKeyUp.load(std::memory_order_relaxed);
    }
    // Keyboard events that reached the engine decision (not filtered out).
    [[nodiscard]] std::uint64_t keyboardComposed() const noexcept {
        return keySuppressed.load(std::memory_order_relaxed) +
               keyPassedThrough.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t mouseEvents() const noexcept {
        return mouseButton.load(std::memory_order_relaxed) +
               mouseWheel.load(std::memory_order_relaxed);
    }
    // Everything the hook callbacks saw, by source (the "why did it move"
    // total: keyboard + mouse buttons/wheel + foreground changes).
    [[nodiscard]] std::uint64_t allSources() const noexcept {
        return keyboardEvents() + mouseEvents() +
               foregroundChanged.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t filteredOut() const noexcept {
        return keySelfInjected.load(std::memory_order_relaxed) +
               keyThirdPartyInjected.load(std::memory_order_relaxed) +
               keyIgnoredOwnWindow.load(std::memory_order_relaxed) +
               keyIgnoredDisabled.load(std::memory_order_relaxed);
    }

    void reset() noexcept {
        keyDown.store(0, std::memory_order_relaxed);
        keyUp.store(0, std::memory_order_relaxed);
        sysKeyDown.store(0, std::memory_order_relaxed);
        sysKeyUp.store(0, std::memory_order_relaxed);
        keySelfInjected.store(0, std::memory_order_relaxed);
        keyThirdPartyInjected.store(0, std::memory_order_relaxed);
        keySuppressed.store(0, std::memory_order_relaxed);
        keyPassedThrough.store(0, std::memory_order_relaxed);
        keyIgnoredOwnWindow.store(0, std::memory_order_relaxed);
        keyComposedOwnField.store(0, std::memory_order_relaxed);
        keyIgnoredDisabled.store(0, std::memory_order_relaxed);
        keyAutoRepeat.store(0, std::memory_order_relaxed);
        mouseButton.store(0, std::memory_order_relaxed);
        mouseWheel.store(0, std::memory_order_relaxed);
        mouseMove.store(0, std::memory_order_relaxed);
        foregroundChanged.store(0, std::memory_order_relaxed);
        consumerWakeups.store(0, std::memory_order_relaxed);
        setEventSyscalls.store(0, std::memory_order_relaxed);
    }
};

} // namespace ok::hook
