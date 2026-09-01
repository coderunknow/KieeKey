//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.0.2 - refactored and completed logic
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
// File: src/core/ModernKeyHook.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.0.2 — ModernKeyHook.cpp
// See ModernKeyHook.hpp for the architecture rationale.
//----------------------------------------------------------------------------
#include "ModernKeyHook.hpp"

#include <windowsx.h>
#include <timeapi.h>     // timeBeginPeriod / timeEndPeriod (self-heal tick)
#include "CpuPause.hpp"  // ok::cpu::pause — portable consumer spin hint
                         // (never include <emmintrin.h> here: x86-only header,
                         //  hard-errors C1189 on ARM64 builds)

#include <algorithm>     // std::clamp (adaptive spin window)

#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")   // MSVC auto-link; CMake also links it
#endif

namespace ok::hook {
namespace {

// Modifier bit layout inside modifierBits_ (mirrors legacy MASK_* flags).
constexpr std::uint32_t kMaskShift   = 0x01;
constexpr std::uint32_t kMaskCtrl    = 0x02;
constexpr std::uint32_t kMaskAlt     = 0x04;
constexpr std::uint32_t kMaskWin     = 0x08;
constexpr std::uint32_t kMaskCaps    = 0x10;
constexpr std::uint32_t kMaskNum     = 0x20;
constexpr std::uint32_t kMaskScroll  = 0x40;

// Self-injection watchdog: if the queue ever saturates for > 200 ms while the
// consumer is alive, something pathological is happening; keep dropping but
// surface it in stats (never block the hook).
constexpr std::uint64_t kMaxAcceptableHookLatencyNs = 200'000'000ULL;

inline std::uint64_t qpcNow() noexcept {
    LARGE_INTEGER li;
    ::QueryPerformanceCounter(&li);
    return static_cast<std::uint64_t>(li.QuadPart);
}
inline std::uint64_t qpcFreq() noexcept {
    LARGE_INTEGER li;
    ::QueryPerformanceFrequency(&li);
    return static_cast<std::uint64_t>(li.QuadPart);
}

ModernKeyHook* g_instance = nullptr;   // trampoline target; single instance

//---------------------------------------------------------------------------
// Modifier helpers — maintained from the key stream itself so the hook path
// costs zero Win32 calls (legacy code called GetKeyState/GetAsyncKeyState
// per event from inside the hook; we track deltas instead).
//---------------------------------------------------------------------------
std::uint32_t modifierMaskForVk(std::uint32_t vk) noexcept {
    switch (vk) {
        case VK_LSHIFT: case VK_RSHIFT: return kMaskShift;
        case VK_LCONTROL: case VK_RCONTROL: return kMaskCtrl;
        case VK_LMENU: case VK_RMENU: return kMaskAlt;
        case VK_LWIN: case VK_RWIN: return kMaskWin;
        default: return 0;
    }
}

} // namespace

//===========================================================================
// Lifecycle
//===========================================================================
bool ModernKeyHook::start(EventHandler handler) {
    if (running_.load(std::memory_order_acquire)) { return true; }
    if (!handler) { return false; }

    handler_ = std::move(handler);

    // Wake event used to interrupt the consumer's wait (portable, no timers).
    wakeEvent_.reset(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!wakeEvent_) { return false; }

    // Seed modifier state from the current thread's key state.
    auto seedMods = [this] {
        std::uint32_t m = 0;
        if (::GetKeyState(VK_LSHIFT) < 0 || ::GetKeyState(VK_RSHIFT) < 0) m |= kMaskShift;
        if (::GetKeyState(VK_LCONTROL) < 0 || ::GetKeyState(VK_RCONTROL) < 0) m |= kMaskCtrl;
        if (::GetKeyState(VK_LMENU) < 0 || ::GetKeyState(VK_RMENU) < 0) m |= kMaskAlt;
        if (::GetKeyState(VK_LWIN) < 0 || ::GetKeyState(VK_RWIN) < 0) m |= kMaskWin;
        if (::GetKeyState(VK_CAPITAL) & 1) m |= kMaskCaps;
        if (::GetKeyState(VK_NUMLOCK) & 1) m |= kMaskNum;
        if (::GetKeyState(VK_SCROLL) & 1) m |= kMaskScroll;
        modifierBits_.store(m, std::memory_order_relaxed);
    };

    running_.store(true, std::memory_order_release);
    stats_.pushed.store(0, std::memory_order_relaxed);
    stats_.droppedOverflow.store(0, std::memory_order_relaxed);
    peakLatencyUs_.store(0, std::memory_order_relaxed);
    // v3.5: re-arm the shutdown acknowledgements (start() may follow stop()).
    hookPumpExited_.store(false, std::memory_order_release);
    consumerExited_.store(false, std::memory_order_release);

    // Consumer first (it must exist before the first event can be pushed).
    // The exited flags are stamped as the thread's VERY LAST action (not
    // inside the member functions) so stop()'s bounded wait is exact.
    consumerThread_ = std::thread([this] {
        consumerThreadMain();
        consumerExited_.store(true, std::memory_order_release);
    });
    hookThread_ = std::thread([this, seedMods] {
        seedMods();
        hookThreadMain();
        hookPumpExited_.store(true, std::memory_order_release);
    });

    // Wait until the hooks are installed (or the hook thread dies).
    for (int i = 0; i < 2000 && hookThread_.joinable(); ++i) {
        if (keyboardHook_ || !running_.load(std::memory_order_acquire)) { break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return keyboardHook_.operator bool();
}

void ModernKeyHook::stop() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) { return; }

    // 1) Post WM_QUIT to the hook pump thread — unhooks everything and wakes it.
    //    The thread id is captured by the pump itself (portable across MSVC
    //    and MinGW, where std::thread::native_handle() is not a HANDLE).
    const DWORD tid = hookThreadId_.load(std::memory_order_acquire);
    if (tid != 0) {
        ::PostThreadMessageW(tid, WM_QUIT, 0, 0);
    }
    // 2) Wake the consumer so it sees running_ == false.
    ::SetEvent(wakeEvent_.get());

    // 3) v3.5 BOUNDED wait ("suddenly stops and cannot be opened again" fix).
    //    The consumer may be wedged inside a SYNCHRONOUS TSF edit session
    //    aimed at a hung application's context — that COM call completes
    //    only when the target pumps again (or dies). join()ing such a
    //    thread used to hang the caller forever: the process survived as a
    //    zombie holding the single-instance mutex, so every relaunch
    //    reported "KieeKey is already running" until a reboot. Wait a
    //    bounded 3 s, then DETACH whatever is stuck and leave its handles
    //    owned by `this` (a detached thread keeps using them until it
    //    unwinds; the OS reclaims everything at process exit).
    constexpr DWORD kQuitBudgetMs = 3000;
    const DWORD deadline = ::GetTickCount() + kQuitBudgetMs;
    for (;;) {
        if (hookPumpExited_.load(std::memory_order_acquire) &&
            consumerExited_.load(std::memory_order_acquire)) { break; }
        if (static_cast<int>(::GetTickCount() - deadline) >= 0) { break; }
        ::Sleep(5);
    }
    const bool pumpDone     = hookPumpExited_.load(std::memory_order_acquire);
    const bool consumerDone = consumerExited_.load(std::memory_order_acquire);
    if (hookThread_.joinable()) {
        if (pumpDone) { hookThread_.join(); }
        else          { hookThread_.detach(); }   // stuck — never block the caller
    }
    if (consumerThread_.joinable()) {
        if (consumerDone) { consumerThread_.join(); }
        else              { consumerThread_.detach(); }
    }

    if (pumpDone && consumerDone) {
        // Explicit cleanup (also performed by the pump on WM_QUIT; belt & braces).
        keyboardHook_.reset();
        mouseHook_.reset();
        fgWinEvent_.reset();
        wakeEvent_.reset();
        handler_ = nullptr;
        producerHandler_ = nullptr;
        finalizer_ = nullptr;   // already ran on the consumer thread
    }
    // else: stuck-thread path — deliberately keep every handle alive so the
    // detached thread(s) can still touch them when their blocked call finally
    // returns; the subsequent process exit reclaims the rest.
}

//===========================================================================
// Hook thread: install hooks, pump messages.
//===========================================================================
void ModernKeyHook::hookThreadMain() noexcept {
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    // Best-practice: keep this thread out of WER hang reporting.
    ::SetThreadUILanguage(MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));

