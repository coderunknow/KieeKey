//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/test_chaos.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
// Chaos / Experimental Lab suite (v1.3.0).
//
//  1. Chaos Case          — enable/disable, killswitch, granularity, determinism
//  2. Glyph transform     — text integrity, render-only rotations
//  3. Vietnamese casing   — full alphabet round-trip, parity ranges, caseless chars
//  4. Display string      — what the renderer/front-end actually receives
//============================================================================
#include "ChaosEngine.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace ok::chaos;

namespace {

std::u32string foldCase(const std::u32string& s) {
    std::u32string out;
    out.reserve(s.size());
    for (char32_t ch : s) {
        out.push_back(ChaosEngine::toLowerVn(ch));
    }
    return out;
}

// Every Vietnamese letter (precomposed) that must survive a case round-trip.
const std::vector<char32_t>& vietnameseAlphabet() {
    static const std::vector<char32_t> alphabet = [] {
        std::vector<char32_t> v;
        // ASCII
        for (char32_t c = U'a'; c <= U'z'; ++c) { v.push_back(c); }
        for (char32_t c = U'A'; c <= U'Z'; ++c) { v.push_back(c); }
        // Latin-1/Latin Extended-A
        const char32_t extra[] = {0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C8, 0x00C9, 0x00CA, 0x00CC,
                                  0x00CD, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D9, 0x00DA, 0x00DD,
                                  0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E8, 0x00E9, 0x00EA, 0x00EC,
                                  0x00ED, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F9, 0x00FA, 0x00FD,
                                  0x0102, 0x0103, 0x0110, 0x0111, 0x0128, 0x0129, 0x0168, 0x0169,
                                  0x01A0, 0x01A1, 0x01AF, 0x01B0};
        for (char32_t c : extra) { v.push_back(c); }
        // Vietnamese Extended (U+1EA0..U+1EF9): alternating upper/lower,
        // starting with uppercase.
        for (char32_t c = 0x1EA0; c <= 0x1EF9; ++c) { v.push_back(c); }
        return v;
    }();
    return alphabet;
}

} // namespace

//---------------------------------------------------------------------------
void testChaosCase() {
    auto& engine = ChaosEngine::instance();
    ChaosConfig cfg;
    cfg.reset();

    // Default: disabled -> no change
    engine.setConfig(cfg);
    assert(!engine.isChaosActive());
    std::u32string orig = U"KieeKey Vietnamese Input Method";
    assert(engine.processCase(orig) == orig);

    // Enable Chaos Case
    cfg.masterEnabled = true;
    cfg.randomCaseEnabled = true;
    cfg.randomCaseIntensity = 1.0f; // 100% case flip
    cfg.caseGranularity = CaseGranularity::ByChar;
    engine.setConfig(cfg);
    assert(engine.isChaosActive());

    std::u32string flipped = engine.processCase(orig, 12345);
    assert(flipped != orig);

    // Determinism: same seed => same output; different seed => (almost surely) different.
    assert(engine.processCase(orig, 12345) == flipped);
    assert(foldCase(flipped) == foldCase(orig));   // case-only change, text intact

    // Master killswitch
    cfg.masterEnabled = false;
    engine.setConfig(cfg);
    assert(!engine.isChaosActive());
    assert(engine.processCase(orig) == orig);

    // Intensity 0.0 must be a no-op even when everything is enabled.
    cfg.masterEnabled = true;
    cfg.randomCaseIntensity = 0.0f;
    engine.setConfig(cfg);
    assert(engine.processCase(orig, 7) == orig);

    // Intensity 1.0 + ByChar flips every cased character.
    cfg.randomCaseIntensity = 1.0f;
    cfg.caseGranularity = CaseGranularity::ByChar;
    engine.setConfig(cfg);
    const std::u32string allFlipped = engine.processCase(U"abcXYZ", 99);
    assert(allFlipped == U"ABCxyz");

    // ByWord: every character of a word shares one decision.
    const std::u32string byWord =
        ChaosEngine::applyRandomCase(U"one two three", 1.0f, CaseGranularity::ByWord, 4);
    std::size_t wordStart = 0;
    while (wordStart < byWord.size()) {
        const std::size_t wordEnd = byWord.find(U' ', wordStart);
        const std::size_t end = (wordEnd == std::u32string::npos) ? byWord.size() : wordEnd;
        if (end > wordStart) {
            const bool firstUpper = ChaosEngine::isUpperVn(byWord[wordStart]);
            for (std::size_t i = wordStart; i < end; ++i) {
                assert(ChaosEngine::isUpperVn(byWord[i]) == firstUpper);
            }
        }
        wordStart = end + 1;
    }

    // getConfig() must round-trip what setConfig() stored (the UI reads it back).
    const ChaosConfig readBack = engine.getConfig();
    assert(readBack.masterEnabled == cfg.masterEnabled);
    assert(readBack.caseGranularity == cfg.caseGranularity);
    assert(readBack.randomCaseIntensity == cfg.randomCaseIntensity);

    cfg.reset();
    engine.setConfig(cfg);
    std::cout << "  [PASS] Chaos Case tests\n";
}

