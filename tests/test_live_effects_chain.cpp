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
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// File: tests/test_live_effects_chain.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// Live external effects — FULL-CHAIN transport suite (v1.3.0-beta5, bug B2).
//
// Tester report (beta4): random casing / glyph flips are enabled in settings,
// the checkbox stays checked, the code table is Unicode, the tray icon is ON —
// yet external applications receive perfectly normal text.
//
// tests/test_live_output_plan.cpp pins the DECISION (TextEngine → planOutput
// → the styled scratch buffer). This file pins the TRANSPORT behind it — the
// half of the chain the decision test never touched:
//
//   producer (hook-thread emulation)
//     → OutputItem{Edit, backspace, text}      (the exact struct main.cpp fills)
//     → ok::wrap::OutputRing                   (the REAL Vyukov SPSC ring, 1024)
//     → consumer thread                        (the REAL drain loop shape)
//     → ok::wrap::InlineEmitter::sendEdit      (the REAL shipped emitter,
//                                               shim-recorded SendInput batches)
//     → decoded screen text
//
// and compares it, keystroke corpus by corpus, against a direct-apply
// reference session running the identical producer code. Any corruption,
// reordering, truncation or silent drop in the transport shows up as a
// screen mismatch — mechanically, on every platform, no Windows needed.
//
// It also pins the GATE (bug B2's other half): liveGateBlocker()/gateActive()
// (LiveEffects.hpp) reproduce main.cpp's veto chain as a pure function, and
// every blocked state must yield byte-identical raw Vietnamese — the feature
// isolation contract, evaluated through the full transport.
//
// Build (mirrors ok_wrap_tests):
//   g++ -std=c++2b -O2 -Isrc/core -Itests -DOK_WRAP_NO_WIN32 -pthread \
//       tests/test_live_effects_chain.cpp src/core/win32_wrapper.cpp \
//       src/core/TextEngine.cpp src/core/ChaosEngine.cpp
//----------------------------------------------------------------------------
#include "win32_wrapper.hpp"     // shim flavor: OutputRing, InlineEmitter, okshim

#include "LiveEffects.hpp"
#include "TextEngine.hpp"

#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

namespace sh = okshim;

using namespace ok::text;
using namespace ok::effects;
using ok::wrap::InlineEmitter;
using ok::wrap::OutputItem;
using ok::wrap::OutputRing;

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

//---------------------------------------------------------------------------
// The producer: mirrors src/app/main.cpp::onHookEventImpl for one keystroke,
// with the SAME decision flow as tests/test_live_output_plan.cpp's AppPath
// (engine.process → replacementUtf16 → restore re-issue → planOutput). The
// only difference is the SINK:
//   * direct — apply (erase, text) to `screen` in place (reference), or
//   * ring   — publish an OutputItem exactly like the hook's producer does;
//              a real consumer thread drains it through the real
//              InlineEmitter into the shim's recorded SendInput batches.
//---------------------------------------------------------------------------
struct ChainSession {
    TextEngine engine;
    LiveEffects fx;
    bool active = false;
    std::wstring plain;     // engine ground truth (both sinks)
    std::wstring screen;    // direct sink result

    OutputRing* ring = nullptr;             // ring sink (nullptr = direct)
    std::uint64_t published = 0;            // ring sink: items pushed

    explicit ChainSession(Config config, EngineOptions options = {})
        : engine(options) {
        fx.configure(config);
        fx.sync();
        active = config.enabled;
    }

