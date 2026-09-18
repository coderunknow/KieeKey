//============================================================================
// KieeKey — tests/win32_headers/commctrl.h
// Minimal stand-in for the Windows common-controls header.
//
// The recording USER32/GDI32 layer (tests/win32_gdi_stub.cpp) can create and
// poke common controls, but the app code must still ask for them the way the
// real SDK requires (`#include <commctrl.h>`), otherwise the host build would
// happily compile code that MSVC rejects — which is exactly what happened with
// TRACKBAR_CLASSW/TBS_*/TBM_* in the Chaos Lab window.
//============================================================================
#pragma once

#include <windows.h>

// Trackbar (slider) class + messages
#define TRACKBAR_CLASSW  L"msctls_trackbar32"
#define TRACKBAR_CLASS   TRACKBAR_CLASSW
#define TBS_HORZ         0x0000
#define TBS_VERT         0x0002
#define TBS_NOTICKS      0x0010L
#define TBS_AUTOTICKS    0x0001
#define TBS_BOTH         0x0008
#define TBS_TOOLTIPS     0x0100
#define TBM_GETPOS       0x0400
#define TBM_GETRANGEMIN  0x0401
#define TBM_GETRANGEMAX  0x0402
#define TBM_SETPOS       0x0405
#define TBM_SETRANGE     0x0406
#define TBM_SETTICFREQ   0x0414
#define TBM_SETPAGESIZE  0x0415
#define TBM_SETLINESIZE  0x0417

typedef struct tagINITCOMMONCONTROLSEX {
    DWORD dwSize;
    DWORD dwICC;
} INITCOMMONCONTROLSEX, *LPINITCOMMONCONTROLSEX;

#define ICC_TAB_CLASSES  0x00000008
#define ICC_BAR_CLASSES  0x00000004
#define ICC_PROGRESS_CLASS 0x00000020

static inline BOOL InitCommonControlsEx(const INITCOMMONCONTROLSEX*) { return TRUE; }
