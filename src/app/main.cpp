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
// File: src/app/main.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — src/app/main.cpp
// The OpenKey-style Windows tray application (replaces the pre-tray console host):
//
//   * System tray icon (green = Vietnamese ON, gray = OFF) with the classic
//     right-click menu:  Bật/Tắt gõ tiếng Việt / Phương thức gõ
//     (Telex, VNI, Simple Telex) / Cài đặt… / Thoát.
//   * Double-click the tray icon → Cài đặt dialog (Win32, Vietnamese UI)
//     with tabs: Bàn phím / Ứng dụng / Gõ tắt / Chẩn đoán (live latency
//     telemetry).
//   * v1.1.1: Vietnamese input is turned on/off ONLY from inside the app —
//     the tray menu item or the always-visible toggle button in the settings
//     dialog. The old global Ctrl+Shift hotkey was REMOVED: it kept firing
//     on chords the user never intended (text selection, shortcuts, injected
//     events), silently switching the IME off mid-work with no visible
//     reason. Every on/off change shows a tray balloon and is persisted
//     immediately.
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
//     -finput-charset=UTF-8 -fexec-charset=UTF-8
//     -I src/core -I src/tsf src/app/main.cpp
//     src/core/ModernKeyHook.cpp src/core/ProcessMonitor.cpp
//     src/core/TextEngine.cpp src/tsf/TsfComposer.cpp
//     src/core/win32_wrapper.cpp src/app/app_res.o
//     -o KieeKey.exe -luser32 -lgdi32 -lshell32 -lole32
//     -lcomctl32 -ldwmapi -lpsapi -lversion -lurlmon -lwinmm -luuid
//   (win32_wrapper.cpp, -lwinmm and -luuid are REQUIRED: the wrapper's
//    watchdog/emitter translation units, the timeBeginPeriod timer calls
//    and FOLDERID_RoamingAppData, respectively. GCC relies on the
//    `#pragma comment(lib, ...)` autolink for winmm — clang/lld does not.)
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
#include <tlhelp32.h>   // v1.1.2-r3: CreateToolhelp32Snapshot (conflict detector)
#include <wtsapi32.h>   // v1.2.0: WTSRegisterSessionNotification (lock/unlock,
                        //          fast-user switching, RDP transitions)
#include <psapi.h>      // v1.3.0-beta7: GetProcessMemoryInfo for SystemSnapshot
#include <winver.h>     // v1.3.0-beta7: GetFileVersionInfo for PE version

// v1.2.0 Stable: WM_POWERBROADCAST event codes. The PBT_* set is versioned by
// _WIN32_WINNT in some SDK/MinGW header combinations, so the two this file
// uses are defaulted rather than assumed (the values are fixed by the
// Windows ABI — they cannot change between SDKs).
#ifndef PBT_APMSUSPEND
#define PBT_APMSUSPEND 0x0004
#endif
// v1.2.1 RC2: NIN_BALLOON* are gated on _WIN32_IE >= 0x0501 in shellapi.h;
// fixed ABI values (WM_USER + 2..5).
#ifndef NIN_BALLOONHIDE
#define NIN_BALLOONHIDE      (WM_USER + 3)
#endif
#ifndef NIN_BALLOONTIMEOUT
#define NIN_BALLOONTIMEOUT   (WM_USER + 4)
#endif
#ifndef NIN_BALLOONUSERCLICK
#define NIN_BALLOONUSERCLICK (WM_USER + 5)
#endif
#ifndef PBT_APMRESUMEAUTOMATIC
#define PBT_APMRESUMEAUTOMATIC 0x0012
#endif
#ifndef PBT_APMRESUME
// Not exposed by every MinGW winuser.h; the documented "resume after a
// critical/manual suspend" code.
#define PBT_APMRESUME 0x0007
#endif

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <shlobj.h>   // v1.1.0: SHGetKnownFolderPath (macros.txt in %APPDATA%)

#include "LockFreeQueue.hpp"
#include "ModernKeyHook.hpp"
#include "Notifications.hpp"   // v1.2.1 RC2: notification center + QuickTelex detector
#include "PerfProfile.hpp"     // v1.2.1 RC2: performance preference profiles
#include "ProcessMonitor.hpp"
#include "Profiler.hpp"
#include "TextEngine.hpp"
#include "Diagnostics.hpp"   // v1.3.0-beta4: recorder wiring + control panel
#include "TsfComposer.hpp"
#include "Win32RAII.hpp"
#include "win32_wrapper.hpp"   // v3.3.1: pipeline (OutputRing 1024, batched
                               // SendInput emitter, self-healing hook wrapper)
#include "Arcade.hpp"          // v1.3.0: Arcade hub & minigames
#include "LiveEffects.hpp"
#include "ChaosEngine.hpp"     // v1.3.0: Chaos case & glyph transforms
#include "AiRival.hpp"         // v1.3.0: Personal AI rival
#include "Progression.hpp"     // v1.3.0: Global progression & levels
#include "TypingAnalytics.hpp" // v1.3.0: Analytics & coach
#include "OnlineGhost.hpp"     // v1.3.0: Online & ghost abstraction
#include "ArcadeWindow.hpp"    // v1.3.0: graphical Arcade Hub window (Win32 GDI)
#include "ChaosLabWindow.hpp"  // v1.3.0: Chaos/Flexing lab window (real output)

#include "resource.h"
#include "DialogLayout.hpp"   // v1.3.0-beta4: runtime layout solver

using namespace ok::hook;
using namespace ok::text;
using ok::monitor::ProcessMonitor;
using ok::tsf::TsfComposer;

namespace {

//===========================================================================
// Single source of truth for the user-visible version strings.
//
// v1.2.0 Stable: these are the ONLY place the marketing version is spelled
// out. Every other carrier must match:
//   * src/app/KieeKeyApp.rc        — FILEVERSION / PRODUCTVERSION (numeric)
//                                    + FileVersion / ProductVersion strings
//   * src/app/KieeKeyApp.manifest  — assemblyIdentity version (numeric)
//   * CMakeLists.txt               — project(KieeKey VERSION …)
//   * README.md / CHANGELOG.md     — documentation
// The four carriers used to disagree (the .rc still carried 1,1,3,0 while its
// own strings said 1.2.0.0), which is how a user ends up unable to answer
// "which build am I running" from File Explorer. `scripts/check_version.py`
// fails the build if they drift apart again.
//===========================================================================
constexpr wchar_t kAppVersion[]     = L"1.3.0";           // numeric, 3-part
// v1.2.2 RC1: [[maybe_unused]] — this is a documented VERSION CARRIER
// (check_version.py reads it), not a code-level constant; the UI shows the
// title/version forms. Keeping it zero-maintenance and warning-clean.
[[maybe_unused]] constexpr wchar_t kAppVersionFull[] = L"1.3.0-beta8";  // with channel
constexpr wchar_t kAppTitle[]       = L"KieeKey v1.3.0-beta8";  // sync with kAppVersionFull

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
    // v1.3.0-beta3 (bug #4): race-free mirror of the macro ("Gõ tắt") editor's
    // EDIT HWND, written on the UI thread at dialog create/destroy and read on
    // the hook thread to decide whether the own-window bypass must stand aside
    // so Vietnamese can be composed into a macro expansion. An atomic avoids
    // reading the UI-thread hSettings on the hook thread (see ownWindowHasFocus).
    std::atomic<HWND> macroEdit{nullptr};
    HICON     hIconOn    = nullptr;
    HICON     hIconOff   = nullptr;

    ok::effects::LiveEffects liveEffects;
    ok::wrap::Win32Wrapper hook;   // v3.3.1: hook + rings + batched emitter +
                                   // self-healing watchdog (same surface as
                                   // ModernKeyHook — drop-in)
    ProcessMonitor monitor;
    TsfComposer    composer;
    TextEngine     engine;
    std::mutex     engineMtx;         // guards engine (producer thread vs dialog)
    OutputRing     outRing;           // producer → consumer

    std::atomic<bool> engineEnabled{true};
    std::atomic<bool> composerAttached{false};
    std::atomic<bool> balloonShown{false};
    // Cached auto-exclusion decision (updated on foreground change only —
    // keeps the per-key hot path to a single relaxed atomic load).
    std::atomic<bool> fgExcluded_{false};
    // Cached per-foreground output policy: true → TSF commit (flicker-prone
    // apps), false → inline SendInput (zero-latency, original-OpenKey style).
    // Also updated when the user changes the output mode.
    std::atomic<bool> fgUseTsf_{false};
    // v3.5 foreground-hang gate: true between a TSF-policy foreground switch
    // and the UI thread's bounded responsiveness probe (WM_APP_FGPROBE).
    // While set — or while the probe fails — output stays on inline
    // SendInput, which cannot wedge the pipeline in a synchronous TSF edit
    // session against a hung application (the historical "suddenly stops"
    // root cause).
    std::atomic<bool> fgProbePending_{false};
    // v3.5 diagnostics: number of foreground switches where the bounded
    // probe found the foreground window hung.
    std::atomic<std::uint64_t> fgHungCount{0};
    // v1.1.0 diagnostics: slow TSF commits observed by the composer watchdog
    // (the app downgrades the affected foreground to inline SendInput).
    std::atomic<std::uint64_t> tsfSlowCount{0};
    // WPM gauge: printable keydowns the engine actually processed.
    std::atomic<std::uint64_t> keysTyped{0};
    // v1.2.0 Stable — producer-fault telemetry + the deferred repair flag
    // armed by onHookEvent()'s noexcept trampoline. Both are expected to stay
    // at zero; a non-zero producerFailures means an edit was dropped in order
    // to keep the IME alive (strictly better than a dead process).
    std::atomic<std::uint64_t> producerFailures{0};
    std::atomic<std::uint64_t> consumerFailures{0};
    std::atomic<bool>          engineResyncPending{false};

    // v1.1.2-r3 diagnostics: times the hook-layer NUMBER-SAFETY GUARD had to
    // discard an engine decision for a digit event (expected to stay 0 —
    // the engine's own digitsAreLiteral tests already pin inertness; a
    // non-zero count means a future engine path regressed and the guard
    // held the line). Surfaced on the Information tab.
    std::atomic<std::uint64_t> digitGuardHits{0};
    std::atomic<std::uint64_t> shortcutGuardHits{0};   // v1.2.1 RC2 bug #3 guard
    // v1.1.2-r3 conflict detector cache (scanConflicts result). UI-thread
    // only: refreshed at startup and on every settings-dialog open.
    std::wstring conflictWarning;   // empty when clean
    std::wstring conflictDetail;

    // v3.4 (S1): event-driven ordering barrier. waitPendingEditsDrained()
    // spins ~2 µs, then waits on this barrier's auto-reset event — the
    // consumer signals it whenever pendingEdits transitions to 0 — hard-
    // capped at 1 ms. Replaces the v3.3.1 2 ms busy-spin that stalled the
    // hook thread (and on ≤2-core hosts stole the consumer's core).
    //
    // Declared BEFORE pendingEdits: the counter holds a reference to it.
    ok::wrap::EditDrainBarrier drainBarrier;

    // Edits the producer has published to outRing that the consumer has not
    // applied yet. Used by the ordering barrier: a pass-through key must not
    // reach the application while edits are pending, otherwise the app's
    // text gets ahead of the engine's buffer and the next edit's backspace
    // deletes the wrong characters (the "ghosting/sticking" when typing and
    // deleting quickly).
    //
    // v1.2.0 Stable: this was a bare std::atomic<std::uint32_t> with the
    // publish/consume/rollback rules spread over six call sites in this file.
    // It is now ok::wrap::PendingEditCounter — same hot-path cost (one
    // acq_rel RMW + one relaxed load), but the rules live in ONE unit-tested
    // place and the lifecycle recovery (forceQuiesce) exists at all. See
    // win32_wrapper.hpp for the two failure modes it closes.
    ok::wrap::PendingEditCounter pendingEdits{drainBarrier};

    // v3.4 (S2): inline output policy. Default stays hook-inline (zero
    // consumer hop — the frozen 1.055 µs burst p50 on quiet real hardware
    // covers the ENTIRE chain including the in-callback SendInput).
    // OPENKEY_INLINE_MODE=deferred moves inline edits onto the consumer
    // thread (OutputItem::Kind::InlineEdit) for hosts where in-callback
    // SendInput serialization is a concern; costs one ring hop + wake.
    bool inlineDeferred = false;

    // output mode: 0=Auto, 1=Always TSF, 2=Always SendInput (inline)
    std::atomic<int> outputMode{0};
    // v1.3.0-beta8: atomic mirror of g.options.codeTable for lock-free live-gate reads.
    // The UI thread writes g.options.codeTable under engineMtx; the hook/consumer
    // threads read the gate via liveGateNow() without locking. Reading the
    // non-atomic options field there is a data race (and stale-read drift). This
    // mirror is the single source for the gate and is kept in sync on every write.
    std::atomic<int> codeTableCache{static_cast<int>(CodeTable::Unicode)};

    // v1.2.1 RC2 — Performance preference profile (persisted) + hybrid
    // flags, and the RESOLVED strategy the hot paths read. Every consumer
    // of a tunable reads `strategy` fields through relaxed atomics mirrored
    // below (never the struct itself) so a profile switch from the UI
    // thread is race-free against the hook/consumer threads.
    std::atomic<int>          perfProfile{static_cast<int>(ok::perf::Profile::Balanced)};
    std::atomic<unsigned>     perfHybrid{ok::perf::kHybridNone};
    std::atomic<std::uint32_t> editBatchMax{32};              // consumer TSF batch cap
    std::atomic<bool>          layoutRecheckEveryKey{false};  // hook: HKL re-check per key
    std::atomic<bool>          tsfSlowDowngrade{true};        // auto inline on slow TSF
    std::atomic<bool>          deferInlineByProfile{false};   // profile-driven deferred inline
    std::atomic<int>           strategyOutput{0};             // 0 auto / 1 inline / 2 tsf
    std::atomic<std::uint64_t> lastKeyTickMs{0};              // adaptive: idle detection

    // v1.2.1 RC2 — notification center (policy engine; UI thread polls) and
    // the QuickTelex unwanted-correction detector (hook thread, under
    // engineMtx — O(1), allocation-free).
    ok::notify::NotificationCenter  notify;
    ok::notify::QuickTelexDetector  quickTelexDetector;
    char32_t                        lastRawKeyUpper = 0;     // hook-thread affine

    // Hook-thread-affine scratch (never touched from other threads):
    std::atomic<HKL> currentHkl{nullptr};  // cached foreground keyboard layout
    // v1.1.3: foreground HWND mirror for the cheap in-stroke HKL re-check —
    // Win+Space / Ctrl+Shift layout switches WITHIN one window fire no
    // EVENT_SYSTEM_FOREGROUND, so the cached layout used to go stale and
    // layoutChar() decoded keys under the previous layout (wrong base chars
    // composed until the next app switch). Refreshed on Space/WordBreak
    // (a few events per word — negligible cost, always fresh within a word).
    std::atomic<void*> fgHwnd{nullptr};
    std::wstring repScratch;               // replacementUtf16 scratch (engineMtx-guarded)

    // settings (GUI mirror; engine holds its own copy under engineMtx)
    // v1.1.2-r2 ROOT-CAUSE HARDENING: the APP default for "digits are
    // literal" is ON. EngineOptions' own default is false (legacy VNI parity
    // for library users), so a plain `EngineOptions options;` member meant
    // that ANY path where loadSettings() never ran — HKCU denied, corrupted
    // registry, sandboxed launch — silently shipped digits-as-composition,
    // i.e. the reported "typing a number applies a tone mark / changes the
    // word" bug, out of the box. The safe shipping default must live in the
    // app layer, not in a registry read that can fail.
    EngineOptions options = []() {
        EngineOptions o{};
        o.digitsAreLiteral = true;
        return o;
    }();
    bool exclIde   = true;
    bool exclGame  = true;
    bool exclShell = false;
    // v1.1.0: persisted Vietnamese on/off state (survives restarts —
    // OpenKey parity; previously every relaunch started enabled).
    bool enabledOnStart = true;
};
AppState g;
std::uint64_t g_startTickMs = 0; // v1.3.0-beta7: process start tick for SystemSnapshot uptime

//===========================================================================
// v1.1.0 — user macro table ("Gõ tắt"). The v1.0.x settings dialog exposed
// a macro checkbox while no resolver was ever installed: the feature was a
// silent no-op. Macros now load from %APPDATA%\KieeKey\macros.txt
// ("abbr=expansion" per line, '#' comments, UTF-8/UTF-16), are editable in
// the settings dialog, and are wired into the engine's MacroResolver
// contract (space expands, the break key is consumed — D3).
//===========================================================================
class MacroTable {
public:
    void clear() { map_.clear(); }

    void set(const std::wstring& abbrLower, const std::wstring& expansion) {
        if (abbrLower.empty()) { return; }
        map_[abbrLower] = expansion;
    }

    // Engine MacroResolver contract: `key` is the macro key accumulator
    // (internal encoding — low 16 bits are the UPPERCASE key char, bit 16
    // set when the user typed it uppercase). Matching folds case; the
    // expansion is emitted verbatim as final code points.
    bool find(const std::vector<std::uint32_t>& key,
              std::vector<std::uint32_t>& data) const {
        if (key.empty() || map_.empty()) { return false; }
        std::wstring abbr;
        abbr.reserve(key.size());
        for (const std::uint32_t v : key) {
            wchar_t c = static_cast<wchar_t>(v & 0xFFFFu);
            if (c >= L'A' && c <= L'Z') { c = static_cast<wchar_t>(c - L'A' + L'a'); }
            abbr.push_back(c);
        }
        const auto it = map_.find(abbr);
        if (it == map_.end()) { return false; }
        data.clear();
        data.reserve(it->second.size());
        for (const wchar_t wc : it->second) {
            data.push_back(static_cast<std::uint32_t>(wc));
        }
        return true;
    }

    [[nodiscard]] bool empty() const noexcept { return map_.empty(); }
    // Ordered view for the editor (stable, diff-friendly serialization).
    [[nodiscard]] const std::map<std::wstring, std::wstring, std::less<>>& items() const noexcept { return map_; }

private:
    std::map<std::wstring, std::wstring, std::less<>> map_;   // ordered → stable editor output
};

MacroTable g_macros;
// v1.1.0-audit fix: raw macros.txt content as read from disk (BOM-stripped,
// decoded). The settings editor is seeded from THIS, not from the
// regenerated canonical text, so the user's own comments/annotations in the
// file survive an Open→OK round-trip (the old flow rewrote the file from
// macrosToText() and silently discarded every hand-written comment).
std::wstring g_macroFileRaw;

// --- macros.txt persistence -------------------------------------------------
std::wstring macroFilePath() {
    wchar_t* appData = nullptr;
    std::wstring dir;
    if (S_OK != ::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)) {
        return L"";
    }
    dir = std::wstring(appData) + L"\\KieeKey";
    ::CoTaskMemFree(appData);
    ::CreateDirectoryW(dir.c_str(), nullptr);   // ok if it already exists
    return dir + L"\\macros.txt";
}

// Minimal UTF-8 → UTF-16 (the file is hand-edited; letters + ASCII '=' only
// in practice, but Vietnamese expansions need real decoding, not ASCII).
std::wstring utf8ToUtf16(const std::string& s) {
    if (s.empty()) { return L""; }
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                        static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) {
        ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                              out.data(), n);
    }
    return out;
}

std::string utf16ToUtf8(const std::wstring& s) {
    if (s.empty()) { return ""; }
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.data(),
                                        static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) {
        ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                              out.data(), n, nullptr, nullptr);
    }
    return out;
}

// Parse macro text (editor content or file content): one
// "abbr=expansion" per line; '#' or ";" comment lines; blank lines skipped.
void parseMacroText(const std::wstring& text) {
    std::wstring line;
    std::wistringstream iss(text);
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == L'\r') { line.pop_back(); }
        const std::size_t first = line.find_first_not_of(L" \t");
        if (first == std::wstring::npos) { continue; }
        if (line[first] == L'#' || line[first] == L';') { continue; }
        const std::size_t eq = line.find(L'=', first);
        if (eq == std::wstring::npos || eq == first) { continue; }
        std::wstring abbr = line.substr(first, eq - first);
        std::wstring expansion = line.substr(eq + 1);
        // trim spaces around both sides
        const auto trim = [](std::wstring& s) {
            const std::size_t b = s.find_first_not_of(L" \t");
            const std::size_t e = s.find_last_not_of(L" \t");
            s = (b == std::wstring::npos) ? L"" : s.substr(b, e - b + 1);
        };
        trim(abbr); trim(expansion);
        if (abbr.empty()) { continue; }
        std::transform(abbr.begin(), abbr.end(), abbr.begin(),
                       [](wchar_t c) {
                           return (c >= L'A' && c <= L'Z')
                                      ? static_cast<wchar_t>(c - L'A' + L'a') : c;
                       });
        g_macros.set(abbr, expansion);
    }
}

void loadMacros() {
    g_macros.clear();
    g_macroFileRaw.clear();
    const std::wstring path = macroFilePath();
    if (path.empty()) { return; }
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) {
        // First run: write a small commented template so the editor tab is
        // self-explanatory (the file doubles as the user documentation).
        std::ofstream out(path.c_str(), std::ios::binary);
        if (out) {
            out << "\xEF\xBB\xBF";   // UTF-8 BOM
            out << "# KieeKey macros (go tat) — one \"abbr=expansion\" per line.\n"
                   "# Lines starting with # are comments. Example (remove the # to use):\n"
                   "#cn=ch\xC3\xA0o\n"
                   "#hcm=H\xE1\xBB\x93 Ch\xED Minh\n";
        }
        g_macroFileRaw = utf8ToUtf16(
            "# KieeKey macros (go tat) — one \"abbr=expansion\" per line.\n"
            "# Lines starting with # are comments. Example (remove the # to use):\n"
            "#cn=ch\xC3\xA0o\n"
            "#hcm=H\xE1\xBB\x93 Ch\xED Minh\n");
        return;
    }
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // BOM handling: UTF-16 LE (Notepad "Unicode") or UTF-8. Decoded byte-wise
    // (no reinterpret_cast — alignment/strict-aliasing safe).
    if (raw.size() >= 2 && raw[0] == '\xFF' && raw[1] == '\xFE') {
        std::wstring text;
        text.reserve((raw.size() - 2) / 2);
        for (std::size_t i = 2; i + 1 < raw.size(); i += 2) {
            text.push_back(static_cast<wchar_t>(static_cast<unsigned char>(raw[i]) |
                          (static_cast<unsigned>(static_cast<unsigned char>(raw[i + 1])) << 8)));
        }
        g_macroFileRaw = text;   // comments survive the editor round-trip
        parseMacroText(text);
    } else {
        if (raw.size() >= 3 && raw[0] == '\xEF' && raw[1] == '\xBB' && raw[2] == '\xBF') {
            raw.erase(0, 3);
        }
        g_macroFileRaw = utf8ToUtf16(raw);
        parseMacroText(g_macroFileRaw);
    }
}

std::wstring macrosToText() {
    // Regenerate the canonical file content from the table (ordered map →
    // stable, diff-friendly output).
    std::wstring out =
        L"# KieeKey macros (gõ tắt) — mỗi dòng một \"từ gọn=kết quả\".\r\n"
        L"# Dòng bắt đầu bằng # là ghi chú. Ví dụ:\r\n"
        L"#cn=chào\r\n"
        L"#hcm=Hồ Chí Minh\r\n";
    for (const auto& [abbr, expansion] : g_macros.items()) {
        out += abbr;
        out += L'=';
        out += expansion;
        out += L"\r\n";
    }
    return out;
}

// v1.1.0-audit fix: table swap and file write are now SEPARATE. The swap
// (parse + replace g_macros) is the only part that must run under engineMtx
// (the hook thread's resolver reads g_macros through that lock); the file
// write is disk I/O and moved OUT of the lock — under it, a synced/AV-scanned
// %APPDATA% write stalled the LL-hook keystroke path (Windows removes a
// low-level hook whose callback exceeds the internal timeout).
void applyMacrosText(const std::wstring& text) {
    g_macros.clear();
    parseMacroText(text);
}

// v1.3.0-beta5 (G2 sweep, same class as bug B8): returns whether the file was
// actually written — the void version turned a failed write (read-only
// %APPDATA%, full disk, policy) into a silent no-op: the user pressed OK, saw
// no error, and the macros were gone after the next restart.
bool writeMacrosFile(const std::wstring& text) {
    const std::wstring path = macroFilePath();
    if (path.empty()) { return false; }
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) { return false; }
    const std::string utf8 = utf16ToUtf8(text);
    out << "\xEF\xBB\xBF" << utf8;
    out.flush();
    return out.good();
}

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
        g.codeTableCache.store(static_cast<int>(g.options.codeTable), std::memory_order_relaxed);
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
        // v1.1.0: persisted Vietnamese on/off (OpenKey parity — the old
        // build forgot the toggle on every restart).
        g.enabledOnStart                   = key.getDword(L"Enabled", 1) != 0;
        // v1.2.1 RC2: performance profile + hybrid flags + notification
        // suppressions (the ONLY persisted notification state).
        const DWORD pp = key.getDword(L"PerfProfile", 0);
        g.perfProfile.store(static_cast<int>(ok::perf::profileFromIndex(pp)), std::memory_order_relaxed);
        g.perfHybrid.store(key.getDword(L"PerfHybrid", 0) & 0x0Fu, std::memory_order_relaxed);
        g.options.useDictionaryRestore     = key.getDword(L"DictionaryRestore", 0) != 0;
        // v1.1.2-r2 ONE-TIME SETTINGS MIGRATION (self-heal).
        //
        // SettingsMigration < 2 identifies any install that has not yet run
        // this release's migration: a fresh machine (no DigitsLiteral value
        // at all) OR a machine touched by the first v1.1.2 build. On those
        // the digits policy is re-asserted to the shipping default ON and
        // persisted immediately — so a poisoned DigitsLiteral=0 (written by
        // any earlier defect rather than by a deliberate user choice) can
        // never survive the upgrade. After the marker is set, the user's
        // own checkbox choice is loaded as-is and respected forever.
        const DWORD mig = key.getDword(L"SettingsMigration", 0);
        if (mig < 2) {
            g.options.digitsAreLiteral     = true;
            key.setDword(L"DigitsLiteral", 1);
            key.setDword(L"SettingsMigration", 2);
        } else {
            g.options.digitsAreLiteral     = key.getDword(L"DigitsLiteral", 1) != 0;
        }
        // v1.3.0-beta5 (bug B6): the arcade run configuration is PERSISTED.
        // Before this, "Áp dụng cấu hình game" wrote ArcadeManager's in-memory
        // config only — every restart came back with the defaults while the
        // dialog showed the user's last choices nowhere, so the button looked
        // broken across sessions. Values are read back with the same ranges
        // the apply button validates.
        {
            auto arcade = ok::arcade::ArcadeManager::instance().getConfig();
            const DWORD fm = key.getDword(L"ArcadeFailMode", 0);
            arcade.rhythmFailMode = (fm == 1) ? ok::arcade::FailMode::HealthBar
                                              : ok::arcade::FailMode::Hardcore;
            arcade.noMistakeFailMode = arcade.rhythmFailMode;
            const DWORD bpm = key.getDword(L"ArcadeRhythmBpm", 112);
            if (bpm >= 60 && bpm <= 220) { arcade.rhythmBpm = static_cast<double>(bpm); }
            const DWORD lang = key.getDword(L"ArcadePassageLang", 0);
            arcade.passageLanguage = (lang == 1) ? ok::arcade::PassageLanguage::English
                                                 : ok::arcade::PassageLanguage::Vietnamese;
            const DWORD steer = key.getDword(L"ArcadeSteering", 0);
            arcade.wasdSteering = static_cast<ok::arcade::WasdSteering>(
                std::clamp<DWORD>(steer, 0, 2));
            // The composition method follows the IME method loaded above.
            arcade.vnInputMethod =
                static_cast<ok::arcade::VnInputMethod>(g.options.inputMethod);
            ok::arcade::ArcadeManager::instance().setConfig(arcade);
        }
        // v1.3.0-beta5 (bug B9): the Live-effects / Chaos / AI-opt-in toggles
        // (tabs 6-7) applied on click but were NEVER persisted — every restart
        // silently reverted them to defaults, the same "I enabled it and it
        // does not work" class as the arcade configuration above. Registry
        // defaults mirror the in-code Config defaults (live: off, case on,
        // glyph none, intensity 50; chaos: all off; AI: opt-in off).
        {
            ok::effects::Config live{};
            live.enabled    = key.getDword(L"LiveEnabled", 0) != 0;
            live.randomCase = key.getDword(L"LiveCase", 1) != 0;
            const DWORD glyph = key.getDword(L"LiveGlyph", 0);
            live.glyph      = static_cast<ok::effects::Glyph>(std::clamp<DWORD>(glyph, 0, 3));
            const DWORD intensity = key.getDword(L"LiveIntensity", 50);
            live.intensity  = (intensity == 25 || intensity == 50 ||
                               intensity == 75 || intensity == 100) ? intensity : 50u;
            g.liveEffects.configure(live);

            auto chaos = ok::chaos::ChaosEngine::instance().getConfig();
            chaos.masterEnabled         = key.getDword(L"ChaosMaster", 0) != 0;
            chaos.randomCaseEnabled     = key.getDword(L"ChaosCase", 0) != 0;
            chaos.glyphTransformEnabled = key.getDword(L"ChaosGlyph", 0) != 0;
            // v1.3.0-beta6 (V3): the Chaos Lab's slider / mode / granularity
            // used to reset to engine defaults on every restart (only the
            // three checkboxes were persisted). Mirror of saveSettings().
            // The two intensities are INDEPENDENT fields (the web bridge
            // exposes both), so they are persisted separately; the Lab's
            // single slider happens to write the same value into both.
            const DWORD caseIntensity = std::clamp<DWORD>(
                key.getDword(L"ChaosIntensityPercent", 50), 0, 100);
            chaos.randomCaseIntensity = static_cast<float>(caseIntensity) / 100.0f;
            const DWORD glyphIntensity = std::clamp<DWORD>(
                key.getDword(L"ChaosGlyphIntensityPercent", 100), 0, 100);
            chaos.glyphIntensity = static_cast<float>(glyphIntensity) / 100.0f;
            const DWORD glyphMode = key.getDword(L"ChaosGlyphMode", 0);
            chaos.glyphMode = static_cast<ok::chaos::GlyphTransformMode>(
                std::clamp<DWORD>(glyphMode, 0, 6));
            const DWORD granularity = key.getDword(L"ChaosCaseGranularity", 0);
            chaos.caseGranularity = (granularity == 1) ? ok::chaos::CaseGranularity::ByWord
                                                       : ok::chaos::CaseGranularity::ByChar;
            ok::chaos::ChaosEngine::instance().setConfig(chaos);

            ok::ai::AiRivalEngine::instance().setOptIn(key.getDword(L"AiOptIn", 0) != 0);
        }
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
        // v1.1.0: persist the Vietnamese on/off state at every change point.
        key.setDword(L"Enabled",           g.engineEnabled.load(std::memory_order_relaxed) ? 1 : 0);
        // v1.2.1 RC2
        key.setDword(L"PerfProfile",       static_cast<DWORD>(g.perfProfile.load(std::memory_order_relaxed)));
        key.setDword(L"PerfHybrid",        static_cast<DWORD>(g.perfHybrid.load(std::memory_order_relaxed)));
        key.setDword(L"DictionaryRestore", g.options.useDictionaryRestore ? 1 : 0);
        // v1.1.2: digits-are-numbers option (the fix for "typing a number
        // produced a tone mark / changed the word").
        key.setDword(L"DigitsLiteral",     g.options.digitsAreLiteral ? 1 : 0);
        // v1.1.2-r2: keep the settings-schema version self-maintaining (the
        // migration in loadSettings() has always run first at startup; this
        // makes the marker explicit on every persist anyway).
        key.setDword(L"SettingsMigration", 2);
        // v1.3.0-beta5 (bug B6): persist the applied arcade run configuration
        // (fail mode, rhythm BPM, passage language, WASD steering choice).
        {
            const auto arcade = ok::arcade::ArcadeManager::instance().getConfig();
            key.setDword(L"ArcadeFailMode",
                         arcade.rhythmFailMode == ok::arcade::FailMode::HealthBar ? 1 : 0);
            key.setDword(L"ArcadeRhythmBpm", static_cast<DWORD>(arcade.rhythmBpm));
            key.setDword(L"ArcadePassageLang",
                         arcade.passageLanguage == ok::arcade::PassageLanguage::English ? 1 : 0);
            key.setDword(L"ArcadeSteering", static_cast<DWORD>(arcade.wasdSteering));
        }
        // v1.3.0-beta5 (bug B9): Live-effects / Chaos / AI-opt-in (mirror of
        // the loadSettings() block; raw values, not combo indices).
        {
            const auto live = g.liveEffects.config();
            key.setDword(L"LiveEnabled",   live.enabled ? 1 : 0);
            key.setDword(L"LiveCase",      live.randomCase ? 1 : 0);
            key.setDword(L"LiveGlyph",     static_cast<DWORD>(live.glyph));
            key.setDword(L"LiveIntensity", live.intensity);
            const auto chaos = ok::chaos::ChaosEngine::instance().getConfig();
            key.setDword(L"ChaosMaster",   chaos.masterEnabled ? 1 : 0);
            key.setDword(L"ChaosCase",     chaos.randomCaseEnabled ? 1 : 0);
            key.setDword(L"ChaosGlyph",    chaos.glyphTransformEnabled ? 1 : 0);
            // v1.3.0-beta6 (V3): persist the Lab knobs too (intensity, glyph
            // mode, case granularity) — they reach ChaosEngine from the Lab
            // window and the web bridge, and saveSettings() runs on every
            // settings change point plus the final teardown sweep, so the
            // registry always mirrors the last config the user applied.
            key.setDword(L"ChaosIntensityPercent",
                         static_cast<DWORD>(chaos.randomCaseIntensity * 100.0f + 0.5f));
            key.setDword(L"ChaosGlyphIntensityPercent",
                         static_cast<DWORD>(chaos.glyphIntensity * 100.0f + 0.5f));
            key.setDword(L"ChaosGlyphMode", static_cast<DWORD>(chaos.glyphMode));
            key.setDword(L"ChaosCaseGranularity",
                         chaos.caseGranularity == ok::chaos::CaseGranularity::ByWord ? 1 : 0);
            key.setDword(L"AiOptIn",
                         ok::ai::AiRivalEngine::instance().isOptIn() ? 1 : 0);
        }
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
    // v1.1.0: numpad digits (NumLock on) — previously unmapped, so numpad
    // entry fell through the layout resolver as pass-through (no VNI digit
    // marks, no macro/abbreviation capture from the numpad).
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        return static_cast<char32_t>(U'0' + (vk - VK_NUMPAD0));
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
        // Dead key — v1.1.0: flush the pending dead state with a SPACE (the
        // documented clear sequence) instead of repeating the same key (which
        // leaves the dead state armed and composed the NEXT letter into an
        // accented char on this thread). The flush resolves the spacing dead
        // character itself (so '^' alone still types '^') and our cached
        // layout state stays clean for the following keystrokes.
        BYTE kbClean[256] = {};
        wchar_t flush[8] = {};
        const int m = ::ToUnicodeEx(VK_SPACE, 0x39, kbClean, flush, 8, 0, hkl);
        return (m >= 1) ? static_cast<char32_t>(flush[0]) : 0;
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
    // v1.2.1 RC2: the explicit output-mode radio wins; when it is Auto the
    // resolved performance strategy decides (Fastest → inline, LeastFlicker
    // → TSF, Balanced/MaxCorrectness → the per-app table).
    int mode = g.outputMode.load(std::memory_order_relaxed);
    if (mode == 0) {
        const int so = g.strategyOutput.load(std::memory_order_relaxed);
        if (so == 1) { mode = 2; } else if (so == 2) { mode = 1; }
    }
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
    g.fgHwnd.store(s ? static_cast<void*>(s->hwnd) : nullptr,
                   std::memory_order_relaxed);
}

// v1.1.3 — re-read the foreground thread's keyboard layout. GetKeyboardLayout
// and GetWindowThreadProcessId are user-mode reads (no syscall); calling this
// on Space / WordBreak keystrokes keeps the layout fresh across in-window
// language switches at zero measurable cost.
void refreshLayoutCache() noexcept {
    const HWND fg = static_cast<HWND>(g.fgHwnd.load(std::memory_order_relaxed));
    g.currentHkl.store(fg ? ::GetKeyboardLayout(::GetWindowThreadProcessId(fg, nullptr))
                          : ::GetKeyboardLayout(0),
                       std::memory_order_relaxed);
}

