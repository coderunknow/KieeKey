// Stand-in for <windows.h> used ONLY by tests/check_windows_ui_compile.sh and
// the ok_arcade_window_tests harness: it pulls in the faithful declaration shim
// (tests/win32_gdi_shim.hpp) so the Windows-only UI code can be compiled,
// linked and exercised on a host without the Windows SDK.
#pragma once
#include "../win32_gdi_shim.hpp"
