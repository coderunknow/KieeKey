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
// File: tests/engine205.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// tests/engine205.cpp — implementation of the ok205 wrapper.
//
// Defines the 21 global configuration variables the 2.0.5 engine reads
// (declared `extern` in the vendored Engine.h), then drives the REAL 2.0.5
// algorithm via its public entry point `vKeyHandleEvent` and reads the
// `HookState` output structure, replicating the legacy hook's consumer
// semantics so the wrapper yields the exact text the 2.0.5 application would
// have produced.
//----------------------------------------------------------------------------
#define LINUX 1

// ---- configuration globals required by the vendored 2.0.5 Engine.h ----
int vLanguage = 1;
int vInputType = 0;            // 0 Telex, 1 VNI, 2 Simple Telex
int vFreeMark = 0;
int vCodeTable = 0;            // 0 Unicode, 1 TCVN3, 2 VNI-Windows
int vSwitchKeyStatus = 0;
int vCheckSpelling = 1;
int vUseModernOrthography = 0;
int vQuickTelex = 0;
int vRestoreIfWrongSpelling = 0;
int vFixRecommendBrowser = 0;
int vUseMacro = 1;
int vUseMacroInEnglishMode = 0;
int vAutoCapsMacro = 0;
int vUseSmartSwitchKey = 0;
int vUpperCaseFirstChar = 0;
int vTempOffSpelling = 0;
int vAllowConsonantZFWJ = 0;
int vQuickStartConsonant = 0;
int vQuickEndConsonant = 0;
int vRememberCode = 0;
int vTempOffOpenKey = 0;

#include "Engine.h"     // vendored reference (tests/reference/openkey-2.0.5/engine)
#include "engine205.hpp"

// HookState is the vendored engine's global output/state struct (defined in
// Engine.cpp); the wrapper reads it to build deltas and to clear the macro key
// after the flush-session workaround (see flushSession()).
extern vKeyHookState HookState;

#include <cstring>
#include <map>

extern vKeyHookState HookState;   // defined in the vendored Engine.cpp

