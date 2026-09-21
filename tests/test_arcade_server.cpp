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
//  7. Flexing payload        — the text KieeKey types on the user's behalf is
//                              delivered by /api/state and never invented by
//                              the browser, plus the /api/preload pipeline
//  8. Chaos lab              — the web lab's transformation is the one the
//                              engine really performs (incl. render-only 90°)
//  9. Progression & AI rival — the HUD numbers of the browser (XP/level) and
//                              the learned rival profile come from the engines,
//                              and the rival is opt-in
//============================================================================
#include "ArcadeServer.hpp"
#include "AiRival.hpp"
#include "ChaosEngine.hpp"
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

//---------------------------------------------------------------------------
void testFlexingPayload() {
    ArcadeServer server;
    assert(server.startGame("flexing"));

    // Nothing is active in another game: the route refuses honestly.
    ArcadeServer other;
    assert(other.startGame("snake"));
    auto refused = other.handleRequest("POST", "/api/preload", "{\"text\":\"abc\"}");
    assert(refused.status == 409);

    // The client supplies the source text; the server reports how much of it
    // it will produce (never the other way around).
    auto preload = server.handleRequest(
        "POST", "/api/preload",
        "{\"text\":\"Xin chao, toi la KieeKey!\",\"granularity\":1}");
    assert(preload.status == 200);
    assert(contains(bodyOf(preload), "\"ok\":true"));
    assert(contains(bodyOf(preload), "\"total\":25"));
    assert(contains(bodyOf(preload), "\"granularity\":1"));

    // Bad bodies are rejected, not silently accepted.
    assert(server.handleRequest("POST", "/api/preload", "{}").status == 400);

    // First keypress: one word of output must arrive through /api/state.
    auto key = server.handleRequest("POST", "/api/input", "{\"vk\":88,\"down\":true}");
    assert(key.status == 200);
    server.tick(1.0 / 60.0);

    const std::string state = bodyOf(server.handleRequest("GET", "/api/state", ""));
    assert(contains(state, "\"flex\":{"));
    assert(contains(state, "\"emitted\":\"Xin \""));
    assert(contains(state, "\"generated\":4"));

    // ...and only once: the polling route may peek but the buffer is consumed
    // by the state route the stream uses.
    const std::string statusPeek = bodyOf(server.handleRequest("GET", "/api/status", ""));
    assert(contains(statusPeek, "\"emitted\":\"\""));
    const std::string second = bodyOf(server.handleRequest("GET", "/api/state", ""));
    assert(contains(second, "\"emitted\":\"\""));

    // A different game carries no Flexing block at all.
    assert(!contains(bodyOf(other.handleRequest("GET", "/api/state", "")), "\"flex\":"));

    // One char per key: the engine, not the client, decides the granularity.
    auto perChar = server.handleRequest("POST", "/api/preload",
                                        "{\"granularity\":0}");
    assert(contains(bodyOf(perChar), "\"granularity\":0"));
    (void)server.handleRequest("POST", "/api/input", "{\"vk\":89,\"down\":true}");
    server.tick(1.0 / 60.0);
    const std::string charState = bodyOf(server.handleRequest("GET", "/api/state", ""));
    assert(contains(charState, "\"generated\":5"));

    // Unknown verbs on the new route keep the documented 405 answer.
    assert(server.handleRequest("PUT", "/api/preload", "{}").status == 405);

    server.stopGame();
    std::cout << "  [PASS] Flexing Mode payload & preload pipeline\n";
}

