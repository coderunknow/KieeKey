//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
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
// File: src/core/ChaosEngine.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "ChaosEngine.hpp"

#include <algorithm>
#include <cwctype>
#include <random>

namespace ok::chaos {

namespace {

// Fast deterministic LCG for repeatable transforms
uint32_t nextLcg(uint32_t& state) noexcept {
    state = state * 1664525u + 1013904223u;
    return state;
}

float nextFloat(uint32_t& state) noexcept {
    return static_cast<float>(nextLcg(state) & 0xFFFFFF) / static_cast<float>(0x1000000);
}

char32_t toggleCase(char32_t ch) noexcept {
    if (ch >= U'a' && ch <= U'z') {
        return ch - (U'a' - U'A');
    }
    if (ch >= U'A' && ch <= U'Z') {
        return ch + (U'a' - U'A');
    }
    // Unicode common Vietnamese letters
    wint_t wch = static_cast<wint_t>(ch);
    if (std::iswlower(wch)) {
        return static_cast<char32_t>(std::towupper(wch));
    }
    if (std::iswupper(wch)) {
        return static_cast<char32_t>(std::towlower(wch));
    }
    return ch;
}

} // namespace

ChaosEngine& ChaosEngine::instance() noexcept {
    static ChaosEngine s_instance;
    return s_instance;
}

ChaosEngine::ChaosEngine() {
    m_config.reset();
    m_active.store(false, std::memory_order_relaxed);
}

void ChaosEngine::setConfig(const ChaosConfig& config) noexcept {
    m_config = config;
    const bool active = m_config.masterEnabled &&
                        (m_config.randomCaseEnabled || m_config.glyphTransformEnabled);
    m_active.store(active, std::memory_order_release);
}

ChaosConfig ChaosEngine::getConfig() const noexcept {
    return m_config;
}

std::u32string ChaosEngine::processCase(std::u32string_view text, uint32_t seed) const {
    if (!m_active.load(std::memory_order_relaxed) || !m_config.randomCaseEnabled) {
        return std::u32string(text);
    }
    return applyRandomCase(text, m_config.randomCaseIntensity, m_config.caseGranularity, seed);
}

std::u32string ChaosEngine::getVisualDisplayString(std::u32string_view text, uint32_t seed) const {
    if (!m_active.load(std::memory_order_relaxed) || !m_config.glyphTransformEnabled) {
        return std::u32string(text);
    }
    return applyGlyphTransform(text, m_config.glyphMode, seed);
}

std::u32string ChaosEngine::applyRandomCase(
    std::u32string_view text,
    float intensity,
    CaseGranularity gran,
    uint32_t seed) {
    if (text.empty() || intensity <= 0.0f) {
        return std::u32string(text);
    }

    uint32_t state = (seed != 0) ? seed : 0x1337C0DE;
    std::u32string out;
    out.reserve(text.size());

    if (gran == CaseGranularity::ByChar) {
        for (char32_t ch : text) {
            if (nextFloat(state) < intensity) {
                out.push_back(toggleCase(ch));
            } else {
                out.push_back(ch);
            }
        }
    } else { // ByWord
        bool wordUpper = (nextFloat(state) < intensity);
        bool inWord = false;
        for (char32_t ch : text) {
            bool isSpaceOrPunct = (ch <= 32 || (ch >= 33 && ch <= 47) || (ch >= 58 && ch <= 64));
            if (isSpaceOrPunct) {
                inWord = false;
                out.push_back(ch);
            } else {
                if (!inWord) {
                    inWord = true;
                    wordUpper = (nextFloat(state) < intensity);
                }
                wint_t wch = static_cast<wint_t>(ch);
                if (wordUpper) {
                    out.push_back(static_cast<char32_t>(std::towupper(wch)));
                } else {
                    out.push_back(static_cast<char32_t>(std::towlower(wch)));
                }
            }
        }
    }
    return out;
}

char32_t ChaosEngine::getFlippedVerticalGlyph(char32_t ch) noexcept {
    switch (ch) {
        case U'a': return 0x0250; // ɐ
        case U'b': return U'q';
        case U'c': return 0x0254; // ɔ
        case U'd': return U'p';
        case U'e': return 0x01DD; // ǝ
        case U'f': return 0x025F; // ɟ
        case U'g': return 0x0183; // ƃ
        case U'h': return 0x0265; // ɥ
        case U'i': return 0x1D09; // ᴉ
        case U'j': return 0x027E; // ɾ
        case U'k': return 0x029E; // ʞ
        case U'l': return U'l';
        case U'm': return 0x026F; // ɯ
        case U'n': return U'u';
        case U'o': return U'o';
        case U'p': return U'd';
        case U'q': return U'b';
        case U'r': return 0x0279; // ɹ
        case U's': return U's';
        case U't': return 0x0287; // ʇ
        case U'u': return U'n';
        case U'v': return 0x028C; // ʌ
        case U'w': return 0x028D; // ʍ
        case U'x': return U'x';
        case U'y': return 0x028E; // ʎ
        case U'z': return U'z';
        case U'A': return 0x2200; // ∀
        case U'B': return 0x15FA;
        case U'C': return 0x0186; // Ɔ
        case U'D': return 0x15E1;
        case U'E': return 0x018E; // Ǝ
        case U'F': return 0x2132; // Ⅎ
        case U'G': return 0x2141; // ⅁
        case U'H': return U'H';
        case U'I': return U'I';
        case U'J': return 0x017F;
        case U'K': return 0x029E;
        case U'L': return 0x02E5;
        case U'M': return U'W';
        case U'N': return 0x1D0E;
        case U'O': return U'O';
        case U'P': return 0x0500;
        case U'Q': return 0x038C;
        case U'R': return 0x1D1A;
        case U'S': return U'S';
        case U'T': return 0x22A5; // ⊥
        case U'U': return 0x2229; // ∩
        case U'V': return 0x039B; // Λ
        case U'W': return U'M';
        case U'X': return U'X';
        case U'Y': return 0x2144; // ⅄
        case U'Z': return U'Z';
        case U'1': return 0x21C2;
        case U'2': return 0x218A;
        case U'3': return 0x0190;
        case U'6': return U'9';
        case U'7': return 0x2C62;
        case U'9': return U'6';
        case U'?': return 0x00BF; // ¿
        case U'!': return 0x00A1; // ¡
        case U'.': return 0x02D9; // ˙
        case U',': return 0x0027; // '
        case U'(': return U')';
        case U')': return U'(';
        case U'[': return U']';
        case U']': return U'[';
        case U'{': return U'}';
        case U'}': return U'{';
        case U'<': return U'>';
        case U'>': return U'<';
        case U'_': return 0x203E; // ‾
        default: return ch;
    }
}

char32_t ChaosEngine::getFlippedHorizontalGlyph(char32_t ch) noexcept {
    switch (ch) {
        case U'b': return U'd';
        case U'd': return U'b';
        case U'p': return U'q';
        case U'q': return U'p';
        case U'c': return 0x0254; // ɔ
        case U'(': return U')';
        case U')': return U'(';
        case U'[': return U']';
        case U']': return U'[';
        case U'{': return U'}';
        case U'}': return U'{';
        case U'<': return U'>';
        case U'>': return U'<';
        case U's': return 0x01A8; // ƨ
        case U'S': return 0x01A7; // Ƨ
        case U'E': return 0x018E; // Ǝ
        case U'J': return 0x0196; // Ɩ
        default: return ch;
    }
}

std::u32string ChaosEngine::applyGlyphTransform(
    std::u32string_view text,
    GlyphTransformMode mode,
    uint32_t seed) {
    if (text.empty() || mode == GlyphTransformMode::None) {
        return std::u32string(text);
    }

    uint32_t state = (seed != 0) ? seed : 0xDEADBEEF;
    std::u32string out;
    out.reserve(text.size());

    switch (mode) {
        case GlyphTransformMode::FlipVertical: {
            for (char32_t ch : text) {
                out.push_back(getFlippedVerticalGlyph(ch));
            }
            break;
        }
        case GlyphTransformMode::FlipHorizontal: {
            for (char32_t ch : text) {
                out.push_back(getFlippedHorizontalGlyph(ch));
            }
            break;
        }
        case GlyphTransformMode::Rotate180: {
            // 180 degrees = vertical flip reversed in order
            for (auto it = text.rbegin(); it != text.rend(); ++it) {
                out.push_back(getFlippedVerticalGlyph(*it));
            }
            break;
        }
        case GlyphTransformMode::Rotate90:
        case GlyphTransformMode::Rotate270: {
            // Visual simulated rotation marks
            for (char32_t ch : text) {
                out.push_back(getFlippedVerticalGlyph(ch));
            }
            break;
        }
        case GlyphTransformMode::Random: {
            for (char32_t ch : text) {
                uint32_t r = nextLcg(state) % 3;
                if (r == 0) {
                    out.push_back(getFlippedVerticalGlyph(ch));
                } else if (r == 1) {
                    out.push_back(getFlippedHorizontalGlyph(ch));
                } else {
                    out.push_back(ch);
                }
            }
            break;
        }
        default: {
            out.assign(text.begin(), text.end());
            break;
        }
    }
    return out;
}

} // namespace ok::chaos