namespace {

// ASCII char -> 2.0.5 Linux key code (platforms/linux.h). 0 = unmappable.
std::uint16_t gKeyMap[256];

void buildKeyMap() {
    std::memset(gKeyMap, 0, sizeof(gKeyMap));
    const char* letters = "abcdefghijklmnopqrstuvwxyz";
    const std::uint16_t letterKeys[] = {
        KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
        KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
        KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    };
    for (int i = 0; i < 26; ++i) gKeyMap[(int)letters[i]] = letterKeys[i];
    // digit row: '1'-'9' -> KEY_1..KEY_9, '0' -> KEY_0 (per vendored linux.h)
    gKeyMap[(int)'0'] = KEY_0;
    for (int i = 1; i <= 9; ++i) gKeyMap[(int)('0' + i)] = KEY_1 + (i - 1);
    gKeyMap[(int)' '] = KEY_SPACE;
    gKeyMap[(int)'['] = KEY_LEFT_BRACKET;
    gKeyMap[(int)']'] = KEY_RIGHT_BRACKET;
    gKeyMap[(int)'.'] = KEY_DOT;
    gKeyMap[(int)','] = KEY_COMMA;
    gKeyMap[(int)';'] = KEY_SEMICOLON;
    gKeyMap[(int)'\''] = KEY_QUOTE;
    gKeyMap[(int)'/'] = KEY_SLASH;
    gKeyMap[(int)'-'] = KEY_MINUS;
    gKeyMap[(int)'='] = KEY_EQUALS;
    gKeyMap[(int)'\\'] = KEY_BACK_SLASH;
    gKeyMap[(int)'`'] = KEY_BACKQUOTE;
    gKeyMap[(int)'\n'] = KEY_ENTER;
}

// US-layout shifted symbols -> the physical unshifted Linux key code.
// The real 2.0.5 application receives these as shift+key events (capsStatus=1)
// which its first branch treats as WORD BREAKS. The wrapper exposes them as
// such via processSymbol().
std::uint16_t gShiftKey[256];

void buildShiftKeyMap() {
    std::memset(gShiftKey, 0, sizeof(gShiftKey));
    gShiftKey[(int)'!'] = KEY_1;  gShiftKey[(int)'@'] = KEY_2;  gShiftKey[(int)'#'] = KEY_3;
    gShiftKey[(int)'$'] = KEY_4;  gShiftKey[(int)'%'] = KEY_5;  gShiftKey[(int)'^'] = KEY_6;
    gShiftKey[(int)'&'] = KEY_7;  gShiftKey[(int)'*'] = KEY_8;  gShiftKey[(int)'('] = KEY_9;
    gShiftKey[(int)')'] = KEY_0;
    gShiftKey[(int)'_'] = KEY_MINUS;   gShiftKey[(int)'+'] = KEY_EQUALS;
    gShiftKey[(int)'{'] = KEY_LEFT_BRACKET;  gShiftKey[(int)'}'] = KEY_RIGHT_BRACKET;
    gShiftKey[(int)'|'] = KEY_BACK_SLASH;
    gShiftKey[(int)':'] = KEY_SEMICOLON;     gShiftKey[(int)'"'] = KEY_QUOTE;
    gShiftKey[(int)'<'] = KEY_COMMA;         gShiftKey[(int)'>'] = KEY_DOT;
    gShiftKey[(int)'?'] = KEY_SLASH;         gShiftKey[(int)'~'] = KEY_BACKQUOTE;
}

// Inverse of gKeyMap: Linux KEY_* code -> ASCII char (0 if unmappable).
char keyCodeToChar(std::uint16_t kc) {
    for (int i = 0; i < 256; ++i) if (gKeyMap[i] == kc) return static_cast<char>(i);
    return 0;
}

bool decodeOne(std::uint32_t v, std::wstring& out) {
    v = getCharacterCode(v);
    if (v & CHAR_CODE_MASK) {
        // real Unicode character (caps already applied via the code table)
        v &= CHAR_MASK;
    } else if (v & CAPS_MASK) {
        // caps-masked RAW KEY CODE: the legacy hooks (OpenKey.mm / OpenKey.cpp
        // SendKeyCode) truncate to 16 bits and synthesize a Shift+key event,
        // which yields the uppercase letter. Map the key code back to ASCII
        // and apply caps — exactly what the application would show.
        v &= CHAR_MASK;
        const char c = keyCodeToChar(static_cast<std::uint16_t>(v));
        if (c == 0) return false;
        if (c >= 'a' && c <= 'z') out += static_cast<wchar_t>(static_cast<unsigned char>(c - 32));
        else out += static_cast<wchar_t>(static_cast<unsigned char>(c));
        return true;
    } else if (v <= 0xFF) {
        // raw key code without caps (Linux codes differ from ASCII): map back
        const char c = keyCodeToChar(static_cast<std::uint16_t>(v));
        if (c == 0) return false;
        out += static_cast<wchar_t>(static_cast<unsigned char>(c));
        return true;
    }
    if (v == 0) return false;
    out += static_cast<wchar_t>(v);
    return true;
}

ok205::Delta makeDelta(std::uint32_t caps) {
    ok205::Delta d;
    const std::int32_t code = HookState.code;
    d.code = code;
    switch (code) {
        case vWillProcess:
        case vRestore:
        case vRestoreAndStartNewSession: {
            d.suppressed = true;
            d.backspace = HookState.backspaceCount;
            for (std::int32_t i = static_cast<std::int32_t>(HookState.newCharCount) - 1; i >= 0; --i) {
                decodeOne(HookState.charData[i], d.text);
            }
            break;
        }
        case vReplaceMaro: {
            d.suppressed = true;
            d.backspace = HookState.backspaceCount;
            for (std::size_t i = 0; i < HookState.macroData.size(); ++i) {
                decodeOne(HookState.macroData[i], d.text);
            }
            break;
        }
        default: {
            // DoNothing / BreakWord: the raw key reaches the application
            // unchanged. 2.0.5 does not reset backspaceCount/newCharCount/
            // extCode for these events (they stay stale from the previous
            // event), but the legacy hook ignores the output struct for
            // pass-through keys — the app acts on the raw key itself. We
            // therefore zero the delta here; the caller (processSpace /
            // processBackspace / processWordBreak) supplies the pass-through
            // effect explicitly.
            d.suppressed = false;
            d.backspace = 0;
            break;
        }
    }
    return d;
}

char32_t visibleChar(char32_t ch, bool caps) {
    if (caps && ch >= U'a' && ch <= U'z') return static_cast<char32_t>(ch - 32);
    return ch;
}

// 2.0.5's vKeyInit() only resets _index/_stateIndex and the word histories;
// the file-local statics tempDisableKey, _spaceCount, _specialChar,
// _hasHandledMacro, _hasHandleQuickConsonant and _willTempOffEngine survive it
// and would leak across harness cases (a fresh case would inherit the previous
// case's mid-word state, skewing the differential). The vendored engine is a
// frozen fixture, so reset through the public API instead:
//   ENTER (word break, non-char key) x2 — clears _specialChar/_typingStates,
//     runs startNewSession() (tempDisableKey, _hasHandledMacro,
//     _hasHandleQuickConsonant, _longWordHelper), resets _willTempOffEngine
//     and clears hMacroKey; a second ENTER guarantees a clean start even when
//     the first one triggers a residual macro expansion.
//   'a' (plain char) — forces the "START AND CHECK KEY" branch, which saves
//     any residual _spaceCount into the word history, where the trailing
//     vKeyInit() discards it.
// The deltas of these flush events are discarded; the trailing vKeyInit()
// clears everything they wrote.
void flushSession() {
    vKeyHandleEvent(Keyboard, KeyDown, KEY_ENTER, 0, false);
    vKeyHandleEvent(Keyboard, KeyDown, KEY_ENTER, 0, false);
    vKeyHandleEvent(Keyboard, KeyDown, KEY_A, 0, false);
    vKeyInit();
    // The fake KEY_A above is a harness flush for the pending-word state
    // (2.0.5 leaks state across cases otherwise), but it also gets accumulated
    // into HookState.macroKey as a stale 'a' (the real app never sends it).
    // Clear it so a fresh case starts with a clean macro key — otherwise
    // findMacro() can never match (e.g. 'ok' would be seen as 'aok').
    HookState.macroKey.clear();
}

} // namespace

