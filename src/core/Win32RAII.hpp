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
// File: src/core/Win32RAII.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.0.2 — Win32RAII.hpp
// RAII wrappers for Windows resources (GPL-3.0, derived from OpenKey 2.0.5)
// Replace every raw HANDLE / HKEY / HHOOK / HGLOBAL in the legacy codebase
// with these types. Zero-throw in noexcept paths, exception-safe elsewhere.
//----------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ok::win32 {

//---------------------------------------------------------------------------
// Generic RAII wrapper for handles closed with a single-argument API
// (CloseHandle, UnhookWindowsHookEx, UnhookWinEvent, ...).
//---------------------------------------------------------------------------
template <typename HandleT, auto Closer, HandleT Invalid = nullptr>
class [[nodiscard]] Win32Handle {
public:
    constexpr Win32Handle() noexcept = default;
    constexpr explicit Win32Handle(HandleT h) noexcept : h_(h) {}

    Win32Handle(const Win32Handle&)            = delete;
    Win32Handle& operator=(const Win32Handle&) = delete;

    Win32Handle(Win32Handle&& other) noexcept : h_(std::exchange(other.h_, Invalid)) {}
    Win32Handle& operator=(Win32Handle&& other) noexcept {
        if (this != &other) { reset(); h_ = std::exchange(other.h_, Invalid); }
        return *this;
    }

    ~Win32Handle() { reset(); }

    [[nodiscard]] constexpr HandleT get() const noexcept { return h_; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return h_ != Invalid; }
    [[nodiscard]] constexpr HandleT* operator&() noexcept { return &h_; }  // for out-params

    void reset(HandleT h = Invalid) noexcept {
        if (h_ != Invalid) { Closer(h_); }
        h_ = h;
    }
    HandleT release() noexcept { return std::exchange(h_, Invalid); }

private:
    HandleT h_ = Invalid;
};

namespace detail {
    inline void closeHandle(HANDLE h) noexcept { ::CloseHandle(h); }
    inline void unhookWindowsHook(HHOOK h) noexcept { ::UnhookWindowsHookEx(h); }
    inline void unhookWinEvent(HWINEVENTHOOK h) noexcept { ::UnhookWinEvent(h); }
    inline void deleteObject(HGDIOBJ h) noexcept { ::DeleteObject(h); }
    inline void regCloseKey(HKEY k) noexcept { ::RegCloseKey(k); }
    inline void globalFree(HGLOBAL h) noexcept { ::GlobalFree(h); }
    inline void findClose(HANDLE h) noexcept { ::FindClose(h); }
}

using ProcessHandle     = Win32Handle<HANDLE, detail::closeHandle>;
using EventHandle       = Win32Handle<HANDLE, detail::closeHandle>;
using HookHandle        = Win32Handle<HHOOK, detail::unhookWindowsHook>;
using WinEventHook      = Win32Handle<HWINEVENTHOOK, detail::unhookWinEvent>;
using GdiObject         = Win32Handle<HGDIOBJ, detail::deleteObject, nullptr>;
using FindHandle        = Win32Handle<HANDLE, detail::findClose>;

//---------------------------------------------------------------------------
// Registry key RAII.
//---------------------------------------------------------------------------
class [[nodiscard]] RegistryKey {
public:
    RegistryKey() noexcept = default;
    ~RegistryKey() { close(); }

    RegistryKey(const RegistryKey&)            = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;
    RegistryKey(RegistryKey&& o) noexcept : h_(std::exchange(o.h_, nullptr)) {}
    RegistryKey& operator=(RegistryKey&& o) noexcept {
        if (this != &o) { close(); h_ = std::exchange(o.h_, nullptr); }
        return *this;
    }

    // Opens (creating if needed) HKCU\Software\TuyenMai\OpenKey — same key as v2.0.5.
    static RegistryKey openAppKey(bool createIfMissing = true) {
        // NOTE: std::wstring_view::data() is NOT guaranteed NUL-terminated,
        // so materialize a std::wstring before passing to the registry APIs.
        const std::wstring kPath = L"SOFTWARE\\TuyenMai\\OpenKey";
        HKEY h = nullptr;
        LONG rc = ::RegOpenKeyExW(HKEY_CURRENT_USER, kPath.c_str(), 0, KEY_READ | KEY_WRITE, &h);
        if (rc == ERROR_FILE_NOT_FOUND && createIfMissing) {
            rc = ::RegCreateKeyExW(HKEY_CURRENT_USER, kPath.c_str(), 0, nullptr,
                                   REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, nullptr, &h, nullptr);
        }
        if (rc != ERROR_SUCCESS) { return {}; }          // failure -> empty key
        return RegistryKey(h);
    }

