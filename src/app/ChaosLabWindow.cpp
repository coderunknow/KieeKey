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
#include "UnicodeText.hpp"

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
bool ChaosLabWindow::ownsFlexingGame() const noexcept { return false; }
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
#include <commctrl.h>   // TRACKBAR_CLASSW / TBS_* / TBM_* (the chaos sliders)

#include <algorithm>
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
// Flexing page (the dedicated "gõ thật" surface for Flexing Mode).
constexpr int kIdFlexInput = 4040;     // type anything here: one key = one step
constexpr int kIdFlexOutput = 4041;    // what the engine produced (and typed out)
constexpr int kIdFlexGran = 4042;      // granularity
constexpr int kIdFlexLoad = 4043;      // load the prepared passage
constexpr int kIdFlexInject = 4044;    // auto-inject every produced chunk
constexpr int kIdFlexPrep = 4045;      // the prepared passage itself
constexpr UINT_PTR kInjectionTimer = 0xC041;
constexpr int kIdFlexSend = 4046;      // type the produced text into the app

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
    // Flexing page
    HWND flexPrep = nullptr;
    HWND flexGran = nullptr;
    HWND flexLoad = nullptr;
    HWND flexOutput = nullptr;
    HWND flexInput = nullptr;
    HWND flexInject = nullptr;
    HWND flexSend = nullptr;
    std::wstring flexProduced;        // everything the engine produced so far
    bool ownsFlexing = false;
    std::wstring pendingInjection;
    std::size_t injectionOffset = 0;
    HWND injectionTarget = nullptr;
    // v1.3.0-beta3 (bug #1): the Lab created every control with NO font, so the
    // EDIT boxes fell back to SYSTEM_FIXED_FONT — a raster face that cannot
    // render Vietnamese diacritics and made the whole window hard to read. This
    // is a DPI-scaled Segoe UI face applied to every child (matching the main
    // settings dialog), recreated on WM_DPICHANGED and freed on WM_DESTROY.
    HFONT uiFont = nullptr;
    UINT  fontDpi = 0;
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
    const std::u32string input = codePointsFromWide(typed);

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

// Appends to a read-only EDIT without stealing the caret position.
void appendToEdit(HWND edit, const std::wstring& text) {
    if (edit == nullptr || text.empty()) {
        return;
    }
    const int length = ::GetWindowTextLengthW(edit);
    ::SendMessageW(edit, EM_SETSEL, static_cast<WPARAM>(length), static_cast<LPARAM>(length));
    ::SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
}

// v1.3.0-beta8 (bug UX-08): the "chars per key" combo used to pass a literal
// 3 to setGranularity() for EVERY entry, so the N of the "3 ký tự (N=3)" mode
// was unreachable from the desktop — the label named a parameter the UI could
// not change, while the web lab (web/labs.js) has always sent a real nChars
// and FlexingGame supports 1..64. The combo now offers concrete N values and
// this table is the single mapping from selection index to (granularity, N),
// shared by the control creation and the apply path so they cannot drift.
struct FlexGranChoice {
    const wchar_t* label;
    ok::arcade::FlexGranularity gran;
    std::uint32_t nChars;
};

inline const FlexGranChoice* flexGranChoices(std::size_t& count) noexcept {
    using G = ok::arcade::FlexGranularity;
    static const FlexGranChoice kChoices[] = {
        {L"1 ký tự",   G::OneCharPerKey, 1},
        {L"1 từ",      G::OneWordPerKey, 1},
        {L"3 ký tự",   G::NCharsPerKey,  3},
        {L"5 ký tự",   G::NCharsPerKey,  5},
        {L"10 ký tự",  G::NCharsPerKey,  10},
        {L"25 ký tự",  G::NCharsPerKey,  25},
        {L"Tự chảy",   G::AutoStream,    1},
    };
    count = sizeof(kChoices) / sizeof(kChoices[0]);
    return kChoices;
}

inline FlexGranChoice flexGranularityChoice(int selection) noexcept {
    std::size_t count = 0;
    const FlexGranChoice* choices = flexGranChoices(count);
    const std::size_t index =
        (selection < 0 || static_cast<std::size_t>(selection) >= count)
            ? 0u
            : static_cast<std::size_t>(selection);
    return choices[index];
}

void applyFlexGranularity(ChaosLabWindow::Impl& impl) {
    auto* game = dynamic_cast<ok::arcade::FlexingGame*>(
        ok::arcade::ArcadeManager::instance().getCurrentGame());
    if (game == nullptr || !impl.ownsFlexing) { return; }
    const int selection = static_cast<int>(::SendMessageW(impl.flexGran, CB_GETCURSEL, 0, 0));
    const auto choice = flexGranularityChoice(selection);
    game->setGranularity(choice.gran, choice.nChars);
}

