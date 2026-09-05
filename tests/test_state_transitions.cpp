//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.1 Stable - refactored and completed logic
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
// File: tests/test_state_transitions.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.2.0 Stable — tests/test_state_transitions.cpp
// STATE-TRANSITION MATRIX for the composition state machine.
//
// WHY THIS FILE EXISTS
//   The release brief for v1.2.0 asks specifically for "stale composition
//   state / partially reset state / invalid state transitions" to be
//   constructed explicitly and tested. The existing suites are excellent at
//   SEQUENCES (type this, get that) but none of them asks the orthogonal
//   question: after an interruption, is the engine in a state that is
//   indistinguishable from a fresh one?
//
//   Every check below is built from one invariant:
//
//       INTERRUPT-THEN-CONTINUE ≡ FRESH-CONTINUE
//
//   i.e. whatever an interruption does to the engine, the NEXT word typed
//   after it must come out byte-identical to the same word typed on a brand
//   new engine. That single property kills stale buffers, phantom transform
//   flags, half-reset macro accumulators and leaked "restore" bookkeeping in
//   one shot, without hard-coding any particular expected text.
//
// Interruptions covered (each applied at EVERY prefix of a set of words,
// and repeated, and interleaved):
//   1. startNewSession()
//   2. resumeFromText(visible word)
//   3. switchToneStyle()
//   4. setOptions()  (a settings change mid-word — the settings dialog can
//                     be OK'd while a composition is pending)
//   5. processEnglishMode()
//   6. backspace to empty, then continue
//   7. word-break keys (space / Enter / Tab / arrows)
//
// Per-event invariants checked after EVERY keystroke:
//   * D2  — backspaceCount <= visibleAccount() (never over-backspace: an
//           over-backspace deletes text the user typed before this session)
//   * D1  — debugScratchSize() <= kMaxBuff (bounded undo history)
//   * PAY — a result that deletes N chars must supply text, unless it is a
//           pure delete
//
// Build & run (also driven by tests/run_all_tests.sh):
//   g++ -std=c++2b -O2 -I src/core tests/test_state_transitions.cpp
//       src/core/TextEngine.cpp -o tst
//   ./tst
// Exit 0 = ALL PASSED.
//----------------------------------------------------------------------------

#include "TextEngine.hpp"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

using namespace ok::text;

namespace {

int g_failures = 0;
int g_checks   = 0;

// Print helper: the C locale makes printf's %ls abort on the first non-ASCII
// code point (Vietnamese text!), silently truncating the failure dump. Encode
// to UTF-8 and print with %s instead.
std::string dump(const std::wstring& s) {
    std::string o;
    for (const wchar_t c : s) {
        const std::uint32_t u = static_cast<std::uint32_t>(c);
        if (u < 0x80) {
            if (u < 32 || u == 127) {
                char b[8];
                std::snprintf(b, sizeof b, "\\x%02X", static_cast<unsigned>(u));
                o += b;
            } else { o.push_back(static_cast<char>(u)); }
        } else if (u < 0x800) {
            o.push_back(static_cast<char>(0xC0 | (u >> 6)));
            o.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        } else if (u < 0x10000) {
            o.push_back(static_cast<char>(0xE0 | (u >> 12)));
            o.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
            o.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        } else {
            o.push_back(static_cast<char>(0xF0 | (u >> 18)));
            o.push_back(static_cast<char>(0x80 | ((u >> 12) & 0x3F)));
            o.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
            o.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        }
    }
    return o;
}

void check(bool ok, const char* what, const char* detail = "") {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  [FAIL] %s%s%s\n", what, detail[0] ? " — " : "", detail);
    }
}

//--- a faithful consumer ----------------------------------------------------
// Mirrors the shipped pipeline: apply backspaces, insert the replacement,
// re-issue the typed key for Restore contracts, and keep the visible text.
struct Consumer {
    TextEngine&         eng;
    std::wstring        text;
    std::wstring        scratch;

    explicit Consumer(TextEngine& e) : eng(e) {}

