//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/win32_gdi_stub.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — tests/win32_gdi_stub.cpp
// Implementation of the USER32/GDI32 surface declared in win32_gdi_shim.hpp.
//
// It is a RECORDER, not an emulator:
//   * windows/classes exist as records, so WM_* messages can be delivered to
//     the real window procedures through SendMessageW();
//   * every drawing call is appended to a call log (kind + parameters + text),
//     which is what the tests assert on ("the sidebar lists all eight games",
//     "the hovered row is highlighted", …);
//   * DCs, brushes, pens and fonts are opaque tokens — the code under test only
//     ever passes them back to the API, exactly like it would on Windows.
//----------------------------------------------------------------------------
#include "win32_gdi_stub.hpp"

#include <commctrl.h>   // the stub emulates the common controls too

#include <cstring>
#include <cwchar>
#include <map>
#include <string>
#include <vector>

namespace okgdi {
static bool g_denyForeground = false;
static int g_dpi = 96;
void denyForegroundChange(bool deny) { g_denyForeground = deny; }
void setDpi(int dpi) { g_dpi = dpi; }

std::vector<DrawCall>& log() {
    static std::vector<DrawCall> calls;
    return calls;
}

void clearLog() { log().clear(); }

std::vector<std::wstring>& messageBoxes() {
    static std::vector<std::wstring> boxes;
    return boxes;
}

void clearMessageBoxes() { messageBoxes().clear(); }

int framePresents() {
    int count = 0;
    for (const DrawCall& call : log()) {
        if (call.kind == "bitblt") { ++count; }
    }
    return count;
}

bool logContainsText(const std::wstring& needle) {
    for (const DrawCall& call : log()) {
        if (call.kind == "text" && call.text.find(needle) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

int countCalls(const char* kind) {
    int count = 0;
    for (const DrawCall& call : log()) {
        if (call.kind == kind) { ++count; }
    }
    return count;
}

int countTextWithColor(COLORREF color) {
    int count = 0;
    for (const DrawCall& call : log()) {
        if (call.kind == "text" && call.color == color) { ++count; }
    }
    return count;
}

namespace {
BYTE g_keyboardState[256]{};
} // namespace

void setShiftDown(bool down) { g_keyboardState[VK_SHIFT] = down ? 0x80 : 0x00; }
void setKeyboardState(const BYTE* state) { std::memcpy(g_keyboardState, state, 256); }
const BYTE* keyboardState() { return g_keyboardState; }

struct Window {
    HWND hwnd = nullptr;
    std::wstring windowClass;
    std::wstring title;
    LRESULT (*proc)(HWND, UINT, WPARAM, LPARAM) = nullptr;
    LONG_PTR userData = 0;
    int controlId = 0;   // reinterpret_cast<INT_PTR>(hMenu) for child controls
    HWND parent = nullptr;
    RECT client{0, 0, 1180, 760};
    bool visible = false;
    bool destroyed = false;
    bool checked = false;      // BM_SETCHECK state
    int  position = 0;         // trackbar position
    int  selection = -1;       // combo box selection
    std::vector<std::wstring> items;
    std::map<UINT_PTR, UINT> timers;
    UINT_PTR nextTimerId = 1;
};

namespace {
std::map<std::wstring, WNDCLASSEXW>& classRegistry() {
    static std::map<std::wstring, WNDCLASSEXW> registry;
    return registry;
}
std::vector<Window*>& windowRegistry() {
    static std::vector<Window*> registry;
    return registry;
}
Window* findWindow(HWND hwnd) {
    for (Window* window : windowRegistry()) {
        if (reinterpret_cast<HWND>(window) == hwnd && !window->destroyed) { return window; }
    }
    return nullptr;
}
HWND g_lastCreated = nullptr;
HWND g_foreground = nullptr;

// Handle -> colour table: the fake brushes/pens remember the colour they were
// created with, so a drawing call can be reported with the colour that was
// actually selected into the DC (like a real GDI spy tool would).
std::map<std::uintptr_t, COLORREF>& handleColors() {
    static std::map<std::uintptr_t, COLORREF> colors;
    return colors;
}

std::uintptr_t nextHandle(std::uintptr_t tag) {
    static std::uintptr_t counter = 1;
    return tag | (++counter << 16);
}

struct DcState {
    HGDIOBJ brush = nullptr;
    HGDIOBJ pen = nullptr;
    HGDIOBJ font = nullptr;
};

std::map<HDC, DcState>& dcStates() {
    static std::map<HDC, DcState> states;
    return states;
}

COLORREF handleColor(HGDIOBJ object) {
    const auto it = handleColors().find(reinterpret_cast<std::uintptr_t>(object));
    return (it != handleColors().end()) ? it->second : 0;
}

COLORREF selectedColor(HDC dc, bool pen) {
    const auto it = dcStates().find(dc);
    if (it == dcStates().end()) { return 0; }
    return handleColor(pen ? it->second.pen : it->second.brush);
}
DWORD g_lastError = 0;
COLORREF g_textColor = 0;
int g_backgroundMode = 0;
UINT g_textAlign = 0;
} // namespace

HWND lastCreatedWindow() { return g_lastCreated; }
HWND foregroundWindow() { return g_foreground; }

std::vector<HWND> allWindows() {
    std::vector<HWND> handles;
    for (Window* window : windowRegistry()) {
        if (!window->destroyed) { handles.push_back(reinterpret_cast<HWND>(window)); }
    }
    return handles;
}

HWND findControl(HWND parent, int controlId) {
    for (Window* window : windowRegistry()) {
        if (!window->destroyed && window->parent == parent && window->controlId == controlId) {
            return reinterpret_cast<HWND>(window);
        }
    }
    return nullptr;
}

HWND findWindowByClass(const std::wstring& windowClass) {
    for (Window* window : windowRegistry()) {
        if (!window->destroyed && window->windowClass == windowClass) {
            return reinterpret_cast<HWND>(window);
        }
    }
    return nullptr;
}

void destroyAllWindows() {
    for (Window* window : windowRegistry()) {
        delete window;
    }
    windowRegistry().clear();
    classRegistry().clear();
    g_lastCreated = nullptr;
    g_foreground = nullptr;
    clearLog();
}

COLORREF brushColor(HBRUSH brush) {
    return handleColor(reinterpret_cast<HGDIOBJ>(brush));
}

} // namespace okgdi

//===========================================================================
// USER32
//===========================================================================
extern "C" {

ATOM RegisterClassExW(const WNDCLASSEXW* wc) {
    if (wc == nullptr || wc->lpszClassName == nullptr) { return 0; }
    auto& registry = okgdi::classRegistry();
    const std::wstring name = wc->lpszClassName;
    if (registry.find(name) != registry.end()) {
        ::SetLastError(ERROR_CLASS_ALREADY_EXISTS);
        return 0;   // "already registered" — exactly what Windows reports
    }
    registry.emplace(name, *wc);
    ::SetLastError(0);
    return 1;
}

HWND CreateWindowExW(DWORD, LPCWSTR className, LPCWSTR title, DWORD style, int x, int y, int width,
                     int height, HWND parent, HMENU menuId, HINSTANCE, LPVOID param) {
    const std::wstring name = (className != nullptr) ? className : L"";
    // The standard control classes exist without a RegisterClassExW call, just
    // like on Windows: they answer DefWindowProc (their state lives in the
    // stub's Window record and in the SendMessageW intercept).
    const bool standardControl = (name == L"BUTTON" || name == L"EDIT" || name == L"STATIC" ||
                                 name == L"COMBOBOX" || name == L"LISTBOX" ||
                                 name == TRACKBAR_CLASSW || name == TOOLTIPS_CLASSW);
    auto it = okgdi::classRegistry().find(name);
    if (it == okgdi::classRegistry().end() && !standardControl) {
        return nullptr;   // window class was never registered
    }
    auto* window = new okgdi::Window();
    window->hwnd = reinterpret_cast<HWND>(window);
    window->windowClass = name;
    window->title = (title != nullptr) ? title : L"";
    window->proc = (it != okgdi::classRegistry().end()) ? it->second.lpfnWndProc : DefWindowProcW;
    window->parent = parent;
    window->controlId = static_cast<int>(reinterpret_cast<INT_PTR>(menuId));
    window->client = RECT{0, 0, width, height};
    window->visible = (style & WS_VISIBLE) != 0;
    okgdi::windowRegistry().push_back(window);
    okgdi::g_lastCreated = window->hwnd;
    if (window->proc == nullptr) {
        return window->hwnd;   // a class without a procedure accepts no messages
    }
    (void)x;
    (void)y;

    // Windows sends WM_NCCREATE (with lpCreateParams) before WM_CREATE; the
    // code under test stores `this` from it.
    CREATESTRUCTW create{};
    create.lpCreateParams = param;
    create.lpszClass = className;
    create.lpszName = title;
    create.style = static_cast<LONG>(style);
    create.cx = width;
    create.cy = height;
    window->proc(window->hwnd, WM_NCCREATE, 0, reinterpret_cast<LPARAM>(&create));
    return window->hwnd;
}

BOOL DestroyWindow(HWND hwnd) {
    okgdi::Window* window = okgdi::findWindow(hwnd);
    if (window == nullptr) { return FALSE; }
    window->proc(hwnd, WM_DESTROY, 0, 0);
    window->destroyed = true;
    if (okgdi::g_foreground == hwnd) { okgdi::g_foreground = nullptr; }
    return TRUE;
}

BOOL ShowWindow(HWND hwnd, int) {
    okgdi::Window* window = okgdi::findWindow(hwnd);
    if (window == nullptr) { return FALSE; }
    window->visible = true;
    return TRUE;
}

BOOL IsWindow(HWND hwnd) { return okgdi::findWindow(hwnd) != nullptr ? TRUE : FALSE; }

BOOL InvalidateRect(HWND, const RECT*, BOOL) { return TRUE; }
// v1.3.0-beta8 (bug UX-10): mouse-leave tracking is a no-op off Windows; the
// harness drives WM_MOUSELEAVE directly when it wants to test hover clearing.
BOOL TrackMouseEvent(TRACKMOUSEEVENT*) { return TRUE; }

BOOL GetClientRect(HWND hwnd, RECT* out) {
    okgdi::Window* window = okgdi::findWindow(hwnd);
    if (window == nullptr || out == nullptr) { return FALSE; }
    *out = window->client;
    return TRUE;
}

BOOL AdjustWindowRectEx(RECT* rect, DWORD, BOOL, DWORD) {
    // A real frame adds borders; the stub keeps the client size identical so
    // layout assertions do not depend on the window manager.
    (void)rect;
    return TRUE;
}

int MessageBoxW(HWND hwnd, LPCWSTR text, LPCWSTR caption, UINT type) {
    (void)hwnd;
    (void)type;
    std::wstring entry = (caption != nullptr) ? caption : L"";
    entry += L" | ";
    entry += (text != nullptr) ? text : L"";
    okgdi::messageBoxes().push_back(entry);
    return IDOK;
}

BOOL SetWindowTextW(HWND hwnd, LPCWSTR text) {
    okgdi::Window* window = okgdi::findWindow(hwnd);
    if (window == nullptr) { return FALSE; }
    window->title = (text != nullptr) ? text : L"";
    return TRUE;
}

int GetWindowTextW(HWND hwnd, LPWSTR buffer, int maxCount) {
    okgdi::Window* window = okgdi::findWindow(hwnd);
    if (window == nullptr || buffer == nullptr || maxCount <= 0) { return 0; }
    const int length = static_cast<int>(window->title.size());
    const int copied = (length < maxCount - 1) ? length : (maxCount - 1);
    std::memcpy(buffer, window->title.c_str(), static_cast<std::size_t>(copied) * sizeof(wchar_t));
    buffer[copied] = L'\0';
    return copied;
}

int GetWindowTextLengthW(HWND hwnd) {
    okgdi::Window* window = okgdi::findWindow(hwnd);
    return (window != nullptr) ? static_cast<int>(window->title.size()) : 0;
}

LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM) { return 0; }

LRESULT SendMessageW(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    okgdi::Window* window = okgdi::findWindow(hwnd);
    if (window == nullptr) { return 0; }
    // Control messages the two window implementations read back (checkbox
    // state, combo selection, trackbar position) are answered by the stub's own
    // per-window state, exactly like the real controls would.
    switch (message) {
        case BM_SETCHECK:
            window->checked = (wParam == BST_CHECKED);
            return 0;
        case BM_GETCHECK:
            return window->checked ? BST_CHECKED : BST_UNCHECKED;
        case CB_ADDSTRING: {
            const auto* text = reinterpret_cast<const wchar_t*>(lParam);
            window->items.emplace_back(text != nullptr ? text : L"");
            return static_cast<LRESULT>(window->items.size()) - 1;
        }
        case CB_SETCURSEL:
            window->selection = static_cast<int>(wParam);
            return 0;
        case CB_GETCURSEL:
            return window->selection;
        // Minimal EDIT support: the two lab windows append produced text with
        // EM_SETSEL + EM_REPLACESEL, the way real edit controls are scripted.
        case EM_SETSEL:
            window->selection = static_cast<int>(wParam);
            return 0;
        case EM_REPLACESEL: {
            const auto* text = reinterpret_cast<const wchar_t*>(lParam);
            const int at = (window->selection >= 0 &&
                            window->selection <= static_cast<int>(window->title.size()))
                               ? window->selection
                               : static_cast<int>(window->title.size());
            const std::wstring inserted = (text != nullptr) ? text : L"";
            window->title.insert(static_cast<std::size_t>(at), inserted);
            window->selection = at + static_cast<int>(inserted.size());
            return 0;
        }
        case TBM_SETRANGE:
            window->position = window->position;   // range is not asserted on
            return 0;
        case TBM_SETPOS:
            if (wParam != 0) { window->position = static_cast<int>(lParam); }
            return 0;
        case TBM_GETPOS:
            return window->position;
        default:
            break;
    }
    if (window->proc == nullptr) { return 0; }
    return window->proc(hwnd, message, wParam, lParam);
}

LONG_PTR GetWindowLongPtrW(HWND hwnd, int index) {
    okgdi::Window* window = okgdi::findWindow(hwnd);
    if (window == nullptr) { return 0; }
    if (index == GWLP_USERDATA) { return window->userData; }
    return 0;
}

LONG_PTR SetWindowLongPtrW(HWND hwnd, int index, LONG_PTR value) {
    okgdi::Window* window = okgdi::findWindow(hwnd);
    if (window == nullptr) { return 0; }
    if (index == GWLP_USERDATA) {
        const LONG_PTR previous = window->userData;
        window->userData = value;
        return previous;
    }
    return 0;
}

HWND GetForegroundWindow(void) { return okgdi::g_foreground; }

DWORD GetCurrentProcessId(void) { return 1; }
DWORD GetWindowThreadProcessId(HWND hwnd, DWORD* processId) {
    const auto* window = okgdi::findWindow(hwnd);
    *processId = window == nullptr ? 0 : (window->windowClass == L"TestTargetApp" ? 2 : 1);
    return *processId;
}

BOOL SetForegroundWindow(HWND hwnd) {
    if (okgdi::g_denyForeground) { return FALSE; }
    okgdi::g_foreground = hwnd;
    return TRUE;
}

HWND SetFocus(HWND hwnd) { return hwnd; }

HCURSOR LoadCursorW(HINSTANCE, LPCWSTR) { return reinterpret_cast<HCURSOR>(1); }
HCURSOR SetCursor(HCURSOR cursor) { return cursor; }

UINT_PTR SetTimer(HWND hwnd, UINT_PTR id, UINT, void (*)(HWND, UINT, UINT_PTR, DWORD)) {
    okgdi::Window* window = okgdi::findWindow(hwnd);
    if (window == nullptr) { return 0; }
    const UINT_PTR used = (id != 0) ? id : ++window->nextTimerId;
    window->timers[used] = 0;
    return used;
}

BOOL KillTimer(HWND hwnd, UINT_PTR id) {
    okgdi::Window* window = okgdi::findWindow(hwnd);
    if (window == nullptr) { return FALSE; }
    window->timers.erase(id);
    return TRUE;
}

BOOL GetKeyboardState(BYTE* state) {
    if (state == nullptr) { return FALSE; }
    std::memcpy(state, okgdi::keyboardState(), 256);
    return TRUE;
}

int ToUnicode(UINT vk, UINT, const BYTE* state, LPWSTR buffer, int count, UINT) {
    if (buffer == nullptr || count <= 0) { return 0; }
    const bool shift = (state != nullptr) && (state[VK_SHIFT] & 0x80) != 0;
    const bool caps = (state != nullptr) && (state[VK_CAPITAL] & 0x01) != 0;
    wchar_t ch = 0;
    if (vk >= 'A' && vk <= 'Z') {
        ch = static_cast<wchar_t>((shift != caps) ? vk : (vk - 'A' + 'a'));
    } else if (vk >= '0' && vk <= '9') {
        ch = static_cast<wchar_t>(vk);
    } else if (vk == VK_SPACE) {
        ch = L' ';
    } else if (vk == VK_ESCAPE) {
        ch = 0x1B;
    } else if (vk == VK_RETURN) {
        ch = L'\r';
    } else if (vk == VK_BACK) {
        ch = 0x08;
    } else {
        return 0;   // arrows, F-keys, … produce no character
    }
    buffer[0] = ch;
    return 1;
}

HINSTANCE GetModuleHandleW(LPCWSTR) { return reinterpret_cast<HINSTANCE>(1); }
DWORD GetLastError(void) { return okgdi::g_lastError; }
void SetLastError(DWORD code) { okgdi::g_lastError = code; }
void Sleep(DWORD) {}

} // extern "C"

//===========================================================================
// GDI32
//===========================================================================
extern "C" {

HDC BeginPaint(HWND hwnd, PAINTSTRUCT* ps) {
    okgdi::Window* window = okgdi::findWindow(hwnd);
    if (ps != nullptr) {
        *ps = PAINTSTRUCT{};
        ps->hdc = reinterpret_cast<HDC>(4);
        ps->rcPaint = (window != nullptr) ? window->client : RECT{0, 0, 0, 0};
    }
    return reinterpret_cast<HDC>(4);
}

BOOL EndPaint(HWND, const PAINTSTRUCT*) { return TRUE; }

HDC GetDC(HWND) { return reinterpret_cast<HDC>(1); }
int ReleaseDC(HWND, HDC) { return 1; }
HDC CreateCompatibleDC(HDC) { return reinterpret_cast<HDC>(2); }
HBITMAP CreateCompatibleBitmap(HDC, int, int) { return reinterpret_cast<HBITMAP>(3); }
BOOL DeleteDC(HDC) { return TRUE; }
BOOL DeleteObject(HGDIOBJ) { return TRUE; }

HGDIOBJ SelectObject(HDC dc, HGDIOBJ object) {
    okgdi::DcState& state = okgdi::dcStates()[dc];
    const std::uintptr_t tag = reinterpret_cast<std::uintptr_t>(object) & 0xF000u;
    HGDIOBJ previous = nullptr;
    if (tag == 0x1000u) {
        previous = state.brush;
        state.brush = object;
    } else if (tag == 0x2000u) {
        previous = state.pen;
        state.pen = object;
    } else if (tag == 0x3000u) {
        previous = state.font;
        state.font = object;
    } else if (tag == 0x4000u) {
        // stock object: NULL_PEN/NULL_BRUSH or a stock pen
        if (reinterpret_cast<std::uintptr_t>(object) >> 12 == 0x4001u) {
            previous = state.pen;
            state.pen = object;
        } else {
            previous = state.brush;
            state.brush = object;
        }
    }
    return previous;
}

HGDIOBJ GetStockObject(int kind) {
    // 0x4001 = stock pen, anything else = stock brush (see SelectObject).
    const std::uintptr_t value = (kind == NULL_PEN) ? 0x4001000u : 0x4002000u;
    return reinterpret_cast<HGDIOBJ>(value);
}

HBRUSH CreateSolidBrush(COLORREF color) {
    const std::uintptr_t handle = okgdi::nextHandle(0x1000u);
    okgdi::handleColors()[handle] = color;
    return reinterpret_cast<HBRUSH>(handle);
}

HPEN CreatePen(int, int width, COLORREF color) {
    const std::uintptr_t handle = okgdi::nextHandle(0x2000u);
    okgdi::handleColors()[handle] = color;
    (void)width;
    return reinterpret_cast<HPEN>(handle);
}

HFONT CreateFontIndirectW(const LOGFONTW* lf) {
    if (lf == nullptr) { return nullptr; }
    const std::uintptr_t handle = okgdi::nextHandle(0x3000u);
    okgdi::handleColors()[handle] = static_cast<COLORREF>(-lf->lfHeight);
    return reinterpret_cast<HFONT>(handle);
}

// v1.3.0-beta3 (bug #1, Chaos Lab font): the Lab now builds a DPI-scaled Segoe UI
// face and pushes it onto every child control. These faithful stand-ins let that
// Windows-only code compile, link and run on a host without the Windows SDK.
HFONT CreateFontW(int cHeight, int, int, int, int, DWORD, DWORD, DWORD, DWORD,
                  DWORD, DWORD, DWORD, DWORD, LPCWSTR) {
    const std::uintptr_t handle = okgdi::nextHandle(0x3000u);
    okgdi::handleColors()[handle] = static_cast<COLORREF>(-cHeight);
    return reinterpret_cast<HFONT>(handle);
}

BOOL EnumChildWindows(HWND hwndParent, WNDENUMPROC lpEnumFunc, LPARAM lParam) {
    if (lpEnumFunc == nullptr) { return TRUE; }
    for (okgdi::Window* window : okgdi::windowRegistry()) {
        if (!window->destroyed && window->parent == hwndParent) {
            if (!lpEnumFunc(reinterpret_cast<HWND>(window), lParam)) { return FALSE; }
        }
    }
    return TRUE;
}

int MulDiv(int nNumber, int nNumerator, int nDenominator) {
    if (nDenominator == 0) { return 0; }
    return static_cast<int>((static_cast<long long>(nNumber) * nNumerator) / nDenominator);
}

// Returning null makes the app code take its classic GetDeviceCaps(LOGPIXELSX)
// DPI fallback — exactly the path an older SDK / MinGW host would exercise.
FARPROC GetProcAddress(HMODULE, const char*) { return nullptr; }

// v1.3.0-beta3 (bug #1): the Lab adopts the system-suggested rect on WM_DPICHANGED.
BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT) { return TRUE; }

int GetDeviceCaps(HDC, int index) {
    return (index == LOGPIXELSX) ? okgdi::g_dpi : 0;
}

BOOL BitBlt(HDC, int x, int y, int width, int height, HDC, int, int, DWORD) {
    okgdi::DrawCall call;
    call.kind = "bitblt";
    call.a = x;
    call.b = y;
    call.c = width;
    call.d = height;
    okgdi::log().push_back(call);
    return TRUE;
}

int FillRect(HDC, const RECT* rect, HBRUSH brush) {
    if (rect == nullptr) { return 0; }
    okgdi::DrawCall call;
    call.kind = "rect";
    call.a = rect->left;
    call.b = rect->top;
    call.c = rect->right - rect->left;
    call.d = rect->bottom - rect->top;
    call.color = okgdi::handleColor(reinterpret_cast<HGDIOBJ>(brush));
    okgdi::log().push_back(call);
    return 1;
}

BOOL Rectangle(HDC dc, int left, int top, int right, int bottom) {
    okgdi::DrawCall call;
    call.kind = "rect";
    call.a = left;
    call.b = top;
    call.c = right - left;
    call.d = bottom - top;
    call.color = okgdi::selectedColor(dc, /*pen=*/false);
    okgdi::log().push_back(call);
    return TRUE;
}

BOOL RoundRect(HDC dc, int left, int top, int right, int bottom, int, int) {
    okgdi::DrawCall call;
    call.kind = "roundrect";
    call.a = left;
    call.b = top;
    call.c = right - left;
    call.d = bottom - top;
    call.color = okgdi::selectedColor(dc, /*pen=*/false);
    okgdi::log().push_back(call);
    return TRUE;
}

BOOL Ellipse(HDC dc, int left, int top, int right, int bottom) {
    okgdi::DrawCall call;
    call.kind = "ellipse";
    call.a = left;
    call.b = top;
    call.c = right - left;
    call.d = bottom - top;
    call.color = okgdi::selectedColor(dc, /*pen=*/false);
    okgdi::log().push_back(call);
    return TRUE;
}

BOOL Polygon(HDC dc, const POINT* points, int count) {
    okgdi::DrawCall call;
    call.kind = "polygon";
    call.c = count;
    call.color = okgdi::selectedColor(dc, /*pen=*/false);
    if (points != nullptr && count > 0) {
        call.a = points[0].x;
        call.b = points[0].y;
    }
    okgdi::log().push_back(call);
    return TRUE;
}

BOOL MoveToEx(HDC, int x, int y, POINT* previous) {
    if (previous != nullptr) { *previous = POINT{x, y}; }
    return TRUE;
}

BOOL LineTo(HDC dc, int x, int y) {
    okgdi::DrawCall call;
    call.kind = "line";
    call.a = x;
    call.b = y;
    call.color = okgdi::selectedColor(dc, /*pen=*/true);
    okgdi::log().push_back(call);
    return TRUE;
}

BOOL TextOutW(HDC dc, int x, int y, LPCWSTR text, int count) {
    okgdi::DrawCall call;
    call.kind = "text";
    call.fontHeight = static_cast<int>(okgdi::handleColors()[reinterpret_cast<std::uintptr_t>(okgdi::dcStates()[dc].font)]);
    call.a = x;
    call.b = y;
    call.d = count;
    call.color = okgdi::g_textColor;
    if (text != nullptr && count > 0) {
        call.text.assign(text, static_cast<std::size_t>(count));
    }
    okgdi::log().push_back(call);
    return TRUE;
}

BOOL ExtTextOutW(HDC dc, int x, int y, UINT options, const RECT* rect, LPCWSTR text, UINT count, const int*) {
    const auto result = TextOutW(dc, x, y, text, static_cast<int>(count));
    auto& call = okgdi::log().back();
    call.kind = "celltext";
    call.c = rect != nullptr ? rect->right - rect->left : 0;
    if (options != ETO_CLIPPED) { call.c = -1; }
    return result;
}

BOOL GetTextExtentPoint32W(HDC, LPCWSTR text, int count, SIZE* out) {
    if (out == nullptr) { return FALSE; }
    // Deterministic 7 px per character: enough for the alignment maths.
    out->cx = static_cast<LONG>(count) * 7;
    out->cy = 16;
    (void)text;
    return TRUE;
}

BOOL GetTextMetricsW(HDC dc, TEXTMETRICW* out) {
    if (out == nullptr) { return FALSE; }
    *out = TEXTMETRICW{};
    out->tmHeight = static_cast<int>(okgdi::handleColors()[reinterpret_cast<std::uintptr_t>(okgdi::dcStates()[dc].font)]);
    if (out->tmHeight == 0) { out->tmHeight = 16; }
    out->tmAscent = 12;
    out->tmDescent = 4;
    return TRUE;
}

int SetBkMode(HDC, int mode) {
    const int previous = okgdi::g_backgroundMode;
    okgdi::g_backgroundMode = mode;
    return previous;
}

COLORREF SetTextColor(HDC, COLORREF color) {
    const COLORREF previous = okgdi::g_textColor;
    okgdi::g_textColor = color;
    return previous;
}

UINT SetTextAlign(HDC, UINT align) {
    const UINT previous = okgdi::g_textAlign;
    okgdi::g_textAlign = align;
    return previous;
}

HRGN CreateRectRgn(int, int, int, int) { return reinterpret_cast<HRGN>(5); }
int SelectClipRgn(HDC, HRGN) { return 1; }

BOOL GradientFill(HDC, TRIVERTEX* vertices, ULONG vertexCount, void*, ULONG, ULONG) {
    okgdi::DrawCall call;
    call.kind = "gradient";
    call.c = static_cast<int>(vertexCount);
    if (vertices != nullptr && vertexCount >= 2) {
        call.a = vertices[0].x;
        call.b = vertices[0].y;
        call.d = vertices[1].y;
    }
    okgdi::log().push_back(call);
    return TRUE;
}

// Full UTF-8 -> UTF-16 decode (surrogate pairs included): the rendered strings
// carry Vietnamese diacritics and emoji, and the tests assert on them.
int MultiByteToWideChar(UINT, DWORD, const char* input, int inputBytes, LPWSTR output,
                        int outputCount) {
    if (input == nullptr) { return 0; }
    const int bytes = (inputBytes < 0) ? static_cast<int>(std::strlen(input)) : inputBytes;
    int written = 0;
    for (int i = 0; i < bytes;) {
        const unsigned char lead = static_cast<unsigned char>(input[i]);
        std::uint32_t codePoint = 0;
        int length = 1;
        if (lead < 0x80) {
            codePoint = lead;
        } else if ((lead >> 5) == 0x6) {
            codePoint = lead & 0x1Fu;
            length = 2;
        } else if ((lead >> 4) == 0xE) {
            codePoint = lead & 0x0Fu;
            length = 3;
        } else if ((lead >> 3) == 0x1E) {
            codePoint = lead & 0x07u;
            length = 4;
        } else {
            codePoint = 0xFFFD;
        }
        for (int k = 1; k < length && i + k < bytes; ++k) {
            codePoint = (codePoint << 6) | (static_cast<unsigned char>(input[i + k]) & 0x3Fu);
        }
        i += length;

        if (codePoint >= 0x10000u) {
            codePoint -= 0x10000u;
            const wchar_t high = static_cast<wchar_t>(0xD800u + (codePoint >> 10));
            const wchar_t low = static_cast<wchar_t>(0xDC00u + (codePoint & 0x3FFu));
            if (output != nullptr) {
                if (written + 2 > outputCount) { return 0; }
                output[written] = high;
                output[written + 1] = low;
            }
            written += 2;
        } else {
            if (output != nullptr) {
                if (written + 1 > outputCount) { return 0; }
                output[written] = static_cast<wchar_t>(codePoint);
            }
            ++written;
        }
    }
    return written;
}

int WideCharToMultiByte(UINT, DWORD, LPCWSTR input, int inputChars, char* output,
                        int outputCount, const char*, BOOL*) {
    if (input == nullptr) { return 0; }
    const int chars = (inputChars < 0) ? static_cast<int>(::wcslen(input)) : inputChars;
    if (output != nullptr) {
        if (chars >= outputCount) { return 0; }
        for (int i = 0; i < chars; ++i) {
            output[i] = (input[i] < 0x80) ? static_cast<char>(input[i]) : '?';
        }
    }
    return chars;
}

errno_t wcscpy_s(wchar_t* destination, size_t size, const wchar_t* source) {
    if (destination == nullptr || source == nullptr || size == 0) { return 22; }
    size_t i = 0;
    for (; source[i] != L'\0' && i + 1 < size; ++i) { destination[i] = source[i]; }
    destination[i] = L'\0';
    return 0;
}

} // extern "C"
