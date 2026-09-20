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
// File: src/core/ProcessMonitor.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — ProcessMonitor.cpp
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
// v1.3.0-beta5 (bug B4): UWP fallback — a foreground UWP window belongs to
// ApplicationFrameHost.exe (or refuses the image-name query entirely); its
// REAL identity is the window's AppUserModelID property store.
// NB: SHGetPropertyStoreForWindow is declared in shellapi.h, which
// WIN32_LEAN_AND_MEAN strips from windows.h — include it explicitly (the
// MinGW cross-build compiles this TU lean; MSVC gets the same declaration).
#include <shellapi.h>
#include <shobjidl.h>
#include <propidl.h>
#ifdef _MSC_VER
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "psapi.lib")
#endif

#include "ProcessNameUtil.hpp"

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

//---------------------------------------------------------------------------
// v1.3.0-beta5 (bug B4) — foreground process NAME resolution, fallback chain.
//
// beta4 had ONE query path and displayed the literal "unknown" whenever ANY
// step failed — including a GetProcessTimes failure AFTER the path query had
// already succeeded (all-or-nothing), and every protected/elevated process
// whose image name a standard-user IME may not read. "unknown" told the user
// nothing and made the whole diagnostics tab look broken.
//
// The chain below tries, in order:
//   1. QueryFullProcessImageNameW, Win32 format   (QUERY_LIMITED handle; the
//      beta4 NULL-size probe is replaced by a proper 32K buffer — the probe
//      form is undocumented for a NULL buffer and fails on some targets)
//   2. QueryFullProcessImageNameW, NATIVE format  (succeeds for some
//      protected processes where the Win32 format is refused)
//   3. GetModuleFileNameExW                       (needs QUERY_INFORMATION|
//      VM_READ — a different access mask that 32-bit targets and some
//      security products treat differently)
//   4. The window's AppUserModelID property store  (UWP: the frame window
//      belongs to ApplicationFrameHost.exe; the AUMID names the REAL app)
//   5. The honest label "pid N (lỗi X)"           (ProcessNameUtil.hpp —
//      the PID still identifies the process for a bug report, the error says
//      why the name is unavailable)
// A failed name query NEVER excludes the app — exclusion is decided solely by
// the elevation probe + classification (see treatAsElevated).
//---------------------------------------------------------------------------
struct NameQuery {
    std::wstring path;        // full image path when resolved (may be native)
    std::wstring aumidName;   // friendly UWP name when the AUMID resolved
    DWORD        lastError = 0;   // error of the last failed image-name step
    bool         resolved() const noexcept {
        return !path.empty() || !aumidName.empty();
    }
};

// Max path incl. long-path prefix; QueryFullProcessImageNameW documents
// 32768 as the sufficient buffer for the Win32 format.
constexpr DWORD kImagePathBufferChars = 32768;

