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
// File: src/app/main.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.0.1 — src/app/main.cpp
// The OpenKey-style Windows tray application (replaces the pre-tray console host):
//
//   * System tray icon (green = Vietnamese ON, gray = OFF) with the classic
//     right-click menu:  Bật gõ tiếng Việt (checked) / Phương thức gõ
//     (Telex, VNI, Simple Telex) / Cài đặt… / Thoát.
//   * Double-click the tray icon → Cài đặt dialog (Win32, Vietnamese UI)
//     with tabs: Bàn phím / Ứng dụng / Chẩn đoán (live latency telemetry).
//   * Global hotkey Ctrl+Shift toggles Vietnamese input on/off
//     (debounced; same default as the original OpenKey).
//
// Input pipeline (fixed since the pre-tray console build):
//   WH_KEYBOARD_LL (ModernKeyHook, async lock-free ring)
//     └─ producer decision on the hook thread:  TextEngine runs HERE so the
//        engine can SUPPRESS consumed keys (return 1) — the app never sees
//        the raw "s" of "as" (no flash, no double letters, no cursor jump).
//        Only the final EditAction (delete N + insert UTF-16) is enqueued.
//     └─ consumer thread (hook-owned): drains EditActions → TsfComposer
//        (ITfEditSession on the app's text store) with a SendInput(UNICODE)
//        last-resort fallback. Zero synthetic backspaces on the TSF path.
//   ProcessMonitor auto-exclusion (IDEs / fullscreen games / shell — each
//   configurable from the dialog), event-driven, zero idle CPU.
//
// Settings persisted to HKCU\Software\TuyenMai\OpenKey (same key as 2.0.5).
//
// Build (MinGW x64):
//   x86_64-w64-mingw32-windres -c 65001 -O coff src/app/KieeKeyApp.rc -o src/app/app_res.o
//   x86_64-w64-mingw32-g++ -std=c++23 -O2 -mwindows -municode
//     -finput-charset=UTF-8 -fexec-charset=UTF-8 -fwide-exec-charset=UTF-16LE
//     -I src/core -I src/tsf src/app/main.cpp
//     src/core/ModernKeyHook.cpp src/core/ProcessMonitor.cpp
//     src/core/TextEngine.cpp src/tsf/TsfComposer.cpp src/app/app_res.o
//     -o KieeKey.exe -luser32 -lgdi32 -lshell32 -lole32
//     -lcomctl32 -ldwmapi -lpsapi -lversion -lurlmon
//----------------------------------------------------------------------------
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>   // Shell_NotifyIcon / NOTIFYICONDATA (excluded by LEAN_AND_MEAN)
#include <timeapi.h>    // timeBeginPeriod/timeEndPeriod (winmm — already linked)

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "CtrlShiftChord.hpp"
#include "LockFreeQueue.hpp"
#include "ModernKeyHook.hpp"
#include "ProcessMonitor.hpp"
#include "Profiler.hpp"
#include "TextEngine.hpp"
#include "TsfComposer.hpp"
#include "Win32RAII.hpp"
#include "win32_wrapper.hpp"   // v3.3.1: pipeline (OutputRing 1024, batched
                               // SendInput emitter, self-healing hook wrapper)

#include "resource.h"

using namespace ok::hook;
using namespace ok::text;
using ok::monitor::ProcessMonitor;
using ok::tsf::TsfComposer;

namespace {

//===========================================================================
// Output item: what the consumer thread must emit (trivially copyable → can
// ride the lock-free SPSC ring).
//
// v3.3.1: OutputItem + the producer→consumer ring moved to the wrapper layer
// (ok::wrap::OutputItem / ok::wrap::OutputRing — the Vyukov SPSC ring at
// capacity 1024, up from the app-local 512). Aliased here so every existing
// call site keeps compiling unchanged.
//===========================================================================
using OutputItem = ok::wrap::OutputItem;
using OutputRing = ok::wrap::OutputRing;
using ok::wrap::kRingTextCap;

//===========================================================================
// Global application state (single instance).
//===========================================================================
struct AppState {
    HINSTANCE hInst      = nullptr;
    HWND      hMain      = nullptr;   // hidden message window
    HWND      hSettings  = nullptr;   // settings dialog (nullable)
    HICON     hIconOn    = nullptr;
    HICON     hIconOff   = nullptr;

    ok::wrap::Win32Wrapper hook;   // v3.3.1: hook + rings + batched emitter +
                                   // self-healing watchdog (same surface as
                                   // ModernKeyHook — drop-in)
    ProcessMonitor monitor;
    TsfComposer    composer;
    TextEngine     engine;
    std::mutex     engineMtx;         // guards engine (producer thread vs dialog)
    OutputRing     outRing;           // producer → consumer

    std::atomic<bool> engineEnabled{true};
    ok::hotkey::CtrlShiftChord ctrlShiftChord;   // hook-thread-affine
    std::atomic<bool> composerAttached{false};
    std::atomic<bool> balloonShown{false};
    // Cached auto-exclusion decision (updated on foreground change only —
    // keeps the per-key hot path to a single relaxed atomic load).
    std::atomic<bool> fgExcluded_{false};
    // Cached per-foreground output policy: true → TSF commit (flicker-prone
    // apps), false → inline SendInput (zero-latency, original-OpenKey style).
    // Also updated when the user changes the output mode.
    std::atomic<bool> fgUseTsf_{false};
    // WPM gauge: printable keydowns the engine actually processed.
    std::atomic<std::uint64_t> keysTyped{0};

    // Number of Edit items currently sitting in outRing that the consumer has
    // not yet applied. Incremented by the producer (TSF path, after a
    // successful outRing push), decremented by the consumer after it applies
    // the edit (commit or fallback). Used by the ordering barrier: a
    // pass-through key must not reach the application while edits are pending,
    // otherwise the app's text gets ahead of the engine's buffer and the next
    // edit's backspace deletes the wrong characters (the "ghosting/sticking"
    // when typing and deleting quickly).
    std::atomic<std::uint32_t> pendingEdits{0};

    // v3.4 (S1): event-driven ordering barrier. waitPendingEditsDrained()
    // spins ~2 µs, then waits on this barrier's auto-reset event — the
    // consumer signals it whenever pendingEdits transitions to 0 — hard-
    // capped at 1 ms. Replaces the v3.3.1 2 ms busy-spin that stalled the
    // hook thread (and on ≤2-core hosts stole the consumer's core).
    ok::wrap::EditDrainBarrier drainBarrier;

    // v3.4 (S2): inline output policy. Default stays hook-inline (zero
    // consumer hop — the frozen 1.055 µs burst p50 on quiet real hardware
    // covers the ENTIRE chain including the in-callback SendInput).
    // OPENKEY_INLINE_MODE=deferred moves inline edits onto the consumer
    // thread (OutputItem::Kind::InlineEdit) for hosts where in-callback
    // SendInput serialization is a concern; costs one ring hop + wake.
    bool inlineDeferred = false;

    // output mode: 0=Auto, 1=Always TSF, 2=Always SendInput (inline)
    std::atomic<int> outputMode{0};

    // Hook-thread-affine scratch (never touched from other threads):
    std::atomic<HKL> currentHkl{nullptr};  // cached foreground keyboard layout
    std::wstring repScratch;               // replacementUtf16 scratch (engineMtx-guarded)