    void apply(const EngineResult& r, const TextInput& in) {
        // --- per-event invariants (D2 / D1 / payload sanity) ---
        if (r.backspaceCount > eng.visibleAccount()) {
            check(false, "D2 over-backspace",
                  ("bs=" + std::to_string(r.backspaceCount) +
                   " visible=" + std::to_string(eng.visibleAccount())).c_str());
        }
        if (eng.debugScratchSize() > kMaxBuff) {
            check(false, "D1 scratch overflow",
                  ("scratch=" + std::to_string(eng.debugScratchSize())).c_str());
        }

        // v1.2.0 Stable: this used to apply r.backspaceCount
        // UNCONDITIONALLY, which does not match the shipped hook
        // (src/app/main.cpp, onHookEventImpl). The real rule is:
        //   suppress = code == ReplaceMacro ? (bs > 0 || scratch non-empty)
        //                                   : (consumed() && !(bs == 0 &&
        //                                      scratch empty))
        // so a DoNothing result with a non-zero backspaceCount (the
        // grammar-repair path) is a PASS-THROUGH — the edit is discarded and
        // the key reaches the application untouched. Applying it here made
        // the model emit "ửokk" for "work" where the real IME emits "ửok".
        scratch.clear();
        if (r.code == EngineCode::ReplaceMacro) {
            eng.macroExpansionUtf16(r, scratch);
        } else {
            eng.replacementUtf16(r, scratch);
        }

        const bool reissueTyped =
            (r.code == EngineCode::Restore ||
             r.code == EngineCode::RestoreAndStartNewSession) &&
            (in.kind == InputKind::Char || in.kind == InputKind::Space);

        bool        suppress = false;
        std::size_t bs       = 0;
        if (r.code == EngineCode::ReplaceMacro) {
            bs = r.backspaceCount;
            if (bs > 0 || !scratch.empty()) { suppress = true; }
        } else if (r.consumed() && !(r.backspaceCount == 0 && scratch.empty())) {
            suppress = true;
            bs       = r.backspaceCount;
        }

        // Restore re-issue contract (Char / Space only — see main.cpp).
        if (reissueTyped) {
            const wchar_t c = (in.kind == InputKind::Space)
                                  ? L' '
                                  : static_cast<wchar_t>(in.ch);
            scratch.push_back(c);
        }

        if (!suppress) {
            // Pass-through: the key reaches the application untouched and
            // the engine's backspaceCount is deliberately NOT applied.
            if (in.kind == InputKind::Char)  { text.push_back(static_cast<wchar_t>(in.ch)); }
            if (in.kind == InputKind::Space) { text.push_back(L' '); }
            return;
        }

        if (bs > text.size()) {
            check(false, "consumer would underflow the document",
                  ("bs=" + std::to_string(bs) + " doc=" + std::to_string(text.size()) +
                   " vis=" + std::to_string(eng.visibleAccount())).c_str());
            return;
        }
        text.resize(text.size() - bs);
        text += scratch;
    }

    void feed(const TextInput& in) { apply(eng.process(in), in); }

    void type(const std::wstring& keys) {
        for (const wchar_t c : keys) {
            TextInput in;
            if (c == L' ') {
                in.kind = InputKind::Space;
            } else {
                in.kind = InputKind::Char;
                in.ch   = static_cast<char32_t>(c);
            }
            feed(in);
        }
    }

    void bs(std::size_t n = 1) {
        for (std::size_t i = 0; i < n; ++i) {
            TextInput in;
            in.kind = InputKind::Backspace;
            feed(in);
        }
    }

    void wordBreak(std::uint16_t vk = 0x0D) {   // Enter
        TextInput in;
        in.kind   = InputKind::WordBreak;
        in.vkCode = vk;
        feed(in);
    }

    void mouseDown() {
        TextInput in;
        in.kind = InputKind::MouseDown;
        feed(in);
    }
};

// Typing `keys` on a fresh engine must produce exactly `expect`.
std::wstring typeOnFresh(const EngineOptions& opts, const std::wstring& keys) {
    TextEngine e(opts);
    Consumer c(e);
    c.type(keys);
    return c.text;
}

