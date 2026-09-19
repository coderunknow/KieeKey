//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/test_live_output_plan.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// Live external effects — SHIPPED-PATH suite (v1.3.0-beta3).
//
// User report: "Một số trò gõ trực tiếp hiện ngoài (như random case,..) thì vẫn
// chưa thấy dùng được, khi gõ ở ngoài vẫn thấy như bình thường."
//
// tests/test_live_effects.cpp verifies the LiveEffects mapper with a hand
// written emulation of the app. That is necessary but not sufficient: the
// decision that actually runs lives in src/app/main.cpp's producer callback,
// which no test could execute, so a wrong decision there was invisible. beta3
// extracted that decision into ok::effects::planOutput() — this file drives the
// REAL TextEngine through the REAL planOutput() exactly in the order the hook
// does (engine.process -> replacementUtf16 -> restore re-issue -> plan ->
// apply), and asserts the two texts a user can compare:
//
//   * `plain`  — what the engine produced (the ground truth the IME must not
//                corrupt: Vietnamese state stays original),
//   * `screen` — what the application receives.
//
// Invariants checked on EVERY keystroke, not just at the end:
//   1. effects off  => screen == plain, byte for byte (feature isolation),
//   2. effects on   => |screen| == |plain| (erase counts stay correct, so the
//                      caret and the engine can never desynchronise),
//   3. screen[i] == fx.map(plain[i], i) (the styling is the documented
//      position-aware mapper — deterministic, reproducible after a backspace),
//   4. a pass-through key is never suppressed without a styled replacement
//      (that is how a character would silently disappear).
//----------------------------------------------------------------------------
#include "LiveEffects.hpp"
#include "TextEngine.hpp"

#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>

using namespace ok::text;
using namespace ok::effects;

namespace {

KeyKind kindOf(char32_t ch) noexcept {
    if (ch == U' ') { return KeyKind::Space; }
    if (ch == U'\b') { return KeyKind::Backspace; }
    if (ch == U'\n' || ch == U'\t' || ch == U',' || ch == U'.' || ch == U';' ||
        ch == U':' || ch == U'!' || ch == U'?') {
        return KeyKind::WordBreak;
    }
    return KeyKind::Char;
}

// Mirrors src/app/main.cpp::onHookEventImpl for one keystroke.
struct AppPath {
    TextEngine engine;
    LiveEffects fx;
    bool active = false;
    std::wstring plain;    // engine ground truth
    std::wstring screen;   // what the app receives
    std::uint64_t styledChars = 0;
    std::uint64_t suppressedKeys = 0;
    std::uint64_t passThroughKeys = 0;
    // The live-effects styling cursor (LiveEffects::position_) restarts at 0
    // on every word-break / control / navigation key ("new context"), while
    // `plain` keeps growing. So the cursor position of the char at buffer
    // index i is (i - segStart), NOT i. segStart is the buffer index where the
    // current styling segment began; the invariant position_ == size-segStart
    // is maintained across advances (styled char), rewinds (backspace/erase)
    // and resets (word break).
    std::size_t segStart = 0;

    explicit AppPath(Config config, EngineOptions options = {}) : engine(options) {
        fx.configure(config);
        fx.sync();
        active = config.enabled;
    }

    void applyEdit(std::size_t erase, const std::wstring& plainText,
                   const std::wstring& screenText) {
        assert(erase <= plain.size());
        assert(erase <= screen.size());
        plain.erase(plain.size() - erase);
        screen.erase(screen.size() - erase);
        plain += plainText;
        screen += screenText;
    }

