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
// Arcade Hub: Snake, Tetris, Fishing, Typing Race, WASD Racing, Rhythm Typing,
// No-Mistake Mode and Flexing Mode.
//
// ARCHITECTURE (v1.3.0 GUI rework):
//   * MODEL/VIEW SPLIT: every game exposes pure rules (`update()`/`handleKey()`)
//     AND a drawable view (`buildFrame()` filling an `ok::arcade::Frame` of
//     primitives). The old ASCII-only `renderText()` survives as an explicitly
//     debug-only fallback — it is NOT a UI any more.
//   * ZERO BACKGROUND OVERHEAD: no threads, no timers, no polling. A game is
//     created on launch and destroyed on close; an inactive hub is an atomic
//     null check on the typing hot path.
//   * DETERMINISM: every game owns a seeded xorshift RNG; the same seed + the
//     same input script always produce the same score (benchmarked).
//   * ISOLATION: Arcade results never enter the official typing benchmarks;
//     the hub hands finished runs back through `pollRunResult()` so the
//     application can feed Progression/Ghost features explicitly.
//
// THREADING CONTRACT:
//   * `handleKey()` / `update()` / `buildFrame()` are called from the input
//     and UI threads only.
//   * The hub publishes the active game through a `std::shared_ptr` snapshot;
//     the previous game is kept alive by the reader's own reference, so a
//     concurrent `launchGame()`/`stopGame()` can never free a game that is
//     being used (the old raw-pointer + unique_ptr design could: use-after-free).
//----------------------------------------------------------------------------
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "ArcadeFrame.hpp"

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

//===========================================================================
// Input plumbing
//===========================================================================
struct InputEvent {
    int vk = 0;         // Win32 virtual key (0 when unknown / synthetic)
    char32_t ch = 0;    // the produced Unicode character (0 for modifier-only)
    bool down = true;
};

enum class InputResult : std::uint8_t {
    NotConsumed = 0,   // let the event continue to the IME
    Consumed = 1,      // the game handled it
    ExitRequested = 2, // the game wants to close (Esc / user quit)
};

// Fishing automation mode (declared early: the hub config references it).
enum class AutomationMode : std::uint8_t {
    Manual = 0,
    Assisted = 1,
    Automated = 2,
};

// Portable VK mirrors (the Win32 codes; kept numeric to stay dependency-free).
namespace vk {
inline constexpr int kBack = 0x08;
inline constexpr int kTab = 0x09;
inline constexpr int kReturn = 0x0D;
inline constexpr int kEscape = 0x1B;
inline constexpr int kPause = 0x13;
inline constexpr int kF1 = 0x70;
inline constexpr int kF2 = 0x71;
inline constexpr int kSpace = 0x20;
inline constexpr int kLeft = 0x25;
inline constexpr int kUp = 0x26;
inline constexpr int kRight = 0x27;
inline constexpr int kDown = 0x28;
} // namespace vk

//===========================================================================
// Deterministic RNG (xorshift32) — seedable, deterministic, allocation-free.
//===========================================================================
class Rng {
public:
    explicit Rng(std::uint32_t seed = 0x12345678u) noexcept { reseed(seed); }

    void reseed(std::uint32_t seed) noexcept {
        // xorshift32 must never be seeded with 0.
        m_state = (seed == 0u) ? 0x9E3779B9u : seed;
    }
    [[nodiscard]] std::uint32_t state() const noexcept { return m_state; }

    [[nodiscard]] std::uint32_t next() noexcept {
        std::uint32_t x = m_state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        m_state = x;
        return x;
    }
    [[nodiscard]] std::uint32_t below(std::uint32_t bound) noexcept {
        return bound == 0u ? 0u : (next() % bound);
    }
    [[nodiscard]] double unitDouble() noexcept {
        return static_cast<double>(next() >> 8) / static_cast<double>(1u << 24);
    }

private:
    std::uint32_t m_state;
};

//===========================================================================
// Run result — handed to the app when a run ends (progression / ghost feed)
//===========================================================================
struct RunResult {
    GameType type = GameType::None;
    std::int64_t score = 0;
    std::int64_t highScore = 0;
    std::uint32_t level = 1;
    std::uint32_t maxCombo = 0;
    double wpm = 0.0;
    double accuracy = 100.0;
    double durationSec = 0.0;
    bool completed = false;   // true = finished the objective (not just a crash)
};

//===========================================================================
// Game configuration (all user-visible knobs in one place)
//===========================================================================
struct ArcadeConfig {
    // Rhythm / FNF
    FailMode rhythmFailMode = FailMode::Hardcore;   // "nhanh quá chết, chậm quá cũng chết"
    double rhythmBpm = 112.0;
    std::uint32_t rhythmNoteCount = 64;
    double rhythmApproachSec = 1.8;

    // No-Mistake
    FailMode noMistakeFailMode = FailMode::Hardcore;
    std::int64_t noMistakeStartReserve = 10000;
    std::int64_t noMistakeScorePenalty = 120000;   // hardcore penalty (documented)
    std::int64_t noMistakeSoftPenalty = 2500;      // health-bar mode penalty
    std::uint32_t noMistakeComboPenalty = 250;

    // Typing race pacer (0 = no pacer car)
    double typingRacePacerWpm = 60.0;

    // WASD race
    double wasdStartFuel = 100.0;
    double wasdObstacleSpacingSec = 2.2;

