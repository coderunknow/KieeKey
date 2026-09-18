//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/test_arcade_window.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — tests/test_arcade_window.cpp
// UI-level harness for the two graphical surfaces of v1.3.0:
//
//   1. Arcade Hub (src/app/ArcadeWindow.cpp, Win32 GDI)
//        * the catalogue sidebar lists every game and highlights the running one
//        * hovering a row repaints that row highlighted
//        * clicking a row launches that game
//        * WM_KEYDOWN drives the running game (with the real character, exactly
//          once per key press — WM_CHAR must not duplicate it)
//        * WM_TIMER advances the game; Esc hands the keyboard back
//        * closing the window stops the run and reports it
//   2. Chaos Lab (src/app/ChaosLabWindow.cpp)
//        * the preview control shows exactly what the chaos engine would emit
//        * with chaos off the preview is byte-identical to the typed text
//        * the "gõ thật vào app đang focus" button writes through the host
//          emitter callback and nothing is injected while it is unchecked
//
// The Windows window procedures run for real (against tests/win32_gdi_stub.cpp,
// which records every GDI call), so this is an execution test of the GDI code
// path, not a compile check. On a Windows host the same binary can be built
// against the platform SDK.
//----------------------------------------------------------------------------
#include "win32_gdi_shim.hpp"
#include "win32_gdi_stub.hpp"

#include "Arcade.hpp"
#include "ArcadeWindow.hpp"
#include "ChaosEngine.hpp"
#include "ChaosLabWindow.hpp"
#include "UnicodeText.hpp"
#include <commctrl.h>
#include "Progression.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace ok::arcade;

namespace {

constexpr int kHubSidebarWidth = ok::app::ArcadeWindow::kSidebarWidth;   // 280
constexpr int kHubRowHeight = 48;
constexpr int kHubFirstRowY = 112;

// Screen Y of the centre of catalogue row `index`.
int rowCentreY(int index) {
    return kHubFirstRowY + index * kHubRowHeight + kHubRowHeight / 2 - 2;
}

void paint(HWND hwnd) {
    okgdi::clearLog();
    ::SendMessageW(hwnd, WM_PAINT, 0, 0);
}

// The GDI recorders store Windows COLORREFs (0x00BBGGRR); the engine's palette
// is packed 0xRRGGBBAA. This converts between the two.
COLORREF toColorRef(Color color) {
    return RGB(ok::arcade::colorR(color), ok::arcade::colorG(color), ok::arcade::colorB(color));
}

int countRectsWithColor(COLORREF color) {
    int count = 0;
    for (const okgdi::DrawCall& call : okgdi::log()) {
        if ((call.kind == "rect" || call.kind == "roundrect") && call.color == color) {
            ++count;
        }
    }
    return count;
}

void pressKey(HWND hwnd, int vk, bool shift = false) {
    okgdi::setShiftDown(shift);
    ::SendMessageW(hwnd, WM_KEYDOWN, static_cast<WPARAM>(vk), 0);
}

std::wstring controlText(HWND control) {
    wchar_t buffer[512]{};
    const int length = ::GetWindowTextW(control, buffer, 512);
    return std::wstring(buffer, static_cast<std::size_t>(length < 0 ? 0 : length));
}

std::wstring wide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                             static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(),
                          needed);
    return out;
}

} // namespace