    // --- typed getters/setters (return default on failure) ---
    LONG getDword(std::wstring_view name, DWORD& out) const noexcept {
        if (!h_) return ERROR_INVALID_HANDLE;
        DWORD size = sizeof(DWORD);
        return ::RegQueryValueExW(h_, std::wstring(name).c_str(), nullptr, nullptr,
                                 reinterpret_cast<LPBYTE>(&out), &size);
    }
    DWORD getDword(std::wstring_view name, DWORD def) const noexcept {
        if (!h_) return def;
        DWORD v = def;
        DWORD size = sizeof(DWORD);
        if (::RegQueryValueExW(h_, std::wstring(name).c_str(), nullptr, nullptr,
                               reinterpret_cast<LPBYTE>(&v), &size) != ERROR_SUCCESS) {
            return def;
        }
        return v;
    }
    LONG setDword(std::wstring_view name, DWORD value) const noexcept {
        if (!h_) return ERROR_INVALID_HANDLE;
        return ::RegSetValueExW(h_, std::wstring(name).c_str(), 0, REG_DWORD,
                                reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }

    // getRegBinary replacement: returns std::vector<BYTE> (no leaked static buffer!)
    std::vector<BYTE> getBinary(std::wstring_view name) const {
        std::vector<BYTE> data;
        if (!h_) return data;
        DWORD size = 0;
        LONG rc = ::RegQueryValueExW(h_, std::wstring(name).c_str(), nullptr, nullptr, nullptr, &size);
        if (rc != ERROR_SUCCESS) return data;
        data.resize(size ? size : 1);
        rc = ::RegQueryValueExW(h_, std::wstring(name).c_str(), nullptr, nullptr, data.data(), &size);
        if (rc != ERROR_SUCCESS) { data.clear(); return data; }
        data.resize(size);
        return data;
    }
    LONG setBinary(std::wstring_view name, const void* p, size_t size) const noexcept {
        if (!h_) return ERROR_INVALID_HANDLE;
        return ::RegSetValueExW(h_, std::wstring(name).c_str(), 0, REG_BINARY,
                                static_cast<const BYTE*>(p), static_cast<DWORD>(size));
    }

    [[nodiscard]] HKEY get() const noexcept { return h_; }
    [[nodiscard]] explicit operator bool() const noexcept { return h_ != nullptr; }

private:
    explicit RegistryKey(HKEY h) noexcept : h_(h) {}
    void close() noexcept { if (h_) { ::RegCloseKey(h_); h_ = nullptr; } }
    HKEY h_ = nullptr;
};

//---------------------------------------------------------------------------
// Scoped clipboard: RAII for OpenClipboard/CloseClipboard with proper error
// cleanup (legacy code leaked the clipboard open on every early return).
//---------------------------------------------------------------------------
class [[nodiscard]] ScopedClipboard {
public:
    explicit ScopedClipboard(HWND owner = nullptr) : ok_(::OpenClipboard(owner) != FALSE) {}
    ~ScopedClipboard() { if (ok_) ::CloseClipboard(); }
    ScopedClipboard(const ScopedClipboard&)            = delete;
    ScopedClipboard& operator=(const ScopedClipboard&) = delete;
    [[nodiscard]] bool opened() const noexcept { return ok_; }

    // Reads CF_UNICODETEXT without leaking handles.
    std::wstring getUnicodeText() const noexcept {
        if (!ok_) return {};
        HANDLE h = ::GetClipboardData(CF_UNICODETEXT);
        if (!h) return {};
        const wchar_t* p = static_cast<const wchar_t*>(::GlobalLock(h));
        if (!p) return {};
        std::wstring text(p);               // copy before unlocking
        ::GlobalUnlock(h);
        return text;
    }

private:
    bool ok_;
};

} // namespace ok::win32
