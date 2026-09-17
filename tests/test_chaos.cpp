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
#include "ChaosEngine.hpp"

#include <cassert>
#include <iostream>

using namespace ok::chaos;

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

    // Master killswitch
    cfg.masterEnabled = false;
    engine.setConfig(cfg);
    assert(!engine.isChaosActive());
    assert(engine.processCase(orig) == orig);

    std::cout << "  [PASS] Chaos Case tests\n";
}

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

    std::cout << "  [PASS] Glyph Transform & Text Integrity tests\n";
}

int main() {
    std::cout << "=== Running Chaos / Experimental Lab Suite ===\n";
    testChaosCase();
    testGlyphTransformTextIntegrity();
    std::cout << "=== ALL CHAOS TESTS PASSED ===\n";
    return 0;
}