    // Fishing automation ("Assisted" only drips progress, "Automated" is a demo)
    AutomationMode fishingAutomation = AutomationMode::Manual;

    // Frame rate clamp for update(): protects slow frames from teleporting
    // game objects (a 1-second stall must not advance physics by 1 second).
    double maxFrameStepSec = 0.05;
};

//===========================================================================
// Shared game interface
//===========================================================================
class IArcadeGame {
public:
    virtual ~IArcadeGame() = default;

    virtual GameType getType() const noexcept = 0;

    virtual void start() = 0;
    virtual void reset() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    [[nodiscard]] virtual bool isPaused() const noexcept = 0;
    [[nodiscard]] virtual bool isGameOver() const noexcept = 0;
    [[nodiscard]] virtual bool wantsExit() const noexcept = 0;

    // dt is the raw frame delta; implementations clamp it (maxFrameStepSec)
    // and use a fixed-step accumulator, so behaviour is frame-rate independent.
    virtual void update(double dt) = 0;
    virtual InputResult handleKey(const InputEvent& ev) = 0;

    // Drawable view (real graphics primitives, not text art).
    virtual void buildFrame(Frame& frame) const = 0;

    virtual void setSeed(std::uint32_t seed) = 0;
    [[nodiscard]] virtual std::uint32_t getSeed() const noexcept = 0;
    [[nodiscard]] virtual std::int64_t getScore() const noexcept = 0;
    [[nodiscard]] virtual std::int64_t getHighScore() const noexcept = 0;

    // ASCII fallback for logs/CLI only.
    [[nodiscard]] virtual std::string renderText() const = 0;

    // One-shot run result (true exactly once per finished run).
    virtual bool pollRunResult(RunResult& out) { (void)out; return false; }

protected:
    // Pause / restart helpers.
    //
    // `allowLetterAlias` must stay FALSE for every game whose text can contain
    // the letter: v1.3.0-beta1 bound "P" and "R" in *all* games, so typing the
    // word "phim" paused No-Mistake Mode and the letter "r" restarted it —
    // the mode was literally unwinnable. Typing games therefore use
    // F1 (pause) / F2 (restart) / Esc only.
    [[nodiscard]] static bool isPauseKey(const InputEvent& ev, bool allowLetterAlias = false) noexcept {
        if (!ev.down) {
            return false;
        }
        if (ev.vk == vk::kPause || ev.vk == vk::kF1) {
            return true;
        }
        return allowLetterAlias && (ev.ch == U'p' || ev.ch == U'P');
    }
    [[nodiscard]] static bool isRestartKey(const InputEvent& ev, bool allowLetterAlias = false) noexcept {
        if (!ev.down) {
            return false;
        }
        if (ev.vk == vk::kF2) {
            return true;
        }
        return allowLetterAlias && (ev.ch == U'r' || ev.ch == U'R');
    }
    [[nodiscard]] static bool isExitKey(const InputEvent& ev) noexcept {
        return ev.down && ev.vk == vk::kEscape;
    }
};

//===========================================================================
// 1. Snake
//===========================================================================
struct SnakePoint {
    int x = 0;
    int y = 0;
    bool operator==(const SnakePoint& o) const noexcept { return x == o.x && y == o.y; }
    bool operator!=(const SnakePoint& o) const noexcept { return !(*this == o); }
};

enum class Direction : std::uint8_t { Up, Down, Left, Right };

class SnakeGame final : public IArcadeGame {
public:
    static constexpr int kWidth = 20;
    static constexpr int kHeight = 15;

    SnakeGame();

    [[nodiscard]] GameType getType() const noexcept override { return GameType::Snake; }

    void start() override;
    void reset() override;
    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    [[nodiscard]] bool isPaused() const noexcept override { return m_paused; }
    [[nodiscard]] bool isGameOver() const noexcept override { return m_gameOver; }
    [[nodiscard]] bool wantsExit() const noexcept override { return m_exitRequested; }

    void update(double dt) override;
    InputResult handleKey(const InputEvent& ev) override;
    void buildFrame(Frame& frame) const override;

    void setSeed(std::uint32_t seed) override { m_rng.reseed(seed); m_seed = seed; }
    [[nodiscard]] std::uint32_t getSeed() const noexcept override { return m_seed; }
    [[nodiscard]] std::int64_t getScore() const noexcept override { return m_score; }
    [[nodiscard]] std::int64_t getHighScore() const noexcept override { return m_highScore; }

    bool pollRunResult(RunResult& out) override;
    [[nodiscard]] std::string renderText() const override;

    //---- view/state accessors (used by the frame builder and by tests)
    [[nodiscard]] const std::deque<SnakePoint>& getBody() const noexcept { return m_body; }
    [[nodiscard]] SnakePoint getFood() const noexcept { return m_food; }
    [[nodiscard]] Direction getDirection() const noexcept { return m_dir; }
    [[nodiscard]] double getTickInterval() const noexcept { return m_tickInterval; }

    // Keyboard input stress test: inject a flood of keystrokes through the
    // same code path as real input (previously it bypassed pause/step logic).
    void stressTestInputQueue(const std::vector<int>& vks);

private:
    void spawnFood();
    void step();
    void die();