//---------------------------------------------------------------------------
void testChaosLab() {
    auto& chaos = ok::chaos::ChaosEngine::instance();
    chaos.setConfig(ok::chaos::ChaosConfig{});

    ArcadeServer server;
    const std::string clean = bodyOf(server.handleRequest("GET", "/api/chaos", ""));
    assert(contains(clean, "\"enabled\":false"));
    assert(contains(clean, "\"active\":false"));

    // Turning everything on through the API is exactly what the lab does.
    const std::string enabled = bodyOf(server.handleRequest(
        "POST", "/api/chaos",
        "{\"enabled\":true,\"randomCase\":true,\"caseIntensityPercent\":100,"
        "\"caseGranularity\":1,\"glyph\":true,\"glyphMode\":2,\"glyphIntensityPercent\":100}"));
    assert(contains(enabled, "\"enabled\":true"));
    assert(contains(enabled, "\"randomCase\":true"));
    assert(contains(enabled, "\"glyphMode\":2"));
    assert(contains(enabled, "\"active\":true"));

    // The preview must be the engine's own output, not a JS lookalike: with the
    // master switch on, the injected text can no longer equal the input.
    const std::string preview = bodyOf(server.handleRequest(
        "POST", "/api/chaos/preview", "{\"text\":\"nguyen van a\"}"));
    assert(contains(preview, "\"preview\":{"));
    assert(!contains(preview, "\"injected\":\"nguyen van a\""));
    assert(contains(preview, "\"display\":\""));

    // Out-of-range values are clamped, unknown fields ignored.
    const std::string clamped = bodyOf(server.handleRequest(
        "POST", "/api/chaos", "{\"caseIntensityPercent\":900,\"glyphMode\":99}"));
    assert(contains(clamped, "\"caseIntensityPercent\":100"));
    assert(contains(clamped, "\"glyphMode\":2"));   // 99 rejected, 2 kept

    // 90° rotation is honest about being render-only.
    (void)server.handleRequest("POST", "/api/chaos", "{\"glyphMode\":1}");
    const std::string rotated = bodyOf(server.handleRequest(
        "POST", "/api/chaos/preview", "{\"text\":\"abc\"}"));
    assert(contains(rotated, "\"renderOnlyMode\":true"));

    // The same guard answers 405/404 for the wrong verb/route.
    assert(server.handleRequest("DELETE", "/api/chaos", "{}").status == 405);
    assert(server.handleRequest("GET", "/api/chaos/nope", "").status == 404);

    chaos.setConfig(ok::chaos::ChaosConfig{});
    std::cout << "  [PASS] Chaos lab transformation via the shared engine\n";
}

//---------------------------------------------------------------------------
void testProgressionAndRivalRoutes() {
    auto& progress = ProgressionEngine::instance();
    progress.reset();

    ArcadeServer server;
    const std::string empty = bodyOf(server.handleRequest("GET", "/api/progression", ""));
    assert(contains(empty, "\"level\":1"));
    assert(contains(empty, "\"xp\":0"));
    assert(contains(empty, "\"levelPercent\":0"));

    // Playing a race through the API must move the HUD numbers: this is the
    // "type a lot and level up" promise, seen from the browser.
    assert(server.startGame("typing-race"));
    auto* race = dynamic_cast<TypingRaceGame*>(server.manager().getCurrentGame());
    assert(race != nullptr);
    const std::u32string passage(race->getPassage());
    for (std::size_t i = 0; i < passage.size() && !race->isGameOver(); ++i) {
        std::string character = utf8FromUtf32(std::u32string(1, passage[i]));
        if (character == "\"" || character == "\\") { character.insert(character.begin(), '\\'); }
        (void)server.handleRequest("POST", "/api/text", "{\"text\":\"" + character + "\"}");
        server.tick(1.0 / 60.0);
    }
    const std::string earned = bodyOf(server.handleRequest("GET", "/api/progression", ""));
    assert(!contains(earned, "\"xp\":0"));
    assert(contains(earned, "\"bestWpm\":"));
    assert(contains(earned, "\"typingRaceBestWpm\":"));

    // The rival is off until the user opts in, and its numbers come from the
    // profile the engine learned (never from the client).
    auto& rival = ok::ai::AiRivalEngine::instance();
    rival.setOptIn(false);
    rival.resetProfile();
    const std::string off = bodyOf(server.handleRequest("GET", "/api/rival", ""));
    assert(contains(off, "\"optIn\":false"));
    assert(contains(off, "\"samples\":0"));
    assert(contains(off, "\"profileWpm\":"));

    const std::string on = bodyOf(server.handleRequest("POST", "/api/rival", "{\"optIn\":true}"));
    assert(contains(on, "\"optIn\":true"));

    // With the race still running, the route previews the rival's finish time
    // on the very passage the player is typing.
    assert(contains(on, "\"passageChars\":"));
    assert(contains(on, "\"rivalFinishSec\":"));
    const double rivalWpm = [&on] {
        const std::string key = "\"rivalWpm\":";
        const std::size_t at = on.find(key);
        return (at == std::string::npos) ? 0.0 : std::stod(on.substr(at + key.size()));
    }();
    assert(rivalWpm > 0.0);

    (void)server.handleRequest("POST", "/api/rival", "{\"train\":true}");
    (void)server.handleRequest("POST", "/api/rival", "{\"reset\":true}");
    assert(contains(bodyOf(server.handleRequest("GET", "/api/rival", "")), "\"samples\":0"));

    // Verb / route guards stay intact for the new endpoints.
    assert(server.handleRequest("DELETE", "/api/progression", "{}").status == 405);
    assert(server.handleRequest("DELETE", "/api/rival", "{}").status == 405);
    assert(server.handleRequest("GET", "/api/rival/nope", "").status == 404);

    server.stopGame();
    rival.setOptIn(false);
    rival.resetProfile();
    progress.reset();
    std::cout << "  [PASS] Progression & AI rival routes\n";
}

