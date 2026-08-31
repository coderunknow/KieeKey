//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.0.2 - refactored and completed logic
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
// File: tests/test_hotfix.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// tests/test_hotfix.cpp — regression tests for the user-reported fixes:
//
//   1. resumeFromText(): after the user deletes a chunk of a partially-typed
//      raw word (mouse/selection) and clicks back, the engine re-syncs to the
//      visible word so retyping composes ("chugsn" -> delete "gsn" -> click ->
//      "sng" must yield "chúng", not "chusng").
//
//   2. CtrlShiftChord: the global Ctrl+Shift toggle fires ONLY on a clean
//      bare chord; Ctrl+Shift+<key> application shortcuts never toggle (this
//      was silently turning the IME off during text selection / Save As…).
//
//   3. Tone-key Restore re-issue: after an engine Restore the hook re-sends
//      the typed key ('chào'+f -> 'chaof', 'chà'+BS then f -> 'chaf',
//      'cas'+s -> 'cas'), fixing the "I must press the tone key twice" and
//      "tone marks don't come back after delete-all" reports. Also locks the
//      edge cases (double/triple tone keys, Space after a toggle, delete-all
//      and retype) against the clean-room oracle.
//
//   4 (v3.1, P0). D1/D2/D3/D4 contract fixes found by the 3-way benchmark:
//      D4 space re-issue after a wrong-spelling Restore ('arbit hối đoái'),
//      D2 over-backspace policy (backspaceCount ≤ committed text; the
//      visible account mirrors the document), D2 root fix (standalone
//      ư/ơ + tone-restored vowels never vanish from the emitted
//      replacement), D3 macro expansion end-to-end.
//----------------------------------------------------------------------------
#include "TextEngine.hpp"
#include "CtrlShiftChord.hpp"
#include "vi_oracle.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace ok::text;

static std::string u8(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) {
        const char32_t cp = static_cast<char32_t>(c);
        if (cp < 0x80) s += static_cast<char>(cp);
        else if (cp < 0x800) {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return s;
}

// ---- minimal app-like consumer: feed keys, apply engine results ----
struct Feed {
    TextEngine eng;
    std::wstring visible;

    explicit Feed(const EngineOptions& eo) : eng(eo) {}

    void key(char32_t c, bool caps = false) {
        TextInput in;
        in.kind = InputKind::Char; in.ch = c; in.isCaps = caps;
        const auto& r = eng.process(in);
        if (!r.consumed() || r.code == EngineCode::ReplaceMacro) {
            visible += (wchar_t)(caps && c >= 'a' && c <= 'z' ? c - 32 : c);
            return;
        }
        const std::wstring rep = eng.replacementUtf16(r);
        size_t b = r.backspaceCount > visible.size() ? visible.size() : r.backspaceCount;
        visible.erase(visible.size() - b, b);
        visible += rep;
        // Legacy-hook consumer semantics (fixed in the shipped app): after a
        // Restore the engine reverted the word to its bare spelling and the
        // hook re-sends the typed key ('chào'+f -> 'chaof', 'cas'+s -> 'cas').
        if (r.code == EngineCode::Restore || r.code == EngineCode::RestoreAndStartNewSession) {
            visible += (wchar_t)(caps && c >= 'a' && c <= 'z' ? c - 32 : c);
        }
    }
    void bs() {
        TextInput in; in.kind = InputKind::Backspace;
        const auto& r = eng.process(in);
        if (!r.consumed()) { if (!visible.empty()) visible.pop_back(); return; }
        const std::wstring rep = eng.replacementUtf16(r);
        size_t b = r.backspaceCount > visible.size() ? visible.size() : r.backspaceCount;
        visible.erase(visible.size() - b, b);
        visible += rep;
    }
    void space() {
        // The app feeds Space as InputKind::Space (never as a Char event).
        TextInput in; in.kind = InputKind::Space;
        const auto& r = eng.process(in);
        if (!r.consumed() || r.code == EngineCode::ReplaceMacro) {
            visible += L' ';
            return;
        }
        const std::wstring rep = eng.replacementUtf16(r);
        size_t b = r.backspaceCount > visible.size() ? visible.size() : r.backspaceCount;
        visible.erase(visible.size() - b, b);
        visible += rep;
        // D4 (v3.1): the space is RE-ISSUED after a Restore — exactly like
        // the typed-character re-issue. Before this fix a wrong-spelling
        // word that triggered Restore on the space ate the space
        // ('arbit hối đoái' rendered 'arbithối đoái').
        if (r.code == EngineCode::Restore || r.code == EngineCode::RestoreAndStartNewSession) {
            visible += L' ';
        }
    }
    void mouseDown() {
        TextInput in; in.kind = InputKind::MouseDown;
        static_cast<void>(eng.process(in));
    }
    // App-level edit the ENGINE never sees (mouse/keyboard selection+Delete):
    // the visible text changes directly, exactly like a real editor.
    void appDelete(size_t n) {
        if (n > visible.size()) n = visible.size();
        visible.erase(visible.size() - n);
    }
};

static int failures = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        /* if constexpr: avoids C4127 (constant condition) under /W4 /WX */    \
        if constexpr (cond) { std::printf("  ok   %s\n", msg); }               \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                  \
    } while (0)