    // settings (GUI mirror; engine holds its own copy under engineMtx)
    EngineOptions options;
    bool exclIde   = true;
    bool exclGame  = true;
    bool exclShell = false;
};
AppState g;

//===========================================================================
// Settings persistence (HKCU\Software\TuyenMai\OpenKey — same key as 2.0.5)
//===========================================================================
ok::win32::RegistryKey settingsKey() { return ok::win32::RegistryKey::openAppKey(true); }

void loadSettings() {
    if (auto key = settingsKey(); key) {
        const DWORD m = key.getDword(L"InputMethod", 0);
        if (m <= 2) { g.options.inputMethod = static_cast<InputMethod>(m); }
        const DWORD c = key.getDword(L"CodeTable", 0);
        if (c <= 4) { g.options.codeTable = static_cast<CodeTable>(c); }
        g.options.checkSpelling            = key.getDword(L"CheckSpelling", 1) != 0;
        g.options.useMacro                 = key.getDword(L"UseMacro", 1) != 0;
        g.options.restoreIfWrongSpelling   = key.getDword(L"RestoreIfWrong", 1) != 0;
        g.options.upperCaseFirstChar       = key.getDword(L"UpperCaseFirst", 0) != 0;
        g.options.useModernOrthography     = key.getDword(L"ModernOrthography", 0) != 0;
        g.options.quickTelex               = key.getDword(L"QuickTelex", 0) != 0;
        g.exclIde                          = key.getDword(L"ExcludeIde", 1) != 0;
        g.exclGame                         = key.getDword(L"ExcludeGame", 1) != 0;
        g.exclShell                        = key.getDword(L"ExcludeShell", 0) != 0;
        const DWORD om = key.getDword(L"OutputMode", 0);
        if (om <= 2) { g.outputMode.store(static_cast<int>(om), std::memory_order_relaxed); }
    }
}

void saveSettings() {
    if (auto key = settingsKey(); key) {
        key.setDword(L"InputMethod",       static_cast<DWORD>(g.options.inputMethod));
        key.setDword(L"CodeTable",         static_cast<DWORD>(g.options.codeTable));
        key.setDword(L"CheckSpelling",     g.options.checkSpelling ? 1 : 0);
        key.setDword(L"UseMacro",          g.options.useMacro ? 1 : 0);
        key.setDword(L"RestoreIfWrong",    g.options.restoreIfWrongSpelling ? 1 : 0);
        key.setDword(L"UpperCaseFirst",    g.options.upperCaseFirstChar ? 1 : 0);
        key.setDword(L"ModernOrthography", g.options.useModernOrthography ? 1 : 0);
        key.setDword(L"QuickTelex",        g.options.quickTelex ? 1 : 0);
        key.setDword(L"ExcludeIde",        g.exclIde ? 1 : 0);
        key.setDword(L"ExcludeGame",       g.exclGame ? 1 : 0);
        key.setDword(L"ExcludeShell",      g.exclShell ? 1 : 0);
        key.setDword(L"OutputMode",        static_cast<DWORD>(g.outputMode.load(std::memory_order_relaxed)));
    }
}

//===========================================================================
// Key → produced character (US layout — the classic Vietnamese keyboard)
//===========================================================================
char32_t produceChar(std::uint32_t vk, bool shift, bool capsLock) noexcept {
    if (vk >= 'A' && vk <= 'Z') {
        const bool upper = shift != capsLock;
        return static_cast<char32_t>(upper ? vk : (vk + ('a' - 'A')));
    }
    if (vk >= '0' && vk <= '9') {
        static constexpr char32_t kShifted[] = {')','!','@','#','$','%','^','&','*','('};
        return shift ? kShifted[vk - '0'] : static_cast<char32_t>(vk);
    }
    switch (vk) {
        case VK_SPACE:   return U' ';
        case VK_OEM_1:   return shift ? U':' : U';';
        case VK_OEM_PLUS:   return shift ? U'+' : U'=';
        case VK_OEM_COMMA:  return shift ? U'<' : U',';
        case VK_OEM_MINUS:  return shift ? U'_' : U'-';
        case VK_OEM_PERIOD: return shift ? U'>' : U'.';
        case VK_OEM_2:      return shift ? U'?' : U'/';
        case VK_OEM_3:      return shift ? U'~' : U'`';
        case VK_OEM_4:      return shift ? U'{' : U'[';
        case VK_OEM_5:      return shift ? U'|' : U'\\';
        case VK_OEM_6:      return shift ? U'}' : U']';
        case VK_OEM_7:      return shift ? U'"' : U'\'';
        case VK_OEM_102:    return shift ? U'|' : U'\\';
        default:            return 0;
    }
}

bool isWordBreakVk(std::uint32_t vk) noexcept {
    switch (vk) {
        case 0x1B: case 0x09: case 0x0D:
        case 0x25: case 0x26: case 0x27: case 0x28:
        case 0x24: case 0x23: case 0x2D: case 0x2E:
        case 0x21: case 0x22:
        case 0x2C: case 0x2A: case 0x29: case 0x2F:
        case 0x2B: case 0x90: case 0x91:
            return true;
        default: return false;
    }
}

bool isModifierVk(std::uint32_t vk) noexcept {
    switch (vk) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN:
        case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
        case VK_PACKET:
            return true;
        default: return false;
    }
}

//===========================================================================
// Keyboard-layout-aware character production.
// Uses the REAL foreground keyboard layout (ToUnicodeEx) so non-US layouts
// (AZERTY, QWERTZ, Vietnamese, …) work correctly — the legacy hardcoded US
// map was a genuine edge-case gap. Falls back to produceChar() (US map)
// when the layout returns nothing (e.g. pure modifier or unmapped key).
// HKL is cached per foreground change; this function is hook-thread only
// (ToUnicodeEx keeps dead-key state per layout, so callers must be serial).
//===========================================================================
char32_t layoutChar(std::uint32_t vk, std::uint32_t scan, const ModifierState& mods) noexcept {
    BYTE kb[256] = {};
    kb[VK_SHIFT]   = mods.shift ? 0x80 : 0;
    kb[VK_CONTROL] = mods.ctrl  ? 0x80 : 0;
    kb[VK_MENU]    = mods.alt   ? 0x80 : 0;
    kb[VK_CAPITAL] = mods.caps  ? 0x01 : 0;
    kb[VK_NUMLOCK] = mods.num   ? 0x01 : 0;

    const HKL hkl = g.currentHkl.load(std::memory_order_relaxed);
    wchar_t buf[8];
    int n = ::ToUnicodeEx(static_cast<UINT>(vk), static_cast<UINT>(scan), kb,
                          buf, 8, 0, hkl);
    if (n < 0) {
        // Dead key: swallow the pending dead state; the second call yields
        // the dead character itself (so typing '^' alone still works).
        n = ::ToUnicodeEx(static_cast<UINT>(vk), static_cast<UINT>(scan), kb,
                          buf, 8, 0, hkl);
        if (n < 0) { n = 0; }
    }
    if (n <= 0) { return 0; }
    return static_cast<char32_t>(buf[0]);
}

//===========================================================================
// Per-app output policy.
//   Auto (0): TSF for flicker-prone apps (browsers + Office family — the
//             exact set the v2.0.5→v3.0 requirement flagged for flicker),
//             inline SendInput for everything else (zero added latency).
//   TSF (1):  always compose via TSF edit session.
//   Send (2): always inline SendInput — the original OpenKey behavior,
//             fastest possible, at the cost of visible delete+reinsert in
//             flicker-prone apps.
//===========================================================================
bool isFlickerProne(ok::monitor::ProcessClass kind, const std::string& exeLower) noexcept {
    if (kind == ok::monitor::ProcessClass::Browser) { return true; }
    static constexpr std::string_view kOffice[] = {
        "winword.exe", "excel.exe", "powerpnt.exe", "outlook.exe",
        "onenote.exe", "msaccess.exe", "visio.exe",
        "wps.exe", "et.exe", "wpp.exe",   // WPS Office
    };
    for (const auto& name : kOffice) {
        if (exeLower == name) { return true; }
    }
    return false;
}

void updateForegroundPolicy() noexcept {
    const auto s = g.monitor.snapshot();
    const int mode = g.outputMode.load(std::memory_order_relaxed);
    switch (mode) {
        case 1: g.fgUseTsf_.store(true, std::memory_order_relaxed);  break;
        case 2: g.fgUseTsf_.store(false, std::memory_order_relaxed); break;
        default:
            g.fgUseTsf_.store(s && isFlickerProne(s->kind, s->exeNameLower),
                              std::memory_order_relaxed);
            break;
    }
    // Refresh the cached keyboard layout for ToUnicodeEx.
    g.currentHkl.store((s && s->hwnd)
        ? ::GetKeyboardLayout(::GetWindowThreadProcessId(s->hwnd, nullptr))
        : ::GetKeyboardLayout(0), std::memory_order_relaxed);
}

// Inline zero-latency output (hook thread). Exactly what original OpenKey
// did: backspaces + Unicode text queued straight into the input stream.
// Normal edits fit one SendInput call (backspace ≤ kMaxBuff=32 — the D2
// engine policy — and replacement ≤ 2*kMaxBuff wchar_t): 32*2 + 64*2 INPUTs
// ≈ 5.4 KiB on the stack, zero heap. Macro expansions (D3) can be longer —
// the loop below flushes the batch and keeps going, so ANY payload size is
// safe (chunked, still ordered, still self-tagged).
inline constexpr std::size_t kMaxInlineInputs = kMaxBuff * 2 + 2 * kMaxBuff * 2 + 4;
void sendBackspaces(std::size_t n) noexcept;    // defined below (fallback output)
void sendUnicodeText(const std::wstring& text) noexcept;

//===========================================================================
// v3.3.1: the batched inline emitter moved to the wrapper layer
// (ok::wrap::InlineEmitter): ONE SendInput call per edit — backspace down/up
// pairs first, then the replacement as KEYEVENTF_UNICODE key/up pairs, all
// self-tagged, chunked in order when a payload exceeds one stack batch.
// Zero heap allocation (stack INPUT array). These adapters keep the historic
// call sites unchanged; the emitter is owned by the Win32Wrapper so the
// watchdog sees our injection ticks.
//===========================================================================
void emitInline(std::size_t backspace, const std::wstring& text) noexcept {
    g.hook.emitter().sendEdit(backspace, text);
}

//===========================================================================
// Fallback output — used ONLY when TSF cannot commit (e.g. elevated window),
// and for inline (zero-latency) mode. Synthetic backspaces are the LAST
// resort; the TSF path never injects them. v3.3.1: implemented by the
// wrapper's batched InlineEmitter (one SendInput per slab, ordered chunks).
//===========================================================================
void sendBackspaces(std::size_t n) noexcept {
    g.hook.emitter().sendEdit(n, L"");
}

void sendUnicodeText(const std::wstring& text) noexcept {
    g.hook.emitter().sendEdit(0, text);
}

//===========================================================================
// PRODUCER — runs on the hook pump thread (serialized; must be fast).
// Returns true to SUPPRESS the key (the app never receives it).
//===========================================================================
void updateExclusionCache() noexcept {
    g.fgExcluded_.store(g.monitor.currentAppAutoExcluded(), std::memory_order_relaxed);
}

// True for keys that move the caret or edit text WITHOUT the engine being
// fed the change (arrows, Delete, Home/End/PgUp/PgDn, Ctrl+Backspace,
// Ctrl+arrows). These desync the engine's raw word buffer from the visible
// text, so a context re-sync is queued before the next keystroke.
bool isCaretEditVk(std::uint32_t vk, bool ctrl) noexcept {
    switch (vk) {
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
        case VK_DELETE: case VK_HOME: case VK_END:
        case VK_PRIOR: case VK_NEXT: case VK_INSERT:
            return true;
        case VK_BACK:                       // Ctrl+Backspace deletes a word
            return ctrl;
        default:
            return false;
    }
}

