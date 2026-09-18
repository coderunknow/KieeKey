//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: src/app/ChaosLabWindow.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "ChaosLabWindow.hpp"

#include "ArcadeHubLaunch.hpp"

#if !defined(_WIN32)
// Portable stub (see ArcadeWindow.cpp): keeps the translation unit compilable
// on a non-Windows cross-check build.
namespace ok::app {
struct ChaosLabWindow::Impl {};
ChaosLabWindow& ChaosLabWindow::instance() noexcept {
    static ChaosLabWindow s_instance;
    return s_instance;
}
ChaosLabWindow::ChaosLabWindow() = default;
bool ChaosLabWindow::open(void*) { return false; }
void ChaosLabWindow::close() {}
bool ChaosLabWindow::isOpen() const noexcept { return false; }
void* ChaosLabWindow::handle() const noexcept { return nullptr; }
bool launchChaosLab() { return false; }
std::u32string ChaosLabWindow::transformForPreview(std::u32string_view, std::uint32_t) {
    return {};
}
void ChaosLabWindow::setEmitCallback(EmitCallback) noexcept {}
ChaosLabWindow::EmitCallback ChaosLabWindow::emitCallback() noexcept { return nullptr; }
} // namespace ok::app

#else  // _WIN32

#include "Arcade.hpp"
#include "ArcadeRender.hpp"
#include "ChaosEngine.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace ok::app {

namespace {

constexpr const wchar_t* kLabClass = L"KieeKeyChaosLab";
constexpr const wchar_t* kLabTitle = L"KieeKey — Phòng thí nghiệm Chaos (test gõ thật)";
constexpr int kIdInput = 4001;      // multiline EDIT: what you type
constexpr int kIdPreview = 4002;    // read-only EDIT: what KieeKey would emit
constexpr int kIdMaster = 4010;
constexpr int kIdCase = 4011;
constexpr int kIdGlyph = 4012;
constexpr int kIdInject = 4013;
constexpr int kIdIntensity = 4020;
constexpr int kIdMode = 4021;
constexpr int kIdFlexing = 4030;
constexpr int kIdSendOnce = 4031;

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                             static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(),
                          needed);
    return wide;
}

std::wstring controlText(HWND control) {
    const int length = ::GetWindowTextLengthW(control);
    if (length <= 0) {
        return {};
    }
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    ::GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

} // namespace

struct ChaosLabWindow::Impl {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    HWND target = nullptr;   // window that had the focus before the lab opened
    HWND input = nullptr;
    HWND preview = nullptr;
    HWND master = nullptr;
    HWND caseBox = nullptr;
    HWND glyphBox = nullptr;
    HWND injectBox = nullptr;
    HWND flexingBox = nullptr;
    HWND intensity = nullptr;
    HWND mode = nullptr;
    bool ignoreNotifications = false;
};

std::u32string ChaosLabWindow::transformForPreview(std::u32string_view input,
                                                   std::uint32_t seed) {
    using ok::chaos::ChaosEngine;
    ChaosEngine& engine = ChaosEngine::instance();
    const ok::chaos::ChaosConfig config = engine.getConfig();
    if (!config.masterEnabled || input.empty()) {
        return std::u32string(input);
    }
    // Exactly the pipeline of the output path: the case transform changes the
    // text layer, then the glyph transform rewrites what is drawn.
    std::u32string result = engine.processCase(input, seed);
    if (config.glyphTransformEnabled) {
        result = engine.getVisualDisplayString(result, seed);
    }
    return result;
}