//---------------------------------------------------------------------------
void testHubSidebarShowsTheWholeCatalog() {
    okgdi::destroyAllWindows();
    ArcadeManager::instance().stopGame();

    ok::app::ArcadeWindow& hub = ok::app::ArcadeWindow::instance();
    assert(hub.open(nullptr, "snake"));
    HWND hwnd = static_cast<HWND>(hub.handle());
    assert(hwnd != nullptr);
    assert(hub.isOpen());

    paint(hwnd);
    assert(okgdi::framePresents() == 1);                       // double-buffered blit
    assert(okgdi::logContainsText(L"ARCADE HUB"));             // header
    assert(okgdi::countCalls("gradient") >= 1);                // gradient background

    // Every catalog entry is listed by its display name.
    for (const GameInfo& info : gameCatalog()) {
        const std::wstring name = wide(std::string(info.nameVi));
        assert(!name.empty());
        assert(okgdi::logContainsText(name));
    }
    // ...and the running game (`snake`) is marked as active.
    assert(countRectsWithColor(toColorRef(0x22C55EFFu)) >= 1);   // accent bar
    // The game itself is painted into its pane (right of the sidebar).
    for (const okgdi::DrawCall& call : okgdi::log()) {
        if (call.kind == "bitblt") {
            assert(call.c == ok::app::ArcadeWindow::kDefaultWidth);
        }
    }
    std::cout << "  [PASS] Hub sidebar lists the whole catalog\n";
}

//---------------------------------------------------------------------------
void testHubHoverAndClickStartAGame() {
    ok::app::ArcadeWindow& hub = ok::app::ArcadeWindow::instance();
    HWND hwnd = static_cast<HWND>(hub.handle());
    assert(hwnd != nullptr);

    const int row = 3;   // typing-race
    ::SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(100, rowCentreY(row)));
    assert(hub.hoverIndexForTest() == row);

    paint(hwnd);
    assert(countRectsWithColor(toColorRef(0x1E293BFFu)) == 1);   // hovered row highlight

    // A click inside the row launches it.
    ::SendMessageW(hwnd, WM_LBUTTONDOWN, 0, MAKELPARAM(100, rowCentreY(row)));
    assert(ArcadeManager::instance().hasActiveGame());
    assert(ArcadeManager::instance().getCurrentGameType() ==
           static_cast<GameType>(gameCatalog()[static_cast<std::size_t>(row)].id));

    // Moving off the list clears the highlight.
    ::SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(600, 400));
    assert(hub.hoverIndexForTest() == -1);

    paint(hwnd);
    assert(okgdi::logContainsText(L"Đua xe theo tốc độ gõ") ||
           okgdi::logContainsText(wide(std::string(gameCatalog()[row].nameVi))));
    std::cout << "  [PASS] Hub hover highlight & click-to-play\n";
}

//---------------------------------------------------------------------------
void testHubKeyboardDrivesTheGame() {
    ok::app::ArcadeWindow& hub = ok::app::ArcadeWindow::instance();
    HWND hwnd = static_cast<HWND>(hub.handle());
    ArcadeManager& manager = ArcadeManager::instance();

    // The row-3 click above started Typing Race.
    auto* game = dynamic_cast<TypingRaceGame*>(manager.getCurrentGame());
    assert(game != nullptr);
    const std::u32string passage(game->getPassage());
    assert(!passage.empty());
    assert(game->getCharIndex() == 0);

    // A key press must register exactly once: WM_KEYDOWN carries the resolved
    // character, WM_CHAR is swallowed.
    const char32_t first = passage[0];
    const int vk = (first >= U'a' && first <= U'z') ? (first - U'a' + 'A') : 0;
    if (vk != 0) {
        pressKey(hwnd, vk, first == U'Q' || first == U'X');   // W/Q need Shift on real layouts
        const std::size_t afterKeyDown = game->getCharIndex();
        ::SendMessageW(hwnd, WM_CHAR, static_cast<WPARAM>(first), 1);
        assert(game->getCharIndex() == afterKeyDown);   // WM_CHAR must not double-count
    }

    // Type the rest of the passage while time actually passes (a run finished
    // with zero elapsed time is legitimately 0 WPM, so the harness types at a
    // human-ish 4 chars per 150 ms ≈ 320 WPM).
    for (std::size_t i = game->getCharIndex(); i < passage.size(); ++i) {
        manager.handleKey(0, passage[i], true);
        if ((i % 4) == 3) {
            manager.update(0.15);
        }
    }
    for (int i = 0; i < 600 && !game->isGameOver(); ++i) {
        manager.update(1.0 / 60.0);
    }
    assert(game->isGameOver());

    // The run was credited to the progression engine (no double credit).
    manager.update(1.0 / 60.0);
    auto stats = ok::progression::ProgressionEngine::instance().getStats();
    assert(stats.typingRaceBestWpm > 0.0);
    std::cout << "  [PASS] Hub keyboard drives the running game\n";
}

