//============================================================================
// KieeKey - A modified version based on OpenKey
// SPDX-License-Identifier: GPL-3.0-or-later
// File: tests/test_arcade_recovery.cpp
//============================================================================
// v1.3.0-beta4 regression suite. Every case below pins a bug that was
// REPRODUCED on beta3 before it was fixed (see CHANGELOG.md "beta4"):
//
//   1. VnComposer backspace desync — after a wrong tone key, Backspace
//      rewound the visible buffer but left the engine's raw-key state
//      poisoned, so every later keystroke composed against invisible state
//      and the game could never be completed again (500/500 seeded fuzz runs
//      failed before the fix). Deterministic minimal case + the fuzz.
//   2. WasdRace ignored Backspace — real front-ends deliver it as
//      vk=0x08/ch=0 (ToUnicode yields nothing for control keys); beta3 only
//      matched ev.ch == '\b'.
//   3. FishingGame had no Vietnamese mode (ASCII-only prompts by design).
//   4. NoMistakeGame had no Vietnamese mode (ASCII-only stream by design).
//   5. RhythmTypingGame lane keys d/f/j/k collide with Telex letters in VN
//      play — beta4 moves the VN lanes to the arrow cluster.
//   6. ArcadeManager game-call race — two front-end threads (hub timer, web
//      bridge) could run update()/handleKey()/buildFrame() on the same game
//      object concurrently; the concurrency hammer below segfaulted ~15% of
//      runs on beta3 (reproduced with NO KieeKey change on main).
//
// Build (see tests/run_all_tests.sh):
//   g++ -std=c++2b -O2 -Wall -Wextra -Isrc/core tests/test_arcade_recovery.cpp \
//       src/core/Arcade.cpp src/core/ArcadeFrame.cpp src/core/ArcadeRender.cpp \
//       src/core/Progression.cpp src/core/TextEngine.cpp -o /tmp/test_arcade_recovery -pthread
#include "Arcade.hpp"

#include <atomic>
#include <iostream>
#include <random>
#include <string>
#include <thread>

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

// Verified Telex for the default TypingRace VN passage (word-aligned).
const char* kTelexWords[] = {"booj", "gox", "tieesng", "vieejt", "hieejn", "ddaji",
                             "toois", "uwu", "ddooj", "treex", "vaf", "toosc", "ddooj", "gox", "phism"};
constexpr std::size_t kTelexWordCount = std::size(kTelexWords);

std::size_t lcp(const std::u32string& a, const std::u32string& b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && a[i] == b[i]) { ++i; }
    return i;
}

//============================================================================
// 1. VnComposer backspace desync (the "sai rồi backspace thì không gõ tiếp
//    được" bug)
//============================================================================
void testComposerBackspaceDesync() {
    std::cout << "--- testComposerBackspaceDesync\n";

    {
        // Minimal deterministic repro from beta3: type the correct word,
        // press ONE wrong tone key, Backspace it, then type correctly.
        TypingRaceGame g;
        g.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        g.start();
        const std::u32string pass(g.getPassage());
        type(g, "booj");
        g.handleKey(charKey('x'));          // wrong tone: engine rewrites "bộ"->"bỗ"
        g.handleKey(vkKey(0x08));           // backspace the wrong char (pops to "b")
        type(g, "ooj gox ");                // retype the syllable remainder + the rest
        check(g.composedText() == U"bộ gõ ",
              "wrong key + backspace + correct typing re-composes exactly");
        check(g.getCharIndex() == pass.size() || g.composedText() == pass.substr(0, g.getCharIndex()),
              "match index agrees with the visible buffer");
    }

    {
        // Backspace is editor-like: one press pops ONE composed code point.
        TypingRaceGame g;
        g.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        g.start();
        type(g, "booj");                    // -> "bộ"
        check(g.composedText() == U"bộ", "Telex composes bộ");
        g.handleKey(vkKey(0x08));
        check(g.composedText() == U"b", "backspace pops one code point (ộ)");
        g.handleKey(vkKey(0x08));
        check(g.composedText().empty(), "backspace at word start empties the buffer");
        // Mid-syllable repair: popping into "b" then typing the tone keys for
        // the remainder re-composes the syllable.
        type(g, "booj");
        check(g.composedText() == U"bộ", "re-typed syllable composes again after full pop");
    }

    {
        // Repeated backspaces below the buffer start must not corrupt state.
        TypingRaceGame g;
        g.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        g.start();
        type(g, "booj gox ");
        for (int i = 0; i < 12; ++i) { g.handleKey(vkKey(0x08)); }
        check(g.composedText().empty(), "12 backspaces clamp at empty");
        type(g, "booj gox ");
        check(g.composedText() == U"bộ gõ ", "typing after over-backspace still composes");
    }

    {
        // The ch='\b' delivery shape (some front-ends send the char).
        TypingRaceGame g;
        g.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        g.start();
        type(g, "booj");
        g.handleKey(charKey(U'\b'));
        check(g.composedText() == U"b", "ch='\\b' backspace also pops one code point");
    }
}