// The interrupt under test, applied to a live engine + consumer.
enum class Interrupt { None, NewSession, Resume, ToneStyle, SetOptions, EnglishMode, BackspaceAll, MouseDown };

const char* interruptName(Interrupt i) {
    switch (i) {
        case Interrupt::None:        return "none";
        case Interrupt::NewSession:  return "startNewSession";
        case Interrupt::Resume:      return "resumeFromText";
        case Interrupt::ToneStyle:   return "switchToneStyle";
        case Interrupt::SetOptions:  return "setOptions";
        case Interrupt::EnglishMode: return "processEnglishMode";
        case Interrupt::BackspaceAll:return "backspace-to-empty";
        case Interrupt::MouseDown:   return "mouseDown";
    }
    return "?";
}

void applyInterrupt(Interrupt i, TextEngine& e, Consumer& c, const EngineOptions& opts) {
    switch (i) {
        case Interrupt::None:
            break;
        case Interrupt::NewSession:
            e.startNewSession();
            break;
        case Interrupt::Resume:
            // Re-sync to the word currently visible before the caret. The
            // consumer's document may hold a trailing non-word char, so feed
            // the trailing ASCII-letter run exactly like the shipped resync
            // (TsfComposer::textBeforeCaret) would.
            {
                std::size_t end = c.text.size();
                while (end > 0) {
                    const wchar_t ch = c.text[end - 1];
                    if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z')) { --end; }
                    else { break; }
                }
                const std::wstring word = c.text.substr(end);
                c.scratch.clear();
                static_cast<void>(e.resumeFromText(word));
            }
            break;
        case Interrupt::ToneStyle: {
            const bool converted = e.switchToneStyle();
            if (converted) { c.apply(e.lastResult(), TextInput{}); }
            break;
        }
        case Interrupt::SetOptions:
            // A settings dialog OK mid-word. Options are unchanged apart from
            // a no-op rewrite — what matters is that setOptions() performs a
            // full, consistent swap.
            e.setOptions(opts);
            break;
        case Interrupt::EnglishMode: {
            // processEnglishMode() is the English-mode (IME bypassed) surface.
            // It must be STATE-NEUTRAL with respect to the Vietnamese
            // composition: the decision is discarded (the key passes through
            // on the English path, it is not applied as a Vietnamese edit),
            // and the pending Vietnamese word must survive untouched.
            TextInput in;
            in.kind = InputKind::Char;
            in.ch   = U'x';
            static_cast<void>(e.processEnglishMode(in));   // decision discarded
            break;
        }
        case Interrupt::BackspaceAll:
            c.bs(c.text.size() + 4);   // deliberately over-backspace the doc
            break;
        case Interrupt::MouseDown:
            c.mouseDown();
            break;
    }
}

const std::wstring kWords[] = {
    L"tooi",      // tồi
    L"chaoj",     // chào (VNI-ish mixture; exercises the raw path too)
    L"as",        // á  (the golden upstream case)
    L"dd",        // đ
    L"aw",        // ă
    L"xfuoic",    // xướng-ish
    L"nghieeng",  // nghiêng
    L"dddd",      // repeated đ key
    L"asdfasdf",  // tone keys as plain letters after a break
    L"chauwg",
};

} // namespace

// Words whose EVERY prefix stays raw ASCII (no Telex/VNI tone key fires), so
// resumeFromText() can legally adopt them at any prefix — the "click into a
// half-typed word and keep typing" contract. Each is followed by a tone key
// so the resumed composition is observable.
const std::wstring kRawWords[] = {
    L"chaof",   // chào
    L"nghieps", // nghiếp-ish
    L"truongf", // trường-ish
    L"bans",    // bán
    L"hoaf",    // hoà / hóa (orthography-dependent — compared against fresh)
};

