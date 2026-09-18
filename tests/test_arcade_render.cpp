//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/test_arcade_render.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
// Arcade render-pipeline suite.
//
// The graphical front-ends (Win32 GDI window and the HTML5 canvas client) are
// both dumb interpreters of `RenderList`, so everything that could go wrong in
// either of them is checked here, head-less:
//
//   1. UTF-8 <-> UTF-32       — Vietnamese text and emoji survive the trip
//   2. Draw-order contract    — rects → circles → lines → polys → texts
//   3. Wire-format integrity  — every game's frame serializes to valid JSON
//   4. Numerical hygiene      — no NaN/Inf/negative-size ever reaches a GPU
//   5. Payload budget         — a frame stays small enough for a 60 Hz stream
//   6. Catalog                — the wire catalog matches the C++ games
//============================================================================
#include "Arcade.hpp"
#include "ArcadeRender.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

using namespace ok::arcade;

namespace {

bool isFiniteList(const RenderList& list) {
    for (const RenderCommand& c : list.commands) {
        if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.w) ||
            !std::isfinite(c.h) || !std::isfinite(c.radius) || !std::isfinite(c.size) ||
            !std::isfinite(c.strokeWidth)) {
            return false;
        }
        for (std::uint8_t i = 0; i < c.pointCount; ++i) {
            if (!std::isfinite(c.xs[i]) || !std::isfinite(c.ys[i])) {
                return false;
            }
        }
    }
    return std::isfinite(list.worldW) && std::isfinite(list.worldH) && list.worldW > 0.0f &&
           list.worldH > 0.0f;
}

// Minimal structural JSON check: balanced braces/brackets, closed strings and
// no raw control characters. Enough to catch a broken serializer without
// pulling a JSON library into the test build.
bool looksLikeJson(const std::string& text) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            } else if (c < 0x20) {
                return false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            ++depth;
        } else if (c == '}' || c == ']') {
            --depth;
            if (depth < 0) {
                return false;
            }
        }
    }
    return depth == 0 && !inString;
}

} // namespace

//---------------------------------------------------------------------------
void testUtf8RoundTrip() {
    const std::u32string vietnamese = U"Tiếng Việt: ăâđêôơư ẠẶẪỆỐỢỤỨỲ";
    const std::string utf8 = utf8FromUtf32(vietnamese);
    assert(!utf8.empty());
    assert(utf8.size() > vietnamese.size());          // multi-byte, as expected
    assert(utf32FromUtf8(utf8) == vietnamese);        // lossless round trip

    // Emoji (4-byte sequences) and the menu glyphs used by the catalog.
    const std::u32string emoji = U"🐍🧱🎣🏎️🎵🌀🔄🗿🤖🌐";
    const std::string emojiUtf8 = utf8FromUtf32(emoji);
    assert(utf32FromUtf8(emojiUtf8) == emoji);

    // Invalid input must degrade to U+FFFD rather than throwing or emitting a
    // broken UTF-8 sequence.
    const std::u32string fromBroken = utf32FromUtf8("\xE2\x28\xA1");
    assert(!fromBroken.empty());
    const std::string sanitized = utf8FromUtf32(std::u32string(1, 0xD800));   // lone surrogate
    assert(sanitized == "?");
    std::cout << "  [PASS] UTF-8 round trip\n";
}

//---------------------------------------------------------------------------
void testDrawOrderAndShapeIntegrity() {
    ArcadeManager manager;
    for (const GameInfo& info : gameCatalog()) {
        assert(manager.launchGame(static_cast<GameType>(info.id), 12345u));
        for (int i = 0; i < 30; ++i) {
            manager.update(1.0 / 60.0);
        }
        const Frame& frame = manager.getFrame();
        RenderList list;
        buildRenderList(frame, list);

        assert(list.commands.size() == frame.rects.size() + frame.circles.size() +
                                           frame.lines.size() + frame.polys.size() +
                                           frame.texts.size());
        assert(list.commands.size() <= RenderList::kMaxCommands);
        assert(isFiniteList(list));

        // Draw order contract (the front-ends rely on it for layering).
        int previous = -1;
        for (const RenderCommand& c : list.commands) {
            const int kind = static_cast<int>(c.kind);
            assert(kind >= previous);
            previous = kind;
            if (c.kind == RenderCommand::Kind::Rect) {
                assert(c.w >= 0.0f && c.h >= 0.0f);      // never a negative size
            }
            if (c.kind == RenderCommand::Kind::Circle) {
                assert(c.w >= 0.0f);
                assert(c.h == 0.0f);
            }
            if (c.kind == RenderCommand::Kind::Poly) {
                assert(c.pointCount >= 2);
                assert(c.pointCount <= PolyShape::kMaxPoints);
            }
            if (c.kind == RenderCommand::Kind::Text) {
                assert(!c.text.empty());
                assert(c.size > 0.0f);
            }
        }

        // The stats block is always present and self-consistent.
        assert(!list.title.empty());
        assert(list.stats.accuracy >= 0.0 && list.stats.accuracy <= 100.0);
        assert(list.stats.level >= 1);
        manager.stopGame();
    }
    std::cout << "  [PASS] Draw order & shape integrity\n";
}

