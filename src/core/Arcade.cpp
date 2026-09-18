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
// File: src/core/Arcade.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "Arcade.hpp"

#include "Progression.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace ok::arcade {

namespace {

constexpr double kMaxFrameStepSec = 0.05;   // 20 FPS floor for physics stepping
constexpr double kMonoCharWidthFactor = 0.62;  // monospace advance / font size

double clampDt(double dt) noexcept {
    if (!(dt > 0.0)) {   // also filters NaN
        return 0.0;
    }
    return dt > kMaxFrameStepSec ? kMaxFrameStepSec : dt;
}

// Fixed-step accumulator: returns how many whole steps of `step` fit in `acc`.
uint32_t takeSteps(double& acc, double step) noexcept {
    if (step <= 0.0) {
        return 0;
    }
    uint32_t steps = 0;
    while (acc >= step && steps < 64) {   // 64 = hard anti-freeze cap
        acc -= step;
        ++steps;
    }
    if (acc >= step) {
        acc = 0.0;
    }
    return steps;
}

//----------------------------------------------------------------------------
// Per-character styled passage line (correct / wrong / caret / upcoming),
// emitted as a handful of text runs (one per state change, not per char).
//----------------------------------------------------------------------------
struct PassageStyle {
    Color typed = palette::kGood;
    Color wrong = palette::kBad;
    Color caret = palette::kWarn;
    Color upcoming = palette::kTextDim;
    float size = 26;
};

float addStyledLine(Frame& frame, float x, float y, std::u32string_view text,
                    std::size_t caretIndex, std::size_t errorIndex, std::size_t begin,
                    const PassageStyle& style) {
    if (begin >= text.size()) {
        begin = text.size();
    }
    const float advance = style.size * kMonoCharWidthFactor;
    float cursorX = x;
    std::size_t runStart = begin;
    Color runColor = (begin == caretIndex) ? style.caret
                                           : ((begin == errorIndex) ? style.wrong : style.upcoming);

    auto flush = [&](std::size_t endExclusive, Color color) {
        if (endExclusive <= runStart) {
            return;
        }
        frame.addText(cursorX, y, style.size, color, TextAlign::Left,
                      text.substr(runStart, endExclusive - runStart), false, true);
        cursorX += advance * static_cast<float>(endExclusive - runStart);
        runStart = endExclusive;
    };

    for (std::size_t i = begin; i < text.size(); ++i) {
        Color color;
        if (i < caretIndex) {
            color = (i == errorIndex) ? style.wrong : style.typed;
        } else if (i == caretIndex) {
            color = style.caret;
        } else {
            color = style.upcoming;
        }
        if (color != runColor) {
            flush(i, runColor);
            runColor = color;
        }
    }
    flush(text.size(), runColor);
    return cursorX - x;
}

// Compute the horizontal scroll offset that keeps the caret inside `windowW`.
float caretScroll(std::size_t caret, float advance, float windowW) noexcept {
    const float caretX = static_cast<float>(caret) * advance;
    if (caretX <= windowW) {
        return 0.0f;
    }
    return caretX - windowW * 0.75f;
}

const char* directionGlyph(Direction d) noexcept {
    switch (d) {
        case Direction::Up: return "Up";
        case Direction::Down: return "Down";
        case Direction::Left: return "Left";
        case Direction::Right: return "Right";
    }
    return "?";
}

} // namespace

//===========================================================================
// 1. Snake
//===========================================================================
SnakeGame::SnakeGame() {
    reset();
}

void SnakeGame::start() {
    reset();
    m_paused = false;
    m_gameOver = false;
    m_exitRequested = false;
    m_resultPending = false;
}

void SnakeGame::reset() {
    m_body.clear();
    m_body.push_back({10, 7});
    m_body.push_back({9, 7});
    m_body.push_back({8, 7});
    m_dir = Direction::Right;
    m_nextDir = Direction::Right;
    m_score = 0;
    m_tickTimer = 0.0;
    m_tickInterval = 0.15;
    m_runTimeSec = 0.0;
    m_paused = false;
    m_gameOver = false;
    m_resultPending = false;
    spawnFood();
}

void SnakeGame::spawnFood() {
    // Try random cells first; fall back to the first free cell so a nearly
    // full board can never leave food inside the snake.
    for (int attempts = 0; attempts < 200; ++attempts) {
        const int fx = static_cast<int>(m_rng.below(static_cast<std::uint32_t>(kWidth)));
        const int fy = static_cast<int>(m_rng.below(static_cast<std::uint32_t>(kHeight)));
        const SnakePoint candidate{fx, fy};
        if (std::find(m_body.begin(), m_body.end(), candidate) == m_body.end()) {
            m_food = candidate;
            return;
        }
    }
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const SnakePoint candidate{x, y};
            if (std::find(m_body.begin(), m_body.end(), candidate) == m_body.end()) {
                m_food = candidate;
                return;
            }
        }
    }
    m_food = {-1, -1};   // board full: no food left to place
}

void SnakeGame::die() {
    if (m_gameOver) {
        return;
    }
    m_gameOver = true;
    m_resultPending = true;
    if (m_score > m_highScore) {
        m_highScore = m_score;
    }
}

void SnakeGame::step() {
    if (m_gameOver || m_paused) {
        return;
    }

    m_dir = m_nextDir;
    SnakePoint head = m_body.front();
    switch (m_dir) {
        case Direction::Up:    head.y--; break;
        case Direction::Down:  head.y++; break;
        case Direction::Left:  head.x--; break;
        case Direction::Right: head.x++; break;
    }

    if (head.x < 0 || head.x >= kWidth || head.y < 0 || head.y >= kHeight) {
        die();
        return;
    }

    const bool willEat = (head == m_food);
    // Self-collision: the tail cell is vacated this tick unless the snake is
    // growing, so moving into it is legal (the pre-v1.3.0 code killed the
    // snake one tick early on every tight turn).
    const std::size_t checkedLength = willEat ? m_body.size() : (m_body.size() - 1);
    for (std::size_t i = 0; i < checkedLength; ++i) {
        if (m_body[i] == head) {
            die();
            return;
        }
    }

    m_body.push_front(head);
    if (willEat) {
        m_score += 100;
        if (m_score > m_highScore) {
            m_highScore = m_score;
        }
        if (m_tickInterval > 0.06) {
            m_tickInterval = std::max(0.06, m_tickInterval - 0.005);
        }
        spawnFood();
    } else {
        m_body.pop_back();
    }
}

void SnakeGame::update(double dt) {
    if (m_paused || m_gameOver) {
        return;
    }
    const double step0 = clampDt(dt);
    m_runTimeSec += step0;
    m_tickTimer += step0;
    const uint32_t steps = takeSteps(m_tickTimer, m_tickInterval);
    for (uint32_t i = 0; i < steps; ++i) {
        step();
        if (m_gameOver) {
            break;
        }
    }
}

InputResult SnakeGame::handleKey(const InputEvent& ev) {
    if (isExitKey(ev)) {
        m_exitRequested = true;
        m_resultPending = true;
        return InputResult::ExitRequested;
    }
    if (!ev.down) {
        return InputResult::Consumed;   // the game owns the keyboard while open
    }
    if (isPauseKey(ev, /*allowLetterAlias=*/true)) {
        m_paused = !m_paused;
        return InputResult::Consumed;
    }
    if (isRestartKey(ev, /*allowLetterAlias=*/true)) {
        reset();
        return InputResult::Consumed;
    }

    if (ev.ch == U'w' || ev.ch == U'W' || ev.vk == vk::kUp) {
        if (m_dir != Direction::Down) m_nextDir = Direction::Up;
    } else if (ev.ch == U's' || ev.ch == U'S' || ev.vk == vk::kDown) {
        if (m_dir != Direction::Up) m_nextDir = Direction::Down;
    } else if (ev.ch == U'a' || ev.ch == U'A' || ev.vk == vk::kLeft) {
        if (m_dir != Direction::Right) m_nextDir = Direction::Left;
    } else if (ev.ch == U'd' || ev.ch == U'D' || ev.vk == vk::kRight) {
        if (m_dir != Direction::Left) m_nextDir = Direction::Right;
    }
    return InputResult::Consumed;
}

void SnakeGame::stressTestInputQueue(const std::vector<int>& vks) {
    for (int vk : vks) {
        InputEvent ev{};
        ev.vk = vk;
        ev.down = true;
        handleKey(ev);
        // Advance exactly one tick so the injected input is exercised through
        // the real stepping path (this used to call step() directly, bypassing
        // the pause / game-over guards).
        update(m_tickInterval);
    }
}

bool SnakeGame::pollRunResult(RunResult& out) {
    if (!m_resultPending) {
        return false;
    }
    m_resultPending = false;
    out.type = GameType::Snake;
    out.score = m_score;
    out.highScore = m_highScore;
    out.level = 1 + static_cast<uint32_t>(m_score / 500);
    out.maxCombo = 0;
    out.wpm = 0.0;
    out.accuracy = 100.0;
    out.durationSec = m_runTimeSec;
    out.completed = false;
    return true;
}

void SnakeGame::buildFrame(Frame& frame) const {
    frame.worldW = 800.0f;
    frame.worldH = 600.0f;
    frame.background = palette::kBackground;
    frame.backgroundTop = rgba(0x14, 0x1B, 0x26);

    constexpr float kCell = 30.0f;
    const float boardW = kCell * static_cast<float>(kWidth);
    const float boardH = kCell * static_cast<float>(kHeight);
    const float boardX = (frame.worldW - boardW) * 0.5f;
    const float boardY = 90.0f;

    frame.addRect(boardX - 6, boardY - 6, boardW + 12, boardH + 12, palette::kPanel, 10);
    for (int x = 0; x <= kWidth; ++x) {
        frame.addLine(boardX + static_cast<float>(x) * kCell, boardY,
                      boardX + static_cast<float>(x) * kCell, boardY + boardH,
                      palette::kGrid, 1);
    }
    for (int y = 0; y <= kHeight; ++y) {
        frame.addLine(boardX, boardY + static_cast<float>(y) * kCell,
                      boardX + boardW, boardY + static_cast<float>(y) * kCell,
                      palette::kGrid, 1);
    }

    for (std::size_t i = 0; i < m_body.size(); ++i) {
        const SnakePoint pt = m_body[i];
        const float x = boardX + static_cast<float>(pt.x) * kCell + 2;
        const float y = boardY + static_cast<float>(pt.y) * kCell + 2;
        frame.addRect(x, y, kCell - 4, kCell - 4,
                      (i == 0) ? palette::kSnakeHead : palette::kSnakeBody, 6);
    }

    if (m_food.x >= 0) {
        frame.addCircle(boardX + (static_cast<float>(m_food.x) + 0.5f) * kCell,
                        boardY + (static_cast<float>(m_food.y) + 0.5f) * kCell,
                        kCell * 0.32f, palette::kFood);
    }

    frame.stats.title = U"🐍 Snake";
    frame.stats.score = m_score;
    frame.stats.highScore = m_highScore;
    frame.stats.level = 1 + static_cast<uint32_t>(m_score / 500);
    frame.stats.status = frame.internNumber(U"Tốc độ: ", static_cast<int64_t>(
        std::lround((1.0 / m_tickInterval) * 10.0)), U" ô/s");
    frame.stats.hint = U"WASD / mũi tên · P tạm dừng · R chơi lại · Esc thoát";
    frame.stats.paused = m_paused;
    frame.stats.gameOver = m_gameOver;
    if (m_gameOver) {
        frame.stats.banner = U"GAME OVER — nhấn R để chơi lại";
    } else if (m_paused) {
        frame.stats.banner = U"TẠM DỪNG";
    }
}

std::string SnakeGame::renderText() const {
    std::ostringstream ss;
    ss << "+";
    for (int x = 0; x < kWidth; ++x) ss << "-";
    ss << "+\n";
    for (int y = 0; y < kHeight; ++y) {
        ss << "|";
        for (int x = 0; x < kWidth; ++x) {
            const SnakePoint pt{x, y};
            if (pt == m_body.front()) {
                ss << "@";
            } else if (pt == m_food) {
                ss << "*";
            } else if (std::find(m_body.begin() + 1, m_body.end(), pt) != m_body.end()) {
                ss << "o";
            } else {
                ss << " ";
            }
        }
        ss << "|\n";
    }
    ss << "+";
    for (int x = 0; x < kWidth; ++x) ss << "-";
    ss << "+\n";
    ss << "Score: " << m_score << " | High: " << m_highScore
       << " | Dir: " << directionGlyph(m_dir)
       << " | Len: " << m_body.size();
    if (m_paused) ss << " [PAUSED]";
    if (m_gameOver) ss << " [GAME OVER - Press R]";
    ss << "\n";
    return ss.str();
}

