//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.0.1 - refactored and completed logic
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
// File: tests/test_win32wrapper.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v3.3.1 — tests/test_win32wrapper.cpp
// Native verification of the Win32 wrapper's platform-independent logic:
//   1. InlineEmitter — batched SendInput semantics (one call per edit,
//      backspaces-before-text order, KEYEVENTF_UNICODE pairs, self-tagging,
//      chunked overflow for oversized payloads).
//   2. HookWatchdog — silent-unhook detection + self-healing state machine
//      (debounce, self-injection immunity, mouse-only immunity, rehook
//      failure retry, tick-wrap safety).
//   3. OutputRing (Vyukov SPSC, 1024) — FIFO integrity at capacity.
// Build: g++ -std=c++20 -I src/core -I tests -DOK_WRAP_NO_WIN32
//            tests/test_win32wrapper.cpp src/core/win32_wrapper.cpp
//----------------------------------------------------------------------------
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <cstdio>
#include <string>
#include <vector>

#include "win32_wrapper.hpp"

using namespace ok::wrap;
namespace sh = okshim;

static int g_failed = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        /* Plain `if`: `if constexpr` requires a constant expression and   \
           is a hard error for the (many) runtime CHECKs in this file —   \
           MSVC /permissive- and GCC both reject it. The constant-value   \
           CHECKs (e.g. ring.capacity == 1024) are intentional regression \
           nets; the C4127 warning they trigger on MSVC is disabled       \
           project-wide (/wd4127), so a plain `if` is correct everywhere. */ \
        if (!(cond)) {                                                     \
            ++g_failed;                                                    \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                  \
    } while (0)

//===========================================================================
// 1. InlineEmitter
//===========================================================================
static void testEmitterBasicEdit() {
    std::printf("== emitter: basic edit = one SendInput, ordered ==\n");
    sh::resetState();
    InlineEmitter em;
    // backspace 1 (down+up) + "á" U+00E1 (down+up) = 4 inputs, ONE call
    em.sendEdit(1, std::wstring(1, static_cast<wchar_t>(0x00E1)));
    CHECK(sh::g_state.calls.size() == 1);
    const auto& batch = sh::g_state.calls[0];
    CHECK(batch.size() == 4);
    CHECK(batch[0].type == sh::INPUT_KEYBOARD && batch[0].ki.wVk == sh::VK_BACK &&
          batch[0].ki.dwFlags == 0);
    CHECK(batch[1].ki.dwFlags == sh::KEYEVENTF_KEYUP);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(batch[i].ki.dwExtraInfo == kSelfInjectedExtraInfo);
    }
    CHECK(batch[2].ki.wVk == 0 && batch[2].ki.wScan == 0x00E1 &&
          batch[2].ki.dwFlags == sh::KEYEVENTF_UNICODE);
    CHECK(batch[3].ki.dwFlags == (sh::KEYEVENTF_UNICODE | sh::KEYEVENTF_KEYUP));
    CHECK(em.sendInputCalls() == 1);
    CHECK(em.inputsInjected() == 4);
    // zero heap: the emitter's hot path never allocates (checked by design —
    // all buffers are stack arrays; this is a structural guarantee).
}

static void testEmitterBackspaceOnly() {
    std::printf("== emitter: backspace-only edit ==\n");
    sh::resetState();
    InlineEmitter em;
    em.sendEdit(3, L"");
    CHECK(sh::g_state.calls.size() == 1);
    CHECK(sh::g_state.calls[0].size() == 6);   // 3 down/up pairs
}

static void testEmitterChunkedOverflow() {
    std::printf("== emitter: oversized payload chunks in order ==\n");
    sh::resetState();
    InlineEmitter em;
    // 200 chars → 200 unicode pairs + 2 BS pairs = 404 inputs > kMaxInlineInputs(196)
    std::wstring big(200, static_cast<wchar_t>(0x1EA5));   // ấ
    em.sendEdit(2, big);
    CHECK(sh::g_state.calls.size() >= 2);
    // reconstruct the global order across calls; assert BS-first and pairing
    std::vector<sh::INPUT> flat;
    for (const auto& c : sh::g_state.calls) {
        flat.insert(flat.end(), c.begin(), c.end());
    }
    CHECK(flat.size() == 2 * 2 + 2 * 200);
    std::size_t idx = 0;
    for (std::size_t i = 0; i < 2; ++i) {
        CHECK(flat[idx].ki.wVk == sh::VK_BACK && flat[idx].ki.dwFlags == 0); ++idx;
        CHECK(flat[idx].ki.dwFlags == sh::KEYEVENTF_KEYUP); ++idx;
    }
    for (std::size_t i = 0; i < 200; ++i) {
        CHECK(flat[idx].ki.wScan == 0x1EA5 &&
              flat[idx].ki.dwFlags == sh::KEYEVENTF_UNICODE); ++idx;
        CHECK(flat[idx].ki.dwFlags == (sh::KEYEVENTF_UNICODE | sh::KEYEVENTF_KEYUP)); ++idx;
    }
    CHECK(em.inputsInjected() == flat.size());
}

