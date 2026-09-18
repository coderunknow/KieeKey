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
// File: src/core/ArcadeFrame.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — ArcadeFrame.hpp
// Portable, OS-independent render model for the Arcade Hub.
//
// WHY THIS EXISTS (v1.3.0 GUI fix):
//   Before this header the games could only describe themselves as monospace
//   ASCII (`renderText()`), so the "UI" was a read-only text box — no real
//   graphics. The games are now split into a *model* (rules) and a *view*
//   (this frame): every game fills a `Frame` with primitive shapes
//   (rects / circles / lines / polygons / text runs) plus a structured
//   `GameStats` block, and each front-end draws those primitives with real
//   graphics:
//     * Win32 GDI front-end (src/app/ArcadeWindow.*) — the shipped UI.
//     * HTML5 canvas front-end (web/) — the same frame over the JSON wire
//       protocol (src/core/ArcadeServer.*).
//   One frame model, two back-ends, therefore no "looks different on
//   Windows vs. browser" divergence, and every drawable property is unit
//   testable without a window.
//
// HARD REAL-TIME CONTRACT (the hot path is the typing path):
//   * A `Frame` is filled with ZERO heap allocations in steady state: shape
//     vectors and the string arena are reserved once by the caller and only
//     `clear()`-ed between frames.
//   * All shape counts are hard-capped. Overflow is *counted*
//     (`droppedShapes`) instead of growing without bound, so a bug in a game
//     renderer can never turn into an OOM inside the hook thread.
//   * Text runs hold `std::u32string_view` into either a string literal or
//     the frame's own arena (`intern()`), never into a temporary.
//----------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ok::arcade {

//===========================================================================
// Colors — packed 0xRRGGBBAA (matches CSS #RRGGBBAA order after the '#')
//===========================================================================
using Color = std::uint32_t;

[[nodiscard]] constexpr Color rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                   std::uint8_t a = 0xFF) noexcept {
    return (static_cast<Color>(r) << 24) | (static_cast<Color>(g) << 16) |
           (static_cast<Color>(b) << 8) | static_cast<Color>(a);
}
[[nodiscard]] constexpr std::uint8_t colorR(Color c) noexcept { return static_cast<std::uint8_t>(c >> 24); }
[[nodiscard]] constexpr std::uint8_t colorG(Color c) noexcept { return static_cast<std::uint8_t>(c >> 16); }
[[nodiscard]] constexpr std::uint8_t colorB(Color c) noexcept { return static_cast<std::uint8_t>(c >> 8); }
[[nodiscard]] constexpr std::uint8_t colorA(Color c) noexcept { return static_cast<std::uint8_t>(c); }

// Shared palette (also mirrored by both front-ends).
namespace palette {
inline constexpr Color kBackground = rgba(0x10, 0x14, 0x1C);
inline constexpr Color kPanel = rgba(0x1B, 0x21, 0x2E);
inline constexpr Color kPanelAlt = rgba(0x24, 0x2C, 0x3C);
inline constexpr Color kGrid = rgba(0x2C, 0x35, 0x46);
inline constexpr Color kAccent = rgba(0x4C, 0xC2, 0xFF);
inline constexpr Color kAccent2 = rgba(0xFF, 0x8A, 0x3D);
inline constexpr Color kGood = rgba(0x51, 0xE8, 0x8A);
inline constexpr Color kBad = rgba(0xFF, 0x5A, 0x5A);
inline constexpr Color kWarn = rgba(0xFF, 0xD1, 0x4A);
inline constexpr Color kText = rgba(0xEC, 0xF1, 0xF8);
inline constexpr Color kTextDim = rgba(0x9A, 0xA7, 0xB8);
inline constexpr Color kSnakeHead = rgba(0x4C, 0xFF, 0xB0);
inline constexpr Color kSnakeBody = rgba(0x2A, 0xC0, 0x7A);
inline constexpr Color kFood = rgba(0xFF, 0x5F, 0x8F);
} // namespace palette

enum class TextAlign : std::uint8_t { Left = 0, Center = 1, Right = 2 };

struct RectShape {
    float x = 0, y = 0, w = 0, h = 0;
    float radius = 0;
    Color fill = 0;
    Color stroke = 0;         // alpha 0 == no stroke
    float strokeWidth = 0;
};

struct CircleShape {
    float cx = 0, cy = 0, r = 0;
    Color fill = 0;
    Color stroke = 0;
    float strokeWidth = 0;
};

struct LineShape {
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    float width = 1;
    Color color = 0;
};

struct PolyShape {
    // Bounded point list: games never need more than a handful of vertices
    // (fish tail, car bonnet, arrow heads, ...).
    static constexpr size_t kMaxPoints = 8;
    std::uint8_t count = 0;
    float xs[kMaxPoints]{};
    float ys[kMaxPoints]{};
    Color fill = 0;
    Color stroke = 0;
    float strokeWidth = 0;
};

