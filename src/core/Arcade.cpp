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

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace ok::arcade {

namespace {

uint32_t arcadeLcg(uint32_t& state) noexcept {
    state = state * 1664525u + 1013904223u;
    return state;
}

// VK codes portable mirrors
constexpr int kVkLeft = 0x25;
constexpr int kVkUp = 0x26;
constexpr int kVkRight = 0x27;
constexpr int kVkDown = 0x28;
constexpr int kVkSpace = 0x20;
constexpr int kVkReturn = 0x0D;

} // namespace

//===========================================================================
// 1. Snake Game Implementation
//===========================================================================
SnakeGame::SnakeGame() {
    reset();
}

void SnakeGame::start() {
    reset();
    m_paused = false;
    m_gameOver = false;
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
    m_paused = false;
    m_gameOver = false;
    spawnFood();
}

void SnakeGame::spawnFood() {
    for (int attempts = 0; attempts < 100; ++attempts) {
        int fx = static_cast<int>(arcadeLcg(m_rngState) % kWidth);
        int fy = static_cast<int>(arcadeLcg(m_rngState) % kHeight);
        SnakePoint candidate{fx, fy};
        bool onBody = false;
        for (const auto& pt : m_body) {
            if (pt == candidate) {
                onBody = true;
                break;
            }
        }
        if (!onBody) {
            m_food = candidate;
            return;
        }
    }
    m_food = {1, 1};
}

void SnakeGame::step() {
    if (m_gameOver || m_paused) return;

    m_dir = m_nextDir;
    SnakePoint head = m_body.front();
    switch (m_dir) {
        case Direction::Up:    head.y--; break;
        case Direction::Down:  head.y++; break;
        case Direction::Left:  head.x--; break;
        case Direction::Right: head.x++; break;
    }

    // Border collision
    if (head.x < 0 || head.x >= kWidth || head.y < 0 || head.y >= kHeight) {
        m_gameOver = true;
        if (m_score > m_highScore) m_highScore = m_score;
        return;
    }

    // Self collision
    for (const auto& pt : m_body) {
        if (pt == head) {
            m_gameOver = true;
            if (m_score > m_highScore) m_highScore = m_score;
            return;
        }
    }

    m_body.push_front(head);
    if (head == m_food) {
        m_score += 100;
        if (m_score > m_highScore) m_highScore = m_score;
        if (m_tickInterval > 0.06) {
            m_tickInterval -= 0.005;
        }
        spawnFood();
    } else {
        m_body.pop_back();
    }
}

void SnakeGame::update(double dt) {
    if (m_paused || m_gameOver) return;
    m_tickTimer += dt;
    while (m_tickTimer >= m_tickInterval) {
        m_tickTimer -= m_tickInterval;
        step();
        if (m_gameOver) break;
    }
}

bool SnakeGame::handleKey(int vk, char32_t ch, bool down) {
    if (!down) return true;

    // Controls: W/A/S/D or Arrow keys
    if (ch == U'w' || ch == U'W' || vk == kVkUp) {
        if (m_dir != Direction::Down) m_nextDir = Direction::Up;
        return true;
    }
    if (ch == U's' || ch == U'S' || vk == kVkDown) {
        if (m_dir != Direction::Up) m_nextDir = Direction::Down;
        return true;
    }
    if (ch == U'a' || ch == U'A' || vk == kVkLeft) {
        if (m_dir != Direction::Right) m_nextDir = Direction::Left;
        return true;
    }
    if (ch == U'd' || ch == U'D' || vk == kVkRight) {
        if (m_dir != Direction::Left) m_nextDir = Direction::Right;
        return true;
    }
    if (ch == U'p' || ch == U'P') {
        m_paused = !m_paused;
        return true;
    }
    if (ch == U'r' || ch == U'R') {
        reset();
        return true;
    }
    return false;
}

void SnakeGame::stressTestInputQueue(const std::vector<int>& vks) {
    for (int vk : vks) {
        handleKey(vk, 0, true);
        step();
    }
}

std::string SnakeGame::renderText() const {
    std::ostringstream ss;
    ss << "+--------------------+\n";
    for (int y = 0; y < kHeight; ++y) {
        ss << "|";
        for (int x = 0; x < kWidth; ++x) {
            SnakePoint pt{x, y};
            if (pt == m_body.front()) {
                ss << "@";
            } else if (pt == m_food) {
                ss << "*";
            } else {
                bool isBody = false;
                for (size_t b = 1; b < m_body.size(); ++b) {
                    if (m_body[b] == pt) {
                        isBody = true;
                        break;
                    }
                }
                ss << (isBody ? "o" : " ");
            }
        }
        ss << "|\n";
    }
    ss << "+--------------------+\n";
    ss << "Score: " << m_score << " | High: " << m_highScore;
    if (m_paused) ss << " [PAUSED]";
    if (m_gameOver) ss << " [GAME OVER - Press R]";
    ss << "\n";
    return ss.str();
}

//===========================================================================
// 2. Tetris Game Implementation
//===========================================================================
namespace {

// 7 pieces x 4 rotations x 4 blocks [y][x]
const int kPieces[7][4][4][2] = {
    // 0: I
    {{{0,0},{1,0},{2,0},{3,0}}, {{1,-1},{1,0},{1,1},{1,2}}, {{0,0},{1,0},{2,0},{3,0}}, {{1,-1},{1,0},{1,1},{1,2}}},
    // 1: O
    {{{0,0},{1,0},{0,1},{1,1}}, {{0,0},{1,0},{0,1},{1,1}}, {{0,0},{1,0},{0,1},{1,1}}, {{0,0},{1,0},{0,1},{1,1}}},
    // 2: T
    {{{1,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{2,1},{1,2}}, {{0,1},{1,1},{2,1},{1,2}}, {{1,0},{0,1},{1,1},{1,2}}},
    // 3: S
    {{{1,0},{2,0},{0,1},{1,1}}, {{1,0},{1,1},{2,1},{2,2}}, {{1,0},{2,0},{0,1},{1,1}}, {{1,0},{1,1},{2,1},{2,2}}},
    // 4: Z
    {{{0,0},{1,0},{1,1},{2,1}}, {{2,0},{1,1},{2,1},{1,2}}, {{0,0},{1,0},{1,1},{2,1}}, {{2,0},{1,1},{2,1},{1,2}}},
    // 5: J
    {{{0,0},{0,1},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{1,2}}, {{0,1},{1,1},{2,1},{2,2}}, {{1,0},{1,1},{0,2},{1,2}}},
    // 6: L
    {{{2,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{1,2},{2,2}}, {{0,1},{1,1},{2,1},{0,2}}, {{0,0},{1,0},{1,1},{1,2}}}
};

} // namespace

TetrisGame::TetrisGame() {
    reset();
}

void TetrisGame::start() {
    reset();
    m_paused = false;
    m_gameOver = false;
}

void TetrisGame::reset() {
    for (auto& row : m_grid) {
        row.fill(0);
    }
    m_score = 0;
    m_linesCleared = 0;
    m_level = 1;
    m_dropTimer = 0.0;
    m_dropInterval = 0.5;
    m_paused = false;
    m_gameOver = false;
    spawnPiece();
}

bool TetrisGame::collides(int px, int py, int rot) const {
    for (int i = 0; i < 4; ++i) {
        int x = px + kPieces[m_curPiece][rot][i][0];
        int y = py + kPieces[m_curPiece][rot][i][1];
        if (x < 0 || x >= kCols || y >= kRows) return true;
        if (y >= 0 && m_grid[y][x] != 0) return true;
    }
    return false;
}

void TetrisGame::spawnPiece() {
    m_curPiece = static_cast<int>(arcadeLcg(m_rng) % 7);
    m_curRot = 0;
    m_curX = 3;
    m_curY = 0;
    if (collides(m_curX, m_curY, m_curRot)) {
        m_gameOver = true;
        if (m_score > m_highScore) m_highScore = m_score;
    }
}

void TetrisGame::lockPiece() {
    for (int i = 0; i < 4; ++i) {
        int x = m_curX + kPieces[m_curPiece][m_curRot][i][0];
        int y = m_curY + kPieces[m_curPiece][m_curRot][i][1];
        if (y >= 0 && y < kRows && x >= 0 && x < kCols) {
            m_grid[y][x] = static_cast<uint8_t>(m_curPiece + 1);
        }
    }
    clearLines();
    spawnPiece();
}

void TetrisGame::clearLines() {
    uint32_t lines = 0;
    for (int y = kRows - 1; y >= 0; --y) {
        bool full = true;
        for (int x = 0; x < kCols; ++x) {
            if (m_grid[y][x] == 0) {
                full = false;
                break;
            }
        }
        if (full) {
            lines++;
            for (int ny = y; ny > 0; --ny) {
                m_grid[ny] = m_grid[ny - 1];
            }
            m_grid[0].fill(0);
            y++; // Check same line again
        }
    }

    if (lines > 0) {
        m_linesCleared += lines;
        int64_t pts = (lines == 1) ? 100 : (lines == 2) ? 300 : (lines == 3) ? 500 : 800;
        m_score += pts * m_level;
        if (m_score > m_highScore) m_highScore = m_score;

        m_level = 1 + (m_linesCleared / 10);
        m_dropInterval = std::max(0.08, 0.5 - static_cast<double>(m_level - 1) * 0.04);
    }
}

void TetrisGame::update(double dt) {
    if (m_paused || m_gameOver) return;
    m_dropTimer += dt;
    if (m_dropTimer >= m_dropInterval) {
        m_dropTimer = 0.0;
        if (!collides(m_curX, m_curY + 1, m_curRot)) {
            m_curY++;
        } else {
            lockPiece();
        }
    }
}

bool TetrisGame::handleKey(int vk, char32_t ch, bool down) {
    if (!down) return true;

    if (ch == U'p' || ch == U'P') {
        m_paused = !m_paused;
        return true;
    }
    if (ch == U'r' || ch == U'R') {
        reset();
        return true;
    }
    if (m_paused || m_gameOver) return true;

    if (ch == U'a' || ch == U'A' || vk == kVkLeft) {
        if (!collides(m_curX - 1, m_curY, m_curRot)) m_curX--;
        return true;
    }
    if (ch == U'd' || ch == U'D' || vk == kVkRight) {
        if (!collides(m_curX + 1, m_curY, m_curRot)) m_curX++;
        return true;
    }
    if (ch == U's' || ch == U'S' || vk == kVkDown) {
        if (!collides(m_curX, m_curY + 1, m_curRot)) {
            m_curY++;
            m_score += 1;
        }
        return true;
    }
    if (ch == U'w' || ch == U'W' || vk == kVkUp) {
        int nextRot = (m_curRot + 1) % 4;
        if (!collides(m_curX, m_curY, nextRot)) m_curRot = nextRot;
        return true;
    }
    if (vk == kVkSpace) {
        // Hard drop
        while (!collides(m_curX, m_curY + 1, m_curRot)) {
            m_curY++;
            m_score += 2;
        }
        lockPiece();
        return true;
    }
    return false;
}

std::string TetrisGame::renderText() const {
    auto gridCopy = m_grid;
    if (!m_gameOver) {
        for (int i = 0; i < 4; ++i) {
            int x = m_curX + kPieces[m_curPiece][m_curRot][i][0];
            int y = m_curY + kPieces[m_curPiece][m_curRot][i][1];
            if (x >= 0 && x < kCols && y >= 0 && y < kRows) {
                gridCopy[y][x] = static_cast<uint8_t>(m_curPiece + 1);
            }
        }
    }

    std::ostringstream ss;
    ss << "+----------+\n";
    for (int y = 0; y < kRows; ++y) {
        ss << "|";
        for (int x = 0; x < kCols; ++x) {
            ss << (gridCopy[y][x] ? "#" : " ");
        }
        ss << "|\n";
    }
    ss << "+----------+\n";
    ss << "Score: " << m_score << " | Level: " << m_level << " | Lines: " << m_linesCleared;
    if (m_paused) ss << " [PAUSED]";
    if (m_gameOver) ss << " [GAME OVER - Press R]";
    ss << "\n";
    return ss.str();
}

//===========================================================================
// 3. Fishing Game Implementation (Câu cá bằng gõ phím)
//===========================================================================
FishingGame::FishingGame() {
    reset();
}

void FishingGame::start() {
    reset();
    m_paused = false;
    m_gameOver = false;
}

void FishingGame::reset() {
    m_score = 0;
    m_catches = 0;
    m_escapes = 0;
    m_paused = false;
    m_gameOver = false;
    hookNewFish();
}

void FishingGame::hookNewFish() {
    uint32_t roll = arcadeLcg(m_rng) % 100;
    uint32_t baitBonus = (m_baitLevel - 1) * 3;

    if (roll < 2 + baitBonus) {
        m_currentRarity = FishRarity::Legendary;
        m_fishName = "Thần Ngư Sông Hồng";
        m_prompt = U"than ngu khong lo xuat hien duoi dong nuoc sau";
        m_escapeTimer = 22.0;
    } else if (roll < 10 + baitBonus) {
        m_currentRarity = FishRarity::Epic;
        m_fishName = "Cá Rồng Hoàng Kim";
        m_prompt = U"ca rong uon luon dep mat tren mat ho";
        m_escapeTimer = 18.0;
    } else if (roll < 25 + baitBonus) {
        m_currentRarity = FishRarity::Rare;
        m_fishName = "Cá Hồi Sa Pa";
        m_prompt = U"ca hoi boi nguoc dong suoi lanh";
        m_escapeTimer = 16.0;
    } else if (roll < 55) {
        m_currentRarity = FishRarity::Uncommon;
        m_fishName = "Cá Trắm Đen";
        m_prompt = U"ca tram den can cau rat khoe";
        m_escapeTimer = 14.0;
    } else {
        m_currentRarity = FishRarity::Common;
        m_fishName = "Cá Rô Đồng";
        m_prompt = U"ca ro dong boi loi tung tang";
        m_escapeTimer = 12.0;
    }

    m_promptIndex = 0;
    m_pullProgress = 20.0;
    m_lineTension = 50.0;
}

void FishingGame::onCatchSuccess() {
    m_catches++;
    int64_t basePts = (m_currentRarity == FishRarity::Legendary) ? 5000 :
                      (m_currentRarity == FishRarity::Epic) ? 2000 :
                      (m_currentRarity == FishRarity::Rare) ? 800 :
                      (m_currentRarity == FishRarity::Uncommon) ? 300 : 100;
    m_score += basePts;
    if (m_score > m_highScore) m_highScore = m_score;
    hookNewFish();
}

void FishingGame::onFishEscape() {
    m_escapes++;
    hookNewFish();
}

void FishingGame::update(double dt) {
    if (m_paused || m_gameOver) return;

    m_escapeTimer -= dt;
    if (m_escapeTimer <= 0.0) {
        onFishEscape();
        return;
    }

    // Fish resistance: natural pull back
    double resistance = 2.0 + static_cast<double>(static_cast<int>(m_currentRarity)) * 1.5;
    m_pullProgress -= resistance * dt;
    if (m_pullProgress <= 0.0) {
        onFishEscape();
        return;
    }

    // Automation handling
    if (m_autoMode == AutomationMode::Assisted) {
        m_pullProgress += 1.5 * dt; // slight passive assist
    } else if (m_autoMode == AutomationMode::Automated) {
        m_autoTimer += dt;
        if (m_autoTimer >= 0.25) { // auto-type 4 chars per second
            m_autoTimer = 0.0;
            if (m_promptIndex < m_prompt.size()) {
                handleKey(0, m_prompt[m_promptIndex], true);
            }
        }
    }
}

bool FishingGame::handleKey(int vk, char32_t ch, bool down) {
    (void)vk;
    if (!down) return true;

    if (ch == U'p' || ch == U'P') {
        m_paused = !m_paused;
        return true;
    }
    if (m_paused || m_gameOver) return true;

    if (m_promptIndex < m_prompt.size()) {
        char32_t expected = m_prompt[m_promptIndex];
        if (ch == expected) {
            m_promptIndex++;
            double pullDelta = 8.0 + (m_reelLevel - 1) * 2.0;
            m_pullProgress += pullDelta;
            if (m_pullProgress >= 100.0 || m_promptIndex == m_prompt.size()) {
                onCatchSuccess();
            }
            return true;
        } else {
            // Mistake penalty
            m_pullProgress = std::max(0.0, m_pullProgress - 5.0);
            return true;
        }
    }
    return false;
}

std::string FishingGame::renderText() const {
    std::ostringstream ss;
    ss << "=== KIEEKEY FISHING ===\n";
    ss << "Fish: " << m_fishName << " [Rarity: "
       << (m_currentRarity == FishRarity::Legendary ? "Legendary" :
           m_currentRarity == FishRarity::Epic ? "Epic" :
           m_currentRarity == FishRarity::Rare ? "Rare" :
           m_currentRarity == FishRarity::Uncommon ? "Uncommon" : "Common")
       << "]\n";
    ss << "Target: ";
    for (size_t i = 0; i < m_prompt.size(); ++i) {
        if (i < m_promptIndex) ss << "*";
        else ss << static_cast<char>(m_prompt[i]);
    }
    ss << "\nReel Progress: [" << static_cast<int>(m_pullProgress) << "%] ";
    int bars = static_cast<int>(m_pullProgress / 5.0);
    for (int i = 0; i < 20; ++i) ss << (i < bars ? "=" : ".");
    ss << "\nEscape in: " << std::fixed << std::setprecision(1) << m_escapeTimer << "s\n";
    ss << "Catches: " << m_catches << " | Escapes: " << m_escapes << " | Score: " << m_score << "\n";
    return ss.str();
}

//===========================================================================
// 4. Typing Race Game Implementation
//===========================================================================
TypingRaceGame::TypingRaceGame() {
    m_passage = U"KieeKey la bo go tieng Viet hien dai toi uu do tre va toc do";
    reset();
}

void TypingRaceGame::start() {
    reset();
    m_paused = false;
    m_finished = false;
}

void TypingRaceGame::reset() {
    m_charIndex = 0;
    m_totalKeys = 0;
    m_correctKeys = 0;
    m_elapsedSec = 0.0;
    m_liveWpm = 0.0;
    m_accuracy = 100.0;
    m_score = 0;
    m_paused = false;
    m_finished = false;
}

double TypingRaceGame::getProgressPercent() const noexcept {
    if (m_passage.empty()) return 100.0;
    return (static_cast<double>(m_charIndex) / static_cast<double>(m_passage.size())) * 100.0;
}

void TypingRaceGame::update(double dt) {
    if (m_paused || m_finished) return;
    m_elapsedSec += dt;
    if (m_elapsedSec > 0.0) {
        double minutes = m_elapsedSec / 60.0;
        m_liveWpm = (static_cast<double>(m_correctKeys) / 5.0) / minutes;
    }
    if (m_totalKeys > 0) {
        m_accuracy = (static_cast<double>(m_correctKeys) / static_cast<double>(m_totalKeys)) * 100.0;
    }
}

bool TypingRaceGame::handleKey(int vk, char32_t ch, bool down) {
    (void)vk;
    if (!down) return true;
    if (m_paused || m_finished) return true;

    m_totalKeys++;
    if (m_charIndex < m_passage.size()) {
        if (ch == m_passage[m_charIndex]) {
            m_charIndex++;
            m_correctKeys++;
            if (m_charIndex == m_passage.size()) {
                m_finished = true;
                m_score = static_cast<int64_t>(m_liveWpm * (m_accuracy / 100.0) * 100.0);
                if (m_score > m_highScore) m_highScore = m_score;
            }
            return true;
        }
    }
    return true;
}

std::string TypingRaceGame::renderText() const {
    std::ostringstream ss;
    ss << "=== TYPING RACE === (Notice: Game score != official benchmark)\n";
    ss << "Track: [";
    int progress = static_cast<int>(getProgressPercent() / 5.0);
    for (int i = 0; i < 20; ++i) {
        if (i == progress) ss << "🏎️";
        else ss << "-";
    }
    ss << "] " << static_cast<int>(getProgressPercent()) << "%\n";
    ss << "Speed: " << static_cast<int>(m_liveWpm) << " WPM | Accuracy: "
       << static_cast<int>(m_accuracy) << "% | Time: "
       << std::fixed << std::setprecision(1) << m_elapsedSec << "s\n";
    if (m_finished) ss << "[FINISH LINE REACHED! Score: " << m_score << "]\n";
    return ss.str();
}

//===========================================================================
// 5. WASD + Typing Racing Game Implementation
//===========================================================================
WasdRaceGame::WasdRaceGame() {
    m_passage = U"lai xe vuot chuong ngai vat toc do cao";
    reset();
}

void WasdRaceGame::start() {
    reset();
    m_paused = false;
    m_gameOver = false;
}

void WasdRaceGame::reset() {
    m_textIndex = 0;
    m_playerLane = 1;
    m_carSpeed = 60.0;
    m_fuel = 100.0;
    m_obstacles.clear();
    m_spawnTimer = 0.0;
    m_dodges = 0;
    m_collisions = 0;
    m_elapsedSec = 0.0;
    m_score = 0;
    m_paused = false;
    m_gameOver = false;
}

void WasdRaceGame::spawnObstacle() {
    int lane = static_cast<int>(arcadeLcg(m_rng) % kLanes);
    m_obstacles.push_back({lane, 100.0});
}

void WasdRaceGame::update(double dt) {
    if (m_paused || m_gameOver) return;
    m_elapsedSec += dt;

    // Advance obstacles
    double moveDist = (m_carSpeed * 0.28) * dt;
    for (auto it = m_obstacles.begin(); it != m_obstacles.end();) {
        it->dist -= moveDist;
        if (it->dist <= 0.0) {
            if (it->lane == m_playerLane) {
                // Collision!
                m_collisions++;
                m_fuel = std::max(0.0, m_fuel - 25.0);
                m_carSpeed = std::max(20.0, m_carSpeed - 20.0);
                if (m_fuel <= 0.0) {
                    m_gameOver = true;
                }
            } else {
                m_dodges++;
                m_score += 50;
            }
            it = m_obstacles.erase(it);
        } else {
            ++it;
        }
    }

    m_spawnTimer += dt;
    if (m_spawnTimer >= 2.0) {
        m_spawnTimer = 0.0;
        spawnObstacle();
    }
}

bool WasdRaceGame::handleKey(int vk, char32_t ch, bool down) {
    if (!down) return true;
    if (m_paused || m_gameOver) return true;

    // WASD Driving Controls
    if (ch == U'a' || ch == U'A' || vk == kVkLeft) {
        if (m_playerLane > 0) m_playerLane--;
        return true;
    }
    if (ch == U'd' || ch == U'D' || vk == kVkRight) {
        if (m_playerLane < kLanes - 1) m_playerLane++;
        return true;
    }
    if (ch == U'w' || ch == U'W' || vk == kVkUp) {
        m_carSpeed = std::min(140.0, m_carSpeed + 10.0);
        return true;
    }
    if (ch == U's' || ch == U'S' || vk == kVkDown) {
        m_carSpeed = std::max(30.0, m_carSpeed - 10.0);
        return true;
    }

    // Typing fuels engine and restores durability
    if (m_textIndex < m_passage.size() && ch == m_passage[m_textIndex]) {
        m_textIndex++;
        m_score += 20;
        m_fuel = std::min(100.0, m_fuel + 5.0);
        if (m_textIndex == m_passage.size()) {
            m_textIndex = 0; // loop passage
        }
        return true;
    }
    return false;
}

std::string WasdRaceGame::renderText() const {
    std::ostringstream ss;
    ss << "=== WASD + TYPING RACING ===\n";
    ss << "Lanes: | ";
    for (int l = 0; l < kLanes; ++l) {
        if (l == m_playerLane) ss << "[CAR] ";
        else ss << "  .   ";
    }
    ss << "|\nSpeed: " << static_cast<int>(m_carSpeed) << " km/h | Fuel/HP: "
       << static_cast<int>(m_fuel) << "% | Dodges: " << m_dodges
       << " | Collisions: " << m_collisions << "\n";
    return ss.str();
}

//===========================================================================
// 6. Rhythm Typing Game Implementation
//===========================================================================
RhythmTypingGame::RhythmTypingGame() {
    reset();
}

void RhythmTypingGame::start() {
    reset();
    m_paused = false;
    m_gameOver = false;
}

void RhythmTypingGame::reset() {
    m_notes.clear();
    // Generate beat sequence spaced by 0.5s
    const char32_t beatKeys[] = {U'd', U'f', U'j', U'k'};
    for (int i = 0; i < 40; ++i) {
        m_notes.push_back({beatKeys[i % 4], 1.0 + static_cast<double>(i) * 0.5, false});
    }
    m_songTime = 0.0;
    m_combo = 0;
    m_maxCombo = 0;
    m_score = 0;
    m_paused = false;
    m_gameOver = false;
}

void RhythmTypingGame::update(double dt) {
    if (m_paused || m_gameOver) return;
    m_songTime += dt;

    // Check notes that expired (missed because player didn't hit in time)
    for (auto& n : m_notes) {
        if (!n.hit && m_songTime > n.targetTimeSec + 0.15) {
            n.hit = true;
            m_combo = 0;
            m_lastRating = HitRating::Miss;
        }
    }

    if (!m_notes.empty() && m_songTime > m_notes.back().targetTimeSec + 0.5) {
        m_gameOver = true;
        if (m_score > m_highScore) m_highScore = m_score;
    }
}

bool RhythmTypingGame::handleKey(int vk, char32_t ch, bool down) {
    (void)vk;
    if (!down) return true;
    if (m_paused || m_gameOver) return true;

    // Search closest unhit note matching key
    for (auto& n : m_notes) {
        if (!n.hit && n.ch == ch) {
            double delta = m_songTime - n.targetTimeSec;
            if (std::abs(delta) <= 0.08) {
                // Perfect!
                n.hit = true;
                m_combo++;
                if (m_combo > m_maxCombo) m_maxCombo = m_combo;
                m_score += 1000 * (1 + m_combo / 10);
                m_lastRating = HitRating::Perfect;
                return true;
            } else if (delta < -0.08 && delta >= -0.15) {
                // Too early
                n.hit = true;
                m_combo = 0;
                m_lastRating = HitRating::TooEarly;
                return true;
            } else if (delta > 0.08 && delta <= 0.15) {
                // Too late
                n.hit = true;
                m_combo = 0;
                m_lastRating = HitRating::TooLate;
                return true;
            }
        }
    }
    return false;
}

std::string RhythmTypingGame::renderText() const {
    std::ostringstream ss;
    ss << "=== RHYTHM TYPING ===\n";
    ss << "Song Time: " << std::fixed << std::setprecision(2) << m_songTime << "s\n";
    ss << "Last Rating: "
       << (m_lastRating == HitRating::Perfect ? "PERFECT!" :
           m_lastRating == HitRating::TooEarly ? "TOO EARLY!" :
           m_lastRating == HitRating::TooLate ? "TOO LATE!" : "MISS!")
       << " | Combo: " << m_combo << " (Max: " << m_maxCombo << ")\n";
    ss << "Score: " << m_score << "\n";
    return ss.str();
}

//===========================================================================
// 7. No-Mistake Mode Implementation
//===========================================================================
NoMistakeGame::NoMistakeGame() {
    m_textStream = U"hoc an hoc noi hoc goi hoc mo can than trong tung phim bam kien tri ben bi";
    reset();
}

void NoMistakeGame::start() {
    reset();
    m_paused = false;
    m_gameOver = false;
}

void NoMistakeGame::reset() {
    m_currentIndex = 0;
    m_combo = 0;
    m_level = 1;
    m_score = 10000; // Starting reserve
    m_paused = false;
    m_gameOver = false;
}

void NoMistakeGame::update(double dt) {
    (void)dt;
}

bool NoMistakeGame::handleKey(int vk, char32_t ch, bool down) {
    (void)vk;
    if (!down) return true;
    if (m_paused || m_gameOver) return true;

    if (m_currentIndex < m_textStream.size()) {
        char32_t target = m_textStream[m_currentIndex];
        if (ch == target) {
            m_currentIndex++;
            m_combo++;
            m_score += 100 * (1 + m_combo / 20);
            if (m_score > m_highScore) m_highScore = m_score;
            m_level = 1 + (m_combo / 50);

            if (m_currentIndex == m_textStream.size()) {
                m_currentIndex = 0; // loop stream
            }
            return true;
        } else {
            // Mistake penalty!
            if (m_combo > m_comboPenalty) m_combo -= m_comboPenalty;
            else m_combo = 0;

            m_score -= m_scorePenalty;
            if (m_score <= 0) {
                m_score = 0;
                m_gameOver = true;
            }
            return true;
        }
    }
    return false;
}

std::string NoMistakeGame::renderText() const {
    std::ostringstream ss;
    ss << "=== NO-MISTAKE MODE ===\n";
    ss << "Combo: " << m_combo << " | Level: " << m_level << " | Score: " << m_score << "\n";
    ss << "Stream: ";
    for (size_t i = 0; i < m_textStream.size(); ++i) {
        if (i == m_currentIndex) ss << "[" << static_cast<char>(m_textStream[i]) << "]";
        else ss << static_cast<char>(m_textStream[i]);
    }
    ss << "\n";
    if (m_gameOver) ss << "[GAME OVER - Penalty broke score reserve! Press R]\n";
    return ss.str();
}

//===========================================================================
// 8. Flexing Mode Implementation
//===========================================================================
FlexingGame::FlexingGame() {
    setPreloadedText(U"KieeKey la bo go tieng Viet hien dai co ca he thong Arcade vo ly nhat lich su!");
    reset();
}

void FlexingGame::setPreloadedText(std::u32string_view text) {
    m_preloadedText = text;
    m_cursor = 0;
}

void FlexingGame::start() {
    reset();
    m_active = true;
    m_paused = false;
    m_completed = false;
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
}

double FlexingGame::getEfficiencyMultiplier() const noexcept {
    if (m_actualKeypresses == 0) return 1.0;
    return static_cast<double>(m_generatedChars) / static_cast<double>(m_actualKeypresses);
}

std::u32string FlexingGame::popEmittedOutput() {
    std::u32string res = m_emittedBuffer;
    m_emittedBuffer.clear();
    return res;
}

void FlexingGame::update(double dt) {
    if (!m_active || m_paused || m_completed) return;
    m_elapsedSec += dt;

    if (m_gran == FlexGranularity::AutoStream && m_cursor < m_preloadedText.size()) {
        // Stream 15 chars/sec
        size_t count = static_cast<size_t>(15.0 * dt);
        if (count == 0) count = 1;
        for (size_t i = 0; i < count && m_cursor < m_preloadedText.size(); ++i) {
            char32_t c = m_preloadedText[m_cursor++];
            m_emittedBuffer.push_back(c);
            m_generatedChars++;
        }
    }

    if (m_elapsedSec > 0.0) {
        double minutes = m_elapsedSec / 60.0;
        m_displayedWpm = (static_cast<double>(m_generatedChars) / 5.0) / minutes;
    }
}

bool FlexingGame::handleKey(int vk, char32_t ch, bool down) {
    (void)vk; (void)ch;
    if (!down) return true;
    if (!m_active || m_paused || m_completed) return true;

    m_actualKeypresses++;

    if (m_cursor >= m_preloadedText.size()) {
        m_completed = true;
        return true;
    }

    size_t emitCount = 1;
    if (m_gran == FlexGranularity::OneWordPerKey) {
        // Emit until next space
        while (m_cursor < m_preloadedText.size()) {
            char32_t c = m_preloadedText[m_cursor++];
            m_emittedBuffer.push_back(c);
            m_generatedChars++;
            if (c == U' ' || c == U'\n') break;
        }
    } else if (m_gran == FlexGranularity::NCharsPerKey) {
        for (uint32_t i = 0; i < m_nChars && m_cursor < m_preloadedText.size(); ++i) {
            m_emittedBuffer.push_back(m_preloadedText[m_cursor++]);
            m_generatedChars++;
        }
    } else { // OneCharPerKey
        m_emittedBuffer.push_back(m_preloadedText[m_cursor++]);
        m_generatedChars++;
    }

    if (m_cursor >= m_preloadedText.size()) {
        m_completed = true;
    }
    return true;
}

std::string FlexingGame::renderText() const {
    std::ostringstream ss;
    ss << "=== FLEXING MODE ===\n";
    ss << "[DISCLAIMER: Flexing Mode does NOT measure real typing performance!]\n";
    ss << "Displayed WPM: " << static_cast<int>(m_displayedWpm)
       << " | Keypresses: " << m_actualKeypresses
       << " | Emitted Chars: " << m_generatedChars
       << " | Efficiency: " << std::fixed << std::setprecision(1) << (getEfficiencyMultiplier() * 100.0) << "%\n";
    return ss.str();
}

//===========================================================================
// Arcade Hub Manager Implementation
//===========================================================================
ArcadeManager& ArcadeManager::instance() noexcept {
    static ArcadeManager s_instance;
    return s_instance;
}

ArcadeManager::ArcadeManager() = default;
ArcadeManager::~ArcadeManager() {
    stopGame();
}

void ArcadeManager::launchGame(GameType type) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_gameInstance.reset();
    m_activeGame.store(nullptr, std::memory_order_release);

    switch (type) {
        case GameType::Snake:
            m_gameInstance = std::make_unique<SnakeGame>();
            break;
        case GameType::Tetris:
            m_gameInstance = std::make_unique<TetrisGame>();
            break;
        case GameType::Fishing:
            m_gameInstance = std::make_unique<FishingGame>();
            break;
        case GameType::TypingRace:
            m_gameInstance = std::make_unique<TypingRaceGame>();
            break;
        case GameType::WasdRace:
            m_gameInstance = std::make_unique<WasdRaceGame>();
            break;
        case GameType::Rhythm:
            m_gameInstance = std::make_unique<RhythmTypingGame>();
            break;
        case GameType::NoMistake:
            m_gameInstance = std::make_unique<NoMistakeGame>();
            break;
        case GameType::Flexing:
            m_gameInstance = std::make_unique<FlexingGame>();
            break;
        default:
            return;
    }

    if (m_gameInstance) {
        m_gameInstance->start();
        m_activeGame.store(m_gameInstance.get(), std::memory_order_release);
    }
}

void ArcadeManager::stopGame() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_activeGame.store(nullptr, std::memory_order_release);
    m_gameInstance.reset();
}

bool ArcadeManager::handleKey(int vk, char32_t ch, bool down) {
    IArcadeGame* game = m_activeGame.load(std::memory_order_acquire);
    if (!game) return false;
    return game->handleKey(vk, ch, down);
}

void ArcadeManager::update(double dt) {
    IArcadeGame* game = m_activeGame.load(std::memory_order_acquire);
    if (game) {
        game->update(dt);
    }
}

std::string ArcadeManager::renderCurrentGame() const {
    IArcadeGame* game = m_activeGame.load(std::memory_order_acquire);
    if (!game) return "No game active.\n";
    return game->renderText();
}

GameType ArcadeManager::getCurrentGameType() const {
    IArcadeGame* game = m_activeGame.load(std::memory_order_acquire);
    return game ? game->getType() : GameType::None;
}

IArcadeGame* ArcadeManager::getCurrentGame() {
    return m_activeGame.load(std::memory_order_acquire);
}

} // namespace ok::arcade
