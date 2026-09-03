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
// File: src/core/ProcessMonitor.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.3 — ProcessMonitor.hpp
// Zero-cost foreground-window detector + auto-exclusion engine.
//
// Design:
//   * Event-driven, not polled: a SetWinEventHook(EVENT_SYSTEM_FOREGROUND)
//     callback updates the snapshot. Idle CPU cost = 0 (no timer, no
//     GetForegroundWindow polling loop).
//   * The snapshot is a std::shared_ptr<const ForegroundInfo>; readers
//     copy the pointer (brief reader lock) and read an immutable object —
//     the writer runs only on a foreground change.
//   * PID-reuse-safe: tracks process creation time, so a recycled PID cannot
//     produce a stale classification.
//   * Classification is O(1) after the snapshot exists: constexpr exe-name
//     tables + cheap window-geometry heuristics for fullscreen games.
//
// v1.1.0 portability note: the snapshot was previously held in
// std::atomic<std::shared_ptr<...>> (P0718). GCC/libstdc++ accepted it,
// but atomic<shared_ptr> is not implemented by libc++ (clang/llvm-mingw)
// nor by MSVC — the type is only trivially-copyable-compatible where a
// library chooses to specialize it, so it is NOT portable C++. The
// replacement is a shared_mutex-guarded shared_ptr: concurrent readers,
// one writer per foreground change, identical observable semantics
// (readers always see either the old or the new immutable snapshot), and
// the per-keystroke hot path never touches it (the app caches the
// exclusion decision in an atomic bool — see main.cpp).
//
// Replaces: OpenKeyHelper::getFrontMostAppExecuteName() + winEventProcCallback
// smart-switch logic (which used process-wide global mutable state).
//----------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>

#include "Win32RAII.hpp"

namespace ok::monitor {

//---------------------------------------------------------------------------
// ForegroundInfo — immutable snapshot, safe to share across threads.
//---------------------------------------------------------------------------
enum class ProcessClass : std::uint8_t {
    Normal,     // ordinary app: Vietnamese processing enabled
    Ide,        // VS Code / Visual Studio / CLion / JetBrains / …: bypass
    Game,       // fullscreen DirectX/Vulkan/OpenGL title: bypass
    Shell,      // explorer.exe & friends: bypass
    Browser,    // Chromium/WebKit/Gecko shell: enabled (per-app rules apply)
    Metro,      // UWP/XAML islands host: enabled with Metro workarounds
};

struct ForegroundInfo {
    DWORD              pid       = 0;
    std::wstring       exePath;      // full image path (UTF-16)
    std::string        exeNameUtf8;  // e.g. "Code.exe"
    std::string        exeNameLower; // lowercased for O(1) classification
    HWND               hwnd      = nullptr;
    FILETIME           processStartTime{};
    ProcessClass       kind      = ProcessClass::Normal;
    bool               fullscreen = false;
    bool               cloaked    = false;   // DWMWA_CLOAKED (virtual desktops)
    // v1.1.3 — the foreground process runs ELEVATED (high-integrity level).
    // Both output paths fail silently there for a non-elevated IME: the TSF
    // edit session cannot marshal across the UIPI boundary and SendInput is
    // blocked with NO error indication — composing "á" swallowed the user's
    // letters outright in elevated cmd/PowerShell/Task Manager/registry
    // editors (the worst failure mode an IME has). The app treats an
    // elevated foreground like an auto-excluded one: raw pass-through plus
    // an engine-buffer reset, so keystrokes are NEVER lost.
    bool               elevated  = false;

    // Auto-bypass set: IDEs/editors and immersive (fullscreen) game titles.
    // Shell (Explorer, Terminal…) is intentionally NOT auto-excluded —
    // legacy OpenKey kept processing there (Vietnamese filenames, etc.).
    [[nodiscard]] bool autoExcluded() const noexcept {
        return kind == ProcessClass::Ide || kind == ProcessClass::Game;
    }
    [[nodiscard]] bool isExplorer() const noexcept {
        return exeNameLower == "explorer.exe";
    }
};

//---------------------------------------------------------------------------
// ProcessMonitor
//---------------------------------------------------------------------------
class ProcessMonitor final {
public:
    ProcessMonitor() noexcept = default;
    ~ProcessMonitor() { stop(); }

    ProcessMonitor(const ProcessMonitor&)            = delete;
    ProcessMonitor& operator=(const ProcessMonitor&) = delete;

    // Registers the WinEvent hook + pump thread. Idempotent.
    [[nodiscard]] bool start();

    // Unregisters; joins the pump thread. Safe from any thread.
    void stop() noexcept;

    // v1.1.0 — true when stop() had to DETACH a wedged pump thread (its
    // bounded budget expired). The app treats this as "never run static
    // destruction over this worker" and exits via ExitProcess (which kills
    // all threads atomically before any teardown code runs).
    [[nodiscard]] bool stuckThreadsDetached() const noexcept { return stuckDetached_.load(std::memory_order_acquire); }

    // Snapshot access (reader side — concurrent, brief shared lock).
    [[nodiscard]] std::shared_ptr<const ForegroundInfo> snapshot() const noexcept {
        std::shared_lock<std::shared_mutex> lk(snapshotMtx_);
        return snapshot_;
    }

    // v1.1.3 — is the CURRENT foreground process elevated? Hard UIPI
    // constraint (not user-configurable): a non-elevated IME can never reach
    // it, so the app passes keystrokes through instead of swallowing them.
    [[nodiscard]] bool currentAppElevated() const noexcept {
        auto s = snapshot();
        return s && s->elevated;
    }