//===========================================================================
// 2. Tetris
//===========================================================================
namespace {

// 7 pieces x 4 rotations x 4 blocks [dy][dx].
// Verified against the standard SRS spawn orientations (v1.3.0 fixed the S/Z
// spawn tables, which had been swapped, and the L/J rotation cycles).
const int kPieces[7][4][4][2] = {
    // 0: I
    {{{0,0},{1,0},{2,0},{3,0}}, {{2,-1},{2,0},{2,1},{2,2}}, {{0,0},{1,0},{2,0},{3,0}}, {{2,-1},{2,0},{2,1},{2,2}}},
    // 1: O
    {{{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}},
    // 2: T
    {{{1,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{2,1},{1,2}}, {{0,1},{1,1},{2,1},{1,2}}, {{1,0},{0,1},{1,1},{1,2}}},
    // 3: S
    {{{1,0},{2,0},{0,1},{1,1}}, {{1,0},{1,1},{2,1},{2,2}}, {{1,0},{2,0},{0,1},{1,1}}, {{1,0},{1,1},{2,1},{2,2}}},
    // 4: Z
    {{{0,0},{1,0},{1,1},{2,1}}, {{2,0},{1,1},{2,1},{1,2}}, {{0,0},{1,0},{1,1},{2,1}}, {{2,0},{1,1},{2,1},{1,2}}},
    // 5: J
    {{{0,0},{0,1},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{1,2}}, {{0,1},{1,1},{2,1},{2,2}}, {{1,0},{1,1},{0,2},{1,2}}},
    // 6: L
    {{{2,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{1,2},{2,2}}, {{0,1},{1,1},{2,1},{0,2}}, {{0,0},{1,0},{1,1},{1,2}}},
};

const Color kPieceColors[8] = {
    0u,
    rgba(0x4C, 0xC2, 0xFF),  // I
    rgba(0xFF, 0xD1, 0x4A),  // O
    rgba(0xC1, 0x7C, 0xFF),  // T
    rgba(0x51, 0xE8, 0x8A),  // S
    rgba(0xFF, 0x5A, 0x5A),  // Z
    rgba(0x5A, 0x8A, 0xFF),  // J
    rgba(0xFF, 0x9A, 0x3D),  // L
};

} // namespace

TetrisGame::TetrisGame() {
    reset();
}

void TetrisGame::setSeed(std::uint32_t seed) {
    m_seed = seed;
    m_rng.reseed(seed);
    m_bagIndex = 7;
}

void TetrisGame::start() {
    reset();
    m_paused = false;
    m_gameOver = false;
    m_exitRequested = false;
    m_resultPending = false;
}

void TetrisGame::reset() {
    for (auto& row : m_grid) {
        row.fill(0);
    }
    m_score = 0;
    m_linesCleared = 0;
    m_level = 1;
    m_dropTimer = 0.0;
    m_dropInterval = 0.6;
    m_lockTimer = 0.0;
    m_runTimeSec = 0.0;
    m_paused = false;
    m_gameOver = false;
    m_resultPending = false;
    // Fresh bag: the very first seven pieces are a permutation of all seven
    // tetrominoes, and `nextFromBag()` guarantees the preview is always
    // consistent with what spawns next.
    m_bagIndex = 7;
    m_nextPiece = nextFromBag();
    spawnPiece();
}

void TetrisGame::refillBag() {
    // 7-bag randomizer: every piece appears once per bag → no droughts.
    for (int i = 0; i < 7; ++i) {
        m_bag[static_cast<std::size_t>(i)] = i;
    }
    for (int i = 6; i > 0; --i) {
        const int j = static_cast<int>(m_rng.below(static_cast<std::uint32_t>(i + 1)));
        std::swap(m_bag[static_cast<std::size_t>(i)], m_bag[static_cast<std::size_t>(j)]);
    }
    m_bagIndex = 0;
}

bool TetrisGame::collidesPiece(int piece, int px, int py, int rot) const {
    if (piece < 0 || piece > 6 || rot < 0 || rot > 3) {
        return true;
    }
    for (int i = 0; i < 4; ++i) {
        const int x = px + kPieces[piece][rot][i][0];
        const int y = py + kPieces[piece][rot][i][1];
        if (x < 0 || x >= kCols || y >= kRows) {
            return true;
        }
        if (y >= 0 && m_grid[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] != 0) {
            return true;
        }
    }
    return false;
}

bool TetrisGame::collides(int px, int py, int rot) const {
    return collidesPiece(m_curPiece, px, py, rot);
}

bool TetrisGame::tryMove(int dx, int dy) {
    if (collides(m_curX + dx, m_curY + dy, m_curRot)) {
        return false;
    }
    m_curX += dx;
    m_curY += dy;
    return true;
}

bool TetrisGame::tryRotate(int delta) {
    const int nextRot = ((m_curRot + delta) % 4 + 4) % 4;
    // Simple wall kicks: try in place, then ±1, ±2 columns.
    static const int kicks[] = {0, -1, 1, -2, 2};
    for (int kick : kicks) {
        if (!collidesPiece(m_curPiece, m_curX + kick, m_curY, nextRot)) {
            m_curX += kick;
            m_curRot = nextRot;
            return true;
        }
    }
    return false;
}

const int* TetrisGame::pieceCellTable(int piece, int rot) noexcept {
    if (piece < 0 || piece > 6 || rot < 0 || rot > 3) {
        return nullptr;
    }
    return &kPieces[piece][rot][0][0];
}

int TetrisGame::getGhostY() const noexcept {
    int y = m_curY;
    while (!collides(m_curX, y + 1, m_curRot)) {
        ++y;
    }
    return y;
}

int TetrisGame::nextFromBag() {
    if (m_bagIndex >= 7) {
        refillBag();
    }
    return m_bag[static_cast<std::size_t>(m_bagIndex++)];
}

void TetrisGame::spawnPiece() {
    m_curPiece = m_nextPiece;
    m_nextPiece = nextFromBag();
    m_curRot = 0;
    m_curX = 3;
    m_curY = 0;
    m_lockTimer = 0.0;
    if (collides(m_curX, m_curY, m_curRot)) {
        finish();
    }
}

void TetrisGame::finish() {
    m_gameOver = true;
    m_resultPending = true;
    if (m_score > m_highScore) {
        m_highScore = m_score;
    }
}

void TetrisGame::lockPiece() {
    for (int i = 0; i < 4; ++i) {
        const int x = m_curX + kPieces[m_curPiece][m_curRot][i][0];
        const int y = m_curY + kPieces[m_curPiece][m_curRot][i][1];
        if (y >= 0 && y < kRows && x >= 0 && x < kCols) {
            m_grid[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] =
                static_cast<std::uint8_t>(m_curPiece + 1);
        }
    }
    clearLines();
    if (!m_gameOver) {
        spawnPiece();
    }
}

void TetrisGame::clearLines() {
    uint32_t lines = 0;
    for (int y = kRows - 1; y >= 0; --y) {
        bool full = true;
        for (int x = 0; x < kCols; ++x) {
            if (m_grid[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] == 0) {
                full = false;
                break;
            }
        }
        if (full) {
            ++lines;
            for (int ny = y; ny > 0; --ny) {
                m_grid[static_cast<std::size_t>(ny)] = m_grid[static_cast<std::size_t>(ny - 1)];
            }
            m_grid[0].fill(0);
            ++y;   // re-check the same row index after the shift
        }
    }

    if (lines > 0) {
        m_linesCleared += lines;
        const std::int64_t pts = (lines == 1) ? 100 : (lines == 2) ? 300 : (lines == 3) ? 500 : 800;
        m_score += pts * static_cast<std::int64_t>(m_level);
        if (m_score > m_highScore) {
            m_highScore = m_score;
        }
        m_level = 1 + (m_linesCleared / 10);
        m_dropInterval = std::max(0.08, 0.6 - static_cast<double>(m_level - 1) * 0.05);
    }
}

void TetrisGame::hardDrop() {
    while (!collides(m_curX, m_curY + 1, m_curRot)) {
        ++m_curY;
        m_score += 2;
    }
    lockPiece();
}

void TetrisGame::update(double dt) {
    if (m_paused || m_gameOver) {
        return;
    }
    const double step = clampDt(dt);
    m_runTimeSec += step;
    m_dropTimer += step;
    m_lockTimer += step;

    const uint32_t steps = takeSteps(m_dropTimer, m_dropInterval);
    for (uint32_t i = 0; i < steps; ++i) {
        if (collides(m_curX, m_curY + 1, m_curRot)) {
            // Lock delay: 0.5 s of grace so a piece never locks the instant it
            // touches down (this is what made the old build feel unresponsive).
            if (m_lockTimer >= 0.5) {
                lockPiece();
                m_lockTimer = 0.0;
            }
            break;
        }
        ++m_curY;
        m_lockTimer = 0.0;
    }
}

InputResult TetrisGame::handleKey(const InputEvent& ev) {
    if (isExitKey(ev)) {
        m_exitRequested = true;
        m_resultPending = true;
        return InputResult::ExitRequested;
    }
    if (!ev.down) {
        return InputResult::Consumed;
    }
    if (isPauseKey(ev, /*allowLetterAlias=*/true)) {
        m_paused = !m_paused;
        return InputResult::Consumed;
    }
    if (isRestartKey(ev, /*allowLetterAlias=*/true)) {
        reset();
        return InputResult::Consumed;
    }
    if (m_paused || m_gameOver) {
        return InputResult::Consumed;
    }

    if (ev.ch == U'a' || ev.ch == U'A' || ev.vk == vk::kLeft) {
        tryMove(-1, 0);
    } else if (ev.ch == U'd' || ev.ch == U'D' || ev.vk == vk::kRight) {
        tryMove(1, 0);
    } else if (ev.ch == U's' || ev.ch == U'S' || ev.vk == vk::kDown) {
        if (tryMove(0, 1)) {
            m_score += 1;
        }
    } else if (ev.ch == U'w' || ev.ch == U'W' || ev.vk == vk::kUp) {
        tryRotate(1);
    } else if (ev.vk == vk::kSpace) {
        hardDrop();
    }
    return InputResult::Consumed;
}

bool TetrisGame::pollRunResult(RunResult& out) {
    if (!m_resultPending) {
        return false;
    }
    m_resultPending = false;
    out.type = GameType::Tetris;
    out.score = m_score;
    out.highScore = m_highScore;
    out.level = m_level;
    out.maxCombo = 0;
    out.wpm = 0.0;
    out.accuracy = 100.0;
    out.durationSec = m_runTimeSec;
    out.completed = false;
    return true;
}

void TetrisGame::buildFrame(Frame& frame) const {
    frame.worldW = 800.0f;
    frame.worldH = 640.0f;
    frame.backgroundTop = rgba(0x14, 0x1B, 0x26);

    constexpr float kCell = 26.0f;
    const float boardW = kCell * static_cast<float>(kCols);
    const float boardH = kCell * static_cast<float>(kRows);
    const float boardX = 120.0f;
    const float boardY = 60.0f;

    frame.addRect(boardX - 5, boardY - 5, boardW + 10, boardH + 10, palette::kPanel, 6);

    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kCols; ++x) {
            const std::uint8_t cell = m_grid[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
            if (cell != 0) {
                frame.addRect(boardX + static_cast<float>(x) * kCell + 1,
                              boardY + static_cast<float>(y) * kCell + 1,
                              kCell - 2, kCell - 2,
                              kPieceColors[cell & 7u], 3);
            }
        }
    }

    if (!m_gameOver) {
        // Ghost piece (landing preview")
        const int ghostY = getGhostY();
        for (int i = 0; i < 4; ++i) {
            const int gx = m_curX + kPieces[m_curPiece][m_curRot][i][0];
            const int gy = ghostY + kPieces[m_curPiece][m_curRot][i][1];
            if (gy >= 0 && gy < kRows) {
                RectShape r{};
                r.x = boardX + static_cast<float>(gx) * kCell + 1;
                r.y = boardY + static_cast<float>(gy) * kCell + 1;
                r.w = kCell - 2;
                r.h = kCell - 2;
                r.radius = 3;
                r.fill = 0;
                r.stroke = rgba(0x9A, 0xA7, 0xB8, 0x88);
                r.strokeWidth = 1.5f;
                frame.addRect(r);
            }
        }
        // Active piece
        for (int i = 0; i < 4; ++i) {
            const int px = m_curX + kPieces[m_curPiece][m_curRot][i][0];
            const int py = m_curY + kPieces[m_curPiece][m_curRot][i][1];
            if (py >= 0 && py < kRows) {
                frame.addRect(boardX + static_cast<float>(px) * kCell + 1,
                              boardY + static_cast<float>(py) * kCell + 1,
                              kCell - 2, kCell - 2,
                              kPieceColors[m_curPiece + 1], 3);
            }
        }
    }

    // Sidebar: next piece preview
    const float sideX = boardX + boardW + 40.0f;
    frame.addRect(sideX, boardY, 200, 150, palette::kPanel, 8);
    frame.addText(sideX + 12, boardY + 10, 16, palette::kTextDim, TextAlign::Left, U"Tiếp theo");
    for (int i = 0; i < 4; ++i) {
        const int nx = kPieces[m_nextPiece][0][i][0];
        const int ny = kPieces[m_nextPiece][0][i][1];
        frame.addRect(sideX + 40 + static_cast<float>(nx) * 24.0f,
                      boardY + 55 + static_cast<float>(ny) * 24.0f, 22, 22,
                      kPieceColors[m_nextPiece + 1], 3);
    }
    frame.addText(sideX + 12, boardY + 170, 20, palette::kText, TextAlign::Left,
                  frame.internNumber(U"Hàng: ", m_linesCleared), true);
    frame.addText(sideX + 12, boardY + 200, 20, palette::kText, TextAlign::Left,
                  frame.internNumber(U"Cấp: ", m_level), true);
    frame.addText(sideX + 12, boardY + 230, 20, palette::kText, TextAlign::Left,
                  frame.internNumber(U"Điểm: ", m_score), true);

    frame.stats.title = U"🧱 Tetris";
    frame.stats.score = m_score;
    frame.stats.highScore = m_highScore;
    frame.stats.level = m_level;
    frame.stats.status = frame.internNumber(U"Hàng đã xoá: ", m_linesCleared);
    frame.stats.hint = U"A/D di chuyển · W xoay · S rơi nhanh · Space rơi thẳng · Esc thoát";
    frame.stats.paused = m_paused;
    frame.stats.gameOver = m_gameOver;
    if (m_gameOver) {
        frame.stats.banner = U"GAME OVER — nhấn R để chơi lại";
    } else if (m_paused) {
        frame.stats.banner = U"TẠM DỪNG";
    }
}

std::string TetrisGame::renderText() const {
    auto gridCopy = m_grid;
    if (!m_gameOver) {
        for (int i = 0; i < 4; ++i) {
            const int x = m_curX + kPieces[m_curPiece][m_curRot][i][0];
            const int y = m_curY + kPieces[m_curPiece][m_curRot][i][1];
            if (x >= 0 && x < kCols && y >= 0 && y < kRows) {
                gridCopy[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] =
                    static_cast<std::uint8_t>(m_curPiece + 1);
            }
        }
    }

    std::ostringstream ss;
    ss << "+----------+\n";
    for (int y = 0; y < kRows; ++y) {
        ss << "|";
        for (int x = 0; x < kCols; ++x) {
            ss << (gridCopy[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] ? "#" : " ");
        }
        ss << "|\n";
    }
    ss << "+----------+\n";
    ss << "Score: " << m_score << " | Level: " << m_level
       << " | Lines: " << m_linesCleared << " | Next: " << m_nextPiece;
    if (m_paused) ss << " [PAUSED]";
    if (m_gameOver) ss << " [GAME OVER - Press R]";
    ss << "\n";
    return ss.str();
}

//===========================================================================
// 3. Fishing
//===========================================================================
FishingGame::FishingGame() {
    // Pre-size every per-catch buffer: a fishing session must not allocate
    // between catches (see tests/test_arcade.cpp "NoAllocations").
    m_prompt.reserve(96);
    m_fishName.reserve(64);
    m_displayFish.reserve(64);
    reset();
}

void FishingGame::start() {
    reset();
    m_paused = false;
    m_gameOver = false;
    m_exitRequested = false;
    m_resultPending = false;
}

void FishingGame::reset() {
    m_score = 0;
    m_catches = 0;
    m_escapes = 0;
    m_runTimeSec = 0.0;
    m_paused = false;
    m_gameOver = false;
    m_resultPending = false;
    hookNewFish();
}

double FishingGame::rarityResistance() const noexcept {
    return 2.0 + static_cast<double>(static_cast<int>(m_currentRarity)) * 1.5;
}

double FishingGame::rarityTensionGain() const noexcept {
    return 3.0 + static_cast<double>(static_cast<int>(m_currentRarity)) * 1.6;
}

void FishingGame::hookNewFish() {
    const std::uint32_t roll = m_rng.below(100);
    const std::uint32_t baitBonus = (m_baitLevel - 1) * 3;

    struct FishDef {
        FishRarity rarity;
        const char* name;
        const char* prompt;
        double escapeSec;
    };
    // Prompts stay ASCII-lowercase on purpose: the player types with a
    // Vietnamese IME loaded, so accented targets would double-transform.
    static const FishDef kFish[] = {
        {FishRarity::Legendary, "Thần Ngư Sông Hồng",
         "than ngu khong lo xuat hien duoi dong nuoc sau", 24.0},
        {FishRarity::Epic, "Cá Rồng Hoàng Kim",
         "ca rong uon luon dep mat tren mat ho", 20.0},
        {FishRarity::Rare, "Cá Hồi Sa Pa", "ca hoi boi nguoc dong suoi lanh", 17.0},
        {FishRarity::Uncommon, "Cá Trắm Đen", "ca tram den can cau rat khoe", 15.0},
        {FishRarity::Common, "Cá Rô Đồng", "ca ro dong boi loi tung tang", 13.0},
    };

    // Roll thresholds follow the documented rarity ladder; bait shifts the roll.
    std::size_t index = 4;
    if (roll < 2u + baitBonus) {
        index = 0;
    } else if (roll < 10u + baitBonus) {
        index = 1;
    } else if (roll < 25u + baitBonus) {
        index = 2;
    } else if (roll < 55u) {
        index = 3;
    }

    const FishDef& def = kFish[index];
    m_currentRarity = def.rarity;
    m_fishName = def.name;
    m_displayFish.clear();
    for (const char* p = def.name; *p != '\0'; ++p) {
        // Fish names are UTF-8 literals; decode them once into UTF-32 for the
        // renderer instead of re-decoding (and allocating) every frame.
        const unsigned char c = static_cast<unsigned char>(*p);
        char32_t cp = c;
        if (c >= 0xC0) {
            int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : 1;
            cp = static_cast<char32_t>(c & (0x3F >> extra));
            for (int k = 0; k < extra && p[1] != '\0'; ++k) {
                ++p;
                cp = (cp << 6) | static_cast<char32_t>(static_cast<unsigned char>(*p) & 0x3F);
            }
        }
        m_displayFish.push_back(cp);
    }
    m_prompt.clear();
    for (const char* p = def.prompt; *p != '\0'; ++p) {
        m_prompt.push_back(static_cast<char32_t>(*p));
    }
    m_escapeTimer = def.escapeSec;
    m_promptIndex = 0;
    m_pullProgress = 20.0;
    m_lineTension = 30.0;
}

void FishingGame::onCatchSuccess() {
    ++m_catches;
    const std::int64_t basePts =
        (m_currentRarity == FishRarity::Legendary) ? 5000 :
        (m_currentRarity == FishRarity::Epic) ? 2000 :
        (m_currentRarity == FishRarity::Rare) ? 800 :
        (m_currentRarity == FishRarity::Uncommon) ? 300 : 100;
    // Rod level is the score multiplier (documented upgrade effect).
    const std::int64_t rodBonus = static_cast<std::int64_t>(m_rodLevel - 1) * 15;
    m_score += basePts + (basePts * rodBonus) / 100;
    if (m_score > m_highScore) {
        m_highScore = m_score;
    }
    hookNewFish();
}

void FishingGame::onFishEscape() {
    ++m_escapes;
    hookNewFish();
}

void FishingGame::update(double dt) {
    if (m_paused || m_gameOver) {
        return;
    }
    const double step = clampDt(dt);
    m_runTimeSec += step;

    m_escapeTimer -= step;
    if (m_escapeTimer <= 0.0) {
        onFishEscape();
        return;
    }

    // The fish pulls back; the line also loses tension when idle.
    m_pullProgress -= rarityResistance() * step;
    m_lineTension = std::max(0.0, m_lineTension - 14.0 * step);
    if (m_pullProgress <= 0.0) {
        onFishEscape();
        return;
    }

    if (m_autoMode == AutomationMode::Assisted) {
        m_pullProgress += 1.5 * step;              // gentle passive assist
        m_pullProgress = std::min(m_pullProgress, 95.0);   // ...never finishes alone
    } else if (m_autoMode == AutomationMode::Automated) {
        m_autoTimer += step;
        if (m_autoTimer >= 0.25) {
            m_autoTimer = 0.0;
            if (m_promptIndex < m_prompt.size()) {
                InputEvent ev{};
                ev.ch = m_prompt[m_promptIndex];
                ev.down = true;
                handleKey(ev);
            }
        }
    }
}

InputResult FishingGame::handleKey(const InputEvent& ev) {
    if (isExitKey(ev)) {
        m_exitRequested = true;
        m_resultPending = true;
        return InputResult::ExitRequested;
    }
    if (!ev.down) {
        return InputResult::Consumed;
    }
    if (isPauseKey(ev)) {
        m_paused = !m_paused;
        return InputResult::Consumed;
    }
    if (m_paused || m_gameOver) {
        return InputResult::Consumed;
    }

    if (m_promptIndex >= m_prompt.size()) {
        return InputResult::Consumed;
    }

    char32_t typed = ev.ch;
    if (typed >= U'A' && typed <= U'Z') {
        typed = static_cast<char32_t>(typed - U'A' + U'a');   // case-insensitive
    }
    if (typed != 0 && typed == m_prompt[m_promptIndex]) {
        ++m_promptIndex;
        const double pullDelta = 8.0 + static_cast<double>(m_reelLevel - 1) * 2.0;
        m_pullProgress += pullDelta;
        // Reeling in a big fish raises the tension: pull too fast and the line
        // snaps (this is what makes the rod/reel/bait upgrades matter).
        m_lineTension += rarityTensionGain();
        if (m_lineTension >= 100.0) {
            m_lineTension = 0.0;
            onFishEscape();
            return InputResult::Consumed;
        }
        if (m_pullProgress >= 100.0 || m_promptIndex == m_prompt.size()) {
            onCatchSuccess();
        }
        return InputResult::Consumed;
    }

    if (typed != 0) {
        m_pullProgress = std::max(0.0, m_pullProgress - 5.0);   // mistyped key
    }
    return InputResult::Consumed;
}

bool FishingGame::pollRunResult(RunResult& out) {
    if (!m_resultPending) {
        return false;
    }
    m_resultPending = false;
    out.type = GameType::Fishing;
    out.score = m_score;
    out.highScore = m_highScore;
    out.level = 1 + m_catches / 5;
    out.maxCombo = m_catches;
    out.wpm = 0.0;
    out.accuracy = (m_catches + m_escapes) > 0
                       ? 100.0 * static_cast<double>(m_catches) /
                             static_cast<double>(m_catches + m_escapes)
                       : 100.0;
    out.durationSec = m_runTimeSec;
    out.completed = false;
    return true;
}

void FishingGame::buildFrame(Frame& frame) const {
    frame.worldW = 1000.0f;
    frame.worldH = 620.0f;
    frame.background = rgba(0x0A, 0x1C, 0x2E);
    frame.backgroundTop = rgba(0x07, 0x2B, 0x46);

    // Water surface + underwater bubbles
    frame.addRect(0, 0, frame.worldW, 300, rgba(0x0B, 0x3A, 0x5C), 0);
    for (int i = 0; i < 14; ++i) {
        const float bx = 40.0f + static_cast<float>(i) * 68.0f;
        const float by = 240.0f - static_cast<float>((i * 37) % 120);
        frame.addCircle(bx, by, 3.0f + static_cast<float>(i % 3), rgba(0x7F, 0xD8, 0xFF, 0x55));
    }

    // Boat + rod
    frame.addRect(120, 180, 150, 26, rgba(0x8B, 0x5A, 0x2B), 8);
    frame.addLine(200, 180, 470, 120, rgba(0xD9, 0xC7, 0xA8), 3);

    // The fish (size and color scale with rarity)
    const float rarityScale = 1.0f + static_cast<float>(static_cast<int>(m_currentRarity)) * 0.22f;
    const float fishX = 520.0f;
    const float fishY = 330.0f;
    const float fishR = 26.0f * rarityScale;
    const Color fishColor = rarityColor(m_currentRarity);
    frame.addCircle(fishX, fishY, fishR, fishColor);
    PolyShape tail{};
    tail.count = 3;
    tail.xs[0] = fishX - fishR;
    tail.ys[0] = fishY;
    tail.xs[1] = fishX - fishR * 2.1f;
    tail.ys[1] = fishY - fishR * 0.8f;
    tail.xs[2] = fishX - fishR * 2.1f;
    tail.ys[2] = fishY + fishR * 0.8f;
    tail.fill = fishColor;
    frame.addPoly(tail);
    frame.addCircle(fishX + fishR * 0.5f, fishY - fishR * 0.25f, 3.5f, rgba(0x10, 0x10, 0x10));

    // Fishing line down to the fish, colored by tension
    const Color lineColor = (m_lineTension > 75.0) ? palette::kBad
                          : (m_lineTension > 45.0) ? palette::kWarn : rgba(0xD9, 0xC7, 0xA8);
    frame.addLine(470, 120, fishX - fishR, fishY, lineColor, 2 + static_cast<float>(m_lineTension) * 0.02f);

    // Pull progress bar
    frame.addRect(150, 400, 700, 26, palette::kPanel, 13);
    frame.addRect(150, 400, 700.0f * static_cast<float>(std::clamp(m_pullProgress / 100.0, 0.0, 1.0)),
                  26, palette::kAccent, 13);
    frame.addText(150, 372, 18, palette::kTextDim, TextAlign::Left, U"Tiến độ kéo cá");
    frame.addText(850, 372, 18, palette::kText, TextAlign::Right,
                  frame.internDouble(U"", m_pullProgress, 0, U"%"));

    // Tension bar
    const Color tensionColor = (m_lineTension > 75.0) ? palette::kBad : palette::kGood;
    frame.addRect(150, 450, 700, 16, palette::kPanel, 8);
    frame.addRect(150, 450, 700.0f * static_cast<float>(std::clamp(m_lineTension / 100.0, 0.0, 1.0)),
                  16, tensionColor, 8);
    frame.addText(150, 426, 16, palette::kTextDim, TextAlign::Left, U"Độ căng dây (quá căng → đứt)");

    // Prompt line with per-character state
    PassageStyle style{};
    style.size = 30.0f;
    style.caret = palette::kAccent;
    addStyledLine(frame, 150, 505, m_prompt, m_promptIndex, m_prompt.size(), 0, style);

    frame.addText(150, 565, 22, palette::kTextDim, TextAlign::Left,
                  frame.intern(m_displayFish));
    frame.addText(850, 565, 22, fishColor, TextAlign::Right,
                  frame.internAscii(rarityName(m_currentRarity), U"[", U"]"));

    frame.stats.title = U"🎣 Câu cá";
    frame.stats.score = m_score;
    frame.stats.highScore = m_highScore;
    frame.stats.level = 1 + m_catches / 5;
    frame.stats.status = frame.internDouble(U"Thoát sau ", m_escapeTimer, 1, U"s");
    frame.stats.hint = U"Gõ đúng câu mồi · P tạm dừng · Esc thoát";
    frame.stats.hasMeter = true;
    frame.stats.meter = m_lineTension;
    frame.stats.meterMax = 100.0;
    frame.stats.progress = m_pullProgress / 100.0;
    frame.stats.paused = m_paused;
    if (m_paused) {
        frame.stats.banner = U"TẠM DỪNG";
    }
}

std::string FishingGame::renderText() const {
    std::ostringstream ss;
    ss << "=== KIEEKEY FISHING ===\n";
    ss << "Fish: " << m_fishName << " [Rarity: " << rarityName(m_currentRarity) << "]\n";
    ss << "Target: ";
    for (std::size_t i = 0; i < m_prompt.size(); ++i) {
        ss << (i < m_promptIndex ? '*' : static_cast<char>(m_prompt[i]));
    }
    ss << "\nReel Progress: " << static_cast<int>(m_pullProgress) << "%\n";
    ss << "Line Tension: " << static_cast<int>(m_lineTension) << "%\n";
    ss << "Escape in: " << m_escapeTimer << "s\n";
    ss << "Catches: " << m_catches << " | Escapes: " << m_escapes
       << " | Score: " << m_score << "\n";
    return ss.str();
}

//===========================================================================
// 4. Typing Race
//===========================================================================
TypingRaceGame::TypingRaceGame() {
    m_passage = U"KieeKey la bo go tieng Viet hien dai toi uu do tre va toc do go phim";
    reset();
}

void TypingRaceGame::setPassage(std::u32string_view passage) {
    if (passage.empty()) {
        return;
    }
    m_passage.assign(passage.begin(), passage.end());
    reset();
}

void TypingRaceGame::start() {
    reset();
    m_paused = false;
    m_finished = false;
    m_exitRequested = false;
    m_resultPending = false;
}

void TypingRaceGame::reset() {
    m_charIndex = 0;
    m_totalKeys = 0;
    m_correctKeys = 0;
    m_errorKeys = 0;
    m_elapsedSec = 0.0;
    m_liveWpm = 0.0;
    m_accuracy = 100.0;
    m_pacerProgress = 0.0;
    m_score = 0;
    m_paused = false;
    m_finished = false;
    m_resultPending = false;
}

double TypingRaceGame::getProgressPercent() const noexcept {
    if (m_passage.empty()) {
        return 100.0;
    }
    return (static_cast<double>(m_charIndex) / static_cast<double>(m_passage.size())) * 100.0;
}

void TypingRaceGame::update(double dt) {
    if (m_paused || m_finished) {
        return;
    }
    const double step = clampDt(dt);
    m_elapsedSec += step;
    if (m_elapsedSec > 0.0) {
        const double minutes = m_elapsedSec / 60.0;
        m_liveWpm = (static_cast<double>(m_correctKeys) / 5.0) / minutes;
    }
    if (m_totalKeys > 0) {
        m_accuracy = (static_cast<double>(m_correctKeys) / static_cast<double>(m_totalKeys)) * 100.0;
    }
    // Pacer car drives at a constant WPM along the same track.
    if (m_pacerWpm > 0.0) {
        const double charsPerSec = (m_pacerWpm * 5.0) / 60.0;
        const double chars = charsPerSec * m_elapsedSec;
        m_pacerProgress = std::clamp(chars / static_cast<double>(std::max<std::size_t>(1, m_passage.size())),
                                     0.0, 1.0);
    }
}

void TypingRaceGame::finish() {
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_resultPending = true;
    // Recompute the final WPM from the *actual* finish time: the old code read
    // the value cached by the previous update() tick, so finishing inside one
    // tick (or on the very first frame) scored literally 0.
    if (m_elapsedSec > 0.0001) {
        m_liveWpm = (static_cast<double>(m_correctKeys) / 5.0) / (m_elapsedSec / 60.0);
    } else {
        m_liveWpm = 0.0;
    }
    if (m_pacerWpm > 0.0) {
        m_pacerProgress = std::min(1.0, (m_pacerWpm * 5.0 / 60.0) * m_elapsedSec /
                                            static_cast<double>(std::max<std::size_t>(1, m_passage.size())));
    }
    m_score = static_cast<std::int64_t>(m_liveWpm * (m_accuracy / 100.0) * 100.0);
    if (m_score > m_highScore) {
        m_highScore = m_score;
    }
}

InputResult TypingRaceGame::handleKey(const InputEvent& ev) {
    if (isExitKey(ev)) {
        m_exitRequested = true;
        m_resultPending = true;
        return InputResult::ExitRequested;
    }
    if (!ev.down) {
        return InputResult::Consumed;
    }
    if (isPauseKey(ev)) {
        m_paused = !m_paused;
        return InputResult::Consumed;
    }
    if (isRestartKey(ev)) {
        reset();
        return InputResult::Consumed;
    }
    if (m_paused || m_finished) {
        return InputResult::Consumed;
    }

    if (ev.ch == U'\0') {
        return InputResult::Consumed;
    }
    if (ev.ch == U'\b') {   // forgiving backspace: step back one character
        if (m_charIndex > 0) {
            --m_charIndex;
        }
        return InputResult::Consumed;
    }

    ++m_totalKeys;
    if (m_charIndex < m_passage.size()) {
        if (ev.ch == m_passage[m_charIndex]) {
            ++m_charIndex;
            ++m_correctKeys;
            if (m_charIndex == m_passage.size()) {
                finish();
            }
        } else {
            ++m_errorKeys;
        }
    }
    return InputResult::Consumed;
}

bool TypingRaceGame::pollRunResult(RunResult& out) {
    if (!m_resultPending) {
        return false;
    }
    m_resultPending = false;
    out.type = GameType::TypingRace;
    out.score = m_score;
    out.highScore = m_highScore;
    out.level = 1;
    out.maxCombo = 0;
    out.wpm = m_liveWpm;
    out.accuracy = m_accuracy;
    out.durationSec = m_elapsedSec;
    out.completed = m_finished;
    return true;
}

void TypingRaceGame::buildFrame(Frame& frame) const {
    frame.worldW = 1000.0f;
    frame.worldH = 620.0f;
    frame.background = rgba(0x12, 0x16, 0x20);
    frame.backgroundTop = rgba(0x1B, 0x2A, 0x44);

    constexpr float kTrackX = 80.0f;
    constexpr float kTrackW = 840.0f;
    constexpr float kTrackY = 150.0f;

    // Track
    frame.addRect(kTrackX, kTrackY, kTrackW, 90, rgba(0x2A, 0x2F, 0x3A), 12);
    for (int i = 0; i <= 10; ++i) {
        const float x = kTrackX + kTrackW * static_cast<float>(i) / 10.0f;
        frame.addLine(x, kTrackY - 8, x, kTrackY + 98, rgba(0x4A, 0x52, 0x60), 1);
    }
    frame.addLine(kTrackX + kTrackW, kTrackY - 20, kTrackX + kTrackW, kTrackY + 110,
                  palette::kWarn, 4);

    // Pacer car (top lane) + player car (bottom lane)
    if (m_pacerWpm > 0.0) {
        const float px = kTrackX + kTrackW * static_cast<float>(m_pacerProgress);
        frame.addRect(px - 44, kTrackY + 6, 88, 34, rgba(0x9A, 0xA7, 0xB8), 8);
        frame.addRect(px - 30, kTrackY + 12, 60, 14, rgba(0x40, 0x48, 0x58), 4);
        frame.addCircle(px - 26, kTrackY + 40, 7, rgba(0x20, 0x24, 0x2C));
        frame.addCircle(px + 26, kTrackY + 40, 7, rgba(0x20, 0x24, 0x2C));
    }

    const float progress = static_cast<float>(getProgressPercent() / 100.0);
    const float carX = kTrackX + kTrackW * progress;
    frame.addRect(carX - 50, kTrackY + 48, 100, 38, palette::kAccent, 10);
    frame.addRect(carX - 32, kTrackY + 56, 64, 16, rgba(0x10, 0x2A, 0x3A), 6);
    frame.addCircle(carX - 30, kTrackY + 88, 8, rgba(0x18, 0x1C, 0x24));
    frame.addCircle(carX + 30, kTrackY + 88, 8, rgba(0x18, 0x1C, 0x24));

    // Live stats panel
    frame.addRect(80, 268, 840, 60, palette::kPanel, 12);
    frame.addText(100, 282, 22, palette::kText, TextAlign::Left,
                  frame.internDouble(U"WPM: ", m_liveWpm, 0), true);
    frame.addText(320, 282, 22, palette::kText, TextAlign::Left,
                  frame.internDouble(U"Chính xác: ", m_accuracy, 0, U"%"));
    frame.addText(620, 282, 22, palette::kText, TextAlign::Left,
                  frame.internDouble(U"Thời gian: ", m_elapsedSec, 1, U"s"));
    frame.addText(880, 282, 22, palette::kTextDim, TextAlign::Right,
                  frame.internNumber(U"Lỗi: ", static_cast<std::int64_t>(m_errorKeys)));

    // Passage with per-character state
    const float advance = 30.0f * kMonoCharWidthFactor;
    const float scroll = caretScroll(m_charIndex, advance, kTrackW);
    PassageStyle style{};
    style.size = 30.0f;
    style.caret = palette::kAccent2;
    frame.addRect(kTrackX, 372, kTrackW, 60, palette::kPanelAlt, 10);
    addStyledLine(frame, kTrackX + 12 - scroll, 384, m_passage, m_charIndex,
                  m_passage.size(), 0, style);

    frame.stats.title = U"🏎️ Đua xe theo tốc độ gõ";
    frame.stats.score = m_score;
    frame.stats.highScore = m_highScore;
    frame.stats.wpm = m_liveWpm;
    frame.stats.accuracy = m_accuracy;
    frame.stats.progress = progress;
    frame.stats.status = frame.internNumber(
        U"Đã gõ: ", static_cast<std::int64_t>(m_charIndex), U" ký tự");
    frame.stats.hint = U"Gõ đúng đoạn văn để xe chạy · P tạm dừng · Esc thoát";
    frame.stats.finished = m_finished;
    frame.stats.gameOver = m_finished;
    frame.stats.paused = m_paused;
    if (m_finished) {
        frame.stats.banner = U"VỀ ĐÍCH! (điểm game ≠ benchmark chính thức)";
    } else if (m_paused) {
        frame.stats.banner = U"TẠM DỪNG";
    }
}

std::string TypingRaceGame::renderText() const {
    std::ostringstream ss;
    ss << "=== TYPING RACE === (Notice: Game score != official benchmark)\n";
    ss << "Passage: ";
    for (std::size_t i = 0; i < m_passage.size(); ++i) {
        ss << (i < m_charIndex ? '*' : static_cast<char>(m_passage[i]));
    }
    ss << "\nSpeed: " << static_cast<int>(m_liveWpm) << " WPM | Accuracy: "
       << static_cast<int>(m_accuracy) << "% | Time: " << m_elapsedSec << "s\n";
    if (m_finished) ss << "[FINISH LINE REACHED! Score: " << m_score << "]\n";
    return ss.str();
}

//===========================================================================
// 5. WASD + Typing Racing
//===========================================================================
WasdRaceGame::WasdRaceGame() {
    m_passage = U"lai xe vuot chuong ngai vat toc do cao";
    m_obstacles.reserve(8);
    reset();
}

void WasdRaceGame::start() {
    reset();
    m_paused = false;
    m_gameOver = false;
    m_exitRequested = false;
    m_resultPending = false;
}

void WasdRaceGame::reset() {
    m_textIndex = 0;
    m_playerLane = 1;
    m_carSpeed = 60.0;
    m_fuel = m_startFuel;
    m_distance = 0.0;
    m_obstacles.clear();
    m_spawnTimer = 0.0;
    m_dodges = 0;
    m_collisions = 0;
    m_elapsedSec = 0.0;
    m_score = 0;
    m_paused = false;
    m_gameOver = false;
    m_resultPending = false;
}

void WasdRaceGame::spawnObstacle() {
    if (m_obstacles.size() >= 8) {
        return;
    }
    // Never spawn an unavoidable wall: at least one lane stays free, and at
    // most two obstacles may share the same "row".
    const int lane = static_cast<int>(m_rng.below(kLanes));
    std::array<bool, kLanes> used{};
    used[static_cast<std::size_t>(lane)] = true;
    for (const auto& ob : m_obstacles) {
        if (ob.dist > kRoadLength - 25.0) {
            used[static_cast<std::size_t>(ob.lane)] = true;
        }
    }
    int freeLanes = 0;
    for (bool u : used) {
        if (!u) ++freeLanes;
    }
    if (freeLanes == 0) {
        return;
    }
    Obstacle ob{};
    ob.lane = lane;
    ob.dist = kRoadLength;
    m_obstacles.push_back(ob);
}

void WasdRaceGame::update(double dt) {
    if (m_paused || m_gameOver) {
        return;
    }
    const double step = clampDt(dt);
    m_elapsedSec += step;

    // Fuel burns with speed; coasting is not free.
    m_fuel = std::max(0.0, m_fuel - (m_carSpeed / 60.0) * 3.2 * step);
    m_carSpeed = std::max(20.0, m_carSpeed - 4.0 * step);
    if (m_fuel <= 0.0) {
        m_gameOver = true;
        m_resultPending = true;
        if (m_score > m_highScore) {
            m_highScore = m_score;
        }
        return;
    }

    const double moveDist = (m_carSpeed * 0.32) * step;   // world units per second
    m_distance += moveDist;

    for (auto& ob : m_obstacles) {
        ob.dist -= moveDist;
        if (ob.dist <= 0.0 && !ob.hit) {
            ob.hit = true;
            if (ob.lane == m_playerLane) {
                ++m_collisions;
                m_fuel = std::max(0.0, m_fuel - 25.0);
                m_carSpeed = std::max(20.0, m_carSpeed - 20.0);
                m_score = std::max<int64_t>(0, m_score - 120);
                if (m_fuel <= 0.0) {
                    m_gameOver = true;
                    m_resultPending = true;
                    if (m_score > m_highScore) {
                        m_highScore = m_score;
                    }
                    return;
                }
            } else {
                ++m_dodges;
                m_score += 50;
            }
        }
    }
    m_obstacles.erase(std::remove_if(m_obstacles.begin(), m_obstacles.end(),
                                     [](const Obstacle& o) { return o.hit && o.dist <= 0.0; }),
                      m_obstacles.end());

    // Spawn cadence shrinks as the car speeds up (reaction window stays fair
    // because obstacles need `kRoadLength / speed` seconds to arrive).
    m_spawnTimer += step;
    const double spacing = std::max(1.1, m_obstacleSpacingSec - (m_carSpeed - 60.0) * 0.012);
    if (m_spawnTimer >= spacing) {
        m_spawnTimer = 0.0;
        const double arrivalTime = kRoadLength / std::max(1.0, m_carSpeed * 0.32);
        if (arrivalTime >= 0.45) {
            spawnObstacle();
        }
    }
}

InputResult WasdRaceGame::handleKey(const InputEvent& ev) {
    if (isExitKey(ev)) {
        m_exitRequested = true;
        m_resultPending = true;
        return InputResult::ExitRequested;
    }
    if (!ev.down) {
        return InputResult::Consumed;
    }
    if (isPauseKey(ev)) {   // F1 / Pause — never a letter: the passage is typed
        m_paused = !m_paused;
        return InputResult::Consumed;
    }
    if (isRestartKey(ev)) {   // F2
        reset();
        return InputResult::Consumed;
    }
    if (m_paused || m_gameOver) {
        return InputResult::Consumed;
    }

    // WASD steering
    if (ev.ch == U'a' || ev.ch == U'A' || ev.vk == vk::kLeft) {
        if (m_playerLane > 0) --m_playerLane;
        return InputResult::Consumed;
    }
    if (ev.ch == U'd' || ev.ch == U'D' || ev.vk == vk::kRight) {
        if (m_playerLane < kLanes - 1) ++m_playerLane;
        return InputResult::Consumed;
    }
    if (ev.ch == U'w' || ev.ch == U'W' || ev.vk == vk::kUp) {
        m_carSpeed = std::min(150.0, m_carSpeed + 12.0);
        return InputResult::Consumed;
    }
    if (ev.ch == U's' || ev.ch == U'S' || ev.vk == vk::kDown) {
        m_carSpeed = std::max(30.0, m_carSpeed - 12.0);
        return InputResult::Consumed;
    }

    // Typing fuels the engine
    if (ev.ch == U'\b') {
        return InputResult::Consumed;
    }
    if (m_textIndex < m_passage.size() && ev.ch == m_passage[m_textIndex]) {
        ++m_textIndex;
        m_score += 20;
        m_fuel = std::min(100.0, m_fuel + 5.0);
        if (m_textIndex == m_passage.size()) {
            m_textIndex = 0;   // loop the passage so the game keeps going
        }
    }
    return InputResult::Consumed;   // the road game owns every key while open
}

bool WasdRaceGame::pollRunResult(RunResult& out) {
    if (!m_resultPending) {
        return false;
    }
    m_resultPending = false;
    out.type = GameType::WasdRace;
    out.score = m_score;
    out.highScore = m_highScore;
    out.level = 1 + static_cast<std::uint32_t>(m_distance / 500.0);
    out.maxCombo = m_dodges;
    out.wpm = 0.0;
    out.accuracy = 100.0;
    out.durationSec = m_elapsedSec;
    out.completed = false;
    return true;
}

void WasdRaceGame::buildFrame(Frame& frame) const {
    frame.worldW = 900.0f;
    frame.worldH = 680.0f;
    frame.background = rgba(0x10, 0x14, 0x1C);
    frame.backgroundTop = rgba(0x1A, 0x22, 0x30);

    constexpr float kRoadX = 250.0f;
    constexpr float kRoadW = 400.0f;
    constexpr float kRoadTop = 60.0f;
    constexpr float kRoadH = 480.0f;
    constexpr float kLaneW = kRoadW / kLanes;

    frame.addRect(kRoadX, kRoadTop, kRoadW, kRoadH, rgba(0x25, 0x2A, 0x33), 8);
    for (int i = 1; i < kLanes; ++i) {
        const float x = kRoadX + kLaneW * static_cast<float>(i);
        for (float y = kRoadTop + 10; y < kRoadTop + kRoadH - 20; y += 40) {
            frame.addRect(x - 2, y, 4, 20, rgba(0x6B, 0x74, 0x84), 2);
        }
    }

    auto laneCenterX = [&](int lane) {
        return kRoadX + kLaneW * (static_cast<float>(lane) + 0.5f);
    };
    auto worldToScreenY = [&](double dist) {
        const double t = 1.0 - std::clamp(dist / kRoadLength, 0.0, 1.0);
        return kRoadTop + static_cast<float>(t) * (kRoadH - 120.0f);
    };

    for (const auto& ob : m_obstacles) {
        if (ob.hit && ob.dist <= 0.0) {
            continue;
        }
        const float y = worldToScreenY(ob.dist);
        const float x = laneCenterX(ob.lane);
        const float scale = 0.55f + 0.45f * static_cast<float>(1.0 - std::clamp(ob.dist / kRoadLength, 0.0, 1.0));
        frame.addRect(x - 34 * scale, y, 68 * scale, 46 * scale, rgba(0xD0, 0x4A, 0x4A), 6);
        frame.addRect(x - 22 * scale, y + 8 * scale, 44 * scale, 20 * scale, rgba(0x30, 0x12, 0x12), 4);
    }

    // Player car
    const float px = laneCenterX(m_playerLane);
    const float py = kRoadTop + kRoadH - 120.0f;
    frame.addRect(px - 38, py, 76, 96, (m_fuel < 25.0) ? palette::kWarn : palette::kAccent, 10);
    frame.addRect(px - 26, py + 14, 52, 34, rgba(0x10, 0x28, 0x3A), 6);

    // Speedometer + fuel gauge on the right
    frame.addRect(690, 90, 170, 26, palette::kPanel, 13);
    frame.addRect(690, 90, 170.0f * static_cast<float>(std::clamp(m_carSpeed / 150.0, 0.0, 1.0)),
                  26, palette::kAccent2, 13);
    frame.addText(690, 62, 18, palette::kTextDim, TextAlign::Left,
                  frame.internDouble(U"Tốc độ ", m_carSpeed, 0, U" km/h"));
    frame.addRect(690, 160, 170, 26, palette::kPanel, 13);
    frame.addRect(690, 160, 170.0f * static_cast<float>(std::clamp(m_fuel / 100.0, 0.0, 1.0)),
                  26, (m_fuel < 25.0) ? palette::kBad : palette::kGood, 13);
    frame.addText(690, 132, 18, palette::kTextDim, TextAlign::Left,
                  frame.internDouble(U"Nhiên liệu ", m_fuel, 0, U"%"));
    frame.addText(690, 210, 18, palette::kText, TextAlign::Left,
                  frame.internNumber(U"Đã né: ", static_cast<std::int64_t>(m_dodges)));
    frame.addText(690, 236, 18, palette::kText, TextAlign::Left,
                  frame.internNumber(U"Va chạm: ", static_cast<std::int64_t>(m_collisions)));

    // Passage prompt at the bottom
    PassageStyle style{};
    style.size = 28.0f;
    style.caret = palette::kAccent2;
    frame.addRect(60, 570, 780, 56, palette::kPanelAlt, 10);
    addStyledLine(frame, 76, 582, m_passage, m_textIndex, m_passage.size(), 0, style);

    frame.stats.title = U"🛣️ Đua xe + gõ phím";
    frame.stats.score = m_score;
    frame.stats.highScore = m_highScore;
    frame.stats.level = 1 + static_cast<std::uint32_t>(m_distance / 500.0);
    frame.stats.status = frame.internDouble(U"Quãng đường ", m_distance, 0, U" m");
    frame.stats.hint = U"A/D đổi làn · W tăng tốc · S phanh · Gõ để nạp nhiên liệu · Esc thoát";
    frame.stats.hasMeter = true;
    frame.stats.meter = m_fuel;
    frame.stats.meterMax = 100.0;
    frame.stats.paused = m_paused;
    frame.stats.gameOver = m_gameOver;
    if (m_gameOver) {
        frame.stats.banner = U"HẾT NHIÊN LIỆU — nhấn R để chơi lại";
    } else if (m_paused) {
        frame.stats.banner = U"TẠM DỪNG";
    }
}

std::string WasdRaceGame::renderText() const {
    std::ostringstream ss;
    ss << "=== WASD + TYPING RACING ===\n";
    ss << "Lanes: | ";
    for (int l = 0; l < kLanes; ++l) {
        ss << (l == m_playerLane ? "[CAR] " : "  .   ");
    }
    ss << "|\n";
    ss << "Speed: " << static_cast<int>(m_carSpeed) << " km/h | Fuel/HP: "
       << static_cast<int>(m_fuel) << "% | Dodges: " << m_dodges
       << " | Collisions: " << m_collisions
       << " | Obstacles: " << m_obstacles.size() << "\n";
    return ss.str();
}

//===========================================================================
// 6. Rhythm Typing (FNF-style)
//===========================================================================
RhythmTypingGame::RhythmTypingGame() {
    m_failReason.reserve(64);
    generateChart();
    reset();
}

void RhythmTypingGame::setBpm(double bpm) noexcept {
    m_bpm = std::clamp(bpm, 40.0, 300.0);
    m_beatSec = 60.0 / m_bpm;
    generateChart();
    reset();
}

void RhythmTypingGame::setNoteCount(std::uint32_t count) noexcept {
    m_noteCount = std::clamp(count, 8u, 512u);
    generateChart();
    reset();
}

void RhythmTypingGame::setApproachSec(double sec) noexcept {
    m_approachSec = std::clamp(sec, 0.6, 4.0);
}

void RhythmTypingGame::setSeed(std::uint32_t seed) {
    m_seed = seed;
    m_rng.reseed(seed);
    generateChart();
    reset();
}

void RhythmTypingGame::generateChart() {
    m_notes.clear();
    static const char32_t kLaneKeys[kLaneCount] = {U'd', U'f', U'j', U'k'};
    m_notes.reserve(m_noteCount);
    // Two beats of lead-in so the first note is never a surprise.
    const double start = m_beatSec * 2.0;
    for (std::uint32_t i = 0; i < m_noteCount; ++i) {
        RhythmNote note{};
        // 90 % of the notes sit on the beat, 10 % on the off-beat (chords on
        // the beat create the FNF-style "dense" feel without unfair timing).
        const bool offBeat = (m_rng.below(10) == 0);
        const double beatIndex = static_cast<double>(i) + (offBeat ? 0.5 : 0.0);
        note.targetTimeSec = start + beatIndex * m_beatSec;
        note.lane = static_cast<std::uint8_t>(m_rng.below(kLaneCount));
        note.ch = kLaneKeys[note.lane];
        note.state = NoteState::Pending;
        note.rating = HitRating::Perfect;
        m_notes.push_back(note);
    }
    std::sort(m_notes.begin(), m_notes.end(),
              [](const RhythmNote& a, const RhythmNote& b) {
                  return a.targetTimeSec < b.targetTimeSec;
              });
}

void RhythmTypingGame::start() {
    reset();
    m_paused = false;
    m_gameOver = false;
    m_exitRequested = false;
    m_resultPending = false;
}

void RhythmTypingGame::reset() {
    for (auto& note : m_notes) {
        note.state = NoteState::Pending;
        note.rating = HitRating::Perfect;
    }
    m_songTime = 0.0;
    m_combo = 0;
    m_maxCombo = 0;
    m_lastRating = HitRating::Perfect;
    m_health = 100.0;
    m_judged = 0;
    m_perfects = 0;
    m_misses = 0;
    m_ratingFlashSec = 0.0;
    m_failReason.clear();
    m_score = 0;
    m_paused = false;
    m_gameOver = false;
    m_finished = false;
    m_resultPending = false;
}

int RhythmTypingGame::laneForKey(char32_t ch) const noexcept {
    switch (ch) {
        case U'd': case U'D': return 0;
        case U'f': case U'F': return 1;
        case U'j': case U'J': return 2;
        case U'k': case U'K': return 3;
        default: return -1;
    }
}

void RhythmTypingGame::registerOutcome(HitRating rating, bool fromExtraKey) {
    m_lastRating = rating;
    m_ratingFlashSec = 0.5;
    ++m_judged;

    switch (rating) {
        case HitRating::Perfect:
            ++m_perfects;
            ++m_combo;
            m_maxCombo = std::max(m_maxCombo, m_combo);
            m_score += 300 * (1 + static_cast<int64_t>(m_combo / 10));
            m_health = std::min(100.0, m_health + 1.5);
            return;
        case HitRating::Good:
            ++m_combo;
            m_maxCombo = std::max(m_maxCombo, m_combo);
            m_score += 150 * (1 + static_cast<int64_t>(m_combo / 10));
            m_health = std::min(100.0, m_health + 0.5);
            return;
        case HitRating::TooEarly:
        case HitRating::TooLate:
        case HitRating::Miss:
        default:
            break;
    }

    // A bad input. In Hardcore (the default) this ends the run immediately —
    // that is the documented "nhanh quá chết, chậm quá cũng chết" contract.
    ++m_misses;
    m_combo = 0;
    if (m_failMode == FailMode::Hardcore) {
        m_gameOver = true;
        m_finished = false;
        m_resultPending = true;
        if (m_score > m_highScore) {
            m_highScore = m_score;
        }
        m_failReason = fromExtraKey ? "Bấm thừa phím (không có nốt)" :
                       (rating == HitRating::TooEarly) ? "Quá nhanh (Too Early)" :
                       (rating == HitRating::TooLate) ? "Quá chậm (Too Late)" :
                                                        "Bỏ lỡ nốt (Miss)";
        return;
    }

    m_health -= (rating == HitRating::Miss) ? 20.0 : 12.0;
    if (m_health <= 0.0) {
        m_health = 0.0;
        m_gameOver = true;
        m_finished = false;
        m_resultPending = true;
        if (m_score > m_highScore) {
            m_highScore = m_score;
        }
        m_failReason = "Hết thanh máu";
    }
}

void RhythmTypingGame::judge(int lane) {
    // Find the *nearest* pending note in that lane (the old code consumed the
    // first pending note in the vector regardless of distance, so a note two
    // seconds away could swallow an input meant for the current one).
    RhythmNote* best = nullptr;
    double bestAbs = 1e9;
    for (auto& note : m_notes) {
        if (note.state != NoteState::Pending || static_cast<int>(note.lane) != lane) {
            continue;
        }
        const double delta = std::abs(m_songTime - note.targetTimeSec);
        if (delta < bestAbs) {
            bestAbs = delta;
            best = &note;
        }
    }
    if (best == nullptr || bestAbs > kLateWindowSec) {
        registerOutcome(HitRating::Miss, true);   // extra key press
        return;
    }

    const double delta = m_songTime - best->targetTimeSec;
    HitRating rating;
    if (std::abs(delta) <= kPerfectWindowSec) {
        rating = HitRating::Perfect;
    } else if (std::abs(delta) <= kGoodWindowSec) {
        rating = HitRating::Good;
    } else if (delta < 0.0) {
        rating = HitRating::TooEarly;
    } else {
        rating = HitRating::TooLate;
    }

    best->state = (rating == HitRating::Perfect || rating == HitRating::Good)
                      ? NoteState::Hit
                      : NoteState::Missed;
    best->rating = rating;
    registerOutcome(rating, false);
}

void RhythmTypingGame::update(double dt) {
    if (m_paused || m_gameOver) {
        return;
    }
    const double step = clampDt(dt);
    m_songTime += step;
    m_ratingFlashSec = std::max(0.0, m_ratingFlashSec - step);

    for (auto& note : m_notes) {
        if (note.state == NoteState::Pending && m_songTime > note.targetTimeSec + kLateWindowSec) {
            note.state = NoteState::Missed;
            note.rating = HitRating::Miss;
            registerOutcome(HitRating::Miss, false);
            if (m_gameOver) {
                return;
            }
        }
    }

    if (!m_notes.empty() && m_songTime > m_notes.back().targetTimeSec + kLateWindowSec + 0.2) {
        // Every note judged: the chart is cleared.
        bool allJudged = true;
        for (const auto& note : m_notes) {
            if (note.state == NoteState::Pending) {
                allJudged = false;
                break;
            }
        }
        if (allJudged) {
            m_finished = true;
            m_gameOver = true;
            m_resultPending = true;
            if (m_score > m_highScore) {
                m_highScore = m_score;
            }
        }
    }
}

InputResult RhythmTypingGame::handleKey(const InputEvent& ev) {
    if (isExitKey(ev)) {
        m_exitRequested = true;
        m_resultPending = true;
        return InputResult::ExitRequested;
    }
    if (!ev.down) {
        return InputResult::Consumed;
    }
    if (isPauseKey(ev)) {
        m_paused = !m_paused;
        return InputResult::Consumed;
    }
    if (isRestartKey(ev)) {
        reset();
        return InputResult::Consumed;
    }
    if (m_paused || m_gameOver) {
        return InputResult::Consumed;
    }

    const int lane = laneForKey(ev.ch);
    if (lane < 0) {
        return InputResult::Consumed;   // lane keys only; other keys are ignored
    }
    judge(lane);
    return InputResult::Consumed;
}

bool RhythmTypingGame::pollRunResult(RunResult& out) {
    if (!m_resultPending) {
        return false;
    }
    m_resultPending = false;
    out.type = GameType::Rhythm;
    out.score = m_score;
    out.highScore = m_highScore;
    out.level = 1 + static_cast<std::uint32_t>(m_bpm / 40.0);
    out.maxCombo = m_maxCombo;
    out.wpm = m_judged > 0 ? (static_cast<double>(m_judged) / 5.0) /
                                 std::max(0.001, m_songTime / 60.0)
                           : 0.0;
    out.accuracy = m_judged > 0
                       ? 100.0 * static_cast<double>(m_perfects) / static_cast<double>(m_judged)
                       : 100.0;
    out.durationSec = m_songTime;
    out.completed = m_finished;
    return true;
}

void RhythmTypingGame::buildFrame(Frame& frame) const {
    frame.worldW = 900.0f;
    frame.worldH = 680.0f;
    frame.background = rgba(0x14, 0x0E, 0x22);
    frame.backgroundTop = rgba(0x2A, 0x12, 0x3E);

    constexpr float kLaneW = 110.0f;
    constexpr float kHighwayX = (900.0f - kLaneW * kLaneCount) * 0.5f;
    constexpr float kHitLineY = 560.0f;
    constexpr float kPixelsPerSecond = 340.0f;

    // Highway
    frame.addRect(kHighwayX - 6, 60, kLaneW * kLaneCount + 12, kHitLineY - 40,
                  rgba(0x1E, 0x16, 0x2E), 10);
    static const Color kLaneColors[kLaneCount] = {
        rgba(0x6C, 0x9C, 0xFF), rgba(0x51, 0xE8, 0x8A),
        rgba(0xFF, 0xD1, 0x4A), rgba(0xFF, 0x6B, 0xA8)};

    for (std::uint32_t lane = 0; lane < kLaneCount; ++lane) {
        const float x = kHighwayX + kLaneW * static_cast<float>(lane);
        frame.addRect(x + 4, 60, kLaneW - 8, kHitLineY - 40, rgba(0x2A, 0x20, 0x40), 8);
        frame.addRect(x + 10, kHitLineY - 8, kLaneW - 20, 16, kLaneColors[lane], 8);
        const char32_t key = (lane == 0) ? U'D' : (lane == 1) ? U'F' : (lane == 2) ? U'J' : U'K';
        const std::u32string_view label(&key, 1);
        frame.addText(x + kLaneW * 0.5f, kHitLineY + 22, 26, kLaneColors[lane],
                      TextAlign::Center, label, true);
    }

    // Notes approaching the hit line
    for (const auto& note : m_notes) {
        const double dtToHit = note.targetTimeSec - m_songTime;
        if (dtToHit > m_approachSec || dtToHit < -kLateWindowSec) {
            continue;
        }
        const float y = kHitLineY - static_cast<float>(dtToHit) * (kPixelsPerSecond / m_approachSec);
        if (y < 50.0f || y > kHitLineY + 30.0f) {
            continue;
        }
        const float x = kHighwayX + kLaneW * static_cast<float>(note.lane);
        Color color = kLaneColors[note.lane];
        if (note.state == NoteState::Missed) {
            color = palette::kBad;
        } else if (note.state == NoteState::Hit) {
            color = palette::kGood;
        }
        frame.addRect(x + 16, y - 16, kLaneW - 32, 32, color, 8);
    }

    // Judgment + combo
    const char* ratingText = "—";
    Color ratingColor = palette::kTextDim;
    switch (m_lastRating) {
        case HitRating::Perfect: ratingText = "PERFECT!"; ratingColor = palette::kGood; break;
        case HitRating::Good: ratingText = "GOOD"; ratingColor = palette::kAccent; break;
        case HitRating::TooEarly: ratingText = "QUÁ NHANH!"; ratingColor = palette::kBad; break;
        case HitRating::TooLate: ratingText = "QUÁ CHẬM!"; ratingColor = palette::kBad; break;
        case HitRating::Miss: ratingText = "MISS!"; ratingColor = palette::kBad; break;
    }
    frame.addText(450, 90, 30, (m_ratingFlashSec > 0.0) ? ratingColor : palette::kTextDim,
                  TextAlign::Center, frame.internAscii(ratingText), true);
    frame.addText(450, 130, 34, palette::kText, TextAlign::Center,
                  frame.internNumber(U"Combo x", static_cast<std::int64_t>(m_combo)), true);

    // Fail mode note + health bar
    if (m_failMode == FailMode::Hardcore) {
        frame.addText(450, 172, 18, palette::kWarn, TextAlign::Center,
                      U"CHẾ ĐỘ HARDCORE: lệch nhịp là chết ngay");
    } else {
        frame.addRect(300, 172, 300, 18, palette::kPanel, 9);
        frame.addRect(300, 172, 300.0f * static_cast<float>(std::clamp(m_health / 100.0, 0.0, 1.0)),
                      18, (m_health < 35.0) ? palette::kBad : palette::kGood, 9);
    }

    frame.stats.title = U"🎵 Gõ theo nhịp (FNF-style)";
    frame.stats.score = m_score;
    frame.stats.highScore = m_highScore;
    frame.stats.combo = m_combo;
    frame.stats.maxCombo = m_maxCombo;
    frame.stats.status = frame.internDouble(U"BPM ", m_bpm, 0);
    frame.stats.hint = U"D / F / J / K đúng nhịp · P tạm dừng · R chơi lại · Esc thoát";
    frame.stats.hasMeter = (m_failMode == FailMode::HealthBar);
    frame.stats.meter = m_health;
    frame.stats.meterMax = 100.0;
    frame.stats.paused = m_paused;
    frame.stats.gameOver = m_gameOver;
    frame.stats.finished = m_finished;
    if (m_gameOver) {
        frame.stats.banner = frame.internAscii(
            m_failReason, m_finished ? U"HOÀN THÀNH BÀI! " : U"THUA! ");
    } else if (m_paused) {
        frame.stats.banner = U"TẠM DỪNG";
    }
}

std::string RhythmTypingGame::renderText() const {
    std::ostringstream ss;
    ss << "=== RHYTHM TYPING (FNF-style, " << (m_failMode == FailMode::Hardcore ? "hardcore" : "health")
       << ") ===\n";
    ss << "Song Time: " << m_songTime << "s | BPM " << static_cast<int>(m_bpm)
       << " | Notes " << m_judged << "/" << m_notes.size() << "\n";
    const char* ratingText = "—";
    switch (m_lastRating) {
        case HitRating::Perfect: ratingText = "PERFECT!"; break;
        case HitRating::Good: ratingText = "GOOD"; break;
        case HitRating::TooEarly: ratingText = "TOO EARLY!"; break;
        case HitRating::TooLate: ratingText = "TOO LATE!"; break;
        case HitRating::Miss: ratingText = "MISS!"; break;
    }
    ss << "Last Rating: " << ratingText << " | Combo: " << m_combo
       << " (Max: " << m_maxCombo << ") | HP " << static_cast<int>(m_health) << "\n";
    ss << "Score: " << m_score << "\n";
    if (m_gameOver) {
        ss << (m_finished ? "[CHART CLEARED] " : "[FAILED] ") << m_failReason << "\n";
    }
    return ss.str();
}

//===========================================================================
// 7. No-Mistake Mode
//===========================================================================
NoMistakeGame::NoMistakeGame() {
    m_textStream = U"hoc an hoc noi hoc goi hoc mo can than trong tung phim bam kien tri ben bi";
    reset();
}

void NoMistakeGame::setStartReserve(std::int64_t reserve) noexcept {
    m_startReserve = std::max<std::int64_t>(0, reserve);
    reset();
}

void NoMistakeGame::start() {
    reset();
    m_paused = false;
    m_gameOver = false;
    m_exitRequested = false;
    m_resultPending = false;
}

void NoMistakeGame::reset() {
    m_currentIndex = 0;
    m_combo = 0;
    m_maxCombo = 0;
    m_level = 1;
    m_mistakes = 0;
    m_score = m_startReserve;   // score IS the life reserve in this mode
    m_elapsedSec = 0.0;
    m_paused = false;
    m_gameOver = false;
    m_finished = false;
    m_resultPending = false;
}

void NoMistakeGame::update(double dt) {
    if (m_paused || m_gameOver) {
        return;
    }
    m_elapsedSec += clampDt(dt);
}

InputResult NoMistakeGame::handleKey(const InputEvent& ev) {
    if (isExitKey(ev)) {
        m_exitRequested = true;
        m_resultPending = true;
        return InputResult::ExitRequested;
    }
    if (!ev.down) {
        return InputResult::Consumed;
    }
    if (isPauseKey(ev)) {
        m_paused = !m_paused;
        return InputResult::Consumed;
    }
    if (isRestartKey(ev)) {
        reset();
        return InputResult::Consumed;
    }
    if (m_paused || m_gameOver || m_textStream.empty()) {
        return InputResult::Consumed;
    }

    const char32_t target = m_textStream[m_currentIndex];
    if (ev.ch == target) {
        ++m_currentIndex;
        ++m_combo;
        m_maxCombo = std::max(m_maxCombo, m_combo);
        m_score += 100 * (1 + static_cast<int64_t>(m_combo / 20));
        if (m_score > m_highScore) {
            m_highScore = m_score;
        }
        m_level = 1 + (m_combo / 50);
        if (m_currentIndex >= m_textStream.size()) {
            // A full clean pass through the stream is the win condition.
            m_finished = true;
            m_gameOver = true;
            m_resultPending = true;
            if (m_score > m_highScore) {
                m_highScore = m_score;
            }
        }
        return InputResult::Consumed;
    }

    // Mistake!
    ++m_mistakes;
    if (m_failMode == FailMode::Hardcore) {
        m_score -= m_scorePenalty;
        if (m_score <= 0) {
            m_score = 0;
        }
        m_combo = 0;
        m_gameOver = true;
        m_finished = false;
        m_resultPending = true;
        return InputResult::Consumed;
    }

    if (m_combo > m_comboPenalty) {
        m_combo -= m_comboPenalty;
    } else {
        m_combo = 0;
    }
    // Health-bar mode uses the (much smaller) soft penalty: with the hardcore
    // penalty every single mistake emptied the reserve, which made the mode an
    // instant-death mode wearing a health bar.
    m_score -= m_softPenalty;
    if (m_score <= 0) {
        m_score = 0;
        m_gameOver = true;
        m_finished = false;
        m_resultPending = true;
    }
    return InputResult::Consumed;
}

bool NoMistakeGame::pollRunResult(RunResult& out) {
    if (!m_resultPending) {
        return false;
    }
    m_resultPending = false;
    out.type = GameType::NoMistake;
    out.score = m_score;
    out.highScore = m_highScore;
    out.level = m_level;
    out.maxCombo = m_maxCombo;
    out.wpm = m_elapsedSec > 0.0
                  ? (static_cast<double>(m_currentIndex) / 5.0) / (m_elapsedSec / 60.0)
                  : 0.0;
    out.accuracy = 100.0 * static_cast<double>(m_currentIndex) /
                   static_cast<double>(std::max<std::size_t>(1, m_currentIndex + m_mistakes));
    out.durationSec = m_elapsedSec;
    out.completed = m_finished;
    return true;
}

void NoMistakeGame::buildFrame(Frame& frame) const {
    frame.worldW = 960.0f;
    frame.worldH = 560.0f;
    frame.background = rgba(0x14, 0x10, 0x1A);
    frame.backgroundTop = rgba(0x2C, 0x14, 0x22);

    // Life reserve bar (score)
    const double reserveFrac = (m_startReserve > 0)
                                   ? std::clamp(static_cast<double>(m_score) /
                                                    static_cast<double>(m_scorePenalty),
                                                0.0, 1.0)
                                   : 1.0;
    frame.addRect(80, 90, 800, 26, palette::kPanel, 13);
    frame.addRect(80, 90, 800.0f * static_cast<float>(reserveFrac), 26,
                  (reserveFrac < 0.25) ? palette::kBad : palette::kAccent, 13);
    frame.addText(80, 62, 18, palette::kTextDim, TextAlign::Left,
                  U"Quỹ điểm (một lỗi = trừ toàn bộ mức phạt)");
    frame.addText(880, 62, 18, palette::kText, TextAlign::Right,
                  frame.internNumber(U"", m_score));

    // Text stream with per-character state
    PassageStyle style{};
    style.size = 34.0f;
    style.caret = palette::kAccent2;
    frame.addRect(60, 180, 840, 70, palette::kPanel, 12);
    addStyledLine(frame, 80, 194, m_textStream, m_currentIndex, m_textStream.size(), 0, style);

    frame.addText(80, 290, 22, palette::kText, TextAlign::Left,
                  frame.internNumber(U"Combo: ", static_cast<std::int64_t>(m_combo)), true);
    frame.addText(300, 290, 22, palette::kText, TextAlign::Left,
                  frame.internNumber(U"Kỷ luật: ", static_cast<std::int64_t>(m_level)), true);
    frame.addText(520, 290, 22, palette::kBad, TextAlign::Left,
                  frame.internNumber(U"Lỗi: ", static_cast<std::int64_t>(m_mistakes)));

    frame.stats.title = U"🎯 Không được sai";
    frame.stats.score = m_score;
    frame.stats.highScore = m_highScore;
    frame.stats.level = m_level;
    frame.stats.combo = m_combo;
    frame.stats.maxCombo = m_maxCombo;
    frame.stats.status = frame.internNumber(U"Ký tự: ",
                                            static_cast<std::int64_t>(m_currentIndex));
    frame.stats.hint = U"Gõ đúng từng ký tự · P tạm dừng · R chơi lại · Esc thoát";
    frame.stats.progress = m_textStream.empty()
                               ? 0.0
                               : static_cast<double>(m_currentIndex) /
                                     static_cast<double>(m_textStream.size());
    frame.stats.hasMeter = true;
    frame.stats.meter = m_score;
    frame.stats.meterMax = static_cast<double>(m_scorePenalty);
    frame.stats.paused = m_paused;
    frame.stats.gameOver = m_gameOver;
    frame.stats.finished = m_finished;
    if (m_gameOver) {
        frame.stats.banner = m_finished ? U"HOÀN HẢO — không một lỗi nào!"
                                        : U"SAI MỘT KÝ TỰ — màn chơi kết thúc";
    } else if (m_paused) {
        frame.stats.banner = U"TẠM DỪNG";
    }
}

std::string NoMistakeGame::renderText() const {
    std::ostringstream ss;
    ss << "=== NO-MISTAKE MODE ===\n";
    ss << "Combo: " << m_combo << " | Level: " << m_level << " | Score: " << m_score
       << " | Mistakes: " << m_mistakes << "\n";
    ss << "Stream: ";
    for (std::size_t i = 0; i < m_textStream.size(); ++i) {
        if (i == m_currentIndex) {
            ss << "[" << static_cast<char>(m_textStream[i]) << "]";
        } else {
            ss << static_cast<char>(m_textStream[i]);
        }
    }
    ss << "\n";
    if (m_gameOver) {
        ss << (m_finished ? "[PERFECT RUN]" : "[GAME OVER - Penalty broke score reserve! Press R]")
           << "\n";
    }
    return ss.str();
}

//===========================================================================
// 8. Flexing Mode
//===========================================================================
FlexingGame::FlexingGame() {
    setPreloadedText(
        U"KieeKey la bo go tieng Viet hien dai co ca he thong Arcade vo ly nhat lich su!\n"
        U"Trong che do Flexing, ban go dai va van ban tu hien ra nhu mot phep thuat.\n"
        U"Mode nay de vui la chinh: diem so khong tinh vao bang xep hang that.");
    reset();
}

void FlexingGame::setPreloadedText(std::u32string_view text) {
    m_preloadedText.assign(text.begin(), text.end());
    m_cursor = 0;
    m_emittedBuffer.clear();
}

void FlexingGame::setGranularity(FlexGranularity gran, std::uint32_t nChars) noexcept {
    m_gran = gran;
    m_nChars = (nChars == 0) ? 1 : std::min(nChars, 64u);
}

void FlexingGame::start() {
    reset();
    m_active = true;
    m_paused = false;
    m_completed = false;
    m_exitRequested = false;
    m_resultPending = false;
}

void FlexingGame::reset() {
    m_cursor = 0;
    m_emittedBuffer.clear();
    m_actualKeypresses = 0;
    m_generatedChars = 0;
    m_elapsedSec = 0.0;
    m_displayedWpm = 0.0;
    m_paused = false;
    m_completed = false;
    m_resultPending = false;
}

double FlexingGame::getEfficiencyMultiplier() const noexcept {
    if (m_actualKeypresses == 0) {
        return 1.0;
    }
    return static_cast<double>(m_generatedChars) / static_cast<double>(m_actualKeypresses);
}

std::u32string FlexingGame::popEmittedOutput() {
    if (m_emittedBuffer.empty()) {
        return std::u32string{};
    }
    // Swap so the caller owns the text without a second copy.
    std::u32string out;
    out.swap(m_emittedBuffer);
    return out;
}

void FlexingGame::update(double dt) {
    if (!m_active || m_paused || m_completed) {
        return;
    }
    const double step = clampDt(dt);
    m_elapsedSec += step;

    if (m_gran == FlexGranularity::AutoStream && m_cursor < m_preloadedText.size()) {
        std::size_t count = static_cast<std::size_t>(15.0 * step);
        if (count == 0) {
            count = 1;
        }
        for (std::size_t i = 0; i < count && m_cursor < m_preloadedText.size(); ++i) {
            m_emittedBuffer.push_back(m_preloadedText[m_cursor++]);
            ++m_generatedChars;
        }
        if (m_cursor >= m_preloadedText.size()) {
            m_completed = true;
            m_resultPending = true;
        }
    }

    // WPM needs a non-zero window; a 5 ms floor keeps the number honest
    // instead of reporting "infinity WPM" on the very first tick.
    const double windowSec = std::max(0.005, m_elapsedSec);
    m_displayedWpm = (static_cast<double>(m_generatedChars) / 5.0) / (windowSec / 60.0);
}

InputResult FlexingGame::handleKey(const InputEvent& ev) {
    if (isExitKey(ev)) {
        m_exitRequested = true;
        m_resultPending = true;
        return InputResult::ExitRequested;
    }
    if (!ev.down) {
        return InputResult::Consumed;
    }
    if (isPauseKey(ev)) {
        m_paused = !m_paused;
        return InputResult::Consumed;
    }
    if (isRestartKey(ev)) {
        reset();
        return InputResult::Consumed;
    }
    if (!m_active || m_paused || m_completed) {
        return InputResult::Consumed;
    }

    ++m_actualKeypresses;

    if (m_cursor >= m_preloadedText.size()) {
        m_completed = true;
        m_resultPending = true;
        return InputResult::Consumed;
    }

    switch (m_gran) {
        case FlexGranularity::OneWordPerKey:
            while (m_cursor < m_preloadedText.size()) {
                const char32_t c = m_preloadedText[m_cursor++];
                m_emittedBuffer.push_back(c);
                ++m_generatedChars;
                if (c == U' ' || c == U'\n') {
                    break;
                }
            }
            break;
        case FlexGranularity::NCharsPerKey:
            for (std::uint32_t i = 0; i < m_nChars && m_cursor < m_preloadedText.size(); ++i) {
                m_emittedBuffer.push_back(m_preloadedText[m_cursor++]);
                ++m_generatedChars;
            }
            break;
        case FlexGranularity::AutoStream:
            break;   // driven by update()
        case FlexGranularity::OneCharPerKey:
        default:
            m_emittedBuffer.push_back(m_preloadedText[m_cursor++]);
            ++m_generatedChars;
            break;
    }

    if (m_cursor >= m_preloadedText.size() && m_gran != FlexGranularity::AutoStream) {
        m_completed = true;
        m_resultPending = true;
    }
    return InputResult::Consumed;
}

bool FlexingGame::pollRunResult(RunResult& out) {
    if (!m_resultPending) {
        return false;
    }
    m_resultPending = false;
    out.type = GameType::Flexing;
    out.score = static_cast<std::int64_t>(m_displayedWpm);
    out.highScore = 0;
    out.level = 1;
    out.maxCombo = 0;
    out.wpm = m_displayedWpm;
    out.accuracy = 0.0;   // explicitly meaningless in this joke mode
    out.durationSec = m_elapsedSec;
    out.completed = m_completed;
    return true;
}

void FlexingGame::buildFrame(Frame& frame) const {
    frame.worldW = 960.0f;
    frame.worldH = 560.0f;
    frame.background = rgba(0x18, 0x12, 0x08);
    frame.backgroundTop = rgba(0x38, 0x22, 0x08);

    frame.addRect(60, 70, 840, 34, rgba(0xFF, 0x8A, 0x3D, 0x33), 8);
    frame.addText(480, 79, 20, palette::kWarn, TextAlign::Center,
                  U"⚠ Chế độ vui: điểm KHÔNG tính vào benchmark hay bảng xếp hạng thật", true);

    // Text that "types itself"
    const std::size_t from = (m_cursor > 220) ? (m_cursor - 220) : 0;
    const std::u32string_view window =
        std::u32string_view(m_preloadedText).substr(from, m_cursor - from + 40);
    float y = 150;
    std::size_t lineStart = 0;
    for (std::size_t i = 0; i <= window.size(); ++i) {
        if (i == window.size() || window[i] == U'\n' || (i - lineStart) >= 74) {
            frame.addText(80, y, 26, palette::kText, TextAlign::Left,
                          window.substr(lineStart, i - lineStart), false, true);
            y += 34;
            lineStart = i + 1;
            if (y > 420) {
                break;
            }
        }
    }

    // Caret
    frame.addRect(80 + static_cast<float>(m_cursor % 74) * 16.0f, 150 + 34.0f *
                      static_cast<float>((m_cursor / 74) % 8), 3, 28, palette::kAccent2, 1);

    // Stats
    frame.addRect(60, 450, 840, 70, palette::kPanel, 12);
    frame.addText(80, 462, 22, palette::kText, TextAlign::Left,
                  frame.internDouble(U"WPM hiển thị: ", m_displayedWpm, 0), true);
    frame.addText(360, 462, 22, palette::kTextDim, TextAlign::Left,
                  frame.internNumber(U"Phím thật: ", static_cast<std::int64_t>(m_actualKeypresses)));
    frame.addText(620, 462, 22, palette::kTextDim, TextAlign::Left,
                  frame.internNumber(U"Ký tự hiện: ", static_cast<std::int64_t>(m_generatedChars)));
    frame.addText(80, 492, 20, palette::kAccent, TextAlign::Left,
                  frame.internDouble(U"Hiệu suất: ", getEfficiencyMultiplier() * 100.0, 0, U"%"));

    frame.stats.title = U"🗿 Flexing Mode";
    frame.stats.score = static_cast<std::int64_t>(m_displayedWpm);
    frame.stats.hasHighScore = false;
    frame.stats.status = frame.internDouble(U"WPM hiển thị ", m_displayedWpm, 0);
    frame.stats.hint = U"Gõ phím bất kỳ · P tạm dừng · R chơi lại · Esc thoát";
    frame.stats.progress = m_preloadedText.empty()
                               ? 0.0
                               : static_cast<double>(m_cursor) /
                                     static_cast<double>(m_preloadedText.size());
    frame.stats.paused = m_paused;
    frame.stats.finished = m_completed;
    frame.stats.gameOver = m_completed;
    if (m_completed) {
        frame.stats.banner = U"XONG! (văn bản đã chuẩn bị sẵn — không phải kỹ năng thật)";
    } else if (m_paused) {
        frame.stats.banner = U"TẠM DỪNG";
    }
}

std::string FlexingGame::renderText() const {
    std::ostringstream ss;
    ss << "=== FLEXING MODE ===\n";
    ss << "[DISCLAIMER: Flexing Mode does NOT measure real typing performance!]\n";
    ss << "Displayed WPM: " << static_cast<int>(m_displayedWpm)
       << " | Keypresses: " << m_actualKeypresses
       << " | Emitted Chars: " << m_generatedChars
       << " | Efficiency: " << (getEfficiencyMultiplier() * 100.0) << "%\n";
    return ss.str();
}

//===========================================================================
// Arcade Hub Manager
//===========================================================================
ArcadeManager& ArcadeManager::instance() noexcept {
    static ArcadeManager s_instance;
    return s_instance;
}

ArcadeManager::ArcadeManager() = default;
ArcadeManager::~ArcadeManager() = default;

std::unique_ptr<IArcadeGame> ArcadeManager::makeGame(GameType type, std::uint32_t seed) const {
    std::unique_ptr<IArcadeGame> game;
    switch (type) {
        case GameType::Snake:      game = std::make_unique<SnakeGame>(); break;
        case GameType::Tetris:     game = std::make_unique<TetrisGame>(); break;
        case GameType::Fishing:    game = std::make_unique<FishingGame>(); break;
        case GameType::TypingRace: game = std::make_unique<TypingRaceGame>(); break;
        case GameType::WasdRace:   game = std::make_unique<WasdRaceGame>(); break;
        case GameType::Rhythm:     game = std::make_unique<RhythmTypingGame>(); break;
        case GameType::NoMistake:  game = std::make_unique<NoMistakeGame>(); break;
        case GameType::Flexing:    game = std::make_unique<FlexingGame>(); break;
        case GameType::None:
        default:
            return nullptr;
    }

    if (game) {
        // Configuration is applied to the concrete classes that support it.
        if (auto* rhythm = dynamic_cast<RhythmTypingGame*>(game.get())) {
            rhythm->setFailMode(m_config.rhythmFailMode);
            rhythm->setBpm(m_config.rhythmBpm);
            rhythm->setNoteCount(m_config.rhythmNoteCount);
            rhythm->setApproachSec(m_config.rhythmApproachSec);
        } else if (auto* noMistake = dynamic_cast<NoMistakeGame*>(game.get())) {
            noMistake->setFailMode(m_config.noMistakeFailMode);
            noMistake->setMistakePenalty(m_config.noMistakeScorePenalty,
                                         m_config.noMistakeComboPenalty);
            noMistake->setStartReserve(m_config.noMistakeStartReserve);
        } else if (auto* race = dynamic_cast<TypingRaceGame*>(game.get())) {
            race->setPacerWpm(m_config.typingRacePacerWpm);
        } else if (auto* wasd = dynamic_cast<WasdRaceGame*>(game.get())) {
            wasd->setStartFuel(m_config.wasdStartFuel);
            wasd->setObstacleSpacing(m_config.wasdObstacleSpacingSec);
        } else if (auto* fishing = dynamic_cast<FishingGame*>(game.get())) {
            fishing->setAutomationMode(m_config.fishingAutomation);
        }
        game->setSeed(seed);
        game->start();
    }
    return game;
}

void ArcadeManager::collectResultFrom(const std::shared_ptr<IArcadeGame>& game) {
    if (!game) {
        return;
    }
    RunResult result;
    if (!game->pollRunResult(result)) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_results.size() < 64) {
        m_results.push_back(result);
    }
    m_resultCollected = true;
}

bool ArcadeManager::launchGame(GameType type) {
    std::uint32_t seed;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_hasLaunched) {
            m_hasLaunched = true;
            m_seed = 0x9E3779B9u;
        }
        // Advance the hub seed so consecutive launches differ but a campaign
        // replayed with the same hub seed is reproducible.
        m_seed = m_seed * 1664525u + 1013904223u;
        seed = m_seed;
    }
    return launchGame(type, seed);
}

