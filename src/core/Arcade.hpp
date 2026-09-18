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
// File: src/core/Arcade.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — Arcade.hpp
// Arcade Hub Framework: Snake, Tetris, Fishing, Typing Race, WASD Racing,
// Rhythm Typing, No-Mistake Mode, and Flexing Mode.
//
// DESIGN & ISOLATION MANDATE:
//   * ZERO BACKGROUND OVERHEAD: No background threads or busy-loops when
//     inactive. Games are instantiated lazily on launch and fully destroyed
//     on stop.
//   * HOT PATH GUARD: An atomic pointer check (`isConsumingKeyboard()`)
//     costs < 1 ns on the typing hot path when Arcade is off.
//   * SEPARATION OF CONCERNS: Flexing Mode and game scores NEVER contaminate
//     official typing benchmarks or user progression.
//----------------------------------------------------------------------------
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ok::arcade {

enum class GameType : std::uint8_t {
    None = 0,
    Snake = 1,
    Tetris = 2,
    Fishing = 3,
    TypingRace = 4,
    WasdRace = 5,
    Rhythm = 6,
    NoMistake = 7,
    Flexing = 8,
};

class IArcadeGame {
public:
    virtual ~IArcadeGame() = default;

    virtual void start() = 0;
    virtual void reset() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    [[nodiscard]] virtual bool isPaused() const noexcept = 0;
    [[nodiscard]] virtual bool isGameOver() const noexcept = 0;

    virtual void update(double dt) = 0;
    virtual bool handleKey(int vk, char32_t ch, bool down) = 0;
    virtual std::string renderText() const = 0;

    [[nodiscard]] virtual int64_t getScore() const noexcept = 0;
    [[nodiscard]] virtual int64_t getHighScore() const noexcept = 0;
    [[nodiscard]] virtual GameType getType() const noexcept = 0;
};

//===========================================================================
// 1. Snake Game
//===========================================================================
struct SnakePoint {
    int x = 0;
    int y = 0;
    bool operator==(const SnakePoint& o) const noexcept { return x == o.x && y == o.y; }
};

enum class Direction : std::uint8_t { Up, Down, Left, Right };

class SnakeGame final : public IArcadeGame {
public:
    static constexpr int kWidth = 20;
    static constexpr int kHeight = 15;

    SnakeGame();
    ~SnakeGame() override = default;

    void start() override;
    void reset() override;
    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    [[nodiscard]] bool isPaused() const noexcept override { return m_paused; }
    [[nodiscard]] bool isGameOver() const noexcept override { return m_gameOver; }

    void update(double dt) override;
    bool handleKey(int vk, char32_t ch, bool down) override;
    std::string renderText() const override;

    [[nodiscard]] int64_t getScore() const noexcept override { return m_score; }
    [[nodiscard]] int64_t getHighScore() const noexcept override { return m_highScore; }
    [[nodiscard]] GameType getType() const noexcept override { return GameType::Snake; }

    // Keyboard input stress test: inject a flood of keystrokes
    void stressTestInputQueue(const std::vector<int>& vks);

private:
    void spawnFood();
    void step();

    std::deque<SnakePoint> m_body;
    SnakePoint m_food{5, 5};
    Direction m_dir = Direction::Right;
    Direction m_nextDir = Direction::Right;
    bool m_paused = false;
    bool m_gameOver = false;
    int64_t m_score = 0;
    int64_t m_highScore = 0;
    double m_tickTimer = 0.0;
    double m_tickInterval = 0.15; // Speed up as score increases
    uint32_t m_rngState = 0x12345678;
};

//===========================================================================
// 2. Tetris Game
//===========================================================================
class TetrisGame final : public IArcadeGame {
public:
    static constexpr int kCols = 10;
    static constexpr int kRows = 20;

    TetrisGame();
    ~TetrisGame() override = default;

    void start() override;
    void reset() override;
    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    [[nodiscard]] bool isPaused() const noexcept override { return m_paused; }
    [[nodiscard]] bool isGameOver() const noexcept override { return m_gameOver; }

    void update(double dt) override;
    bool handleKey(int vk, char32_t ch, bool down) override;
    std::string renderText() const override;

    [[nodiscard]] int64_t getScore() const noexcept override { return m_score; }
    [[nodiscard]] int64_t getHighScore() const noexcept override { return m_highScore; }
    [[nodiscard]] GameType getType() const noexcept override { return GameType::Tetris; }
    [[nodiscard]] uint32_t getLevel() const noexcept { return m_level; }
    [[nodiscard]] uint32_t getLinesCleared() const noexcept { return m_linesCleared; }

private:
    bool collides(int px, int py, int rot) const;
    void spawnPiece();
    void lockPiece();
    void clearLines();

    std::array<std::array<uint8_t, kCols>, kRows> m_grid{};
    int m_curPiece = 0;
    int m_curX = 3;
    int m_curY = 0;
    int m_curRot = 0;
    bool m_paused = false;
    bool m_gameOver = false;
    int64_t m_score = 0;
    int64_t m_highScore = 0;
    uint32_t m_linesCleared = 0;
    uint32_t m_level = 1;
    double m_dropTimer = 0.0;
    double m_dropInterval = 0.5;
    uint32_t m_rng = 0x87654321;
};

//===========================================================================
// 3. Fishing Game (Câu cá bằng gõ phím)
//===========================================================================
enum class FishRarity : std::uint8_t {
    Common = 0,
    Uncommon = 1,
    Rare = 2,
    Epic = 3,
    Legendary = 4,
};

enum class AutomationMode : std::uint8_t {
    Manual = 0,
    Assisted = 1,
    Automated = 2,
};

class FishingGame final : public IArcadeGame {
public:
    FishingGame();
    ~FishingGame() override = default;

    void start() override;
    void reset() override;
    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    [[nodiscard]] bool isPaused() const noexcept override { return m_paused; }
    [[nodiscard]] bool isGameOver() const noexcept override { return m_gameOver; }

    void update(double dt) override;
    bool handleKey(int vk, char32_t ch, bool down) override;
    std::string renderText() const override;

    [[nodiscard]] int64_t getScore() const noexcept override { return m_score; }
    [[nodiscard]] int64_t getHighScore() const noexcept override { return m_highScore; }
    [[nodiscard]] GameType getType() const noexcept override { return GameType::Fishing; }

    void setAutomationMode(AutomationMode mode) noexcept { m_autoMode = mode; }
    [[nodiscard]] AutomationMode getAutomationMode() const noexcept { return m_autoMode; }

    // Upgrades
    void upgradeRod() noexcept { if (m_rodLevel < 5) ++m_rodLevel; }
    void upgradeBait() noexcept { if (m_baitLevel < 5) ++m_baitLevel; }
    void upgradeReel() noexcept { if (m_reelLevel < 5) ++m_reelLevel; }
    [[nodiscard]] uint32_t getRodLevel() const noexcept { return m_rodLevel; }
    [[nodiscard]] uint32_t getBaitLevel() const noexcept { return m_baitLevel; }
    [[nodiscard]] uint32_t getReelLevel() const noexcept { return m_reelLevel; }

private:
    void hookNewFish();
    void onCatchSuccess();
    void onFishEscape();

    std::u32string m_prompt;
    size_t m_promptIndex = 0;
    FishRarity m_currentRarity = FishRarity::Common;
    std::string m_fishName;
    double m_pullProgress = 20.0; // 0.0 to 100.0%
    double m_lineTension = 50.0;  // 0.0 to 100.0%
    double m_escapeTimer = 18.0;
    bool m_paused = false;
    bool m_gameOver = false;
    int64_t m_score = 0;
    int64_t m_highScore = 0;
    uint32_t m_catches = 0;
    uint32_t m_escapes = 0;

    AutomationMode m_autoMode = AutomationMode::Manual;
    uint32_t m_rodLevel = 1;
    uint32_t m_baitLevel = 1;
    uint32_t m_reelLevel = 1;
    double m_autoTimer = 0.0;
    uint32_t m_rng = 0x55AA55AA;
};

//===========================================================================
// 4. Typing Race Game
//===========================================================================
class TypingRaceGame final : public IArcadeGame {
public:
    TypingRaceGame();
    ~TypingRaceGame() override = default;

    void start() override;
    void reset() override;
    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    [[nodiscard]] bool isPaused() const noexcept override { return m_paused; }
    [[nodiscard]] bool isGameOver() const noexcept override { return m_finished; }

    void update(double dt) override;
    bool handleKey(int vk, char32_t ch, bool down) override;
    std::string renderText() const override;

    [[nodiscard]] int64_t getScore() const noexcept override { return m_score; }
    [[nodiscard]] int64_t getHighScore() const noexcept override { return m_highScore; }
    [[nodiscard]] GameType getType() const noexcept override { return GameType::TypingRace; }

    [[nodiscard]] double getLiveWpm() const noexcept { return m_liveWpm; }
    [[nodiscard]] double getAccuracy() const noexcept { return m_accuracy; }
    [[nodiscard]] double getProgressPercent() const noexcept;

private:
    std::u32string m_passage;
    size_t m_charIndex = 0;
    uint32_t m_totalKeys = 0;
    uint32_t m_correctKeys = 0;
    double m_elapsedSec = 0.0;
    double m_liveWpm = 0.0;
    double m_accuracy = 100.0;
    bool m_paused = false;
    bool m_finished = false;
    int64_t m_score = 0;
    int64_t m_highScore = 0;
};

//===========================================================================
// 5. WASD + Typing Racing Game (Multitasking)
//===========================================================================
struct Obstacle {
    int lane = 1;   // 0: Left, 1: Center, 2: Right
    double dist = 100.0; // Distance ahead
};

class WasdRaceGame final : public IArcadeGame {
public:
    static constexpr int kLanes = 3;

    WasdRaceGame();
    ~WasdRaceGame() override = default;

    void start() override;
    void reset() override;
    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    [[nodiscard]] bool isPaused() const noexcept override { return m_paused; }
    [[nodiscard]] bool isGameOver() const noexcept override { return m_gameOver; }

    void update(double dt) override;
    bool handleKey(int vk, char32_t ch, bool down) override;
    std::string renderText() const override;

    [[nodiscard]] int64_t getScore() const noexcept override { return m_score; }
    [[nodiscard]] int64_t getHighScore() const noexcept override { return m_highScore; }
    [[nodiscard]] GameType getType() const noexcept override { return GameType::WasdRace; }

private:
    void spawnObstacle();

    std::u32string m_passage;
    size_t m_textIndex = 0;
    int m_playerLane = 1;
    double m_carSpeed = 60.0; // km/h
    double m_fuel = 100.0;
    std::vector<Obstacle> m_obstacles;
    double m_spawnTimer = 0.0;
    uint32_t m_dodges = 0;
    uint32_t m_collisions = 0;
    bool m_paused = false;
    bool m_gameOver = false;
    int64_t m_score = 0;
    int64_t m_highScore = 0;
    double m_elapsedSec = 0.0;
    uint32_t m_rng = 0xFEEDC0DE;
};

//===========================================================================
// 6. Rhythm Typing Game (FNF-Style)
//===========================================================================
enum class HitRating : std::uint8_t {
    Miss = 0,
    TooEarly = 1,
    Perfect = 2,
    TooLate = 3,
};

struct RhythmNote {
    char32_t ch;
    double targetTimeSec;
    bool hit = false;
};

class RhythmTypingGame final : public IArcadeGame {
public:
    RhythmTypingGame();
    ~RhythmTypingGame() override = default;

    void start() override;
    void reset() override;
    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    [[nodiscard]] bool isPaused() const noexcept override { return m_paused; }
    [[nodiscard]] bool isGameOver() const noexcept override { return m_gameOver; }

    void update(double dt) override;
    bool handleKey(int vk, char32_t ch, bool down) override;
    std::string renderText() const override;

    [[nodiscard]] int64_t getScore() const noexcept override { return m_score; }
    [[nodiscard]] int64_t getHighScore() const noexcept override { return m_highScore; }
    [[nodiscard]] GameType getType() const noexcept override { return GameType::Rhythm; }
    [[nodiscard]] uint32_t getCombo() const noexcept { return m_combo; }
    [[nodiscard]] uint32_t getMaxCombo() const noexcept { return m_maxCombo; }

private:
    std::vector<RhythmNote> m_notes;
    double m_songTime = 0.0;
    uint32_t m_combo = 0;
    uint32_t m_maxCombo = 0;
    HitRating m_lastRating = HitRating::Perfect;
    bool m_paused = false;
    bool m_gameOver = false;
    int64_t m_score = 0;
    int64_t m_highScore = 0;
};

//===========================================================================
// 7. No-Mistake Mode
//===========================================================================
class NoMistakeGame final : public IArcadeGame {
public:
    NoMistakeGame();
    ~NoMistakeGame() override = default;

    void start() override;
    void reset() override;
    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    [[nodiscard]] bool isPaused() const noexcept override { return m_paused; }
    [[nodiscard]] bool isGameOver() const noexcept override { return m_gameOver; }

    void update(double dt) override;
    bool handleKey(int vk, char32_t ch, bool down) override;
    std::string renderText() const override;

    [[nodiscard]] int64_t getScore() const noexcept override { return m_score; }
    [[nodiscard]] int64_t getHighScore() const noexcept override { return m_highScore; }
    [[nodiscard]] GameType getType() const noexcept override { return GameType::NoMistake; }
    [[nodiscard]] uint32_t getCombo() const noexcept { return m_combo; }

    void setMistakePenalty(int64_t scorePenalty, uint32_t comboPenalty) noexcept {
        m_scorePenalty = scorePenalty;
        m_comboPenalty = comboPenalty;
    }

private:
    std::u32string m_textStream;
    size_t m_currentIndex = 0;
    uint32_t m_combo = 0;
    uint32_t m_level = 1;
    bool m_paused = false;
    bool m_gameOver = false;
    int64_t m_score = 0;
    int64_t m_highScore = 0;
    int64_t m_scorePenalty = 120000;
    uint32_t m_comboPenalty = 250;
};

//===========================================================================
// 8. Flexing Mode (Intentional Joke / Entertainment Mode)
//===========================================================================
enum class FlexGranularity : std::uint8_t {
    OneCharPerKey = 0,
    OneWordPerKey = 1,
    NCharsPerKey = 2,
    AutoStream = 3,
};

class FlexingGame final : public IArcadeGame {
public:
    FlexingGame();
    ~FlexingGame() override = default;

    void setPreloadedText(std::u32string_view text);
    void setGranularity(FlexGranularity gran, uint32_t nChars = 3) noexcept {
        m_gran = gran;
        m_nChars = nChars;
    }

    void start() override;
    void reset() override;
    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    [[nodiscard]] bool isPaused() const noexcept override { return m_paused; }
    [[nodiscard]] bool isGameOver() const noexcept override { return m_completed; }

    void update(double dt) override;
    bool handleKey(int vk, char32_t ch, bool down) override;
    std::string renderText() const override;

    [[nodiscard]] int64_t getScore() const noexcept override { return static_cast<int64_t>(m_displayedWpm); }
    [[nodiscard]] int64_t getHighScore() const noexcept override { return 0; }
    [[nodiscard]] GameType getType() const noexcept override { return GameType::Flexing; }

    [[nodiscard]] double getDisplayedWpm() const noexcept { return m_displayedWpm; }
    [[nodiscard]] uint64_t getActualKeypresses() const noexcept { return m_actualKeypresses; }
    [[nodiscard]] uint64_t getGeneratedChars() const noexcept { return m_generatedChars; }
    [[nodiscard]] double getEfficiencyMultiplier() const noexcept;

    std::u32string popEmittedOutput();

private:
    std::u32string m_preloadedText;
    size_t m_cursor = 0;
    std::u32string m_emittedBuffer;
    FlexGranularity m_gran = FlexGranularity::OneCharPerKey;
    uint32_t m_nChars = 3;
    uint64_t m_actualKeypresses = 0;
    uint64_t m_generatedChars = 0;
    double m_elapsedSec = 0.0;
    double m_displayedWpm = 0.0;
    bool m_active = false;
    bool m_paused = false;
    bool m_completed = false;
};

//===========================================================================
// Arcade Hub Manager
//===========================================================================
class ArcadeManager {
public:
    static ArcadeManager& instance() noexcept;

    void launchGame(GameType type);
    void stopGame();

    [[nodiscard]] bool isConsumingKeyboard() const noexcept {
        return m_activeGame.load(std::memory_order_relaxed) != nullptr;
    }

    bool handleKey(int vk, char32_t ch, bool down);
    void update(double dt);
    std::string renderCurrentGame() const;

    [[nodiscard]] GameType getCurrentGameType() const;
    IArcadeGame* getCurrentGame();

private:
    ArcadeManager();
    ~ArcadeManager();

    std::atomic<IArcadeGame*> m_activeGame{nullptr};
    std::unique_ptr<IArcadeGame> m_gameInstance;
    mutable std::mutex m_mutex;
};

} // namespace ok::arcade
