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
// File: tests/test_arcade_beta8_ux.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// v1.3.0-beta8 — UI/UX audit regression suite.
//
// Every case here reproduces a defect that was found by PLAYING the product
// (driving the real front-end event sequences and the real key streams), not
// by reading the code. Each one was failing before the fix in the same commit.
//
//   UX-01  the web config bridge ignored `applyNow` on the browser's actual
//          input-then-change event sequence, so the BPM slider and the passage
//          language select never affected the running game;
//   UX-02  WASD Race in English swallowed a/s/d/w as steering, making its own
//          passage impossible to type and the game uncompletable;
//   UX-03  the WASD Race game-over banner told the player to press R, a key
//          that game deliberately does not accept;
//   UX-04  a freshly launched No-Mistake run displayed 0 % accuracy;
//   UX-05  the No-Mistake hint shipped the non-word "tững" for "từng";
//   UX-06  opening the web hub POSTed the static HTML defaults and silently
//          reset every arcade setting configured on the desktop;
//   UX-07  /api/config answered ok:true for values it had rejected or keys it
//          did not understand.
//
// These are USER-VISIBLE contracts: "typing the passage advances the passage",
// "the control I released changed the game", "the HUD does not lie". They are
// deliberately written against observable behaviour, not internal fields.
//----------------------------------------------------------------------------
#include "Arcade.hpp"
#include "ArcadeServer.hpp"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace ok::arcade;