//===========================================================================
// v3.4 profiling sinks (KIEEKEY_PROFILE builds only — everything below
// compiles out entirely in the default configuration).
//===========================================================================
#if KIEEKEY_PROFILE
ok::prof::HookSink     g_hookSink;      // hook thread only
ok::prof::ConsumerSink g_consumerSink;  // consumer thread only
std::atomic<std::uint64_t> g_profileSeq{0};
#endif

//===========================================================================
// Ordering barrier: before letting a pass-through key reach the application,
// wait (bounded) for the consumer to drain any queued Edit items. Edits are
// applied asynchronously on the consumer thread, while pass-through keys are
// delivered to the app immediately by the hook. Without this barrier the app's
// text can get ahead of the engine's buffer, so the next edit's backspace
// deletes the wrong characters — the "ghosting/sticking" when typing and
// deleting quickly.
//
// v3.4 (S1): the mechanism changed from a 200,000-iteration busy-spin
// (~2 ms, hook-thread stall + consumer-core theft on ≤2-core hosts) to the
// hybrid EditDrainBarrier: ~2 µs spin, then an event-based wait on the
// consumer's drained signal, hard-capped at 1 ms. The consumer signals the
// barrier's event whenever pendingEdits transitions to 0 (see
// flushEditBatch / the InlineEdit handler). Timeout degradation is identical
// to v3.3.1's exhausted spin (deliver the key anyway) at ≤ half the worst
// case, and is counted in g.drainBarrier.timeouts() for diagnostics.
//===========================================================================
void waitPendingEditsDrained() noexcept {
#if KIEEKEY_PROFILE
    const std::uint64_t profT0 = ok::prof::qpcNow();
    g.drainBarrier.waitDrained(g.pendingEdits);
    // The barrier cost lands on the NEXT pass-through record (the key being
    // delivered): the hook thread reads t_lastBarrierNs when building its
    // StageRecord. Overwritten on every barrier call (serialized thread).
    t_lastBarrierNs = ok::prof::nsSince(profT0);
#else
    g.drainBarrier.waitDrained(g.pendingEdits);
#endif
}
#if KIEEKEY_PROFILE
// ns spent in the most recent ordering-barrier wait (hook thread only)
inline std::uint32_t t_lastBarrierNs = 0;
#endif

// Queue a consumer-side re-sync: read the visible word before the caret via
// TSF and replay it into the engine (resumeFromText). Only meaningful on the
// TSF output path (the inline/SendInput path has no document access). Returns
// true iff a Resync item was actually queued (the consumer must be woken).
bool requestContextResync() noexcept {
    if (!g.fgUseTsf_.load(std::memory_order_relaxed)) { return false; }
    OutputItem it;
    it.kind = OutputItem::Kind::Resync;
    return g.outRing.try_push(it);   // false if full — degraded only
}

