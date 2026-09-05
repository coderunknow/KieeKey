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
// File: tests/uk_driver.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.3 — tests/uk_driver.cpp
// Minimal Linux harness for the VENDORED UNMODIFIED UniKey engine
// (tests/reference/unikey — the authentic UKEngine from the UniKey 4.x
// GPL source). Feeds a key stream and produces the UTF-16 output + backspace
// delta exactly like the UniKey Windows hook's consumer, so the engine can be
// driven headlessly for differential / latency benchmarks.
//
// The engine API (see tests/reference/unikey/ukengine.h):
//   UkEngine::process(keyCode, backs, outBuf, outSize, outType)
//     — keyCode is the US-layout VK (UniKey's "virtual key"); on success
//       (ret != 0) it produced a change: delete `backs` chars, then append
//       `outSize` bytes from outBuf (UTF-16 when charsetId==CONV_CHARSET_UNICODE).
//     — ret == 0 means the key was buffered (no visible change yet).
//   UkEngine::processBackspace(...) — Backspace key (VK_BACK=8).
//   UkEngine::reset() — start a new word (vKeyInit equivalent).
//
// Consumer model (faithful to UniKey's XIM/GTK/Windows front-ends):
//   * The engine buffers pending keystrokes and emits a changed span only when
//     a tone/consonant conversion happens (ret != 0). The front-end shows the
//     RAW pending keystrokes as the preedit between conversions.
//   * We keep `pending` = the raw keystrokes typed since the last commit.
//     - ret != 0: the engine wants to replace the TAIL of the pending word:
//       delete `backs` UTF-16 units from the END of pending, then append the
//       emitted span. (The span can itself be multi-char.)
//     - ret == 0: nothing changed; the raw key stays in `pending`.
//   * A word-break key (Space/Enter/punct): commit `pending` (converted form is
//     already reflected in it), then the word-break char is passed through.
//
// Build (from repo root):
//   g++ -std=c++17 -O2 -DLINUX -fpermissive -include tests/reference/unikey/uk_fix.h \
//       -w -I tests/reference/unikey tests/uk_driver.cpp \
//       tests/reference/unikey/ukengine.cpp tests/reference/unikey/inputproc.cpp \
//       tests/reference/unikey/macro.cpp tests/reference/unikey/mactab.cpp \
//       tests/reference/unikey/usrkeymap.cpp tests/reference/unikey/charset.cpp \
//       tests/reference/unikey/convert.cpp tests/reference/unikey/data.cpp \
//       tests/reference/unikey/error.cpp tests/reference/unikey/pattern.cpp \
//       tests/reference/unikey/byteio.cpp -o uk_driver
//----------------------------------------------------------------------------
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>

#include "ukengine.h"