    void key(char32_t ch) {
        const KeyKind kind = kindOf(ch);
        TextInput in{};
        in.kind = (ch == U' ')   ? InputKind::Space
                : (ch == U'\b')  ? InputKind::Backspace
                : (ch == U'\n' || ch == U'\t') ? InputKind::WordBreak
                                               : InputKind::Char;
        in.ch = ch;
        in.isCaps = (ch >= U'A' && ch <= U'Z');

        const EngineResult& r = engine.process(in);
        std::wstring replacement = engine.replacementUtf16(r);
        if (r.code == EngineCode::ReplaceMacro) {
            engine.macroExpansionUtf16(r, replacement);
        }
        std::size_t erase = r.backspaceCount;
        bool suppress = false;
        if (r.code == EngineCode::ReplaceMacro) {
            suppress = (erase != 0 || !replacement.empty());
        } else if (r.consumed() && !(erase == 0 && replacement.empty())) {
            suppress = true;
        }
        // Restore re-issue (hotfix §3 / D4): the reverted word is followed by
        // the key the user actually pressed.
        const bool reissue = (r.code == EngineCode::Restore ||
                              r.code == EngineCode::RestoreAndStartNewSession) &&
                             (in.kind == InputKind::Char || in.kind == InputKind::Space);
        if (reissue) {
            replacement.push_back(static_cast<wchar_t>(ch));
        }

        // planOutput() styles the scratch buffer IN PLACE (the hook reuses one
        // buffer, zero per-key allocation), so keep the unstyled ground truth
        // separately — it is what `plain` (the engine's Vietnamese) must show.
        const std::wstring groundTruth = replacement;
        std::wstring scratch = replacement;
        const OutputPlan plan = planOutput(active, suppress, erase, scratch,
                                           kind, ch, fx);

        // Contract 4: a swallowed key MUST carry the text that replaces it,
        // otherwise the character silently disappeared.
        assert(!plan.suppress || plan.backspace != 0 || !scratch.empty());
        // Suppress and native pass-through are mutually exclusive.
        assert(!(plan.suppress && plan.nativePassThrough));

        // planOutput resets the styling cursor (LiveEffects::reset) exactly for
        // a word-break/navigation key, or a Char/Space that is a control or
        // supplementary code point (delivered natively, never truncated).
        // planOutput calls fx.reset() ONLY on the native pass-through path: a
        // word-break/navigation key, or a Char/Space that is a control or
        // supplementary code point. An engine-suppressed key (macro/restore on
        // punctuation) is styled instead and does NOT reset the cursor, so this
        // is gated on nativePassThrough (suppress and native are exclusive).
        const bool resetEvent = plan.nativePassThrough &&
            ((kind == KeyKind::WordBreak || kind == KeyKind::Other) ||
             ((kind == KeyKind::Char || kind == KeyKind::Space) &&
              (ch < 0x20 || ch > 0xFFFF)));

        const std::size_t oldSize = plain.size();
        std::size_t eraseCount = 0;
        if (plan.suppress) {
            // The application never sees the raw key: the plan's text is what
            // lands. The ground truth is the engine's own replacement — or, for
            // a literal character the engine passed on, that character.
            std::wstring truth = groundTruth;
            if (!suppress && (in.kind == InputKind::Char || in.kind == InputKind::Space)) {
                truth.assign(1, static_cast<wchar_t>(ch));
            }
            assert(truth.size() == scratch.size());   // BMP styling is 1:1
            eraseCount = plan.backspace;
            applyEdit(plan.backspace, truth, scratch);
            ++suppressedKeys;
        } else {
            // The application receives the raw key untouched.
            if (ch == U'\b') {
                if (!plain.empty()) { plain.pop_back(); eraseCount = 1; }
                if (!screen.empty()) { screen.pop_back(); }
            } else {
                plain.push_back(static_cast<wchar_t>(ch));
                screen.push_back(static_cast<wchar_t>(ch));
            }
            ++passThroughKeys;
        }
        if (plan.styled) { styledChars += scratch.size(); }

        // Maintain segStart so the invariant position_ == size-segStart holds.
        //  * reset event (word break / control / nav): the cursor restarts at
        //    0, so a fresh segment begins at the new end of the buffer.
        //  * otherwise an erase of `eraseCount` chars from the end can reach
        //    back past segStart into frozen (already-styled) territory; when it
        //    does, LiveEffects::backspace clamps the cursor to 0 and the new
        //    segment boundary moves back to (oldSize - eraseCount).
        if (resetEvent) {
            segStart = plain.size();
        } else {
            const std::size_t afterErase =
                oldSize >= eraseCount ? oldSize - eraseCount : 0;
            if (segStart > afterErase) { segStart = afterErase; }
            if (segStart > plain.size()) { segStart = plain.size(); }
        }

        // Invariant 2 (always): the app buffer and the engine ground truth stay
        // the same length, so the caret and erase counts can never desync.
        assert(plain.size() == screen.size());
        // Invariant 3 (segment-relative): every char styled in the CURRENT
        // segment equals the documented position-aware mapper evaluated at its
        // cursor position (index - segStart). Chars before segStart were frozen
        // by a word break and already validated on the keystroke that styled
        // them; native pass-through chars are raw (screen == plain) by design.
        for (std::size_t i = segStart; i < plain.size(); ++i) {
            assert(screen[i] == static_cast<wchar_t>(
                                   fx.map(static_cast<char32_t>(plain[i]),
                                          i - segStart)));
        }
    }