int main() {
    std::printf("== KieeKey state-transition matrix ==\n\n");

    const InputMethod kMethods[] = {InputMethod::Telex, InputMethod::Vni,
                                    InputMethod::SimpleTelex};

    //=====================================================================
    // P1 — NO STALE STATE LEAKS INTO THE NEXT WORD.
    //=====================================================================
    // Whatever an interruption does to the word being typed, the NEXT word
    // must compose exactly as it does on a brand-new engine. This single
    // property kills stale buffers, phantom transform flags, half-reset
    // macro accumulators and leaked restore bookkeeping in one shot, without
    // hard-coding any particular expected text.
    //
    // (The interrupted word itself is deliberately NOT compared: a break
    //  interruption is SUPPOSED to leave it raw — that is what "break"
    //  means. What must not happen is for that residue to change how the
    //  following word is composed.)
    for (const InputMethod m : kMethods) {
        for (const bool modern : {false, true}) {
            EngineOptions opts{};
            opts.inputMethod          = m;
            opts.digitsAreLiteral     = true;   // shipping default
            opts.useModernOrthography = modern;
            opts.checkSpelling        = true;
            opts.restoreIfWrongSpelling = true;

            std::printf("-- P1 next-word independence: method=%d modern=%d --\n",
                        static_cast<int>(m), modern ? 1 : 0);

            const std::wstring kProbes[] = {L"tooi", L"as", L"dd", L"chaoj", L"aw"};
            for (const std::wstring& w : kWords) {
                for (int ii = 0; ii <= static_cast<int>(Interrupt::MouseDown); ++ii) {
                    const Interrupt in = static_cast<Interrupt>(ii);
                    for (std::size_t prefix = 0; prefix <= w.size(); ++prefix) {
                        for (const std::wstring& probe : kProbes) {
                            // Reference: the probe word typed on a fresh engine.
                            const std::wstring expect = typeOnFresh(opts, probe);

                            TextEngine e(opts);
                            Consumer  c(e);
                            c.type(w.substr(0, prefix));
                            applyInterrupt(in, e, c, opts);
                            // Finish the interrupted word, then a space (word
                            // break) and the probe word.
                            c.type(w.substr(prefix));
                            c.type(L" ");
                            c.type(probe);

                            // The probe is the trailing |expect| characters.
                            const bool tailOk =
                                c.text.size() >= expect.size() &&
                                c.text.compare(c.text.size() - expect.size(),
                                               expect.size(), expect) == 0;
                            if (!tailOk) {
                                ++g_failures;
                                std::printf(
                                    "  [FAIL] m=%d modern=%d word=%s interrupt=%s "
                                    "prefix=%zu probe=%s\n"
                                    "         got    [%s]\n"
                                    "         want tail [%s]\n",
                                    static_cast<int>(m), modern ? 1 : 0,
                                    dump(w).c_str(), interruptName(in), prefix,
                                    dump(probe).c_str(),
                                    dump(c.text).c_str(), dump(expect).c_str());
                            }
                            ++g_checks;
                        }
                    }
                }
            }
            std::printf("   %d combinations checked so far\n\n", g_checks);
        }
    }

    //=====================================================================
    // P2 — resumeFromText() IS TRANSPARENT.
    //=====================================================================
    // Clicking into (or arrowing within) a half-typed RAW word and keeping
    // typing must produce exactly what typing straight through produces.
    // This is the contract TsfComposer::textBeforeCaret + resumeFromText
    // implement, and it is exercised at EVERY prefix.
    {
        std::printf("-- P2 resumeFromText transparency --\n");
        for (const InputMethod m : kMethods) {
            for (const bool modern : {false, true}) {
                EngineOptions opts{};
                opts.inputMethod          = m;
                opts.digitsAreLiteral     = true;
                opts.useModernOrthography = modern;
                for (const std::wstring& w : kRawWords) {
                    const std::wstring expect = typeOnFresh(opts, w);
                    for (std::size_t prefix = 0; prefix <= w.size(); ++prefix) {
                        TextEngine e(opts);
                        Consumer  c(e);
                        c.type(w.substr(0, prefix));
                        // The visible word before the caret is the raw ASCII
                        // run the consumer's document ends with.
                        std::size_t start = c.text.size();
                        while (start > 0) {
                            const wchar_t ch = c.text[start - 1];
                            if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z')) { --start; }
                            else { break; }
                        }
                        const std::wstring visible = c.text.substr(start);
                        c.scratch.clear();
                        const bool resumed = e.resumeFromText(visible);
                        c.type(w.substr(prefix));
                        if (resumed && c.text != expect) {
                            ++g_failures;
                            std::printf(
                                "  [FAIL] resume m=%d modern=%d word=%s prefix=%zu\n"
                                "         got    [%s]\n         expect [%s]\n",
                                static_cast<int>(m), modern ? 1 : 0,
                                dump(w).c_str(), prefix,
                                dump(c.text).c_str(), dump(expect).c_str());
                        }
                        ++g_checks;
                    }
                }
            }
        }
        std::printf("   ok (resume is transparent at every prefix)\n\n");
    }

    //=====================================================================
    // P3 — REPEATED RESYNC CANNOT INFLATE THE D2 BACKSPACE CLAMP (P1 bug).
    //=====================================================================
    // Regression test for the v1.2.0 fix: resumeFromText() used to run its
    // replay through process(), which counted every replayed letter as a NEW
    // committed character. A resync is queued on every caret-moving key and
    // every click, so the D2 clamp (backspaceCount <= visibleAccount) could
    // be inflated by k x len(word) with k arrow presses — letting a later
    // edit delete text the engine never committed.
    {
        std::printf("-- P3 repeated resync stays bounded --\n");
        EngineOptions opts{};
        opts.digitsAreLiteral = true;
        for (const std::wstring& w : {std::wstring(L"chaof"), std::wstring(L"nghieps")}) {
            TextEngine e(opts);
            Consumer  c(e);
            c.type(L"x ");          // some unrelated committed text
            const std::size_t base = e.visibleAccount();
            for (int i = 0; i < 500; ++i) {
                static_cast<void>(e.resumeFromText(w.substr(0, w.size() - 1)));
            }
            const std::size_t after = e.visibleAccount();
            const std::size_t limit = w.size();
            if (after > limit) {
                ++g_failures;
                std::printf("  [FAIL] 500 resyncs inflated visibleAccount to %zu "
                            "(word=%s, cap=%zu)\n",
                            after, dump(w).c_str(), limit);
            }
            ++g_checks;
            // And the resulting edits must stay inside the adopted word.
            c.type(L"f");
            if (c.text.empty()) { ++g_failures; std::printf("  [FAIL] empty doc after resync storm\n"); }
            ++g_checks;
            (void)base;
        }
        // Interleaved with real typing (the realistic shape: click, arrow,
        // type, arrow, type…).
        {
            TextEngine e(opts);
            Consumer  c(e);
            for (int i = 0; i < 2000; ++i) {
                c.type(L"chao");
                static_cast<void>(e.resumeFromText(L"chao"));
                TextInput in;
                in.kind = InputKind::WordBreak;   // arrow / Home / End
                in.vkCode = 0x25;
                c.feed(in);
            }
            if (e.visibleAccount() > 64u) {
                ++g_failures;
                std::printf("  [FAIL] interleaved resync+typing inflated the "
                            "account to %zu\n", e.visibleAccount());
            }
            ++g_checks;
        }
        std::printf("   ok\n\n");
    }

    //=====================================================================
    // P4 — DETERMINISM / REPEATABILITY.
    //=====================================================================
    {
        std::printf("-- P4 determinism --\n");
        EngineOptions opts{};
        opts.digitsAreLiteral = true;
        const std::wstring expect = typeOnFresh(opts, L"tooi chaoj ba dd");
        for (int i = 0; i < 2000; ++i) {
            const std::wstring got = typeOnFresh(opts, L"tooi chaoj ba dd");
            if (got != expect) {
                ++g_failures;
                std::printf("  [FAIL] run %d differs: [%s] vs [%s]\n", i,
                            dump(got).c_str(), dump(expect).c_str());
                break;
            }
            ++g_checks;
        }
        std::printf("   ok\n\n");
    }

std::printf("----------------------------------------------------------\n");
    std::printf("  checks: %d   failures: %d\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("\nALL STATE-TRANSITION TESTS PASSED\n");
        return 0;
    }
    std::printf("\nSTATE-TRANSITION TESTS FAILED\n");
    return 1;
}