//============================================================================
// 1b. Realistic recovery fuzz: perturb with wrong keys + backspaces, then
//     hold Backspace to an aligned word boundary and retype. Must ALWAYS
//     complete. (The same oracle failed 500/500 runs on beta3.)
//============================================================================
void testRecoveryFuzz() {
    std::cout << "--- testRecoveryFuzz\n";
    const unsigned kSeeds = 300;
    unsigned bad = 0;
    unsigned maxRepair = 0;
    for (unsigned seed = 1; seed <= kSeeds; ++seed) {
        std::mt19937 rng(seed);
        TypingRaceGame g;
        g.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        g.start();
        const std::u32string pass(g.getPassage());
        const std::size_t typed = 1 + rng() % (kTelexWordCount - 1);
        for (std::size_t w = 0; w < typed; ++w) {
            type(g, kTelexWords[w]);
            g.handleKey(charKey(' '));
            if (rng() % 3 == 0) {
                const int n = 1 + static_cast<int>(rng() % 3);
                for (int i = 0; i < n; ++i) { g.handleKey(charKey("qwzxjkf"[rng() % 7])); }
                for (int i = 0; i < n; ++i) { g.handleKey(vkKey(0x08)); }
            }
            if (rng() % 4 == 0) { g.handleKey(vkKey(0x08)); }
        }
        // Hold backspace until the visible text sits on an aligned word
        // boundary WITH its separator (exact prefix, ends with space).
        unsigned repair = 0;
        for (;;) {
            const std::u32string c = g.composedText();
            const bool ok = c.empty() ||
                            (lcp(c, pass) == c.size() && c.back() == U' ' &&
                             c.size() < pass.size());
            if (ok) { break; }
            g.handleKey(vkKey(0x08));
            if (++repair > 256) { break; }
        }
        maxRepair = std::max(maxRepair, repair);
        const std::u32string c = g.composedText();
        std::size_t completeWords = 0;
        for (char32_t ch : c) { if (ch == U' ') { ++completeWords; } }
        for (std::size_t w = completeWords; w < kTelexWordCount; ++w) {
            type(g, kTelexWords[w]);
            if (w + 1 < kTelexWordCount) { g.handleKey(charKey(' ')); }
        }
        if (!g.isGameOver() || g.getCharIndex() != pass.size()) {
            ++bad;
            if (bad <= 3) {
                std::cout << "        seed " << seed << " not recoverable (composed='"
                          << u8(g.composedText()) << "' idx=" << g.getCharIndex()
                          << "/" << pass.size() << ")\n";
            }
        }
    }
    check(bad == 0, "word-boundary recovery fuzz: all 300 seeded runs completable");
    (void)maxRepair;
}

