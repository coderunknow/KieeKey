//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: demo/arcade_cli.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — Portable Arcade & Labs CLI Demonstrator
// Allows testing and playing minigames, chaos transforms, AI rival,
// progression stats and typing analytics in a terminal.
//----------------------------------------------------------------------------
#include <iostream>
#include <string>
#include "Arcade.hpp"
#include "ArcadeRender.hpp"
#include "ChaosEngine.hpp"
#include "AiRival.hpp"
#include "Progression.hpp"
#include "TypingAnalytics.hpp"
#include "OnlineGhost.hpp"
#include "kieekey_core.hpp"

using namespace ok::arcade;
using namespace ok::chaos;
using namespace ok::ai;
using namespace ok::progression;
using namespace ok::analytics;

// v1.3.0: the CLI shows the SAME display list the graphical front-ends paint
// (Win32 GDI window / HTML5 canvas), so a terminal run is a faithful preview of
// what the GUI shows — it is not a separate ASCII game implementation.
void dumpFrame(ArcadeManager& manager) {
    const Frame& frame = manager.getFrame();
    RenderList list;
    buildRenderList(frame, list);
    const GameStats& stats = frame.stats;
    std::cout << "  title   : " << list.title << "\n";
    if (!list.status.empty()) { std::cout << "  status  : " << list.status << "\n"; }
    if (!list.hint.empty())   { std::cout << "  hint    : " << list.hint << "\n"; }
    std::cout << "  score   : " << stats.score << "  (best " << stats.highScore << ")\n";
    std::cout << "  wpm/acc : " << stats.wpm << " WPM / " << stats.accuracy << "%\n";
    std::cout << "  progress: " << static_cast<int>(stats.progress * 100.0) << "%\n";
    std::cout << "  shapes  : " << list.commands.size() << " draw commands"
              << " (rects+circles+lines+polys+texts), dropped "
              << frame.droppedShapes << "\n";
    const std::string json = renderListToJson(list);
    std::cout << "  wire    : " << json.size() << " bytes of JSON per frame\n";
    std::cout << "\n" << renderListToText(list);
}

void printMenu() {
    std::cout << "\n======================================================\n";
    std::cout << "  KieeKey Arcade & Typing Lab (" << OPENKEY_KIEEKEY_VERSION_STRING << ")\n";
    std::cout << "======================================================\n";
    std::cout << "  1. Snake Game (Run 10 steps simulation)\n";
    std::cout << "  2. Tetris Game (Run simulation & display board)\n";
    std::cout << "  3. Fishing Game (Hook, reel, and catch simulation)\n";
    std::cout << "  4. Typing Race (Run race simulation)\n";
    std::cout << "  5. WASD + Typing Racing (Multitasking dodge simulation)\n";
    std::cout << "  6. Rhythm Typing (FNF-style timing simulation)\n";
    std::cout << "  7. No-Mistake Mode (Simulation)\n";
    std::cout << "  8. Flexing Mode (Ludicrous Speed typing demo)\n";
    std::cout << "  9. Chaos / Experimental Lab (Chaos Case & Glyph Flip)\n";
    std::cout << " 10. AI Typing Rival & Ghost Replay\n";
    std::cout << " 11. View Global Progression, XP & Achievements\n";
    std::cout << " 12. View Real-Time Typing Analytics & Coach Advice\n";
    std::cout << "  0. Exit\n";
    std::cout << "Choose an option: ";
}