// Producer handler: runs on the hook pump thread (serialized). Returns the
// hook decision — whether to swallow the key AND whether the consumer thread
// has queued work that needs a wake (see ModernKeyHook::ProducerDecision).
using PD = ok::hook::ModernKeyHook::ProducerDecision;
PD onHookEvent(const KeyEvent& ev) noexcept {
    // ---- global hotkey: Ctrl+Shift toggles Vietnamese input -------------
    // Fires ONLY on a deliberate bare chord (both modifiers released without
    // any other key in between). Ctrl+Shift+<key> application shortcuts
    // (Ctrl+Shift+S, Ctrl+Shift+Tab, Ctrl+Shift+arrows for text selection,
    // …) must never toggle — the old any-chord trigger silently turned the
    // IME off in the middle of the user's work.
    if (ev.source == EventSource::Keyboard) {
        const bool isCtrlKey  = ev.vkCode == VK_LCONTROL || ev.vkCode == VK_RCONTROL;
        const bool isShiftKey = ev.vkCode == VK_LSHIFT  || ev.vkCode == VK_RSHIFT;
        const bool keyDown    = (ev.action == KeyAction::KeyDown ||
                                 ev.action == KeyAction::SysKeyDown);
        if (g.ctrlShiftChord.onKey(isCtrlKey, isShiftKey, !isCtrlKey && !isShiftKey && keyDown,
                                   keyDown) == ok::hotkey::CtrlShiftChord::Action::Toggle) {
            g.engineEnabled.store(!g.engineEnabled.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> lk(g.engineMtx);
                g.engine.startNewSession();   // drop stale word state
            }
            if (g.hMain) { ::PostMessageW(g.hMain, WM_APP_TOGGLE, 0, 0); }
        }
    }

    if (!g.engineEnabled.load(std::memory_order_relaxed)) { return PD{}; }

    // ---- v3.3.1: F9 (bare — no modifiers) switches the tone style --------
    // OpenKey-convention hotkey: converts the PENDING word's mark placement
    // between the two orthography styles ("hoá" <-> "hóa") directly inside
    // the engine's state buffer AND flips the placement style for future
    // words. The conversion is a core TextEngine operation (switchToneStyle)
    // — the wrapper/app only transports the resulting edit, exactly like any
    // other engine decision. With nothing pending the key still flips the
    // style (and is consumed) so the next composition follows immediately.
    if (ev.source == EventSource::Keyboard &&
        (ev.action == KeyAction::KeyDown || ev.action == KeyAction::SysKeyDown) &&
        ev.vkCode == VK_F9 && !ev.modifiers.ctrl && !ev.modifiers.alt &&
        !ev.modifiers.shift && !ev.modifiers.win &&
        !g.fgExcluded_.load(std::memory_order_relaxed)) {
        bool converted = false;
        std::size_t bs = 0;
        {
            std::lock_guard<std::mutex> lk(g.engineMtx);
            converted = g.engine.switchToneStyle();
            if (converted) {
                const EngineResult& r = g.engine.lastResult();
                g.engine.replacementUtf16(r, g.repScratch);
                bs = r.backspaceCount;
            }
        }
        if (converted) {
            // Apply the conversion edit through the same per-app output
            // policy as a normal keystroke edit (TSF batch or inline emit).
            if (g.fgUseTsf_.load(std::memory_order_relaxed) &&
                g.repScratch.size() <= kRingTextCap) {
                OutputItem it;
                it.kind      = OutputItem::Kind::Edit;
                it.backspace = static_cast<std::uint32_t>(bs);
                const std::size_t n = std::min<std::size_t>(g.repScratch.size(),
                                                            std::size(it.text));
                it.textLen = static_cast<std::uint32_t>(n);
                for (std::size_t i = 0; i < n; ++i) { it.text[i] = g.repScratch[i]; }
                if (!g.outRing.try_push(it)) {
                    emitInline(bs, g.repScratch);   // ring full — inline fallback
                    return PD{true, false};
                }
                g.pendingEdits.fetch_add(1, std::memory_order_relaxed);
                return PD{true, true};
            }
            emitInline(bs, g.repScratch);
            return PD{true, false};
        }
        return PD{true, false};   // style flipped (no pending word) — F9 consumed
    }

    // ---- bookkeeping / environment events ----
    if (ev.source == EventSource::ForegroundChanged) {
        g.monitor.refreshNow();          // refresh the snapshot (rare — not typing path)
        updateExclusionCache();          // cache for the per-key hot path
        updateForegroundPolicy();        // TSF-vs-inline + keyboard layout cache
        if (g.fgExcluded_.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lk(g.engineMtx);
            g.engine.startNewSession();
        }
        OutputItem it;
        it.kind = OutputItem::Kind::ForegroundChanged;
        static_cast<void>(g.outRing.try_push(it));
        return PD{false, true};
    }
    if (ev.source == EventSource::Mouse) {
        TextInput in;
        in.kind = InputKind::MouseDown;
        std::lock_guard<std::mutex> lk(g.engineMtx);
        static_cast<void>(g.engine.process(in));   // implicit word break
        // The caret jumped (user clicked into text). Re-sync the engine to
        // the visible word before the caret so retyping composes onto it
        // instead of raw-passing ("chugsn" -> select/delete "gsn" -> click ->
        // "sng" must become "chúng", not "chusng").
        // Apply any pending edits BEFORE the click is delivered: the click
        // moves the caret, and an edit applied at the new caret position
        // would insert into the wrong place (a ghost).
        waitPendingEditsDrained();
        const bool queued = requestContextResync();
        return PD{false, queued};
    }
    if (ev.action != KeyAction::KeyDown && ev.action != KeyAction::SysKeyDown) return PD{};
    if (ev.injected || ev.vkCode == 0 || isModifierVk(ev.vkCode)) return PD{};
    if (g.fgExcluded_.load(std::memory_order_relaxed)) {
        waitPendingEditsDrained();   // finish the previous word before the key passes
        std::lock_guard<std::mutex> lk(g.engineMtx);
        g.engine.startNewSession();
        return PD{};
    }

    // ---- caret/text changed outside the engine (nav & edit keys): the app
    //      moved the caret or deleted text (arrows, Delete, Home/End/PgUp/Dn,
    //      Ctrl+Backspace, Ctrl+arrows) without the engine seeing it. Queue a
    //      context re-sync so the next keystroke composes onto the visible
    //      word. (WordBreak keys like Tab/Enter are already fed to the
    //      engine; plain Backspace is fed too.)
    bool consumerWork = false;
    if (isCaretEditVk(ev.vkCode, ev.modifiers.ctrl)) {
        consumerWork = requestContextResync();
    }

    // ---- build the normalized TextInput ----
    const bool shift   = ev.modifiers.shift;
    const bool capsOn  = ev.modifiers.caps;
    const bool ctrl    = ev.modifiers.ctrl;
    const bool alt     = ev.modifiers.alt;
    const bool isCaps  = shift != capsOn;    // XOR — engine contract
    const bool otherCtrl = ctrl || alt;

    TextInput in;
    in.isCaps    = isCaps;
    in.otherCtrl = otherCtrl;

    if (ev.vkCode == VK_SPACE) {
        in.kind = InputKind::Space;
    } else if (ev.vkCode == VK_BACK) {
        in.kind = InputKind::Backspace;
    } else if (isWordBreakVk(ev.vkCode)) {
        in.kind   = InputKind::WordBreak;
        in.vkCode = static_cast<std::uint16_t>(ev.vkCode);
    } else {
        // Layout-aware first (real keyboard layout), US-map fallback.
        char32_t ch = layoutChar(ev.vkCode, ev.scanCode, ev.modifiers);
        if (ch == 0) { ch = produceChar(ev.vkCode, shift, capsOn); }
        if (ch == 0) {
            waitPendingEditsDrained();   // F-keys, media keys… pass through
            return PD{false, consumerWork};
        }
        in.kind = InputKind::Char;
        in.ch   = ch;
    }

    g.keysTyped.fetch_add(1, std::memory_order_relaxed);   // WPM gauge

    // ---- engine decision (fast, deterministic — safe on the hook thread).
    //      process() returns a const ref to engine-internal state; everything
    //      we need is read (or copied into the scratch) under the lock.
    bool     suppress = false;
    bool     reissueTyped = false;
    std::size_t bs    = 0;
#if KIEEKEY_PROFILE
    const bool profOn = ok::prof::enabled();
    ok::prof::StageRecord profRec;
    const std::uint64_t profT0 = ev.timestampQpc;
#endif
    {
        std::lock_guard<std::mutex> lk(g.engineMtx);
        const EngineResult& r = g.engine.process(in);
        g.engine.replacementUtf16(r, g.repScratch);   // scratch — no per-key alloc
        // Restore re-issue contracts (the app-visible semantics the legacy
        // hooks deliver and the user expects):
        //   * CHAR restore (hotfix §3): after reverting the word to its bare
        //     spelling the hook RE-SENDS the typed key — 'chào'+f → 'chaof'.
        //   * SPACE restore (D4, v3.1): after a wrong-spelling word triggers
        //     Restore on the space, the space is RE-ISSUED — without it the
        //     space was eaten ('arbit hối đoái' rendered 'arbithối đoái').
        // Backspace / word-break / mouse Restores keep their no-re-issue
        // semantics (verified against 2.0.5).
        reissueTyped = (r.code == EngineCode::Restore ||
                        r.code == EngineCode::RestoreAndStartNewSession) &&
                       (in.kind == InputKind::Char || in.kind == InputKind::Space);
        if (r.code == EngineCode::ReplaceMacro) {
            // D3 (v3.1): the expansion rides in the result as final Unicode
            // code points — the hook applies it: delete backspaceCount chars
            // at the caret, then type the expansion, and CONSUME the break
            // key (macro + space → expansion, no extra space; the expansion
            // itself provides the separator). Pre-v3.1 the raw key passed
            // through and the expansion was silently dropped.
            bs = r.backspaceCount;
            g.engine.macroExpansionUtf16(r, g.repScratch);
            if (bs > 0 || !g.repScratch.empty()) { suppress = true; }
        } else if (r.consumed() &&
                   !(r.backspaceCount == 0 && g.repScratch.empty())) {
            suppress = true;
            bs = r.backspaceCount;
        }
    }
#if KIEEKEY_PROFILE
    if (profOn) {
        profRec.seq = g_profileSeq.fetch_add(1, std::memory_order_relaxed);
        profRec.vk  = static_cast<std::uint32_t>(ev.vkCode);
        profRec.backspace = static_cast<std::uint32_t>(bs);
        profRec.textLen   = static_cast<std::uint32_t>(g.repScratch.size());
        profRec.stageNs[0] = ok::prof::nsSince(profT0);   // t1: decision done
        profRec.flags |= 1u << 0;
    }
#endif
    if (!suppress) {
        // Pass-through: the app is about to receive this key. Ensure every
        // previously queued edit has been applied so the app's text cannot
        // overtake the engine's buffer (ghost/stick guard).
        waitPendingEditsDrained();
#if KIEEKEY_PROFILE
        if (profOn) {
            profRec.stageNs[1] = ok::prof::nsSince(profT0);   // t2: delivered
            profRec.flags |= 1u << 1;
            profRec.barrierNs = t_lastBarrierNs;
            g_hookSink.push(profRec);
        }
#endif
        return PD{false, consumerWork};
    }
    if (reissueTyped) {
        // Char: in.ch is the case-adjusted char the user actually typed
        // (layoutChar already applied Shift/Caps). Space: re-issue L' '.
        // This re-types the key exactly as the legacy hook's
        // SendKeyCode(_keycode|CAPS_MASK) would.
        // static_cast: wchar_t is 16-bit on MSVC/MinGW; pushing a char32_t
        // implicitly is C4244 under MSVC /W4 /WX.
        g.repScratch.push_back(
            static_cast<wchar_t>(in.kind == InputKind::Space ? L' ' : in.ch));
    }

    // ---- output: per-app policy ----
    // D3 note: a long macro expansion does not fit the ring item's fixed
    // text buffer (2*kMaxBuff+1 wchar_t). Instead of truncating it, such an
    // edit takes the inline chunked SendInput path directly (the TSF path
    // remains the default for every normal edit).
    if (g.fgUseTsf_.load(std::memory_order_relaxed) &&
        g.repScratch.size() <= kRingTextCap) {
        OutputItem it;
        it.kind      = OutputItem::Kind::Edit;
        it.backspace = static_cast<std::uint32_t>(bs);
        // Copy the FULL replacement (up to the buffer's worst-case capacity).
        // textLen must equal what we actually copied — the consumer builds
        // std::wstring(text, text + textLen), so any mismatch is an OOB read.
        const std::size_t n = std::min<std::size_t>(g.repScratch.size(),
                                                    std::size(it.text));
        it.textLen = static_cast<std::uint32_t>(n);
        for (std::size_t i = 0; i < n; ++i) { it.text[i] = g.repScratch[i]; }
#if KIEEKEY_PROFILE
        if (profOn) { it.profT0 = profT0; it.profSeq = profRec.seq; }
#endif
        // Push BEFORE the hook enqueues the KeyEvent (ordering guarantee).
        // If the ring is ever full, fall back to inline rather than lose
        // the user's character.
        if (!g.outRing.try_push(it)) {
            emitInline(bs, g.repScratch);
            return PD{true, false};
        }
        // One more pending edit the consumer must apply before any
        // pass-through key may reach the app (see waitPendingEditsDrained).
        g.pendingEdits.fetch_add(1, std::memory_order_relaxed);
#if KIEEKEY_PROFILE
        if (profOn) {
            profRec.stageNs[1] = ok::prof::nsSince(profT0);   // t2: pushed
            profRec.flags |= (1u << 1) | ok::prof::kIsEdit;
            g_hookSink.push(profRec);   // t3/t4/t5 completed consumer-side
        }
#endif
        return PD{true, true};
    } else {
        // Zero-latency inline path — no consumer hop, original-OpenKey feel.
        // v3.4 (S2): opt-in deferred mode (env OPENKEY_INLINE_MODE=deferred)
        // routes the edit through the consumer instead. Default stays
        // hook-inline: the entire producer-side chain including SendInput
        // measured ≤ ~3 µs p50 on real Windows (frozen burst data), while
        // deferring always adds the ring hop + wake to the same chain.
#if KIEEKEY_PROFILE
        if (profOn) { profRec.flags |= ok::prof::kIsEdit; }
#endif
        if (g.inlineDeferred && g.repScratch.size() <= kRingTextCap) {
            OutputItem it;
            it.kind      = OutputItem::Kind::InlineEdit;
            it.backspace = static_cast<std::uint32_t>(bs);
            const std::size_t n = std::min<std::size_t>(g.repScratch.size(),
                                                        std::size(it.text));
            it.textLen = static_cast<std::uint32_t>(n);
            for (std::size_t i = 0; i < n; ++i) { it.text[i] = g.repScratch[i]; }
            if (g.outRing.try_push(it)) {
                g.pendingEdits.fetch_add(1, std::memory_order_relaxed);
                return PD{true, true};
            }
            // ring full — degrade to in-callback emit (never drop a char)
        }
        emitInline(bs, g.repScratch);
        return PD{true, false};
    }
}

//===========================================================================
// CONSUMER — runs on the hook's consumer thread; performs the actual text
// I/O (TSF first, SendInput fallback). Never touches the engine.
//===========================================================================
namespace {
// Bounded batch of pending edits for ONE TSF edit session (see below).
// thread_local: the consumer thread is the only user; reused across drains.
inline constexpr std::size_t kMaxEditBatch = 32;
thread_local std::vector<ok::tsf::EditDelta> g_editBatch;
thread_local std::uint32_t g_editBatchCount = 0;   // pendingEdits units in the batch
#if KIEEKEY_PROFILE
thread_local std::uint64_t g_editBatchT0 = 0;      // t0 of the batch's first item
thread_local std::uint64_t g_editBatchSeq = 0;
#endif
} // namespace