//===========================================================================
// 2. HookWatchdog
//===========================================================================
namespace {
struct FakeEnv final : HookWatchdog::Env {
    std::uint32_t nowMs = 10'000;
    std::uint32_t lastInputMs = 10'000;
    std::uint64_t rehookCalls = 0;
    std::uint32_t tickNow() const noexcept override { return nowMs; }
    std::uint32_t systemLastInputTick() const noexcept override { return lastInputMs; }
    bool reinstallHooks() noexcept override {
        ++rehookCalls;
        return !okshim::g_state.failRehook;
    }
};
} // namespace

static void testWatchdogHealthy() {
    std::printf("== watchdog: healthy typing → no rehook ==\n");
    sh::resetState();
    FakeEnv env;
    std::atomic<std::uint32_t> kb{0}, mouse{0}, self{0};
    HookWatchdog wd(env, HookWatchdog::Config{}, kb, mouse, self);
    // keyboard events seen at the same tick the system last saw input
    for (int i = 0; i < 50; ++i) {
        env.nowMs += 5;
        env.lastInputMs = env.nowMs;
        kb.store(env.nowMs, std::memory_order_relaxed);
        CHECK(!wd.check());
    }
    CHECK(wd.rehookCount() == 0);
}

static void testWatchdogSilentUnhookRecovers() {
    std::printf("== watchdog: silent unhook → debounced rehook (5–15 ms) ==\n");
    sh::resetState();
    FakeEnv env;
    std::atomic<std::uint32_t> kb{0}, mouse{0}, self{0};
    kb.store(env.nowMs, std::memory_order_relaxed);
    HookWatchdog wd(env, HookWatchdog::Config{5, 60, 2}, kb, mouse, self);
    // System keeps seeing input; the dead hook stops delivering.
    const std::uint32_t unhookAt = env.nowMs;
    for (int i = 0; i < 6; ++i) {
        env.nowMs += 5;
        env.lastInputMs = env.nowMs;      // user keeps typing, hook sees nothing
        // kb stays stale
    }
    // skew = 30 ms < 60 ms threshold — not yet a miss
    CHECK(!wd.check());
    for (int i = 0; i < 7; ++i) { env.nowMs += 5; env.lastInputMs = env.nowMs; }
    // skew = 65 ms → miss #1
    CHECK(!wd.check());
    env.nowMs += 5; env.lastInputMs = env.nowMs;
    // miss #2 → rehook fires; elapsed since unhook ≈ 70 ms of typing, but the
    // re-establishment itself (detection→rehook) spans 2 ticks = ~10 ms ✓
    CHECK(wd.check());
    CHECK(wd.rehookCount() == 1);
    CHECK(env.rehookCalls == 1);
    CHECK(unhookAt <= env.nowMs);
    // after recovery, healthy again
    kb.store(env.nowMs, std::memory_order_relaxed);
    CHECK(!wd.check());
}

static void testWatchdogSelfInjectionImmunity() {
    std::printf("== watchdog: own SendInput explains system input ==\n");
    sh::resetState();
    FakeEnv env;
    std::atomic<std::uint32_t> kb{0}, mouse{0}, self{0};
    kb.store(env.nowMs, std::memory_order_relaxed);
    HookWatchdog wd(env, HookWatchdog::Config{}, kb, mouse, self);
    // our injected edits keep landing; the (filtered) hook stamps happen via
    // the emitter → self tick fresh; system input is fully accounted for.
    for (int i = 0; i < 100; ++i) {
        env.nowMs += 5;
        env.lastInputMs = env.nowMs;
        self.store(env.nowMs, std::memory_order_relaxed);   // emitter stamp
        CHECK(!wd.check());
    }
    CHECK(wd.rehookCount() == 0);
}