bool ArcadeManager::launchGame(GameType type, std::uint32_t seed) {
    // Retire any finished run before replacing the game so results are never
    // lost when the player switches games directly from the hub.
    {
        std::shared_ptr<IArcadeGame> previous;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            previous = m_game;
        }
        collectResultFrom(previous);
    }

    auto game = makeGame(type, seed);
    if (!game) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_game = std::move(game);
        m_active.store(true, std::memory_order_release);
        m_resultCollected = false;   // fresh run: nothing reported yet
    }
    return true;
}

void ArcadeManager::stopGame() {
    std::shared_ptr<IArcadeGame> game;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        game = m_game;
        m_game.reset();
        m_active.store(false, std::memory_order_release);
    }
    if (game) {
        // Synthesize a result for endless games (Fishing/Flexing) so a manual
        // "thoát" still feeds progression and the ghost log. When the run was
        // already reported (update() drained it at the finish line) nothing is
        // synthesized, so a score can never be credited twice.
        collectResultFrom(game);
        const bool alreadyReported = [this] {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_resultCollected;
        }();
        if (!alreadyReported && game->getScore() > 0) {
            RunResult synth;
            synth.type = game->getType();
            synth.score = game->getScore();
            synth.highScore = game->getHighScore();
            synth.completed = false;
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_results.size() < 64) {
                m_results.push_back(synth);
            }
        }
    }
}