//---------------------------------------------------------------------------
void testHubTimerEscAndClose() {
    ok::app::ArcadeWindow& hub = ok::app::ArcadeWindow::instance();
    HWND hwnd = static_cast<HWND>(hub.handle());
    ArcadeManager& manager = ArcadeManager::instance();

    manager.launchGame(GameType::Snake);
    assert(manager.hasActiveGame());

    // WM_TIMER runs one frame (the same call the 60 Hz timer makes).
    for (int i = 0; i < 10; ++i) {
        ::SendMessageW(hwnd, WM_TIMER,
                       static_cast<WPARAM>(ok::app::ArcadeWindow::kTimerId), 0);
    }
    const Frame& frame = manager.getFrame();
    assert(frame.stats.title.size() > 0);

    // Esc exits the game; the hub window stays open.
    pressKey(hwnd, 0x1B);
    assert(!manager.hasActiveGame());
    assert(hub.isOpen());

    // Closing the hub releases resources and stops any run.
    manager.launchGame(GameType::Tetris);
    hub.close();
    assert(!hub.isOpen());
    assert(!manager.hasActiveGame());
    assert(okgdi::findWindowByClass(L"KieeKeyArcadeHubWindow") == nullptr);
    std::cout << "  [PASS] Hub timer / Esc / close lifecycle\n";
}

