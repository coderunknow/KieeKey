//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/win32_gdi_shim.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — tests/win32_gdi_shim.hpp
// Minimal but FAITHFUL declaration of the Win32 USER32/GDI32 surface used by
// the graphical Arcade Hub (src/app/ArcadeWindow.cpp) and the Chaos Lab
// (src/app/ChaosLabWindow.cpp).
//
// WHY THIS EXISTS
//   Both files are Windows-only, and the development/CI sandbox for the v1.3.0
//   arcade work has no MSVC, no MinGW and no Wine — meaning ~1100 lines of
//   never-compiled UI code. This shim gives that code a real compile+link+run
//   harness on any host: the signatures match the platform SDK (so a typo or a
//   wrong argument type is caught here, not on a user's machine), and the
//   implementations in tests/win32_gdi_stub.cpp record every GDI call, so the
//   frame pipeline can be asserted WITHOUT a display.
//
//   It is NOT a Win32 emulator and it is not compiled into any product: the
//   shim only ever backs the test binary `ok_arcade_window_tests`.
//
// NOT a substitute for running on Windows: the CI workflow still builds and
// smoke-tests the real binaries on windows-2022.
//----------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>

// The pointer-sized Get/SetWindowLongPtrW variants are what the window code
// uses; on a 64-bit host (any Linux/Windows x64 CI runner) that is the
// LongPtr form regardless of the compiler's idea of _WIN64.
#if !defined(_WIN64) && (defined(__x86_64__) || defined(__aarch64__) || defined(_M_X64))
#  define _WIN64 1
#endif

// ---- scalar types (Win32 ABI sizes) --------------------------------------
using BOOL      = int;
using BYTE      = unsigned char;
using WORD      = unsigned short;
using DWORD     = unsigned int;    // 32-bit on every supported host
using UINT      = unsigned int;
using LONG      = int;             // 32-bit on Windows and on LP64 hosts
using ULONG     = unsigned int;
using ATOM      = WORD;
using ULONG_PTR = std::uintptr_t;
using LONG_PTR  = std::intptr_t;
using INT_PTR   = std::intptr_t;
using UINT_PTR  = std::uintptr_t;
using COLOR16   = unsigned short;
using COLORREF  = DWORD;
using WPARAM    = UINT_PTR;
using LPARAM    = LONG_PTR;
using LRESULT   = LONG_PTR;
using WCHAR     = wchar_t;
using errno_t   = int;
using LPCWSTR   = const wchar_t*;
using LPWSTR    = wchar_t*;

// Calling-convention annotation: empty on every non-Windows compiler.
#ifndef CALLBACK
#  define CALLBACK
#endif
#ifndef WINAPI
#  define WINAPI
#endif

struct HWND__;
struct HDC__;
struct HBRUSH__;
struct HPEN__;
struct HFONT__;
struct HBITMAP__;
struct HRGN__;
struct HICON__;
struct HMENU__;
struct HINSTANCE__;
struct HCURSOR__;

using HWND      = HWND__*;
using HDC       = HDC__*;
using HBRUSH    = HBRUSH__*;
using HPEN      = HPEN__*;
using HFONT     = HFONT__*;
using HBITMAP   = HBITMAP__*;
using HRGN      = HRGN__*;
using HICON     = HICON__*;
using HMENU     = HMENU__*;
using HINSTANCE = HINSTANCE__*;
using HCURSOR   = HCURSOR__*;
using HGDIOBJ   = void*;
using LPVOID    = void*;
using HANDLE    = void*;
using HMODULE     = HINSTANCE;                       // GetModuleHandle/LoadLibrary module handle
using FARPROC     = void (*)();                      // generic function pointer (GetProcAddress)
using WNDENUMPROC = BOOL (CALLBACK*)(HWND, LPARAM);  // EnumChildWindows callback

#ifndef TRUE
#  define TRUE 1
#endif
#ifndef FALSE
#  define FALSE 0
#endif
#ifndef NULL
#  define NULL 0
#endif

// ---- geometry -------------------------------------------------------------
struct POINT { LONG x; LONG y; };
struct RECT { LONG left; LONG top; LONG right; LONG bottom; };
struct SIZE { LONG cx; LONG cy; };

