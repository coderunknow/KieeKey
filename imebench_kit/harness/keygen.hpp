//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.0.1 - refactored and completed logic
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
// File: imebench_kit/harness/keygen.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// imebench_kit/harness/keygen.hpp — deterministic word -> keystrokes
// generators for Telex and VNI (the "round-trip typing" protocol input).
// The Telex tables mirror tests/mega_correctness.cpp charToTelex.
// VNI convention: vowel digit after the letter (6=hat,7=hook,8=breve,9=đ),
// tone digit 1..5 appended after the vowel group.
//----------------------------------------------------------------------------
#pragma once

#include <string>
#include <cstdint>

namespace keygen {

inline bool charToTelex(wchar_t c, std::string& keys) {
    switch (c) {
        case L'a': keys += 'a'; return true;
        case L'\u00E0': keys += "af"; return true;
        case L'\u00E1': keys += "as"; return true;
        case L'\u1EA3': keys += "ar"; return true;
        case L'\u00E3': keys += "ax"; return true;
        case L'\u1EA1': keys += "aj"; return true;
        case L'\u0103': keys += "aw"; return true;
        case L'\u1EB1': keys += "awf"; return true;
        case L'\u1EAF': keys += "aws"; return true;
        case L'\u1EB3': keys += "awr"; return true;
        case L'\u1EB5': keys += "awx"; return true;
        case L'\u1EB7': keys += "awj"; return true;
        case L'\u00E2': keys += "aa"; return true;
        case L'\u1EA7': keys += "aaf"; return true;
        case L'\u1EA5': keys += "aas"; return true;
        case L'\u1EA9': keys += "aar"; return true;
        case L'\u1EAB': keys += "aax"; return true;
        case L'\u1EAD': keys += "aaj"; return true;
        case L'e': keys += 'e'; return true;
        case L'\u00E8': keys += "ef"; return true;
        case L'\u00E9': keys += "es"; return true;
        case L'\u1EBB': keys += "er"; return true;
        case L'\u1EBD': keys += "ex"; return true;
        case L'\u1EB9': keys += "ej"; return true;
        case L'\u00EA': keys += "ee"; return true;
        case L'\u1EC1': keys += "eef"; return true;
        case L'\u1EBF': keys += "ees"; return true;
        case L'\u1EC3': keys += "eer"; return true;
        case L'\u1EC5': keys += "eex"; return true;
        case L'\u1EC7': keys += "eej"; return true;
        case L'i': keys += 'i'; return true;
        case L'\u00EC': keys += "if"; return true;
        case L'\u00ED': keys += "is"; return true;
        case L'\u1EC9': keys += "ir"; return true;
        case L'\u0129': keys += "ix"; return true;
        case L'\u1ECB': keys += "ij"; return true;
        case L'o': keys += 'o'; return true;
        case L'\u00F2': keys += "of"; return true;
        case L'\u00F3': keys += "os"; return true;
        case L'\u1ECF': keys += "or"; return true;
        case L'\u00F5': keys += "ox"; return true;
        case L'\u1ECD': keys += "oj"; return true;
        case L'\u00F4': keys += "oo"; return true;
        case L'\u1ED3': keys += "oof"; return true;
        case L'\u1ED1': keys += "oos"; return true;
        case L'\u1ED5': keys += "oor"; return true;
        case L'\u1ED7': keys += "oox"; return true;
        case L'\u1ED9': keys += "ooj"; return true;
        case L'\u01A1': keys += "ow"; return true;
        case L'\u1EDD': keys += "owf"; return true;
        case L'\u1EDB': keys += "ows"; return true;
        case L'\u1EDF': keys += "owr"; return true;
        case L'\u1EE1': keys += "owx"; return true;
        case L'\u1EE3': keys += "owj"; return true;
        case L'u': keys += 'u'; return true;
        case L'\u00F9': keys += "uf"; return true;
        case L'\u00FA': keys += "us"; return true;
        case L'\u1EE7': keys += "ur"; return true;
        case L'\u0169': keys += "ux"; return true;
        case L'\u1EE5': keys += "uj"; return true;
        case L'\u01B0': keys += "uw"; return true;
        case L'\u1EEB': keys += "uwf"; return true;
        case L'\u1EE9': keys += "uws"; return true;
        case L'\u1EED': keys += "uwr"; return true;
        case L'\u1EEF': keys += "uwx"; return true;
        case L'\u1EF1': keys += "uwj"; return true;
        case L'y': keys += 'y'; return true;
        case L'\u1EF3': keys += "yf"; return true;
        case L'\u00FD': keys += "ys"; return true;
        case L'\u1EF7': keys += "yr"; return true;
        case L'\u1EF9': keys += "yx"; return true;
        case L'\u1EF5': keys += "yj"; return true;
        case L'\u0111': keys += "dd"; return true;
        default:
            if (c >= L'a' && c <= L'z') { keys += static_cast<char>(c); return true; }
            return false;   // outside the Telex alphabet (Đ, punct, …)
    }
}

inline bool charToVni(wchar_t c, std::string& keys) {
    static const struct V { wchar_t c; const char* k; } t[] = {
        {L'a', "a"}, {L'\u00E0', "a2"}, {L'\u00E1', "a1"}, {L'\u1EA3', "a3"},
        {L'\u00E3', "a4"}, {L'\u1EA1', "a5"},
        {L'\u0103', "a8"}, {L'\u1EB1', "a82"}, {L'\u1EAF', "a81"}, {L'\u1EB3', "a83"},
        {L'\u1EB5', "a84"}, {L'\u1EB7', "a85"},
        {L'\u00E2', "a6"}, {L'\u1EA7', "a62"}, {L'\u1EA5', "a61"}, {L'\u1EA9', "a63"},
        {L'\u1EAB', "a64"}, {L'\u1EAD', "a65"},
        {L'e', "e"}, {L'\u00E8', "e2"}, {L'\u00E9', "e1"}, {L'\u1EBB', "e3"},
        {L'\u1EBD', "e4"}, {L'\u1EB9', "e5"},
        {L'\u00EA', "e6"}, {L'\u1EC1', "e62"}, {L'\u1EBF', "e61"}, {L'\u1EC3', "e63"},
        {L'\u1EC5', "e64"}, {L'\u1EC7', "e65"},
        {L'i', "i"}, {L'\u00EC', "i2"}, {L'\u00ED', "i1"}, {L'\u1EC9', "i3"},
        {L'\u0129', "i4"}, {L'\u1ECB', "i5"},
        {L'o', "o"}, {L'\u00F2', "o2"}, {L'\u00F3', "o1"}, {L'\u1ECF', "o3"},
        {L'\u00F5', "o4"}, {L'\u1ECD', "o5"},
        {L'\u00F4', "o6"}, {L'\u1ED3', "o62"}, {L'\u1ED1', "o61"}, {L'\u1ED5', "o63"},
        {L'\u1ED7', "o64"}, {L'\u1ED9', "o65"},
        {L'\u01A1', "o7"}, {L'\u1EDD', "o72"}, {L'\u1EDB', "o71"}, {L'\u1EDF', "o73"},
        {L'\u1EE1', "o74"}, {L'\u1EE3', "o75"},
        {L'u', "u"}, {L'\u00F9', "u2"}, {L'\u00FA', "u1"}, {L'\u1EE7', "u3"},
        {L'\u0169', "u4"}, {L'\u1EE5', "u5"},
        {L'\u01B0', "u7"}, {L'\u1EEB', "u72"}, {L'\u1EE9', "u71"}, {L'\u1EED', "u73"},
        {L'\u1EEF', "u74"}, {L'\u1EF1', "u75"},
        {L'y', "y"}, {L'\u1EF3', "y2"}, {L'\u00FD', "y1"}, {L'\u1EF7', "y3"},
        {L'\u1EF9', "y4"}, {L'\u1EF5', "y5"},
        {L'\u0111', "d9"},
    };
    for (const V& e : t) {
        if (e.c == c) { keys += e.k; return true; }
    }
    if (c >= L'a' && c <= L'z') { keys += static_cast<char>(c); return true; }
    return false;
}

inline bool wordToKeys(const std::wstring& word, std::string& keys, int method) {
    keys.clear();
    for (wchar_t c : word) {
        if (c == L' ') { keys += ' '; continue; }
        const bool ok = (method == 0) ? charToTelex(c, keys) : charToVni(c, keys);
        if (!ok) return false;
    }
    return !keys.empty();
}

} // namespace keygen
