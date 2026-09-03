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
// File: src/core/TextEngine.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.3 — TextEngine.hpp
// C++20 string_view based Telex / VNI / Simple-Telex state machine.
//
// Modernization vs. the 2.0.5 engine (Engine.cpp):
//   * Every legacy file-scope global (TypingWord, _index, _stateIndex,
//     _spaceCount, tempDisableKey, vowel indices, …) is now private member
//     state of a TextEngine instance  ⇒ reentrant, thread-safe per instance,
//     unit-testable, and one instance per IME session instead of one for the
//     whole process.
//   * Input is a normalized TextInput (char32_t from the keyboard layout +
//     VK code for control keys) — layout independent, no hardcoded character
//     map, no `wstring_convert` (removed in C++26).
//   * Output is ready-to-insert UTF-16 (EngineResult::replacementUtf16),
//     produced by resolving the internal bit-encoding to final characters —
//     the consumer feeds this straight to TSF / SendInput. The legacy
//     clipboard + Shift+Insert round-trip is GONE (its race conditions were
//     the #1 cause of duplicate/ghost letters).
//   * No macros: IS_KEY_* / IS_MARK_KEY / CHR / IS_DOUBLE_CODE become
//     constexpr member functions.
//   * std::array buffers + bounds-checked accessors; MAX_BUFF=32 preserved.
//----------------------------------------------------------------------------
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "VietnameseTables.hpp"

namespace ok::text {

inline constexpr std::size_t kMaxBuff = 32;
// Bounded-growth limits (see TextEngine.cpp for rationale):
// * Macro key accumulator must stay ≤ 255 so the uint8 backspaceCount cast
//   (legacy `hBPC = (Byte)hMacroKey.size()`) is exact.
// * Long-word overflow buffer: undo history can represent at most
//   64 × kMaxBuff = 2048 chars, so overflow beyond that is pure memory.
inline constexpr std::size_t kMaxMacroKey = 255;
inline constexpr std::size_t kMaxLongWord = 2048;

//---------------------------------------------------------------------------
// Input method / code table enums (same numeric values as v2.0.5 settings)
//---------------------------------------------------------------------------
enum class InputMethod : std::uint8_t { Telex = 0, Vni = 1, SimpleTelex = 2 };
enum class CodeTable : std::uint8_t {
    Unicode = 0, Tcvn3 = 1, VniWindows = 2, UnicodeCompound = 3, Cp1258 = 4,
};

// P3 (v3.1): output encoding beyond the code tables. VIQR renders the
// composed Vietnamese as pure-ASCII mnemonic markup (DDBB, a`, o^, o+ …)
// for terminals/channels that cannot carry precomposed Unicode.
enum class OutputEncoding : std::uint8_t { Unicode = 0, Viqr = 1 };

//---------------------------------------------------------------------------
// Normalized input from the keyboard layer (see ModernKeyHook consumer).
//---------------------------------------------------------------------------
enum class InputKind : std::uint8_t {
    Char,        // printable character produced by the key (ch valid)
    Space,       // VK_SPACE
    Backspace,   // VK_BACK
    WordBreak,   // Esc/Tab/Enter/Arrows/Home/End/Delete/… (vkCode valid)
    MouseDown,   // implicit word break
};

struct TextInput {
    InputKind kind        = InputKind::Char;
    char32_t  ch          = 0;      // produced character (already lowercased by caller? no — keep as-is)
    std::uint16_t vkCode  = 0;      // VK_* for control kinds
    bool      isCaps      = false;  // shift OR caps-lock active
    bool      otherCtrl   = false;  // ctrl/alt held
};

//---------------------------------------------------------------------------
// Engine decision, mirroring the legacy vKeyHookState codes.
//---------------------------------------------------------------------------
enum class EngineCode : std::uint8_t {
    DoNothing = 0,                 // pass the key through untouched
    WillProcess,                   // engine rewrote the pending word
    BreakWord,                     // start new session
    Restore,                       // undo last transformation, re-type key
    ReplaceMacro,                  // consumer should expand a macro
    RestoreAndStartNewSession,
};

//---------------------------------------------------------------------------
// Result handed to the consumer (composer) thread.
//---------------------------------------------------------------------------
struct EngineResult {
    EngineCode code          = EngineCode::DoNothing;
    std::uint8_t backspaceCount = 0;   // chars to erase before insertion
    std::uint8_t newCharCount   = 0;   // chars in newChars
    std::uint8_t extCode        = 0;   // 1 break / 2 delete / 3 normal / 4 no-empty-char
    std::array<std::uint32_t, kMaxBuff> newChars{};  // internal encoding
    std::vector<std::uint32_t> macroKey{};           // macro bookkeeping (consumer)
    // ReplaceMacro payload (D3 contract): the resolved macro expansion as
    // final Unicode code points, ready to type after deleting
    // backspaceCount characters. Empty unless code == ReplaceMacro. The
    // macro RESOLVER supplies final code points (letters already
    // case-adjusted, Vietnamese precomposed); the engine only strips the
    // internal kCharCodeMask / kPureCharMask tags if a resolver sets them.
    // Consumers MUST apply the expansion themselves (shipped Win32 hook,
    // TSF composer and the verification harness all do) — pre-v3.1 the
    // shipped consumer let the raw key through, which silently dropped
    // every macro expansion (462,627 gap events in the mega differential).
    std::vector<std::uint32_t> macroExpansion{};

