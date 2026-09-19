//============================================================================
// KieeKey - A modified version based on OpenKey
// SPDX-License-Identifier: GPL-3.0-or-later
// File: tests/test_arcade_vn.cpp
//============================================================================
// v1.3.0-beta3 (bug #2): prove the typing games accept Vietnamese. The games run
// inside KieeKey's own window, which the keyboard hook bypasses, so each game
// composes Telex/VNI in-window (VnComposer) and matches the composed text against
// a diacritic-bearing target. These tests drive the REAL game objects with the
// REAL Telex keystrokes and assert progress, completion, scoring and the EN fallback.
//
// Build:
//   g++ -std=c++2b -O2 -Wall -Wextra -Isrc/core tests/test_arcade_vn.cpp \
//       src/core/Arcade.cpp src/core/ArcadeFrame.cpp src/core/ArcadeRender.cpp \
//       src/core/Progression.cpp src/core/TextEngine.cpp -o /tmp/test_arcade_vn -pthread
#include "Arcade.hpp"

#include <iostream>
#include <string>

using namespace ok::arcade;

namespace {

int g_fail = 0;

void check(bool cond, const char* what) {
    if (cond) { std::cout << "  [ok]   " << what << "\n"; }
    else { std::cout << "  [FAIL] " << what << "\n"; ++g_fail; }
}

std::string u8(const std::u32string& s) {
    std::string o;
    for (char32_t c : s) {
        if (c < 0x80) { o.push_back(static_cast<char>(c)); }
        else if (c < 0x800) { o.push_back(static_cast<char>(0xC0 | (c >> 6))); o.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
        else if (c < 0x10000) { o.push_back(static_cast<char>(0xE0 | (c >> 12))); o.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F))); o.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
        else { o.push_back(static_cast<char>(0xF0 | (c >> 18))); o.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F))); o.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F))); o.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
    }
    return o;
}

InputEvent charKey(char32_t ch) { InputEvent ev; ev.vk = 0; ev.ch = ch; ev.down = true; return ev; }
InputEvent vkKey(int vk)        { InputEvent ev; ev.vk = vk; ev.ch = 0; ev.down = true; return ev; }

void type(IArcadeGame& g, const std::string& asciiKeys) {
    for (char c : asciiKeys) { g.handleKey(charKey(static_cast<char32_t>(static_cast<unsigned char>(c)))); }
}

// Verified Telex for the default VN passages (see tests/test_vn_composer.cpp and
// the probe that confirmed each composes to exactly the target string).
const char* kTypingRaceTelex =
    "booj gox tieesng vieejt hieejn ddaji toois uwu ddooj treex vaf toosc ddooj gox phism";
const char* kWasdTelex =
    "lais xe vuowjt chuwowngs ngaij vaajt toosc ddooj cao";

} // namespace

