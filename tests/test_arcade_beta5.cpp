// test_arcade_beta5.cpp — portable pins for the beta4 tester's arcade clusters.
//
// B5: Typing Race / Fishing / WASD Race / NoMistake — after a wrong Telex word
//     the composed buffer diverged from the target but NOTHING rendered it, so
//     "backspace after a wrong word doesn't work" (it did work; it was just
//     invisible). beta5 renders the composed buffer with the divergent tail in
//     red plus a "Backspace để sửa" hint. These tests drive real games through
//     real key events and inspect the Frame text runs.
// B6: NoMistake — a wrong FINAL word never ended the run (the space-boundary
//     judgment cannot fire without a trailing space) and WPM/accuracy showed 0
//     mid-run. beta5 adds the end-of-run verdict (applyWrongWordVn, once) and
//     live stats; these tests pin both, plus Hardcore-vs-HealthBar behavior and
//     ArcadeManager::setConfig live application.
// B7: WASD Race — VN mode has no WASD steering because a/s/d/w are Telex keys.
//     beta5 adds a player choice (Arrows / WASD / Both); in WASD-involving
//     modes the letters steer AND still feed the composer (never swallowed).
//     Pinned by unit checks + a 1000-seed fuzz over all three modes that
//     compares composed text/index against an independent VnComposer.
//
// All Telex encodings embedded here are composer-verified AT RUNTIME: the
// first section feeds them through a reference VnComposer and fails loudly if
// any encoding does not produce the expected diacritic-bearing text.

#include "Arcade.hpp"
#include "VnComposer.hpp"   // reference composer for the fuzz invariants

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using namespace ok::arcade;

namespace {

int g_fail = 0;
int g_pass = 0;

void check(bool cond, const char* what) {
    if (cond) { ++g_pass; }
    else { ++g_fail; std::cout << "  FAIL: " << what << "\n"; }
}

InputEvent charKey(char32_t ch) { InputEvent ev; ev.vk = 0; ev.ch = ch; ev.down = true; return ev; }
InputEvent vkKey(int vk)        { InputEvent ev; ev.vk = vk; ev.ch = 0; ev.down = true; return ev; }

void type(IArcadeGame& g, const std::string& asciiKeys) {
    for (char c : asciiKeys) {
        g.handleKey(charKey(static_cast<char32_t>(static_cast<unsigned char>(c))));
    }
}

bool contains(std::u32string_view hay, std::u32string_view needle) {
    return hay.find(needle) != std::u32string_view::npos;
}

// Find a text run placed at (approximately) row y with the given color.
const TextShape* findText(const Frame& f, float y, Color color) {
    for (const TextShape& t : f.texts) {
        if (std::fabs(t.y - y) < 0.5f && t.color == color) { return &t; }
    }
    return nullptr;
}

bool anyTextContains(const Frame& f, std::u32string_view needle) {
    for (const TextShape& t : f.texts) {
        if (contains(t.text, needle)) { return true; }
    }
    return false;
}

// ---- verified corpora (beta4 set + beta5 additions) ------------------------
// Default TypingRace VN passage: "bộ gõ tiếng việt hiện đại tối ưu độ trễ và
// tốc độ gõ phím" — the beta4-verified encoding from test_arcade_recovery.cpp.
const char* kTypingTelex[] = {"booj", "gox", "tieesng", "vieejt", "hieejn", "ddaji",
                              "toois", "uwu", "ddooj", "treex", "vaf", "toosc",
                              "ddooj", "gox", "phism"};
constexpr std::u32string_view kTypingVn =
    U"bộ gõ tiếng việt hiện đại tối ưu độ trễ và tốc độ gõ phím";

// NoMistake VN stream (kNoMistakeStreamVn) — encoding verified at runtime by
// this test's first section before it is used to drive the game.
const char* kNoMistakeTelex[] = {"hocj", "awn", "hocj", "nois", "hocj", "gois",
                                 "hocj", "mowr", "caarn", "thaanj", "trong", "tuwfng",
                                 "phism", "bawsn", "kieen", "trif", "beenf", "bir"};
constexpr std::u32string_view kNoMistakeVn =
    U"học ăn học nói học gói học mở cẩn thận trong từng phím bắn kiên trì bền bỉ";

std::u32string joinTelex(const char* const* words, std::size_t n, bool trailingSpace) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += words[i];
        if (i + 1 < n || trailingSpace) { out += ' '; }
    }
    std::u32string u;
    for (char c : out) { u.push_back(static_cast<char32_t>(static_cast<unsigned char>(c))); }
    return u;
}