    [[nodiscard]] bool consumed() const noexcept {
        return code != EngineCode::DoNothing;
    }
};

//---------------------------------------------------------------------------
// Engine options (mirror the v2.0.5 settings globals).
//---------------------------------------------------------------------------
struct EngineOptions {
    InputMethod inputMethod   = InputMethod::Telex;
    CodeTable   codeTable     = CodeTable::Unicode;
    bool checkSpelling        = true;
    bool useModernOrthography = false;   // oà/uý vs òa/úy
    bool quickTelex           = false;
    bool restoreIfWrongSpelling = true;
    bool freeMark             = false;   // "tự do dấu" (marks allowed on any vowel)
    bool allowConsonantZfwj   = false;
    bool quickStartConsonant  = false;
    bool quickEndConsonant    = false;
    bool upperCaseFirstChar   = false;
    bool useMacro             = true;
    bool useMacroInEnglishMode = false;
    // P1 (v3.1): dictionary-assisted foreign-word protection — the v3.1
    // word-break restore decision consults a general Vietnamese lexicon:
    // a pending composition is reverted to the raw keystrokes only when the
    // composed form is NOT a lexicon word and differs from the raw typing.
    // With the feature OFF the engine is byte-identical to v3.0 (lockstep).
    bool useDictionaryRestore = false;
    // P3 (v3.1): VIQR output (pure-ASCII mnemonic markup).
    OutputEncoding outputEncoding = OutputEncoding::Unicode;
    // P3 (v3.1): user-defined keymap enabled (the map itself is installed
    // via setKeymapOverride — UniKey-parity feature).
    bool useUserKeymap = false;
    // v1.1.2 — "numbers are numbers": when TRUE, digits 0-9 NEVER act as
    // Vietnamese composition keys (VNI tone/vowel/d-bar/tone-removal keys)
    // and always type the literal digit — even mid-word, in every input
    // method. This kills the whole "I typed a number and the previous word
    // got a tone mark / turned into đ" bug class for users who do NOT type
    // Vietnamese by the VNI digit convention. Telex / Simple Telex are
    // unaffected either way: digits already pass through there.
    //
    // v1.1.2-r3 ROOT CAUSE FIX: the LIBRARY default is now TRUE — it must
    // match the product promise. The r1/r2 releases shipped the fix only in
    // the Win32 tray app while EngineOptions{} still meant "digits compose"
    // (legacy OpenKey parity): the WinUI 3 front-end (and ANY future
    // TextEngine consumer) silently constructed the legacy behavior and
    // re-introduced the exact reported bug. Library default = shipping
    // default; classic VNI digit composition stays available by setting
    // this flag to FALSE explicitly (the Win32 dialog checkbox and the
    // legacy-parity test harnesses do exactly that).
    bool digitsAreLiteral = true;    // library default = the product default
};

//---------------------------------------------------------------------------
// TextEngine — one instance per IME session. All mutable state is internal.
//---------------------------------------------------------------------------
class TextEngine final {
public:
    explicit TextEngine(EngineOptions opts = {}) noexcept;