//===========================================================================
// v1.2.1 RC2 — Performance preference profiles: resolve + apply.
//
// ONE function applies the centralized strategy (PerfProfile.hpp) to every
// runtime knob: consumer spin bounds (ModernKeyHook), ordering-barrier
// budget/spin (EditDrainBarrier), consumer TSF batch cap, output preference
// (feeds updateForegroundPolicy), deferred-inline routing, per-key layout
// re-check and the engine's dictionary restore. Called on the UI thread at
// startup, on every settings apply, and (Adaptive only) from the 1 s
// telemetry tick. Never called on the hook thread.
//===========================================================================
ok::perf::Telemetry collectTelemetry() noexcept {
    ok::perf::Telemetry t;
    t.tsfSlowCommits  = g.tsfSlowCount.load(std::memory_order_relaxed);
    t.barrierTimeouts = g.drainBarrier.timeouts();
    const std::uint64_t now = ::GetTickCount64();
    const std::uint64_t last = g.lastKeyTickMs.load(std::memory_order_relaxed);
    t.idleSeconds = last ? static_cast<std::uint32_t>((now - last) / 1000) : 0;
    SYSTEM_POWER_STATUS ps{};
    if (::GetSystemPowerStatus(&ps)) { t.onBattery = (ps.ACLineStatus == 0); }
    SYSTEM_INFO si{};
    ::GetNativeSystemInfo(&si);
    t.logicalCpus = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;
    return t;
}

ok::perf::Strategy g_appliedStrategy = ok::perf::resolveStrategy(ok::perf::Profile::Balanced);

// Returns true when the strategy changed. `lockEngine` — the caller does not
// already hold engineMtx.
bool g_strategyApplied = false;   // false until the first apply (startup)

bool applyPerfStrategy(bool lockEngine, bool force = false) noexcept {
    const auto profile = ok::perf::profileFromIndex(
        static_cast<std::uint32_t>(g.perfProfile.load(std::memory_order_relaxed)));
    const auto hybrid  = static_cast<std::uint8_t>(g.perfHybrid.load(std::memory_order_relaxed) & 0x0Fu);
    const ok::perf::Strategy st = ok::perf::resolveStrategy(profile, hybrid, collectTelemetry());
    if (!force && g_strategyApplied && st == g_appliedStrategy) {
        return false;
    }
    g_strategyApplied = true;
    g_appliedStrategy = st;
    g.hook.setConsumerSpinBounds(st.consumerSpinFloorUs, st.consumerSpinCapUs);
    g.drainBarrier.setTuning((st.barrierBudgetUs + 999) / 1000, st.barrierSpinIters);
    g.editBatchMax.store(std::clamp<std::uint32_t>(st.editBatchMax, 1, 64), std::memory_order_relaxed);
    g.layoutRecheckEveryKey.store(st.layoutRecheckEveryKey, std::memory_order_relaxed);
    g.tsfSlowDowngrade.store(st.tsfSlowDowngrade, std::memory_order_relaxed);
    g.deferInlineByProfile.store(st.deferInlineToConsumer, std::memory_order_relaxed);
    g.strategyOutput.store(static_cast<int>(st.output), std::memory_order_relaxed);
    // Engine-side: the dictionary restore is a real EngineOptions field.
    // The user's explicit checkbox state is kept in g.options; the profile
    // only ever turns it ON (MaxCorrectness / ExtraCorrect hybrid).
    const bool wantDict = st.dictionaryRestore || g.options.useDictionaryRestore;
    {
        std::unique_lock<std::mutex> lk(g.engineMtx, std::defer_lock);
        if (lockEngine) { lk.lock(); }
        if (g.engine.options().useDictionaryRestore != wantDict) {
            EngineOptions o = g.engine.options();
            o.useDictionaryRestore = wantDict;
            g.engine.setOptions(o);
        }
    }
    updateForegroundPolicy();
    return true;
}

// Inline zero-latency output (hook thread). Exactly what original OpenKey
// did: backspaces + Unicode text queued straight into the input stream.
// Normal edits fit one SendInput call (backspace ≤ kMaxBuff=32 — the D2
// engine policy — and replacement ≤ 2*kMaxBuff wchar_t): 32*2 + 64*2 INPUTs
// ≈ 5.4 KiB on the stack, zero heap. Macro expansions (D3) can be longer —
// the loop below flushes the batch and keeps going, so ANY payload size is
// safe (chunked, still ordered, still self-tagged).
// v1.2.1 Stable: the app-local stack-batch constant was deleted — the
// emitter moved to the wrapper layer (ok::wrap::InlineEmitter with its own
// kMaxInlineInputs) and this copy had been dead since v3.3.1. Nothing here
// builds INPUT arrays any more; every inline emit goes through
// g.hook.emitter().sendEdit(), which owns the batching.
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
// v1.3.0: every arcade launcher opens the graphical Arcade Hub window.
// The games used to be rendered as ASCII into a static control in the settings
// dialog; the hub is now a real GDI window (src/app/ArcadeWindow.cpp) that
// paints the same RenderList the HTML5 client consumes.
void openArcadeHub(const char* slug) {
    ok::app::ArcadeWindow& hub = ok::app::ArcadeWindow::instance();
    // v1.3.0-beta5 (G2 sweep, same class as bug B8): the bool was discarded —
    // a failed hub creation (class registration / CreateWindowEx) left every
    // arcade entry point silently dead. Surface the Win32 error instead.
    if (!hub.open(g.hMain, slug != nullptr ? std::string(slug) : std::string())) {
        const DWORD err = ::GetLastError();
        wchar_t detail[256];
        ::swprintf(detail, std::size(detail),
                   L"Không mở được cửa sổ Arcade Hub (mã lỗi Win32: %lu).\n"
                   L"Hãy thử khởi động lại KieeKeyApp.exe; nếu vẫn lỗi, gửi mã này "
                   L"kèm báo cáo (tab Chẩn đoán).",
                   err);
        ::MessageBoxW(g.hMain, detail, L"KieeKey — Arcade Hub", MB_OK | MB_ICONERROR);
        return;
    }
    hub.focus();
}

//===========================================================================
// v1.3.0-beta6 (V4) — TESTER EVIDENCE for the Windows-only residuals.
//
// beta5 closed with four questions only the tester's machine can answer:
// B2 (do live effects really reach external apps?), B4 (is the foreground
// process name really resolved?), B1 (what are the real DPI/font/DWM
// numbers?), B8 (does the MessageBox path work?). The diagnostics report
// now carries MACHINE-READABLE lines for the first three; these helpers
// fill them. Everything here is noexcept + exception-swallowing: it runs on
// the hook/consumer threads, and evidence must never take the IME down.
//===========================================================================
namespace {

// The gate verdict for the CURRENT live flags — the same pure model the
// hook evaluates per key (ok::effects::liveGateBlocker). Single source of
// truth for the UI readout AND the emit-chain evidence lines.
ok::effects::GateBlocker liveGateNow() noexcept {
    const bool ime = g.engineEnabled.load(std::memory_order_relaxed);
    const bool excl = g.fgExcluded_.load(std::memory_order_relaxed);
    const bool master = g.liveEffects.enabled();
    const bool uni = static_cast<CodeTable>(g.codeTableCache.load(std::memory_order_relaxed)) == CodeTable::Unicode;
    return ok::effects::liveGateBlocker(ime, excl, master, uni);
}

// One delivery reached an output layer: record WHERE it went (foreground
// window class + process from the monitor snapshot) and the gate state at
// emit time. channel: 0 = TSF commit, 1 = SendInput.
void recordEmitEvidence(std::uint8_t channel, std::uint32_t chars) noexcept {
    // Gated like the consumer's own counters: at level Off the only cost is
    // one relaxed load, so the hot path stays honest. The ring itself is a
    // single-writer mutex (only the output thread records deliveries).
    if (!ok::diag::Diagnostics::instance().atLeast(ok::diag::Level::Basic)) {
        return;
    }
    try {
        ok::diag::EmitRecord rec{};
        rec.channel = channel;
        rec.chars = chars;
        rec.gate = static_cast<std::uint8_t>(liveGateNow());
        const HWND fg = ::GetForegroundWindow();
        wchar_t cls[64]{};
        if (fg != nullptr && ::GetClassNameW(fg, cls, 64) > 0) {
            rec.windowClass = utf16ToUtf8(std::wstring(cls));
        }
        if (const auto snap = g.monitor.snapshot()) {
            rec.pid = snap->pid;
            rec.processName = snap->exeNameUtf8;
        }
        ok::diag::Diagnostics::instance().recordEmit(std::move(rec));
    } catch (...) {
        // Evidence collection must never kill the IME.
    }
}

// Filled near windowDpi() (it needs the DPI helpers defined later).
void refreshEvidenceContext() noexcept;
void refreshSystemSnapshot() noexcept;
void syncDiagnosticsCounters() noexcept;
void refreshDiagnostics() noexcept;

} // namespace

void emitInline(std::size_t backspace, const std::wstring& text) noexcept {
    g.hook.emitter().sendEdit(backspace, text);
    // v1.3.0-beta7: keep Diagnostics in sync with the real emitter.
    // SendInputCalls was stuck at 0 while the emit-chain trace showed 13
    // deliveries: the counter was only incremented on the deferred
    // consumer path, never on the hot inline path. One relaxed add here
    // closes that gap with no hook-thread cost.
    if (backspace != 0 || !text.empty()) {
        ok::diag::Diagnostics::instance().add(ok::diag::Counter::SendInputCalls);
        // Backspace-only edits still inject input; count once per emit.
    }
    // v1.3.0-beta6 (V4): the producer-side SendInput sink — ring-full
    // fallbacks, inline decisions and the Chaos Lab injection all land here.
    if (!text.empty()) {
        recordEmitEvidence(1, static_cast<std::uint32_t>(text.size()));
    }
}

// v1.3.0: the Chaos Lab window types its transformed text through the very same
// emitter the IME uses, so "chaos mode gõ thật được ngoài" is testable without
// touching the global chaos switch.
std::size_t emitForChaosLab(const std::wstring& text) {
    if (text.empty()) {
        return 0;
    }
    emitInline(0, text);
    return text.size();
}

// Opens the Chaos / Flexing lab window (registers the emitter hook first).
// v1.3.0-beta5 (bug B8): the bool was discarded — when the window failed to
// open (class registration or CreateWindowEx), every entry point (tray menu,
// tab-6 button, --chaos-lab) did NOTHING at all and the user had no idea the
// click was even received. Surface the failure with the actual Win32 error so
// a report can say more than "it doesn't open".
void openChaosLab() {
    ok::app::ChaosLabWindow::setEmitCallback(&emitForChaosLab);
    if (!ok::app::ChaosLabWindow::instance().open(g.hMain)) {
        const DWORD err = ::GetLastError();
        wchar_t detail[256];
        ::swprintf(detail, std::size(detail),
                   L"Không mở được cửa sổ Chaos Lab (mã lỗi Win32: %lu).\n"
                   L"Hãy thử khởi động lại KieeKeyApp.exe; nếu vẫn lỗi, gửi mã này "
                   L"kèm báo cáo (tab Chẩn đoán).",
                   err);
        ::MessageBoxW(g.hMain, detail, L"KieeKey — Chaos Lab", MB_OK | MB_ICONERROR);
    }
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
    // v1.1.3: an ELEVATED foreground joins the auto-exclusion set (hard UIPI
    // constraint — see ForegroundInfo::elevated). Both output paths fail
    // silently there; passing keystrokes through untouched is the only safe
    // behavior, and it matches how the app already treats IDEs/games.
    const bool excluded = g.monitor.currentAppAutoExcluded() ||
                          g.monitor.currentAppElevated();
    g.fgExcluded_.store(excluded, std::memory_order_relaxed);
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
#if KIEEKEY_PROFILE
// ns spent in the most recent ordering-barrier wait (hook thread only).
// v1.1.0: declared BEFORE waitPendingEditsDrained() which uses it (the old
// use-before-declaration only compiled because the default build drops the
// profiler entirely; KIEEKEY_PROFILE=1 failed with C2065).
inline std::uint32_t t_lastBarrierNs = 0;
#endif

void waitPendingEditsDrained() noexcept {
    // PendingEditCounter::waitDrained() is [[nodiscard]] on purpose — a
    // lifecycle drain MUST know whether the wait was satisfied or timed out
    // (see drainPendingEditsForLifecycle). This call site is the plain
    // pass-through ordering guard, where a timeout degrades to the same
    // "deliver the key anyway" behaviour either way, so the result is
    // deliberately discarded. (MSVC /W4 /WX turns a bare discard into
    // C4834, which broke the v1.2.0 Stable CI build.)
#if KIEEKEY_PROFILE
    const std::uint64_t profT0 = ok::prof::qpcNow();
    static_cast<void>(g.pendingEdits.waitDrained());
    // The barrier cost lands on the NEXT pass-through record (the key being
    // delivered): the hook thread reads t_lastBarrierNs when building its
    // StageRecord. Overwritten on every barrier call (serialized thread).
    t_lastBarrierNs = ok::prof::nsSince(profT0);
#else
    static_cast<void>(g.pendingEdits.waitDrained());
#endif
}

//===========================================================================
// v1.2.0 Stable — bounded drains for LIFECYCLE transitions.
//
// waitPendingEditsDrained() (above) only WAITS. That is correct on the
// steady-state path, where the producer keeps queueing work and therefore
// keeps waking the consumer. It is NOT enough at a lifecycle transition:
// once the producer stops queueing key events (engine switched off,
// foreground auto-excluded, power resume, session unlock), a count that was
// already published has nothing left to wake the consumer for. The wait then
// burns its full 1 ms budget and gives up with the count STILL armed — and
// that armed count degraded every later pass-through keystroke by the full
// barrier budget. This is the "sometimes typing takes a moment, then it goes
// away" report.
//
// Both helpers poke the consumer first (SetEvent — the consumer wakes, drains
// the out-ring and releases the count) and then wait.
//===========================================================================
// Hook-thread flavour: bounded to the barrier budget, never forces a reset.
// (An edit may legitimately still be in flight against a slow foreground;
//  the TSF slow-commit watchdog owns that case.)
void drainPendingEditsForHook() noexcept {
    if (g.pendingEdits.pending() == 0) { return; }
    if (g.pendingEdits.waitDrained()) { return; }
    // Stranded: nobody is going to wake the consumer. One SetEvent here is
    // paid at most once per transition — never on the steady-state path.
    g.hook.pokeConsumer();
    static_cast<void>(g.pendingEdits.waitDrained());
}

// UI/lifecycle flavour: may force quiescence. Only valid where the caller
// guarantees no further edits can be published (the engine is off, or the
// pipeline is being torn down) — that is what makes clearing the count a
// recovery rather than an ordering violation.
void drainPendingEditsForLifecycle() noexcept {
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (g.pendingEdits.pending() == 0) { return; }
        g.hook.pokeConsumer();
        if (g.pendingEdits.waitDrained()) { return; }
    }
    // The consumer demonstrably cannot make progress (the classic wedge: a
    // synchronous TSF edit session marshalled into a hung application's STA).
    // Leaving the count armed would stall every following keystroke, and the
    // edits it counts can never be applied anyway — clear it and say so.
    g.pendingEdits.forceQuiesce();
}

// Queue a consumer-side re-sync: read the visible word before the caret via
// TSF and replay it into the engine (resumeFromText). Only meaningful on the
// TSF output path (the inline/SendInput path has no document access). Returns
// true iff a Resync item was actually queued (the consumer must be woken).
bool requestContextResync() noexcept {
    // Visual Unicode is not a reversible representation of the source word.
    // Never replay it into the Vietnamese engine while live effects are on.
    if (g.liveEffects.enabled()) { return false; }
    if (!g.fgUseTsf_.load(std::memory_order_relaxed)) { return false; }
    OutputItem it;
    it.kind = OutputItem::Kind::Resync;
    return g.outRing.try_push(it);   // false if full — degraded only
}

// Producer handler: runs on the hook pump thread (serialized). Returns the
// hook decision — whether to swallow the key AND whether the consumer thread
// has queued work that needs a wake (see ModernKeyHook::ProducerDecision).
using PD = ok::hook::ModernKeyHook::ProducerDecision;

// Only keyboard input is bypassed in our UI. Foreground/mouse bookkeeping
// still runs so the next external composition cannot inherit a stale word.
// Query ownership instead of reading UI-thread HWNDs or constructing window
// singletons on the low-level hook thread (both raced window creation/close).
bool ownWindowHasFocus() noexcept {
    DWORD processId = 0;
    const HWND foreground = ::GetForegroundWindow();
    if (foreground == nullptr) { return false; }
    ::GetWindowThreadProcessId(foreground, &processId);
    return processId == ::GetCurrentProcessId();
}

// v1.3.0-beta3 (bug #4): true when the currently focused control is the macro
// ("Gõ tắt") editor EDIT in our own settings dialog. The own-window bypass must
// stand aside there so the engine composes Vietnamese into a macro expansion
// (Telex "tieengs" -> "tiếng") instead of leaving the raw keystrokes. Reads only
// the atomic mirror published on the UI thread — never the UI-thread hSettings —
// and returns false on any doubt (so the safe default is always "keep bypassing").
bool focusIsMacroEditor() noexcept {
    const HWND macroEdit = g.macroEdit.load(std::memory_order_acquire);
    if (macroEdit == nullptr) { return false; }
    const HWND foreground = ::GetForegroundWindow();
    if (foreground == nullptr) { return false; }
    const DWORD tid = ::GetWindowThreadProcessId(foreground, nullptr);
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);
    if (!::GetGUIThreadInfo(tid, &gui)) { return false; }
    return gui.hwndFocus == macroEdit;
}

//---------------------------------------------------------------------------
// v1.2.0 Stable — FAULT ISOLATION for the producer (hook) thread.
//
// onHookEventImpl does real work on the most latency-critical thread in the
// system, and some of it allocates (the replacement scratch, macro
// expansions). It is wrapped in a noexcept trampoline because the function is
// called DIRECTLY from the WH_KEYBOARD_LL callback: an exception escaping a
// noexcept frame calls std::terminate(), which would kill the IME process
// mid-keystroke AND — because the callback never returns — trip Windows'
// LowLevelHooksTimeout and get the hook silently removed. One crash, two
// failure modes, both invisible until the user notices the tray icon lying.
//
// The degradation is deliberately PASS-THROUGH: the key reaches the
// application untouched, so no keystroke is ever lost or duplicated by the
// recovery. The engine may have advanced its buffer without the screen
// following, so the failure arms a resync that the next key event applies
// (see g.engineResyncPending).
//---------------------------------------------------------------------------
PD onHookEventImpl(const KeyEvent& ev);

PD onHookEvent(const KeyEvent& ev) noexcept {
    // v1.3.0-beta4: diagnostics recorders. ONE relaxed load gates the whole
    // block; at Level::Off this is a single compare + branch on the hot path.
    ok::diag::Diagnostics& diag = ok::diag::Diagnostics::instance();
    const bool diagOn = diag.atLeast(ok::diag::Level::Basic);
    const std::int64_t diagT0 = diagOn ? ok::diag::Diagnostics::nowUs() : 0;
    try {
        const PD pd = onHookEventImpl(ev);
        if (diagOn) {
            if (ev.source == EventSource::Keyboard) {
                if (ev.action == KeyAction::KeyDown || ev.action == KeyAction::SysKeyDown) {
                    diag.add(ok::diag::Counter::KeyDown);
                } else {
                    diag.add(ok::diag::Counter::KeyUp);
                }
                diag.add(pd.suppressKey ? ok::diag::Counter::KeySuppressed
                                        : ok::diag::Counter::KeyPassThrough);
            } else if (ev.source == EventSource::Mouse) {
                diag.add(ok::diag::Counter::MouseButton);
            } else if (ev.source == EventSource::ForegroundChanged) {
                diag.add(ok::diag::Counter::ForegroundChanged);
            }
            diag.record(ok::diag::Stage::HookToDecision,
                        ok::diag::Diagnostics::nowUs() - diagT0);
        }
        return pd;
    } catch (...) {
        // Counted (MemoryFailPoint-style degradation), never fatal. Taking
        // g.engineMtx here could deadlock if the thrower owns it, so the
        // repair is deferred to the next key event instead.
        g.producerFailures.fetch_add(1, std::memory_order_relaxed);
        if (diagOn) { diag.add(ok::diag::Counter::ProducerExceptions); }
        g.engineResyncPending.store(true, std::memory_order_release);
        return PD{};   // pass-through — the keystroke is never lost
    }
}