    void emit(std::size_t erase, const std::wstring& text,
              const std::wstring& plainText) {
        // The engine ground truth is bookkept identically in both sinks.
        assert(erase <= plain.size());
        plain.erase(plain.size() - erase);
        plain += plainText;
        if (ring != nullptr) {
            // Exactly main.cpp's producer fill: Kind::Edit + backspace +
            // UTF-16 text capped at kRingTextCap.
            assert(text.size() < ok::wrap::kRingTextCap);
            OutputItem it{};
            it.kind = OutputItem::Kind::Edit;
            it.backspace = static_cast<std::uint32_t>(erase);
            it.textLen = static_cast<std::uint32_t>(text.size());
            for (std::size_t i = 0; i < text.size(); ++i) { it.text[i] = text[i]; }
            it.text[text.size()] = L'\0';
            while (!ring->try_push(it)) { std::this_thread::yield(); }
            ++published;
        } else {
            assert(erase <= screen.size());
            screen.erase(screen.size() - erase);
            screen += text;
        }
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
        // Restore re-issue (hotfix §3 / D4).
        const bool reissue = (r.code == EngineCode::Restore ||
                              r.code == EngineCode::RestoreAndStartNewSession) &&
                             (in.kind == InputKind::Char || in.kind == InputKind::Space);
        if (reissue) {
            replacement.push_back(static_cast<wchar_t>(ch));
        }

        const std::wstring groundTruth = replacement;
        std::wstring scratch = replacement;
        const OutputPlan plan = planOutput(active, suppress, erase, scratch,
                                           kind, ch, fx);
        assert(!plan.suppress || plan.backspace != 0 || !scratch.empty());
        assert(!(plan.suppress && plan.nativePassThrough));

        if (plan.suppress) {
            // The app never sees the raw key: the plan's text lands instead.
            std::wstring truth = groundTruth;
            if (!suppress && (in.kind == InputKind::Char || in.kind == InputKind::Space)) {
                truth.assign(1, static_cast<wchar_t>(ch));
            }
            assert(truth.size() == scratch.size());   // BMP styling is 1:1
            emit(plan.backspace, scratch, truth);
        } else if (ch == U'\b') {
            // Raw backspace: the app deletes one char. Through the ring this
            // rides as the pure-erase edit (bs=1, empty text) — exactly the
            // shape sendBackspaces() uses on the shipped path.
            if (!plain.empty()) {
                if (ring != nullptr) {
                    emit(1, std::wstring(), std::wstring());
                } else {
                    plain.pop_back();
                    screen.pop_back();
                }
            }
        } else {
            // Native pass-through: the app receives the raw character. The
            // ring sink models that delivery as a UNICODE injection of the
            // same BMP unit — indistinguishable to the decoded screen.
            if (ring != nullptr) {
                emit(0, std::wstring(1, static_cast<wchar_t>(ch)),
                     std::wstring(1, static_cast<wchar_t>(ch)));
            } else {
                plain.push_back(static_cast<wchar_t>(ch));
                screen.push_back(static_cast<wchar_t>(ch));
            }
        }
        // Invariant: the ground truth and the direct screen never diverge in
        // length (the caret can never desync from the engine).
        if (ring == nullptr) { assert(plain.size() == screen.size()); }
    }

    void type(const std::u32string& keys) { for (char32_t ch : keys) { key(ch); } }

    void finishRing() {
        if (ring == nullptr) { return; }
        OutputItem sentinel{};
        sentinel.kind = OutputItem::Kind::None;
        while (!ring->try_push(sentinel)) { std::this_thread::yield(); }
    }
};

// The REAL consumer loop shape: drain the ring, hand every Edit to the real
// InlineEmitter (shim-recorded SendInput), stop at the sentinel.
void consumerMain(OutputRing& ring, InlineEmitter& em) {
    OutputItem it{};
    for (;;) {
        if (ring.try_pop(it)) {
            if (it.kind == OutputItem::Kind::None) { return; }
            if (it.kind == OutputItem::Kind::Edit) {
                em.sendEdit(it.backspace, it.text, it.textLen);
            }
            continue;
        }
        std::this_thread::yield();
    }
}

