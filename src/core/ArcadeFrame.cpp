//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: src/core/ArcadeFrame.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "ArcadeFrame.hpp"

#include <algorithm>

namespace ok::arcade {

Frame::Frame() {
    rects.reserve(kMaxRects);
    circles.reserve(kMaxCircles);
    lines.reserve(kMaxLines);
    polys.reserve(kMaxPolys);
    texts.reserve(kMaxTexts);
    m_arena.reserve(kArenaReserve);
}

void Frame::clear() noexcept {
    rects.clear();
    circles.clear();
    lines.clear();
    polys.clear();
    texts.clear();
    stats = GameStats{};
    background = palette::kBackground;
    backgroundTop = palette::kBackground;
    worldW = 1280.0f;
    worldH = 720.0f;
    // `clear()` keeps the capacity (no allocation) but MUST truncate the
    // string: leaving the old contents in place made every later intern append
    // after them, so the arena grew without bound while `m_arenaUsed` stayed
    // low — i.e. the overflow guard never fired and the frame silently lost
    // text after a few seconds of play.
    m_arena.clear();
    m_arenaUsed = 0;
    droppedShapes = 0;
}

bool Frame::addRect(const RectShape& r) {
    if (rects.size() >= kMaxRects) {
        ++droppedShapes;
        return false;
    }
    rects.push_back(r);
    return true;
}

bool Frame::addRect(float x, float y, float w, float h, Color fill, float radius) {
    RectShape r{};
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    r.radius = radius;
    r.fill = fill;
    r.stroke = 0;
    r.strokeWidth = 0;
    return addRect(r);
}

bool Frame::addCircle(const CircleShape& c) {
    if (circles.size() >= kMaxCircles) {
        ++droppedShapes;
        return false;
    }
    circles.push_back(c);
    return true;
}

bool Frame::addCircle(float cx, float cy, float r, Color fill) {
    CircleShape c{};
    c.cx = cx;
    c.cy = cy;
    c.r = r;
    c.fill = fill;
    c.stroke = 0;
    c.strokeWidth = 0;
    return addCircle(c);
}

bool Frame::addLine(float x1, float y1, float x2, float y2, Color color, float width) {
    if (lines.size() >= kMaxLines) {
        ++droppedShapes;
        return false;
    }
    LineShape l{};
    l.x1 = x1;
    l.y1 = y1;
    l.x2 = x2;
    l.y2 = y2;
    l.color = color;
    l.width = width;
    lines.push_back(l);
    return true;
}

bool Frame::addPoly(const PolyShape& p) {
    if (polys.size() >= kMaxPolys) {
        ++droppedShapes;
        return false;
    }
    polys.push_back(p);
    return true;
}

bool Frame::addText(float x, float y, float size, Color color, TextAlign align,
                    std::u32string_view text, bool bold, bool mono) {
    if (texts.size() >= kMaxTexts) {
        ++droppedShapes;
        return false;
    }
    TextShape t{};
    t.x = x;
    t.y = y;
    t.size = size;
    t.color = color;
    t.align = align;
    t.bold = bold;
    t.mono = mono;
    // Own the string: a view into the caller's stack frame (or into a
    // temporary) must not outlive this call — see the note in the header.
    t.text = owns(text) ? text : intern(text);
    texts.push_back(t);
    return true;
}

std::u32string_view Frame::intern(std::u32string_view s) {
    if (s.empty()) {
        return {};
    }
    if (m_arenaUsed + s.size() > kArenaReserve) {
        ++droppedShapes;
        return {};
    }
    // Advance past a NUL sentinel so the previous string stays NUL-terminated
    // (handy for Win32 DrawTextW, which wants a length anyway, but keeps the
    // arena debuggable in a watch window).
    if (m_arenaUsed > 0) {
        m_arena.push_back(U'\0');
        ++m_arenaUsed;
        if (m_arenaUsed + s.size() > kArenaReserve) {
            ++droppedShapes;
            return {};
        }
    }
    const size_t offset = m_arenaUsed;
    m_arena.append(s);
    m_arenaUsed += s.size();
    return std::u32string_view(m_arena.data() + offset, s.size());
}

namespace {

void appendInt(std::u32string& out, std::int64_t value) {
    if (value == 0) {
        out.push_back(U'0');
        return;
    }
    bool negative = value < 0;
    // Use unsigned magnitude so INT64_MIN is handled without UB.
    std::uint64_t mag = negative ? (static_cast<std::uint64_t>(-(value + 1)) + 1ULL)
                                 : static_cast<std::uint64_t>(value);
    char tmp[24];
    int n = 0;
    while (mag > 0 && n < 24) {
        tmp[n++] = static_cast<char>('0' + static_cast<int>(mag % 10));
        mag /= 10;
    }
    if (negative) {
        out.push_back(U'-');
    }
    while (n > 0) {
        out.push_back(static_cast<char32_t>(tmp[--n]));
    }
}

} // namespace

std::u32string_view Frame::internNumber(std::u32string_view prefix, std::int64_t value,
                                        std::u32string_view suffix) {
    const size_t need = prefix.size() + suffix.size() + 24;
    if (m_arenaUsed + need > kArenaReserve) {
        ++droppedShapes;
        return {};
    }
    // Compose into a scratch area at the end of the arena, then keep the
    // [start,end) window.
    if (m_arenaUsed > 0) {
        m_arena.push_back(U'\0');
        ++m_arenaUsed;
    }
    const size_t start = m_arenaUsed;
    m_arena.append(prefix);
    appendInt(m_arena, value);
    m_arena.append(suffix);
    const size_t len = m_arena.size() - start;
    m_arenaUsed = m_arena.size();
    return std::u32string_view(m_arena.data() + start, len);
}

std::u32string_view Frame::internAscii(std::string_view utf8, std::u32string_view prefix,
                                      std::u32string_view suffix) {
    if (m_arenaUsed + prefix.size() + utf8.size() + suffix.size() + 2 > kArenaReserve) {
        ++droppedShapes;
        return {};
    }
    if (m_arenaUsed > 0) {
        m_arena.push_back(U'\0');
        ++m_arenaUsed;
    }
    const size_t start = m_arenaUsed;
    m_arena.append(prefix);
    // Decode UTF-8 (BMP + supplementary) straight into the arena.
    size_t i = 0;
    while (i < utf8.size()) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        char32_t cp = c;
        if (c >= 0xF0 && i + 3 < utf8.size()) {
            cp = static_cast<char32_t>(c & 0x07u);
            cp = (cp << 6) | static_cast<char32_t>(static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu);
            cp = (cp << 6) | static_cast<char32_t>(static_cast<unsigned char>(utf8[i + 2]) & 0x3Fu);
            cp = (cp << 6) | static_cast<char32_t>(static_cast<unsigned char>(utf8[i + 3]) & 0x3Fu);
            i += 4;
        } else if (c >= 0xE0 && i + 2 < utf8.size()) {
            cp = static_cast<char32_t>(c & 0x0Fu);
            cp = (cp << 6) | static_cast<char32_t>(static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu);
            cp = (cp << 6) | static_cast<char32_t>(static_cast<unsigned char>(utf8[i + 2]) & 0x3Fu);
            i += 3;
        } else if (c >= 0xC0 && i + 1 < utf8.size()) {
            cp = static_cast<char32_t>(c & 0x1Fu);
            cp = (cp << 6) | static_cast<char32_t>(static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu);
            i += 2;
        } else {
            i += 1;
        }
        m_arena.push_back(cp);
    }
    m_arena.append(suffix);
    const size_t len = m_arena.size() - start;
    m_arenaUsed = m_arena.size();
    return std::u32string_view(m_arena.data() + start, len);
}