PD onHookEventImpl(const KeyEvent& ev) {
    if (g.liveEffects.sync()) { g.engineResyncPending.store(true, std::memory_order_release); }
    // A dedicated emergency switch; no game sees it and IME remains enabled.
    if (ev.source == EventSource::Keyboard && ev.action == KeyAction::KeyDown &&
        ev.vkCode == VK_F12 && ev.modifiers.ctrl && ev.modifiers.alt &&
        !ev.modifiers.win && g.liveEffects.enabled()) {
        g.liveEffects.disable();
        g.engineResyncPending.store(true, std::memory_order_release);
        return PD{true, false};
    }
    // v1.1.1: the global Ctrl+Shift toggle hotkey was REMOVED. It was the
    // root cause of the recurring "the IME suddenly turns off for no reason"
    // reports: bare Ctrl+Shift is also Windows' language-switch chord and a
    // thousand application shortcuts' prefix, third-party software injects
    // it, and a missed key-up made unrelated chords fire. Vietnamese input
    // is now switched on/off ONLY from inside the app (tray menu item or the
    // settings-dialog toggle button — see toggleEngineFromUi()), and every
    // change is confirmed with a tray balloon and persisted immediately.
    // The hook thread no longer toggles anything.
    if (!g.engineEnabled.load(std::memory_order_relaxed)) { return PD{}; }

    // Games accept input ONLY through their focused UI, never through this
    // system-wide hook. Background Arcade/Flexing must not eat another app's
    // keys or mutate a game concurrently with the UI timer.
    // v1.3.0-beta3 (bug #4): macroEditorFocus is computed ONLY inside the
    // own-window branch, so external typing (the hot path) pays nothing extra.
    bool macroEditorFocus = false;
    if (ev.source == EventSource::Keyboard && ownWindowHasFocus()) {
        macroEditorFocus = focusIsMacroEditor();
        if (!macroEditorFocus) {
            g.liveEffects.reset();
            g.engineResyncPending.store(true, std::memory_order_release);
            return PD{};
        }
        // Fall through: the focused control is the macro ("Gõ tắt") editor, a
        // plain EDIT in our own dialog. Compose Vietnamese into it through the
        // normal engine path (live effects are forced OFF below so the stored
        // expansion is the clean text the user typed). The self-injected filter
        // stops the composed keys from re-entering the hook.
    }

    // v1.2.0 Stable: repair after a producer-side fault (see onHookEvent).
    // The previous event threw after the engine had already consumed the
    // key, so the engine's buffer and the visible text disagree; dropping
    // the pending word is the only safe way back to a known-good state.
    if (g.engineResyncPending.exchange(false, std::memory_order_acq_rel)) {
        std::lock_guard<std::mutex> lk(g.engineMtx);
        // The engine's buffer and the visible text disagree, so visibleAccount_
        // (the D2 over-backspace clamp) is unreliable — take the engine fully
        // back to fresh-engine state (v1.3.0-beta3) rather than the partial
        // word drop, which left the stale account loosening the clamp.
        g.engine.resetForNewContext();
        g.liveEffects.reset();
    }

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
                if (g.liveEffects.enabled() && static_cast<CodeTable>(g.codeTableCache.load(std::memory_order_relaxed)) == CodeTable::Unicode) {
                    g.liveEffects.rewrite(bs, g.repScratch);
                }
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
                // v1.1.3 (see main edit site): count published BEFORE push.
                g.pendingEdits.publish(1);
                if (!g.outRing.try_push(it)) {
                    g.pendingEdits.rollback(1);
                    emitInline(bs, g.repScratch);   // ring full — inline fallback
                    return PD{true, false};
                }
                return PD{true, true};
            }
            emitInline(bs, g.repScratch);
            return PD{true, false};
        }
        return PD{true, false};   // style flipped (no pending word) — F9 consumed
    }

    // ---- bookkeeping / environment events ----
    if (ev.source == EventSource::ForegroundChanged) {
        {
            std::lock_guard<std::mutex> lk(g.engineMtx);
            // v1.3.0-beta3 over-backspace fix: a foreground switch is a NEW
            // DOCUMENT context — the engine committed nothing in the new window,
            // so every word-scoped field AND the D2 visible-account clamp must
            // return to fresh-engine state. beta2 called the PARTIAL
            // startNewSession() here (and only when live effects were on), which
            // left visibleAccount_ holding the PREVIOUS window's committed
            // length; the clamp `backspaceCount <= visibleAccount_` then failed
            // to bound a correction in the new window, so the first tone mark /
            // restore after Alt-Tab could erase text to the LEFT of the caret
            // (silent data loss, reproduced by tests/test_live_output_plan.cpp).
            g.engine.resetForNewContext();
            if (g.liveEffects.enabled()) { g.liveEffects.reset(); }
        }
        // v1.1.3: refreshNow() is KEPT deliberately (correctness before
        // micro-optimization). The monitor's own WinEvent pump publishes the
        // new snapshot on ITS thread, but WinEvent delivery across two pump
        // threads is unordered — without the synchronous refresh here the
        // exclusion cache could briefly describe the PREVIOUS foreground and
        // mis-apply the per-app policy to the first keystrokes after Alt-Tab.
        // The duplicate work is bounded (once per app switch, not per key).
        g.monitor.refreshNow();          // refresh the snapshot (rare — not typing path)
        updateExclusionCache();          // cache for the per-key hot path
        updateForegroundPolicy();        // TSF-vs-inline + keyboard layout cache
        // v1.1.0: re-seed the ENGINE-FACING modifier tracker from the OS key
        // state after a foreground change: a missed Shift/Ctrl/CapsLock KeyUp
        // (hook timeout, UIPI transition, RDP switch) previously left stale
        // tracked bits behind. The
        // tracked shift/caps bits feed layoutChar()/produceChar(), i.e. the
        // case of every character the engine stores in its word buffer.
        // While a stale bit claims shift/caps is held (secure desktop, RDP,
        // another hook swallowing key-ups, injected CapsLock toggles),
        // pass-through letters render with the TRUE (lowercase) system state
        // while the engine's buffer holds kCapsMask — the next tone mark then
        // re-emits that letter UPPERCASE ("vợ" → "vỢ") and word-break
        // restores re-type raw keys with mutated case. Reseeding here is the
        // consumer-layer fix for the "tone marks make letters uppercase"
        // report. Pump-thread serialized — same thread as applyModifierDelta.
        g.hook.resyncModifiersFromOs();
        // v3.5 foreground-hang gate: a synchronous TSF edit session marshals
        // into the focused app's STA — if that app is HUNG, the consumer
        // thread wedges inside RequestEditSession(TF_ES_SYNC) until the app
        // pumps again (the historical "KieeKey suddenly stopped" trigger).
        // Before any edit rides TSF into the new foreground, the UI thread
        // runs a bounded WM_NULL probe; until it passes we stay on inline
        // SendInput, which can never block.
        if (g.fgUseTsf_.load(std::memory_order_relaxed)) {
            g.fgUseTsf_.store(false, std::memory_order_relaxed);
            g.fgProbePending_.store(true, std::memory_order_relaxed);
            // v1.2.0 Stable: carry the probed HWND in wParam. The UI thread
            // discards the result if the foreground moved on while the probe
            // was queued (see probeForegroundResponsiveness).
            const auto fgSnap = g.monitor.snapshot();
            if (g.hMain) {
                ::PostMessageW(g.hMain, WM_APP_FGPROBE,
                               reinterpret_cast<WPARAM>(fgSnap ? fgSnap->hwnd : nullptr), 0);
            }
        }
        // (The unconditional resetForNewContext() at the top of this branch
        // already covers the excluded-foreground case — a second partial reset
        // here would be redundant and would re-dirty nothing.)
        OutputItem it;
        it.kind = OutputItem::Kind::ForegroundChanged;
        static_cast<void>(g.outRing.try_push(it));
        // v1.1.0: refresh the tray tooltip (excluded-app hint + state).
        // v1.1.0-audit fix (kept): use the TOOLTIP-ONLY message here — the
        // full-toggle path persists ALL settings to the registry, and every
        // Alt-Tab triggering 13 RegSetValueExW calls on the UI thread was
        // the old bug. Tooltip refresh alone never touches the registry.
        if (g.hMain) { ::PostMessageW(g.hMain, WM_APP_UPDATE_TIP, 0, 0); }
        return PD{false, true};
    }
    if (ev.source == EventSource::Mouse) {
        TextInput in;
        in.kind = InputKind::MouseDown;
        std::lock_guard<std::mutex> lk(g.engineMtx);
        static_cast<void>(g.engine.process(in));   // implicit word break
        if (g.liveEffects.enabled()) {
            g.engine.startNewSession();
            g.liveEffects.reset();
        }
        // The caret jumped (user clicked into text). Re-sync the engine to
        // the visible word before the caret so retyping composes onto it
        // instead of raw-passing ("chugsn" -> select/delete "gsn" -> click ->
        // "sng" must become "chúng", not "chusng").
        // Apply any pending edits BEFORE the click is delivered: the click
        // moves the caret, and an edit applied at the new caret position
        // would insert into the wrong place (a ghost).
        //
        // v1.1.3 latency fix: a WHEEL notch does NOT move the caret, so it
        // gains nothing from the ordering barrier — yet the wait ran on the
        // SHARED hook pump thread (the keyboard hook lives there too), so
        // scrolling while edits were in flight stalled keystrokes for up to
        // the 1 ms barrier budget per notch. Buttons still take the barrier.
        const bool isWheel = (ev.wParam == WM_MOUSEWHEEL || ev.wParam == WM_MOUSEHWHEEL);
        if (!isWheel) { waitPendingEditsDrained(); }
        const bool queued = requestContextResync();
        return PD{false, queued};
    }
    if (ev.action != KeyAction::KeyDown && ev.action != KeyAction::SysKeyDown) return PD{};
    if (ev.injected || ev.vkCode == 0 || isModifierVk(ev.vkCode)) return PD{};
    if (g.fgExcluded_.load(std::memory_order_relaxed)) {
        // v1.2.0 Stable: a lifecycle drain, not a bare wait — the engine is
        // being taken out of the loop for this app, so no further key event
        // will queue work and a stranded count would stall every following
        // pass-through key by the full barrier budget.
        drainPendingEditsForHook();
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
    if (g.liveEffects.enabled() &&
        (isCaretEditVk(ev.vkCode, ev.modifiers.ctrl) || ev.modifiers.win ||
         (ev.modifiers.ctrl != ev.modifiers.alt))) {
        waitPendingEditsDrained();
        std::lock_guard<std::mutex> lk(g.engineMtx);
        g.engine.startNewSession();
        g.liveEffects.reset();
        return PD{};   // shortcuts/navigation stay native; no styled context replay
    }
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
    // v1.1.0 AltGr fix: on AltGr layouts (German, Polish, Portuguese…)
    // Ctrl+Alt IS the AltGr modifier — the LL stream reports Ctrl+Alt for a
    // plain printable character. Treating it as a ctrl-combo word-broke
    // every AltGr character (typing "Grüße" broke mid-word at every ü).
    // UniKey parity: Ctrl+Alt+key composes normally; plain Ctrl or plain
    // Alt (real shortcuts) still bypass composition.
    const bool otherCtrl = (ctrl && alt) ? false : (ctrl || alt);

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

    // v1.3.0: telemetry & analytics observation (lock-free fixed ring buffer)
    ok::analytics::TypingAnalyticsEngine::instance().observeKey(
        in.ch, ::GetTickCount64() * 1000, in.kind == InputKind::Backspace, false, false);
    if (ok::ai::AiRivalEngine::instance().isOptIn()) {
        ok::ai::AiRivalEngine::instance().observeKeystroke(
            in.ch, ::GetTickCount64() * 1000, in.kind == InputKind::Backspace, false);
    }
    // v1.3.0 (fix): this ran `recordTypingSession()` — which takes the
    // progression mutex — on the HOOK THREAD, once per keystroke, in a function
    // otherwise documented as lock-free. The lock-free counters below are folded
    // in by the settings/status timer through flushStats().
    if (in.kind == InputKind::Char && in.ch > 32) {
        static std::atomic<std::uint64_t> s_lastProgressionTick{0};
        const std::uint64_t nowTick = ::GetTickCount64();
        const std::uint64_t previousTick =
            s_lastProgressionTick.exchange(nowTick, std::memory_order_relaxed);
        if (previousTick != 0 && nowTick > previousTick && (nowTick - previousTick) < 2000) {
            // Only gaps < 2 s count as "active typing time" (a pause is not
            // typing time and must not inflate the WPM denominator).
            ok::progression::ProgressionEngine::instance().recordActiveTimeMs(nowTick - previousTick);
        }
        ok::progression::ProgressionEngine::instance().recordKeystroke(false, 1, 0);
    } else if (in.kind == InputKind::Backspace) {
        ok::progression::ProgressionEngine::instance().recordKeystroke(true, 0, 0);
    }

    g.keysTyped.fetch_add(1, std::memory_order_relaxed);   // WPM gauge
    // v1.2.1 RC2: Adaptive profile idle signal (one relaxed store; the
    // tick value is already computed by the hook layer per event).
    g.lastKeyTickMs.store(::GetTickCount64(), std::memory_order_relaxed);
    // v1.1.3: pick up in-window layout switches (Win+Space etc.) between
    // words — a space or navigation key always precedes the next word.
    // v1.2.1 RC2: MaxCorrectness re-checks on EVERY key (two user-mode
    // reads, ~50 ns) so a mid-word layout switch can never mis-decode.
    if (in.kind == InputKind::Space || in.kind == InputKind::WordBreak) {
        // One finished word -> one learned word (lock-free, hook thread safe).
        ok::progression::ProgressionEngine::instance().recordKeystroke(false, 0, 1);
    }
    if (in.kind == InputKind::Space || in.kind == InputKind::WordBreak ||
        g.layoutRecheckEveryKey.load(std::memory_order_relaxed)) {
        refreshLayoutCache();
    }

    // ---- engine decision (fast, deterministic — safe on the hook thread).
    //      process() returns a const ref to engine-internal state; everything
    //      we need is read (or copied into the scratch) under the lock.
    bool     suppress = false;
    bool     liveOutput = false;
    bool     reissueTyped = false;
    std::size_t bs    = 0;
#if KIEEKEY_PROFILE
    const bool profOn = ok::prof::enabled();
    ok::prof::StageRecord profRec;
    const std::uint64_t profT0 = ev.timestampQpc;
#endif
    {
        std::lock_guard<std::mutex> lk(g.engineMtx);
        // v1.1.3 double-check: the enabled flag is re-read under the lock so
        // a toggle that lands between the early-return and this point cannot
        // emit an edit AFTER the OFF transition's drain (the drain above sees
        // a quiesced queue; an in-flight decision must not re-arm it).
        if (!g.engineEnabled.load(std::memory_order_relaxed)) {
            waitPendingEditsDrained();
            return PD{false, false};   // disabled mid-stroke — pass through
        }
        // bug #4: never style the macro editor — the stored expansion must be
        // the clean Vietnamese the user typed, not a random-case/flip variant.
        liveOutput = !macroEditorFocus &&
                     g.liveEffects.enabled() && static_cast<CodeTable>(g.codeTableCache.load(std::memory_order_relaxed)) == CodeTable::Unicode;
        const std::int64_t diagEngT0 =
            ok::diag::Diagnostics::instance().atLeast(ok::diag::Level::Basic)
                ? ok::diag::Diagnostics::nowUs() : 0;
        const EngineResult& r = g.engine.process(in);
        if (diagEngT0 != 0) {
            // v1.3.0-beta4: engine-decision latency + the edit counter, both
            // gated to Basic and both relaxed-RMW only (hook thread safe).
            ok::diag::Diagnostics::instance().record(
                ok::diag::Stage::EngineDecision,
                ok::diag::Diagnostics::nowUs() - diagEngT0);
            if (r.consumed()) {
                ok::diag::Diagnostics::instance().add(ok::diag::Counter::EngineEdits);
            }
        }
        // v1.1.2-r3 NUMBER-SAFETY GUARD (defense in depth, the LAST layer
        // before output). The engine promise is: with digitsAreLiteral ON, a
        // digit Char event is inert (DoNothing — the tests assert zero
        // backspaces in every context). This hook-level guard makes the
        // app-layer guarantee independent of the engine: if ANY engine path
        // (current or future) ever returns a consumed/Restore decision for a
        // bare digit while the shipped policy says "digits are numbers", the
        // decision is discarded and the digit passes through untouched.
        // The engine buffer is re-synced to the raw screen (the digit WAS
        // delivered literally), so no phantom state can compose afterward.
        // Macro expansions are NOT affected: they fire on Space/Enter break
        // events, never on the digit Char event itself.
        const bool digitLiteralEvent =
            g.options.digitsAreLiteral &&
            in.kind == InputKind::Char &&
            in.ch >= U'0' && in.ch <= U'9' &&
            r.code != EngineCode::DoNothing &&
            r.code != EngineCode::ReplaceMacro;
        // v1.2.1 RC2 BUG #3 — SHORTCUT-SAFETY GUARD. A Ctrl/Alt chord whose
        // key is a punctuation word-break (Ctrl+, Ctrl+. Ctrl+/ Ctrl+;) or a
        // control VK (Ctrl+Enter, Ctrl+Tab, Alt+Enter, ...) is fed to the
        // engine as a word break so the pending word is finalised. When that
        // word is a non-Vietnamese spelling under restoreIfWrongSpelling
        // ("wolf", "wifi") the engine returns a Restore edit and the hook
        // CONSUMED the chord: the app never saw its shortcut (VS Code
        // Ctrl+, = Settings, Ctrl+Enter = Send in mail clients). Found by
        // tests/stress_rc2.cpp scenario 4. Policy: a chord is never
        // suppressed and never rewrites text; the engine word state is
        // dropped (the app is about to act on the shortcut anyway).
        const bool shortcutRestoreEvent =
            in.otherCtrl &&
            (r.code == EngineCode::Restore ||
             r.code == EngineCode::RestoreAndStartNewSession);
        if (digitLiteralEvent || shortcutRestoreEvent) {
            if (digitLiteralEvent) { g.digitGuardHits.fetch_add(1, std::memory_order_relaxed); }
            else                   { g.shortcutGuardHits.fetch_add(1, std::memory_order_relaxed); }
            g.engine.startNewSession();   // screen keeps what it shows
            g.repScratch.clear();         // guarded: no replacement to apply
        } else {
            g.engine.replacementUtf16(r, g.repScratch);   // scratch — no per-key alloc
        }
        // v1.2.1 RC2 — QuickTelex unwanted-correction observation. Only
        // active while the option is ON; ~5 integer ops per key, no
        // allocation, never blocks (the notification itself is raised into
        // a lock-free slot the UI thread polls). Rules mirror
        // tests/test_notifications.cpp::testDetectorAgainstEngine exactly.
        if (g.options.quickTelex) {
            const std::uint64_t nowMs = ::GetTickCount64();
            auto& det = g.quickTelexDetector;
            if (in.kind == InputKind::Backspace) {
                det.observeBackspace(nowMs);
                g.lastRawKeyUpper = 0;
            } else if (in.kind == InputKind::Space || in.kind == InputKind::WordBreak) {
                det.observeWordCommitted(nowMs);
                g.lastRawKeyUpper = 0;
            } else {
                const char32_t up = (in.ch >= U'a' && in.ch <= U'z') ? in.ch - 32 : in.ch;
                if (r.code == EngineCode::WillProcess && r.backspaceCount == 1 &&
                    r.newCharCount == 2 && up == g.lastRawKeyUpper &&
                    ok::text::kQuickTelex.find(static_cast<std::uint16_t>(up)) !=
                        ok::text::kQuickTelex.end()) {
                    det.observeExpansion(up, nowMs);
                    g.lastRawKeyUpper = 0;
                } else {
                    if (r.code == EngineCode::Restore ||
                        r.code == EngineCode::RestoreAndStartNewSession) {
                        det.observeRestore(nowMs);
                    } else {
                        det.observeRawChar(nowMs);
                    }
                    g.lastRawKeyUpper = up;
                }
            }
            det.maybeRaise(g.notify, nowMs);
        }
        // Restore re-issue contracts (the app-visible semantics the legacy
        // hooks deliver and the user expects):
        //   * CHAR restore (hotfix §3): after reverting the word to its bare
        //     spelling the hook RE-SENDS the typed key — 'chào'+f → 'chaof'.
        //   * SPACE restore (D4, v3.1): after a wrong-spelling word triggers
        //     Restore on the space, the space is RE-ISSUED — without it the
        //     space was eaten ('arbit hối đoái' rendered 'arbithối đoái').
        // Backspace / word-break / mouse Restores keep their no-re-issue
        // semantics (verified against 2.0.5).
        reissueTyped = !digitLiteralEvent && !shortcutRestoreEvent &&
                       (r.code == EngineCode::Restore ||
                        r.code == EngineCode::RestoreAndStartNewSession) &&
                       (in.kind == InputKind::Char || in.kind == InputKind::Space);
        if (digitLiteralEvent || shortcutRestoreEvent) {
            // Guarded: force the pass-through contract (suppress stays false,
            // repScratch is not consumed by the output path below).
            g.repScratch.clear();
        } else if (r.code == EngineCode::ReplaceMacro) {
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

    if (liveOutput) {
        // v1.3.0-beta3: the live-effects output decision now lives in ONE
        // place — ok::effects::planOutput() — which tests/test_live_output_plan
        // .cpp drives through the REAL TextEngine. Before beta3 this logic was
        // inline here, inside a Windows-only TU no test could execute, so a
        // wrong decision was invisible ("bật random case rồi mà gõ bên ngoài
        // vẫn bình thường"). planOutput styles g.repScratch IN PLACE (the hook
        // reuses one buffer — zero per-key allocation) and returns the
        // suppress/backspace decision; it is behaviour-identical to the inline
        // branch it replaces (verified by the shipped-path suite).
        const ok::effects::KeyKind kind =
            (in.kind == InputKind::Char)      ? ok::effects::KeyKind::Char :
            (in.kind == InputKind::Space)     ? ok::effects::KeyKind::Space :
            (in.kind == InputKind::Backspace) ? ok::effects::KeyKind::Backspace :
            (in.kind == InputKind::WordBreak) ? ok::effects::KeyKind::WordBreak :
                                                ok::effects::KeyKind::Other;
        const char32_t typed = (in.kind == InputKind::Space) ? U' ' : in.ch;
        const ok::effects::OutputPlan plan =
            ok::effects::planOutput(true, suppress, bs, g.repScratch,
                                    kind, typed, g.liveEffects);
        suppress = plan.suppress;
        bs         = plan.backspace;
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
        // v1.1.3 race fix: publish the pending count BEFORE the item becomes
        // visible to the consumer (acq_rel). Incrementing AFTER the push
        // allowed a preemption window where the consumer applied the edit and
        // read prev == 0 — the "reached zero" notification was then skipped
        // and a PHANTOM pending count stayed armed forever, degrading every
        // later pass-through keystroke to the full 1 ms barrier wait.
        g.pendingEdits.publish(1);
        if (!g.outRing.try_push(it)) {
            g.pendingEdits.rollback(1);   // roll back
            emitInline(bs, g.repScratch);
            return PD{true, false};
        }
        // One more pending edit the consumer must apply before any
        // pass-through key may reach the app (see waitPendingEditsDrained).
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
        if ((g.inlineDeferred || g.deferInlineByProfile.load(std::memory_order_relaxed)) &&
            g.repScratch.size() <= kRingTextCap) {
            OutputItem it;
            it.kind      = OutputItem::Kind::InlineEdit;
            it.backspace = static_cast<std::uint32_t>(bs);
            const std::size_t n = std::min<std::size_t>(g.repScratch.size(),
                                                        std::size(it.text));
            it.textLen = static_cast<std::uint32_t>(n);
            for (std::size_t i = 0; i < n; ++i) { it.text[i] = g.repScratch[i]; }
            // v1.1.3 (see main edit site): count published BEFORE the push.
            g.pendingEdits.publish(1);
            if (g.outRing.try_push(it)) {
                return PD{true, true};
            }
            g.pendingEdits.rollback(1);   // roll back
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
// v1.2.1 RC2: the RC1 constant (32) is now the Balanced profile's batch cap
// (g.editBatchMax); kMaxEditBatch is the hard upper bound any profile can
// request (LeastFlicker = 64).
inline constexpr std::size_t kMaxEditBatch = 64;
thread_local std::vector<ok::tsf::EditDelta> g_editBatch;
thread_local std::uint32_t g_editBatchCount = 0;   // pendingEdits units in the batch
#if KIEEKEY_PROFILE
thread_local std::uint64_t g_editBatchT0 = 0;      // t0 of the batch's first item
thread_local std::uint64_t g_editBatchSeq = 0;
#endif
} // namespace

void onConsumerEvent(const KeyEvent& /*ev*/) noexcept {
    // v1.1.3: a failed attach() now RESETS the latch so a later foreground
    // can retry, instead of TSF staying dead for the whole process lifetime
    // with no diagnostic.
    bool expected = false;
    if (g.composerAttached.compare_exchange_strong(expected, true)) {
        if (!g.composer.attach()) {
            g.composerAttached.store(false, std::memory_order_relaxed);
        }
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
        // v1.1.3 OOM hardening: the commit path copies wstring payloads; a
        // bad_alloc here previously escaped the noexcept drain loop and
        // std::terminate'd the whole IME mid-keystroke ("suddenly stops").
        // Degrade to the SendInput fallback for the unapplied suffix instead.
        std::size_t appliedCountRead = 0;
        bool batchOk = false;
        // v1.3.0-beta4: consumer-side diagnostics — TSF commit latency and
        // the commit counters (relaxed, gated to Basic).
        ok::diag::Diagnostics& diagIns = ok::diag::Diagnostics::instance();
        const bool diagC = diagIns.atLeast(ok::diag::Level::Basic);
        const std::int64_t diagCommitT0 = diagC ? ok::diag::Diagnostics::nowUs() : 0;
        try {
            batchOk = g.composer.commitBatch(g_editBatch, &appliedCountRead);
        } catch (...) {
            batchOk = false;
            appliedCountRead = 0;   // unknown progress — re-deliver the batch
        }
        // v1.3.0-beta6 (V4): a successful TSF commit IS a delivery — record
        // it for the emit-chain evidence (channel 0 = TSF).
        if (batchOk) {
            std::uint32_t totalChars = 0;
            for (const ok::tsf::EditDelta& d : g_editBatch) {
                totalChars += static_cast<std::uint32_t>(d.text.size());
            }
            recordEmitEvidence(0, totalChars);
        }
        if (diagC) {
            diagIns.record(ok::diag::Stage::TsfCommit,
                           ok::diag::Diagnostics::nowUs() - diagCommitT0);
            diagIns.add(ok::diag::Counter::TsfCommits);
            if (batchOk && g.composer.lastCommitSlow()) {
                diagIns.add(ok::diag::Counter::TsfSlowCommits);
            }
        }
        if (batchOk) {
            // v1.1.0 — TSF slow-commit watchdog: a synchronous commit that
            // took ≥ 100 ms means the foreground app's STA is starving (the
            // pre-hang symptom of "KieeKey suddenly stops"). Degrade THIS
            // foreground to inline SendInput (which can never block) until
            // the next app switch re-evaluates the policy.
            if (g.composer.lastCommitSlow()) {
                // v1.2.1 RC2: the count is telemetry (Adaptive profile input,
                // Information tab) and is always kept; the DOWNGRADE itself
                // is a profile knob (LeastFlicker keeps TSF regardless).
                const auto n = g.tsfSlowCount.fetch_add(1, std::memory_order_relaxed) + 1;
                if (g.tsfSlowDowngrade.load(std::memory_order_relaxed)) {
                    g.fgUseTsf_.store(false, std::memory_order_relaxed);
                    // Tell the user once per hour at most (policy in
                    // Notifications.hpp); lock-free slot, UI thread polls.
                    g.notify.raise(ok::notify::Id::TsfSlowDowngrade,
                                   static_cast<std::uint8_t>(std::min<std::uint64_t>(100, 50 + n * 10)),
                                   static_cast<std::uint32_t>(n), ::GetTickCount64());
                }
            }
        } else {
            // Last-resort fallback: synthetic backspaces + Unicode text.
            // v1.1.3 (T3): TSF sessions are NOT transactional — deltas the
            // session already applied are IN the document. Re-emitting the
            // whole batch duplicated word-initial pure-insert deltas ("t",
            // then "to" over it -> "tto"). Deliver only the UNAPPLIED suffix.
            std::size_t applied = 0;
            if (appliedCountRead) { applied = appliedCountRead; }
            std::uint32_t fallbackChars = 0;   // v1.3.0-beta6 (V4)
            for (std::size_t i = applied; i < g_editBatch.size(); ++i) {
                const ok::tsf::EditDelta& d = g_editBatch[i];
                sendBackspaces(d.backspace);
                sendUnicodeText(d.text);
                fallbackChars += static_cast<std::uint32_t>(d.text.size());
            }
            // The fallback re-emitted through SendInput — that is the
            // delivery the tester asked about (channel 1).
            if (fallbackChars > 0) { recordEmitEvidence(1, fallbackChars); }
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
        // v1.1.3: the "reached zero" test re-READS the counter after an
        // acq_rel subtract instead of trusting the fetch_sub previous value —
        // a late producer fetch_add (the race fixed above) could slip between
        // the two and leave the barrier armed forever.
        g.pendingEdits.consume(g_editBatchCount);
        g_editBatch.clear();
        g_editBatchCount = 0;
    };

    OutputItem it;
    while (g.outRing.try_pop(it)) {
        if (it.kind == OutputItem::Kind::Edit) {
            // v1.2.0 Stable: this push_back allocates (a std::wstring per
            // delta). The whole function is noexcept — an escaping bad_alloc
            // here called std::terminate() and killed the IME mid-keystroke,
            // leaving a live tray icon over a dead engine. Degrade instead:
            // emit this one delta through the inline (SendInput) path, which
            // needs no heap, and keep draining the ring.
            try {
                g_editBatch.push_back(ok::tsf::EditDelta{
                    it.backspace, std::wstring(it.text, it.text + it.textLen)});
            } catch (...) {
                g.consumerFailures.fetch_add(1, std::memory_order_relaxed);
                g.pendingEdits.rollback(1);      // never counted → never waited for
                // Direct emitter call: no std::wstring, no allocation — the
                // fallback must not be able to throw for the same reason.
                g.hook.emitter().sendEdit(it.backspace, it.text, it.textLen);
                recordEmitEvidence(1, it.textLen);   // v1.3.0-beta6 (V4)
                continue;
            }
            ++g_editBatchCount;
#if KIEEKEY_PROFILE
            if (profOn && g_editBatch.size() == 1) {
                g_editBatchT0 = it.profT0;    // first item of the batch owns t0
                g_editBatchSeq = it.profSeq;
            }
#endif
            // v1.2.1 RC2: the batch cap is a profile knob (1 = one edit per
            // commit for MaxCorrectness … 64 for LeastFlicker); kMaxEditBatch
            // remains the hard upper bound of the reserved vector.
            if (g_editBatch.size() >= std::min<std::size_t>(
                    kMaxEditBatch, g.editBatchMax.load(std::memory_order_relaxed))) {
                flushEditBatch();
            }
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
            // v1.3.0-beta4: consumer-side diagnostics — the SendInput emit
            // latency + call counter (relaxed, gated to Basic).
            if (ok::diag::Diagnostics::instance().atLeast(ok::diag::Level::Basic)) {
                ok::diag::Diagnostics& dIns = ok::diag::Diagnostics::instance();
                const std::int64_t dT0 = ok::diag::Diagnostics::nowUs();
                g.hook.emitter().sendEdit(it.backspace, it.text, it.textLen);
                dIns.record(ok::diag::Stage::SendInputCall,
                            ok::diag::Diagnostics::nowUs() - dT0);
                dIns.add(ok::diag::Counter::SendInputCalls);
            } else {
                g.hook.emitter().sendEdit(it.backspace, it.text, it.textLen);
            }
            // v1.3.0-beta6 (V4): deferred inline edit delivered via SendInput.
            recordEmitEvidence(1, it.textLen);
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
            g.pendingEdits.consume(1);
            continue;
        }
        // Non-edit item: apply any pending edits FIRST (ring order), then the
        // control item.
        flushEditBatch();
        if (it.kind == OutputItem::Kind::ForegroundChanged) {
            // v3.5: re-resolve the focused document ONLY while the output
            // policy actually uses TSF. While the foreground is gated (probe
            // pending or probe failed), GetFocus here could marshal into the
            // hung target's STA on this thread; ensureContext() lazily
            // re-resolves on the first TSF commit once TSF is re-enabled.
            if (g.fgUseTsf_.load(std::memory_order_relaxed)) {
                g.composer.onForegroundChanged();
            }
        } else if (it.kind == OutputItem::Kind::Resync) {
            if (g.liveEffects.enabled()) { continue; }
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
// v1.1.2-r3 CONFLICT DETECTOR — the root causes OUTSIDE KieeKey.
//
// Symptom reported by users: "typing a number produces a tone mark or a
// word" — persisting even after every in-app fix. Two external causes
// produce EXACTLY that symptom while KieeKey is doing everything right:
//
//   1. Another Vietnamese input method is running in the same session
//      (EVKey, UniKey, OpenKey, GoTiengViet, LabanKey…). Its VNI mode
//      converts digits to tone marks regardless of KieeKey's state.
//
//   2. Windows 10/11 ships built-in "Vietnamese - Telex" and
//      "Vietnamese - Number Key-Based" keyboard LAYOUTS (KLID 0x0002042A,
//      0x0003042A — any 0x…042A sub-layout other than the plain 0x0000042A
//      "Vietnamese" base, whose layout file is the digit-neutral one).
//      These convert digits at the OS level, even with every IME app off.
//
// Neither can be fixed from inside KieeKey — but both MUST be surfaced,
// otherwise the user concludes (rationally, and wrongly) that the KieeKey
// patch "didn't work". The scan result is shown on the Information tab and
// folded into the startup balloon. UI-thread only, run at startup + each
// settings-dialog open (~1–2 ms: one Toolhelp snapshot + a few registry
// reads — never on the hook thread).
//===========================================================================
struct ConflictScan {
    std::wstring warning;    // user-facing multi-line warning (empty = clean)
    std::wstring detail;     // short one-line summary for diagnostics view
};

[[nodiscard]] ConflictScan scanConflicts() {
    ConflictScan out;
    std::wstring imes;    // comma-joined running IME names
    std::wstring layouts; // comma-joined Vietnamese variant layout names

    // ---- 1. running IME processes (Toolhelp snapshot) -------------------
    static constexpr std::wstring_view kImeProcs[] = {
        L"evkey.exe", L"evkeyagent.exe", L"evkeyplayer.exe", L"evkey64.exe",
        L"unikey.exe", L"unikeynt.exe", L"openkey.exe",
        L"gotiengviet.exe", L"labankey.exe",
    };
    {
        HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe{};
            pe.dwSize = sizeof(pe);
            if (::Process32FirstW(snap, &pe)) {
                do {
                    // Lowercase the image name in place.
                    wchar_t* p = pe.szExeFile;
                    for (; *p; ++p) {
                        if (*p >= L'A' && *p <= L'Z') { *p = static_cast<wchar_t>(*p - L'A' + L'a'); }
                    }
                    const std::wstring_view exe{pe.szExeFile};
                    for (const std::wstring_view known : kImeProcs) {
                        if (exe == known) {
                            if (!imes.empty()) { imes += L", "; }
                            imes += exe;
                            break;
                        }
                    }
                } while (::Process32NextW(snap, &pe));
            }
            ::CloseHandle(snap);
        }
    }

    // ---- 2. installed Windows Vietnamese variant keyboard layouts -------
    // Enumerate the user's enabled layouts (HKCU\Keyboard Layout\Preload,
    // values "1".."9" = KLID strings) and flag every 0x…042A KLID that is
    // NOT the plain base layout 0000042A — on real Windows those are the
    // Telex / Number Key-Based variants which rewrite digits.
    {
        HKEY hk = nullptr;
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, L"Keyboard Layout\\Preload", 0,
                            KEY_READ, &hk) == ERROR_SUCCESS) {
            for (DWORD i = 1; i <= 9; ++i) {
                const std::wstring val = std::to_wstring(static_cast<unsigned long long>(i));
                wchar_t klid[16] = {};
                DWORD size = sizeof(klid);
                DWORD type = 0;
                if (::RegQueryValueExW(hk, val.c_str(), nullptr, &type,
                                       reinterpret_cast<LPBYTE>(klid), &size) != ERROR_SUCCESS ||
                    type != REG_SZ) {
                    continue;
                }
                // Normalize: lowercase, keep the 8-hex KLID.
                std::wstring k{klid};
                for (wchar_t& c : k) {
                    if (c >= L'A' && c <= L'Z') { c = static_cast<wchar_t>(c - L'A' + L'a'); }
                }
                if (k.size() < 8) { continue; }
                k.resize(8);
                if (k != L"0000042a" && k.substr(4) == L"042a") {
                    if (!layouts.empty()) { layouts += L", "; }
                    if (k == L"0002042a")      { layouts += L"Vietnamese - Telex (Windows)"; }
                    else if (k == L"0003042a") { layouts += L"Vietnamese - Number Key-Based (Windows)"; }
                    else                       { layouts += (L"Vietnamese layout " + k); }
                }
            }
            ::RegCloseKey(hk);
        }
    }

    // ---- 3. user-facing summary -----------------------------------------
    if (!imes.empty() && !layouts.empty()) {
        out.detail = L"Bộ gõ khác đang chạy: " + imes +
                     L" · Bàn phím hệ thống: " + layouts;
    } else if (!imes.empty()) {
        out.detail = L"Bộ gõ khác đang chạy: " + imes;
    } else if (!layouts.empty()) {
        out.detail = L"Bàn phím hệ thống: " + layouts;
    } else {
        out.detail = L"Không phát hiện bộ gõ / bàn phím tiếng Việt nào khác";
        return out;   // clean
    }

    out.warning = L"⚠ PHÁT HIỆN NGUYÊN NHÂN GÂY LỖI SỐ → DẤU:\n" + out.detail +
                  L".\nCác bộ gõ / bàn phím này chuyển số thành dấu tiếng Việt "
                  L"NGAY CẢ KHI KieeKey đang hoạt động đúng — chúng cùng gõ với "
                  L"KieeKey sẽ xung đột. Hãy THOÁT (hoặc gỡ) bộ gõ đó, và xóa bàn "
                  L"phím tiếng Việt kiểu Telex/VNI trong Cài đặt Windows → "
                  L"Time & language → Language & region, rồi khởi động lại máy.";
    return out;
}

// v1.1.2-r3: one-glance diagnostics block for the Information tab — the
// RUNNING build + live engine state + the conflict verdict. This is what
// makes "which exe am I actually running, and what is converting my
// digits" answerable on the user's machine without external tooling.
std::wstring infoDiagnosticsText() {
    std::wstring s = L"Bản đang chạy: KieeKey v";
    s += kAppVersion;
    s += g.engineEnabled.load(std::memory_order_relaxed) ? L" · Bộ gõ: BẬT"
                                                         : L" · Bộ gõ: TẮT";
    s += L" · Phương thức: ";
    s += g.options.inputMethod == InputMethod::Telex ? L"Telex"
         : g.options.inputMethod == InputMethod::Vni ? L"VNI"
                                                     : L"Simple Telex";
    s += L" · Số 0–9: ";
    s += g.options.digitsAreLiteral ? L"chữ số (bật)" : L"gõ dấu VNI (tắt)";
    if (!g.conflictWarning.empty()) {
        s += L"\n⚠ ";
        s += g.conflictDetail;
        s += L".\nHãy thoát/gỡ bộ gõ đó (hoặc xóa bàn phím Telex/VNI của "
             L"Windows) — nó chuyển số thành dấu ngay cả khi KieeKey đúng.";
    } else {
        s += L"\nKhông có bộ gõ / bàn phím tiếng Việt nào khác xung đột — "
             L"chữ số 0–9 do KieeKey bảo đảm.";
    }
    const std::uint64_t hits = g.digitGuardHits.load(std::memory_order_relaxed);
    if (hits != 0) {
        s += L"\nLớp bảo vệ số đã chặn " + std::to_wstring(hits) +
             L" lần (bất thường — vui lòng cập nhật KieeKey).";
    }
    const std::uint64_t sc = g.shortcutGuardHits.load(std::memory_order_relaxed);
    if (sc != 0) {
        s += L"\nPhím tắt Ctrl/Alt được bảo vệ khỏi sửa lỗi chính tả: " +
             std::to_wstring(sc) + L" lần.";
    }
    return s;
}

//===========================================================================
// Tray icon + context menu
//===========================================================================
// v1.1.0: shared tooltip builder — includes the version and, when the
// foreground app is auto-excluded, WHICH app the engine is paused in (the
// old tip gave no hint, so users reported "KieeKey suddenly stopped").
// v1.3.0-beta5 (bug B2): the live-effects GATE readout — the FIRST veto in
// the hook's evaluation chain (pure model: ok::effects::liveGateBlocker,
// pinned by tests/test_live_effects_chain.cpp), worded for the user. Shown
// on tab 6 (live, per timer tick), in the tray tooltip, and in the exported
// diagnostics report, so "I enabled it but nothing happens" always has a
// visible, exact answer.
const wchar_t* liveGateStatusText() noexcept {
    using ok::effects::GateBlocker;
    // v1.3.0-beta6 (V4): same gate model the emit-chain evidence records.
    switch (liveGateNow()) {
        case GateBlocker::None:
            return L"Hiệu ứng: SẴN SÀNG (IME bật · Unicode · app không loại trừ)";
        case GateBlocker::ImeDisabled:
            return L"Hiệu ứng: TẮT — bộ gõ đang TẮT (bật lại ở nút trên cùng)";
        case GateBlocker::AppExcluded:
            return L"Hiệu ứng: TẮT — app này bị loại trừ (quyền cao / IDE / game)";
        case GateBlocker::MasterOff:
            return L"Hiệu ứng: TẮT — chưa bật ô 'Bật khi gõ bên ngoài' ở trên";
        case GateBlocker::NonUnicodeTable:
        default:
            return L"Hiệu ứng: TẮT — cần bảng mã Unicode (đổi ở tab Bàn phím)";
    }
}

std::wstring trayTipText() {
    std::wstring tip = L"KieeKey v";
    tip += kAppVersion;
    tip += L" — ";
    tip += (g.options.inputMethod == InputMethod::Telex ? L"Telex"
            : g.options.inputMethod == InputMethod::Vni ? L"VNI" : L"Simple Telex");
    tip += g.engineEnabled.load(std::memory_order_relaxed)
               ? L" [Đang bật]"
               : L" [Đang tắt]";
    if (g.engineEnabled.load(std::memory_order_relaxed) &&
        g.fgExcluded_.load(std::memory_order_relaxed)) {
        const auto snap = g.monitor.snapshot();
        if (snap) {
            std::wstring app = L" — tạm tắt trong ";
            app += utf8ToUtf16(snap->exeNameUtf8);
            tip += app;
        }
    }
    // v1.3.0-beta5 (bug B2): when the live-effects channel is switched on,
    // the tooltip says whether it can actually reach the foreground app —
    // the state was invisible before, so a veto looked like a dead feature.
    if (g.liveEffects.enabled()) {
        using ok::effects::GateBlocker;
        const GateBlocker blocker = ok::effects::liveGateBlocker(
            g.engineEnabled.load(std::memory_order_relaxed),
            g.fgExcluded_.load(std::memory_order_relaxed),
            true,
            static_cast<CodeTable>(g.codeTableCache.load(std::memory_order_relaxed)) == CodeTable::Unicode);
        switch (blocker) {
            case GateBlocker::None:
                tip += L" — hiệu ứng: BẬT";
                break;
            case GateBlocker::ImeDisabled:
                tip += L" — hiệu ứng: TẮT (bộ gõ tắt)";
                break;
            case GateBlocker::AppExcluded:
                tip += L" — hiệu ứng: TẮT (app loại trừ)";
                break;
            default:
                tip += L" — hiệu ứng: TẮT (cần bảng mã Unicode)";
                break;
        }
    }
    return tip;
}

// Forward declaration — defined further below; toggleEngineFromUi() shows a
// confirming balloon on every on/off change (v1.1.1).
void showTrayBalloon(const wchar_t* title, const wchar_t* text) noexcept;

// v1.1.2 — single creation path for the settings window (used by the tray
// menu's Cài đặt… and Thông tin items and by the double-click action).
// `tab` selects the initially shown tab (0..4; 4 = Information).
void openSettingsDialog(int tab);

void addTrayIcon() noexcept {
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = g.hMain;
    nid.uID              = 1;
    nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon            = g.engineEnabled.load() ? g.hIconOn : g.hIconOff;
    std::wstring tip = trayTipText();
    if (tip.size() >= std::size(nid.szTip)) { tip.resize(std::size(nid.szTip) - 1); }
    std::copy(tip.begin(), tip.end(), nid.szTip);
    nid.szTip[tip.size()] = L'\0';
    ::Shell_NotifyIconW(NIM_ADD, &nid);

    // welcome balloon once per process run
    if (!g.balloonShown.exchange(true)) {
        nid.uFlags |= NIF_INFO;
        nid.dwInfoFlags = NIIF_INFO;
        // v1.1.0 (feedback fix): the persisted on/off state meant a relaunch
        // could silently start DISABLED while the welcome text still claimed
        // the IME was "ready". The balloon states the ACTUAL startup state
        // and points at the in-app switch (v1.1.1: no more hotkey).
        // v1.1.2-r3: the balloon is also the BUILD PROOF — it states the
        // exact running version + the digits policy, so a machine still
        // auto-starting an old pre-fix exe is immediately identifiable.
        std::wstring info = g.engineEnabled.load(std::memory_order_relaxed)
            ? L"Đã sẵn sàng (đang BẬT). "
            : L"Đang TẮT (lưu từ phiên trước). ";
        info += (g.options.digitsAreLiteral ? L"Số 0–9: CHỮ SỐ (bật). "
                                            : L"Số 0–9: gõ dấu VNI (tắt). ");
        if (!g.conflictWarning.empty()) {
            info += L"CẢNH BÁO: phát hiện bộ gõ khác — mở Thông tin để xem.";
        } else {
            info += L"Không có bộ gõ nào khác xung đột.";
        }
        std::wstring title = kAppTitle;
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
    std::wstring tip = trayTipText();
    if (tip.size() >= std::size(nid.szTip)) { tip.resize(std::size(nid.szTip) - 1); }
    std::copy(tip.begin(), tip.end(), nid.szTip);
    nid.szTip[tip.size()] = L'\0';
    ::Shell_NotifyIconW(NIM_MODIFY, &nid);
}

//===========================================================================
// v1.1.1 — the ONE in-app on/off path. Called from the tray menu item and
// from the settings-dialog toggle button (both run on the UI thread).
// The old global Ctrl+Shift hotkey was removed (it fired on chords the user
// never intended — the "suddenly turns off" root cause); the ONLY way to
// switch Vietnamese input now is this explicit, visible, confirmed control.
// Every change: drops stale engine word state, re-seeds the modifier
// tracker (safe — a single atomic store), updates the tray icon, PERSISTS
// the new state immediately (so a restart can never resurrect a stale
// on/off value) and shows a confirming balloon.
//===========================================================================
void toggleEngineFromUi() {
    const bool enable = !g.engineEnabled.load(std::memory_order_relaxed);
    {
        // The lock covers ONLY the engine state — registry I/O (saveSettings)
        // must never extend the hook-thread stall window.
        std::lock_guard<std::mutex> lk(g.engineMtx);
        g.engine.startNewSession();   // drop stale word state
    }
    g.engineEnabled.store(enable, std::memory_order_relaxed);
    if (enable) {
        // Re-seed the engine-facing modifier tracker before the next word
        // can compose with a stale shift/caps bit (same rationale as the
        // ForegroundChanged resync; atomic — safe from the UI thread).
        g.hook.resyncModifiersFromOs();
    } else {
        // v1.1.3 ordering fix (v1.2.0: completed): with edits still queued
        // (pending > 0 — realistic against a slow STA foreground),
        // disabled-mode letters reached the app immediately while the stale
        // edit committed later and deleted the WRONG characters. Drain the
        // queue so OFF is a clean cut-off point.
        //
        // v1.2.0: the plain wait could never actually succeed here — turning
        // the engine OFF stops the producer, so nothing wakes a parked
        // consumer and the drain always timed out with the count still armed
        // (every later keystroke then paid 1 ms of hook-thread stall until
        // an edit happened to re-arm the wake). drainPendingEditsForLifecycle()
        // pokes the consumer first and, if it is genuinely wedged, clears the
        // count — safe precisely because the engine is OFF and no further
        // edit can be published.
        drainPendingEditsForLifecycle();
    }
    updateTrayIcon();
    saveSettings();   // persist at every change point (restart-proof)
    if (enable) {
        showTrayBalloon(L"KieeKey — Bật",
                        L"Đã BẬT gõ tiếng Việt.");
    } else {
        showTrayBalloon(L"KieeKey — Tắt",
                        L"Đã TẮT gõ tiếng Việt.\n"
                        L"Nhấp phải biểu tượng khay (hoặc nút trong Cài đặt) để bật lại.");
    }
}

void showTrayMenu() noexcept {
    HMENU menu = ::CreatePopupMenu();
    HMENU methodMenu = ::CreatePopupMenu();

    // static_cast<UINT>: MF_STRING is `long` in the MinGW headers, and the
    // `long | UINT` fold then narrowed to the UINT parameter trips GCC
    // -Wsign-conversion (MSVC /W4 is silent here). Same value either way.
    // v1.1.1: the item text states the ACTION it performs (no more bare
    // checkmark + hotkey to hunt for): "Tắt gõ tiếng Việt" while running,
    // "Bật gõ tiếng Việt" while stopped.
    ::AppendMenuW(menu,
                  static_cast<UINT>(MF_STRING),
                  IDM_TOGGLE,
                  g.engineEnabled.load(std::memory_order_relaxed)
                      ? L"Tắt gõ tiếng Việt"
                      : L"Bật gõ tiếng Việt");
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

    // v1.3.0 Arcade Hub menu
    HMENU arcadeMenu = ::CreatePopupMenu();
    ::AppendMenuW(arcadeMenu, MF_STRING, IDM_ARCADE_HUB, L"Mở Arcade Hub (Tất cả game)…");
    ::AppendMenuW(arcadeMenu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(arcadeMenu, MF_STRING, IDM_ARCADE_SNAKE, L"🐍 Snake (Rắn săn mồi)");
    ::AppendMenuW(arcadeMenu, MF_STRING, IDM_ARCADE_TETRIS, L"🧱 Tetris (Xếp gạch)");
    ::AppendMenuW(arcadeMenu, MF_STRING, IDM_ARCADE_FISHING, L"🎣 Fishing (Câu cá gõ phím)");
    ::AppendMenuW(arcadeMenu, MF_STRING, IDM_ARCADE_TYPINGRACE, L"🏎️ Typing Race (Đua xe)");
    ::AppendMenuW(arcadeMenu, MF_STRING, IDM_ARCADE_WASDRACE, L"🏎️ WASD + Typing Racing");
    ::AppendMenuW(arcadeMenu, MF_STRING, IDM_ARCADE_RHYTHM, L"🎵 Rhythm Typing (FNF-style)");
    ::AppendMenuW(arcadeMenu, MF_STRING, IDM_ARCADE_NOMISTAKE, L"🎯 No-Mistake Mode");
    ::AppendMenuW(arcadeMenu, MF_STRING, IDM_ARCADE_FLEXING, L"🗿 Flexing Mode (Joke)");
    ::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(arcadeMenu), L"KieeKey Arcade");

    ::AppendMenuW(menu, MF_STRING, IDM_LIVE_EFFECTS, L"Hiệu ứng gõ bên ngoài (hoa/thường, lật chữ)…");
    ::AppendMenuW(menu, MF_STRING, IDM_CHAOS_LAB, L"🌀 Phòng Chaos Lab…");
    ::AppendMenuW(menu, MF_STRING, IDM_AI_RIVAL, L"🤖 AI Typing Rival & Coach…");
    ::AppendMenuW(menu, MF_STRING, IDM_PROGRESSION, L"📈 Tiến trình & Thành tích…");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Cài đặt…");
    ::AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"Thông tin & giới thiệu");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Thoát");

    POINT pt;
    ::GetCursorPos(&pt);
    ::SetForegroundWindow(g.hMain);
    const UINT cmd = static_cast<UINT>(::TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                                        pt.x, pt.y, 0, g.hMain, nullptr));
    // v1.1.0-audit fix: KB135880 tray-menu idiom — the WM_NULL posted right
    // after TrackPopupMenu lets the menu's foreground state resolve, so the
    // menu reliably dismisses when the user clicks elsewhere (required
    // whenever SetForegroundWindow precedes TrackPopupMenu).
    ::PostMessageW(g.hMain, WM_NULL, 0, 0);
    ::DestroyMenu(menu);

    switch (cmd) {
        case IDM_TOGGLE: {
            // v1.1.1: single shared in-app toggle path (persist + balloon).
            toggleEngineFromUi();
            break;
        }
        case IDM_METHOD_TELEX: case IDM_METHOD_VNI: case IDM_METHOD_SIMPLETELEX: {
            const auto m = static_cast<InputMethod>(cmd - IDM_METHOD_TELEX);
            {
                std::lock_guard<std::mutex> lk(g.engineMtx);
                g.options.inputMethod = m;
                g.engine.setOptions(g.options);
                g.engine.resetForConfigurationChange();  // v1.2.2 RC2
            }
            saveSettings();
            updateTrayIcon();
            break;
        }
        case IDM_SETTINGS:
            openSettingsDialog(0);
            break;
        case IDM_ABOUT:
            // v1.1.2: the in-app introduction (Information tab).
            openSettingsDialog(4);
            break;
        case IDM_ARCADE_HUB:
            openArcadeHub(nullptr);
            break;
        case IDM_ARCADE_SNAKE:
            openArcadeHub("snake");
            break;
        case IDM_ARCADE_TETRIS:
            openArcadeHub("tetris");
            break;
        case IDM_ARCADE_FISHING:
            openArcadeHub("fishing");
            break;
        case IDM_ARCADE_TYPINGRACE:
            openArcadeHub("typing-race");
            break;
        case IDM_ARCADE_WASDRACE:
            openArcadeHub("wasd-race");
            break;
        case IDM_ARCADE_RHYTHM:
            openArcadeHub("rhythm");
            break;
        case IDM_ARCADE_NOMISTAKE:
            openArcadeHub("no-mistake");
            break;
        case IDM_ARCADE_FLEXING:
            openArcadeHub("flexing");
            break;
        case IDM_LIVE_EFFECTS:
            openSettingsDialog(6);
            break;
        case IDM_CHAOS_LAB:
            // The lab is its own window: type text, watch the chaos transform
            // live, and (optionally) write the result into the focused app.
            openChaosLab();
            break;
        case IDM_AI_RIVAL:
            openSettingsDialog(7);
            break;
        case IDM_PROGRESSION:
            openSettingsDialog(8);
            break;
        case IDM_EXIT:
            ::PostMessageW(g.hMain, WM_CLOSE, 0, 0);
            break;
        default:
            break;
    }
}

//===========================================================================
// v3.5 reliability — "suddenly stops / cannot open again" hardening
//
// Four user-facing failure modes this block eliminates:
//   1. Explorer.exe restarts (crash / shell update) → the tray icon is gone
//      while the app keeps running → "KieeKey suddenly stopped", and the
//      single-instance mutex blocked every relaunch. Fixed by handling
//      TaskbarCreated (icon resurrection) and the second-instance wake
//      protocol below (healthy instance restores its icon + balloons).
//   2. The focused app hangs while TSF output is active → the consumer
//      thread wedges inside RequestEditSession(TF_ES_SYNC) (marshals into
//      the target's STA). Fixed by the foreground-hang probe (WM_APP_FGPROBE):
//      output stays on inline SendInput until the foreground answers.
//   3. Exit while the consumer is wedged (case 2) → the OLD unbounded
//      join() in stop() hung the UI thread → a ZOMBIE process kept holding
//      the single-instance mutex. Fixed by the bounded shutdown in
//      ModernKeyHook::stop() / ProcessMonitor::stop() — and, as the last
//      line of defense, terminateStaleInstance() below takes over a zombie
//      so a relaunch NEVER requires a reboot or Task Manager.
//===========================================================================
// v1.2.0 Stable: the names are deliberately VERSION-FREE.
//
// They used to embed "1.1.0" — a leftover from the release that introduced
// the single-instance protocol. Two consequences, both real:
//   * mixed version identifiers in the shipped binary (the mutex name was
//     the only place in the build still claiming 1.1.0); and
//   * a genuine upgrade hazard: the moment the name changed, an OLD instance
//     already running and a NEWLY launched one would no longer share a
//     mutex, so BOTH would start, both would install a low-level keyboard
//     hook, and every keystroke would be composed twice (double letters).
// A singleton exists to prevent exactly that, so the name must be stable
// across versions.
constexpr wchar_t kSingletonMutexName[] = L"KieeKey_Singleton";
constexpr wchar_t kWakeEventName[]      = L"KieeKey_Wake";

UINT      g_msgTaskbarCreated = 0;    // RegisterWindowMessageW("TaskbarCreated")
HANDLE    g_wakeExitEvent     = nullptr;   // manual-reset; stops the watcher
std::thread g_wakeWatcher;                   // serves second-instance wake
std::atomic<bool> g_appExiting{false};

void showTrayBalloon(const wchar_t* title, const wchar_t* text) noexcept {
    if (!g.hMain) { return; }
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = g.hMain;
    nid.uID              = 1;
    nid.uFlags           = NIF_INFO;
    nid.dwInfoFlags      = NIIF_INFO;
    std::wstring t(title);
    std::wstring s(text);
    if (t.size() >= std::size(nid.szInfoTitle))  { t.resize(std::size(nid.szInfoTitle) - 1); }
    if (s.size() >= std::size(nid.szInfo))       { s.resize(std::size(nid.szInfo) - 1); }
    std::copy(t.begin(), t.end(), nid.szInfoTitle);
    nid.szInfoTitle[t.size()] = L'\0';
    std::copy(s.begin(), s.end(), nid.szInfo);
    nid.szInfo[s.size()] = L'\0';
    ::Shell_NotifyIconW(NIM_MODIFY, &nid);
}

//===========================================================================
// v1.2.1 RC2 — intelligent notifications: registry-backed suppression store,
// UI-thread presentation (tray balloon → click opens a 3-way choice) and the
// QuickTelex "Turn off / Keep / Don't show again" flow.
//
// The hot paths only ever call NotificationCenter::raise() (lock-free slot).
// Everything below runs on the UI thread from the 1 s WM_TIMER on the hidden
// main window. When the shell has no notification area (Explorer down,
// kiosk shells, RDP without tray) Shell_NotifyIcon simply fails and the
// item is dropped — nothing is retried, nothing blocks.
//===========================================================================
void settingsToControls();   // fwd (defined with the settings dialog)

std::wstring notifySuppressValueName(ok::notify::Id id) {
    return L"NotifySuppress_" + std::to_wstring(static_cast<int>(id));
}
ok::notify::Store makeNotifyStore() {
    ok::notify::Store st;
    st.isSuppressed = [](ok::notify::Id id) -> bool {
        if (auto key = settingsKey(); key) {
            return key.getDword(notifySuppressValueName(id), 0) != 0;
        }
        return false;
    };
    st.setSuppressed = [](ok::notify::Id id, bool v) {
        if (auto key = settingsKey(); key) {
            key.setDword(notifySuppressValueName(id), v ? 1 : 0);
        }
    };
    return st;
}

ok::notify::Notification g_shownNotification;   // the balloon currently on screen (UI thread)

void presentNotification(const ok::notify::Notification& n) {
    using ok::notify::Id;
    g_shownNotification = n;
    switch (n.id) {
        case Id::QuickTelexUnwanted: {
            wchar_t body[256];
            std::swprintf(body, std::size(body),
                L"Có vẻ bạn đã hoàn tác %u lần việc Telex nhanh tự đổi phụ âm kép "
                L"(pp→ph, tt→th, cc→ch…). Nhấp vào đây để chọn: Tắt Telex nhanh / Giữ / Không hỏi lại.",
                static_cast<unsigned>(n.evidence));
            showTrayBalloon(L"KieeKey — Telex nhanh đang sửa nhầm?", body);
            break;
        }
        case Id::TsfSlowDowngrade:
            showTrayBalloon(L"KieeKey — Đã chuyển sang SendInput",
                L"Ứng dụng hiện tại phản hồi TSF chậm nên KieeKey đã chuyển sang xuất trực tiếp "
                L"để giữ độ trễ thấp. Nhấp để biết thêm.");
            break;
        case Id::BarrierTimeouts:
            showTrayBalloon(L"KieeKey — Máy đang quá tải",
                L"Một số phím phải chờ lệnh sửa trước đó lâu hơn bình thường. "
                L"Hồ sơ \"Tự động thích ứng\" sẽ tự nới thời gian chờ.");
            break;
        case Id::HookReinstalled:
            showTrayBalloon(L"KieeKey — Hook bàn phím đã tự phục hồi",
                L"Windows đã gỡ hook bàn phím cấp thấp và KieeKey đã cài lại. Không cần thao tác gì.");
            break;
        case Id::AutoExcludeUnavailable:
            // v1.2.2 RC4 (P2-2): the process monitor failed to start, so
            // auto-exclusion (fullscreen games, elevated apps) is off for
            // this session. Say so once instead of failing silently.
            showTrayBalloon(L"KieeKey — Tự động loại trừ ứng dụng không khả dụng",
                L"Không theo dõi được cửa sổ đang chạy phía trước nên tính năng tự tắt khi chơi game "
                L"toàn màn hình / ứng dụng quản trị bị vô hiệu trong phiên này. Các chế độ gõ vẫn hoạt động.");
            break;
        default: break;
    }
}

// Balloon clicked: the QuickTelex item opens a Vietnamese 3-way choice.
// Every action goes through NotificationCenter::resolve() so "Don't show
// again" is persisted centrally.
void onNotificationClicked() {
    using ok::notify::Action;
    using ok::notify::Id;
    const ok::notify::Notification n = g_shownNotification;
    g_shownNotification = {};
    if (!n.valid()) { return; }
    if (n.id == Id::QuickTelexUnwanted) {
        // TaskDialog-free 3-way choice (MessageBox keeps the UI dependency
        // surface at user32 only): Yes = Tắt, No = Giữ, Cancel = Không hỏi lại.
        const int r = ::MessageBoxW(g.hMain,
            L"Telex nhanh (cc→ch, gg→gi, kk→kh, nn→ng, pp→ph, qq→qu, tt→th, uu→ư) "
            L"có vẻ đang đổi những phụ âm kép bạn muốn giữ nguyên.\n\n"
            L"Yes  = TẮT Telex nhanh\n"
            L"No   = GIỮ bật (nhắc lại sau nếu còn xảy ra)\n"
            L"Cancel = Giữ bật và KHÔNG HỎI LẠI",
            L"KieeKey — Telex nhanh", MB_YESNOCANCEL | MB_ICONQUESTION | MB_TOPMOST);
        if (r == IDYES) {
            {
                std::lock_guard<std::mutex> lk(g.engineMtx);
                g.options.quickTelex = false;
                g.engine.setOptions(g.options);
                g.engine.resetForConfigurationChange();  // v1.2.2 RC2
                g.quickTelexDetector.reset();
            }
            g.notify.resolve(n.id, Action::TurnOff);
            saveSettings();
            if (g.hSettings) { settingsToControls(); }
            showTrayBalloon(L"KieeKey", L"Đã TẮT Telex nhanh. Bật lại trong Cài đặt → Bàn phím.");
        } else if (r == IDCANCEL) {
            g.notify.resolve(n.id, Action::DontShowAgain);
        } else {
            g.notify.resolve(n.id, Action::Keep);
            g.quickTelexDetector.reset();   // start counting afresh
        }
        return;
    }
    if (n.id == Id::TsfSlowDowngrade) {
        openSettingsDialog(3);   // diagnostics tab shows the slow-commit counter
    }
    g.notify.resolve(n.id, Action::Dismissed);
}

// 1 s UI-thread tick: adaptive re-resolution + notification poll.
void onNotifyTick() {
    if (g.perfProfile.load(std::memory_order_relaxed) == static_cast<int>(ok::perf::Profile::Adaptive)) {
        applyPerfStrategy(/*lockEngine=*/true);
    }
    // Hook self-heal telemetry → informational notification (low priority).
    {
        static std::uint64_t s_lastReinstalls = 0;
        const std::uint64_t re = g.hook.hookReinstallCount();
        if (re > s_lastReinstalls) {
            s_lastReinstalls = re;
            g.notify.raise(ok::notify::Id::HookReinstalled, 60,
                           static_cast<std::uint32_t>(re), ::GetTickCount64());
        }
        static std::uint64_t s_lastTimeouts = 0;
        const std::uint64_t to = g.drainBarrier.timeouts();
        if (to >= 50 && to - s_lastTimeouts >= 50) {   // sustained, not a blip
            s_lastTimeouts = to;
            g.notify.raise(ok::notify::Id::BarrierTimeouts, 85,
                           static_cast<std::uint32_t>(to), ::GetTickCount64());
        }
    }
    if (!g.notify.hasPending()) { return; }
    const ok::notify::Notification n = g.notify.takePending(::GetTickCount64());
    if (n.valid()) { presentNotification(n); }
}

// Re-register the tray icon: after an Explorer restart the old icon is gone
// (NIM_ADD), after a wake it may still exist (NIM_ADD fails harmlessly, the
// following NIM_MODIFY refreshes icon + tip either way).
void restoreTrayIcon() noexcept {
    if (!g.hMain) { return; }
    addTrayIcon();
    updateTrayIcon();
}

// Second-instance wake service (first instance only): when a new KieeKey
// process signals kWakeEventName, restore the tray icon and show a balloon
// so the user can see the app is alive (the classic reason for a second
// launch is "the icon disappeared").
void wakeWatcherMain() noexcept {
    HANDLE wake = ::CreateEventW(nullptr, FALSE, FALSE, kWakeEventName);
    if (wake == nullptr) { return; }
    HANDLE objs[2] = { wake, g_wakeExitEvent };
    for (;;) {
        const DWORD w = ::WaitForMultipleObjects(2, objs, FALSE, INFINITE);
        if (w != WAIT_OBJECT_0) { break; }   // exit event (or error) → leave
        if (g_appExiting.load(std::memory_order_relaxed)) { break; }
        if (g.hMain) { ::PostMessageW(g.hMain, WM_APP_RESTORE, 0, 0); }
    }
    ::CloseHandle(wake);
}

// Second-instance side: ask the running instance to restore its tray icon.
void signalRunningInstance() noexcept {
    if (HANDLE ev = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, kWakeEventName)) {
        ::SetEvent(ev);
        ::CloseHandle(ev);
    }
}

// True iff the earlier instance's UI thread answers a bounded ping within
// 2 s (SendMessageTimeout + SMTO_ABORTIFHUNG — the canonical liveness probe
// Explorer/Task Manager themselves use).
bool runningInstanceResponsive() noexcept {
    // v1.1.0-audit fix: "window not yet created" is NOT evidence of a hung
    // instance. The first instance owns the singleton mutex from the top of
    // wWinMain but only creates KieeKeyMain after loadSettings/loadMacros/
    // class registration — a second launch inside that window used to treat
    // the missing window as a zombie and TERMINATED the healthy instance
    // (its PID was already published, image name matched). Retry the find
    // for up to 3 s before concluding the instance is unreachable.
    HWND prev = nullptr;
    for (int attempt = 0; attempt < 30; ++attempt) {
        prev = ::FindWindowW(L"KieeKeyMain", nullptr);
        if (prev != nullptr) { break; }
        ::Sleep(100);
    }
    if (prev == nullptr) { return false; }
    DWORD_PTR res = 0;
    return ::SendMessageTimeoutW(prev, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 2000, &res) != 0;
}

// Takeover of a HUNG earlier instance (its UI thread just failed the ping).
// The PID was published in the registry at startup and is validated against
// our own executable image name, so an unrelated recycled PID can never be
// terminated. Best effort: on any failure we simply fall through.
void terminateStaleInstance() noexcept {
    DWORD pid = 0;
    if (auto key = settingsKey(); key) {
        pid = key.getDword(L"RunningPid", 0);
    }
    if (pid == 0 || pid == ::GetCurrentProcessId()) { return; }
    HANDLE h = ::OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
                             FALSE, pid);
    if (h == nullptr) { return; }
    wchar_t img[MAX_PATH + 1] = {};
    wchar_t self[MAX_PATH + 1] = {};
    DWORD imgSz = MAX_PATH + 1;
    if (::QueryFullProcessImageNameW(h, 0, img, &imgSz) &&
        ::GetModuleFileNameW(nullptr, self, MAX_PATH + 1) != 0) {
        const auto leaf = [](const wchar_t* p) {
            const wchar_t* last = p;
            for (const wchar_t* c = p; *c != L'\0'; ++c) {
                if (*c == L'\\' || *c == L'/') { last = c + 1; }
            }
            return last;
        };
        const auto eqLowerAscii = [](const wchar_t* a, const wchar_t* b) {
            for (;; ++a, ++b) {
                wchar_t x = *a; wchar_t y = *b;
                if (x >= L'A' && x <= L'Z') { x = static_cast<wchar_t>(x - L'A' + L'a'); }
                if (y >= L'A' && y <= L'Z') { y = static_cast<wchar_t>(y - L'A' + L'a'); }
                if (x != y) { return false; }
                if (x == L'\0') { return true; }
            }
        };
        if (eqLowerAscii(leaf(img), leaf(self))) {
            ::TerminateProcess(h, 0);
            ::WaitForSingleObject(h, 5000);
        }
    }
    ::CloseHandle(h);
}

void writeRunningPid() noexcept {
    if (auto key = settingsKey(); key) {
        static_cast<void>(key.setDword(L"RunningPid", ::GetCurrentProcessId()));
    }
}

void clearRunningPid() noexcept {
    if (auto key = settingsKey(); key) {
        static_cast<void>(key.setDword(L"RunningPid", 0));
    }
}

//===========================================================================
// v1.2.0 Stable — POWER / SESSION / DISPLAY lifecycle.
//
// A keyboard IME lives exactly where the OS's worst lifecycle edges are:
// every one of these transitions can drop low-level hook callbacks, lose a
// key-up, change the DPI or monitor topology, or change the foreground
// without an EVENT_SYSTEM_FOREGROUND (Alt-Tab into a lock screen doesn't
// produce one that means anything). KieeKey used to ignore all of them, so
// the state carried across a resume was whatever happened to be cached:
//   * the tracked Shift/Ctrl/CapsLock bits (a modifier released while the
//     secure desktop owned the keyboard is never seen as a key-up);
//   * the cached keyboard layout HKL (a layout switch on the lock screen);
//   * the per-app exclusion + TSF-vs-inline policy for a foreground that no
//     longer exists;
//   * and any pending-edit count, with no consumer wake coming for it.
// The resync below is the "known-good state" the release notes promise: it is
// cheap (it runs once per transition, never per keystroke) and it is
// idempotent, so a spurious or repeated message is harmless.
//
// Suspend/lock is the conservative half: the pending word is dropped, because
// a key-up lost across the transition would otherwise leave a phantom
// composition that the next keystroke composes onto.
//===========================================================================
void onLifecycleSuspend() noexcept {
    // Drop the pending word: no key-up is guaranteed to arrive across a
    // suspend / lock, so any half-composed word is already untrustworthy.
    std::lock_guard<std::mutex> lk(g.engineMtx);
    g.engine.startNewSession();
}

void onLifecycleResume() noexcept {
    // 1. Re-seed the delta-tracked modifier state from the OS (the same
    //    resync the ForegroundChanged path performs, for the same reason).
    g.hook.resyncModifiersFromOs();
    // 2. Re-read the foreground: the monitor's snapshot, the exclusion cache,
    //    the TSF-vs-inline policy and the cached keyboard layout.
    g.monitor.refreshNow();
    updateExclusionCache();
    updateForegroundPolicy();
    // 3. Any pending-edit count predates the transition; a consumer parked
    //    through it has no wake coming. Poke + drain (bounded), and force
    //    quiescence only if the consumer is genuinely wedged.
    drainPendingEditsForLifecycle();
    // 4. Clear the foreground-hang gate: a probe that was in flight when the
    //    machine suspended will never complete, and a stuck "pending" flag
    //    would keep the output path on inline SendInput for that foreground
    //    forever (a silent, permanent downgrade).
    g.fgProbePending_.store(false, std::memory_order_relaxed);
    // 5. The pending word is dropped (see onLifecycleSuspend) and the tray
    //    tooltip is refreshed (the foreground may have changed under us).
    {
        std::lock_guard<std::mutex> lk(g.engineMtx);
        g.engine.startNewSession();
    }
    updateTrayIcon();
}

// UI thread (posted as WM_APP_FGPROBE from the hook pump after a foreground
// switch that selected the TSF policy): a bounded WM_NULL ping decides
// whether the new foreground may receive synchronous TSF edit sessions.
// Hung → the policy stays inline SendInput (cannot block); healthy → the
// snapshot-based policy is restored. 150 ms budget, once per app switch.
void probeForegroundResponsiveness(HWND probedHwnd) noexcept {
    const auto s = g.monitor.snapshot();
    const HWND fg = (s ? s->hwnd : nullptr);
    bool healthy = true;
    if (fg != nullptr && (probedHwnd == nullptr || probedHwnd == fg)) {
        DWORD_PTR res = 0;
        healthy = ::SendMessageTimeoutW(fg, WM_NULL, 0, 0,
                                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 150,
                                        &res) != 0;
    } else {
        // v1.2.0 Stable: the foreground changed again while the probe was in
        // flight (Alt-Tab twice in 150 ms is ordinary). The old code restored
        // the snapshot-based policy unconditionally, which re-armed TSF for a
        // window this probe never actually pinged — the exact hang the gate
        // exists to prevent. A stale probe result is simply discarded; the
        // newer foreground switch has already posted its own probe.
        healthy = false;
    }
    if (healthy) {
        updateForegroundPolicy();   // restore the snapshot-based policy (TSF if chosen)
    } else {
        if (fg != nullptr && (probedHwnd == nullptr || probedHwnd == fg)) {
            g.fgHungCount.fetch_add(1, std::memory_order_relaxed);
        }
        // Keep inline — fgUseTsf_ was already cleared by the producer.
    }
    // v1.1.0-audit fix: the probe ran — the pending flag was set by the
    // producer and never cleared anywhere (dead state contradicting the
    // documented invariant). Clear it in BOTH outcomes so the flag means
    // "probe in flight", matching its declaration comment.
    g.fgProbePending_.store(false, std::memory_order_relaxed);
}

//===========================================================================
// Settings window (Win32, Vietnamese UI — built in code for pixel control)
// v1.1.0: DPI-aware layout — every coordinate below is expressed in 96-dpi
// logical pixels and scaled to the dialog's actual DPI at creation (the
// manifest already opts the process into PerMonitorV2). Fixes the clipped
// "Luôn SendInput" radio and the shrunken controls at 125–200 % scaling.
//===========================================================================
void showTab(int tab);   // fwd (defined below; used by settingsToControls)

// Per-monitor DPI of the window (GetDpiForWindow when available; the
// classic LOGPIXELSX fallback keeps MinGW/older SDKs compiling).
UINT windowDpi(HWND hwnd) noexcept {
    if (const HMODULE user32 = ::GetModuleHandleW(L"user32.dll")) {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        const auto fn = reinterpret_cast<GetDpiForWindowFn>(
            ::GetProcAddress(user32, "GetDpiForWindow"));
        if (fn != nullptr) {
            const UINT d = fn(hwnd);
            if (d != 0) { return d; }
        }
    }
    HDC dc = ::GetDC(hwnd);
    const UINT dpi = (dc != nullptr)
        ? static_cast<UINT>(::GetDeviceCaps(dc, LOGPIXELSX)) : 96;
    if (dc != nullptr) { ::ReleaseDC(hwnd, dc); }
    return dpi ? dpi : 96;
}

namespace {

// v1.3.0-beta6 (V4): refresh the B1/B4 evidence blocks from live Win32
// state. Called on foreground change and right before any report is
// generated/exported/copied, so the numbers are the machine's AT THAT
// MOMENT, not stale startup values. All probes are dynamic GetProcAddress
// lookups: the exe must build against older SDKs and run on a clean machine.
UINT systemDpiOrFallback() noexcept {
    if (const HMODULE user32 = ::GetModuleHandleW(L"user32.dll")) {
        using Fn = UINT(WINAPI*)();
        const auto fn = reinterpret_cast<Fn>(::GetProcAddress(user32, "GetDpiForSystem"));
        if (fn != nullptr) {
            const UINT d = fn();
            if (d != 0) { return d; }
        }
    }
    const HDC dc = ::GetDC(nullptr);
    const UINT dpi = (dc != nullptr)
        ? static_cast<UINT>(::GetDeviceCaps(dc, LOGPIXELSX)) : 96;
    if (dc != nullptr) { ::ReleaseDC(nullptr, dc); }
    return dpi ? dpi : 96;
}

void refreshEvidenceContext() noexcept {
    try {
        auto& diag = ok::diag::Diagnostics::instance();

        // B4: HOW the foreground process name was resolved (the monitor
        // stamps nameApi at resolution time; see ProcessMonitor.cpp).
        if (const auto snap = g.monitor.snapshot()) {
            ok::diag::ProcessResolution res{};
            res.pid = snap->pid;
            res.api = snap->nameApi;
            res.error = snap->nameQueryError;
            res.elevated = snap->elevated;
            res.name = snap->exeNameUtf8;
            diag.setProcessResolution(std::move(res));
        }

        // B1: the real DPI / font / DWM numbers.
        ok::diag::DisplayMetrics dm{};
        const HWND probe = (g.hSettings != nullptr) ? g.hSettings
                                                    : ::GetForegroundWindow();
        dm.dpi = (probe != nullptr) ? windowDpi(probe) : systemDpiOrFallback();
        dm.systemDpi = systemDpiOrFallback();
        dm.fontFace = "Segoe UI";          // the dialog's face (see cachedFont)
        dm.fontHeightPx = -::MulDiv(13, static_cast<int>(dm.dpi), 96);

        if (const HMODULE dwmapi = ::LoadLibraryW(L"dwmapi.dll")) {
            using DwmFn = HRESULT(WINAPI*)(BOOL*);
            const auto fn = reinterpret_cast<DwmFn>(
                ::GetProcAddress(dwmapi, "DwmIsCompositionEnabled"));
            BOOL enabled = FALSE;
            if (fn != nullptr && fn(&enabled) == S_OK) {
                dm.dwmComposition = (enabled == TRUE);
            }
            ::FreeLibrary(dwmapi);
        }
        if (const HMODULE shcore = ::LoadLibraryW(L"shcore.dll")) {
            // PROCESS_DPI_AWARENESS: 0 unaware, 1 system, 2 per-monitor.
            using AwareFn = HRESULT(WINAPI*)(HANDLE, int*);
            const auto fn = reinterpret_cast<AwareFn>(
                ::GetProcAddress(shcore, "GetProcessDpiAwareness"));
            int awareness = 0;
            if (fn != nullptr && fn(nullptr, &awareness) == S_OK) {
                dm.perMonitorAware = (awareness >= 2);
            }
            ::FreeLibrary(shcore);
        }
        dm.screenWidth = static_cast<std::uint32_t>(::GetSystemMetrics(SM_CXSCREEN));
        dm.screenHeight = static_cast<std::uint32_t>(::GetSystemMetrics(SM_CYSCREEN));
        diag.setDisplayMetrics(std::move(dm));
    } catch (...) {
        // Evidence must never take the IME down.
    }
}

// v1.3.0-beta7: full SystemSnapshot refresh — the source-of-truth pass that
// closes the beta6 report gap (os/arch/appVersion/foreground/keyboardLayout/
// outputMode/inputMethod/codeTable/memory/cpu/uptime/dpi/ime/hook/fgHook/
// liveEffects/excluded all stuck at 0/empty/96). Pure Win32 reads, noexcept,
// best-effort: any failure leaves that field at its last good value rather
// than taking the IME down. Called on the UI thread before every report/
 // quick-check / export / copy and from the hook's foreground path.
void refreshSystemSnapshot() noexcept {
    try {
        auto& diag = ok::diag::Diagnostics::instance();
        ok::diag::SystemSnapshot snap = diag.systemSnapshot();

        // -- OS name + arch (portable, no versionhelpers) --
        {
            OSVERSIONINFOEXW ovi{};
            ovi.dwOSVersionInfoSize = sizeof(ovi);
            // GetVersionEx is shimmed by manifest; use RtlGetVersion dynamically.
            if (const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll")) {
                using RtlFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);
                auto fn = reinterpret_cast<RtlFn>(::GetProcAddress(ntdll, "RtlGetVersion"));
                if (fn != nullptr) { fn(&ovi); }
                else { ::GetVersionExW(reinterpret_cast<OSVERSIONINFOW*>(&ovi)); }
            } else {
                ::GetVersionExW(reinterpret_cast<OSVERSIONINFOW*>(&ovi));
            }
            std::string os = "Windows " + std::to_string(ovi.dwMajorVersion) + "." +
                             std::to_string(ovi.dwMinorVersion) +
                             " (build " + std::to_string(ovi.dwBuildNumber) + ")";
            // Product name from registry (e.g. "Windows 11 Pro") if available
            HKEY hk = nullptr;
            if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hk) == ERROR_SUCCESS) {
                wchar_t prod[128]{}; DWORD sz = sizeof(prod); DWORD tp = 0;
                if (::RegQueryValueExW(hk, L"ProductName", nullptr, &tp, reinterpret_cast<BYTE*>(prod), &sz) == ERROR_SUCCESS && tp == REG_SZ) {
                    os = utf16ToUtf8(prod) + " " + os;
                }
                ::RegCloseKey(hk);
            }
            snap.osName = std::move(os);
        }
        {
            SYSTEM_INFO si{}; ::GetNativeSystemInfo(&si);
            switch (si.wProcessorArchitecture) {
                case PROCESSOR_ARCHITECTURE_AMD64: snap.arch = "x64"; break;
                case PROCESSOR_ARCHITECTURE_ARM:   snap.arch = "ARM"; break;
                case PROCESSOR_ARCHITECTURE_ARM64: snap.arch = "ARM64"; break;
                case PROCESSOR_ARCHITECTURE_IA64:  snap.arch = "IA64"; break;
                default: snap.arch = "x86"; break;
            }
            // ARM64EC (emulated x64 on ARM64) — IsWow64Process2 dynamic.
            if (snap.arch == "ARM64") {
                if (const HMODULE kern = ::GetModuleHandleW(L"kernel32.dll")) {
                    using WowFn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
                    auto fn = reinterpret_cast<WowFn>(::GetProcAddress(kern, "IsWow64Process2"));
                    USHORT procMach = 0, nativeMach = 0;
                    if (fn != nullptr && fn(::GetCurrentProcess(), &procMach, &nativeMach)) {
                        if (procMach == 0x8664) { snap.arch = "ARM64EC"; } // IMAGE_FILE_MACHINE_AMD64
                    }
                }
            }
        }
        {
            // App version: marketing + PE numeric (from version resource if present)
            std::string ver = utf16ToUtf8(kAppVersionFull);
            // Try reading FileVersion from the exe's version resource
            wchar_t exePath[MAX_PATH]{};
            if (::GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
                DWORD handle = 0;
                DWORD sz = ::GetFileVersionInfoSizeW(exePath, &handle);
                if (sz != 0) {
                    std::vector<std::uint8_t> buf(sz);
                    if (::GetFileVersionInfoW(exePath, handle, sz, buf.data())) {
                        VS_FIXEDFILEINFO* ffi = nullptr; UINT len = 0;
                        if (::VerQueryValueW(buf.data(), L"\\", reinterpret_cast<void**>(&ffi), &len) && ffi != nullptr) {
                            ver += " (PE " + std::to_string(HIWORD(ffi->dwFileVersionMS)) + "." +
                                   std::to_string(LOWORD(ffi->dwFileVersionMS)) + "." +
                                   std::to_string(HIWORD(ffi->dwFileVersionLS)) + "." +
                                   std::to_string(LOWORD(ffi->dwFileVersionLS)) + ")";
                        }
                    }
                }
            }
            snap.appVersion = std::move(ver);
        }
        // -- uptime / memory / cpu --
        {
            const std::uint64_t now = ::GetTickCount64();
            if (g_startTickMs == 0) { g_startTickMs = now; }
            snap.uptimeMs = (now >= g_startTickMs) ? (now - g_startTickMs) : 0;
        }
        {
            PROCESS_MEMORY_COUNTERS pmc{};
            pmc.cb = sizeof(pmc);
            if (::GetProcessMemoryInfo(::GetCurrentProcess(), &pmc, sizeof(pmc))) {
                snap.workingSetKb = pmc.WorkingSetSize / 1024;
                snap.peakWorkingSetKb = pmc.PeakWorkingSetSize / 1024;
            }
        }
        {
            FILETIME ct{}, et{}, kt{}, ut{};
            if (::GetProcessTimes(::GetCurrentProcess(), &ct, &et, &kt, &ut)) {
                auto ftToMs = [](FILETIME ft) -> std::uint64_t {
                    ULARGE_INTEGER v{}; v.LowPart = ft.dwLowDateTime; v.HighPart = ft.dwHighDateTime;
                    return v.QuadPart / 10000ULL;
                };
                snap.kernelTimeMs = ftToMs(kt);
                snap.userTimeMs = ftToMs(ut);
                const std::uint64_t totalMs = snap.kernelTimeMs + snap.userTimeMs;
                if (snap.uptimeMs > 0) {
                    // cpu % since process start (single sample, not delta)
                    // totalMs is per-core time; normalize by uptime and cpu count is not needed for "since start" estimate — use wall time.
                    snap.cpuPercentSinceStart = (static_cast<double>(totalMs) * 100.0) / static_cast<double>(snap.uptimeMs);
                    if (snap.cpuPercentSinceStart > 100.0 * 64) { snap.cpuPercentSinceStart = 0.0; } // guard overflow
                }
            }
        }
        // -- foreground app / layout / output mode / input method / code table --
        {
            if (const auto s = g.monitor.snapshot()) {
                std::string app = s->exeNameUtf8;
                if (app.empty()) { app = "pid " + std::to_string(s->pid); }
                // Append policy hint like the status line
                if (g.fgExcluded_.load(std::memory_order_relaxed)) {
                    app += g.monitor.currentAppElevated() ? " (elevated, pass-through)" : " (excluded)";
                } else {
                    app += g.fgUseTsf_.load(std::memory_order_relaxed) ? " (TSF)" : " (SendInput)";
                }
                snap.foregroundApp = std::move(app);
            } else {
                snap.foregroundApp = "(none)";
            }
        }
        {
            const HKL hkl = g.currentHkl.load(std::memory_order_relaxed);
            wchar_t lang[16]{};
            // LOWORD = LANGID, format as hex 8-digit KLID-like
            swprintf_s(lang, L"%08X", static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(hkl)));
            snap.keyboardLayout = utf16ToUtf8(lang);
            // Also try to get locale name for readability
            wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
            if (::LCIDToLocaleName(MAKELCID(LOWORD(hkl), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0) > 0) {
                snap.keyboardLayout += " (" + utf16ToUtf8(name) + ")";
            }
        }
        {
            const int om = g.outputMode.load(std::memory_order_relaxed);
            if (om == 1) { snap.outputMode = "Always TSF"; }
            else if (om == 2) { snap.outputMode = "Always SendInput"; }
            else {
                snap.outputMode = g.fgUseTsf_.load(std::memory_order_relaxed) ? "Auto (TSF)" : "Auto (SendInput)";
            }
        }
        {
            switch (g.options.inputMethod) {
                case InputMethod::Telex: snap.inputMethod = "Telex"; break;
                case InputMethod::Vni: snap.inputMethod = "VNI"; break;
                case InputMethod::SimpleTelex: snap.inputMethod = "SimpleTelex"; break;
                default: snap.inputMethod = "Telex"; break;
            }
        }
        {
            switch (g.options.codeTable) {
                case CodeTable::Unicode: snap.codeTable = "Unicode"; break;
                case CodeTable::Tcvn3: snap.codeTable = "TCVN3"; break;
                case CodeTable::VniWindows: snap.codeTable = "VNI Windows"; break;
                case CodeTable::UnicodeCompound: snap.codeTable = "UnicodeCompound"; break;
                case CodeTable::Cp1258: snap.codeTable = "CP1258"; break;
                default: snap.codeTable = "Unicode"; break;
            }
        }
        // -- dpi / flags --
        {
            const HWND probe = (g.hSettings != nullptr) ? g.hSettings : ::GetForegroundWindow();
            snap.dpi = (probe != nullptr) ? windowDpi(probe) : systemDpiOrFallback();
            if (snap.dpi == 0) { snap.dpi = 96; }
        }
        snap.imeEnabled = g.engineEnabled.load(std::memory_order_relaxed);
        snap.hookInstalled = g.hook.running();
        // fgHookInstalled: the foreground window's thread has a hook opportunity?
        // We treat hookInstalled as proxy: if our LL hook is installed, fg hook is conceptually installed.
        snap.fgHookInstalled = snap.hookInstalled;
        snap.liveEffectsEnabled = g.liveEffects.enabled();
        snap.excludedApp = g.fgExcluded_.load(std::memory_order_relaxed);

        diag.setSystemSnapshot(snap);
        // Also refresh the B1/B4 evidence blocks so snapshot and displayMetrics never disagree at report time.
        refreshEvidenceContext();
    } catch (...) {
        // Snapshot must never take the IME down.
    }
}