namespace {

int g_checks = 0;

void check(bool condition, const char* what) {
    ++g_checks;
    if (!condition) {
        std::cerr << "  [FAIL] " << what << "\n";
        std::abort();
    }
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool contains(std::u32string_view haystack, std::u32string_view needle) {
    return haystack.find(needle) != std::u32string_view::npos;
}

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

//---------------------------------------------------------------------------
// UX-02 — WASD Race must be playable in BOTH languages.
//
// The English passage is "lai xe vuot chuong ngai vat toc do cao": it contains
// five a/s/d/w characters. beta7 consumed those as steering and returned
// before the typing path, so a player typing the passage perfectly stalled on
// the 'a' of "lai" and the run could never progress or refuel. Steering is a
// side effect of a letter; it must never eat the keystroke.
//---------------------------------------------------------------------------
void testWasdRaceEnglishIsCompletable() {
    std::cout << "== UX-02: WASD Race passage is typable in every steering mode ==\n";
    for (int mode = 0; mode <= 2; ++mode) {
        for (int lang = 0; lang <= 1; ++lang) {
            ArcadeManager manager;
            ArcadeConfig config = manager.getConfig();
            config.passageLanguage = (lang == 1) ? PassageLanguage::English
                                                 : PassageLanguage::Vietnamese;
            config.wasdSteering = static_cast<WasdSteering>(mode);
            manager.setConfig(config);
            assert(manager.launchGame(GameType::WasdRace));
            auto* game = dynamic_cast<WasdRaceGame*>(manager.getCurrentGame());
            assert(game != nullptr);

            const std::u32string passage(game->getPassage());
            assert(!passage.empty());
            // Type the passage exactly as displayed. Every character must make
            // progress; the index wraps to 0 on the final one (the road game
            // loops its passage on purpose).
            std::size_t stalls = 0;
            for (std::size_t i = 0; i < passage.size(); ++i) {
                const std::size_t before = game->getTextIndex();
                manager.handleKey(0, passage[i], true);
                if (game->getTextIndex() == before) {
                    ++stalls;
                }
            }
            check(stalls == 0,
                  "every character of the displayed passage advances the run");
            check(game->getFuel() > 0.0, "typing the passage keeps the tank alive");
        }
    }

    // Steering itself must still work, and arrows stay steering-only.
    {
        ArcadeManager manager;
        ArcadeConfig config = manager.getConfig();
        config.passageLanguage = PassageLanguage::English;
        config.wasdSteering = WasdSteering::Arrows;
        manager.setConfig(config);
        assert(manager.launchGame(GameType::WasdRace));
        auto* game = dynamic_cast<WasdRaceGame*>(manager.getCurrentGame());
        const int lane = game->getPlayerLane();
        manager.handleKey(0x25, 0, true);   // VK_LEFT
        check(game->getPlayerLane() == lane - 1, "EN: arrow keys steer");
        const int lane2 = game->getPlayerLane();
        manager.handleKey(0, U'd', true);
        check(game->getPlayerLane() == lane2 + 1, "EN: 'd' still steers");
    }

    // The beta5 Vietnamese contract is untouched: in Arrows mode the letters
    // are pure Telex input, and in WASD mode they steer AND compose.
    {
        ArcadeManager manager;
        ArcadeConfig config = manager.getConfig();
        config.passageLanguage = PassageLanguage::Vietnamese;
        config.wasdSteering = WasdSteering::Arrows;
        manager.setConfig(config);
        assert(manager.launchGame(GameType::WasdRace));
        auto* game = dynamic_cast<WasdRaceGame*>(manager.getCurrentGame());
        const int lane = game->getPlayerLane();
        manager.handleKey(0, U'd', true);
        check(game->getPlayerLane() == lane, "VN/Arrows: 'd' does not steer");
        check(!game->composedText().empty(), "VN/Arrows: 'd' reaches the composer");
    }
    {
        ArcadeManager manager;
        ArcadeConfig config = manager.getConfig();
        config.passageLanguage = PassageLanguage::Vietnamese;
        config.wasdSteering = WasdSteering::Wasd;
        manager.setConfig(config);
        assert(manager.launchGame(GameType::WasdRace));
        auto* game = dynamic_cast<WasdRaceGame*>(manager.getCurrentGame());
        const int lane = game->getPlayerLane();
        manager.handleKey(0, U'd', true);
        check(game->getPlayerLane() == lane + 1, "VN/Wasd: 'd' steers");
        check(!game->composedText().empty(), "VN/Wasd: 'd' also composes");
    }
    std::cout << "  [PASS] WASD Race is completable and steering still works\n";
}

//---------------------------------------------------------------------------
// UX-01 — the config bridge on the REAL browser event sequence.
//
// web/arcade.js binds pushConfig() to `input` and pushConfig({applyNow:true})
// to `change`. A range slider fires `input` for every intermediate value and
// then one `change` with the SAME final value; a <select> fires both too. The
// old bridge compared the request against the STORED config, so the `change`
// call saw "no difference" and did nothing at all.
//---------------------------------------------------------------------------
void testWebSliderEventSequenceApplies() {
    std::cout << "== UX-01: browser input-then-change actually applies ==\n";
    {
        ArcadeServer server(ArcadeServerConfig{});
        auto started = server.handleRequest("POST", "/api/start", "{\"slug\":\"rhythm\"}");
        assert(started.status == 200);
        const double initial =
            dynamic_cast<RhythmTypingGame*>(server.manager().getCurrentGame())->getBpm();

        // Dragging the slider: four `input` events, no applyNow.
        for (const char* value : {"120", "140", "160", "180"}) {
            auto drag = server.handleRequest(
                "POST", "/api/config", std::string("{\"rhythmBpm\":") + value + "}");
            assert(drag.status == 200);
        }
        // Releasing it: one `change` carrying the same final value.
        auto release = server.handleRequest("POST", "/api/config",
                                            "{\"rhythmBpm\":180,\"applyNow\":1}");
        check(contains(release.body, "\"restartApplied\":true"),
              "the release event reports that it rebuilt the run");
        const double now =
            dynamic_cast<RhythmTypingGame*>(server.manager().getCurrentGame())->getBpm();
        check(now == 180.0 && initial != 180.0,
              "the RUNNING game really plays the tempo the slider shows");
    }
    {
        // The passage-language <select>: same two-event shape.
        ArcadeServer server(ArcadeServerConfig{});
        assert(server.handleRequest("POST", "/api/start", "{\"slug\":\"typing-race\"}").status == 200);
        (void)server.handleRequest("POST", "/api/config", "{\"passageLanguage\":1}");
        auto release = server.handleRequest("POST", "/api/config",
                                            "{\"passageLanguage\":1,\"applyNow\":1}");
        check(contains(release.body, "\"restartApplied\":true"),
              "changing the passage language rebuilds the run");
        check(server.manager().getConfig().passageLanguage == PassageLanguage::English,
              "and the stored config agrees");
    }
    {
        // Honesty in the other direction: once the run IS up to date, the
        // bridge must not claim a restart it did not perform.
        ArcadeServer server(ArcadeServerConfig{});
        assert(server.handleRequest("POST", "/api/start", "{\"slug\":\"rhythm\"}").status == 200);
        auto first = server.handleRequest("POST", "/api/config",
                                          "{\"rhythmBpm\":170,\"applyNow\":1}");
        check(contains(first.body, "\"restartApplied\":true"), "a real change applies");
        auto again = server.handleRequest("POST", "/api/config",
                                          "{\"rhythmBpm\":170,\"applyNow\":1}");
        check(contains(again.body, "\"restartApplied\":false"),
              "an already-applied value is not a fake restart");
        check(contains(again.body, "\"restartRequired\":false"),
              "and it is not reported as pending either");
    }
    {
        // A live-only knob never asks for a rebuild.
        ArcadeServer server(ArcadeServerConfig{});
        assert(server.handleRequest("POST", "/api/start", "{\"slug\":\"rhythm\"}").status == 200);
        auto live = server.handleRequest("POST", "/api/config", "{\"rhythmFailMode\":1}");
        check(contains(live.body, "\"restartRequired\":false"),
              "fail mode is applied live");
    }
    std::cout << "  [PASS] applyNow survives the real browser event order\n";
}

//---------------------------------------------------------------------------
// UX-06 — opening the web hub must not overwrite the desktop configuration.
//---------------------------------------------------------------------------
void testWebBootDoesNotClobberDesktopConfig() {
    std::cout << "== UX-06: the web hub reads the config instead of resetting it ==\n";
    ArcadeServer server(ArcadeServerConfig{});

    // What the user set on the desktop.
    ArcadeConfig desktop = server.manager().getConfig();
    desktop.rhythmBpm = 200;
    desktop.passageLanguage = PassageLanguage::English;
    desktop.rhythmFailMode = FailMode::HealthBar;
    desktop.typingRacePacerWpm = 140;
    desktop.wasdSteering = WasdSteering::Both;
    server.manager().setConfig(desktop);

    // The config must be READABLE — without a GET route the page had nothing
    // to hydrate from, which is why it used to push its HTML defaults.
    auto read = server.handleRequest("GET", "/api/config", "");
    check(read.status == 200, "GET /api/config is served");
    check(contains(read.body, "\"rhythmBpm\":200"), "it reports the live BPM");
    check(contains(read.body, "\"passageLanguage\":1"), "and the live language");
    check(contains(read.body, "\"typingRacePacerWpm\":140"), "and the live pacer");
    check(contains(read.body, "\"wasdSteering\":2"), "and the live steering mode");
    check(contains(read.body, "\"rhythmFailMode\":1"), "and the live fail mode");

    // Nothing the page does at boot may change the engine.
    const ArcadeConfig after = server.manager().getConfig();
    check(after.rhythmBpm == 200.0 && after.typingRacePacerWpm == 140.0 &&
              after.passageLanguage == PassageLanguage::English &&
              after.wasdSteering == WasdSteering::Both &&
              after.rhythmFailMode == FailMode::HealthBar,
          "reading the config leaves every setting untouched");

    // And the client really was changed to hydrate rather than push.
    const std::string js = readFile("web/arcade.js");
    if (!js.empty()) {
        check(contains(js, "hydrateConfig"),
              "web/arcade.js boots by hydrating from the server");
        const std::size_t boot = js.find("loadCatalog();");
        check(boot != std::string::npos, "the boot block is present");
        const std::string tail = js.substr(boot);
        check(!contains(tail.substr(0, tail.find("startStream()")), "pushConfig()"),
              "the boot block no longer POSTs the static HTML defaults");
    }
    std::cout << "  [PASS] the desktop configuration survives a web page load\n";
}

//---------------------------------------------------------------------------
// UX-07 — /api/config must not report success for values it discarded.
//---------------------------------------------------------------------------
void testConfigRouteReportsRejectedKeys() {
    std::cout << "== UX-07: rejected values are named, not silently dropped ==\n";
    ArcadeServer server(ArcadeServerConfig{});

    auto tooFast = server.handleRequest("POST", "/api/config", "{\"rhythmBpm\":9999}");
    check(contains(tooFast.body, "\"rejectedKeys\":\"rhythmBpm\""),
          "an out-of-range BPM is reported back");
    check(!contains(tooFast.body, "9999"), "and is not stored");

    // The pacer is now validated against the same range the UI offers (0..200);
    // it used to accept up to 300, so the bridge stored values no control could
    // display — inconsistent with its sibling rhythmBpm.
    auto pacer = server.handleRequest("POST", "/api/config", "{\"typingRacePacerWpm\":250}");
    check(contains(pacer.body, "\"rejectedKeys\":\"typingRacePacerWpm\""),
          "a pacer beyond the slider range is rejected");
    check(!contains(pacer.body, "\"typingRacePacerWpm\":250"),
          "and is not echoed as applied");

    // Keys the desktop supports but this route does not are named too, instead
    // of vanishing behind a flat ok:true.
    for (const char* key : {"fishingAutomation", "vnInputMethod", "wasdObstacleSpacingSec",
                            "rhythmNoteCount", "noMistakeStartReserve"}) {
        const std::string body = std::string("{\"") + key + "\":2}";
        auto resp = server.handleRequest("POST", "/api/config", body);
        check(contains(resp.body, std::string("\"rejectedKeys\":\"") + key + "\""),
              "an unsupported key is reported rather than swallowed");
    }

    // A good request still reports an empty rejection list.
    auto good = server.handleRequest("POST", "/api/config", "{\"rhythmBpm\":150}");
    check(contains(good.body, "\"rejectedKeys\":\"\""),
          "an accepted change reports nothing rejected");
    check(contains(good.body, "\"rhythmBpm\":150"), "and is echoed as applied");
    std::cout << "  [PASS] the config route distinguishes accepted from discarded\n";
}

//---------------------------------------------------------------------------
// UX-03 / UX-04 / UX-05 — the HUD must not lie to the player.
//---------------------------------------------------------------------------
void testHudTellsTheTruth() {
    std::cout << "== UX-03/04/05: HUD text matches what the game actually does ==\n";

    // UX-03: the game-over banner may only name a key that restarts the run.
    {
        ArcadeManager manager;
        assert(manager.launchGame(GameType::WasdRace));
        auto* game = dynamic_cast<WasdRaceGame*>(manager.getCurrentGame());
        game->setStartFuel(1.0);
        game->reset();
        game->start();
        for (int i = 0; i < 2000 && !game->isGameOver(); ++i) {
            manager.update(0.05);
        }
        check(game->isGameOver(), "the race ends when the tank runs dry");
        const std::u32string banner(manager.getFrame().stats.banner);
        check(!banner.empty(), "a game-over banner is shown");

        // Whatever key the banner advertises must really restart the run.
        if (contains(banner, U"nhấn R")) {
            manager.handleKey(0, U'r', true);
            check(!game->isGameOver(), "a banner promising R is backed by R");
        }
        if (contains(banner, U"F2")) {
            manager.handleKey(0x71, 0, true);   // VK_F2
            check(!game->isGameOver(), "a banner promising F2 is backed by F2");
        }
    }

    // Snake and Tetris DO accept R, so their wording stays valid.
    for (GameType type : {GameType::Snake, GameType::Tetris}) {
        ArcadeManager manager;
        assert(manager.launchGame(type));
        auto* game = manager.getCurrentGame();
        for (int i = 0; i < 6000 && !game->isGameOver(); ++i) {
            manager.update(0.05);
        }
        if (game->isGameOver() && contains(manager.getFrame().stats.banner, U"nhấn R")) {
            manager.handleKey(0, U'r', true);
            check(!game->isGameOver(), "Snake/Tetris really do restart on R");
        }
    }

    // UX-04: an untouched run is flawless, not 0 % accurate.
    for (int lang = 0; lang <= 1; ++lang) {
        ArcadeManager manager;
        ArcadeConfig config = manager.getConfig();
        config.passageLanguage = (lang == 1) ? PassageLanguage::English
                                             : PassageLanguage::Vietnamese;
        manager.setConfig(config);
        assert(manager.launchGame(GameType::NoMistake));
        check(manager.getFrame().stats.accuracy == 100.0,
              "a fresh No-Mistake run shows 100 % accuracy");
    }
    // ...and a real mistake still lowers it.
    {
        ArcadeManager manager;
        ArcadeConfig config = manager.getConfig();
        config.passageLanguage = PassageLanguage::English;
        config.noMistakeFailMode = FailMode::HealthBar;
        manager.setConfig(config);
        assert(manager.launchGame(GameType::NoMistake));
        auto* game = dynamic_cast<NoMistakeGame*>(manager.getCurrentGame());
        const std::u32string stream(game->getTextStream());
        manager.handleKey(0, stream[0], true);      // one correct
        manager.handleKey(0, U'\u00D7', true);      // one wrong
        check(manager.getFrame().stats.accuracy < 100.0,
              "a genuine mistake still shows below 100 %");
    }

    // UX-05: no misspelt Vietnamese on a Vietnamese typing trainer's HUD.
    for (GameType type : {GameType::Snake, GameType::Tetris, GameType::Fishing,
                          GameType::TypingRace, GameType::WasdRace, GameType::Rhythm,
                          GameType::NoMistake, GameType::Flexing}) {
        ArcadeManager manager;
        assert(manager.launchGame(type));
        const Frame& frame = manager.getFrame();
        check(!contains(frame.stats.hint, U"tững"), "no 'tững' typo in any hint");
        check(!contains(frame.stats.status, U"tững"), "no 'tững' typo in any status");
        check(!contains(frame.stats.banner, U"tững"), "no 'tững' typo in any banner");
    }
    std::cout << "  [PASS] banners, gauges and hints match real behaviour\n";
}

//---------------------------------------------------------------------------
// UX-08 — the Chaos Lab "characters per key" combo must actually vary N.
//
// The desktop combo passed a hard-coded 3 for every entry while advertising
// "3 ký tự (N=3)", so N was unreachable from the UI. The engine supports
// 1..64 and the web lab has always sent a real value. This checks the engine
// contract the desktop now drives, plus the source-level mapping table.
//---------------------------------------------------------------------------
void testFlexingGranularityIsConfigurable() {
    std::cout << "== UX-08: Flexing N chars-per-key is a real setting ==\n";
    for (std::uint32_t n : {1u, 3u, 5u, 10u, 25u}) {
        FlexingGame game;
        std::u32string text(64, U'x');
        game.setPreloadedText(text);
        game.setGranularity(FlexGranularity::NCharsPerKey, n);
        game.start();
        game.handleKey(InputEvent{0, U'q', true});
        check(game.getCursor() == n, "one key emits exactly N characters");
    }

    // The desktop combo now offers several N values mapped from one table.
    const std::string source = readFile("src/app/ChaosLabWindow.cpp");
    if (!source.empty()) {
        check(contains(source, "flexGranularityChoice"),
              "the lab window resolves N from the shared choice table");
        check(!contains(source, "std::clamp(selection, 0, 3)), 3)"),
              "the hard-coded nChars=3 is gone");
        check(contains(source, "5 ký tự") && contains(source, "10 ký tự"),
              "the combo offers more than one N");
    }
    std::cout << "  [PASS] the granularity combo drives the engine parameter\n";
}

} // namespace

int main() {
    std::cout << "=== Running Arcade beta8 UI/UX Regression Suite ===\n";
    testWasdRaceEnglishIsCompletable();
    testWebSliderEventSequenceApplies();
    testWebBootDoesNotClobberDesktopConfig();
    testConfigRouteReportsRejectedKeys();
    testHudTellsTheTruth();
    testFlexingGranularityIsConfigurable();
    std::cout << "=== ALL BETA8 UI/UX TESTS PASSED (" << g_checks << " checks) ===\n";
    return 0;
}
