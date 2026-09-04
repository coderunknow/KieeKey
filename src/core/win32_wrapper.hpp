//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
// KieeKey v1.2.1 RC1 - refactored and completed logic
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
// File: src/core/win32_wrapper.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.2.1 RC1 — win32_wrapper.hpp
// The Win32 transport layer of the IME: hook lifecycle, lock-free queueing,
// batched synthetic input, priorities, and hook self-healing.
//
// ARCHITECTURE (v3.3 contract — preserved, no latency regressions):
//
//   ┌────────────────────┐  KeyEvent ring (Vyukov SPSC, 4096)
//   │  HOOK PUMP THREAD  │ ──────────────────────────────┐
//   │ WH_KEYBOARD_LL     │  producer decision runs HERE   │
//   │ WH_MOUSE_LL        │  (engine decision, O(1);       ▼
//   │ EVENT_FOREGROUND   │   72 ns/key core)         ┌────────────────────┐
//   │ + watchdog WM_TIMER│                            │  CONSUMER THREAD   │
//   └────────────────────┘                            │  TIME_CRITICAL     │
//           ▲                                         │  drains OutputItem │
//           │ OutputItem ring (Vyukov SPSC, 1024)     │  batch → ONE       │
//   ┌───────┴───────────────────────────────┐        │  SendInput per edit│
//   │ PRODUCER (hook thread, engine decide) │────────└────────────────────┘
//   └───────────────────────────────────────┘
//
// Layer discipline (v3.3.1 review contract):
//   * This wrapper is a TRANSPORT. It must never contain Vietnamese typing
//     heuristics — every composition decision comes from the core TextEngine
//     (kieekey_core.hpp), invoked by the producer callback the app installs.
//   * The consumer applies OutputItems with the batched InlineEmitter
//     (inline mode) or the app's TSF sink — never both for one edit.
//
// v3.3.1 additions:
//   * HookWatchdog — self-healing against the OS silently unhooking a
//     low-level hook (LowLevelHooksTimeout expiry, desktop switches, UIPI
//     transitions). Heartbeat ticks run inside the hook message pump
//     (WM_TIMER, ~5 ms period); aliveness is inferred from
//     GetLastInputInfo vs the ticks the LL callbacks themselves stamp.
//     Two consecutive misses trigger a synchronous unhook + reinstall on
//     the pump thread — re-establishment lands within 5–15 ms of the
//     confirmed loss, with no user-visible key loss beyond the already
//     lost events.
//   * InlineEmitter — every engine edit (backspaces + replacement) is
//     composed into ONE stack INPUT array and issued with a single
//     SendInput call (chunked only when a payload exceeds one batch).
//     Zero heap allocation, self-tagged dwExtraInfo so the hook skips
//     its own output.
//   * Priority policy: consumer thread at THREAD_PRIORITY_TIME_CRITICAL,
//     hook pump at THREAD_PRIORITY_HIGHEST, process at
//     ABOVE_NORMAL_PRIORITY_CLASS (v1.2.1 RC1: reduced from HIGH + TIME_CRIT
//     on both threads — the over-elevation starved the foreground app's text
//     rendering thread on loaded systems).
//   * OutputRing is the Vyukov SPSC ring with capacity 1024.
//----------------------------------------------------------------------------
#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <type_traits>

#if defined(_WIN32) && !defined(OK_WRAP_NO_WIN32)
#define OK_WRAP_HAS_WIN32 1
#include <windows.h>
#include "CpuPause.hpp"         // ok::cpu::pause — portable spin hint
                                // (PAUSE on x86/x64/ARM64EC, YIELD on ARM64)
#include "Win32RAII.hpp"        // ok::win32::EventHandle (barrier event)
#else
#define OK_WRAP_HAS_WIN32 0
// Native test build: the minimal Win32 surface this header names, declared
// at global scope exactly like <windows.h> would.
#include "win32_shim.hpp"
#endif

#include "LockFreeQueue.hpp"
#include "kieekey_core.hpp"

#if OK_WRAP_HAS_WIN32
#include "ModernKeyHook.hpp"   // production: the wrapper composes the hook pump
#endif

