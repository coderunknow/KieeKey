//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/test_arcade_server.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
// Arcade web bridge suite (head-less: no socket, no browser).
//
//  1. Routing                — every documented route, plus its error cases
//  2. Session control        — start / pause / restart / stop
//  3. Input forwarding       — key events reach the C++ game and are consumed
//  4. Static serving         — web root + path-traversal guard
//  5. Full simulated run     — play a whole game through the API and verify
//                              that the frame, the score and the progression
//                              records all move
//  6. Isolation              — two servers own independent sessions
//============================================================================
#include "ArcadeServer.hpp"
#include "Progression.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace ok::arcade;
using ok::progression::ProgressionEngine;

namespace {

std::string bodyOf(const HttpResponse& response) { return response.body; }

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string makeTempWebRoot() {
    const auto root = std::filesystem::temp_directory_path() / "kieekey_web_test";
    std::filesystem::create_directories(root);
    std::ofstream index(root / "index.html", std::ios::trunc);
    index << "<!DOCTYPE html><title>KieeKey Arcade Hub</title><canvas id=\"screen\"></canvas>";
    std::ofstream script(root / "arcade.js", std::ios::trunc);
    script << "// test asset\n";
    return root.string();
}

} // namespace

//---------------------------------------------------------------------------
void testRouting() {
    ArcadeServer server;
    assert(!server.isRunning());

    auto ping = server.handleRequest("GET", "/api/ping", "");
    assert(ping.status == 200);
    assert(contains(bodyOf(ping), "\"pong\":true"));

    auto catalog = server.handleRequest("GET", "/api/catalog", "");
    assert(catalog.status == 200);
    assert(contains(bodyOf(catalog), "\"games\""));
    assert(contains(bodyOf(catalog), "\"snake\""));
    assert(contains(bodyOf(catalog), "\"flexing\""));

    auto state = server.handleRequest("GET", "/api/state", "");
    assert(state.status == 200);
    assert(contains(bodyOf(state), "\"active\":false"));
    assert(contains(bodyOf(state), "\"frame\":"));

    // Query strings are ignored, not 404s.
    auto withQuery = server.handleRequest("GET", "/api/state?t=123", "");
    assert(withQuery.status == 200);

    auto health = server.handleRequest("GET", "/healthz", "");
    assert(health.status == 200 && health.body == "ok");

    auto missing = server.handleRequest("GET", "/definitely-not-here", "");
    assert(missing.status == 404);

    auto badStart = server.handleRequest("POST", "/api/start", "{\"slug\":\"flappy\"}");
    assert(badStart.status == 400);
    assert(contains(bodyOf(badStart), "\"ok\":false"));

    auto noBody = server.handleRequest("POST", "/api/start", "");
    assert(noBody.status == 400);

    auto wrongMethod = server.handleRequest("PUT", "/api/ping", "");
    assert(wrongMethod.status == 405);
    std::cout << "  [PASS] HTTP routing & error cases\n";
}

//---------------------------------------------------------------------------
void testSessionControl() {
    ArcadeServer server;

    auto start = server.handleRequest("POST", "/api/start", "{\"slug\":\"tetris\"}");
    assert(start.status == 200);
    assert(contains(bodyOf(start), "\"started\":\"tetris\""));

    auto state = server.handleRequest("GET", "/api/state", "");
    assert(contains(bodyOf(state), "\"active\":true"));
    assert(contains(bodyOf(state), "\"slug\":\"tetris\""));
    assert(contains(bodyOf(state), "\"gameId\":2"));
    assert(contains(bodyOf(state), "\"paused\":false"));

    // Start by numeric id as well (the client may not know the slug yet).
    assert(server.handleRequest("POST", "/api/start", "{\"id\":1}").status == 200);
    state = server.handleRequest("GET", "/api/state", "");
    assert(contains(bodyOf(state), "\"slug\":\"snake\""));

    auto pause = server.handleRequest("POST", "/api/pause", "{}");
    assert(pause.status == 200);
    state = server.handleRequest("GET", "/api/state", "");
    assert(contains(bodyOf(state), "\"paused\":true"));

    assert(server.handleRequest("POST", "/api/pause", "{}").status == 200);
    state = server.handleRequest("GET", "/api/state", "");
    assert(contains(bodyOf(state), "\"paused\":false"));

    assert(server.handleRequest("POST", "/api/restart", "{}").status == 200);
    assert(server.handleRequest("POST", "/api/stop", "{}").status == 200);
    state = server.handleRequest("GET", "/api/state", "");
    assert(contains(bodyOf(state), "\"active\":false"));
    std::cout << "  [PASS] Session control tests\n";
}

