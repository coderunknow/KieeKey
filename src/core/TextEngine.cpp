//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.1.3 - refactored and completed logic
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
// File: src/core/TextEngine.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.3 — TextEngine.cpp
// C++20 Telex/VNI/Simple-Telex state machine.
//
// Porting notes:
//   * Algorithm = OpenKey 2.0.5 Engine.cpp, with ONE deliberate correctness
//     fix taken from upstream master: the 2.0.5 tag iterated
//         for (i = 0; i < _vowelForMark.size(); i++) charset = _vowelForMark[i];
//     i.e. std::map::operator[] with keys 0..5 while the map's real keys are
//     letters — which inserted EMPTY rows, so tone marks were silently never
//     inserted. Master uses `for (auto& e : _vowelForMark)`. We port master's
//     fix (see handleMainKey) — verified by the golden test "as" -> "á".
//   * All legacy file-scope state is now per-instance members; all macros are
//     constexpr helpers; output is resolved to UTF-16 for the composer.
//   * Internal identity is UPPERCASE (tables store 0x41='A' …); `caps` is the
//     legacy shift-XOR-caps status.
//----------------------------------------------------------------------------
#include "TextEngine.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <map>
#include <tuple>

namespace ok::text {

//===========================================================================
// Construction / session
//===========================================================================
TextEngine::TextEngine(EngineOptions opts) noexcept : opts_(opts) {
    useSpellingBefore_ = opts_.checkSpelling;
    // Pre-reserve the reusable state buffers so the hot path never hits the
    // heap (vector-of-arrays grows only for genuinely long typing history).
    typingStates_.reserve(128);
    typingStatesLen_.reserve(128);
    typingStatesData_.reserve(kMaxBuff);
    specialChar_.reserve(kMaxBuff);
    result_.macroKey.reserve(128);
    macroData_.reserve(kMaxBuff);
    // Long-word overflow buffer: covers words up to 288 chars (256 + kMaxBuff)
    // with ZERO allocations; beyond that, amortized growth only.
    longWordHelper_.reserve(256);
    startNewSession();
}

void TextEngine::startNewSession() noexcept {
    index_ = 0;
    stateIndex_ = 0;
    tempDisableKey_ = false;
    hasHandledMacro_ = false;
    hasHandleQuickConsonant_ = false;
    midWordToggle_ = false;   // v3.3.1: fresh word, fresh toggle bookkeeping
    macroExpandLen_ = 0;      // v1.1.0: no expansion on screen in a fresh session
    longWordHelper_.clear();
    result_.backspaceCount = 0;
    result_.newCharCount = 0;
}

bool TextEngine::resumeFromText(const std::wstring& rawWord) noexcept {
    startNewSession();
    if (rawWord.empty()) { return false; }

    // Only pure ASCII letters are replayable raw Telex/VNI input. A word that
    // already contains tone marks or punctuation is already "finalized" — the
    // engine cannot resume composition from it (this matches every IME: text
    // edited outside the keystroke stream is opaque).
    for (const wchar_t wc : rawWord) {
        const char32_t c = static_cast<char32_t>(wc);
        if (!((c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z'))) { return false; }
    }

    // v1.1.0 — RAW replay (desync fix). The letters on screen are the RAW
    // keystrokes (that is what textBeforeCaret reads back), so the state
    // machine must be advanced WITHOUT applying transforms: a visible "as"
    // replayed through the full engine left transform masks inside
    // typingWord_ and made the next word break "restore" composed text that
    // was never displayed (duplicated/ghost letters). rawReplay_ mode inserts
    // each letter verbatim — buffer state exactly mirrors the screen.
    rawReplay_ = true;
    for (const wchar_t wc : rawWord) {
        const char32_t c = static_cast<char32_t>(wc);
        TextInput in;
        in.kind   = InputKind::Char;
        in.ch     = c;
        in.isCaps = (c >= U'A' && c <= U'Z');
        (void)process(in);   // state advance only
    }
    rawReplay_ = false;
    return true;
}

void TextEngine::tempOffSpellChecking() noexcept {
    if (useSpellingBefore_) {
        opts_.checkSpelling = !opts_.checkSpelling;
    }
}

//===========================================================================
// Primary API
//===========================================================================
const EngineResult& TextEngine::process(const TextInput& in) {
    // NOTE: macroKey_ must SURVIVE across calls (it is the macro key
    // accumulator, mirroring the legacy persistent HookState.macroKey).
    // Only the per-event fields are reset here. P2 hot-path note: only
    // newChars[0] is zeroed — every consumer reads newChars through
    // newCharCount (replacementUtf16 / the assert loop / the ring copy),
    // and the bracket-check below needs exactly slot 0 clean; zeroing the
    // whole 32-slot array cost ~15 ns/key for nothing.
    result_.code = EngineCode::DoNothing;
    result_.backspaceCount = 0;
    result_.newCharCount = 0;
    result_.extCode = 0;
    result_.newChars[0] = 0;
    result_.macroExpansion.clear();

    const bool caps = in.isCaps;   // shift XOR caps — computed by the consumer

    switch (in.kind) {
        case InputKind::Char: {
            char32_t raw = in.ch;
            if (raw == 0) { return result_; }
            // P3: user-defined keymap (applied to the produced character
            // before Vietnamese processing; the keymap is identity when the
            // option is off or the entry is missing).
            if (opts_.useUserKeymap && keymapResolver_) {
                const char32_t mapped = keymapResolver_(raw);
                if (mapped != 0) { raw = mapped; }
            }
            const bool chCaps = caps || (raw >= U'A' && raw <= U'Z');
            const char32_t c = toUpperAscii(raw);
            // Legacy first branch: shifted digits, ctrl/alt combos, word
            // breaks, and digits typed at word start all pass through.
            if ((isNumberKey(c) && chCaps) || in.otherCtrl ||
                isWordBreakChar(c) || (index_ == 0 && isNumberKey(c))) {
                wordBreakBranch(c, chCaps, in, isMacroBreakChar(c));
            } else {
                mainKeyBranch(c, chCaps);
            }
            break;
        }
        case InputKind::Space:      spaceBranch(U' ', caps); break;
        case InputKind::Backspace:  backspaceBranch(caps);   break;
        case InputKind::WordBreak:  wordBreakBranch(0, caps, in,
                                                    in.vkCode == 0x0D);  // Enter = macro trigger
            break;
        case InputKind::MouseDown:  wordBreakBranch(0, caps, in, false); break;
    }

    //--------------------------------------------------------------------
    // D2 — over-backspace policy (engine-side).
    //
    // The consumer contract is: "delete backspaceCount characters at the
    // caret, then insert the replacement". backspaceCount must never
    // exceed the number of characters the consumer actually committed at
    // the caret — otherwise a consumer without its own clamp deletes text
    // the user never asked to delete (observed as 1,670 over-backspace
    // events in the mega differential).
    //
    // visibleAccount_ mirrors the committed length exactly for the
    // verification harness (empty document at engine construction) and is
    // a conservative lower bound for real consumers (pre-existing document
    // content only makes the true committed length larger).
    //--------------------------------------------------------------------
    if (result_.backspaceCount > visibleAccount_) {
        result_.backspaceCount = static_cast<std::uint8_t>(visibleAccount_);
    }
#ifndef NDEBUG
    // D2 root invariant: every emitted character must resolve to a visible
    // code point. (The one known drop family — standalone ư/ơ entries whose
    // W-tone was stripped by a later transform — is fixed in resolveChar;
    // this assert keeps any future regression visible. Cheap: only entries
    // carrying payload bits beyond char+caps can ever drop.)
    for (std::size_t dbgI = 0; dbgI < result_.newCharCount; ++dbgI) {
        if (result_.newChars[dbgI] & ~(kCharMask | kCapsMask)) {
            if (resolveChar(result_.newChars[dbgI]) == 0) {
                std::fprintf(stderr, "[D2-assert] dropping entry idx=%zu code=0x%X code_=%d bs=%u n=%u\n",
                             dbgI, result_.newChars[dbgI], (int)result_.code,
                             result_.backspaceCount, result_.newCharCount);
            }
            assert(resolveChar(result_.newChars[dbgI]) != 0);
        }
    }
    assert(result_.backspaceCount <= visibleAccount_);
#endif
    // Consumer-visible accounting (mirrors the verification consumer,
    // including the Restore re-issue contracts: the typed character after a
    // Char-Restore — hotfix §3 — and the space after a Space-Restore — D4).
    switch (in.kind) {
        case InputKind::Backspace:
            if (visibleAccount_ > 0) { --visibleAccount_; }
            break;
        default:
            break;
    }
    if (result_.code == EngineCode::ReplaceMacro) {
        // The expansion replaces the raw keys; the break key itself is
        // consumed (neither the space nor the newline is typed).
        const std::size_t expLen = result_.macroExpansion.size();
        visibleAccount_ = visibleAccount_ > result_.backspaceCount
                              ? visibleAccount_ - result_.backspaceCount + expLen
                              : expLen;
    } else {
        switch (in.kind) {
            case InputKind::Char:
                if (result_.code == EngineCode::DoNothing) {
                    ++visibleAccount_;                       // raw key lands
                } else {
                    visibleAccount_ += result_.newCharCount; // replacement lands
                    if (result_.code == EngineCode::Restore ||
                        result_.code == EngineCode::RestoreAndStartNewSession) {
                        ++visibleAccount_;                   // typed key re-issued
                    }
                    visibleAccount_ -= result_.backspaceCount;
                }
                break;
            case InputKind::Space:
                if (result_.code == EngineCode::DoNothing) {
                    ++visibleAccount_;                       // space lands
                } else {
                    // Restore family: the consumer applies the revert edit
                    // AND re-issues the space (D4 contract).
                    visibleAccount_ += result_.newCharCount + 1;
                    visibleAccount_ -= result_.backspaceCount;
                }
                break;
            case InputKind::WordBreak:
                if (in.vkCode == 0x0D) { ++visibleAccount_; }   // Enter lands as "\n"
                break;
            default:
                break;   // MouseDown / non-Enter breaks: no text lands
        }
    }

    finalizeResult();
    return result_;
}

//===========================================================================
// process() branches — faithful ports of vKeyHandleEvent's three big blocks
//===========================================================================
bool TextEngine::isWordBreakAny(const TextInput& in) const noexcept {
    switch (in.kind) {
        case InputKind::MouseDown: return true;
        case InputKind::Char:      return isWordBreakChar(toUpperAscii(in.ch));
        case InputKind::WordBreak: return isWordBreakVk(in.vkCode);
        default:                   return false;
    }
}

void TextEngine::wordBreakBranch(char32_t c, bool caps, const TextInput& in, bool macroBreak) {
    result_.code = EngineCode::DoNothing;
    result_.backspaceCount = 0;
    result_.newCharCount = 0;
    result_.extCode = 1;   // word break

    // ---- macro feature ----
    if (opts_.useMacro && macroBreak && !hasHandledMacro_ &&
        findMacro(result_.macroKey, macroData_)) {
        result_.code = EngineCode::ReplaceMacro;
        result_.backspaceCount = static_cast<std::uint8_t>(result_.macroKey.size());
        setMacroExpansionFromResolver();
        hasHandledMacro_ = true;
    } else if ((opts_.quickStartConsonant || opts_.quickEndConsonant) &&
               !tempDisableKey_ && macroBreak) {
        checkQuickConsonant();
    } else if (opts_.restoreIfWrongSpelling && isWordBreakAny(in)) {
        if (!tempDisableKey_ && opts_.checkSpelling) {
            checkSpelling(true);
        }
        if (tempDisableKey_) {
            if (opts_.useDictionaryRestore && dictResolver_) {
                // P1: the lexicon supersedes the structural restore — a
                // composed word the dictionary knows (e.g. the compound
                // "đăngten") survives even though one syllable is invalid.
                if (!checkRestoreIfNotInDictionary(EngineCode::RestoreAndStartNewSession)) {
                    result_.code = EngineCode::DoNothing;
                }
            } else if (!checkRestoreIfWrongSpelling(EngineCode::RestoreAndStartNewSession)) {
                result_.code = EngineCode::DoNothing;
            }
        } else if (opts_.useDictionaryRestore && dictResolver_) {
            // Structurally fine, but a transform may still have produced a
            // non-word ("bata" -> "bât"): veto via the lexicon.
            if (!checkRestoreIfNotInDictionary(EngineCode::RestoreAndStartNewSession)) {
                result_.code = EngineCode::DoNothing;
            }
        }
    }

    isCharKeyCode_ = (in.kind == InputKind::Char) && isCharKeyCodeChar(c);
    if (!isCharKeyCode_) {
        specialChar_.clear();
        // Clear BOTH halves of the parallel history (states + lengths). They
        // must never diverge: a lone clear here previously let typingStatesLen_
        // grow for the whole session while the 64-entry cap (keyed on
        // typingStates_.size()) never fired.
        typingStates_.clear();
        typingStatesLen_.clear();
    } else {
        if (spaceCount_ > 0) {
            saveWord(U' ', spaceCount_);
            spaceCount_ = 0;
        } else {
            saveWord();
        }
        specialChar_.push_back(c | (caps ? kCapsMask : 0));
        result_.extCode = 3;   // normal key
    }

    if (result_.code == EngineCode::DoNothing) {
        startNewSession();
        opts_.checkSpelling = useSpellingBefore_;
        willTempOffEngine_ = false;
    } else if (result_.code == EngineCode::ReplaceMacro || hasHandleQuickConsonant_) {
        index_ = 0;
        // v1.1.0: a ReplaceMacro at a word break previously cleared ONLY
        // index_, leaving stateIndex_/tempDisableKey_/longWordHelper_ stale
        // (the next word composed onto phantom state). Reset the full word
        // state — but NOT hasHandledMacro_ (it must survive one space to
        // suppress the double-expansion path).
        if (result_.code == EngineCode::ReplaceMacro) {
            stateIndex_ = 0;
            tempDisableKey_ = false;
            specialChar_.clear();
            spaceCount_ = 0;
            longWordHelper_.clear();
            macroExpandLen_ = 0;
            for (const std::uint32_t v : result_.macroExpansion) {
                macroExpandLen_ += (v >= 0x10000u) ? 2u : 1u;
            }
        }
    }

    if (opts_.useMacro) {
        if (isCharKeyCode_) {
            pushMacroKey(c | (caps ? kCapsMask : 0));
        } else {
            result_.macroKey.clear();
        }
    }

    if (opts_.upperCaseFirstChar) {
        if (c == U'.') {
            upperCaseStatus_ = 1;
        } else if (in.vkCode == 0x0D) {
            upperCaseStatus_ = 2;
        } else {
            upperCaseStatus_ = 0;
        }
    }
}

void TextEngine::spaceBranch(char32_t /*c*/, bool /*caps*/) {
    if (!tempDisableKey_ && opts_.checkSpelling) {
        checkSpelling(true);
    }
    if (opts_.useMacro && !hasHandledMacro_ && findMacro(result_.macroKey, macroData_)) {
        result_.code = EngineCode::ReplaceMacro;
        result_.backspaceCount = static_cast<std::uint8_t>(result_.macroKey.size());
        setMacroExpansionFromResolver();
        // v1.1.0 macro-visibility fix (D3 contract): the space key is
        // CONSUMED by the expansion — the screen shows the expansion, no
        // space. The old ++spaceCount_ made the engine believe a space was
        // committed, so the first backspace after any macro expansion ran the
        // wrong restore path (ghost letters composed onto phantom text).
        // Model: spaceCount_ = 0, and the expansion becomes the tracked
        // visible word (macroExpandLen_) that backspaceBranch walks down.
        // Pending bracket/space bookkeeping is dropped: after the expansion
        // the ONLY tracked visible text is the expansion itself (a
        // consistent backspace model — no ghost state mixing).
        spaceCount_ = 0;
        specialChar_.clear();
        macroExpandLen_ = 0;
        for (const std::uint32_t v : result_.macroExpansion) {
            macroExpandLen_ += (v >= 0x10000u) ? 2u : 1u;   // UTF-16 units
        }
        hasHandledMacro_ = true;
        // v1.1.0-followup: the raw keys are REPLACED on screen, so the next
        // word must start from a clean session. The pre-fix code left
        // index_ / stateIndex_ / longWordHelper_ at their pre-expansion
        // values: the NEXT word composed onto the phantom raw keys, and —
        // because hasHandledMacro_ never re-armed (spaceCount_ == 0 means
        // the next letter no longer runs startNewSession) — the same macro
        // could not expand twice in a row ("xl xl " typed "xin lỗixl ").
        // Mirrors the word-break ReplaceMacro tail below; hasHandledMacro_
        // still suppresses the immediately-following space and is re-armed
        // by the next fresh-word letter in mainKeyBranch.
        index_ = 0;
        stateIndex_ = 0;
        tempDisableKey_ = false;
        longWordHelper_.clear();
    } else if ((opts_.quickStartConsonant || opts_.quickEndConsonant) &&
               !tempDisableKey_ && checkQuickConsonant()) {
        ++spaceCount_;
    } else if (opts_.restoreIfWrongSpelling && tempDisableKey_ && !hasHandledMacro_) {
        if (opts_.useDictionaryRestore && dictResolver_) {
            // P1: lexicon-gated restore (see wordBreakBranch).
            if (!checkRestoreIfNotInDictionary(EngineCode::Restore)) {
                result_.code = EngineCode::DoNothing;
            }
        } else if (!checkRestoreIfWrongSpelling(EngineCode::Restore)) {
            result_.code = EngineCode::DoNothing;
        }
        ++spaceCount_;
    } else if (opts_.useDictionaryRestore && dictResolver_ && !hasHandledMacro_) {
        // Structurally valid composition that is still not a lexicon word
        // ("bata" -> "bât"): revert to the raw keystrokes.
        if (!checkRestoreIfNotInDictionary(EngineCode::Restore)) {
            result_.code = EngineCode::DoNothing;
        }
        ++spaceCount_;
    } else {
        result_.code = EngineCode::DoNothing;
        ++spaceCount_;
    }
    if (opts_.useMacro) {
        result_.macroKey.clear();
    }
    if (opts_.upperCaseFirstChar && upperCaseStatus_ == 1) {
        upperCaseStatus_ = 2;
    }
    if (spaceCount_ == 1) {
        if (specialChar_.size() > 0) {
            saveSpecialChar();
        } else {
            saveWord();
        }
    }
    opts_.checkSpelling = useSpellingBefore_;
    willTempOffEngine_ = false;
}

void TextEngine::backspaceBranch(bool /*caps*/) {
    result_.code = EngineCode::DoNothing;
    result_.extCode = 2;   // delete
    if (specialChar_.size() > 0) {
        specialChar_.pop_back();
        if (specialChar_.empty()) {
            restoreLastTypingState();
        }
    } else if (spaceCount_ > 0) {
        --spaceCount_;
        if (spaceCount_ == 0) {
            restoreLastTypingState();
        }
    } else if (macroExpandLen_ > 0) {
        // v1.1.0: backspacing into a macro expansion — the screen shows the
        // expansion (D3), so each backspace deletes one of ITS characters and
        // the raw-key history must NOT be restored (the pre-1.1.0 behavior
        // composed onto the expansion's raw key sequence = ghost letters).
        --macroExpandLen_;
        if (macroExpandLen_ == 0) {
            // The expansion is fully deleted. The raw keys it REPLACED are
            // gone from the screen as well — there is nothing valid to
            // restore, so clear the whole session INCLUDING the raw-key
            // history (otherwise a later backspace at index_ == 0 would
            // restore the phantom pre-expansion keys).
            startNewSession();
            specialChar_.clear();
            typingStates_.clear();
            typingStatesLen_.clear();
        }
        result_.backspaceCount = 0;
        result_.newCharCount = 0;
        result_.extCode = 2;
    } else {
        if (stateIndex_ > 0) {
            --stateIndex_;
        }
        if (index_ > 0) {
            --index_;
            if (longWordHelper_.size() > 0) {
                for (std::size_t i = kMaxBuff - 1; i > 0; --i) {
                    typingWord_[i] = typingWord_[i - 1];
                }
                typingWord_[0] = longWordHelper_.back();
                longWordHelper_.pop_back();
                ++index_;
            }
            if (opts_.checkSpelling) {
                checkSpelling();
            }
        }
        if (opts_.useMacro && result_.macroKey.size() > 0) {
            result_.macroKey.pop_back();
        }
        result_.backspaceCount = 0;
        result_.newCharCount = 0;
        result_.extCode = 2;
        if (index_ == 0) {
            startNewSession();
            specialChar_.clear();
            restoreLastTypingState();
        } else {
            checkGrammar(1);
        }
    }
}

void TextEngine::mainKeyBranch(char32_t c, bool caps) {
    if (willTempOffEngine_) {
        result_.code = EngineCode::DoNothing;
        result_.extCode = 3;
        return;
    }
    // v1.1.0 — resync replay: mirror the visible raw letters into the buffer
    // verbatim. No transforms (insertMark/insertD/…), no grammar/spelling
    // decisions, no macro or uppercase bookkeeping — those paths would store
    // state for text the user never actually typed as raw keys. The result
    // stays DoNothing so the D2 visible-account counts the replayed letters
    // as committed (they ARE on screen).
    if (rawReplay_) {
        insertState(c, caps);
        insertKey(c, caps, false);
        result_.code = EngineCode::DoNothing;
        result_.backspaceCount = 0;
        result_.newCharCount = 0;
        result_.extCode = 3;
        return;
    }
    // v1.1.0-followup — fresh-word re-arm after a macro expansion. The
    // expansion consumed the break key (D3), so the legacy "next letter
    // after a tracked space restarts the session" trigger never fires
    // (spaceCount_ == 0). A letter arriving while the engine holds no
    // pending word is therefore the start of a NEW word: re-arm the macro
    // flag (the identical abbreviation must expand again — "xl xl " →
    // "xin lỗi xin lỗi") and drop the expansion walk-down counter (the
    // expansion is committed history once new text follows; backspaces
    // after this point delete the NEW text, not the expansion).
    if ((hasHandledMacro_ || macroExpandLen_ != 0) &&
        index_ == 0 && specialChar_.empty() && spaceCount_ == 0) {
        hasHandledMacro_ = false;
        macroExpandLen_ = 0;
    }
    if (spaceCount_ > 0) {
        result_.backspaceCount = 0;
        result_.newCharCount = 0;
        result_.extCode = 0;
        startNewSession();
        saveWord(U' ', spaceCount_);
        spaceCount_ = 0;
    } else if (specialChar_.size() > 0) {
        saveSpecialChar();
    }

    insertState(c, caps);

    if (!isSpecialKey(c) || tempDisableKey_) {
        if (opts_.quickTelex && isQuickTelexKey(c)) {
            handleQuickTelex(c, caps);
            return;
        }
        result_.code = EngineCode::DoNothing;
        result_.backspaceCount = 0;
        result_.newCharCount = 0;
        result_.extCode = 3;   // normal key
        insertKey(c, caps);
    } else {
        result_.code = EngineCode::DoNothing;
        result_.extCode = 3;
        handleMainKey(c, caps);
    }

    if (!opts_.freeMark && !isKeyD(c)) {
        if (result_.code == EngineCode::DoNothing) {
            checkGrammar(-1);
        } else {
            checkGrammar(0);
        }
    }

    if (result_.code == EngineCode::Restore) {
        insertKey(c, caps);
        if (stateIndex_ > 0) {
            --stateIndex_;
        }
    }

    // ---- macro key bookkeeping ----
    if (opts_.useMacro) {
        if (result_.code == EngineCode::DoNothing) {
            pushMacroKey(c | (caps ? kCapsMask : 0));
        } else if (result_.code == EngineCode::WillProcess ||
                   result_.code == EngineCode::Restore) {
            const std::size_t bpc = result_.backspaceCount;
            for (std::size_t i = 0; i < bpc && !result_.macroKey.empty(); ++i) {
                result_.macroKey.pop_back();
            }
            const std::size_t from = index_ - bpc;
            for (std::size_t i = from; i < result_.newCharCount + from; ++i) {
                pushMacroKey(typingWord_[i]);
            }
        }
    }

    if (opts_.upperCaseFirstChar) {
        if (index_ == 1 && upperCaseStatus_ == 2) {
            upperCaseFirstCharacter();
        }
        upperCaseStatus_ = 0;
    }

    // ---- case [ ] : standalone-key word boundaries ----
    if (isBracketKey(c) &&
        (isBracketKey(static_cast<char32_t>(result_.newChars[0] & kCharMask)) ||
         opts_.inputMethod == InputMethod::SimpleTelex)) {
        if (index_ - (result_.code == EngineCode::WillProcess ? result_.backspaceCount : 0) > 0) {
            --index_;
            saveWord();
        }
        index_ = 0;
        tempDisableKey_ = false;
        stateIndex_ = 0;
        result_.extCode = 3;
        specialChar_.push_back(c | (caps ? kCapsMask : 0));
    }
}

//===========================================================================
// English-mode macro hook (vEnglishMode)
//===========================================================================
const EngineResult& TextEngine::processEnglishMode(const TextInput& in) {
    result_.code = EngineCode::DoNothing;
    result_.backspaceCount = 0;
    result_.newCharCount = 0;
    result_.extCode = 0;
    result_.newChars[0] = 0;
    result_.macroExpansion.clear();

    if (in.kind == InputKind::MouseDown || (in.otherCtrl && !in.isCaps)) {
        result_.macroKey.clear();
        willTempOffEngine_ = false;
    } else if (in.kind == InputKind::Space) {
        if (!hasHandledMacro_ && findMacro(result_.macroKey, macroData_)) {
            result_.code = EngineCode::ReplaceMacro;
            result_.backspaceCount = static_cast<std::uint8_t>(result_.macroKey.size());
            setMacroExpansionFromResolver();
        }
        result_.macroKey.clear();
        willTempOffEngine_ = false;
    } else if (in.kind == InputKind::Backspace) {
        if (result_.macroKey.size() > 0) {
            result_.macroKey.pop_back();
        } else {
            willTempOffEngine_ = false;
        }
    } else {
        const bool breakKey = (in.kind == InputKind::WordBreak) ||
                              (in.kind == InputKind::Char &&
                               isWordBreakChar(toUpperAscii(in.ch)));
        if (breakKey) {
            result_.macroKey.clear();
            willTempOffEngine_ = false;
        } else if (in.kind == InputKind::Char) {
            if (!willTempOffEngine_) {
                pushMacroKey(toUpperAscii(in.ch) |
                             (in.isCaps ? kCapsMask : 0));
            }
        }
    }
    return result_;
}

//===========================================================================
// Word buffer primitives (insertKey / insertState / save* / restore)
//===========================================================================
void TextEngine::insertKey(char32_t c, bool caps, bool check) {
    if (index_ >= kMaxBuff) {
        // Long-word overflow. Bounded: the undo history can represent at most
        // 64 × kMaxBuff = 2048 chars, so keeping more overflow than that is
        // pure memory growth with no functional benefit (legacy 2.0.5 grew
        // _longWordHelper for the whole session). 2048 entries = 8 KiB.
        if (longWordHelper_.size() < kMaxLongWord) {
            longWordHelper_.push_back(typingWord_[0]);
        }
        for (std::size_t i = 0; i < kMaxBuff - 1; ++i) {
            typingWord_[i] = typingWord_[i + 1];
        }
        typingWord_[kMaxBuff - 1] = c | (caps ? kCapsMask : 0);
    } else {
        typingWord_[index_++] = c | (caps ? kCapsMask : 0);
    }
    if (opts_.checkSpelling && check) {
        checkSpelling();
    }
    // allow d after consonant
    if (c == U'D' && index_ >= 2 && isConsonantAt(index_ - 2)) {
        tempDisableKey_ = false;
    }
}

void TextEngine::pushMacroKey(std::uint32_t v) noexcept {
    // Macro key accumulator — bounded at 255 entries so the uint8
    // backspaceCount cast (EngineResult::backspaceCount, legacy `hBPC`) is
    // always exact. Legacy 2.0.5 grew hMacroKey unboundedly per word and
    // TRUNCATED at >255 (`hBPC = (Byte)hMacroKey.size()`), which produced a
    // wrong backspace count. No real macro is anywhere near 255 chars; beyond
    // it we simply stop accumulating (findMacro can never match anyway).
    if (result_.macroKey.size() < kMaxMacroKey) {
        result_.macroKey.push_back(v);
    }
}

void TextEngine::insertState(char32_t c, bool caps) {
    if (stateIndex_ >= kMaxBuff) {
        for (std::size_t i = 0; i < kMaxBuff - 1; ++i) {
            keyStates_[i] = keyStates_[i + 1];
        }
        keyStates_[kMaxBuff - 1] = c | (caps ? kCapsMask : 0);
    } else {
        keyStates_[stateIndex_++] = c | (caps ? kCapsMask : 0);
    }
}

void TextEngine::saveWord() {
    // Push the current word as one or more fixed-capacity history entries.
    // Arrays + parallel lengths: no heap allocation per entry (legacy used
    // vector-of-vectors → one alloc per word break).
    //
    // The undo-history scratch MUST start empty here. The long-word flush
    // below triggers on size >= kMaxBuff; if typingStatesData_ still holds
    // leftovers from a restored state (backspacing a long word, then typing
    // another long word quickly), the flush would never fire and
    // pushTypingState would write past its fixed-size entry array.
    typingStatesData_.clear();
    if (result_.code != EngineCode::ReplaceMacro) {
        if (index_ > 0) {
            if (longWordHelper_.size() > 0) {
                std::size_t i = 0;
                for (; i < longWordHelper_.size(); ++i) {
                    typingStatesData_.push_back(longWordHelper_[i]);
                    if (typingStatesData_.size() >= kMaxBuff) {
                        pushTypingState();
                    }
                }
                if (!typingStatesData_.empty()) { pushTypingState(); }
                longWordHelper_.clear();
            }
            for (std::size_t i = 0; i < index_; ++i) {
                typingStatesData_.push_back(typingWord_[i]);
            }
            if (!typingStatesData_.empty()) { pushTypingState(); }
        }
    } else {
        for (std::size_t i = 0; i < result_.macroKey.size(); ++i) {
            typingStatesData_.push_back(result_.macroKey[i]);
            if (typingStatesData_.size() >= kMaxBuff) {
                pushTypingState();
            }
        }
        if (!typingStatesData_.empty()) { pushTypingState(); }
    }
}

void TextEngine::saveWord(char32_t keyCode, int count) {
    typingStatesData_.clear();
    for (int i = 0; i < count; ++i) {
        typingStatesData_.push_back(keyCode);
        if (typingStatesData_.size() >= kMaxBuff) {
            pushTypingState();
        }
    }
    if (!typingStatesData_.empty()) { pushTypingState(); }
}

void TextEngine::saveSpecialChar() {
    typingStatesData_.clear();
    for (std::uint32_t ch : specialChar_) {
        typingStatesData_.push_back(ch);
        if (typingStatesData_.size() >= kMaxBuff) {
            pushTypingState();
        }
    }
    if (!typingStatesData_.empty()) { pushTypingState(); }
    specialChar_.clear();
}

void TextEngine::pushTypingState() {
    // Entry is always non-empty here; store fixed array + length.
    // D1 invariant: the scratch can never hold more than one fixed entry —
    // every append site flushes at >= kMaxBuff and the restore path (the
    // only place stale data used to survive) now pre-clears.
    assert(typingStatesData_.size() <= kMaxBuff);
    std::array<std::uint32_t, kMaxBuff> entry{};
    for (std::size_t i = 0; i < typingStatesData_.size(); ++i) {
        entry[i] = typingStatesData_[i];
    }
    typingStates_.push_back(entry);
    typingStatesLen_.push_back(static_cast<std::uint8_t>(typingStatesData_.size()));
    typingStatesData_.clear();

    // Bound the undo history. The stack grows ~128 B per committed word for
    // the WHOLE session (legacy OpenKey had the same flaw, worse: it used
    // vector-of-vectors → 1 heap alloc per word, unbounded). Backspace only
    // ever restores the most recent states, so keeping the tail is exactly
    // right, and memory stays flat at 64 × 128 B ≈ 8 KiB. The cap is keyed
    // on typingStatesLen_ (the half that must never be allowed to grow).
    constexpr std::size_t kMaxHistory = 64;
    if (typingStatesLen_.size() > kMaxHistory) {
        const std::size_t over = typingStatesLen_.size() - kMaxHistory;
        typingStatesLen_.erase(typingStatesLen_.begin(), typingStatesLen_.begin() + over);
        typingStates_.erase(typingStates_.begin(), typingStates_.begin() + over);
    }
}

void TextEngine::restoreLastTypingState() {
    if (typingStates_.size() > 0) {
        typingStatesData_.assign(typingStates_.back().begin(),
                                 typingStates_.back().begin() + typingStatesLen_.back());
        typingStates_.pop_back();
        typingStatesLen_.pop_back();
        if (typingStatesData_.size() > 0) {
            const char32_t first =
                static_cast<char32_t>(typingStatesData_[0] & kCharMask);
            if (first == U' ') {
                spaceCount_ = static_cast<int>(typingStatesData_.size());
                index_ = 0;
            } else if (isCharKeyCodeChar(first)) {
                index_ = 0;
                specialChar_ = typingStatesData_;
                checkSpelling();
            } else {
                for (std::size_t i = 0; i < typingStatesData_.size(); ++i) {
                    typingWord_[i] = typingStatesData_[i];
                }
                index_ = typingStatesData_.size();
            }
        }
    }
    // D1: the scratch is consumed — clear it so no stale entries survive
    // the restore. Before this pre-clear, a later saveWord() on a long word
    // appended onto a full (32-entry) scratch whose == kMaxBuff flush
    // trigger could never fire again, and pushTypingState() copied > 32
    // entries into its fixed 32-slot array (stack-buffer-overflow — the
    // 10 pre-empted suite-7 long-session defects in the mega differential).
    typingStatesData_.clear();
}

//===========================================================================
// Spelling / vowel machinery (port of checkSpelling)
//===========================================================================
void TextEngine::checkSpelling(bool forceCheckVowel) {
    spellingOK_ = false;
    spellingVowelOK_ = true;
    spellingEndIndex_ = index_;

    if (index_ > 0 && chr(index_ - 1) == U']') {
        spellingEndIndex_ = index_ - 1;
    }

    if (spellingEndIndex_ > 0) {
        std::size_t j = 0;
        // ---- check first consonant ----
        if (isConsonantAt(0)) {
            std::size_t i = 0;
            for (; i < kConsonantTable.size(); ++i) {
                spellingFlag_ = false;
                if (spellingEndIndex_ < kConsonantTable[i].size()) {
                    spellingFlag_ = true;
                }
                j = 0;
                for (; j < kConsonantTable[i].size(); ++j) {
                    if (spellingEndIndex_ > j &&
                        (kConsonantTable[i][j] & ~(opts_.quickStartConsonant ? kEndConsonantMask : 0)) != chr(j) &&
                        (kConsonantTable[i][j] & ~(opts_.allowConsonantZfwj ? kConsonantAllowMask : 0)) != chr(j)) {
                        spellingFlag_ = true;
                        break;
                    }
                }
                if (spellingFlag_) {
                    continue;
                }
                break;
            }
        }

        if (j == spellingEndIndex_) {   // for "d" case
            spellingOK_ = true;
        }

        // ---- check next vowel ----
        std::size_t k = j;
        vowelStart_ = k;
        if (chr(vowelStart_) == U'U' && k > 0 && k < spellingEndIndex_ - 1 &&
            chr(vowelStart_ - 1) == U'Q') {
            k = k + 1;
            j = k;
            vowelStart_ = k;
        } else if (index_ >= 2 && chr(0) == U'G' && chr(1) == U'I' &&
                   isConsonantAt(2)) {
            vowelStart_ = k = j = 1;   // fix "gìn"
        }
        std::size_t l = 0;
        for (; l < 3; ++l) {
            if (k < spellingEndIndex_ && !isConsonantAt(k)) {
                ++k;
                vowelEnd_ = k;
            }
        }
        if (k > j) {   // has vowel
            spellingVowelOK_ = false;
            if (k - j > 1 && forceCheckVowel) {
                const auto it = kVowelCombine.find(chr(j));
                if (it != kVowelCombine.end()) {
                    const auto& vowelSet = it->second;
                    for (std::size_t v = 0; v < vowelSet.size(); ++v) {
                        spellingFlag_ = false;
                        std::size_t ii = 1;
                        for (; ii < vowelSet[v].size(); ++ii) {
                            if (j + ii - 1 < spellingEndIndex_ &&
                                vowelSet[v][ii] !=
                                    (chr(j + ii - 1) |
                                     (typingWord_[j + ii - 1] & kToneWMask) |
                                     (typingWord_[j + ii - 1] & kToneMask))) {
                                spellingFlag_ = true;
                                break;
                            }
                        }
                        if (spellingFlag_ || (k < spellingEndIndex_ && !vowelSet[v][0]) ||
                            (j + ii - 1 < spellingEndIndex_ && !isConsonantAt(j + ii - 1))) {
                            continue;
                        }
                        spellingVowelOK_ = true;
                        break;
                    }
                }
            } else if (!isConsonantAt(j)) {
                spellingVowelOK_ = true;
            }

            // ---- continue check last consonant ----
            for (std::size_t ii = 0; ii < kEndConsonantTable.size(); ++ii) {
                spellingFlag_ = false;
                j = 0;
                for (; j < kEndConsonantTable[ii].size(); ++j) {
                    if (spellingEndIndex_ > k + j &&
                        (kEndConsonantTable[ii][j] & ~(opts_.quickEndConsonant ? kEndConsonantMask : 0)) != chr(k + j)) {
                        spellingFlag_ = true;
                        break;
                    }
                }
                if (spellingFlag_) {
                    continue;
                }
                if (k + j >= spellingEndIndex_) {
                    spellingOK_ = true;
                    break;
                }
            }

            // limit: end consonant "ch", "t" cannot combine with ~, `, ?
            if (spellingOK_) {
                if (index_ >= 3 && chr(index_ - 1) == U'H' && chr(index_ - 2) == U'C' &&
                    !((typingWord_[index_ - 3] & kMark1Mask) ||
                      (typingWord_[index_ - 3] & kMark5Mask) ||
                      !(typingWord_[index_ - 3] & kMarkMask))) {
                    spellingOK_ = false;
                } else if (index_ >= 2 && chr(index_ - 1) == U'T' &&
                           !((typingWord_[index_ - 2] & kMark1Mask) ||
                             (typingWord_[index_ - 2] & kMark5Mask) ||
                             !(typingWord_[index_ - 2] & kMarkMask))) {
                    spellingOK_ = false;
                }
            }
        }
    } else {
        spellingOK_ = true;
    }
    tempDisableKey_ = !(spellingOK_ && spellingVowelOK_);
}

//===========================================================================
// Grammar repair after edits (port of checkGrammar)
//===========================================================================
void TextEngine::checkGrammar(int deltaBackspace) {
    if (index_ <= 1 || index_ >= kMaxBuff) {
        return;
    }
    findAndCalculateVowel(true);
    if (vowelCount_ == 0) {
        return;
    }
    isCheckedGrammar_ = false;

    std::size_t l = vowelStart_;

    // "thuơn"/"ưoi"/"ưom"/"ưoc" double-ư repair
    if (index_ >= 3) {
        for (std::size_t i = index_ - 1; i != std::size_t(-1); --i) {
            if (chr(i) == U'N' || chr(i) == U'C' || chr(i) == U'I' ||
                chr(i) == U'M' || chr(i) == U'P' || chr(i) == U'T') {
                if (i >= 2 && chr(i - 1) == U'O' && chr(i - 2) == U'U') {
                    if ((typingWord_[i - 1] & kToneWMask) ^ (typingWord_[i - 2] & kToneWMask)) {
                        typingWord_[i - 2] |= kToneWMask;
                        typingWord_[i - 1] |= kToneWMask;
                        isCheckedGrammar_ = true;
                        break;
                    }
                }
            }
        }
    }

    // move mark onto the correct vowel
    if (index_ >= 2) {
        for (std::size_t i = l; i <= vowelEnd_; ++i) {
            if (typingWord_[i] & kMarkMask) {
                const std::uint32_t mark = typingWord_[i] & kMarkMask;
                typingWord_[i] &= ~kMarkMask;
                insertMark(mark, false);
                if (i != vowelWillSetMark_) {
                    isCheckedGrammar_ = true;
                }
                break;
            }
        }
    }

    // re-arrange data to send back
    if (isCheckedGrammar_) {
        if (result_.code == EngineCode::DoNothing) {
            result_.code = EngineCode::WillProcess;
        }
        result_.backspaceCount = 0;
        std::size_t idx = 0;
        for (std::size_t i = index_ - 1; i != std::size_t(-1); --i) {
            if (i < l) {
                break;
            }
            ++result_.backspaceCount;
            result_.newChars[idx++] = getCharacterCode(typingWord_[i]);
        }
        result_.newCharCount = result_.backspaceCount;
        result_.backspaceCount += static_cast<std::uint8_t>(deltaBackspace);
        result_.extCode = 4;
    }
}

//===========================================================================
// Vowel scanning (findAndCalculateVowel)
//===========================================================================
void TextEngine::findAndCalculateVowel(bool forGrammar) {
    vowelCount_ = 0;
    vowelStart_ = vowelEnd_ = 0;
    for (std::size_t iii = index_ - 1; iii != std::size_t(-1); --iii) {
        if (isConsonantAt(iii)) {
            if (vowelCount_ > 0) {
                break;
            }
        } else {
            if (vowelCount_ == 0) {
                vowelEnd_ = iii;
            }
            if (!forGrammar) {
                if ((iii >= 1 && (chr(iii) == U'I' && chr(iii - 1) == U'G')) ||
                    (iii >= 1 && (chr(iii) == U'U' && chr(iii - 1) == U'Q'))) {
                    break;
                }
            }
            vowelStart_ = iii;
            ++vowelCount_;
        }
    }
    if (vowelStart_ >= 1 && chr(vowelStart_) == U'U' && chr(vowelStart_ - 1) == U'Q') {
        ++vowelStart_;
        --vowelCount_;
    }
}

bool TextEngine::canHasEndConsonant() {
    const auto it = kVowelCombine.find(chr(vowelStart_));
    if (it == kVowelCombine.end()) {
        return false;
    }
    const auto& vo = it->second;
    for (std::size_t ii = 0; ii < vo.size(); ++ii) {
        std::size_t kk = vowelStart_;
        std::size_t iii = 1;
        for (; iii < vo[ii].size(); ++iii) {
            if (kk > vowelEnd_ ||
                (chr(kk) | (typingWord_[kk] & kToneMask) | (typingWord_[kk] & kToneWMask)) != vo[ii][iii]) {
                break;
            }
            ++kk;
        }
        if (iii >= vo[ii].size()) {
            return vo[ii][0] == 1;
        }
    }
    return false;
}

//===========================================================================
// Mark removal / insertion (ports of removeMark / insertMark + mark position)
//===========================================================================
void TextEngine::removeMark() {
    findAndCalculateVowel(true);
    isChanged_ = false;
    if (index_ > 0) {
        for (std::size_t i = vowelStart_; i <= vowelEnd_; ++i) {
            if (typingWord_[i] & kMarkMask) {
                typingWord_[i] &= ~kMarkMask;
                isChanged_ = true;
            }
        }
    }
    if (isChanged_) {
        result_.code = EngineCode::WillProcess;
        result_.backspaceCount = 0;
        std::size_t idx = 0;
        for (std::size_t i = index_ - 1; i != std::size_t(-1); --i) {
            if (i < vowelStart_) {
                break;
            }
            ++result_.backspaceCount;
            result_.newChars[idx++] = getCharacterCode(typingWord_[i]);
        }
        result_.newCharCount = result_.backspaceCount;
    } else {
        result_.code = EngineCode::DoNothing;
    }
}

void TextEngine::handleOldMark() {
    if (vowelCount_ == 0 && chr(vowelEnd_) == U'I') {
        vowelWillSetMark_ = vowelEnd_;
    } else {
        vowelWillSetMark_ = vowelStart_;
    }
    result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelWillSetMark_);

    if (vowelCount_ == 3 ||
        (vowelEnd_ + 1 < index_ && isConsonantAt(vowelEnd_ + 1) && canHasEndConsonant())) {
        vowelWillSetMark_ = vowelStart_ + 1;
        result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelWillSetMark_);
    }

    for (std::size_t ii = vowelStart_; ii <= vowelEnd_; ++ii) {
        if ((chr(ii) == U'E' && typingWord_[ii] & kToneMask) ||
            (chr(ii) == U'O' && typingWord_[ii] & kToneWMask)) {
            vowelWillSetMark_ = ii;
            result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelWillSetMark_);
            break;
        }
    }
    result_.newCharCount = result_.backspaceCount;
}

void TextEngine::handleModernMark() {
    vowelWillSetMark_ = vowelEnd_;
    result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelEnd_);