void ArcadeManager::restartGame() {
    std::shared_ptr<IArcadeGame> game;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        game = m_game;
    }
    if (game) {
        collectResultFrom(game);   // bank the abandoned attempt first
        game->reset();
        game->start();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_resultCollected = false;
    }
}

bool ArcadeManager::hasActiveGame() const noexcept {
    return m_active.load(std::memory_order_acquire);
}

bool ArcadeManager::isConsumingKeyboard() const noexcept {
    if (!m_active.load(std::memory_order_acquire)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_game != nullptr;
}

void ArcadeManager::update(double dt) {
    std::shared_ptr<IArcadeGame> game;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        game = m_game;
    }
    if (!game) {
        return;
    }
    const double step = (dt > m_config.maxFrameStepSec) ? m_config.maxFrameStepSec
                       : (dt > 0.0 ? dt : 0.0);
    game->update(step);
    // A run that just ended is reported *now*: the previous build only queued
    // the result on the next key press, so a player who finished a race and
    // closed the window (or the hub tab) lost the score, the highscore and the
    // progression XP.
    collectResultFrom(game);
    // ...and credited immediately (XP, records, achievements, level-up) so
    // every front-end that pumps frames — the GDI hub, the web bridge, the CLI
    // — awards progress without extra wiring. Popped results are gone, so a
    // second drain (e.g. the settings timer) can never double-credit.
    drainRunResultsToProgression();
}