//---------------------------------------------------------------------------
// v1.3.0: POST /api/config must not silently drop what the user changed.
//  * fail mode / pacer / automation -> applied to the RUNNING game, no restart;
//  * chart knobs (bpm, note count, approach, reserve, start fuel) -> reported
//    as restartRequired, or applied right away when the caller passes
//    applyNow=1 (which rebuilds the run instead of pretending).
void testConfigApplyNow() {
    ArcadeServer server;
    auto begin = server.handleRequest("POST", "/api/start", "{\"slug\":\"rhythm\"}");
    assert(begin.status == 200);

    // Live knob: no relaunch needed, and the response says so.
    auto live = server.handleRequest("POST", "/api/config", "{\"rhythmFailMode\":1}");
    assert(live.status == 200);
    assert(contains(bodyOf(live), "\"restartRequired\":false"));
    assert(contains(bodyOf(live), "\"rhythmFailMode\":1"));

    // Chart knob: reported as requiring a relaunch — the old code answered
    // ok:true and changed nothing, which is exactly what made the web slider
    // look broken.
    auto chart = server.handleRequest("POST", "/api/config", "{\"rhythmBpm\":150}");
    assert(chart.status == 200);
    assert(contains(bodyOf(chart), "\"restartRequired\":true"));
    assert(contains(bodyOf(chart), "\"rhythmBpm\":150"));
    assert(contains(bodyOf(chart), "rhythmNoteCount"));   // the key list is named

    // v1.3.0-beta8 (bug UX-01) — THIS EXPECTATION WAS WRONG AND HID THE BUG.
    // The previous call stored 150 WITHOUT relaunching, so the run is still
    // playing the old chart: the pending change is real and applyNow=1 must
    // honour it. The old assertion ("re-sending the same value is a no-op")
    // compared the request against the STORED config instead of against the
    // RUNNING one, which is exactly the browser's event sequence — `input`
    // stores, `change` re-sends the same value with applyNow — so the final
    // event always concluded "nothing to do" and the tempo never changed.
    // The suite stayed green while the web slider was dead.
    auto pending = server.handleRequest("POST", "/api/config",
                                        "{\"rhythmBpm\":150,\"applyNow\":1}");
    assert(pending.status == 200);
    assert(contains(bodyOf(pending), "\"restartApplied\":true"));

    // A no-op is only a no-op once the RUN is already up to date.
    auto noop = server.handleRequest("POST", "/api/config",
                                     "{\"rhythmBpm\":150,\"applyNow\":1}");
    assert(noop.status == 200);
    assert(contains(bodyOf(noop), "\"restartApplied\":false"));
    assert(contains(bodyOf(noop), "\"restartRequired\":false"));

    auto applied = server.handleRequest("POST", "/api/config",
                                        "{\"rhythmBpm\":170,\"applyNow\":1}");
    assert(applied.status == 200);
    assert(contains(bodyOf(applied), "\"restartApplied\":true"));
    assert(contains(bodyOf(applied), "\"restartRequired\":false"));

    auto state = server.handleRequest("GET", "/api/state", "");
    assert(state.status == 200);
    assert(contains(bodyOf(state), "\"active\":true"));
    assert(contains(bodyOf(state), "rhythm"));
    assert(contains(bodyOf(state), "\"frame\":"));

    // The knob survives a restart request (config is sticky, not per-game).
    auto sticky = server.handleRequest("POST", "/api/config", "{}");
    assert(sticky.status == 200);
    assert(contains(bodyOf(sticky), "\"rhythmBpm\":170"));
    assert(contains(bodyOf(sticky), "\"rhythmFailMode\":1"));
    assert(contains(bodyOf(sticky), "\"restartRequired\":false"));

    // Out-of-range values are ignored instead of corrupting the config.
    auto bad = server.handleRequest("POST", "/api/config", "{\"rhythmBpm\":99999}");
    assert(bad.status == 200);
    assert(contains(bodyOf(bad), "\"rhythmBpm\":170"));
    assert(!contains(bodyOf(bad), "99999"));

    (void)server.handleRequest("POST", "/api/stop", "");
    std::cout << "  [PASS] config route (live knobs + applyNow relaunch)\n";
}