namespace ok::wrap {

#if OK_WRAP_HAS_WIN32
using ok::hook::kSelfInjectedExtraInfo;
#else
// Same magic constant as ModernKeyHook.hpp (native test build; the shim
// recorder asserts events carry exactly this tag).
inline constexpr ULONG_PTR kSelfInjectedExtraInfo = 0x0B00B1E5;
#endif

//---------------------------------------------------------------------------
// OutputItem — trivially copyable consumer payload (rides the SPSC ring).
// Same layout contract as the v3.1 app-local item, now owned by the wrapper
// and ringed at 1024 (v3.3 Vyukov Ring 1024).
//---------------------------------------------------------------------------
struct OutputItem {
    // v3.4: InlineEdit — an edit the producer decided to emit through the
    // batched InlineEmitter (SendInput) but DEFERRED to the consumer thread
    // (opt-in, S2 policy: env OPENKEY_INLINE_MODE=deferred). Ordered against
    // Edit items by ring order; the consumer flushes pending TSF edits first,
    // then issues the SendInput. Counts into pendingEdits like every edit.
    enum class Kind : std::uint8_t { None, Edit, ForegroundChanged, Resync, InlineEdit } kind = Kind::None;
    std::uint32_t backspace = 0;      // chars to delete before the caret
    std::uint32_t textLen   = 0;      // length of text[] (UTF-16 units)
#if KIEEKEY_PROFILE
    // v3.4 profiling channel: the producer's t0 + record seq ride with the
    // item so the consumer can stamp t3/t4/t5 against the same t0.
    std::uint64_t profT0  = 0;
    std::uint64_t profSeq = 0;
#endif
    // Worst case: newCharCount <= kMaxBuff entries; a UnicodeCompound entry
    // decodes to base + combining mark = 2 wchar_t -> up to 2*kMaxBuff units.
    wchar_t text[2 * ok::text::kMaxBuff + 1] = {};
};
inline constexpr std::size_t kRingTextCap = 2 * ok::text::kMaxBuff + 1;
static_assert(2 * ok::text::kMaxBuff + 1 == 65, "ring text capacity drift");
static_assert(std::is_trivially_copyable_v<OutputItem>, "OutputItem rides the lock-free ring");

using OutputRing = ok::lockfree::SPSCRing<OutputItem, 1024>;   // v3.3: Vyukov Ring 1024

//---------------------------------------------------------------------------
// InlineEmitter — batched synthetic input (zero allocation).
//
// One SendInput call per edit: backspace down/up pairs first, then the
// replacement as KEYEVENTF_UNICODE key/up pairs. Every INPUT carries the
// self-injection magic in dwExtraInfo; the LL hook filters those events.
// Oversized payloads (long macro expansions) chunk into consecutive batched
// calls without ever reordering.
//
// Test build (OK_WRAP_HAS_WIN32 == 0): SendInput is the shim's counting
// recorder, so unit tests assert exact batching/ordering semantics.
//---------------------------------------------------------------------------
class InlineEmitter final {
public:
    // INPUTs per batch: worst normal edit = 32*2 backspaces + 64*2 unicode + 4 slack.
    static constexpr std::size_t kMaxInlineInputs = ok::text::kMaxBuff * 2 + 2 * ok::text::kMaxBuff * 2 + 4;

    InlineEmitter() noexcept = default;
    explicit InlineEmitter(std::atomic<std::uint32_t>* selfInjectTickMs) noexcept
        : selfInjectTickMs_(selfInjectTickMs) {}

    // ONE batched SendInput for (backspace deletions + text insertion).
    void sendEdit(std::size_t backspace, const wchar_t* text, std::size_t len) noexcept;

    // Convenience overload for std::wstring payloads.
    void sendEdit(std::size_t backspace, const std::wstring& text) noexcept {
        sendEdit(backspace, text.data(), text.size());
    }

    // Watchdog feed: the atomic the emitter stamps on every SendInput call
    // (nullable — stamped only when wired, e.g. by Win32Wrapper).
    void setSelfInjectTick(std::atomic<std::uint32_t>* p) noexcept { selfInjectTickMs_ = p; }

