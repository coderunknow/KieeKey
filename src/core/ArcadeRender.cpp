//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: src/core/ArcadeRender.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "ArcadeRender.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ok::arcade {

namespace {

constexpr int kCoordDecimals = 2;
constexpr int kSizeDecimals = 2;

void appendFloat(std::string& out, double value, int decimals) {
    if (!std::isfinite(value)) {
        value = 0.0;   // a broken game must never emit NaN/Inf on the wire
    }
    // Round first so "-0.00" collapses to "0" and 12.999999 prints as 13.
    const double scale = std::pow(10.0, static_cast<double>(decimals));
    const double rounded = std::round(value * scale) / scale;
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, rounded);
    // Trim trailing zeros: keeps the payload small (this runs every frame).
    std::size_t length = std::strlen(buffer);
    if (decimals > 0) {
        while (length > 0 && buffer[length - 1] == '0') {
            --length;
        }
        if (length > 0 && buffer[length - 1] == '.') {
            --length;
        }
    }
    if (length == 0 || (length == 1 && buffer[0] == '-')) {
        out += '0';
        return;
    }
    out.append(buffer, length);
}

void appendColor(std::string& out, Color color) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "\"#%02X%02X%02X%02X\"", colorR(color), colorG(color),
                  colorB(color), colorA(color));
    out += buffer;
}

void appendJsonString(std::string& out, std::string_view text) {
    out += '"';
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04X", c);
                    out += buffer;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void appendBool(std::string& out, bool value) { out += value ? "true" : "false"; }

} // namespace

//===========================================================================
// UTF-8 <-> UTF-32
//===========================================================================
void utf8FromUtf32(std::u32string_view text, std::string& out) {
    out.clear();   // keeps the capacity: no allocation once the buffer grew
    for (char32_t ch : text) {
        const std::uint32_t cp = static_cast<std::uint32_t>(ch);
        if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
            out += '?';   // never emit an invalid UTF-8 sequence
            continue;
        }
        if (cp < 0x80u) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800u) {
            out += static_cast<char>(0xC0u | (cp >> 6));
            out += static_cast<char>(0x80u | (cp & 0x3Fu));
        } else if (cp < 0x10000u) {
            out += static_cast<char>(0xE0u | (cp >> 12));
            out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
            out += static_cast<char>(0x80u | (cp & 0x3Fu));
        } else {
            out += static_cast<char>(0xF0u | (cp >> 18));
            out += static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
            out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
            out += static_cast<char>(0x80u | (cp & 0x3Fu));
        }
    }
}

std::string utf8FromUtf32(std::u32string_view text) {
    std::string out;
    out.reserve(text.size() * 2);
    utf8FromUtf32(text, out);
    return out;
}

std::u32string utf32FromUtf8(std::string_view text) {
    std::u32string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char b0 = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = 0;
        std::size_t extra = 0;
        if (b0 < 0x80u) {
            cp = b0;
        } else if ((b0 & 0xE0u) == 0xC0u) {
            cp = b0 & 0x1Fu;
            extra = 1;
        } else if ((b0 & 0xF0u) == 0xE0u) {
            cp = b0 & 0x0Fu;
            extra = 2;
        } else if ((b0 & 0xF8u) == 0xF0u) {
            cp = b0 & 0x07u;
            extra = 3;
        } else {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        if (i + extra >= text.size()) {
            out.push_back(0xFFFD);
            break;
        }
        bool valid = true;
        for (std::size_t k = 1; k <= extra; ++k) {
            const unsigned char bk = static_cast<unsigned char>(text[i + k]);
            if ((bk & 0xC0u) != 0x80u) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (bk & 0x3Fu);
        }
        if (!valid) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        out.push_back(static_cast<char32_t>(cp));
        i += extra + 1;
    }
    return out;
}

//===========================================================================
// Viewport
//===========================================================================
Viewport computeViewport(float worldW, float worldH, float deviceW, float deviceH) noexcept {
    Viewport view;
    view.deviceW = std::isfinite(deviceW) && deviceW > 0.0f ? deviceW : 1.0f;
    view.deviceH = std::isfinite(deviceH) && deviceH > 0.0f ? deviceH : 1.0f;
    const float safeWorldW = std::isfinite(worldW) && worldW > 0.0f ? worldW : 1280.0f;
    const float safeWorldH = std::isfinite(worldH) && worldH > 0.0f ? worldH : 720.0f;
    const float scaleX = view.deviceW / safeWorldW;
    const float scaleY = view.deviceH / safeWorldH;
    view.scale = (scaleX < scaleY) ? scaleX : scaleY;
    view.contentW = safeWorldW * view.scale;
    view.contentH = safeWorldH * view.scale;
    view.offsetX = (view.deviceW - view.contentW) * 0.5f;
    view.offsetY = (view.deviceH - view.contentH) * 0.5f;
    return view;
}