void feedAll(VnComposer& c, const std::u32string& keys) {
    for (char32_t ch : keys) {
        if (ch == U' ') { c.feedSpace(); } else { c.feedProduced(ch); }
    }
}

//============================================================================
// 0. Composer-verify every Telex encoding embedded in this file (ground rule:
//    never drive a game with an unverified corpus).
//============================================================================
void testCorpusIsComposerVerified() {
    std::cout << "== beta5 corpus: Telex encodings self-verify ==\n";
    {
        VnComposer c; c.setMethod(ok::text::InputMethod::Telex);
        feedAll(c, joinTelex(kTypingTelex, std::size(kTypingTelex), false));
        check(c.text() == std::u32string(kTypingVn), "TypingRace Telex corpus composes to the VN passage");
    }
    {
        VnComposer c; c.setMethod(ok::text::InputMethod::Telex);
        feedAll(c, joinTelex(kNoMistakeTelex, std::size(kNoMistakeTelex), false));
        check(c.text() == std::u32string(kNoMistakeVn),
              "NoMistake Telex corpus composes to 'học ăn ... bền bỉ'");
    }
}

//============================================================================
// B5 — the divergent composed tail is rendered in red with a Backspace hint,
// and disappears once Backspace repairs the composition.
//============================================================================
void testTypingRaceDivergentTail() {
    std::cout << "== B5: TypingRace renders the red divergent tail ==\n";
    TypingRaceGame game;
    game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
    game.start();

    // The passage starts with 'b'; typing 'c' diverges immediately.
    game.handleKey(charKey(U'c'));
    check(game.composedText() == U"c", "wrong char composed");
    check(game.getCharIndex() == 0, "wrong char does not advance");

    Frame f;
    game.buildFrame(f);
    const TextShape* bad = findText(f, 456.0f, palette::kBad);
    check(bad != nullptr && contains(bad->text, U"c"),
          "red divergent tail rendered at the composed row");
    const TextShape* label = findText(f, 456.0f, palette::kTextDim);
    check(label != nullptr && contains(label->text, U"Bạn đã gõ:"),
          "'Bạn đã gõ:' label rendered next to the tail");
    check(contains(f.stats.hint, U"Backspace"), "hint tells the player Backspace fixes it");

    // Backspace repairs: the tail must vanish (the beta4 complaint was that
    // this worked but was invisible).
    game.handleKey(vkKey(0x08));
    check(game.composedText().empty(), "backspace cleared the divergent char");
    Frame f2;
    game.buildFrame(f2);
    check(findText(f2, 456.0f, palette::kBad) == nullptr,
          "no red tail once the composition matches again");

    // A matched prefix diverged by one char renders green prefix + red tail.
    type(game, "boojc");
    check(game.composedText() == U"bộc" && game.getCharIndex() == 2,
          "correct Telex prefix advances, extra char diverges");
    Frame f3;
    game.buildFrame(f3);
    const TextShape* good = findText(f3, 456.0f, palette::kGood);
    check(good != nullptr && contains(good->text, U"bộ"), "matched prefix rendered green");
    const TextShape* bad3 = findText(f3, 456.0f, palette::kBad);
    check(bad3 != nullptr && contains(bad3->text, U"c"), "divergent tail rendered red");
}

void testFishingDivergentTail() {
    std::cout << "== B5: Fishing renders the red divergent tail ==\n";
    FishingGame game;
    game.setSeed(7);
    game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
    game.start();
    for (int i = 0; i < 10; ++i) { game.update(1.0 / 60.0); }

    // Every VN fishing prompt starts with 't' (thần) or 'c' (cá), so "booj"
    // (bộ) diverges no matter which fish is hooked.
    type(game, "booj");
    check(game.composedText() == U"bộ", "fishing composed 'bộ'");

    Frame f;
    game.buildFrame(f);
    const TextShape* bad = findText(f, 544.0f, palette::kBad);
    check(bad != nullptr && !bad->text.empty(), "red divergent tail rendered in Fishing");
    check(contains(f.stats.hint, U"Backspace"), "fishing hint mentions Backspace");

    game.handleKey(vkKey(0x08));
    game.handleKey(vkKey(0x08));
    Frame f2;
    game.buildFrame(f2);
    check(findText(f2, 544.0f, palette::kBad) == nullptr,
          "fishing tail gone after backspace repair");
}

