// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 coderunknow
#include "LiveEffects.hpp"
#include "TextEngine.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <thread>

using namespace ok::text;
using namespace ok::effects;

struct Typist {
    TextEngine engine;
    LiveEffects effects;
    std::wstring raw, screen;
    explicit Typist(Config config, EngineOptions options = {}) : engine(options) {
        effects.configure(config);
        effects.sync();
    }
    void key(char32_t ch) {
        TextInput in{};
        in.kind = ch == U' ' ? InputKind::Space : ch == U'\b' ? InputKind::Backspace : InputKind::Char;
        in.ch = ch;
        in.isCaps = ch >= U'A' && ch <= U'Z';
        const auto& r = engine.process(in);
        std::wstring replacement = engine.replacementUtf16(r);
        if (r.code == EngineCode::ReplaceMacro) { engine.macroExpansionUtf16(r, replacement); }
        std::size_t erase = r.backspaceCount;
        const bool edit = r.consumed() && (erase != 0 || !replacement.empty());
        if (edit) {
            if ((r.code == EngineCode::Restore || r.code == EngineCode::RestoreAndStartNewSession) &&
                (in.kind == InputKind::Char || in.kind == InputKind::Space)) {
                replacement.push_back(static_cast<wchar_t>(ch));
            }
        } else {
            erase = ch == U'\b' ? 1 : 0;
            replacement = ch == U'\b' ? L"" : std::wstring(1, static_cast<wchar_t>(ch));
        }
        assert(!edit || erase <= raw.size());
        apply(erase, replacement);
    }
    void apply(std::size_t erase, std::wstring replacement) {
        assert(raw.size() == screen.size());
        erase = std::min(erase, raw.size());
        raw.erase(raw.size() - erase);
        screen.erase(screen.size() - erase);
        raw += replacement;
        effects.rewrite(erase, replacement);
        screen += replacement;
        assert(raw.size() == screen.size());
        for (std::size_t i = 0; i < raw.size(); ++i) {
            assert(static_cast<char32_t>(screen[i]) == effects.map(static_cast<char32_t>(raw[i]), i));
        }
    }
    void type(std::u32string_view keys) { for (auto ch : keys) { key(ch); } }
};

int main() {
    for (const auto glyph : {Glyph::None, Glyph::UpsideDown, Glyph::Mirror, Glyph::Random}) {
        for (unsigned intensity : {0u, 25u, 50u, 100u}) {
            Typist telex({true, true, glyph, intensity});
            telex.type(U"tieengs vieetj");
            assert(telex.raw == L"tiếng việt");
            if (glyph == Glyph::None && intensity == 100) { assert(telex.screen == L"TIẾNG VIỆT"); }
            // Undo tone, retype, English restoration, word history and repeated edits.
            telex.type(U"\b\bjt wolf wifi cas\b\bs \b\b\b\baas ");
            EngineOptions vni;
            vni.inputMethod = InputMethod::Vni;
            vni.digitsAreLiteral = false;
            Typist digits({true, true, glyph, intensity}, vni);
            digits.type(U"tie6ng1 vie6t5");
            assert(digits.raw == L"tiếng việt");
            digits.type(U"\b\b5 123 abc\b\bdef");
        }
    }
    Typist macro({true, true, Glyph::Random, 50});
    macro.engine.setMacroResolver([](const std::vector<std::uint32_t>& key, std::vector<std::uint32_t>& out) {
        if (key != std::vector<std::uint32_t>{'B', 'T'}) { return false; }
        const std::u32string value = U"bình thường ";
        out.assign(value.begin(), value.end());
        return true;
    });
    macro.type(U"bt ");
    assert(macro.raw == L"bình thường ");
    macro.type(U"\b\btieengs");
    Typist tone({true, true, Glyph::Mirror, 50});
    tone.type(U"hoaf");
    const auto beforeTone = tone.raw;
    assert(tone.engine.switchToneStyle());
    const auto& toneResult = tone.engine.lastResult();
    tone.apply(toneResult.backspaceCount, tone.engine.replacementUtf16(toneResult));
    assert(tone.raw != beforeTone);
    tone.type(U"\baf");
    Typist off({false, true, Glyph::Random, 100});
    off.type(U"tieengs vieetj wolf wifi 123 ");
    assert(off.raw == off.screen);

    // Bounded deterministic mixed typing/editing campaign: compare EVERY visible
    // state to the unmodified engine, not just the final word.
    Typist fuzz({true, true, Glyph::Random, 50});
    std::uint32_t state = 17;
    const std::u32string alphabet = U"abcdefghijklmnopqrstuvwxyzasfrxj 0123456789.,\b\b";
    for (int i = 0; i < 25000; ++i) {
        state = state * 1664525u + 1013904223u;
        fuzz.key(alphabet[state % alphabet.size()]);
        if (i % 100 == 99) { // simulate a focus/caret boundary: both sessions reset
            fuzz.engine.startNewSession(); fuzz.effects.reset(); fuzz.raw.clear(); fuzz.screen.clear();
        }
    }

    LiveEffects effects;
    effects.configure({true, true, Glyph::UpsideDown, 100});
    assert(effects.sync());
    std::wstring text = L"Macro tiếng Việt ";
    text.push_back(static_cast<wchar_t>(0xD83D));
    text.push_back(static_cast<wchar_t>(0xDE00));
    const auto size = text.size();
    effects.rewrite(0, text);
    assert(text.size() == size && text[size-2] == 0xD83D && text[size-1] == 0xDE00);
    effects.disable(); assert(!effects.enabled()); assert(effects.sync());
    std::wstring normal = L"aáđ";
    assert(!effects.rewrite(0, normal) && normal == L"aáđ");

    // Concurrent UI publication never touches the producer's cursor or exposes
    // a partially updated config. Only sync/rewrite run on the producer thread.
    std::thread ui([&] {
        for (int i = 0; i < 10000; ++i) { effects.configure({bool(i & 1), true, Glyph::Mirror, 50}); }
    });
    for (int i = 0; i < 10000; ++i) {
        effects.sync();
        std::wstring value = L"Việt";
        effects.rewrite(0, value);
        assert(value.size() == 4);
    }
    ui.join();
    std::cout << "Live effects: literals, Telex/VNI rewrites, backspace, restore, Unicode and publication PASS\n";
}