    // Diagnostics.
    [[nodiscard]] std::uint64_t sendInputCalls() const noexcept { return sendInputCalls_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t inputsInjected() const noexcept { return inputsInjected_.load(std::memory_order_relaxed); }
    // v1.1.3: inputs the system REJECTED (UIPI foreground, job limits) —
    // each one is a user keystroke the app never received.
    [[nodiscard]] std::uint64_t sendInputFailed() const noexcept { return sendInputFailed_.load(std::memory_order_relaxed); }

private:
    void stampInjected() noexcept {
        if (selfInjectTickMs_) {
            selfInjectTickMs_->store(static_cast<std::uint32_t>(::GetTickCount()),
                                     std::memory_order_relaxed);
        }
    }

    std::atomic<std::uint32_t>* selfInjectTickMs_ = nullptr;   // watchdog feed (nullable)
    std::atomic<std::uint64_t> sendInputCalls_{0};
    std::atomic<std::uint64_t> inputsInjected_{0};
    std::atomic<std::uint64_t> sendInputFailed_{0};
};

//---------------------------------------------------------------------------
// EditDrainBarrier (v3.4, fixes S1) — event-driven ordering barrier.
//
// Contract: a pass-through key must not reach the application while edits
// are still pending, or the app's text overtakes the engine's buffer and the
// next edit's backspace deletes the wrong characters (ghosting/sticking).
// The barrier is enforced on the HOOK THREAD inside the LL callback, so its
// cost is paid on the critical path of every following keystroke.
//
// v3.3.1 did a bounded busy-spin (200,000 × _mm_pause ≈ 2 ms) inside the LL
// callback. Two measured/pathological failures:
//   (a) a slow TSF commit (Word: 100 µs–1 ms) stalled the hook thread for
//       up to 2 ms before EVERY following key — pass-through AND the next
//       tone mark;
//   (b) on ≤ 2-core hosts the spin actively stole the consumer thread's
//       core, so the spin CAUSED the delay it waited for (self-inflicted).
//
// v3.4 hybrid: a ~2 µs spin (absorbs the common case — consumer hot, drain
// completes in ~1 µs, zero kernel transition), then an event-based wait on
// the consumer's "drained" notification, hard-capped at kWaitBudgetMs.
// The wait YIELDS the core: the TIME_CRITICAL consumer actually gets
// scheduled, and the callback stays far below LowLevelHooksTimeout.
//
// Lost-wakeup safety (stateful auto-reset event):
//   consumer: pending.fetch_sub(n) == n  →  notifyDrained()
//   producer: pending != 0 → spin → wait(budget) → re-check pending.
//   A SetEvent that lands BEFORE the producer's wait is not lost — the
//   event is STATEFUL, the wait consumes it immediately and the loop
//   re-checks the counter. A timeout degrades exactly like v3.3.1's
//   exhausted spin bound (deliver the key anyway), at half the worst case,
//   and is counted for diagnostics.
//---------------------------------------------------------------------------
class EditDrainBarrier final {
public:
    // Hard hook-thread blocking budget (milliseconds). The LL callback must
    // never wait longer than this in ANY path (enforced by test builds).
    static constexpr std::uint32_t kWaitBudgetMs = 1;
    // Spin iterations before falling to the event wait (~2 µs: each
    // _mm_pause ≈ 30–40 cycles on modern x86; 200 × ~35 cy ≈ 7 kcy ≈ 2.3 µs
    // at 3 GHz).
    static constexpr std::uint32_t kSpinIters = 200;

    EditDrainBarrier(const EditDrainBarrier&)            = delete;
    EditDrainBarrier& operator=(const EditDrainBarrier&) = delete;

#if OK_WRAP_HAS_WIN32
    EditDrainBarrier() noexcept {
        ev_.reset(::CreateEventW(nullptr, FALSE, FALSE, nullptr));   // auto-reset
    }

    // Consumer side: call when pendingEdits transitions to 0.
    void notifyDrained() noexcept {
        if (ev_) { ::SetEvent(ev_.get()); }
    }