//---------------------------------------------------------------------------
void testInputForwarding() {
    ArcadeServer server;
    assert(server.startGame("snake"));

    // Arrow keys are consumed by the game while it is live.
    auto key = server.handleRequest("POST", "/api/input", "{\"vk\":39,\"ch\":\"\",\"down\":true}");
    assert(key.status == 200);
    assert(contains(bodyOf(key), "\"ok\":true"));
    // InputResult::Consumed == 1
    assert(contains(bodyOf(key), "\"result\":1"));

    // Key release must be forwarded too (games track held keys for racing).
    auto release = server.handleRequest("POST", "/api/input", "{\"vk\":39,\"ch\":\"\",\"down\":false}");
    assert(release.status == 200);

    // Text route: the typed characters reach the game.
    assert(server.startGame("typing-race"));
    auto typed = server.handleRequest("POST", "/api/text", "{\"text\":\"KieeKey\"}");
    assert(typed.status == 200);
    auto missingText = server.handleRequest("POST", "/api/text", "{}");
    assert(missingText.status == 400);

    // Escape leaves the game (InputResult::ExitRequested == 2).
    auto escape = server.handleRequest("POST", "/api/input", "{\"vk\":27,\"ch\":\"\",\"down\":true}");
    assert(escape.status == 200);
    assert(contains(bodyOf(escape), "\"result\":2"));
    std::cout << "  [PASS] Input forwarding tests\n";
}

//---------------------------------------------------------------------------
void testStaticServingAndTraversal() {
    const std::string webRoot = makeTempWebRoot();
    ArcadeServerConfig config;
    config.webRoot = webRoot;
    ArcadeServer server(config);

    auto index = server.handleRequest("GET", "/", "");
    assert(index.status == 200);
    assert(contains(index.contentType, "text/html"));
    assert(contains(bodyOf(index), "KieeKey Arcade Hub"));

    auto explicitIndex = server.handleRequest("GET", "/index.html", "");
    assert(explicitIndex.status == 200);

    auto script = server.handleRequest("GET", "/arcade.js", "");
    assert(script.status == 200);
    assert(contains(script.contentType, "javascript"));

    auto head = server.handleRequest("HEAD", "/index.html", "");
    assert(head.status == 200);
    assert(head.body.empty());

    auto missing = server.handleRequest("GET", "/nope.js", "");
    assert(missing.status == 404);

    // Path traversal is refused *before* touching the filesystem.
    for (const char* attack : {"/../etc/passwd", "/..%2Fetc/passwd", "/../../etc/shadow",
                               "/sub/../../etc/passwd"}) {
        auto response = server.handleRequest("GET", attack, "");
        assert(response.status == 404);
        assert(!contains(bodyOf(response), "root:"));
    }

    // The API stays reachable even with a bogus web root.
    ArcadeServerConfig broken;
    broken.webRoot = "/definitely/not/a/directory";
    ArcadeServer brokenServer(broken);
    assert(brokenServer.handleRequest("GET", "/api/ping", "").status == 200);
    assert(brokenServer.handleRequest("GET", "/index.html", "").status == 404);

    std::filesystem::remove_all(webRoot);
    std::cout << "  [PASS] Static serving & traversal guards\n";
}