void testWasdRaceDivergentTailAndHints() {
    std::cout << "== B5/B7: WasdRace composed row + steering-mode hints ==\n";
    {
        WasdRaceGame game;   // default steering = Arrows
        game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        game.start();
        check(game.steeringMode() == WasdSteering::Arrows, "default steering is Arrows");
        type(game, "booj");   // passage starts with 'l' → diverges
        Frame f;
        game.buildFrame(f);
        check(findText(f, 550.0f, palette::kBad) != nullptr,
              "red divergent tail rendered in WasdRace");
        check(contains(f.stats.hint, U"Backspace"), "diverged hint overrides with repair instruction");
    }
    {
        WasdRaceGame game;
        game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        game.setSteeringMode(WasdSteering::Wasd);
        game.start();
        Frame f;
        game.buildFrame(f);
        check(contains(f.stats.hint, U"A/S/D/W"), "Wasd-mode VN hint advertises steering letters");
    }
}

void testNoMistakeDivergentTail() {
    std::cout << "== B5: NoMistake renders the red divergent tail ==\n";
    NoMistakeGame game;
    game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
    game.setFailMode(FailMode::HealthBar);
    game.setStartReserve(10000);
    game.start();
    type(game, "cooj");   // stream starts with "học" → 'c' diverges
    Frame f;
    game.update(0.1);
    game.buildFrame(f);
    check(findText(f, 262.0f, palette::kBad) != nullptr,
          "red divergent tail rendered in NoMistake");
    check(contains(f.stats.hint, U"Backspace"), "NoMistake hint mentions Backspace");
}

//============================================================================
// B6 — NoMistake: end-of-run verdict for a wrong FINAL word, one-shot, repair
// still wins, live WPM/accuracy, and manager live-config application.
//============================================================================
void typeNoMistakePrefix(NoMistakeGame& game, std::size_t words) {
    // words 0..words-1 each committed with a trailing space.
    for (std::size_t i = 0; i < words; ++i) {
        type(game, kNoMistakeTelex[i]);
        game.handleKey(charKey(U' '));
    }
}

void testNoMistakeFinalWordVerdict() {
    std::cout << "== B6: NoMistake end-of-run verdict (wrong FINAL word) ==\n";
    const std::size_t kWordCount = std::size(kNoMistakeTelex);
    const std::u32string stream(kNoMistakeVn);

    // Hardcore: a wrong final word ENDS the run (beta4: it sat there forever).
    {
        NoMistakeGame game;
        game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        game.setFailMode(FailMode::Hardcore);
        game.start();
        typeNoMistakePrefix(game, kWordCount - 1);
        check(!game.isGameOver(), "run alive before the final word");
        type(game, "bar");   // final word is "bỉ" (bir); "bar" diverges and the
                             // composed length reaches the stream length
        check(game.isGameOver(), "Hardcore: wrong final word ends the run");
        check(game.getMistakes() == 1, "wrong final word counted exactly once");
        RunResult r{};
        check(game.pollRunResult(r) && !r.completed,
              "result reported as NOT completed (loss, not win)");
    }

    // HealthBar: the same input penalizes but keeps the run alive; a second
    // divergent key must NOT re-judge (m_vnEndJudged is one-shot); repairing
    // with Backspace can still reach the win.
    {
        NoMistakeGame game;
        game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        game.setFailMode(FailMode::HealthBar);
        game.setStartReserve(10000);
        game.start();
        typeNoMistakePrefix(game, kWordCount - 1);
        const std::int64_t scoreBefore = game.getScore();
        type(game, "bar");
        check(!game.isGameOver(), "HealthBar: wrong final word does not end a healthy run");
        check(game.getMistakes() == 1, "HealthBar: mistake counted once");
        check(game.getScore() < scoreBefore, "HealthBar: soft penalty applied");
        type(game, "zzz");   // more divergent keys
        check(game.getMistakes() == 1, "end verdict is one-shot (no double counting)");
        // Repair: backspace down to the committed prefix, then type the real
        // final word — the win condition must still be reachable after a
        // judged end-of-run divergence.
        for (int i = 0; i < 6 && game.composedText().size() > stream.size() - 2; ++i) {
            game.handleKey(vkKey(0x08));
        }
        type(game, "bir");
        check(game.isGameOver(), "repair path finished the run");
        RunResult r{};
        check(game.pollRunResult(r) && r.completed,
              "repaired final word reports a COMPLETED run");
        check(game.getMistakes() == 1, "the earlier mistake stays on record");
    }

    // Tone-repairable tail: at full stream length with only the tone mark
    // missing ("bi" where "bỉ" is expected) the run must WAIT — judging it
    // would end runs for correct typing whose tone key has not arrived yet.
    {
        NoMistakeGame game;
        game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        game.setFailMode(FailMode::Hardcore);
        game.start();
        typeNoMistakePrefix(game, kWordCount - 1);
        type(game, "bi");   // composed length == stream length, base letters match
        check(!game.isGameOver(), "tone-pending tail is NOT judged (run waits)");
        check(game.getMistakes() == 0, "no mistake while the tone key is pending");
        game.handleKey(charKey(U'r'));   // completes "bỉ"
        RunResult r{};
        check(game.pollRunResult(r) && r.completed, "tone key completes the clean win");
        check(game.getMistakes() == 0, "clean win keeps zero mistakes");
    }

    // Win with zero mistakes still works (the verdict must not fire early).
    {
        NoMistakeGame game;
        game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        game.setFailMode(FailMode::Hardcore);
        game.start();
        typeNoMistakePrefix(game, kWordCount - 1);
        type(game, kNoMistakeTelex[kWordCount - 1]);
        RunResult r{};
        check(game.pollRunResult(r) && r.completed, "clean run completes");
        check(game.getMistakes() == 0, "clean run has zero mistakes");
    }
}