namespace {

UkSharedMem g_shm;

// US-layout VK for an ASCII char (UniKey uses the Windows virtual-key codes;
// the engine's input classifier maps VK_A..VK_Z -> a-z, VK_SPACE -> space).
unsigned int vkForChar(char c) {
    if (c >= 'a' && c <= 'z') return static_cast<unsigned int>('a') + (c - 'a');
    if (c == ' ') return 32;                       // VK_SPACE
    if (c == ',') return 188;                      // VK_OEM_COMMA
    if (c == '.') return 190;                      // VK_OEM_PERIOD
    if (c == ';') return 186;                      // VK_OEM_1
    if (c == '\'') return 222;                     // VK_OEM_7
    if (c == '[') return 219;                      // VK_OEM_4
    if (c == ']') return 221;                      // VK_OEM_6
    return static_cast<unsigned int>(c);           // digits etc. map to themselves
}

// UTF-16LE bytes -> UTF-8 string.
std::string u16ToUtf8(const unsigned char* p, int n) {
    std::string s;
    for (int i = 0; i + 1 < n; i += 2) {
        const unsigned int cp = static_cast<unsigned int>(p[i]) |
                                (static_cast<unsigned int>(p[i+1]) << 8);
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

// Backspace `n` UTF-16 code units from the end of a UTF-8 string.
void eraseUtf16FromEnd(std::string& s, std::size_t n) {
    std::size_t toErase = 0, units = 0;
    for (auto it = s.rbegin(); it != s.rend() && units < n; ++it) {
        const unsigned char b = static_cast<unsigned char>(*it);
        if ((b & 0xC0) != 0x80) ++units;   // lead byte of a UTF-8 seq = 1 UTF-16 unit
        ++toErase;
    }
    s.erase(s.size() - toErase);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: uk_driver '<telex-keystrokes>' [imethod]\n"
                             "  imethod: 0=Telex 1=VNI 2=VIQR 3=MS-Viet 5=SimpleTelex\n");
        return 2;
    }
    const char* keys = argv[1];
    const UkInputMethod im = argc > 2
        ? static_cast<UkInputMethod>(std::atoi(argv[2]))
        : UkTelex;

    std::memset(&g_shm, 0, sizeof(g_shm));
    g_shm.initialized = 1;
    g_shm.vietKey = 1;
    g_shm.iconShown = 0;
    g_shm.usrKeyMapLoaded = 0;
    g_shm.charsetId = CONV_CHARSET_UNICODE;         // UTF-16 output
    g_shm.options.freeMarking = 1;
    g_shm.options.modernStyle = 0;
    g_shm.options.macroEnabled = 0;
    g_shm.options.useUnicodeClipboard = 0;
    g_shm.options.alwaysMacro = 0;
    g_shm.options.strictSpellCheck = 0;
    g_shm.options.useIME = 0;
    g_shm.options.spellCheckEnabled = 1;
    g_shm.options.autoNonVnRestore = 0;

    SetupUnikeyEngine();
    g_shm.input.init();
    g_shm.input.setIM(im);

    UkEngine engine;
    engine.setCtrlInfo(&g_shm);

    std::string committed;      // text the app has committed (word-break'd)
    std::string pending;        // raw keystrokes since the last commit (preedit)
    for (const char* p = keys; *p; ++p) {
        const char c = *p;
        int backs = 0;
        unsigned char out[256] = {};
        int outSize = sizeof(out);
        UkOutputType outType = UkCharOutput;
        int ret = 0;

        const bool isWordBreak = (c == ' ' || c == '\n' || c == ',' || c == '.' ||
                                  c == ';' || c == '\'' || c == '[' || c == ']');
        if (c == '#') {
            ret = engine.processBackspace(backs, out, outSize, outType);
        } else if (isWordBreak) {
            ret = engine.process(vkForChar(c), backs, out, outSize, outType);
        } else {
            ret = engine.process(vkForChar(c), backs, out, outSize, outType);
        }

        if (c == '#') {
            // Backspace: the engine tells us how many chars to delete from the
            // pending word (or the front-end just deletes one raw key).
            if (ret && backs > 0) {
                eraseUtf16FromEnd(pending, static_cast<std::size_t>(backs));
            } else if (!pending.empty()) {
                pending.pop_back();                 // plain raw-key backspace
            } else {
                committed.pop_back();               // nothing pending — delete committed
            }
            continue;
        }

        if (ret != 0) {
            // Engine emitted a changed span: it replaces the tail of the pending
            // word. Delete `backs` UTF-16 units from pending, then append the
            // emitted span (UTF-16 -> UTF-8).
            eraseUtf16FromEnd(pending, static_cast<std::size_t>(backs));
            pending += u16ToUtf8(out, outSize);
        } else {
            // Key absorbed with no visible change: it becomes part of the
            // preedit. For a word-break key that ends the word, the engine
            // commits silently (ret==0) — the front-end commits the pending
            // word and passes the break char through.
            if (isWordBreak) {
                committed += pending;
                committed += c;                     // the space / punct itself
                pending.clear();
            } else {
                pending += c;
            }
        }
    }

    // Final visible text: committed + pending (the pending word is what the
    // user sees in the preedit / inline composition).
    std::printf("%s%s\n", committed.c_str(), pending.c_str());
    return 0;
}