std::u32string_view Frame::internDouble(std::u32string_view prefix, double value, int decimals,
                                        std::u32string_view suffix) {
    if (decimals < 0) decimals = 0;
    if (decimals > 4) decimals = 4;
    // Round-half-away-from-zero on the scaled integer, then print with a
    // synthetic decimal point: avoids <sstream> locales and allocations.
    double scale = 1.0;
    for (int i = 0; i < decimals; ++i) scale *= 10.0;
    double scaled = value * scale;
    if (scaled > 9.0e15) scaled = 9.0e15;
    if (scaled < -9.0e15) scaled = -9.0e15;
    std::int64_t whole = static_cast<std::int64_t>(scaled >= 0 ? scaled + 0.5 : scaled - 0.5);
    bool negative = whole < 0;
    std::uint64_t mag = negative ? (static_cast<std::uint64_t>(-(whole + 1)) + 1ULL)
                                 : static_cast<std::uint64_t>(whole);
    const size_t need = prefix.size() + suffix.size() + 32;
    if (m_arenaUsed + need > kArenaReserve) {
        ++droppedShapes;
        return {};
    }
    if (m_arenaUsed > 0) {
        m_arena.push_back(U'\0');
        ++m_arenaUsed;
    }
    const size_t start = m_arenaUsed;
    m_arena.append(prefix);
    if (negative) {
        m_arena.push_back(U'-');
    }
    appendInt(m_arena, static_cast<std::int64_t>(mag / static_cast<std::uint64_t>(scale)));
    if (decimals > 0) {
        m_arena.push_back(U'.');
        std::uint64_t frac = mag % static_cast<std::uint64_t>(scale);
        char digits[8] = {};
        for (int i = 0; i < decimals; ++i) {
            digits[i] = static_cast<char>('0' + static_cast<int>(frac % 10));
            frac /= 10;
        }
        for (int i = decimals - 1; i >= 0; --i) {
            m_arena.push_back(static_cast<char32_t>(digits[i]));
        }
    }
    m_arena.append(suffix);
    const size_t len = m_arena.size() - start;
    m_arenaUsed = m_arena.size();
    return std::u32string_view(m_arena.data() + start, len);
}