//===========================================================================
// Frame -> RenderList
//===========================================================================
void RenderList::clear() noexcept {
    worldW = 1280.0f;
    worldH = 720.0f;
    background = 0;
    backgroundTop = 0;
    commands.clear();
    stats = GameStats{};
    title.clear();
    status.clear();
    banner.clear();
    hint.clear();
}

namespace {

// Front-ends receive plain floats: a NaN would silently poison a canvas or a
// GDI path, so every coordinate is sanitized at the boundary of the pipeline.
[[nodiscard]] inline float finiteOrZero(float value) noexcept {
    return std::isfinite(value) ? value : 0.0f;
}

[[nodiscard]] inline float nonNegative(float value) noexcept {
    const float v = finiteOrZero(value);
    return v < 0.0f ? 0.0f : v;
}

} // namespace


namespace {

// Copies `value` into `target` only when the content actually differs. Assigned
// unconditionally, every frame allocated a fresh heap buffer for the long
// Vietnamese title/status/banner/hint strings (measured: ~4 allocations per
// frame, ~240 per second at 60 FPS for a text-heavy game). The text almost
// never changes between frames, so a size+memcmp guard removes the churn while
// keeping the rendered result bit-identical.
void assignIfChanged(std::string& target, const std::string& value) {
    if (target.size() == value.size() &&
        (value.empty() || std::memcmp(target.data(), value.data(), value.size()) == 0)) {
        return;
    }
    target.assign(value);
}

} // namespace

void buildRenderList(const Frame& frame, RenderList& out) {
    out.worldW = (std::isfinite(frame.worldW) && frame.worldW > 0.0f) ? frame.worldW : 1280.0f;
    out.worldH = (std::isfinite(frame.worldH) && frame.worldH > 0.0f) ? frame.worldH : 720.0f;
    out.background = frame.background;
    out.backgroundTop = frame.backgroundTop;
    out.stats = frame.stats;
    utf8FromUtf32(frame.stats.title, out.scratch);
    assignIfChanged(out.title, out.scratch);
    utf8FromUtf32(frame.stats.status, out.scratch);
    assignIfChanged(out.status, out.scratch);
    utf8FromUtf32(frame.stats.banner, out.scratch);
    assignIfChanged(out.banner, out.scratch);
    utf8FromUtf32(frame.stats.hint, out.scratch);
    assignIfChanged(out.hint, out.scratch);

    const std::size_t commandCount = frame.rects.size() + frame.circles.size() +
                                     frame.lines.size() + frame.polys.size() + frame.texts.size();
    if (out.commands.capacity() < commandCount) {
        // Geometric reserve: a game whose frame grows by one command (new
        // obstacles, a longer label) must not reallocate on every growth step.
        out.commands.reserve(std::max<std::size_t>(commandCount * 2, commandCount + 16));
    }
    if (out.commands.size() < commandCount) {
        out.commands.resize(commandCount);
    }

    // Commands are written by INDEX into the (reused) vector instead of being
    // cleared and re-appended: a std::vector::clear() destroys each command's
    // text buffer, so the long labels ("Câu cá quý hiếm", the race passage…)
    // were re-allocated every frame. Reuse keeps them — measured with
    // tools/arcade_bench.cpp: 0 allocations per frame in steady state.
    std::size_t index = 0;
    const auto nextCommand = [&]() -> RenderCommand& {
        RenderCommand& slot = out.commands[index++];
        slot.reset();
        return slot;
    };

    for (const RectShape& r : frame.rects) {
        RenderCommand& c = nextCommand();
        c.kind = RenderCommand::Kind::Rect;
        c.x = finiteOrZero(r.x); c.y = finiteOrZero(r.y);
        c.w = nonNegative(r.w); c.h = nonNegative(r.h);
        c.radius = nonNegative(r.radius);
        c.fill = r.fill; c.stroke = r.stroke; c.strokeWidth = nonNegative(r.strokeWidth);
    }
    for (const CircleShape& s : frame.circles) {
        RenderCommand& c = nextCommand();
        c.kind = RenderCommand::Kind::Circle;
        c.x = finiteOrZero(s.cx); c.y = finiteOrZero(s.cy); c.w = nonNegative(s.r);
        c.fill = s.fill; c.stroke = s.stroke; c.strokeWidth = nonNegative(s.strokeWidth);
    }
    for (const LineShape& l : frame.lines) {
        RenderCommand& c = nextCommand();
        c.kind = RenderCommand::Kind::Line;
        c.x = finiteOrZero(l.x1); c.y = finiteOrZero(l.y1);
        c.w = finiteOrZero(l.x2); c.h = finiteOrZero(l.y2);
        c.strokeWidth = nonNegative(l.width);
        c.fill = l.color; c.stroke = l.color;
    }
    for (const PolyShape& p : frame.polys) {
        RenderCommand& c = nextCommand();
        c.kind = RenderCommand::Kind::Poly;
        c.pointCount = (p.count > PolyShape::kMaxPoints)
                           ? static_cast<std::uint8_t>(PolyShape::kMaxPoints)
                           : p.count;
        for (std::size_t i = 0; i < c.pointCount; ++i) {
            c.xs[i] = finiteOrZero(p.xs[i]);
            c.ys[i] = finiteOrZero(p.ys[i]);
        }
        c.fill = p.fill; c.stroke = p.stroke; c.strokeWidth = nonNegative(p.strokeWidth);
    }
    for (const TextShape& t : frame.texts) {
        RenderCommand& c = nextCommand();
        c.kind = RenderCommand::Kind::Text;
        c.x = finiteOrZero(t.x); c.y = finiteOrZero(t.y);
        c.size = (nonNegative(t.size) < 1.0f) ? 1.0f : nonNegative(t.size);
        c.fill = t.color;
        c.align = t.align;
        c.bold = t.bold;
        c.mono = t.mono;
        utf8FromUtf32(t.text, out.scratch);
        // Reuses the slot's buffer. A little slack on growth keeps a label that
        // changes length (a score, a fuel percentage) from reallocating every
        // time it gains a digit — measured as the last remaining per-frame
        // allocation in wasd-race before this.
        if (c.text.capacity() < out.scratch.size()) {
            c.text.reserve(out.scratch.size() + 8);
        }
        c.text.assign(out.scratch);
    }
    out.commands.resize(index);
}