    // Publish this pump's thread id BEFORE installing hooks so stop() can
    // always wake it via PostThreadMessage(WM_QUIT).
    hookThreadId_.store(::GetCurrentThreadId(), std::memory_order_release);

    g_instance = this;

    HINSTANCE inst = ::GetModuleHandleW(nullptr);

    // LL hooks must be global; the callback fires on THIS thread's pump.
    keyboardHook_.reset(::SetWindowsHookExW(WH_KEYBOARD_LL, &ModernKeyHook::keyboardProc, inst, 0));
    mouseHook_.reset(::SetWindowsHookExW(WH_MOUSE_LL, &ModernKeyHook::mouseProc, inst, 0));
    fgWinEvent_.reset(::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                        nullptr, &ModernKeyHook::winEventProc, 0, 0,
                                        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS));

    // v3.3.1 self-healing heartbeat: arm the pump-tick WM_TIMER. The tick
    // runs the watchdog inside THIS message loop, so a detected silent
    // unhook is repaired in place (thread-affine) within 5–15 ms. Raise the
    // system timer resolution so a 5 ms period is honored (default 15.6 ms).
    const UINT tickMs = pumpTickIntervalMs_.load(std::memory_order_relaxed);
    const bool tickArmed = (tickMs != 0) && (pumpTick_ != nullptr);
    // v3.5 reliability: when the watchdog tick is NOT armed, arm a 1 s
    // fallback timer anyway so this pump ALWAYS wakes periodically — a lost
    // WM_QUIT can then never stall stop() (the loop re-checks running_).
    const bool fallbackTick = !tickArmed;
    if (tickArmed) {
        ::timeBeginPeriod(1);
        ::SetTimer(nullptr, kSelfHealTimerId, tickMs, nullptr);
    } else {
        ::SetTimer(nullptr, kSelfHealTimerId, 1000, nullptr);
    }

    // The installing thread MUST pump messages or LL hooks never fire.
    MSG msg;
    while (running_.load(std::memory_order_acquire)) {
        const BOOL r = ::GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0)  { break; }               // WM_QUIT
        if (r == -1) { break; }               // error
        if (tickArmed && msg.message == WM_TIMER &&
            msg.hwnd == nullptr && msg.wParam == kSelfHealTimerId) {
            // Watchdog heartbeat — handled inline; never dispatched.
            if (pumpTick_) { pumpTick_(); }
            continue;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    if (tickArmed) {
        ::KillTimer(nullptr, kSelfHealTimerId);
        ::timeEndPeriod(1);
    } else if (fallbackTick) {
        ::KillTimer(nullptr, kSelfHealTimerId);
    }

    // Unhook from the owning thread to avoid dangling callback pointers.
    keyboardHook_.reset();
    mouseHook_.reset();
    fgWinEvent_.reset();
    g_instance = nullptr;
}

