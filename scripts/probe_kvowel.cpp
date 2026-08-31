//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.0 - refactored and completed logic
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
// File: scripts/probe_kvowel.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
// Dump kVowel['O'] patterns + simulate checkCorrectVowel for "mon"/"kimon"
#include <cstdio>
#include "TextEngine.hpp"
#include "FlatTables.hpp"

using namespace ok::text;

int main() {
    const auto& tbl = kVowel;   // FlatMap<uint16_t, FlatVec<FlatVec<uint16_t>>, 4>
    for (const auto& entry : tbl.entries) {
        if (entry.key != 0x4F) { continue; }
        const auto& pats = entry.second;
        std::printf("kVowel['O'] : %zu patterns\n", pats.size());
        for (const auto& p : pats) {
            std::printf("  { ");
            for (std::size_t j = 0; j < p.size(); ++j) {
                std::printf("%c%s", static_cast<char>(p[j]),
                            (j + 1 < p.size()) ? ", " : "");
            }
            std::printf(" }  size=%zu\n", p.size());
        }
    }
    return 0;
}