//---------------------------------------------------------------------------
void testChaosLabPreviewAndInjection() {
    using ok::app::ChaosLabWindow;

    // The lab remembers the application that had the focus before opening, so
    // the harness creates a stand-in "other application" window first.
    WNDCLASSEXW targetClass{};
    targetClass.cbSize = sizeof(WNDCLASSEXW);
    targetClass.lpfnWndProc = DefWindowProcW;
    targetClass.lpszClassName = L"TestTargetApp";
    ::RegisterClassExW(&targetClass);
    HWND fakeTarget = ::CreateWindowExW(0, L"TestTargetApp", L"Ứng dụng khác", WS_OVERLAPPEDWINDOW,
                                       0, 0, 400, 300, nullptr, nullptr, nullptr, nullptr);
    assert(fakeTarget != nullptr);
    ::SetForegroundWindow(fakeTarget);

    ChaosLabWindow& lab = ChaosLabWindow::instance();
    assert(lab.open(nullptr));
    HWND labWindow = static_cast<HWND>(lab.handle());
    assert(labWindow != nullptr);

    HWND input = okgdi::findControl(labWindow, 4001);
    HWND preview = okgdi::findControl(labWindow, 4002);
    HWND injectBox = okgdi::findControl(labWindow, 4013);
    HWND sendButton = okgdi::findControl(labWindow, 4031);
    assert(input != nullptr && preview != nullptr && injectBox != nullptr && sendButton != nullptr);

    // --- chaos OFF: the preview is byte-identical to what was typed ---------
    auto& chaos = ok::chaos::ChaosEngine::instance();
    ok::chaos::ChaosConfig config = chaos.getConfig();
    config.masterEnabled = false;
    config.randomCaseEnabled = false;
    config.glyphTransformEnabled = false;
    chaos.setConfig(config);

    const std::wstring typed = L"KieeKey chaos lab 123";
    ::SetWindowTextW(input, typed.c_str());
    ::SendMessageW(labWindow, WM_COMMAND,
                   MAKEWPARAM(4001, EN_CHANGE), reinterpret_cast<LPARAM>(input));
    assert(controlText(preview) == typed);

    // --- chaos ON (random case): same text, different casing ---------------
    config.masterEnabled = true;
    config.randomCaseEnabled = true;
    config.randomCaseIntensity = 1.0f;
    config.glyphTransformEnabled = false;
    chaos.setConfig(config);

    ::SendMessageW(labWindow, WM_COMMAND,
                   MAKEWPARAM(4001, EN_CHANGE), reinterpret_cast<LPARAM>(input));
    const std::wstring previewText = controlText(preview);
    assert(previewText.size() == typed.size());
    assert(previewText != typed);           // the case transform is visible
    for (std::size_t i = 0; i < typed.size(); ++i) {
        assert(std::towlower(typed[i]) == std::towlower(previewText[i]));
    }

    // --- injection: only when the checkbox is ticked -----------------------
    static std::wstring emitted;
    static int emitCount = 0;
    ChaosLabWindow::setEmitCallback([](const std::wstring& text) -> std::size_t {
        emitted = text;
        ++emitCount;
        return text.size();
    });

    ::SendMessageW(sendButton, BM_SETCHECK, BST_UNCHECKED, 0);
    ::SendMessageW(labWindow, WM_COMMAND,
                   MAKEWPARAM(4031, BN_CLICKED), reinterpret_cast<LPARAM>(sendButton));
    assert(emitCount == 0);                 // nothing is written while unchecked

    ::SendMessageW(injectBox, BM_SETCHECK, BST_CHECKED, 0);
    ::SendMessageW(labWindow, WM_COMMAND,
                   MAKEWPARAM(4031, BN_CLICKED), reinterpret_cast<LPARAM>(sendButton));
    assert(emitCount == 1);
    assert(emitted == previewText);         // exactly the previewed text
    assert(::GetForegroundWindow() == fakeTarget);   // focus handed back to the app

    // Reopening an already-focused lab must retain the external destination.
    ::SetForegroundWindow(labWindow);
    assert(lab.open(nullptr));
    const int beforeReopen = emitCount;
    ::SendMessageW(labWindow, WM_COMMAND, MAKEWPARAM(4031, BN_CLICKED), 0);
    assert(emitCount == beforeReopen + 1);
    assert(::GetForegroundWindow() == fakeTarget);

    // Windows can deny foreground activation. Never type into the lab then.
    ::SetForegroundWindow(labWindow);
    okgdi::denyForegroundChange(true);
    const int beforeDenied = emitCount;
    ::SendMessageW(labWindow, WM_COMMAND, MAKEWPARAM(4031, BN_CLICKED), 0);
    assert(emitCount == beforeDenied);
    okgdi::denyForegroundChange(false);

    // Controls reflect the engine on reopen; real trackbars use WM_HSCROLL,
    // not WM_COMMAND. Changing intensity must not turn master/case OFF.
    HWND slider = okgdi::findControl(labWindow, 4020);
    assert(::SendMessageW(okgdi::findControl(labWindow, 4010), BM_GETCHECK, 0, 0) == BST_CHECKED);
    ::SendMessageW(slider, TBM_SETPOS, TRUE, 25);
    ::SendMessageW(labWindow, WM_HSCROLL, 0, reinterpret_cast<LPARAM>(slider));
    assert(chaos.getConfig().masterEnabled);
    assert(chaos.getConfig().randomCaseEnabled);
    assert(chaos.getConfig().randomCaseIntensity == 0.25f);

    // --- the lab can drive Flexing Mode ------------------------------------
    ArcadeManager::instance().stopGame();
    HWND flexingBox = okgdi::findControl(labWindow, 4030);
    assert(flexingBox != nullptr);
    ::SendMessageW(flexingBox, BM_SETCHECK, BST_CHECKED, 0);
    ::SendMessageW(labWindow, WM_COMMAND,
                   MAKEWPARAM(4030, BN_CLICKED), reinterpret_cast<LPARAM>(flexingBox));
    assert(ArcadeManager::instance().getCurrentGameType() == GameType::Flexing);

    // --- Flexing page: the prepared passage is what appears ---------------
    HWND flexPrep = okgdi::findControl(labWindow, 4045);
    HWND flexLoad = okgdi::findControl(labWindow, 4043);
    HWND flexInput = okgdi::findControl(labWindow, 4040);
    HWND flexOutput = okgdi::findControl(labWindow, 4041);
    HWND flexGran = okgdi::findControl(labWindow, 4042);
    HWND flexSend = okgdi::findControl(labWindow, 4046);
    HWND flexPerChunk = okgdi::findControl(labWindow, 4044);
    assert(flexPrep != nullptr && flexLoad != nullptr && flexInput != nullptr);
    assert(flexOutput != nullptr && flexGran != nullptr && flexSend != nullptr);
    assert(flexPerChunk != nullptr);

    const std::wstring prepared = L"abcdef ghij";   // 11 characters
    ::SetWindowTextW(flexPrep, prepared.c_str());
    ::SendMessageW(labWindow, WM_COMMAND,
                   MAKEWPARAM(4043, BN_CLICKED), reinterpret_cast<LPARAM>(flexLoad));
    auto* flexGame = dynamic_cast<ok::arcade::FlexingGame*>(
        ArcadeManager::instance().getCurrentGame());
    assert(flexGame != nullptr);
    assert(flexGame->getPreloadedText().size() == prepared.size());
    assert(controlText(flexOutput).empty());

    // Each character typed into the flexing box steps the game and the engine
    // — never the user's own key — decides what lands in the output box.
    ::SetWindowTextW(flexInput, L"x");
    for (int step = 0; step < 4; ++step) {
        ::SendMessageW(labWindow, WM_COMMAND,
                       MAKEWPARAM(4040, EN_CHANGE), reinterpret_cast<LPARAM>(flexInput));
    }
    // A real edit sends EN_UPDATE before EN_CHANGE. The update notification
    // must not produce a second character.
    ::SendMessageW(labWindow, WM_COMMAND,
                   MAKEWPARAM(4040, EN_UPDATE), reinterpret_cast<LPARAM>(flexInput));
    const std::wstring produced = controlText(flexOutput);
    assert(produced == L"abcd");            // OneCharPerKey, from the passage
    assert(flexGame->getCursor() == 4);

    // Granularity changes preserve the cursor (word mode produces "ef " next).
    ::SendMessageW(flexGran, CB_SETCURSEL, 1, 0);
    ::SendMessageW(labWindow, WM_COMMAND,
                   MAKEWPARAM(4042, CBN_SELCHANGE), reinterpret_cast<LPARAM>(flexGran));
    ::SendMessageW(labWindow, WM_COMMAND,
                   MAKEWPARAM(4040, EN_CHANGE), reinterpret_cast<LPARAM>(flexInput));
    assert(controlText(flexOutput) == L"abcdef ");   // one whole word per key

    // The timer pump keeps the game alive without inventing output.
    ::SendMessageW(labWindow, WM_TIMER, ChaosLabWindow::kTimerId, 0);
    assert(controlText(flexOutput) == L"abcdef ");

    // "Gõ chữ Flexing ra app": hands the focus back and really types what the
    // engine produced — the button is an explicit action, the checkbox only
    // decides whether it goes out as one paste or chunk by chunk.
    const int beforeFlexSend = emitCount;
    ::SetForegroundWindow(labWindow);       // the lab has the focus while typing
    ::SendMessageW(labWindow, WM_COMMAND,
                   MAKEWPARAM(4046, BN_CLICKED), reinterpret_cast<LPARAM>(flexSend));
    assert(emitCount == beforeFlexSend + 1);
    assert(emitted == L"abcdef ");      // the engine's own output, in order
    assert(::GetForegroundWindow() == fakeTarget);

    // Per-chunk mode types the same text in several steps instead of one paste.
    emitCount = 0;
    emitted.clear();
    ::SendMessageW(flexPerChunk, BM_SETCHECK, BST_CHECKED, 0);
    ::SetForegroundWindow(labWindow);
    ::SendMessageW(labWindow, WM_COMMAND,
                   MAKEWPARAM(4046, BN_CLICKED), reinterpret_cast<LPARAM>(flexSend));
    assert(emitCount == 0);                 // UI stays responsive: queued, no Sleep
    ::SendMessageW(labWindow, WM_TIMER, 0xC041, 0);
    ::SendMessageW(labWindow, WM_TIMER, 0xC041, 0);
    assert(emitCount == 2);                 // 7 chars / 6 = two chunks
    assert(emitted == L" ");            // ...the last of them
    ::SendMessageW(flexPerChunk, BM_SETCHECK, BST_UNCHECKED, 0);

    // Focus lost between chunks cancels all remaining text.
    emitCount = 0;
    ::SendMessageW(flexPerChunk, BM_SETCHECK, BST_CHECKED, 0);
    ::SetForegroundWindow(labWindow);
    ::SendMessageW(labWindow, WM_COMMAND, MAKEWPARAM(4046, BN_CLICKED), 0);
    ::SendMessageW(labWindow, WM_TIMER, 0xC041, 0);
    assert(emitCount == 1);
    ::SetForegroundWindow(labWindow);
    ::SendMessageW(labWindow, WM_TIMER, 0xC041, 0);
    assert(emitCount == 1);
    ::SetForegroundWindow(fakeTarget);
    ::SendMessageW(labWindow, WM_TIMER, 0xC041, 0);
    assert(emitCount == 1);   // no automatic resumption after cancellation
    ::SendMessageW(flexPerChunk, BM_SETCHECK, BST_UNCHECKED, 0);

    // With nothing produced yet the button must not type silence into the app.
    ::SetWindowTextW(flexPrep, L"mot doan khac");
    ::SendMessageW(labWindow, WM_COMMAND,
                   MAKEWPARAM(4043, BN_CLICKED), reinterpret_cast<LPARAM>(flexLoad));
    assert(controlText(flexOutput).empty());
    emitCount = 0;
    ::SendMessageW(labWindow, WM_COMMAND,
                   MAKEWPARAM(4046, BN_CLICKED), reinterpret_cast<LPARAM>(flexSend));
    assert(emitCount == 0);
    assert(controlText(flexOutput).find(L"chua co chu nao") != std::wstring::npos);

    // Empty reload clears a previous passage; closing stops lab-owned Flexing.
    ::SetWindowTextW(flexPrep, L"");
    ::SendMessageW(labWindow, WM_COMMAND, MAKEWPARAM(4043, BN_CLICKED), 0);
    assert(flexGame->getPreloadedText().empty());
    ChaosLabWindow::setEmitCallback(nullptr);
    lab.close();
    assert(!lab.isOpen());
    assert(!ArcadeManager::instance().hasActiveGame());
    std::cout << "  [PASS] Chaos Lab preview & injection\n";
}