//===========================================================================
// v3.3.1 — in-place hook re-installation (self-healing recovery action).
// Pump thread ONLY (LL hooks are thread-affine). Drops and re-arms all
// three hooks; returns true when the keyboard hook lives again.
//===========================================================================
bool ModernKeyHook::reinstallHooksOnPump() noexcept {
    if (!running_.load(std::memory_order_acquire)) { return false; }
    keyboardHook_.reset();
    mouseHook_.reset();
    fgWinEvent_.reset();
    HINSTANCE inst = ::GetModuleHandleW(nullptr);
    keyboardHook_.reset(::SetWindowsHookExW(WH_KEYBOARD_LL, &ModernKeyHook::keyboardProc, inst, 0));
    mouseHook_.reset(::SetWindowsHookExW(WH_MOUSE_LL, &ModernKeyHook::mouseProc, inst, 0));
    fgWinEvent_.reset(::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                        nullptr, &ModernKeyHook::winEventProc, 0, 0,
                                        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS));
    const bool ok = keyboardHook_.operator bool();
    if (ok) { hookReinstallCount_.fetch_add(1, std::memory_order_relaxed); }
    return ok;
}

//===========================================================================
// Consumer thread: sole consumer of the ring; runs the text pipeline.
//===========================================================================
void ModernKeyHook::consumerThreadMain() noexcept {
    // v3.3.1: the consumer applies the engine's edits — the user-visible
    // tail of the typing pipeline. It runs at TIME_CRITICAL alongside the
    // pump: with the process at HIGH_PRIORITY_CLASS the pair preempts
    // ordinary work, which is what keeps p99 E2E latency at microseconds
    // under burst typing (the spin window below absorbs whole bursts).
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    const std::uint64_t freq = qpcFreq();
    const auto usNow = [freq]() noexcept -> std::uint64_t {
        return qpcNow() * 1'000'000ULL / freq;   // µs — QPC-backed, monotonic
    };
    KeyEvent batch[64];

    // v3.3.1 adaptive spin window: after draining, spin for ~1.2× the WORST
    // recent inter-arrival gap (clamped 4–200 µs) before blocking. The v3.0
    // fixed 4096-pause window (~7 µs) missed realistic typing gaps (5–20 ms),
    // so every keystroke paid a full kernel wake (~1–3 µs on Windows, worse
    // on loaded hosts). With the adaptive window the consumer stays hot
    // through the actual gap of a typing burst — the wake cost vanishes from
    // the per-key path — while true idle still parks in WaitForSingleObject
    // at zero CPU (one capped spin per idle transition). Recent-max (not an
    // EMA) so catch-up batches (gap≈0) cannot drag the window below the real
    // gaps — a missed window costs a kernel wake, the exact thing this
    // window exists to avoid.
    std::uint64_t lastArrivalUs = usNow();
    std::uint64_t gapWindow[32]{};   // recent batch-gap ring (max over window)
    std::size_t   gapIdx = 0;
    const auto pushGap = [&](std::uint64_t gap) {
        gapWindow[gapIdx & 31u] = gap;
        ++gapIdx;
    };
    const auto recentMaxGap = [&]() noexcept -> std::uint64_t {
        std::uint64_t m = 0;
        for (const std::uint64_t g : gapWindow) { if (g > m) { m = g; } }
        return m;
    };

    while (running_.load(std::memory_order_acquire) || !queue_.empty()) {
        // Batch drain: amortize atomics; keep up with a typist even under
        // sustained load (legacy code processed one event per callback).
        const std::size_t n = queue_.try_pop_batch(batch, std::size(batch));
        if (n != 0) {
            const std::uint64_t now = usNow();
            const std::uint64_t gap = now - lastArrivalUs;
            lastArrivalUs = now;
            if (gap > 0 && gap < 1'000'000) {          // ignore >1 s pauses
                pushGap(gap);
            }

            for (std::size_t i = 0; i < n; ++i) {
                const KeyEvent& ev = batch[i];

                // E2E latency (QPC delta hook-capture -> consumer).
                if (ev.source != EventSource::ForegroundChanged) {
                    const std::uint64_t latencyNs =
                        (qpcNow() - ev.timestampQpc) * 1'000'000'000ULL / freq;
                    const std::int64_t us = static_cast<std::int64_t>(latencyNs / 1000);
                    stats_.lastLatencyUs.store(us, std::memory_order_relaxed);

                    std::int64_t peak = peakLatencyUs_.load(std::memory_order_relaxed);
                    while (us > peak &&
                           !peakLatencyUs_.compare_exchange_weak(peak, us, std::memory_order_relaxed)) {}

                    // EMA with ~12% weight, integer-only (single writer).
                    const std::int64_t prev = avgLatencyUs_.load(std::memory_order_relaxed);
                    const std::int64_t avg = (prev == 0) ? us : (prev * 7 + us) / 8;
                    avgLatencyUs_.store(avg, std::memory_order_relaxed);
                }

                if (handler_) { handler_(ev); }   // TextEngine + composer live here
            }
            continue;    // burst mode: never wait while data is flowing
        }

        // Queue empty: adaptive pause-spin to catch the very next enqueue
        // without a thread wake (see the window rationale above).
        const std::uint64_t spinBudgetUs =
            std::clamp<std::uint64_t>(recentMaxGap() + recentMaxGap() / 4, 4, 200);
        const std::uint64_t spinStart = usNow();
        while (queue_.empty() && usNow() - spinStart < spinBudgetUs) {
            for (int s = 0; s < 64; ++s) { ok::cpu::pause(); }
        }
        if (queue_.empty() && running_.load(std::memory_order_acquire)) {
            // v3.4 (S4) parked protocol — publish "blocked" BEFORE the final
            // queue re-check so a racing producer either sees parked=true
            // (SetEvent) or the re-check sees the item (unpark, drain).
            consumerParked_.store(true, std::memory_order_release);
            if (!queue_.empty() || !running_.load(std::memory_order_acquire)) {
                consumerParked_.store(false, std::memory_order_release);
                continue;
            }
            ::WaitForSingleObject(wakeEvent_.get(), INFINITE);
            consumerParked_.store(false, std::memory_order_release);
            ::ResetEvent(wakeEvent_.get());
        }
    }

    // Thread-affine teardown (COM/TSF objects live on this thread).
    if (finalizer_) { finalizer_(); }
}