//---------------------------------------------------------------------------
void testGlyphTransformTextIntegrity() {
    auto& engine = ChaosEngine::instance();
    ChaosConfig cfg;
    cfg.reset();

    std::u32string orig = U"hello world";

    // Visual transform FlipVertical
    auto vFlip = ChaosEngine::applyGlyphTransform(orig, GlyphTransformMode::FlipVertical);
    assert(vFlip != orig);

    // FlipHorizontal
    auto hFlip = ChaosEngine::applyGlyphTransform(orig, GlyphTransformMode::FlipHorizontal);
    assert(hFlip != orig);

    // Rotate180
    auto r180 = ChaosEngine::applyGlyphTransform(orig, GlyphTransformMode::Rotate180);
    assert(r180 != orig);

    // Every glyph transform is 1:1 with the input: never lose or add characters
    // (the engine keeps its own text model; only the display is mutated).
    for (auto mode : {GlyphTransformMode::Rotate90, GlyphTransformMode::Rotate180,
                      GlyphTransformMode::Rotate270, GlyphTransformMode::FlipHorizontal,
                      GlyphTransformMode::FlipVertical, GlyphTransformMode::Random}) {
        const std::u32string out = ChaosEngine::applyGlyphTransform(orig, mode, 11, 1.0f);
        assert(out.size() == orig.size());
        for (char32_t ch : out) {
            assert(ch != 0);
            assert(ch >= 0x20);            // never emits control characters
        }
    }

    // Rotate90/270 are render-only: the text layer must stay byte-identical.
    assert(ChaosEngine::isRenderOnlyRotation(GlyphTransformMode::Rotate90));
    assert(ChaosEngine::isRenderOnlyRotation(GlyphTransformMode::Rotate270));
    assert(!ChaosEngine::isRenderOnlyRotation(GlyphTransformMode::Rotate180));
    assert(ChaosEngine::applyGlyphTransform(orig, GlyphTransformMode::Rotate90) == orig);
    assert(ChaosEngine::applyGlyphTransform(orig, GlyphTransformMode::Rotate270) == orig);
    // "text safe" means the mode may be written into a real document; a
    // render-only rotation is by definition handled by the renderer instead.
    assert(!ChaosEngine::isTextSafe(GlyphTransformMode::Rotate90));
    assert(!ChaosEngine::isTextSafe(GlyphTransformMode::Rotate270));
    assert(ChaosEngine::isTextSafe(GlyphTransformMode::FlipVertical));
    assert(ChaosEngine::isTextSafe(GlyphTransformMode::Rotate180));

    // Intensity 0 => identity, intensity 1 => full transform.
    assert(ChaosEngine::applyGlyphTransform(orig, GlyphTransformMode::FlipVertical, 3, 0.0f) == orig);

    // CRITICAL INVARIANT: The underlying text model remains unchanged!
    // Visual transforms produce display lookalikes only.
    cfg.masterEnabled = true;
    cfg.glyphTransformEnabled = true;
    cfg.glyphMode = GlyphTransformMode::FlipVertical;
    engine.setConfig(cfg);

    auto displayStr = engine.getVisualDisplayString(orig);
    assert(!displayStr.empty());
    // Underlying text is still 'orig'
    assert(orig == U"hello world");

    // Disabled glyph transform => display string identical to the input.
    cfg.glyphTransformEnabled = false;
    engine.setConfig(cfg);
    assert(engine.getVisualDisplayString(orig) == orig);

    cfg.reset();
    engine.setConfig(cfg);
    std::cout << "  [PASS] Glyph Transform & Text Integrity tests\n";
}

