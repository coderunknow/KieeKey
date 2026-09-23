//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// File: src/app/PersistPolicy.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//---------------------------------------------------------------------------
// v1.3.0-beta8 (bug FT-01) — the DECISIONS that used to be implicit, in one
// portable place so a test can pin them without a Windows message loop.
//
// The beta7 bug was not a missing format: ProgressionEngine::saveToFile() and
// AiRivalEngine::serializeProfile() have existed since v1.3.0 and are unit
// tested. The bug was that NOTHING under src/app/ called them, so every
// session started from level 1 and the AI profile was forgotten on exit.
// main.cpp now does the I/O; this header owns the rules around it:
//
//   * WHICH files: %APPDATA%\KieeKey\progression.dat and aiprofile.dat. The
//     extension is .dat because the engines write their own checksummed text
//     format — naming it .json would be a lie.
//   * WHEN to write the AI profile: only while the user is opted in. Opting
//     out PURGES the profile (resetProfile()); persisting it afterwards would
//     silently undo that promise.
//   * HOW OFTEN the crash-safe sweep runs: 30 s, and only when a counter
//     actually moved (a 1 s timer must not rewrite a file forever).
//---------------------------------------------------------------------------
#ifndef KIEEKEY_APP_PERSIST_POLICY_HPP
#define KIEEKEY_APP_PERSIST_POLICY_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace ok::apppolicy {

//--- file names (inside the %APPDATA%\KieeKey directory) --------------------
inline constexpr std::string_view kProgressionFileName = "progression.dat";
inline constexpr std::string_view kAiProfileFileName    = "aiprofile.dat";

//--- the AI profile is only written while the user is opted in ---------------
[[nodiscard]] constexpr bool shouldPersistAi(bool optedIn) noexcept {
    return optedIn;
}

//--- throttled crash-safe progression sweep ---------------------------------
inline constexpr std::uint64_t kPersistThrottleMs = 30000;

// True when the throttled writer has work: something moved since the last save
// AND the throttle window has elapsed (the first sweep of a session always
// runs — `lastMs == 0`).
[[nodiscard]] constexpr bool persistSweepDue(std::uint64_t nowMs,
                                             std::uint64_t lastMs,
                                             std::uint64_t savedXp,
                                             std::uint64_t currentXp,
                                             std::uint64_t savedKeys,
                                             std::uint64_t currentKeys,
                                             std::uint64_t savedAiObservations,
                                             std::uint64_t currentAiObservations) noexcept {
    const bool changed = (savedXp != currentXp) || (savedKeys != currentKeys) ||
                         (savedAiObservations != currentAiObservations);
    if (!changed) { return false; }
    if (lastMs == 0) { return true; }
    return (nowMs - lastMs) >= kPersistThrottleMs;
}

// The engines take std::string_view; hand them a UTF-8 path. %APPDATA% is not
// guaranteed ASCII (a Vietnamese user name is the common case on this app's
// target machines), so the caller converts instead of passing bytes through.
[[nodiscard]] inline std::string joinPath(std::string_view dir,
                                          std::string_view name) {
    std::string out(dir);
    if (!out.empty() && out.back() != '\\' && out.back() != '/') { out += '\\'; }
    out += name;
    return out;
}

} // namespace ok::apppolicy

#endif // KIEEKEY_APP_PERSIST_POLICY_HPP
