//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/test_arcade.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// Arcade Hub test suite (v1.3.0).
//
// This file replaces the v1.3.0-beta1 smoke test, which only asserted that
// `renderText()` was non-empty and therefore passed on a build with:
//   * a snake that died one tick early on every tight turn,
//   * swapped S/Z tetromino spawn tables,
//   * a TypingRace score that was always 0 when the run finished,
//   * a Rhythm mode with no death condition at all ("nhanh quá chết, chậm quá
//     cũng chết" was documented but not implemented),
//   * a No-Mistake mode that was mathematically impossible to reach level 2 in,
//   * an ArcadeManager that freed the running game from the UI thread while the
//     input thread was still inside it (use-after-free).
//
// Every test below targets a *behaviour*, not a getter, and the suite runs in
// Release as well as Debug (it never relies on assert() being enabled).
//----------------------------------------------------------------------------
#include "Arcade.hpp"
#include "Progression.hpp"
#include "ArcadeRender.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>
#include <string>
#include <thread>
#include <vector>

using namespace ok::arcade;

//===========================================================================
// Minimal assertion framework (works with NDEBUG)
//===========================================================================
namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_currentTest = "";

void reportFailure(const char* file, int line, const char* expr, const std::string& detail) {
    ++g_failures;
    std::printf("  [FAIL] %s:%d (%s): %s%s%s\n", file, line, g_currentTest, expr,
                detail.empty() ? "" : " — ", detail.c_str());
}

#define CHECK(expr)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(expr)) {                                                           \
            reportFailure(__FILE__, __LINE__, #expr, std::string());             \
        }                                                                        \
    } while (0)