void syncDiagnosticsCounters() noexcept {
    try {
        auto& diag = ok::diag::Diagnostics::instance();
        // HookCounters -> Diagnostics counters (the beta6 gap: hook.pushed/
        // dropped / wakes were live but never reached the report, so the
        // report could show 0 while the wrapper had seen thousands).
        const auto& hc = g.hook.counters();
        diag.set(ok::diag::Counter::QueuedToConsumer, g.hook.pushed());
        diag.set(ok::diag::Counter::QueueOverflowDropped, g.hook.dropped());
        diag.set(ok::diag::Counter::ConsumerWakes, hc.consumerWakeups.load(std::memory_order_relaxed));
        diag.set(ok::diag::Counter::SetEventSyscalls, hc.setEventSyscalls.load(std::memory_order_relaxed));
        // Keyboard/mouse/foreground are already counted in onHookEvent, but
        // rebasing from the source-of-truth HookCounters here guarantees the
        // report and the UI can never drift (e.g. after a raw injected event
        // that bypassed the producer handler).
        // We only overwrite if the hook counter is non-zero to avoid clearing
        // diagnostics-only increments (like SendInputCalls which lives outside hook).
        // Instead, we ensure hook counters dominate for those sources.
        const std::uint64_t kbd = hc.keyboardEvents();
        if (kbd != 0) {
            // Decompose into KeyDown/KeyUp for compatibility: report shows both.
            // We set them proportionally? Instead just ensure keyboardEvents total matches.
            // Diagnostics::keyboardEvents() == KeyDown+KeyUp, so if our total differs, adjust.
            const std::uint64_t cur = diag.keyboardEvents();
            if (cur != kbd) {
                // Rebase KeyDown to match kbd, zero KeyUp — total is what matters for health checks.
                // But to preserve split, distribute: half up, half down roughly.
                // Simpler: set KeyDown to kbd, KeyUp delta is extra — but keyboardEvents() counts both.
                // So set KeyDown = kbd, KeyUp = 0 if cur != kbd; total will be kbd.
                // First zero both via set, then set KeyDown.
                diag.set(ok::diag::Counter::KeyDown, kbd);
                diag.set(ok::diag::Counter::KeyUp, 0);
            }
        }
    } catch (...) {
    }
}

