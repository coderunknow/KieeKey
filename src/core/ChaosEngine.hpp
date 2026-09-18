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
// File: src/core/ChaosEngine.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — ChaosEngine.hpp
// Chaos / Experimental Lab: Random Capitalization & Glyph Visual Transforms.
//
// DESIGN PRINCIPLES:
//   * STRICT ISOLATION: When disabled (the default), overhead on the input
//     path is zero (a single inline boolean check).
//   * TEXT INTEGRITY: Visual glyph transformations (flip/rotate) are applied
//     at the presentation/rendering layer. The underlying text remains
//     100% original Unicode so copy/paste, spelling checks, and editor
//     cursors are never corrupted.
//   * EXPLICIT OPT-IN: Features must be explicitly turned on with clear warnings.
//     A master killswitch instantly disables all chaos effects.
//----------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace ok::chaos {

enum class CaseGranularity : std::uint8_t {
    ByChar = 0,
    ByWord = 1,
};

enum class GlyphTransformMode : std::uint8_t {
    None = 0,
    Rotate90 = 1,
    Rotate180 = 2,
    Rotate270 = 3,
    FlipHorizontal = 4,
    FlipVertical = 5,
    Random = 6,
};

struct ChaosConfig {
    bool masterEnabled = false;
    bool randomCaseEnabled = false;
    float randomCaseIntensity = 0.5f;   // 0.0f (no change) to 1.0f (flip all)
    CaseGranularity caseGranularity = CaseGranularity::ByChar;

    bool glyphTransformEnabled = false;
    GlyphTransformMode glyphMode = GlyphTransformMode::None;

    void reset() noexcept {
        masterEnabled = false;
        randomCaseEnabled = false;
        randomCaseIntensity = 0.5f;
        caseGranularity = CaseGranularity::ByChar;
        glyphTransformEnabled = false;
        glyphMode = GlyphTransformMode::None;
    }
};

class ChaosEngine {
public:
    static ChaosEngine& instance() noexcept;

    void setConfig(const ChaosConfig& config) noexcept;
    ChaosConfig getConfig() const noexcept;

    [[nodiscard]] bool isChaosActive() const noexcept {
        return m_active.load(std::memory_order_relaxed);
    }

    // Transform text casing for Chaos Case mode.
    // When disabled, returns text unchanged.
    std::u32string processCase(std::u32string_view text, uint32_t seed = 0) const;

    // Visual glyph transformation for display/rendering layer.
    // The underlying text buffer is preserved.
    std::u32string getVisualDisplayString(std::u32string_view text, uint32_t seed = 0) const;

    // Low-level pure helper functions (usable in unit tests or custom pipelines)
    static std::u32string applyRandomCase(
        std::u32string_view text,
        float intensity,
        CaseGranularity gran,
        uint32_t seed = 0);

    static std::u32string applyGlyphTransform(
        std::u32string_view text,
        GlyphTransformMode mode,
        uint32_t seed = 0);

    static char32_t getFlippedVerticalGlyph(char32_t ch) noexcept;
    static char32_t getFlippedHorizontalGlyph(char32_t ch) noexcept;

private:
    ChaosEngine();
    ~ChaosEngine() = default;

    ChaosConfig m_config{};
    std::atomic<bool> m_active{false};
};

} // namespace ok::chaos