//===========================================================================
// JSON
//===========================================================================
void renderListToJson(const RenderList& list, std::string& out) {
    out.clear();
    // Grow geometrically: a frame whose command count differs by one must not
    // reallocate, or a 60 Hz caller would churn the allocator for nothing.
    const std::size_t needed = 2048 + list.commands.size() * 40;
    if (out.capacity() < needed) {
        out.reserve(needed + needed / 4);
    }
    out += "{\"w\":";
    appendFloat(out, list.worldW, 0);
    out += ",\"h\":";
    appendFloat(out, list.worldH, 0);
    out += ",\"bg\":";
    appendColor(out, list.background);
    out += ",\"bg2\":";
    appendColor(out, list.backgroundTop);

    out += ",\"stats\":{\"score\":";
    out += std::to_string(list.stats.score);
    out += ",\"best\":";
    out += std::to_string(list.stats.highScore);
    out += ",\"level\":";
    out += std::to_string(list.stats.level);
    out += ",\"combo\":";
    out += std::to_string(list.stats.combo);
    out += ",\"maxCombo\":";
    out += std::to_string(list.stats.maxCombo);
    out += ",\"lives\":";
    out += std::to_string(list.stats.lives);
    out += ",\"wpm\":";
    appendFloat(out, list.stats.wpm, 1);
    out += ",\"acc\":";
    appendFloat(out, list.stats.accuracy, 1);
    out += ",\"progress\":";
    appendFloat(out, list.stats.progress, 4);
    out += ",\"meter\":";
    appendFloat(out, list.stats.meter, 4);
    out += ",\"meterMax\":";
    appendFloat(out, list.stats.meterMax, 4);
    out += ",\"hasMeter\":";
    appendBool(out, list.stats.hasMeter);
    out += ",\"paused\":";
    appendBool(out, list.stats.paused);
    out += ",\"gameOver\":";
    appendBool(out, list.stats.gameOver);
    out += ",\"finished\":";
    appendBool(out, list.stats.finished);
    out += ",\"hasBest\":";
    appendBool(out, list.stats.hasHighScore);
    out += "}";

    out += ",\"title\":";
    appendJsonString(out, list.title);
    out += ",\"status\":";
    appendJsonString(out, list.status);
    out += ",\"banner\":";
    appendJsonString(out, list.banner);
    out += ",\"hint\":";
    appendJsonString(out, list.hint);

    out += ",\"cmds\":[";
    for (std::size_t i = 0; i < list.commands.size(); ++i) {
        const RenderCommand& c = list.commands[i];
        if (i != 0) {
            out += ',';
        }
        out += '[';
        out += std::to_string(static_cast<int>(c.kind));
        switch (c.kind) {
            case RenderCommand::Kind::Rect:
                out += ',';
                appendFloat(out, c.x, kCoordDecimals);
                out += ',';
                appendFloat(out, c.y, kCoordDecimals);
                out += ',';
                appendFloat(out, c.w, kCoordDecimals);
                out += ',';
                appendFloat(out, c.h, kCoordDecimals);
                out += ',';
                appendFloat(out, c.radius, kCoordDecimals);
                out += ',';
                appendColor(out, c.fill);
                out += ',';
                appendColor(out, c.stroke);
                out += ',';
                appendFloat(out, c.strokeWidth, kCoordDecimals);
                break;
            case RenderCommand::Kind::Circle:
                out += ',';
                appendFloat(out, c.x, kCoordDecimals);
                out += ',';
                appendFloat(out, c.y, kCoordDecimals);
                out += ',';
                appendFloat(out, c.w, kCoordDecimals);
                out += ',';
                appendColor(out, c.fill);
                out += ',';
                appendColor(out, c.stroke);
                out += ',';
                appendFloat(out, c.strokeWidth, kCoordDecimals);
                break;
            case RenderCommand::Kind::Line:
                out += ',';
                appendFloat(out, c.x, kCoordDecimals);
                out += ',';
                appendFloat(out, c.y, kCoordDecimals);
                out += ',';
                appendFloat(out, c.w, kCoordDecimals);
                out += ',';
                appendFloat(out, c.h, kCoordDecimals);
                out += ',';
                appendColor(out, c.stroke);
                out += ',';
                appendFloat(out, c.strokeWidth, kCoordDecimals);
                break;
            case RenderCommand::Kind::Poly:
                out += ',';
                out += std::to_string(c.pointCount);
                for (std::uint8_t p = 0; p < c.pointCount; ++p) {
                    out += ',';
                    appendFloat(out, c.xs[p], kCoordDecimals);
                    out += ',';
                    appendFloat(out, c.ys[p], kCoordDecimals);
                }
                out += ',';
                appendColor(out, c.fill);
                out += ',';
                appendColor(out, c.stroke);
                out += ',';
                appendFloat(out, c.strokeWidth, kCoordDecimals);
                break;
            case RenderCommand::Kind::Text:
                out += ',';
                appendFloat(out, c.x, kCoordDecimals);
                out += ',';
                appendFloat(out, c.y, kCoordDecimals);
                out += ',';
                appendFloat(out, c.size, kSizeDecimals);
                out += ',';
                appendColor(out, c.fill);
                out += ',';
                out += std::to_string(static_cast<int>(c.align));
                out += ',';
                appendBool(out, c.bold);
                out += ',';
                appendBool(out, c.mono);
                out += ',';
                appendJsonString(out, c.text);
                break;
        }
        out += ']';
    }
    out += "]}";
}

