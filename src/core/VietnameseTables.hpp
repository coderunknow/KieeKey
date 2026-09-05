//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.1 RC3 - refactored and completed logic
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
// File: src/core/VietnameseTables.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.3 — VietnameseTables.hpp
// Vietnamese phonetics data tables (Telex/VNI). Faithful 1:1 migration of
// OpenKey 2.0.5 engine/Vietnamese.cpp data into modern C++20 containers.
//
// The *data* is the hard-won linguistic knowledge of the original project and
// is kept byte-for-byte identical (correctness first). The *access* layer is
// modernized: span-based views, constexpr masks, no macros, no `using namespace`.
//----------------------------------------------------------------------------
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string_view>
#include <vector>

namespace ok::text {

//----------------------------------------------------------------------------
// Mask constants (constexpr replacements for the legacy #define block).
//----------------------------------------------------------------------------
inline constexpr std::uint32_t kCapsMask         = 0x10000u;  // shifted / caps
inline constexpr std::uint32_t kToneMask         = 0x20000u;  // ^ hat (â, ê, ô)
inline constexpr std::uint32_t kToneWMask        = 0x40000u;  // ư / ơ (w tone)
inline constexpr std::uint32_t kMark1Mask        = 0x80000u;  // sắc   á
inline constexpr std::uint32_t kMark2Mask        = 0x100000u; // huyền à
inline constexpr std::uint32_t kMark3Mask        = 0x200000u; // hỏi   ả
inline constexpr std::uint32_t kMark4Mask        = 0x400000u; // ngã   ã
inline constexpr std::uint32_t kMark5Mask        = 0x800000u; // nặng  ạ
inline constexpr std::uint32_t kMarkMask         = 0xF80000u; // any mark
inline constexpr std::uint32_t kCharMask         = 0xFFFFu;   // low 16 bits
inline constexpr std::uint32_t kStandaloneMask   = 0x1000000u;// standalone w
inline constexpr std::uint32_t kCharCodeMask     = 0x2000000u;// coded output
inline constexpr std::uint32_t kPureCharMask     = 0x80000000u;

inline constexpr std::uint16_t kEndConsonantMask   = 0x4000u;
inline constexpr std::uint16_t kConsonantAllowMask = 0x8000u;

//----------------------------------------------------------------------------
// Input-method specific processing characters (legacy ProcessingChar table).
// Columns: S F R X J A O E W D Z
//----------------------------------------------------------------------------
// NOTE: internal identity is UPPERCASE (mirrors the legacy VK code tables,
// where KEY_A == 0x41 == 'A'). Input is normalized to uppercase by the engine.
inline constexpr std::array<std::array<char32_t, 11>, 3> kProcessingChar = {{
    { U'S', U'F', U'R', U'X', U'J', U'A', U'O', U'E', U'W', U'D', U'Z' }, // Telex
    { U'1', U'2', U'3', U'4', U'5', U'6', U'6', U'7', U'8', U'9', U'0' }, // VNI
    { U'S', U'F', U'R', U'X', U'J', U'A', U'O', U'E', U'W', U'D', U'Z' }, // Simple Telex
}};

//----------------------------------------------------------------------------
// Code tables (0: Unicode, 1: TCVN3, 2: VNI-Windows, 3: Unicode Compound,
// 4: CP-1258). Row layout per the original:
//   {key, {CAPS_CHAR, NORMAL_CHAR, CAPS_W_CHAR, NORMAL_W_CHAR,
//          then 5 tone pairs (CAPS,NORMAL) if the row carries tones}}
// Values are the exact 2.0.5 table — do not "fix" them without re-testing.
//----------------------------------------------------------------------------

// sắc, huyền, hỏi, ngã, nặng — combining marks for "Unicode Compound" (table 3)
inline constexpr std::array<std::uint16_t, 5> kUnicodeCompoundMarks = {0x0301, 0x0300, 0x0309, 0x0303, 0x0323};

//----------------------------------------------------------------------------
// All phonetics lookup tables (code tables, vowel/combine/mark structures,
// consonant tables, quick-telex maps) live in FlatTables.hpp as constexpr
// flat storage: O(1) / binary-search lookups, zero heap, zero RB-tree walks.
// The data is the same validated literals from the legacy std::map tables,
// regenerated by tools/gen_flat_tables.py — do not hand-edit the generated
// file; rerun the generator instead.
//----------------------------------------------------------------------------
} // namespace ok::text

#include "FlatTables.hpp"
