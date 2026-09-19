//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: src/core/ChaosEngine.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "ChaosEngine.hpp"

#include <algorithm>

namespace ok::chaos {

namespace {

// Fast deterministic LCG for repeatable transforms.
uint32_t nextLcg(uint32_t& state) noexcept {
    state = state * 1664525u + 1013904223u;
    return state;
}

float nextFloat(uint32_t& state) noexcept {
    return static_cast<float>(nextLcg(state) & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

constexpr bool isAsciiLower(char32_t ch) noexcept { return ch >= U'a' && ch <= U'z'; }
constexpr bool isAsciiUpper(char32_t ch) noexcept { return ch >= U'A' && ch <= U'Z'; }

// Vietnamese "Latin Extended Additional" block: U+1EA0 .. U+1EF9 is laid out as
// strictly alternating UPPER/lower pairs starting with an uppercase letter
// (Ạ ạ Ả ả ... ỹ), so parity decides the case.
constexpr char32_t kVnBlockFirst = 0x1EA0;
constexpr char32_t kVnBlockLast = 0x1EF9;

} // namespace

// Case classification is defined by the mapping itself: a character is
// uppercase when lowercasing it changes it (and vice versa). This keeps the two
// halves of the table impossible to desynchronise — the old implementation
// duplicated the ranges in both directions and disagreed with itself for
// U+00DF..U+00FF.
bool ChaosEngine::isUpperVn(char32_t ch) noexcept {
    return toLowerVn(ch) != ch;
}

bool ChaosEngine::isLowerVn(char32_t ch) noexcept {
    return toUpperVn(ch) != ch;
}

char32_t ChaosEngine::toUpperVn(char32_t ch) noexcept {
    if (isAsciiLower(ch)) {
        return static_cast<char32_t>(ch - 32);
    }
    if (ch >= kVnBlockFirst && ch <= kVnBlockLast) {
        return (((ch - kVnBlockFirst) % 2) == 1) ? static_cast<char32_t>(ch - 1) : ch;
    }
    if (ch == 0x00FF) {
        return 0x0178;   // ÿ → Ÿ
    }
    if (ch >= 0x00E0 && ch <= 0x00FE && ch != 0x00F7) {
        return static_cast<char32_t>(ch - 32);   // à..þ (minus ÷)
    }
    if (ch >= 0x0100 && ch <= 0x017F) {
        switch (ch) {
            case 0x0131: return U'I';
            case 0x0138: return ch;
            case 0x0149: return ch;
            case 0x017F: return U'S';
            default:
                return (((ch - 0x0100) % 2) == 1) ? static_cast<char32_t>(ch - 1) : ch;
        }
    }
    // Latin Extended-B is *not* parity-aligned: U+01A8, U+01AA, U+01AB and
    // U+01AE are lowercase letters sitting on an even offset, so the pair
    // table below is spelled out instead of guessed from the offset.
    switch (ch) {
        case 0x01A1: case 0x01A3: case 0x01A5: case 0x01A8:
        case 0x01AD: case 0x01B0:
            return static_cast<char32_t>(ch - 1);   // ơ→Ơ, ư→Ư, ƣ→Ƣ, ƥ→Ƥ, ƭ→Ƭ, ƨ→Ƨ
        default:
            return ch;
    }
}

char32_t ChaosEngine::toLowerVn(char32_t ch) noexcept {
    if (isAsciiUpper(ch)) {
        return static_cast<char32_t>(ch + 32);
    }
    if (ch >= kVnBlockFirst && ch <= kVnBlockLast) {
        return (((ch - kVnBlockFirst) % 2) == 0) ? static_cast<char32_t>(ch + 1) : ch;
    }
    if (ch == 0x0178) {
        return 0x00FF;   // Ÿ → ÿ
    }
    if (ch >= 0x00C0 && ch <= 0x00DE && ch != 0x00D7) {
        return static_cast<char32_t>(ch + 32);
    }
    if (ch >= 0x0100 && ch <= 0x017F) {
        switch (ch) {
            case 0x0130: return U'i';
            case 0x0138: return ch;
            case 0x0149: return ch;
            default:
                return (((ch - 0x0100) % 2) == 0) ? static_cast<char32_t>(ch + 1) : ch;
        }
    }
    switch (ch) {
        case 0x01A0: case 0x01A2: case 0x01A4: case 0x01A7:
        case 0x01AC: case 0x01AF:
            return static_cast<char32_t>(ch + 1);   // Ơ→ơ, Ư→ư, Ƣ→ƣ, Ƥ→ƥ, Ƭ→ƭ, Ƨ→ƨ
        default:
            return ch;
    }
}

char32_t ChaosEngine::toggleCaseVn(char32_t ch) noexcept {
    if (isUpperVn(ch)) {
        return toLowerVn(ch);
    }
    if (isLowerVn(ch)) {
        return toUpperVn(ch);
    }
    return ch;
}

//===========================================================================
// Config publication
//===========================================================================
ChaosEngine& ChaosEngine::instance() noexcept {
    static ChaosEngine s_instance;
    return s_instance;
}

ChaosEngine::ChaosEngine() {
    m_configForUi.reset();
    setConfig(m_configForUi);
}

std::uint64_t ChaosEngine::pack(const Snapshot& s) noexcept {
    std::uint64_t bits = 0;
    bits |= static_cast<std::uint64_t>(s.magic & 0xFFu) << 0;
    bits |= static_cast<std::uint64_t>(s.active & 0x1u) << 8;
    bits |= static_cast<std::uint64_t>(s.randomCase & 0x1u) << 9;
    bits |= static_cast<std::uint64_t>(s.granularity & 0x1u) << 10;
    bits |= static_cast<std::uint64_t>(s.glyphTransform & 0x1u) << 11;
    bits |= static_cast<std::uint64_t>(s.glyphMode & 0x7u) << 12;
    bits |= static_cast<std::uint64_t>(s.intensityPercent) << 16;
    bits |= static_cast<std::uint64_t>(s.glyphIntensityPercent) << 24;
    bits |= static_cast<std::uint64_t>(s.rotateRenderOnly & 0x1u) << 32;
    return bits;
}

ChaosEngine::Snapshot ChaosEngine::unpack(std::uint64_t bits) noexcept {
    Snapshot s{};
    s.magic = static_cast<std::uint32_t>(bits & 0xFFu);
    s.active = static_cast<std::uint8_t>((bits >> 8) & 0x1u);
    s.randomCase = static_cast<std::uint8_t>((bits >> 9) & 0x1u);
    s.granularity = static_cast<std::uint8_t>((bits >> 10) & 0x1u);
    s.glyphTransform = static_cast<std::uint8_t>((bits >> 11) & 0x1u);
    s.glyphMode = static_cast<std::uint8_t>((bits >> 12) & 0x7u);
    s.intensityPercent = static_cast<std::uint8_t>((bits >> 16) & 0xFFu);
    s.glyphIntensityPercent = static_cast<std::uint8_t>((bits >> 24) & 0xFFu);
    s.rotateRenderOnly = static_cast<std::uint8_t>((bits >> 32) & 0x1u);
    return s;
}

void ChaosEngine::setConfig(const ChaosConfig& config) noexcept {
    Snapshot snap{};
    snap.magic = 0x4Bu;   // 'K'
    snap.randomCase = config.randomCaseEnabled ? 1u : 0u;
    snap.granularity = static_cast<std::uint8_t>(config.caseGranularity);
    snap.glyphTransform = config.glyphTransformEnabled ? 1u : 0u;
    snap.glyphMode = static_cast<std::uint8_t>(config.glyphMode) & 0x7u;
    snap.intensityPercent = static_cast<std::uint8_t>(
        std::clamp(config.randomCaseIntensity, 0.0f, 1.0f) * 100.0f + 0.5f);
    snap.glyphIntensityPercent = static_cast<std::uint8_t>(
        std::clamp(config.glyphIntensity, 0.0f, 1.0f) * 100.0f + 0.5f);
    snap.rotateRenderOnly = config.rotateRenderOnly ? 1u : 0u;
    const bool active = config.masterEnabled &&
                        (config.randomCaseEnabled || config.glyphTransformEnabled);
    snap.active = active ? 1u : 0u;

    m_snapshot.store(pack(snap), std::memory_order_release);
    m_active.store(active ? 1u : 0u, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(m_configMutex);
    m_configForUi = config;
    m_configForUi.randomCaseIntensity = std::clamp(config.randomCaseIntensity, 0.0f, 1.0f);
    m_configForUi.glyphIntensity = std::clamp(config.glyphIntensity, 0.0f, 1.0f);
}

ChaosConfig ChaosEngine::getConfig() const noexcept {
    std::lock_guard<std::mutex> lock(m_configMutex);
    return m_configForUi;
}

//===========================================================================
// Transforms
//===========================================================================
std::u32string ChaosEngine::processCase(std::u32string_view text, uint32_t seed) const {
    const Snapshot snap = unpack(m_snapshot.load(std::memory_order_acquire));
    if (snap.active == 0 || snap.randomCase == 0 || snap.magic != 0x4Bu) {
        return std::u32string(text);
    }
    const float intensity = static_cast<float>(snap.intensityPercent) / 100.0f;
    return applyRandomCase(text, intensity,
                           snap.granularity == 0 ? CaseGranularity::ByChar
                                                 : CaseGranularity::ByWord,
                           seed);
}

std::u32string ChaosEngine::getVisualDisplayString(std::u32string_view text, uint32_t seed) const {
    const Snapshot snap = unpack(m_snapshot.load(std::memory_order_acquire));
    if (snap.active == 0 || snap.glyphTransform == 0 || snap.magic != 0x4Bu) {
        return std::u32string(text);
    }
    const auto mode = static_cast<GlyphTransformMode>(snap.glyphMode);
    if (snap.rotateRenderOnly != 0 && isRenderOnlyRotation(mode)) {
        // The renderer rotates the drawing; the text itself stays untouched so
        // the document never receives lookalikes that cannot be rotated back.
        return std::u32string(text);
    }
    return applyGlyphTransform(text, mode, seed,
                               static_cast<float>(snap.glyphIntensityPercent) / 100.0f);
}

std::u32string ChaosEngine::applyRandomCase(std::u32string_view text, float intensity,
                                            CaseGranularity gran, uint32_t seed) {
    if (text.empty() || intensity <= 0.0f) {
        return std::u32string(text);
    }
    uint32_t state = (seed != 0) ? seed : 0x1337C0DEu;
    std::u32string out;
    out.reserve(text.size());

    if (intensity >= 1.0f && gran == CaseGranularity::ByChar) {
        // Deterministic full flip: no RNG consumption at all.
        for (char32_t ch : text) {
            out.push_back(toggleCaseVn(ch));
        }
        return out;
    }

    if (gran == CaseGranularity::ByChar) {
        for (char32_t ch : text) {
            out.push_back(nextFloat(state) < intensity ? toggleCaseVn(ch) : ch);
        }
        return out;
    }

    // ByWord: every word is forced entirely to UPPER or lower.
    bool wordUpper = (nextFloat(state) < intensity);
    bool inWord = false;
    for (char32_t ch : text) {
        const bool separator = (ch == U' ' || ch == U'\t' || ch == U'\n' || ch == U'\r' ||
                                ch == U'.' || ch == U',' || ch == U'!' || ch == U'?' ||
                                ch == U';' || ch == U':' || ch == U'-' || ch == U'/' ||
                                ch == U'(' || ch == U')' || ch == U'"' || ch == U'\'');
        if (separator) {
            inWord = false;
            out.push_back(ch);
            continue;
        }
        if (!inWord) {
            inWord = true;
            wordUpper = (nextFloat(state) < intensity);
        }
        out.push_back(wordUpper ? toUpperVn(ch) : toLowerVn(ch));
    }
    return out;
}

char32_t ChaosEngine::getFlippedVerticalGlyph(char32_t ch) noexcept {
    switch (ch) {
        case U'a': return 0x0250;   // ɐ
        case U'b': return U'q';
        case U'c': return 0x0254;   // ɔ
        case U'd': return U'p';
        case U'e': return 0x01DD;   // ǝ
        case U'f': return 0x025F;   // ɟ
        case U'g': return 0x0183;   // ƃ
        case U'h': return 0x0265;   // ɥ
        case U'i': return 0x1D09;   // ᴉ
        case U'j': return 0x027E;   // ɾ
        case U'k': return 0x029E;   // ʞ
        case U'l': return U'l';
        case U'm': return 0x026F;   // ɯ
        case U'n': return U'u';
        case U'o': return U'o';
        case U'p': return U'd';
        case U'q': return U'b';
        case U'r': return 0x0279;   // ɹ
        case U's': return U's';
        case U't': return 0x0287;   // ʇ
        case U'u': return U'n';
        case U'v': return 0x028C;   // ʌ
        case U'w': return 0x028D;   // ʍ
        case U'x': return U'x';
        case U'y': return 0x028E;   // ʎ
        case U'z': return U'z';
        case U'A': return 0x2200;   // ∀
        case U'B': return 0x15FA;
        case U'C': return 0x0186;   // Ɔ
        case U'D': return 0x15E1;
        case U'E': return 0x018E;   // Ǝ
        case U'F': return 0x2132;   // Ⅎ
        case U'G': return 0x2141;   // ⅁
        case U'H': return U'H';
        case U'I': return U'I';
        case U'J': return 0x017F;   // ſ
        case U'K': return 0x029E;   // ʞ
        case U'L': return 0x02E5;
        case U'M': return U'W';
        case U'N': return 0x1D0E;
        case U'O': return U'O';
        case U'P': return 0x0500;
        case U'Q': return 0x038C;
        case U'R': return 0x1D1A;
        case U'S': return U'S';
        case U'T': return 0x22A5;   // ⊥
        case U'U': return 0x2229;   // ∩
        case U'V': return 0x039B;   // Λ
        case U'W': return U'M';
        case U'X': return U'X';
        case U'Y': return 0x2144;   // ⅄
        case U'Z': return U'Z';
        case U'0': return U'0';
        case U'1': return 0x21C2;
        case U'2': return 0x218A;
        case U'3': return 0x0190;   // Ɛ
        case U'4': return 0x21C3;
        case U'6': return U'9';
        case U'7': return 0x2C62;
        case U'9': return U'6';
        case U'?': return 0x00BF;   // ¿
        case U'!': return 0x00A1;   // ¡
        case U'.': return 0x02D9;   // ˙
        case U',': return 0x0027;   // '
        case U'(': return U')';
        case U')': return U'(';
        case U'[': return U']';
        case U']': return U'[';
        case U'{': return U'}';
        case U'}': return U'{';
        case U'<': return U'>';
        case U'>': return U'<';
        case U'_': return 0x203E;   // ‾
        // ---- v1.3.0-beta4: Vietnamese precomposed vowels ----
        // A vertical flip turns a rising tone stroke into a falling one, so
        // the honest single-codepoint approximation is the TONE MIRROR:
        //   sắc (´) <-> huyền (`), hỏi (?) <-> ngã (~),
        //   nặng (.) -> hỏi (a dot below reappears above as a hook-ish mark).
        // Shape marks (circumflex/breve/horn) are visually near-symmetric and
        // stay. Vowels with no tone (â, ă, ê, ô, ơ, ư, đ) have no precomposed
        // mirror and pass through — the mode stays 1:1 with the input, which
        // the live-effects erase accounting REQUIRES (one UTF-16 code unit in,
        // one out). Before this table, typing Vietnamese showed NO visible
        // ---- v1.3.0-beta4: Vietnamese precomposed vowels ----
        // A vertical flip turns a rising tone stroke into a falling one, so
        // the honest single-codepoint approximation is the TONE MIRROR:
        //   sắc (´) <-> huyền (`), hỏi (?) <-> ngã (~),
        //   nặng (.) -> hỏi (a dot below reappears above as a hook-ish mark).
        // Shape marks (circumflex/breve/horn) are visually near-symmetric and
        // stay. Vowels with no tone (â, ă, ê, ô, ơ, ư, đ) have no precomposed
        // mirror and pass through — the mode stays 1:1 with the input, which
        // the live-effects erase accounting REQUIRES (one UTF-16 code unit in,
        // one out). Before this table, typing Vietnamese showed NO visible
        // change at any intensity: the reported "mode không hoạt động".
        case U'\u00C0': return U'\u00C1';   // À -> Á
        case U'\u00C1': return U'\u00C0';   // Á -> À
        case U'\u00C3': return U'\u1EA2';   // Ã -> Ả
        case U'\u00C8': return U'\u00C9';   // È -> É
        case U'\u00C9': return U'\u00C8';   // É -> È
        case U'\u00CC': return U'\u00CD';   // Ì -> Í
        case U'\u00CD': return U'\u00CC';   // Í -> Ì
        case U'\u00D2': return U'\u00D3';   // Ò -> Ó
        case U'\u00D3': return U'\u00D2';   // Ó -> Ò
        case U'\u00D5': return U'\u1ECE';   // Õ -> Ỏ
        case U'\u00D9': return U'\u00DA';   // Ù -> Ú
        case U'\u00DA': return U'\u00D9';   // Ú -> Ù
        case U'\u00DD': return U'\u1EF2';   // Ý -> Ỳ
        case U'\u00E0': return U'\u00E1';   // à -> á
        case U'\u00E1': return U'\u00E0';   // á -> à
        case U'\u00E3': return U'\u1EA3';   // ã -> ả
        case U'\u00E8': return U'\u00E9';   // è -> é
        case U'\u00E9': return U'\u00E8';   // é -> è
        case U'\u00EC': return U'\u00ED';   // ì -> í
        case U'\u00ED': return U'\u00EC';   // í -> ì
        case U'\u00F2': return U'\u00F3';   // ò -> ó
        case U'\u00F3': return U'\u00F2';   // ó -> ò
        case U'\u00F5': return U'\u1ECF';   // õ -> ỏ
        case U'\u00F9': return U'\u00FA';   // ù -> ú
        case U'\u00FA': return U'\u00F9';   // ú -> ù
        case U'\u00FD': return U'\u1EF3';   // ý -> ỳ
        case U'\u1EA0': return U'\u1EA2';   // Ạ -> Ả
        case U'\u1EA1': return U'\u1EA3';   // ạ -> ả
        case U'\u1EA2': return U'\u00C3';   // Ả -> Ã
        case U'\u1EA3': return U'\u00E3';   // ả -> ã
        case U'\u1EA4': return U'\u1EA6';   // Ấ -> Ầ
        case U'\u1EA5': return U'\u1EA7';   // ấ -> ầ
        case U'\u1EA6': return U'\u1EA4';   // Ầ -> Ấ
        case U'\u1EA7': return U'\u1EA5';   // ầ -> ấ
        case U'\u1EA8': return U'\u1EAA';   // Ẩ -> Ẫ
        case U'\u1EA9': return U'\u1EAB';   // ẩ -> ẫ
        case U'\u1EAA': return U'\u1EA8';   // Ẫ -> Ẩ
        case U'\u1EAB': return U'\u1EA9';   // ẫ -> ẩ
        case U'\u1EAC': return U'\u1EA8';   // Ậ -> Ẩ
        case U'\u1EAD': return U'\u1EA9';   // ậ -> ẩ
        case U'\u1EAE': return U'\u1EB0';   // Ắ -> Ằ
        case U'\u1EAF': return U'\u1EB1';   // ắ -> ằ
        case U'\u1EB0': return U'\u1EAE';   // Ằ -> Ắ
        case U'\u1EB1': return U'\u1EAF';   // ằ -> ắ
        case U'\u1EB2': return U'\u1EB4';   // Ẳ -> Ẵ
        case U'\u1EB3': return U'\u1EB5';   // ẳ -> ẵ
        case U'\u1EB4': return U'\u1EB2';   // Ẵ -> Ẳ
        case U'\u1EB5': return U'\u1EB3';   // ẵ -> ẳ
        case U'\u1EB6': return U'\u1EB2';   // Ặ -> Ẳ
        case U'\u1EB7': return U'\u1EB3';   // ặ -> ẳ
        case U'\u1EB8': return U'\u1EBA';   // Ẹ -> Ẻ
        case U'\u1EB9': return U'\u1EBB';   // ẹ -> ẻ
        case U'\u1EBA': return U'\u1EBC';   // Ẻ -> Ẽ
        case U'\u1EBB': return U'\u1EBD';   // ẻ -> ẽ
        case U'\u1EBC': return U'\u1EBA';   // Ẽ -> Ẻ
        case U'\u1EBD': return U'\u1EBB';   // ẽ -> ẻ
        case U'\u1EBE': return U'\u1EC0';   // Ế -> Ề
        case U'\u1EBF': return U'\u1EC1';   // ế -> ề
        case U'\u1EC0': return U'\u1EBE';   // Ề -> Ế
        case U'\u1EC1': return U'\u1EBF';   // ề -> ế
        case U'\u1EC2': return U'\u1EC4';   // Ể -> Ễ
        case U'\u1EC3': return U'\u1EC5';   // ể -> ễ
        case U'\u1EC4': return U'\u1EC2';   // Ễ -> Ể
        case U'\u1EC5': return U'\u1EC3';   // ễ -> ể
        case U'\u1EC6': return U'\u1EC2';   // Ệ -> Ể
        case U'\u1EC7': return U'\u1EC3';   // ệ -> ể
        case U'\u1EC8': return U'\u0128';   // Ỉ -> Ĩ
        case U'\u1EC9': return U'\u0129';   // ỉ -> ĩ
        case U'\u1ECA': return U'\u1EC8';   // Ị -> Ỉ
        case U'\u1ECB': return U'\u1EC9';   // ị -> ỉ
        case U'\u1ECC': return U'\u1ECE';   // Ọ -> Ỏ
        case U'\u1ECD': return U'\u1ECF';   // ọ -> ỏ
        case U'\u1ECE': return U'\u00D5';   // Ỏ -> Õ
        case U'\u1ECF': return U'\u00F5';   // ỏ -> õ
        case U'\u1ED0': return U'\u1ED2';   // Ố -> Ồ
        case U'\u1ED1': return U'\u1ED3';   // ố -> ồ
        case U'\u1ED2': return U'\u1ED0';   // Ồ -> Ố
        case U'\u1ED3': return U'\u1ED1';   // ồ -> ố
        case U'\u1ED4': return U'\u1ED6';   // Ổ -> Ỗ
        case U'\u1ED5': return U'\u1ED7';   // ổ -> ỗ
        case U'\u1ED6': return U'\u1ED4';   // Ỗ -> Ổ
        case U'\u1ED7': return U'\u1ED5';   // ỗ -> ổ
        case U'\u1ED8': return U'\u1ED4';   // Ộ -> Ổ
        case U'\u1ED9': return U'\u1ED5';   // ộ -> ổ
        case U'\u1EDA': return U'\u1EDC';   // Ớ -> Ờ
        case U'\u1EDB': return U'\u1EDD';   // ớ -> ờ
        case U'\u1EDC': return U'\u1EDA';   // Ờ -> Ớ
        case U'\u1EDD': return U'\u1EDB';   // ờ -> ớ
        case U'\u1EDE': return U'\u1EE0';   // Ở -> Ỡ
        case U'\u1EDF': return U'\u1EE1';   // ở -> ỡ
        case U'\u1EE0': return U'\u1EDE';   // Ỡ -> Ở
        case U'\u1EE1': return U'\u1EDF';   // ỡ -> ở
        case U'\u1EE2': return U'\u1EDE';   // Ợ -> Ở
        case U'\u1EE3': return U'\u1EDF';   // ợ -> ở
        case U'\u1EE4': return U'\u1EE6';   // Ụ -> Ủ
        case U'\u1EE5': return U'\u1EE7';   // ụ -> ủ
        case U'\u1EE6': return U'\u0168';   // Ủ -> Ũ
        case U'\u1EE7': return U'\u0169';   // ủ -> ũ
        case U'\u1EE8': return U'\u1EEA';   // Ứ -> Ừ
        case U'\u1EE9': return U'\u1EEB';   // ứ -> ừ
        case U'\u1EEA': return U'\u1EE8';   // Ừ -> Ứ
        case U'\u1EEB': return U'\u1EE9';   // ừ -> ứ
        case U'\u1EEC': return U'\u1EEE';   // Ử -> Ữ
        case U'\u1EED': return U'\u1EEF';   // ử -> ữ
        case U'\u1EEE': return U'\u1EEC';   // Ữ -> Ử
        case U'\u1EEF': return U'\u1EED';   // ữ -> ử
        case U'\u1EF0': return U'\u1EEC';   // Ự -> Ử
        case U'\u1EF1': return U'\u1EED';   // ự -> ử
        case U'\u1EF2': return U'\u00DD';   // Ỳ -> Ý
        case U'\u1EF3': return U'\u00FD';   // ỳ -> ý
        case U'\u1EF4': return U'\u1EF6';   // Ỵ -> Ỷ
        case U'\u1EF5': return U'\u1EF7';   // ỵ -> ỷ
        case U'\u1EF6': return U'\u1EF8';   // Ỷ -> Ỹ
        case U'\u1EF7': return U'\u1EF9';   // ỷ -> ỹ
        case U'\u1EF8': return U'\u1EF6';   // Ỹ -> Ỷ
        case U'\u1EF9': return U'\u1EF7';   // ỹ -> ỷ
        default: return ch;
    }
}

char32_t ChaosEngine::getFlippedHorizontalGlyph(char32_t ch) noexcept {
    switch (ch) {
        case U'b': return U'd';
        case U'd': return U'b';
        case U'p': return U'q';
        case U'q': return U'p';
        case U'c': return 0x0254;   // ɔ
        case U'(': return U')';
        case U')': return U'(';
        case U'[': return U']';
        case U']': return U'[';
        case U'{': return U'}';
        case U'}': return U'{';
        case U'<': return U'>';
        case U'>': return U'<';
        case U's': return 0x01A8;   // ƨ
        case U'S': return 0x01A7;   // Ƨ
        case U'E': return 0x018E;   // Ǝ
        case U'J': return 0x0196;   // Ɩ
        case U'L': return 0x2143;   // ⅃
        case U'P': return 0x01A5;   // ƥ
        case U'Z': return U'Z';
        case U'2': return U'S';
        case U'5': return U'Z';
        // ---- v1.3.0-beta4: Vietnamese precomposed vowels ----
        // Horizontally mirroring a tone mark turns sắc into huyền and vice
        // versa (the same rising->falling stroke change as the vertical
        // flip), so the vowel table is shared with getFlippedVerticalGlyph.
        case U'\u00C0': return U'\u00C1';   // À -> Á
        case U'\u00C1': return U'\u00C0';   // Á -> À
        case U'\u00C3': return U'\u1EA2';   // Ã -> Ả
        case U'\u00C8': return U'\u00C9';   // È -> É
        case U'\u00C9': return U'\u00C8';   // É -> È
        case U'\u00CC': return U'\u00CD';   // Ì -> Í
        case U'\u00CD': return U'\u00CC';   // Í -> Ì
        case U'\u00D2': return U'\u00D3';   // Ò -> Ó
        case U'\u00D3': return U'\u00D2';   // Ó -> Ò
        case U'\u00D5': return U'\u1ECE';   // Õ -> Ỏ
        case U'\u00D9': return U'\u00DA';   // Ù -> Ú
        case U'\u00DA': return U'\u00D9';   // Ú -> Ù
        case U'\u00DD': return U'\u1EF2';   // Ý -> Ỳ
        case U'\u00E0': return U'\u00E1';   // à -> á
        case U'\u00E1': return U'\u00E0';   // á -> à
        case U'\u00E3': return U'\u1EA3';   // ã -> ả
        case U'\u00E8': return U'\u00E9';   // è -> é
        case U'\u00E9': return U'\u00E8';   // é -> è
        case U'\u00EC': return U'\u00ED';   // ì -> í
        case U'\u00ED': return U'\u00EC';   // í -> ì
        case U'\u00F2': return U'\u00F3';   // ò -> ó
        case U'\u00F3': return U'\u00F2';   // ó -> ò
        case U'\u00F5': return U'\u1ECF';   // õ -> ỏ
        case U'\u00F9': return U'\u00FA';   // ù -> ú
        case U'\u00FA': return U'\u00F9';   // ú -> ù
        case U'\u00FD': return U'\u1EF3';   // ý -> ỳ
        case U'\u1EA0': return U'\u1EA2';   // Ạ -> Ả
        case U'\u1EA1': return U'\u1EA3';   // ạ -> ả
        case U'\u1EA2': return U'\u00C3';   // Ả -> Ã
        case U'\u1EA3': return U'\u00E3';   // ả -> ã
        case U'\u1EA4': return U'\u1EA6';   // Ấ -> Ầ
        case U'\u1EA5': return U'\u1EA7';   // ấ -> ầ
        case U'\u1EA6': return U'\u1EA4';   // Ầ -> Ấ
        case U'\u1EA7': return U'\u1EA5';   // ầ -> ấ
        case U'\u1EA8': return U'\u1EAA';   // Ẩ -> Ẫ
        case U'\u1EA9': return U'\u1EAB';   // ẩ -> ẫ
        case U'\u1EAA': return U'\u1EA8';   // Ẫ -> Ẩ
        case U'\u1EAB': return U'\u1EA9';   // ẫ -> ẩ
        case U'\u1EAC': return U'\u1EA8';   // Ậ -> Ẩ
        case U'\u1EAD': return U'\u1EA9';   // ậ -> ẩ
        case U'\u1EAE': return U'\u1EB0';   // Ắ -> Ằ
        case U'\u1EAF': return U'\u1EB1';   // ắ -> ằ
        case U'\u1EB0': return U'\u1EAE';   // Ằ -> Ắ
        case U'\u1EB1': return U'\u1EAF';   // ằ -> ắ
        case U'\u1EB2': return U'\u1EB4';   // Ẳ -> Ẵ
        case U'\u1EB3': return U'\u1EB5';   // ẳ -> ẵ
        case U'\u1EB4': return U'\u1EB2';   // Ẵ -> Ẳ
        case U'\u1EB5': return U'\u1EB3';   // ẵ -> ẳ
        case U'\u1EB6': return U'\u1EB2';   // Ặ -> Ẳ
        case U'\u1EB7': return U'\u1EB3';   // ặ -> ẳ
        case U'\u1EB8': return U'\u1EBA';   // Ẹ -> Ẻ
        case U'\u1EB9': return U'\u1EBB';   // ẹ -> ẻ
        case U'\u1EBA': return U'\u1EBC';   // Ẻ -> Ẽ
        case U'\u1EBB': return U'\u1EBD';   // ẻ -> ẽ
        case U'\u1EBC': return U'\u1EBA';   // Ẽ -> Ẻ
        case U'\u1EBD': return U'\u1EBB';   // ẽ -> ẻ
        case U'\u1EBE': return U'\u1EC0';   // Ế -> Ề
        case U'\u1EBF': return U'\u1EC1';   // ế -> ề
        case U'\u1EC0': return U'\u1EBE';   // Ề -> Ế
        case U'\u1EC1': return U'\u1EBF';   // ề -> ế
        case U'\u1EC2': return U'\u1EC4';   // Ể -> Ễ
        case U'\u1EC3': return U'\u1EC5';   // ể -> ễ
        case U'\u1EC4': return U'\u1EC2';   // Ễ -> Ể
        case U'\u1EC5': return U'\u1EC3';   // ễ -> ể
        case U'\u1EC6': return U'\u1EC2';   // Ệ -> Ể
        case U'\u1EC7': return U'\u1EC3';   // ệ -> ể
        case U'\u1EC8': return U'\u0128';   // Ỉ -> Ĩ
        case U'\u1EC9': return U'\u0129';   // ỉ -> ĩ
        case U'\u1ECA': return U'\u1EC8';   // Ị -> Ỉ
        case U'\u1ECB': return U'\u1EC9';   // ị -> ỉ
        case U'\u1ECC': return U'\u1ECE';   // Ọ -> Ỏ
        case U'\u1ECD': return U'\u1ECF';   // ọ -> ỏ
        case U'\u1ECE': return U'\u00D5';   // Ỏ -> Õ
        case U'\u1ECF': return U'\u00F5';   // ỏ -> õ
        case U'\u1ED0': return U'\u1ED2';   // Ố -> Ồ
        case U'\u1ED1': return U'\u1ED3';   // ố -> ồ
        case U'\u1ED2': return U'\u1ED0';   // Ồ -> Ố
        case U'\u1ED3': return U'\u1ED1';   // ồ -> ố
        case U'\u1ED4': return U'\u1ED6';   // Ổ -> Ỗ
        case U'\u1ED5': return U'\u1ED7';   // ổ -> ỗ
        case U'\u1ED6': return U'\u1ED4';   // Ỗ -> Ổ
        case U'\u1ED7': return U'\u1ED5';   // ỗ -> ổ
        case U'\u1ED8': return U'\u1ED4';   // Ộ -> Ổ
        case U'\u1ED9': return U'\u1ED5';   // ộ -> ổ
        case U'\u1EDA': return U'\u1EDC';   // Ớ -> Ờ
        case U'\u1EDB': return U'\u1EDD';   // ớ -> ờ
        case U'\u1EDC': return U'\u1EDA';   // Ờ -> Ớ
        case U'\u1EDD': return U'\u1EDB';   // ờ -> ớ
        case U'\u1EDE': return U'\u1EE0';   // Ở -> Ỡ
        case U'\u1EDF': return U'\u1EE1';   // ở -> ỡ
        case U'\u1EE0': return U'\u1EDE';   // Ỡ -> Ở
        case U'\u1EE1': return U'\u1EDF';   // ỡ -> ở
        case U'\u1EE2': return U'\u1EDE';   // Ợ -> Ở
        case U'\u1EE3': return U'\u1EDF';   // ợ -> ở
        case U'\u1EE4': return U'\u1EE6';   // Ụ -> Ủ
        case U'\u1EE5': return U'\u1EE7';   // ụ -> ủ
        case U'\u1EE6': return U'\u0168';   // Ủ -> Ũ
        case U'\u1EE7': return U'\u0169';   // ủ -> ũ
        case U'\u1EE8': return U'\u1EEA';   // Ứ -> Ừ
        case U'\u1EE9': return U'\u1EEB';   // ứ -> ừ
        case U'\u1EEA': return U'\u1EE8';   // Ừ -> Ứ
        case U'\u1EEB': return U'\u1EE9';   // ừ -> ứ
        case U'\u1EEC': return U'\u1EEE';   // Ử -> Ữ
        case U'\u1EED': return U'\u1EEF';   // ử -> ữ
        case U'\u1EEE': return U'\u1EEC';   // Ữ -> Ử
        case U'\u1EEF': return U'\u1EED';   // ữ -> ử
        case U'\u1EF0': return U'\u1EEC';   // Ự -> Ử
        case U'\u1EF1': return U'\u1EED';   // ự -> ử
        case U'\u1EF2': return U'\u00DD';   // Ỳ -> Ý
        case U'\u1EF3': return U'\u00FD';   // ỳ -> ý
        case U'\u1EF4': return U'\u1EF6';   // Ỵ -> Ỷ
        case U'\u1EF5': return U'\u1EF7';   // ỵ -> ỷ
        case U'\u1EF6': return U'\u1EF8';   // Ỷ -> Ỹ
        case U'\u1EF7': return U'\u1EF9';   // ỷ -> ỹ
        case U'\u1EF8': return U'\u1EF6';   // Ỹ -> Ỷ
        case U'\u1EF9': return U'\u1EF7';   // ỹ -> ỷ
        default: return ch;
    }
}

std::u32string ChaosEngine::applyGlyphTransform(std::u32string_view text, GlyphTransformMode mode,
                                                uint32_t seed, float intensity) {
    if (text.empty() || mode == GlyphTransformMode::None) {
        return std::u32string(text);
    }
    // Quarter turns are a renderer concern; returning the text unchanged is the
    // only honest text-level result.
    if (isRenderOnlyRotation(mode)) {
        return std::u32string(text);
    }

    uint32_t state = (seed != 0) ? seed : 0xDEADBEEFu;
    const float alpha = std::clamp(intensity, 0.0f, 1.0f);

    auto maybe = [&](char32_t original, char32_t transformed) {
        if (alpha >= 1.0f) {
            return transformed;
        }
        if (alpha <= 0.0f) {
            return original;
        }
        return nextFloat(state) < alpha ? transformed : original;
    };

    std::u32string out;
    out.reserve(text.size());

    switch (mode) {
        case GlyphTransformMode::FlipVertical:
            for (char32_t ch : text) {
                out.push_back(maybe(ch, getFlippedVerticalGlyph(ch)));
            }
            break;
        case GlyphTransformMode::FlipHorizontal:
            for (char32_t ch : text) {
                out.push_back(maybe(ch, getFlippedHorizontalGlyph(ch)));
            }
            break;
        case GlyphTransformMode::Rotate180:
            // Upside-down = reversed order + vertical-flip lookalikes.
            for (auto it = text.rbegin(); it != text.rend(); ++it) {
                out.push_back(getFlippedVerticalGlyph(*it));
            }
            break;
        case GlyphTransformMode::Random:
            for (char32_t ch : text) {
                const uint32_t r = nextLcg(state) % 3u;
                if (r == 0u) {
                    out.push_back(getFlippedVerticalGlyph(ch));
                } else if (r == 1u) {
                    out.push_back(getFlippedHorizontalGlyph(ch));
                } else {
                    out.push_back(ch);
                }
            }
            break;
        case GlyphTransformMode::Rotate90:
        case GlyphTransformMode::Rotate270:
        case GlyphTransformMode::None:
        default:
            out.assign(text.begin(), text.end());
            break;
    }
    return out;
}

} // namespace ok::chaos
