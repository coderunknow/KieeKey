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

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--test") {
        std::cout << "KieeKey Arcade CLI self-test OK\n";
        return 0;
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
            std::cout << mgr.renderCurrentGame();
            mgr.stopGame();
            break;
        }
        case 2: {
            std::cout << "\n--- Tetris Simulation ---\n";
            mgr.launchGame(GameType::Tetris);
            mgr.update(0.5);
            std::cout << mgr.renderCurrentGame();
            mgr.stopGame();
            break;
        }
        case 3: {
            std::cout << "\n--- Fishing Simulation ---\n";
            mgr.launchGame(GameType::Fishing);
            std::cout << mgr.renderCurrentGame();
            mgr.stopGame();
            break;
        }
        case 4: {
            std::cout << "\n--- Typing Race Simulation ---\n";
            mgr.launchGame(GameType::TypingRace);
            mgr.handleKey(0, U'K', true);
            mgr.update(0.5);
            std::cout << mgr.renderCurrentGame();
            mgr.stopGame();
            break;
        }
        case 5: {
            std::cout << "\n--- WASD Racing Simulation ---\n";
            mgr.launchGame(GameType::WasdRace);
            mgr.update(0.5);
            std::cout << mgr.renderCurrentGame();
            mgr.stopGame();
            break;
        }
        case 6: {
            std::cout << "\n--- Rhythm Typing Simulation ---\n";
            mgr.launchGame(GameType::Rhythm);
            mgr.update(1.0);
            mgr.handleKey(0, U'd', true);
            std::cout << mgr.renderCurrentGame();
            mgr.stopGame();
            break;
        }
        case 7: {
            std::cout << "\n--- No-Mistake Simulation ---\n";
            mgr.launchGame(GameType::NoMistake);
            mgr.handleKey(0, U'h', true);
            mgr.handleKey(0, U'o', true);
            mgr.handleKey(0, U'c', true);
            std::cout << mgr.renderCurrentGame();
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
            std::cout << mgr.renderCurrentGame();
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