InputResult ArcadeManager::handleKey(const InputEvent& ev) {
    std::shared_ptr<IArcadeGame> game;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        game = m_game;
    }
    if (!game) {
        return InputResult::NotConsumed;
    }
    const InputResult result = game->handleKey(ev);
    if (result == InputResult::ExitRequested || game->wantsExit()) {
        // Queue the run result, then release the keyboard back to the IME.
        collectResultFrom(game);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_game == game) {
                m_game.reset();
                m_active.store(false, std::memory_order_release);
            }
        }
    }
    return result;
}

InputResult ArcadeManager::handleKey(int vkValue, char32_t ch, bool down) {
    InputEvent ev{};
    ev.vk = vkValue;
    ev.ch = ch;
    ev.down = down;
    return handleKey(ev);
}

const Frame& ArcadeManager::getFrame() const {
    std::shared_ptr<IArcadeGame> game;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        game = m_game;
    }
    m_frame.clear();
    if (game) {
        game->buildFrame(m_frame);
    } else {
        m_frame.worldW = 960.0f;
        m_frame.worldH = 560.0f;
        m_frame.stats.title = U"KieeKey Arcade";
        m_frame.stats.status = U"Chưa có game nào đang chạy";
        m_frame.addText(480, 240, 28, palette::kTextDim, TextAlign::Center,
                        U"Chọn một game trong Arcade Hub để bắt đầu");
    }
    return m_frame;
}

