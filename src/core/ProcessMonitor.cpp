//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.1 Stable - refactored and completed logic
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
// File: src/core/ProcessMonitor.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.3 — ProcessMonitor.cpp
// See ProcessMonitor.hpp. Event-driven foreground tracking.
//----------------------------------------------------------------------------
#include "ProcessMonitor.hpp"

#include <algorithm>
#include <cctype>
// v1.2.1 Stable — portability fix (found by the new Windows cross-build
// gate): isFullscreenOnPrimary() calls std::abs on LONG deltas. std::abs for
// integral types is declared by <cstdlib>; MSVC's <windows.h> chain happens
// to pull it in, but a conforming toolchain (clang/libc++ with mingw-w64
// headers) does not — the file previously FAILED to compile off-MSVC.
#include <cstdlib>

#include <dwmapi.h>
#include <psapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "psapi.lib")
#endif

namespace ok::monitor {
namespace {

// Case-insensitive lowercase for ASCII exe names (exe names are ASCII).
std::string toLowerAscii(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Primary-monitor fullscreen heuristic: the window rect must exactly cover the
// monitor it sits on. Zero per-frame cost — runs only on foreground changes.
bool isFullscreenOnPrimary(HWND hwnd) noexcept {
    RECT wr{};
    if (!::GetWindowRect(hwnd, &wr)) { return false; }

    HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!::GetMonitorInfoW(mon, &mi)) { return false; }
    const RECT& mr = mi.rcMonitor;

    const LONG w = wr.right - wr.left, h = wr.bottom - wr.top;
    const LONG mw = mr.right - mr.left, mh = mr.bottom - mr.top;
    // Exact cover of the monitor work area, with tolerance for 1px borders.
    return std::abs(w - mw) <= 2 && std::abs(h - mh) <= 2 &&
           std::abs(wr.left - mr.left) <= 2 && std::abs(wr.top - mr.top) <= 2;
}

// DWMWA_CLOAKED: window hidden by shell (e.g. on a virtual desktop).
bool isCloaked(HWND hwnd) noexcept {
    BOOL cloaked = FALSE;
    if (SUCCEEDED(::DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
        return cloaked != FALSE;
    }
    return false;
}

bool getImagePathAndStartTime(DWORD pid, std::wstring& path, FILETIME& start) noexcept {
    path.clear();
    using ok::win32::ProcessHandle;
    ProcessHandle h = ok::win32::ProcessHandle(
        ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!h) { return false; }

    DWORD size = 0;
    if (!::QueryFullProcessImageNameW(h.get(), 0, nullptr, &size) &&
        ::GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return false;
    }
    std::wstring buf(size + 8, L'\0');
    DWORD bufLen = static_cast<DWORD>(buf.size());
    if (!::QueryFullProcessImageNameW(h.get(), 0, buf.data(), &bufLen)) {
        return false;
    }
    buf.resize(bufLen);
    path = buf;

    FILETIME created{}, exited{}, kern{}, user{};
    if (!::GetProcessTimes(h.get(), &created, &exited, &kern, &user)) {
        return false;
    }
    start = created;
    return true;
}

ProcessClass classify(const std::string& exeLower, bool fullscreen, bool isShell) {
    using namespace classification;
    if (isShell || inTable(exeLower, kShellExecutables)) {
        return ProcessClass::Shell;
    }
    if (inTable(exeLower, kIdeExecutables)) {
        return ProcessClass::Ide;
    }
    if (inTable(exeLower, kBrowserExecutables)) {
        return ProcessClass::Browser;
    }
    // Fullscreen + non-browser + not-shell ⇒ treat as an immersive game title
    // (DirectX/Vulkan/OpenGL). OpenKey must not fight game input.
    if (fullscreen) {
        return ProcessClass::Game;
    }
    // UWP hosts get the Metro workaround path.
    if (exeLower == "applicationframehost.exe" || exeLower == "shellexperiencehost.exe") {
        return ProcessClass::Metro;
    }
    return ProcessClass::Normal;
}

} // namespace

//===========================================================================
// Lifecycle
//===========================================================================
bool ProcessMonitor::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) { return true; }
    // v1.1.3: mirror ModernKeyHook::start — a previous stop() that DETACHED a
    // wedged pump leaves that pump running against this same object (its
    // 1 s fallback timer re-checks running_, which the exchange above just
    // set true again). Spawning a second pump would give two pumps and let
    // the old one's unwind unhook the new one's WinEvent hook. Refuse.
    if (stuckDetached_.load(std::memory_order_acquire)) {
        running_.store(false, std::memory_order_release);
        return false;
    }

    pumpExited_.store(false, std::memory_order_release);
    stuckDetached_.store(false, std::memory_order_release);   // v1.1.0 re-arm
    // v1.2.1 Stable: re-arm the installation handshake (a previous
    // start()/stop() cycle must never let this start() see a stale "installed").
    hookInstalled_.store(false, std::memory_order_release);
    pumpThread_ = std::thread([this] {
        pumpThreadMain();
        pumpExited_.store(true, std::memory_order_release);
    });