    // ---- primary API -----------------------------------------------------
    // Feed one normalized input; returns the decision + replacement text.
    // The consumer then:
    //   1. if !consumed(): forward the original key to the app;
    //   2. else: apply backspaceCount deletions and insert replacementUtf16()
    //      via TSF (preferred) or SendInput(KEYEVENTF_UNICODE) batch.
    //
    // Returns a const reference to the engine's internal result (legacy
    // OpenKey used a process-global HookState read by the consumer — this is
    // the same pattern, instance-scoped). The reference is valid until the
    // next process() call on the SAME engine and must not be retained across
    // calls. This is what makes the hot path allocation-free: no
    // return-by-value copy of the macro-key accumulator per keystroke.
    [[nodiscard]] const EngineResult& process(const TextInput& in);

    // Result accessor for the non-process() producers (v3.3.1 tone-style
    // switching): switchToneStyle() fills result_ exactly like process();
    // read it through this accessor. Same validity contract: the reference
    // is valid until the next process()/switchToneStyle() on THIS engine.
    [[nodiscard]] const EngineResult& lastResult() const noexcept { return result_; }

    // v3.3.1 — seamless tone-style switching for the PENDING word.
    // Converts the mark placement of the word currently in the state buffer
    // between the two Vietnamese orthography styles — old ("òa úy", mark on
    // the first vowel of the oà/oa·oe·uy families) and modern ("oà uý", mark
    // on the second) — e.g. pending "hoá" becomes "hóa" and vice versa.
    // The conversion happens directly on the internal state buffer (mask move
    // + whole-word re-emission through the normal EngineResult), so the
    // consumer applies it like any other edit: backspaceCount chars deleted,
    // replacement re-typed — net zero length change, no corruption, no
    // dropped characters. Composition continues seamlessly on the converted
    // buffer, and the engine's useModernOrthography option is flipped so
    // SUBSEQUENT marks follow the new style.
    // Returns true when the pending word was visibly converted (a result is
    // pending in the usual result accessors — replacementUtf16(result()));
    // returns false when there was nothing to convert (no pending word, no
    // mark, or a group whose placement is identical in both styles) — the
    // style is still flipped for future words in that case.
    // App wiring: call on the consumer/engine thread (same lock as process()),
    // map to a user chord (OpenKey convention: F9 with no modifiers), and
    // apply the returned edit like any WillProcess result.
    bool switchToneStyle();

    // Re-sync the composition state to raw ASCII letters that are ALREADY on
    // screen (e.g. the user clicked into / deleted part of a partially-typed
    // word and will keep typing). The letters are quietly replayed through the
    // normal per-key state machine (outputs are discarded — the text is
    // already visible), so the next keystroke can compose onto the visible
    // word. Returns false (and leaves the session clean) when the word is
    // empty or contains non-ASCII — composition can't resume from composed
    // or non-letter text. Call under the same lock as process().
    bool resumeFromText(const std::wstring& rawWord) noexcept;

    void startNewSession() noexcept;

    // English-mode macro hook (mirror of vEnglishMode; macro table lives in
    // the consumer). Returns true when the key should be suppressed.
    [[nodiscard]] const EngineResult& processEnglishMode(const TextInput& in);

    // Consumer-installed macro table lookup (default: no macros).
    using MacroResolver = std::function<bool(const std::vector<std::uint32_t>& key,
                                             std::vector<std::uint32_t>& data)>;
    void setMacroResolver(MacroResolver r) noexcept { macroResolver_ = std::move(r); }

    // P3: user-defined keymap (UniKey parity). Maps a produced character to
    // the character the user's keymap assigns BEFORE any Vietnamese
    // processing — a missing entry passes the key through untouched.
    using KeymapResolver = std::function<char32_t(char32_t produced)>;
    void setKeymapOverride(KeymapResolver r) noexcept { keymapResolver_ = std::move(r); }

    // Consumer-installed Vietnamese lexicon lookup (default: none). Receives
    // the COMPOSED pending word as resolved Unicode code points; returns
    // true when it is a known Vietnamese word. Used only by the
    // useDictionaryRestore decision at word breaks — the lexicon can veto a
    // transform (revert to raw) but never injects text of its own.
    using DictionaryResolver = std::function<bool(const std::vector<std::uint32_t>& composed)>;
    void setDictionaryResolver(DictionaryResolver r) noexcept { dictResolver_ = std::move(r); }