std::string renderListToJson(const RenderList& list) {
    std::string out;
    renderListToJson(list, out);
    return out;
}

std::string gameCatalogToJson() {
    std::string out = "{\"games\":[";
    const auto& catalog = gameCatalog();
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        const GameInfo& g = catalog[i];
        if (i != 0) {
            out += ',';
        }
        out += "{\"id\":";
        out += std::to_string(g.id);
        out += ",\"slug\":";
        appendJsonString(out, g.slug != nullptr ? g.slug : "");
        out += ",\"nameVi\":";
        appendJsonString(out, g.nameVi != nullptr ? g.nameVi : "");
        out += ",\"nameEn\":";
        appendJsonString(out, g.nameEn != nullptr ? g.nameEn : "");
        out += ",\"emoji\":";
        appendJsonString(out, g.emoji != nullptr ? g.emoji : "");
        out += ",\"descVi\":";
        appendJsonString(out, g.descriptionVi != nullptr ? g.descriptionVi : "");
        out += ",\"controlsVi\":";
        appendJsonString(out, g.controlsVi != nullptr ? g.controlsVi : "");
        out += ",\"typing\":";
        appendBool(out, g.typingDriven);
        out += ",\"injects\":";
        appendBool(out, g.canInjectText);
        out += '}';
    }
    out += "]}";
    return out;
}

std::string renderListToText(const RenderList& list) {
    std::string out;
    out.reserve(256 + list.commands.size() * 8);
    out += "frame ";
    out += std::to_string(static_cast<int>(list.worldW));
    out += 'x';
    out += std::to_string(static_cast<int>(list.worldH));
    out += " cmds=";
    out += std::to_string(list.commands.size());
    out += '\n';
    if (!list.title.empty()) {
        out += "title: ";
        out += list.title;
        out += '\n';
    }
    if (!list.status.empty()) {
        out += "status: ";
        out += list.status;
        out += '\n';
    }
    for (const RenderCommand& c : list.commands) {
        if (c.kind != RenderCommand::Kind::Text || c.text.empty()) {
            continue;
        }
        out += c.text;
        out += '\n';
    }
    return out;
}

} // namespace ok::arcade