//===========================================================================
// Producer side — the ONLY code that runs inside the hook callback.
//===========================================================================
bool ModernKeyHook::enqueue(const KeyEvent& ev) noexcept {
    stats_.pushed.fetch_add(1, std::memory_order_relaxed);
    if (queue_.try_push(ev)) {
        // v3.4 (S4) parked-flag wake protocol: only signal when the consumer
        // is (possibly) BLOCKED in WaitForSingleObject. While the consumer is
        // in its adaptive spin window it polls the ring directly, so the
        // SetEvent syscall is pure overhead — during a burst this removes one
        // kernel transition per keystroke from the hook callback.
        //
        // Lost-wakeup analysis:
        //   consumer: parked=true → re-check queue → (non-empty? unpark, drain)
        //             → WaitForSingleObject(wakeEvent_)
        //   producer: try_push → if (parked) SetEvent.
        //   * push before parked=true      → consumer's re-check sees it ✓
        //   * push after parked=true       → producer sees parked → SetEvent ✓
        //   * push during the re-check gap → re-check is AFTER parked=true, so
        //     the push either sees parked=true (SetEvent) or the re-check
        //     itself finds the item (unpark) ✓
        if (consumerParked_.load(std::memory_order_acquire)) {
            ::SetEvent(wakeEvent_.get());
        }
        clearSaturation();
        return true;
    }

    // Saturated. Policy: never block the hook; drop oldest by draining one
    // slot (keeps newest, preserves order), or drop this event.
    stats_.droppedOverflow.fetch_add(1, std::memory_order_relaxed);
    noteSaturation();
    if (overflowPolicy_.load(std::memory_order_relaxed) == OverflowPolicy::DropOldest) {
        KeyEvent discard;
        static_cast<void>(queue_.try_pop(discard));   // remove oldest, then retry once
        if (queue_.try_push(ev)) {
            if (consumerParked_.load(std::memory_order_acquire)) {
                ::SetEvent(wakeEvent_.get());
            }
            return true;
        }
    }
    return false;                  // DropNewest (default): input dropped, counted
}

