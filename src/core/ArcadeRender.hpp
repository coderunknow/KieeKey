//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: src/core/ArcadeRender.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — ArcadeRender.hpp
// The single, device-independent display list shared by every graphical
// front-end of the Arcade Hub:
//
//   Frame (game logic)  ──buildRenderList()──►  RenderList
//                                                 ├─► Win32 GDI   (ArcadeWindow)
//                                                 └─► HTML5 canvas (ArcadeServer)
//
// Why an extra layer: a game must not know whether it is drawn by GDI, by a
// canvas, or by the ASCII fallback. `Frame` is already structured, but it
// groups shapes by type and keeps text in a frame-owned arena, which is not
// something a renderer in another language can consume. `RenderList` fixes
// both: one flat, ordered command stream with UTF-8 text and plain floats, so
// the C++ window and the web client can never disagree about what a frame
// looks like.
//----------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ArcadeFrame.hpp"

namespace ok::arcade {

//---------------------------------------------------------------------------
// Draw order (documented contract, asserted by tests/test_arcade_render.cpp):
//   background gradient → rects → circles → lines → polys → texts.
//---------------------------------------------------------------------------
struct RenderCommand {
    enum class Kind : std::uint8_t { Rect = 0, Circle = 1, Line = 2, Poly = 3, Text = 4 };

    Kind kind = Kind::Rect;
    // Rect   : x, y, w, h (+ radius)
    // Circle : x = cx, y = cy, w = r
    // Line   : x, y = start, w, h = end, radius unused, strokeWidth = width
    // Poly   : xs/ys[pointCount]
    // Text   : x, y = baseline-left, `text` is UTF-8
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float radius = 0.0f;
    Color fill = 0;
    Color stroke = 0;
    float strokeWidth = 0.0f;
    std::uint8_t pointCount = 0;
    float xs[PolyShape::kMaxPoints]{};
    float ys[PolyShape::kMaxPoints]{};
    float size = 16.0f;
    TextAlign align = TextAlign::Left;
    bool bold = false;
    bool mono = false;
    float advance = 0;
    std::string text;   // UTF-8, empty for non-text commands

    // Resets every field for reuse. Deliberately KEEPS the text buffer's
    // capacity: a frame is rebuilt 60 times a second and the labels rarely
    // change, so reusing the buffer is what keeps the steady-state path
    // allocation-free (see tools/arcade_bench.cpp).
    void reset() noexcept {
        kind = Kind::Rect;
        x = y = w = h = radius = 0.0f;
        fill = stroke = 0;
        strokeWidth = 0.0f;
        pointCount = 0;
        size = 16.0f;
        align = TextAlign::Left;
        bold = false;
        mono = false;
        advance = 0;
        text.clear();
    }
};

struct RenderList {
    static constexpr std::size_t kMaxCommands = Frame::kMaxRects + Frame::kMaxCircles +
                                                Frame::kMaxLines + Frame::kMaxPolys +
                                                Frame::kMaxTexts;

    float worldW = 1280.0f;
    float worldH = 720.0f;
    Color background = 0;
    Color backgroundTop = 0;
    std::vector<RenderCommand> commands;

    // Stats, with every text field already converted to UTF-8 so the list is
    // self-contained and safe to hand to another thread/process.
    GameStats stats;
    std::string title;
    std::string status;
    std::string banner;
    std::string hint;

    // Internal scratch buffer reused by buildRenderList() (UTF-8 conversion);
    // never serialized, never part of the wire contract.
    std::string scratch;

    void clear() noexcept;
};

//---------------------------------------------------------------------------
// Viewport: world units -> device pixels, aspect-preserving with letterboxing.
// Shared by the GDI window and the canvas client so a game is laid out exactly
// the same on both (a 16:9 game in a 4:3 window is centered, never stretched).
//---------------------------------------------------------------------------
struct Viewport {
    float scale = 1.0f;      // world unit -> device pixel
    float offsetX = 0.0f;    // letterbox offset in device pixels
    float offsetY = 0.0f;
    float deviceW = 0.0f;
    float deviceH = 0.0f;
    float contentW = 0.0f;   // scaled world size inside the device area
    float contentH = 0.0f;

    [[nodiscard]] float toDeviceX(float worldX) const noexcept {
        return offsetX + worldX * scale;
    }
    [[nodiscard]] float toDeviceY(float worldY) const noexcept {
        return offsetY + worldY * scale;
    }
};

// Never returns a degenerate viewport (zero/NaN device sizes fall back to 1:1).
[[nodiscard]] Viewport computeViewport(float worldW, float worldH, float deviceW,
                                       float deviceH) noexcept;

// Fills `out` from `frame`. Deterministic, allocation-bounded (reserves the
// worst case once) and safe on an empty/invalid frame.
void buildRenderList(const Frame& frame, RenderList& out);

// UTF-8 helpers shared with the front-ends.
[[nodiscard]] std::string utf8FromUtf32(std::u32string_view text);

// Same conversion into a caller-owned buffer (cleared first, capacity kept) —
// the allocation-free form used by the frame builder.
void utf8FromUtf32(std::u32string_view text, std::string& out);
[[nodiscard]] std::u32string utf32FromUtf8(std::string_view text);

// Serializes to the wire format consumed by the HTML5 canvas client.
//   {"w":1280,"h":720,"bg":"#RRGGBBAA","bg2":"…",
//    "stats":{…},"title":"…","status":"…","banner":"…","hint":"…",
//    "cmds":[[0,…],…]}
std::string renderListToJson(const RenderList& list);

// Same serialization into a caller-owned buffer (cleared first, capacity kept).
// A 60 Hz caller keeps one buffer alive instead of allocating a fresh string
// per frame — this is what tools/arcade_bench.cpp measures as "json per frame".
void renderListToJson(const RenderList& list, std::string& out);

// Catalog of the games (id, slug, title, emoji, control hint) as JSON.
std::string gameCatalogToJson();

// Human-readable dump used by the ASCII fallback and by the tests.
[[nodiscard]] std::string renderListToText(const RenderList& list);

} // namespace ok::arcade