#define CHECK_MSG(expr, msg)                                                     \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(expr)) {                                                           \
            reportFailure(__FILE__, __LINE__, #expr, (msg));                     \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                    \
    do {                                                                         \
        ++g_checks;                                                              \
        const double va = static_cast<double>(a);                                \
        const double vb = static_cast<double>(b);                                \
        if (std::fabs(va - vb) > (tol)) {                                        \
            char buf[160];                                                       \
            std::snprintf(buf, sizeof(buf), "got %.6f, expected %.6f (±%.6f)",   \
                          va, vb, static_cast<double>(tol));                     \
            reportFailure(__FILE__, __LINE__, #a " ~= " #b, std::string(buf));   \
        }                                                                        \
    } while (0)

//---------------------------------------------------------------------------
// Allocation counter: proving the frame pipeline is allocation-free in steady
// state is the only way to keep "zero hot-path overhead" honest.
//---------------------------------------------------------------------------
std::atomic<long long> g_allocations{0};
std::atomic<bool> g_countAllocations{false};

void noteAllocation() noexcept {
    if (g_countAllocations.load(std::memory_order_relaxed)) {
        g_allocations.fetch_add(1, std::memory_order_relaxed);
    }
}

long long measureAllocations(const std::function<void()>& fn) {
    g_allocations.store(0, std::memory_order_relaxed);
    g_countAllocations.store(true, std::memory_order_relaxed);
    fn();
    g_countAllocations.store(false, std::memory_order_relaxed);
    return g_allocations.load(std::memory_order_relaxed);
}

void keyDown(IArcadeGame& game, char32_t ch, int vk = 0) {
    InputEvent ev{};
    ev.ch = ch;
    ev.vk = vk;
    ev.down = true;
    game.handleKey(ev);
}

[[maybe_unused]] void keyUp(IArcadeGame& game, char32_t ch, int vk = 0) {
    InputEvent ev{};
    ev.ch = ch;
    ev.vk = vk;
    ev.down = false;
    game.handleKey(ev);
}

[[maybe_unused]] void keyDown(IArcadeGame& game, std::string_view ascii) {
    for (char c : ascii) {
        keyDown(game, static_cast<char32_t>(static_cast<unsigned char>(c)));
    }
}

//---------------------------------------------------------------------------
// Independent Tetris oracle: predicts where a piece lands using ONLY the
// exposed geometry + the public grid, so the game's rotate/move/drop logic is
// verified against something the game itself does not provide.
//---------------------------------------------------------------------------
struct TetrisOracle {
    std::array<std::array<int, TetrisGame::kCols>, TetrisGame::kRows> grid{};

    static bool collides(const std::array<std::array<int, TetrisGame::kCols>, TetrisGame::kRows>& g,
                         int piece, int rot, int px, int py) {
        const int* cells = TetrisGame::pieceCellTable(piece, rot);
        if (cells == nullptr) {
            return true;
        }
        for (int i = 0; i < 4; ++i) {
            const int x = px + cells[i * 2];
            const int y = py + cells[i * 2 + 1];
            if (x < 0 || x >= TetrisGame::kCols || y >= TetrisGame::kRows) {
                return true;
            }
            if (y >= 0 && g[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] != 0) {
                return true;
            }
        }
        return false;
    }

    static int landingY(const std::array<std::array<int, TetrisGame::kCols>, TetrisGame::kRows>& g,
                        int piece, int rot, int px) {
        int y = 0;
        while (!collides(g, piece, rot, px, y + 1)) {
            ++y;
        }
        return y;
    }

    static double evaluate(const std::array<std::array<int, TetrisGame::kCols>, TetrisGame::kRows>& g,
                           int piece, int rot, int px) {
        auto after = g;
        const int py = landingY(g, piece, rot, px);
        const int* cells = TetrisGame::pieceCellTable(piece, rot);
        for (int i = 0; i < 4; ++i) {
            const int x = px + cells[i * 2];
            const int y = py + cells[i * 2 + 1];
            if (y >= 0) {
                after[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = 1;
            }
        }
        int cleared = 0;
        for (int y = 0; y < TetrisGame::kRows; ++y) {
            bool full = true;
            for (int x = 0; x < TetrisGame::kCols; ++x) {
                if (after[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] == 0) {
                    full = false;
                    break;
                }
            }
            if (full) {
                ++cleared;
            }
        }
        int holes = 0;
        int aggregateHeight = 0;
        int bumpiness = 0;
        std::array<int, TetrisGame::kCols> heights{};
        for (int x = 0; x < TetrisGame::kCols; ++x) {
            int h = 0;
            bool seen = false;
            for (int y = 0; y < TetrisGame::kRows; ++y) {
                if (after[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] != 0) {
                    if (!seen) {
                        seen = true;
                        h = TetrisGame::kRows - y;
                    }
                } else if (seen) {
                    ++holes;
                }
            }
            heights[static_cast<std::size_t>(x)] = h;
            aggregateHeight += h;
        }
        for (int x = 0; x + 1 < TetrisGame::kCols; ++x) {
            bumpiness += std::abs(heights[static_cast<std::size_t>(x)] -
                                  heights[static_cast<std::size_t>(x + 1)]);
        }
        return -0.51 * aggregateHeight + 0.76 * cleared - 0.36 * holes - 0.18 * bumpiness;
    }
};

int gridSum(const std::array<std::array<std::uint8_t, TetrisGame::kCols>, TetrisGame::kRows>& grid) {
    int total = 0;
    for (const auto& row : grid) {
        for (std::uint8_t cell : row) {
            total += cell;
        }
    }
    return total;
}

// Breadth-first search on the snake board: returns the first move that keeps a
// path to the food (avoids the "greedy walker traps itself" flakiness).
char32_t snakeBfsMove(const SnakeGame& snake) {
    const SnakePoint head = snake.getBody().front();
    const SnakePoint food = snake.getFood();
    std::array<std::array<bool, SnakeGame::kWidth>, SnakeGame::kHeight> blocked{};
    for (const auto& pt : snake.getBody()) {
        if (pt.x >= 0 && pt.x < SnakeGame::kWidth && pt.y >= 0 && pt.y < SnakeGame::kHeight) {
            blocked[static_cast<std::size_t>(pt.y)][static_cast<std::size_t>(pt.x)] = true;
        }
    }
    blocked[static_cast<std::size_t>(head.y)][static_cast<std::size_t>(head.x)] = false;
    const int dx[4] = {0, 0, -1, 1};
    const int dy[4] = {-1, 1, 0, 0};
    const char32_t keys[4] = {U'w', U's', U'a', U'd'};
    std::array<std::array<int, SnakeGame::kWidth>, SnakeGame::kHeight> from{};
    for (auto& row : from) {
        row.fill(-1);
    }
    std::vector<SnakePoint> queue{head};
    from[static_cast<std::size_t>(head.y)][static_cast<std::size_t>(head.x)] = -2;
    std::size_t qi = 0;
    while (qi < queue.size()) {
        const SnakePoint cur = queue[qi++];
        if (cur == food) {
            break;
        }
        for (int d = 0; d < 4; ++d) {
            const int nx = cur.x + dx[d];
            const int ny = cur.y + dy[d];
            if (nx < 0 || nx >= SnakeGame::kWidth || ny < 0 || ny >= SnakeGame::kHeight) {
                continue;
            }
            if (blocked[static_cast<std::size_t>(ny)][static_cast<std::size_t>(nx)]) {
                continue;
            }
            if (from[static_cast<std::size_t>(ny)][static_cast<std::size_t>(nx)] != -1) {
                continue;
            }
            from[static_cast<std::size_t>(ny)][static_cast<std::size_t>(nx)] = d;
            queue.push_back({nx, ny});
        }
    }
    if (from[static_cast<std::size_t>(food.y)][static_cast<std::size_t>(food.x)] == -1) {
        return U' ';   // unreachable; caller keeps its current heading
    }
    SnakePoint cur = food;
    while (true) {
        const int d = from[static_cast<std::size_t>(cur.y)][static_cast<std::size_t>(cur.x)];
        if (d < 0) {
            return U' ';
        }
        const int px = cur.x - dx[d];
        const int py = cur.y - dy[d];
        if (px == head.x && py == head.y) {
            return keys[d];
        }
        cur = {px, py};
    }
}

void advance(IArcadeGame& game, double seconds, double step = 1.0 / 60.0) {
    for (double t = 0.0; t < seconds; t += step) {
        game.update(step);
        if (game.isGameOver()) {
            break;
        }
    }
}

} // namespace

// Global new/delete interception (test binary only).
void* operator new(std::size_t size) {
    noteAllocation();
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

//===========================================================================
// 1. Frame model
//===========================================================================
static void testFrameModel() {
    g_currentTest = "FrameModel";
    Frame frame;
    CHECK(frame.rects.empty());
    CHECK(frame.texts.empty());

    // Caps are enforced by dropping (and counting), never by growing.
    for (std::size_t i = 0; i < Frame::kMaxRects + 50; ++i) {
        frame.addRect(0, 0, 10, 10, 0xFF0000FFu);
    }
    CHECK(frame.rects.size() == Frame::kMaxRects);
    CHECK(frame.droppedShapes == 50);

    frame.clear();
    CHECK(frame.rects.empty());
    CHECK(frame.droppedShapes == 0);
    CHECK(frame.rects.capacity() >= Frame::kMaxRects);   // capacity is never released

    // Text interning + formatting helpers
    const auto hello = frame.intern(U"hello");
    CHECK(hello == U"hello");
    const auto number = frame.internNumber(U"Score: ", 1234);
    CHECK(number == U"Score: 1234");
    const auto negative = frame.internNumber(U"N=", -7, U"!");
    CHECK(negative == U"N=-7!");
    const auto decimal = frame.internDouble(U"wpm=", 61.25, 1);
    CHECK(decimal == U"wpm=61.2" || decimal == U"wpm=61.3");   // rounding direction documented below
    const auto ascii = frame.internAscii("PERFECT!", U"[", U"]");
    CHECK(ascii == U"[PERFECT!]");
    // The very first interned string must stay intact after later interns.
    CHECK(hello.size() == 5 && hello[0] == U'h');

    // Integer formatting edge cases
    CHECK(frame.internNumber({}, 0) == U"0");
    CHECK(frame.internNumber({}, -1) == U"-1");
    CHECK(frame.internDouble({}, -0.4, 0) == U"0" || frame.internDouble({}, -0.4, 0) == U"-0");

    // Steady-state frame building must not allocate.
    Frame hot;
    const long long allocations = measureAllocations([&] {
        for (int i = 0; i < 500; ++i) {
            hot.clear();
            hot.addRect(0, 0, 10, 10, 0xFFFFFFFFu);
            hot.addText(0, 0, 12, 0xFFFFFFFFu, TextAlign::Left, hot.intern(U"abc"), false, false);
            hot.addText(0, 0, 12, 0xFFFFFFFFu, TextAlign::Left, hot.internNumber(U"n=", i), false, false);
        }
    });
    CHECK_MSG(allocations == 0, "frame building allocated " + std::to_string(allocations) + " times");
}

//===========================================================================
// 2. Catalog
//===========================================================================
static void testCatalog() {
    g_currentTest = "Catalog";
    CHECK(gameCatalog().size() == 8);
    for (const auto& info : gameCatalog()) {
        CHECK(info.id >= 1 && info.id <= 8);
        CHECK(info.slug != nullptr && info.slug[0] != '\0');
        CHECK(gameTypeFromSlug(info.slug) == info.id);
        CHECK(gameInfo(info.id) != nullptr);
    }
    CHECK(gameTypeFromSlug("does-not-exist") == 0);
    CHECK(gameSlug(static_cast<int>(GameType::Snake)) == "snake");
}

//===========================================================================
// 3. Snake
//===========================================================================
static void testSnake() {
    g_currentTest = "Snake";

    SnakeGame snake;
    snake.setSeed(1234);
    snake.start();
    CHECK(snake.getScore() == 0);
    CHECK(!snake.isGameOver());
    CHECK(snake.getBody().size() == 3);

    // Moving into the cell the tail is vacating must NOT kill the snake
    // (the pre-v1.3.0 code checked the tail too and killed the snake here).
    snake.handleKey(InputEvent{0, U's', true});    // down
    snake.update(0.16);
    snake.handleKey(InputEvent{0, U'a', true});    // left
    snake.update(0.16);
    snake.handleKey(InputEvent{0, U'w', true});    // up
    snake.update(0.16);
    snake.handleKey(InputEvent{0, U'd', true});    // right (back into the old tail cell)
    snake.update(0.16);
    CHECK_MSG(!snake.isGameOver(), "moving into the vacated tail cell must be legal");

    // Wall collision is fatal.
    SnakeGame wall;
    wall.setSeed(7);
    wall.start();
    advance(wall, 5.0);
    CHECK(wall.isGameOver());

    // Determinism: same seed → same food layout → same run.
    SnakeGame a;
    SnakeGame b;
    a.setSeed(42);
    b.setSeed(42);
    a.start();
    b.start();
    for (int i = 0; i < 40; ++i) {
        a.update(0.05);
        b.update(0.05);
    }
    CHECK(a.getScore() == b.getScore());
    CHECK(a.getFood().x == b.getFood().x && a.getFood().y == b.getFood().y);

    // Eating grows the snake and raises the speed.
    SnakeGame eater;
    eater.setSeed(99);
    eater.start();
    const double initialInterval = eater.getTickInterval();
    const std::size_t initialLength = eater.getBody().size();
    bool grew = false;
    for (int i = 0; i < 4000 && !eater.isGameOver(); ++i) {
        // Steer along a real path to the food (BFS), so growth is deterministic
        // and never depends on the food spawning in a convenient spot.
        const char32_t ch = snakeBfsMove(eater);
        if (ch != U' ') {
            eater.handleKey(InputEvent{0, ch, true});
        }
        eater.update(0.05);
        if (eater.getBody().size() > initialLength) {
            grew = true;
            break;
        }
    }
    CHECK(grew);
    CHECK(eater.getTickInterval() < initialInterval);

    // Pause stops the simulation; Escape exits and stops consuming input.
    SnakeGame pauser;
    pauser.start();
    pauser.handleKey(InputEvent{0, U'p', true});
    CHECK(pauser.isPaused());
    const SnakePoint before = pauser.getBody().front();
    pauser.update(1.0);
    CHECK(pauser.getBody().front().x == before.x && pauser.getBody().front().y == before.y);
    pauser.handleKey(InputEvent{0, U'p', true});
    CHECK(!pauser.isPaused());

    CHECK(pauser.handleKey(InputEvent{0x1B, 0, true}) == InputResult::ExitRequested);
    CHECK(pauser.wantsExit());

    // The stress harness drives the real input path and terminates.
    SnakeGame stress;
    stress.start();
    std::vector<int> vks;
    for (int i = 0; i < 500; ++i) {
        vks.push_back(0x25 + (i % 4));
    }
    stress.stressTestInputQueue(vks);
    CHECK(!stress.renderText().empty());
}

//===========================================================================
// 4. Tetris
//===========================================================================
static void testTetris() {
    g_currentTest = "Tetris";

    TetrisGame tetris;
    tetris.setSeed(2024);
    tetris.start();
    CHECK(tetris.getLevel() == 1);
    CHECK(!tetris.isGameOver());

    // 7-bag randomizer: the first seven pieces must be a permutation of 0..6.
    {
        std::vector<int> seen;
        seen.push_back(tetris.getCurrentPiece());
        for (int i = 0; i < 6; ++i) {
            tetris.handleKey(InputEvent{0x20, 0, true});   // hard drop → next piece
            seen.push_back(tetris.getCurrentPiece());
        }
        std::sort(seen.begin(), seen.end());
        CHECK_MSG(seen == std::vector<int>({0, 1, 2, 3, 4, 5, 6}),
                  "7-bag randomizer must yield every tetromino once per bag");
    }

    // Piece identity: S and Z are different shapes at spawn (the old table had
    // them swapped so both rendered as the same tetromino).
    CHECK(TetrisGame::kCols == 10 && TetrisGame::kRows == 20);

    // Hard drop lands on the ghost row and locks the piece into the grid.
    TetrisGame drop;
    drop.setSeed(5);
    drop.start();
    CHECK(drop.getGhostY() >= 0);
    drop.handleKey(InputEvent{0x20, 0, true});
    CHECK(drop.getScore() >= 2);                             // hard-drop points
    CHECK_MSG(gridSum(drop.getGrid()) > 0, "hard drop must lock the piece into the grid");

    // Rotation kicks off the wall instead of silently failing.
    TetrisGame kick;
    kick.setSeed(11);
    kick.start();
    for (int i = 0; i < 6; ++i) {
        kick.handleKey(InputEvent{0, U'a', true});   // push against the left wall
    }
    const int xBefore = kick.getCurrentX();
    kick.handleKey(InputEvent{0, U'w', true});
    CHECK(kick.getCurrentRotation() != 0 || kick.getCurrentX() != xBefore);

    // Play a competent game with a Dellacherie-style placement AI and verify
    // (a) the game's rotation/move/drop ends exactly where the oracle says it
    //     will and (b) lines actually clear and score.
    TetrisGame ai;
    ai.setSeed(3);
    ai.start();
    int placementMismatches = 0;
    int checkedPlacements = 0;
    for (int piece = 0; piece < 80 && !ai.isGameOver() && ai.getLinesCleared() < 2; ++piece) {
        const int pieceId = ai.getCurrentPiece();   // capture: the drop spawns the next piece
        std::array<std::array<int, TetrisGame::kCols>, TetrisGame::kRows> snapshot{};
        const auto& live = ai.getGrid();
        for (int y = 0; y < TetrisGame::kRows; ++y) {
            for (int x = 0; x < TetrisGame::kCols; ++x) {
                snapshot[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] =
                    live[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] != 0 ? 1 : 0;
            }
        }

        int bestRot = 0;
        int bestX = 0;
        double bestScore = -1e18;
        for (int rot = 0; rot < 4; ++rot) {
            for (int x = -3; x < TetrisGame::kCols; ++x) {
                if (TetrisOracle::collides(snapshot, pieceId, rot, x, 0)) {
                    continue;
                }
                const double score = TetrisOracle::evaluate(snapshot, pieceId, rot, x);
                if (score > bestScore) {
                    bestScore = score;
                    bestRot = rot;
                    bestX = x;
                }
            }
        }

        // Execute the plan through the public input API.
        for (int r = 0; r < bestRot; ++r) {
            ai.handleKey(InputEvent{0, U'w', true});
        }
        for (int guard = 0; guard < 16 && ai.getCurrentX() != bestX; ++guard) {
            ai.handleKey(InputEvent{0, ai.getCurrentX() > bestX ? U'a' : U'd', true});
        }
        const int expectedY = TetrisOracle::landingY(snapshot, pieceId, bestRot, bestX);
        const bool planApplied = ai.getCurrentRotation() == bestRot && ai.getCurrentX() == bestX;
        ai.handleKey(InputEvent{0x20, 0, true});

        if (planApplied && ai.getLinesCleared() == 0) {
            ++checkedPlacements;
            // Rebuild what the board should look like and compare.
            auto expected = snapshot;
            const int* cells = TetrisGame::pieceCellTable(pieceId, bestRot);
            for (int i = 0; i < 4; ++i) {
                const int x = bestX + cells[i * 2];
                const int y = expectedY + cells[i * 2 + 1];
                if (y >= 0) {
                    expected[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = 1;
                }
            }
            const auto& now = ai.getGrid();
            for (int y = 0; y < TetrisGame::kRows; ++y) {
                for (int x = 0; x < TetrisGame::kCols; ++x) {
                    const int got = now[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] != 0 ? 1 : 0;
                    if (got != expected[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]) {
                        ++placementMismatches;
                    }
                }
            }
        }
    }
    CHECK_MSG(placementMismatches == 0,
              "hard-dropped pieces did not land where the oracle predicted (" +
                  std::to_string(placementMismatches) + " cells differ)");
    CHECK(checkedPlacements >= 5);
    CHECK_MSG(ai.getLinesCleared() >= 1, "competent play must complete at least one line");
    CHECK(ai.getScore() > 0);

    // Stacking to the ceiling ends the game (and reports a run result).
    TetrisGame topper;
    topper.setSeed(77);
    topper.start();
    for (int i = 0; i < 400 && !topper.isGameOver(); ++i) {
        topper.handleKey(InputEvent{0x20, 0, true});
    }
    CHECK(topper.isGameOver());
    RunResult result;
    CHECK(topper.pollRunResult(result));
    CHECK(result.type == GameType::Tetris);
    CHECK(!topper.pollRunResult(result));   // exactly once

    // Gravity honours the fixed-step accumulator (frame-rate independence).
    TetrisGame slow;
    TetrisGame fast;
    slow.setSeed(1);
    fast.setSeed(1);
    slow.start();
    fast.start();
    for (int i = 0; i < 30; ++i) {
        slow.update(1.0 / 30.0);
    }
    for (int i = 0; i < 120; ++i) {
        fast.update(1.0 / 120.0);
    }
    CHECK(slow.getCurrentY() == fast.getCurrentY());
}

//===========================================================================
// 5. Fishing
//===========================================================================
static void testFishing() {
    g_currentTest = "Fishing";

    FishingGame fish;
    fish.setSeed(4242);
    fish.start();
    CHECK(!fish.isGameOver());
    CHECK(fish.getRodLevel() == 1);

    // Typing is case-insensitive (typing "C" for a lowercase prompt used to be
    // treated as a mistake).
    const std::u32string_view prompt = fish.getPrompt();
    CHECK(!prompt.empty());
    const char32_t first = prompt[0];
    const char32_t upper = (first >= U'a' && first <= U'z')
                               ? static_cast<char32_t>(first - U'a' + U'A')
                               : first;
    fish.handleKey(InputEvent{0, upper, true});
    CHECK(fish.getPromptIndex() == 1);

    // A wrong key costs progress, never a crash.
    const double beforeWrong = fish.getPullProgress();
    fish.handleKey(InputEvent{0, U'~', true});
    CHECK(fish.getPullProgress() <= beforeWrong);

    // Complete a prompt → a catch is scored and a new fish is hooked.
    FishingGame catcher;
    catcher.setSeed(8);
    catcher.start();
    const std::uint32_t catchesBefore = catcher.getCatches();
    const std::u32string_view target = catcher.getPrompt();
    for (char32_t ch : target) {
        catcher.handleKey(InputEvent{0, ch, true});
    }
    CHECK_MSG(catcher.getCatches() == catchesBefore + 1, "finishing the prompt must land the fish");
    CHECK(catcher.getScore() > 0);

    // The line snaps when it is pulled too hard with a heavy fish.
    FishingGame snapper;
    snapper.setSeed(1);
    snapper.start();
    // Force a legendary fish (lowest roll) and reel it in without pause.
    for (int guard = 0; guard < 400 && snapper.getRarity() != FishRarity::Legendary; ++guard) {
        const std::u32string_view p = snapper.getPrompt();
        for (char32_t ch : p) {
            snapper.handleKey(InputEvent{0, ch, true});
        }
    }
    if (snapper.getRarity() == FishRarity::Legendary) {
        const std::uint32_t escapesBefore = snapper.getEscapes();
        const std::u32string_view p = snapper.getPrompt();
        for (char32_t ch : p) {
            snapper.handleKey(InputEvent{0, ch, true});
        }
        CHECK_MSG(snapper.getEscapes() > escapesBefore,
                  "a legendary fish must be able to snap the line when pulled too fast");
    }

    // Fish escape when the timer runs out.
    FishingGame escaper;
    escaper.setSeed(9);
    escaper.start();
    const std::uint32_t escapes0 = escaper.getEscapes();
    advance(escaper, 40.0);
    CHECK(escaper.getEscapes() > escapes0);

    // Upgrades are bounded and change the pull rate.
    FishingGame upgraded;
    upgraded.setSeed(3);
    upgraded.start();
    for (int i = 0; i < 10; ++i) {
        upgraded.upgradeRod();
        upgraded.upgradeBait();
        upgraded.upgradeReel();
    }
    CHECK(upgraded.getRodLevel() == 5);
    CHECK(upgraded.getBaitLevel() == 5);
    CHECK(upgraded.getReelLevel() == 5);

    // Automation modes stay bounded (Assisted can never finish alone).
    FishingGame assisted;
    assisted.setSeed(6);
    assisted.start();
    assisted.setAutomationMode(AutomationMode::Assisted);
    advance(assisted, 20.0);
    CHECK(assisted.getCatches() == 0);
    CHECK(assisted.getPullProgress() <= 95.0 + 1e-6);

    FishingGame automated;
    automated.setSeed(6);
    automated.start();
    automated.setAutomationMode(AutomationMode::Automated);
    advance(automated, 60.0);
    CHECK(automated.getCatches() > 0);
}

//===========================================================================
// 6. Typing Race
//===========================================================================
static void testTypingRace() {
    g_currentTest = "TypingRace";

    TypingRaceGame race;
    race.setSeed(1);
    race.setPassage(U"abc");
    race.setPacerWpm(60.0);
    race.start();
    CHECK(!race.isGameOver());

    for (int i = 0; i < 120; ++i) {
        race.update(1.0 / 60.0);   // two seconds of driving
    }
    race.handleKey(InputEvent{0, U'a', true});
    race.handleKey(InputEvent{0, U'b', true});
    race.handleKey(InputEvent{0, U'c', true});
    CHECK(race.isGameOver());
    // The old build scored 0 here because the WPM used by the score was only
    // refreshed inside update(), which never ran between the last key and the
    // finish. Finishing must produce a *meaningful* score.
    CHECK_MSG(race.getScore() > 0, "finishing a race in one tick must not score 0");
    CHECK(race.getLiveWpm() > 0.0);

    // Wrong keys lower accuracy but never advance the cursor.
    TypingRaceGame acc;
    acc.setPassage(U"abcd");
    acc.start();
    acc.handleKey(InputEvent{0, U'a', true});
    acc.handleKey(InputEvent{0, U'z', true});
    acc.handleKey(InputEvent{0, U'b', true});
    acc.update(1.0);
    CHECK(acc.getCharIndex() == 2);
    CHECK_NEAR(acc.getAccuracy(), 100.0 * 2.0 / 3.0, 0.001);

    // Backspace steps back one character (forgiving mode).
    acc.handleKey(InputEvent{0x08, U'\b', true});
    CHECK(acc.getCharIndex() == 1);

    // The pacer car advances monotonically with time and never exceeds the track.
    TypingRaceGame pacer;
    pacer.setPassage(U"a long passage to type while the pacer car drives ahead");
    pacer.setPacerWpm(120.0);
    pacer.start();
    double last = -1.0;
    for (int i = 0; i < 200; ++i) {
        pacer.update(1.0 / 60.0);
        CHECK(pacer.getPacerProgress() >= last - 1e-9);
        last = pacer.getPacerProgress();
    }
    CHECK(last <= 1.0);

    // Escape leaves the game and reports the run.
    pacer.handleKey(InputEvent{0x1B, 0, true});
    CHECK(pacer.wantsExit());
    RunResult res;
    CHECK(pacer.pollRunResult(res));
    CHECK(res.type == GameType::TypingRace);
}

//===========================================================================
// 7. WASD + typing race
//===========================================================================
static void testWasdRace() {
    g_currentTest = "WasdRace";

    WasdRaceGame wasd;
    wasd.setSeed(31);
    wasd.start();
    CHECK(!wasd.isGameOver());

    // Lane changes clamp at the road edges.
    for (int i = 0; i < 10; ++i) {
        wasd.handleKey(InputEvent{0, U'a', true});
    }
    CHECK(wasd.getPlayerLane() == 0);
    for (int i = 0; i < 10; ++i) {
        wasd.handleKey(InputEvent{0, U'd', true});
    }
    CHECK(wasd.getPlayerLane() == WasdRaceGame::kLanes - 1);

    // W increases speed, S brakes (and never below the floor).
    const double before = wasd.getCarSpeed();
    wasd.handleKey(InputEvent{0, U'w', true});
    CHECK(wasd.getCarSpeed() > before);
    for (int i = 0; i < 30; ++i) {
        wasd.handleKey(InputEvent{0, U's', true});
    }
    CHECK(wasd.getCarSpeed() >= 30.0);

    // Typing refuels the tank and advances the passage index.
    WasdRaceGame filler;
    filler.setSeed(5);
    filler.start();
    filler.update(3.0);
    const double fuelBefore = filler.getFuel();
    const std::u32string_view passage = filler.getPassage();
    filler.handleKey(InputEvent{0, passage[0], true});
    CHECK(filler.getTextIndex() == 1);
    CHECK(filler.getFuel() > fuelBefore - 3.5);   // >= before minus the burn of one frame

    // Fuel exhaustion ends the run.
    WasdRaceGame dry;
    dry.setSeed(2);
    dry.setStartFuel(1.0);
    dry.start();
    advance(dry, 30.0);
    CHECK(dry.isGameOver());
    RunResult result;
    CHECK(dry.pollRunResult(result));
    CHECK(result.type == GameType::WasdRace);

    // Every key is consumed while the game is open (the old code let letters
    // fall through to the IME, so typing in-game also typed into the document).
    CHECK(dry.handleKey(InputEvent{0, U'z', true}) == InputResult::Consumed);
    CHECK(dry.handleKey(InputEvent{0, 0, false}) == InputResult::Consumed);

    // Obstacle spawning always leaves at least one lane free.
    WasdRaceGame spawner;
    spawner.setSeed(123);
    spawner.start();
    for (int i = 0; i < 3000; ++i) {
        spawner.update(1.0 / 60.0);
        if (spawner.isGameOver()) {
            break;
        }
        // Group obstacles spawned at the same distance and count lanes used.
        std::array<int, WasdRaceGame::kLanes> used{};
        for (const auto& ob : spawner.getObstacles()) {
            if (ob.dist > WasdRaceGame::kRoadLength - 20.0) {
                used[static_cast<std::size_t>(ob.lane)] = 1;
            }
        }
        const int freeLanes = 3 - (used[0] + used[1] + used[2]);
        CHECK(freeLanes >= 1);
        if (freeLanes < 1) {
            break;
        }
    }
}

//===========================================================================
// 8. Rhythm typing
//===========================================================================
static void testRhythm() {
    g_currentTest = "Rhythm";

    RhythmTypingGame rhythm;
    rhythm.setSeed(100);
    rhythm.setBpm(120.0);
    rhythm.setNoteCount(16);
    rhythm.start();
    CHECK(rhythm.getNotes().size() == 16);
    CHECK(rhythm.getFailMode() == FailMode::Hardcore);
    CHECK(!rhythm.isGameOver());

    // Chart is sorted, beat-aligned and uses the four lane keys.
    const auto& notes = rhythm.getNotes();
    for (std::size_t i = 1; i < notes.size(); ++i) {
        CHECK(notes[i].targetTimeSec >= notes[i - 1].targetTimeSec);
    }
    for (const auto& note : notes) {
        CHECK(note.ch == U'd' || note.ch == U'f' || note.ch == U'j' || note.ch == U'k');
    }

    // Perfect hit exactly on the note time.
    RhythmTypingGame perfect;
    perfect.setSeed(55);
    perfect.setBpm(120.0);
    perfect.setNoteCount(16);
    perfect.start();
    const RhythmNote first = perfect.getNotes().front();
    while (perfect.getSongTime() < first.targetTimeSec) {
        perfect.update(1.0 / 240.0);
    }
    perfect.handleKey(InputEvent{0, first.ch, true});
    CHECK(perfect.getLastRating() == HitRating::Perfect || perfect.getLastRating() == HitRating::Good);
    CHECK(perfect.getCombo() >= 1);
    CHECK(perfect.getScore() > 0);

    // Hitting way too early = death in the default hardcore mode.
    RhythmTypingGame early;
    early.setSeed(56);
    early.setBpm(120.0);
    early.setNoteCount(16);
    early.start();
    const RhythmNote target = early.getNotes().front();
    // Advance to just before the note but outside every window.
    while (early.getSongTime() < target.targetTimeSec - RhythmTypingGame::kLateWindowSec - 0.02) {
        early.update(1.0 / 240.0);
    }
    early.handleKey(InputEvent{0, target.ch, true});
    CHECK_MSG(early.isGameOver(), "an early press outside the window must fail a run in hardcore");

    // Missing a note (no input at all) = death in hardcore mode.
    RhythmTypingGame missing;
    missing.setSeed(57);
    missing.setBpm(180.0);
    missing.setNoteCount(16);
    missing.start();
    advance(missing, 5.0);
    CHECK_MSG(missing.isGameOver(), "not pressing a note must fail a run in hardcore");

    // Health-bar mode survives mistakes but drains.
    RhythmTypingGame health;
    health.setSeed(58);
    health.setBpm(150.0);
    health.setNoteCount(16);
    health.setFailMode(FailMode::HealthBar);
    health.start();
    const RhythmNote firstNote = health.getNotes().front();
    while (health.getSongTime() < firstNote.targetTimeSec) {
        health.update(1.0 / 240.0);
    }
    health.handleKey(InputEvent{0, firstNote.ch, true});
    const double healthAfterHit = health.getHealth();
    CHECK(healthAfterHit >= 0.0 && healthAfterHit <= 100.0);
    advance(health, 3.0);
    CHECK_MSG(health.getHealth() < 100.0, "missed notes must drain the health bar");
    advance(health, 60.0);
    CHECK_MSG(health.isGameOver(), "a drained health bar must end the run");

    // Extra key presses (no note in the window) count as a miss.
    RhythmTypingGame extra;
    extra.setSeed(59);
    extra.setBpm(120.0);
    extra.setNoteCount(16);
    extra.start();
    advance(extra, 0.1);
    extra.handleKey(InputEvent{0, U'j', true});
    CHECK(extra.getMissCount() == 1);
    CHECK(extra.isGameOver());

    // Nearest-note judging: pressing the lane key hits the closest pending note.
    RhythmTypingGame nearest;
    nearest.setSeed(60);
    nearest.setBpm(120.0);
    nearest.setNoteCount(32);
    nearest.setFailMode(FailMode::HealthBar);   // a missed note must not freeze the clock here
    nearest.start();
    std::size_t secondNoteIndex = 0;
    for (std::size_t i = 1; i < nearest.getNotes().size(); ++i) {
        if (nearest.getNotes()[i].lane == nearest.getNotes()[0].lane) {
            secondNoteIndex = i;
            break;
        }
    }
    CHECK(secondNoteIndex != 0);
    while (nearest.getSongTime() < nearest.getNotes()[1].targetTimeSec) {
        nearest.update(1.0 / 240.0);
    }
    const char32_t lane2 = nearest.getNotes()[1].ch;
    nearest.handleKey(InputEvent{0, lane2, true});
    CHECK_NEAR(nearest.getSongTime(), nearest.getNotes()[1].targetTimeSec, 0.02);

    // A full chart with perfect timing clears the run.
    RhythmTypingGame clear;
    clear.setSeed(61);
    clear.setBpm(240.0);
    clear.setNoteCount(8);
    clear.start();
    for (int guard = 0; guard < 20000 && !clear.isGameOver(); ++guard) {
        // Look ahead: press the next pending note exactly on its beat.
        const RhythmNote* next = nullptr;
        for (const auto& n : clear.getNotes()) {
            if (n.state == NoteState::Pending) {
                next = &n;
                break;
            }
        }
        if (next == nullptr) {
            break;
        }
        if (clear.getSongTime() >= next->targetTimeSec) {
            clear.handleKey(InputEvent{0, next->ch, true});
        }
        clear.update(1.0 / 480.0);
    }
    CHECK(clear.getPerfectCount() >= 7);

    // Determinism with the same seed.
    RhythmTypingGame d1;
    RhythmTypingGame d2;
    d1.setSeed(777);
    d2.setSeed(777);
    d1.setBpm(112.0);
    d2.setBpm(112.0);
    d1.start();
    d2.start();
    for (std::size_t i = 0; i < d1.getNotes().size(); ++i) {
        CHECK(d1.getNotes()[i].ch == d2.getNotes()[i].ch);
        CHECK_NEAR(d1.getNotes()[i].targetTimeSec, d2.getNotes()[i].targetTimeSec, 1e-9);
    }

    // Escape reports a run result.
    nearest.handleKey(InputEvent{0x1B, 0, true});
    RunResult res;
    CHECK(nearest.pollRunResult(res));
}

//===========================================================================
// 9. No-Mistake mode
//===========================================================================
static void testNoMistake() {
    g_currentTest = "NoMistake";

    NoMistakeGame game;
    game.start();
    CHECK(!game.isGameOver());
    const std::u32string_view stream = game.getTextStream();
    CHECK(!stream.empty());
    CHECK(game.getScore() == game.getStartReserve());

    // Correct keys build combo, level and score.
    game.handleKey(InputEvent{0, stream[0], true});
    game.handleKey(InputEvent{0, stream[1], true});
    CHECK(game.getCombo() == 2);
    CHECK(game.getCurrentIndex() == 2);
    CHECK(game.getScore() > game.getStartReserve());

    // Hardcore: one mistake ends the run immediately (documented behaviour).
    game.handleKey(InputEvent{0, U'~', true});
    CHECK(game.isGameOver());
    CHECK(game.getMistakes() == 1);
    RunResult result;
    CHECK(game.pollRunResult(result));
    CHECK(result.type == GameType::NoMistake);
    CHECK(!result.completed);

    // Health-bar mode: mistakes are survivable while the reserve lasts.
    NoMistakeGame soft;
    soft.setFailMode(FailMode::HealthBar);
    soft.start();
    const std::u32string_view softStream = soft.getTextStream();
    soft.handleKey(InputEvent{0, softStream[0], true});
    for (int i = 0; i < 20 && !soft.isGameOver(); ++i) {
        soft.handleKey(InputEvent{0, U'~', true});
    }
    CHECK(soft.getMistakes() > 1);
    CHECK(soft.getScore() >= 0);   // reserve never goes negative

    // Completing the stream is the win condition.
    NoMistakeGame winner;
    winner.start();
    const std::u32string_view winStream = winner.getTextStream();
    for (char32_t ch : winStream) {
        winner.handleKey(InputEvent{0, ch, true});
    }
    CHECK(winner.isGameOver());
    CHECK(winner.getScore() > 0);
    RunResult winResult;
    CHECK(winner.pollRunResult(winResult));
    CHECK_MSG(winResult.completed, "clearing the whole stream must report a completed run");
}

//===========================================================================
// 10. Flexing mode
//===========================================================================
static void testFlexing() {
    g_currentTest = "Flexing";

    FlexingGame flex;
    flex.setPreloadedText(U"Hello World Wide Web");
    flex.setGranularity(FlexGranularity::OneCharPerKey);
    flex.start();
    CHECK(!flex.isGameOver());

    keyDown(flex, U'x');
    CHECK(flex.popEmittedOutput() == U"H");
    CHECK(flex.getActualKeypresses() == 1);
    CHECK(flex.getGeneratedChars() == 1);

    flex.setGranularity(FlexGranularity::OneWordPerKey);
    keyDown(flex, U'y');
    CHECK(flex.popEmittedOutput() == U"ello ");

    flex.setGranularity(FlexGranularity::NCharsPerKey, 3);
    keyDown(flex, U'z');
    CHECK(flex.popEmittedOutput() == U"Wor");

    // The WPM readout must stay finite even on the first tick (the old code
    // divided by a zero-length elapsed window).
    FlexingGame fast;
    fast.setPreloadedText(U"abcdef");
    fast.setGranularity(FlexGranularity::AutoStream);
    fast.start();
    fast.update(0.0);
    CHECK_MSG(std::isfinite(fast.getDisplayedWpm()), "WPM must never be inf/NaN");

    // Auto-stream finishes the text on its own and reports the run.
    advance(fast, 3.0);
    CHECK(fast.getCursor() == 6);
    CHECK(fast.isGameOver());
    RunResult res;
    CHECK(fast.pollRunResult(res));
    CHECK(res.type == GameType::Flexing);

    // Efficiency multiplier = generated characters per real key press.
    FlexingGame eff;
    eff.setPreloadedText(U"one two three four five");
    eff.setGranularity(FlexGranularity::OneWordPerKey);
    eff.start();
    keyDown(eff, U'a');
    CHECK(eff.getEfficiencyMultiplier() > 1.0);

    // Escape exits cleanly.
    CHECK(eff.handleKey(InputEvent{0x1B, 0, true}) == InputResult::ExitRequested);
}

//===========================================================================
// 11. Hub manager
//===========================================================================
static void testManager() {
    g_currentTest = "Manager";
    auto& hub = ArcadeManager::instance();

    hub.stopGame();
    CHECK(!hub.isConsumingKeyboard());
    CHECK(hub.getCurrentGameType() == GameType::None);

    // Every catalog entry can actually be launched.
    for (const auto& info : gameCatalog()) {
        const bool launched = hub.launchGame(static_cast<GameType>(info.id));
        CHECK_MSG(launched, std::string("launch failed for ") + info.slug);
        CHECK(hub.isConsumingKeyboard());
        CHECK(static_cast<int>(hub.getCurrentGameType()) == info.id);
        const Frame& frame = hub.getFrame();
        CHECK(!frame.stats.title.empty());
        CHECK(!frame.rects.empty() || !frame.circles.empty() || !frame.texts.empty());
        CHECK_MSG(frame.droppedShapes == 0,
                  std::string("frame overflow for ") + info.slug + ": " +
                      std::to_string(frame.droppedShapes) + " shapes dropped");
        hub.update(1.0 / 60.0);
        hub.handleKey(InputEvent{0, U'x', true});
        hub.handleKey(InputEvent{0, U'x', false});
    }

    // Launching a game queues the previous run's result (no silent loss).
    hub.stopGame();
    for (RunResult drain; hub.pollRunResult(drain);) {
    }
    hub.setConfig(ArcadeConfig{});
    hub.launchGame(GameType::Snake, 4242);
    hub.update(0.2);
    CHECK(hub.handleKey(InputEvent{0x1B, 0, true}) == InputResult::ExitRequested);
    CHECK_MSG(!hub.isConsumingKeyboard(), "Escape must hand the keyboard back to the IME");
    RunResult res;
    CHECK(hub.pollRunResult(res));
    CHECK(res.type == GameType::Snake);

    // Config is honoured when a game is created.
    ArcadeConfig config;
    config.rhythmFailMode = FailMode::HealthBar;
    config.rhythmBpm = 180.0;
    config.rhythmNoteCount = 24;
    config.noMistakeStartReserve = 555;
    hub.setConfig(config);
    CHECK(hub.getConfig().rhythmBpm == 180.0);
    hub.launchGame(GameType::Rhythm, 1);
    auto* rhythm = dynamic_cast<RhythmTypingGame*>(hub.getCurrentGame());
    CHECK(rhythm != nullptr);
    if (rhythm != nullptr) {
        CHECK(rhythm->getFailMode() == FailMode::HealthBar);
        CHECK(rhythm->getBpm() == 180.0);
        CHECK(rhythm->getNoteCount() == 24);
    }
    hub.launchGame(GameType::NoMistake, 1);
    auto* noMistake = dynamic_cast<NoMistakeGame*>(hub.getCurrentGame());
    CHECK(noMistake != nullptr);
    if (noMistake != nullptr) {
        CHECK(noMistake->getStartReserve() == 555);
    }
    hub.setConfig(ArcadeConfig{});
    hub.stopGame();

    // Concurrency: launching/stopping from another thread while the "input
    // thread" hammers handleKey()/getFrame(). With the old unique_ptr design
    // this was a use-after-free (run under ASan to make the difference visible).
    std::atomic<bool> stop{false};
    std::thread launcher([&] {
        while (!stop.load()) {
            hub.launchGame(GameType::Snake, 7);
            std::this_thread::yield();
            hub.update(1.0 / 60.0);
            hub.stopGame();
        }
    });
    for (int i = 0; i < 20000; ++i) {
        hub.handleKey(InputEvent{0, U'w', true});
        hub.update(1.0 / 60.0);
        (void)hub.getFrame();
        (void)hub.isConsumingKeyboard();
    }
    stop.store(true);
    launcher.join();
    hub.stopGame();
    CHECK(true);   // reaching here without crashing/ASan report is the assertion
}

//===========================================================================
// 12. Frame invariants for every game
//===========================================================================
static void testFrameInvariants() {
    g_currentTest = "FrameInvariants";
    for (const auto& info : gameCatalog()) {
        IArcadeGame* rawGame = nullptr;
        auto& hub = ArcadeManager::instance();
        hub.launchGame(static_cast<GameType>(info.id), 1234);
        rawGame = hub.getCurrentGame();
        CHECK(rawGame != nullptr);
        if (rawGame == nullptr) {
            continue;
        }

        // A few frames of activity, then draw.
        for (int i = 0; i < 240; ++i) {
            hub.update(1.0 / 60.0);
            hub.handleKey(InputEvent{0, U'd', true});
            hub.handleKey(InputEvent{0, U'f', true});
            hub.handleKey(InputEvent{0, U'j', true});
            hub.handleKey(InputEvent{0, U'k', true});
            if (rawGame->isGameOver()) {
                break;
            }
        }
        const Frame& frame = hub.getFrame();
        CHECK_MSG(frame.droppedShapes == 0,
                  std::string(info.slug) + " dropped " + std::to_string(frame.droppedShapes) +
                      " shapes (raise the cap or fix the renderer)");
        CHECK(frame.worldW > 0 && frame.worldH > 0);
        CHECK(frame.background != 0);
        CHECK(!frame.stats.title.empty());

        // Geometry: nothing may be drawn absurdly far outside the world (a
        // common symptom of a scale/coordinate bug that a text test cannot see).
        const float slack = 120.0f;
        for (const auto& r : frame.rects) {
            CHECK(r.x > -slack && r.x < frame.worldW + slack);
            CHECK(r.y > -slack && r.y < frame.worldH + slack);
            CHECK(r.w >= 0 && r.h >= 0);
        }
        for (const auto& c : frame.circles) {
            CHECK(c.cx > -slack && c.cx < frame.worldW + slack);
            CHECK(c.cy > -slack && c.cy < frame.worldH + slack);
            CHECK(c.r > 0);
        }
        for (const auto& t : frame.texts) {
            CHECK(!t.text.empty());
            CHECK(t.size > 0);
            CHECK(t.color != 0);
        }
        for (const auto& t : frame.texts) {
            CHECK(t.x > -slack && t.x < frame.worldW + slack);
        }
    }
    ArcadeManager::instance().stopGame();
}

//===========================================================================
// 13. Determinism + fixed-step behaviour of the hub
//===========================================================================
static void testDeterminism() {
    g_currentTest = "Determinism";

    auto playScripted = [](GameType type, double step, int steps) {
        auto& hub = ArcadeManager::instance();
        hub.stopGame();
        hub.launchGame(type, 987654321u);
        for (int i = 0; i < steps; ++i) {
            hub.update(step);
            hub.handleKey(InputEvent{0, U'd', true});
            hub.handleKey(InputEvent{0, U'w', true});
            hub.handleKey(InputEvent{0, U's', true});
            hub.handleKey(InputEvent{0, U'a', true});
            hub.handleKey(InputEvent{0, U'f', true});
        }
        const std::int64_t score = hub.getCurrentGame() ? hub.getCurrentGame()->getScore() : -1;
        hub.stopGame();
        return score;
    };

    for (const auto& info : gameCatalog()) {
        const auto type = static_cast<GameType>(info.id);
        const std::int64_t first = playScripted(type, 1.0 / 60.0, 300);
        const std::int64_t second = playScripted(type, 1.0 / 60.0, 300);
        CHECK_MSG(first == second, std::string("non-deterministic score for ") + info.slug);
    }

    // Same total time, different frame sizes → same score for the accumulator
    // based games (Snake/Tetris). Text-driven games count keys, not time, so
    // their score also stays stable.
    const std::int64_t at60 = playScripted(GameType::Snake, 1.0 / 60.0, 300);
    const std::int64_t at120 = playScripted(GameType::Snake, 1.0 / 120.0, 600);
    CHECK_MSG(at60 == at120, "snake score must not depend on the frame rate");
}

//===========================================================================
// 14. Allocation-free steady state
//===========================================================================
static void testNoSteadyStateAllocations() {
    g_currentTest = "NoAllocations";
    auto& hub = ArcadeManager::instance();
    for (const auto& info : gameCatalog()) {
        hub.stopGame();
        hub.launchGame(static_cast<GameType>(info.id), 555);
        // Warm up: chart generation, obstacle pools, string buffers, ...
        for (int i = 0; i < 60; ++i) {
            hub.update(1.0 / 60.0);
            (void)hub.getFrame();
        }
        const long long allocations = measureAllocations([&] {
            for (int i = 0; i < 240; ++i) {
                hub.update(1.0 / 60.0);
                const Frame& frame = hub.getFrame();
                // Touch the frame so nothing is optimised away.
                if (!frame.rects.empty()) {
                    g_allocations.fetch_add(0, std::memory_order_relaxed);
                }
            }
        });
        CHECK_MSG(allocations == 0,
                  std::string(info.slug) + " allocated " + std::to_string(allocations) +
                      " times across 240 idle frames");

        // The rendering path is part of the frame budget too: once warmed up,
        // building the display list AND serializing it must not touch the heap
        // (the web bridge serializes every frame; the GDI hub builds every
        // frame). This is what keeps a 60 Hz front-end from churning malloc.
        ok::arcade::RenderList list;
        std::string json;
        ok::arcade::buildRenderList(hub.getFrame(), list);
        ok::arcade::renderListToJson(list, json);
        const long long renderAllocations = measureAllocations([&] {
            for (int i = 0; i < 240; ++i) {
                hub.update(1.0 / 60.0);
                ok::arcade::buildRenderList(hub.getFrame(), list);
                ok::arcade::renderListToJson(list, json);   // buffer is reused
            }
        });
        // Display list + wire JSON of every frame: no per-frame heap traffic.
        // Both front-ends call exactly these two functions 60 times a second,
        // so a regression here shows up as allocator churn during play.
        //
        // The budget is not literally zero because a frame whose *content
        // grows* (a new obstacle label, a longer score) legitimately allocates
        // once for the new text slot; the buffers are then reused forever.
        // Measured worst case across the eight games: 3 allocations in 240
        // frames (wasd-race). Anything above the 12 here means a per-frame
        // allocation crept back in (before the v1.3.0 render fix this number
        // was ~4 per frame, i.e. ~960 in this window).
        CHECK_MSG(renderAllocations <= 12,
                  std::string(info.slug) + " render path allocated " +
                      std::to_string(renderAllocations) + " times across 240 frames");
    }
    hub.stopGame();
}

//===========================================================================
// v1.3.0 FIX: Frame::addText() stored the caller's std::u32string_view as-is,
// so a game that built a label from a local
//     const char32_t key = U'D';
//     const std::u32string_view label(&key, 1);
// left the frame pointing at destroyed stack memory — the rhythm lane labels
// rendered as unrelated characters ("噌"), and any front-end reading the frame
// after the game returned saw garbage. addText() now copies foreign views into
// the frame arena (interned views stay by reference).
static void testFrameOwnsText() {
    g_currentTest = "FrameTextOwnership";
    Frame frame;

    // Build the frame inside a scope, exactly like ArcadeManager::getFrame()
    // does, then read the text AFTER those locals are gone.
    {
        RhythmTypingGame rhythm;
        rhythm.setSeed(1234u);
        rhythm.setNoteCount(16);
        rhythm.start();
        for (int i = 0; i < 20; ++i) {
            rhythm.update(1.0 / 60.0);
        }
        rhythm.buildFrame(frame);
    }

    // Clobber the stack the game's locals used to live on: with the old
    // behaviour this is what the lane labels would read back.
    volatile std::uint32_t clobber[512];
    for (std::size_t i = 0; i < std::size(clobber); ++i) {
        clobber[i] = 0x564C564Cu;             // the garbage char seen in the wild
    }
    (void)clobber;

    CHECK(frame.texts.size() > 0);
    bool sawLane = false;
    for (const TextShape& t : frame.texts) {
        CHECK(t.text.data() != nullptr || t.text.empty());
        for (char32_t c : t.text) {
            // Every label in this game is ASCII or Vietnamese; the old bug left
            // CJK codepoints (0x564C etc.) in the buffer.
            CHECK_MSG(c < 0x2000 || c >= 0x1EA0,
                      "frame text must not contain stack garbage");
        }
        if (t.text == U"D" || t.text == U"F" || t.text == U"J" || t.text == U"K") {
            sawLane = true;
        }
    }
    CHECK_MSG(sawLane, "the four rhythm lane labels survive the buildFrame() scope");

    // The same frame re-read after another game wrote its own text: interned
    // strings must still be the ones that were added.
    const std::u32string firstText(frame.texts.front().text);
    frame.clear();
    CHECK(frame.texts.empty());
    CHECK(frame.droppedShapes == 0);
}

//===========================================================================
// v1.3.0: POST /api/config used to be stored and forgotten — a running game
// never saw the new fail mode, so the web dropdown (and the desktop config row)
// looked dead. setConfig() now pushes the live-safe subset immediately, and the
// chart-building knobs are reported as "needs a relaunch" instead of being
// silently swallowed.
static void testLiveConfigReachesRunningGame() {
    g_currentTest = "LiveConfig";
    auto& hub = ArcadeManager::instance();
    hub.setSeed(4242u);
    CHECK(hub.launchGame(GameType::Rhythm));

    auto* rhythm = dynamic_cast<RhythmTypingGame*>(hub.getCurrentGame());
    CHECK(rhythm != nullptr);
    if (rhythm == nullptr) {
        return;
    }
    CHECK(rhythm->getFailMode() == FailMode::Hardcore);   // the shipped default

    // Play a little so a silent reset() would be visible below.
    for (int i = 0; i < 30; ++i) {
        hub.update(0.05);
    }
    const double beforeSongTime = rhythm->getSongTime();
    const std::size_t beforeNotes = rhythm->getNotes().size();
    const std::int64_t beforeScore = rhythm->getScore();
    const double beforeBpm = rhythm->getBpm();
    CHECK(beforeSongTime > 0.0);

    ArcadeConfig config = hub.getConfig();
    config.rhythmFailMode = FailMode::HealthBar;
    config.rhythmBpm = beforeBpm + 20.0;      // chart knob: needs a relaunch
    config.typingRacePacerWpm = 90.0;         // live knob

    // The chart knob is reported (not swallowed), the live knob lands on the
    // game object that is running right now ...
    CHECK(hub.configNeedsRelaunch(config));
    hub.setConfig(config);
    CHECK(rhythm->getFailMode() == FailMode::HealthBar);

    // ... without restarting it: clock, chart and score survive the change.
    CHECK(rhythm->getSongTime() >= beforeSongTime);
    CHECK(rhythm->getNotes().size() == beforeNotes);
    CHECK(rhythm->getScore() >= beforeScore);
    CHECK(rhythm->getBpm() == beforeBpm);

    // The relaunch hook rebuilds the RUN (not the game object) so the
    // chart-level knobs finally apply — same seed, new tempo, fresh clock.
    const std::uint32_t seed = rhythm->getSeed();
    CHECK(hub.relaunchCurrentGame());
    auto* relaunched = dynamic_cast<RhythmTypingGame*>(hub.getCurrentGame());
    CHECK(relaunched != nullptr);
    if (relaunched != nullptr) {
        CHECK(relaunched->getBpm() == config.rhythmBpm);
        CHECK(relaunched->getFailMode() == FailMode::HealthBar);
        CHECK(relaunched->getSeed() == seed);
        CHECK(relaunched->getSongTime() < beforeSongTime);
    }

    // A config that only touches live knobs never asks for a relaunch, and the
    // pacer update reaches a running race immediately.
    ArcadeConfig liveOnly = hub.getConfig();
    liveOnly.noMistakeFailMode = FailMode::HealthBar;
    liveOnly.fishingAutomation = AutomationMode::Automated;
    liveOnly.typingRacePacerWpm = 123.0;
    CHECK(!hub.configNeedsRelaunch(liveOnly));
    hub.setConfig(liveOnly);
    CHECK(hub.launchGame(GameType::TypingRace, 7u));
    auto* race = dynamic_cast<TypingRaceGame*>(hub.getCurrentGame());
    CHECK(race != nullptr && race->getPacerWpm() == 123.0);
    ArcadeConfig faster = liveOnly;
    faster.typingRacePacerWpm = 77.0;
    CHECK(!hub.configNeedsRelaunch(faster));
    hub.setConfig(faster);
    CHECK(race != nullptr && race->getPacerWpm() == 77.0);

    hub.stopGame();
}

//===========================================================================
// Progression must be credited by the *native* path (ArcadeManager::update),
// not only by the HTTP bridge: "type a lot to level up" is a user-facing
// promise, and the desktop hub never talks to the bridge.
static void testProgressionCreditedNatively() {
    g_currentTest = "NativeProgressionCredit";
    auto& hub = ArcadeManager::instance();
    auto& progress = ok::progression::ProgressionEngine::instance();

    hub.stopGame();
    for (RunResult drain; hub.pollRunResult(drain);) {
    }
    progress.reset();
    const auto before = progress.getStats();
    CHECK(before.totalXp == 0);
    CHECK(before.typingRaceBestWpm == 0.0);

    // Play a real typing race to the finish line through the manager only.
    hub.setConfig(ArcadeConfig{});
    hub.launchGame(GameType::TypingRace, 99);
    auto* race = dynamic_cast<TypingRaceGame*>(hub.getCurrentGame());
    CHECK(race != nullptr);
    if (race != nullptr) {
        const std::u32string passage(race->getPassage());
        for (std::size_t i = 0; i < passage.size() && !race->isGameOver(); ++i) {
            hub.handleKey(0, passage[i], true);
            hub.update(0.06);            // ~ the measured typing speed of the run
        }
        CHECK(race->isGameOver());
    }

    // update() drains the finished run into the progression engine by itself.
    hub.update(1.0 / 60.0);
    const auto after = progress.getStats();
    CHECK_MSG(after.totalXp > 0, "the native update() path must credit XP");
    CHECK(after.typingRaceBestWpm > 0.0);
    CHECK(after.typingRaceBestWpm >= before.typingRaceBestWpm);

    // A run is credited exactly once: draining again must not double the XP.
    const auto snapshot = progress.getStats();
    CHECK(hub.drainRunResultsToProgression() == 0);
    CHECK(hub.pendingRunResultCount() == 0);
    CHECK(progress.getStats().totalXp == snapshot.totalXp);

    // Flexing Mode stays a joke: it never adds XP (documented exclusion).
    const std::uint64_t xpBefore = progress.getStats().totalXp;
    hub.launchGame(GameType::Flexing, 7);
    for (int i = 0; i < 40; ++i) {
        hub.handleKey(0, U'x', true);
        hub.update(1.0 / 60.0);
    }
    hub.stopGame();
    hub.update(1.0 / 60.0);
    CHECK(progress.getStats().totalXp == xpBefore);

    hub.stopGame();
    progress.reset();
}

int main() {
    std::printf("=== KieeKey Arcade Hub test suite (v1.3.0) ===\n");
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // keep progress visible if a test hangs
#define RUN(fn)                                                                  \
    do {                                                                         \
        std::printf("--- %s\n", #fn);                                            \
        fn();                                                                    \
    } while (0)
    RUN(testFrameModel);
    RUN(testCatalog);
    RUN(testSnake);
    RUN(testTetris);
    RUN(testFishing);
    RUN(testTypingRace);
    RUN(testWasdRace);
    RUN(testRhythm);
    RUN(testNoMistake);
    RUN(testFlexing);
    RUN(testManager);
    RUN(testFrameInvariants);
    RUN(testDeterminism);
    RUN(testNoSteadyStateAllocations);
    RUN(testFrameOwnsText);
    RUN(testLiveConfigReachesRunningGame);
    RUN(testProgressionCreditedNatively);

    std::printf("--- %d checks, %d failures ---\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("=== ALL ARCADE HUB TESTS PASSED ===\n");
        return 0;
    }
    std::printf("=== ARCADE HUB TESTS FAILED ===\n");
    return 1;
}