void refreshDiagnostics() noexcept {
    refreshSystemSnapshot();
    syncDiagnosticsCounters();
}

} // namespace

// v1.1.0: the settings dialog's DPI (set in WM_CREATE before controls are
// built; uiFont() scales the face height to match).
UINT g_settingsDpi = 96;

// v1.1.2 — the tab the settings window shows on creation (0..4; 4 = the
// Information tab, used by the tray menu's “Thông tin & giới thiệu”).
int g_settingsOpenTab = 0;

//===========================================================================
// v1.2.0 Stable — one per-DPI font cache for all three face variants.
//
// The v1.1.x caches EVICTED an entry (round-robin over four slots) and
// DeleteObject()'d the font it kicked out — while live controls still held
// that HFONT. A user who dragged the dialog across four different-DPI
// monitors (or whose monitor set changed scaling at runtime) could therefore
// end up with controls painting through a deleted GDI object: garbled or
// default-font text, with no error anywhere. That is a use-after-free with a
// cosmetic symptom, which is exactly the class of bug this release audits
// for.
//
// The fix is the boring one: the working set is bounded by the NUMBER OF
// DISTINCT DPI VALUES A DESKTOP CAN HAVE (a handful; 8 slots is generous),
// each font is a few KB, and the process exits soon after the dialog closes.
// So the cache never evicts — it returns the closest existing font if it is
// ever full, which degrades to "slightly wrong point size" instead of
// "painting through freed memory".
//===========================================================================
namespace {
struct FontCache {
    struct Entry { UINT dpi = 0; int weight = 0; int px96 = 0; HFONT font = nullptr; };
    static constexpr std::size_t kSlots = 8;
    Entry slots[kSlots]{};
};

// px96 = face height in 96-dpi logical pixels (negative for "match this
// cell height", the CreateFont convention used throughout this dialog).
HFONT cachedFont(int px96, int weight) noexcept {
    static FontCache cache;
    const UINT dpi = g_settingsDpi ? g_settingsDpi : 96;
    std::size_t freeSlot = FontCache::kSlots;
    for (std::size_t i = 0; i < FontCache::kSlots; ++i) {
        FontCache::Entry& e = cache.slots[i];
        if (e.font == nullptr) { if (freeSlot == FontCache::kSlots) { freeSlot = i; } continue; }
        if (e.dpi == dpi && e.weight == weight && e.px96 == px96) { return e.font; }
    }
    if (freeSlot == FontCache::kSlots) {
        // Cache full (more than 8 distinct DPI/face combinations — not
        // reachable on real hardware). Reuse a font that already exists
        // rather than deleting one that is in use.
        return cache.slots[0].font;
    }
    const int px = -::MulDiv(px96, static_cast<int>(dpi), 96);
    HFONT f = ::CreateFontW(px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Segoe UI");
    if (f != nullptr) {
        cache.slots[freeSlot] = FontCache::Entry{dpi, weight, px96, f};
        return f;
    }
    // CreateFont failed (out of GDI handles): fall back to any font we
    // already own, else nullptr (controls then keep the system font).
    for (std::size_t i = 0; i < FontCache::kSlots; ++i) {
        if (cache.slots[i].font != nullptr) { return cache.slots[i].font; }
    }
    return nullptr;
}
} // namespace

HFONT uiFont() noexcept      { return cachedFont(13, FW_NORMAL); }
HFONT uiFontBold() noexcept  { return cachedFont(13, FW_SEMIBOLD); }
HFONT uiFontTitle() noexcept { return cachedFont(20, FW_SEMIBOLD); }

//===========================================================================
// v1.2.0 Stable — live DPI re-scaling of an OPEN settings dialog.
//
// WM_DPICHANGED used to resize only the frame and leave every child control
// at the creation monitor's scale (documented in the old comment as a known
// limitation): drag the window to a 150 % monitor and the labels kept their
// 100 % geometry and font, so the right-hand column overflowed the frame and
// the fonts looked blurry or clipped. Rebuilding the dialog would fix the
// layout but throw away unsaved edits, which is worse.
//
// This scales every child rectangle in place by newDpi/oldDpi and re-applies
// the matching per-DPI font, preserving all control content. It runs on a
// monitor/DPI change only — never on the input path.
//===========================================================================
namespace {
struct RescaleCtx {
    UINT  oldDpi;
    UINT  newDpi;
    HFONT oldNormal;
    HFONT oldBold;
    HFONT oldTitle;
    HFONT newNormal;
    HFONT newBold;
    HFONT newTitle;
};

BOOL CALLBACK rescaleChild(HWND child, LPARAM lp) noexcept {
    const auto* ctx = reinterpret_cast<const RescaleCtx*>(lp);
    if (ctx == nullptr || ctx->oldDpi == 0 || ctx->newDpi == 0) { return TRUE; }

    RECT rc{};
    if (!::GetWindowRect(child, &rc)) { return TRUE; }
    HWND parent = ::GetParent(child);
    if (parent == nullptr) { return TRUE; }
    // Work in parent-client coordinates so a border/style change cannot skew
    // the mapping, then scale about the origin and write it back.
    ::MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rc), 2);
    auto scale = [ctx](LONG v) noexcept -> LONG {
        return ::MulDiv(v, static_cast<int>(ctx->newDpi), static_cast<int>(ctx->oldDpi));
    };
    const LONG x = scale(rc.left);
    const LONG y = scale(rc.top);
    const LONG w = scale(rc.right - rc.left);
    const LONG h = scale(rc.bottom - rc.top);
    ::SetWindowPos(child, nullptr, x, y, w, h,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);

    // Re-apply the font that matches the control's weight class at the NEW
    // DPI (the control keeps whatever it had if we cannot classify it).
    const HFONT cur = reinterpret_cast<HFONT>(
        ::SendMessageW(child, WM_GETFONT, 0, 0));
    HFONT replacement = nullptr;
    if (cur == ctx->oldTitle)       { replacement = ctx->newTitle; }
    else if (cur == ctx->oldBold)   { replacement = ctx->newBold; }
    else if (cur == ctx->oldNormal) { replacement = ctx->newNormal; }
    if (replacement != nullptr && replacement != cur) {
        ::SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(replacement), TRUE);
    }
    return TRUE;
}
} // namespace

// Re-scale an open settings dialog after its DPI changed (WM_DPICHANGED,
// WM_DISPLAYCHANGE). No-op when the dialog is closed or the DPI is unchanged.
void refreshSettingsDpi() noexcept {
    if (!g.hSettings) { return; }
    const UINT newDpi = windowDpi(g.hSettings);
    if (newDpi == 0 || newDpi == g_settingsDpi) { return; }

    // Snapshot the OLD fonts (they are still alive — the cache never
    // deletes an in-use font) so each control can be re-classified.
    const RescaleCtx ctx{
        g_settingsDpi, newDpi,
        uiFont(), uiFontBold(), uiFontTitle(),
        nullptr, nullptr, nullptr};
    g_settingsDpi = newDpi;   // cachedFont() now mints the new-DPI faces
    RescaleCtx full{
        ctx.oldDpi, newDpi,
        ctx.oldNormal, ctx.oldBold, ctx.oldTitle,
        uiFont(), uiFontBold(), uiFontTitle()};
    ::EnumChildWindows(g.hSettings, &rescaleChild,
                       reinterpret_cast<LPARAM>(&full));
    ::InvalidateRect(g.hSettings, nullptr, TRUE);
}

HWND mkCtl(HWND parent, LPCWSTR cls, LPCWSTR text, DWORD style, int x, int y,
           int w, int h, HMENU id) {
    HWND c = ::CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE,
                               x, y, w, h, parent, id, g.hInst, nullptr);
    if (c) { ::SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()), TRUE); }
    return c;
}

// v1.1.2 — refresh the always-visible header status line: engine state,
// input method, code table and the digits policy at a glance.
void updateHeaderStatus() {
    if (!g.hSettings) { return; }
    const wchar_t* method = g.options.inputMethod == InputMethod::Telex ? L"Telex"
                          : g.options.inputMethod == InputMethod::Vni   ? L"VNI"
                                                                        : L"Simple Telex";
    std::wstring s = g.engineEnabled.load(std::memory_order_relaxed)
                         ? L"● Bộ gõ: ĐANG BẬT"
                         : L"● Bộ gõ: ĐANG TẮT";
    s += L"  —  ";
    s += method;
    s += L"  —  Số 0–9: ";
    s += g.options.digitsAreLiteral ? L"chữ số" : L"gõ dấu (VNI)";
    ::SetWindowTextW(::GetDlgItem(g.hSettings, IDC_STAT_HEAD_STATUS), s.c_str());
}

// v1.1.2-r2 FAIL-SAFE CONTROL READS. IsDlgButtonChecked()/SendMessageW() on
// a missing or just-destroyed control return 0 — which for a checkbox means
// "unchecked". Historically that class of silent zero could flip a persisted
// option without the user ever seeing the control (the exact mechanism that
// can resurrect the digits bug: OK-ing a half-built dialog would re-enable
// digit composition and persist it). The fallback is the CURRENT in-memory
// value, so the dialog can only CHANGE an option through a real control.
bool dlgChecked(HWND h, int id, bool fallback) noexcept {
    return ::GetDlgItem(h, id)
               ? (::IsDlgButtonChecked(h, id) == BST_CHECKED)
               : fallback;
}

void settingsFromControls() {
    const bool telex = (::SendMessageW(::GetDlgItem(g.hSettings, IDC_RADIO_TELEX),
                                       BM_GETCHECK, 0, 0) == BST_CHECKED);
    const bool vni   = (::SendMessageW(::GetDlgItem(g.hSettings, IDC_RADIO_VNI),
                                       BM_GETCHECK, 0, 0) == BST_CHECKED);
    const LRESULT codeTableSel =
        ::SendMessageW(::GetDlgItem(g.hSettings, IDC_COMBO_CODETABLE), CB_GETCURSEL, 0, 0);
    // v1.1.0: the macro editor content is the source of truth — parse it and
    // persist to %APPDATA%\KieeKey\macros.txt (previously the macro checkbox
    // was a silent no-op: no resolver was ever installed).
    const HWND edit = ::GetDlgItem(g.hSettings, IDC_EDIT_MACRO);
    std::wstring macroText;
    if (edit != nullptr) {
        const int len = ::GetWindowTextLengthW(edit);
        macroText.assign(static_cast<std::size_t>(len) + 1, L'\0');
        const int got = ::GetWindowTextW(edit, macroText.data(), len + 1);
        macroText.resize(static_cast<std::size_t>(got > 0 ? got : 0));
    }
    // v1.1.3 latency fix: hoist EVERY control read above the engine lock —
    // each IsDlgButtonChecked/SendMessageW is a cross-thread control call
    // that used to extend the hook thread's stall window to the whole block
    // on every OK/Apply. Only the STATE SWAP runs under engineMtx now (the
    // checkbox reads touch no shared state).
    const bool chkMacro    = dlgChecked(g.hSettings, IDC_CHK_MACRO,    g.options.useMacro);
    const bool chkDigits   = dlgChecked(g.hSettings, IDC_CHK_DIGITS,   g.options.digitsAreLiteral);
    const bool chkSpell    = dlgChecked(g.hSettings, IDC_CHK_SPELL,    g.options.checkSpelling);
    const bool chkRestore  = dlgChecked(g.hSettings, IDC_CHK_RESTORE,  g.options.restoreIfWrongSpelling);
    const bool chkUpper    = dlgChecked(g.hSettings, IDC_CHK_UPPER,    g.options.upperCaseFirstChar);
    const bool chkModern   = dlgChecked(g.hSettings, IDC_CHK_MODERN,   g.options.useModernOrthography);
    const bool chkQuick    = dlgChecked(g.hSettings, IDC_CHK_QUICK,    g.options.quickTelex);
    const bool chkExclIde  = dlgChecked(g.hSettings, IDC_CHK_EXCLUDE_IDE,  g.exclIde);
    const bool chkExclGame = dlgChecked(g.hSettings, IDC_CHK_EXCLUDE_GAME, g.exclGame);
    const bool chkExclShl  = dlgChecked(g.hSettings, IDC_CHK_EXCLUDE_SHELL, g.exclShell);
    const HWND outAutoCtl  = ::GetDlgItem(g.hSettings, IDC_RADIO_OUT_AUTO);
    const bool outAuto     = outAutoCtl && (::SendMessageW(outAutoCtl, BM_GETCHECK, 0, 0) == BST_CHECKED);
    const HWND outTsfCtl   = ::GetDlgItem(g.hSettings, IDC_RADIO_OUT_TSF);
    const bool outTsf      = outTsfCtl && (::SendMessageW(outTsfCtl, BM_GETCHECK, 0, 0) == BST_CHECKED);
    const bool hasTelexCtl = ::GetDlgItem(g.hSettings, IDC_RADIO_TELEX) != nullptr;
    const bool hasComboCtl = ::GetDlgItem(g.hSettings, IDC_COMBO_CODETABLE) != nullptr;
    // v1.2.1 RC2: performance profile + hybrids + notification mute (all
    // read above the lock, fail-safe to the current values).
    const HWND perfCtl     = ::GetDlgItem(g.hSettings, IDC_COMBO_PERF);
    const int  perfSel     = perfCtl ? static_cast<int>(::SendMessageW(perfCtl, CB_GETCURSEL, 0, 0))
                                     : g.perfProfile.load(std::memory_order_relaxed);
    const unsigned curHyb  = g.perfHybrid.load(std::memory_order_relaxed);
    const bool hybLowCpu   = dlgChecked(g.hSettings, IDC_CHK_PERF_LOWCPU, (curHyb & ok::perf::kHybridLowCpu) != 0);
    const bool hybDict     = dlgChecked(g.hSettings, IDC_CHK_PERF_DICT,   (curHyb & ok::perf::kHybridExtraCorrect) != 0);
    const bool notifyOn    = dlgChecked(g.hSettings, IDC_CHK_NOTIFY, !g.notify.sessionMuted());
    // v1.1.0 (race fix): parse + swap the table UNDER engineMtx — the
    // hook thread reads g_macros through the resolver inside
    // engine.process(), which always runs under this lock. The FILE WRITE
    // is deliberately outside the lock (see applyMacrosText/writeMacrosFile).
    {
        std::lock_guard<std::mutex> lk(g.engineMtx);
        if (edit != nullptr) { applyMacrosText(macroText); }
        // v1.1.2-r2: radio/combo groups follow the same fail-safe rule — the
        // assignment only happens when its controls actually exist; a null
        // HWND would otherwise fall through to the LAST enum member
        // (SimpleTelex / CP1258 / SendInput) on a half-built dialog.
        if (hasTelexCtl) {
            g.options.inputMethod = telex ? InputMethod::Telex
                                  : vni   ? InputMethod::Vni
                                  :         InputMethod::SimpleTelex;
        }
        if (hasComboCtl) {
            g.options.codeTable = (codeTableSel >= 0 && codeTableSel <= 4)
                ? static_cast<CodeTable>(codeTableSel) : CodeTable::Unicode;
            g.codeTableCache.store(static_cast<int>(g.options.codeTable), std::memory_order_relaxed);
        }
        // v1.1.3: every value below was read ABOVE the lock (fail-safe
        // dlgChecked fallbacks — a missing control can never silently turn
        // an option off); only the assignments remain in the critical section.
        g.options.useMacro                 = chkMacro;
        g.options.digitsAreLiteral         = chkDigits;
        g.options.checkSpelling            = chkSpell;
        g.options.restoreIfWrongSpelling   = chkRestore;
        g.options.upperCaseFirstChar       = chkUpper;
        g.options.useModernOrthography     = chkModern;
        g.options.quickTelex               = chkQuick;
        g.exclIde                          = chkExclIde;
        g.exclGame                         = chkExclGame;
        g.exclShell                        = chkExclShl;
        if (outAutoCtl != nullptr) {
            g.outputMode.store(outAuto ? 0 : outTsf ? 1 : 2, std::memory_order_relaxed);
        }
        if (perfSel >= 0 && perfSel < static_cast<int>(ok::perf::Profile::kCount)) {
            g.perfProfile.store(perfSel, std::memory_order_relaxed);
        }
        g.perfHybrid.store((hybLowCpu ? ok::perf::kHybridLowCpu : 0u) |
                           (hybDict ? ok::perf::kHybridExtraCorrect : 0u), std::memory_order_relaxed);
        g.notify.setSessionMuted(!notifyOn);
        g.engine.setOptions(g.options);
        g.engine.resetForConfigurationChange();  // v1.2.2 RC2
        applyPerfStrategy(/*lockEngine=*/false, /*force=*/true);   // already under engineMtx
        g.monitor.setExcludeIde(g.exclIde);
        g.monitor.setExcludeGame(g.exclGame);
        g.monitor.setExcludeShell(g.exclShell);
        updateExclusionCache();
        updateForegroundPolicy();   // output mode affects the TSF-vs-inline decision
    }
    // Disk write OUTSIDE engineMtx: never let file I/O extend the window in
    // which the hook thread's keystroke path can be blocked.
    if (edit != nullptr) {
        if (writeMacrosFile(macroText)) {
            g_macroFileRaw = macroText;   // editor text is now the file content
        } else if (HWND hint = ::GetDlgItem(g.hSettings, IDC_STAT_MACRO_HINT)) {
            // Keep the in-memory macros (they still work this session) but say
            // out loud that they will NOT survive a restart.
            ::SetWindowTextW(hint,
                             L"⚠ Không ghi được %APPDATA%\\KieeKey\\macros.txt — gõ tắt chỉ "
                             L"hoạt động trong phiên này (kiểm tra quyền thư mục / ổ đĩa).");
        }
    }
}

void settingsToControls() {
    ::CheckRadioButton(g.hSettings, IDC_RADIO_TELEX, IDC_RADIO_SIMPLETELEX,
                       IDC_RADIO_TELEX + static_cast<int>(g.options.inputMethod));
    ::SendMessageW(::GetDlgItem(g.hSettings, IDC_COMBO_CODETABLE), CB_SETCURSEL,
                   static_cast<WPARAM>(g.options.codeTable), 0);
    ::CheckDlgButton(g.hSettings, IDC_CHK_MACRO,   g.options.useMacro ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_DIGITS,  g.options.digitsAreLiteral ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_SPELL,   g.options.checkSpelling ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_RESTORE, g.options.restoreIfWrongSpelling ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_UPPER,   g.options.upperCaseFirstChar ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_MODERN,  g.options.useModernOrthography ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_QUICK,   g.options.quickTelex ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_EXCLUDE_IDE,   g.exclIde ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_EXCLUDE_GAME,  g.exclGame ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_EXCLUDE_SHELL, g.exclShell ? BST_CHECKED : BST_UNCHECKED);
    const int outMode = g.outputMode.load(std::memory_order_relaxed);
    ::SendMessageW(::GetDlgItem(g.hSettings, IDC_COMBO_PERF), CB_SETCURSEL,
                   static_cast<WPARAM>(g.perfProfile.load(std::memory_order_relaxed)), 0);
    {
        const unsigned hyb = g.perfHybrid.load(std::memory_order_relaxed);
        ::CheckDlgButton(g.hSettings, IDC_CHK_PERF_LOWCPU, (hyb & ok::perf::kHybridLowCpu) ? BST_CHECKED : BST_UNCHECKED);
        ::CheckDlgButton(g.hSettings, IDC_CHK_PERF_DICT,   (hyb & ok::perf::kHybridExtraCorrect) ? BST_CHECKED : BST_UNCHECKED);
        ::CheckDlgButton(g.hSettings, IDC_CHK_NOTIFY, g.notify.sessionMuted() ? BST_UNCHECKED : BST_CHECKED);
    }
    ::CheckRadioButton(g.hSettings, IDC_RADIO_OUT_AUTO, IDC_RADIO_OUT_SEND,
                       IDC_RADIO_OUT_AUTO + outMode);
    // v1.1.1: load the macro definitions into the editor. v1.1.0-audit fix:
    // seed from the RAW file content when we have it (user comments survive);
    // the regenerated canonical text is only the fallback for a missing file.
    ::SetWindowTextW(::GetDlgItem(g.hSettings, IDC_EDIT_MACRO),
                     g_macroFileRaw.empty() ? macrosToText().c_str()
                                            : g_macroFileRaw.c_str());
    // v1.1.1: mirror the current on/off state into the always-visible toggle
    // button (the replacement for the removed Ctrl+Shift hotkey).
    ::SetWindowTextW(::GetDlgItem(g.hSettings, IDC_BTN_TOGGLE),
                     g.engineEnabled.load(std::memory_order_relaxed)
                         ? L"Bộ gõ: ĐANG BẬT — bấm để TẮT"
                         : L"Bộ gõ: ĐANG TẮT — bấm để BẬT");
    // The optional feature controls must reflect the live engine on reopen;
    // unchecked defaults previously silently overwrote enabled options.
    const auto chaos = ok::chaos::ChaosEngine::instance().getConfig();
    ::CheckDlgButton(g.hSettings, IDC_CHK_CHAOS_MASTER, chaos.masterEnabled ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_CHAOS_CASE, chaos.randomCaseEnabled ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_GLYPH_TRANSFORM, chaos.glyphTransformEnabled ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_AI_OPTIN,
                     ok::ai::AiRivalEngine::instance().isOptIn() ? BST_CHECKED : BST_UNCHECKED);
    const auto live = g.liveEffects.config();
    ::CheckDlgButton(g.hSettings, IDC_CHK_LIVE, live.enabled ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(g.hSettings, IDC_CHK_LIVE_CASE, live.randomCase ? BST_CHECKED : BST_UNCHECKED);
    ::SendMessageW(::GetDlgItem(g.hSettings, IDC_CMB_LIVE_GLYPH), CB_SETCURSEL,
                   static_cast<WPARAM>(live.glyph), 0);
    ::SendMessageW(::GetDlgItem(g.hSettings, IDC_CMB_LIVE_INTENSITY), CB_SETCURSEL,
                   live.intensity == 100 ? 3 : (live.intensity == 75 ? 2 :
                   (live.intensity == 25 ? 0 : 1)), 0);
    const auto arcade = ok::arcade::ArcadeManager::instance().getConfig();
    ::SendMessageW(::GetDlgItem(g.hSettings, IDC_CMB_FAILMODE), CB_SETCURSEL,
                   arcade.rhythmFailMode == ok::arcade::FailMode::HealthBar ? 1 : 0, 0);
    ::SetDlgItemInt(g.hSettings, IDC_EDT_RHYTHM_BPM, static_cast<UINT>(arcade.rhythmBpm), FALSE);
    // v1.3.0-beta3 (bug #2): reflect the saved passage language (VN is index 0).
    ::SendMessageW(::GetDlgItem(g.hSettings, IDC_CMB_PASSAGE_LANG), CB_SETCURSEL,
                   arcade.passageLanguage == ok::arcade::PassageLanguage::English ? 1 : 0, 0);
    // v1.3.0-beta5 (bug B7): reflect the steering-key choice.
    ::SendMessageW(::GetDlgItem(g.hSettings, IDC_CMB_STEERING), CB_SETCURSEL,
                   static_cast<WPARAM>(arcade.wasdSteering), 0);
    updateHeaderStatus();
    showTab(g_settingsOpenTab);
}

static constexpr int kTab0[] = {
    IDC_GRP_METHOD, IDC_RADIO_TELEX, IDC_RADIO_VNI, IDC_RADIO_SIMPLETELEX,
    IDC_STAT_METHOD_HINT,
    IDC_STAT_CODETABLE, IDC_COMBO_CODETABLE,
    IDC_GRP_OPTIONS,
    IDC_CHK_DIGITS, IDC_CHK_SPELL, IDC_CHK_RESTORE, IDC_CHK_QUICK,
    IDC_CHK_MODERN, IDC_CHK_UPPER, IDC_CHK_MACRO,
    IDC_GRP_OUTPUT,
    IDC_RADIO_OUT_AUTO, IDC_RADIO_OUT_TSF, IDC_RADIO_OUT_SEND,
    IDC_STAT_OUT_NOTE,
    IDC_STAT_PERF_LAB, IDC_COMBO_PERF, IDC_CHK_PERF_LOWCPU, IDC_CHK_PERF_DICT,
    IDC_STAT_PERF_NOTE,
    0
};
static constexpr int kTab1[] = {
    IDC_STAT_APPS_TITLE, IDC_STAT_APPS_NOTE, IDC_CHK_NOTIFY,
    IDC_CHK_EXCLUDE_IDE, IDC_CHK_EXCLUDE_GAME, IDC_CHK_EXCLUDE_SHELL, 0
};
// v1.1.0: tab 2 is the macro editor ("Gõ tắt"); diagnostics moved to 3.
static constexpr int kTab2[] = {
    IDC_STAT_MACRO_HINT, IDC_EDIT_MACRO, 0
};
static constexpr int kTab3[] = {
    IDC_STAT_LATLAB, IDC_STAT_AVGLAB, IDC_STAT_PUSHLAB, IDC_STAT_DROPLAB,
    IDC_STAT_WPMLAB, IDC_STAT_DESC,
    IDC_STAT_MOUSELAB, IDC_STAT_MOUSEV, IDC_STAT_FGLAB, IDC_STAT_FGV,
    IDC_STAT_RINGLAB, IDC_STAT_RINGV,
    IDC_STAT_LATVAL, IDC_STAT_AVGVAL, IDC_STAT_PUSHV, IDC_STAT_DROPV,
    IDC_STAT_WPMVAL,
    IDC_STAT_BARRIERLAB, IDC_STAT_BARRIERV,
    IDC_STAT_REINSTLAB, IDC_STAT_REINSTV,
    IDC_STAT_TSFLAB, IDC_STAT_TSFV,
    IDC_STAT_APPLAB, IDC_STAT_APPV,
    IDC_GRP_DIAG, IDC_RAD_DIAG_OFF, IDC_RAD_DIAG_BASIC, IDC_RAD_DIAG_FULL,
    IDC_BTN_DIAG_RUN, IDC_BTN_DIAG_REPORT, IDC_BTN_DIAG_COPY, IDC_STAT_DIAG_RESULT, 0
};
// v1.1.2: tab 4 — Information (introduces the app inside the app).
static constexpr int kTab4[] = {
    IDC_STAT_INFO_NAME, IDC_STAT_INFO_STATUS, IDC_STAT_INFO_ABOUT,
    IDC_STAT_INFO_FEAT, IDC_STAT_INFO_GUIDE, IDC_STAT_INFO_LICENSE,
    IDC_LNK_REPO, 0
};
// v1.3.0: tabs 5..8 (Arcade, Chaos, AI, Progression)
static constexpr int kTab5[] = {
    IDC_GRP_ARCADE, IDC_BTN_PLAY_SNAKE, IDC_BTN_PLAY_TETRIS, IDC_BTN_PLAY_FISHING,
    IDC_BTN_PLAY_TYPINGRACE, IDC_BTN_PLAY_WASDRACE, IDC_BTN_PLAY_RHYTHM,
    IDC_BTN_PLAY_NOMISTAKE, IDC_BTN_PLAY_FLEXING, IDC_STAT_ARCADE_STATUS,
    IDC_BTN_OPEN_CHAOS_LAB, IDC_CMB_FAILMODE, IDC_EDT_RHYTHM_BPM,
    IDC_BTN_APPLY_ARCADE_CFG, IDC_STAT_FAILMODE, IDC_STAT_RHYTHM_BPM,
    IDC_STAT_PASSAGE_LANG, IDC_CMB_PASSAGE_LANG,
    IDC_STAT_STEERING, IDC_CMB_STEERING, 0
};
static constexpr int kTab6[] = {
    IDC_GRP_CHAOS, IDC_CHK_CHAOS_MASTER, IDC_CHK_CHAOS_CASE,
    IDC_CHK_GLYPH_TRANSFORM, IDC_STAT_CHAOS_WARN,
    IDC_GRP_LIVE, IDC_CHK_LIVE, IDC_CHK_LIVE_CASE, IDC_CMB_LIVE_GLYPH,
    IDC_STAT_LIVE_GLYPH, IDC_CMB_LIVE_INTENSITY, IDC_STAT_LIVE_INTENSITY,
    IDC_STAT_LIVE_HINT, IDC_STAT_LIVE_GATE, 0
};
static constexpr int kTab7[] = {
    IDC_GRP_AI, IDC_CHK_AI_OPTIN, IDC_BTN_AI_RESET,
    IDC_STAT_AI_STATS, IDC_STAT_COACH_ADVICE, 0
};
static constexpr int kTab8[] = {
    IDC_GRP_PROG, IDC_STAT_LEVEL_VAL, IDC_STAT_XP_VAL,
    IDC_STAT_KEYS_VAL, IDC_STAT_ACHIEVEMENTS, IDC_BTN_PROG_RESET, 0
};

//===========================================================================
// v1.3.0-beta5 (bug B1) — settings dialog window refit + scroll fallback.
//
// The beta4 solver measured and reflowed the page children but applied the
// window growth ALL-OR-NOTHING (`if (growth.unsatisfiedPx == 0)`): whenever
// the monitor work area could not satisfy the full growth — the common case
// at 125-150 % scaling on laptop screens — NOTHING was applied while the
// page children had already moved to their solved rects. Result: labels
// overlapping the unmoved button row, content clipped at the old window
// bottom, and no way to reach it. The window also counted the always-visible
// button row as page content, so it "wanted" ~42 px of growth on every open,
// guaranteeing the unsatisfied branch on small screens.
//
// The fix keeps ok::layout pure (DialogLayout.hpp — pinned by
// tests/test_dialog_layout.cpp) and applies its WindowRefit decision here:
//   * grow OR shrink toward the solved content height, clamped to the work
//     area (partial growth is applied, never skipped);
//   * move the window fully on screen;
//   * whatever still does not fit becomes a WS_VSCROLL range — the page
//     scrolls (children move up and clip against the tab viewport) instead
//     of being lost;
//   * re-solve on WM_DPICHANGED after the child rescale, so a label that
//     wraps taller at the new scale grows instead of clipping.
//===========================================================================
namespace {

struct SettingsScrollState {
    // Page children + their SOLVED rects (dialog-client px), captured by
    // solveSettingsLayout(); scrolling re-positions from this baseline.
    std::vector<std::pair<HWND, ok::layout::Rect>> solved;
    ok::layout::Rect viewport{};          // tab display rect (client coords)
    int  perTabContentBottom[9] = {};     // deepest solved bottom per tab
    int  viewportBottom = 0;              // viewport.bottom after the refit
    int  offset = 0;                      // current scroll offset (px)
    int  range  = 0;                      // current tab's scroll range (px)
    bool enabled = false;                 // WS_VSCROLL currently on
};
SettingsScrollState g_settingsScroll;

void applySettingsScrollOffset(HWND hwnd);          // defined below showTab
void settingsScrollSetTab(HWND hwnd, int tabIndex); // defined below showTab

} // namespace

void showTab(int tab) {
    // Every control (including static labels) belongs to exactly one tab;
    // toggle visibility so only the active tab's controls are shown.
    // (v3.0 bugfix: labels previously had no IDs and were never hidden —
    //  all three tabs' labels were drawn overlapping each other.)
    // v1.1.2: the header (icon/title/status) and the bottom button row are
    // NOT tab-assigned — they are always visible chrome.

    if (!g.hSettings) { return; }
    for (const int* p = kTab0; *p; ++p) { ::ShowWindow(::GetDlgItem(g.hSettings, *p), tab == 0 ? SW_SHOW : SW_HIDE); }
    for (const int* p = kTab1; *p; ++p) { ::ShowWindow(::GetDlgItem(g.hSettings, *p), tab == 1 ? SW_SHOW : SW_HIDE); }
    for (const int* p = kTab2; *p; ++p) { ::ShowWindow(::GetDlgItem(g.hSettings, *p), tab == 2 ? SW_SHOW : SW_HIDE); }
    for (const int* p = kTab3; *p; ++p) { ::ShowWindow(::GetDlgItem(g.hSettings, *p), tab == 3 ? SW_SHOW : SW_HIDE); }
    for (const int* p = kTab4; *p; ++p) { ::ShowWindow(::GetDlgItem(g.hSettings, *p), tab == 4 ? SW_SHOW : SW_HIDE); }
    for (const int* p = kTab5; *p; ++p) { ::ShowWindow(::GetDlgItem(g.hSettings, *p), tab == 5 ? SW_SHOW : SW_HIDE); }
    for (const int* p = kTab6; *p; ++p) { ::ShowWindow(::GetDlgItem(g.hSettings, *p), tab == 6 ? SW_SHOW : SW_HIDE); }
    for (const int* p = kTab7; *p; ++p) { ::ShowWindow(::GetDlgItem(g.hSettings, *p), tab == 7 ? SW_SHOW : SW_HIDE); }
    for (const int* p = kTab8; *p; ++p) { ::ShowWindow(::GetDlgItem(g.hSettings, *p), tab == 8 ? SW_SHOW : SW_HIDE); }
    // v1.3.0-beta5 (bug B1): each tab has its own content depth — recompute
    // the scroll range and jump back to the top (no-op before the first
    // solve, when WM_CREATE's settingsToControls() runs showTab early).
    settingsScrollSetTab(g.hSettings, tab);
}

//===========================================================================
// v1.3.0-beta5 (bug B1) — the solve/apply machinery (declared above).
//===========================================================================
namespace {

// Re-position + clip every solved page child for the current scroll offset.
// A child fully outside the viewport gets an EMPTY region (hidden without
// touching SW_SHOW/SW_HIDE, so showTab()'s visibility contract survives);
// a child straddling an edge is clipped to it; an unclipped child has its
// region cleared. Always-visible chrome and the tab control are untouched.
void applySettingsScrollOffset(HWND hwnd) {
    if (hwnd == nullptr || g_settingsScroll.solved.empty()) { return; }
    for (const auto& entry : g_settingsScroll.solved) {
        const ok::layout::ScrolledChild sc = ok::layout::scrollChildRect(
            entry.second, g_settingsScroll.offset, g_settingsScroll.viewport);
        ::SetWindowPos(entry.first, nullptr, sc.rect.x, sc.rect.y,
                       sc.rect.w, sc.rect.h, SWP_NOZORDER | SWP_NOACTIVATE);
        if (!sc.visible) {
            if (HRGN rgn = ::CreateRectRgn(0, 0, 0, 0)) {
                ::SetWindowRgn(entry.first, rgn, TRUE);   // rgn ownership passes
            }
        } else if (sc.clipped) {
            if (HRGN rgn = ::CreateRectRgn(sc.clip.x, sc.clip.y,
                                           sc.clip.x + sc.clip.w,
                                           sc.clip.y + sc.clip.h)) {
                ::SetWindowRgn(entry.first, rgn, TRUE);
            }
        } else {
            ::SetWindowRgn(entry.first, nullptr, TRUE);
        }
    }
}

// Per-tab scroll range from the stored content depths; resets to the top.
void settingsScrollSetTab(HWND hwnd, int tabIndex) {
    if (hwnd == nullptr || g_settingsScroll.solved.empty()) { return; }
    if (tabIndex < 0 || tabIndex > 8) { tabIndex = 0; }
    g_settingsScroll.range = std::max(
        0, g_settingsScroll.perTabContentBottom[tabIndex] -
               g_settingsScroll.viewportBottom);
    g_settingsScroll.offset = 0;
    const ok::layout::ScrollMetrics m = ok::layout::scrollMetrics(
        g_settingsScroll.viewport.h, g_settingsScroll.range);
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(0, m.rangeMax - 1);   // Win32 range is inclusive
    si.nPage = static_cast<UINT>(m.pagePx);
    si.nPos = 0;
    ::SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    applySettingsScrollOffset(hwnd);
}

// Solve the dialog layout for the CURRENT state and apply the refit
// decision atomically: page children to their solved rects, the window
// grown/shrunk/moved per ok::layout::refitWindow, tab + bottom chrome
// resized/shifted by the same client delta, and the residual exposed as a
// scroll range. Runs at WM_CREATE and after every WM_DPICHANGED rescale;
// idempotent (re-solving an already-solved dialog changes nothing).
void solveSettingsLayout(HWND hwnd) {
    if (hwnd == nullptr) { return; }
    HWND tabCtl = ::GetDlgItem(hwnd, IDC_TAB);
    if (tabCtl == nullptr) { return; }

    const int dpi = static_cast<int>(g_settingsDpi != 0 ? g_settingsDpi : 96);
    const auto S = [dpi](int px) { return ::MulDiv(px, dpi, 96); };

    // -- 1a. Tab headers: measure + plan; multi-row if needed. The label
    // texts come FROM the control (TCM_GETITEMW) so this can never drift
    // from what WM_CREATE inserted.
    RECT rcTab{};
    ::GetWindowRect(tabCtl, &rcTab);
    ::MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&rcTab), 2);
    const int tabWidth = rcTab.right - rcTab.left;
    HDC tdc = ::GetDC(tabCtl);
    if (tdc != nullptr) {
        HGDIOBJ oldFont = ::SelectObject(tdc, uiFont());
        std::vector<int> labelW(9, 0);
        wchar_t buf[64];
        for (int i = 0; i < 9; ++i) {
            TCITEMW ti{};
            ti.mask = TCIF_TEXT;
            ti.pszText = buf;
            ti.cchTextMax = 64;
            buf[0] = L'\0';
            if (::SendMessageW(tabCtl, TCM_GETITEMW, static_cast<WPARAM>(i),
                               reinterpret_cast<LPARAM>(&ti)) != 0) {
                RECT r{0, 0, 0, 0};
                ::DrawTextW(tdc, buf, -1, &r, DT_CALCRECT | DT_SINGLELINE);
                labelW[static_cast<std::size_t>(i)] = r.right - r.left;
            }
        }
        ::SelectObject(tdc, oldFont);
        ::ReleaseDC(tabCtl, tdc);
        const ok::layout::TabPlan tabPlan = ok::layout::planTabs(
            labelW, labelW, tabWidth - S(16), S(18), S(22), S(6));
        const LONG_PTR tabStyle = ::GetWindowLongPtrW(tabCtl, GWL_STYLE);
        if (tabPlan.multiline && (tabStyle & TCS_MULTILINE) == 0) {
            ::SetWindowLongPtrW(tabCtl, GWL_STYLE, tabStyle | TCS_MULTILINE);
        }
    }

    // -- 1b. Pages: measure every label, autoFit. --
    const int* pages[9] = {kTab0, kTab1, kTab2, kTab3,
                           kTab4, kTab5, kTab6, kTab7, kTab8};
    auto pageOf = [&pages](int id) {
        for (int t = 0; t < 9; ++t) {
            for (const int* p = pages[t]; *p; ++p) {
                if (*p == id) { return t; }
            }
        }
        return ok::layout::ControlSpec::kAlwaysVisible;
    };
    RECT disp = rcTab;
    ::SendMessageW(tabCtl, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&disp));

    std::vector<ok::layout::ControlSpec> specs;
    specs.reserve(140);
    std::vector<HWND> hwnds;
    hwnds.reserve(140);
    wchar_t cls[32];
    wchar_t text[512];
    for (HWND c = ::GetWindow(hwnd, GW_CHILD); c != nullptr;
         c = ::GetWindow(c, GW_HWNDNEXT)) {
        if (c == tabCtl) { continue; }
        const int id = ::GetDlgCtrlID(c);
        if (id == 0) { continue; }
        RECT rc{};
        ::GetWindowRect(c, &rc);
        ::MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&rc), 2);
        ok::layout::ControlSpec spec;
        spec.id = id;
        spec.rect = {rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top};
        spec.tab = pageOf(id);
        const int clsLen = ::GetClassNameW(c, cls, 32);
        const bool isStatic = (clsLen == 6 && wcscmp(cls, L"STATIC") == 0);
        const LONG_PTR style = ::GetWindowLongPtrW(c, GWL_STYLE);
        const bool isGroupBox =
            (clsLen == 6 && wcscmp(cls, L"BUTTON") == 0) &&
            ((style & BS_GROUPBOX) == BS_GROUPBOX);
        spec.groupBox = isGroupBox;
        spec.growable = isStatic && !isGroupBox &&
                        ((style & SS_TYPEMASK) != SS_ICON) &&
                        ((style & SS_TYPEMASK) != SS_OWNERDRAW);
        if (spec.growable) {
            const int tLen = ::GetWindowTextW(c, text, 512);
            if (tLen > 0) {
                HDC dc = ::GetDC(hwnd);
                if (dc != nullptr) {
                    HGDIOBJ of = ::SelectObject(dc,
                        reinterpret_cast<HGDIOBJ>(::SendMessageW(
                            c, WM_GETFONT, 0, 0)));
                    RECT calc{0, 0, spec.rect.w, 0};
                    ::DrawTextW(dc, text, tLen, &calc,
                                DT_CALCRECT | DT_WORDBREAK);
                    ::SelectObject(dc, of);
                    ::ReleaseDC(hwnd, dc);
                    spec.requiredHeight = calc.bottom - calc.top;
                }
            }
        }
        specs.push_back(spec);
        hwnds.push_back(c);
    }
    const ok::layout::LayoutPlan plan = ok::layout::autoFit(
        specs, disp.top, disp.bottom);

    // Per-tab content depths (page controls only — the always-visible button
    // row lives below the viewport by design and must never drive growth).
    for (int t = 0; t < 9; ++t) { g_settingsScroll.perTabContentBottom[t] = disp.top; }
    for (std::size_t i = 0; i < specs.size(); ++i) {
        if (specs[i].tab == ok::layout::ControlSpec::kAlwaysVisible) { continue; }
        int& depth = g_settingsScroll.perTabContentBottom[specs[i].tab];
        depth = std::max(depth, plan.rects[i].bottom());
    }
    int deepest = disp.bottom;
    for (int t = 0; t < 9; ++t) {
        deepest = std::max(deepest, g_settingsScroll.perTabContentBottom[t]);
    }

    // -- 2. Window refit decision (pure model; work area of OUR monitor). --
    RECT rcDlg{};
    ::GetWindowRect(hwnd, &rcDlg);
    RECT rcCli{};
    ::GetClientRect(hwnd, &rcCli);
    RECT rcWork{};
    if (!::SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0)) {
        rcWork = rcDlg;   // never leave the model without a bound
    }
    const HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (mon != nullptr) {
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (::GetMonitorInfoW(mon, &mi)) { rcWork = mi.rcWork; }
    }
    const ok::layout::WindowRefit fit = ok::layout::refitWindow(
        ok::layout::Rect{rcDlg.left, rcDlg.top,
                         rcDlg.right - rcDlg.left, rcDlg.bottom - rcDlg.top},
        rcCli.bottom - rcCli.top, disp.bottom, deepest,
        ok::layout::Rect{rcWork.left, rcWork.top,
                         rcWork.right - rcWork.left, rcWork.bottom - rcWork.top});

    // Any tab overflowing after the refit => keep WS_VSCROLL available.
    const int newViewportBottom = disp.bottom + fit.clientDelta;
    bool anyScroll = false;
    for (int t = 0; t < 9; ++t) {
        if (g_settingsScroll.perTabContentBottom[t] > newViewportBottom) {
            anyScroll = true;
        }
    }
    const LONG_PTR dlgStyle = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
    const bool haveScroll = (dlgStyle & WS_VSCROLL) != 0;
    if (anyScroll != haveScroll) {
        ::SetWindowLongPtrW(hwnd, GWL_STYLE,
                            anyScroll ? (dlgStyle | WS_VSCROLL)
                                      : (dlgStyle & ~static_cast<LONG_PTR>(WS_VSCROLL)));
    }
    g_settingsScroll.enabled = anyScroll;

    // -- 3. Apply: window rect, then tab + bottom chrome by clientDelta. --
    ::SetWindowPos(hwnd, nullptr, fit.windowRect.x, fit.windowRect.y,
                   fit.windowRect.w, fit.windowRect.h,
                   SWP_NOZORDER | SWP_NOACTIVATE |
                       (anyScroll != haveScroll ? SWP_FRAMECHANGED : 0));
    if (anyScroll != haveScroll) {
        // The scrollbar changed the client width — re-read it.
        ::GetClientRect(hwnd, &rcCli);
    }
    {
        const int vsw = anyScroll ? ::GetSystemMetrics(SM_CXVSCROLL) : 0;
        const int newTabW = std::max(S(200),
            static_cast<int>(rcCli.right - rcCli.left) - S(24) - vsw);
        ::SetWindowPos(tabCtl, nullptr, S(12), S(66), newTabW,
                       (rcTab.bottom - rcTab.top) + fit.clientDelta,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (fit.clientDelta != 0) {
        for (HWND c = ::GetWindow(hwnd, GW_CHILD); c != nullptr;
             c = ::GetWindow(c, GW_HWNDNEXT)) {
            if (c == tabCtl) { continue; }
            if (pageOf(::GetDlgCtrlID(c)) != ok::layout::ControlSpec::kAlwaysVisible) {
                continue;   // page content keeps its solved rect (applied below)
            }
            RECT rc{};
            ::GetWindowRect(c, &rc);
            ::MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&rc), 2);
            if (rc.top >= rcTab.bottom - S(4)) {   // the bottom button row
                ::SetWindowPos(c, nullptr, 0, rc.top + fit.clientDelta,
                               0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
    }

    // -- 4. Store the solved baseline + viewport; scroll to the top. --
    RECT disp2{};
    ::GetWindowRect(tabCtl, &disp2);
    ::MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&disp2), 2);
    ::SendMessageW(tabCtl, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&disp2));
    g_settingsScroll.viewport = ok::layout::Rect{
        disp2.left, disp2.top, disp2.right - disp2.left, disp2.bottom - disp2.top};
    g_settingsScroll.viewportBottom = g_settingsScroll.viewport.bottom();
    g_settingsScroll.solved.clear();
    for (std::size_t i = 0; i < specs.size(); ++i) {
        if (specs[i].tab == ok::layout::ControlSpec::kAlwaysVisible) { continue; }
        g_settingsScroll.solved.emplace_back(hwnds[i], plan.rects[i]);
    }
    int curTab = static_cast<int>(::SendMessageW(tabCtl, TCM_GETCURSEL, 0, 0));
    if (curTab < 0 || curTab > 8) { curTab = 0; }
    settingsScrollSetTab(hwnd, curTab);   // applies solved rects at offset 0
}

} // namespace