void onConsumerEvent(const KeyEvent& /*ev*/) noexcept {
    if (!g.composerAttached.exchange(true)) {
        static_cast<void>(g.composer.attach());   // CoInitializeEx on this thread
    }
#if KIEEKEY_PROFILE
    const bool profOn = ok::prof::enabled();
#endif
    // Flush the current edit batch through ONE synchronous TSF edit session.
    // Batching consecutive edits removes the per-keystroke RequestEditSession
    // round-trip — the dominant term in accent-typing latency — and applies
    // the deltas in engine order inside a single session.
    auto flushEditBatch = [&]() noexcept {
        if (g_editBatch.empty()) { return; }
#if KIEEKEY_PROFILE
        const std::uint64_t profT0 = g_editBatchT0;
        const std::uint64_t profSeq = g_editBatchSeq;
        const std::uint64_t tFlush = ok::prof::qpcNow();   // t4: flush start
        if (profOn && profT0 != 0) {
            ok::prof::StageRecord r;
            r.seq = profSeq;
            r.stageNs[2] = ok::prof::nsSince(profT0);      // t3: dequeued
            r.flags |= 1u << 2;
            g_consumerSink.push(r);
        }
#endif
        if (g.composer.commitBatch(g_editBatch)) {
            // applied via TSF — nothing else to do
        } else {
            // last-resort fallback: synthetic backspaces + Unicode text
            for (const ok::tsf::EditDelta& d : g_editBatch) {
                sendBackspaces(d.backspace);
                sendUnicodeText(d.text);
            }
        }
#if KIEEKEY_PROFILE
        if (profOn && profT0 != 0) {
            // complete the consumer record: t4 (flush start) + t5 (session
            // returned / fallback SendInput issued)
            for (std::uint64_t i = g_consumerSink.count(); i > 0; --i) {
                ok::prof::StageRecord& r = g_consumerSink.at(i - 1);
                if (r.seq == profSeq) {
                    r.stageNs[3] = ok::prof::nsBetween(profT0, tFlush);
                    r.stageNs[4] = ok::prof::nsSince(profT0);
                    r.flags |= (1u << 3) | (1u << 4);
                    break;
                }
            }
        }
#endif
        // v3.4 (S1): signal the ordering barrier when the pending count hits
        // zero — pass-through keys waiting on the hook thread wake on this
        // event instead of burning CPU in a spin loop.
        const std::uint32_t prev =
            g.pendingEdits.fetch_sub(g_editBatchCount, std::memory_order_relaxed);
        if (prev == g_editBatchCount) { g.drainBarrier.notifyDrained(); }
        g_editBatch.clear();
        g_editBatchCount = 0;
    };

    OutputItem it;
    while (g.outRing.try_pop(it)) {
        if (it.kind == OutputItem::Kind::Edit) {
            g_editBatch.push_back(ok::tsf::EditDelta{
                it.backspace, std::wstring(it.text, it.text + it.textLen)});
            ++g_editBatchCount;
#if KIEEKEY_PROFILE
            if (profOn && g_editBatch.size() == 1) {
                g_editBatchT0 = it.profT0;    // first item of the batch owns t0
                g_editBatchSeq = it.profSeq;
            }
#endif
            if (g_editBatch.size() >= kMaxEditBatch) { flushEditBatch(); }
            continue;   // keep draining — more edits may follow in the burst
        }
        // v3.4 (S2): deferred inline edit — flush pending TSF edits first
        // (ring order), then emit through the batched emitter. Counts into
        // pendingEdits exactly like a TSF edit (same ordering barrier).
        if (it.kind == OutputItem::Kind::InlineEdit) {
            flushEditBatch();
#if KIEEKEY_PROFILE
            const std::uint64_t profT0 = it.profT0;
            const std::uint64_t profSeq = it.profSeq;
            const std::uint64_t tFlush = ok::prof::qpcNow();
#endif
            g.hook.emitter().sendEdit(it.backspace, it.text, it.textLen);
#if KIEEKEY_PROFILE
            if (profOn && profT0 != 0) {
                ok::prof::StageRecord r;
                r.seq = profSeq;
                r.flags = ok::prof::kIsEdit;
                r.stageNs[2] = ok::prof::nsBetween(profT0, tFlush);   // t3
                r.stageNs[3] = r.stageNs[2];                          // t4≈t3
                r.stageNs[4] = ok::prof::nsSince(profT0);             // t5
                r.flags |= (1u << 2) | (1u << 3) | (1u << 4);
                g_consumerSink.push(r);
            }
#endif
            const std::uint32_t prev =
                g.pendingEdits.fetch_sub(1, std::memory_order_relaxed);
            if (prev == 1) { g.drainBarrier.notifyDrained(); }
            continue;
        }
        // Non-edit item: apply any pending edits FIRST (ring order), then the
        // control item.
        flushEditBatch();
        if (it.kind == OutputItem::Kind::ForegroundChanged) {
            g.composer.onForegroundChanged();
        } else if (it.kind == OutputItem::Kind::Resync) {
            // The user clicked / moved the caret / edited text outside the
            // engine. Read the raw word before the caret and re-sync the
            // engine so the next keystroke composes onto the visible text.
            std::wstring word;
            if (g.composer.textBeforeCaret(word)) {
                std::lock_guard<std::mutex> lk(g.engineMtx);
                static_cast<void>(g.engine.resumeFromText(word));
            }
        }
    }
    flushEditBatch();
}

//===========================================================================
// Tray icon + context menu
//===========================================================================
void addTrayIcon() noexcept {
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = g.hMain;
    nid.uID              = 1;
    nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon            = g.engineEnabled.load() ? g.hIconOn : g.hIconOff;
    std::wstring tip = L"KieeKey — "
        + std::wstring(g.options.inputMethod == InputMethod::Telex ? L"Telex"
                        : g.options.inputMethod == InputMethod::Vni ? L"VNI" : L"Simple Telex")
        + (g.engineEnabled.load() ? L" [Bật — Ctrl+Shift để tắt]" : L" [Tắt — Ctrl+Shift để bật]");
    if (tip.size() >= std::size(nid.szTip)) { tip.resize(std::size(nid.szTip) - 1); }
    std::copy(tip.begin(), tip.end(), nid.szTip);
    nid.szTip[tip.size()] = L'\0';
    ::Shell_NotifyIconW(NIM_ADD, &nid);

    // welcome balloon once per process run
    if (!g.balloonShown.exchange(true)) {
        nid.uFlags |= NIF_INFO;
        nid.dwInfoFlags = NIIF_INFO;
        std::wstring info = L"Bộ gõ tiếng Việt đã sẵn sàng.\n"
                            L"Nhấn Ctrl+Shift để bật/tắt, nhấp đúp vào biểu tượng để mở Cài đặt.";
        std::wstring title = L"KieeKey v1.0.1";
        if (info.size() >= std::size(nid.szInfo))  { info.resize(std::size(nid.szInfo) - 1); }
        if (title.size() >= std::size(nid.szInfoTitle)) { title.resize(std::size(nid.szInfoTitle) - 1); }
        std::copy(info.begin(), info.end(), nid.szInfo);
        nid.szInfo[info.size()] = L'\0';
        std::copy(title.begin(), title.end(), nid.szInfoTitle);
        nid.szInfoTitle[title.size()] = L'\0';
        ::Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
}

void updateTrayIcon() noexcept {
    if (!g.hMain) { return; }
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g.hMain;
    nid.uID    = 1;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon  = g.engineEnabled.load() ? g.hIconOn : g.hIconOff;
    std::wstring tip = L"KieeKey — "
        + std::wstring(g.options.inputMethod == InputMethod::Telex ? L"Telex"
                        : g.options.inputMethod == InputMethod::Vni ? L"VNI" : L"Simple Telex")
        + (g.engineEnabled.load() ? L" [Bật]" : L" [Tắt]");
    if (tip.size() >= std::size(nid.szTip)) { tip.resize(std::size(nid.szTip) - 1); }
    std::copy(tip.begin(), tip.end(), nid.szTip);
    nid.szTip[tip.size()] = L'\0';
    ::Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void showTrayMenu() noexcept {
    HMENU menu = ::CreatePopupMenu();
    HMENU methodMenu = ::CreatePopupMenu();

    // static_cast<UINT>: MF_STRING is `long` in the MinGW headers, and the
    // `long | UINT` fold then narrowed to the UINT parameter trips GCC
    // -Wsign-conversion (MSVC /W4 is silent here). Same value either way.
    ::AppendMenuW(menu,
                  static_cast<UINT>(MF_STRING)
                      | (g.engineEnabled.load() ? MF_CHECKED : 0u),
                  IDM_TOGGLE, L"Bật gõ tiếng Việt");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(methodMenu,
                  static_cast<UINT>(MF_STRING)
                      | (g.options.inputMethod == InputMethod::Telex ? MF_CHECKED : 0u),
                  IDM_METHOD_TELEX, L"Telex");
    ::AppendMenuW(methodMenu,
                  static_cast<UINT>(MF_STRING)
                      | (g.options.inputMethod == InputMethod::Vni ? MF_CHECKED : 0u),
                  IDM_METHOD_VNI, L"VNI");
    ::AppendMenuW(methodMenu,
                  static_cast<UINT>(MF_STRING)
                      | (g.options.inputMethod == InputMethod::SimpleTelex ? MF_CHECKED : 0u),
                  IDM_METHOD_SIMPLETELEX, L"Simple Telex");
    ::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(methodMenu), L"Phương thức gõ");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Cài đặt…");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Thoát");

    POINT pt;
    ::GetCursorPos(&pt);
    ::SetForegroundWindow(g.hMain);
    const UINT cmd = static_cast<UINT>(::TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                                        pt.x, pt.y, 0, g.hMain, nullptr));
    ::DestroyMenu(menu);

    switch (cmd) {
        case IDM_TOGGLE: {
            g.engineEnabled.store(!g.engineEnabled.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
            std::lock_guard<std::mutex> lk(g.engineMtx);
            g.engine.startNewSession();
            updateTrayIcon();
            break;
        }
        case IDM_METHOD_TELEX: case IDM_METHOD_VNI: case IDM_METHOD_SIMPLETELEX: {
            const auto m = static_cast<InputMethod>(cmd - IDM_METHOD_TELEX);
            {
                std::lock_guard<std::mutex> lk(g.engineMtx);
                g.options.inputMethod = m;
                g.engine.setOptions(g.options);
                g.engine.startNewSession();
            }
            saveSettings();
            updateTrayIcon();
            break;
        }
        case IDM_SETTINGS:
            if (g.hSettings) { ::SetForegroundWindow(g.hSettings); }
            else if (g.hInst) {
                g.hSettings = ::CreateWindowExW(0, L"KieeKeySettings",
                                                L"KieeKey — Cài đặt",
                                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                                WS_MINIMIZEBOX,
                                                CW_USEDEFAULT, CW_USEDEFAULT, 480, 470,
                                                nullptr, nullptr, g.hInst, nullptr);
                ::ShowWindow(g.hSettings, SW_SHOW);
            }
            break;
        case IDM_EXIT:
            ::PostMessageW(g.hMain, WM_CLOSE, 0, 0);
            break;
        default:
            break;
    }
}