    std::deque<SnakePoint> m_body;
    SnakePoint m_food{5, 5};
    Direction m_dir = Direction::Right;
    Direction m_nextDir = Direction::Right;
    bool m_paused = false;
    bool m_gameOver = false;
    bool m_exitRequested = false;
    bool m_resultPending = false;
    std::int64_t m_score = 0;
    std::int64_t m_highScore = 0;
    double m_tickTimer = 0.0;
    double m_tickInterval = 0.15;
    double m_runTimeSec = 0.0;
    Rng m_rng{0x12345678u};
    std::uint32_t m_seed = 0x12345678u;
};

//===========================================================================
// 2. Tetris
//===========================================================================
class TetrisGame final : public IArcadeGame {
public:
    static constexpr int kCols = 10;
    static constexpr int kRows = 20;

    TetrisGame();

    [[nodiscard]] GameType getType() const noexcept override { return GameType::Tetris; }

    void start() override;
    void reset() override;
    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    [[nodiscard]] bool isPaused() const noexcept override { return m_paused; }
    [[nodiscard]] bool isGameOver() const noexcept override { return m_gameOver; }
    [[nodiscard]] bool wantsExit() const noexcept override { return m_exitRequested; }

    void update(double dt) override;
    InputResult handleKey(const InputEvent& ev) override;
    void buildFrame(Frame& frame) const override;

    void setSeed(std::uint32_t seed) override;
    [[nodiscard]] std::uint32_t getSeed() const noexcept override { return m_seed; }
    [[nodiscard]] std::int64_t getScore() const noexcept override { return m_score; }
    [[nodiscard]] std::int64_t getHighScore() const noexcept override { return m_highScore; }

    bool pollRunResult(RunResult& out) override;
    [[nodiscard]] std::string renderText() const override;

    [[nodiscard]] std::uint32_t getLevel() const noexcept { return m_level; }
    [[nodiscard]] std::uint32_t getLinesCleared() const noexcept { return m_linesCleared; }
    [[nodiscard]] int getCurrentPiece() const noexcept { return m_curPiece; }
    [[nodiscard]] int getCurrentX() const noexcept { return m_curX; }
    [[nodiscard]] int getCurrentY() const noexcept { return m_curY; }
    [[nodiscard]] int getCurrentRotation() const noexcept { return m_curRot; }
    [[nodiscard]] int getNextPiece() const noexcept { return m_nextPiece; }
    [[nodiscard]] const std::array<std::array<std::uint8_t, kCols>, kRows>& getGrid() const noexcept {
        return m_grid;
    }
    [[nodiscard]] int getGhostY() const noexcept;   // hard-drop landing row for the current piece
    // Public tetromino geometry: 8 ints (dx0,dy0,dx1,dy1,dx2,dy2,dx3,dy3) for
    // the 4 cells of `piece` in `rotation`. Used by the tests as an
    // independent oracle and by front-ends that draw piece previews.
    [[nodiscard]] static const int* pieceCellTable(int piece, int rot) noexcept;

private:
    bool collides(int px, int py, int rot) const;
    bool collidesPiece(int piece, int px, int py, int rot) const;
    bool tryRotate(int delta);
    bool tryMove(int dx, int dy);
    void hardDrop();
    void spawnPiece();
    void lockPiece();
    void clearLines();
    void refillBag();
    [[nodiscard]] int nextFromBag();
    void finish();

    std::array<std::array<std::uint8_t, kCols>, kRows> m_grid{};
    std::array<int, 7> m_bag{};
    int m_bagIndex = 7;
    int m_curPiece = 0;
    int m_nextPiece = 0;
    int m_curX = 3;
    int m_curY = 0;
    int m_curRot = 0;
    bool m_paused = false;
    bool m_gameOver = false;
    bool m_exitRequested = false;
    bool m_resultPending = false;
    std::int64_t m_score = 0;
    std::int64_t m_highScore = 0;
    std::uint32_t m_linesCleared = 0;
    std::uint32_t m_level = 1;
    double m_dropTimer = 0.0;
    double m_dropInterval = 0.6;
    double m_runTimeSec = 0.0;
    double m_lockTimer = 0.0;
    Rng m_rng{0x87654321u};
    std::uint32_t m_seed = 0x87654321u;
};

//===========================================================================
// 3. Fishing (Câu cá bằng gõ phím)
//===========================================================================
enum class FishRarity : std::uint8_t {
    Common = 0,
    Uncommon = 1,
    Rare = 2,
    Epic = 3,
    Legendary = 4,
};

class FishingGame final : public IArcadeGame {
public:
    FishingGame();

    [[nodiscard]] GameType getType() const noexcept override { return GameType::Fishing; }

    void start() override;
    void reset() override;
    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    [[nodiscard]] bool isPaused() const noexcept override { return m_paused; }
    [[nodiscard]] bool isGameOver() const noexcept override { return m_gameOver; }
    [[nodiscard]] bool wantsExit() const noexcept override { return m_exitRequested; }

    void update(double dt) override;
    InputResult handleKey(const InputEvent& ev) override;
    void buildFrame(Frame& frame) const override;

    void setSeed(std::uint32_t seed) override { m_rng.reseed(seed); m_seed = seed; }
    [[nodiscard]] std::uint32_t getSeed() const noexcept override { return m_seed; }
    [[nodiscard]] std::int64_t getScore() const noexcept override { return m_score; }
    [[nodiscard]] std::int64_t getHighScore() const noexcept override { return m_highScore; }

