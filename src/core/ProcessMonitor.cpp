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
// File: src/core/ProcessMonitor.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.0.1 — ProcessMonitor.cpp
// See ProcessMonitor.hpp. Event-driven foreground tracking.
//----------------------------------------------------------------------------
#include "ProcessMonitor.hpp"

#include <algorithm>
#include <cctype>

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

    pumpExited_.store(false, std::memory_order_release);
    pumpThread_ = std::thread([this] {
        pumpThreadMain();
        pumpExited_.store(true, std::memory_order_release);
    });

    // Wait for hook installation.
    for (int i = 0; i < 2000 && pumpThread_.joinable(); ++i) {
        if (fgEvent_ || !running_.load(std::memory_order_acquire)) { break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!fgEvent_) { running_.store(false, std::memory_order_release); return false; }

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
    // RegisterApplicationRestart: on crash, Windows relaunches OpenKey with
    // the given command line. Complements, not replaces, Windows Error
    // Reporting. NULL command line keeps the current one.
    std::wstring cmd(args);
    HRESULT hr = ::RegisterApplicationRestart(
        args.empty() ? nullptr : cmd.c_str(),
        flags | RESTART_NO_CRASH | RESTART_NO_HANG);
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
    g_current = nullptr;
}

//===========================================================================
// Snapshot builder — O(1)-ish; runs only on foreground-change events.
//===========================================================================
void ProcessMonitor::updateFromWindow(HWND fg) noexcept {
    DWORD pid = 0;
    if (fg == nullptr || ::GetWindowThreadProcessId(fg, &pid) == 0 || pid == 0) {
        return;
    }

    auto info = std::make_shared<ForegroundInfo>();
    info->hwnd = fg;
    info->pid  = pid;
    info->fullscreen = isFullscreenOnPrimary(fg);
    info->cloaked    = isCloaked(fg);

    if (!getImagePathAndStartTime(pid, info->exePath, info->processStartTime)) {
        info->exeNameUtf8 = "unknown";
        info->exeNameLower = "unknown";
        info->kind = ProcessClass::Normal;
        std::atomic_store(&snapshot_, std::move(info));
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

    std::atomic_store(&snapshot_, std::move(info));   // single atomic publish
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