//---------------------------------------------------------------------------
void testJsonSerialization() {
    ArcadeManager manager;
    assert(manager.launchGame(GameType::TypingRace, 4242u));
    for (int i = 0; i < 120; ++i) {
        manager.update(1.0 / 60.0);
        if ((i % 5) == 0) {
            manager.handleKey(0, U'a' + static_cast<char32_t>(i % 26), true);
        }
    }

    const Frame& frame = manager.getFrame();
    RenderList list;
    buildRenderList(frame, list);
    const std::string json = renderListToJson(list);

    assert(looksLikeJson(json));
    assert(json.rfind("{\"w\":", 0) == 0);
    assert(json.find("\"stats\":{") != std::string::npos);
    assert(json.find("\"cmds\":[") != std::string::npos);
    assert(json.size() < 512u * 1024u);            // bounded per-frame payload
    // The frame's own text must be present, escaped, never raw.
    assert(json.find("\"title\":\"") != std::string::npos);

    // Quotes and newlines in a game-provided string must be escaped.
    Frame custom;
    custom.clear();
    custom.addText(10, 10, 16, palette::kText, TextAlign::Left, U"quote \" and \\ and \n");
    RenderList customList;
    buildRenderList(custom, customList);
    const std::string customJson = renderListToJson(customList);
    assert(looksLikeJson(customJson));
    assert(customJson.find("\\\"") != std::string::npos);
    assert(customJson.find("\\\\") != std::string::npos);
    assert(customJson.find("\\n") != std::string::npos);

    // A non-finite coordinate from a hypothetical broken game must be clamped,
    // not forwarded as "nan" (which would produce invalid JSON).
    Frame broken;
    broken.clear();
    broken.addRect(std::nanf(""), 0.0f, 10.0f, 10.0f, palette::kAccent);
    broken.addRect(0.0f, 0.0f, std::numeric_limits<float>::infinity(), 5.0f, palette::kAccent);
    RenderList brokenList;
    buildRenderList(broken, brokenList);
    const std::string brokenJson = renderListToJson(brokenList);
    assert(looksLikeJson(brokenJson));
    assert(brokenJson.find("nan") == std::string::npos);
    assert(brokenJson.find("inf") == std::string::npos);
    assert(isFiniteList(brokenList));

    // Text dump (ASCII fallback) carries the title and the live texts.
    const std::string text = renderListToText(list);
    assert(text.find("frame ") == 0);
    assert(text.find("cmds=") != std::string::npos);
    std::cout << "  [PASS] JSON serialization & injection safety\n";
}

//---------------------------------------------------------------------------
void testPayloadBudget() {
    ArcadeManager manager;
    std::size_t worst = 0;
    std::string worstGame;
    for (const GameInfo& info : gameCatalog()) {
        assert(manager.launchGame(static_cast<GameType>(info.id), 7u));
        for (int i = 0; i < 240; ++i) {
            manager.update(1.0 / 60.0);
            if ((i % 7) == 0) {
                const char32_t ch = U'a' + static_cast<char32_t>(i % 26);
                manager.handleKey(0, ch, true);
                manager.handleKey(0x27, 0, true);
            }
            if ((i % 11) == 0) {
                manager.handleKey(0, U' ', true);
            }
            if ((i % 13) == 0) {
                RenderList list;
                buildRenderList(manager.getFrame(), list);
                const std::string json = renderListToJson(list);
                if (json.size() > worst) {
                    worst = json.size();
                    worstGame = info.slug != nullptr ? info.slug : "?";
                }
            }
        }
        manager.stopGame();
    }
    // At 60 fps a 128 KB frame would be ~7.5 MB/s, which is fine on loopback
    // but wasteful; the games are designed to stay an order of magnitude below.
    assert(worst > 0);
    assert(worst < 128u * 1024u);
    std::cout << "  [PASS] Payload budget (worst " << worst << " bytes, " << worstGame << ")\n";
}

