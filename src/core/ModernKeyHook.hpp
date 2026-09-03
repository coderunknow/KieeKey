//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.1.3 - refactored and completed logic
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
// File: src/core/ModernKeyHook.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.3 — ModernKeyHook.hpp
// Asynchronous, non-blocking low-level keyboard/mouse hook engine.
//
// Architecture (fixes every latency/ghosting flaw of the legacy OpenKey.cpp):
//
//   ┌──────────────────┐   try_push (O(1), lock-free)   ┌──────────────────┐
//   │   HOOK THREAD    │ ──────────────────────────────▶ │  CONSUMER THREAD │
//   │ installs hooks,  │      SPSC lock-free ring       │ drains queue,    │
//   │ pumps messages;  │                                │ runs TextEngine, │
//   │ LL callbacks run │ ◀─────────────────────────────  │ TSF composer,    │
//   │ here, serialized │   CallNextHookEx immediately   │ app exclusion…   │
//   └──────────────────┘                                └──────────────────┘
//
// The legacy code ran the ENTIRE text pipeline (SendInput, clipboard,
// broadcast WM_CHAR) inside the hook callback. That is why keystrokes were
// dropped/duplicated: SendInput round-trips + clipboard contention stalled
// the hook, and _syncKey bookkeeping desynchronized from reality.
//
// Here the callback is O(1): build a POD KeyEvent, try_push, return. All
// synthesis happens on the consumer thread; the hook thread is never blocked.
//
// Threading model:
//   * hookThread   — installs WH_KEYBOARD_LL + WH_MOUSE_LL + SetWinEventHook
//                    and pumps messages (required: LL hooks fire only on the
//                    installing thread's message loop). Sole producer.
//   * consumerThread — sole consumer; invokes EventHandler per drained event.
//   * stop() is safe from any thread and joins both threads.
//----------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <utility>

#include "LockFreeQueue.hpp"
#include "Win32RAII.hpp"

namespace ok::hook {

// Magic value stamped into INPUT.dwExtraInfo for events this process injects.
// The hook skips them (legacy code used `dwExtraInfo != 0`; a magic constant
// is stricter — it cannot accidentally filter third-party injected input).
inline constexpr ULONG_PTR kSelfInjectedExtraInfo = 0x0B00B1E5;

// ---------------------------------------------------------------------------
// KeyEvent — immutable POD payload carried through the ring buffer.
// Must stay trivially copyable (queue requirement).
// ---------------------------------------------------------------------------
enum class KeyAction : std::uint8_t { KeyDown, KeyUp, SysKeyDown, SysKeyUp };
enum class EventSource : std::uint8_t { Keyboard, Mouse, ForegroundChanged };

struct ModifierState {
    bool shift : 1;
    bool ctrl  : 1;
    bool alt   : 1;
    bool win   : 1;
    bool caps  : 1;
    bool num   : 1;
    bool scroll: 1;
};

struct KeyEvent {
    std::uint64_t  timestampQpc;      // QueryPerformanceCounter at capture
    std::uint32_t  vkCode;            // virtual-key code (VK_*)
    std::uint32_t  scanCode;          // hardware scan code
    std::uint32_t  wParam;            // original WM_KEYDOWN/WM_KEYUP/… (diagnostics)
    std::uint64_t  extraInfo;         // KBDLLHOOKSTRUCT::dwExtraInfo
    KeyAction      action;
    EventSource    source;
    ModifierState  modifiers;
    bool           extended : 1;      // KEYEVENTF_EXTENDEDKEY
    bool           injected  : 1;     // KEYEVENTF_INJECTED (from OS/other app)
    bool           altDown  : 1;      // keyboardData->flags & LLKHF_ALTDOWN

