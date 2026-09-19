//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/test_hook_counters.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// HookCounters suite.
//
// Regression under test (user report, beta2): "sự kiện phím đã xử lí tăng dù
// chưa gõ phím". The number came from ONE counter that three event sources
// incremented. These tests pin the semantics that make the report impossible:
//
//   * a mouse click, a wheel notch and a foreground change MUST NOT move
//     keyboardEvents(),
//   * a mouse move must not move ANY total (it only feeds the watchdog),
//   * one physical press is two events (down + up) and the UI must be able to
//     show both readings without them contradicting each other,
//   * our own SendInput is counted separately from real keys,
//   * enabled=false makes every counter a true no-op (Level::Off overhead),
//   * concurrent producers cannot lose an increment.
//----------------------------------------------------------------------------
#include "HookCounters.hpp"

#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

using ok::hook::HookCounters;

namespace {

void testKeyboardTotalIsKeyboardOnly() {
    HookCounters c;
    assert(c.keyboardEvents() == 0);

    // Non-keyboard activity that beta2 folded into the "keyboard" number.
    c.add(c.mouseButton, 5);
    c.add(c.mouseWheel, 2);
    c.add(c.foregroundChanged, 3);
    assert(c.keyboardEvents() == 0);          // REGRESSION GUARD
    assert(c.mouseEvents() == 7);
    assert(c.allSources() == 10);

    // A mouse MOVE is heartbeat-only: no total may move.
    c.add(c.mouseMove, 1000);
    assert(c.mouseEvents() == 7);
    assert(c.allSources() == 10);
    assert(c.keyboardEvents() == 0);

    // One physical press = down + up.
    c.add(c.keyDown);
    assert(c.keyboardEvents() == 1);
    c.add(c.keyUp);
    assert(c.keyboardEvents() == 2);
    // Alt-chords are keyboard events too (WM_SYSKEYDOWN/UP).
    c.add(c.sysKeyDown);
    c.add(c.sysKeyUp);
    assert(c.keyboardEvents() == 4);

    // Our own injected characters are NOT user keystrokes.
    c.add(c.keySelfInjected, 6);
    assert(c.keyboardEvents() == 4);
    assert(c.filteredOut() == 6);
    c.add(c.keyThirdPartyInjected, 1);
    assert(c.keyboardEvents() == 4);
    assert(c.filteredOut() == 7);

    // Suppressed vs delivered partition the composed keys.
    c.add(c.keySuppressed, 1);
    c.add(c.keyPassedThrough, 3);
    assert(c.keyboardComposed() == 4);
    std::cout << "  [PASS] keyboard total counts keyboard events only\n";
}

void testResetAndMasterSwitch() {
    HookCounters c;
    c.add(c.keyDown, 10);
    c.add(c.mouseButton, 4);
    c.add(c.foregroundChanged, 2);
    assert(c.keyboardEvents() == 10 && c.allSources() == 16);
    c.reset();
    assert(c.keyboardEvents() == 0 && c.allSources() == 0 && c.mouseEvents() == 0);
    assert(c.consumerWakeups.load() == 0 && c.setEventSyscalls.load() == 0);

    // Level::Off == zero increments (the "diagnostics must be free" contract).
    c.enabled.store(false, std::memory_order_relaxed);
    c.add(c.keyDown, 100);
    c.add(c.mouseButton, 100);
    assert(c.keyboardEvents() == 0 && c.mouseEvents() == 0);
    c.enabled.store(true, std::memory_order_relaxed);
    c.add(c.keyDown, 1);
    assert(c.keyboardEvents() == 1);
    std::cout << "  [PASS] reset + master switch (Off is a true no-op)\n";
}

void testConcurrentProducers() {
    // The LL callbacks run on the pump thread while the UI timer reads and the
    // consumer increments its own ids: nothing may be lost.
    HookCounters c;
    constexpr int kThreads = 4;
    constexpr int kPerThread = 50000;
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&c, t] {
            for (int i = 0; i < kPerThread; ++i) {
                if (t % 2 == 0) { c.add(c.keyDown); c.add(c.keyUp); }
                else            { c.add(c.mouseButton); c.add(c.foregroundChanged); }
            }
        });
    }
    for (auto& worker : workers) { worker.join(); }
    const std::uint64_t expectedPairs =
        static_cast<std::uint64_t>(kThreads / 2) * kPerThread;
    assert(c.keyDown.load() == expectedPairs);
    assert(c.keyUp.load() == expectedPairs);
    assert(c.keyboardEvents() == 2 * expectedPairs);
    assert(c.mouseButton.load() == expectedPairs);
    assert(c.foregroundChanged.load() == expectedPairs);
    std::cout << "  [PASS] 4 concurrent producers x 50k events: nothing lost\n";
}

void testBypassAccounting() {
    // The "cannot type Vietnamese in our own dialog" fix is observable from
    // these two counters: a bypassed key and a composed-in-our-own-field key
    // are DIFFERENT outcomes and must not be summed into one bucket.
    HookCounters c;
    c.add(c.keyIgnoredOwnWindow, 3);      // game window had focus
    c.add(c.keyComposedOwnField, 2);      // macro editor had focus -> IME ran
    assert(c.keyIgnoredOwnWindow.load() == 3);
    assert(c.keyComposedOwnField.load() == 2);
    assert(c.keyboardComposed() == 0);    // neither was delivered to an app yet
    c.add(c.keySuppressed, 2);            // the macro editor's two composed keys
    assert(c.keyboardComposed() == 2);
    std::cout << "  [PASS] own-window bypass vs own-field composition are distinct\n";
}

} // namespace

int main() {
    std::cout << "=== Running HookCounters Suite ===\n";
    testKeyboardTotalIsKeyboardOnly();
    testResetAndMasterSwitch();
    testConcurrentProducers();
    testBypassAccounting();
    std::cout << "=== ALL HOOK COUNTER TESTS PASSED ===\n";
    return 0;
}