//---- v1.3.0: non-interactive smoke test -----------------------------------
// Every game must (a) launch, (b) answer input through the shared
// ArcadeManager, (c) keep producing a display list for the GUI, and (d) turn
// that list into wire JSON. The same code path the Win32 window and the web
// player use, without a window or a socket.
int selfTestAllGames() {
    auto& mgr = ArcadeManager::instance();
    int failures = 0;
    const GameType games[] = {GameType::Snake,       GameType::Tetris,
                              GameType::Fishing,     GameType::TypingRace,
                              GameType::WasdRace,    GameType::Rhythm,
                              GameType::NoMistake,   GameType::Flexing};
    const char* slugs[] = {"snake",  "tetris", "fishing",  "typing-race",
                           "wasd-race", "rhythm", "no-mistake", "flexing"};

    for (std::size_t index = 0; index < std::size(games); ++index) {
        if (!mgr.launchGame(games[index], static_cast<std::uint32_t>(index) + 1u)) {
            std::cout << "  [FAIL] " << slugs[index] << ": launch refused\n";
            ++failures;
            continue;
        }
        const char32_t keys[] = {U'a', U's', U'd', U'f', U'j', U'k', U'l', U' '};
        for (int step = 0; step < 48; ++step) {
            (void)mgr.handleKey(0, keys[step % static_cast<int>(std::size(keys))], true);
            (void)mgr.handleKey(0, keys[step % static_cast<int>(std::size(keys))], false);
            mgr.update(1.0 / 60.0);
        }

        RenderList list;
        buildRenderList(mgr.getFrame(), list);
        std::string json;
        renderListToJson(list, json);
        const std::size_t commands = list.commands.size();
        const bool alive = mgr.getCurrentGameType() != GameType::None;
        if (commands == 0 || json.size() < 32) {
            std::cout << "  [FAIL] " << slugs[index] << ": empty frame (" << commands
                      << " commands, " << json.size() << " bytes)\n";
            ++failures;
        } else {
            std::cout << "  [ ok ] " << slugs[index] << ": " << commands << " commands, "
                      << json.size() << " bytes of JSON, title \"" << list.title << "\"\n";
        }
        if (!alive) {
            std::cout << "  [FAIL] " << slugs[index] << ": the run ended by itself\n";
            ++failures;
        }
        (void)mgr.handleKey(0x1B, 0, true);   // Esc: leave the game
    }

    // The Chaos engine and the Flexing pipeline are part of the same feature
    // set, so the smoke test covers them too.
    ChaosEngine& chaos = ChaosEngine::instance();
    ChaosConfig config = chaos.getConfig();
    config.masterEnabled = true;
    config.randomCaseEnabled = true;
    config.randomCaseIntensity = 1.0f;
    chaos.setConfig(config);
    const std::u32string shaped = chaos.processCase(U"nguyen van a", 7);
    if (shaped == U"nguyen van a") {
        std::cout << "  [FAIL] chaos: random case changed nothing\n";
        ++failures;
    } else {
        std::cout << "  [ ok ] chaos: " << ok::arcade::utf8FromUtf32(shaped) << "\n";
    }
    chaos.setConfig(ChaosConfig{});

    if (failures == 0) {
        std::cout << "KieeKey Arcade CLI self-test OK (8/8 games + chaos)\n";
    }
    return failures;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--test") {
        return selfTestAllGames();
    }

    auto& mgr = ArcadeManager::instance();
    int choice = -1;

    // Default automated test run if non-interactive
    if (argc > 1 && std::string(argv[1]) == "--auto") {
        choice = 1;
    } else {
        printMenu();
        if (!(std::cin >> choice)) {
            choice = 0;
        }
    }

    switch (choice) {
        case 1: {
            std::cout << "\n--- Snake Simulation ---\n";
            mgr.launchGame(GameType::Snake);
            for (int i = 0; i < 5; ++i) {
                mgr.update(0.15);
            }
            dumpFrame(mgr);
            mgr.stopGame();
            break;
        }
        case 2: {
            std::cout << "\n--- Tetris Simulation ---\n";
            mgr.launchGame(GameType::Tetris);
            mgr.update(0.5);
            dumpFrame(mgr);
            mgr.stopGame();
            break;
        }
        case 3: {
            std::cout << "\n--- Fishing Simulation ---\n";
            mgr.launchGame(GameType::Fishing);
            dumpFrame(mgr);
            mgr.stopGame();
            break;
        }
        case 4: {
            std::cout << "\n--- Typing Race Simulation ---\n";
            mgr.launchGame(GameType::TypingRace);
            mgr.handleKey(0, U'K', true);
            mgr.update(0.5);
            dumpFrame(mgr);
            mgr.stopGame();
            break;
        }
        case 5: {
            std::cout << "\n--- WASD Racing Simulation ---\n";
            mgr.launchGame(GameType::WasdRace);
            mgr.update(0.5);
            dumpFrame(mgr);
            mgr.stopGame();
            break;
        }
        case 6: {
            std::cout << "\n--- Rhythm Typing Simulation ---\n";
            mgr.launchGame(GameType::Rhythm);
            mgr.update(1.0);
            mgr.handleKey(0, U'd', true);
            dumpFrame(mgr);
            mgr.stopGame();
            break;
        }
        case 7: {
            std::cout << "\n--- No-Mistake Simulation ---\n";
            mgr.launchGame(GameType::NoMistake);
            mgr.handleKey(0, U'h', true);
            mgr.handleKey(0, U'o', true);
            mgr.handleKey(0, U'c', true);
            dumpFrame(mgr);
            mgr.stopGame();
            break;
        }
        case 8: {
            std::cout << "\n--- Flexing Mode Demo ---\n";
            mgr.launchGame(GameType::Flexing);
            for (int i = 0; i < 10; ++i) {
                mgr.handleKey(0x41, U'a', true);
            }
            mgr.update(0.2);
            dumpFrame(mgr);
            mgr.stopGame();
            break;
        }
        case 9: {
            std::cout << "\n--- Chaos Lab Demo ---\n";
            std::u32string sample = U"KieeKey Vietnamese Input Method";
            auto cCase = ChaosEngine::applyRandomCase(sample, 0.7f, CaseGranularity::ByChar, 12345);
            auto fVert = ChaosEngine::applyGlyphTransform(sample, GlyphTransformMode::FlipVertical);
            std::cout << "Original:      KieeKey Vietnamese Input Method\n";
            std::cout << "Chaos Cased:   ";
            for (char32_t c : cCase) std::cout << static_cast<char>(c);
            std::cout << "\n[Notice: Visual effect only. Underlying text remains 100% original Unicode.]\n";
            break;
        }
        case 10: {
            std::cout << "\n--- Personal AI Rival Demo ---\n";
            auto& ai = AiRivalEngine::instance();
            ai.setOptIn(true);
            auto prof = ai.getProfile();
            std::cout << "AI Learned Mean IKI: " << prof.meanIkiMs << " ms\n";
            std::cout << "AI Natural Typo Rate: " << (prof.errorRate * 100.0) << "%\n";
            ai.setOptIn(false);
            break;
        }
        case 11: {
            std::cout << "\n--- Progression & Level ---\n";
            auto stats = ProgressionEngine::instance().getStats();
            std::cout << "Level: " << stats.currentLevel << " (Total XP: " << stats.totalXp << ")\n";
            std::cout << "Total Keystrokes: " << stats.totalKeystrokes << "\n";
            std::cout << "Best WPM: " << stats.bestWpm << "\n";
            break;
        }
        case 12: {
            std::cout << "\n--- Analytics & Coach Advice ---\n";
            auto recs = TypingAnalyticsEngine::instance().generateCoachingAdvice();
            if (recs.empty()) {
                std::cout << "Sample count under threshold (need >= 40 keys to generate suggestions).\n";
            }
            break;
        }
        default:
            std::cout << "Exiting KieeKey Arcade CLI.\n";
            break;
    }

    return 0;
}