    // rule 2
    if (vowelCount_ == 3 &&
        ((chr(vowelStart_) == U'O' && chr(vowelStart_ + 1) == U'A' && chr(vowelStart_ + 2) == U'I') ||
         (chr(vowelStart_) == U'U' && chr(vowelStart_ + 1) == U'Y' && chr(vowelStart_ + 2) == U'U') ||
         (chr(vowelStart_) == U'O' && chr(vowelStart_ + 1) == U'E' && chr(vowelStart_ + 2) == U'O') ||
         (chr(vowelStart_) == U'U' && chr(vowelStart_ + 1) == U'Y' && chr(vowelStart_ + 2) == U'A'))) {
        vowelWillSetMark_ = vowelStart_ + 1;
        result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelWillSetMark_);
    } else if ((chr(vowelStart_) == U'O' && chr(vowelStart_ + 1) == U'I') ||
               (chr(vowelStart_) == U'A' && chr(vowelStart_ + 1) == U'I') ||
               (chr(vowelStart_) == U'U' && chr(vowelStart_ + 1) == U'I')) {
        vowelWillSetMark_ = vowelStart_;
        result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelWillSetMark_);
    } else if (vowelEnd_ >= 1 && chr(vowelEnd_ - 1) == U'A' && chr(vowelEnd_) == U'Y') {
        vowelWillSetMark_ = vowelEnd_ - 1;
        result_.backspaceCount = static_cast<std::uint8_t>((index_ - vowelEnd_) + 1);
    } else if (chr(vowelStart_) == U'U' && chr(vowelStart_ + 1) == U'O') {
        vowelWillSetMark_ = vowelStart_ + 1;
        result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelWillSetMark_);
    } else if (chr(vowelStart_ + 1) == U'O' || chr(vowelStart_ + 1) == U'U') {
        vowelWillSetMark_ = vowelEnd_ - 1;
        result_.backspaceCount = static_cast<std::uint8_t>((index_ - vowelEnd_) + 1);
    } else if (chr(vowelStart_) == U'O' || chr(vowelStart_) == U'U') {
        vowelWillSetMark_ = vowelEnd_;
        result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelEnd_);
    }

    // rule 3.1
    if ((chr(vowelStart_) == U'I' && (typingWord_[vowelStart_ + 1] & (U'E' | kToneMask))) ||
        (chr(vowelStart_) == U'Y' && (typingWord_[vowelStart_ + 1] & (U'E' | kToneMask))) ||
        (chr(vowelStart_) == U'U' && typingWord_[vowelStart_ + 1] == (U'O' | kToneMask)) ||
        (typingWord_[vowelStart_] == (U'U' | kToneWMask) &&
         typingWord_[vowelStart_ + 1] == (U'O' | kToneWMask))) {
        if (vowelStart_ + 2 < index_) {
            const char32_t c2 = chr(vowelStart_ + 2);
            if (c2 == U'P' || c2 == U'T' || c2 == U'M' || c2 == U'N' ||
                c2 == U'O' || c2 == U'U' || c2 == U'I' || c2 == U'C' ||
                // v1.1.0: the CH/NH/NG digraph checks compared chr(vS+2)
                // against two different letters each (impossible — dead code
                // carried verbatim from 2.0.5 Engine.cpp:671-673). The intent
                // (and the reason for the VSI+3 guard) is the two-character
                // end cluster at vS+2..vS+3: iêch/iênh/iêng place the mark on
                // the second vowel (kiếch, ý nh…). Compare position vS+3 now.
                (vowelStart_ + 3 < index_ && chr(vowelStart_ + 2) == U'C' && chr(vowelStart_ + 3) == U'H') ||
                (vowelStart_ + 3 < index_ && chr(vowelStart_ + 2) == U'N' && chr(vowelStart_ + 3) == U'H') ||
                (vowelStart_ + 3 < index_ && chr(vowelStart_ + 2) == U'N' && chr(vowelStart_ + 3) == U'G')) {
                vowelWillSetMark_ = vowelStart_ + 1;
                result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelWillSetMark_);
            } else {
                vowelWillSetMark_ = vowelStart_;
                result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelWillSetMark_);
            }
        } else {
            vowelWillSetMark_ = vowelStart_;
            result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelWillSetMark_);
        }
    }
    // rule 3.2 — v1.1.0 correction of the post-audit: the 2.0.5 source
    // carried the ia/ya/ua "ascending diphthong" checks as DEAD code
    // (chr(vS) compared against two different letters — never true), and
    // the v1.1.0 activation of that rule (mark forced onto the FIRST vowel)
    // measurably diverged from BOTH the real 2.0.5 engine and the clean-room
    // oracle for exactly the cases the dead code was never able to cover:
    //   * "ua" followed by an end consonant — kVowelCombine carries {1,U,A},
    //     so rule 4 leaves the rule-7 placement (mark on the SECOND vowel,
    //     "uãm"/quán-family) untouched; the activated rule 3.2 forced the
    //     first vowel instead.
    //   * 3-vowel groups starting "ua" (uai…) — rule 4 never runs (count!=2),
    //     so the activated rule 3.2 again forced the first vowel over the
    //     rule-7 default (last vowel).
    // Rules 4 (I+A/U+A G/Q refinements) and 7 already produce the documented
    // "mía"/"mựa"/"giá" placements for every live input, so the faithful
    // port keeps this branch dead exactly as 2.0.5 shipped it.
    //
    // (rule 4 follows)

    // rule 4
    if (vowelCount_ == 2) {
        if (((chr(vowelStart_) == U'I') && (chr(vowelStart_ + 1) == U'A')) ||
            ((chr(vowelStart_) == U'I') && (chr(vowelStart_ + 1) == U'U')) ||
            ((chr(vowelStart_) == U'I') && (chr(vowelStart_ + 1) == U'O'))) {
            if (vowelStart_ == 0 || chr(vowelStart_ - 1) != U'G') {
                vowelWillSetMark_ = vowelStart_;
                result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelWillSetMark_);
            } else {
                vowelWillSetMark_ = vowelStart_ + 1;
                result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelWillSetMark_);
            }
        } else if ((chr(vowelStart_) == U'U') && (chr(vowelStart_ + 1) == U'A')) {
            if (vowelStart_ == 0 || chr(vowelStart_ - 1) != U'Q') {
                if (vowelEnd_ + 1 >= index_ || !canHasEndConsonant()) {
                    vowelWillSetMark_ = vowelStart_;
                    result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelWillSetMark_);
                }
            } else {
                vowelWillSetMark_ = vowelStart_ + 1;
                result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelWillSetMark_);
            }
        } else if ((chr(vowelStart_) == U'O') && (chr(vowelStart_ + 1) == U'O')) {   // "thoong"
            vowelWillSetMark_ = vowelEnd_;
            result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelWillSetMark_);
        }
    }
}