    // ---- settings (thread-affine: call from consumer thread only) --------
    void setOptions(EngineOptions opts) noexcept { opts_ = opts; resetSpellingFlag(); }
    [[nodiscard]] const EngineOptions& options() const noexcept { return opts_; }
    void tempOffSpellChecking() noexcept;   // toggles while a Ctrl combo is held
    void tempOffEngine(bool off) noexcept { willTempOffEngine_ = off; }

    // Resolve the engine's internal encoding to final UTF-16 text.
    // `coded` is one EngineResult::newChars entry.
    [[nodiscard]] std::uint32_t resolveChar(std::uint32_t coded) const noexcept;

    // Legacy keyCodeToCharacter(): plain key codes (no CHAR_CODE/PURE flag)
    // are mapped to the character the key produces (lowercase, or uppercase
    // when CAPS_MASK is set) — exactly as the legacy hook did at send time.
    [[nodiscard]] static char32_t keyCodeToCharacter(std::uint32_t data) noexcept;

    // Convert a full result buffer into ready-to-insert UTF-16.
    [[nodiscard]] std::wstring replacementUtf16(const EngineResult& r) const;

    // Allocation-free variant: appends nothing, writes into a caller-owned
    // scratch buffer (the hot path reuses one buffer — zero heap churn per
    // keystroke).
    void replacementUtf16(const EngineResult& r, std::wstring& out) const;

    // Render EngineResult::macroExpansion (ReplaceMacro payload) to UTF-16.
    // Allocation-free on non-macro events (clears an empty output first).
    void macroExpansionUtf16(const EngineResult& r, std::wstring& out) const;