//===========================================================================
// Settings window (Win32, Vietnamese UI — built in code for pixel control)
//===========================================================================
void showTab(int tab);   // fwd (defined below; used by settingsToControls)

HFONT uiFont() noexcept {
    static HFONT f = ::CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    return f;
}

HWND mkCtl(HWND parent, LPCWSTR cls, LPCWSTR text, DWORD style, int x, int y,
           int w, int h, HMENU id) {
    HWND c = ::CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE,
                               x, y, w, h, parent, id, g.hInst, nullptr);
    if (c) { ::SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()), TRUE); }
    return c;
}

void settingsFromControls() {
    const bool telex = (::SendMessageW(::GetDlgItem(g.hSettings, IDC_RADIO_TELEX),
                                       BM_GETCHECK, 0, 0) == BST_CHECKED);
    const bool vni   = (::SendMessageW(::GetDlgItem(g.hSettings, IDC_RADIO_VNI),
                                       BM_GETCHECK, 0, 0) == BST_CHECKED);
    const LRESULT codeTableSel =
        ::SendMessageW(::GetDlgItem(g.hSettings, IDC_COMBO_CODETABLE), CB_GETCURSEL, 0, 0);
    std::lock_guard<std::mutex> lk(g.engineMtx);
    g.options.inputMethod = telex ? InputMethod::Telex
                          : vni   ? InputMethod::Vni
                          :         InputMethod::SimpleTelex;
    g.options.codeTable = (codeTableSel >= 0 && codeTableSel <= 4)
        ? static_cast<CodeTable>(codeTableSel) : CodeTable::Unicode;
    g.options.useMacro                 = ::IsDlgButtonChecked(g.hSettings, IDC_CHK_MACRO) == BST_CHECKED;
    g.options.checkSpelling            = ::IsDlgButtonChecked(g.hSettings, IDC_CHK_SPELL) == BST_CHECKED;
    g.options.restoreIfWrongSpelling   = ::IsDlgButtonChecked(g.hSettings, IDC_CHK_RESTORE) == BST_CHECKED;
    g.options.upperCaseFirstChar       = ::IsDlgButtonChecked(g.hSettings, IDC_CHK_UPPER) == BST_CHECKED;
    g.options.useModernOrthography     = ::IsDlgButtonChecked(g.hSettings, IDC_CHK_MODERN) == BST_CHECKED;
    g.options.quickTelex               = ::IsDlgButtonChecked(g.hSettings, IDC_CHK_QUICK) == BST_CHECKED;
    g.exclIde                          = ::IsDlgButtonChecked(g.hSettings, IDC_CHK_EXCLUDE_IDE) == BST_CHECKED;
    g.exclGame                         = ::IsDlgButtonChecked(g.hSettings, IDC_CHK_EXCLUDE_GAME) == BST_CHECKED;
    g.exclShell                        = ::IsDlgButtonChecked(g.hSettings, IDC_CHK_EXCLUDE_SHELL) == BST_CHECKED;
    const bool outAuto = (::SendMessageW(::GetDlgItem(g.hSettings, IDC_RADIO_OUT_AUTO),
                                         BM_GETCHECK, 0, 0) == BST_CHECKED);
    const bool outTsf  = (::SendMessageW(::GetDlgItem(g.hSettings, IDC_RADIO_OUT_TSF),
                                         BM_GETCHECK, 0, 0) == BST_CHECKED);
    g.outputMode.store(outAuto ? 0 : outTsf ? 1 : 2, std::memory_order_relaxed);
    g.engine.setOptions(g.options);
    g.engine.startNewSession();
    g.monitor.setExcludeIde(g.exclIde);
    g.monitor.setExcludeGame(g.exclGame);
    g.monitor.setExcludeShell(g.exclShell);
    updateExclusionCache();
    updateForegroundPolicy();   // output mode affects the TSF-vs-inline decision
}

void settingsToControls() {
    ::CheckRadioButton(g.hSettings, IDC_RADIO_TELEX, IDC_RADIO_SIMPLETELEX,
                       IDC_RADIO_TELEX + static_cast<int>(g.options.inputMethod));
    ::SendMessageW(::GetDlgItem(g.hSettings, IDC_COMBO_CODETABLE), CB_SETCURSEL,
                   static_cast<WPARAM>(g.options.codeTable), 0);
    ::CheckDlgButton(g.hSettings, IDC_CHK_MACRO,   g.options.useMacro ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_SPELL,   g.options.checkSpelling ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_RESTORE, g.options.restoreIfWrongSpelling ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_UPPER,   g.options.upperCaseFirstChar ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_MODERN,  g.options.useModernOrthography ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_QUICK,   g.options.quickTelex ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_EXCLUDE_IDE,   g.exclIde ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_EXCLUDE_GAME,  g.exclGame ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_EXCLUDE_SHELL, g.exclShell ? BST_CHECKED : BST_UNCHECKED);
    const int outMode = g.outputMode.load(std::memory_order_relaxed);
    ::CheckRadioButton(g.hSettings, IDC_RADIO_OUT_AUTO, IDC_RADIO_OUT_SEND,
                       IDC_RADIO_OUT_AUTO + outMode);
    showTab(0);
}

void showTab(int tab) {
    // Every control (including static labels) belongs to exactly one tab;
    // toggle visibility so only the active tab's controls are shown.
    // (v3.0 bugfix: labels previously had no IDs and were never hidden —
    //  all three tabs' labels were drawn overlapping each other.)
    static constexpr int kTab0[] = {
        IDC_STAT_METHOD, IDC_STAT_CODETABLE, IDC_STAT_OUT_LAB, IDC_STAT_OUT_NOTE,
        IDC_RADIO_TELEX, IDC_RADIO_VNI, IDC_RADIO_SIMPLETELEX,
        IDC_RADIO_OUT_AUTO, IDC_RADIO_OUT_TSF, IDC_RADIO_OUT_SEND,
        IDC_COMBO_CODETABLE, IDC_CHK_MACRO, IDC_CHK_SPELL, IDC_CHK_RESTORE,
        IDC_CHK_UPPER, IDC_CHK_MODERN, IDC_CHK_QUICK, 0
    };
    static constexpr int kTab1[] = {
        IDC_STAT_APPS_TITLE, IDC_STAT_APPS_NOTE,
        IDC_CHK_EXCLUDE_IDE, IDC_CHK_EXCLUDE_GAME, IDC_CHK_EXCLUDE_SHELL, 0
    };
    static constexpr int kTab2[] = {
        IDC_STAT_LATLAB, IDC_STAT_AVGLAB, IDC_STAT_PUSHLAB, IDC_STAT_DROPLAB,
        IDC_STAT_WPMLAB, IDC_STAT_DESC,
        IDC_STAT_LATVAL, IDC_STAT_AVGVAL, IDC_STAT_PUSHV, IDC_STAT_DROPV,
        IDC_STAT_WPMVAL, 0
    };
    if (!g.hSettings) { return; }
    for (const int* p = kTab0; *p; ++p) { ::ShowWindow(::GetDlgItem(g.hSettings, *p), tab == 0 ? SW_SHOW : SW_HIDE); }
    for (const int* p = kTab1; *p; ++p) { ::ShowWindow(::GetDlgItem(g.hSettings, *p), tab == 1 ? SW_SHOW : SW_HIDE); }
    for (const int* p = kTab2; *p; ++p) { ::ShowWindow(::GetDlgItem(g.hSettings, *p), tab == 2 ? SW_SHOW : SW_HIDE); }
}