void TextEngine::insertMark(std::uint32_t markMask, bool canModify) {
    vowelCount_ = 0;

    if (canModify) {
        result_.code = EngineCode::WillProcess;
    }
    result_.backspaceCount = result_.newCharCount = 0;

    findAndCalculateVowel();
    vowelWillSetMark_ = 0;

    if (vowelCount_ == 1) {
        vowelWillSetMark_ = vowelEnd_;
        result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelEnd_);
    } else {
        if (!opts_.useModernOrthography) {
            handleOldMark();
        } else {
            handleModernMark();
        }
        if (typingWord_[vowelEnd_] & kToneMask || typingWord_[vowelEnd_] & kToneWMask) {
            vowelWillSetMark_ = vowelEnd_;
        }
    }

    std::size_t kk = index_ - 1 - vowelStart_;
    if (typingWord_[vowelWillSetMark_] & markMask) {
        // duplicate same mark -> restore
        typingWord_[vowelWillSetMark_] &= ~kMarkMask;
        if (canModify) {
            result_.code = EngineCode::Restore;
        }
        for (std::size_t ii = vowelStart_; ii < index_; ++ii) {
            typingWord_[ii] &= ~kMarkMask;
            result_.newChars[kk--] = getCharacterCode(typingWord_[ii]);
        }
        tempDisableKey_ = true;
    } else {
        typingWord_[vowelWillSetMark_] &= ~kMarkMask;
        typingWord_[vowelWillSetMark_] |= markMask;
        for (std::size_t ii = vowelStart_; ii < index_; ++ii) {
            if (ii != vowelWillSetMark_) {
                typingWord_[ii] &= ~kMarkMask;
            }
            result_.newChars[kk--] = getCharacterCode(typingWord_[ii]);
        }
        result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelStart_);
    }
    result_.newCharCount = result_.backspaceCount;
}

