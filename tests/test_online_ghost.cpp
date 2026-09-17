//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/test_online_ghost.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "OnlineGhost.hpp"

#include <cassert>
#include <iostream>

using namespace ok::online;

void testLocalGhostProvider() {
    LocalGhostProvider provider;
    provider.clear();

    ReplaySession r1;
    r1.sessionId = "sess_001";
    r1.modeName = "TypingRace";
    r1.playerTag = "RacerOne";
    r1.finalScore = 8500;
    r1.finalWpm = 85.0f;
    r1.frames.push_back({0, 0, 0.0f, 100.0f});
    r1.frames.push_back({500, 5, 80.0f, 100.0f});

    assert(provider.saveReplay(r1));

    auto loaded = provider.loadReplay("sess_001");
    assert(loaded.has_value());
    assert(loaded->playerTag == "RacerOne");
    assert(loaded->frames.size() == 2);

    ReplaySession r2;
    r2.sessionId = "sess_002";
    r2.modeName = "TypingRace";
    r2.playerTag = "RacerTwo";
    r2.finalScore = 12000;
    r2.finalWpm = 110.0f;
    assert(provider.saveReplay(r2));

    auto best = provider.getBestReplay("TypingRace");
    assert(best.has_value());
    assert(best->sessionId == "sess_002");
    assert(best->finalScore == 12000);

    auto list = provider.listReplays("TypingRace");
    assert(list.size() == 2);

    std::cout << "  [PASS] Local Ghost & Replay provider tests\n";
}

void testOfflineLeaderboard() {
    OfflineLeaderboardProvider lb;
    lb.clear();
    assert(!lb.isOnline());

    lb.submitScore({"TypingRace", "Alice", 9000, 90.0f, 98.0f, 1000, "sig1"});
    lb.submitScore({"TypingRace", "Bob", 11000, 110.0f, 99.0f, 1001, "sig2"});
    lb.submitScore({"TypingRace", "Charlie", 7000, 70.0f, 95.0f, 1002, "sig3"});

    auto top2 = lb.getLeaderboard("TypingRace", 2);
    assert(top2.size() == 2);
    assert(top2[0].playerTag == "Bob");
    assert(top2[1].playerTag == "Alice");

    std::cout << "  [PASS] Offline Leaderboard abstraction tests\n";
}

int main() {
    std::cout << "=== Running Online / Ghost Abstraction Suite ===\n";
    testLocalGhostProvider();
    testOfflineLeaderboard();
    std::cout << "=== ALL ONLINE/GHOST TESTS PASSED ===\n";
    return 0;
}