void testNoMistakeMidStreamWrongWord() {
    std::cout << "== B6: NoMistake space-boundary wrong word (both fail modes) ==\n";
    {
        NoMistakeGame game;
        game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        game.setFailMode(FailMode::HealthBar);
        game.setStartReserve(10000);
        game.start();
        type(game, "hocj awn hocj noisx ");   // "nóis" wrong at the space
        check(game.getMistakes() == 1, "HealthBar: mid-stream wrong word counted");
        check(!game.isGameOver(), "HealthBar: mid-stream wrong word survives");
    }
    {
        NoMistakeGame game;
        game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        game.setFailMode(FailMode::Hardcore);
        game.start();
        type(game, "hocj awn hocj noisx ");
        check(game.getMistakes() == 1, "Hardcore: mid-stream wrong word counted");
        check(game.isGameOver(), "Hardcore: mid-stream wrong word ends the run");
    }
}

void testNoMistakeLiveStats() {
    std::cout << "== B6: NoMistake live WPM/accuracy while playing ==\n";
    NoMistakeGame game;
    game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
    game.setFailMode(FailMode::HealthBar);
    game.setStartReserve(10000);
    game.start();
    type(game, "hocj awn ");
    game.update(0.5);
    game.update(0.5);   // 1.0 s elapsed
    Frame f;
    game.buildFrame(f);
    check(f.stats.wpm > 0.0, "live WPM is non-zero mid-run (beta4 showed 0)");
    check(f.stats.accuracy > 0.0 && f.stats.accuracy <= 100.0,
          "live accuracy is populated mid-run");
}

void testManagerLiveConfigApply() {
    std::cout << "== B6: ArcadeManager::setConfig applies to the live game ==\n";
    auto& mgr = ArcadeManager::instance();
    mgr.launchGame(GameType::NoMistake);
    check(mgr.hasActiveGame(), "NoMistake launched headless");
    ArcadeConfig cfg = mgr.getConfig();
    cfg.noMistakeFailMode = FailMode::HealthBar;
    cfg.rhythmFailMode = FailMode::HealthBar;
    cfg.wasdSteering = WasdSteering::Both;
    mgr.setConfig(cfg);
    auto* nm = dynamic_cast<NoMistakeGame*>(mgr.getCurrentGame());
    check(nm != nullptr, "current game is NoMistake");
    check(nm && nm->getFailMode() == FailMode::HealthBar,
          "live setConfig reached the running NoMistake game");
    mgr.stopGame();
}