NameQuery queryProcessName(DWORD pid, HWND fg) noexcept {
    NameQuery out;
    using ok::win32::ProcessHandle;

    // Steps 1+2: one QUERY_LIMITED handle, both formats.
    {
        ProcessHandle h(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        if (h) {
            std::wstring buf(kImagePathBufferChars, L'\0');
            DWORD len = static_cast<DWORD>(buf.size());
            if (::QueryFullProcessImageNameW(h.get(), 0, buf.data(), &len) && len > 0) {
                buf.resize(len);
                out.path = std::move(buf);
                return out;
            }
            out.lastError = ::GetLastError();
            len = static_cast<DWORD>(buf.size());
            if (::QueryFullProcessImageNameW(h.get(), PROCESS_NAME_NATIVE,
                                             buf.data(), &len) && len > 0) {
                buf.resize(len);
                out.path = std::move(buf);
                return out;
            }
            if (out.lastError == 0) { out.lastError = ::GetLastError(); }
        } else {
            out.lastError = ::GetLastError();
        }
    }

    // Step 3: the classic psapi query under a different access mask.
    {
        ProcessHandle h(::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                      FALSE, pid));
        if (h) {
            std::wstring buf(MAX_PATH * 2, L'\0');
            const DWORD n = ::GetModuleFileNameExW(h.get(), nullptr, buf.data(),
                                                   static_cast<DWORD>(buf.size()));
            if (n > 0) {
                buf.resize(n);
                out.path = std::move(buf);
                return out;
            }
            if (out.lastError == 0) { out.lastError = ::GetLastError(); }
        } else if (out.lastError == 0) {
            out.lastError = ::GetLastError();
        }
    }

    // Step 4: the window's AppUserModelID (UWP hosts + protected processes
    // whose windows still expose the property store).
    if (fg != nullptr) {
        IPropertyStore* store = nullptr;
        if (SUCCEEDED(::SHGetPropertyStoreForWindow(
                fg, IID_IPropertyStore,
                reinterpret_cast<void**>(&store))) && store != nullptr) {
            // PKEY_AppUserModel_ID = {9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, 5
            // (declared locally: propsys is not a link dependency of ok_core).
            const PROPERTYKEY kAumid{
                {0x9F4C2855, 0x9F79, 0x4B39,
                 {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};
            PROPVARIANT pv{};
            if (SUCCEEDED(store->GetValue(kAumid, &pv))) {
                if (pv.vt == VT_LPWSTR && pv.pwszVal != nullptr &&
                    pv.pwszVal[0] != L'\0') {
                    out.aumidName = friendlyNameFromAumid(pv.pwszVal);
                }
                ::PropVariantClear(&pv);
            }
            store->Release();
        }
    }
    return out;
}

// The window's AUMID even when the image path DID resolve: a UWP foreground
// window's owner process is ApplicationFrameHost.exe — displaying the frame
// host instead of the app was part of the "diagnostics look broken" report.
std::wstring queryWindowAumidName(HWND fg) noexcept {
    if (fg == nullptr) { return {}; }
    IPropertyStore* store = nullptr;
    if (FAILED(::SHGetPropertyStoreForWindow(
            fg, IID_IPropertyStore,
            reinterpret_cast<void**>(&store))) || store == nullptr) {
        return {};
    }
    const PROPERTYKEY kAumid{
        {0x9F4C2855, 0x9F79, 0x4B39,
         {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};
    std::wstring friendly;
    PROPVARIANT pv{};
    if (SUCCEEDED(store->GetValue(kAumid, &pv))) {
        if (pv.vt == VT_LPWSTR && pv.pwszVal != nullptr && pv.pwszVal[0] != L'\0') {
            friendly = std::wstring(friendlyNameFromAumid(pv.pwszVal));
        }
        ::PropVariantClear(&pv);
    }
    store->Release();
    return friendly;
}

// Creation time is a BEST-EFFORT side query now: beta4 folded it into the
// name query and a GetProcessTimes failure discarded an already-resolved
// path (the field is diagnostic-only — nothing in the codebase reads it).
bool queryStartTime(DWORD pid, FILETIME& start) noexcept {
    using ok::win32::ProcessHandle;
    ProcessHandle h(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!h) { return false; }
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
    const bool openProcessOk = (proc.get() != nullptr);
    HANDLE token = nullptr;
    const bool openTokenOk = openProcessOk &&
                             ::OpenProcessToken(proc.get(), TOKEN_QUERY, &token);
    DWORD elevation = 0;
    DWORD retLen = 0;
    bool tokenInfoOk = false;
    bool elevatedFlag = false;
    if (openTokenOk) {
        tokenInfoOk = ::GetTokenInformation(token, TokenElevation, &elevation,
                                            sizeof(elevation), &retLen) != FALSE;
        elevatedFlag = (elevation != 0);
        ::CloseHandle(token);
    }
    // v1.3.0-beta5 (bug B4): the DECISION lives in the pure table
    // (ProcessNameUtil.hpp, pinned by tests/test_process_monitor.cpp) — an
    // ACCESS_DENIED on the process/token really means "outranks us" (UIPI
    // blocks both output paths → pass through), while a token-READ failure
    // with both handles open must NOT disable the IME.
    return treatAsElevated(openProcessOk, openTokenOk, tokenInfoOk, elevatedFlag);
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

    // v1.3.0-beta5 (bug B4): the name query is a fallback chain and NEVER
    // all-or-nothing; the creation time is a separate best-effort side query.
    const NameQuery name = queryProcessName(pid, fg);
    (void)queryStartTime(pid, info->processStartTime);
    info->nameQueryError = name.lastError;

    // v1.2.2 RC4 (P2-4): PID-reuse revalidation. The OpenProcess / image-name
    // / process-times / token queries above run while the window's owner
    // could in theory exit and its PID be recycled — classifying the NEW
    // process would flip exclusion/elevation for one foreground event.
    // Re-query the window's owner and publish only when it still matches;
    // on mismatch keep the previous snapshot (the next event retries).
    DWORD pidNow = 0;
    if (::GetWindowThreadProcessId(fg, &pidNow) == 0 || pidNow != pid) {
        return;
    }

    // Display-name decision:
    //   * image path resolved   → last path segment (exeNameFromPath), EXCEPT
    //     for the UWP frame host: its AUMID names the REAL app, so prefer it.
    //   * only the AUMID resolved → the friendly package name (Metro).
    //   * nothing resolved → the honest "pid N (lỗi X)" label. The bare word
    //     "unknown" (beta4) is gone: it named no process and stated no reason.
    std::wstring_view nameW;
    if (!name.path.empty()) {
        info->exePath = name.path;
        nameW = exeNameFromPath(name.path);
        constexpr std::wstring_view kFrameHost = L"applicationframehost.exe";
        const bool isFrameHost =
            nameW.size() == kFrameHost.size() &&
            ::_wcsnicmp(nameW.data(), kFrameHost.data(), kFrameHost.size()) == 0;
        if (isFrameHost) {
            const std::wstring friendly = queryWindowAumidName(fg);
            if (!friendly.empty()) {
                // Publish the friendly name but KEEP exePath (classification
                // and any internal logic still see the real host binary).
                info->exePath = name.path;
                info->kind = ProcessClass::Metro;
                const int needF = ::WideCharToMultiByte(
                    CP_UTF8, 0, friendly.data(),
                    static_cast<int>(friendly.size()), nullptr, 0, nullptr, nullptr);
                std::string utf8F(static_cast<std::size_t>(std::max(0, needF)), '\0');
                if (needF > 0) {
                    ::WideCharToMultiByte(CP_UTF8, 0, friendly.data(),
                                          static_cast<int>(friendly.size()),
                                          utf8F.data(), needF, nullptr, nullptr);
                }
                info->exeNameUtf8 = utf8F;
                info->exeNameLower = toLowerAscii(info->exeNameUtf8);
                std::unique_lock<std::shared_mutex> lkAumid(snapshotMtx_);
                snapshot_ = std::move(info);
                return;
            }
        }
    } else if (!name.aumidName.empty()) {
        nameW = name.aumidName;
        info->kind = ProcessClass::Metro;
    } else {
        const std::wstring label = unknownLabelW(pid, name.lastError);
        const int needL = ::WideCharToMultiByte(
            CP_UTF8, 0, label.data(), static_cast<int>(label.size()),
            nullptr, 0, nullptr, nullptr);
        std::string utf8L(static_cast<std::size_t>(std::max(0, needL)), '\0');
        if (needL > 0) {
            ::WideCharToMultiByte(CP_UTF8, 0, label.data(),
                                  static_cast<int>(label.size()),
                                  utf8L.data(), needL, nullptr, nullptr);
        }
        info->exeNameUtf8 = utf8L;
        // Classification keys stay machine-readable: an unresolved name is
        // never in any exclusion table.
        info->exeNameLower = "unresolved";
        info->kind = ProcessClass::Normal;
        std::unique_lock<std::shared_mutex> lkUnk(snapshotMtx_);
        snapshot_ = std::move(info);
        return;
    }

    // Convert to UTF-8 for the classification tables.
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, nameW.data(),
                                           static_cast<int>(nameW.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(need), '\0');
    if (need > 0) {
        ::WideCharToMultiByte(CP_UTF8, 0, nameW.data(), static_cast<int>(nameW.size()),
                              utf8.data(), need, nullptr, nullptr);
    }
    info->exeNameUtf8 = utf8;
    info->exeNameLower = toLowerAscii(utf8);
    if (!name.path.empty()) {
        // Classified ONLY from a real image name. The AUMID branch above is
        // already Metro: a friendly package name is in no exe table, and
        // running classify() on it could misfile a fullscreen UWP app as a
        // Game and auto-exclude it.
        info->kind = classify(info->exeNameLower, info->fullscreen,
                              info->exeNameLower == "explorer.exe");
    }

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