// ---- GDI objects ----------------------------------------------------------
struct LOGFONTW {
    LONG  lfHeight;
    LONG  lfWidth;
    LONG  lfEscapement;
    LONG  lfOrientation;
    LONG  lfWeight;
    BYTE  lfItalic;
    BYTE  lfUnderline;
    BYTE  lfStrikeOut;
    BYTE  lfCharSet;
    BYTE  lfOutPrecision;
    BYTE  lfClipPrecision;
    BYTE  lfQuality;
    BYTE  lfPitchAndFamily;
    WCHAR lfFaceName[32];
};
struct TEXTMETRICW {
    LONG  tmHeight;
    LONG  tmAscent;
    LONG  tmDescent;
    LONG  tmInternalLeading;
    LONG  tmExternalLeading;
    LONG  tmAveCharWidth;
    LONG  tmMaxCharWidth;
    LONG  tmWeight;
};
struct TRIVERTEX {
    LONG     x;
    LONG     y;
    COLOR16  Red;
    COLOR16  Green;
    COLOR16  Blue;
    COLOR16  Alpha;
};
struct GRADIENT_RECT { ULONG UpperLeft; ULONG LowerRight; };

// ---- window classes -------------------------------------------------------
struct CREATESTRUCTW {
    LPVOID      lpCreateParams;
    HINSTANCE   hInstance;
    HMENU       hMenu;
    HWND        hwndParent;
    int         cy;
    int         cx;
    int         y;
    int         x;
    LONG        style;
    LPCWSTR     lpszName;
    LPCWSTR     lpszClass;
    DWORD       dwExStyle;
};
struct WNDCLASSEXW {
    UINT      cbSize;
    UINT      style;
    LRESULT (*lpfnWndProc)(HWND, UINT, WPARAM, LPARAM);
    int       cbClsExtra;
    int       cbWndExtra;
    HINSTANCE hInstance;
    HICON     hIcon;
    HCURSOR   hCursor;
    HBRUSH    hbrBackground;
    LPCWSTR   lpszMenuName;
    LPCWSTR   lpszClassName;
    HICON     hIconSm;
};
struct PAINTSTRUCT {
    HDC  hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore;
    BOOL fIncUpdate;
    BYTE rgbReserved[32];
};

// ---- constants used by the two window implementations ---------------------
#define WIN32_LEAN_AND_MEAN 1
#define NOMINMAX 1

// window styles / messages
#define WS_CHILD             0x40000000L
#define WS_VISIBLE           0x10000000L
#define WS_CLIPCHILDREN      0x02000000L
#define WS_OVERLAPPEDWINDOW  0x00CF0000L
#define WS_BORDER            0x00800000L
#define WS_VSCROLL           0x00200000L
#define WS_TABSTOP           0x00010000L
#define CS_VREDRAW           0x0001
#define CS_HREDRAW           0x0002
#define CS_DBLCLKS           0x0008

#define WM_NCCREATE      0x0081
#define WM_SIZE          0x0005
#define WM_PAINT         0x000F
#define WM_CLOSE         0x0010
#define WM_ERASEBKGND    0x0014
#define WM_SETCURSOR     0x0020
#define WM_GETDLGCODE    0x0087
#define WM_SETFONT       0x0030
#define WM_DPICHANGED    0x02E0
#define SWP_NOSIZE       0x0001
#define SWP_NOMOVE       0x0002
#define SWP_NOZORDER     0x0004
#define SWP_NOACTIVATE   0x0010
#define WM_KEYDOWN       0x0100
#define WM_KEYUP         0x0101
#define WM_CHAR          0x0102
#define WM_HSCROLL       0x0114
#define WM_SYSKEYDOWN    0x0104
#define WM_SYSCHAR       0x0106
#define WM_MOUSEMOVE     0x0200
#define WM_LBUTTONDOWN   0x0201
#define WM_TIMER         0x0113
#define WM_COMMAND       0x0111
#define WM_DESTROY       0x0002

// control styles / notifications
// NOTE: the trackbar (TRACKBAR_CLASSW/TBS_*/TBM_*) and the mouse-coordinate
// macros (GET_X_LPARAM/GET_Y_LPARAM) deliberately live in
// tests/win32_headers/commctrl.h and windowsx.h — the real SDK only exposes
// them through those headers, and shadowing them here once let a missing
// #include reach CI (C2065/C3861 on MSVC).
#define BS_AUTOCHECKBOX  0x00000003L
#define BS_PUSHBUTTON    0x00000000L
#define ES_MULTILINE     0x0004L
#define ES_AUTOVSCROLL   0x0040L
#define ES_READONLY      0x0800L
#define ES_WANTRETURN    0x1000L
#define SS_LEFT          0x00000000L
#define CBS_DROPDOWNLIST 0x0003L
#define EN_CHANGE        0x0300
#define EN_UPDATE        0x0400
#define BN_CLICKED       0
#define BST_UNCHECKED    0
#define BST_CHECKED      1
#define BM_GETCHECK      0x00F0
#define BM_SETCHECK      0x00F1
#define CB_ADDSTRING     0x0143
#define CB_GETCURSEL     0x0147
#define CB_SETCURSEL     0x014E
#define CBN_SELCHANGE    1
#define EM_SETSEL        0x00B1
#define EM_REPLACESEL    0x00C2