std::size_t ensureFlexingGame(ChaosLabWindow::Impl& impl) {
    auto& manager = ok::arcade::ArcadeManager::instance();
    if (manager.getCurrentGameType() != ok::arcade::GameType::Flexing) {
        (void)manager.launchGame(ok::arcade::GameType::Flexing);
    }
    auto* game = dynamic_cast<ok::arcade::FlexingGame*>(manager.getCurrentGame());
    if (game == nullptr) { return 0; }
    impl.ownsFlexing = true;
    // An empty passage clears the old one too; loading after completion resets
    // the run. Merely changing granularity must NOT call this reload path.
    game->setPreloadedText(codePointsFromWide(controlText(impl.flexPrep)));
    applyFlexGranularity(impl);
    ::SendMessageW(impl.flexingBox, BM_SETCHECK, BST_CHECKED, 0);
    return game->getPreloadedText().size();
}

// One pump of the flexing game: advance the engine, take whatever text it
// produced, log it and (when armed) really type it into the focus application.
// v1.3.0-beta7 (B3): cap flexProduced to 8k chars to avoid unbounded growth
// when the user holds a key in the flex input; clear ownsFlexing when the
// current game is no longer Flexing (hub replaced it) so stale state cannot
// leak into the next Flexing session.
void pumpFlexing(ChaosLabWindow::Impl& impl, bool alsoOnTimer) {
    auto& manager = ok::arcade::ArcadeManager::instance();
    if (!impl.ownsFlexing) {
        return;
    }
    if (manager.getCurrentGameType() != ok::arcade::GameType::Flexing) {
        impl.ownsFlexing = false;
        return;
    }
    auto* game = dynamic_cast<ok::arcade::FlexingGame*>(manager.getCurrentGame());
    if (game == nullptr) {
        impl.ownsFlexing = false;
        return;
    }
    if (alsoOnTimer) {
        if (::GetForegroundWindow() != impl.hwnd) { return; }
        manager.update(static_cast<double>(ChaosLabWindow::kTimerIntervalMs) / 1000.0);
    }
    std::u32string produced = game->popEmittedOutput();
    if (produced.empty()) {
        return;
    }
    const std::wstring wide = widen(ok::arcade::utf8FromUtf32(produced));
    // Cap to 8192 chars — keep the tail so the user still sees recent output.
    constexpr std::size_t kCap = 8192;
    if (impl.flexProduced.size() + wide.size() > kCap) {
        const std::size_t overflow = (impl.flexProduced.size() + wide.size()) - kCap;
        if (overflow >= impl.flexProduced.size()) {
            impl.flexProduced.clear();
        } else {
            impl.flexProduced.erase(0, overflow);
        }
        // Also truncate the EDIT control — otherwise it grows without bound.
        if (impl.flexOutput != nullptr) {
            ::SetWindowTextW(impl.flexOutput, impl.flexProduced.c_str());
        }
    }
    impl.flexProduced += wide;
    appendToEdit(impl.flexOutput, wide);

}

// Never inject into KieeKey itself (including settings/other owned windows).
bool isExternalTarget(HWND target) {
    DWORD processId = 0;
    if (target == nullptr || !::IsWindow(target)) { return false; }
    ::GetWindowThreadProcessId(target, &processId);
    return processId != 0 && processId != ::GetCurrentProcessId();
}

void cancelInjection(ChaosLabWindow::Impl& impl) {
    ::KillTimer(impl.hwnd, kInjectionTimer);
    impl.pendingInjection.clear();
    impl.injectionOffset = 0;
    impl.injectionTarget = nullptr;
}

void pumpInjection(ChaosLabWindow::Impl& impl) {
    // Focus can change between chunks. Cancel instead of typing into the wrong
    // document, and never sleep/block the UI thread for a long passage.
    if (!isExternalTarget(impl.injectionTarget) ||
        ::GetForegroundWindow() != impl.injectionTarget ||
        ChaosLabWindow::emitCallback() == nullptr) {
        cancelInjection(impl);
        return;
    }
    std::size_t end = std::min(impl.injectionOffset + 6, impl.pendingInjection.size());
    if (end < impl.pendingInjection.size() && end > impl.injectionOffset &&
        impl.pendingInjection[end - 1] >= 0xD800 && impl.pendingInjection[end - 1] <= 0xDBFF &&
        impl.pendingInjection[end] >= 0xDC00 && impl.pendingInjection[end] <= 0xDFFF) {
        ++end;   // never split a UTF-16 surrogate pair across SendInput batches
    }
    const std::wstring chunk = impl.pendingInjection.substr(impl.injectionOffset,
                                                            end - impl.injectionOffset);
    if (ChaosLabWindow::emitCallback()(chunk) != chunk.size()) {
        cancelInjection(impl);
        return;
    }
    impl.injectionOffset = end;
    if (end == impl.pendingInjection.size()) { cancelInjection(impl); }
}