    // Hook-thread side. Returns true when the drain completed (pending hit
    // 0 within the budget), false on timeout (key is delivered anyway —
    // identical degradation to v3.3.1, at ≤ half the worst-case stall).
    //
    // waitDrainedHooked — test/advanced entry point: injects a per-spin-
    // iteration hook and an explicit budget. Production semantics flow
    // through waitDrained() below (default budget = kWaitBudgetMs). Tests
    // use the hook to land a drain at an EXACT spin iteration (fully
    // deterministic — no second thread, no dependence on OS scheduling)
    // and a generous budget when they exercise the wake-up mechanism; the
    // stall cap itself is pinned by the dedicated budget-bounded test.
    // (Rationale: on a loaded CI VM a freshly slept thread's first wake
    // can be scheduled after the whole 1 ms budget, so sleep-based barrier
    // tests race the deadline and fail — a scheduler artifact, not a
    // barrier defect.)
    template <typename PauseFn>
    bool waitDrainedHooked(const std::atomic<std::uint32_t>& pending,
                           std::uint32_t spinIters, PauseFn&& pause,
                           std::uint32_t budgetMs) noexcept {
        if (pending.load(std::memory_order_acquire) == 0) { return true; }
        for (std::uint32_t i = 0; i < spinIters; ++i) {
            pauseCpu();
            pause();
            if (pending.load(std::memory_order_acquire) == 0) { return true; }
        }
        if (!ev_) { return pending.load(std::memory_order_acquire) == 0; }
        // Event wait, bounded by the absolute deadline (wrap-safe signed ms).
        // v1.1.0: QPC-backed deadline. The previous GetTickCount() deadline
        // advanced at the system clock interrupt (15.6 ms default), so the
        // 1 ms budget was only real while the process held
        // timeBeginPeriod(1); if that ever fails (winmm contention, policy)
        // one coarse tick inflated the LL-callback stall to ~15 ms. QPC is
        // monotonic and high-resolution unconditionally.
        const std::int64_t deadline = qpcMs() + static_cast<std::int64_t>(budgetMs);
        while (true) {
            if (pending.load(std::memory_order_acquire) == 0) { return true; }
            const std::int64_t left = deadline - qpcMs();
            if (left <= 0) {
                // Count for diagnostics (main.cpp's diagnostics tab reads
                // timeouts()); the shim flavor counts this too.
                timeouts_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            ::WaitForSingleObject(ev_.get(), static_cast<DWORD>(left));
        }
    }

    bool waitDrained(const std::atomic<std::uint32_t>& pending,
                     std::uint32_t spinIters = kSpinIters) noexcept {
        return waitDrainedHooked(pending, spinIters, [] {}, kWaitBudgetMs);
    }

    [[nodiscard]] std::uint64_t timeouts() const noexcept { return timeouts_.load(std::memory_order_relaxed); }

private:
    // v1.1.0 — QPC millisecond clock for the barrier deadline (see
    // waitDrainedHooked). Magic-static frequency cache: initialized once,
    // thread-safe, zero per-call overhead.
    static std::int64_t qpcMs() noexcept {
        static const std::int64_t freq = []() noexcept {
            LARGE_INTEGER f{};
            ::QueryPerformanceFrequency(&f);
            return f.QuadPart;
        }();
        LARGE_INTEGER now{};
        ::QueryPerformanceCounter(&now);
        return (now.QuadPart * 1000) / freq;
    }
    // Portable CPU spin hint — the per-architecture mapping (PAUSE on
    // x86/x64/ARM64EC, YIELD on ARM64/ARM) lives in CpuPause.hpp; do NOT
    // include <immintrin.h> here, it hard-errors on ARM64 (C1189).
    static void pauseCpu() noexcept { ok::cpu::pause(); }

    ok::win32::EventHandle ev_;
    std::atomic<std::uint64_t> timeouts_{0};

#else  // OK_WRAP_NO_WIN32 — shim flavor (stateful flag + condvar mirror)

    EditDrainBarrier() noexcept = default;

    void notifyDrained() noexcept {
        {
            std::lock_guard<std::mutex> lk(m_);
            evt_.store(true, std::memory_order_release);
        }
        cv_.notify_one();
    }

    // Test/advanced entry point — injects a per-spin-iteration hook and an
    // explicit budget. Production semantics flow through waitDrained()
    // below (default budget = kWaitBudgetMs). See the Win32 flavor's
    // waitDrainedHooked for the full rationale: sleep-based barrier tests
    // race the 1 ms deadline on loaded CI VMs (a sleeping thread's first
    // wake can be scheduled after the whole budget), so wake-up mechanics
    // are verified with exact-iteration hooks + a generous budget instead.
    template <typename PauseFn>
    bool waitDrainedHooked(const std::atomic<std::uint32_t>& pending,
                           std::uint32_t spinIters, PauseFn&& pause,
                           std::uint32_t budgetMs) noexcept {
        if (pending.load(std::memory_order_acquire) == 0) { return true; }
        for (std::uint32_t i = 0; i < spinIters; ++i) {
            pause();
            if (pending.load(std::memory_order_acquire) == 0) { return true; }
        }
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(budgetMs);
        while (true) {
            if (pending.load(std::memory_order_acquire) == 0) { return true; }
            if (std::chrono::steady_clock::now() >= deadline) {
                timeouts_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait_until(lk, deadline, [&] {
                return evt_.load(std::memory_order_acquire) ||
                       pending.load(std::memory_order_acquire) == 0;
            });
            evt_.store(false, std::memory_order_release);   // auto-reset
        }
    }

    bool waitDrained(const std::atomic<std::uint32_t>& pending,
                     std::uint32_t spinIters = kSpinIters) noexcept {
        return waitDrainedHooked(pending, spinIters, [] { }, kWaitBudgetMs);
    }

    [[nodiscard]] std::uint64_t timeouts() const noexcept { return timeouts_.load(std::memory_order_relaxed); }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::atomic<bool> evt_{false};
    std::atomic<std::uint64_t> timeouts_{0};
#endif
};

//---------------------------------------------------------------------------
// PendingEditCounter (v1.2.0 Stable) — the ordering-barrier bookkeeping.
//
// HISTORY: this counter used to live inline in src/app/main.cpp as a bare
// std::atomic<std::uint32_t> incremented/decremented at six call sites. Being
// app-local it was untestable, and two of its failure modes are exactly the
// kind of bug this release exists to remove:
//
//   (a) STRANDED COUNT — a lifecycle transition (engine switched off, IME
//       auto-excluded in the current app, pipeline (re)started) stops the
//       producer from queueing work. Nothing wakes the consumer any more, so
//       a count that was already published is never released. From then on
//       EVERY pass-through keystroke paid the full barrier timeout (1 ms of
//       hook-thread stall per key) until an edit happened to re-arm the
//       wake — the "sometimes it takes a moment" symptom.
//   (b) PHANTOM COUNT — publishing after the ring push let the consumer apply
//       the edit and observe 0 before the increment landed; the "reached
//       zero" notification was then skipped (fixed in v1.1.3, pinned here).
//
// It now lives in the wrapper layer so the invariants are unit-testable on
// EVERY platform (this is pure std + the barrier above, no Win32).
//
// CONTRACT
//   * publish(n)  — producer, BEFORE the item becomes visible to the consumer
//                   (acq_rel), so the consumer can never observe "applied
//                   everything" while a publish is still in flight.
//   * rollback(n) — producer, when the push failed (edit degraded to inline).
//   * consume(n)  — consumer, after the n edits were actually applied.
//   * forceQuiesce() — lifecycle only: clears the count and releases every
//                   waiter. Safe ONLY where the caller guarantees no further
//                   edits can be produced (engine off / pipeline stopped).
//                   It is the recovery for (a), not a general-purpose reset.
//---------------------------------------------------------------------------
class PendingEditCounter final {
public:
    explicit PendingEditCounter(EditDrainBarrier& barrier) noexcept : barrier_(barrier) {}

    PendingEditCounter(const PendingEditCounter&)            = delete;
    PendingEditCounter& operator=(const PendingEditCounter&) = delete;

    // Producer side — publish BEFORE the ring push (see contract above).
    void publish(std::uint32_t n = 1) noexcept {
        if (n == 0) { return; }
        pending_.fetch_add(n, std::memory_order_acq_rel);
    }

    // Producer side — the push failed; the edit was emitted inline instead.
    void rollback(std::uint32_t n = 1) noexcept {
        if (n == 0) { return; }
        pending_.fetch_sub(n, std::memory_order_acq_rel);
        releaseIfDrained();
    }

    // Consumer side — n edits were applied (or definitively abandoned).
    void consume(std::uint32_t n) noexcept {
        if (n == 0) { return; }
        pending_.fetch_sub(n, std::memory_order_acq_rel);
        releaseIfDrained();
    }

    // Lifecycle side — see contract. Clears a stranded count AND wakes every
    // waiter, so a stalled hook thread can never stay blocked on an edit that
    // is never going to be applied.
    void forceQuiesce() noexcept {
        pending_.store(0, std::memory_order_release);
        barrier_.notifyDrained();
    }

    // Producer side — bounded wait until every published edit was applied.
    // Returns false on timeout (the key is delivered anyway — the documented
    // degradation, counted in EditDrainBarrier::timeouts()).
    [[nodiscard]] bool waitDrained() noexcept { return barrier_.waitDrained(pending_); }

    [[nodiscard]] std::uint32_t pending() const noexcept {
        return pending_.load(std::memory_order_acquire);
    }

private:
    void releaseIfDrained() noexcept {
        // Re-READ after the release: a late producer publish (the race fixed
        // in v1.1.3) can slip between the fetch_sub and this load, and a
        // skipped notification would leave the barrier armed forever.
        if (pending_.load(std::memory_order_acquire) == 0) {
            barrier_.notifyDrained();
        }
    }

    EditDrainBarrier&      barrier_;
    std::atomic<std::uint32_t> pending_{0};
};

//---------------------------------------------------------------------------
// HookWatchdog — hook self-healing (v3.3.1).
//
// Detection model: the LL callbacks stamp a tick for EVERY event they see
// (keyboard, mouse, including our own injected events) — ModernKeyHook
// exposes these as lastKbEventTickMs()/lastMouseEventTickMs(). The system
//-wide last-input tick (GetLastInputInfo) covers ALL input, injected or
// not. When the system saw input that NEITHER LL hook saw (and we did not
// inject — our own emitter stamps its ticks), an LL hook has been silently
// removed. Two consecutive violations (debounce against tick jitter) trigger
// the recovery: unhook + reinstall ON THE PUMP THREAD (LL hooks are
// thread-affine), which re-arms WH_KEYBOARD_LL in O(ms).
//
// The tick is driven by the pump's WM_TIMER (ModernKeyHook::setPumpTick);
// with timeBeginPeriod(1) the pump tick lands every ~5–10 ms, so the
// detect → re-establish window meets the 5–15 ms requirement.
//
// The environment vtable keeps the state machine unit-testable natively
// (fake clock, fake last-input, counting rehook) without Windows.
//---------------------------------------------------------------------------
class HookWatchdog final {
public:
    struct Env {
        virtual ~Env() = default;
        [[nodiscard]] virtual std::uint32_t tickNow() const noexcept = 0;        // ms clock
        [[nodiscard]] virtual std::uint32_t systemLastInputTick() const noexcept = 0; // GetLastInputInfo
        [[nodiscard]] virtual bool reinstallHooks() noexcept = 0;                // pump-thread rehook
        // v1.1.3: ACTIVE KEYBOARD PROBE — inject a harmless self-tagged key
        // (VK 0xFF, unassigned, down+up). A live keyboard hook stamps it and
        // filters it (the app never sees it); a dead one lets it through and
        // leaves the keyboard stamp stale. This is the only way to
        // distinguish "mouse-only user, keyboard stamp legitimately idle"
        // from "keyboard hook silently removed" — GetLastInputInfo cannot.
        [[nodiscard]] virtual bool probeKeyboard() noexcept = 0;
    };

    struct Config {
        std::uint32_t tickIntervalMs   = 5;      // WM_TIMER period (pump)
        std::uint32_t missThresholdMs  = 60;     // seen-vs-system skew that counts as a miss
        std::uint32_t missesToRehook   = 2;      // consecutive misses before recovery
        std::uint32_t probeIntervalMs  = 20;     // min spacing between active probes
    };

    HookWatchdog(Env& env, const Config& cfg,
                 const std::atomic<std::uint32_t>& kbSeenTickMs,
                 const std::atomic<std::uint32_t>& mouseSeenTickMs,
                 const std::atomic<std::uint32_t>& selfInjectTickMs) noexcept
        : env_(env), cfg_(cfg), kbSeen_(kbSeenTickMs), mouseSeen_(mouseSeenTickMs),
          selfInject_(selfInjectTickMs) {}

    // One heartbeat. Call from the pump tick ONLY (reinstall must run on
    // the pump thread). Returns true when a rehook was performed now.
    //
    // v3.4 (S6): fast path — when WE stamped input within the miss
    // threshold, the hook is alive BY DEFINITION (the stamp comes from our
    // own LL callback or our own injector), so the GetLastInputInfo syscall
    // is skipped entirely. During active typing/mouse use the heartbeat
    // costs one GetTickCount + three relaxed atomic loads (~30 ns) instead
    // of a syscall every 5 ms. Detection latency is unchanged: a dead hook
    // stops stamping, `seen` goes stale past the threshold, and the check
    // falls through to the system-tick comparison exactly as before.
    // v1.1.3 — per-hook detection. The old fast path collapsed keyboard,
    // mouse and self-inject stamps into ONE max, so a silently-removed
    // WH_KEYBOARD_LL stayed invisible while the mouse was in use (every
    // mouse move refreshed the max) — the self-heal promise only held for
    // TOTAL hook death. The keyboard stamp is now evaluated INDEPENDENTLY;
    // an ambiguous "system input is fresh but our keyboard stamp is stale"
    // state (mouse-only use, or a dead keyboard hook) is resolved by the
    // ACTIVE PROBE: a live hook stamps the probe and the ambiguity clears
    // within one tick; a dead one never does and the miss counter walks to
    // the rehook (detection still ~10-25 ms, well inside the 5-15 ms spec
    // for the common typing case where nothing masks the keyboard stamp).
    bool check() noexcept {
        const std::uint32_t now = env_.tickNow();
        const std::uint32_t kbSeen = kbSeen_.load(std::memory_order_relaxed);
        if (static_cast<std::int32_t>(now - kbSeen) <
            static_cast<std::int32_t>(cfg_.missThresholdMs)) {
            misses_ = 0;   // fresh keyboard stamp of our own — hook alive
            return false;
        }
        const std::uint32_t systemTick = env_.systemLastInputTick();
        // Signed diffs — correct across the 49.7-day GetTickCount wrap.
        const std::int32_t skew =
            static_cast<std::int32_t>(systemTick - kbSeen);
        if (skew <= static_cast<std::int32_t>(cfg_.missThresholdMs)) {
            misses_ = 0;   // no unexplained system input vs the keyboard stamp
            return false;
        }
        // System input is newer than our last keyboard sighting. Mouse-only
        // activity or our own injector keep this state legitimately benign —
        // resolve it with the active probe, then treat every tick it
        // persists as a miss (a dead hook cannot refresh the stamp, so the
        // counter reaches the rehook in missesToRehook ticks).
        const std::uint32_t mouseSeen = mouseSeen_.load(std::memory_order_relaxed);
        const std::uint32_t selfSeen = selfInject_.load(std::memory_order_relaxed);
        const bool mouseFresh = static_cast<std::int32_t>(now - mouseSeen) <
                                static_cast<std::int32_t>(cfg_.missThresholdMs);
        const bool selfFresh = static_cast<std::int32_t>(now - selfSeen) <
                               static_cast<std::int32_t>(cfg_.missThresholdMs);
        if (mouseFresh || selfFresh) {
            if (static_cast<std::int32_t>(now - lastProbeTickMs_) >=
                static_cast<std::int32_t>(cfg_.probeIntervalMs)) {
                lastProbeTickMs_ = now;
                static_cast<void>(env_.probeKeyboard());
            }
            ++misses_;
            if (misses_ >= cfg_.missesToRehook) {
                misses_ = 0;
                if (env_.reinstallHooks()) {
                    lastRehookTickMs_.store(env_.tickNow(), std::memory_order_relaxed);
                    rehookCount_.fetch_add(1, std::memory_order_relaxed);
                    return true;
                }
            }
            return false;
        }
        // No mask at all: system input is fresh, our keyboard hook saw
        // nothing, the mouse hook saw nothing, we injected nothing — a
        // keyboard event was swallowed elsewhere or the hook is gone.
        if (++misses_ >= cfg_.missesToRehook) {
            misses_ = 0;
            if (env_.reinstallHooks()) {
                lastRehookTickMs_.store(env_.tickNow(), std::memory_order_relaxed);
                rehookCount_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            // Rehook failed (transient UIPI/permission): keep counting;
            // the next tick retries. No further escalation here — the
            // app surfaces rehookCount for diagnostics.
        }
        return false;
    }

    [[nodiscard]] std::uint64_t rehookCount() const noexcept { return rehookCount_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint32_t lastRehookTickMs() const noexcept { return lastRehookTickMs_.load(std::memory_order_relaxed); }

private:
    [[nodiscard]] std::uint32_t latestSeenTick() const noexcept {
        std::uint32_t t = kbSeen_.load(std::memory_order_relaxed);
        const std::uint32_t m = mouseSeen_.load(std::memory_order_relaxed);
        if (static_cast<std::int32_t>(m - t) > 0) { t = m; }
        const std::uint32_t s = selfInject_.load(std::memory_order_relaxed);
        if (static_cast<std::int32_t>(s - t) > 0) { t = s; }
        return t;
    }

    Env& env_;
    Config cfg_;
    const std::atomic<std::uint32_t>& kbSeen_;
    const std::atomic<std::uint32_t>& mouseSeen_;
    const std::atomic<std::uint32_t>& selfInject_;
    std::uint32_t misses_ = 0;
    std::uint32_t lastProbeTickMs_ = 0;
    std::atomic<std::uint64_t> rehookCount_{0};
    std::atomic<std::uint32_t> lastRehookTickMs_{0};
};

//---------------------------------------------------------------------------
// Win32Wrapper — the assembled v3.3.1 pipeline (hook + rings + emitter +
// watchdog + priority policy). Drop-in compatible with ModernKeyHook usage
// in the app (same start/stop/producer-handler surface), plus:
//   * outRing()          — the 1024-slot OutputItem ring for the producer
//   * emitter()          — the batched inline emitter for the consumer
//   * enableSelfHealing()-installs the pump-tick watchdog heartbeat
//   * tone-style chord plumbing stays in the APP's producer callback
//     (engine-owned logic; the wrapper carries no typing rules)
// (Production/Windows only — the native test build exercises the ring,
// emitter and watchdog state machine directly.)
//---------------------------------------------------------------------------
#if OK_WRAP_HAS_WIN32
class Win32Wrapper final {
public:
    Win32Wrapper() noexcept = default;
    ~Win32Wrapper() { stop(); }
    Win32Wrapper(const Win32Wrapper&)            = delete;
    Win32Wrapper& operator=(const Win32Wrapper&) = delete;

    // Same contract as ModernKeyHook::start (handler runs on the consumer
    // thread). Applies the process priority policy, then starts the hook.
    [[nodiscard]] bool start(ok::hook::ModernKeyHook::EventHandler handler) {
        // v1.2.1 RC1 — process priority policy (once): ABOVE_NORMAL instead
        // of HIGH_PRIORITY_CLASS.
        //
        // RATIONALE: HIGH_PRIORITY_CLASS elevates EVERY thread in the
        // process (UI thread, monitor thread, watchdog, settings dialog)
        // above normal-priority applications. Combined with TIME_CRITICAL on
        // the consumer + HIGHEST on the pump, this created scheduler
        // contention with the foreground application's own text rendering
        // thread. The user-visible symptom was "fast microbenchmarks but
        // typing feels laggy under load" — the IME's threads were starving
        // the app that needed to paint the text.
        //
        // ABOVE_NORMAL_PRIORITY_CLASS keeps the IME ahead of background work
        // (compilers, downloads) while yielding to normal-priority foreground
        // threads. The consumer alone at TIME_CRITICAL provides the latency
        // edge; the process boost is redundant with it and harmful.
        if (!priorityApplied_.exchange(true, std::memory_order_relaxed)) {
            ::SetPriorityClass(::GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
        }
        emitter_.setSelfInjectTick(&selfInjectTickMs_);
        return hook_.start(std::move(handler));
    }

    void setProducerHandler(ok::hook::ModernKeyHook::ProducerHandler h) noexcept {
        hook_.setProducerHandler(std::move(h));
    }
    void setConsumerFinalizer(ok::hook::ModernKeyHook::Finalizer f) noexcept {
        hook_.setConsumerFinalizer(std::move(f));
    }
    void stop() noexcept { hook_.stop(); }

    // v1.2.0 Stable — wake the consumer from any thread (lifecycle recovery
    // for edits stranded by a transition that stops the producer; see
    // ModernKeyHook::pokeConsumer for the failure mode).
    void pokeConsumer() noexcept { hook_.pokeConsumer(); }
    [[nodiscard]] std::uint64_t overflowWakeCount() const noexcept { return hook_.overflowWakeCount(); }
    [[nodiscard]] std::uint64_t handlerExceptionCount() const noexcept { return hook_.handlerExceptionCount(); }

    [[nodiscard]] bool running() const noexcept { return hook_.running(); }

    // v3.3.1: install the self-healing watchdog on the pump (idempotent).
    // Call BEFORE start() so the heartbeat arms with the pump.
    void enableSelfHealing() noexcept;

    // ---- pipeline surfaces ----
    [[nodiscard]] OutputRing& outRing() noexcept { return outRing_; }
    [[nodiscard]] const OutputRing& outRing() const noexcept { return outRing_; }
    [[nodiscard]] InlineEmitter& emitter() noexcept { return emitter_; }

    // ---- diagnostics ----
    [[nodiscard]] std::uint64_t pushed()  const noexcept { return hook_.pushed(); }
    [[nodiscard]] std::uint64_t dropped() const noexcept { return hook_.dropped(); }
    [[nodiscard]] std::int64_t  peakLatencyUs() const noexcept { return hook_.peakLatencyUs(); }
    [[nodiscard]] std::int64_t  avgLatencyUs()  const noexcept { return hook_.avgLatencyUs(); }
    // v1.1.0 — diagnostics forwarding (reset-on-open peak, self-heal count).
    void resetPeakLatency() noexcept { hook_.resetPeakLatency(); }
    [[nodiscard]] std::uint64_t hookReinstallCount() const noexcept { return hook_.hookReinstallCount(); }
    // v1.1.0 — shutdown telemetry: true when stop() had to detach a wedged
    // worker (the app exits via ExitProcess on that path; see main.cpp).
    [[nodiscard]] bool stuckThreadsDetached() const noexcept { return hook_.stuckThreadsDetached(); }
    [[nodiscard]] std::uint64_t rehookCount() const noexcept { return rehookCount_.load(std::memory_order_relaxed); }

    // v1.1.0 — re-seed the engine-facing modifier tracker from the OS key
    // state (forwarding to ModernKeyHook::resyncModifiersFromOs; see there
    // for the stale shift/caps rationale). Pump thread only — the app calls
    // it from the ForegroundChanged branch of its producer handler.
    void resyncModifiersFromOs() noexcept { hook_.resyncModifiersFromOs(); }

private:
    // Watchdog environment bound to the Win32 APIs + the owned hook.
    // (The shim path exposes the same global names, so the environment is
    // identical under both builds — only the hook differs.)
    struct PumpEnv final : HookWatchdog::Env {
        explicit PumpEnv(ok::hook::ModernKeyHook& h) noexcept : hook(h) {}
        [[nodiscard]] std::uint32_t tickNow() const noexcept override {
            return static_cast<std::uint32_t>(::GetTickCount());
        }
        [[nodiscard]] std::uint32_t systemLastInputTick() const noexcept override {
            LASTINPUTINFO li{};
            li.cbSize = sizeof(li);
            if (::GetLastInputInfo(&li)) { return li.dwTime; }
            return 0;
        }
        bool reinstallHooks() noexcept override { return hook.reinstallHooksOnPump(); }
        // v1.1.3 active keyboard probe: VK 0xFF (unassigned) down+up, tagged
        // with the self-injection magic. A live keyboard hook stamps BOTH
        // events and filters them (the focused app never sees them — the
        // stamp happens before the extra-info filter in the LL callback).
        // A dead hook lets them reach the app, which ignores the unassigned
        // VK — harmless by construction — and leaves the stamp stale.
        bool probeKeyboard() noexcept override {
            INPUT in[2]{};
            in[0].type = INPUT_KEYBOARD;
            in[0].ki.wVk = 0xFF;
            in[0].ki.dwExtraInfo = ok::hook::kSelfInjectedExtraInfo;
            in[1] = in[0];
            in[1].ki.dwFlags = KEYEVENTF_KEYUP;
            return ::SendInput(2, in, sizeof(INPUT)) == 2;
        }
        ok::hook::ModernKeyHook& hook;
    };

    ok::hook::ModernKeyHook hook_;
    OutputRing outRing_;
    std::atomic<std::uint32_t> selfInjectTickMs_{0};   // emitter → watchdog feed
    InlineEmitter emitter_;
    std::unique_ptr<PumpEnv> watchdogEnv_;
    std::unique_ptr<HookWatchdog> watchdog_;
    std::atomic<std::uint64_t> rehookCount_{0};
    std::atomic<bool> priorityApplied_{false};
};
#endif // OK_WRAP_HAS_WIN32

} // namespace ok::wrap
