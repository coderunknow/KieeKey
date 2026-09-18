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
// Chaos / Experimental Lab: Random Capitalization (🌀 HOA/thường) and Glyph
// Visual Transforms (🔄 xoay/lật).
//
// DESIGN PRINCIPLES:
//   * STRICT ISOLATION: when disabled (the default), the output path costs one
//     relaxed atomic load.
//   * TEXT INTEGRITY: glyph transforms are a *presentation* concern. The text
//     model keeps the original Unicode so copy/paste, spell-check and editor
//     cursors are never corrupted.
//   * LOCALE INDEPENDENCE (v1.3.0 fix): case mapping used std::towupper /
//     std::towlower in the "C" locale, which does NOT know Vietnamese — every
//     accented letter (ế, ộ, ư, đ, ...) was silently left untouched, so
//     "Random HOA/thường" only ever worked on plain ASCII. The mapping below is
//     explicit (ASCII + Latin-1 + Latin Extended-A/B + Vietnamese block
//     U+1EA0..U+1EF9) and therefore identical on every host.
//   * QUARTER-TURN HONESTY (v1.3.0 fix): 90°/270° rotation cannot be expressed
//     with Unicode lookalikes. Those two modes are *render-only* — the text is
//     returned unchanged and the front-end rotates the drawing (see
//     `isRenderOnlyRotation()`); they are never silently applied to text that
//     is about to be injected into a document.
//----------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace ok::chaos {

enum class CaseGranularity : std::uint8_t {
    ByChar = 0,
    ByWord = 1,
};

enum class GlyphTransformMode : std::uint8_t {
    None = 0,
    Rotate90 = 1,        // render-only (see isRenderOnlyRotation)
    Rotate180 = 2,       // upside-down lookalikes (text-level)
    Rotate270 = 3,       // render-only
    FlipHorizontal = 4,  // text-level
    FlipVertical = 5,    // text-level
    Random = 6,          // per-character random pick of the text-level modes
};

struct ChaosConfig {
    bool masterEnabled = false;
    bool randomCaseEnabled = false;
    float randomCaseIntensity = 0.5f;   // 0.0f (no change) .. 1.0f (flip all)
    CaseGranularity caseGranularity = CaseGranularity::ByChar;

    bool glyphTransformEnabled = false;
    GlyphTransformMode glyphMode = GlyphTransformMode::None;
    float glyphIntensity = 1.0f;        // fraction of characters transformed
    bool rotateRenderOnly = true;       // 90/270 handled by the renderer

    void reset() noexcept {
        masterEnabled = false;
        randomCaseEnabled = false;
        randomCaseIntensity = 0.5f;
        caseGranularity = CaseGranularity::ByChar;
        glyphTransformEnabled = false;
        glyphMode = GlyphTransformMode::None;
        glyphIntensity = 1.0f;
        rotateRenderOnly = true;
    }
};

class ChaosEngine {
public:
    static ChaosEngine& instance() noexcept;

    // Config access. setConfig() publishes an immutable snapshot that the
    // output path reads with a single relaxed atomic load — no mutex, no data
    // race between the settings UI and the hook thread (the previous version
    // wrote a plain struct from the UI thread while the hook thread read it).
    void setConfig(const ChaosConfig& config) noexcept;
    [[nodiscard]] ChaosConfig getConfig() const noexcept;

    [[nodiscard]] bool isChaosActive() const noexcept {
        return m_active.load(std::memory_order_relaxed) != 0;
    }

    // Random case for the output path. Returns `text` unchanged when the mode
    // is off (or the master killswitch is engaged).
    std::u32string processCase(std::u32string_view text, uint32_t seed = 0) const;

    // Display-only glyph transform. Never mutates the caller's buffer.
    std::u32string getVisualDisplayString(std::u32string_view text, uint32_t seed = 0) const;

    // ---- pure helpers (unit-testable, no global state) ---------------------
    static std::u32string applyRandomCase(std::u32string_view text, float intensity,
                                          CaseGranularity gran, uint32_t seed = 0);
    static std::u32string applyGlyphTransform(std::u32string_view text, GlyphTransformMode mode,
                                              uint32_t seed = 0, float intensity = 1.0f);

    [[nodiscard]] static bool isRenderOnlyRotation(GlyphTransformMode mode) noexcept {
        return mode == GlyphTransformMode::Rotate90 || mode == GlyphTransformMode::Rotate270;
    }
    // True when the mode can be written into a real document.
    [[nodiscard]] static bool isTextSafe(GlyphTransformMode mode) noexcept {
        return mode != GlyphTransformMode::None && !isRenderOnlyRotation(mode);
    }

    // ---- locale-independent case mapping -----------------------------------
    [[nodiscard]] static bool isUpperVn(char32_t ch) noexcept;
    [[nodiscard]] static bool isLowerVn(char32_t ch) noexcept;
    [[nodiscard]] static char32_t toUpperVn(char32_t ch) noexcept;
    [[nodiscard]] static char32_t toLowerVn(char32_t ch) noexcept;
    [[nodiscard]] static char32_t toggleCaseVn(char32_t ch) noexcept;

    // ---- glyph lookalikes ---------------------------------------------------
    [[nodiscard]] static char32_t getFlippedVerticalGlyph(char32_t ch) noexcept;
    [[nodiscard]] static char32_t getFlippedHorizontalGlyph(char32_t ch) noexcept;

private:
    ChaosEngine();
    ~ChaosEngine() = default;
    ChaosEngine(const ChaosEngine&) = delete;
    ChaosEngine& operator=(const ChaosEngine&) = delete;

    // Immutable snapshot published to the output path. `alpha` is copied into
    // the byte-sized fields so one atomic load is enough.
    struct Snapshot {
        std::uint32_t magic = 0;
        std::uint8_t active = 0;
        std::uint8_t randomCase = 0;
        std::uint8_t granularity = 0;
        std::uint8_t glyphTransform = 0;
        std::uint8_t glyphMode = 0;
        std::uint8_t intensityPercent = 0;
        std::uint8_t glyphIntensityPercent = 0;
        std::uint8_t rotateRenderOnly = 1;
        std::uint8_t pad[3]{};
    };

    [[nodiscard]] static std::uint64_t pack(const Snapshot& s) noexcept;
    [[nodiscard]] static Snapshot unpack(std::uint64_t bits) noexcept;

    mutable std::mutex m_configMutex;      // guards m_configForUi only
    ChaosConfig m_configForUi{};
    std::atomic<std::uint64_t> m_snapshot{0};
    std::atomic<std::uint32_t> m_active{0};
};

} // namespace ok::chaos
