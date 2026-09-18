//============================================================================
// KieeKey — tests/win32_headers/windowsx.h
// Minimal stand-in for <windowsx.h> (the mouse-coordinate macros the hub window
// uses for hover hit testing). Kept separate so the host build fails exactly
// like MSVC when a source forgets the include.
//============================================================================
#pragma once

#include <windows.h>

#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