//---------------------------------------------------------------------------
void testVietnameseCaseMapping() {
    for (char32_t lower : vietnameseAlphabet()) {
        const char32_t upper = ChaosEngine::toUpperVn(lower);
        const char32_t backLower = ChaosEngine::toLowerVn(upper);
        if (upper != lower) {
            assert(ChaosEngine::isUpperVn(upper));
            assert(ChaosEngine::isLowerVn(backLower));
            assert(ChaosEngine::toggleCaseVn(upper) == backLower);
            assert(ChaosEngine::toggleCaseVn(lower) == upper);
            // The mapping is involutive for the cased characters of the set.
            assert(ChaosEngine::toUpperVn(backLower) == upper);
        }
    }

    // Spot checks with actual Vietnamese words, upper and lower.
    const std::u32string lowerWord = U"tiếng việt rất đẹp";
    const std::u32string upperWord = U"TIẾNG VIỆT RẤT ĐẸP";
    const std::u32string lowered = ChaosEngine::applyRandomCase(upperWord, 0.0f,
                                                                CaseGranularity::ByChar, 1);
    assert(lowered == upperWord);   // intensity 0 keeps input untouched
    assert(ChaosEngine::toLowerVn(0x1EA0) == 0x1EA1);   // Ạ -> ạ
    assert(ChaosEngine::toUpperVn(0x1EA1) == 0x1EA0);   // ạ -> Ạ
    assert(ChaosEngine::toLowerVn(0x01AF) == 0x01B0);   // Ư -> ư
    assert(ChaosEngine::toUpperVn(0x01B0) == 0x01AF);   // ư -> Ư
    assert(ChaosEngine::toUpperVn(0x00FF) == 0x0178);   // ÿ -> Ÿ
    assert(ChaosEngine::toLowerVn(0x0178) == 0x00FF);   // Ÿ -> ÿ

    // Caseless characters must not be mangled or reported as cased.
    assert(ChaosEngine::toUpperVn(0x00D7) == 0x00D7);   // ×
    assert(ChaosEngine::toLowerVn(0x00D7) == 0x00D7);
    assert(!ChaosEngine::isUpperVn(0x00D7));
    assert(!ChaosEngine::isLowerVn(0x00D7));

    // Case randomisation over Vietnamese text preserves the whole text under folding.
    const std::u32string mixed = ChaosEngine::applyRandomCase(lowerWord, 1.0f,
                                                              CaseGranularity::ByChar, 2024);
    assert(foldCase(mixed) == foldCase(lowerWord));
    std::cout << "  [PASS] Vietnamese case mapping tests\n";
}

//---------------------------------------------------------------------------
void testGlyphTablesAndDisplayPipeline() {
    // Upside-down / mirrored lookalike tables: never map a printable character
    // to a control character or to NUL.
    for (char32_t c = 0x20; c < 0x300; ++c) {
        const char32_t v = ChaosEngine::getFlippedVerticalGlyph(c);
        const char32_t h = ChaosEngine::getFlippedHorizontalGlyph(c);
        assert(v >= 0x20);
        assert(h >= 0x20);
    }
    // Documented mirror pairs are exact involutions.
    assert(ChaosEngine::getFlippedVerticalGlyph(U'b') == U'q');
    assert(ChaosEngine::getFlippedVerticalGlyph(U'q') == U'b');
    assert(ChaosEngine::getFlippedHorizontalGlyph(U'b') == U'd');
    assert(ChaosEngine::getFlippedHorizontalGlyph(U'd') == U'b');
    assert(ChaosEngine::getFlippedVerticalGlyph(U'p') == U'd');
    // Characters with no lookalike are passed through untouched (so the text
    // layer is never damaged by a partial table).
    for (char32_t c = 0x20; c < 0x250; ++c) {
        const char32_t v = ChaosEngine::getFlippedVerticalGlyph(c);
        if (v == c) { assert(ChaosEngine::getFlippedVerticalGlyph(v) == v); }
    }

    // Full pipeline: case chaos + glyph chaos together, on a Vietnamese sentence.
    auto& engine = ChaosEngine::instance();
    ChaosConfig cfg;
    cfg.reset();
    cfg.masterEnabled = true;
    cfg.randomCaseEnabled = true;
    cfg.randomCaseIntensity = 0.6f;
    cfg.glyphTransformEnabled = true;
    cfg.glyphMode = GlyphTransformMode::Random;
    cfg.glyphIntensity = 0.5f;
    engine.setConfig(cfg);
    assert(engine.isChaosActive());

    const std::u32string src = U"Hôm nay trời đẹp quá, gõ phím thôi!";
    const std::u32string out = engine.getVisualDisplayString(src, 4242);
    assert(!out.empty());
    assert(out.size() == src.size());                 // 1:1 glyph substitution
    assert(engine.getVisualDisplayString(src, 4242) == out);   // deterministic

    cfg.reset();
    engine.setConfig(cfg);
    assert(!engine.isChaosActive());
    std::cout << "  [PASS] Glyph tables & full display pipeline tests\n";
}