//===========================================================================
// D / ^(AOE) / W handlers
//===========================================================================
void TextEngine::insertD(char32_t /*c*/, bool /*caps*/) {
    result_.code = EngineCode::WillProcess;
    result_.backspaceCount = 0;
    for (std::size_t ii = index_ - 1; ii != std::size_t(-1); --ii) {
        ++result_.backspaceCount;
        if (chr(ii) == U'D') {
            if (typingWord_[ii] & kToneMask) {
                result_.code = EngineCode::Restore;
                typingWord_[ii] &= ~kToneMask;
                result_.newChars[index_ - 1 - ii] = typingWord_[ii];
                tempDisableKey_ = true;
                break;
            }
            typingWord_[ii] |= kToneMask;
            result_.newChars[index_ - 1 - ii] = getCharacterCode(typingWord_[ii]);
            break;
        }
        result_.newChars[index_ - 1 - ii] = getCharacterCode(typingWord_[ii]);
    }
    result_.newCharCount = result_.backspaceCount;
}

void TextEngine::insertAOE(char32_t data, bool /*caps*/) {
    findAndCalculateVowel();

    // remove W tone
    for (std::size_t ii = vowelStart_; ii <= vowelEnd_; ++ii) {
        typingWord_[ii] &= ~kToneWMask;
    }

    result_.code = EngineCode::WillProcess;
    result_.backspaceCount = 0;

    for (std::size_t ii = index_ - 1; ii != std::size_t(-1); --ii) {
        ++result_.backspaceCount;
        if (chr(ii) == data) {   // reverse unicode char
            // v3.3.1: a toggle whose target vowel is NOT the immediately
            // preceding character crossed a final consonant (kVowel['O']
            // patterns {O,N} {O,N,G} …) — the "mono"→"môn" mid-word
            // transform family. The lexicon-gated word-break decision uses
            // this flag: when the raw keystrokes are ALSO a lexicon word,
            // they win at the break (UniKey's mid-word strictness).
            if (ii != index_ - 1) { midWordToggle_ = true; }
            if (typingWord_[ii] & kToneMask) {
                // restore and disable temporary
                result_.code = EngineCode::Restore;
                typingWord_[ii] &= ~kToneMask;
                // Emit the still-decoded character (mark/breve bits may
                // remain: 'ấ' toggles back to 'á', NOT to an invisible 0).
                // Emitting the raw coded entry produced a 0-resolution in
                // resolveChar → the visible character vanished from the
                // replacement while backspaceCount still counted it.
                result_.newChars[index_ - 1 - ii] = getCharacterCode(typingWord_[ii]);
                if (data != U'O') {   // case "thoòng" stays enabled
                    tempDisableKey_ = true;
                }
                break;
            }
            typingWord_[ii] |= kToneMask;
            if (!isKeyD(data)) {
                typingWord_[ii] &= ~kToneWMask;
            }
            result_.newChars[index_ - 1 - ii] = getCharacterCode(typingWord_[ii]);
            break;
        }
        // present old char
        result_.newChars[index_ - 1 - ii] = getCharacterCode(typingWord_[ii]);
    }
    result_.newCharCount = result_.backspaceCount;
}