// GDI
#define TRANSPARENT      1
#define SRCCOPY          0x00CC0020u
#define PS_SOLID         0
#define FW_NORMAL        400
#define FW_BOLD          700
#define DEFAULT_CHARSET  1
#define CLEARTYPE_QUALITY 5
#define OUT_DEFAULT_PRECIS  0
#define CLIP_DEFAULT_PRECIS 0
#define DEFAULT_PITCH       0
#define FIXED_PITCH      1
#define VARIABLE_PITCH   2
#define LOGPIXELSX       88
#define GRADIENT_FILL_RECT_V 0x00000001
#define TA_LEFT          0x0000
#define TA_NOUPDATECP    0x0000
#define NULL_PEN         8
#define COLOR_BTNFACE    15

// misc
#define CP_UTF8          65001
#define GWLP_USERDATA    (-21)
#define SW_SHOW          5
#define IDC_ARROW        ((LPCWSTR)32512)
#define ERROR_CLASS_ALREADY_EXISTS 1410
#define HTCLIENT         1
#define DLGC_WANTALLKEYS 0x0004
#define DLGC_WANTARROWS  0x0001
#define DLGC_WANTCHARS   0x0080
#define CW_USEDEFAULT    ((int)0x80000000)

static_assert(sizeof(WORD) == 2, "Win32 WORD must be 16-bit");
static_assert(sizeof(DWORD) == 4, "Win32 DWORD must be 32-bit");
static_assert(sizeof(LONG) == 4, "Win32 LONG must be 32-bit");

// keyboard
#define VK_SHIFT     0x10
#define VK_BACK      0x08
#define VK_RETURN    0x0D
#define VK_ESCAPE    0x1B
#define VK_SPACE     0x20
#define VK_CONTROL   0x11
#define VK_MENU      0x12
#define VK_CAPITAL   0x14
#define VK_LSHIFT    0xA0
#define VK_RSHIFT    0xA1
#define VK_LCONTROL  0xA2
#define VK_RCONTROL  0xA3
#define VK_LMENU     0xA4
#define VK_RMENU     0xA5
#define VK_LWIN      0x5B
#define VK_RWIN      0x5C
#define VK_NUMLOCK   0x90
#define VK_SCROLL    0x91

#define RGB(r, g, b) ((COLORREF)(((BYTE)(r)) | (((BYTE)(g)) << 8) | (((DWORD)((BYTE)(b))) << 16)))
#define MAKELPARAM(l, h) ((LPARAM)(((DWORD)(l)) | (((DWORD)(h)) << 16)))
#define MAKEWPARAM(l, h) ((WPARAM)(((DWORD)(l)) | (((DWORD)(h)) << 16)))
#define LOWORD(v) ((WORD)(((ULONG_PTR)(v)) & 0xFFFF))
#define HIWORD(v) ((WORD)((((ULONG_PTR)(v)) >> 16) & 0xFFFF))

#define TOOLTIPS_CLASSW L"tooltips_class32"

// ---- functions (signatures follow the Windows SDK) ------------------------
// v1.3.0-beta5 (bug B8): MessageBoxW joins the recording USER32 layer so the
// hub/lab failure surfacing compiles (and is assertable) off-Windows.
#ifndef MB_OK
#define MB_OK          0x00000000L
#endif
#ifndef MB_ICONERROR
#define MB_ICONERROR   0x00000010L
#endif
#ifndef IDOK
#define IDOK           1
#endif