    // Wait for hook installation.
    // v1.2.1 Stable — poll the ATOMIC handshake flag (hookInstalled_), not
    // the fgEvent_ handle member: the pump thread owns and writes that
    // handle, and reading it from this thread was the same unsynchronized
    // cross-thread access ModernKeyHook::start() already eliminated in
    // v1.2.0 (its own comment documents why: a data race, benign on current
    // hardware, undefined by the standard). The pump now publishes the same
    // installation fact through an atomic.
    for (int i = 0; i < 2000 && pumpThread_.joinable(); ++i) {
        if (hookInstalled_.load(std::memory_order_acquire) ||
            !running_.load(std::memory_order_acquire)) { break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!hookInstalled_.load(std::memory_order_acquire)) {
        // v1.1.0-audit fix: join the pump we just spawned before returning
        // failure. The old code only cleared running_ — stop() then early-
        // returned on the already-clear flag and NEVER joined, so
        // pumpThread_ stayed joinable and ~ProcessMonitor() called
        // std::terminate() (deterministic abort-at-exit whenever
        // SetWinEventHook fails). Bounded wait + detach mirrors stop().
        running_.store(false, std::memory_order_release);
        if (pumpThread_.joinable()) {
            const DWORD tid = pumpThreadId_.load(std::memory_order_acquire);
            if (tid != 0) { ::PostThreadMessageW(tid, WM_QUIT, 0, 0); }
            for (int i = 0; i < 400; ++i) {
                if (pumpExited_.load(std::memory_order_acquire)) { break; }
                ::Sleep(5);
            }
            if (pumpExited_.load(std::memory_order_acquire)) {
                pumpThread_.join();
                fgEvent_.reset();
            } else {
                stuckDetached_.store(true, std::memory_order_release);
                pumpThread_.detach();
            }
        }
        return false;
    }

    // Seed immediately with the current foreground window.
    updateFromWindow(::GetForegroundWindow());
    return true;
}

void ProcessMonitor::stop() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) { return; }
    if (pumpThread_.joinable()) {
        // The id was published by the pump thread itself (works on MSVC and
        // MinGW alike; native_handle() is not a HANDLE under MinGW posix).
        const DWORD tid = pumpThreadId_.load(std::memory_order_acquire);
        if (tid != 0) { ::PostThreadMessageW(tid, WM_QUIT, 0, 0); }
        // v3.5 BOUNDED wait: the pump self-wakes every 1 s (fallback timer,
        // see pumpThreadMain), so this join cannot hang even if the WM_QUIT
        // message is somehow lost. Past the budget we detach the stuck pump
        // and leave fgEvent_ owned by `this` (the pump resets it on its own
        // unwind) — a shutdown must never become a zombie process.
        for (int i = 0; i < 400; ++i) {
            if (pumpExited_.load(std::memory_order_acquire)) { break; }
            ::Sleep(5);
        }
        if (pumpExited_.load(std::memory_order_acquire)) {
            pumpThread_.join();
            fgEvent_.reset();
        } else {
            // v1.1.0: publish the stuck state so the app can avoid running
            // static destruction over this worker (ExitProcess instead of a
            // teardown race against a detached thread touching members).
            stuckDetached_.store(true, std::memory_order_release);
            pumpThread_.detach();
        }
    } else {
        fgEvent_.reset();
    }
}

void ProcessMonitor::refreshNow() noexcept {
    updateFromWindow(::GetForegroundWindow());
}

bool ProcessMonitor::enableCrashRecovery(std::wstring_view args, DWORD flags) noexcept {
    // RegisterApplicationRestart: on crash, Windows relaunches KieeKey with
    // the given command line. Complements, not replaces, Windows Error
    // Reporting. NULL command line keeps the current one.
    // v1.1.0: the caller's flags are honored VERBATIM — the previous code
    // silently ORed RESTART_NO_CRASH | RESTART_NO_HANG into every call,
    // which disabled exactly the crash/hang recovery this function exists
    // for while contradicting the documented contract.
    std::wstring cmd(args);
    HRESULT hr = ::RegisterApplicationRestart(
        args.empty() ? nullptr : cmd.c_str(),
        flags);
    return SUCCEEDED(hr);
}

//===========================================================================
// Pump thread: SET win-event hook + message loop (same pattern as the hook).
//===========================================================================
void ProcessMonitor::pumpThreadMain() noexcept {
    g_current = this;
    // Publish the pump thread id BEFORE anything can need to wake us (see
    // ProcessMonitor.hpp — portable across MSVC and MinGW).
    pumpThreadId_.store(::GetCurrentThreadId(), std::memory_order_release);
    fgEvent_.reset(::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                     nullptr, &ProcessMonitor::winEventProc, 0, 0,
                                     WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS));
    // v1.2.1 Stable: publish the installation handshake (start() polls this
    // atomic instead of reading the fgEvent_ handle cross-thread).
    hookInstalled_.store(fgEvent_.operator bool(), std::memory_order_release);
    // v3.5 reliability: 1 s fallback timer — the pump ALWAYS wakes
    // periodically and re-checks running_, so a lost WM_QUIT can never turn
    // shutdown into a hang.
    constexpr UINT_PTR kMonitorTickTimerId = 0x0B00C;
    ::SetTimer(nullptr, kMonitorTickTimerId, 1000, nullptr);
    MSG msg;
    while (running_.load(std::memory_order_acquire)) {
        const BOOL r = ::GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) { break; }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    ::KillTimer(nullptr, kMonitorTickTimerId);
    fgEvent_.reset();
    // v1.2.1 Stable: the hook is gone with this thread — clear the handshake
    // so no later observer can read a stale "installed".
    hookInstalled_.store(false, std::memory_order_release);
    g_current = nullptr;
}