    // Debug/test hooks (cheap, stable):
    //  * visibleAccount — the engine's count of characters the consumer has
    //    committed at the caret due to this session's events (pass-through
    //    chars + applied edits − backspaces). backspaceCount is guaranteed
    //    ≤ visibleAccount() for every result (D2 over-backspace policy).
    //  * debugScratchSize — size of the internal restore scratch
    //    (typingStatesData_). Guaranteed ≤ kMaxBuff after every event (D1).
    [[nodiscard]] std::size_t visibleAccount() const noexcept { return visibleAccount_; }
    [[nodiscard]] std::size_t debugScratchSize() const noexcept { return typingStatesData_.size(); }

private:
    // ---- internal encoding helpers (constexpr replacements for macros) ----
    static constexpr char32_t toUpperAscii(char32_t c) noexcept {
        return (c >= U'a' && c <= U'z') ? static_cast<char32_t>(c - 32) : c;
    }
    static constexpr bool isNumberKey(char32_t c) noexcept {
        return c >= U'0' && c <= U'9';
    }
    static constexpr bool isWordBreakChar(char32_t c) noexcept {
        // Legacy _breakCode printable subset PLUS the shifted-symbol characters.
        // The shipped app (MainWindow::produceChar) resolves a shifted-symbol
        // key to its CHARACTER (shift+1 -> '!') and feeds a Char event with the
        // shift flag, so the engine must classify those symbols as word breaks
        // itself — the legacy shifted-digit check can never fire on the resolved
        // character. This mirrors the 2.0.5 hook, which delivers the raw key
        // with the shift bit and breaks the word.
        return c == U',' || c == U'.' || c == U'/' || c == U';' || c == U'\'' ||
               c == U'\\' || c == U'-' || c == U'=' || c == U'`' ||
               c == U'!' || c == U'@' || c == U'#' || c == U'$' || c == U'%' ||
               c == U'^' || c == U'&' || c == U'*' || c == U'(' || c == U')' ||
               c == U'_' || c == U'+' || c == U'{' || c == U'}' || c == U'|' ||
               c == U':' || c == U'"' || c == U'<' || c == U'>' || c == U'?' ||
               c == U'~';
    }
    static constexpr bool isMacroBreakChar(char32_t c) noexcept {
        // Legacy _macroBreakCode (printable subset).
        return c == U',' || c == U'.' || c == U'/' || c == U';' || c == U'\'' ||
               c == U'\\' || c == U'-' || c == U'=';
    }
    static constexpr bool isCharKeyCodeChar(char32_t c) noexcept {
        // Legacy _charKeyCode (printable subset).
        return c == U'`' || (c >= U'0' && c <= U'9') || c == U'-' || c == U'=' ||
               c == U'[' || c == U']' || c == U'\\' || c == U';' || c == U'\'' ||
               c == U',' || c == U'.' || c == U'/';
    }
    static constexpr bool isWordBreakVk(std::uint16_t vk) noexcept {
        // Values match the Windows VK_* codes (also valid on other platforms
        // as opaque control identifiers for testing).
        switch (vk) {
            case 0x1B: case 0x09: case 0x0D:           // Esc, Tab, Enter
            case 0x25: case 0x26: case 0x27: case 0x28: // arrows
            case 0x24: case 0x23: case 0x2D: case 0x2E: // Home, End, Ins, Del
            case 0x21: case 0x22:                       // PgUp, PgDn
            case 0x2C: case 0x2A: case 0x29: case 0x2F: // Snap, Print, Sel, Help
            case 0x2B: case 0x90: case 0x91:            // Exec, NumLock, Scroll
                return true;
            default: return false;
        }
    }
    bool isSpecialKey(char32_t c) const noexcept {
        const auto m = opts_.inputMethod;
        if (m == InputMethod::Telex || m == InputMethod::SimpleTelex) {
            return c == U'W' || c == U'E' || c == U'R' || c == U'O' || c == U'[' ||
                   c == U']' || c == U'A' || c == U'S' || c == U'D' || c == U'F' ||
                   c == U'J' || c == U'Z' || c == U'X' || c == U'W';
        }
        // v1.1.2: with digitsAreLiteral the VNI digit keys (1-0: tones,
        // vowel marks, đ, tone removal) are disabled — a digit is a digit.
        return !opts_.digitsAreLiteral && isNumberKey(c);
    }
    constexpr char32_t PCH(std::size_t col) const noexcept {
        return kProcessingChar[static_cast<std::size_t>(opts_.inputMethod)][col];
    }
    bool isKeyZ(char32_t c)  const noexcept { return PCH(10) == c; }
    bool isKeyD(char32_t c)  const noexcept { return PCH(9)  == c; }
    bool isKeyW(char32_t c)  const noexcept {
        const auto m = opts_.inputMethod;
        return (m == InputMethod::Telex || m == InputMethod::SimpleTelex)
                   ? PCH(8) == c
                   : (m == InputMethod::Vni && (PCH(8) == c || PCH(7) == c));
    }
    bool isKeyDouble(char32_t c) const noexcept {
        const auto m = opts_.inputMethod;
        return (m == InputMethod::Telex || m == InputMethod::SimpleTelex)
                   ? (PCH(5) == c || PCH(6) == c || PCH(7) == c)
                   : (m == InputMethod::Vni && PCH(6) == c);
    }
    bool isKeyS(char32_t c) const noexcept { return PCH(0) == c; }
    bool isKeyF(char32_t c) const noexcept { return PCH(1) == c; }
    bool isKeyR(char32_t c) const noexcept { return PCH(2) == c; }
    bool isKeyX(char32_t c) const noexcept { return PCH(3) == c; }
    bool isKeyJ(char32_t c) const noexcept { return PCH(4) == c; }

    // NOTE: internal identity is uppercase (tables store 0x41 = 'A' …).
    static constexpr bool isVowelChar(char32_t c) noexcept {
        return c == U'A' || c == U'E' || c == U'U' || c == U'Y' || c == U'I' || c == U'O';
    }
    static constexpr bool isConsonantChar(char32_t c) noexcept { return !isVowelChar(c); }
    bool isMarkKey(char32_t c) const noexcept {
        const auto m = opts_.inputMethod;
        if (m == InputMethod::Telex || m == InputMethod::SimpleTelex) {
            return c == U'S' || c == U'F' || c == U'R' || c == U'J' || c == U'X';
        }
        return c == U'1' || c == U'2' || c == U'3' || c == U'5' || c == U'4';
    }
    static constexpr bool isBracketKey(char32_t c) noexcept {
        return c == U'[' || c == U']';
    }
    static constexpr bool isDoubleCode() noexcept {
        return false;   // legacy "double code" = VNI/compound — the NEW engine
                        // always emits explicit UTF-16, so no double-bytes.
    }

    // ---- word-state helpers ----
    std::uint32_t& at(std::size_t i) noexcept { return typingWord_[i]; }
    const std::uint32_t& at(std::size_t i) const noexcept { return typingWord_[i]; }
    std::uint16_t chr(std::size_t i) const noexcept {
        return static_cast<std::uint16_t>(typingWord_[i] & kCharMask);
    }
    bool isConsonantAt(std::size_t i) const noexcept {
        return isConsonantChar(static_cast<char32_t>(chr(i)));
    }