//---------------------------------------------------------------------------
void testFullSimulatedRun() {
    auto& progress = ProgressionEngine::instance();
    progress.reset();
    assert(progress.getStats().typingRaceBestWpm == 0.0);

    ArcadeServer server;
    assert(server.startGame("typing-race"));

    auto* game = dynamic_cast<TypingRaceGame*>(server.manager().getCurrentGame());
    assert(game != nullptr);
    const std::u32string passage(game->getPassage());
    assert(!passage.empty());

    std::string opening = bodyOf(server.handleRequest("GET", "/api/state", ""));
    assert(contains(opening, "\"active\":true"));
    assert(contains(opening, "\"slug\":\"typing-race\""));

    // Type the whole passage through the HTTP layer, exactly as the browser
    // client does, and watch the frame evolve.
    bool frameChanged = false;
    for (std::size_t i = 0; i < passage.size(); ++i) {
        std::string character = utf8FromUtf32(std::u32string(1, passage[i]));
        if (character == "\"" || character == "\\") {
            character.insert(character.begin(), '\\');
        }
        auto typed = server.handleRequest("POST", "/api/text", "{\"text\":\"" + character + "\"}");
        assert(typed.status == 200);
        assert(contains(bodyOf(typed), "\"result\":1"));   // the game consumed it
        for (int sub = 0; sub < 3; ++sub) {
            server.tick(1.0 / 60.0);
        }
        if (game->isGameOver()) {
            break;
        }
        if ((i % 8) == 0) {
            if (bodyOf(server.handleRequest("GET", "/api/state", "")) != opening) {
                frameChanged = true;
            }
        }
    }
    for (int frame = 0; frame < 600 && !game->isGameOver(); ++frame) {
        server.tick(1.0 / 60.0);
    }
    assert(game->isGameOver());
    assert(frameChanged);

    // The finished run reached the progression engine through the documented
    // arcade -> progression mapping (a typing race sets the best WPM).
    const std::string finished = bodyOf(server.handleRequest("GET", "/api/state", ""));
    assert(contains(finished, "\"gameOver\":true"));
    const auto stats = progress.getStats();
    assert(stats.typingRaceBestWpm > 0.0);
    assert(stats.totalXp > 0);

    server.stopGame();
    server.tick(1.0 / 60.0);
    assert(contains(bodyOf(server.handleRequest("GET", "/api/state", "")), "\"active\":false"));
    progress.reset();
    std::cout << "  [PASS] Full simulated run & progression feed\n";
}

//---------------------------------------------------------------------------
void testSessionIsolation() {
    ArcadeServer first;
    ArcadeServer second;

    assert(first.startGame("snake"));
    assert(second.startGame("tetris"));

    assert(contains(bodyOf(first.handleRequest("GET", "/api/state", "")), "\"slug\":\"snake\""));
    assert(contains(bodyOf(second.handleRequest("GET", "/api/state", "")), "\"slug\":\"tetris\""));

    first.stopGame();
    assert(contains(bodyOf(first.handleRequest("GET", "/api/state", "")), "\"active\":false"));
    assert(contains(bodyOf(second.handleRequest("GET", "/api/state", "")), "\"active\":true"));

    // The config route validates its inputs instead of trusting the client.
    auto badConfig = first.handleRequest("POST", "/api/config", "{\"rhythmBpm\":99999}");
    assert(badConfig.status == 200);
    assert(!contains(bodyOf(badConfig), "\"rhythmBpm\":99999"));
    auto goodConfig = first.handleRequest(
        "POST", "/api/config",
        "{\"rhythmFailMode\":1,\"noMistakeFailMode\":1,\"rhythmBpm\":150,\"typingRacePacerWpm\":90}");
    assert(contains(bodyOf(goodConfig), "\"rhythmBpm\":150"));
    assert(contains(bodyOf(goodConfig), "\"typingRacePacerWpm\":90"));
    assert(contains(bodyOf(goodConfig), "\"rhythmFailMode\":1"));

    // ...and the two enumerations used for progression stay in lock-step.
    assert(progressionGameIdFor(GameType::Snake) == 1);
    assert(progressionGameIdFor(GameType::Tetris) == 2);
    assert(progressionGameIdFor(GameType::Fishing) == 3);
    assert(progressionGameIdFor(GameType::TypingRace) == 4);
    assert(progressionGameIdFor(GameType::WasdRace) == 5);
    assert(progressionGameIdFor(GameType::Rhythm) == 6);
    assert(progressionGameIdFor(GameType::NoMistake) == 7);
    assert(progressionGameIdFor(GameType::Flexing) == 8);
    assert(progressionGameIdFor(GameType::None) == 0);
    std::cout << "  [PASS] Session isolation & config validation\n";
}

int main() {
    std::cout << "=== Running Arcade Server Suite ===\n";
    testRouting();
    testSessionControl();
    testInputForwarding();
    testStaticServingAndTraversal();
    testFullSimulatedRun();
    testSessionIsolation();
    std::cout << "=== ALL ARCADE SERVER TESTS PASSED ===\n";
    return 0;
}