void TextEngine::insertW(char32_t /*data*/, bool /*caps*/) {
    isRestoredW_ = false;
    findAndCalculateVowel();

    for (std::size_t ii = vowelStart_; ii <= vowelEnd_; ++ii) {
        typingWord_[ii] &= ~kToneMask;
    }

    if (vowelCount_ > 1) {
        result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelStart_);
        result_.newCharCount = result_.backspaceCount;

        if (((typingWord_[vowelStart_] & kToneWMask) && (typingWord_[vowelStart_ + 1] & kToneWMask)) ||
            ((typingWord_[vowelStart_] & kToneWMask) && chr(vowelStart_ + 1) == U'I') ||
            ((typingWord_[vowelStart_] & kToneWMask) && chr(vowelStart_ + 1) == U'A')) {
            result_.code = EngineCode::Restore;
            std::size_t idx = 0;
            for (std::size_t ii = vowelStart_; ii < index_; ++ii) {
                typingWord_[ii] &= ~kToneWMask;
                result_.newChars[index_ - 1 - ii] = getCharacterCode(typingWord_[ii]) & ~kStandaloneMask;
                ++idx;
            }
            isRestoredW_ = true;
            tempDisableKey_ = true;
        } else {
            result_.code = EngineCode::WillProcess;

            if ((chr(vowelStart_) == U'U' && chr(vowelStart_ + 1) == U'O')) {
                if (vowelStart_ >= 2 && typingWord_[vowelStart_ - 2] == U'T' &&
                    typingWord_[vowelStart_ - 1] == U'H') {
                    typingWord_[vowelStart_ + 1] |= kToneWMask;
                    if (vowelStart_ + 2 < index_ && chr(vowelStart_ + 2) == U'N') {
                        typingWord_[vowelStart_] |= kToneWMask;
                    }
                } else if (vowelStart_ >= 1 && typingWord_[vowelStart_ - 1] == U'Q') {
                    typingWord_[vowelStart_ + 1] |= kToneWMask;
                } else {
                    typingWord_[vowelStart_] |= kToneWMask;
                    typingWord_[vowelStart_ + 1] |= kToneWMask;
                }
            } else if ((chr(vowelStart_) == U'U' && chr(vowelStart_ + 1) == U'A') ||
                       (chr(vowelStart_) == U'U' && chr(vowelStart_ + 1) == U'I') ||
                       (chr(vowelStart_) == U'U' && chr(vowelStart_ + 1) == U'U') ||
                       (chr(vowelStart_) == U'O' && chr(vowelStart_ + 1) == U'I')) {
                typingWord_[vowelStart_] |= kToneWMask;
            } else if ((chr(vowelStart_) == U'I' && chr(vowelStart_ + 1) == U'O') ||
                       (chr(vowelStart_) == U'O' && chr(vowelStart_ + 1) == U'A')) {
                typingWord_[vowelStart_ + 1] |= kToneWMask;
            } else {
                tempDisableKey_ = true;
                isChanged_ = false;
                result_.code = EngineCode::DoNothing;
            }

            for (std::size_t ii = vowelStart_; ii < index_; ++ii) {
                result_.newChars[index_ - 1 - ii] = getCharacterCode(typingWord_[ii]);
            }
        }
        return;
    }

    result_.code = EngineCode::WillProcess;
    result_.backspaceCount = 0;

    for (std::size_t ii = index_ - 1; ii != std::size_t(-1); --ii) {
        if (ii < vowelStart_) {
            break;
        }
        ++result_.backspaceCount;
        switch (chr(ii)) {
            case U'A':
            case U'U':
            case U'O':
                if (typingWord_[ii] & kToneWMask) {
                    if (typingWord_[ii] & kStandaloneMask) {
                        result_.code = EngineCode::WillProcess;
                        if (chr(ii) == U'U') {
                            typingWord_[ii] = U'W' | ((typingWord_[ii] & kCapsMask) ? kCapsMask : 0);
                        } else if (chr(ii) == U'O') {
                            result_.code = EngineCode::Restore;
                            typingWord_[ii] = U'O' | ((typingWord_[ii] & kCapsMask) ? kCapsMask : 0);
                            isRestoredW_ = true;
                        }
                        result_.newChars[index_ - 1 - ii] = typingWord_[ii];
                    } else {
                        result_.code = EngineCode::Restore;
                        typingWord_[ii] &= ~kToneWMask;
                        // Emit the still-decoded character — a mark on the
                        // vowel ('ừ' → mark stays when the ư-hook toggles
                        // off) must remain visible, not resolve to 0.
                        result_.newChars[index_ - 1 - ii] = getCharacterCode(typingWord_[ii]);
                        isRestoredW_ = true;
                    }
                    tempDisableKey_ = true;
                } else {
                    typingWord_[ii] |= kToneWMask;
                    typingWord_[ii] &= ~kToneMask;
                    result_.newChars[index_ - 1 - ii] = getCharacterCode(typingWord_[ii]);
                }
                break;
            default:
                result_.newChars[index_ - 1 - ii] = getCharacterCode(typingWord_[ii]);
                break;
        }
    }
    result_.newCharCount = result_.backspaceCount;
}

//===========================================================================
// Standalone [ ] and w handling
//===========================================================================
void TextEngine::reverseLastStandaloneChar(char32_t keyCode, bool caps) {
    result_.code = EngineCode::WillProcess;
    result_.backspaceCount = 0;
    result_.newCharCount = 1;
    result_.extCode = 4;
    typingWord_[index_ - 1] =
        (keyCode | kToneWMask | kStandaloneMask | (caps ? kCapsMask : 0));
    result_.newChars[0] = getCharacterCode(typingWord_[index_ - 1]);
}

void TextEngine::checkForStandaloneChar(char32_t c, bool caps, char32_t keyWillReverse) {
    // v1.1.0 bounds fix: the legacy port indexed typingWord_[index_ - 1]
    // BEFORE the index_ == 0 guards below — with an empty word that is
    // typingWord_[SIZE_MAX] (std::array OOB read; legacy 2.0.5 got away with
    // the signed -1 equivalent on a raw buffer). Guard first, behave
    // identically afterwards.
    if (index_ > 0 && chr(index_ - 1) == keyWillReverse &&
        (typingWord_[index_ - 1] & kToneWMask)) {
        result_.code = EngineCode::WillProcess;
        result_.backspaceCount = 1;
        result_.newCharCount = 1;
        typingWord_[index_ - 1] = c | (caps ? kCapsMask : 0);
        result_.newChars[0] = getCharacterCode(typingWord_[index_ - 1]);
        return;
    }

    // "w" after "u" when reversing to "o": e.g. "uw" -> "ưo"
    if (index_ > 0 && chr(index_ - 1) == U'U' && keyWillReverse == U'O') {
        insertKey(keyWillReverse, caps);
        reverseLastStandaloneChar(keyWillReverse, caps);
        return;
    }

    if (index_ == 0) {
        insertKey(c, caps, false);
        reverseLastStandaloneChar(keyWillReverse, caps);
        return;
    }
    if (index_ == 1) {
        for (std::uint16_t bad : kStandaloneWBad) {
            if (chr(0) == bad) {
                insertKey(c, caps);
                return;
            }
        }
        insertKey(c, caps, false);
        reverseLastStandaloneChar(keyWillReverse, caps);
        return;
    }
    if (index_ == 2) {
        for (const auto& pair : kDoubleWAllowed) {
            if (chr(0) == pair[0] && chr(1) == pair[1]) {
                insertKey(c, caps, false);
                reverseLastStandaloneChar(keyWillReverse, caps);
                return;
            }
        }
        insertKey(c, caps);
        return;
    }
    insertKey(c, caps);
}