    bool pollRunResult(RunResult& out) override;
    [[nodiscard]] std::string renderText() const override;

    void setAutomationMode(AutomationMode mode) noexcept { m_autoMode = mode; }
    [[nodiscard]] AutomationMode getAutomationMode() const noexcept { return m_autoMode; }

    // Upgrades (bounded, 1..5). Effects: rod = score, bait = rarity, reel = pull.
    void upgradeRod() noexcept { if (m_rodLevel < 5) ++m_rodLevel; }
    void upgradeBait() noexcept { if (m_baitLevel < 5) ++m_baitLevel; }
    void upgradeReel() noexcept { if (m_reelLevel < 5) ++m_reelLevel; }
    [[nodiscard]] std::uint32_t getRodLevel() const noexcept { return m_rodLevel; }
    [[nodiscard]] std::uint32_t getBaitLevel() const noexcept { return m_baitLevel; }
    [[nodiscard]] std::uint32_t getReelLevel() const noexcept { return m_reelLevel; }

    [[nodiscard]] std::u32string_view getPrompt() const noexcept { return m_prompt; }
    [[nodiscard]] std::size_t getPromptIndex() const noexcept { return m_promptIndex; }
    [[nodiscard]] FishRarity getRarity() const noexcept { return m_currentRarity; }
    [[nodiscard]] std::string_view getFishName() const noexcept { return m_fishName; }
    [[nodiscard]] double getPullProgress() const noexcept { return m_pullProgress; }
    [[nodiscard]] double getTension() const noexcept { return m_lineTension; }
    [[nodiscard]] double getEscapeTimer() const noexcept { return m_escapeTimer; }
    [[nodiscard]] std::uint32_t getCatches() const noexcept { return m_catches; }
    [[nodiscard]] std::uint32_t getEscapes() const noexcept { return m_escapes; }

private:
    void hookNewFish();
    void onCatchSuccess();
    void onFishEscape();
    [[nodiscard]] double rarityResistance() const noexcept;
    [[nodiscard]] double rarityTensionGain() const noexcept;

    std::u32string m_prompt;
    std::size_t m_promptIndex = 0;
    FishRarity m_currentRarity = FishRarity::Common;
    // Reserved once in the constructor: hookNewFish()/renderer reuse the same
    // buffers, so a fishing session allocates nothing between catches.
    std::string m_fishName;
    std::u32string m_displayFish;
    double m_pullProgress = 20.0;
    double m_lineTension = 30.0;
    double m_escapeTimer = 18.0;
    bool m_paused = false;
    bool m_gameOver = false;
    bool m_exitRequested = false;
    bool m_resultPending = false;
    std::int64_t m_score = 0;
    std::int64_t m_highScore = 0;
    std::uint32_t m_catches = 0;
    std::uint32_t m_escapes = 0;
    double m_runTimeSec = 0.0;

    AutomationMode m_autoMode = AutomationMode::Manual;
    std::uint32_t m_rodLevel = 1;
    std::uint32_t m_baitLevel = 1;
    std::uint32_t m_reelLevel = 1;
    double m_autoTimer = 0.0;
    Rng m_rng{0x55AA55AAu};
    std::uint32_t m_seed = 0x55AA55AAu;
};

//===========================================================================
// 4. Typing Race (🏎️ đua xe bằng tốc độ gõ)
//===========================================================================
class TypingRaceGame final : public IArcadeGame {
public:
    static constexpr double kTrackLength = 100.0;   // world units, cars advance along it

    TypingRaceGame();

    [[nodiscard]] GameType getType() const noexcept override { return GameType::TypingRace; }

    void start() override;
    void reset() override;
    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    [[nodiscard]] bool isPaused() const noexcept override { return m_paused; }
    [[nodiscard]] bool isGameOver() const noexcept override { return m_finished; }
    [[nodiscard]] bool wantsExit() const noexcept override { return m_exitRequested; }

    void update(double dt) override;
    InputResult handleKey(const InputEvent& ev) override;
    void buildFrame(Frame& frame) const override;

    void setSeed(std::uint32_t seed) override { m_seed = seed; }
    [[nodiscard]] std::uint32_t getSeed() const noexcept override { return m_seed; }
    [[nodiscard]] std::int64_t getScore() const noexcept override { return m_score; }
    [[nodiscard]] std::int64_t getHighScore() const noexcept override { return m_highScore; }

    bool pollRunResult(RunResult& out) override;
    [[nodiscard]] std::string renderText() const override;

    void setPassage(std::u32string_view passage);
    [[nodiscard]] std::u32string_view getPassage() const noexcept { return m_passage; }
    [[nodiscard]] std::size_t getCharIndex() const noexcept { return m_charIndex; }
    void setPacerWpm(double wpm) noexcept { m_pacerWpm = wpm; }
    [[nodiscard]] double getPacerWpm() const noexcept { return m_pacerWpm; }
    [[nodiscard]] double getPacerProgress() const noexcept { return m_pacerProgress; }
    [[nodiscard]] double getLiveWpm() const noexcept { return m_liveWpm; }
    [[nodiscard]] double getAccuracy() const noexcept { return m_accuracy; }
    [[nodiscard]] double getElapsedSec() const noexcept { return m_elapsedSec; }
    [[nodiscard]] double getPlayerProgress() const noexcept { return getProgressPercent(); }
    [[nodiscard]] double getProgressPercent() const noexcept;

private:
    void finish();