//---------------------------------------------------------------------------
void testCatalogWireFormat() {
    const std::string json = gameCatalogToJson();
    assert(looksLikeJson(json));
    const auto& catalog = gameCatalog();
    assert(catalog.size() == 8);
    for (const GameInfo& info : catalog) {
        assert(info.slug != nullptr && info.slug[0] != '\0');
        assert(info.nameVi != nullptr && info.emoji != nullptr);
        assert(json.find(std::string("\"slug\":\"") + info.slug + "\"") != std::string::npos);
        // slug -> id -> slug must be a closed loop (the wire ids are stable).
        assert(gameTypeFromSlug(info.slug) == info.id);
        assert(gameSlug(info.id) == info.slug);
        assert(gameInfo(info.id) != nullptr);
        assert(gameInfo(info.id)->id == info.id);
    }
    // Unknown slugs must be rejected, never silently mapped to a game.
    assert(gameTypeFromSlug("") == 0);
    assert(gameTypeFromSlug("flappy-bird") == 0);
    assert(gameTypeFromSlug("../../etc/passwd") == 0);
    std::cout << "  [PASS] Catalog wire format\n";
}

//---------------------------------------------------------------------------
void testViewportLetterboxing() {
    // Square device, 16:9 world: the content is centred with top/bottom bars.
    const Viewport square = computeViewport(1600.0f, 900.0f, 800.0f, 800.0f);
    assert(std::fabs(square.scale - 0.5f) < 1e-4f);
    assert(std::fabs(square.contentW - 800.0f) < 1e-3f);
    assert(std::fabs(square.contentH - 450.0f) < 1e-3f);
    assert(std::fabs(square.offsetY - 175.0f) < 1e-3f);
    assert(std::fabs(square.offsetX) < 1e-3f);

    // The world centre always maps to the device centre.
    assert(std::fabs(square.toDeviceX(800.0f) - 400.0f) < 1e-3f);
    assert(std::fabs(square.toDeviceY(450.0f) - 400.0f) < 1e-3f);

    // Exact aspect match: no letterboxing at all.
    const Viewport exact = computeViewport(1000.0f, 620.0f, 2000.0f, 1240.0f);
    assert(std::fabs(exact.scale - 2.0f) < 1e-4f);
    assert(std::fabs(exact.offsetX) < 1e-3f && std::fabs(exact.offsetY) < 1e-3f);

    // Degenerate inputs never produce a NaN/zero-scale viewport.
    for (const auto& pair : {std::pair<float, float>{0.0f, 0.0f},
                             std::pair<float, float>{std::nanf(""), 100.0f},
                             std::pair<float, float>{-100.0f, -100.0f}}) {
        const Viewport view = computeViewport(pair.first, pair.second, 640.0f, 480.0f);
        assert(std::isfinite(view.scale) && view.scale > 0.0f);
        assert(std::isfinite(view.offsetX) && std::isfinite(view.offsetY));
    }

    // Widescreen device, 4:3 world: side bars, content height fills the device.
    const Viewport wide = computeViewport(800.0f, 600.0f, 1600.0f, 600.0f);
    assert(std::fabs(wide.scale - 1.0f) < 1e-4f);
    assert(std::fabs(wide.offsetX - 400.0f) < 1e-3f);
    std::cout << "  [PASS] Viewport letterboxing tests\n";
}

int main() {
    std::cout << "=== Running Arcade Render Pipeline Suite ===\n";
    testUtf8RoundTrip();
    testDrawOrderAndShapeIntegrity();
    testJsonSerialization();
    testPayloadBudget();
    testViewportLetterboxing();
    testCatalogWireFormat();
    std::cout << "=== ALL ARCADE RENDER TESTS PASSED ===\n";
    return 0;
}
