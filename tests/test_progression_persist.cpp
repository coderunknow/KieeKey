//============================================================================
// KieeKey - A modified version based on OpenKey
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
// File: tests/test_progression_persist.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//---------------------------------------------------------------------------
// FT-01 (v1.3.0-beta8) — progression & AI profile SURVIVE A RESTART.
//
// The engines always could save (and tests/test_progression.cpp pins their
// round-trip): the beta7 defect was that NOTHING under src/app/ ever called
// them, so XP, level and the learned AI profile died with the process. This
// suite covers the two halves of the fix that a Windows-free test can own:
//
//   1. the RULES in src/app/PersistPolicy.hpp — file names, the opt-in gate,
//      and when the 30 s crash-safe sweep is due;
//   2. a REAL end-to-end round trip through the app's own decision sequence
//      (fresh -> save -> restart -> load), including the interrupted-session
//      case the throttle exists for.
//
// The boot/exit CALL SITES in main.cpp are checked by
// scripts/audit_feature_persistence.py (a wiring grep-gate, same idea as
// audit_settings_wiring.py) because main.cpp needs windows.h to compile.
//---------------------------------------------------------------------------
#include "PersistPolicy.hpp"
#include "Progression.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace ok::apppolicy;
using ok::progression::LoadOutcome;
using ok::progression::ProgressionEngine;

namespace {

int g_checks = 0;
#define CHECK(cond) do { ++g_checks; assert(cond); } while (0)

std::string tempPath(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

void testFileNamesAreTheEngineFormats() {
    // .dat, not .json: the engines write their own checksummed text format.
    CHECK(kProgressionFileName == "progression.dat");
    CHECK(kAiProfileFileName == "aiprofile.dat");
    CHECK(joinPath("C:\\Users\\x\\AppData\\Roaming\\KieeKey", kProgressionFileName)
          == "C:\\Users\\x\\AppData\\Roaming\\KieeKey\\progression.dat");
    CHECK(joinPath("C:\\dir", kAiProfileFileName) == "C:\\dir\\aiprofile.dat");
    // A non-ASCII user name survives the UTF-8 conversion the app performs.
    CHECK(joinPath("C:\\Users\\Nguyễn\\AppData\\Roaming\\KieeKey", kAiProfileFileName)
          == "C:\\Users\\Nguyễn\\AppData\\Roaming\\KieeKey\\aiprofile.dat");
    std::cout << "  [PASS] FT-01: file names are progression.dat / aiprofile.dat\n";
}

void testAiProfileIsOnlyPersistedWhileOptedIn() {
    // Opting out PURGES the profile; writing it afterwards would silently undo
    // the user's decision, so the gate must be exactly the opt-in flag.
    CHECK(!shouldPersistAi(false));
    CHECK(shouldPersistAi(true));
    std::cout << "  [PASS] FT-01: the AI file is written only while opted in\n";
}

void testThrottlePolicy() {
    CHECK(kPersistThrottleMs == 30000);
    // First sweep of a session always runs when something moved.
    CHECK(persistSweepDue(1000, 0, 10, 11, 0, 0, 0, 0));
    // Nothing moved -> no write, however long it has been.
    CHECK(!persistSweepDue(999999, 1000, 10, 10, 5, 5, 7, 7));
    // Moved, but inside the window -> wait.
    CHECK(!persistSweepDue(1000 + kPersistThrottleMs - 1, 1000, 10, 11, 0, 0, 0, 0));
    // Moved and the window elapsed -> write.
    CHECK(persistSweepDue(1000 + kPersistThrottleMs, 1000, 10, 11, 0, 0, 0, 0));
    // Each counter can trigger the sweep on its own.
    CHECK(persistSweepDue(50000, 1000, 10, 10, 5, 6, 7, 7));
    CHECK(persistSweepDue(50000, 1000, 10, 10, 5, 5, 7, 8));
    std::cout << "  [PASS] FT-01: 30 s crash-safe sweep\n";
}

void testRestartRoundTrip() {
    const std::string path = tempPath("kieekey_ft01_progression.dat");
    std::remove(path.c_str());

    // --- session 1: type, gain a level, clean exit -------------------------
    {
        auto& prog = ProgressionEngine::instance();
        prog.reset();
        for (int i = 0; i < 2500; ++i) { prog.recordKeystroke(); }
        prog.flushStats();
        const auto before = prog.getStats();
        CHECK(before.totalKeystrokes == 2500);
        CHECK(before.totalXp > 0);
        CHECK(shouldPersistAi(true));
        CHECK(prog.saveToFile(path));     // the app's exit sweep
    }

    // --- session 2: fresh process instance, load at boot -------------------
    {
        auto& prog = ProgressionEngine::instance();
        prog.reset();
        CHECK(prog.getStats().totalXp == 0);          // "restart"
        LoadOutcome outcome = LoadOutcome::FileMissing;
        CHECK(prog.loadFromFile(path, &outcome));     // the app's boot load
        CHECK(outcome == LoadOutcome::Ok);
        CHECK(prog.getStats().totalKeystrokes == 2500);
        CHECK(prog.getStats().totalXp > 0);
    }

    // --- the interrupted session the throttle exists for -------------------
    {
        auto& prog = ProgressionEngine::instance();
        const std::uint64_t savedXp = prog.getStats().totalXp;
        for (int i = 0; i < 400; ++i) { prog.recordKeystroke(); }
        prog.flushStats();
        // Pretend the machine died BEFORE the 30 s sweep: the file still has
        // the previous snapshot, so at most one window of progress is lost.
        auto& fresh = ProgressionEngine::instance();
        fresh.reset();
        LoadOutcome outcome = LoadOutcome::FileMissing;
        CHECK(fresh.loadFromFile(path, &outcome));
        CHECK(fresh.getStats().totalXp == savedXp);
    }

    std::remove(path.c_str());
    std::cout << "  [PASS] FT-01: save -> restart -> load round trip\n";
}

void testCorruptFileIsFailOpen() {
    const std::string path = tempPath("kieekey_ft01_corrupt.dat");
    {
        std::ofstream ofs(path, std::ios::trunc);
        ofs << "KIEEKEY_PROGRESSION_V1\n!!!not a field!!!\n";
    }
    auto& prog = ProgressionEngine::instance();
    prog.reset();
    LoadOutcome outcome = LoadOutcome::Ok;
    // Fail-open: the app starts usable and the user is warned once.
    CHECK(!prog.loadFromFile(path, &outcome));
    CHECK(outcome == LoadOutcome::Corrupt);
    CHECK(prog.getStats().totalXp == 0);
    CHECK(prog.getStats().currentLevel == 1);
    std::remove(path.c_str());
    std::cout << "  [PASS] FT-01: a corrupt file never blocks the boot\n";
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout << "=== Running FT-01 Persistence Suite ===\n";
    testFileNamesAreTheEngineFormats();
    testAiProfileIsOnlyPersistedWhileOptedIn();
    testThrottlePolicy();
    testRestartRoundTrip();
    testCorruptFileIsFailOpen();
    std::cout << "=== ALL FT-01 PERSISTENCE TESTS PASSED (" << g_checks
              << " checks) ===\n";
    return 0;
}