extern "C" {

// user32
HWND  CreateWindowExW(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                      LPVOID);
int   MessageBoxW(HWND, LPCWSTR, LPCWSTR, UINT);
BOOL  DestroyWindow(HWND);
BOOL  ShowWindow(HWND, int);
BOOL  IsWindow(HWND);
BOOL  InvalidateRect(HWND, const RECT*, BOOL);
BOOL  GetClientRect(HWND, RECT*);
BOOL  AdjustWindowRectEx(RECT*, DWORD, BOOL, DWORD);
BOOL  SetWindowTextW(HWND, LPCWSTR);
BOOL  SetWindowPos(HWND, HWND, int, int, int, int, UINT);
int   GetWindowTextW(HWND, LPWSTR, int);
int   GetWindowTextLengthW(HWND);
LRESULT CALLBACK DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
LRESULT SendMessageW(HWND, UINT, WPARAM, LPARAM);
#ifdef _WIN64
LONG_PTR GetWindowLongPtrW(HWND, int);
LONG_PTR SetWindowLongPtrW(HWND, int, LONG_PTR);
#else
LONG     GetWindowLongW(HWND, int);
LONG     SetWindowLongW(HWND, int, LONG);
#endif
HWND  GetForegroundWindow(void);
DWORD GetWindowThreadProcessId(HWND, DWORD*);
DWORD GetCurrentProcessId(void);
BOOL  SetForegroundWindow(HWND);
HWND  SetFocus(HWND);
HCURSOR LoadCursorW(HINSTANCE, LPCWSTR);
HCURSOR SetCursor(HCURSOR);
UINT_PTR SetTimer(HWND, UINT_PTR, UINT, void (*)(HWND, UINT, UINT_PTR, DWORD));
BOOL  KillTimer(HWND, UINT_PTR);
BOOL  GetKeyboardState(BYTE*);
int   ToUnicode(UINT, UINT, const BYTE*, LPWSTR, int, UINT);
HINSTANCE GetModuleHandleW(LPCWSTR);
FARPROC GetProcAddress(HMODULE, const char*);
int   MulDiv(int, int, int);
BOOL  EnumChildWindows(HWND, WNDENUMPROC, LPARAM);
DWORD GetLastError(void);
void  SetLastError(DWORD);
void  Sleep(DWORD);
ATOM  RegisterClassExW(const WNDCLASSEXW*);

// gdi32
HDC     BeginPaint(HWND, PAINTSTRUCT*);
BOOL    EndPaint(HWND, const PAINTSTRUCT*);
HDC     GetDC(HWND);
int     ReleaseDC(HWND, HDC);
HDC     CreateCompatibleDC(HDC);
HBITMAP CreateCompatibleBitmap(HDC, int, int);
BOOL    DeleteDC(HDC);
BOOL    DeleteObject(HGDIOBJ);
HGDIOBJ SelectObject(HDC, HGDIOBJ);
HGDIOBJ GetStockObject(int);
HBRUSH  CreateSolidBrush(COLORREF);
HPEN    CreatePen(int, int, COLORREF);
HFONT   CreateFontIndirectW(const LOGFONTW*);
HFONT   CreateFontW(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD,
                    DWORD, DWORD, DWORD, LPCWSTR);
int     GetDeviceCaps(HDC, int);
BOOL    BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD);
int     FillRect(HDC, const RECT*, HBRUSH);
BOOL    Rectangle(HDC, int, int, int, int);
BOOL    RoundRect(HDC, int, int, int, int, int, int);
BOOL    Ellipse(HDC, int, int, int, int);
BOOL    Polygon(HDC, const POINT*, int);
BOOL    MoveToEx(HDC, int, int, POINT*);
BOOL    LineTo(HDC, int, int);
#define ETO_CLIPPED 0x0004
BOOL    ExtTextOutW(HDC, int, int, UINT, const RECT*, LPCWSTR, UINT, const int*);
BOOL    TextOutW(HDC, int, int, LPCWSTR, int);
BOOL    GetTextExtentPoint32W(HDC, LPCWSTR, int, SIZE*);
BOOL    GetTextMetricsW(HDC, TEXTMETRICW*);
int     SetBkMode(HDC, int);
COLORREF SetTextColor(HDC, COLORREF);
UINT    SetTextAlign(HDC, UINT);
HRGN    CreateRectRgn(int, int, int, int);
int     SelectClipRgn(HDC, HRGN);
BOOL    GradientFill(HDC, TRIVERTEX*, ULONG, void*, ULONG, ULONG);
int     MultiByteToWideChar(UINT, DWORD, const char*, int, LPWSTR, int);
int     WideCharToMultiByte(UINT, DWORD, LPCWSTR, int, char*, int, const char*, BOOL*);
errno_t wcscpy_s(wchar_t*, size_t, const wchar_t*);

} // extern "C"

// The real SDK has a 2-argument template overload of wcscpy_s that deduces the
// destination size; the shim provides the same convenience so `wcscpy_s(dst,
// L"...")` (as used in ArcadeWindow.cpp) compiles unchanged.
inline errno_t wcscpy_s(wchar_t (&destination)[32], const wchar_t* source) {
    return ::wcscpy_s(destination, static_cast<size_t>(32), source);
}