//---------------------------------------------------------------------------
void testUnicodeLabText() {
    using ok::app::codePointsFromWide;
    const std::wstring pair{static_cast<wchar_t>(0xD83D), static_cast<wchar_t>(0xDE00)};
    assert(codePointsFromWide(L"Việt " + pair) == U"Việt 😀");
    assert(codePointsFromWide(std::wstring(1, static_cast<wchar_t>(0xD83D))) == U"�");
    assert(codePointsFromWide(std::wstring(1, static_cast<wchar_t>(0xDE00))) == U"�");
    assert(codePointsFromWide(L"Việt 😀") == U"Việt 😀");
}

void testHubDpiAndShortcuts() {
    auto& hub = ok::app::ArcadeWindow::instance();
    auto& manager = ArcadeManager::instance();
    for (int dpi : {120, 144, 192}) {
        okgdi::setDpi(dpi);
        assert(hub.open(nullptr, "snake"));
        HWND hwnd = static_cast<HWND>(hub.handle());
        const int y = rowCentreY(2) * dpi / 96;
        ::SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(100, y));
        assert(hub.hoverIndexForTest() == 2);
        ::SendMessageW(hwnd, WM_LBUTTONDOWN, 0, MAKELPARAM(100, y));
        assert(manager.getCurrentGameType() == GameType::Fishing);
        // Alt+F4 must go to DefWindowProc, never the game input path.
        const auto before = manager.getCurrentGame()->isPaused();
        ::SendMessageW(hwnd, WM_SYSKEYDOWN, 0x70, 0); // Alt+F1
        assert(manager.getCurrentGame()->isPaused() == before);
        BYTE keys[256]{};
        keys[VK_CONTROL] = 0x80;
        okgdi::setKeyboardState(keys);
        ::SendMessageW(hwnd, WM_KEYDOWN, 0x70, 0); // Ctrl+F1 is not pause
        assert(manager.getCurrentGame()->isPaused() == before);
        keys[VK_CONTROL] = 0;
        okgdi::setKeyboardState(keys);
        ::SendMessageW(hwnd, WM_KEYDOWN, 0x70, 0); // bare F1 is pause
        assert(manager.getCurrentGame()->isPaused() != before);
        hub.close();
    }
    okgdi::setDpi(96);
}