namespace ok205 {

void applyOptions(const Options& o) {
    vLanguage = 1;
    vInputType = static_cast<int>(o.method);
    vCodeTable = o.table;
    vCheckSpelling = o.checkSpelling ? 1 : 0;
    vUseModernOrthography = o.modernOrthography ? 1 : 0;
    vQuickTelex = o.quickTelex ? 1 : 0;
    vRestoreIfWrongSpelling = o.restoreIfWrongSpelling ? 1 : 0;
    vFreeMark = o.freeMark ? 1 : 0;
    vQuickStartConsonant = o.quickStartConsonant ? 1 : 0;
    vQuickEndConsonant = o.quickEndConsonant ? 1 : 0;
    vUpperCaseFirstChar = o.upperCaseFirstChar ? 1 : 0;
    vUseMacro = o.useMacro ? 1 : 0;
    vUseMacroInEnglishMode = o.useMacroInEnglishMode ? 1 : 0;
    vAllowConsonantZFWJ = o.allowConsonantZFWJ ? 1 : 0;
    vAutoCapsMacro = o.autoCapsMacro ? 1 : 0;
    vUseSmartSwitchKey = 0;
    vFixRecommendBrowser = 0;
    vTempOffSpelling = 0;
    vRememberCode = 0;
    vTempOffOpenKey = 0;
}

void init(const Options& o) {
    applyOptions(o);
    buildKeyMap();
    buildShiftKeyMap();
    vKeyInit();
    flushSession();
}

void setOptions(const Options& o) {
    // Change the settings the engine reads, keeping the pending word —
    // exactly what the real 2.0.5 application does when the user switches
    // input method mid-session (only the globals change; no reset).
    applyOptions(o);
}

void installMacro(const std::string& key, const std::wstring& content) {
    std::string utf8;
    for (wchar_t c : content) {
        char32_t cp = static_cast<char32_t>(c);
        if (cp < 0x80) utf8 += static_cast<char>(cp);
        else if (cp < 0x800) {
            utf8 += static_cast<char>(0xC0 | (cp >> 6));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            utf8 += static_cast<char>(0xE0 | (cp >> 12));
            utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    addMacro(key, utf8);
}

void clearMacros() {
    // readFromFile(path) appends; to clear, re-init with a fresh map is not
    // exposed — recreate the engine session instead (no-op here; harness
    // installs macros once at startup).
}

void reset() { vKeyInit(); flushSession(); }

bool processChar(char32_t ch, bool caps, bool ctrl, Delta& out) {
    if (ch > 0xFF) return false;
    const std::uint16_t kc = gKeyMap[static_cast<int>(ch)];
    if (kc == 0) return false;
    vKeyHandleEvent(Keyboard, KeyDown, kc, caps ? 1 : 0, ctrl);
    out = makeDelta(caps ? 1 : 0);
    if (out.code == vRestore || out.code == vRestoreAndStartNewSession) {
        // Legacy-hook consumer semantics (OpenKey.mm / win32 OpenKey.cpp):
        // after a Restore the hook re-sends the typed key to the app
        // (SendKeyCode(_keycode | CAPS_MASK)), turning 'cass' into 'cas'.
        out.text += visibleChar(ch, caps);
    } else if (!out.suppressed && out.backspace == 0) {
        // plain pass-through: the app sees the typed (case-adjusted) char
        out.text = std::wstring(1, static_cast<wchar_t>(visibleChar(ch, caps)));
    }
    return true;
}

bool processSymbol(char c, Delta& out) {
    // Real-key model for shifted-symbol output chars: 2.0.5 sees the physical
    // key with Shift held (capsStatus=1) and its first branch classifies
    // shifted digits / symbol keys as word breaks, so the symbol reaches the
    // application as a plain pass-through. Mirrors what the legacy hook would
    // have delivered.
    if (c > 0x7F) return false;
    std::uint16_t kc = gShiftKey[(int)c];
    if (kc == 0) return false;
    vKeyHandleEvent(Keyboard, KeyDown, kc, 1, false);   // shift held
    out = makeDelta(1);
    if (!out.suppressed && out.backspace == 0) {
        out.text = std::wstring(1, static_cast<wchar_t>(c));
    }
    return true;
}

void processSpace(bool caps, Delta& out) {
    vKeyHandleEvent(Keyboard, KeyDown, KEY_SPACE, caps ? 1 : 0, false);
    out = makeDelta(caps ? 1 : 0);
    if (!out.suppressed && out.backspace == 0) out.text = L" ";
}

void processBackspace(Delta& out) {
    vKeyHandleEvent(Keyboard, KeyDown, KEY_DELETE, 0, false);
    out = makeDelta(0);
    if (!out.suppressed && out.backspace == 0) out.backspace = 1;   // app deletes one char
}

void processWordBreak(std::uint16_t vk, Delta& out) {
    std::uint16_t kc = 0;
    wchar_t visible = 0;
    switch (vk) {
        case 0x0D: kc = KEY_ENTER; visible = L'\n'; break;
        case 0x09: kc = KEY_TAB;   visible = L'\t'; break;
        default:   kc = 0; break;
    }
    if (kc == 0) { out = Delta{}; return; }
    vKeyHandleEvent(Keyboard, KeyDown, kc, 0, false);
    out = makeDelta(0);
    if (!out.suppressed && out.backspace == 0) out.text = std::wstring(1, visible);
}

void processMouseDown(Delta& out) {
    vKeyHandleEvent(Mouse, MouseDown, 0, 0, false);
    out = makeDelta(0);
    if (out.suppressed) { out.backspace = 0; out.text.clear(); }
}

} // namespace ok205
