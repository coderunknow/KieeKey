//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.1 Stable - refactored and completed logic
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
// File: tests/engine205.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// tests/engine205.hpp — wrapper around the REAL OpenKey 2.0.5 engine
// (unmodified upstream sources vendored under tests/reference/openkey-2.0.5,
// compiled via their native Linux platform path). Used as the third model in
// the differential benchmark (tests/mega_correctness.cpp).
//----------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <string>

namespace ok205 {

enum class Method : int { Telex = 0, Vni = 1, SimpleTelex = 2 };

struct Options {
    Method method              = Method::Telex;
    int    table               = 0;   // 0=Unicode 1=TCVN3 2=VNI-Windows
    bool   checkSpelling       = true;
    bool   modernOrthography   = false;
    bool   quickTelex          = false;
    bool   restoreIfWrongSpelling = false;
    bool   freeMark            = false;
    bool   quickStartConsonant = false;
    bool   quickEndConsonant   = false;
    bool   upperCaseFirstChar  = false;
    bool   useMacro            = true;
    bool   useMacroInEnglishMode = false;
    bool   allowConsonantZFWJ  = false;
    bool   autoCapsMacro       = false;
};

// What the legacy hook would deliver to the application for one key event.
// Apply as: pop `backspace` chars from the app text, then append `text`.
struct Delta {
    std::int32_t  code = 0;     // 0 nothing, 1 process, 2 break, 3 restore, 4 replace-macro
    std::uint32_t backspace = 0;
    std::wstring  text;         // replacement text (oldest-first) or the visible pass-through char
    bool          suppressed = false;  // true if the raw key was consumed by the engine
};

void init(const Options& o);
void setOptions(const Options& o);   // change settings only — pending word is kept
void installMacro(const std::string& key, const std::wstring& content);
void clearMacros();
void reset();                   // startNewSession-equivalent (vKeyInit)

// Returns false if the char cannot be mapped to a 2.0.5 Linux key code.
bool processChar(char32_t ch, bool caps, bool ctrl, Delta& out);
bool processSymbol(char c, Delta& out);   // shifted-symbol as real shift+key event (word break + pass-through)
void processSpace(bool caps, Delta& out);
void processBackspace(Delta& out);
void processWordBreak(std::uint16_t vk, Delta& out);   // vk: 0x0D enter, 0x20 space-like, 0x09 tab
void processMouseDown(Delta& out);

} // namespace ok205