//===========================================================================
// v1.3.0-beta4 — Diagnostics control panel (tab 3 "Chẩn đoán")
//
// ok::diag shipped in beta3 fully implemented and unit-tested but with ZERO
// call sites: no UI, no recorder, nothing to look at. This section is the
// missing half — level control (persisted), a REAL quick self-check, and the
// report export — plus the recorder wiring on the hook path further below.
//===========================================================================

// %APPDATA%\KieeKey (the macros.txt directory; created on demand).
std::wstring diagAppDataDir() {
    wchar_t* appData = nullptr;
    std::wstring dir;
    if (S_OK != ::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)) {
        return L"";
    }
    dir = std::wstring(appData) + L"\\KieeKey";
    ::CoTaskMemFree(appData);
    ::CreateDirectoryW(dir.c_str(), nullptr);   // ok if it already exists
    return dir;
}

std::wstring diagLevelFilePath() {
    const std::wstring dir = diagAppDataDir();
    return dir.empty() ? std::wstring() : dir + L"\\diag-level.txt";
}

// The level survives restarts via a one-line file ("0"/"1"/"2"). Kept out of
// the main options matrix on purpose: the option-matrix tests pin the exact
// option set, and a diagnostics knob is operational, not a typing behaviour.
ok::diag::Level loadDiagLevel() {
    const std::wstring path = diagLevelFilePath();
    if (path.empty()) { return ok::diag::Level::Basic; }
    std::ifstream f(path.c_str(), std::ios::binary);
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!raw.empty() && raw[0] != '0' && raw[0] != '1' && raw[0] != '2') {
        return ok::diag::Level::Basic;
    }
    return raw.empty() ? ok::diag::Level::Basic : ok::diag::levelFromInt(raw[0] - '0');
}

void saveDiagLevel(ok::diag::Level level) {
    const std::wstring path = diagLevelFilePath();
    if (path.empty()) { return; }
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    if (out) { out << static_cast<int>(level); }
}

// The quick self-check exercises the SHIPPED pipeline pieces for real (a local
// TextEngine round-trip, the live counters/histograms, verdict + report). It
// is the "Chạy kiểm tra nhanh" button: a pass means the machinery answers, not
// that the machine is fast.
int runDiagQuickCheck(std::string& failDetail) {
    int passed = 0;
    const int total = 6;
    failDetail.clear();

    // 1. Engine composition round-trip on the default configuration.
    try {
        TextEngine engine;
        ok::text::TextInput in{};
        in.kind = ok::text::InputKind::Char;
        const ok::text::EngineResult* last = nullptr;
        for (const char c : std::string("booj")) {
            in.ch = static_cast<char32_t>(c);
            last = &engine.process(in);
        }
        std::wstring rep;
        engine.replacementUtf16(*last, rep);
        if (rep == L"bộ") { ++passed; } else { failDetail += "engine;"; }
    } catch (...) { failDetail += "engine-throw;"; }

    // 2. Engine backspace at a word boundary must be a clean no-op (the exact
    //    contract the arcade backspace fix depends on).
    try {
        TextEngine engine;
        ok::text::TextInput in{};
        in.kind = ok::text::InputKind::Backspace;
        const ok::text::EngineResult& r = engine.process(in);
        if (!r.consumed()) { ++passed; } else { failDetail += "backspace;"; }
    } catch (...) { failDetail += "backspace-throw;"; }

    // 3. Live counters answer (they were being recorded all along).
    try {
        const std::uint64_t n = ok::diag::Diagnostics::instance().keyboardEvents();
        (void)n;   // any value is a pass; the API must simply not misbehave
        ++passed;
    } catch (...) { failDetail += "counters;"; }

    // 4. Latency histograms answer with sane bucket totals.
    try {
        const std::uint64_t n = ok::diag::Diagnostics::instance()
                                    .histogram(ok::diag::Stage::HookToDecision)
                                    .total();
        (void)n;
        ++passed;
    } catch (...) { failDetail += "histogram;"; }

    // 5. The health verdict + full report generate.
    try {
        const std::string v = ok::diag::Diagnostics::instance().verdict();
        const std::string rep = ok::diag::Diagnostics::instance().report(8);
        if (!v.empty() && rep.size() > 100) { ++passed; } else { failDetail += "report;"; }
    } catch (...) { failDetail += "report-throw;"; }

    // 6. v1.3.0-beta5 (bug B2): the live-effects DECISION chain answers —
    //    the same planOutput() the hook runs, exercised in-process with the
    //    gate held open (active=true): a styled replacement must come back.
    //    (tests/test_live_effects_chain.cpp pins the transport behind it.)
    try {
        ok::effects::LiveEffects fx;
        ok::effects::Config cfg;
        cfg.enabled = true;
        cfg.randomCase = true;
        cfg.glyph = ok::effects::Glyph::None;
        cfg.intensity = 100;
        fx.configure(cfg);
        fx.sync();
        std::wstring text = L"aaaa";
        const ok::effects::OutputPlan plan = ok::effects::planOutput(
            /*active=*/true, /*engineSuppress=*/true, /*engineBackspace=*/0,
            text, ok::effects::KeyKind::Char, U'a', fx);
        if (plan.suppress && text.size() == 4 && text != L"aaaa") { ++passed; }
        else { failDetail += "livefx;"; }
    } catch (...) { failDetail += "livefx-throw;"; }

    (void)total;
    return passed;
}