    std::u32string m_passage;
    std::size_t m_charIndex = 0;
    std::uint32_t m_totalKeys = 0;
    std::uint32_t m_correctKeys = 0;
    std::uint32_t m_errorKeys = 0;
    double m_elapsedSec = 0.0;
    double m_liveWpm = 0.0;
    double m_accuracy = 100.0;
    double m_pacerWpm = 60.0;
    double m_pacerProgress = 0.0;
    bool m_paused = false;
    bool m_finished = false;
    bool m_exitRequested = false;
    bool m_resultPending = false;
    std::int64_t m_score = 0;
    std::int64_t m_highScore = 0;
    std::uint32_t m_seed = 0xABCDEF01u;
};

//===========================================================================
// 5. WASD + Typing Racing (multitasking)
//===========================================================================
struct Obstacle {
    int lane = 1;            // 0 left, 1 center, 2 right
    double dist = 100.0;     // distance ahead of the player (decreases)
    bool hit = false;
};

class WasdRaceGame final : public IArcadeGame {
public:
    static constexpr int kLanes = 3;
    static constexpr double kRoadLength = 110.0;

    WasdRaceGame();

    [[nodiscard]] GameType getType() const noexcept override { return GameType::WasdRace; }

    void start() override;
    void reset() override;
    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    [[nodiscard]] bool isPaused() const noexcept override { return m_paused; }
    [[nodiscard]] bool isGameOver() const noexcept override { return m_gameOver; }
    [[nodiscard]] bool wantsExit() const noexcept override { return m_exitRequested; }

    void update(double dt) override;
    InputResult handleKey(const InputEvent& ev) override;
    void buildFrame(Frame& frame) const override;

    void setSeed(std::uint32_t seed) override { m_rng.reseed(seed); m_seed = seed; }
    [[nodiscard]] std::uint32_t getSeed() const noexcept override { return m_seed; }
    [[nodiscard]] std::int64_t getScore() const noexcept override { return m_score; }
    [[nodiscard]] std::int64_t getHighScore() const noexcept override { return m_highScore; }

    bool pollRunResult(RunResult& out) override;
    [[nodiscard]] std::string renderText() const override;

    void setStartFuel(double fuel) noexcept { m_startFuel = fuel; m_fuel = fuel; }
    void setObstacleSpacing(double sec) noexcept { m_obstacleSpacingSec = std::max(0.6, sec); }
    [[nodiscard]] double getObstacleSpacing() const noexcept { return m_obstacleSpacingSec; }
    [[nodiscard]] std::u32string_view getPassage() const noexcept { return m_passage; }
    [[nodiscard]] std::size_t getTextIndex() const noexcept { return m_textIndex; }
    [[nodiscard]] int getPlayerLane() const noexcept { return m_playerLane; }
    [[nodiscard]] double getCarSpeed() const noexcept { return m_carSpeed; }
    [[nodiscard]] double getFuel() const noexcept { return m_fuel; }
    [[nodiscard]] double getDistance() const noexcept { return m_distance; }
    [[nodiscard]] const std::vector<Obstacle>& getObstacles() const noexcept { return m_obstacles; }
    [[nodiscard]] std::uint32_t getDodges() const noexcept { return m_dodges; }
    [[nodiscard]] std::uint32_t getCollisions() const noexcept { return m_collisions; }

private:
    void spawnObstacle();

    std::u32string m_passage;
    std::size_t m_textIndex = 0;
    int m_playerLane = 1;
    double m_carSpeed = 60.0;
    double m_fuel = 100.0;
    double m_startFuel = 100.0;
    double m_distance = 0.0;
    std::vector<Obstacle> m_obstacles;
    double m_spawnTimer = 0.0;
    double m_obstacleSpacingSec = 2.2;
    std::uint32_t m_dodges = 0;
    std::uint32_t m_collisions = 0;
    bool m_paused = false;
    bool m_gameOver = false;
    bool m_exitRequested = false;
    bool m_resultPending = false;
    int64_t m_score = 0;
    std::int64_t m_highScore = 0;
    double m_elapsedSec = 0.0;
    Rng m_rng{0xFEEDC0DEu};
    std::uint32_t m_seed = 0xFEEDC0DEu;
};

//===========================================================================
// 6. Rhythm Typing (FNF-style)
//===========================================================================
enum class HitRating : std::uint8_t {
    Miss = 0,
    TooEarly = 1,
    TooLate = 2,
    Good = 3,
    Perfect = 4,
};

enum class NoteState : std::uint8_t {
    Pending = 0,
    Hit = 1,
    Missed = 2,
};

struct RhythmNote {
    char32_t ch = 0;            // the lane key the player must press
    std::uint8_t lane = 0;      // 0..3
    double targetTimeSec = 0.0;
    NoteState state = NoteState::Pending;
    HitRating rating = HitRating::Perfect;
};

class RhythmTypingGame final : public IArcadeGame {
public:
    static constexpr std::uint32_t kLaneCount = 4;
    // Judgment windows (seconds, absolute delta to the note time):
    //   |Δ| <= kPerfectWindow          -> Perfect
    //   kPerfectWindow < |Δ| <= kGood  -> Good
    //   kGood < |Δ| <= kLate           -> TooEarly / TooLate  (= a miss in Hardcore)
    //   |Δ| >  kLate                   -> not this note
    static constexpr double kPerfectWindowSec = 0.070;
    static constexpr double kGoodWindowSec = 0.110;
    static constexpr double kLateWindowSec = 0.170;

