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
// File: src/core/win32_wrapper.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v3.3.1 — win32_wrapper.hpp
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
//   * Priority policy: hook pump + consumer threads run at
//     THREAD_PRIORITY_TIME_CRITICAL and the process at HIGH_PRIORITY_CLASS.
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
        const std::uint32_t deadline = tickMs() + budgetMs;
        while (true) {
            if (pending.load(std::memory_order_acquire) == 0) { return true; }
            const std::int32_t left =
                static_cast<std::int32_t>(deadline - tickMs());
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
    static std::uint32_t tickMs() noexcept { return static_cast<std::uint32_t>(::GetTickCount()); }
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
    };

    struct Config {
        std::uint32_t tickIntervalMs   = 5;      // WM_TIMER period (pump)
        std::uint32_t missThresholdMs  = 60;     // seen-vs-system skew that counts as a miss
        std::uint32_t missesToRehook   = 2;      // consecutive misses before recovery
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
    bool check() noexcept {
        const std::uint32_t now = env_.tickNow();
        const std::uint32_t seen = latestSeenTick();
        if (static_cast<std::int32_t>(now - seen) <
            static_cast<std::int32_t>(cfg_.missThresholdMs)) {
            misses_ = 0;   // fresh stamp of our own — hook alive
            return false;
        }
        const std::uint32_t systemTick = env_.systemLastInputTick();
        // Signed diff — correct across the 49.7-day GetTickCount wrap.
        const std::int32_t skew = static_cast<std::int32_t>(systemTick - seen);
        if (skew > static_cast<std::int32_t>(cfg_.missThresholdMs)) {
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
        } else {
            misses_ = 0;   // healthy (or our own injection explains the input)
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
        // Process priority policy (once): HIGH_PRIORITY_CLASS keeps the pump
        // + consumer pair (both TIME_CRITICAL) ahead of ordinary work
        // without the starvation risk of REALTIME.
        if (!priorityApplied_.exchange(true, std::memory_order_relaxed)) {
            ::SetPriorityClass(::GetCurrentProcess(), HIGH_PRIORITY_CLASS);
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
    [[nodiscard]] std::uint64_t rehookCount() const noexcept { return rehookCount_.load(std::memory_order_relaxed); }

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
