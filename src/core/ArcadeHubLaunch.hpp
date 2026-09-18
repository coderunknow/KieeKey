//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: src/core/ArcadeHubLaunch.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — ArcadeHubLaunch.hpp
// Two-line façade over the native Arcade Hub / Chaos Lab windows.
//
// The WinUI 3 settings surface only includes core headers (src/core) and must
// not depend on src/app, so the launch entry points live here and are
// implemented next to the windows themselves (ArcadeWindow.cpp /
// ChaosLabWindow.cpp). Both are no-ops on non-Windows builds.
//----------------------------------------------------------------------------
#pragma once

namespace ok::app {

// Opens (or focuses) the graphical Arcade Hub window and starts `slug`
// (nullptr = keep the current game). Returns false when unavailable.
bool launchArcadeHub(const char* slug);

// Opens (or focuses) the Chaos / Flexing lab window. Returns false when the
// platform has no window support (non-Windows build).
bool launchChaosLab();

} // namespace ok::app