std::string ArcadeManager::renderCurrentGame() const {
    std::shared_ptr<IArcadeGame> game;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        game = m_game;
    }
    if (!game) {
        return "No game active.\n";
    }
    return game->renderText();
}

GameType ArcadeManager::getCurrentGameType() const {
    std::shared_ptr<IArcadeGame> game;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        game = m_game;
    }
    return game ? game->getType() : GameType::None;
}

IArcadeGame* ArcadeManager::getCurrentGame() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_game.get();
}

const IArcadeGame* ArcadeManager::getCurrentGame() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_game.get();
}

void ArcadeManager::setConfig(const ArcadeConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
}

ArcadeConfig ArcadeManager::getConfig() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config;
}

int ArcadeManager::progressionGameIdFor(GameType type) noexcept {
    switch (type) {
        case GameType::Snake: return 1;
        case GameType::Tetris: return 2;
        case GameType::Fishing: return 3;
        case GameType::TypingRace: return 4;
        case GameType::WasdRace: return 5;
        case GameType::Rhythm: return 6;
        case GameType::NoMistake: return 7;
        case GameType::Flexing: return 8;
        case GameType::None: return 0;
    }
    return 0;
}

std::size_t ArcadeManager::drainRunResultsToProgression() {
    std::size_t credited = 0;
    RunResult result;
    while (pollRunResult(result)) {
        const int gameId = progressionGameIdFor(result.type);
        if (gameId <= 0) {
            continue;
        }
        ok::progression::ProgressionEngine::instance().recordArcadeRun(
            gameId, result.score, result.maxCombo, result.wpm);
        ++credited;
    }
    return credited;
}