    RhythmTypingGame();

    [[nodiscard]] GameType getType() const noexcept override { return GameType::Rhythm; }

    void start() override;
    void reset() override;
    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    [[nodiscard]] bool isPaused() const noexcept override { return m_paused; }
    [[nodiscard]] bool isGameOver() const noexcept override { return m_gameOver; }
    [[nodiscard]] bool wantsExit() const noexcept override { return m_exitRequested; }

    void update(double dt) override;
    InputResult handleKey(const InputEvent& ev) override;
    void buildFrame(Frame& frame) const override;

    void setSeed(std::uint32_t seed) override;
    [[nodiscard]] std::uint32_t getSeed() const noexcept override { return m_seed; }
    [[nodiscard]] std::int64_t getScore() const noexcept override { return m_score; }
    [[nodiscard]] std::int64_t getHighScore() const noexcept override { return m_highScore; }

    bool pollRunResult(RunResult& out) override;
    [[nodiscard]] std::string renderText() const override;

    // Configuration
    void setFailMode(FailMode mode) noexcept { m_failMode = mode; }
    [[nodiscard]] FailMode getFailMode() const noexcept { return m_failMode; }
    void setBpm(double bpm) noexcept;
    [[nodiscard]] double getBpm() const noexcept { return m_bpm; }
    void setNoteCount(std::uint32_t count) noexcept;
    [[nodiscard]] std::uint32_t getNoteCount() const noexcept {
        return static_cast<std::uint32_t>(m_notes.size());
    }
    void setApproachSec(double sec) noexcept;
    [[nodiscard]] double getApproachSec() const noexcept { return m_approachSec; }

    // View/state accessors
    [[nodiscard]] double getSongTime() const noexcept { return m_songTime; }
    [[nodiscard]] const std::vector<RhythmNote>& getNotes() const noexcept { return m_notes; }
    [[nodiscard]] std::uint32_t getCombo() const noexcept { return m_combo; }
    [[nodiscard]] std::uint32_t getMaxCombo() const noexcept { return m_maxCombo; }
    [[nodiscard]] HitRating getLastRating() const noexcept { return m_lastRating; }
    [[nodiscard]] double getHealth() const noexcept { return m_health; }
    [[nodiscard]] double getHealthMax() const noexcept { return 100.0; }
    [[nodiscard]] std::uint32_t getJudgedCount() const noexcept { return m_judged; }
    [[nodiscard]] std::uint32_t getPerfectCount() const noexcept { return m_perfects; }
    [[nodiscard]] std::uint32_t getMissCount() const noexcept { return m_misses; }
    [[nodiscard]] std::string_view getFailReason() const noexcept { return m_failReason; }

private:
    void generateChart();
    void judge(int lane);
    void registerOutcome(HitRating rating, bool fromExtraKey);
    [[nodiscard]] int laneForKey(char32_t ch) const noexcept;

    std::vector<RhythmNote> m_notes;
    std::uint32_t m_noteCount = 64;
    double m_songTime = 0.0;
    double m_bpm = 112.0;
    double m_approachSec = 1.8;
    double m_beatSec = 0.536;
    std::uint32_t m_combo = 0;
    std::uint32_t m_maxCombo = 0;
    HitRating m_lastRating = HitRating::Perfect;
    FailMode m_failMode = FailMode::Hardcore;
    double m_health = 100.0;
    std::uint32_t m_judged = 0;
    std::uint32_t m_perfects = 0;
    std::uint32_t m_misses = 0;
    double m_ratingFlashSec = 0.0;
    std::string m_failReason;
    bool m_paused = false;
    bool m_gameOver = false;
    bool m_finished = false;
    bool m_exitRequested = false;
    bool m_resultPending = false;
    std::int64_t m_score = 0;
    std::int64_t m_highScore = 0;
    Rng m_rng{0x5A5A1234u};
    std::uint32_t m_seed = 0x5A5A1234u;
};

//===========================================================================
// 7. No-Mistake Mode
//===========================================================================
class NoMistakeGame final : public IArcadeGame {
public:
    NoMistakeGame();

    [[nodiscard]] GameType getType() const noexcept override { return GameType::NoMistake; }

    void start() override;
    void reset() override;
    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    [[nodiscard]] bool isPaused() const noexcept override { return m_paused; }
    [[nodiscard]] bool isGameOver() const noexcept override { return m_gameOver; }
    [[nodiscard]] bool wantsExit() const noexcept override { return m_exitRequested; }

    void update(double dt) override;
    InputResult handleKey(const InputEvent& ev) override;
    void buildFrame(Frame& frame) const override;

    void setSeed(std::uint32_t seed) override { m_seed = seed; }
    [[nodiscard]] std::uint32_t getSeed() const noexcept override { return m_seed; }
    [[nodiscard]] std::int64_t getScore() const noexcept override { return m_score; }
    [[nodiscard]] std::int64_t getHighScore() const noexcept override { return m_highScore; }

    bool pollRunResult(RunResult& out) override;
    [[nodiscard]] std::string renderText() const override;