//---------------------------------------------------------------------------
// v1.3.0-beta6 (V2/B7): the WASD-race steering choice reaches the web player.
// It is a LIVE knob (applyLiveConfigToGame reinterprets the next key), so it
// must be applied without a relaunch and echoed back; out-of-range values are
// ignored. The behavioral side (steer+compose) is pinned in test_arcade_beta5.
void testWasdSteeringConfig() {
    ArcadeServer server;
    auto begin = server.handleRequest("POST", "/api/start", "{\"slug\":\"wasd-race\"}");
    assert(begin.status == 200);

    // Default is Arrows (0); the route echoes the applied value.
    auto base = server.handleRequest("POST", "/api/config", "{}");
    assert(base.status == 200);
    assert(contains(bodyOf(base), "\"wasdSteering\":0"));

    // Live knob: applied immediately, no relaunch requested.
    auto set = server.handleRequest("POST", "/api/config", "{\"wasdSteering\":2}");
    assert(set.status == 200);
    assert(contains(bodyOf(set), "\"wasdSteering\":2"));
    assert(contains(bodyOf(set), "\"restartRequired\":false"));

    // Sticky across an empty-body read.
    auto sticky = server.handleRequest("POST", "/api/config", "{}");
    assert(contains(bodyOf(sticky), "\"wasdSteering\":2"));

    // Out-of-range is ignored instead of corrupting the config.
    auto bad = server.handleRequest("POST", "/api/config", "{\"wasdSteering\":9}");
    assert(bad.status == 200);
    assert(contains(bodyOf(bad), "\"wasdSteering\":2"));
    assert(!contains(bodyOf(bad), "\"wasdSteering\":9"));

    (void)server.handleRequest("POST", "/api/stop", "");
    std::cout << "  [PASS] wasd steering config route (B7 web parity)\n";
}

//---------------------------------------------------------------------------
// v1.3.0-beta6 (V2/B5): the red divergent tail + the "Backspace để sửa" hint
// are built at FRAME level (Arcade.cpp addComposedLine / stats.hint) and the
// web player receives them through the SAME JSON the GDI renderer never sees.
// This proves the web transport carries them: type wrong Telex into a fresh
// Vietnamese wasd-race run and read /api/state. Passages are real sentences,
// so "aaaaa" diverges from every one of them within a few keys.
void testDivergentTailReachesWebJson() {
    ArcadeServer server;
    auto begin = server.handleRequest("POST", "/api/start", "{\"slug\":\"wasd-race\"}");
    assert(begin.status == 200);

    // Sanity: a non-diverged frame has no composed-buffer line yet.
    auto clean = server.handleRequest("GET", "/api/state", "");
    assert(clean.status == 200);
    assert(!contains(bodyOf(clean), "Bạn đã gõ"));

    // Type characters that cannot match the start of any passage.
    auto typed = server.handleRequest("POST", "/api/text", "{\"text\":\"aaaaa\"}");
    assert(typed.status == 200);

    auto diverged = server.handleRequest("GET", "/api/state", "");
    assert(diverged.status == 200);
    const std::string body = bodyOf(diverged);
    // The composed-buffer line label reaches the web client...
    assert(contains(body, "Bạn đã gõ"));
    // ...with the corrective hint (N Backspace presses) in the frame stats...
    assert(contains(body, "Sai — nhấn Backspace"));
    assert(contains(body, "để sửa"));
    // ...and the divergent tail arrives as its OWN text run drawn in the
    // "wrong" palette color (same format as ArcadeRender's appendColor).
    char badHex[16];
    std::snprintf(badHex, sizeof(badHex), "\"#%02X%02X%02X%02X\"", colorR(palette::kBad),
                  colorG(palette::kBad), colorB(palette::kBad), colorA(palette::kBad));
    assert(contains(body, badHex));
    assert(contains(body, "aaaa"));   // the composed tail itself, as a run payload
    std::cout << "  [PASS] divergent tail + Backspace hint reach the web JSON (B5)\n";
}