bool ArcadeManager::pollRunResult(RunResult& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_results.empty()) {
        return false;
    }
    out = m_results.front();
    m_results.erase(m_results.begin());
    return true;
}

std::size_t ArcadeManager::pendingRunResultCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_results.size();
}

void ArcadeManager::setSeed(std::uint32_t seed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_seed = seed;
    m_hasLaunched = true;
}

//===========================================================================
// Small helpers
//===========================================================================
std::string_view hitRatingName(HitRating rating) noexcept {
    switch (rating) {
        case HitRating::Perfect: return "perfect";
        case HitRating::Good: return "good";
        case HitRating::TooEarly: return "too-early";
        case HitRating::TooLate: return "too-late";
        case HitRating::Miss: return "miss";
    }
    return "unknown";
}

std::string_view rarityName(FishRarity rarity) noexcept {
    switch (rarity) {
        case FishRarity::Common: return "Common";
        case FishRarity::Uncommon: return "Uncommon";
        case FishRarity::Rare: return "Rare";
        case FishRarity::Epic: return "Epic";
        case FishRarity::Legendary: return "Legendary";
    }
    return "Common";
}

Color rarityColor(FishRarity rarity) noexcept {
    switch (rarity) {
        case FishRarity::Common: return rgba(0x9A, 0xA7, 0xB8);
        case FishRarity::Uncommon: return rgba(0x51, 0xE8, 0x8A);
        case FishRarity::Rare: return rgba(0x4C, 0xC2, 0xFF);
        case FishRarity::Epic: return rgba(0xC1, 0x7C, 0xFF);
        case FishRarity::Legendary: return rgba(0xFF, 0xD1, 0x4A);
    }
    return rgba(0x9A, 0xA7, 0xB8);
}

} // namespace ok::arcade