    void setMistakePenalty(std::int64_t scorePenalty, std::uint32_t comboPenalty) noexcept {
        m_scorePenalty = scorePenalty;
        m_comboPenalty = comboPenalty;
    }
    [[nodiscard]] std::int64_t getScorePenalty() const noexcept { return m_scorePenalty; }
    void setSoftPenalty(std::int64_t penalty) noexcept { m_softPenalty = std::max<std::int64_t>(1, penalty); }
    [[nodiscard]] std::int64_t getSoftPenalty() const noexcept { return m_softPenalty; }
    [[nodiscard]] std::uint32_t getComboPenalty() const noexcept { return m_comboPenalty; }
    void setFailMode(FailMode mode) noexcept { m_failMode = mode; }
    [[nodiscard]] FailMode getFailMode() const noexcept { return m_failMode; }
    void setStartReserve(std::int64_t reserve) noexcept;
    [[nodiscard]] std::int64_t getStartReserve() const noexcept { return m_startReserve; }
    [[nodiscard]] std::u32string_view getTextStream() const noexcept { return m_textStream; }
    [[nodiscard]] std::size_t getCurrentIndex() const noexcept { return m_currentIndex; }
    [[nodiscard]] std::uint32_t getCombo() const noexcept { return m_combo; }
    [[nodiscard]] std::uint32_t getLevel() const noexcept { return m_level; }
    [[nodiscard]] std::uint32_t getMistakes() const noexcept { return m_mistakes; }

private:
    std::u32string m_textStream;
    std::size_t m_currentIndex = 0;
    std::uint32_t m_combo = 0;
    std::uint32_t m_maxCombo = 0;
    std::uint32_t m_level = 1;
    std::uint32_t m_mistakes = 0;
    bool m_paused = false;
    bool m_gameOver = false;
    bool m_finished = false;
    bool m_exitRequested = false;
    bool m_resultPending = false;
    std::int64_t m_score = 10000;
    std::int64_t m_highScore = 0;
    std::int64_t m_scorePenalty = 120000;
    std::int64_t m_softPenalty = 2500;
    std::int64_t m_startReserve = 10000;
    std::uint32_t m_comboPenalty = 250;
    FailMode m_failMode = FailMode::Hardcore;
    double m_elapsedSec = 0.0;
    std::uint32_t m_seed = 0x11223344u;
};

//===========================================================================
// 8. Flexing Mode (intentional joke / entertainment mode)
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

    [[nodiscard]] GameType getType() const noexcept override { return GameType::Flexing; }

    void setPreloadedText(std::u32string_view text);
    [[nodiscard]] std::u32string_view getPreloadedText() const noexcept { return m_preloadedText; }
    void setGranularity(FlexGranularity gran, std::uint32_t nChars = 3) noexcept;
    [[nodiscard]] FlexGranularity getGranularity() const noexcept { return m_gran; }

    void start() override;
    void reset() override;
    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    [[nodiscard]] bool isPaused() const noexcept override { return m_paused; }
    [[nodiscard]] bool isGameOver() const noexcept override { return m_completed; }
    [[nodiscard]] bool wantsExit() const noexcept override { return m_exitRequested; }

    void update(double dt) override;
    InputResult handleKey(const InputEvent& ev) override;
    void buildFrame(Frame& frame) const override;

    void setSeed(std::uint32_t seed) override { m_seed = seed; }
    [[nodiscard]] std::uint32_t getSeed() const noexcept override { return m_seed; }
    [[nodiscard]] std::int64_t getScore() const noexcept override {
        // Entertainment only: the "score" is the displayed WPM, explicitly
        // excluded from progression and official benchmarks.
        return static_cast<std::int64_t>(m_displayedWpm);
    }
    [[nodiscard]] std::int64_t getHighScore() const noexcept override { return 0; }

    bool pollRunResult(RunResult& out) override;
    [[nodiscard]] std::string renderText() const override;

    [[nodiscard]] double getDisplayedWpm() const noexcept { return m_displayedWpm; }
    [[nodiscard]] std::uint64_t getActualKeypresses() const noexcept { return m_actualKeypresses; }
    [[nodiscard]] std::uint64_t getGeneratedChars() const noexcept { return m_generatedChars; }
    [[nodiscard]] double getEfficiencyMultiplier() const noexcept;
    [[nodiscard]] std::size_t getCursor() const noexcept { return m_cursor; }

    // Text produced since the last call (the app layer injects it when the
    // user enabled "type for real"; the game itself never touches the OS).
    std::u32string popEmittedOutput();

private:
    std::u32string m_preloadedText;
    std::size_t m_cursor = 0;
    std::u32string m_emittedBuffer;
    FlexGranularity m_gran = FlexGranularity::OneCharPerKey;
    std::uint32_t m_nChars = 3;
    std::uint64_t m_actualKeypresses = 0;
    std::uint64_t m_generatedChars = 0;
    double m_elapsedSec = 0.0;
    double m_displayedWpm = 0.0;
    double m_streamCredit = 0.0;
    bool m_active = false;
    bool m_paused = false;
    bool m_completed = false;
    bool m_exitRequested = false;
    bool m_resultPending = false;
    std::uint32_t m_seed = 0x77777777u;
};

//===========================================================================
// Arcade Hub Manager
//===========================================================================
class ArcadeManager {
public:
    static ArcadeManager& instance() noexcept;

