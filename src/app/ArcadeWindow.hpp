//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: src/app/ArcadeWindow.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — ArcadeWindow.hpp
// The Arcade Hub as a real Win32 window, drawn with GDI (vector shapes, no
// console, no text-mode fallback).
//
//   * Owns nothing but the window: the games live in `ok::arcade::ArcadeManager`
//     and the window only paints the `RenderList` it is handed, exactly like the
//     HTML5 client does with the same list.
//   * WM_TIMER at ~16 ms drives update+paint (the same cadence as the browser
//     client's server-side tick thread).
//   * Keyboard goes through the same `handleKey()` entry point as the web client,
//     so pause/restart/exit behave identically in both front-ends.
//   * The window is closed (not destroyed) on Esc/Hub close: `stopGame()`
//     already reports the run to progression.
//
// The header is deliberately free of <windows.h> so the layout constants and
// the public API can be reviewed (and included by docs/tests) anywhere.
//----------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <string>

namespace ok::app {

// Forward declaration of the Win32 handle type: defined as `void*` here so this
// header stays portable; the implementation reinterprets it as HWND.
using NativeWindowHandle = void*;

class ArcadeWindow {
public:
    // Layout constants shared with the GDI painter (device-independent units,
    // scaled by the monitor DPI at paint time).
    static constexpr int kDefaultWidth = 1180;
    static constexpr int kDefaultHeight = 760;
    static constexpr int kSidebarWidth = 280;
    static constexpr int kFooterHeight = 64;
    static constexpr int kTimerId = 0xA57C;
    static constexpr int kTimerIntervalMs = 16;

    static ArcadeWindow& instance() noexcept;

    // Creates the window (or brings it to the front when it already exists) and
    // starts a game. `slugOrEmpty` chooses the game; empty keeps the last one.
    bool open(NativeWindowHandle owner, const std::string& slugOrEmpty = {});

    // Opens the hub with the given game already running (L"snake", "tetris", ...).
    bool openGame(NativeWindowHandle owner, const std::string& slug);

    void close();
    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] NativeWindowHandle handle() const noexcept;

    // Called by the host message loop when the settings tab wants the hub to
    // take over the keyboard.
    void focus();

    // Catalogue row under the mouse (-1 when none). The window procedure lives
    // outside the class, so the hover state has these two tiny accessors; they
    // are also what an automated UI test can assert against.
    [[nodiscard]] int hoverIndexForTest() const noexcept;
    void setHoverIndex(int index) noexcept;
    [[nodiscard]] double dpiScale() const noexcept;

    // v1.3.0-beta8 (bug UX-10): WM_DPICHANGED used to write the new scale by
    // reaching into `self->m_impl` from the free window procedure — a PRIVATE
    // member, so the translation unit did not compile at all on any conforming
    // compiler. The hub is only built into the Windows app target and the one
    // portable harness that exercises it (ok_arcade_window_tests) was itself
    // unwired from CMake, so nothing ever compiled this file and the break
    // shipped unnoticed. The setter mirrors the existing dpiScale() getter.
    void setDpiScale(double scale) noexcept;

    // One game frame + one repaint. Public so the host can drive it from its own
    // timer if WM_TIMER is not available (tests/automation).
    void pump(double dtSeconds);

    // Paints the window on demand. Called by the window procedure on WM_PAINT;
    // public because the procedure lives outside the class, and it takes the
    // native handle (not a HDC) so this header stays free of <windows.h>.
    static void paintNow(NativeWindowHandle handle);

private:
    ArcadeWindow();
    ~ArcadeWindow() = default;
    ArcadeWindow(const ArcadeWindow&) = delete;
    ArcadeWindow& operator=(const ArcadeWindow&) = delete;

    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace ok::app