//===========================================================================
// v1.1.3 — elevation probe for the foreground process. UIPI: a non-elevated
// process can neither marshal TSF edit sessions into an elevated target nor
// SendInput to it (SendInput fails SILENTLY per MSDN). Detecting the
// integrity level lets the app pass keystrokes through untouched instead of
// swallowing them. Runs only on foreground-change events (rare); an
// ACCESS_DENIED OpenProcess is itself the answer (we are lower integrity).
//===========================================================================
static bool isElevatedProcess(DWORD pid) noexcept {
    if (pid == 0) { return false; }
    ok::win32::ProcessHandle proc(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                                FALSE, pid));
    if (!proc.get()) {
        // Cannot even query: the target outranks us (or is protected).
        return true;
    }
    HANDLE token = nullptr;
    if (!::OpenProcessToken(proc.get(), TOKEN_QUERY, &token)) {
        return true;   // same reasoning as above
    }
    DWORD elevation = 0;
    DWORD retLen = 0;
    const BOOL ok = ::GetTokenInformation(token, TokenElevation, &elevation,
                                          sizeof(elevation), &retLen);
    ::CloseHandle(token);
    return ok && elevation != 0;
}

//===========================================================================
// Snapshot builder — O(1)-ish; runs only on foreground-change events.
//===========================================================================
void ProcessMonitor::updateFromWindow(HWND fg) noexcept {
    // v1.1.3 OOM hardening: this builds strings/shared_ptr under noexcept
    // and runs on the hook thread (refreshNow) and a C callback (pump) —
    // an escaping bad_alloc would std::terminate the process mid-typing.
    // Degrade instead: keep the previous snapshot (stale by one switch,
    // harmless) and let the next event retry.
    try {
    DWORD pid = 0;
    if (fg == nullptr || ::GetWindowThreadProcessId(fg, &pid) == 0 || pid == 0) {
        return;
    }

    auto info = std::make_shared<ForegroundInfo>();
    info->hwnd = fg;
    info->pid  = pid;
    info->fullscreen = isFullscreenOnPrimary(fg);
    info->cloaked    = isCloaked(fg);
    info->elevated   = isElevatedProcess(pid);

    if (!getImagePathAndStartTime(pid, info->exePath, info->processStartTime)) {
        info->exeNameUtf8 = "unknown";
        info->exeNameLower = "unknown";
        info->kind = ProcessClass::Normal;
        {
            std::unique_lock<std::shared_mutex> lk(snapshotMtx_);
            snapshot_ = std::move(info);
        }
        return;
    }

    // exe name from full path (last '\' segment).
    const std::wstring_view path(info->exePath);
    const std::size_t pos = path.find_last_of(L"\\/");
    const std::wstring_view name = (pos == std::wstring_view::npos) ? path : path.substr(pos + 1);

    // Convert to UTF-8 for the classification tables.
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, name.data(),
                                           static_cast<int>(name.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(need), '\0');
    if (need > 0) {
        ::WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(name.size()),
                              utf8.data(), need, nullptr, nullptr);
    }
    info->exeNameUtf8 = utf8;
    info->exeNameLower = toLowerAscii(utf8);
    info->kind = classify(info->exeNameLower, info->fullscreen,
                          info->exeNameLower == "explorer.exe");

    {
        std::unique_lock<std::shared_mutex> lk(snapshotMtx_);
        snapshot_ = std::move(info);   // single publish (readers keep the old object)
    }
    } catch (const std::bad_alloc&) {
        // Memory pressure: keep the previous snapshot; retry on the next
        // foreground event. Never let OOM kill the IME process.
    } catch (...) {
        // Same policy for any unexpected exception from string conversion.
    }
}

//---------------------------------------------------------------------------
// WinEvent trampoline — runs on the pump thread (OUTOFCONTEXT).
//---------------------------------------------------------------------------
void CALLBACK ProcessMonitor::winEventProc(HWINEVENTHOOK, DWORD ev, HWND hwnd,
                                           LONG idObj, LONG /*idChild*/, DWORD, DWORD) {
    ProcessMonitor* self = g_current;
    if (self == nullptr || ev != EVENT_SYSTEM_FOREGROUND || idObj != OBJID_WINDOW) { return; }
    self->updateFromWindow(hwnd);
}

} // namespace ok::monitor