    [[nodiscard]] bool isSelfInjected() const noexcept {
        return extraInfo == static_cast<std::uint64_t>(kSelfInjectedExtraInfo);
    }
};

// ---------------------------------------------------------------------------
// Overflow policy for a saturated ring. Dropping is always the safe choice in
// a hook path; DropNewest preserves input order/causality (never reorders).
// ---------------------------------------------------------------------------
enum class OverflowPolicy { DropNewest, DropOldest };

// ---------------------------------------------------------------------------
// ModernKeyHook
// ---------------------------------------------------------------------------
class ModernKeyHook final {
public:
    using EventHandler = std::function<void(const KeyEvent&)>;

    // Optional PRODUCER-side decision callback, invoked synchronously inside
    // the hook callbacks (on the hook pump thread, serialized). Return true to
    // SUPPRESS the key (return 1 from the hook) — this is how a modern IME
    // prevents the raw keystroke from ever reaching the app (no "as" flash,
    // no double letters). The callback must be O(1)-ish: engine decision
    // only — never block, never send input here (that belongs on the consumer).
    //
    // wakeConsumer tells the hook whether the consumer thread has queued work
    // (an OutputItem / Resync / ForegroundChanged item was pushed to the
    // consumer's out-ring). Only then does the hook enqueue a KeyEvent +
    // SetEvent. Pass-through keys and inline-mode corrections (which emit
    // directly from the hook thread) produce NO consumer work and therefore
    // no wake — this removes a kernel signal + consumer spin/wake cycle from
    // the majority of key events (every KeyUp, every unconsumed key).
    struct ProducerDecision {
        bool suppressKey  = false;   // return 1 from the hook (app never sees the key)
        bool wakeConsumer = false;   // consumer has queued work to drain
    };
    using ProducerHandler = std::function<ProducerDecision(const KeyEvent&)>;

    ModernKeyHook() noexcept = default;
    ~ModernKeyHook() { stop(); }

    ModernKeyHook(const ModernKeyHook&)            = delete;
    ModernKeyHook& operator=(const ModernKeyHook&) = delete;

    // Start hook + consumer threads. `handler` runs on the consumer thread.
    // Returns false if hook installation failed (e.g. UIPI: caller lacks
    // permission to hook an elevated foreground window — not fatal, fall back
    // to polling mode or warn the user).
    [[nodiscard]] bool start(EventHandler handler);

    // Set/clear the producer-side decision callback (may be called before
    // start; the callback runs on the hook thread, so set it before start).
    void setProducerHandler(ProducerHandler h) noexcept { producerHandler_ = std::move(h); }
    [[nodiscard]] bool hasProducerHandler() const noexcept { return producerHandler_ != nullptr; }

    // Optional callback run ONCE on the consumer thread when it exits (after
    // the drain loop). Use it for thread-affine teardown: e.g. releasing COM
    // objects that were created there (CoInitializeEx'd STA objects must be
    // released on the same thread — releasing them from the main thread after
    // join is a COM apartment violation).
    using Finalizer = std::function<void()>;
    void setConsumerFinalizer(Finalizer f) noexcept { finalizer_ = std::move(f); }

    // Idempotent; joins threads; safe from any thread, may be called twice.
    void stop() noexcept;

    // v1.1.0 — true when stop() had to DETACH a wedged worker (bounded
    // budget expired). Mirrors ProcessMonitor::stuckThreadsDetached(): the
    // app uses it to skip static destruction (ExitProcess) so the detached
    // thread can never race object teardown.
    [[nodiscard]] bool stuckThreadsDetached() const noexcept { return stuckDetached_.load(std::memory_order_acquire); }

    [[nodiscard]] bool running() const noexcept { return running_.load(std::memory_order_acquire); }

    void setOverflowPolicy(OverflowPolicy p) noexcept { overflowPolicy_.store(p, std::memory_order_relaxed); }