//===========================================================================
// Quick Telex / quick consonants / upper-case-first
//===========================================================================
void TextEngine::handleQuickTelex(char32_t c, bool caps) {
    result_.code = EngineCode::WillProcess;
    result_.backspaceCount = 1;
    result_.newCharCount = 2;
    // static_cast: kQuickTelex is keyed on std::uint16_t; an implicit
    // char32_t→uint16_t argument is C4244 under MSVC /W4 /WX.
    const auto it = kQuickTelex.find(static_cast<std::uint16_t>(c));
    if (it == kQuickTelex.end()) {
        result_.code = EngineCode::DoNothing;
        result_.backspaceCount = 0;
        result_.newCharCount = 0;
        insertKey(c, caps);
        return;
    }
    result_.newChars[1] = it->second[0] | (caps ? kCapsMask : 0);
    result_.newChars[0] = it->second[1] | (caps ? kCapsMask : 0);
    insertKey(it->second[1], caps, false);
}

bool TextEngine::checkQuickConsonant() {
    if (index_ <= 1) {
        return false;
    }
    // v1.1.3 bounds fix: the transformation inserts ONE character (index_ +
    // 1 total). At index_ == kMaxBuff - 1 the insert would overflow the
    // fixed buffer bookkeeping: newCharCount was already computed as
    // index_ + 1 = kMaxBuff while the guarded ++index_ no longer ran,
    // emitting a stale 32nd char and a backspaceCount/newCharCount mismatch.
    // A 31+ char word is far beyond any Vietnamese syllable — skip the
    // quick-consonant transform there and let the key insert normally.
    if (index_ >= kMaxBuff - 1) {
        return false;
    }
    int l = 0;
    if (index_ > 0) {
        if (opts_.quickStartConsonant) {
            const auto it = kQuickStartConsonant.find(chr(0));
            if (it != kQuickStartConsonant.end()) {
                result_.code = EngineCode::Restore;
                result_.backspaceCount = static_cast<std::uint8_t>(index_);
                result_.newCharCount = static_cast<std::uint8_t>(index_ + 1);
                if (index_ < kMaxBuff - 1) {
                    ++index_;
                }
                for (std::size_t i = index_ - 1; i >= 2; --i) {
                    typingWord_[i] = typingWord_[i - 1];
                }
                typingWord_[1] = it->second[1] |
                                 (((typingWord_[0] & kCapsMask) && (typingWord_[2] & kCapsMask)) ? kCapsMask : 0);
                typingWord_[0] = it->second[0] | (typingWord_[0] & kCapsMask);
                l = 1;
            }
        }
        if (opts_.quickEndConsonant && index_ >= 2 &&
            !isConsonantAt(index_ - 2)) {
            const auto it = kQuickEndConsonant.find(chr(index_ - 1));
            if (it != kQuickEndConsonant.end()) {
                result_.code = EngineCode::Restore;
                if (l == 1) {
                    ++result_.newCharCount;
                } else {
                    result_.backspaceCount = 1;
                    result_.newCharCount = 2;
                }
                if (index_ < kMaxBuff - 1) {
                    ++index_;
                }
                typingWord_[index_ - 1] =
                    it->second[1] | (typingWord_[index_ - 2] & kCapsMask);
                typingWord_[index_ - 2] =
                    it->second[0] | (typingWord_[index_ - 2] & kCapsMask);
                l = 1;
            }
        }
        if (l == 1) {
            hasHandleQuickConsonant_ = true;
            std::size_t idx = 0;
            for (std::size_t i = index_ - 1; i != std::size_t(-1); --i) {
                result_.newChars[idx++] = getCharacterCode(typingWord_[i]);
            }
            return true;
        }
    }
    return false;
}

void TextEngine::upperCaseFirstCharacter() {
    if (!(typingWord_[0] & kCapsMask)) {
        result_.code = EngineCode::WillProcess;
        result_.backspaceCount = 0;
        result_.newCharCount = 1;
        typingWord_[0] |= kCapsMask;
        result_.newChars[0] = getCharacterCode(typingWord_[0]);
        upperCaseStatus_ = 0;
        if (opts_.useMacro && result_.macroKey.size() > 0) {
            result_.macroKey[0] |= kCapsMask;
        }
    }
}

bool TextEngine::checkRestoreIfWrongSpelling(EngineCode handleCode) {
    for (std::size_t ii = 0; ii < index_; ++ii) {
        if (!isConsonantAt(ii) &&
            (typingWord_[ii] & kMarkMask || typingWord_[ii] & kToneMask ||
             typingWord_[ii] & kToneWMask)) {
            result_.code = handleCode;
            result_.backspaceCount = static_cast<std::uint8_t>(index_);
            result_.newCharCount = static_cast<std::uint8_t>(stateIndex_);
            for (std::size_t i = 0; i < stateIndex_; ++i) {
                typingWord_[i] = keyStates_[i];
                result_.newChars[stateIndex_ - 1 - i] = typingWord_[i];
            }
            index_ = stateIndex_;
            return true;
        }
    }
    return false;
}

bool TextEngine::rawKeysToCodePoints(std::vector<std::uint32_t>& out) const {
    out.clear();
    for (std::size_t i = 0; i < stateIndex_; ++i) {
        const std::uint32_t v = resolveChar(getCharacterCode(keyStates_[i]));
        if (v == 0) { return false; }
        out.push_back(v);
    }
    return true;
}

bool TextEngine::lexiconApprovedAlternative(std::array<std::uint32_t, kMaxBuff>& out,
                                            std::size_t& outLen) {
    outLen = 0;
    if (!dictResolver_ || index_ < 2) { return false; }
    // Candidate family: the uo→ươ W-hook split ("hươ" vs "huơ"). The composed
    // buffer holds a contiguous U|kToneWMask followed by O|kToneWMask (the
    // 2.0.5 W-section hook); the candidate drops ONLY the u's W-hook so the
    // u stays a plain u and the o keeps its ơ ("huơ" — the word UniKey
    // writes). Conservative gating:
    //   * the U/O pair must carry nothing but the W-hook (no tone/mark/caps/
    //     standalone payload on either), so marked forms ("hưở", "quờ"…)
    //     never take this path;
    //   * every other buffer entry must be a plain letter;
    //   * the candidate must be a lexicon word (it never invents text).
    for (std::size_t i = 0; i + 1 < index_; ++i) {
        const std::uint32_t u = typingWord_[i];
        const std::uint32_t o = typingWord_[i + 1];
        constexpr std::uint32_t kOnlyW = kCharMask | kToneWMask;
        if ((u & ~kOnlyW) != 0 || (o & ~kOnlyW) != 0) { continue; }
        if ((u & kCharMask) != U'U' || !(u & kToneWMask)) { continue; }
        if ((o & kCharMask) != U'O' || !(o & kToneWMask)) { continue; }
        bool allPlain = true;
        for (std::size_t j = 0; j < index_; ++j) {
            if (j == i || j == i + 1) { continue; }
            if (typingWord_[j] & ~(kCharMask | kCapsMask)) { allPlain = false; break; }
        }
        if (!allPlain) { continue; }
        // Candidate → code points → lexicon lookup (reuses dictScratch_: the
        // caller's composed-form lookup has already finished with it).
        std::array<std::uint32_t, kMaxBuff> cand = typingWord_;
        cand[i] = u & ~kToneWMask;
        dictScratch_.clear();
        bool resolvable = true;
        for (std::size_t j = 0; j < index_; ++j) {
            const std::uint32_t v = resolveChar(getCharacterCode(cand[j]));
            if (v == 0) { resolvable = false; break; }
            dictScratch_.push_back(v);
        }
        if (!resolvable) { continue; }
        if (dictResolver_(dictScratch_)) {
            out   = cand;
            outLen = index_;
            return true;
        }
    }
    return false;
}

bool TextEngine::checkRestoreIfNotInDictionary(EngineCode handleCode) {
    if (!dictResolver_ || index_ == 0) {
        return false;
    }
    // No transforms applied — the composed form IS the raw typing; there is
    // nothing a dictionary veto could improve (the user typed exactly this).
    bool sameAsRaw = (index_ == stateIndex_);
    for (std::size_t i = 0; sameAsRaw && i < index_; ++i) {
        sameAsRaw = (typingWord_[i] == keyStates_[i]);
    }
    if (sameAsRaw) {
        return false;
    }
    // Composed word → resolved code points → lexicon lookup.
    dictScratch_.clear();
    for (std::size_t i = 0; i < index_; ++i) {
        const std::uint32_t ch = getCharacterCode(typingWord_[i]);
        const std::uint32_t v = resolveChar(ch);
        if (v == 0) { return false; }   // unresolvable — never veto
        dictScratch_.push_back(v);
    }
    if (dictResolver_(dictScratch_)) {
        // v3.3.1 — the composed form is a known Vietnamese word, BUT when a
        // mid-word vowel toggle consumed a key ("mono" → "môn") and the raw
        // keystrokes are ALSO a lexicon word, the raw keystrokes win
        // (lexicon-gated mid-word strictness; matches UniKey's behavior and
        // the corpus for loanwords). Without the flag this branch never
        // changes anything — matched-config parity is untouched.
        if (midWordToggle_ && rawKeysToCodePoints(dictScratch_) &&
            dictResolver_(dictScratch_)) {
            midWordToggle_ = false;
            result_.code = handleCode;
            result_.backspaceCount = static_cast<std::uint8_t>(index_);
            result_.newCharCount = static_cast<std::uint8_t>(stateIndex_);
            for (std::size_t i = 0; i < stateIndex_; ++i) {
                typingWord_[i] = keyStates_[i];
                result_.newChars[stateIndex_ - 1 - i] = typingWord_[i];
            }
            index_ = stateIndex_;
            return true;
        }
        return false;   // composed form is a known Vietnamese word — keep it
    }
    // Composed form is NOT a lexicon word.
    // v3.3.1 — before reverting to raw, offer the partial-composition
    // candidate ("hươ" → "huơ"): when the W-hook split of the composed form
    // is a lexicon word, emit THAT instead of the raw keystrokes.
    std::array<std::uint32_t, kMaxBuff> candidate{};
    std::size_t candidateLen = 0;
    if (lexiconApprovedAlternative(candidate, candidateLen)) {
        midWordToggle_ = false;
        result_.code = handleCode;
        result_.backspaceCount = static_cast<std::uint8_t>(index_);
        result_.newCharCount = static_cast<std::uint8_t>(candidateLen);
        for (std::size_t i = 0; i < candidateLen; ++i) {
            typingWord_[i] = candidate[i];
            // getCharacterCode (not the raw internal entry): the candidate's
            // W-hooked 'o' must resolve to ơ — raw entries carry payload bits
            // that resolveChar alone would strip to the plain key.
            result_.newChars[candidateLen - 1 - i] = getCharacterCode(typingWord_[i]);
        }
        // index_ unchanged: the candidate has exactly the composed length
        // (only the W-hook mask was cleared).
        return true;
    }
    // Lexicon veto: revert the whole pending word to its raw keystrokes.
    midWordToggle_ = false;
    result_.code = handleCode;
    result_.backspaceCount = static_cast<std::uint8_t>(index_);
    result_.newCharCount = static_cast<std::uint8_t>(stateIndex_);
    for (std::size_t i = 0; i < stateIndex_; ++i) {
        typingWord_[i] = keyStates_[i];
        result_.newChars[stateIndex_ - 1 - i] = typingWord_[i];
    }
    index_ = stateIndex_;
    return true;
}

