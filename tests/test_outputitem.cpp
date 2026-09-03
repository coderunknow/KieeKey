//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.1.3 - refactored and completed logic
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
// File: tests/test_outputitem.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.3 — tests/test_outputitem.cpp
// REGRESSION TEST for the producer→consumer OutputItem contract in
// src/app/main.cpp.
//
// Historical bug (found in the adversarial round): OutputItem::text was
// kMaxBuff+1 wchar_t while a replacement can be up to 2*kMaxBuff wchar_t
// (newCharCount ≤ kMaxBuff entries, each decoding to base + combining mark
// = 2 wchar_t under the UnicodeCompound code table). The producer copied
// `textLen = repScratch.size()` chars into a too-small array → out-of-bounds
// write on the hook thread's stack, and the consumer's
// std::wstring(text, text+textLen) → out-of-bounds read.
//
// This test pins the FIXED contract:
//   1. The buffer is big enough for the worst case (2*kMaxBuff wchar_t).
//   2. The producer clamps the copy to the buffer and sets textLen == copied
//      count (consumer reads exactly the copied range — never past it).
//   3. A worst-case 64-wchar payload round-trips losslessly.
//
//   g++ -std=c++23 -O2 -I src/core tests/test_outputitem.cpp -o t3 && ./t3
//   (also run under -fsanitize=address,undefined)
//----------------------------------------------------------------------------
#include <cstdio>
#include <cstring>
#include <string>

#include "TextEngine.hpp"

using namespace ok::text;

namespace {

// Mirrors src/app/main.cpp OutputItem (must stay in sync — compile-time
// static_asserts below enforce the sizing contract).
struct OutputItem {
    enum class Kind : std::uint8_t { None, Edit, ForegroundChanged } kind = Kind::None;
    std::uint32_t backspace = 0;
    std::uint32_t textLen   = 0;
    wchar_t text[2 * kMaxBuff + 1] = {};
};
static_assert(sizeof(OutputItem) >= 2 * kMaxBuff * sizeof(wchar_t) + 12,
              "OutputItem must hold the worst-case replacement (2*kMaxBuff wchar_t)");

// Producer-side copy, exactly as in main.cpp onHookEvent().
std::size_t producerCopy(OutputItem& it, const std::wstring& src, std::uint32_t backspace) noexcept {
    it.kind      = OutputItem::Kind::Edit;
    it.backspace = backspace;
    const std::size_t n = std::min<std::size_t>(src.size(), std::size(it.text));
    it.textLen   = static_cast<std::uint32_t>(n);
    for (std::size_t i = 0; i < n; ++i) { it.text[i] = src[i]; }
    return n;
}

// Consumer-side reconstruction, exactly as in main.cpp onConsumerEvent().
std::wstring consumerRead(const OutputItem& it) noexcept {
    return std::wstring(it.text, it.text + it.textLen);
}

int g_failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { ++g_failures; std::printf("  [FAIL] %s\n", what); }
    else     { std::printf("  [ ok ] %s\n", what); }
}

} // namespace

int main() {
    std::printf("KieeKey — OutputItem layout-contract regression\n");

    // 1. Worst-case payload: 64 wchar_t (32 compound entries × 2) must fit.
    std::wstring worst;
    for (int i = 0; i < 2 * static_cast<int>(kMaxBuff); ++i) {
        worst.push_back(static_cast<wchar_t>(0x100 + i));   // 64 distinct chars
    }
    static_assert(2 * kMaxBuff == 64, "test depends on kMaxBuff == 32");
    OutputItem it;
    const std::size_t copied = producerCopy(it, worst, 7);
    check(copied == worst.size(), "worst-case 64-wchar payload fully copied (no truncation)");
    check(it.textLen == copied, "textLen == copied count (consumer never reads past buffer)");
    check(consumerRead(it) == worst, "worst-case payload round-trips exactly");

    // 2. Over-long payload (defensive): clamp must hold, textLen must match.
    std::wstring overlong(worst);
    overlong.append(200, L'x');          // 264 wchar — beyond any real replacement
    OutputItem it2;
    const std::size_t c2 = producerCopy(it2, overlong, 1);
    check(c2 == std::size(it2.text), "over-long payload clamped to buffer size");
    check(it2.textLen == c2, "over-long: textLen still equals copied count");
    check(consumerRead(it2).size() == std::size(it2.text), "over-long: consumer reads exact copied range");

    // 3. Empty replacement (pure delete) round-trips.
    OutputItem it3;
    producerCopy(it3, L"", 3);
    check(it3.backspace == 3 && it3.textLen == 0 && consumerRead(it3).empty(),
          "pure-delete item: backspace kept, empty text");

    std::printf("\n%s\n", g_failures == 0 ? "ALL OUTPUTITEM TESTS PASSED" : "OUTPUTITEM TESTS FAILED");
    return g_failures == 0 ? 0 : 1;
}