// "Xuất báo cáo": write ok::diag::report() next to macros.txt and hand the
// path back for the status line. UTF-8 with BOM so Notepad opens it correctly.
bool exportDiagReport(std::wstring& outPath) {
    const std::wstring dir = diagAppDataDir();
    if (dir.empty()) { return false; }
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    wchar_t name[64]{};
    swprintf_s(name, L"\\diag-report-%04u%02u%02u-%02u%02u%02u.txt",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    outPath = dir + name;
    std::ofstream out(outPath.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) { return false; }
    // v1.3.0-beta7: full snapshot + counter sync at export time — the beta6
    // report showed 0s/96 DPI because only B1/B4 evidence was refreshed here.
    refreshDiagnostics();
    out << "\xEF\xBB\xBF";
    out << ok::diag::Diagnostics::instance().report(40);
    // v1.3.0-beta5 (bug B2): the live-effects gate verdict belongs in the
    // exported report — a tester mailing this file answers "why do I see no
    // effects?" without another round trip.
    out << "\n[live-effects gate] " << utf16ToUtf8(liveGateStatusText()) << "\n";
    return true;
}

void setDiagLevelAndSave(ok::diag::Level level) {
    ok::diag::Diagnostics::instance().setLevel(level);
    saveDiagLevel(level);
}

LRESULT CALLBACK settingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // v1.1.3 exception safety: the body allocates heavily (macro
            // text parse, control setup, fonts). A bad_alloc unwinding
            // through CreateWindowExW's C frames is UB and left
            // g.hSettings dangling at a never-created HWND. Fail the
            // creation cleanly: catch, return -1, publish only on success.
            try {
            g.hSettings = hwnd;
            // v1.1.0 — DPI-relative layout. All coordinates below are
            // 96-dpi logical pixels; S() scales them to the actual monitor.
            g_settingsDpi = windowDpi(hwnd);
            const auto S = [](int px) {
                return ::MulDiv(px, static_cast<int>(g_settingsDpi), 96);
            };
            // Resize the frame so the client area matches the scaled layout
            // (the fixed creation size below is only a placeholder).
            {
                RECT rc{0, 0, S(560), S(622)};
                ::AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                        WS_MINIMIZEBOX, FALSE);
                ::SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left,
                               rc.bottom - rc.top,
                               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }

            // ---- v1.1.2 header: icon + name + live status (always visible) ----
            {
                HWND icon = mkCtl(hwnd, L"STATIC", L"",
                                  WS_CHILD | WS_VISIBLE | SS_ICON,
                                  S(14), S(10), S(34), S(34),
                                  reinterpret_cast<HMENU>(IDC_STAT_HEAD_ICON));
                if (g.hIconOn) {
                    ::SendMessageW(icon, STM_SETIMAGE, IMAGE_ICON,
                                   reinterpret_cast<LPARAM>(g.hIconOn));
                }
            }
            {
                HWND t = mkCtl(hwnd, L"STATIC", kAppTitle,
                               WS_CHILD | WS_VISIBLE, S(58), S(12), S(360), S(28),
                               reinterpret_cast<HMENU>(IDC_STAT_HEAD_TITLE));
                ::SendMessageW(t, WM_SETFONT, reinterpret_cast<WPARAM>(uiFontTitle()), TRUE);
            }
            mkCtl(hwnd, L"STATIC", L"",
                  WS_CHILD | WS_VISIBLE, S(58), S(42), S(490), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_HEAD_STATUS));

            // Tab control (5 tabs — v1.1.2 adds “Thông tin”)
            HWND tab = mkCtl(hwnd, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                             S(12), S(66), S(536), S(506), reinterpret_cast<HMENU>(IDC_TAB));
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            wchar_t t0[] = L"Bàn phím";
            wchar_t t1[] = L"Ứng dụng";
            wchar_t t2[] = L"Gõ tắt";
            wchar_t t3[] = L"Chẩn đoán";
            wchar_t t4[] = L"Thông tin";
            wchar_t t5[] = L"Arcade";
            // v1.3.0-beta4: the last three headers were clipped mid-word
            // (9 labels need ~598px, the control offers 528px). Shortened;
            // the runtime planTabs() below can still go multi-row at high DPI.
            wchar_t t6[] = L"Chaos";
            wchar_t t7[] = L"AI";
            wchar_t t8[] = L"Cấp độ";
            item.pszText = t0; ::SendMessageW(tab, TCM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));
            item.pszText = t1; ::SendMessageW(tab, TCM_INSERTITEMW, 1, reinterpret_cast<LPARAM>(&item));
            item.pszText = t2; ::SendMessageW(tab, TCM_INSERTITEMW, 2, reinterpret_cast<LPARAM>(&item));
            item.pszText = t3; ::SendMessageW(tab, TCM_INSERTITEMW, 3, reinterpret_cast<LPARAM>(&item));
            item.pszText = t4; ::SendMessageW(tab, TCM_INSERTITEMW, 4, reinterpret_cast<LPARAM>(&item));
            item.pszText = t5; ::SendMessageW(tab, TCM_INSERTITEMW, 5, reinterpret_cast<LPARAM>(&item));
            item.pszText = t6; ::SendMessageW(tab, TCM_INSERTITEMW, 6, reinterpret_cast<LPARAM>(&item));
            item.pszText = t7; ::SendMessageW(tab, TCM_INSERTITEMW, 7, reinterpret_cast<LPARAM>(&item));
            item.pszText = t8; ::SendMessageW(tab, TCM_INSERTITEMW, 8, reinterpret_cast<LPARAM>(&item));

            // ---- tab 0: Bàn phím (v1.1.2: grouped layout + digits option) ----
            mkCtl(hwnd, L"BUTTON", L"Phương thức gõ",
                  WS_CHILD | WS_VISIBLE | BS_GROUPBOX, S(24), S(100), S(494), S(84),
                  reinterpret_cast<HMENU>(IDC_GRP_METHOD));
            mkCtl(hwnd, L"BUTTON", L"Telex", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  S(44), S(122), S(74), S(20), reinterpret_cast<HMENU>(IDC_RADIO_TELEX));
            mkCtl(hwnd, L"BUTTON", L"VNI", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  S(128), S(122), S(58), S(20), reinterpret_cast<HMENU>(IDC_RADIO_VNI));
            mkCtl(hwnd, L"BUTTON", L"Simple Telex", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  S(196), S(122), S(112), S(20), reinterpret_cast<HMENU>(IDC_RADIO_SIMPLETELEX));
            mkCtl(hwnd, L"STATIC",
                  L"Telex & Simple Telex gõ dấu bằng chữ (as → á). VNI gõ dấu bằng số "
                  L"(a1 → á) — chỉ khi tùy chọn chữ số bên dưới đang TẮT.",
                  WS_CHILD | WS_VISIBLE, S(44), S(146), S(460), S(34),
                  reinterpret_cast<HMENU>(IDC_STAT_METHOD_HINT));
            mkCtl(hwnd, L"STATIC", L"Bảng mã:", WS_CHILD | WS_VISIBLE, S(28), S(190), S(90), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_CODETABLE));
            HWND combo = mkCtl(hwnd, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                               CBS_DROPDOWNLIST, S(128), S(186), S(210), S(200), reinterpret_cast<HMENU>(IDC_COMBO_CODETABLE));
            for (const wchar_t* s : {L"Unicode", L"TCVN3 (ABC)", L"VNI Windows",
                                     L"Unicode tổ hợp", L"CP 1258"}) {
                ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
            }
            mkCtl(hwnd, L"BUTTON", L"Tùy chọn gõ",
                  WS_CHILD | WS_VISIBLE | BS_GROUPBOX, S(24), S(214), S(494), S(168),
                  reinterpret_cast<HMENU>(IDC_GRP_OPTIONS));
            const wchar_t* kOpts[] = {
                L"Số 0–9 luôn là chữ số — không dùng số để gõ dấu tiếng Việt (VNI)",
                L"Kiểm tra chính tả",
                L"Tự sửa từ gõ sai — khôi phục phím gốc khi từ vô nghĩa",
                L"Telex nhanh (cc→ch, gg→gi, kk→kh…)",
                L"Chính tả mới (oà / uý thay vì òa / úy)",
                L"Viết hoa đầu câu",
                L"Gõ tắt (macro) — quản lý từ gọn trong tab Gõ tắt",
            };
            const int kIds[] = {IDC_CHK_DIGITS, IDC_CHK_SPELL, IDC_CHK_RESTORE,
                                IDC_CHK_QUICK, IDC_CHK_MODERN, IDC_CHK_UPPER,
                                IDC_CHK_MACRO};
            for (int i = 0; i < 7; ++i) {
                // INT_PTR round-trip: reinterpret_cast<HMENU>(int) directly is
                // a 32→64-bit pointer widening that MSVC flags as C4312 under
                // /W4 /WX (HMENU is pointer-sized). Casting through INT_PTR is
                // the canonical control-id idiom and is warning-free.
                mkCtl(hwnd, L"BUTTON", kOpts[i], WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                      S(44), S(218) + i * S(22), S(460), S(20),
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIds[i])));
            }

            // Chế độ xuất — v1.1.2: grouped, unchanged semantics.
            mkCtl(hwnd, L"BUTTON", L"Chế độ xuất & hiệu năng",
                  WS_CHILD | WS_VISIBLE | BS_GROUPBOX, S(24), S(388), S(494), S(178),
                  reinterpret_cast<HMENU>(IDC_GRP_OUTPUT));
            mkCtl(hwnd, L"BUTTON", L"Auto", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  S(44), S(410), S(66), S(20), reinterpret_cast<HMENU>(IDC_RADIO_OUT_AUTO));
            mkCtl(hwnd, L"BUTTON", L"Luôn TSF (chống nháy)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  S(118), S(410), S(180), S(20), reinterpret_cast<HMENU>(IDC_RADIO_OUT_TSF));
            mkCtl(hwnd, L"BUTTON", L"Luôn SendInput (nhanh nhất)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  S(44), S(432), S(240), S(20), reinterpret_cast<HMENU>(IDC_RADIO_OUT_SEND));
            mkCtl(hwnd, L"STATIC",
                  L"Auto: TSF cho trình duyệt & Office (không nháy chữ), SendInput trực tiếp "
                  L"cho các ứng dụng khác. Khuyến nghị: giữ Auto và chọn hồ sơ hiệu năng bên dưới.",
                  WS_CHILD | WS_VISIBLE, S(44), S(454), S(460), S(34),
                  reinterpret_cast<HMENU>(IDC_STAT_OUT_NOTE));
            // v1.2.1 RC2 — Performance preference profile (inside the output group).
            mkCtl(hwnd, L"STATIC", L"Hồ sơ hiệu năng:", WS_CHILD | WS_VISIBLE,
                  S(44), S(492), S(110), S(18), reinterpret_cast<HMENU>(IDC_STAT_PERF_LAB));
            HWND perfCombo = mkCtl(hwnd, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                   CBS_DROPDOWNLIST, S(158), S(488), S(180), S(160),
                                   reinterpret_cast<HMENU>(IDC_COMBO_PERF));
            for (const wchar_t* s : {L"Cân bằng (mặc định)", L"Nhanh nhất", L"Ít nháy chữ nhất",
                                     L"Chính xác tối đa", L"Tự động thích ứng"}) {
                ::SendMessageW(perfCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
            }
            mkCtl(hwnd, L"BUTTON", L"Tiết kiệm CPU", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                  S(346), S(489), S(80), S(20), reinterpret_cast<HMENU>(IDC_CHK_PERF_LOWCPU));
            mkCtl(hwnd, L"BUTTON", L"Từ điển", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                  S(430), S(489), S(80), S(20), reinterpret_cast<HMENU>(IDC_CHK_PERF_DICT));
            mkCtl(hwnd, L"STATIC",
                  L"Nhanh nhất: SendInput, xử lý nóng. Ít nháy: TSF gộp lệnh. Chính xác: mọi lưới an toàn. "
                  L"Tự động: điều chỉnh theo máy. Có thể kết hợp thêm hai ô bên phải.",
                  WS_CHILD | WS_VISIBLE, S(44), S(516), S(460), S(34),
                  reinterpret_cast<HMENU>(IDC_STAT_PERF_NOTE));
            // (v1.3.0-beta4: IDC_CHK_NOTIFY moved to tab 1 — the output
            // group's notes grew to their real wrapped height and the page
            // had no room left below them; the toggle is app behaviour.)

            // ---- tab 1: Ứng dụng ----
            mkCtl(hwnd, L"STATIC", L"Tự động tắt bộ gõ khi cửa sổ đang chạy là:",
                  WS_CHILD | WS_VISIBLE, S(28), S(110), S(470), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_APPS_TITLE));
            mkCtl(hwnd, L"BUTTON", L"IDE / Editor (VS Code, Visual Studio, CLion…)",
                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, S(28), S(140), S(480), S(20),
                  reinterpret_cast<HMENU>(IDC_CHK_EXCLUDE_IDE));
            mkCtl(hwnd, L"BUTTON", L"Trò chơi toàn màn hình (DirectX / Vulkan / OpenGL)",
                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, S(28), S(166), S(480), S(20),
                  reinterpret_cast<HMENU>(IDC_CHK_EXCLUDE_GAME));
            mkCtl(hwnd, L"BUTTON", L"Windows Shell (Explorer, Terminal, CMD)",
                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, S(28), S(192), S(480), S(20),
                  reinterpret_cast<HMENU>(IDC_CHK_EXCLUDE_SHELL));
            mkCtl(hwnd, L"STATIC",
                  L"Lưu ý: tắt loại trừ Shell để gõ tên file tiếng Việt trong "
                  L"Explorer. Việc phát hiện cửa sổ là theo sự kiện (WinEvent), "
                  L"không tốn CPU khi rảnh. Khi bộ gõ tự tắt, trạng thái hiện ngay "
                  L"trên dòng đầu cửa sổ và tooltip khay hệ thống.",
                  WS_CHILD | WS_VISIBLE, S(28), S(224), S(480), S(64),
                  reinterpret_cast<HMENU>(IDC_STAT_APPS_NOTE));
            // v1.3.0-beta4: moved from tab 0 (see the output group) — this
            // page has the room and the toggle is application behaviour.
            mkCtl(hwnd, L"BUTTON", L"Thông báo thông minh (gợi ý khi Telex nhanh sửa nhầm, TSF chậm…)",
                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, S(28), S(320), S(480), S(20),
                  reinterpret_cast<HMENU>(IDC_CHK_NOTIFY));

            // ---- tab 2: Gõ tắt (v1.1.0 — the macro feature is now real) ----
            mkCtl(hwnd, L"STATIC",
                  L"Mỗi dòng một từ gọn:  từgọn=kết quả   (VD: cn=chào, hcm=Hồ Chí Minh). "
                  L"Dòng # là ghi chú. Gõ từ gọn rồi nhấn Space để mở rộng.",
                  WS_CHILD | WS_VISIBLE, S(28), S(110), S(480), S(40),
                  reinterpret_cast<HMENU>(IDC_STAT_MACRO_HINT));
            mkCtl(hwnd, L"EDIT", L"",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_BORDER |
                  ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                  S(28), S(156), S(480), S(396),
                  reinterpret_cast<HMENU>(IDC_EDIT_MACRO));
            // Publish the macro editor HWND for the hook thread's bypass
            // exemption (bug #4). Cleared wherever the dialog is torn down.
            g.macroEdit.store(::GetDlgItem(hwnd, IDC_EDIT_MACRO),
                              std::memory_order_release);

            // ---- tab 3: Chẩn đoán ----
            // v1.3.0-beta5 (bug B3+B4): ONE coherent telemetry panel, one row
            // per SOURCE. The beta4 row "Sự kiện bàn phím đã xử lý" was fed by
            // pushed() — the SPSC ring counter that keyboard events AND mouse
            // button/wheel events AND foreground changes ALL increment — so it
            // climbed while the user merely dragged the mouse. Each number now
            // says exactly what it counts (HookCounters.hpp is the owner);
            // failure counters carry an explicit "0 = tốt" annotation so a
            // healthy zero is not mistaken for a broken/never-run diagnostic.
            mkCtl(hwnd, L"STATIC", L"Độ trễ đỉnh hook → xử lý (µs):", WS_CHILD | WS_VISIBLE,
                  S(28), S(110), S(250), S(18), reinterpret_cast<HMENU>(IDC_STAT_LATLAB));
            mkCtl(hwnd, L"STATIC", L"—", WS_CHILD | WS_VISIBLE, S(290), S(110), S(210), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_LATVAL));
            mkCtl(hwnd, L"STATIC", L"Độ trễ trung bình (µs):", WS_CHILD | WS_VISIBLE,
                  S(28), S(134), S(250), S(18), reinterpret_cast<HMENU>(IDC_STAT_AVGLAB));
            mkCtl(hwnd, L"STATIC", L"—", WS_CHILD | WS_VISIBLE, S(290), S(134), S(210), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_AVGVAL));
            mkCtl(hwnd, L"STATIC", L"Sự kiện bàn phím (xuống + thả):", WS_CHILD | WS_VISIBLE,
                  S(28), S(158), S(250), S(18), reinterpret_cast<HMENU>(IDC_STAT_PUSHLAB));
            mkCtl(hwnd, L"STATIC", L"0", WS_CHILD | WS_VISIBLE, S(290), S(158), S(210), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_PUSHV));
            mkCtl(hwnd, L"STATIC", L"Sự kiện chuột (nút + cuộn, ngắt từ):", WS_CHILD | WS_VISIBLE,
                  S(28), S(182), S(250), S(18), reinterpret_cast<HMENU>(IDC_STAT_MOUSELAB));
            mkCtl(hwnd, L"STATIC", L"0", WS_CHILD | WS_VISIBLE, S(290), S(182), S(210), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_MOUSEV));
            mkCtl(hwnd, L"STATIC", L"Lần đổi cửa sổ (foreground):", WS_CHILD | WS_VISIBLE,
                  S(28), S(206), S(250), S(18), reinterpret_cast<HMENU>(IDC_STAT_FGLAB));
            mkCtl(hwnd, L"STATIC", L"0", WS_CHILD | WS_VISIBLE, S(290), S(206), S(210), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_FGV));
            mkCtl(hwnd, L"STATIC", L"Đã đẩy vào hàng đợi (mọi nguồn):", WS_CHILD | WS_VISIBLE,
                  S(28), S(230), S(250), S(18), reinterpret_cast<HMENU>(IDC_STAT_RINGLAB));
            mkCtl(hwnd, L"STATIC", L"0", WS_CHILD | WS_VISIBLE, S(290), S(230), S(210), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_RINGV));
            mkCtl(hwnd, L"STATIC", L"Bị bỏ (hàng đợi đầy) — 0 = tốt:", WS_CHILD | WS_VISIBLE,
                  S(28), S(254), S(250), S(18), reinterpret_cast<HMENU>(IDC_STAT_DROPLAB));
            mkCtl(hwnd, L"STATIC", L"0", WS_CHILD | WS_VISIBLE, S(290), S(254), S(210), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_DROPV));
            mkCtl(hwnd, L"STATIC", L"Tốc độ gõ (ký tự/phút, ≈ WPM × 5):", WS_CHILD | WS_VISIBLE,
                  S(28), S(278), S(250), S(18), reinterpret_cast<HMENU>(IDC_STAT_WPMLAB));
            mkCtl(hwnd, L"STATIC", L"—", WS_CHILD | WS_VISIBLE, S(290), S(278), S(210), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_WPMVAL));
            // v1.1.0 telemetry: barrier timeouts, self-healing reinstalls, TSF
            // slow commits, and the live per-app state.
            mkCtl(hwnd, L"STATIC", L"Chờ quá hạn (barrier) — 0 = tốt:", WS_CHILD | WS_VISIBLE,
                  S(28), S(302), S(250), S(18), reinterpret_cast<HMENU>(IDC_STAT_BARRIERLAB));
            mkCtl(hwnd, L"STATIC", L"0", WS_CHILD | WS_VISIBLE, S(290), S(302), S(210), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_BARRIERV));
            mkCtl(hwnd, L"STATIC", L"Tự phục hồi hook — 0 = tốt:", WS_CHILD | WS_VISIBLE,
                  S(28), S(326), S(250), S(18), reinterpret_cast<HMENU>(IDC_STAT_REINSTLAB));
            mkCtl(hwnd, L"STATIC", L"0", WS_CHILD | WS_VISIBLE, S(290), S(326), S(210), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_REINSTV));
            mkCtl(hwnd, L"STATIC", L"Commit TSF chậm — 0 = tốt:", WS_CHILD | WS_VISIBLE,
                  S(28), S(350), S(250), S(18), reinterpret_cast<HMENU>(IDC_STAT_TSFLAB));
            mkCtl(hwnd, L"STATIC", L"0", WS_CHILD | WS_VISIBLE, S(290), S(350), S(210), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_TSFV));
            mkCtl(hwnd, L"STATIC", L"Ứng dụng hiện tại:", WS_CHILD | WS_VISIBLE,
                  S(28), S(374), S(250), S(18), reinterpret_cast<HMENU>(IDC_STAT_APPLAB));
            mkCtl(hwnd, L"STATIC", L"—", WS_CHILD | WS_VISIBLE, S(290), S(374), S(210), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_APPV));
            mkCtl(hwnd, L"STATIC",
                  L"Hàng đợi lock-free SPSC; quyết định gõ chạy trên hook thread; xuất "
                  L"TSF/SendInput trực tiếp. Số đếm THEO NGUỒN: chuột không tăng số phím.",
                  WS_CHILD | WS_VISIBLE, S(28), S(398), S(480), S(48),
                  reinterpret_cast<HMENU>(IDC_STAT_DESC));

            // v1.3.0-beta4: the diagnostics module (ok::diag) shipped in
            // beta3 fully implemented but with ZERO call sites and no UI —
            // this group is the missing control panel. Level gates every
            // recorder (Off = one relaxed load per key), the quick check
            // runs a real smoke test of the shipped pipeline, and the report
            // button writes ok::diag::report() to %APPDATA%\KieeKey.
            mkCtl(hwnd, L"BUTTON", L"Tự kiểm tra & mức chẩn đoán",
                  WS_CHILD | WS_VISIBLE | BS_GROUPBOX, S(24), S(450), S(494), S(112),
                  reinterpret_cast<HMENU>(IDC_GRP_DIAG));
            mkCtl(hwnd, L"BUTTON", L"Tắt", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  S(44), S(470), S(54), S(20), reinterpret_cast<HMENU>(IDC_RAD_DIAG_OFF));
            mkCtl(hwnd, L"BUTTON", L"Cơ bản", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  S(106), S(470), S(80), S(20), reinterpret_cast<HMENU>(IDC_RAD_DIAG_BASIC));
            mkCtl(hwnd, L"BUTTON", L"Đầy đủ", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  S(194), S(470), S(84), S(20), reinterpret_cast<HMENU>(IDC_RAD_DIAG_FULL));
            mkCtl(hwnd, L"BUTTON", L"Chạy kiểm tra nhanh", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  S(44), S(496), S(170), S(26), reinterpret_cast<HMENU>(IDC_BTN_DIAG_RUN));
            mkCtl(hwnd, L"BUTTON", L"Xuất báo cáo", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  S(226), S(496), S(140), S(26), reinterpret_cast<HMENU>(IDC_BTN_DIAG_REPORT));
            // v1.3.0-beta6 (V4): one-click copy — the tester pastes the whole
            // report straight back into the bug thread, no file hunting.
            mkCtl(hwnd, L"BUTTON", L"Sao chép báo cáo", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  S(378), S(496), S(132), S(26), reinterpret_cast<HMENU>(IDC_BTN_DIAG_COPY));
            mkCtl(hwnd, L"STATIC", L"Cơ bản: đếm + độ trễ · Đầy đủ: thêm trace · Tắt: ~miễn phí",
                  WS_CHILD | WS_VISIBLE, S(44), S(528), S(456), S(26),
                  reinterpret_cast<HMENU>(IDC_STAT_DIAG_RESULT));

            // ---- tab 4: Thông tin (v1.1.2 — in-app introduction) ----
            // v1.1.2-r3: the tagline merged into the about paragraph; its
            // slot is now the LIVE DIAGNOSTICS block (running build + state
            // + conflict verdict — the "why do my digits still convert"
            // answer, refreshed every timer tick).
            {
                HWND n = mkCtl(hwnd, L"STATIC", kAppTitle,
                               WS_CHILD | WS_VISIBLE, S(28), S(110), S(400), S(34),
                               reinterpret_cast<HMENU>(IDC_STAT_INFO_NAME));
                ::SendMessageW(n, WM_SETFONT, reinterpret_cast<WPARAM>(uiFontTitle()), TRUE);
            }
            mkCtl(hwnd, L"STATIC", L"",
                  WS_CHILD | WS_VISIBLE, S(28), S(148), S(500), S(64),
                  reinterpret_cast<HMENU>(IDC_STAT_INFO_STATUS));
            mkCtl(hwnd, L"STATIC",
                  L"Bộ gõ tiếng Việt hiện đại cho Windows — nhanh, chính xác, "
                  L"ổn định. KieeKey chạy nền trong khay, giúp gõ tiếng Việt có "
                  L"dấu trong mọi ứng dụng (Word, Chrome, VS Code, game…). Lõi "
                  L"gõ hiện đại hoá từ OpenKey: quyết định gõ chạy ngay trên "
                  L"hook thread, xuất chữ trực tiếp qua TSF/SendInput — không "
                  L"clipboard, không chữ nháy. Bật/tắt ngay trong ứng dụng.",
                  WS_CHILD | WS_VISIBLE, S(28), S(220), S(500), S(88),
                  reinterpret_cast<HMENU>(IDC_STAT_INFO_ABOUT));
            mkCtl(hwnd, L"STATIC",
                  L"Tính năng chính:\n"
                  L"• Ba phương thức gõ Telex · VNI · Simple Telex và 5 bảng mã phổ biến\n"
                  L"• Số 0–9 luôn gõ ra chữ số — hết cảnh gõ số bị thành dấu tiếng Việt\n"
                  L"• Gõ tắt (macro) nhiều dòng, tự lưu vào %APPDATA%\\KieeKey\\macros.txt\n"
                  L"• Tự tắt trong IDE và game toàn màn hình để không vướng thao tác\n"
                  L"• Chống nháy chữ qua TSF cho trình duyệt & Office; SendInput siêu nhanh\n"
                  L"• F9 chuyển kiểu đặt dấu cho từ đang gõ (oà ↔ òa) — giữ nguyên từ đã gõ",
                  WS_CHILD | WS_VISIBLE, S(28), S(316), S(500), S(112),
                  reinterpret_cast<HMENU>(IDC_STAT_INFO_FEAT));
            mkCtl(hwnd, L"STATIC",
                  L"Hướng dẫn nhanh:\n"
                  L"• Bật/tắt: nhấp trái (hoặc phải) biểu tượng khay, hoặc nút lớn phía dưới\n"
                  L"• Đổi phương thức gõ: trình đơn khay → Phương thức gõ, hoặc tab Bàn phím\n"
                  L"• Mọi thay đổi được lưu ngay — thoát và mở lại luôn giữ nguyên cấu hình",
                  WS_CHILD | WS_VISIBLE, S(28), S(436), S(500), S(72),
                  reinterpret_cast<HMENU>(IDC_STAT_INFO_GUIDE));
            mkCtl(hwnd, L"STATIC",
                  L"Nguồn gốc & bản quyền: KieeKey phát triển từ OpenKey © 2019 Tuyen Mai, "
                  L"phát hành theo GNU GPL v3. Phần hiện đại hoá © 2026 coderunknow.",
                  WS_CHILD | WS_VISIBLE, S(28), S(510), S(500), S(32),
                  reinterpret_cast<HMENU>(IDC_STAT_INFO_LICENSE));
            mkCtl(hwnd, WC_LINK,
                  L"<A HREF=\"https://github.com/coderunknow/KieeKey\">Mã nguồn · tài liệu · cập nhật: github.com/coderunknow/KieeKey</A>",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP, S(28), S(546), S(500), S(20),
                  reinterpret_cast<HMENU>(IDC_LNK_REPO));

            // ---- tab 5: Arcade (v1.3.0) ----
            mkCtl(hwnd, L"BUTTON", L"KieeKey Arcade (8 Minigames)",
                  WS_CHILD | WS_VISIBLE | BS_GROUPBOX, S(24), S(100), S(494), S(324),
                  reinterpret_cast<HMENU>(IDC_GRP_ARCADE));
            mkCtl(hwnd, L"BUTTON", L"🐍 Snake (Rắn săn mồi)", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  S(44), S(126), S(220), S(28), reinterpret_cast<HMENU>(IDC_BTN_PLAY_SNAKE));
            mkCtl(hwnd, L"BUTTON", L"🧱 Tetris (Xếp gạch)", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  S(280), S(126), S(220), S(28), reinterpret_cast<HMENU>(IDC_BTN_PLAY_TETRIS));
            mkCtl(hwnd, L"BUTTON", L"🎣 Fishing (Câu cá gõ phím)", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  S(44), S(162), S(220), S(28), reinterpret_cast<HMENU>(IDC_BTN_PLAY_FISHING));
            mkCtl(hwnd, L"BUTTON", L"🏎️ Typing Race (Đua xe)", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  S(280), S(162), S(220), S(28), reinterpret_cast<HMENU>(IDC_BTN_PLAY_TYPINGRACE));
            mkCtl(hwnd, L"BUTTON", L"🏎️ WASD + Typing Racing", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  S(44), S(198), S(220), S(28), reinterpret_cast<HMENU>(IDC_BTN_PLAY_WASDRACE));
            mkCtl(hwnd, L"BUTTON", L"🎵 Rhythm Typing (FNF)", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  S(280), S(198), S(220), S(28), reinterpret_cast<HMENU>(IDC_BTN_PLAY_RHYTHM));
            mkCtl(hwnd, L"BUTTON", L"🎯 No-Mistake Mode", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  S(44), S(234), S(220), S(28), reinterpret_cast<HMENU>(IDC_BTN_PLAY_NOMISTAKE));
            mkCtl(hwnd, L"BUTTON", L"🗿 Flexing Mode (Joke)", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  S(280), S(234), S(220), S(28), reinterpret_cast<HMENU>(IDC_BTN_PLAY_FLEXING));
            mkCtl(hwnd, L"STATIC", L"Trạng thái: Chưa có game nào đang chạy.",
                  WS_CHILD | WS_VISIBLE, S(44), S(270), S(456), S(52),
                  reinterpret_cast<HMENU>(IDC_STAT_ARCADE_STATUS));

            // v1.3.0: the games open in their own graphical window; this tab is
            // the launcher + the run configuration.
            mkCtl(hwnd, L"BUTTON", L"🗿 Phòng Chaos / Flexing (test gõ thật)",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP, S(44), S(326), S(220), S(28),
                  reinterpret_cast<HMENU>(IDC_BTN_OPEN_CHAOS_LAB));
            mkCtl(hwnd, L"STATIC", L"Chế độ Rhythm / No-Mistake:",
                  WS_CHILD | WS_VISIBLE | SS_LEFT, S(280), S(326), S(220), S(20), reinterpret_cast<HMENU>(IDC_STAT_FAILMODE));
            HWND failMode = mkCtl(hwnd, L"COMBOBOX", L"",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                  S(280), S(344), S(220), S(120), reinterpret_cast<HMENU>(IDC_CMB_FAILMODE));
            if (failMode != nullptr) {
                ::SendMessageW(failMode, CB_ADDSTRING, 0,
                               reinterpret_cast<LPARAM>(L"Hardcore — sai là chết (mặc định)"));
                ::SendMessageW(failMode, CB_ADDSTRING, 0,
                               reinterpret_cast<LPARAM>(L"Thanh máu — sai trừ máu"));
                ::SendMessageW(failMode, CB_SETCURSEL, 0, 0);
            }
            mkCtl(hwnd, L"STATIC", L"Nhịp Rhythm (BPM 60-220):",
                  WS_CHILD | WS_VISIBLE | SS_LEFT, S(280), S(368), S(160), S(20), reinterpret_cast<HMENU>(IDC_STAT_RHYTHM_BPM));
            mkCtl(hwnd, L"EDIT", L"112",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | WS_BORDER,
                  S(444), S(366), S(56), S(22), reinterpret_cast<HMENU>(IDC_EDT_RHYTHM_BPM));
            mkCtl(hwnd, L"BUTTON", L"✔ Áp dụng cấu hình game",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP, S(44), S(362), S(220), S(28),
                  reinterpret_cast<HMENU>(IDC_BTN_APPLY_ARCADE_CFG));

            // v1.3.0-beta3 (bug #2): typing-game passage language. Vietnamese (the
            // default) shows diacritic-bearing prompts and composes Telex/VNI inside
            // the game window (the hook bypasses our own windows); English keeps the
            // legacy ASCII prompt with 1:1 matching. The composition method follows
            // the IME method the user already configured (g.options.inputMethod).
            mkCtl(hwnd, L"STATIC", L"Ngôn ngữ đoạn văn:",
                  WS_CHILD | WS_VISIBLE | SS_LEFT, S(44), S(398), S(200), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_PASSAGE_LANG));
            HWND passageLang = mkCtl(hwnd, L"COMBOBOX", L"",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                  S(250), S(396), S(250), S(140), reinterpret_cast<HMENU>(IDC_CMB_PASSAGE_LANG));
            if (passageLang != nullptr) {
                ::SendMessageW(passageLang, CB_ADDSTRING, 0,
                               reinterpret_cast<LPARAM>(L"Tiếng Việt (Telex/VNI) — mặc định"));
                ::SendMessageW(passageLang, CB_ADDSTRING, 0,
                               reinterpret_cast<LPARAM>(L"English (ASCII)"));
                ::SendMessageW(passageLang, CB_SETCURSEL, 0, 0);
            }

            // v1.3.0-beta5 (bug B7): steering-key choice for the WASD race.
            // In VN mode the letters a/s/d/w are Telex/VNI composition keys,
            // so beta4 forced steering onto the arrows — players who drive
            // with WASD asked for it back. The modes (see WasdSteering):
            // arrows only (default), WASD steer + compose, or both. The
            // choice persists and applies live to a running game.
            mkCtl(hwnd, L"STATIC", L"Phím lái (đua xe WASD):",
                  WS_CHILD | WS_VISIBLE | SS_LEFT, S(44), S(430), S(200), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_STEERING));
            HWND steering = mkCtl(hwnd, L"COMBOBOX", L"",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                  S(250), S(428), S(250), S(140), reinterpret_cast<HMENU>(IDC_CMB_STEERING));
            if (steering != nullptr) {
                ::SendMessageW(steering, CB_ADDSTRING, 0,
                               reinterpret_cast<LPARAM>(L"Mũi tên (mặc định)"));
                ::SendMessageW(steering, CB_ADDSTRING, 0,
                               reinterpret_cast<LPARAM>(L"WASD — lái VÀ gõ Telex/VNI"));
                ::SendMessageW(steering, CB_ADDSTRING, 0,
                               reinterpret_cast<LPARAM>(L"Cả hai (mũi tên + WASD)"));
                ::SendMessageW(steering, CB_SETCURSEL, 0, 0);
            }

            // ---- tab 6: Phòng Chaos (v1.3.0) ----
            mkCtl(hwnd, L"BUTTON", L"Phòng thí nghiệm Chaos & Thử nghiệm",
                  WS_CHILD | WS_VISIBLE | BS_GROUPBOX, S(24), S(100), S(494), S(196),
                  reinterpret_cast<HMENU>(IDC_GRP_CHAOS));
            mkCtl(hwnd, L"BUTTON", L"Bật Chaos trong Lab (không đổi chữ khi gõ bình thường)",
                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, S(44), S(126), S(450), S(22),
                  reinterpret_cast<HMENU>(IDC_CHK_CHAOS_MASTER));
            mkCtl(hwnd, L"BUTTON", L"Random Casing / Chaos Case (Viết hoa - thường ngẫu nhiên)",
                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, S(44), S(154), S(450), S(22),
                  reinterpret_cast<HMENU>(IDC_CHK_CHAOS_CASE));
            mkCtl(hwnd, L"BUTTON", L"Glyph Visual Transform (Xoay / lật hiển thị chữ)",
                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, S(44), S(182), S(450), S(22),
                  reinterpret_cast<HMENU>(IDC_CHK_GLYPH_TRANSFORM));
            mkCtl(hwnd, L"STATIC",
                  L"Các tùy chọn ở trên chỉ dành cho Lab. Để đổi chữ khi gõ trong ứng dụng khác, "
                  L"dùng nhóm Hiệu ứng gõ trực tiếp ở dưới. Hai chế độ độc lập. "
                  // v1.3.0-beta5 (G1): measured per-key cost of the active lab
                  // engine, next to the toggle it belongs to.
                  L"Chi phí khi Chaos bật: ≈ +21 ns/phím (p50 64→85, bench beta4 — docs/PERFORMANCE.md).",
                  WS_CHILD | WS_VISIBLE, S(44), S(214), S(450), S(88),
                  reinterpret_cast<HMENU>(IDC_STAT_CHAOS_WARN));

            mkCtl(hwnd, L"BUTTON", L"Hiệu ứng gõ trực tiếp — ứng dụng bên ngoài",
                  WS_CHILD | WS_VISIBLE | BS_GROUPBOX, S(24), S(304), S(494), S(245),
                  reinterpret_cast<HMENU>(IDC_GRP_LIVE));
            mkCtl(hwnd, L"BUTTON", L"Bật khi gõ bên ngoài (tắt nhanh: Ctrl+Alt+F12)",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                  S(44), S(328), S(450), S(24), reinterpret_cast<HMENU>(IDC_CHK_LIVE));
            mkCtl(hwnd, L"BUTTON", L"Random casing — đổi hoa/thường theo từng ký tự",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                  S(44), S(358), S(450), S(24), reinterpret_cast<HMENU>(IDC_CHK_LIVE_CASE));
            mkCtl(hwnd, L"STATIC", L"Glyph Unicode:", WS_CHILD | WS_VISIBLE,
                  S(44), S(394), S(125), S(22), reinterpret_cast<HMENU>(IDC_STAT_LIVE_GLYPH));
            HWND liveGlyph = mkCtl(hwnd, L"COMBOBOX", L"",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                  S(176), S(390), S(318), S(160), reinterpret_cast<HMENU>(IDC_CMB_LIVE_GLYPH));
            for (const wchar_t* mode : {L"Không đổi glyph", L"Lật ngược từng chữ (giống xoay 180°)",
                                       L"Lật ngang từng chữ", L"Lật ngẫu nhiên"}) {
                ::SendMessageW(liveGlyph, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mode));
            }
            mkCtl(hwnd, L"STATIC", L"Cường độ:", WS_CHILD | WS_VISIBLE,
                  S(44), S(430), S(125), S(22), reinterpret_cast<HMENU>(IDC_STAT_LIVE_INTENSITY));
            HWND liveIntensity = mkCtl(hwnd, L"COMBOBOX", L"",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                  S(176), S(426), S(318), S(140), reinterpret_cast<HMENU>(IDC_CMB_LIVE_INTENSITY));
            for (const wchar_t* level : {L"25%", L"50%", L"75%", L"100%"}) {
                ::SendMessageW(liveIntensity, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(level));
            }
            mkCtl(hwnd, L"STATIC",
                  L"Cần bộ gõ BẬT + bảng mã Unicode (TCVN3/VNI không hiện được chữ lật — "
                  L"đổi ở tab Bàn phím); không tác động app bị loại trừ. Nguyên âm tiếng Việt "
                  L"có dấu được lật theo dấu: sắc↔huyền, hỏi↔ngã; chữ không có bản lật giữ "
                  L"nguyên. Không dùng khi nhập mật khẩu; không xoay hình học 90°/270°. "
                  // v1.3.0-beta5: B9 made these toggles persist (beta4 reverted
                  // them every restart); G1 documents the measured cost.
                  L"Mặc định TẮT; bật/tắt ĐƯỢC lưu qua lần chạy (từ beta5). "
                  L"Chi phí khi bật: lật glyph ≈ +1,8 ns/ký tự (bench beta4 — docs/PERFORMANCE.md).",
                  // v1.3.0-beta5 (G1): grew two clauses (persistence + measured
                  // cost) — the rect moves up into the 12px slack under the
                  // intensity label and grows to 92px (bottom S(546), still
                  // above the gate readout at S(550)); audit_layout verifies.
                  WS_CHILD | WS_VISIBLE, S(44), S(454), S(450), S(92),
                  reinterpret_cast<HMENU>(IDC_STAT_LIVE_HINT));
            // v1.3.0-beta5 (bug B2): the GATE READOUT — one line that always
            // tells the truth about whether live effects can reach external
            // apps right now, and if not, WHICH condition vetoes them (the
            // pure model behind it is ok::effects::liveGateBlocker). Updated
            // every timer tick (IME toggle, foreground exclusion, code-table
            // changes all reflect within 500 ms).
            mkCtl(hwnd, L"STATIC", liveGateStatusText(), WS_CHILD | WS_VISIBLE,
                  S(28), S(550), S(490), S(17),
                  reinterpret_cast<HMENU>(IDC_STAT_LIVE_GATE));

            // ---- tab 7: AI Rival & Coaching (v1.3.0) ----
            mkCtl(hwnd, L"BUTTON", L"Personal AI Typing Rival & Huấn luyện viên",
                  WS_CHILD | WS_VISIBLE | BS_GROUPBOX, S(24), S(100), S(494), S(280),
                  reinterpret_cast<HMENU>(IDC_GRP_AI));
            mkCtl(hwnd, L"BUTTON", L"Cho phép AI học nhịp gõ cá nhân (Opt-in an toàn, hoàn toàn cục bộ)",
                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, S(44), S(126), S(450), S(22),
                  reinterpret_cast<HMENU>(IDC_CHK_AI_OPTIN));
            mkCtl(hwnd, L"BUTTON", L"Đặt lại hồ sơ AI", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  S(44), S(154), S(160), S(26), reinterpret_cast<HMENU>(IDC_BTN_AI_RESET));
            mkCtl(hwnd, L"STATIC", L"Chỉ số AI: Chưa có dữ liệu (Cần bật Opt-in và gõ thử)",
                  WS_CHILD | WS_VISIBLE, S(44), S(186), S(450), S(35),
                  reinterpret_cast<HMENU>(IDC_STAT_AI_STATS));
            mkCtl(hwnd, L"STATIC", L"Coach: Hãy gõ thêm để Coach phân tích nhịp gõ...",
                  WS_CHILD | WS_VISIBLE, S(44), S(226), S(450), S(50),
                  reinterpret_cast<HMENU>(IDC_STAT_COACH_ADVICE));

            // ---- tab 8: Tiến trình (v1.3.0) ----
            mkCtl(hwnd, L"BUTTON", L"Tiến trình người dùng & Thành tích",
                  WS_CHILD | WS_VISIBLE | BS_GROUPBOX, S(24), S(100), S(494), S(280),
                  reinterpret_cast<HMENU>(IDC_GRP_PROG));
            mkCtl(hwnd, L"STATIC", L"Cấp độ: Level 1", WS_CHILD | WS_VISIBLE,
                  S(44), S(126), S(220), S(22), reinterpret_cast<HMENU>(IDC_STAT_LEVEL_VAL));
            mkCtl(hwnd, L"STATIC", L"Tổng XP: 0 XP", WS_CHILD | WS_VISIBLE,
                  S(280), S(126), S(220), S(22), reinterpret_cast<HMENU>(IDC_STAT_XP_VAL));
            mkCtl(hwnd, L"STATIC", L"Tổng số phím đã gõ: 0", WS_CHILD | WS_VISIBLE,
                  S(44), S(154), S(450), S(22), reinterpret_cast<HMENU>(IDC_STAT_KEYS_VAL));
            mkCtl(hwnd, L"STATIC", L"Thành tích: Bắt đầu hành trình cùng KieeKey!",
                  WS_CHILD | WS_VISIBLE, S(44), S(182), S(450), S(60),
                  reinterpret_cast<HMENU>(IDC_STAT_ACHIEVEMENTS));
            mkCtl(hwnd, L"BUTTON", L"Đặt lại tiến trình", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  S(44), S(250), S(160), S(26), reinterpret_cast<HMENU>(IDC_BTN_PROG_RESET));

            // ---- buttons ----
            // v1.1.1: the always-visible in-app ON/OFF switch (the removed
            // Ctrl+Shift hotkey's replacement). Lives on the button row so it
            // stays reachable from EVERY tab; the label mirrors the current
            // state and is refreshed by WM_TIMER every 500 ms.
            {
                HWND tg = mkCtl(hwnd, L"BUTTON", L"",
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                          S(12), S(580), S(240), S(30),
                          reinterpret_cast<HMENU>(IDC_BTN_TOGGLE));
                ::SendMessageW(tg, WM_SETFONT, reinterpret_cast<WPARAM>(uiFontBold()), TRUE);
            }
            mkCtl(hwnd, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                  S(300), S(580), S(76), S(30), reinterpret_cast<HMENU>(IDOK));
            mkCtl(hwnd, L"BUTTON", L"Hủy", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  S(384), S(580), S(76), S(30), reinterpret_cast<HMENU>(IDCANCEL));
            mkCtl(hwnd, L"BUTTON", L"Áp dụng", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  S(468), S(580), S(80), S(30), reinterpret_cast<HMENU>(IDC_BTN_APPLY));

            // ---- v1.3.0-beta4/beta5: runtime layout solve (ok::layout) ----
            // The authored rectangles above are the 96-dpi design, gated
            // statically by scripts/audit_layout.py. Here the app MEASURES
            // the real font on the real monitor (DrawTextW + DT_CALCRECT)
            // and lets the solver reflow: every wrapping label grows to its
            // measured height, the controls below shift down, group boxes
            // stretch to keep containing their children, and the WINDOW is
            // refit (v1.3.0-beta5, bug B1): grown OR shrunk toward the
            // solved content height clamped to the monitor work area —
            // partial growth is applied instead of skipped — moved fully on
            // screen, and whatever still does not fit becomes a WS_VSCROLL
            // range, so no content can overlap the button row or clip
            // silently again. See solveSettingsLayout() + the pure model in
            // DialogLayout.hpp (tests/test_dialog_layout.cpp).
            solveSettingsLayout(hwnd);
            settingsToControls();
            // v1.3.0-beta4: reflect the persisted diagnostics level in the
            // tab-3 radios (the level itself was applied at boot).
            {
                const ok::diag::Level lvl = ok::diag::Diagnostics::instance().level();
                ::CheckRadioButton(hwnd, IDC_RAD_DIAG_OFF, IDC_RAD_DIAG_FULL,
                                   lvl == ok::diag::Level::Off ? IDC_RAD_DIAG_OFF :
                                   lvl == ok::diag::Level::Full ? IDC_RAD_DIAG_FULL :
                                                                  IDC_RAD_DIAG_BASIC);
            }
            // v1.1.0: the latency peak now reads "since the dialog opened"
            // (previously a process-lifetime outlier dominated the display).
            g.hook.resetPeakLatency();
            ::SetTimer(hwnd, 1, 500, nullptr);   // live telemetry
            return 0;
            } catch (const std::bad_alloc&) {
                g.hSettings = nullptr; g.macroEdit.store(nullptr, std::memory_order_release);
                return -1;
            } catch (...) {
                g.hSettings = nullptr; g.macroEdit.store(nullptr, std::memory_order_release);
                return -1;
            }
        }

        case WM_NOTIFY: {
            const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
            if (nm && nm->idFrom == IDC_TAB && nm->code == TCN_SELCHANGE) {
                showTab(static_cast<int>(::SendMessageW(nm->hwndFrom, TCM_GETCURSEL, 0, 0)));
            }
            // v1.1.2: the Information tab's repository link (SysLink) — open
            // the URL in the default browser.
            if (nm && nm->idFrom == IDC_LNK_REPO &&
                (nm->code == NM_CLICK || nm->code == NM_RETURN)) {
                ::ShellExecuteW(hwnd, L"open",
                                L"https://github.com/coderunknow/KieeKey",
                                nullptr, nullptr, SW_SHOWNORMAL);
            }
            return 0;
        }

        case WM_DPICHANGED: {
            // v1.1.0: follow the monitor DPI — resize the frame to the OS-
            // suggested rect.
            // v1.2.0: AND re-scale every child control + font. Previously the
            // controls kept their creation-monitor geometry, so a dialog
            // dragged to a differently-scaled monitor rendered clipped/
            // mis-scaled until it was closed and reopened.
            const auto* rects = reinterpret_cast<const RECT*>(lParam);
            if (rects != nullptr) {
                const int w = rects->right - rects->left;
                const int h = rects->bottom - rects->top;
                ::SetWindowPos(hwnd, nullptr, rects->left, rects->top, w, h,
                               SWP_NOZORDER | SWP_NOACTIVATE);
            }
            refreshSettingsDpi();
            // v1.3.0-beta5 (bug B1): re-measure + re-solve at the new scale
            // (refreshSettingsDpi only rescales rects/fonts in place; a label
            // that wraps taller at the new DPI must GROW, and the window must
            // refit against the new monitor's work area — including the
            // scroll fallback when the new scale no longer fits).
            solveSettingsLayout(hwnd);
            return 0;
        }

        case WM_VSCROLL: {
            // v1.3.0-beta5 (bug B1): scroll fallback — the page children
            // move up and clip against the tab viewport (pure model:
            // ok::layout::scrollChildRect, pinned by tests). The always-
            // visible header/button chrome never scrolls.
            if (!g_settingsScroll.enabled) { return 0; }
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            if (!::GetScrollInfo(hwnd, SB_VERT, &si)) { return 0; }
            int pos = g_settingsScroll.offset;
            const int linePx = ::MulDiv(16, static_cast<int>(g_settingsDpi), 96);
            switch (LOWORD(wParam)) {
                case SB_LINEUP:        pos -= linePx; break;
                case SB_LINEDOWN:      pos += linePx; break;
                case SB_PAGEUP:        pos -= static_cast<int>(si.nPage); break;
                case SB_PAGEDOWN:      pos += static_cast<int>(si.nPage); break;
                case SB_THUMBPOSITION:
                case SB_THUMBTRACK:    pos = si.nTrackPos; break;
                case SB_TOP:           pos = 0; break;
                case SB_BOTTOM:        pos = si.nMax; break;
                default: return 0;
            }
            pos = std::clamp(pos, 0, std::max(0, si.nMax));
            if (pos == g_settingsScroll.offset) { return 0; }
            g_settingsScroll.offset = pos;
            si.fMask = SIF_POS;
            si.nPos = pos;
            ::SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            applySettingsScrollOffset(hwnd);
            return 0;
        }

        case WM_TIMER: {
            wchar_t buf[64];

            // v1.1.1: keep the always-visible on/off button in sync with the
            // engine state (it can also be flipped from the tray menu while
            // this dialog is open). v1.1.2: the header status line follows.
            const wchar_t* toggleLabel =
                g.engineEnabled.load(std::memory_order_relaxed)
                    ? L"Bộ gõ: ĐANG BẬT — bấm để TẮT"
                    : L"Bộ gõ: ĐANG TẮT — bấm để BẬT";
            {
                HWND b = ::GetDlgItem(hwnd, IDC_BTN_TOGGLE);
                wchar_t cur[64];
                if (::GetWindowTextW(b, cur, 64) == 0 || std::wcscmp(cur, toggleLabel) != 0) {
                    ::SetWindowTextW(b, toggleLabel);
                }
            }
            updateHeaderStatus();

            // v1.1.2-r3: the Information tab's live diagnostics block (the
            // conflict verdict can change while the dialog is open — the
            // user may quit EVKey right now).
            {
                static std::wstring s_lastDiag;
                const std::wstring diag = infoDiagnosticsText();
                if (diag != s_lastDiag) {
                    ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_INFO_STATUS),
                                     diag.c_str());
                    s_lastDiag = diag;
                }
            }

            // Latency: peak + EMA average.
            std::swprintf(buf, std::size(buf), L"%lld", static_cast<long long>(g.hook.peakLatencyUs()));
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_LATVAL), buf);
            std::swprintf(buf, std::size(buf), L"%lld", static_cast<long long>(g.hook.avgLatencyUs()));
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_AVGVAL), buf);

            // Counters — v1.3.0-beta5 (bug B3): ONE ROW PER SOURCE.
            // PUSHV shows keyboardEvents() (KeyDown+KeyUp+SysKeyDown+SysKeyUp),
            // NEVER pushed() again: the ring counter also climbs on mouse
            // buttons/wheel and foreground changes, which is exactly the
            // "counter moves while I only drag the mouse" report.
            {
                const ok::hook::HookCounters& hc = g.hook.counters();
                std::swprintf(buf, std::size(buf), L"%llu",
                              static_cast<unsigned long long>(hc.keyboardEvents()));
                ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_PUSHV), buf);
                std::swprintf(buf, std::size(buf), L"%llu",
                              static_cast<unsigned long long>(hc.mouseEvents()));
                ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_MOUSEV), buf);
                std::swprintf(buf, std::size(buf), L"%llu",
                              static_cast<unsigned long long>(
                                  hc.foregroundChanged.load(std::memory_order_relaxed)));
                ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_FGV), buf);
            }
            std::swprintf(buf, std::size(buf), L"%llu",
                          static_cast<unsigned long long>(g.hook.pushed()));
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_RINGV), buf);
            std::swprintf(buf, std::size(buf), L"%llu",
                          static_cast<unsigned long long>(g.hook.dropped()));
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_DROPV), buf);

            // v1.1.0 telemetry rows.
            std::swprintf(buf, std::size(buf), L"%llu",
                          static_cast<unsigned long long>(g.drainBarrier.timeouts()));
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_BARRIERV), buf);
            std::swprintf(buf, std::size(buf), L"%llu",
                          static_cast<unsigned long long>(g.hook.hookReinstallCount()));
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_REINSTV), buf);
            std::swprintf(buf, std::size(buf), L"%llu",
                          static_cast<unsigned long long>(g.tsfSlowCount.load(std::memory_order_relaxed)));
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_TSFV), buf);
            {
                // v1.3.0-beta5 (bug B4): the row now says WHY the IME is off
                // for this app — "elevated" (UIPI pass-through) and "auto-
                // excluded" (IDE/game policy) are different facts with
                // different remedies, and an unresolved process name arrives
                // here as the honest "pid N (lỗi X)" label instead of the
                // beta4 bare "unknown".
                const auto snap = g.monitor.snapshot();
                const bool excl = g.fgExcluded_.load(std::memory_order_relaxed);
                std::wstring app = snap ? utf8ToUtf16(snap->exeNameUtf8) : std::wstring(L"—");
                if (snap) {
                    if (excl) {
                        app += g.monitor.currentAppElevated()
                                   ? L"  —  TẮT: app chạy quyền cao hơn KieeKey (gõ thô)"
                                   : L"  —  TẮT: loại trừ tự động (IDE/game)";
                    } else {
                        app += L"  —  đang gõ";
                    }
                }
                ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_APPV), app.c_str());
            }
            // v1.3.0-beta5 (bug B2): the tab-6 gate readout follows the IME
            // state, the foreground exclusion and the code table live.
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_LIVE_GATE),
                             liveGateStatusText());

            // WPM gauge: EMA of printable-characters-per-minute.
            // v1.1.0: decays to 0 after ~2 s of silence — the old gauge froze
            // on the last rate forever once the user stopped typing.
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
                if (keys == s_lastKeys && dt >= 1.0) {
                    s_kpmEma = static_cast<std::int64_t>(static_cast<double>(s_kpmEma) * 0.25);
                } else {
                    s_kpmEma = (s_kpmEma == 0)
                        ? static_cast<std::int64_t>(kpm)
                        : static_cast<std::int64_t>(static_cast<double>(s_kpmEma) * 0.7 + kpm * 0.3);
                }
            }
            s_lastKeys = keys;
            s_lastMs   = nowMs;
            std::swprintf(buf, std::size(buf), L"%lld", static_cast<long long>(s_kpmEma));
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_WPMVAL), buf);

            ::CheckDlgButton(hwnd, IDC_CHK_LIVE,
                             g.liveEffects.enabled() ? BST_CHECKED : BST_UNCHECKED);
            // v1.3.0: Live updates for Arcade, AI, and Progression tabs.
            // The arcade itself is drawn in its own GDI window; the tab shows a
            // compact status line so the user knows what is running.
            {
                auto& arcade = ok::arcade::ArcadeManager::instance();
                if (arcade.hasActiveGame()) {
                    const ok::arcade::Frame& aframe = arcade.getFrame();
                    const ok::arcade::GameStats& astats = aframe.stats;
                    const std::string slug = std::string(
                        ok::arcade::gameSlug(static_cast<int>(arcade.getCurrentGameType())));
                    wchar_t abuf[320];
                    std::swprintf(abuf, std::size(abuf),
                                  L"Đang chơi: %hs | Điểm %lld | WPM %.1f | Chuẩn xác %.1f%%\r\n"
                                  L"Game chạy trong cửa sổ KieeKey Arcade Hub (nút bên trên).",
                                  slug.c_str(), static_cast<long long>(astats.score), astats.wpm,
                                  astats.accuracy);
                    ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_ARCADE_STATUS), abuf);
                } else {
                    ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_ARCADE_STATUS),
                                     L"Chưa có game nào đang chạy.\r\n"
                                     L"Nhấn một nút game để mở cửa sổ Arcade Hub.\r\n"
                                     // v1.3.0-beta5 (G1): measured standby cost,
                                     // so the idle hook overhead is documented
                                     // where the feature lives.
                                     L"Chi phí khi không chơi: ≈ +2 ns/phím (p50 64→66, bench beta4 — docs/PERFORMANCE.md).");
                }
            }

            // v1.3.0: credit every arcade run that finished since the last tick
            // (XP, records, achievements) — the desktop hub used to drop them,
            // so "gõ nhiều để lên cấp" only ever worked through the web bridge.
            ok::arcade::ArcadeManager::instance().drainRunResultsToProgression();

            // Fold the hook thread's lock-free counters into the aggregate
            // before reading them (this is also where level-ups are applied).
            ok::progression::ProgressionEngine::instance().flushStats();

            // Progression stats
            auto pstats = ok::progression::ProgressionEngine::instance().getStats();
            wchar_t pbuf[128];
            std::swprintf(pbuf, std::size(pbuf), L"Cấp độ: Level %u", pstats.currentLevel);
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_LEVEL_VAL), pbuf);
            std::swprintf(pbuf, std::size(pbuf), L"Tổng XP: %llu XP", static_cast<unsigned long long>(pstats.totalXp));
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_XP_VAL), pbuf);
            std::swprintf(pbuf, std::size(pbuf), L"Tổng số phím đã gõ: %llu", static_cast<unsigned long long>(pstats.totalKeystrokes));
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_KEYS_VAL), pbuf);

            // AI Profile stats
            if (ok::ai::AiRivalEngine::instance().isOptIn()) {
                ok::ai::AiRivalEngine::instance().trainBatch();
                auto aiprof = ok::ai::AiRivalEngine::instance().getProfile();
                wchar_t aibuf[256];
                // v1.3.0-beta5 (G1): the opt-in's measured hot-path cost rides
                // along with the stats it produces.
                std::swprintf(aibuf, std::size(aibuf),
                              L"AI: Mean IKI %.1f ms | Lỗi tự nhiên %.1f%% | Trễ phím dấu %.1f ms | Chi phí: ≈ +11 ns/phím (bench beta4)",
                              aiprof.meanIkiMs, aiprof.errorRate * 100.0, aiprof.toneDelayMs);
                ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_AI_STATS), aibuf);

                auto coachRecs = ok::analytics::TypingAnalyticsEngine::instance().generateCoachingAdvice();
                if (!coachRecs.empty()) {
                    std::string ctext = "[Thực tế]: " + coachRecs[0].measuredFact + "\n[Gợi ý]: " + coachRecs[0].heuristicAdvice;
                    std::wstring wctext = utf8ToUtf16(ctext);
                    ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_COACH_ADVICE), wctext.c_str());
                }
            } else {
                ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_AI_STATS), L"AI: chưa bật (không học nhịp gõ).");
                ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_COACH_ADVICE), L"Coach: bật AI để xem phân tích.");
            }
            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_BTN_TOGGLE:
                    // v1.1.1: the in-app ON/OFF switch (replacement for the
                    // removed Ctrl+Shift hotkey). Persists + balloons via the
                    // shared path; the WM_TIMER tick refreshes the label.
                    toggleEngineFromUi();
                    return 0;
                case IDC_BTN_PLAY_SNAKE:
                    openArcadeHub("snake");
                    return 0;
                case IDC_BTN_PLAY_TETRIS:
                    openArcadeHub("tetris");
                    return 0;
                case IDC_BTN_PLAY_FISHING:
                    openArcadeHub("fishing");
                    return 0;
                case IDC_BTN_PLAY_TYPINGRACE:
                    openArcadeHub("typing-race");
                    return 0;
                case IDC_BTN_PLAY_WASDRACE:
                    openArcadeHub("wasd-race");
                    return 0;
                case IDC_BTN_PLAY_RHYTHM:
                    openArcadeHub("rhythm");
                    return 0;
                case IDC_BTN_PLAY_NOMISTAKE:
                    openArcadeHub("no-mistake");
                    return 0;
                case IDC_BTN_PLAY_FLEXING:
                    openArcadeHub("flexing");
                    return 0;
                case IDC_BTN_OPEN_CHAOS_LAB:
                    // Dedicated lab window: type text, see the exact chaos
                    // output, and (optionally) write it into the focused app.
                    openChaosLab();
                    return 0;
                case IDC_BTN_APPLY_ARCADE_CFG: {
                    auto cfg = ok::arcade::ArcadeManager::instance().getConfig();
                    HWND combo = ::GetDlgItem(hwnd, IDC_CMB_FAILMODE);
                    const int selection = (combo != nullptr)
                                              ? static_cast<int>(::SendMessageW(combo, CB_GETCURSEL,
                                                                               0, 0))
                                              : 0;
                    const auto mode = (selection == 1) ? ok::arcade::FailMode::HealthBar
                                                       : ok::arcade::FailMode::Hardcore;
                    cfg.rhythmFailMode = mode;
                    cfg.noMistakeFailMode = mode;
                    wchar_t bpmText[16]{};
                    ::GetDlgItemTextW(hwnd, IDC_EDT_RHYTHM_BPM, bpmText, 16);
                    const long bpm = std::wcstol(bpmText, nullptr, 10);
                    if (bpm >= 60 && bpm <= 220) {
                        cfg.rhythmBpm = static_cast<double>(bpm);
                    }
                    // v1.3.0-beta3 (bug #2): passage language (VN default) + the
                    // composition method, which follows the IME method already
                    // configured so the games compose Telex/VNI exactly like the IME.
                    HWND langCombo = ::GetDlgItem(hwnd, IDC_CMB_PASSAGE_LANG);
                    const int langSel = (langCombo != nullptr)
                                            ? static_cast<int>(::SendMessageW(langCombo,
                                                                              CB_GETCURSEL, 0, 0))
                                            : 0;
                    cfg.passageLanguage = (langSel == 1)
                                              ? ok::arcade::PassageLanguage::English
                                              : ok::arcade::PassageLanguage::Vietnamese;
                    cfg.vnInputMethod =
                        static_cast<ok::arcade::VnInputMethod>(g.options.inputMethod);
                    // v1.3.0-beta5 (bug B7): steering-key choice (clamped —
                    // CB_ERR/-1 from an untouched combo falls back to Arrows).
                    HWND steerCombo = ::GetDlgItem(hwnd, IDC_CMB_STEERING);
                    const int steerSel = (steerCombo != nullptr)
                                             ? static_cast<int>(::SendMessageW(steerCombo,
                                                                               CB_GETCURSEL, 0, 0))
                                             : 0;
                    cfg.wasdSteering = static_cast<ok::arcade::WasdSteering>(
                        std::clamp(steerSel, 0, 2));
                    ok::arcade::ArcadeManager::instance().setConfig(cfg);
                    // v1.3.0-beta5 (bug B6): PERSIST right away — before this,
                    // the applied config lived only in memory and vanished on
                    // restart unless the user also pressed OK on the dialog.
                    saveSettings();
                    ::MessageBoxW(hwnd,
                                  L"Đã áp dụng + lưu: chế độ Rhythm/No-Mistake, nhịp BPM, "
                                  L"ngôn ngữ đoạn văn và phím lái cho các game.",
                                  L"KieeKey Arcade", MB_OK | MB_ICONINFORMATION);
                    return 0;
                }
                case IDC_CHK_LIVE:
                case IDC_CHK_LIVE_CASE:
                case IDC_CMB_LIVE_GLYPH:
                case IDC_CMB_LIVE_INTENSITY: {
                    const auto notification = HIWORD(wParam);
                    if (notification != BN_CLICKED && notification != CBN_SELCHANGE) { return 0; }
                    ok::effects::Config config;
                    config.enabled = ::IsDlgButtonChecked(hwnd, IDC_CHK_LIVE) == BST_CHECKED;
                    config.randomCase = ::IsDlgButtonChecked(hwnd, IDC_CHK_LIVE_CASE) == BST_CHECKED;
                    const auto mode = ::SendMessageW(::GetDlgItem(hwnd, IDC_CMB_LIVE_GLYPH), CB_GETCURSEL, 0, 0);
                    config.glyph = static_cast<ok::effects::Glyph>(std::clamp<LRESULT>(mode, 0, 3));
                    const auto intensity = ::SendMessageW(::GetDlgItem(hwnd, IDC_CMB_LIVE_INTENSITY), CB_GETCURSEL, 0, 0);
                    config.intensity = intensity == 3 ? 100u : (intensity == 2 ? 75u :
                                     (intensity == 0 ? 25u : 50u));
                    g.liveEffects.configure(config);
                    g.engineResyncPending.store(true, std::memory_order_release);
                    saveSettings();   // v1.3.0-beta5 (bug B9): persist on toggle
                    return 0;
                }
                case IDC_CHK_CHAOS_MASTER:
                case IDC_CHK_CHAOS_CASE:
                case IDC_CHK_GLYPH_TRANSFORM: {
                    auto& chaos = ok::chaos::ChaosEngine::instance();
                    ok::chaos::ChaosConfig cfg = chaos.getConfig();
                    cfg.masterEnabled = (::IsDlgButtonChecked(hwnd, IDC_CHK_CHAOS_MASTER) == BST_CHECKED);
                    cfg.randomCaseEnabled = (::IsDlgButtonChecked(hwnd, IDC_CHK_CHAOS_CASE) == BST_CHECKED);
                    cfg.glyphTransformEnabled = (::IsDlgButtonChecked(hwnd, IDC_CHK_GLYPH_TRANSFORM) == BST_CHECKED);
                    chaos.setConfig(cfg);
                    saveSettings();   // v1.3.0-beta5 (bug B9): persist on toggle
                    return 0;
                }
                case IDC_CHK_AI_OPTIN: {
                    bool opt = (::IsDlgButtonChecked(hwnd, IDC_CHK_AI_OPTIN) == BST_CHECKED);
                    ok::ai::AiRivalEngine::instance().setOptIn(opt);
                    saveSettings();   // v1.3.0-beta5 (bug B9): persist on toggle
                    return 0;
                }
                case IDC_BTN_AI_RESET:
                    ok::ai::AiRivalEngine::instance().resetProfile();
                    return 0;
                case IDC_BTN_PROG_RESET:
                    ok::progression::ProgressionEngine::instance().reset();
                    return 0;
                // ---- v1.3.0-beta4: diagnostics control panel (tab 3) ----
                case IDC_RAD_DIAG_OFF:
                case IDC_RAD_DIAG_BASIC:
                case IDC_RAD_DIAG_FULL: {
                    const ok::diag::Level lvl =
                        (wParam == IDC_RAD_DIAG_OFF) ? ok::diag::Level::Off :
                        (wParam == IDC_RAD_DIAG_FULL) ? ok::diag::Level::Full :
                                                        ok::diag::Level::Basic;
                    setDiagLevelAndSave(lvl);
                    if (HWND r = ::GetDlgItem(hwnd, IDC_STAT_DIAG_RESULT)) {
                        ::SetWindowTextW(r, lvl == ok::diag::Level::Off
                            ? L"Chẩn đoán: TẮT — mọi bộ đếm ngừng (chi phí ~0)"
                            : lvl == ok::diag::Level::Full
                                ? L"Chẩn đoán: ĐẦY ĐỦ — bộ đếm + độ trễ + ghi từng phím "
                                  L"(chi phí: docs/PERFORMANCE.md)"
                                : L"Chẩn đoán: CƠ BẢN — bộ đếm + độ trễ "
                                  L"(chi phí: docs/PERFORMANCE.md)");
                    }
                    return 0;
                }
                case IDC_BTN_DIAG_RUN: {
                    refreshDiagnostics();
                    std::string failDetail;
                    const int passed = runDiagQuickCheck(failDetail);
                    if (HWND r = ::GetDlgItem(hwnd, IDC_STAT_DIAG_RESULT)) {
                        wchar_t buf[160]{};
                        if (passed == 6) {
                            swprintf_s(buf, L"Kiểm tra nhanh: ĐẠT 6/6 hạng mục ✓");
                        } else {
                            swprintf_s(buf, L"Kiểm tra nhanh: %d/6 — lỗi: %hs",
                                       passed, failDetail.c_str());
                        }
                        ::SetWindowTextW(r, buf);
                    }
                    return 0;
                }
                case IDC_BTN_DIAG_REPORT: {
                    std::wstring path;
                    if (exportDiagReport(path)) {
                        if (HWND r = ::GetDlgItem(hwnd, IDC_STAT_DIAG_RESULT)) {
                            std::wstring reportMsg = L"Đã xuất báo cáo: " + path;
                            ::SetWindowTextW(r, reportMsg.c_str());
                        }
                    } else if (HWND r = ::GetDlgItem(hwnd, IDC_STAT_DIAG_RESULT)) {
                        ::SetWindowTextW(r, L"Không ghi được báo cáo (thư mục %APPDATA%?)");
                    }
                    return 0;
                }
                case IDC_BTN_DIAG_COPY: {
                    // v1.3.0-beta7: full refresh before copy, like export.
                    refreshDiagnostics();
                    const std::wstring wide = utf8ToUtf16(
                        ok::diag::Diagnostics::instance().report(40));
                    bool okCopy = false;
                    if (::OpenClipboard(hwnd)) {
                        ::EmptyClipboard();
                        const SIZE_T bytes = (wide.size() + 1) * sizeof(wchar_t);
                        HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
                        if (mem != nullptr) {
                            wchar_t* dst = static_cast<wchar_t*>(::GlobalLock(mem));
                            bool locked = (dst != nullptr);
                            if (locked) {
                                std::memcpy(dst, wide.c_str(), bytes);
                                ::GlobalUnlock(mem);
                            }
                            // SetClipboardData takes OWNERSHIP on success;
                            // free only when it refused the handle.
                            if (locked && ::SetClipboardData(CF_UNICODETEXT, mem) != nullptr) {
                                okCopy = true;
                            } else {
                                ::GlobalFree(mem);
                            }
                        }
                        ::CloseClipboard();
                    }
                    if (HWND r = ::GetDlgItem(hwnd, IDC_STAT_DIAG_RESULT)) {
                        ::SetWindowTextW(r, okCopy
                            ? L"Đã sao chép báo cáo vào clipboard — dán (Ctrl+V) vào báo cáo lỗi"
                            : L"Không mở được clipboard để sao chép báo cáo");
                    }
                    return 0;
                }
                // v1.3.0-beta5 (bug B9): the "Bàn phím" tab live-applies on
                // click. Before this, tab-0 controls only took effect through
                // OK/Apply — a tester who flipped a checkbox and closed the
                // dialog with X (or simply expected the instant feedback the
                // tab-6/7 toggles give) saw "the option does nothing". Same
                // fail-safe read path as OK (dlgChecked fallbacks), same
                // apply-under-lock, plus an immediate persist so what the
                // dialog shows is what restarts.
                case IDC_RADIO_TELEX:
                case IDC_RADIO_VNI:
                case IDC_RADIO_SIMPLETELEX:
                case IDC_CHK_DIGITS:
                case IDC_CHK_SPELL:
                case IDC_CHK_RESTORE:
                case IDC_CHK_QUICK:
                case IDC_CHK_MODERN:
                case IDC_CHK_UPPER:
                case IDC_CHK_MACRO:
                case IDC_RADIO_OUT_AUTO:
                case IDC_RADIO_OUT_TSF:
                case IDC_RADIO_OUT_SEND:
                case IDC_CHK_PERF_LOWCPU:
                case IDC_CHK_PERF_DICT:
                case IDC_CHK_NOTIFY:
                case IDC_CHK_EXCLUDE_IDE:
                case IDC_CHK_EXCLUDE_GAME:
                case IDC_CHK_EXCLUDE_SHELL: {
                    if (HIWORD(wParam) != BN_CLICKED) { return 0; }
                    settingsFromControls();
                    saveSettings();
                    updateTrayIcon();
                    updateHeaderStatus();
                    return 0;
                }
                case IDC_COMBO_CODETABLE:
                case IDC_COMBO_PERF: {
                    if (HIWORD(wParam) != CBN_SELCHANGE) { return 0; }
                    settingsFromControls();
                    saveSettings();
                    updateTrayIcon();
                    updateHeaderStatus();
                    return 0;
                }
                case IDOK:
                    settingsFromControls();
                    saveSettings();
                    updateTrayIcon();
                    [[fallthrough]];
                case IDCANCEL:
                    ::KillTimer(hwnd, 1);
                    ::DestroyWindow(hwnd);
                    g.hSettings = nullptr; g.macroEdit.store(nullptr, std::memory_order_release);
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

        // v1.1.0: the explicit Esc/Enter WM_KEYDOWN handling was REMOVED —
        // the main message loop already routes this window through
        // IsDialogMessage (Tab navigation, Esc→IDCancel, Enter→IDOK), and
        // the double handling raced the code-table combo dropdown across
        // common-control versions.

        case WM_CLOSE:
            ::KillTimer(hwnd, 1);
            ::DestroyWindow(hwnd);
            g.hSettings = nullptr; g.macroEdit.store(nullptr, std::memory_order_release);
            return 0;

        case WM_DESTROY:
            g.hSettings = nullptr; g.macroEdit.store(nullptr, std::memory_order_release);
            // v1.3.0-beta5 (bug B1): drop the scroll/solve baseline — the
            // child HWNDs are dead and the next dialog re-solves from scratch.
            g_settingsScroll.solved.clear();
            g_settingsScroll.viewport = ok::layout::Rect{};
            g_settingsScroll.viewportBottom = 0;
            g_settingsScroll.offset = 0;
            g_settingsScroll.range = 0;
            g_settingsScroll.enabled = false;
            return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