namespace {

void refreshPreview(ChaosLabWindow::Impl& impl) {
    if (impl.input == nullptr || impl.preview == nullptr) {
        return;
    }
    const std::wstring typed = controlText(impl.input);
    const std::u32string input = [] (const std::wstring& wide) {
        std::u32string out;
        out.reserve(wide.size());
        for (wchar_t ch : wide) {
            out.push_back(static_cast<char32_t>(ch));
        }
        return out;
    }(typed);

    const std::u32string transformed =
        ChaosLabWindow::transformForPreview(input, 0x5EEDu);
    const std::wstring wide = widen(ok::arcade::utf8FromUtf32(transformed));
    ::SetWindowTextW(impl.preview, wide.c_str());
}

void applyCheckboxes(ChaosLabWindow::Impl& impl) {
    auto& engine = ok::chaos::ChaosEngine::instance();
    ok::chaos::ChaosConfig config = engine.getConfig();
    config.masterEnabled =
        impl.master != nullptr && ::SendMessageW(impl.master, BM_GETCHECK, 0, 0) == BST_CHECKED;
    config.randomCaseEnabled =
        impl.caseBox != nullptr && ::SendMessageW(impl.caseBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    config.glyphTransformEnabled =
        impl.glyphBox != nullptr && ::SendMessageW(impl.glyphBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (impl.intensity != nullptr) {
        const int percent = static_cast<int>(::SendMessageW(impl.intensity, TBM_GETPOS, 0, 0));
        config.randomCaseIntensity = static_cast<float>(percent) / 100.0f;
        config.glyphIntensity = config.randomCaseIntensity;
    }
    if (impl.mode != nullptr) {
        const int selection = static_cast<int>(::SendMessageW(impl.mode, CB_GETCURSEL, 0, 0));
        config.glyphMode = static_cast<ok::chaos::GlyphTransformMode>(
            selection < 0 ? 0 : (selection > 6 ? 6 : selection));
    }
    engine.setConfig(config);
}

} // namespace

namespace {

LRESULT CALLBACK labWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* impl = reinterpret_cast<ChaosLabWindow::Impl*>(
        ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message) {
        case WM_NCCREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }
        case WM_COMMAND: {
            if (impl == nullptr) {
                break;
            }
            const int control = LOWORD(wParam);
            const int notification = HIWORD(wParam);
            if (control == kIdInput && (notification == EN_CHANGE || notification == EN_UPDATE)) {
                refreshPreview(*impl);
                return 0;
            }
            if (control == kIdMaster || control == kIdCase || control == kIdGlyph ||
                control == kIdIntensity || control == kIdMode) {
                applyCheckboxes(*impl);
                refreshPreview(*impl);
                return 0;
            }
            if (control == kIdFlexing && notification == BN_CLICKED) {
                // Flexing Mode: the arcade hub owns the loop and pushes the
                // prepared text; the checkbox only forwards the intent.
                const bool enabled =
                    ::SendMessageW(impl->flexingBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (enabled) {
                    ok::arcade::ArcadeManager::instance().launchGame(ok::arcade::GameType::Flexing);
                } else {
                    ok::arcade::ArcadeManager::instance().stopGame();
                }
                ::SetFocus(impl->input);
                return 0;
            }
            if (control == kIdSendOnce && notification == BN_CLICKED) {
                // Type the transformed text into the application the user was
                // working in before opening the lab. The lab itself must give
                // the focus back first, otherwise SendInput would type into the
                // lab's own edit control.
                const bool inject =
                    ::SendMessageW(impl->injectBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
                const std::wstring text = controlText(impl->preview);
                if (inject && !text.empty() && ::IsWindow(impl->target)) {
                    ::SetForegroundWindow(impl->target);
                    ::Sleep(80);   // let the activation settle before typing
                    if (ChaosLabWindow::emitCallback() != nullptr) {
                        (void)ChaosLabWindow::emitCallback()(text);
                    }
                }
                return 0;
            }
            break;
        }
        case WM_TIMER:
            if (impl != nullptr && wParam == static_cast<WPARAM>(ChaosLabWindow::kTimerId)) {
                refreshPreview(*impl);
                return 0;
            }
            break;
        case WM_CLOSE:
            ChaosLabWindow::instance().close();
            return 0;
        case WM_DESTROY:
            ::KillTimer(hwnd, ChaosLabWindow::kTimerId);
            return 0;
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

namespace {
ChaosLabWindow::EmitCallback g_emitCallback = nullptr;
} // namespace

void ChaosLabWindow::setEmitCallback(EmitCallback callback) noexcept {
    g_emitCallback = callback;
}

ChaosLabWindow::EmitCallback ChaosLabWindow::emitCallback() noexcept {
    return g_emitCallback;
}

ChaosLabWindow& ChaosLabWindow::instance() noexcept {
    static ChaosLabWindow s_instance;
    return s_instance;
}

ChaosLabWindow::ChaosLabWindow() : m_impl(new Impl()) {}

bool ChaosLabWindow::open(void* owner) {
    if (m_impl == nullptr) {
        return false;
    }
    m_impl->owner = static_cast<HWND>(owner);
    // Remember the application the user was typing in, so the "gõ thật vào app"
    // button can hand the focus back before injecting the transformed text.
    m_impl->target = ::GetForegroundWindow();

    if (m_impl->hwnd == nullptr) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = labWndProc;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kLabClass;
        if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        RECT desired{0, 0, 760, 620};
        ::AdjustWindowRectEx(&desired, WS_OVERLAPPEDWINDOW, FALSE, 0);
        m_impl->hwnd = ::CreateWindowExW(
            0, kLabClass, kLabTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
            CW_USEDEFAULT, desired.right - desired.left, desired.bottom - desired.top,
            m_impl->owner, nullptr, ::GetModuleHandleW(nullptr), m_impl);
        if (m_impl->hwnd == nullptr) {
            return false;
        }

        const auto create = [&](const wchar_t* klass, const wchar_t* text, DWORD style, int x,
                                int y, int width, int height, int id) -> HWND {
            return ::CreateWindowExW(
                0, klass, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height,
                m_impl->hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                ::GetModuleHandleW(nullptr), nullptr);
        };

        create(L"STATIC", L"Bạn gõ gì cũng được — ô dưới hiện chính xác thứ KieeKey sẽ phát ra:",
               SS_LEFT, 14, 10, 720, 18, -1);
        m_impl->input = create(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL |
                                                      ES_WANTRETURN,
                               14, 32, 720, 130, kIdInput);
        create(L"STATIC", L"Kết quả sau Chaos Engine (case + glyph):", SS_LEFT, 14, 172, 720, 18,
               -1);
        m_impl->preview = create(L"EDIT", L"",
                                 WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                                 14, 194, 720, 130, kIdPreview);

        m_impl->master = create(L"BUTTON", L"Bật Chaos (master)", BS_AUTOCHECKBOX, 14, 336, 200, 22,
                                kIdMaster);
        m_impl->caseBox = create(L"BUTTON", L"Đổi hoa/thường ngẫu nhiên", BS_AUTOCHECKBOX, 14, 360,
                                 240, 22, kIdCase);
        m_impl->glyphBox = create(L"BUTTON", L"Biến đổi glyph (xoay/lật)", BS_AUTOCHECKBOX, 14, 384,
                                  240, 22, kIdGlyph);
        m_impl->injectBox = create(L"BUTTON", L"Gõ thật vào app đang mở", BS_AUTOCHECKBOX, 14, 408,
                                   260, 22, kIdInject);
        m_impl->flexingBox = create(L"BUTTON", L"Flexing Mode (chữ tự hiện)", BS_AUTOCHECKBOX, 14,
                                    432, 260, 22, kIdFlexing);

        create(L"STATIC", L"Cường độ:", SS_LEFT, 300, 336, 80, 18, -1);
        m_impl->intensity = create(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS, 380, 332, 340, 26,
                                   kIdIntensity);
        ::SendMessageW(m_impl->intensity, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        ::SendMessageW(m_impl->intensity, TBM_SETPOS, TRUE, 60);

        create(L"STATIC", L"Glyph mode:", SS_LEFT, 300, 366, 80, 18, -1);
        m_impl->mode = create(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 380, 364, 200, 200,
                              kIdMode);
        const wchar_t* modes[] = {L"None", L"Rotate90 (render-only)", L"Rotate180",
                                  L"Rotate270 (render-only)", L"FlipHorizontal", L"FlipVertical",
                                  L"Random"};
        for (const wchar_t* mode : modes) {
            ::SendMessageW(m_impl->mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mode));
        }
        ::SendMessageW(m_impl->mode, CB_SETCURSEL, 5, 0);

        create(L"BUTTON", L"Gõ kết quả vào app đang focus", BS_PUSHBUTTON, 300, 402, 260, 28,
               kIdSendOnce);
        create(L"STATIC",
               L"P / F1 tạm dừng game · F2 chơi lại · Esc thoát game (khi cửa sổ này đang focus)",
               SS_LEFT, 300, 440, 430, 18, -1);

        ::SetTimer(m_impl->hwnd, kTimerId, kTimerIntervalMs, nullptr);
    }

    ::ShowWindow(m_impl->hwnd, SW_SHOW);
    ::SetForegroundWindow(m_impl->hwnd);
    ::SetFocus(m_impl->input);
    return true;
}

void ChaosLabWindow::close() {
    if (m_impl == nullptr) {
        return;
    }
    if (m_impl->hwnd != nullptr) {
        HWND hwnd = m_impl->hwnd;
        m_impl->hwnd = nullptr;
        ::KillTimer(hwnd, kTimerId);
        ::DestroyWindow(hwnd);
    }
}

bool ChaosLabWindow::isOpen() const noexcept {
    return m_impl != nullptr && m_impl->hwnd != nullptr && ::IsWindow(m_impl->hwnd) != FALSE;
}

void* ChaosLabWindow::handle() const noexcept {
    return (m_impl != nullptr) ? static_cast<void*>(m_impl->hwnd) : nullptr;
}

bool launchChaosLab() {
    ChaosLabWindow& lab = ChaosLabWindow::instance();
    return lab.open(nullptr);
}

} // namespace ok::app

#endif  // _WIN32