//===========================================================================
// v3.3.1 — seamless tone-style switching ("hoá" <-> "hóa")
//===========================================================================
bool TextEngine::switchToneStyle() {
    const bool oldStyle = opts_.useModernOrthography;
    opts_.useModernOrthography = !oldStyle;

    if (index_ == 0) {
        return false;   // nothing pending — the flip applies to future words
    }
    findAndCalculateVowel();
    if (vowelCount_ <= 1) {
        return false;   // single-vowel group: both styles place the mark alike
    }
    // Locate the current tone mark inside the vowel group.
    std::size_t markPos = static_cast<std::size_t>(-1);
    std::uint32_t markBits = 0;
    for (std::size_t i = vowelStart_; i <= vowelEnd_; ++i) {
        if (typingWord_[i] & kMarkMask) {
            markPos  = i;
            markBits = typingWord_[i] & kMarkMask;
            break;
        }
    }
    if (markPos == static_cast<std::size_t>(-1)) {
        return false;   // no mark pending — the flip applies to future marks
    }
    // Where would the OTHER style place this mark for this exact buffer
    // state? Reuse the engine's own placement rules (the insertMark flow):
    // temporarily clear the mark so handleOldMark/handleModernMark see the
    // "fresh composition" state, then place it at the target position.
    typingWord_[markPos] &= ~kMarkMask;
    if (!opts_.useModernOrthography) {
        handleOldMark();
    } else {
        handleModernMark();
    }
    // insertMark's post-handler override: an existing W-tone on the last
    // vowel pulls the mark to it.
    if (typingWord_[vowelEnd_] & (kToneMask | kToneWMask)) {
        vowelWillSetMark_ = vowelEnd_;
    }
    const std::size_t target = vowelWillSetMark_;
    typingWord_[target] |= markBits;

    if (target == markPos) {
        return false;   // this group is placed identically in both styles
    }
    // Emit the conversion as a normal WillProcess edit — rewrite the word
    // from the vowel-group start (the exact span insertMark rewrites). Net
    // length change is zero: backspaceCount == newCharCount, so the
    // consumer-visible account is untouched (no corruption, no dropped
    // characters — the D2 clamp and its invariants hold by construction).
    result_.code = EngineCode::WillProcess;
    result_.backspaceCount = static_cast<std::uint8_t>(index_ - vowelStart_);
    for (std::size_t i = vowelStart_; i < index_; ++i) {
        if (i != target) {
            typingWord_[i] &= ~kMarkMask;   // never two marks in one group
        }
        result_.newChars[index_ - 1 - i] = getCharacterCode(typingWord_[i]);
    }
    result_.newCharCount = result_.backspaceCount;
    vowelWillSetMark_ = target;   // scratch left consistent for composition
    return true;
}

//===========================================================================
// checkCorrectVowel + main dispatcher (handleMainKey)
//===========================================================================
void TextEngine::checkCorrectVowel(
    const FlatVec<FlatVec<std::uint16_t>>& charset, int& i, int& k, char32_t markKey) {
    // ignore "qu" case
    if (index_ >= 2 && chr(index_ - 1) == U'U' && chr(index_ - 2) == U'Q') {
        isCorect_ = false;
        return;
    }
    k = static_cast<int>(index_ - 1);
    int j = static_cast<int>(charset[static_cast<std::size_t>(i)].size()) - 1;
    for (; j >= 0; --j) {
        if ((charset[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] &
             ~(opts_.quickEndConsonant ? kEndConsonantMask : 0)) != chr(static_cast<std::size_t>(k))) {
            isCorect_ = false;
            return;
        }
        --k;
        if (k < 0) {
            break;
        }
    }

    // limit mark for end consonant "C"/"T"
    if (isCorect_ && charset[static_cast<std::size_t>(i)].size() > 1 &&
        (isKeyF(markKey) || isKeyX(markKey) || isKeyR(markKey))) {
        if (charset[static_cast<std::size_t>(i)][1] == U'C' ||
            charset[static_cast<std::size_t>(i)][1] == U'T') {
            isCorect_ = false;
        } else if (charset[static_cast<std::size_t>(i)].size() > 2 &&
                   charset[static_cast<std::size_t>(i)][2] == U'T') {
            isCorect_ = false;
        }
    }

    if (isCorect_ && k >= 0 && chr(static_cast<std::size_t>(k)) == chr(static_cast<std::size_t>(k + 1))) {
        isCorect_ = false;
    }
}

void TextEngine::handleMainKey(char32_t c, bool caps) {
    // ---- Z key removes mark ----
    if (isKeyZ(c)) {
        removeMark();
        if (!isChanged_) {
            insertKey(c, caps);
        }
        return;
    }

    if (c == U'[') {
        checkForStandaloneChar(c, caps, U'O');
        return;
    }
    if (c == U']') {
        checkForStandaloneChar(c, caps, U'U');
        return;
    }

    // ---- D key (đ) ----
    if (isKeyD(c)) {
        isCorect_ = false;
        isChanged_ = false;
        int k = static_cast<int>(index_);
        int i = 0;
        for (; i < static_cast<int>(kConsonantD.size()); ++i) {
            if (index_ < kConsonantD[static_cast<std::size_t>(i)].size()) {
                continue;
            }
            isCorect_ = true;
            checkCorrectVowel(kConsonantD, i, k, c);
            // allow d after consonant
            if (!isCorect_ && index_ >= 2 && chr(index_ - 1) == U'D' &&
                isConsonantAt(index_ - 2)) {
                isCorect_ = true;
            }
            if (isCorect_) {
                isChanged_ = true;
                insertD(c, caps);
                break;
            }
        }
        if (!isChanged_) {
            insertKey(c, caps);
        }
        return;
    }

    // ---- mark keys (S F R X J / 1-5) ----
    if (isMarkKey(c)) {
        // UPSTREAM FIX (master): iterate the map by entry, not operator[](0..5)
        for (const auto& vowelEntry : kVowelForMark) {
            const auto& charset = vowelEntry.second;
            isCorect_ = false;
            isChanged_ = false;
            int k = static_cast<int>(index_);
            int l = 0;
            for (; l < static_cast<int>(charset.size()); ++l) {
                if (index_ < charset[static_cast<std::size_t>(l)].size()) {
                    continue;
                }
                isCorect_ = true;
                checkCorrectVowel(charset, l, k, c);
                if (isCorect_) {
                    isChanged_ = true;
                    if (isKeyS(c))       { insertMark(kMark1Mask); }
                    else if (isKeyF(c))  { insertMark(kMark2Mask); }
                    else if (isKeyR(c))  { insertMark(kMark3Mask); }
                    else if (isKeyX(c))  { insertMark(kMark4Mask); }
                    else if (isKeyJ(c))  { insertMark(kMark5Mask); }
                    break;
                }
            }
            if (isCorect_) {
                break;
            }
        }
        if (!isChanged_) {
            insertKey(c, caps);
        }
        return;
    }

    // ---- vowel keys ----
    if (opts_.inputMethod == InputMethod::Vni) {
        // v1.1.0: validity-tracked scan — no O/A/E in the buffer must leave
        // the PREVIOUS word's index in vowelEnd_ (stale → digit 6 composed
        // onto a phantom vowel). Guarded again at the isKeyW 7/8 path below.
        vniVowelEndValid_ = false;
        for (std::size_t i = index_ - 1; i != std::size_t(-1); --i) {
            if (chr(i) == U'O' || chr(i) == U'A' || chr(i) == U'E') {
                vowelEnd_ = i;
                vniVowelEndValid_ = true;
                break;
            }
        }
    }

    const char32_t keyForAEO =
        (opts_.inputMethod != InputMethod::Vni)
            ? c
            : ((c == U'7' || c == U'8') ? U'W'
                                        : (c == U'6' ? (vniVowelEndValid_
                                                          ? static_cast<char32_t>(chr(vowelEnd_))
                                                          : U'\0')
                                                     : c));

    // static_cast: kVowel is keyed on std::uint16_t; the ternary above
    // promotes to char32_t and an implicit narrowing argument is C4244
    // under MSVC /W4 /WX.
    const auto vowelIt = kVowel.find(static_cast<std::uint16_t>(keyForAEO));
    static const FlatVec<FlatVec<std::uint16_t>> kEmpty{};
    const auto& charset = (vowelIt != kVowel.end()) ? vowelIt->second : kEmpty;

    isCorect_ = false;
    isChanged_ = false;
    int k = static_cast<int>(index_);
    int i = 0;
    for (; i < static_cast<int>(charset.size()); ++i) {
        if (index_ < charset[static_cast<std::size_t>(i)].size()) {
            continue;
        }
        isCorect_ = true;
        checkCorrectVowel(charset, i, k, c);
        if (isCorect_) {
            isChanged_ = true;
            if (isKeyDouble(c)) {
                insertAOE(keyForAEO, caps);
            } else if (isKeyW(c)) {
                if (opts_.inputMethod == InputMethod::Vni) {
                    vniVowelEndValid_ = false;
                    for (std::size_t j = index_ - 1; j != std::size_t(-1); --j) {
                        if (chr(j) == U'O' || chr(j) == U'U' || chr(j) == U'A' ||
                            chr(j) == U'E') {
                            vowelEnd_ = j;
                            vniVowelEndValid_ = true;
                            break;
                        }
                    }
                    if (vniVowelEndValid_ &&
                        ((c == U'7' && chr(vowelEnd_) == U'A' &&
                          (vowelEnd_ >= 1 ? chr(vowelEnd_ - 1) != U'U' : true)) ||
                         (c == U'8' && (chr(vowelEnd_) == U'O' || chr(vowelEnd_) == U'U')))) {
                        break;
                    }
                }
                insertW(keyForAEO, caps);
            }
            break;
        }
    }

    if (!isChanged_) {
        if (c == U'W' && opts_.inputMethod != InputMethod::SimpleTelex) {
            checkForStandaloneChar(c, caps, U'U');
        } else {
            insertKey(c, caps);
        }
    }
}

//===========================================================================
// Character-code resolution (port of getCharacterCode)
//===========================================================================
std::uint32_t TextEngine::getCharacterCode(const std::uint32_t& data) const noexcept {
    const int capsElem = (data & kCapsMask) ? 0 : 1;
    const std::uint32_t key = data & kCharMask;
    const auto& table = codeTableFor(static_cast<int>(opts_.codeTable));

    if (data & kMarkMask) {   // has tone mark
        int markElem = -2;
        switch (data & kMarkMask) {
            case kMark1Mask: markElem = 0; break;
            case kMark2Mask: markElem = 2; break;
            case kMark3Mask: markElem = 4; break;
            case kMark4Mask: markElem = 6; break;
            case kMark5Mask: markElem = 8; break;
            default: break;
        }
        markElem += capsElem;

        if (key == U'A' || key == U'O' || key == U'U' || key == U'E') {
            if ((data & kToneMask) == 0 && (data & kToneWMask) == 0) {
                markElem += 4;
            }
        }

        std::uint32_t rowKey = key;
        if (data & kToneMask) {
            rowKey |= kToneMask;
        } else if (data & kToneWMask) {
            rowKey |= kToneWMask;
        }
        const auto it = table.find(rowKey);
        if (it == table.end()) {
            return data;
        }
        const auto& row = it->second;
        if (markElem < 0 || static_cast<std::size_t>(markElem) >= row.size()) {
            return data;
        }
        return row[static_cast<std::size_t>(markElem)] | kCharCodeMask;
    }

    // no mark
    const auto it = table.find(key);
    if (it == table.end()) {
        return data;
    }
    const auto& row = it->second;
    if (data & kToneMask) {
        return row[static_cast<std::size_t>(capsElem)] | kCharCodeMask;
    }
    if (data & kToneWMask) {
        return row[static_cast<std::size_t>(capsElem + 2)] | kCharCodeMask;
    }
    return data;
}

//===========================================================================
// Output resolution → ready-to-insert UTF-16 (kills clipboard+Shift+Insert)
//===========================================================================
std::uint32_t TextEngine::resolveChar(std::uint32_t coded) const noexcept {
    if (coded & kPureCharMask) {
        return coded & kCharMask;   // pure character
    }
    if (coded & kCharCodeMask) {
        return coded & kCharMask;   // already a final character (from a table)
    }
    if (coded & kStandaloneMask) {
        // Standalone ư/ơ entry (D2 root fix). A later transform (tone-mark
        // insert, mark move, …) can strip the entry's kToneWMask while the
        // standalone marker stays; the entry must still resolve to the
        // visible vowel it represents — previously it fell through to
        // keyCodeToCharacter(), which rejected the extra payload bits and
        // returned 0, so the character silently VANISHED from the emitted
        // replacement while backspaceCount still counted it (the committed
        // text desynchronized → the over-backspace family). Matches the
        // 2.0.5 hook, which re-types the standalone vowel.
        const char32_t base = static_cast<char32_t>(coded & kCharMask);
        const bool caps = (coded & kCapsMask) != 0;
        if (base == U'U') { return static_cast<std::uint32_t>(caps ? 0x1AF : 0x1B0); } // Ư ư
        if (base == U'O') { return static_cast<std::uint32_t>(caps ? 0x1A0 : 0x1A1); } // Ơ ơ
        return static_cast<std::uint32_t>(base);   // defensive — not produced
    }
    // Plain key code (e.g. trailing consonant 'N', or 'U'/'O' without tone):
    // resolve to the key the legacy hook would re-type (SendKeyCode sends
    // the VK + caps — internal payload bits like a stray mark mask on a
    // consonant are engine-internal and must never blank the character).
    return static_cast<std::uint32_t>(
        keyCodeToCharacter((coded & kCharMask) | (coded & kCapsMask)));
}