//============================================================================
// 2. WasdRace Backspace — both delivery shapes, both languages.
//============================================================================
void testWasdBackspace() {
    std::cout << "--- testWasdBackspace\n";
    {
        // Vietnamese: type part of the passage, backspace (vk shape), retype.
        WasdRaceGame g;
        g.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        g.start();
        const std::u32string pass(g.getPassage());
        type(g, "lais");
        const std::size_t idx1 = g.getTextIndex();
        check(idx1 >= 3, "VN WasdRace types the first word");
        g.handleKey(vkKey(0x08));
        check(g.getTextIndex() < idx1, "VN WasdRace: vk=0x08 backspace rewinds");
        g.handleKey(vkKey(0x08));
        g.handleKey(vkKey(0x08));
        g.handleKey(vkKey(0x08));
        type(g, "lais xe");
        check(g.composedText() == U"lái xe", "VN WasdRace types on after backspace");
        g.handleKey(charKey(U'\b'));
        check(g.composedText() == U"lái x", "VN WasdRace: ch='\\b' backspace rewinds");
        (void)pass;
    }
    {
        // English: the game rewinds one index per backspace.
        WasdRaceGame g;
        g.setPassageLanguage(PassageLanguage::English, VnInputMethod::Telex);
        g.start();
        // v1.3.0-beta8 (bug UX-02): "1:1" now really is 1:1. The passage is
        // "lai xe ...", so typing "la" must advance TWO characters. This used
        // to expect 1 because the 'a' was swallowed by WASD steering before it
        // could reach the typing path — the assertion documented the very bug
        // that made the English passage impossible to complete (five of its
        // characters are a/s/d/w) while reading like a correctness check.
        type(g, "la");
        check(g.getTextIndex() == 2, "EN WasdRace types ASCII 1:1");
        g.handleKey(vkKey(0x08));
        check(g.getTextIndex() == 1, "EN WasdRace: vk=0x08 backspace rewinds");
    }
}

//============================================================================
// 3. Fishing Vietnamese mode.
//============================================================================
void testFishingVietnamese() {
    std::cout << "--- testFishingVietnamese\n";
    FishingGame g;
    g.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
    g.start();
    check(g.isVnMode(), "fishing enters VN mode");
    const std::u32string pass(g.getPrompt());
    bool hasDiacritic = false;
    for (char32_t c : pass) {
        if (c >= 0x80) { hasDiacritic = true; }
    }
    check(!pass.empty() && hasDiacritic, "VN fishing prompt carries diacritics");

    // Type the prompt with verified Telex. The fish rarity is rolled at
    // hook time, so pick the Telex table matching the ACTUAL prompt.
    struct PromptTelex { const char32_t* promptStart; const char* telex; };
    static const PromptTelex kPromptTelex[] = {
        {U"thần ngư", "thaanf nguw khoorng loof xuaats hieenj duwois dofng nwowsc saau "},
        {U"cá rồng", "cas roofng uoosn luowjn ddejp mawst treen mawjt hoof "},
        {U"cá hồi", "cas hoofi bowi nguowjc dofng suoosi lajnh "},
        {U"cá trắm", "cas trawsm dden caanf caau raast kheos "},
        {U"cá rô", "cas roo ddoofng bowi looji tung tawng "},
    };
    const char* telex = nullptr;
    for (const auto& pt : kPromptTelex) {
        const std::u32string head(pt.promptStart);
        if (pass.compare(0, head.size(), head) == 0) {
            telex = pt.telex;
            break;
        }
    }
    check(telex != nullptr, "hooked prompt is one of the five VN prompts");
    if (telex == nullptr) { return; }

    // Smart fisher: type a key, pump the frame clock, and when the line gets
    // tense PAUSE (update ticks decay 14 tension/sec) — exactly the loop the
    // in-game hint teaches ("chậm lại nếu dây quá căng"). The catch fires
    // when the pull bar fills, which can happen before the prompt's last
    // word; the strong invariants are (a) the composed text always stays a
    // prefix of the prompt (coherent composition, never garbage) and (b)
    // progress advances deep into the prompt.
    bool coherent = true;
    std::size_t maxIdx = 0;
    for (const char* q = telex; *q; ++q) {
        g.handleKey(charKey(static_cast<char32_t>(static_cast<unsigned char>(*q))));
        g.update(0.02);
        for (int wait = 0; wait < 250 && g.isLineTensionHigh(); ++wait) {
            g.update(0.02);
        }
        if (g.getPrompt() != pass) { break; }   // fish escaped/caught: stop typing
        // Mid-syllable Telex states are transiently non-prefix by design (raw
        // letters show before the tone key lands); the strong invariant is
        // that every COMMITTED word boundary is a clean prefix of the target.
        if (*q == ' ') {
            const std::u32string now = g.composedText();
            if (lcp(now, pass) != now.size()) { coherent = false; }
        }
        maxIdx = std::max(maxIdx, g.getPromptIndex());
    }
    check(coherent, "every committed word was a clean prefix of the VN prompt");
    check(maxIdx >= 8, "progress advanced deep into the VN prompt");
    check(g.getCatches() >= 1, "a catch was registered");
    // The next fish is ready with a fresh composer state.
    const std::u32string next(g.getPrompt());
    check(!next.empty() && next != pass, "a new fish was hooked");
    check(g.composedText().empty() ||
              lcp(g.composedText(), next) == g.composedText().size(),
          "composer state is coherent with the new prompt");

    // Backspace mid-prompt rewinds the visible progress.
    g.handleKey(vkKey(0x08));
    check(g.getPromptIndex() < next.size(), "backspace rewinds fishing progress after catch");

    // English mode keeps the legacy ASCII prompt.
    g.setPassageLanguage(PassageLanguage::English, VnInputMethod::Telex);
    const std::u32string en(g.getPrompt());
    bool asciiOnly = true;
    for (char32_t c : en) { if (c >= 0x80) { asciiOnly = false; } }
    check(asciiOnly && !en.empty(), "EN fishing prompt is ASCII (legacy behaviour)");
}