int main() {
    std::cout.sync_with_stdio(false);

    std::cout << "== ArcadeConfig defaults (bug #2: Vietnamese is the default) ==\n";
    {
        ArcadeConfig cfg;
        check(cfg.passageLanguage == PassageLanguage::Vietnamese, "default passageLanguage = Vietnamese");
        check(cfg.vnInputMethod == VnInputMethod::Telex, "default vnInputMethod = Telex");
    }

    std::cout << "== TypingRace: Vietnamese passage + in-window Telex composition ==\n";
    {
        TypingRaceGame game;
        game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        game.start();
        check(game.isVnMode(), "VN mode active");
        const std::u32string pass(game.getPassage());
        check(pass == U"bộ gõ tiếng việt hiện đại tối ưu độ trễ và tốc độ gõ phím",
              "VN target bears diacritics");
        check(u8(pass).find("ộ") != std::string::npos, "target contains precomposed 'ộ'");

        type(game, kTypingRaceTelex);
        check(game.getCharIndex() == pass.size(), "typed Telex composed the WHOLE passage");
        check(game.isGameOver(), "game finishes on completion");
        check(game.getAccuracy() > 99.0, "flawless VN play scores 100% accuracy");
        check(game.composedText() == pass, "composedText() equals the target");
        check(game.getLiveWpm() >= 0.0, "WPM is computed (non-negative)");
    }

    std::cout << "== TypingRace: a wrong tone does not complete the passage ==\n";
    {
        TypingRaceGame game;
        game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        game.start();
        const std::u32string pass(game.getPassage());
        type(game, "booj gox");              // "bộ gõ" — correct prefix
        const std::size_t good = game.getCharIndex();
        check(good == 5, "composed 'bộ gõ' (5 code points)");
        type(game, " tieesnx");              // "tiến"~ wrong: 'x' (ngã) where 's' (sắc) belongs
        check(!game.isGameOver(), "wrong tone does not finish the game");
        check(game.getCharIndex() < pass.size(), "wrong tone leaves the passage incomplete");
    }

    std::cout << "== TypingRace: English mode keeps 1:1 ASCII matching ==\n";
    {
        TypingRaceGame game;
        game.setPassageLanguage(PassageLanguage::English, VnInputMethod::Telex);
        game.start();
        check(!game.isVnMode(), "EN mode active");
        const std::u32string pass(game.getPassage());
        check(pass == U"KieeKey la bo go tieng Viet hien dai toi uu do tre va toc do go phim",
              "EN target is the legacy ASCII prompt");
        for (std::size_t i = 0; i < 12 && i < pass.size(); ++i) { game.handleKey(charKey(pass[i])); }
        check(game.getCharIndex() == 12, "EN 1:1 matching advances one char per key");
        check(game.composedText().empty(), "EN mode has no composed text");
    }

    std::cout << "== WasdRace: VN mode steers with arrows, letters compose ==\n";
    {
        WasdRaceGame game;
        game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        game.start();
        check(game.isVnMode(), "WasdRace VN mode active");
        check(game.getPlayerLane() == 1, "starts in the center lane");

        // 'a' is a Telex letter in VN mode — it must compose, NOT steer.
        game.handleKey(charKey(U'a'));
        check(game.getPlayerLane() == 1, "letter 'a' does not steer in VN mode");

        // Arrow keys still steer.
        game.handleKey(vkKey(vk::kLeft));
        check(game.getPlayerLane() == 0, "arrow LEFT steers in VN mode");
        game.handleKey(vkKey(vk::kRight));
        check(game.getPlayerLane() == 1, "arrow RIGHT steers back");

        game.reset();
        type(game, "lais");                  // "lái"
        check(game.getTextIndex() == 3, "composed 'lái' -> textIndex 3");
        check(game.composedText() == U"lái", "WasdRace composedText 'lái'");
    }

    std::cout << "== WasdRace: composing the VN passage scores + loops ==\n";
    {
        WasdRaceGame game;
        game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        game.start();
        const std::int64_t score0 = game.getScore();
        const double fuel0 = game.getFuel();
        type(game, kWasdTelex);
        check(game.getScore() > score0, "score increased while composing the VN passage");
        check(game.getFuel() >= fuel0, "fuel did not drop from correct composition");
        // The passage loops on completion, so textIndex wraps back below the length.
        check(game.getTextIndex() < game.getPassage().size(), "passage loops (textIndex wrapped)");
    }

    std::cout << "== WasdRace: English mode keeps WASD letter steering ==\n";
    {
        WasdRaceGame game;
        game.setPassageLanguage(PassageLanguage::English, VnInputMethod::Telex);
        game.start();
        check(!game.isVnMode(), "EN mode");
        check(game.getPlayerLane() == 1, "center lane");
        game.handleKey(charKey(U'a'));       // 'a' steers left in EN mode
        check(game.getPlayerLane() == 0, "letter 'a' steers in EN mode (legacy behavior)");
    }

    if (g_fail == 0) { std::cout << "\nALL ARCADE-VN TESTS PASSED\n"; return 0; }
    std::cout << "\n" << g_fail << " ARCADE-VN TEST(S) FAILED\n";
    return 1;
}