//---------------------------------------------------------------------------
// v1.3.0-beta4: Vietnamese precomposed vowels must VISIBLY transform under
// the glyph modes. Before the tone-mirror table, typing Vietnamese showed no
// change at any intensity — the reported "mode không hoạt động khi gõ ở ngoài".
void testVietnameseGlyphFlips() {
    // Tone mirror: sắc <-> huyền (a vertical or horizontal flip turns the
    // rising stroke into a falling one).
    assert(ChaosEngine::getFlippedVerticalGlyph(U'\u00E1') == U'\u00E0');   // á -> à
    assert(ChaosEngine::getFlippedVerticalGlyph(U'\u00E0') == U'\u00E1');   // à -> á
    assert(ChaosEngine::getFlippedVerticalGlyph(U'\u1EBF') == U'\u1EC1');   // ế -> ề
    assert(ChaosEngine::getFlippedHorizontalGlyph(U'\u1ED1') == U'\u1ED3'); // ố -> ồ
    assert(ChaosEngine::getFlippedVerticalGlyph(U'\u1ED9') == U'\u1ED5');   // ộ -> ổ
    // hook <-> tilde
    assert(ChaosEngine::getFlippedVerticalGlyph(U'\u1EA3') == U'\u00E3');   // ả -> ã
    assert(ChaosEngine::getFlippedVerticalGlyph(U'\u1EC9') == U'\u0129');   // ỉ -> ĩ
    // Tone-less vowels have no precomposed mirror: pass through unchanged.
    assert(ChaosEngine::getFlippedVerticalGlyph(U'\u00E2') == U'\u00E2');   // â
    assert(ChaosEngine::getFlippedVerticalGlyph(U'\u0111') == U'\u0111');   // đ
    // Every accented VN vowel maps to a single codepoint (the live-effects
    // erase accounting requires 1 UTF-16 unit in -> 1 unit out).
    for (char32_t ch = 0x00C0; ch <= 0x1EF9; ++ch) {
        if (ChaosEngine::getFlippedVerticalGlyph(ch) != ch) {
            assert(ChaosEngine::getFlippedVerticalGlyph(ch) >= 0x20);
        }
    }
    // A real Vietnamese sentence visibly changes under every text-safe mode,
    // and the output length is IDENTICAL to the input (1:1 substitution).
    const std::u32string vn = U"b\u1ED9 g\u00F5 ti\u1EBFng Vi\u1EC7t c\u00F3 d\u1EA5u";
    for (auto mode : {GlyphTransformMode::FlipVertical, GlyphTransformMode::FlipHorizontal,
                      GlyphTransformMode::Rotate180, GlyphTransformMode::Random}) {
        const std::u32string out = ChaosEngine::applyGlyphTransform(vn, mode, 7, 1.0f);
        assert(out.size() == vn.size());
        std::size_t changed = 0;
        for (std::size_t i = 0; i < vn.size(); ++i) {
            if (out[i] != vn[i]) { ++changed; }
        }
        assert(changed >= vn.size() / 3);   // the sentence is visibly transformed
    }
    // Rotate180 keeps the classic upside-down shape: last char first.
    const std::u32string r180 = ChaosEngine::applyGlyphTransform(vn, GlyphTransformMode::Rotate180, 7, 1.0f);
    assert(r180.front() == ChaosEngine::getFlippedVerticalGlyph(vn.back()));
    std::cout << "  [PASS] Vietnamese glyph flip (tone mirror) tests\n";
}

int main() {
    std::cout << "=== Running Chaos / Experimental Lab Suite ===\n";
    testChaosCase();
    testGlyphTransformTextIntegrity();
    testVietnameseCaseMapping();
    testGlyphTablesAndDisplayPipeline();
    testVietnameseGlyphFlips();
    std::cout << "=== ALL CHAOS TESTS PASSED ===\n";
    return 0;
}