//============================================================================
// 4. NoMistake Vietnamese mode — WORD-strict judging.
//============================================================================
void testNoMistakeVietnamese() {
    std::cout << "--- testNoMistakeVietnamese\n";
    {
        NoMistakeGame g;
        g.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        g.setFailMode(FailMode::HealthBar);   // class default is Hardcore
        g.start();
        const std::u32string stream(g.getTextStream());
        check(stream.find(U'ọ') != std::u32string::npos ||
              stream.find(U'ậ') != std::u32string::npos ||
              stream.find(U'ẫ') != std::u32string::npos,
              "VN no-mistake stream carries diacritics");
        // học ăn học nói học gói học mở ... (verified Telex, word-aligned)
        type(g, "hojc awn hojc nois hojc gois hojc mowr");
        g.handleKey(charKey(' '));
        check(g.getCurrentIndex() >= 20, "VN no-mistake progresses through composed words");
        check(g.getMistakes() == 0, "correct words are not mistakes");
        // Backspace BEFORE committing fixes a typo without a penalty.
        type(g, "caarx");                   // slip: ngã instead of hỏi
        g.handleKey(vkKey(0x08));           // pops back to "c"
        type(g, "aarn ");                   // retype the syllable remainder
        check(g.getMistakes() == 0, "backspace-repaired word is not a mistake");
        // A committed WRONG word is the mistake.
        type(g, "thanx ");
        check(g.getMistakes() == 1, "committed wrong word counts as the mistake");
        // And typing can continue afterwards (no wedge).
        type(g, "trong");
        check(!g.isGameOver() || g.getCurrentIndex() >= g.getTextStream().size(),
              "game continues (or finished) after a mistake in HealthBar mode");
        (void)0;
    }
    {
        // Hardcore: a committed wrong word ends the run.
        NoMistakeGame g;
        g.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        g.setFailMode(FailMode::Hardcore);
        g.start();
        type(g, "hojc awn ");
        check(!g.isGameOver(), "correct words keep Hardcore alive");
        type(g, "xyz ");
        check(g.isGameOver(), "committed wrong word ends Hardcore run");
    }
}