    // ---- algorithm (faithful port; details in .cpp) ----
    bool isWordBreakAny(const TextInput& in) const noexcept;
    void wordBreakBranch(char32_t c, bool caps, const TextInput& in, bool macroBreak);
    void spaceBranch(char32_t c, bool caps);
    void backspaceBranch(bool caps);
    void mainKeyBranch(char32_t c, bool caps);
    void insertKey(char32_t c, bool caps, bool check = true);
    void insertState(char32_t c, bool caps);
    void saveWord();
    void saveWord(char32_t keyCode, int count);
    void saveSpecialChar();
    void pushTypingState();               // array-backed history entry (no alloc)
    void restoreLastTypingState();
    void checkSpelling(bool forceCheckVowel = false);
    void checkGrammar(int deltaBackspace);
    void findAndCalculateVowel(bool forGrammar = false);
    bool canHasEndConsonant();
    void removeMark();
    void insertMark(std::uint32_t markMask, bool canModify = true);
    void handleOldMark();
    void handleModernMark();
    void insertD(char32_t c, bool caps);
    void insertAOE(char32_t c, bool caps);
    void insertW(char32_t c, bool caps);
    void handleMainKey(char32_t c, bool caps);
    void handleQuickTelex(char32_t c, bool caps);
    void checkForStandaloneChar(char32_t c, bool caps, char32_t keyWillReverse);
    void reverseLastStandaloneChar(char32_t c, bool caps);
    void upperCaseFirstCharacter();
    void pushMacroKey(std::uint32_t v) noexcept;   // bounded accumulator (see .cpp)
    bool checkRestoreIfWrongSpelling(EngineCode handleCode);
    // P1: lexicon-gated restore — fires when the composed word differs from
    // the raw keystrokes AND is not a dictionary word (see the option doc).
    // v3.3.1 refinement — the resolver also arbitrates the two remaining
    // DIVERGENCES.md families:
    //   * W-hook split ("huơ" vs "hươ"): when the composed form is NOT a
    //     lexicon word, the engine offers the partial-composition candidate
    //     (u stays u, only the o carries the W-hook) — when THAT candidate is
    //     a lexicon word it is emitted instead of the raw revert.
    //   * Mid-word toggle collision ("mono" vs "môn"): when a NON-adjacent
    //     vowel toggle consumed a key (insertAOE crossing a final consonant)
    //     and BOTH the composed form and the raw keystrokes are lexicon
    //     words, the raw keystrokes win (UniKey's mid-word strictness, gated
    //     by the lexicon instead of a structural rule).
    bool checkRestoreIfNotInDictionary(EngineCode handleCode);
    // Build the resolved code-point form of the raw keystrokes (keyStates_)
    // into `out`; returns false when any entry is unresolvable.
    bool rawKeysToCodePoints(std::vector<std::uint32_t>& out) const;
    // Try the partial-composition candidates of the current buffer (the
    // uo→ươ W-hook split family); returns true (and fills `out` with the
    // candidate's internal entries) when a candidate is a lexicon word.
    // Non-const: reuses the dictScratch_ member (zero allocation).
    bool lexiconApprovedAlternative(std::array<std::uint32_t, kMaxBuff>& out,
                                    std::size_t& outLen);
    // D3: copy the resolver's expansion into the current result as final
    // Unicode code points (called exactly when code becomes ReplaceMacro).
    void setMacroExpansionFromResolver();
    bool checkQuickConsonant();
    bool isQuickTelexKey(char32_t c) const noexcept {
        return index_ > 0 && (c == U'C' || c == U'G' || c == U'K' || c == U'N' ||
                              c == U'Q' || c == U'P' || c == U'T') &&
               chr(index_ - 1) == c;
    }
    void checkCorrectVowel(const FlatVec<FlatVec<std::uint16_t>>& charset,
                           int& i, int& k, char32_t markKey);
    std::uint32_t getCharacterCode(const std::uint32_t& data) const noexcept;
    bool findMacro(const std::vector<std::uint32_t>& key, std::vector<std::uint32_t>& data);
    void resetSpellingFlag() noexcept { useSpellingBefore_ = opts_.checkSpelling; }
    void finalizeResult() noexcept;