std::size_t typeIntoFocusApp(ChaosLabWindow::Impl& impl, const std::wstring& text, bool perChunk) {
    cancelInjection(impl);
    if (text.empty() || !isExternalTarget(impl.target) || ChaosLabWindow::emitCallback() == nullptr) {
        return 0;
    }
    if (!::SetForegroundWindow(impl.target) || ::GetForegroundWindow() != impl.target) {
        return 0;   // activation can be denied by Windows; fail closed
    }
    if (!perChunk) { return ChaosLabWindow::emitCallback()(text); }
    impl.pendingInjection = text;
    impl.injectionTarget = impl.target;
    if (::SetTimer(impl.hwnd, kInjectionTimer, 60, nullptr) == 0) {
        cancelInjection(impl);
    }
    return 0;   // queued, not yet emitted
}

void syncChaosControls(ChaosLabWindow::Impl& impl) {
    const auto cfg = ok::chaos::ChaosEngine::instance().getConfig();
    ::SendMessageW(impl.master, BM_SETCHECK, cfg.masterEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    ::SendMessageW(impl.caseBox, BM_SETCHECK, cfg.randomCaseEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    ::SendMessageW(impl.glyphBox, BM_SETCHECK, cfg.glyphTransformEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    // v1.3.0-beta6 (V3): ROUND, don't truncate — float 0.57 is 0.5699999…,
    // which truncated to 56 and visibly nudged the slider down on every
    // Lab open / config sync (ArcadeServer's JSON echo already rounds).
    ::SendMessageW(impl.intensity, TBM_SETPOS, TRUE,
                   static_cast<LPARAM>(cfg.randomCaseIntensity * 100.0f + 0.5f));
    ::SendMessageW(impl.mode, CB_SETCURSEL, static_cast<WPARAM>(cfg.glyphMode), 0);
}

} // namespace

namespace {

// ---- v1.3.0-beta3 (bug #1): Chaos Lab font ---------------------------------
// Per-monitor DPI of the Lab window (GetDpiForWindow when available; the classic
// LOGPIXELSX fallback keeps MinGW/older SDKs compiling). Mirrors main.cpp.
UINT labWindowDpi(HWND hwnd) noexcept {
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

// A DPI-scaled Segoe UI face — the same family/quality as the settings dialog.
// Segoe UI is Unicode-complete, so Vietnamese precomposed syllables and combining
// marks render correctly (SYSTEM_FIXED_FONT could not).
HFONT makeLabFont(UINT dpi) noexcept {
    const int px = -::MulDiv(13, static_cast<int>(dpi ? dpi : 96), 96);
    return ::CreateFontW(px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

BOOL CALLBACK setChildFontProc(HWND child, LPARAM lp) noexcept {
    ::SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
    return TRUE;
}

// (Re)create the font for the current DPI and push it onto every child control.
// dpiOverride lets WM_DPICHANGED pass the new DPI (GetDpiForWindow may still
// report the old value while that message is being processed).
void applyLabFont(ChaosLabWindow::Impl& impl, UINT dpiOverride = 0) {
    if (impl.hwnd == nullptr) { return; }
    const UINT dpi = dpiOverride ? dpiOverride : labWindowDpi(impl.hwnd);
    if (impl.uiFont != nullptr && impl.fontDpi == dpi) {
        // Same DPI: just re-assert (cheap) so newly created children get it too.
        ::EnumChildWindows(impl.hwnd, setChildFontProc,
                           reinterpret_cast<LPARAM>(impl.uiFont));
        return;
    }
    HFONT next = makeLabFont(dpi);
    if (next == nullptr) { next = impl.uiFont; }   // CreateFont failed: keep old
    if (next == nullptr) { return; }                // nothing usable
    HFONT old = impl.uiFont;
    impl.uiFont = next;
    impl.fontDpi = dpi;
    ::EnumChildWindows(impl.hwnd, setChildFontProc, reinterpret_cast<LPARAM>(impl.uiFont));
    // Delete the previous face only AFTER every child has switched to the new one
    // (a font still selected into a control cannot be deleted). On DPI change this
    // avoids leaking a GDI face per rescale.
    if (old != nullptr && old != next) { ::DeleteObject(old); }
}

void destroyLabFont(ChaosLabWindow::Impl& impl) noexcept {
    if (impl.uiFont != nullptr) {
        ::DeleteObject(impl.uiFont);
        impl.uiFont = nullptr;
        impl.fontDpi = 0;
    }
}

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
                    (void)ensureFlexingGame(*impl);
                } else if (impl->ownsFlexing) {
                    if (ok::arcade::ArcadeManager::instance().getCurrentGameType() ==
                        ok::arcade::GameType::Flexing) {
                        ok::arcade::ArcadeManager::instance().stopGame();
                    }
                    impl->ownsFlexing = false;
                }
                ::SetFocus(impl->flexInput);
                return 0;
            }
            if (control == kIdFlexLoad && notification == BN_CLICKED) {
                // Reload the prepared passage into the shared FlexingGame.
                const std::size_t total = ensureFlexingGame(*impl);
                impl->flexProduced.clear();
                if (impl->flexOutput != nullptr) {
                    ::SetWindowTextW(impl->flexOutput, L"");
                }
                if (total == 0) {
                    ::SetWindowTextW(impl->flexOutput,
                                     L"(chua co van ban chuan bi — hay nhap vao o tren)");
                }
                ::SetFocus(impl->flexInput);
                return 0;
            }
            if (control == kIdFlexGran && notification == CBN_SELCHANGE) {
                applyFlexGranularity(*impl);
                return 0;
            }
            if (control == kIdFlexInput && notification == EN_CHANGE) {
                // One physical character typed in the flexing box = one step of
                // the game. The text the user sees appearing comes from the
                // engine, not from what they pressed.
                auto& manager = ok::arcade::ArcadeManager::instance();
                if (impl->ownsFlexing && manager.getCurrentGameType() == ok::arcade::GameType::Flexing) {
                    (void)manager.handleKey(0, U'x', true);
                    pumpFlexing(*impl, false);
                }
                return 0;
            }
            if (control == kIdFlexSend && notification == BN_CLICKED) {
                // Type everything the engine produced into the application the
                // user was in before opening the lab ("Flexing gõ thật ra ngoài").
                const bool perChunk =
                    ::SendMessageW(impl->flexInject, BM_GETCHECK, 0, 0) == BST_CHECKED;
                (void)typeIntoFocusApp(*impl, impl->flexProduced, perChunk);
                if (impl->flexProduced.empty() && impl->flexOutput != nullptr) {
                    ::SetWindowTextW(impl->flexOutput,
                                     L"(chua co chu nao — hay go vai phim vao o duoi cung)");
                }
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
                if (inject) { (void)typeIntoFocusApp(*impl, text, false); }
                return 0;
            }
            break;
        }
        case WM_HSCROLL:
            if (impl != nullptr && reinterpret_cast<HWND>(lParam) == impl->intensity) {
                applyCheckboxes(*impl);
                refreshPreview(*impl);
                return 0;
            }
            break;
        case WM_TIMER:
            if (impl != nullptr && wParam == kInjectionTimer) {
                pumpInjection(*impl);
                return 0;
            }
            if (impl != nullptr && wParam == static_cast<WPARAM>(ChaosLabWindow::kTimerId)) {
                refreshPreview(*impl);
                // The flexing page also pumps the ArcadeManager, so the same
                // FlexingGame the hub runs is what produces the text here.
                pumpFlexing(*impl, true);
                return 0;
            }
            break;
        case WM_DPICHANGED: {
            // v1.3.0-beta3 (bug #1): rescale the Segoe UI face to the new DPI,
            // re-apply it to every child, then adopt the system-suggested rect so
            // the window itself resizes on the new monitor.
            if (impl != nullptr) { applyLabFont(*impl, static_cast<UINT>(HIWORD(wParam))); }
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested != nullptr) {
                ::SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                               suggested->right - suggested->left,
                               suggested->bottom - suggested->top,
                               SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        }
        case WM_CLOSE:
            ChaosLabWindow::instance().close();
            return 0;
        case WM_DESTROY:
            ::KillTimer(hwnd, ChaosLabWindow::kTimerId);
            if (impl != nullptr) { destroyLabFont(*impl); }
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
    const HWND foreground = ::GetForegroundWindow();
    if (isExternalTarget(foreground)) { m_impl->target = foreground; }

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

        RECT desired{0, 0, 760, 880};
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
               SS_LEFT, 300, 440, 430, 34, -1);

        //---- Flexing Mode page: type anything, the prepared text appears ----
        create(L"STATIC", L"🗿 FLEXING MODE — gõ gì cũng được, chữ chuẩn bị trước tự hiện ra:",
               SS_LEFT, 14, 478, 720, 18, -1);
        create(L"STATIC", L"Văn bản chuẩn bị trước:", SS_LEFT, 14, 500, 200, 18, -1);
        m_impl->flexPrep = create(L"EDIT", L"KieeKey Flexing Mode: ban go gi de tao ra dong chu nay!\r\n"
                                            L"Day la van ban duoc chuan bi truoc, engine C++ go ho ban.",
                                  WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                  14, 520, 520, 66, kIdFlexPrep);
        m_impl->flexLoad = create(L"BUTTON", L"Nạp văn bản", BS_PUSHBUTTON, 546, 520, 90, 26,
                                  kIdFlexLoad);
        m_impl->flexSend = create(L"BUTTON", L"Gõ chữ Flexing ra app", BS_PUSHBUTTON, 546, 552,
                                  114, 26, kIdFlexSend);
        create(L"STATIC", L"Mỗi phím sinh ra:", SS_LEFT, 610, 502, 128, 18, -1);
        m_impl->flexGran = create(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 646, 520, 92, 200,
                                  kIdFlexGran);
        {
            // v1.3.0-beta8 (bug UX-08): populated from the shared choice table
            // so the labels and the applied N can never disagree.
            std::size_t granCount = 0;
            const FlexGranChoice* granChoices = flexGranChoices(granCount);
            for (std::size_t i = 0; i < granCount; ++i) {
                ::SendMessageW(m_impl->flexGran, CB_ADDSTRING, 0,
                               reinterpret_cast<LPARAM>(granChoices[i].label));
            }
        }
        ::SendMessageW(m_impl->flexGran, CB_SETCURSEL, 0, 0);
        m_impl->flexInject = create(L"BUTTON", L"Gõ từng nhịp", BS_AUTOCHECKBOX, 660, 552, 78, 22,
                                    kIdFlexInject);

        create(L"STATIC", L"Chữ engine đã sinh ra (và đã gõ thật ra ngoài):", SS_LEFT, 14, 594, 720,
               18, -1);
        m_impl->flexOutput = create(L"EDIT", L"",
                                    WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                                    14, 614, 720, 92, kIdFlexOutput);

        create(L"STATIC", L"Gõ phím bất kỳ vào ô này (mỗi ký tự = một nhịp của game):", SS_LEFT, 14,
               714, 720, 18, -1);
        m_impl->flexInput = create(L"EDIT", L"",
                                   WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
                                   14, 734, 720, 60, kIdFlexInput);
        create(L"STATIC",
               L"Mẹo: gõ vài phím vào ô dưới, rồi bấm \"Gõ chữ Flexing ra app\" — chữ sẽ thật sự "
               L"được gõ vào ứng dụng bạn đang dùng (tick \"Gõ từng nhịp\" để gõ chậm như người thật).",
               SS_LEFT, 14, 800, 720, 36, -1);

        // v1.3.0-beta3 (bug #1): give every control the DPI-scaled Segoe UI face
        // BEFORE the window is shown, so the EDIT boxes render Vietnamese instead
        // of falling back to the raster SYSTEM_FIXED_FONT.
        applyLabFont(*m_impl);

        ::SetTimer(m_impl->hwnd, kTimerId, kTimerIntervalMs, nullptr);
    }

    syncChaosControls(*m_impl);
    refreshPreview(*m_impl);
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
        cancelInjection(*m_impl);
        if (m_impl->ownsFlexing && ok::arcade::ArcadeManager::instance().getCurrentGameType() ==
            ok::arcade::GameType::Flexing) {
            ok::arcade::ArcadeManager::instance().stopGame();
        }
        m_impl->ownsFlexing = false;
        m_impl->flexProduced.clear();
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

bool ChaosLabWindow::ownsFlexingGame() const noexcept {
    return m_impl != nullptr && m_impl->ownsFlexing &&
           m_impl->hwnd != nullptr && ::IsWindow(m_impl->hwnd) != FALSE;
}

bool launchChaosLab() {
    ChaosLabWindow& lab = ChaosLabWindow::instance();
    return lab.open(nullptr);
}

} // namespace ok::app

#endif  // _WIN32