//---------------------------------------------------------------------------
// v1.3.0 FIX: the bridge's JSON reader only understood \n, \t and \r and
// dropped the backslash for everything else, so \uXXXX escapes arrived as
// literal "u0103" text (Python's json.dumps escapes non-ASCII by default) and
// an escaped quote terminated the string early. Both are user-visible: the
// Flexing preload typed mojibake and typing games received the wrong
// characters.
void testJsonStringEscapes() {
    ArcadeServer server;
    (void)server.handleRequest("POST", "/api/start", "{\"slug\":\"flexing\"}");

    // "v\u0103n b\u1ea3n" == "văn bản" == 7 characters.
    auto escaped = server.handleRequest(
        "POST", "/api/preload",
        "{\"text\":\"v\\u0103n b\\u1ea3n\",\"granularity\":0}");
    assert(escaped.status == 200);
    assert(contains(bodyOf(escaped), "\"total\":7"));

    // Raw UTF-8 must give the same count (and keep working).
    auto raw = server.handleRequest(
        "POST", "/api/preload",
        "{\"text\":\"v\xc4\x83n b\xe1\xba\xa3n\",\"granularity\":0}");
    assert(raw.status == 200);
    assert(contains(bodyOf(raw), "\"total\":7"));

    // A surrogate pair is one character: U+1F5FF (the Flexing emoji).
    auto pair = server.handleRequest(
        "POST", "/api/preload",
        "{\"text\":\"\\ud83d\\udfff\",\"granularity\":0}");
    assert(pair.status == 200);
    assert(contains(bodyOf(pair), "\"total\":1"));

    // Escaped quote / backslash / newline must not break the string or the
    // parse: 3 characters here (a, ", b) and the newline form keeps the
    // 1-character count of the second call.
    auto quote = server.handleRequest(
        "POST", "/api/preload",
        "{\"text\":\"a\\\"b\",\"granularity\":0}");
    assert(quote.status == 200);
    assert(contains(bodyOf(quote), "\"total\":3"));

    auto slash = server.handleRequest(
        "POST", "/api/preload",
        "{\"text\":\"a\\\\b\",\"granularity\":0}");
    assert(slash.status == 200);
    assert(contains(bodyOf(slash), "\"total\":3"));

    // A string with no closing quote is rejected instead of being accepted
    // with a half-parsed value (the decoder refuses to guess).
    auto broken = server.handleRequest(
        "POST", "/api/preload", "{\"text\":\"unterminated");
    assert(broken.status == 400);
    assert(contains(bodyOf(broken), "\"ok\":false"));

    (void)server.handleRequest("POST", "/api/stop", "");
    std::cout << "  [PASS] JSON string escapes (\\uXXXX, quotes, surrogates)\n";
}

int main() {
    std::cout << "=== Running Arcade Server Suite ===\n";
    testRouting();
    testSessionControl();
    testInputForwarding();
    testStaticServingAndTraversal();
    testFullSimulatedRun();
    testSessionIsolation();
    testFlexingPayload();
    testChaosLab();
    testProgressionAndRivalRoutes();
    testConfigApplyNow();
    testWasdSteeringConfig();
    testDivergentTailReachesWebJson();
    testJsonStringEscapes();
    std::cout << "=== ALL ARCADE SERVER TESTS PASSED ===\n";
    return 0;
}