LRESULT CALLBACK settingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g.hSettings = hwnd;
            // Tab control
            HWND tab = mkCtl(hwnd, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                             12, 12, 456, 350, reinterpret_cast<HMENU>(IDC_TAB));
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            wchar_t t0[] = L"Bàn phím";
            wchar_t t1[] = L"Ứng dụng";
            wchar_t t2[] = L"Chẩn đoán";
            item.pszText = t0; ::SendMessageW(tab, TCM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));
            item.pszText = t1; ::SendMessageW(tab, TCM_INSERTITEMW, 1, reinterpret_cast<LPARAM>(&item));
            item.pszText = t2; ::SendMessageW(tab, TCM_INSERTITEMW, 2, reinterpret_cast<LPARAM>(&item));

            // ---- tab 0: Bàn phím ----
            mkCtl(hwnd, L"STATIC", L"Phương thức gõ:", WS_CHILD | WS_VISIBLE, 28, 52, 110, 18,
                  reinterpret_cast<HMENU>(IDC_STAT_METHOD));
            mkCtl(hwnd, L"BUTTON", L"Telex", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  150, 50, 80, 20, reinterpret_cast<HMENU>(IDC_RADIO_TELEX));
            mkCtl(hwnd, L"BUTTON", L"VNI", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  235, 50, 80, 20, reinterpret_cast<HMENU>(IDC_RADIO_VNI));
            mkCtl(hwnd, L"BUTTON", L"Simple Telex", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  320, 50, 130, 20, reinterpret_cast<HMENU>(IDC_RADIO_SIMPLETELEX));
            mkCtl(hwnd, L"STATIC", L"Bảng mã:", WS_CHILD | WS_VISIBLE, 28, 82, 110, 18,
                  reinterpret_cast<HMENU>(IDC_STAT_CODETABLE));
            HWND combo = mkCtl(hwnd, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                               CBS_DROPDOWNLIST, 150, 80, 200, 200, reinterpret_cast<HMENU>(IDC_COMBO_CODETABLE));
            for (const wchar_t* s : {L"Unicode", L"TCVN3 (ABC)", L"VNI Windows",
                                     L"Unicode tổ hợp", L"CP 1258"}) {
                ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
            }
            const wchar_t* kOpts[] = {
                L"Gõ tắt (macro)",
                L"Kiểm tra chính tả",
                L"Tự sửa từ gõ sai",
                L"Viết hoa đầu câu",
                L"Chính tả mới (oà / uý)",
                L"Telex nhanh (cc→ch, gg→gi)",
            };
            const int kIds[] = {IDC_CHK_MACRO, IDC_CHK_SPELL, IDC_CHK_RESTORE,
                                IDC_CHK_UPPER, IDC_CHK_MODERN, IDC_CHK_QUICK};
            for (int i = 0; i < 6; ++i) {
                // INT_PTR round-trip: reinterpret_cast<HMENU>(int) directly is
                // a 32→64-bit pointer widening that MSVC flags as C4312 under
                // /W4 /WX (HMENU is pointer-sized). Casting through INT_PTR is
                // the canonical control-id idiom and is warning-free.
                mkCtl(hwnd, L"BUTTON", kOpts[i], WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                      28, 118 + i * 26, 320, 20,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIds[i])));
            }

            // Chế độ xuất (WPM: inline SendInput is the zero-latency path)
            mkCtl(hwnd, L"STATIC", L"Chế độ xuất:", WS_CHILD | WS_VISIBLE, 28, 290, 110, 18,
                  reinterpret_cast<HMENU>(IDC_STAT_OUT_LAB));
            mkCtl(hwnd, L"BUTTON", L"Auto", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  150, 288, 60, 20, reinterpret_cast<HMENU>(IDC_RADIO_OUT_AUTO));
            mkCtl(hwnd, L"BUTTON", L"Luôn TSF (chống nháy)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  215, 288, 170, 20, reinterpret_cast<HMENU>(IDC_RADIO_OUT_TSF));
            mkCtl(hwnd, L"BUTTON", L"Luôn SendInput (nhanh nhất)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  390, 288, 200, 20, reinterpret_cast<HMENU>(IDC_RADIO_OUT_SEND));
            mkCtl(hwnd, L"STATIC",
                  L"Auto: TSF cho trình duyệt & Office (không nháy chữ), SendInput trực tiếp "
                  L"cho các ứng dụng khác (nhanh nhất, không qua thread phụ).",
                  WS_CHILD | WS_VISIBLE, 28, 314, 440, 32,
                  reinterpret_cast<HMENU>(IDC_STAT_OUT_NOTE));

            // ---- tab 1: Ứng dụng ----
            mkCtl(hwnd, L"STATIC", L"Tự động tắt bộ gõ khi cửa sổ đang chạy là:",
                  WS_CHILD | WS_VISIBLE, 28, 52, 420, 18,
                  reinterpret_cast<HMENU>(IDC_STAT_APPS_TITLE));
            mkCtl(hwnd, L"BUTTON", L"IDE / Editor (VS Code, Visual Studio, CLion…)",
                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 28, 80, 420, 20,
                  reinterpret_cast<HMENU>(IDC_CHK_EXCLUDE_IDE));
            mkCtl(hwnd, L"BUTTON", L"Trò chơi toàn màn hình (DirectX / Vulkan / OpenGL)",
                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 28, 104, 420, 20,
                  reinterpret_cast<HMENU>(IDC_CHK_EXCLUDE_GAME));
            mkCtl(hwnd, L"BUTTON", L"Windows Shell (Explorer, Terminal, CMD)",
                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 28, 128, 420, 20,
                  reinterpret_cast<HMENU>(IDC_CHK_EXCLUDE_SHELL));
            mkCtl(hwnd, L"STATIC",
                  L"Lưu ý: tắt loại trừ Shell để gõ tên file tiếng Việt trong "
                  L"Explorer. Việc phát hiện cửa sổ là theo sự kiện (WinEvent), "
                  L"không tốn CPU khi rảnh.",
                  WS_CHILD | WS_VISIBLE, 28, 160, 420, 60,
                  reinterpret_cast<HMENU>(IDC_STAT_APPS_NOTE));

            // ---- tab 2: Chẩn đoán ----
            mkCtl(hwnd, L"STATIC", L"Độ trễ đỉnh hook → xử lý (µs):", WS_CHILD | WS_VISIBLE,
                  28, 52, 220, 18, reinterpret_cast<HMENU>(IDC_STAT_LATLAB));
            mkCtl(hwnd, L"STATIC", L"—", WS_CHILD | WS_VISIBLE, 260, 52, 180, 18,
                  reinterpret_cast<HMENU>(IDC_STAT_LATVAL));
            mkCtl(hwnd, L"STATIC", L"Độ trễ trung bình (µs):", WS_CHILD | WS_VISIBLE,
                  28, 78, 220, 18, reinterpret_cast<HMENU>(IDC_STAT_AVGLAB));
            mkCtl(hwnd, L"STATIC", L"—", WS_CHILD | WS_VISIBLE, 260, 78, 180, 18,
                  reinterpret_cast<HMENU>(IDC_STAT_AVGVAL));
            mkCtl(hwnd, L"STATIC", L"Sự kiện bàn phím đã xử lý:", WS_CHILD | WS_VISIBLE,
                  28, 104, 220, 18, reinterpret_cast<HMENU>(IDC_STAT_PUSHLAB));
            mkCtl(hwnd, L"STATIC", L"0", WS_CHILD | WS_VISIBLE, 260, 104, 180, 18,
                  reinterpret_cast<HMENU>(IDC_STAT_PUSHV));
            mkCtl(hwnd, L"STATIC", L"Sự kiện bị bỏ (hàng đợi đầy):", WS_CHILD | WS_VISIBLE,
                  28, 130, 220, 18, reinterpret_cast<HMENU>(IDC_STAT_DROPLAB));
            mkCtl(hwnd, L"STATIC", L"0", WS_CHILD | WS_VISIBLE, 260, 130, 180, 18,
                  reinterpret_cast<HMENU>(IDC_STAT_DROPV));
            mkCtl(hwnd, L"STATIC", L"Tốc độ gõ (ký tự/phút, ≈ WPM × 5):", WS_CHILD | WS_VISIBLE,
                  28, 156, 230, 18, reinterpret_cast<HMENU>(IDC_STAT_WPMLAB));
            mkCtl(hwnd, L"STATIC", L"—", WS_CHILD | WS_VISIBLE, 260, 156, 180, 18,
                  reinterpret_cast<HMENU>(IDC_STAT_WPMVAL));
            mkCtl(hwnd, L"STATIC",
                  L"Kiến trúc: hàng đợi lock-free SPSC; hook thread không bao giờ bị chặn; "
                  L"quyết định bộ gõ chạy ngay trên hook thread; xuất qua TSF hoặc SendInput "
                  L"trực tiếp (không Backspace giả, không clipboard).",
                  WS_CHILD | WS_VISIBLE, 28, 188, 420, 110,
                  reinterpret_cast<HMENU>(IDC_STAT_DESC));

            // ---- buttons ----
            mkCtl(hwnd, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                  250, 400, 70, 26, reinterpret_cast<HMENU>(IDOK));
            mkCtl(hwnd, L"BUTTON", L"Hủy", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  328, 400, 70, 26, reinterpret_cast<HMENU>(IDCANCEL));
            mkCtl(hwnd, L"BUTTON", L"Áp dụng", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  406, 400, 70, 26, reinterpret_cast<HMENU>(IDC_BTN_APPLY));

            settingsToControls();
            ::SetTimer(hwnd, 1, 500, nullptr);   // live telemetry
            return 0;
        }

        case WM_NOTIFY: {
            const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
            if (nm && nm->idFrom == IDC_TAB && nm->code == TCN_SELCHANGE) {
                showTab(static_cast<int>(::SendMessageW(nm->hwndFrom, TCM_GETCURSEL, 0, 0)));
            }
            return 0;
        }

        case WM_TIMER: {
            wchar_t buf[64];

            // Latency: peak + EMA average.
            std::swprintf(buf, std::size(buf), L"%lld", static_cast<long long>(g.hook.peakLatencyUs()));
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_LATVAL), buf);
            std::swprintf(buf, std::size(buf), L"%lld", static_cast<long long>(g.hook.avgLatencyUs()));
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_AVGVAL), buf);

            // Counters.
            std::swprintf(buf, std::size(buf), L"%llu",
                          static_cast<unsigned long long>(g.hook.pushed()));
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_PUSHV), buf);
            std::swprintf(buf, std::size(buf), L"%llu",
                          static_cast<unsigned long long>(g.hook.dropped()));
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_DROPV), buf);

            // WPM gauge: EMA of printable-characters-per-minute.
            static std::uint64_t s_lastKeys = 0;
            static std::int64_t  s_lastMs   = 0;
            static std::int64_t  s_kpmEma   = 0;
            const auto nowMs  = static_cast<std::int64_t>(::GetTickCount64());
            const auto keys   = g.keysTyped.load(std::memory_order_relaxed);
            if (s_lastMs != 0 && nowMs > s_lastMs) {
                const double dt  = static_cast<double>(nowMs - s_lastMs) / 1000.0;
                // static_cast<double>: u64→double implicit conversions are
                // C4244 candidates under MSVC /W4 /WX.
                const double kpm = (static_cast<double>(keys - s_lastKeys) / dt) * 60.0;   // keys/min
                s_kpmEma = (s_kpmEma == 0)
                    ? static_cast<std::int64_t>(kpm)
                    : static_cast<std::int64_t>(static_cast<double>(s_kpmEma) * 0.7 + kpm * 0.3);
            }
            s_lastKeys = keys;
            s_lastMs   = nowMs;
            std::swprintf(buf, std::size(buf), L"%lld", static_cast<long long>(s_kpmEma));
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_WPMVAL), buf);
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if (wParam == VK_RETURN &&
                ::SendMessageW(::GetDlgItem(hwnd, IDC_COMBO_CODETABLE),
                               CB_GETDROPPEDSTATE, 0, 0) == FALSE) {
                settingsFromControls();
                saveSettings();
                updateTrayIcon();
                ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            break;

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDOK:
                    settingsFromControls();
                    saveSettings();
                    updateTrayIcon();
                    [[fallthrough]];
                case IDCANCEL:
                    ::KillTimer(hwnd, 1);
                    ::DestroyWindow(hwnd);
                    g.hSettings = nullptr;
                    return 0;
                case IDC_BTN_APPLY:
                    settingsFromControls();
                    saveSettings();
                    updateTrayIcon();
                    return 0;
                default: break;
            }
            return 0;
        }

        case WM_CLOSE:
            ::KillTimer(hwnd, 1);
            ::DestroyWindow(hwnd);
            g.hSettings = nullptr;
            return 0;

        case WM_DESTROY:
            g.hSettings = nullptr;
            return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

