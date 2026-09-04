//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.0 - refactored and completed logic
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
// KieeKey v1.2.0 — src/app/main.cpp
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

// v1.2.0 Stable: WM_POWERBROADCAST event codes. The PBT_* set is versioned by
// _WIN32_WINNT in some SDK/MinGW header combinations, so the two this file
// uses are defaulted rather than assumed (the values are fixed by the
// Windows ABI — they cannot change between SDKs).
#ifndef PBT_APMSUSPEND
#define PBT_APMSUSPEND 0x0004
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
constexpr wchar_t kAppVersion[]     = L"1.2.0";           // numeric, 3-part
constexpr wchar_t kAppVersionFull[] = L"1.2.0 Stable";    // with channel
constexpr wchar_t kAppTitle[]       = L"KieeKey v1.2.0 Stable";  // sync with kAppVersionFull

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

void writeMacrosFile(const std::wstring& text) {
    const std::wstring path = macroFilePath();
    if (path.empty()) { return; }
    std::ofstream out(path.c_str(), std::ios::binary);
    if (out) {
        const std::string utf8 = utf16ToUtf8(text);
        out << "\xEF\xBB\xBF" << utf8;
    }
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
        // v1.1.2: digits-are-numbers option (the fix for "typing a number
        // produced a tone mark / changed the word").
        key.setDword(L"DigitsLiteral",     g.options.digitsAreLiteral ? 1 : 0);
        // v1.1.2-r2: keep the settings-schema version self-maintaining (the
        // migration in loadSettings() has always run first at startup; this
        // makes the marker explicit on every persist anyway).
        key.setDword(L"SettingsMigration", 2);
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
    if (!g.fgUseTsf_.load(std::memory_order_relaxed)) { return false; }
    OutputItem it;
    it.kind = OutputItem::Kind::Resync;
    return g.outRing.try_push(it);   // false if full — degraded only
}

// Producer handler: runs on the hook pump thread (serialized). Returns the
// hook decision — whether to swallow the key AND whether the consumer thread
// has queued work that needs a wake (see ModernKeyHook::ProducerDecision).
using PD = ok::hook::ModernKeyHook::ProducerDecision;

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
    try {
        return onHookEventImpl(ev);
    } catch (...) {
        // Counted (MemoryFailPoint-style degradation), never fatal. Taking
        // g.engineMtx here could deadlock if the thrower owns it, so the
        // repair is deferred to the next key event instead.
        g.producerFailures.fetch_add(1, std::memory_order_relaxed);
        g.engineResyncPending.store(true, std::memory_order_release);
        return PD{};   // pass-through — the keystroke is never lost
    }
}