//---------------------------------------------------------------------------
// keyCodeToCharacter — reverse of the legacy _characterMap. The table keys
// are key codes (identity = uppercase letter, digit, or punctuation VK),
// optionally OR'd with kCapsMask; values are the produced characters.
//---------------------------------------------------------------------------
char32_t TextEngine::keyCodeToCharacter(std::uint32_t data) noexcept {
    // Direct [caps?][vk] table built at compile time — same mapping as the
    // legacy static std::map, but O(1) with zero indirection (the legacy
    // RB-tree walk was on the hot path: resolveChar() -> keyCodeToCharacter()
    // per output character).
    constexpr std::array<std::array<char32_t, 256>, 2> kTable = [] {
        std::array<std::array<char32_t, 256>, 2> t{};
        for (std::size_t i = 0; i < 256; ++i) { t[0][i] = 0; t[1][i] = 0; }
        // a–z / A–Z  (KEY_A == 0x41 == 'A')
        for (char32_t c = U'a'; c <= U'z'; ++c) {
            const std::uint32_t vk = static_cast<std::uint32_t>(c - 32);
            t[0][vk] = c;
            t[1][vk] = static_cast<char32_t>(c - 32);
        }
        // 1–0 / shifted symbols (KEY_1 == 0x31 …)
        constexpr char32_t digits[10]  = {U'1',U'2',U'3',U'4',U'5',U'6',U'7',U'8',U'9',U'0'};
        constexpr char32_t shiftedD[10] = {U'!',U'@',U'#',U'$',U'%',U'^',U'&',U'*',U'(',U')'};
        for (int i = 0; i < 10; ++i) {
            const std::uint32_t vk = static_cast<std::uint32_t>(U'0' + ((i + 1) % 10));
            t[0][vk] = digits[i];
            t[1][vk] = shiftedD[i];
        }
        // punctuation pairs {lower, shifted, VK}
        constexpr std::array<std::tuple<char32_t, char32_t, std::uint32_t>, 11> punct = {{
            {U'`', U'~', 0xDC}, {U'-', U'_', 0xBD}, {U'=', U'+', 0xBB},
            {U'[', U'{', 0xDB}, {U']', U'}', 0xDD}, {U'\\', U'|', 0xDE},
            {U';', U':', 0xBA}, {U'\'', U'"', 0xC0}, {U',', U'<', 0xBC},
            {U'.', U'>', 0xBE}, {U'/', U'?', 0xBF},
        }};
        for (const auto& [lo, hi, vk] : punct) {
            t[0][vk] = lo;
            t[1][vk] = hi;
        }
        t[0][0x20] = U' ';   // KEY_SPACE
        // ASCII identity for the standalone-bracket keys: KieeKey's
        // normalized input carries the produced CHARACTER, so '[' (0x5B) and
        // ']' (0x5D) are stored raw in the word buffer / key-state history
        // (they are the Telex standalone ơ/ư keys). A later rewrite that
        // re-emits them must render the bracket, not resolve to 0 (the VK
        // codes 0xDB/0xDD above are what the legacy hook sent; both
        // identities are valid here).
        t[0][0x5B] = U'['; t[1][0x5B] = U'[';
        t[0][0x5D] = U']'; t[1][0x5D] = U']';
        return t;
    }();

    // Only the caps bit may be set above the VK byte; anything else is
    // "not found" (matches the legacy map returning 0).
    if ((data & 0xFFFF0000u) != 0 && (data & 0xFFFF0000u) != kCapsMask) {
        return 0;
    }
    return kTable[(data >> 16) & 1u][data & 0xFFu];
}

namespace {
// Append one code point as UTF-16 (surrogate pair for astral planes).
void appendUtf16(std::wstring& out, char32_t cp) {
    if (cp <= 0xFFFF) {
        out.push_back(static_cast<wchar_t>(cp));
    } else if (cp <= 0x10FFFF) {
        const char32_t t = cp - 0x10000;
        out.push_back(static_cast<wchar_t>(0xD800 + (t >> 10)));
        out.push_back(static_cast<wchar_t>(0xDC00 + (t & 0x3FF)));
    }
}
} // namespace

std::wstring TextEngine::replacementUtf16(const EngineResult& r) const {
    std::wstring out;
    replacementUtf16(r, out);
    return out;
}

void TextEngine::macroExpansionUtf16(const EngineResult& r, std::wstring& out) const {
    out.clear();
    for (std::uint32_t v : r.macroExpansion) {
        appendUtf16(out, static_cast<char32_t>(v));
    }
}

namespace {
// P3: VIQR mnemonic output — precomposed Vietnamese -> pure ASCII.
// (Complete map for the Vietnamese TetxLatin Extended Additional set the
// engine can produce; plain ASCII letters and đ/Đ map to dd/DD.)
struct ViqrPair { char32_t cp; const char* viqr; };
inline constexpr ViqrPair kViqrMap[] = {
    {0x00C0, "A`"}, {0x00E0, "a`"}, {0x00C1, "A'"}, {0x00E1, "a'"},
    {0x1EA2, "A?"}, {0x1EA3, "a?"}, {0x00C3, "A~"}, {0x00E3, "a~"},
    {0x1EA0, "A."}, {0x1EA1, "a."},
    {0x0102, "AA"}, {0x0103, "aa"}, {0x1EAE, "AA'"}, {0x1EAF, "aa'"},
    {0x1EB0, "AA`"}, {0x1EB1, "aa`"}, {0x1EB2, "AA?"}, {0x1EB3, "aa?"},
    {0x1EB4, "AA~"}, {0x1EB5, "aa~"}, {0x1EB6, "AA."}, {0x1EB7, "aa."},
    {0x00C2, "A^"}, {0x00E2, "a^"}, {0x1EA4, "A^'"}, {0x1EA5, "a^'"},
    {0x1EA6, "A^`"}, {0x1EA7, "a^`"}, {0x1EA8, "A^?"}, {0x1EA9, "a^?"},
    {0x1EAA, "A^~"}, {0x1EAB, "a^~"}, {0x1EAC, "A^."}, {0x1EAD, "a^."},
    {0x00C8, "E`"}, {0x00E8, "e`"}, {0x00C9, "E'"}, {0x00E9, "e'"},
    {0x1EBA, "E?"}, {0x1EBB, "e?"}, {0x1EBC, "E~"}, {0x1EBD, "e~"},
    {0x1EB8, "E."}, {0x1EB9, "e."},
    {0x00CA, "E^"}, {0x00EA, "e^"}, {0x1EBE, "E^'"}, {0x1EBF, "e^'"},
    {0x1EC0, "E^`"}, {0x1EC1, "e^`"}, {0x1EC2, "E^?"}, {0x1EC3, "e^?"},
    {0x1EC4, "E^~"}, {0x1EC5, "e^~"}, {0x1EC6, "E^."}, {0x1EC7, "e^."},
    {0x00CC, "I`"}, {0x00EC, "i`"}, {0x00CD, "I'"}, {0x00ED, "i'"},
    {0x1EC8, "I?"}, {0x1EC9, "i?"}, {0x0128, "I~"}, {0x0129, "i~"},
    {0x1ECA, "I."}, {0x1ECB, "i."},
    {0x00D2, "O`"}, {0x00F2, "o`"}, {0x00D3, "O'"}, {0x00F3, "o'"},
    {0x1ECE, "O?"}, {0x1ECF, "o?"}, {0x00D5, "O~"}, {0x00F5, "o~"},
    {0x1ECC, "O."}, {0x1ECD, "o."},
    {0x00D4, "O^"}, {0x00F4, "o^"}, {0x1ED0, "O^'"}, {0x1ED1, "o^'"},
    {0x1ED2, "O^`"}, {0x1ED3, "o^`"}, {0x1ED4, "O^?"}, {0x1ED5, "o^?"},
    {0x1ED6, "O^~"}, {0x1ED7, "o^~"}, {0x1ED8, "O^."}, {0x1ED9, "o^."},
    {0x01A0, "O+"}, {0x01A1, "o+"}, {0x1EDA, "O+'"}, {0x1EDB, "o+'"},
    {0x1EDC, "O+`"}, {0x1EDD, "o+`"}, {0x1EDE, "O+?"}, {0x1EDF, "o+?"},
    {0x1EE0, "O+~"}, {0x1EE1, "o+~"}, {0x1EE2, "O+."}, {0x1EE3, "o+."},
    {0x00D9, "U`"}, {0x00F9, "u`"}, {0x00DA, "U'"}, {0x00FA, "u'"},
    {0x1EE6, "U?"}, {0x1EE7, "u?"}, {0x00DB, "U~"}, {0x0169, "u~"},
    {0x1EE4, "U."}, {0x1EE5, "u."},
    {0x01AF, "U+"}, {0x01B0, "u+"}, {0x1EE8, "U+'"}, {0x1EE9, "u+'"},
    {0x1EEA, "U+`"}, {0x1EEB, "u+`"}, {0x1EEC, "U+?"}, {0x1EED, "u+?"},
    {0x1EEE, "U+~"}, {0x1EEF, "u+~"}, {0x1EF0, "U+."}, {0x1EF1, "u+."},
    {0x1EF2, "Y`"}, {0x1EF3, "y`"}, {0x00DD, "Y'"}, {0x00FD, "y'"},
    {0x1EF6, "Y?"}, {0x1EF7, "y?"}, {0x1EF8, "Y~"}, {0x1EF9, "y~"},
    {0x1EF4, "Y."}, {0x1EF5, "y."},
    {0x0110, "DD"}, {0x0111, "dd"},
};

void appendViqr(std::wstring& out, char32_t cp) {
    for (const ViqrPair& p : kViqrMap) {
        if (p.cp == cp) {
            for (const char* q = p.viqr; *q; ++q) { out.push_back(static_cast<wchar_t>(*q)); }
            return;
        }
    }
    out.push_back(static_cast<wchar_t>(cp));   // plain ASCII passes through
}
} // namespace

void TextEngine::replacementUtf16(const EngineResult& r, std::wstring& out) const {
    // Decode engine encoding → UTF-16, OLDEST-FIRST. The engine fills
    // newChars MOST-RECENT-FIRST (matching the legacy buffer); the legacy
    // hook sent them via `for (_k = newCharCount - 1; _k >= 0; _k--)`, i.e.
    // OLDEST-FIRST — so iterate in reverse to reproduce the exact character
    // order the legacy consumer produced.
    out.clear();
    out.reserve(r.newCharCount + 1);
    for (std::size_t i = r.newCharCount; i-- > 0;) {
        std::uint32_t v = resolveChar(r.newChars[i]);
        if (v & kCharCodeMask) {
            v &= kCharMask;
        }
        if (v == 0) {
            continue;
        }
        // "Unicode Compound" marks (table 3) encode base+mark in one word
        if (opts_.codeTable == CodeTable::UnicodeCompound && (v & 0xE000u)) {
            const std::uint32_t markIdx = (v >> 13) & 0x7;
            const char32_t base = static_cast<char32_t>(v & 0x1FFFu);
            appendUtf16(out, base);
            if (markIdx > 0 && markIdx <= kUnicodeCompoundMarks.size()) {
                appendUtf16(out, static_cast<char32_t>(kUnicodeCompoundMarks[markIdx - 1]));
            }
        } else if (opts_.outputEncoding == OutputEncoding::Viqr) {
            appendViqr(out, static_cast<char32_t>(v));
        } else {
            appendUtf16(out, static_cast<char32_t>(v));
        }
    }
}

//===========================================================================
// findMacro + finalize
//===========================================================================
bool TextEngine::findMacro(const std::vector<std::uint32_t>& key,
                           std::vector<std::uint32_t>& data) {
    if (!macroResolver_) {
        return false;
    }
    return macroResolver_(key, data);
}

void TextEngine::setMacroExpansionFromResolver() {
    // The macro resolver contract (see EngineResult::macroExpansion): the
    // resolver supplies FINAL Unicode code points. Internal payload tags are
    // stripped defensively so a legacy-style resolver (char-code-tagged
    // entries) still renders correctly; caps-masked plain key codes are
    // resolved through the same table the legacy hook used.
    result_.macroExpansion.clear();
    result_.macroExpansion.reserve(macroData_.size());
    for (std::uint32_t v : macroData_) {
        if (v & (kCharCodeMask | kPureCharMask)) {
            v &= kCharMask;
        } else if (v & kStandaloneMask) {
            v = resolveChar(v);
        } else if (v & kCapsMask) {
            const char32_t mapped = keyCodeToCharacter(v);
            if (mapped != 0) { v = static_cast<std::uint32_t>(mapped); }
            // else: keep as-is (the consumer renders unknown codes literally)
        }
        result_.macroExpansion.push_back(v);
    }
}

void TextEngine::finalizeResult() noexcept {
    if (result_.newCharCount > kMaxBuff) {
        result_.newCharCount = static_cast<std::uint8_t>(kMaxBuff);
    }
    // If the engine decided "restore", the consumer replaces backspaceCount
    // chars and inserts newChars; anything left in the buffer is ignored.
}

} // namespace ok::text