// Decode the recorded SendInput batches into the text the app receives:
// key-DOWN events only (ups are the mirror), VK_BACK deletes one char, a
// KEYEVENTF_UNICODE down inserts its scan-code unit. Also enforces the
// self-injection tag on EVERY input — an untagged event would re-enter the
// hook and loop.
std::wstring decodeShimScreen() {
    std::wstring screen;
    for (const auto& batch : sh::g_state.calls) {
        for (const auto& in : batch) {
            assert(in.type == sh::INPUT_KEYBOARD);
            assert(in.ki.dwExtraInfo == ok::wrap::kSelfInjectedExtraInfo);
            if ((in.ki.dwFlags & sh::KEYEVENTF_KEYUP) != 0) { continue; }
            if ((in.ki.dwFlags & sh::KEYEVENTF_UNICODE) != 0) {
                screen.push_back(static_cast<wchar_t>(in.ki.wScan));
            } else if (in.ki.wVk == sh::VK_BACK) {
                assert(!screen.empty());   // over-backspace = corrupted chain
                if (!screen.empty()) { screen.pop_back(); }
            }
        }
    }
    return screen;
}

// Run one corpus through the full ring→consumer→emitter transport.
std::wstring runRingSession(const Config& config, const std::u32string& corpus,
                            std::wstring* plainOut, std::uint64_t* publishedOut) {
    sh::resetState();
    OutputRing ring;
    InlineEmitter emitter;
    ChainSession session(config);
    session.ring = &ring;
    std::thread consumer(consumerMain, std::ref(ring), std::ref(emitter));
    session.type(corpus);
    session.finishRing();
    consumer.join();
    if (plainOut != nullptr) { *plainOut = session.plain; }
    if (publishedOut != nullptr) { *publishedOut = session.published; }
    return decodeShimScreen();
}

std::wstring runDirectSession(const Config& config, const std::u32string& corpus,
                              std::wstring* plainOut) {
    ChainSession session(config);
    session.type(corpus);
    if (plainOut != nullptr) { *plainOut = session.plain; }
    return session.screen;
}

Config effectsOff() { return Config{}; }
Config randomCaseFull() {
    Config c;
    c.enabled = true;
    c.randomCase = true;
    c.glyph = Glyph::None;
    c.intensity = 100;
    return c;
}
Config glyphFlipFull() {
    Config c;
    c.enabled = true;
    c.randomCase = false;
    c.glyph = Glyph::UpsideDown;
    c.intensity = 100;
    return c;
}

// The corpora: composer-verified Telex (the beta3/beta4 suites type these
// exact strings through the real engine — "tieengs vieetj " → "tiếng việt ").
const std::u32string kSimple = U"tieengs vieetj ";
const std::u32string kMixed  = U"tieengs vieetj nhanh123 wolf wifi \b\b\bas ";
const std::u32string kVniish = U"as aR af ax aj ";

void testGateBlockerTable() {
    // The pure gate: first veto wins, in the hook's evaluation order.
    assert(liveGateBlocker(true, false, true, true) == GateBlocker::None);
    assert(gateActive(true, false, true, true));
    // IME off beats everything.
    assert(liveGateBlocker(false, true, true, true) == GateBlocker::ImeDisabled);
    assert(liveGateBlocker(false, false, false, false) == GateBlocker::ImeDisabled);
    // Exclusion beats the channel switches.
    assert(liveGateBlocker(true, true, true, true) == GateBlocker::AppExcluded);
    assert(liveGateBlocker(true, true, false, false) == GateBlocker::AppExcluded);
    // Master off beats the code-table veto.
    assert(liveGateBlocker(true, false, false, true) == GateBlocker::MasterOff);
    assert(liveGateBlocker(true, false, false, false) == GateBlocker::MasterOff);
    // Unicode table is the last gate.
    assert(liveGateBlocker(true, false, true, false) == GateBlocker::NonUnicodeTable);
    // gateActive is exactly (blocker == None) over the whole input space.
    for (int mask = 0; mask < 16; ++mask) {
        const bool ime = (mask & 1) != 0, excl = (mask & 2) != 0;
        const bool master = (mask & 4) != 0, uni = (mask & 8) != 0;
        assert(gateActive(ime, excl, master, uni) ==
               (liveGateBlocker(ime, excl, master, uni) == GateBlocker::None));
    }
    std::cout << "  [PASS] liveGateBlocker: the hook's veto chain as a pure"
                 " function (first veto wins, full input space)\n";
}