//---------------------------------------------------------------------------
// v1.1.0 saturation watchdog.
//
// `kMaxAcceptableHookLatencyNs` above documents the budget: the input ring may
// fill up for a keystroke or two under a burst (the consumer catches up within
// microseconds and nothing is lost beyond the counted drop), but a queue that
// stays full for a fifth of a second means the consumer is genuinely stuck —
// e.g. a synchronous TSF edit session inside an application that stopped
// pumping. That is exactly the condition that used to be invisible in the
// diagnostics (only a slowly growing "dropped" counter). It is now measured as
// a distinct run with its own worst-case duration.
//---------------------------------------------------------------------------
void ModernKeyHook::noteSaturation() noexcept {
    const std::uint64_t now = qpcNow();
    const std::uint64_t start = satStartQpc_.load(std::memory_order_relaxed);
    if (start == 0) {                       // first drop of a new window
        satStartQpc_.store(now, std::memory_order_relaxed);
        return;
    }
    // Saturating arithmetic: (now - start) is correct across any QPC wrap
    // within the 64-bit counter, and the division cannot overflow because the
    // elapsed ticks of a real stall are orders of magnitude below 2^64/1e9.
    const std::uint64_t elapsedNs =
        (now - start) * 1'000'000'000ULL / (qpcFreq() != 0 ? qpcFreq() : 1);
    if (elapsedNs < kMaxAcceptableHookLatencyNs) { return; }

    saturationRuns_.fetch_add(1, std::memory_order_relaxed);
    const std::int64_t us = static_cast<std::int64_t>(elapsedNs / 1'000ULL);
    std::int64_t peak = peakSaturationUs_.load(std::memory_order_relaxed);
    while (us > peak &&
           !peakSaturationUs_.compare_exchange_weak(peak, us, std::memory_order_relaxed)) {}
    satStartQpc_.store(0, std::memory_order_relaxed);   // next run counts fresh
}

void ModernKeyHook::clearSaturation() noexcept {
    // Hot path: one relaxed load, and a store only when a window is open.
    if (satStartQpc_.load(std::memory_order_relaxed) != 0) {
        satStartQpc_.store(0, std::memory_order_relaxed);
    }
}

void ModernKeyHook::applyModifierDelta(const KeyEvent& ev) noexcept {
    const std::uint32_t mask = modifierMaskForVk(ev.vkCode);
    if (!mask) { return; }
    std::uint32_t bits = modifierBits_.load(std::memory_order_relaxed);
    if (ev.action == KeyAction::KeyUp || ev.action == KeyAction::SysKeyUp) {
        bits &= ~mask;
    } else {
        bits |= mask;
    }
    modifierBits_.store(bits, std::memory_order_relaxed);
}

// ---- suppressed-KeyDown bitmap (hook-thread-affine) -----------------------
bool ModernKeyHook::wasSuppressedDown(std::uint32_t vk) const noexcept {
    if (vk >= 256) { return false; }
    return (suppressedDown_[vk >> 5] & (1u << (vk & 31u))) != 0;
}
void ModernKeyHook::setSuppressedDown(std::uint32_t vk) noexcept {
    if (vk >= 256) { return; }
    suppressedDown_[vk >> 5] |= (1u << (vk & 31u));
}
void ModernKeyHook::clearSuppressedDown(std::uint32_t vk) noexcept {
    if (vk >= 256) { return; }
    suppressedDown_[vk >> 5] &= ~(1u << (vk & 31u));
}

//---------------------------------------------------------------------------
// WH_KEYBOARD_LL
//---------------------------------------------------------------------------
LRESULT CALLBACK ModernKeyHook::keyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    ModernKeyHook* self = g_instance;
    if (nCode < 0 || self == nullptr) {
        return ::CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // v3.3.1 self-healing heartbeat stamp: EVERY event this hook sees
    // (physical, third-party-injected, our own) refreshes the tick BEFORE
    // any filtering. The watchdog treats "system saw input that we did not"
    // as a silent unhook; a fresh stamp here keeps healthy states quiet.
    self->lastKbEventTickMs_.store(static_cast<std::uint32_t>(::GetTickCount()),
                                   std::memory_order_relaxed);

    const auto* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);

    // Filter our own injected events FIRST (magic extra-info), as legacy did.
    if (kb->dwExtraInfo == kSelfInjectedExtraInfo) {
        return ::CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    KeyEvent ev{};
    ev.timestampQpc = qpcNow();
    ev.vkCode       = static_cast<std::uint32_t>(kb->vkCode);
    ev.scanCode     = static_cast<std::uint32_t>(kb->scanCode);
    ev.wParam       = static_cast<std::uint32_t>(wParam);
    ev.extraInfo    = static_cast<std::uint64_t>(kb->dwExtraInfo);
    ev.extended     = (kb->flags & LLKHF_EXTENDED) != 0;
    ev.injected     = (kb->flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) != 0;
    ev.altDown      = (kb->flags & LLKHF_ALTDOWN) != 0;
    ev.source       = EventSource::Keyboard;

    switch (wParam) {
        case WM_KEYDOWN:    ev.action = KeyAction::KeyDown;    break;
        case WM_KEYUP:      ev.action = KeyAction::KeyUp;      break;
        case WM_SYSKEYDOWN: ev.action = KeyAction::SysKeyDown; break;
        case WM_SYSKEYUP:   ev.action = KeyAction::SysKeyUp;   break;
        default:
            return ::CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // Snapshot modifiers for the event BEFORE applying its own delta.
    const std::uint32_t bits = self->modifierBits_.load(std::memory_order_relaxed);
    ev.modifiers = ModifierState{
        static_cast<bool>(bits & kMaskShift), static_cast<bool>(bits & kMaskCtrl),
        static_cast<bool>(bits & kMaskAlt),   static_cast<bool>(bits & kMaskWin),
        static_cast<bool>(bits & kMaskCaps),  static_cast<bool>(bits & kMaskNum),
        static_cast<bool>(bits & kMaskScroll)};

    self->applyModifierDelta(ev);          // then advance the tracker

    // Phantom-KeyUp guard: when we suppressed a key's KeyDown, the app must
    // never receive the matching KeyUp either (a KeyUp without a KeyDown is a
    // stuck/ghost key to several apps). Suppress it here, and forget the
    // tracked vk so a later real press of the same key is unaffected.
    const bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
    if (isKeyUp && self->wasSuppressedDown(ev.vkCode)) {
        self->clearSuppressedDown(ev.vkCode);
        return 1;                            // swallow the matching KeyUp too
    }

    // Producer-side decision BEFORE enqueue: the IME engine may swallow this
    // key so the app never sees it (e.g. the "s" of "as"), and it must push
    // any output item BEFORE the KeyEvent is enqueued — otherwise the
    // consumer could drain the output ring before the item exists (the last
    // suppressed key of a word would never be emitted). Must be fast.
    const ProducerDecision d = self->producerHandler_
        ? self->producerHandler_(ev) : ProducerDecision{};

    // Remember suppressed KeyDowns so their KeyUp can be swallowed above.
    // Modifiers are never suppressed (the producer handler returns PD{} for
    // them), so the bitmap stays clean.
    if (!isKeyUp && d.suppressKey) {
        self->setSuppressedDown(ev.vkCode);
    }

    // Enqueue ONLY when the consumer has queued work (an Edit/Resync/
    // ForegroundChanged item was pushed). Pass-through keys, KeyUps and
    // inline-mode corrections produce no consumer work: count them for the
    // diagnostics but skip the queue push + SetEvent syscall + consumer wake.
    // (The queue is the consumer's wake signal; the KeyEvent payload itself is
    // not read by the consumer handler.)
    if (d.wakeConsumer) { self->enqueue(ev); }
    else               { self->countPassThrough(ev); }

    if (d.suppressKey) { return 1; }         // key does not reach the app
    return ::CallNextHookEx(nullptr, nCode, wParam, lParam);
}

//---------------------------------------------------------------------------
// WH_MOUSE_LL — mouse activity = implicit word break (legacy used it too).
//---------------------------------------------------------------------------
LRESULT CALLBACK ModernKeyHook::mouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    ModernKeyHook* self = g_instance;
    if (nCode < 0 || self == nullptr) {
        return ::CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // v3.3.1 heartbeat stamp for ALL mouse events (moves included — they
    // are high-frequency and must keep the watchdog quiet during mouse-only
    // activity; GetLastInputInfo counts every move as system input).
    self->lastMouseEventTickMs_.store(static_cast<std::uint32_t>(::GetTickCount()),
                                      std::memory_order_relaxed);

    KeyEvent ev{};
    ev.timestampQpc = qpcNow();
    ev.source       = EventSource::Mouse;
    ev.vkCode       = 0;
    ev.wParam       = static_cast<std::uint32_t>(wParam);
    switch (wParam) {
        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN: case WM_NCXBUTTONDOWN:
        case WM_LBUTTONUP:   case WM_RBUTTONUP:   case WM_MBUTTONUP:
        case WM_XBUTTONUP:   case WM_NCXBUTTONUP: {
            // Mouse = implicit word break. The handler runs the engine's word
            // break and (TSF mode) queues a Resync; enqueue only when that
            // Resync actually landed in the out-ring.
            const ProducerDecision d = self->producerHandler_
                ? self->producerHandler_(ev) : ProducerDecision{};
            if (d.wakeConsumer) { self->enqueue(ev); }
            else               { self->countPassThrough(ev); }
            break;
        }
        default:
            break;
    }
    return ::CallNextHookEx(nullptr, nCode, wParam, lParam);
}

//---------------------------------------------------------------------------
// EVENT_SYSTEM_FOREGROUND — smart-switch / per-app rules are driven by this
// event, never by timer polling (zero idle CPU).
//---------------------------------------------------------------------------
void CALLBACK ModernKeyHook::winEventProc(HWINEVENTHOOK, DWORD ev, HWND hwnd,
                                          LONG idObj, LONG /*idChild*/, DWORD, DWORD) {
    ModernKeyHook* self = g_instance;
    if (self == nullptr) { return; }
    if (ev != EVENT_SYSTEM_FOREGROUND || idObj != OBJID_WINDOW || hwnd == nullptr) { return; }

    KeyEvent fg{};
    fg.timestampQpc = qpcNow();
    fg.source       = EventSource::ForegroundChanged;
    // USER/GDI handles are 32-bit significant on 64-bit Windows (sign-extended
    // 32-bit values, per the Win64 USER handle documentation): store the low
    // 32 bits explicitly so the narrowing is deliberate, not an accident
    // (C4244 under /W4 /WX). The stamp is diagnostics-only — smart-switch
    // re-queries the real focus through TSF (ITfThreadMgr::GetFocus), never
    // from this field.
    fg.wParam       = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(hwnd) & 0xFFFF'FFFFu);   // foreground HWND
    const ProducerDecision d = self->producerHandler_
        ? self->producerHandler_(fg) : ProducerDecision{};
    if (d.wakeConsumer) { self->enqueue(fg); }                  // serialized on hook pump thread
    else               { self->countPassThrough(fg); }
}

} // namespace ok::hook
