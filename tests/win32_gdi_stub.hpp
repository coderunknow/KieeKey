//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/win32_gdi_stub.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — tests/win32_gdi_stub.hpp
// The introspection API of the Win32 GDI recorder (win32_gdi_stub.cpp): what a
// UI test can ask about the frames the Arcade Hub / Chaos Lab painted, and how
// it injects mouse/keyboard messages.
//----------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

#include "win32_gdi_shim.hpp"

namespace okgdi {
void denyForegroundChange(bool deny);
void setDpi(int dpi);

struct DrawCall {
    std::string kind;      // "rect" | "roundrect" | "ellipse" | "line" | "polygon" | "text" |
                           // "gradient" | "bitblt"
    int fontHeight = 0;
    int a = 0;             // x / x1 / left
    int b = 0;             // y / y1 / top
    int c = 0;             // w / x2
    int d = 0;             // h / y2
    COLORREF color = 0;    // brush / text colour
    std::wstring text;     // text calls only
};

std::vector<DrawCall>& log();
void clearLog();

// v1.3.0-beta5 (bug B8): every MessageBoxW call is recorded as
// "<caption> | <text>" so tests can assert the failure surfacing fires.
std::vector<std::wstring>& messageBoxes();
void clearMessageBoxes();
int framePresents();                                   // "bitblt" calls = painted frames
bool logContainsText(const std::wstring& needle);
int countCalls(const char* kind);
int countTextWithColor(COLORREF color);
COLORREF brushColor(HBRUSH brush);   // the colour a fake brush was created with

// Keyboard state seen by ToUnicode()/GetKeyboardState().
void setShiftDown(bool down);
void setKeyboardState(const BYTE* state);
const BYTE* keyboardState();

// Window/control lookup.
HWND findWindowByClass(const std::wstring& windowClass);
HWND findControl(HWND parent, int controlId);
HWND lastCreatedWindow();
HWND foregroundWindow();
std::vector<HWND> allWindows();
void destroyAllWindows();

} // namespace okgdi