void testTransportMatchesDirectEffectsOff() {
    // Feature isolation through the FULL transport: effects off → the app
    // receives byte-identical raw Vietnamese, whichever sink produced it.
    std::wstring plainRing, plainDirect, plainRef;
    const std::wstring screenRing =
        runRingSession(effectsOff(), kMixed, &plainRing, nullptr);
    const std::wstring screenDirect =
        runDirectSession(effectsOff(), kMixed, &plainDirect);
    assert(screenRing == screenDirect);
    assert(plainRing == plainDirect);
    // And the ground truth is the composer-verified Vietnamese.
    runDirectSession(effectsOff(), kSimple, &plainRef);
    assert(plainRef == L"tiếng việt ");
    const std::wstring simpleRing = runRingSession(effectsOff(), kSimple, nullptr, nullptr);
    assert(simpleRing == std::wstring(L"tiếng việt "));
    std::cout << "  [PASS] effects off: ring→consumer→emitter transport is"
                 " byte-identical to direct apply (\"tiếng việt \")\n";
}

void testTransportMatchesDirectStyled() {
    // The styled path through the full transport: random casing at 100 %
    // intensity must arrive EXACTLY as the direct reference styled it — same
    // characters, same case flips, same length — and at least one flip must
    // have happened (otherwise the "effects do nothing" report would be true).
    for (const std::u32string& corpus : {kSimple, kMixed, kVniish}) {
        std::wstring plainRing, plainDirect;
        std::uint64_t published = 0;
        const std::wstring ring =
            runRingSession(randomCaseFull(), corpus, &plainRing, &published);
        const std::wstring direct =
            runDirectSession(randomCaseFull(), corpus, &plainDirect);
        assert(ring == direct);
        assert(plainRing == plainDirect);
        assert(ring.size() == plainRing.size());   // erase counts stayed correct
        assert(published > 0);                     // the ring really carried it
        bool flipped = false;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            if (ring[i] != plainRing[i]) { flipped = true; }
        }
        assert(flipped);                           // 100 % intensity: SOMETHING changed
    }
    std::cout << "  [PASS] random case 100 %: styled text survives the ring +"
                 " consumer + emitter byte-for-byte, flips present\n";

    // Glyph flip (UpsideDown) through the transport: every flipped char must
    // equal the ChaosEngine's mapping of the ground-truth char.
    std::wstring plainG;
    const std::wstring ringG = runRingSession(glyphFlipFull(), kSimple, &plainG, nullptr);
    const std::wstring directG = runDirectSession(glyphFlipFull(), kSimple, nullptr);
    assert(ringG == directG);
    assert(ringG.size() == plainG.size());
    bool anyFlipped = false;
    for (std::size_t i = 0; i < ringG.size(); ++i) {
        const char32_t expected = ok::chaos::ChaosEngine::getFlippedVerticalGlyph(
            static_cast<char32_t>(plainG[i]));
        assert(ringG[i] == static_cast<wchar_t>(expected));
        if (ringG[i] != plainG[i]) { anyFlipped = true; }
    }
    assert(anyFlipped);
    std::cout << "  [PASS] upside-down glyphs: every emitted char is the"
                 " ChaosEngine mapping, transport intact\n";
}

void testGateBlockedStatesStayRaw() {
    // Bug B2's contract: whenever ANY gate vetoes (blocker != None), the
    // app receives the raw engine text through the full transport — no
    // partial styling, no dropped chars.
    struct Blocked { const char* name; bool ime, excl, master, uni; };
    const Blocked blocked[] = {
        {"IME off",       false, false, true,  true },
        {"app excluded",  true,  true,  true,  true },
        {"master off",    true,  false, false, true },
        {"non-Unicode",   true,  false, true,  false},
    };
    const std::wstring rawRef = runDirectSession(effectsOff(), kSimple, nullptr);
    for (const Blocked& b : blocked) {
        assert(!gateActive(b.ime, b.excl, b.master, b.uni));
        // gateActive == false is exactly planOutput(active=false): even with
        // the channel CONFIG enabled, a vetoed gate emits raw Vietnamese.
        ChainSession vetoed(randomCaseFull());
        vetoed.active = gateActive(b.ime, b.excl, b.master, b.uni);   // false
        vetoed.type(kSimple);
        assert(vetoed.screen == rawRef);
        assert(vetoed.screen == std::wstring(L"tiếng việt "));
    }
    std::cout << "  [PASS] every vetoed gate (IME off / excluded / master off /"
                 " non-Unicode) emits raw Vietnamese\n";
}