//===========================================================================
// Catalog — one source of truth for menus, the web client and the tests.
//===========================================================================
const std::vector<GameInfo>& gameCatalog() noexcept {
    static const std::vector<GameInfo> kCatalog = {
        {1, "snake", "Rắn săn mồi", "Snake", "🐍",
         "Điều khiển rắn ăn mồi, càng ăn càng nhanh.", "WASD / phím mũi tên · P hoặc F1 tạm dừng · R hoặc F2 chơi lại · Esc thoát",
         false, false},
        {2, "tetris", "Xếp gạch", "Tetris", "🧱",
         "7 loại tetromino, xoay, xoá hàng, tăng cấp.", "A/D di chuyển · W xoay · S rơi nhanh · Space rơi thẳng · P/F1 tạm dừng",
         false, false},
        {3, "fishing", "Câu cá bằng gõ phím", "Fishing", "🎣",
         "Gõ đúng câu mồi để kéo cá vào bờ; cá quý hiếm thoát nhanh hơn.", "Gõ đúng ký tự hiện ra · F1 tạm dừng · Esc thoát",
         true, false},
        {4, "typing-race", "Đua xe theo tốc độ gõ", "Typing Race", "🏎️",
         "Đua với chính tốc độ gõ của bạn trên đường đua WPM.", "Gõ đúng đoạn văn để tăng tốc · F1 tạm dừng · Esc thoát",
         true, false},
        {5, "wasd-race", "Đua xe + né chướng ngại vật", "WASD + Typing Racing", "🛣️",
         "Vừa gõ để tiếp nhiên liệu, vừa WASD để né xe.", "A/D đổi làn · W tăng tốc · S phanh · Gõ để nạp nhiên liệu · F1 tạm dừng",
         true, false},
        {6, "rhythm", "Gõ theo nhịp FNF-style", "Rhythm Typing", "🎵",
         "Bấm đúng nốt theo nhịp: quá nhanh chết, quá chậm chết.", "D / F / J / K đúng nhịp · F1 tạm dừng · F2 chơi lại · Esc thoát",
         true, false},
        {7, "no-mistake", "Không được sai", "No-Mistake Mode", "🎯",
         "Một lỗi là mất cả màn chơi (cấu hình được).", "Gõ đúng từng ký tự · F1 tạm dừng · F2 chơi lại · Esc thoát",
         true, false},
        {8, "flexing", "Flexing Mode", "Flexing Mode", "🗿",
         "Vui là chính: gõ đại nhưng văn bản tự hiện (không tính điểm chuẩn).", "Gõ phím bất kỳ · F1 tạm dừng · F2 chơi lại · Esc thoát",
         false, true},
    };
    return kCatalog;
}

const GameInfo* gameInfo(int gameTypeId) noexcept {
    for (const auto& info : gameCatalog()) {
        if (static_cast<int>(info.id) == gameTypeId) {
            return &info;
        }
    }
    return nullptr;
}

std::string_view gameSlug(int gameTypeId) noexcept {
    const GameInfo* info = gameInfo(gameTypeId);
    return info ? std::string_view(info->slug) : std::string_view("none");
}

int gameTypeFromSlug(std::string_view slug) noexcept {
    for (const auto& info : gameCatalog()) {
        if (slug == info.slug) {
            return static_cast<int>(info.id);
        }
    }
    return 0;
}

} // namespace ok::arcade