void testPassageCellsNeverOverlap() {
    auto& hub = ok::app::ArcadeWindow::instance();
    auto& manager = ArcadeManager::instance();
    for (int dpi : {96, 120, 144, 192}) {
        okgdi::setDpi(dpi);
        for (const char* slug : {"typing-race", "fishing", "no-mistake", "wasd-race"}) {
            assert(hub.open(nullptr, slug));
            HWND hwnd = static_cast<HWND>(hub.handle());
            for (int step = 0; step < 90; ++step) {
                const auto& frame = manager.getFrame();
                char32_t next = 0;
                for (const auto& line : frame.texts) {
                    if (line.advance > 0 && !line.text.empty() &&
                        (line.color == palette::kAccent || line.color == palette::kAccent2)) {
                        next = line.text.front(); break;
                    }
                }
                paint(hwnd);
                int right = -1;
                std::size_t cells = 0;
                for (const auto& call : okgdi::log()) {
                    if (call.kind != "celltext") { continue; }
                    ++cells;
                    assert(call.c > 0); // explicitly clipped cell, never natural-width run
                    assert(call.a >= right);
                    right = call.a + call.c;
                    // Font and cell share ONE viewport scale; DPI must not be
                    // multiplied into text a second time at 125/150/200%.
                    assert(call.fontHeight <= call.c * 2 + 2);
                    assert(right <= ok::app::ArcadeWindow::kDefaultWidth);
                }
                assert(cells > 0 && cells < 70);
                if (next != 0) { manager.handleKey(0, next, true); }
            }
            hub.close();
        }
    }
    okgdi::setDpi(96);
    std::cout << "  [PASS] live passage cells: 4 games x 4 DPIs x 90 frames, no overlap\n";
}

int main() {
    std::cout << "=== Running Arcade Hub UI (Win32 GDI) Suite ===\n";
    testHubSidebarShowsTheWholeCatalog();
    testHubHoverAndClickStartAGame();
    testHubKeyboardDrivesTheGame();
    testHubTimerEscAndClose();
    testChaosLabPreviewAndInjection();
    testUnicodeLabText();
    testHubDpiAndShortcuts();
    testPassageCellsNeverOverlap();
    okgdi::destroyAllWindows();
    std::cout << "=== ALL ARCADE WINDOW UI TESTS PASSED ===\n";
    return 0;
}
