// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 coderunknow
#pragma once

#include <string>
#include <string_view>

namespace ok::app {

// Windows EDIT controls store UTF-16, not one code point per wchar_t.
// Also accepts native UTF-32 wchar_t in portable UI tests.
inline std::u32string codePointsFromWide(std::wstring_view text) {
    std::u32string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto ch = static_cast<char32_t>(text[i]);
        if (ch >= 0xD800 && ch <= 0xDBFF) {
            if (i + 1 < text.size()) {
                const auto low = static_cast<char32_t>(text[i + 1]);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    result.push_back(0x10000 + ((ch - 0xD800) << 10) + (low - 0xDC00));
                    ++i;
                    continue;
                }
            }
            ch = 0xFFFD;
        } else if ((ch >= 0xDC00 && ch <= 0xDFFF) || ch > 0x10FFFF) {
            ch = 0xFFFD;
        }
        result.push_back(ch);
    }
    return result;
}

} // namespace ok::app