//============================================================================
// B7 — WASD steering choice in VN mode: letters steer AND feed the composer.
//============================================================================
void testWasdSteeringModes() {
    std::cout << "== B7: WasdRace steering modes (VN) ==\n";
    // Arrows (beta4 behavior): letters NEVER steer; every letter composes.
    {
        WasdRaceGame game;
        game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        game.setSteeringMode(WasdSteering::Arrows);
        game.start();
        const int lane0 = game.getPlayerLane();
        VnComposer ref; ref.setMethod(ok::text::InputMethod::Telex);
        game.handleKey(charKey(U'a'));
        ref.feedProduced(U'a');
        check(game.getPlayerLane() == lane0, "Arrows: 'a' does not steer");
        check(game.composedText() == ref.text() && !ref.text().empty(),
              "Arrows: 'a' fed the composer");
        game.handleKey(vkKey(vk::kLeft));
        check(game.getPlayerLane() == lane0 - 1, "Arrows: LEFT arrow steers");
        game.handleKey(charKey(U'w'));
        ref.feedProduced(U'w');
        check(game.getPlayerLane() == lane0 - 1, "Arrows: 'w' does not steer");
        check(game.composedText() == ref.text(),
              "Arrows: 'w' reached the composer (never swallowed)");
    }
    // Wasd: a/s/d/w steer the car AND fall through to the composer.
    {
        WasdRaceGame game;
        game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        game.setSteeringMode(WasdSteering::Wasd);
        game.start();
        const int lane0 = game.getPlayerLane();
        VnComposer ref; ref.setMethod(ok::text::InputMethod::Telex);
        for (char32_t ch : {U'a', U'd', U'd', U'w', U's'}) {
            game.handleKey(charKey(ch));
            ref.feedProduced(ch);
        }
        check(game.getPlayerLane() == lane0 + 1, "Wasd: a/d steered the car (left, right, right)");
        check(game.composedText() == ref.text(),
              "Wasd: every steering letter ALSO fed the composer (W never swallowed)");
        game.handleKey(vkKey(vk::kLeft));
        check(game.getPlayerLane() == lane0, "Wasd: arrows still steer too");
    }
    // Both: same letter behavior as Wasd (steer + compose), arrows steer.
    {
        WasdRaceGame game;
        game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        game.setSteeringMode(WasdSteering::Both);
        game.start();
        const int lane0 = game.getPlayerLane();
        VnComposer ref; ref.setMethod(ok::text::InputMethod::Telex);
        game.handleKey(charKey(U's'));
        ref.feedProduced(U's');
        check(game.getPlayerLane() == lane0, "Both: 's' does not change lane (speed only)");
        check(game.composedText() == ref.text(), "Both: 's' fed the composer");
        game.handleKey(charKey(U'a'));
        ref.feedProduced(U'a');
        check(game.getPlayerLane() == lane0 - 1, "Both: 'a' steers");
        check(game.composedText() == ref.text(), "Both: 'a' fed the composer too");
    }
    // EN mode is unchanged by the setting: letters steer, no composer.
    {
        WasdRaceGame game;
        game.setPassageLanguage(PassageLanguage::English, VnInputMethod::Telex);
        game.setSteeringMode(WasdSteering::Wasd);
        game.start();
        const int lane0 = game.getPlayerLane();
        game.handleKey(charKey(U'a'));
        check(game.getPlayerLane() == lane0 - 1, "EN: 'a' steers (legacy behavior kept)");
        check(game.composedText().empty(), "EN: no VN composer active");
    }
}