//===========================================================================
// Main (hidden) window
//===========================================================================
LRESULT CALLBACK mainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_APP_TRAY:
            if (lParam == WM_LBUTTONDBLCLK) {
                if (g.hSettings) { ::SetForegroundWindow(g.hSettings); }
                else if (g.hInst) {
                    g.hSettings = ::CreateWindowExW(0, L"KieeKeySettings",
                                                    L"KieeKey — Cài đặt",
                                                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                                    WS_MINIMIZEBOX,
                                                    CW_USEDEFAULT, CW_USEDEFAULT, 480, 470,
                                                    nullptr, nullptr, g.hInst, nullptr);
                    ::ShowWindow(g.hSettings, SW_SHOW);
                }
            } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                showTrayMenu();
            }
            return 0;

        case WM_APP_TOGGLE:
            updateTrayIcon();
            return 0;

        case WM_ENDSESSION:
            g.hook.stop();          // consumer finalizer runs composer.detach()
            g.monitor.stop();
            return 0;

        case WM_DESTROY:
            g.hook.stop();          // consumer finalizer runs composer.detach()
            g.monitor.stop();
            NOTIFYICONDATAW nid{};
            nid.cbSize = sizeof(nid);
            nid.hWnd   = hwnd;
            nid.uID    = 1;
            ::Shell_NotifyIconW(NIM_DELETE, &nid);
            ::PostQuitMessage(0);
            return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

//===========================================================================
// Entry point
//===========================================================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    // Single instance
    HANDLE mutex = ::CreateMutexW(nullptr, TRUE, L"KieeKey_1.0.1_Singleton");
    if (mutex && ::GetLastError() == ERROR_ALREADY_EXISTS) {
        ::MessageBoxW(nullptr, L"KieeKey đang chạy (xem khay hệ thống).",
                      L"KieeKey", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // Raise THIS process's timer resolution to 1 ms (per-process on
    // Windows 10 2004+; reverted automatically at process exit, and by the
    // matching timeEndPeriod on the clean-exit path below). The
    // EditDrainBarrier's event wait is documented as hard-capped at
    // kWaitBudgetMs = 1 ms; without this, WaitForSingleObject(ev, 1) can
    // stretch to a full default clock interrupt (~15.6 ms) — the barrier's
    // worst-case hook-thread stall would silently be 15× its contract.
    // winmm is already linked (ok_core).
    (void)::timeBeginPeriod(1);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_TAB_CLASSES;
    ::InitCommonControlsEx(&icc);

    g.hInst = hInst;

    // Icons (green = ON, gray = OFF — same idea as the original OpenKey)
    g.hIconOn  = static_cast<HICON>(::LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON),
                                                 IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    g.hIconOff = static_cast<HICON>(::LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON_OFF),
                                                 IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    if (!g.hIconOn)  { g.hIconOn  = ::LoadIconW(nullptr, IDI_APPLICATION); }
    if (!g.hIconOff) { g.hIconOff = g.hIconOn; }

    // Settings from registry
    loadSettings();
    {
        std::lock_guard<std::mutex> lk(g.engineMtx);
        g.engine.setOptions(g.options);
    }
    g.monitor.setExcludeIde(g.exclIde);
    g.monitor.setExcludeGame(g.exclGame);
    g.monitor.setExcludeShell(g.exclShell);

    // v3.4 (S2): opt-in deferred inline output (diagnostic/fallback policy).
    // Default (unset) keeps the in-callback SendInput — the zero-hop path.
    if (const char* m = std::getenv("OPENKEY_INLINE_MODE");
        m != nullptr && std::strcmp(m, "deferred") == 0) {
        g.inlineDeferred = true;
    }

    // Window classes
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = mainProc;
    wc.hInstance     = hInst;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"KieeKeyMain";
    ::RegisterClassExW(&wc);
    WNDCLASSEXW ws{};
    ws.cbSize        = sizeof(ws);
    ws.lpfnWndProc   = settingsProc;
    ws.hInstance     = hInst;
    ws.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    ws.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    ws.lpszClassName = L"KieeKeySettings";
    ::RegisterClassExW(&ws);

    // Hidden main window (owns the tray icon)
    g.hMain = ::CreateWindowExW(0, L"KieeKeyMain", L"KieeKey",
                                WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);
    if (!g.hMain) { return 1; }

    // Process monitor (foreground detection, zero idle CPU)
    (void)g.monitor.start();
    g.monitor.refreshNow();
    updateExclusionCache();
    updateForegroundPolicy();

    // Hook: producer decides (engine runs on the hook thread, may suppress);
    // consumer emits the edits (TSF / SendInput fallback).
    g.hook.setProducerHandler(onHookEvent);
    // TSF/COM objects are created on the consumer thread (CoInitializeEx in
    // onConsumerEvent); tear them down there too, not from the main thread
    // after join (STA objects must be released on their creating thread).
    g.hook.setConsumerFinalizer([] { g.composer.detach(); });
    // v3.3.1: arm the hook self-healing watchdog (pump-tick heartbeat; a
    // silently-unhooked WH_KEYBOARD_LL is re-established within 5–15 ms).
    g.hook.enableSelfHealing();
    if (!g.hook.start(onConsumerEvent)) {
        ::MessageBoxW(g.hMain,
                      L"Không thể cài đặt hook bàn phím cấp thấp.\n\n"
                      L"Nếu bạn đang chạy với quyền quản trị, hãy thử mở lại "
                      L"chương trình không phải ở chế độ quản trị (hoặc ngược lại).",
                      L"KieeKey", MB_OK | MB_ICONERROR);
        g.hook.stop();
        g.monitor.stop();
        return 2;
    }

    addTrayIcon();

    // Message loop (IsDialogMessage gives the settings window Tab/Enter/Esc
    // navigation while it is open)
    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (g.hSettings && ::IsDialogMessageW(g.hSettings, &msg)) { continue; }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    g.hook.stop();      // consumer finalizer already ran composer.detach()
    g.monitor.stop();
    ::timeEndPeriod(1); // match timeBeginPeriod above (other exits: the OS
                        // reverts the resolution when the process dies)
    if (mutex) { ::ReleaseMutex(mutex); ::CloseHandle(mutex); }
    return static_cast<int>(msg.wParam);
}