static void testWatchdogMouseOnlyImmunity() {
    std::printf("== watchdog: mouse-only activity is not an unhook ==\n");
    sh::resetState();
    FakeEnv env;
    std::atomic<std::uint32_t> kb{0}, mouse{0}, self{0};
    kb.store(env.nowMs, std::memory_order_relaxed);
    HookWatchdog wd(env, HookWatchdog::Config{}, kb, mouse, self);
    for (int i = 0; i < 100; ++i) {
        env.nowMs += 5;
        env.lastInputMs = env.nowMs;
        mouse.store(env.nowMs, std::memory_order_relaxed);   // moves keep it fresh
        CHECK(!wd.check());
    }
    CHECK(wd.rehookCount() == 0);
}

static void testWatchdogRehookFailureRetries() {
    std::printf("== watchdog: failed rehook retries on the next tick ==\n");
    sh::resetState();
    FakeEnv env;
    std::atomic<std::uint32_t> kb{0}, mouse{0}, self{0};
    kb.store(env.nowMs, std::memory_order_relaxed);
    HookWatchdog wd(env, HookWatchdog::Config{5, 60, 2}, kb, mouse, self);
    okshim::g_state.failRehook = true;   // SetWindowsHookEx keeps failing
    for (int i = 0; i < 20; ++i) {
        env.nowMs += 5;
        env.lastInputMs = env.nowMs;
        wd.check();   // keep missing; rehooks fail
    }
    CHECK(wd.rehookCount() == 0);        // no SUCCESS recorded
    CHECK(env.rehookCalls >= 2);         // but recovery retried
    okshim::g_state.failRehook = false;
    // after a failed recovery the debounce restarts: two further misses
    // before the successful rehook (keeps a broken environment from
    // hammering SetWindowsHookEx every 5 ms).
    env.nowMs += 5; env.lastInputMs = env.nowMs;
    CHECK(!wd.check());
    env.nowMs += 5; env.lastInputMs = env.nowMs;
    CHECK(wd.check());                   // eventually succeeds
    CHECK(wd.rehookCount() == 1);
}

static void testWatchdogTickWrap() {
    std::printf("== watchdog: GetTickCount wrap (49.7 days) safety ==\n");
    sh::resetState();
    FakeEnv env;
    std::atomic<std::uint32_t> kb{0}, mouse{0}, self{0};
    env.nowMs = 0xFFFF'F000u;            // near wrap
    kb.store(env.nowMs, std::memory_order_relaxed);
    HookWatchdog wd(env, HookWatchdog::Config{}, kb, mouse, self);
    for (int i = 0; i < 40; ++i) {
        env.nowMs += 5;                  // crosses 0
        env.lastInputMs = env.nowMs;
        kb.store(env.nowMs, std::memory_order_relaxed);
        CHECK(!wd.check());
    }
    CHECK(wd.rehookCount() == 0);
}

//===========================================================================
// 3. OutputRing (Vyukov SPSC, 1024)
//===========================================================================
static void testOutputRing() {
    std::printf("== OutputRing 1024: FIFO integrity + capacity ==\n");
    OutputRing ring;
    CHECK(ring.capacity == 1024);
    // fill to capacity
    for (std::uint32_t i = 0; i < 1024; ++i) {
        OutputItem it;
        it.kind = OutputItem::Kind::Edit;
        it.backspace = i;
        CHECK(ring.try_push(it));
    }
    OutputItem full{};
    full.backspace = 0xFFFFFFFF;
    CHECK(!ring.try_push(full));          // bounded
    for (std::uint32_t i = 0; i < 1024; ++i) {
        OutputItem it;
        CHECK(ring.try_pop(it));
        CHECK(it.backspace == i);         // strict FIFO
    }
    OutputItem none{};
    CHECK(!ring.try_pop(none));           // empty
}

//===========================================================================
// 4. EditDrainBarrier (v3.4, S1) — hybrid ordering-barrier semantics
//===========================================================================
static void testBarrierImmediatePass() {
    std::printf("== barrier: pending==0 exits without waiting ==\n");
    EditDrainBarrier b;
    std::atomic<std::uint32_t> pending{0};
    const bool ok = b.waitDrained(pending);
    CHECK(ok);
    CHECK(pending.load() == 0);
}

static void testBarrierSpinCatch() {
    std::printf("== barrier: drain landing inside the spin window ==\n");
    EditDrainBarrier b;
    std::atomic<std::uint32_t> pending{1};
    // Deterministic spin-window catch: the drain lands at an EXACT spin
    // iteration via the injected hook — no second thread, zero dependence
    // on OS scheduling. (The previous version slept 50 µs on a helper
    // thread; on a loaded CI VM that wake-up can be scheduled after the
    // barrier's whole 1 ms budget, so the waiter legitimately timed out
    // and CHECK(ok) failed — a scheduler artifact, not a barrier defect.)
    int hookIters = 0;
    const bool ok = b.waitDrainedHooked(
        pending, EditDrainBarrier::kSpinIters,
        [&] {
            if (++hookIters == 120) {          // inside the 200-iter window
                pending.store(0, std::memory_order_release);
                b.notifyDrained();
            }
        },
        EditDrainBarrier::kWaitBudgetMs);
    CHECK(ok);                            // caught by the spin loop
    CHECK(hookIters == 120);              // exited at the drain iteration
    CHECK(pending.load() == 0);
    CHECK(b.timeouts() == 0);             // no timeout was consumed
}