    // ---- state (was: 30+ file-scope globals in Engine.cpp) ----
    EngineOptions opts_;
    std::array<std::uint32_t, kMaxBuff> typingWord_{};
    std::array<std::uint32_t, kMaxBuff> keyStates_{};
    std::size_t index_      = 0;   // typing word length
    std::size_t stateIndex_ = 0;   // key-state history length

    std::vector<std::uint32_t> longWordHelper_;
    // Typing-history states. Stored as fixed kMaxBuff arrays + parallel
    // lengths so a word-break push_back copies 128 bytes with NO heap
    // allocation (legacy used vector-of-vectors → one alloc per word break).
    std::vector<std::array<std::uint32_t, kMaxBuff>> typingStates_;
    std::vector<std::uint8_t> typingStatesLen_;
    std::vector<std::uint32_t> typingStatesData_;   // restore scratch (reused)
    std::vector<std::uint32_t> specialChar_;

    bool tempDisableKey_        = false;
    bool willTempOffEngine_     = false;
    // v1.1.0: resync-replay mode (resumeFromText). While set, process() only
    // advances the raw word buffer — no transforms, no grammar/spelling, no
    // macro bookkeeping. The pre-1.1.0 replay fed visible letters through the
    // FULL state machine, so a visible raw word like "as"/"dd"/"aw" left
    // transform masks stored in typingWord_ (the next word break then
    // "restored" phantom composed text — duplicated letters on screen).
    bool rawReplay_             = false;
    // v1.1.0: VNI vowel-scan validity. The VNI branch scans the buffer for
    // the last O/A/E to map digit 6/7/8 keys; when nothing matches, the old
    // code kept the PREVIOUS word's index (stale vowelEnd_) and could apply
    // the transform against a phantom vowel. Reset per scan; guarded at use.
    bool vniVowelEndValid_      = false;
    // v3.3.1: a NON-adjacent vowel toggle consumed the key (insertAOE found
    // its target vowel across a final consonant — the "mono"→"môn" family).
    // Set mid-word, consumed by the lexicon-gated word-break decision.
    bool midWordToggle_         = false;
    bool hasHandledMacro_       = false;
    bool hasHandleQuickConsonant_ = false;
    bool isRestoredW_           = false;
    bool isCheckedGrammar_      = false;
    bool isCorect_              = false;
    bool isChanged_             = false;
    bool isCharKeyCode_         = false;
    int  spaceCount_            = 0;
    // v1.1.0 macro-visibility model: after a ReplaceMacro the expansion — not
    // the raw keys — is on screen (D3 contract: the break key is consumed).
    // macroExpandLen_ tracks the expansion's visible UTF-16 length so
    // backspaceBranch can walk it down like ordinary text instead of
    // "restoring" raw keys that were never displayed (the ghost-letters
    // after macro + backspace bug). 0 = no pending expansion.
    std::size_t macroExpandLen_ = 0;
    std::uint8_t upperCaseStatus_ = 0;
    bool useSpellingBefore_     = true;

    std::size_t vowelCount_ = 0, vowelStart_ = 0, vowelEnd_ = 0, vowelWillSetMark_ = 0;

    // spelling scratch (member to preserve exact original control flow)
    bool spellingOK_ = false, spellingFlag_ = false, spellingVowelOK_ = true;
    std::size_t spellingEndIndex_ = 0;

    MacroResolver macroResolver_;
    DictionaryResolver dictResolver_;
    KeymapResolver keymapResolver_;
    std::vector<std::uint32_t> macroData_;   // resolver output scratch
    std::vector<std::uint32_t> dictScratch_; // composed-word scratch (lexicon lookup)
    EngineResult result_;   // result being assembled (replaces global HookState)

    // D2 over-backspace policy: the engine's model of how many characters
    // the consumer has committed at the caret because of this engine's
    // events (pass-through chars, applied edits, re-issued restore keys,
    // minus applied backspaces). The harness/verification consumer model
    // starts from an empty document, so this mirrors its committed text
    // exactly; a real consumer's document may hold MORE text (typed before
    // the engine was enabled, other windows) — the account is then a lower
    // bound, which keeps the backspaceCount clamp conservative (safe).
    std::size_t visibleAccount_ = 0;
};

} // namespace ok::text