    // Diagnostics (zero-cost: a few atomics)
    [[nodiscard]] std::uint64_t pushed()    const noexcept { return stats_.pushed.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t dropped()   const noexcept { return stats_.droppedOverflow.load(std::memory_order_relaxed); }
    // Median-free worst-case hook-to-consumer latency watermark, microseconds.
    [[nodiscard]] std::int64_t  peakLatencyUs() const noexcept { return peakLatencyUs_.load(std::memory_order_relaxed); }
    // v1.1.0: clear the watermark (the settings dialog resets it when opened
    // so the Diagnostics tab shows the CURRENT session's peak, not a stale
    // process-lifetime outlier).
    void resetPeakLatency() noexcept { peakLatencyUs_.store(0, std::memory_order_relaxed); }
    // Exponential moving average of hook→consumer latency (single writer on
    // the consumer thread; relaxed load is fine for telemetry).
    [[nodiscard]] std::int64_t  avgLatencyUs() const noexcept { return avgLatencyUs_.load(std::memory_order_relaxed); }

    //-------------------------------------------------------------------------
    // v3.3.1 — Hook Self-Healing support.
    //
    // Windows silently removes a low-level hook whose callback exceeds the
    // LowLevelHooksTimeout, and on desktop/UIPI transitions — WITHOUT the
    // hook handle becoming invalid, so nothing observable changes except
    // that the callbacks stop arriving. Detection: the LL callbacks stamp
    // a millisecond tick for EVERY event they see (before any filtering,
    // including our own injected events); the watchdog compares against the
    // system-wide last-input tick (GetLastInputInfo covers all input). A
    // system input newer than BOTH stamps (and not explained by our own
    // SendInput, stamped separately) means an LL hook is gone.
    //-------------------------------------------------------------------------
    [[nodiscard]] std::uint32_t lastKbEventTickMs()  const noexcept { return lastKbEventTickMs_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint32_t lastMouseEventTickMs() const noexcept { return lastMouseEventTickMs_.load(std::memory_order_relaxed); }
    // Reference accessors (v3.3.1): the self-healing watchdog (Win32Wrapper
    // -> HookWatchdog, win32_wrapper.hpp) POLLS these atomics from the
    // pump-tick heartbeat and therefore needs stable references, which the
    // by-value getters above cannot provide. Exposing const references keeps
    // proper encapsulation — outside code can read but never write; only the
    // LL callbacks (pump thread) store ticks into the members themselves.
    [[nodiscard]] const std::atomic<std::uint32_t>& kbEventTickRef()    const noexcept { return lastKbEventTickMs_; }
    [[nodiscard]] const std::atomic<std::uint32_t>& mouseEventTickRef() const noexcept { return lastMouseEventTickMs_; }
    // Count of successful in-place hook reinstalls (watchdog recoveries).
    [[nodiscard]] std::uint64_t hookReinstallCount() const noexcept { return hookReinstallCount_.load(std::memory_order_relaxed); }

    // Pump-tick callback (the watchdog heartbeat). Runs ON the pump thread
    // when the pump's WM_TIMER fires (armed in hookThreadMain when this is
    // set before start()). The callback must be cheap (it runs in the pump);
    // never pump/block inside it.
    using PumpTick = std::function<void()>;
    void setPumpTick(PumpTick t) noexcept { pumpTick_ = std::move(t); }
    // WM_TIMER period for the pump tick, milliseconds (0 disarms). Set
    // BEFORE start(). 5 ms + timeBeginPeriod(1) meets the 5–15 ms
    // re-establishment requirement.
    void setPumpTickIntervalMs(std::uint32_t ms) noexcept { pumpTickIntervalMs_.store(ms, std::memory_order_relaxed); }

    // Re-install WH_KEYBOARD_LL / WH_MOUSE_LL / the foreground WinEvent hook
    // IN PLACE on the pump thread. Public for the self-healing watchdog —
    // MUST be called from the pump thread only (LL hooks are thread-affine;
    // the watchdog enforces this by calling from its WM_TIMER tick).
    // Returns true when the keyboard hook is installed again.
    [[nodiscard]] bool reinstallHooksOnPump() noexcept;

    //-------------------------------------------------------------------------
    // v1.1.0 — re-seed the delta-tracked modifier state (shift/ctrl/alt/win +
    // caps/num/scroll toggles) from the REAL OS key state (GetAsyncKeyState).
    //
    // A low-level hook receives no events while a UAC / secure desktop /
    // elevated window is foreground (UIPI), across RDP transitions, and when
    // another application's hook swallows modifier key-ups or injects
    // CapsLock toggles. In all of those windows the delta tracker misses
    // key-ups and its shift/caps bits go STALE while the system moved on.
    // The stale bits feed layoutChar()/produceChar(), so the engine's word
    // buffer disagrees in CASE with the visible text: pass-through letters
    // render with the true system case, but the next tone-mark / đ / â
    // transform re-emits the letter from the buffer with the stale case
    // (lowercase typing → "vợ" appearing as "vỢ"). Call this on every
    // foreground change, and when the IME is re-enabled from the in-app
    // on/off switch (v1.1.1), so a stale window can never compose wrong-case
    // output.
    //
    // Threading: hook pump thread (the producer handler runs there,
    // serialized with the LL callbacks and applyModifierDelta). v1.1.1: the
    // UI thread may also call this when re-enabling from the in-app switch —
    // the implementation is a single atomic store, so it is safe from any
    // thread.
    //-------------------------------------------------------------------------
    void resyncModifiersFromOs() noexcept;

private:
    static constexpr std::size_t kQueueCapacity = 4096;   // 4096 * ~64B ≈ 256 KiB
    using Queue = ok::lockfree::SPSCRing<KeyEvent, kQueueCapacity>;

    // --- static trampolines (Windows callback signatures) ---
    static LRESULT CALLBACK keyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK mouseProc(int nCode, WPARAM wParam, LPARAM lParam);
    static void CALLBACK winEventProc(HWINEVENTHOOK h, DWORD ev, HWND hwnd,
                                      LONG idObj, LONG idChild, DWORD idThread, DWORD ms);

    void hookThreadMain() noexcept;      // installs hooks + pumps messages
    void consumerThreadMain() noexcept;  // drains ring, dispatches handler

    // Capture modifier state: maintained incrementally from the key stream
    // (zero per-event API cost) and seeded from GetKeyState at hook install.
    void applyModifierDelta(const KeyEvent& ev) noexcept;

    // Suppressed-KeyDown bitmap (hook-thread-affine; see keyboardProc).
    [[nodiscard]] bool wasSuppressedDown(std::uint32_t vk) const noexcept;
    void setSuppressedDown(std::uint32_t vk) noexcept;
    void clearSuppressedDown(std::uint32_t vk) noexcept;

    bool enqueue(const KeyEvent& ev) noexcept;   // applies overflow policy
    // Count an event that produced no consumer work (never queued): keeps the
    // pushed() diagnostics counter accurate without a SetEvent syscall.
    void countPassThrough(const KeyEvent& ev) noexcept {
        static_cast<void>(ev);
        stats_.pushed.fetch_add(1, std::memory_order_relaxed);
    }

    // ---- state (all cross-thread fields are atomic) ----
    std::atomic<bool> running_{false};
    std::atomic<OverflowPolicy> overflowPolicy_{OverflowPolicy::DropNewest};
    ok::lockfree::QueueStats stats_;
    std::atomic<std::int64_t> peakLatencyUs_{0};
    std::atomic<std::int64_t> avgLatencyUs_{0};
    // v3.4 (S4): consumer parked flag — see enqueue(). Set before the
    // consumer blocks on wakeEvent_, cleared after wake/re-check. Lets the
    // producer skip the SetEvent syscall for every key that arrives while
    // the consumer is spinning (the entire hot-burst population).
    std::atomic<bool> consumerParked_{false};

    ok::win32::EventHandle wakeEvent_;           // signaled to wake consumer
    ok::win32::HookHandle   keyboardHook_;
    ok::win32::HookHandle   mouseHook_;
    ok::win32::WinEventHook fgWinEvent_;

    std::thread hookThread_;
    std::thread consumerThread_;
    EventHandler handler_;
    ProducerHandler producerHandler_;
    Finalizer finalizer_;   // runs on the consumer thread at exit

    // The lock-free ring itself: sole producer = hook pump thread,
    // sole consumer = consumer thread. False-sharing isolated via padding.
    alignas(ok::lockfree::cacheline) Queue queue_;

    // v3.5 reliability — exit acknowledgements for the BOUNDED shutdown in
    // stop(). The consumer can wedge inside a synchronous TSF edit session
    // issued at a hung application's marshaled context (RequestEditSession
    // TF_ES_SYNC only completes when the target pumps again); join()ing
    // such a thread hangs the UI thread forever and leaves a zombie process
    // holding the single-instance mutex. The flags let stop() distinguish
    // "exited" from "stuck" and detach() the stuck thread instead; the OS
    // reaps it when the process exits.
    std::atomic<bool> hookPumpExited_{false};
    std::atomic<bool> consumerExited_{false};
    // v1.1.0: published by stop() when a wedged worker had to be detached
    // (see stuckThreadsDetached()).
    std::atomic<bool> stuckDetached_{false};

    // Hook pump thread id, captured inside hookThreadMain (portable: works on
    // MSVC AND MinGW, where std::thread::native_handle() is pthread_t, not a
    // HANDLE — GetThreadId would be invalid there). Used to wake the pump on
    // stop() via PostThreadMessage(WM_QUIT).
    std::atomic<DWORD> hookThreadId_{0};

    // producer/consumer-shared read-mostly data
    alignas(ok::lockfree::cacheline) std::atomic<std::uint32_t> modifierBits_{0};

    // vk codes whose KeyDown the producer decided to SUPPRESS. The matching
    // KeyUp must also be suppressed (return 1) so the application never sees
    // a KeyUp without a matching KeyDown — several apps (terminals, games,
    // some edit controls) treat that phantom KeyUp as a stuck/ghost key,
    // which is part of the "typing fast and deleting leaves ghost characters"
    // symptom. Hook-thread-affine (LL hooks fire only on the installing
    // thread): no atomics needed.
    std::array<std::uint32_t, 8> suppressedDown_{};   // 256-bit bitmap (vk 0..255)

    // v1.1.3 — EDGE-DETECT state for the toggle keys (CapsLock/NumLock/
    // ScrollLock). The tracker previously XORed the toggle bit on EVERY
    // KeyDown, but a low-level hook receives auto-repeat WM_KEYDOWNs while
    // the key is held (KBDLLHOOKSTRUCT carries no repeat flag): holding
    // CapsLock for half a second XORed the tracked bit ~15-30 times and left
    // it OUT OF PHASE with the real OS toggle state — every subsequent tone
    // mark / đ / â transform then re-emitted its word with the wrong case
    // ("vợ" rendering as "vỢ"). The bitmap (hook-thread-affine, same trick
    // as suppressedDown_) flips the bit only on the up→down TRANSITION, so
    // repeats are ignored and the tracked level always matches the OS.
    std::array<std::uint32_t, 8> toggleKeyDown_{};    // 256-bit bitmap (vk 0..255)
    [[nodiscard]] bool wasToggleKeyDown(std::uint32_t vk) const noexcept;
    void setToggleKeyDown(std::uint32_t vk) noexcept;
    void clearToggleKeyDown(std::uint32_t vk) noexcept;

    // v3.3.1 self-healing state. The event stamps are written by the LL
    // callbacks (hook thread) and read by the watchdog (pump tick — same
    // thread, but std::atomic keeps the contract explicit and race-free).
    static constexpr UINT_PTR kSelfHealTimerId = 0x0B00B;   // thread timer id
    std::atomic<std::uint32_t> lastKbEventTickMs_{0};    // stamped per LL keyboard event
    std::atomic<std::uint32_t> lastMouseEventTickMs_{0}; // stamped per LL mouse event
    std::atomic<std::uint32_t> pumpTickIntervalMs_{0};   // 0 = no watchdog tick
    std::atomic<std::uint64_t> hookReinstallCount_{0};
    PumpTick pumpTick_;                                  // runs on the pump thread
};

} // namespace ok::hook