// 1000-seed fuzz over the three steering modes: the composed text and index
// must equal an independent VnComposer fed the same characters — steering can
// never steal a keystroke from composition — and the lane model must match.
void testWasdSteeringFuzz() {
    std::cout << "== B7: 1000-seed steering fuzz (VN, all modes) ==\n";
    std::mt19937 rng(20260920u);
    const char32_t kChars[] = {U'a', U'd', U'w', U's', U'o', U'u', U'j', U'x',
                               U'g', U'n', U'i', U'e', U'l', U'c', U' '};
    bool mismatch = false, laneBad = false, boundBad = false;

    for (std::uint32_t seed = 0; seed < 1000 && !mismatch && !laneBad; ++seed) {
        const WasdSteering mode = static_cast<WasdSteering>(seed % 3);
        WasdRaceGame game;
        game.setSeed(seed);
        game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        game.setSteeringMode(mode);
        game.start();

        VnComposer ref;
        ref.setMethod(ok::text::InputMethod::Telex);
        const std::u32string target(game.getPassage());

        int lane = game.getPlayerLane();
        const std::size_t steps = 6 + (seed % 10);   // < passage length: no wrap
        for (std::size_t i = 0; i < steps; ++i) {
            const bool arrow = (rng() % 6) == 0;
            if (arrow) {
                const bool left = (rng() % 2) == 0;
                game.handleKey(vkKey(left ? vk::kLeft : vk::kRight));
                if (left) { lane = std::max(0, lane - 1); }
                else { lane = std::min(2, lane + 1); }
            } else {
                const char32_t ch = kChars[rng() % std::size(kChars)];
                game.handleKey(charKey(ch));
                if (ch == U' ') { ref.feedSpace(); } else { ref.feedProduced(ch); }
                if (mode != WasdSteering::Arrows) {
                    if (ch == U'a' || ch == U'A') { lane = std::max(0, lane - 1); }
                    else if (ch == U'd' || ch == U'D') { lane = std::min(2, lane + 1); }
                }
            }
            if (game.isGameOver()) { break; }   // fuel exhausted: state resets, stop
            if (game.composedText() != ref.text() ||
                game.getTextIndex() != ref.matchLength(target)) {
                mismatch = true;
                break;
            }
            if (game.getPlayerLane() != lane) { laneBad = true; break; }
            if (game.getPlayerLane() < 0 || game.getPlayerLane() > 2) { boundBad = true; break; }
        }
    }
    check(!mismatch, "fuzz: steering never corrupts VN composition (1000 seeds x 3 modes)");
    check(!laneBad, "fuzz: lane changes follow the selected steering mode");
    check(!boundBad, "fuzz: lane always in bounds");
}

// 1000-seed recovery fuzz for TypingRace: composed text and progress must
// equal an independent composer fed the identical event sequence (Backspace
// included) — the exact operation the tester reported as "not working".
void testTypingRaceRecoveryFuzz() {
    std::cout << "== B5: 1000-seed TypingRace recovery fuzz ==\n";
    std::mt19937 rng(777u);
    const char32_t kChars[] = {U'b', U'o', U'j', U'g', U'x', U't', U'i', U'e',
                               U's', U'n', U'v', U'h', U'd', U'a', U'u', U'w', U' '};
    bool bad = false;
    for (std::uint32_t seed = 0; seed < 1000 && !bad; ++seed) {
        TypingRaceGame game;
        game.setSeed(seed);
        game.setPassageLanguage(PassageLanguage::Vietnamese, VnInputMethod::Telex);
        game.start();
        VnComposer ref;
        ref.setMethod(ok::text::InputMethod::Telex);
        const std::u32string target(game.getPassage());
        const std::size_t steps = 5 + (seed % 12);   // far below passage length
        for (std::size_t i = 0; i < steps; ++i) {
            if ((rng() % 5) == 0) {
                game.handleKey(vkKey(0x08));
                ref.feedBackspace();
            } else {
                const char32_t ch = kChars[rng() % std::size(kChars)];
                game.handleKey(charKey(ch));
                if (ch == U' ') { ref.feedSpace(); } else { ref.feedProduced(ch); }
            }
            if (game.composedText() != ref.text() ||
                game.getCharIndex() != ref.matchLength(target)) {
                bad = true;
                break;
            }
        }
    }
    check(!bad, "fuzz: backspace-after-wrong-word tracks the composer exactly (1000 seeds)");
}

} // namespace

int main() {
    testCorpusIsComposerVerified();
    testTypingRaceDivergentTail();
    testFishingDivergentTail();
    testWasdRaceDivergentTailAndHints();
    testNoMistakeDivergentTail();
    testNoMistakeFinalWordVerdict();
    testNoMistakeMidStreamWrongWord();
    testNoMistakeLiveStats();
    testManagerLiveConfigApply();
    testWasdSteeringModes();
    testWasdSteeringFuzz();
    testTypingRaceRecoveryFuzz();

    if (g_fail == 0) {
        std::cout << "\nALL ARCADE-BETA5 TESTS PASSED (" << g_pass << " checks)\n";
        return 0;
    }
    std::cout << "\n" << g_fail << " ARCADE-BETA5 TEST(S) FAILED (" << g_pass << " passed)\n";
    return 1;
}