    //---- lifecycle -------------------------------------------------------
    bool launchGame(GameType type);
    bool launchGame(GameType type, std::uint32_t seed);
    void stopGame();
    void restartGame();

    [[nodiscard]] bool hasActiveGame() const noexcept;
    // True while a live game owns the keyboard (false once it ends/exits, so
    // the IME is never held hostage by a finished game).
    [[nodiscard]] bool isConsumingKeyboard() const noexcept;

    //---- per-frame ---------------------------------------------------------
    // `dt` seconds; the hub clamps it and every game uses a fixed-step
    // accumulator, so 30/60/144 Hz behave the same.
    void update(double dt);

    // Full input event (key down AND up). Returns the game's verdict; the hub
    // additionally honours an exit request by deactivating the game.
    InputResult handleKey(const InputEvent& ev);
    InputResult handleKey(int vk, char32_t ch, bool down);

    // Ready-to-draw frame (valid until the next update()/launch).
    [[nodiscard]] const Frame& getFrame() const;
    [[nodiscard]] std::string renderCurrentGame() const;

    [[nodiscard]] GameType getCurrentGameType() const;
    IArcadeGame* getCurrentGame();
    [[nodiscard]] const IArcadeGame* getCurrentGame() const;

    //---- configuration ----------------------------------------------------
    // Stores the config AND pushes the live-safe subset to the running game
    // (fail mode / penalties / pacer / automation). Chart-building knobs
    // (rhythm BPM + note count + approach, no-mistake reserve, WASD start
    // fuel) still take effect on the next launch, because their setters
    // regenerate or reset the run.
    void setConfig(const ArcadeConfig& config);

    // v1.3.0: true when this config differs from the current one only in knobs
    // whose setter regenerates/resets a run (rhythm chart, no-mistake reserve,
    // WASD start fuel). The web bridge answers `restartRequired` with exactly
    // this instead of silently swallowing the user's slider.
    [[nodiscard]] bool configNeedsRelaunch(const ArcadeConfig& config) const;

    // Recreates the current RUN with the stored config (launch + start), so the
    // knobs above become visible without the player leaving the game.
    // Returns false when no game is running.
    bool relaunchCurrentGame();

    [[nodiscard]] ArcadeConfig getConfig() const;

    //---- results -----------------------------------------------------------
    // Finished runs, queued once each (progression / ghost / achievements).
    bool pollRunResult(RunResult& out);

    // Progression game id for a game type (1..8, 0 = none). The arcade ids
    // already match the recorder ids, but the mapping is explicit so a future
    // enum reshuffle cannot silently credit the wrong game.
    [[nodiscard]] static int progressionGameIdFor(GameType type) noexcept;

    // Credits EVERY finished run to the global progression engine (XP, records,
    // bests, achievements, level-ups) and returns how many runs were credited.
    //
    // v1.3.0 FIX: this used to live only in the HTTP bridge (ArcadeServer::tick),
    // so playing a game in the desktop app never awarded anything — the whole
    // "type a lot to level up" feature silently did nothing on Windows. Both
    // front-ends now call this same function (hub timer / bridge tick), and a
    // result is popped exactly once, so nothing can be credited twice.
    std::size_t drainRunResultsToProgression();
    [[nodiscard]] std::size_t pendingRunResultCount() const;

    // Test/bench hook: deterministic time source replacement is done by the
    // caller driving update() explicitly; this only seeds all games.
    void setSeed(std::uint32_t seed);

    // A front-end that needs its own session (the web ArcadeServer, the ASCII
    // demo, tests) may own an instance; the singleton above stays for the app.
    ArcadeManager();
    ~ArcadeManager();
    ArcadeManager(const ArcadeManager&) = delete;
    ArcadeManager& operator=(const ArcadeManager&) = delete;

private:
    // The config is an explicit parameter (v1.3.0): makeGame() used to read
    // m_config without the mutex, racing setConfig() from the web bridge.
    std::unique_ptr<IArcadeGame> makeGame(GameType type, std::uint32_t seed,
                                          const ArcadeConfig& config) const;
    // Moves a finished run out of `game` into the result queue, exactly once
    // per run. Called from update(), handleKey(), launchGame() and stopGame(),
    // so a score is credited even when the player never presses another key.
    void collectResultFrom(const std::shared_ptr<IArcadeGame>& game);

    mutable std::mutex m_mutex;                        // guards launch/stop only
    std::shared_ptr<IArcadeGame> m_game;               // atomic snapshot for readers
    std::atomic<bool> m_active{false};                 // fast hot-path check
    ArcadeConfig m_config{};
    std::vector<RunResult> m_results;
    std::uint32_t m_seed = 0;
    bool m_hasLaunched = false;
    bool m_resultCollected = false;   // current run already reported
    // Reusable frame buffer: shapes are reserved once, cleared per frame →
    // no per-frame allocations on the UI thread.
    mutable Frame m_frame;
};

//===========================================================================
// Game-run scoring helpers (single source of truth for both front-ends)
//===========================================================================
[[nodiscard]] std::string_view hitRatingName(HitRating rating) noexcept;
[[nodiscard]] std::string_view rarityName(FishRarity rarity) noexcept;
[[nodiscard]] Color rarityColor(FishRarity rarity) noexcept;

} // namespace ok::arcade