int main() {
    EngineOptions eo;
    eo.inputMethod = InputMethod::Telex;
    eo.checkSpelling = true;
    eo.restoreIfWrongSpelling = true;
    eo.useMacro = false;

    //===================================================================
    // 1. resumeFromText — the "chusng" fix
    //===================================================================
    std::printf("[1] resumeFromText (user-reported \"chusng\")\n");

    {
        // Plain backspace path already worked: chugsn + 3xBS + sng -> chúng.
        Feed f(eo);
        for (char c : std::string("chugsn")) f.key(c);
        f.bs(); f.bs(); f.bs();
        for (char c : std::string("sng")) f.key(c);
        CHECK(u8(f.visible) == "chúng", "backspace path: chugsn+3BS+sng -> chúng");
    }

    {
        // Mouse-selection delete path (previously "chusng"): the app deleted
        // gsn (engine unaware), the user clicked (MouseDown resets), the app
        // re-syncs the engine to the visible word "chu".
        Feed f(eo);
        for (char c : std::string("chugsn")) f.key(c);
        f.appDelete(3);                     // app deletes "gsn" -> "chu"
        f.mouseDown();                      // engine resets (as today)
        const bool resumed = f.eng.resumeFromText(L"chu");   // new app step
        CHECK(resumed, "resumeFromText(\\\"chu\\\") returns true");
        for (char c : std::string("sng")) f.key(c);
        CHECK(u8(f.visible) == "chúng", "selection-delete path: resume(\"chu\")+sng -> chúng");
    }

    {
        // Same for a longer base word.
        Feed f(eo);
        for (char c : std::string("khong")) f.key(c);
        f.mouseDown();
        static_cast<void>(f.eng.resumeFromText(L"khong"));
        for (char c : std::string("s")) f.key(c);   // khong+s -> khóng
        CHECK(u8(f.visible) == "khóng", "resume(\"khong\")+s -> khóng");
    }

    {
        // Non-ASCII word (already composed on screen) is NOT resumable ->
        // returns false and leaves a clean session (raw typing follows).
        Feed f(eo);
        f.mouseDown();
        const bool r1 = f.eng.resumeFromText(L"chúng");   // composed
        CHECK(!r1, "resumeFromText(composed \\\"chúng\\\") returns false");
        for (char c : std::string("sng")) f.key(c);
        CHECK(u8(f.visible) == "sng", "non-resumable context -> raw typing (safe)");
    }

    {
        // Empty / punctuation word is not resumable.
        Feed f(eo);
        f.mouseDown();
        CHECK(!f.eng.resumeFromText(L""), "resumeFromText(empty) returns false");
        CHECK(!f.eng.resumeFromText(L"chu!"), "resumeFromText(\\\"chu!\\\") returns false");
    }

    //===================================================================
    // 2. CtrlShiftChord — the "turns off with unknown causes" fix
    //===================================================================
    std::printf("[2] CtrlShiftChord (clean-chord-only toggle)\n");
    using Action = ok::hotkey::CtrlShiftChord::Action;

    {
        // Bare Ctrl+Shift pressed & released, nothing between -> toggles once.
        ok::hotkey::CtrlShiftChord c;
        Action a;
        a = c.onKey(true,  false, false, true);    // Ctrl down
        assert(a == Action::None);
        a = c.onKey(false, true,  false, true);    // Shift down (chord starts)
        assert(a == Action::None);
        a = c.onKey(false, true,  false, false);   // Shift up (both released)
        CHECK(a == Action::Toggle, "bare Ctrl+Shift releases -> Toggle");
        a = c.onKey(true,  false, false, false);   // Ctrl up
        assert(a == Action::None);
    }

    {
        // Ctrl+Shift+S (Save As) must NOT toggle.
        ok::hotkey::CtrlShiftChord c;
        Action a;
        c.onKey(true,  false, false, true);        // Ctrl down
        c.onKey(false, true,  false, true);        // Shift down
        a = c.onKey(false, false, true,  true);    // S down -> cancels
        assert(a == Action::None);
        a = c.onKey(false, false, true,  false);   // S up
        assert(a == Action::None);
        a = c.onKey(false, true,  false, false);   // Shift up
        CHECK(a == Action::None, "Ctrl+Shift+S does NOT toggle");
        c.onKey(true,  false, false, false);       // Ctrl up
    }

    {
        // Ctrl+Shift+arrows (text selection) must NOT toggle, repeatedly.
        ok::hotkey::CtrlShiftChord c;
        c.onKey(true,  false, false, true);        // Ctrl down
        c.onKey(false, true,  false, true);        // Shift down
        c.onKey(false, false, true,  true);        // Left down
        c.onKey(false, false, true,  false);       // Left up
        c.onKey(false, false, true,  true);        // Right down
        c.onKey(false, false, true,  false);       // Right up
        const Action a = c.onKey(false, true, false, false);   // Shift up
        CHECK(a == Action::None, "Ctrl+Shift+arrows (selection) does NOT toggle");
        c.onKey(true, false, false, false);
    }

    {
        // Ctrl+Shift+Esc (task manager) does NOT toggle.
        ok::hotkey::CtrlShiftChord c;
        c.onKey(true,  false, false, true);
        c.onKey(false, true,  false, true);
        c.onKey(false, false, true,  true);        // Esc down
        const Action a = c.onKey(false, true, false, false);   // Shift up
        CHECK(a == Action::None, "Ctrl+Shift+Esc does NOT toggle");
    }

    {
        // After a cancelled chord, the NEXT clean chord still toggles.
        ok::hotkey::CtrlShiftChord c;
        c.onKey(true,  false, false, true);        // Ctrl down
        c.onKey(false, true,  false, true);        // Shift down
        c.onKey(false, false, true,  true);        // S down (cancel)
        c.onKey(false, false, true,  false);
        c.onKey(false, true,  false, false);       // Shift up (cancelled)
        c.onKey(true,  false, false, false);       // Ctrl up
        c.onKey(true,  false, false, true);        // fresh bare chord
        c.onKey(false, true,  false, true);
        const Action a = c.onKey(false, true, false, false);
        CHECK(a == Action::Toggle, "clean chord after a cancelled one still toggles");
    }

    {
        // Single modifier taps (Ctrl alone, Shift alone) never toggle.
        ok::hotkey::CtrlShiftChord c;
        c.onKey(true,  false, false, true);        // Ctrl down
        c.onKey(true,  false, false, false);       // Ctrl up
        c.onKey(false, true,  false, true);        // Shift down
        const Action a = c.onKey(false, true, false, false);   // Shift up
        CHECK(a == Action::None, "lone Ctrl / Shift taps do NOT toggle");
    }

    {
        // Ctrl+Shift held, then a third modifier (Alt) -> no toggle.
        ok::hotkey::CtrlShiftChord c;
        c.onKey(true,  false, false, true);
        c.onKey(false, true,  false, true);
        c.onKey(false, false, true,  true);        // Alt down (non-shift/ctrl key)
        const Action a = c.onKey(false, true, false, false);
        CHECK(a == Action::None, "Ctrl+Shift+Alt does NOT toggle");
    }

    //===================================================================
    // 3. Tone-key Restore re-issue — the "chào -> chaof", "chàf -> chaf",
    //    and "tone marks not coming back" fixes
    //
    // Root cause: on a duplicate/toggling tone key the engine answers with a
    // Restore (revert the word to its bare spelling: backspace + replacement).
    // OpenKey 2.0.5's hook then RE-SENDS the typed key, so the tone is simply
    // toggled OFF and the key still lands: 'chào'+f -> 'chaof'. KieeKey's
    // consumer previously dropped the typed key after the Restore, so one
    // press deleted the tone mark and the character ('chao', 'cha', 'ca') —
    // the user had to press twice. All sequences below are verified equal to
    // the unmodified 2.0.5 engine and the clean-room oracle.
    //===================================================================
    std::printf("[3] tone-key Restore re-issue (chào+BS+f / chaof / cass)\\n");

    {
        // Bug 1: 'chào' (chaof) + one more 'f' must become 'chaof', not 'chao'.
        Feed f(eo);
        for (char c : std::string("chaof")) f.key(c);
        CHECK(u8(f.visible) == "chào", "chaof -> chào");
        f.key('f');
        CHECK(u8(f.visible) == "chaof", "chaof+f -> chaof (one press, not two)");
        f.key('f');
        CHECK(u8(f.visible) == "chaoff", "chaof+f+f -> chaoff (repeat still types)");
    }

    {
        // Bug 3: 'chào' -> Backspace (deletes 'o') -> 'f' must be 'chaf',
        // not 'chàf' and not 'cha'.
        Feed f(eo);
        for (char c : std::string("chaof")) f.key(c);
        f.bs();
        CHECK(u8(f.visible) == "chà", "chaof+BS -> chà");
        f.key('f');
        CHECK(u8(f.visible) == "chaf", "chà+f -> chaf (tone toggles off, f lands)");
        f.key('f');
        CHECK(u8(f.visible) == "chaff", "chaf+f -> chaff");
    }

    {
        // Tone-toggle family: 'cas'+s -> 'cas' (not 'ca'), then repeats.
        Feed f(eo);
        for (char c : std::string("cas")) f.key(c);
        CHECK(u8(f.visible) == "cá", "cas -> cá");
        f.key('s');
        CHECK(u8(f.visible) == "cas", "cas+s -> cas (tone toggled off, s typed)");
        f.key('s');
        CHECK(u8(f.visible) == "cass", "cass+s -> cass");
        f.key('s');
        CHECK(u8(f.visible) == "casss", "casss+s -> casss");
    }

    {
        // Space/word-break semantics are UNCHANGED by the re-issue fix: only
        // the typed CHARACTER key is re-sent after a Restore; a Space after a
        // toggle still yields exactly one space (no duplicate, no re-issue),
        // exactly matching 2.0.5 and the oracle.
        Feed f(eo);
        for (char c : std::string("cas")) f.key(c);
        f.key('s');                          // tone toggle -> cas (temp-disabled)
        CHECK(u8(f.visible) == "cas", "cas+s -> cas (toggle state before space)");
        f.space();
        CHECK(u8(f.visible) == "cas ", "space after toggle -> single space (2.0.5 semantics)");
        for (char c : std::string("chaof")) f.key(c);
        CHECK(u8(f.visible) == "cas chào", "post-toggle space + new word composes normally");
    }

    {
        // Bug 2: delete ALL characters then retype — tones must come back.
        Feed f(eo);
        for (char c : std::string("chaof")) f.key(c);
        CHECK(u8(f.visible) == "chào", "chaof -> chào (before delete)");
        for (int i = 0; i < 5; ++i) f.bs();   // delete all
        CHECK(u8(f.visible).empty(), "after BSx5 the text is empty");
        for (char c : std::string("chaof")) f.key(c);
        CHECK(u8(f.visible) == "chào", "delete-all + retype chaof -> chào (tones return)");
    }

    {
        // Bug 2 variant: tone-toggle BEFORE delete-all (the engine is in the
        // post-Restore state) then retype — still composes.
        Feed f(eo);
        for (char c : std::string("chaof")) f.key(c);
        f.key('f');                          // toggle -> chaof (re-issued)
        CHECK(u8(f.visible) == "chaof", "chaof+f -> chaof (toggle state)");
        for (int i = 0; i < 5; ++i) f.bs();
        CHECK(u8(f.visible).empty(), "after BSx5 the text is empty (toggle case)");
        for (char c : std::string("chaof")) f.key(c);
        CHECK(u8(f.visible) == "chào", "toggle+delete-all + retype chaof -> chào");
    }

    {
        // App-level delete-all (mouse selection + appDelete, engine unaware)
        // then retype — the resumeFromText path must leave a clean session.
        Feed f(eo);
        for (char c : std::string("chaof")) f.key(c);
        f.appDelete(5);                      // app cleared the whole word
        f.mouseDown();                       // engine word break
        static_cast<void>(f.eng.resumeFromText(L""));   // app re-syncs to empty
        for (char c : std::string("chaof")) f.key(c);
        CHECK(u8(f.visible) == "chào", "select-all+delete + retype chaof -> chào");
    }

    {
        // Mixed real flow: toggle, space, another word with its own toggle.
        Feed f(eo);
        for (char c : std::string("chaof")) f.key(c);
        f.key('f');                          // chaof
        f.space();                           // space (word break)
        for (char c : std::string("cas")) f.key(c);
        f.key('s');                          // cas
        CHECK(u8(f.visible) == "chaof cas", "chaof cas (mixed toggle + space + toggle)");
    }

    {
        // Bug 4 (found by the three-engine benchmark, ASAN-verified): typing a
        // word longer than the 32-char word buffer, backspacing it (the undo
        // history restore fills the fixed-size scratch with 32 entries), then
        // typing another long word and hitting space used to overflow
        // pushTypingState's fixed array (saveWord did not clear the leftover
        // scratch and its exact-equality flush could never fire). This is the
        // "typing and deleting quickly" crash path — must be a clean word.
        Feed f(eo);
        const std::string longw = "abcdefghijklmnopqrstuvwxyzabcdefghijklmn";  // 40 chars
        for (char c : longw) f.key(c);
        f.space();                           // commit the long word
        CHECK(f.visible.size() == 41, "long word + space committed (40 + 1)");
        f.bs();                              // restoreLastTypingState -> 32-entry scratch
        CHECK(f.visible.size() == 40, "backspace after long word deletes the space");
        for (char c : longw) f.key(c);
        f.space();                           // was: OOB write in pushTypingState
        CHECK(f.visible.size() == 81, "long word + backspace + long word survives (no OOB)");
    }

    //=========================================================================
    // §5 (v3.1, P0) — D1/D2/D3/D4 contract fixes from the 3-way benchmark
    //=========================================================================
    std::printf("\n--- §5 P0 contract fixes (D1-D4) ---\n");

    {
        // D4 — the flagship repro: 'arbit hoi doai ' must render with its
        // spaces. Pre-fix: the wrong-spelling Restore on the space ate the
        // space ('arbithối đoái'). The oracle must produce the same text
        // under the same consumer contract.
        Feed f(eo);
        orel::Oracle ora;   // same defaults (Telex, spelling, restore)
        std::wstring ot;
        auto oraKey = [&](char32_t c) {
            orel::Event ev; ev.kind = orel::Kind::Char; ev.ch = c;
            return ora.process(ev);
        };
        auto oraSpace = [&]() {
            orel::Event ev; ev.kind = orel::Kind::Space;
            const auto& r = ora.process(ev);
            std::wstring out;
            if (r.code == orel::Code::ReplaceMacro) {
                for (std::uint32_t v : r.macroExpansion) out += static_cast<wchar_t>(v);
            } else if (r.consumed()) {
                out += r.replacement;
                if (r.code == orel::Code::Restore || r.code == orel::Code::RestoreAndStartNewSession) {
                    out += L' ';   // D4 re-issue
                }
            } else {
                out += L' ';
            }
            // apply the erase the engine/oracle asked for
            const std::size_t bs = std::min<std::size_t>(r.backspaceCount, ot.size());
            ot.erase(ot.size() - bs, bs);
            return out;
        };
        const char* stream = "arbit hooir ddoais ";
        for (const char* p = stream; *p; ++p) {
            if (*p == ' ') { f.space(); ot += oraSpace(); }
            else {
                f.key(static_cast<char32_t>(*p));
                const auto& r = oraKey(*p);
                if (!r.consumed()) { ot += static_cast<wchar_t>(*p); }
                else if (r.code != orel::Code::ReplaceMacro) {
                    const std::size_t bs = std::min<std::size_t>(r.backspaceCount, ot.size());
                    ot.erase(ot.size() - bs, bs);
                    ot += r.replacement;
                    if (r.code == orel::Code::Restore || r.code == orel::Code::RestoreAndStartNewSession) {
                        ot += static_cast<wchar_t>(*p);
                    }
                }
            }
        }
        CHECK(u8(f.visible) == "arbit hổ" "i đoá" "i ",
              "D4: 'arbit hooir ddoais ' -> 'arbit hối đoái ' (space kept after Restore)");
        CHECK(f.visible == ot, "D4: engine == clean-room oracle under the same contract");
        CHECK(f.eng.visibleAccount() == f.visible.size(),
              "D4: visible account == committed text (D2 invariant)");
    }

    {
        // D2 root fix — tone-toggle restore keeps the mark visible:
        // 'asaa' walks á -> ấ -> (hat toggled off) 'á', and the re-issued
        // 'a' lands: "áa". Pre-fix the restored entry was emitted raw and
        // resolved to 0, so the 'á' silently vanished ('aa' then 'a...').
        Feed f(eo);
        for (char c : std::string("asaa")) f.key(c);
        CHECK(u8(f.visible) == "áa",
              "D2: 'asaa' -> 'áa' (mark survives the tone-toggle restore)");
        CHECK(f.eng.visibleAccount() == f.visible.size(),
              "D2: account matches the document after the toggle family");
    }

    {
        // D2 root fix — standalone ư survives later transforms:
        // 'wiaas' (w -> ư standalone, aa -> â, s -> mark, then the
        // wrong-spelling restore on space) must keep every character the
        // engine counts: pre-fix 'ư' vanished mid-word and the following
        // restore over-backspaced (the 1,670-event mega finding).
        Feed f(eo);
        for (char c : std::string("wiaas")) f.key(c);
        f.space();
        CHECK(u8(f.visible) == "wiaas ",
              "D2+D4: 'wiaas ' -> 'wiaas ' (no vanished chars, space kept)");
        CHECK(f.eng.visibleAccount() == f.visible.size(),
              "D2: account matches the document after the standalone family");
    }

    {
        // D2 clamp + policy — replay the FROZEN over-backspace reproducers
        // (the exact op streams from the 1,670-event mega finding, saved in
        // tests/repros/) under the v3.1 consumer contract: no event may
        // request more backspaces than the committed text, the visible
        // account must mirror the document after EVERY event, and the
        // engine must stay in lockstep with the clean-room oracle.
        struct ReproDef { const char* ops; InputMethod method; bool macro; };
        static const ReproDef repros[] = {
            {"wiaas ",   InputMethod::Telex, false},   // suite 1 (standalone ư drop)
            {"wiaaj  ",  InputMethod::Telex, false},   // suite 1
            {"weerrte ", InputMethod::Telex, true},    // suite 10 (macro bs)
            {"[^a^afd4  pcp", InputMethod::Telex, true}, // suite 3 (backspace)
        };
        // Harness macro table (same content as mega_correctness gMacros).
        static const std::vector<std::pair<std::vector<std::uint32_t>, std::wstring>> macros = {
            {{'O','K'}, L"\u0111\u01B0\u1EE3c"},
            {{'V','C','L'}, L"v\u00E3i"},
            {{'B','T'}, L"b\u00ECnh th\u01B0\u1EDDng"},
            {{'X','L'}, L"xin l\u1ED7i"},
            {{'A','B','C'}, L"a b c"},
            {{'U','O','N','G'}, L"u\u1ED1ng"},
            {{'T','E','L','E','X'}, L"b\u1ED9 g\u00F5 ti\u1EBFng vi\u1EC7t"},
        };
        auto resolver =
            [](const std::vector<std::uint32_t>& key, std::vector<std::uint32_t>& data) {
                for (const auto& m : macros) {
                    if (key == m.first) {
                        for (wchar_t c : m.second) { data.push_back(static_cast<std::uint32_t>(c)); }
                        return true;
                    }
                }
                return false;
            };
        auto expansionOf = [](const std::vector<std::uint32_t>& v) {
            std::wstring s;
            for (std::uint32_t c : v) { s += static_cast<wchar_t>(c); }
            return s;
        };
        bool allOk = true;
        for (const auto& rd : repros) {
            EngineOptions ro = eo;
            ro.inputMethod = rd.method;
            ro.useMacro = rd.macro;
            TextEngine eng(ro);
            orel::Oracle ora;   // mirrors ro (same defaults + method)
            orel::Options oo{};
            oo.method = static_cast<orel::Method>(static_cast<int>(rd.method));
            oo.useMacro = rd.macro;
            ora.setOptions(oo);
            eng.setMacroResolver(resolver);
            ora.setMacroResolver(resolver);
            std::wstring et, ot;
            bool caps = false;
            auto apply = [&](std::size_t bs, const std::wstring& rep, std::wstring& t) {
                if (bs > t.size()) { allOk = false; }   // over-backspace contract
                bs = std::min(bs, t.size());
                t.erase(t.size() - bs, bs);
                t += rep;
            };
            for (const char* p = rd.ops; *p; ++p) {
                if (*p == '^') { caps = true; continue; }
                if (*p == ' ' || *p == '\n' || *p == '~') {
                    const bool isEnter = (*p == '\n');
                    const bool isMouse = (*p == '~');
                    {   // engine
                        TextInput in;
                        in.kind = isEnter ? InputKind::WordBreak : (isMouse ? InputKind::MouseDown : InputKind::Space);
                        in.vkCode = isEnter ? 0x0D : 0;
                        const auto& r = eng.process(in);
                        if (r.code == EngineCode::ReplaceMacro) {
                            apply(r.backspaceCount, expansionOf(r.macroExpansion), et);
                        } else if (isEnter) {
                            et += L"\n";
                        } else if (!isMouse) {
                            if (r.consumed()) {
                                apply(r.backspaceCount, eng.replacementUtf16(r), et);
                                if (r.code == EngineCode::Restore || r.code == EngineCode::RestoreAndStartNewSession) { et += L' '; }
                            } else {
                                et += L' ';   // plain pass-through space
                            }
                        }
                    }
                    {   // oracle
                        orel::Event ev;
                        ev.kind = isEnter ? orel::Kind::WordBreak : (isMouse ? orel::Kind::MouseDown : orel::Kind::Space);
                        ev.vk = isEnter ? 0x0D : 0;
                        const auto& r = ora.process(ev);
                        if (r.code == orel::Code::ReplaceMacro) {
                            apply(r.backspaceCount, expansionOf(r.macroExpansion), ot);
                        } else if (isEnter) {
                            ot += L"\n";
                        } else if (!isMouse) {
                            if (r.consumed()) {
                                apply(r.backspaceCount, r.replacement, ot);
                                if (r.code == orel::Code::Restore || r.code == orel::Code::RestoreAndStartNewSession) { ot += L' '; }
                            } else {
                                ot += L' ';
                            }
                        }
                    }
                    caps = false;
                } else {
                    {   // engine
                        TextInput in; in.kind = InputKind::Char; in.ch = static_cast<char32_t>(*p); in.isCaps = caps;
                        const auto& r = eng.process(in);
                        if (r.code == EngineCode::ReplaceMacro) {
                            apply(r.backspaceCount, expansionOf(r.macroExpansion), et);
                        } else if (r.consumed()) {
                            apply(r.backspaceCount, eng.replacementUtf16(r), et);
                            if (r.code == EngineCode::Restore || r.code == EngineCode::RestoreAndStartNewSession) {
                                et += static_cast<wchar_t>(caps && *p >= 'a' && *p <= 'z' ? *p - 32 : *p);
                            }
                        } else {
                            et += static_cast<wchar_t>(caps && *p >= 'a' && *p <= 'z' ? *p - 32 : *p);
                        }
                    }
                    {   // oracle
                        orel::Event ev; ev.kind = orel::Kind::Char; ev.ch = static_cast<char32_t>(*p); ev.caps = caps;
                        const auto& r = ora.process(ev);
                        if (r.code == orel::Code::ReplaceMacro) {
                            apply(r.backspaceCount, expansionOf(r.macroExpansion), ot);
                        } else if (r.consumed()) {
                            apply(r.backspaceCount, r.replacement, ot);
                            if (r.code == orel::Code::Restore || r.code == orel::Code::RestoreAndStartNewSession) {
                                ot += static_cast<wchar_t>(caps && *p >= 'a' && *p <= 'z' ? *p - 32 : *p);
                            }
                        } else {
                            ot += static_cast<wchar_t>(caps && *p >= 'a' && *p <= 'z' ? *p - 32 : *p);
                        }
                    }
                    caps = false;
                }
                if (eng.visibleAccount() != et.size()) { allOk = false; std::printf("    [dbg] after ops[%ld] char='%c': acct=%zu vis=%zu\n", (long)(p - rd.ops), *p, eng.visibleAccount(), et.size()); }
            }
            if (et != ot) { allOk = false; }   // engine == oracle lockstep
        }
        CHECK(allOk,
              "D2: frozen over-backspace repro streams satisfy bs<=committed + account==document + oracle lockstep");
    }

    {
        // D2/D1 invariant sweep — the account and the scratch stay bounded
        // through a long mixed session (long words, backspaces into the undo
        // history, restores, spaces, mouse breaks).
        Feed f(eo);
        const std::string lw = "abcdefghijklmnopqrstuvwxyzabcdefghijklmn"; // 40 chars
        bool ok = true;
        auto walk = [&](const char* s) {
            for (const char* p = s; *p; ++p) {
                if (*p == ' ') f.space();
                else if (*p == '\b') f.bs();
                else f.key(static_cast<char32_t>(*p));
                if (f.eng.visibleAccount() != f.visible.size()) { ok = false; }
                if (f.eng.debugScratchSize() > ok::text::kMaxBuff) { ok = false; }
            }
        };
        const std::string slw1 = lw, slw2 = lw;
        walk(slw1.c_str()); walk(" "); walk("\b"); walk(slw2.c_str()); walk(" xin chao "); walk("\b\b\b");
        walk("mut\bof "); walk("~"); walk("duoc "); walk("\b\b\b\b\b");
        CHECK(ok, "D1/D2: account==document and scratch<=32 through the whole session");
        CHECK(f.eng.debugScratchSize() <= ok::text::kMaxBuff,
              "D1: restore scratch never exceeds kMaxBuff");
    }

    {
        // D3 — macro expansion end-to-end: the engine returns ReplaceMacro
        // WITH the expansion payload; the consumer deletes len(key) chars and
        // types the expansion. (Pre-v3.1 the shipped hook let the raw key
        // through and the expansion was lost — 462,627 gap events.)
        EngineOptions me = eo;
        me.useMacro = true;   // the shared eo disables macros; D3 needs them
        Feed f(me);
        f.eng.setMacroResolver(
            [](const std::vector<std::uint32_t>& key, std::vector<std::uint32_t>& data) {
                static const std::vector<std::uint32_t> kKey = {'B', 'T'};
                if (key == kKey) {
                    const wchar_t* exp = L"bình thường";
                    for (const wchar_t* p = exp; *p; ++p) { data.push_back(static_cast<std::uint32_t>(*p)); }
                    return true;
                }
                return false;
            });
        for (char c : std::string("bt")) f.key(c);
        CHECK(u8(f.visible) == "bt", "D3 setup: raw keys typed");
        TextInput in; in.kind = InputKind::Space;
        const auto& r = f.eng.process(in);
        CHECK(r.code == EngineCode::ReplaceMacro, "D3: space triggers ReplaceMacro");
        CHECK(r.backspaceCount == 2, "D3: backspaceCount == macro key length");
        std::wstring exp;
        for (std::uint32_t v : r.macroExpansion) { exp += static_cast<wchar_t>(v); }
        CHECK(u8(exp) == "b\xc3\xacnh th\xc6\xb0\xe1\xbb\x9dng",
              "D3: expansion rides in the result ('bình thường')");
        // consumer application: erase 2, insert expansion, space consumed
        f.visible.erase(f.visible.size() - r.backspaceCount, r.backspaceCount);
        f.visible += exp;
        CHECK(u8(f.visible) == "b\xc3\xacnh th\xc6\xb0\xe1\xbb\x9dng",
              "D3: consumer applies delete + expansion end-to-end");
    }

    {
        // D1 — the suite-7 long-session signature: long word committed,
        // backspace (restore fills the scratch), second long word, space.
        // Pinned in Bug 4 above; assert the scratch bound explicitly now.
        Feed f(eo);
        const std::string lw = "abcdefghijklmnopqrstuvwxyzabcdefghijklmn";
        for (char c : lw) f.key(c);
        f.space();
        f.bs();
        CHECK(f.eng.debugScratchSize() <= ok::text::kMaxBuff,
              "D1: scratch bounded right after the undo-history restore");
        for (char c : lw) f.key(c);
        f.space();
        CHECK(f.eng.debugScratchSize() <= ok::text::kMaxBuff,
              "D1: scratch bounded after the second long word");
        CHECK(f.visible.size() == 81, "D1: long-word/long-word session committed cleanly");
    }

    std::printf("\n%s (failures: %d)\n", failures == 0 ? "ALL HOTFIX TESTS PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