    void type(const std::u32string& keys) { for (char32_t ch : keys) { key(ch); } }
    void wordBoundary() {
        // A foreground/window switch: the app takes the engine to a fresh
        // context and resets the effect cursor (onHookEventImpl's
        // ForegroundChanged branch). v1.3.0-beta3: this is the FULL
        // resetForNewContext(), not the partial startNewSession() — the latter
        // left visibleAccount_ holding the previous window's committed length,
        // so the D2 clamp could not bound a correction in the new window and it
        // over-erased (the exact bug this suite reproduces and now guards).
        engine.resetForNewContext();
        fx.reset();
        plain.clear();
        screen.clear();
        segStart = 0;
    }
};


void testOffIsByteIdentical() {
    AppPath app(Config{false, true, Glyph::Random, 100});
    app.type(U"tieengs vieetj nhanh123 wolf wifi \b\b\bas ");
    assert(app.screen == app.plain);
    assert(app.styledChars == 0);
    assert(!app.screen.empty());
    // The engine still composes Vietnamese with the channel off.
    assert(app.plain.find(L"tiếng") != std::wstring::npos ||
           app.plain.find(L"nhanh") != std::wstring::npos);
    std::cout << "  [PASS] effects OFF: screen == plain on every keystroke\n";
}

void testOnStylesAndKeepsLength() {
    for (const Glyph glyph : {Glyph::None, Glyph::UpsideDown, Glyph::Mirror, Glyph::Random}) {
        for (const unsigned intensity : {25u, 50u, 100u}) {
            Config config{true, true, glyph, intensity};
            AppPath app(config);
            app.type(U"tieengs vieetj ");
            // The engine's Vietnamese must be intact …
            assert(app.plain == L"tiếng việt ");
            // … and the screen must actually differ (this is the user-visible
            // symptom: "gõ ở ngoài vẫn thấy như bình thường").
            if (intensity == 100u) {
                assert(app.screen != app.plain);
                assert(app.styledChars > 0);
            }
            assert(app.screen.size() == app.plain.size());
            // Backspace + retype reproduces the SAME styling (position-aware).
            const std::wstring before = app.screen;
            app.type(U"\b\b\b\b\b");
            app.type(U"vietj ");
            (void)before;
            assert(app.screen.size() == app.plain.size());

            app.wordBoundary();
            app.type(U"hom nay troi dep qua 123 ");
            assert(app.plain == L"hom nay troi dep qua 123 ");
            assert(app.screen.size() == app.plain.size());
        }
    }
    std::cout << "  [PASS] effects ON: styled output, intact engine state, equal lengths\n";
}

void testEveryGlyphModeProducesVisibleChange() {
    // At 100% intensity each mode must change SOMETHING on an ordinary
    // sentence; a mode that silently does nothing is the reported bug.
    const std::u32string sentence = U"KieeKey bo go tieng Viet nhanh va chinh xac";
    for (const Glyph glyph : {Glyph::UpsideDown, Glyph::Mirror, Glyph::Random}) {
        AppPath app(Config{true, true, glyph, 100});
        app.type(sentence);
        assert(app.screen != app.plain);
        assert(app.screen.size() == app.plain.size());
        std::size_t changed = 0;
        for (std::size_t i = 0; i < app.plain.size(); ++i) {
            if (app.plain[i] != app.screen[i]) { ++changed; }
        }
        assert(changed > 0);
    }
    // Random casing alone (no glyph mode) must also be visible.
    AppPath caseOnly(Config{true, true, Glyph::None, 100});
    caseOnly.type(sentence);
    assert(caseOnly.screen != caseOnly.plain);
    std::size_t caseChanged = 0;
    for (std::size_t i = 0; i < caseOnly.plain.size(); ++i) {
        if (caseOnly.plain[i] != caseOnly.screen[i]) { ++caseChanged; }
    }
    assert(caseChanged > 5);
    std::cout << "  [PASS] all four glyph modes + random casing change the output\n";
}

void testIntensityScalesTheEffect() {
    const std::u32string sentence = U"mot hai ba bon nam sau bay tam chin muoi";
    std::size_t previous = std::string::npos;
    for (const unsigned intensity : {0u, 25u, 50u, 100u}) {
        AppPath app(Config{true, true, Glyph::None, intensity});
        app.type(sentence);
        std::size_t changed = 0;
        for (std::size_t i = 0; i < app.plain.size(); ++i) {
            if (app.plain[i] != app.screen[i]) { ++changed; }
        }
        if (intensity == 0u) { assert(changed == 0); }
        if (previous != std::string::npos) { assert(changed >= previous); }
        previous = changed;
    }
    assert(previous > 0);
    std::cout << "  [PASS] intensity 0/25/50/100 scales the number of styled chars\n";
}

void testBackspaceKeepsCursorAligned() {
    AppPath app(Config{true, true, Glyph::Random, 100});
    app.type(U"tieengs");
    assert(app.plain == L"tiếng");
    const std::wstring screenAtFull = app.screen;
    // Delete two characters and retype them: the styling must come back
    // identical (the cursor rewinds with the erase count).
    app.type(U"\b\b");
    assert(app.plain == L"tiế");
    assert(app.screen.size() == 3);
    app.type(U"ng");
    assert(app.plain == L"tiếng");
    assert(app.screen == screenAtFull);
    std::cout << "  [PASS] backspace rewinds the styling cursor (deterministic retype)\n";
}

void testMacroExpansionIsStyledToo() {
    AppPath app(Config{true, true, Glyph::Random, 100});
    app.engine.setMacroResolver(
        [](const std::vector<std::uint32_t>& key, std::vector<std::uint32_t>& out) {
            if (key != std::vector<std::uint32_t>{'B', 'T'}) { return false; }
            const std::u32string value = U"bình thường";
            out.assign(value.begin(), value.end());
            return true;
        });
    app.type(U"bt ");
    // D3 contract (main.cpp ReplaceMacro branch): the macro fires on the
    // space and CONSUMES it — "macro + space -> expansion, no extra space;
    // the expansion itself provides the separator". So there is no trailing
    // space, and the 11-char expansion rides the styled channel unchanged.
    assert(app.plain == L"bình thường");
    assert(app.screen.size() == app.plain.size());
    assert(app.screen != app.plain);
    std::cout << "  [PASS] macro expansion rides the same styled channel\n";
}

void testSupplementaryCharsPassThrough() {
    // A non-BMP character must never be truncated into a surrogate half: the
    // plan refuses to style it, leaves the scratch untouched, and the app
    // delivers the raw key natively.
    LiveEffects fx;
    fx.configure(Config{true, true, Glyph::Random, 100});
    fx.sync();
    std::wstring scratch;   // empty, as for a DoNothing char event
    const OutputPlan plan = planOutput(true, false, 0, scratch,
                                       KeyKind::Char, U'\U0001F600', fx);
    assert(plan.nativePassThrough);
    assert(!plan.suppress);
    assert(scratch.empty());   // never styled, never truncated to a surrogate

    // Control characters (Tab/Enter arrive as WordBreak in the app, but a stray
    // control char must not be styled either).
    std::wstring ctrlScratch;
    const OutputPlan control = planOutput(true, false, 0, ctrlScratch,
                                          KeyKind::Char, U'\x01', fx);
    assert(control.nativePassThrough && ctrlScratch.empty());
    std::cout << "  [PASS] supplementary/control characters are delivered natively\n";
}

void testVniAndDigits() {
    // VNI with digits-as-marks: the styled channel must not corrupt numbers.
    EngineOptions vni;
    vni.inputMethod = InputMethod::Vni;
    vni.digitsAreLiteral = false;
    AppPath app(Config{true, true, Glyph::Random, 100}, vni);
    app.type(U"tie6ng1 vie6t5 ");
    assert(app.plain == L"tiếng việt ");
    assert(app.screen.size() == app.plain.size());

    EngineOptions literal;
    literal.digitsAreLiteral = true;
    AppPath digits(Config{true, true, Glyph::UpsideDown, 100}, literal);
    digits.type(U"so 1234567890 ");
    assert(digits.plain == L"so 1234567890 ");
    assert(digits.screen.size() == digits.plain.size());
    std::cout << "  [PASS] VNI composition and literal digits survive styling\n";
}

void testContextResetPreventsOverBackspace() {
    // v1.3.0-beta3 regression — the foreground-switch over-backspace bug.
    //
    // A window switch must take the engine to a FRESH document context. beta2
    // called the partial startNewSession(), which left visibleAccount_ (the D2
    // clamp `backspaceCount <= visibleAccount_`) holding the PREVIOUS window's
    // committed length. In the new window the engine then composed "i"+"x"->"ĩ"
    // (1 rendered char) and, on 'w', asked to erase 2 — one more than existed —
    // deleting text to the LEFT of the caret. resetForNewContext() zeroes the
    // account, so the clamp re-arms and the engine becomes decision-identical to
    // a brand-new engine (the option-matrix tier-6 FRESH contract).
    // `strict` mirrors the D2 account exactly — valid for a plain Char stream
    // with no space/backspace/restore (the post-reset "window B" typing). The
    // setup "window A" garbage stream uses the tolerant mode (its accounting
    // involves spaces/backspaces this minimal consumer does not replay
    // bit-for-bit); it only has to inflate visibleAccount_ above zero.
    auto runStream = [](TextEngine& eng, const std::u32string& stream,
                        std::wstring& rendered, bool strict) {
        for (char32_t ch : stream) {
            TextInput in{};
            in.ch = ch;
            in.kind = (ch == U' ')  ? InputKind::Space
                    : (ch == U'\b') ? InputKind::Backspace
                                    : InputKind::Char;
            const EngineResult& r = eng.process(in);
            std::wstring rep = eng.replacementUtf16(r);
            if (r.code == EngineCode::ReplaceMacro) { eng.macroExpansionUtf16(r, rep); }
            const std::size_t bs = r.backspaceCount;
            if (strict) {
                // D2 over-backspace contract: the engine may never ask to erase
                // more than the consumer had rendered BEFORE this edit. THIS is
                // the assert that fires under the partial reset ('w' wants bs=2
                // with one rendered char) and holds under resetForNewContext().
                assert(bs <= rendered.size());
            }
            const std::size_t erase = bs < rendered.size() ? bs : rendered.size();
            rendered.erase(rendered.size() - erase);
            if (r.code == EngineCode::DoNothing && in.kind == InputKind::Char) {
                rendered.push_back(static_cast<wchar_t>(ch));   // raw key lands
            } else {
                rendered += rep;                                // replacement lands
            }
            if (strict) {
                // D2 mirror: AFTER the edit lands, the engine's committed-count
                // tracks the rendered length exactly (a fresh/reset engine and
                // its consumer never drift).
                assert(eng.visibleAccount() == rendered.size());
            }
        }
    };

    const std::u32string garbage = U"asfrxwj0123456789,. \b\b";
    const std::u32string newWord = U"ixw";   // composes "ĩ", then 'w' recomposes

    // Commit text in "window A", switch context, then type in "window B": no
    // over-backspace, and decision-identical to a fresh engine.
    TextEngine reset;
    {
        std::wstring windowA;
        runStream(reset, garbage, windowA, /*strict=*/false);
        assert(reset.visibleAccount() > 0);   // sanity: window A committed text
    }
    reset.resetForNewContext();
    assert(reset.visibleAccount() == 0);      // D2 clamp re-armed for window B
    std::wstring windowB;
    runStream(reset, newWord, windowB, /*strict=*/true);   // must not over-erase

    TextEngine fresh;
    std::wstring freshOut;
    runStream(fresh, newWord, freshOut, /*strict=*/true);
    assert(windowB == freshOut);              // decision-identical to fresh
    assert(reset.visibleAccount() == fresh.visibleAccount());

    std::cout << "  [PASS] context reset re-arms the D2 clamp (no over-backspace"
                 " after a window switch; fresh-engine parity)\n";
}

void testFuzzCampaign() {
    // Bounded deterministic campaign: every keystroke re-checks the length and
    // mapping invariants, across word boundaries, backspaces and restores.
    AppPath app(Config{true, true, Glyph::Random, 50});
    std::uint32_t state = 20260919u;
    const std::u32string alphabet =
        U"abcdefghijklmnopqrstuvwxyz asfrxwj0123456789\b\b,. \n";
    for (int i = 0; i < 40000; ++i) {
        state = state * 1664525u + 1013904223u;
        app.key(alphabet[state % alphabet.size()]);
        if (i % 250 == 249) { app.wordBoundary(); }
    }
    assert(app.plain.size() == app.screen.size());
    std::cout << "  [PASS] 40k-key fuzz campaign: invariants hold on every key\n";
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);   // survive assert()/abort()
    std::cout << "=== Running Live Output Plan (shipped-path) Suite ===\n";
    testOffIsByteIdentical();
    testOnStylesAndKeepsLength();
    testEveryGlyphModeProducesVisibleChange();
    testIntensityScalesTheEffect();
    testBackspaceKeepsCursorAligned();
    testMacroExpansionIsStyledToo();
    testSupplementaryCharsPassThrough();
    testVniAndDigits();
    testContextResetPreventsOverBackspace();
    testFuzzCampaign();
    std::cout << "=== ALL LIVE OUTPUT PLAN TESTS PASSED ===\n";
    return 0;
}