    // Convenience: does the CURRENT foreground app require auto-exclusion?
    // Honors the user-configurable flags (setExclude*), defaulting to the
    // classification-level rules (IDEs + fullscreen games).
    [[nodiscard]] bool currentAppAutoExcluded() const noexcept {
        auto s = snapshot();
        if (!s) { return false; }
        switch (s->kind) {
            case ProcessClass::Ide:   return exclIde_.load(std::memory_order_relaxed);
            case ProcessClass::Game:  return exclGame_.load(std::memory_order_relaxed);
            case ProcessClass::Shell: return exclShell_.load(std::memory_order_relaxed);
            default:                  return false;
        }
    }

    // User-configurable auto-exclusion switches (safe from any thread).
    void setExcludeIde(bool on)   noexcept { exclIde_.store(on, std::memory_order_relaxed); }
    void setExcludeGame(bool on)  noexcept { exclGame_.store(on, std::memory_order_relaxed); }
    void setExcludeShell(bool on) noexcept { exclShell_.store(on, std::memory_order_relaxed); }
    [[nodiscard]] bool excludeIde()   const noexcept { return exclIde_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool excludeGame()  const noexcept { return exclGame_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool excludeShell() const noexcept { return exclShell_.load(std::memory_order_relaxed); }

    // RegisterApplicationRestart integration (crash recovery, per directive).
    // `flags`: RESTART_NO_CRASH | RESTART_NO_HANG | RESTART_NO_REBOOT as needed.
    // v1.1.0: the flags are now passed through VERBATIM (the previous
    // implementation silently ORed RESTART_NO_CRASH | RESTART_NO_HANG into
    // every call, contradicting this contract and partially ignoring the
    // caller's request).
    static bool enableCrashRecovery(std::wstring_view commandLineArgs = L"",
                                    DWORD flags = 0) noexcept;

    // Test/CLI hook: force-refresh now (used by the UI "re-test" button).
    void refreshNow() noexcept;

private:
    void pumpThreadMain() noexcept;
    void updateFromWindow(HWND fg) noexcept;   // builds a new snapshot

    static void CALLBACK winEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    inline static ProcessMonitor* g_current = nullptr;   // trampoline target

    std::atomic<bool> running_{false};
    // v1.1.0 portability fix: shared_mutex + plain shared_ptr (see the
    // class comment — atomic<shared_ptr> is not portable C++).
    mutable std::shared_mutex snapshotMtx_;
    std::shared_ptr<const ForegroundInfo> snapshot_;   // guarded by snapshotMtx_
    ok::win32::WinEventHook fgEvent_;
    std::thread pumpThread_;
    // Pump thread id, captured INSIDE pumpThreadMain before the hook install
    // (portable: works on MSVC AND MinGW, where std::thread::native_handle()
    // is a pthread_t, not a HANDLE — GetThreadId(native_handle()) does not
    // compile/link there). stop() posts WM_QUIT to this id to wake the pump.
    std::atomic<DWORD> pumpThreadId_{0};
    // v3.5 reliability: exit acknowledgement for the bounded shutdown in
    // stop() (stamp is the pump thread's very last action — see start()).
    std::atomic<bool> pumpExited_{false};
    // v1.1.0: set when stop()'s bounded budget expired and the pump had to
    // be detached (see stuckThreadsDetached()).
    std::atomic<bool> stuckDetached_{false};

    // User-configurable exclusion switches (defaults match the legacy
    // OpenKey behavior: IDEs + fullscreen games bypass, shell stays active).
    std::atomic<bool> exclIde_{true};
    std::atomic<bool> exclGame_{true};
    std::atomic<bool> exclShell_{false};
};

//---------------------------------------------------------------------------
// Static classification tables (constexpr — zero runtime cost).
//---------------------------------------------------------------------------
namespace classification {

// IDEs and editors that must always bypass Vietnamese processing.
inline constexpr std::string_view kIdeExecutables[] = {
    "code.exe",          // VS Code
    "devenv.exe",        // Visual Studio
    "clion64.exe", "clion.exe",
    "idea64.exe", "idea.exe",          // IntelliJ
    "pycharm64.exe", "pycharm.exe",
    "webstorm64.exe", "webstorm.exe",
    "goland64.exe", "goland.exe",
    "rider64.exe", "rider.exe",
    "androidstudio64.exe", "androidstudio.exe",
    "eclipse.exe",
    "sublime_text.exe",
    "notepad++.exe",
    "vim.exe",
    "emacs.exe",
    "x64dbg.exe", "x32dbg.exe", "ollydbg.exe",
    "fiddler.exe",
    "dbeaver.exe",
    "wt.exe",            // Windows Terminal (shell-ish)
    "mintty.exe",
    "alacritty.exe",
    "windowsTerminal.exe",
};

// Browsers (Vietnamese stays enabled, but the Chromium autocomplete
// workaround + TSF path apply).
inline constexpr std::string_view kBrowserExecutables[] = {
    "chrome.exe", "msedge.exe", "brave.exe", "opera.exe", "vivaldi.exe",
    "firefox.exe", "iexplore.exe",
};

// Always bypass regardless of geometry.
inline constexpr std::string_view kShellExecutables[] = {
    "explorer.exe", "taskmgr.exe", "cmd.exe", "powershell.exe", "pwsh.exe",
    "conhost.exe", "searchapp.exe", "startmenuexperiencehost.exe",
    "lockapp.exe", "logonui.exe", "shellexperiencehost.exe",
};

[[nodiscard]] inline bool inTable(std::string_view lower,
                                  std::span<const std::string_view> table) noexcept {
    for (auto name : table) {
        if (lower == name) { return true; }
    }
    return false;
}

} // namespace classification

} // namespace ok::monitor