PD onHookEventImpl(const KeyEvent& ev) {
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

    // v1.2.0 Stable: repair after a producer-side fault (see onHookEvent).
    // The previous event threw after the engine had already consumed the
    // key, so the engine's buffer and the visible text disagree; dropping
    // the pending word is the only safe way back to a known-good state.
    if (g.engineResyncPending.exchange(false, std::memory_order_acq_rel)) {
        std::lock_guard<std::mutex> lk(g.engineMtx);
        g.engine.startNewSession();
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
        if (g.fgExcluded_.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lk(g.engineMtx);
            g.engine.startNewSession();
        }
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

    g.keysTyped.fetch_add(1, std::memory_order_relaxed);   // WPM gauge
    // v1.1.3: pick up in-window layout switches (Win+Space etc.) between
    // words — a space or navigation key always precedes the next word.
    if (in.kind == InputKind::Space || in.kind == InputKind::WordBreak) {
        refreshLayoutCache();
    }

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
        // v1.1.3 double-check: the enabled flag is re-read under the lock so
        // a toggle that lands between the early-return and this point cannot
        // emit an edit AFTER the OFF transition's drain (the drain above sees
        // a quiesced queue; an in-flight decision must not re-arm it).
        if (!g.engineEnabled.load(std::memory_order_relaxed)) {
            waitPendingEditsDrained();
            return PD{false, false};   // disabled mid-stroke — pass through
        }
        const EngineResult& r = g.engine.process(in);
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
        if (digitLiteralEvent) {
            g.digitGuardHits.fetch_add(1, std::memory_order_relaxed);
            g.engine.startNewSession();   // screen now shows the raw digit
            g.repScratch.clear();         // guarded: no replacement to apply
        } else {
            g.engine.replacementUtf16(r, g.repScratch);   // scratch — no per-key alloc
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
        reissueTyped = !digitLiteralEvent &&
                       (r.code == EngineCode::Restore ||
                        r.code == EngineCode::RestoreAndStartNewSession) &&
                       (in.kind == InputKind::Char || in.kind == InputKind::Space);
        if (digitLiteralEvent) {
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
        if (g.inlineDeferred && g.repScratch.size() <= kRingTextCap) {
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
inline constexpr std::size_t kMaxEditBatch = 32;
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
        try {
            batchOk = g.composer.commitBatch(g_editBatch, &appliedCountRead);
        } catch (...) {
            batchOk = false;
            appliedCountRead = 0;   // unknown progress — re-deliver the batch
        }
        if (batchOk) {
            // v1.1.0 — TSF slow-commit watchdog: a synchronous commit that
            // took ≥ 100 ms means the foreground app's STA is starving (the
            // pre-hang symptom of "KieeKey suddenly stops"). Degrade THIS
            // foreground to inline SendInput (which can never block) until
            // the next app switch re-evaluates the policy.
            if (g.composer.lastCommitSlow()) {
                g.fgUseTsf_.store(false, std::memory_order_relaxed);
                g.tsfSlowCount.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            // Last-resort fallback: synthetic backspaces + Unicode text.
            // v1.1.3 (T3): TSF sessions are NOT transactional — deltas the
            // session already applied are IN the document. Re-emitting the
            // whole batch duplicated word-initial pure-insert deltas ("t",
            // then "to" over it -> "tto"). Deliver only the UNAPPLIED suffix.
            std::size_t applied = 0;
            if (appliedCountRead) { applied = appliedCountRead; }
            for (std::size_t i = applied; i < g_editBatch.size(); ++i) {
                const ok::tsf::EditDelta& d = g_editBatch[i];
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
                continue;
            }
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
    return s;
}

//===========================================================================
// Tray icon + context menu
//===========================================================================
// v1.1.0: shared tooltip builder — includes the version and, when the
// foreground app is auto-excluded, WHICH app the engine is paused in (the
// old tip gave no hint, so users reported "KieeKey suddenly stopped").
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
                g.engine.startNewSession();
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
        g.engine.setOptions(g.options);
        g.engine.startNewSession();
        g.monitor.setExcludeIde(g.exclIde);
        g.monitor.setExcludeGame(g.exclGame);
        g.monitor.setExcludeShell(g.exclShell);
        updateExclusionCache();
        updateForegroundPolicy();   // output mode affects the TSF-vs-inline decision
    }
    // Disk write OUTSIDE engineMtx: never let file I/O extend the window in
    // which the hook thread's keystroke path can be blocked.
    if (edit != nullptr) {
        writeMacrosFile(macroText);
        g_macroFileRaw = macroText;   // editor text is now the file content
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
    updateHeaderStatus();
    showTab(g_settingsOpenTab);
}

void showTab(int tab) {
    // Every control (including static labels) belongs to exactly one tab;
    // toggle visibility so only the active tab's controls are shown.
    // (v3.0 bugfix: labels previously had no IDs and were never hidden —
    //  all three tabs' labels were drawn overlapping each other.)
    // v1.1.2: the header (icon/title/status) and the bottom button row are
    // NOT tab-assigned — they are always visible chrome.
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
        0
    };
    static constexpr int kTab1[] = {
        IDC_STAT_APPS_TITLE, IDC_STAT_APPS_NOTE,
        IDC_CHK_EXCLUDE_IDE, IDC_CHK_EXCLUDE_GAME, IDC_CHK_EXCLUDE_SHELL, 0
    };
    // v1.1.0: tab 2 is the macro editor ("Gõ tắt"); diagnostics moved to 3.
    static constexpr int kTab2[] = {
        IDC_STAT_MACRO_HINT, IDC_EDIT_MACRO, 0
    };
    static constexpr int kTab3[] = {
        IDC_STAT_LATLAB, IDC_STAT_AVGLAB, IDC_STAT_PUSHLAB, IDC_STAT_DROPLAB,
        IDC_STAT_WPMLAB, IDC_STAT_DESC,
        IDC_STAT_LATVAL, IDC_STAT_AVGVAL, IDC_STAT_PUSHV, IDC_STAT_DROPV,
        IDC_STAT_WPMVAL,
        IDC_STAT_BARRIERLAB, IDC_STAT_BARRIERV,
        IDC_STAT_REINSTLAB, IDC_STAT_REINSTV,
        IDC_STAT_TSFLAB, IDC_STAT_TSFV,
        IDC_STAT_APPLAB, IDC_STAT_APPV, 0
    };
    // v1.1.2: tab 4 — Information (introduces the app inside the app).
    static constexpr int kTab4[] = {
        IDC_STAT_INFO_NAME, IDC_STAT_INFO_STATUS, IDC_STAT_INFO_ABOUT,
        IDC_STAT_INFO_FEAT, IDC_STAT_INFO_GUIDE, IDC_STAT_INFO_LICENSE,
        IDC_LNK_REPO, 0
    };
    if (!g.hSettings) { return; }
    for (const int* p = kTab0; *p; ++p) { ::ShowWindow(::GetDlgItem(g.hSettings, *p), tab == 0 ? SW_SHOW : SW_HIDE); }
    for (const int* p = kTab1; *p; ++p) { ::ShowWindow(::GetDlgItem(g.hSettings, *p), tab == 1 ? SW_SHOW : SW_HIDE); }
    for (const int* p = kTab2; *p; ++p) { ::ShowWindow(::GetDlgItem(g.hSettings, *p), tab == 2 ? SW_SHOW : SW_HIDE); }
    for (const int* p = kTab3; *p; ++p) { ::ShowWindow(::GetDlgItem(g.hSettings, *p), tab == 3 ? SW_SHOW : SW_HIDE); }
    for (const int* p = kTab4; *p; ++p) { ::ShowWindow(::GetDlgItem(g.hSettings, *p), tab == 4 ? SW_SHOW : SW_HIDE); }
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
                RECT rc{0, 0, S(560), S(608)};
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
                             S(12), S(66), S(536), S(492), reinterpret_cast<HMENU>(IDC_TAB));
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            wchar_t t0[] = L"Bàn phím";
            wchar_t t1[] = L"Ứng dụng";
            wchar_t t2[] = L"Gõ tắt";
            wchar_t t3[] = L"Chẩn đoán";
            wchar_t t4[] = L"Thông tin";
            item.pszText = t0; ::SendMessageW(tab, TCM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));
            item.pszText = t1; ::SendMessageW(tab, TCM_INSERTITEMW, 1, reinterpret_cast<LPARAM>(&item));
            item.pszText = t2; ::SendMessageW(tab, TCM_INSERTITEMW, 2, reinterpret_cast<LPARAM>(&item));
            item.pszText = t3; ::SendMessageW(tab, TCM_INSERTITEMW, 3, reinterpret_cast<LPARAM>(&item));
            item.pszText = t4; ::SendMessageW(tab, TCM_INSERTITEMW, 4, reinterpret_cast<LPARAM>(&item));

            // ---- tab 0: Bàn phím (v1.1.2: grouped layout + digits option) ----
            mkCtl(hwnd, L"BUTTON", L"Phương thức gõ",
                  WS_CHILD | WS_VISIBLE | BS_GROUPBOX, S(24), S(86), S(494), S(76),
                  reinterpret_cast<HMENU>(IDC_GRP_METHOD));
            mkCtl(hwnd, L"BUTTON", L"Telex", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  S(44), S(108), S(74), S(20), reinterpret_cast<HMENU>(IDC_RADIO_TELEX));
            mkCtl(hwnd, L"BUTTON", L"VNI", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  S(128), S(108), S(58), S(20), reinterpret_cast<HMENU>(IDC_RADIO_VNI));
            mkCtl(hwnd, L"BUTTON", L"Simple Telex", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  S(196), S(108), S(112), S(20), reinterpret_cast<HMENU>(IDC_RADIO_SIMPLETELEX));
            mkCtl(hwnd, L"STATIC",
                  L"Telex & Simple Telex gõ dấu bằng chữ (as → á). VNI gõ dấu bằng số "
                  L"(a1 → á) — chỉ khi tùy chọn chữ số bên dưới đang TẮT.",
                  WS_CHILD | WS_VISIBLE, S(44), S(132), S(460), S(26),
                  reinterpret_cast<HMENU>(IDC_STAT_METHOD_HINT));
            mkCtl(hwnd, L"STATIC", L"Bảng mã:", WS_CHILD | WS_VISIBLE, S(28), S(172), S(90), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_CODETABLE));
            HWND combo = mkCtl(hwnd, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                               CBS_DROPDOWNLIST, S(128), S(168), S(210), S(200), reinterpret_cast<HMENU>(IDC_COMBO_CODETABLE));
            for (const wchar_t* s : {L"Unicode", L"TCVN3 (ABC)", L"VNI Windows",
                                     L"Unicode tổ hợp", L"CP 1258"}) {
                ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
            }
            mkCtl(hwnd, L"BUTTON", L"Tùy chọn gõ",
                  WS_CHILD | WS_VISIBLE | BS_GROUPBOX, S(24), S(196), S(494), S(180),
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
            mkCtl(hwnd, L"BUTTON", L"Chế độ xuất",
                  WS_CHILD | WS_VISIBLE | BS_GROUPBOX, S(24), S(386), S(494), S(160),
                  reinterpret_cast<HMENU>(IDC_GRP_OUTPUT));
            mkCtl(hwnd, L"BUTTON", L"Auto", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  S(44), S(408), S(66), S(20), reinterpret_cast<HMENU>(IDC_RADIO_OUT_AUTO));
            mkCtl(hwnd, L"BUTTON", L"Luôn TSF (chống nháy)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  S(118), S(408), S(180), S(20), reinterpret_cast<HMENU>(IDC_RADIO_OUT_TSF));
            mkCtl(hwnd, L"BUTTON", L"Luôn SendInput (nhanh nhất)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  S(44), S(432), S(240), S(20), reinterpret_cast<HMENU>(IDC_RADIO_OUT_SEND));
            mkCtl(hwnd, L"STATIC",
                  L"Auto: TSF cho trình duyệt & Office (không nháy chữ), SendInput trực tiếp "
                  L"cho các ứng dụng khác (nhanh nhất, không qua thread phụ). "
                  L"Khuyến nghị: giữ Auto — ứng dụng tự chọn đường xuất tốt nhất.",
                  WS_CHILD | WS_VISIBLE, S(44), S(458), S(460), S(62),
                  reinterpret_cast<HMENU>(IDC_STAT_OUT_NOTE));

            // ---- tab 1: Ứng dụng ----
            mkCtl(hwnd, L"STATIC", L"Tự động tắt bộ gõ khi cửa sổ đang chạy là:",
                  WS_CHILD | WS_VISIBLE, S(28), S(96), S(470), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_APPS_TITLE));
            mkCtl(hwnd, L"BUTTON", L"IDE / Editor (VS Code, Visual Studio, CLion…)",
                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, S(28), S(126), S(480), S(20),
                  reinterpret_cast<HMENU>(IDC_CHK_EXCLUDE_IDE));
            mkCtl(hwnd, L"BUTTON", L"Trò chơi toàn màn hình (DirectX / Vulkan / OpenGL)",
                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, S(28), S(152), S(480), S(20),
                  reinterpret_cast<HMENU>(IDC_CHK_EXCLUDE_GAME));
            mkCtl(hwnd, L"BUTTON", L"Windows Shell (Explorer, Terminal, CMD)",
                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, S(28), S(178), S(480), S(20),
                  reinterpret_cast<HMENU>(IDC_CHK_EXCLUDE_SHELL));
            mkCtl(hwnd, L"STATIC",
                  L"Lưu ý: tắt loại trừ Shell để gõ tên file tiếng Việt trong "
                  L"Explorer. Việc phát hiện cửa sổ là theo sự kiện (WinEvent), "
                  L"không tốn CPU khi rảnh. Khi bộ gõ tự tắt, trạng thái hiện ngay "
                  L"trên dòng đầu cửa sổ và tooltip khay hệ thống.",
                  WS_CHILD | WS_VISIBLE, S(28), S(210), S(480), S(64),
                  reinterpret_cast<HMENU>(IDC_STAT_APPS_NOTE));

            // ---- tab 2: Gõ tắt (v1.1.0 — the macro feature is now real) ----
            mkCtl(hwnd, L"STATIC",
                  L"Mỗi dòng một từ gọn:  từgọn=kết quả   (VD: cn=chào, hcm=Hồ Chí Minh). "
                  L"Dòng # là ghi chú. Gõ từ gọn rồi nhấn Space để mở rộng.",
                  WS_CHILD | WS_VISIBLE, S(28), S(96), S(480), S(40),
                  reinterpret_cast<HMENU>(IDC_STAT_MACRO_HINT));
            mkCtl(hwnd, L"EDIT", L"",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_BORDER |
                  ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                  S(28), S(142), S(480), S(396),
                  reinterpret_cast<HMENU>(IDC_EDIT_MACRO));

            // ---- tab 3: Chẩn đoán ----
            mkCtl(hwnd, L"STATIC", L"Độ trễ đỉnh hook → xử lý (µs):", WS_CHILD | WS_VISIBLE,
                  S(28), S(96), S(230), S(18), reinterpret_cast<HMENU>(IDC_STAT_LATLAB));
            mkCtl(hwnd, L"STATIC", L"—", WS_CHILD | WS_VISIBLE, S(270), S(96), S(230), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_LATVAL));
            mkCtl(hwnd, L"STATIC", L"Độ trễ trung bình (µs):", WS_CHILD | WS_VISIBLE,
                  S(28), S(122), S(230), S(18), reinterpret_cast<HMENU>(IDC_STAT_AVGLAB));
            mkCtl(hwnd, L"STATIC", L"—", WS_CHILD | WS_VISIBLE, S(270), S(122), S(230), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_AVGVAL));
            mkCtl(hwnd, L"STATIC", L"Sự kiện bàn phím đã xử lý:", WS_CHILD | WS_VISIBLE,
                  S(28), S(148), S(230), S(18), reinterpret_cast<HMENU>(IDC_STAT_PUSHLAB));
            mkCtl(hwnd, L"STATIC", L"0", WS_CHILD | WS_VISIBLE, S(270), S(148), S(230), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_PUSHV));
            mkCtl(hwnd, L"STATIC", L"Sự kiện bị bỏ (hàng đợi đầy):", WS_CHILD | WS_VISIBLE,
                  S(28), S(174), S(230), S(18), reinterpret_cast<HMENU>(IDC_STAT_DROPLAB));
            mkCtl(hwnd, L"STATIC", L"0", WS_CHILD | WS_VISIBLE, S(270), S(174), S(230), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_DROPV));
            mkCtl(hwnd, L"STATIC", L"Tốc độ gõ (ký tự/phút, ≈ WPM × 5):", WS_CHILD | WS_VISIBLE,
                  S(28), S(200), S(240), S(18), reinterpret_cast<HMENU>(IDC_STAT_WPMLAB));
            mkCtl(hwnd, L"STATIC", L"—", WS_CHILD | WS_VISIBLE, S(270), S(200), S(230), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_WPMVAL));
            // v1.1.0 telemetry: barrier timeouts, self-healing reinstalls, TSF
            // slow commits, and the live per-app state.
            mkCtl(hwnd, L"STATIC", L"Chờ hàng đợi quá hạn (barrier):", WS_CHILD | WS_VISIBLE,
                  S(28), S(226), S(230), S(18), reinterpret_cast<HMENU>(IDC_STAT_BARRIERLAB));
            mkCtl(hwnd, L"STATIC", L"0", WS_CHILD | WS_VISIBLE, S(270), S(226), S(230), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_BARRIERV));
            mkCtl(hwnd, L"STATIC", L"Lần tự phục hồi hook:", WS_CHILD | WS_VISIBLE,
                  S(28), S(252), S(230), S(18), reinterpret_cast<HMENU>(IDC_STAT_REINSTLAB));
            mkCtl(hwnd, L"STATIC", L"0", WS_CHILD | WS_VISIBLE, S(270), S(252), S(230), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_REINSTV));
            mkCtl(hwnd, L"STATIC", L"Commit TSF chậm (đã hạ cấp):", WS_CHILD | WS_VISIBLE,
                  S(28), S(278), S(230), S(18), reinterpret_cast<HMENU>(IDC_STAT_TSFLAB));
            mkCtl(hwnd, L"STATIC", L"0", WS_CHILD | WS_VISIBLE, S(270), S(278), S(230), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_TSFV));
            mkCtl(hwnd, L"STATIC", L"Ứng dụng hiện tại:", WS_CHILD | WS_VISIBLE,
                  S(28), S(304), S(230), S(18), reinterpret_cast<HMENU>(IDC_STAT_APPLAB));
            mkCtl(hwnd, L"STATIC", L"—", WS_CHILD | WS_VISIBLE, S(270), S(304), S(230), S(18),
                  reinterpret_cast<HMENU>(IDC_STAT_APPV));
            mkCtl(hwnd, L"STATIC",
                  L"Kiến trúc: hàng đợi lock-free SPSC; hook thread không bao giờ bị chặn; "
                  L"quyết định bộ gõ chạy ngay trên hook thread; xuất qua TSF hoặc SendInput "
                  L"trực tiếp (không Backspace giả, không clipboard). Đỉnh độ trễ tính từ "
                  L"lúc mở hộp thoại.",
                  WS_CHILD | WS_VISIBLE, S(28), S(338), S(480), S(90),
                  reinterpret_cast<HMENU>(IDC_STAT_DESC));

            // ---- tab 4: Thông tin (v1.1.2 — in-app introduction) ----
            // v1.1.2-r3: the tagline merged into the about paragraph; its
            // slot is now the LIVE DIAGNOSTICS block (running build + state
            // + conflict verdict — the "why do my digits still convert"
            // answer, refreshed every timer tick).
            {
                HWND n = mkCtl(hwnd, L"STATIC", kAppTitle,
                               WS_CHILD | WS_VISIBLE, S(28), S(96), S(400), S(34),
                               reinterpret_cast<HMENU>(IDC_STAT_INFO_NAME));
                ::SendMessageW(n, WM_SETFONT, reinterpret_cast<WPARAM>(uiFontTitle()), TRUE);
            }
            mkCtl(hwnd, L"STATIC", L"",
                  WS_CHILD | WS_VISIBLE, S(28), S(134), S(500), S(64),
                  reinterpret_cast<HMENU>(IDC_STAT_INFO_STATUS));
            mkCtl(hwnd, L"STATIC",
                  L"Bộ gõ tiếng Việt hiện đại cho Windows — nhanh, chính xác, "
                  L"ổn định. KieeKey chạy nền trong khay, giúp gõ tiếng Việt có "
                  L"dấu trong mọi ứng dụng (Word, Chrome, VS Code, game…). Lõi "
                  L"gõ hiện đại hoá từ OpenKey: quyết định gõ chạy ngay trên "
                  L"hook thread, xuất chữ trực tiếp qua TSF/SendInput — không "
                  L"clipboard, không chữ nháy. Bật/tắt ngay trong ứng dụng.",
                  WS_CHILD | WS_VISIBLE, S(28), S(206), S(500), S(88),
                  reinterpret_cast<HMENU>(IDC_STAT_INFO_ABOUT));
            mkCtl(hwnd, L"STATIC",
                  L"Tính năng chính:\n"
                  L"• Ba phương thức gõ Telex · VNI · Simple Telex và 5 bảng mã phổ biến\n"
                  L"• Số 0–9 luôn gõ ra chữ số — hết cảnh gõ số bị thành dấu tiếng Việt\n"
                  L"• Gõ tắt (macro) nhiều dòng, tự lưu vào %APPDATA%\\KieeKey\\macros.txt\n"
                  L"• Tự tắt trong IDE và game toàn màn hình để không vướng thao tác\n"
                  L"• Chống nháy chữ qua TSF cho trình duyệt & Office; SendInput siêu nhanh\n"
                  L"• F9 chuyển kiểu đặt dấu cho từ đang gõ (oà ↔ òa) — giữ nguyên từ đã gõ",
                  WS_CHILD | WS_VISIBLE, S(28), S(302), S(500), S(112),
                  reinterpret_cast<HMENU>(IDC_STAT_INFO_FEAT));
            mkCtl(hwnd, L"STATIC",
                  L"Hướng dẫn nhanh:\n"
                  L"• Bật/tắt: nhấp trái (hoặc phải) biểu tượng khay, hoặc nút lớn phía dưới\n"
                  L"• Đổi phương thức gõ: trình đơn khay → Phương thức gõ, hoặc tab Bàn phím\n"
                  L"• Mọi thay đổi được lưu ngay — thoát và mở lại luôn giữ nguyên cấu hình",
                  WS_CHILD | WS_VISIBLE, S(28), S(422), S(500), S(72),
                  reinterpret_cast<HMENU>(IDC_STAT_INFO_GUIDE));
            mkCtl(hwnd, L"STATIC",
                  L"Nguồn gốc & bản quyền: KieeKey phát triển từ OpenKey © 2019 Tuyen Mai, "
                  L"phát hành theo GNU GPL v3. Phần hiện đại hoá © 2026 coderunknow.",
                  WS_CHILD | WS_VISIBLE, S(28), S(502), S(500), S(32),
                  reinterpret_cast<HMENU>(IDC_STAT_INFO_LICENSE));
            mkCtl(hwnd, WC_LINK,
                  L"<A HREF=\"https://github.com/coderunknow/KieeKey\">Mã nguồn · tài liệu · cập nhật: github.com/coderunknow/KieeKey</A>",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP, S(28), S(538), S(500), S(20),
                  reinterpret_cast<HMENU>(IDC_LNK_REPO));

            // ---- buttons ----
            // v1.1.1: the always-visible in-app ON/OFF switch (the removed
            // Ctrl+Shift hotkey's replacement). Lives on the button row so it
            // stays reachable from EVERY tab; the label mirrors the current
            // state and is refreshed by WM_TIMER every 500 ms.
            {
                HWND tg = mkCtl(hwnd, L"BUTTON", L"",
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                          S(12), S(566), S(240), S(30),
                          reinterpret_cast<HMENU>(IDC_BTN_TOGGLE));
                ::SendMessageW(tg, WM_SETFONT, reinterpret_cast<WPARAM>(uiFontBold()), TRUE);
            }
            mkCtl(hwnd, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                  S(300), S(566), S(76), S(30), reinterpret_cast<HMENU>(IDOK));
            mkCtl(hwnd, L"BUTTON", L"Hủy", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  S(384), S(566), S(76), S(30), reinterpret_cast<HMENU>(IDCANCEL));
            mkCtl(hwnd, L"BUTTON", L"Áp dụng", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  S(468), S(566), S(80), S(30), reinterpret_cast<HMENU>(IDC_BTN_APPLY));

            settingsToControls();
            // v1.1.0: the latency peak now reads "since the dialog opened"
            // (previously a process-lifetime outlier dominated the display).
            g.hook.resetPeakLatency();
            ::SetTimer(hwnd, 1, 500, nullptr);   // live telemetry
            return 0;
            } catch (const std::bad_alloc&) {
                g.hSettings = nullptr;
                return -1;
            } catch (...) {
                g.hSettings = nullptr;
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

            // Counters.
            std::swprintf(buf, std::size(buf), L"%llu",
                          static_cast<unsigned long long>(g.hook.pushed()));
            ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_PUSHV), buf);
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
                const auto snap = g.monitor.snapshot();
                const bool excl = g.fgExcluded_.load(std::memory_order_relaxed);
                std::wstring app = snap ? utf8ToUtf16(snap->exeNameUtf8) : std::wstring(L"—");
                if (snap) {
                    app += excl ? L"  —  đang TẮT (loại trừ tự động)"
                                : L"  —  đang gõ";
                }
                ::SetWindowTextW(::GetDlgItem(hwnd, IDC_STAT_APPV), app.c_str());
            }

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

        // v1.1.0: the explicit Esc/Enter WM_KEYDOWN handling was REMOVED —
        // the main message loop already routes this window through
        // IsDialogMessage (Tab navigation, Esc→IDCancel, Enter→IDOK), and
        // the double handling raced the code-table combo dropdown across
        // common-control versions.

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
            }
            return 0;

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