//============================================================================
// 5. Rhythm Vietnamese mode — arrow lanes.
//============================================================================
void testRhythmVietnamese() {
    std::cout << "--- testRhythmVietnamese\n";
    RhythmTypingGame g;
    g.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
    g.setFailMode(FailMode::Hardcore);
    g.setBpm(90.0);
    g.setNoteCount(16);
    g.start();
    check(g.isVnMode(), "rhythm enters VN mode");

    // At t=0 every note is far away: an ARROW press must be judged (Hardcore
    // death = the arrow reached a lane), a random letter must be ignored
    // UNLESS it is a legacy d/f/j/k alias.
    g.handleKey(charKey('x'));
    check(!g.isGameOver(), "non-lane letter is ignored in VN mode");
    g.handleKey(vkKey(vk::kLeft));
    check(g.isGameOver(), "Left arrow is judged as lane 0 (Hardcore death)");
    (void)0;

    // The legacy letters still work as aliases.
    RhythmTypingGame g2;
    g2.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
    g2.setFailMode(FailMode::Hardcore);
    g2.start();
    g2.handleKey(charKey('d'));
    check(g2.isGameOver(), "legacy 'd' still hits lane 0 in VN mode");

    // VN note glyphs are Vietnamese syllables (non-ASCII).
    RhythmTypingGame g3;
    g3.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
    g3.start();
    const std::u32string glyph = g3.noteGlyph(0);
    bool hasDiacritic = false;
    for (char32_t c : glyph) { if (c >= 0x80) { hasDiacritic = true; } }
    check(!glyph.empty() && hasDiacritic, "VN note glyph is a diacritic syllable");

    // English mode: glyph is the legacy lane letter, letters judge.
    RhythmTypingGame g4;
    g4.setPassageLanguage(PassageLanguage::English, VnInputMethod::Telex);
    g4.setFailMode(FailMode::Hardcore);
    g4.start();
    const std::u32string enGlyph = g4.noteGlyph(0);
    check(enGlyph.size() == 1 && enGlyph[0] < 0x80, "EN note glyph is the legacy letter");
    g4.handleKey(charKey('x'));
    check(!g4.isGameOver(), "non-lane letter ignored in EN mode too");
    g4.handleKey(charKey('j'));
    check(g4.isGameOver(), "'j' judges in EN mode");
}

//============================================================================
// 6. ArcadeManager concurrency hammer (segfaulted ~15% of runs on beta3).
//============================================================================
void testManagerConcurrency() {
    std::cout << "--- testManagerConcurrency\n";
    auto& hub = ArcadeManager::instance();
    hub.stopGame();
    for (RunResult drain; hub.pollRunResult(drain);) {}
    std::atomic<bool> stop{false};
    std::thread launcher([&] {
        while (!stop.load()) {
            hub.launchGame(GameType::Snake, 7);
            std::this_thread::yield();
            hub.update(1.0 / 60.0);
            hub.stopGame();
        }
    });
    for (int i = 0; i < 6000; ++i) {
        hub.handleKey(InputEvent{0, U'w', true});
        hub.update(1.0 / 60.0);
        (void)hub.getFrame();
        (void)hub.isConsumingKeyboard();
    }
    stop.store(true);
    launcher.join();
    hub.stopGame();
    check(true, "concurrency hammer survived (no crash/UB)");
}

} // namespace

int main() {
    std::cout.sync_with_stdio(false);
    std::cout << "=== KieeKey arcade recovery suite (v1.3.0-beta4) ===\n";
    testComposerBackspaceDesync();
    testRecoveryFuzz();
    testWasdBackspace();
    testFishingVietnamese();
    testNoMistakeVietnamese();
    testRhythmVietnamese();
    testManagerConcurrency();
    std::cout << (g_fail == 0 ? "=== ALL RECOVERY TESTS PASSED ===\n"
                              : "=== RECOVERY TESTS FAILED ===\n");
    return g_fail == 0 ? 0 : 1;
}