//===========================================================================
// v1.1.2 — the single settings-window creation path (tray menu “Cài đặt…”,
// “Thông tin & giới thiệu”, and the tray double-click all land here).
// Selects the tab to open BEFORE creating the window: settingsToControls()
// reads g_settingsOpenTab and shows exactly that tab (no post-create flicker).
//===========================================================================
void openSettingsDialog(int tab) {
    // v1.1.2-r3: refresh the external-conflict scan on EVERY dialog open —
    // the user may have quit/started EVKey since the last check, and the
    // Information tab must show the current truth.
    {
        const ConflictScan cs = scanConflicts();
        g.conflictWarning = cs.warning;
        g.conflictDetail  = cs.detail;
    }
    if (g.hSettings) {
        // Already open: just bring it up on the requested tab.
        g_settingsOpenTab = tab;
        const int cur = static_cast<int>(::SendMessageW(
            ::GetDlgItem(g.hSettings, IDC_TAB), TCM_GETCURSEL, 0, 0));
        if (cur != tab) {
            ::SendMessageW(::GetDlgItem(g.hSettings, IDC_TAB), TCM_SETCURSEL,
                           static_cast<WPARAM>(tab), 0);
            showTab(tab);
        }
        ::SetForegroundWindow(g.hSettings);
        return;
    }
    if (!g.hInst) { return; }
    g_settingsOpenTab = tab;
    const HWND created = ::CreateWindowExW(0, L"KieeKeySettings",
                                    L"KieeKey — Cài đặt & Thông tin",
                                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                    WS_MINIMIZEBOX,
                                    CW_USEDEFAULT, CW_USEDEFAULT, 572, 622,
                                    nullptr, nullptr, g.hInst, nullptr);
    // v1.1.3: publish the handle ONLY for a real window — a failed
    // creation previously left g.hSettings dangling and every
    // IsDialogMessageW/SetWindowTextW call targeting garbage.
    if (created != nullptr) {
        g.hSettings = created;
        ::ShowWindow(g.hSettings, SW_SHOW);
    }
}

//===========================================================================
// Main (hidden) window
//===========================================================================
LRESULT CALLBACK mainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // v3.5: Explorer restarted → the tray icon is gone. Re-register it so
    // the app never looks "stopped" while it is perfectly alive.
    if (msg == g_msgTaskbarCreated && g_msgTaskbarCreated != 0) {
        restoreTrayIcon();
        return 0;
    }
    switch (msg) {
        case WM_APP_RESTORE:
            // Second-instance wake: our tray icon may have been lost (e.g.
            // Explorer restart). Restore it and confirm visibly.
            restoreTrayIcon();
            showTrayBalloon(L"KieeKey",
                            L"KieeKey đang hoạt động — biểu tượng khay đã được khôi phục.");
            if (g.hSettings) { ::SetForegroundWindow(g.hSettings); }
            return 0;
        case WM_APP_FGPROBE:
            probeForegroundResponsiveness(reinterpret_cast<HWND>(wParam));
            return 0;

        // ---- v1.2.0 Stable: power / session / display lifecycle -----------
        case WM_POWERBROADCAST:
            switch (wParam) {
                case PBT_APMSUSPEND:
                    // About to sleep (or hibernate). Drop the pending word —
                    // no key-up is guaranteed to arrive across the resume.
                    onLifecycleSuspend();
                    return TRUE;
                case PBT_APMRESUMEAUTOMATIC:   // user-present resume
                case PBT_APMRESUME:            // explicit resume
                    onLifecycleResume();
                    return TRUE;
                default:
                    break;
            }
            return TRUE;

        case WM_WTSSESSION_CHANGE:
            // Session lock/unlock, fast-user switching, RDP connect/disconnect:
            // the secure desktop owns the keyboard while we are locked, so
            // every hook callback in that window is lost (same reasoning as a
            // suspend, minus the power transition).
            if (wParam == WTS_SESSION_LOCK || wParam == WTS_SESSION_LOGOFF) {
                onLifecycleSuspend();
            } else if (wParam == WTS_SESSION_UNLOCK || wParam == WTS_SESSION_LOGON) {
                onLifecycleResume();
            }
            return 0;

        case WM_DISPLAYCHANGE:
            // Monitor topology / resolution changed. Re-evaluate the
            // foreground policy (fullscreen-game detection depends on the
            // monitor rect) and, if the settings dialog is open, re-scale it
            // so it cannot be left at the old monitor's DPI.
            g.monitor.refreshNow();
            updateExclusionCache();
            updateForegroundPolicy();
            if (g.hSettings) { refreshSettingsDpi(); }
            return 0;
        case WM_APP_TRAY:
            if (lParam == WM_LBUTTONDBLCLK) {
                openSettingsDialog(0);
            } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                showTrayMenu();
            } else if (lParam == NIN_BALLOONUSERCLICK) {
                onNotificationClicked();          // v1.2.1 RC2
            } else if (lParam == NIN_BALLOONTIMEOUT || lParam == NIN_BALLOONHIDE) {
                g_shownNotification = {};         // nothing to act on any more
            }
            return 0;

        case WM_TIMER:
            if (wParam == 7) { onNotifyTick(); return 0; }   // v1.2.1 RC2
            break;

        case WM_APP_UPDATE_TIP:
            // Tooltip-only refresh (foreground change / exclusion hint):
            // deliberately does NOT persist settings — this fires on every
            // app switch (see the producer's ForegroundChanged path).
            updateTrayIcon();
            return 0;

        case WM_ENDSESSION:
            // v1.1.0-audit fix: WM_ENDSESSION arrives with wParam == FALSE
            // when a logoff/shutdown is CANCELED (another app vetoed in its
            // WM_QUERYENDSESSION; our DefWindowProc answers TRUE so we are
            // on the notify list). Stopping the hook/monitor in that case
            // silently killed the IME while the session continued — the
            // classic "KieeKey suddenly stopped typing". Only tear down on
            // a REAL end-session.
            if (wParam != FALSE) {
                // v1.1.1: persist the CURRENT state/settings before teardown
                // so a logoff can never leave stale values behind.
                saveSettings();
                g.hook.stop();      // consumer finalizer runs composer.detach()
                g.monitor.stop();
                // v1.1.3: end the process here. Previously the teardown left
                // a LIVE tray icon over a DEAD IME (no hook, no restart path
                // anywhere) if the session-end raced or was aborted after our
                // TRUE — the "tray icon there but nothing types" trap. The
                // session is ending; a clean quit is always correct.
                NOTIFYICONDATAW nidEnd{};
                nidEnd.cbSize = sizeof(nidEnd);
                nidEnd.hWnd   = hwnd;
                nidEnd.uID    = 1;
                ::Shell_NotifyIconW(NIM_DELETE, &nidEnd);
                ::PostQuitMessage(0);
            }
            return 0;

        case WM_DESTROY:
            // v1.2.0: stop listening for session notifications — the window
            // is going away and the notification must not be re-routed.
            (void)::WTSUnRegisterSessionNotification(hwnd);
            // v1.1.1: final persistence sweep on the clean-exit path — every
            // change point already saves; this guarantees the registry always
            // mirrors the last UI state the user saw (restart-proof).
            saveSettings();
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
    // v3.5: shell restart notification for tray-icon resurrection.
    g_msgTaskbarCreated = ::RegisterWindowMessageW(L"TaskbarCreated");

    // v1.3.0-beta7: capture process start tick for uptime (must be before any snapshot)
    g_startTickMs = ::GetTickCount64();

    // v1.3.0-beta5 (bug B8): register the Chaos Lab emitter once at boot. The
    // Arcade Hub sidebar entry opens the lab through the launchChaosLab()
    // façade (src/core/ArcadeHubLaunch.hpp), which does not pass through this
    // translation unit — without a boot-time registration its "gõ thật vào
    // app" button would have no emitter when the lab was opened from the hub.
    ok::app::ChaosLabWindow::setEmitCallback(&emitForChaosLab);

    // Single instance — WITH a takeover path for a hung earlier process.
    // History: an earlier build could leave a zombie holding this mutex (a
    // consumer wedged in a synchronous TSF edit session + an unbounded
    // shutdown join), and every relaunch then reported "already running"
    // until a reboot. Three-step protocol now:
    //   1) an earlier instance exists → ask it to restore its tray icon
    //      (the usual reason people relaunch is a LOST TRAY ICON);
    //   2) if its UI thread answers a bounded ping → it is healthy, done;
    //   3) if NOT → it is a hung zombie: terminate it (PID + image-name
    //      validated) and acquire the singleton ourselves.
    HANDLE mutex = ::CreateMutexW(nullptr, TRUE, kSingletonMutexName);
    if (mutex && ::GetLastError() == ERROR_ALREADY_EXISTS) {
        signalRunningInstance();
        if (runningInstanceResponsive()) {
            ::CloseHandle(mutex);
            return 0;
        }
        terminateStaleInstance();
        ::CloseHandle(mutex);
        mutex = ::CreateMutexW(nullptr, TRUE, kSingletonMutexName);
        if (mutex && ::GetLastError() == ERROR_ALREADY_EXISTS) {
            ::MessageBoxW(nullptr, L"KieeKey đang chạy (xem khay hệ thống).",
                          L"KieeKey", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
    }
    if (mutex) { writeRunningPid(); }
    // (If CreateMutexW itself failed we still run; only the cross-instance
    //  lock is unavailable — same policy as previous versions.)

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
    // v1.3.0: the Chaos Lab window uses a trackbar (TRACKBAR_CLASSW), which
    // lives in the common-controls bar class; without ICC_BAR_CLASSES the
    // control is not registered and the lab comes up without its sliders.
    icc.dwICC  = ICC_TAB_CLASSES | ICC_BAR_CLASSES;
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
    // v1.3.0-beta4: apply the persisted diagnostics level BEFORE the hook
    // installs, so the recorders observe the user's choice from the first
    // keystroke (default Basic; Off/Full come from diag-level.txt).
    ok::diag::Diagnostics::instance().setLevel(loadDiagLevel());
    loadMacros();   // v1.1.0: real macro table (%APPDATA%\KieeKey\macros.txt)
    {
        std::lock_guard<std::mutex> lk(g.engineMtx);
        g.engine.setOptions(g.options);
        // v1.1.0: wire the macro resolver — the "Gõ tắt" feature was a
        // silent no-op in 1.0.x (no resolver was ever installed).
        g.engine.setMacroResolver(
            [](const std::vector<std::uint32_t>& key,
               std::vector<std::uint32_t>& data) {
                return g_macros.find(key, data);
            });
    }
    // v1.1.0: restore the persisted Vietnamese on/off state (OpenKey parity;
    // every 1.0.x relaunch started enabled regardless of the last state).
    g.engineEnabled.store(g.enabledOnStart, std::memory_order_relaxed);
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
    if (!g.hMain) {
        // v1.2.0 Stable: leave no stale single-instance state behind. The old
        // early return skipped clearRunningPid(), so the registry kept
        // advertising a PID that is gone — the next launch's zombie-takeover
        // probe then had a dead PID to investigate.
        clearRunningPid();
        if (mutex) { ::ReleaseMutex(mutex); ::CloseHandle(mutex); }
        ::timeEndPeriod(1);
        return 1;
    }

    // v1.2.0 Stable: receive WM_WTSSESSION_CHANGE (lock/unlock, fast-user
    // switching, RDP connect/disconnect). Without this the IME never learns
    // that the secure desktop owned the keyboard, so every modifier key-up
    // released while locked stayed "held" in the delta tracker.
    // Best effort — on an OS/edition without the API the window simply never
    // receives the message.
    (void)::WTSRegisterSessionNotification(g.hMain, NOTIFY_FOR_THIS_SESSION);

    // Process monitor (foreground detection, zero idle CPU)
    // v1.2.2 RC4 (P2-2): surface a failed start. Auto-exclusion silently off
    // for the whole session used to be invisible; the balloon notifies once
    // when the UI is up (the notify center holds it until the tray exists).
    if (!g.monitor.start()) {
        g.notify.raise(ok::notify::Id::AutoExcludeUnavailable, 90, 0, ::GetTickCount64());
    }
    g.monitor.refreshNow();
    updateExclusionCache();
    updateForegroundPolicy();
    // v1.2.1 RC2: apply the persisted performance profile to every runtime
    // knob before the first keystroke; load persisted notification
    // suppressions ("Don't show again") from the registry.
    g.notify.setStore(makeNotifyStore());
    g.notify.loadSuppressions();
    applyPerfStrategy(/*lockEngine=*/true, /*force=*/true);
    // Adaptive re-resolution + notification polling: a 1 s timer on the
    // hidden main window (UI thread). One relaxed load when nothing is
    // pending — no per-key cost anywhere.
    ::SetTimer(g.hMain, 7, 1000, nullptr);

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
        // v1.2.0 Stable: same hygiene as the other early return — no stale
        // PID, no held mutex, no raised timer resolution.
        clearRunningPid();
        if (mutex) { ::ReleaseMutex(mutex); ::CloseHandle(mutex); }
        ::timeEndPeriod(1);
        return 2;
    }

    // v1.2.0 Stable: the pipeline is starting from a known-quiet state.
    // Defensive against a future restart path that reuses this object after
    // edits were published (see PendingEditCounter::forceQuiesce's contract).
    g.pendingEdits.forceQuiesce();
    // v1.3.0-beta7: seed the diagnostics snapshot so the first report after startup is not all zeros
    refreshDiagnostics();

    // v1.1.2-r3: scan for external digit-conversion causes (other IMEs,
    // Windows Vietnamese Telex/VNI layouts) BEFORE the welcome balloon so
    // the balloon can carry the verdict.
    {
        const ConflictScan cs = scanConflicts();
        g.conflictWarning = cs.warning;
        g.conflictDetail  = cs.detail;
    }

    addTrayIcon();

    // v3.5: serve second-instance wake signals (restore tray + balloon).
    g_wakeExitEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_wakeExitEvent != nullptr) {
        try {
            g_wakeWatcher = std::thread(wakeWatcherMain);
        } catch (...) {
            ::CloseHandle(g_wakeExitEvent);
            g_wakeExitEvent = nullptr;
        }
    }

    // v1.3.0: command-line switches for the graphical surfaces, so the hub can
    // be opened straight from a shortcut or a script:
    //   KieeKey.exe --arcade[=slug]   open the Arcade Hub window
    //   KieeKey.exe --chaos-lab       open the Chaos / Flexing lab window
    //   KieeKey.exe --settings[=N]    open the settings dialog on tab N
    {
        int argumentCount = 0;
        LPWSTR* arguments = ::CommandLineToArgvW(::GetCommandLineW(), &argumentCount);
        if (arguments != nullptr) {
            for (int i = 1; i < argumentCount; ++i) {
                const std::wstring argument = arguments[i];
                if (argument.rfind(L"--arcade", 0) == 0) {
                    std::string slug;
                    const std::size_t equals = argument.find(L'=');
                    if (equals != std::wstring::npos) {
                        // v1.3.0: utf16ToUtf8() instead of
                        // `slug.assign(value.begin(), value.end())`. Copying a
                        // wchar_t range into a narrow string makes the STL
                        // assign `char = const wchar_t` inside <xutility>, which
                        // MSVC /W4 reports as C4244 *in the header* — under /WX
                        // that is C2220 and it points at the STL, not at this
                        // line. Converting explicitly keeps the diagnostic
                        // meaningful and the slug correct for non-ASCII input.
                        slug = utf16ToUtf8(argument.substr(equals + 1));
                    }
                    openArcadeHub(slug.empty() ? nullptr : slug.c_str());
                } else if (argument == L"--chaos-lab") {
                    openChaosLab();
                } else if (argument.rfind(L"--settings", 0) == 0) {
                    int tab = 0;
                    const std::size_t equals = argument.find(L'=');
                    if (equals != std::wstring::npos) {
                        // std::wcstol reads the wide text directly, so no
                        // narrowing conversion happens at all (same C4244 class
                        // as above: the iterator-pair string constructor).
                        tab = static_cast<int>(
                            std::wcstol(argument.c_str() + equals + 1, nullptr, 10));
                    }
                    openSettingsDialog(tab);
                }
            }
            ::LocalFree(arguments);
        }
    }

    // Message loop (IsDialogMessage gives the settings window Tab/Enter/Esc
    // navigation while it is open)
    MSG msg;
    // v1.1.3: GetMessage returns -1 on error — the classic `> 0` loop then
    // fell into the FULL shutdown path on a transient error (IME gone, tray
    // icon gone). Treat -1 as "skip this message, keep running".
    BOOL gmRet = 0;
    while ((gmRet = ::GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        if (g.hSettings && ::IsDialogMessageW(g.hSettings, &msg)) { continue; }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    // v3.5: stop the wake watcher BEFORE the bounded hook shutdown so the
    // final WM_APP_RESTORE can never race a dying window.
    g_appExiting.store(true, std::memory_order_relaxed);
    if (g_wakeExitEvent != nullptr) { ::SetEvent(g_wakeExitEvent); }
    if (g_wakeWatcher.joinable()) { g_wakeWatcher.join(); }
    if (g_wakeExitEvent != nullptr) {
        ::CloseHandle(g_wakeExitEvent);
        g_wakeExitEvent = nullptr;
    }

    g.hook.stop();      // BOUNDED — a wedged consumer can no longer hang the exit
    g.monitor.stop();
    clearRunningPid();
    // v1.1.0: if shutdown had to abandon a wedged worker, terminate the
    // process NOW instead of running static destruction over a detached
    // thread (it still touches the queue/handles when its blocked COM call
    // finally returns). ExitProcess kills every thread atomically before
    // any teardown code runs — no teardown race, no zombie.
    if (g.hook.stuckThreadsDetached() || g.monitor.stuckThreadsDetached()) {
        ::ExitProcess(0);
    }
    ::timeEndPeriod(1); // match timeBeginPeriod above (other exits: the OS
                        // reverts the resolution when the process dies)
    if (mutex) { ::ReleaseMutex(mutex); ::CloseHandle(mutex); }
    return static_cast<int>(msg.wParam);
}