static void testBarrierNotifyBeforeWait() {
    std::printf("== barrier: notify racing the wait is not lost ==\n");
    EditDrainBarrier b;
    std::atomic<std::uint32_t> pending{1};
    // Lost-wakeup race, made deterministic: the consumer drains only after
    // the waiter has provably left the spin window (handshake via the
    // injected hook — not a sleep), so the notify lands exactly as the
    // waiter enters the event wait. BOTH interleavings — the stateful
    // event carrying the signal from BEFORE the wait, or the notify
    // waking the wait in flight — must end in ok == true. The budget is
    // generous (500 ms): this test verifies the WAKE-UP MECHANISM, not
    // the stall cap (pinned separately by testBarrierBudgetBounded). The
    // old version slept 500 µs on the consumer and raced the 1 ms
    // deadline — on a CI VM the consumer's first wake can land after the
    // whole budget, so the waiter timed out and the check failed.
    std::atomic<bool> waiterLeftSpin{false};
    std::thread consumer([&] {
        while (!waiterLeftSpin.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        pending.store(0, std::memory_order_release);
        b.notifyDrained();
    });
    const bool ok = b.waitDrainedHooked(
        pending, 2,                       // 2 pause iters → event wait
        [&] { waiterLeftSpin.store(true, std::memory_order_release); },
        500);
    consumer.join();
    CHECK(ok);
    CHECK(b.timeouts() == 0);             // the wake-up was never lost
}

static void testBarrierBudgetBounded() {
    std::printf("== barrier: pathological stall bounded by the 1 ms budget ==\n");
    EditDrainBarrier b;
    std::atomic<std::uint32_t> pending{1};   // consumer NEVER drains
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = b.waitDrained(pending);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();
    CHECK(!ok);                              // budget exhausted → deliver anyway
    // ~1 ms budget + generous scheduler slack: on a CI VM the test thread
    // itself can be descheduled briefly around the deadline; 250 ms still
    // catches any real regression (missing/broken deadline = hang, which
    // the ctest TIMEOUT property turns into a hard failure).
    CHECK(ms >= 0 && ms <= 250);
    CHECK(b.timeouts() == 1);                // counted for diagnostics
    pending.store(0);
}

static void testBarrierManyWaitersOneNotify() {
    std::printf("== barrier: repeated cycles (spin→wait→notify) stay consistent ==\n");
    EditDrainBarrier b;
    std::atomic<std::uint32_t> pending{0};
    // Handshake instead of a free-running consumer: the consumer drains
    // only after the waiter has left the spin window, so no cycle can
    // time out merely because the CI VM scheduled the consumer thread
    // late (the free-running variant passed its quiet run but carries the
    // same latent scheduler-race flake the sleep-based tests hit).
    for (int i = 0; i < 200; ++i) {
        pending.store(1, std::memory_order_release);
        std::atomic<bool> waiterLeftSpin{false};
        std::thread consumer([&] {
            while (!waiterLeftSpin.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            pending.store(0, std::memory_order_release);
            b.notifyDrained();
        });
        const bool ok = b.waitDrainedHooked(
            pending, 2,
            [&] { waiterLeftSpin.store(true, std::memory_order_release); },
            500);
        consumer.join();
        CHECK(ok);
    }
}

int main() {
    testEmitterBasicEdit();
    testEmitterBackspaceOnly();
    testEmitterChunkedOverflow();
    testWatchdogHealthy();
    testWatchdogSilentUnhookRecovers();
    testWatchdogSelfInjectionImmunity();
    testWatchdogMouseOnlyImmunity();
    testWatchdogRehookFailureRetries();
    testWatchdogTickWrap();
    testOutputRing();
    testBarrierImmediatePass();
    testBarrierSpinCatch();
    testBarrierNotifyBeforeWait();
    testBarrierBudgetBounded();
    testBarrierManyWaitersOneNotify();
    if (g_failed == 0) {
        std::printf("\nALL WIN32-WRAPPER TESTS PASSED\n");
        return 0;
    }
    std::printf("\n%d CHECKS FAILED\n", g_failed);
    return 1;
}