struct TextShape {
    float x = 0, y = 0;
    float size = 16;         // px in world units
    Color color = 0;
    TextAlign align = TextAlign::Left;
    bool bold = false;
    bool mono = false;
    std::u32string_view text{};
};

//===========================================================================
// GameStats — the part of the frame every front-end can render generically
// (top score strip + status line + centered banner).
//===========================================================================
struct GameStats {
    std::int64_t score = 0;
    std::int64_t highScore = 0;
    std::uint32_t level = 1;
    std::uint32_t combo = 0;
    std::uint32_t maxCombo = 0;
    std::uint32_t lives = 0;
    double wpm = 0.0;
    double accuracy = 100.0;
    double progress = 0.0;    // 0.0 .. 1.0
    double meter = 0.0;       // 0.0 .. 1.0 generic secondary meter (fuel/HP/pull)
    double meterMax = 1.0;
    bool hasMeter = false;
    bool paused = false;
    bool gameOver = false;
    bool finished = false;
    bool hasHighScore = true;

    std::u32string_view title{};    // game name
    std::u32string_view status{};   // one-line live status
    std::u32string_view banner{};   // big centered message (may be empty)
    std::u32string_view hint{};     // control hint
};

//===========================================================================
// Frame — the drawable frame handed to a front-end.
//===========================================================================
class Frame {
public:
    // Hard caps: keep a rendering bug from becoming an allocation storm.
    static constexpr size_t kMaxRects = 1024;
    static constexpr size_t kMaxCircles = 512;
    static constexpr size_t kMaxLines = 256;
    static constexpr size_t kMaxPolys = 128;
    static constexpr size_t kMaxTexts = 512;
    static constexpr size_t kArenaReserve = 8192;

    Frame();

    float worldW = 1280.0f;
    float worldH = 720.0f;
    Color background = palette::kBackground;
    Color backgroundTop = palette::kBackground;   // equal to `background` == flat fill

    std::vector<RectShape> rects;
    std::vector<CircleShape> circles;
    std::vector<LineShape> lines;
    std::vector<PolyShape> polys;
    std::vector<TextShape> texts;
    GameStats stats;

    // Diagnostics: shapes dropped because a cap was hit (must stay 0).
    std::uint32_t droppedShapes = 0;

    void clear() noexcept;

    //---- primitive helpers: return false (and bump droppedShapes) when full
    bool addRect(const RectShape& r);
    bool addRect(float x, float y, float w, float h, Color fill, float radius = 0);
    bool addCircle(const CircleShape& c);
    bool addCircle(float cx, float cy, float r, Color fill);
    bool addLine(float x1, float y1, float x2, float y2, Color color, float width = 1);
    bool addPoly(const PolyShape& p);
    bool addText(float x, float y, float size, Color color, TextAlign align,
                 std::u32string_view text, bool bold = false, bool mono = false);

    //---- text helpers -------------------------------------------------------
    // "Intern" a string into the frame-owned arena and return a view that
    // stays valid until the next clear(). Returns an empty view when the
    // arena is full (and counts it as a dropped shape).
    std::u32string_view intern(std::u32string_view s);

    // Compose "<label> <number>" style HUD strings without allocating.
    std::u32string_view internNumber(std::u32string_view prefix, std::int64_t value,
                                     std::u32string_view suffix = {});
    // Decode a UTF-8 literal into the arena (used for status/banner strings that
    // are built at runtime) — no per-frame allocation.
    std::u32string_view internAscii(std::string_view utf8,
                                    std::u32string_view prefix = {},
                                    std::u32string_view suffix = {});
    std::u32string_view internDouble(std::u32string_view prefix, double value, int decimals,
                                     std::u32string_view suffix = {});

private:
    std::u32string m_arena;
    size_t m_arenaUsed = 0;
};

//===========================================================================
// Fail modes shared by the hardcore typing games (Rhythm, No-Mistake)
//===========================================================================
enum class FailMode : std::uint8_t {
    Hardcore = 0,   // a single bad input ends the run immediately (default)
    HealthBar = 1,  // bad inputs drain a bar; the run ends when it is empty
};

//===========================================================================
// Game catalog metadata (single source of truth for both front-ends)
//===========================================================================
struct GameInfo {
    std::uint8_t id;              // ok::arcade::GameType value
    const char* slug;             // stable wire id  ("snake", "tetris", ...)
    const char* nameVi;           // Vietnamese display name
    const char* nameEn;           // English display name
    const char* emoji;            // menu glyph (UTF-8)
    const char* descriptionVi;    // short Vietnamese description
    const char* controlsVi;       // controls summary
    bool typingDriven;            // true when the core loop is typing
    bool canInjectText;           // true when the game legitimately emits text
};

[[nodiscard]] std::string_view gameSlug(int gameTypeId) noexcept;
[[nodiscard]] const GameInfo* gameInfo(int gameTypeId) noexcept;
[[nodiscard]] const std::vector<GameInfo>& gameCatalog() noexcept;
[[nodiscard]] int gameTypeFromSlug(std::string_view slug) noexcept;

} // namespace ok::arcade