void testEmitterEditShapes() {
    // The two edit shapes the chain produces, verified on the RECORDED
    // SendInput batches of the real InlineEmitter:
    //   * pure insert (bs=0): UNICODE pairs only, NO VK_BACK inputs — the
    //     fast path TsfComposer::applyDelta and sendEdit both rely on;
    //   * pure erase (bs>0, empty text): VK_BACK pairs only.
    sh::resetState();
    OutputRing ring;
    InlineEmitter emitter;
    ChainSession session(randomCaseFull());
    session.ring = &ring;
    std::thread consumer(consumerMain, std::ref(ring), std::ref(emitter));
    session.type(kMixed);            // contains raw backspaces (\b\b\b)
    session.finishRing();
    consumer.join();

    bool sawPureInsert = false, sawPureErase = false;
    for (const auto& batch : sh::g_state.calls) {
        bool hasBack = false, hasUnicode = false;
        for (const auto& in : batch) {
            if ((in.ki.dwFlags & sh::KEYEVENTF_KEYUP) != 0) { continue; }
            if ((in.ki.dwFlags & sh::KEYEVENTF_UNICODE) != 0) { hasUnicode = true; }
            else if (in.ki.wVk == sh::VK_BACK) { hasBack = true; }
        }
        if (hasUnicode && !hasBack) { sawPureInsert = true; }
        if (hasBack && !hasUnicode) { sawPureErase = true; }
        // Backspaces always precede the replacement text within one batch
        // (the shipped emitter's ordering contract).
        bool seenUnicode = false, orderOk = true;
        for (const auto& in : batch) {
            if ((in.ki.dwFlags & sh::KEYEVENTF_KEYUP) != 0) { continue; }
            if ((in.ki.dwFlags & sh::KEYEVENTF_UNICODE) != 0) { seenUnicode = true; }
            else if (in.ki.wVk == sh::VK_BACK && seenUnicode) { orderOk = false; }
        }
        assert(orderOk);
    }
    assert(sawPureInsert);
    assert(sawPureErase);
    assert(emitter.sendInputCalls() == sh::g_state.calls.size());
    std::cout << "  [PASS] emitter edit shapes: pure inserts carry no VK_BACK,"
                 " erases precede text, every batch recorded\n";
}

void testTransportUnderRepetition() {
    // A longer session (the corpus repeated) through one consumer thread:
    // the SPSC ring + emitter must not lose, duplicate or reorder a single
    // edit — the decoded screen still equals the direct reference.
    std::u32string long_corpus;
    for (int i = 0; i < 60; ++i) { long_corpus += kMixed; }
    std::wstring plainRing, plainDirect;
    std::uint64_t published = 0;
    const std::wstring ring =
        runRingSession(randomCaseFull(), long_corpus, &plainRing, &published);
    const std::wstring direct =
        runDirectSession(randomCaseFull(), long_corpus, &plainDirect);
    assert(ring == direct);
    assert(plainRing == plainDirect);
    assert(published > 100);
    std::cout << "  [PASS] 60x corpus through ring + consumer thread: zero"
                 " loss, zero reorder (" << published << " edits)\n";
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout << "=== Running Live-Effects FULL-CHAIN Suite (bug B2) ===\n";
    testGateBlockerTable();
    testTransportMatchesDirectEffectsOff();
    testTransportMatchesDirectStyled();
    testGateBlockedStatesStayRaw();
    testEmitterEditShapes();
    testTransportUnderRepetition();
    std::cout << "=== ALL LIVE-EFFECTS CHAIN TESTS PASSED ===\n";
    return 0;
}
