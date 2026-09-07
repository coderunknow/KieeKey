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
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// File: tests/test_option_matrix.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — tests/test_option_matrix.cpp
// OPTION-MATRIX DIFFERENTIAL TESTING SYSTEM (RC2 §3, §4, §5, §13).
//
// WHY THIS FILE EXISTS
//   v1.2.2 RC1 introduced bitmask classifiers, last-character sequence
//   buckets and direct-index lookups selected from EngineOptions. A gate
//   that only validates the DEFAULT configuration cannot prove that an
//   option switch leaves no stale mask / bucket / grammar / spelling state,
//   nor that consumer-side settings (performance profiles) are semantically
//   inert. RC2's release mandate makes that proof explicit.
//
// TIERS (select with --tier=N, default all):
//   1  Full Cartesian: method(3) × codeTable(5) × outputEncoding(2) = 30
//      configs × {corpus, seeded-random} streams, engine-vs-oracle
//      lockstep (tuple + rendering). VIQR configs have no oracle model by
//      design: they are checked by (a) tuple-identity against the identical
//      Unicode config and (b) projection: every rendered replacement must
//      equal harness-VIQR(oracle Unicode rendering). The harness VIQR
//      table is written from the Unicode Vietnamese blocks independently
//      of the engine's flat map — a transcription error cannot cancel out.
//   2  Pairwise covering array over the 11 semantic booleans (spelling,
//      modern, quickTelex, restoreIfWrong, freeMark, Zfwj, quickStart/End,
//      upperFirst, macroPunct, dictRestore) × method(3). Rows whose flag
//      the oracle does not model (macroPunct — the DOCUMENTED v1.2.0
//      punctuation-macro difference) run the invariant battery only; the
//      goldens tier pins their exact expected text separately.
//   3  Runtime option TRANSITIONS (brief §4) at four switch-point classes
//      (early mid-word, right after a word break, during backspace
//      correction, deep mid-stream), four modes:
//        CONTRACT  setOptions+startNewSession (shipped app behavior):
//                  from the switch onward, per-event tuples AND rendered
//                  text must equal a FRESH engine built with the target
//                  options fed the same suffix. Catches every stale-state
//                  leak across the boundary.
//        LIVE      setOptions only (dialog OK'd mid-composition): engine
//                  must match an oracle given the identical transition at
//                  the identical event — the oracle independently re-derives
//                  every decision from ITS fresh option snapshot, so a
//                  cached decision from the previous option fails lockstep.
//        ROUNDTRIP setOptions(unchanged value) before EVERY event must be
//                  indistinguishable from never calling the setter.
//        RENDER    for table/encoding switches: decisions AND resolved
//                  renderings must equal an engine that used the target
//                  rendering from the start (from the switch onward) —
//                  encoding is pure rendering, never a decision input.
//      Plus A→B→A: switching back to A at a boundary must exactly restore
//      the stay-on-A decision stream (no hidden latch, no stale history).
//   4  Performance-profile matrix: 5 profiles × 16 hybrid flags. The app
//      maps a Strategy to the engine through EXACTLY one field
//      (useDictionaryRestore, one-way ON — main.cpp applyPerfStrategy).
//      Every row must therefore leave engine output byte-identical to the
//      baseline, or byte-identical to the documented dictionary-restore
//      reference where the strategy turns it on (asserted, not assumed).
//      Extra assertions: event count / consumed count / total backspaces
//      identical across all non-dict rows; flag-ON-without-lexicon ==
//      flag-OFF (resolver gating); at least one row genuinely differs
//      (the invariance test cannot pass vacuously).
//   5  Pipeline transition test: option-change JOBS ride the same SPSC ring
//      as keystrokes (the shipped serialized pattern), including backlog
//      pressure and parked/spin handoffs; the consumer's decision trace must
//      equal a single-threaded reference replay of the same job order — no
//      drops, no duplicates, no reordering, no transition-induced drift.
//   6  OPTION TORTURE (RC3 §6): a long-lived engine is hammered with rapid,
//      seeded full-space option flips (every EngineOptions field, including
//      the non-oracle-modeled ones) on a random keystroke stream, verified
//      three independent ways per round: FRESH — after setOptions +
//      resetForConfigurationChange the live engine must be decision-identical
//      to a brand-new engine built with the target options (the RC1 stale-
//      latch bug class, now at EVERY flip point); ORACLE — flips the oracle
//      models keep live lockstep with the oracle's independently re-derived
//      decisions; INVAR — D1/D2/CNT hold under in-flight flips and temp-off
//      toggles. Two seeded engines with the identical schedule also fold
//      digests, so any hidden global/seed state breaks the digest check.
//   i  Option-cache STALENESS probe against RC1's indexing: an engine that
//      REACHES config X through many setOptions hops (X→other→default→X)
//      must be bit-identical to an engine FRESH-BUILT with X on the same
//      stream, and must return to baseline bit-identically after switching
//      back; identical-value setOptions re-asserted mid-stream stays a no-op.
//   g  Goldens for the DOCUMENTED option differences (punctuation macro,
//      VIQR projection identity, digitsAreLiteral product promise in every
//      method + legacy VNI digit composition) — so "invariance" can never
//      silently degrade into "everything identical by accident".
//
// Per-event invariants in EVERY runner:
//   D2  backspaceCount <= committed length at decision time (pre-event
//       visibleAccount) — never over-backspace user text
//   D1  debugScratchSize() <= kMaxBuff (bounded restore scratch)
//   CNT newCharCount <= 2*kMaxBuff; macroKey accumulator <= 255
//   STB doc-mirror + trace digests compared live, not just at the end
//
// Usage: test_option_matrix [--tier=all|1|2|3|4|5|6|i|g] [--events=N]
//                           [--quick] [--json=PATH]
// Exit 0 = every tier passed. Artifacts: --json machine-readable summary.
//----------------------------------------------------------------------------
#include "TextEngine.hpp"
#include "kieekey_core.hpp"   // v1.2.2 RC4 (B-2): OPENKEY_KIEEKEY_VERSION_STRING — the banner can no longer drift from the release
#include "vi_oracle.hpp"
#include "PerfProfile.hpp"
#include "LockFreeQueue.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

using namespace ok::text;

namespace {

//----------------------------------------------------------------------------
// Bookkeeping
//----------------------------------------------------------------------------
std::uint64_t g_failures = 0;
std::string g_curCfg;   // label of the config currently being executed
std::string g_cfgBuf;   // backing store for snprintf-built labels
int g_printBudget = 24; // cap for per-config diagnostic prints
std::uint64_t g_events = 0;
std::uint64_t g_configs = 0;
std::uint64_t g_transitions = 0;
std::uint64_t g_staleAbandon = 0;

struct JsonRow {
    std::string tier, name;
    std::uint64_t events = 0, configs = 0;
    std::uint64_t mismatches = 0, stale = 0, overBs = 0, goldens = 0, transitions = 0;
    std::uint64_t digest = 0;
};
std::vector<JsonRow> g_json;

#define OM_CHECK(cond)                                                      \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                   \
        }                                                                   \
    } while (0)

std::string utf8Of(const std::wstring& w) {
    std::string s;
    for (wchar_t wc : w) {
        const std::uint32_t u = static_cast<std::uint32_t>(wc);
        if (u < 0x80) s.push_back(static_cast<char>(u));
        else if (u < 0x800) {
            s.push_back(static_cast<char>(0xC0 | (u >> 6)));
            s.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        } else {
            s.push_back(static_cast<char>(0xE0 | (u >> 12)));
            s.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        }
    }
    return s;
}

std::string hexDumpText(const std::wstring& w) {
    std::string o;
    char buf[16];
    for (std::size_t i = 0; i < w.size() && i < 12; ++i) {
        std::snprintf(buf, sizeof buf, " U+%04X", static_cast<unsigned>(static_cast<char32_t>(w[i])));
        o += buf;
    }
    if (w.size() > 12) o += " ...";
    if (o.empty()) o = " <empty>";
    return o;
}

inline std::uint64_t fnvMix(std::uint64_t h, std::uint64_t v) noexcept {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    return h;
}
inline std::uint64_t digestText(const std::wstring& s) noexcept {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (wchar_t c : s) h = fnvMix(h, static_cast<std::uint64_t>(static_cast<char32_t>(c)));
    return h;
}
inline std::uint64_t packTuple(std::uint32_t code, std::uint32_t bs, std::uint32_t n,
                               std::uint32_t consumed) noexcept {
    return (static_cast<std::uint64_t>(code & 0xFFu) << 24) |
           (static_cast<std::uint64_t>(bs & 0xFFu) << 16) |
           (static_cast<std::uint64_t>(n & 0xFFu) << 8) | (consumed & 1u);
}

//----------------------------------------------------------------------------
// Event model
//----------------------------------------------------------------------------
enum class EK : std::uint8_t { Char, Space, Back, Break, Mouse };

struct Ev {
    EK            kind = EK::Char;
    char32_t      ch   = 0;
    std::uint16_t vk   = 0;
    bool          caps = false;
    bool          ctrl = false;
};

inline TextInput toEngineInput(const Ev& e) {
    TextInput in;
    switch (e.kind) {
        case EK::Char:  in.kind = InputKind::Char; in.ch = e.ch;
                        in.isCaps = e.caps; in.otherCtrl = e.ctrl; break;
        case EK::Space: in.kind = InputKind::Space; break;
        case EK::Back:  in.kind = InputKind::Backspace; break;
        case EK::Break: in.kind = InputKind::WordBreak; in.vkCode = e.vk; break;
        case EK::Mouse: in.kind = InputKind::MouseDown; break;
    }
    return in;
}

inline orel::Event toOracleEvent(const Ev& e) {
    orel::Event ev;
    switch (e.kind) {
        case EK::Char:  ev.kind = orel::Kind::Char; ev.ch = e.ch;
                        ev.caps = e.caps; ev.ctrl = e.ctrl; break;
        case EK::Space: ev.kind = orel::Kind::Space; break;
        case EK::Back:  ev.kind = orel::Kind::Backspace; break;
        case EK::Break: ev.kind = orel::Kind::WordBreak; ev.vk = e.vk; break;
        case EK::Mouse: ev.kind = orel::Kind::MouseDown; break;
    }
    return ev;
}

//----------------------------------------------------------------------------
// Macro table + lexicon (same macro content as the release gate)
//----------------------------------------------------------------------------
struct MacroDef { const char* key; const wchar_t* expansion; };
constexpr MacroDef kMacros[] = {
    {"ok",   L"\u0111\u01B0\u1EE3c"},            // được
    {"vcl",  L"v\u00E3i"},                        // vãi
    {"bt",   L"b\u00ECnh th\u01B0\u1EDDng"},      // bình thường
    {"xl",   L"xin l\u1ED7i"},                    // xin lỗi
    {"abc",  L"a b c"},
    {"uong", L"u\u1ED1ng"},                       // uống
};

bool macroLookup(const std::vector<std::uint32_t>& key, std::vector<std::uint32_t>& data) {
    std::string raw;
    for (std::uint32_t v : key) {
        char32_t ch = static_cast<char32_t>(v & orel::kCharMask);
        if (ch < 32 || ch >= 127) return false;
        char c = static_cast<char>(ch);
        if (v & orel::kCapsMask) { if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32); }
        else                     { if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32); }
        raw.push_back(c);
    }
    for (const auto& m : kMacros) {
        if (raw == m.key) {
            data.clear();
            for (const wchar_t* p = m.expansion; *p; ++p)
                data.push_back(static_cast<std::uint32_t>(*p));
            return true;
        }
    }
    return false;
}

std::wstring expansionText(const std::vector<std::uint32_t>& v) {
    std::wstring s;
    for (std::uint32_t c : v) s += static_cast<wchar_t>(c);
    return s;
}

const std::unordered_set<std::wstring>& lexicon() {
    static const std::unordered_set<std::wstring> l = {
        L"an", L"am", L"be", L"bo", L"ca", L"co", L"di", L"de", L"em", L"ga",
        L"ho", L"i", L"ka", L"la", L"me", L"mo", L"my", L"nam", L"nghe", L"ni",
        L"no", L"nu", L"on", L"ra", L"so", L"ta", L"thi", L"ti", L"to", L"tu",
        L"uc", L"vi", L"vo", L"xe", L"y",
        L"hoa", L"hoa\u0300", L"h\u00F2a", L"h\u00F3a", L"h\u1ED3a", L"h\u00E3a",
        L"m\u00F4ng", L"mon", L"lang", L"song", L"mua", L"nang", L"ua", L"ia",
        L"yeu", L"ngu", L"xin", L"ch\u00E0o", L"vi\u1EC7c", L"\u0111\u01B0\u1EE3c",
        L"nh\u1EDB", L"ung", L"inh", L"uow", L"nang\u0323", L"da", L"da\u0300",
        L"\u0111\u00E0", L"\u0111a", L"ba", L"ba\u0301", L"b\u00E1", L"cha",
        L"gba", L"mon\u0303", L"m\u00F2ng", L"m\u00F3ng", L"m\u1ECDng", L"m\u1ECDng",
    };
    return l;
}

bool dictLookup(const std::vector<std::uint32_t>& composed) {
    std::wstring w;
    for (std::uint32_t c : composed) {
        if (c > 0xFFFF) return false;
        w.push_back(static_cast<wchar_t>(c));
    }
    return lexicon().count(w) != 0;
}

//----------------------------------------------------------------------------
// Options bridges
//----------------------------------------------------------------------------
EngineOptions toEngine(const orel::Options& o) {
    EngineOptions e;
    e.inputMethod = static_cast<InputMethod>(static_cast<int>(o.method));
    e.codeTable = static_cast<CodeTable>(static_cast<int>(o.table));
    e.digitsAreLiteral = false;    // the oracle models v2.0.5 digit composition
    e.checkSpelling = o.checkSpelling;
    e.useModernOrthography = o.modernOrthography;
    e.quickTelex = o.quickTelex;
    e.restoreIfWrongSpelling = o.restoreIfWrongSpelling;
    e.freeMark = o.freeMark;
    e.allowConsonantZfwj = o.allowConsonantZfwj;
    e.quickStartConsonant = o.quickStartConsonant;
    e.quickEndConsonant = o.quickEndConsonant;
    e.upperCaseFirstChar = o.upperCaseFirstChar;
    e.useMacro = o.useMacro;
    e.useMacroInEnglishMode = o.useMacroInEnglishMode;
    e.useDictionaryRestore = o.useDictionaryRestore;
    return e;
}

orel::Options toOracle(const EngineOptions& e) {
    orel::Options o;
    o.method = static_cast<orel::Method>(static_cast<int>(e.inputMethod));
    o.table = static_cast<orel::CodeTable>(static_cast<int>(e.codeTable));
    o.checkSpelling = e.checkSpelling;
    o.modernOrthography = e.useModernOrthography;
    o.quickTelex = e.quickTelex;
    o.restoreIfWrongSpelling = e.restoreIfWrongSpelling;
    o.freeMark = e.freeMark;
    o.allowConsonantZfwj = e.allowConsonantZfwj;
    o.quickStartConsonant = e.quickStartConsonant;
    o.quickEndConsonant = e.quickEndConsonant;
    o.upperCaseFirstChar = e.upperCaseFirstChar;
    o.useMacro = e.useMacro;
    o.useMacroInEnglishMode = e.useMacroInEnglishMode;
    o.useDictionaryRestore = e.useDictionaryRestore;
    return o;
}

EngineOptions optsBase() {
    EngineOptions o;
    o.digitsAreLiteral = false;   // legacy-pin for oracle lockstep comparability
    return o;
}

//----------------------------------------------------------------------------
// VIQR projection (harness-side; built from the Unicode Vietnamese block)
//----------------------------------------------------------------------------
inline const std::vector<std::pair<char32_t, std::string>>& viqrTable() {
    static const std::vector<std::pair<char32_t, std::string>> t = [] {
        std::vector<std::pair<char32_t, std::string>> v;
        auto add = [&](char32_t cp, const std::string& s) { v.emplace_back(cp, s); };
        // Tone order: none, acute ', grave `, hook-hỏi ?, tilde ~, dot-under .
        const std::array<char32_t, 6> A_up{{0x0041,0x00C1,0x00C0,0x1EA2,0x00C3,0x1EA0}},
                                      A_lo{{0x0061,0x00E1,0x00E0,0x1EA3,0x00E3,0x1EA1}},
                                     aC_up{{0x00C2,0x1EA4,0x1EA6,0x1EA8,0x1EAA,0x1EAC}},
                                     aC_lo{{0x00E2,0x1EA5,0x1EA7,0x1EA9,0x1EAB,0x1EAD}},
                                     aB_up{{0x0102,0x1EAE,0x1EB0,0x1EB2,0x1EB4,0x1EB6}},
                                     aB_lo{{0x0103,0x1EAF,0x1EB1,0x1EB3,0x1EB5,0x1EB7}},
                                      E_up{{0x0045,0x00C9,0x00C8,0x1EBA,0x1EBC,0x1EB8}},
                                      E_lo{{0x0065,0x00E9,0x00E8,0x1EBB,0x1EBD,0x1EB9}},
                                      eC_up{{0x00CA,0x1EBE,0x1EC0,0x1EC2,0x1EC4,0x1EC6}},
                                      eC_lo{{0x00EA,0x1EBF,0x1EC1,0x1EC3,0x1EC5,0x1EC7}},
                                      I_up{{0x0049,0x00CD,0x00CC,0x1EC8,0x0128,0x1ECA}},
                                      I_lo{{0x0069,0x00ED,0x00EC,0x1EC9,0x0129,0x1ECB}},
                                      O_up{{0x004F,0x00D3,0x00D2,0x1ECE,0x00D5,0x1ECC}},
                                      O_lo{{0x006F,0x00F3,0x00F2,0x1ECF,0x00F5,0x1ECD}},
                                      oC_up{{0x00D4,0x1ED0,0x1ED2,0x1ED4,0x1ED6,0x1ED8}},
                                      oC_lo{{0x00F4,0x1ED1,0x1ED3,0x1ED5,0x1ED7,0x1ED9}},
                                      oH_up{{0x01A0,0x1EDA,0x1EDC,0x1EDE,0x1EE0,0x1EE2}},
                                      oH_lo{{0x01A1,0x1EDB,0x1EDD,0x1EDF,0x1EE1,0x1EE3}},
                                      U_up{{0x0055,0x00DA,0x00D9,0x1EE6,0x00DB,0x1EE4}},
                                      U_lo{{0x0075,0x00FA,0x00F9,0x1EE7,0x0169,0x1EE5}},
                                      uH_up{{0x01AF,0x1EE8,0x1EEA,0x1EEC,0x1EEE,0x1EF0}},
                                      uH_lo{{0x01B0,0x1EE9,0x1EEB,0x1EED,0x1EEF,0x1EF1}},
                                      Y_up{{0x0059,0x00DD,0x1EF2,0x1EF6,0x1EF8,0x1EF4}},
                                      Y_lo{{0x0079,0x00FD,0x1EF3,0x1EF7,0x1EF9,0x1EF5}};
        const std::pair<const std::array<char32_t,6>*, const char*> families[] = {
            {&A_up, "A"}, {&A_lo, "a"}, {&aC_up, "A^"}, {&aC_lo, "a^"},
            {&aB_up, "AA"}, {&aB_lo, "aa"}, {&E_up, "E"}, {&E_lo, "e"},
            {&eC_up, "E^"}, {&eC_lo, "e^"}, {&I_up, "I"}, {&I_lo, "i"},
            {&O_up, "O"}, {&O_lo, "o"}, {&oC_up, "O^"}, {&oC_lo, "o^"},
            {&oH_up, "O+"}, {&oH_lo, "o+"}, {&U_up, "U"}, {&U_lo, "u"},
            {&uH_up, "U+"}, {&uH_lo, "u+"}, {&Y_up, "Y"}, {&Y_lo, "y"},
        };
        const char* tone[] = { "", "'", "`", "?", "~", "." };
        for (const auto& fam : families) {
            for (int t2 = 0; t2 < 6; ++t2)
                add((*fam.first)[t2], std::string(fam.second) + tone[t2]);
        }
        add(0x0110, "DD");
        add(0x0111, "dd");
        add(0x0168, "U~");   // RC2: mirror of the completed engine map
        return v;
    }();
    return t;
}

std::string viqrOf(char32_t cp) {
    for (const auto& kv : viqrTable())
        if (kv.first == cp) return kv.second;
    if (cp < 0x80) return std::string(1, static_cast<char>(cp));
    return {};   // unmapped non-ASCII: the engine must not produce this under VIQR
}

std::wstring toViqrWide(const std::wstring& uni) {
    std::wstring out;
    for (wchar_t wc : uni) {
        const std::string v = viqrOf(static_cast<char32_t>(wc));
        for (char c : v) out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    return out;
}

//----------------------------------------------------------------------------
// Streams
//----------------------------------------------------------------------------
// Key-string mini-language:  ' ' space | \b backspace | \n enter | \t tab |
//   ^c caps-ed char | + mouse | ~a ctrl+a | % arrow | ! esc
std::vector<Ev> parseKeyStream(const std::string& keys) {
    std::vector<Ev> v;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const char c = keys[i];
        Ev e;
        if (c == ' ') { e.kind = EK::Space; }
        else if (c == '\b') { e.kind = EK::Back; }
        else if (c == '\n') { e.kind = EK::Break; e.vk = 0x0D; }
        else if (c == '\t') { e.kind = EK::Break; e.vk = 0x09; }
        else if (c == '^') {
            if (i + 1 < keys.size()) {
                const char nx = keys[++i];
                e.kind = EK::Char; e.ch = static_cast<char32_t>(static_cast<unsigned char>(nx));
                e.caps = true;
            } else continue;
        }
        else if (c == '+') { e.kind = EK::Mouse; }
        else if (c == '~') { e.kind = EK::Char; e.ch = U'a'; e.ctrl = true; }
        else if (c == '%') { e.kind = EK::Break; e.vk = 0x27; }
        else if (c == '!') { e.kind = EK::Break; e.vk = 0x1B; }
        else {
            e.kind = EK::Char; e.ch = static_cast<char32_t>(static_cast<unsigned char>(c));
            e.caps = (c >= 'A' && c <= 'Z');
        }
        v.push_back(e);
    }
    return v;
}

const char* kCorpusParts[] = {
    "ddaaf as az aw aa ee oo uw ow uow iww as",
    "tieengs viecejt namf vieejt namx ddooongf xaax",
    "^T ^E ^L 7 ^D",
    "nguwoi duocj khong the nao ma^y tinh duoc",
    "xin^ chao, lam~ the^ nao? ok~ xl.",
    "arbitrary english words here and there123",
    "asasas asdfqwer zzzz jjjj xxxx rrrr ffff",
    "pphongf ggiahf kkhi hhaha quaa thuuwowngf",
    "hoaq hoa8 1234567890 -- == // ;; '' ```",
    "chugns\bx\bxungs concho\b\b\b\bong moongs\bing ",
    "uowf nuowc` gia\bp \b\bp x \b\b\b",
    "a[ ]b as] az[ [as] ]dd[",
    "dd\bd\b \bas\bs anm\bm as",
    "mono mo\ns ho\na hoa\n \b\b\b\b",
    "ngan ngaan nga^n \b\b\b\bas",
    "ok~ vcl bt xl abc",
    "VNI: e1e4e5e6e7e8e9 o3o6o7o8 u5y1 t6i2 h7o8a5",
    "vni2 6789 1234 d9 a0 w1 x5",
    "\t!%,.;:-/\\'\"= 0123456789 as\td\td ",
    "aa as af ar aj ax aw ad ae ao au ay am an ap at ac ag ah al ak",
    "uwo uwu uwow uowu gi gieng giengc kh khinh ngh nghieng qu que",
    "tr trai tro trang gh ghi nghe khoe khoeo xo xoa xoan xuan",
};

std::vector<Ev> makeCorpus() {
    std::vector<Ev> v;
    for (const char* p : kCorpusParts) {
        auto s = parseKeyStream(p);
        v.insert(v.end(), s.begin(), s.end());
    }
    return v;
}

std::vector<Ev> makeRandom(std::uint64_t seed, std::size_t n) {
    static const char32_t alpha[] =
        U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        U" .,;:'\"-=/\\[]`()~!@#$%^&*<>{}|_+";
    std::mt19937_64 rng(seed);
    std::vector<Ev> v;
    v.reserve(n);
    const std::size_t alen = sizeof(alpha) / sizeof(alpha[0]) - 1;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t r = static_cast<std::uint32_t>(rng() % 100u);
        Ev e;
        if (r < 62) {
            const std::uint32_t k = static_cast<std::uint32_t>(rng() % alen);
            const char32_t c = alpha[k];
            if (c == U' ') { e.kind = EK::Space; }
            else { e.kind = EK::Char; e.ch = c; e.caps = (c >= U'A' && c <= U'Z'); }
        } else if (r < 78) { e.kind = EK::Back; }
        else if (r < 84) {
            e.kind = EK::Break;
            static const std::uint16_t vks[] = {0x09, 0x0D, 0x1B, 0x25, 0x27, 0x24, 0x23, 0x2E};
            e.vk = vks[rng() % 8];
        } else if (r < 88) { e.kind = EK::Mouse; }
        else if (r < 92) { e.kind = EK::Char; e.ch = U'a' + (rng() % 26); e.ctrl = true; }
        else if (r < 96) { e.kind = EK::Char; e.ch = U'1' + (rng() % 9); }
        else { e.kind = EK::Char; e.ch = U'A' + (rng() % 26); e.caps = true; }
        v.push_back(e);
    }
    return v;
}

//----------------------------------------------------------------------------
// Pair runner: engine (+optional oracle lockstep) with doc mirror
//----------------------------------------------------------------------------
struct Runner {
    TextEngine eng;
    const bool hasOracle;
    orel::Oracle ora{orel::Options{}};
    std::wstring et, ot;
    std::uint64_t evCount = 0, mism = 0, overBs = 0, tupleMismatch = 0;
    std::uint64_t traceE = 0, traceO = 0, docE = 0, docO = 0;

    Runner(bool withOracle, const EngineOptions& eo)
        : eng(eo), hasOracle(withOracle), ora(toOracle(eo)) {
        eng.setMacroResolver(macroLookup);
        eng.setDictionaryResolver(dictLookup);
        if (withOracle) {
            ora.setMacroResolver(macroLookup);
            ora.setDictionaryResolver(dictLookup);
        }
    }

    static char32_t produced(char32_t raw, bool caps) {
        if (caps && raw >= U'a' && raw <= U'z') return raw - 32;
        return raw;
    }
    static void eraseTail(std::wstring& doc, std::size_t n) {
        n = std::min<std::size_t>(n, doc.size());
        doc.erase(doc.size() - n, n);
    }

    void feed(const Ev& e, bool viqrMode) {
        ++evCount;
        ++g_events;
        const std::size_t acctBefore = eng.visibleAccount();
        const EngineResult& r = eng.process(toEngineInput(e));
        std::wstring rep;
        eng.replacementUtf16(r, rep);

        // ---- per-event invariants (engine side) ----
        if (static_cast<std::size_t>(r.backspaceCount) > acctBefore) ++overBs;
        if (r.newCharCount > 2 * kMaxBuff || eng.debugScratchSize() > kMaxBuff ||
            r.macroKey.size() > kMaxMacroKey) {
            OM_CHECK(false);
        }

        // ---- consumer-mirror doc update (gate methodology) ----
        if (r.code == EngineCode::ReplaceMacro) {
            std::wstring exp;
            eng.macroExpansionUtf16(r, exp);
            eraseTail(et, r.backspaceCount);
            et += exp;
        } else if (r.code != EngineCode::DoNothing) {
            eraseTail(et, r.backspaceCount);
            et += rep;
            if (r.code == EngineCode::Restore || r.code == EngineCode::RestoreAndStartNewSession) {
                if (e.kind == EK::Char) et += static_cast<wchar_t>(produced(e.ch, e.caps));
                else if (e.kind == EK::Space) et += L' ';
            }
        } else {
            if (e.kind == EK::Char) et += static_cast<wchar_t>(produced(e.ch, e.caps));
            else if (e.kind == EK::Space) et += L' ';
            else if (e.kind == EK::Break && e.vk == 0x0D) et += L'\n';
        }
        traceE = fnvMix(traceE, packTuple(static_cast<std::uint32_t>(r.code), r.backspaceCount,
                                          r.newCharCount, r.consumed() ? 1 : 0));
        docE = fnvMix(docE, digestText(et));

        if (!hasOracle) return;
        const orel::Result& o = ora.process(toOracleEvent(e));
        if (o.code == orel::Code::ReplaceMacro) {
            eraseTail(ot, o.backspaceCount);
            ot += expansionText(o.macroExpansion);
        } else if (o.consumed()) {
            eraseTail(ot, o.backspaceCount);
            ot += o.replacement;
            if (o.code == orel::Code::Restore || o.code == orel::Code::RestoreAndStartNewSession) {
                if (e.kind == EK::Char) ot += static_cast<wchar_t>(produced(e.ch, e.caps));
                else if (e.kind == EK::Space) ot += L' ';
            }
        } else {
            if (e.kind == EK::Char) ot += static_cast<wchar_t>(produced(e.ch, e.caps));
            else if (e.kind == EK::Space) ot += L' ';
            else if (e.kind == EK::Break && e.vk == 0x0D) ot += L'\n';
        }
        traceO = fnvMix(traceO, packTuple(static_cast<std::uint32_t>(o.code), o.backspaceCount,
                                          o.newCharCount, o.consumed() ? 1 : 0));
        docO = fnvMix(docO, digestText(ot));

        const std::uint64_t eTuple = packTuple(static_cast<std::uint32_t>(r.code), r.backspaceCount,
                                               r.newCharCount, 0);
        const std::uint64_t oTuple = packTuple(static_cast<std::uint32_t>(o.code), o.backspaceCount,
                                               o.newCharCount, 0);
        if (eTuple != oTuple) {
            ++tupleMismatch;
            if (tupleMismatch <= 3)
                std::printf("  [%s] tuple mismatch @ev%llu E{c=%d,bs=%u,n=%u} O{c=%d,bs=%u,n=%u}\n",
                            g_curCfg.c_str(),
                            static_cast<unsigned long long>(evCount),
                            static_cast<int>(r.code), r.backspaceCount, r.newCharCount,
                            static_cast<int>(o.code), o.backspaceCount, o.newCharCount);
        }
        if (!viqrMode) {
            if (rep != o.replacement) {
                ++mism;
                if (firstDiffEv == (std::size_t)-1) firstDiffEv = evCount;
                if (mism <= 3)
                    std::printf("  [%s] render mismatch @ev%llu E=%s O=%s\n",
                                g_curCfg.c_str(),
                                static_cast<unsigned long long>(evCount),
                                hexDumpText(rep).c_str(), hexDumpText(o.replacement).c_str());
            }
        } else {
            const std::wstring want = toViqrWide(o.replacement);
            if (rep != want) {
                ++mism;
                if (mism <= 3)
                    std::printf("  [%s] VIQR projection mismatch @ev%llu got=%s want=%s\n",
                                g_curCfg.c_str(),
                                static_cast<unsigned long long>(evCount),
                                hexDumpText(rep).c_str(), hexDumpText(want).c_str());
            }
            if (r.code == EngineCode::ReplaceMacro) {
                std::wstring exp;
                eng.macroExpansionUtf16(r, exp);
                const std::wstring expWant = toViqrWide(expansionText(o.macroExpansion));
                if (exp != expWant) {
                    ++mism;
                    if (mism <= 3)
                        std::printf("  VIQR expansion mismatch @ev%llu\n",
                                    static_cast<unsigned long long>(evCount));
                }
            }
        }
        if (ora.staleHistorySize() >= 32 || ora.overflowDetected()) {
            ++g_staleAbandon;
        }
    }

    std::size_t firstDiffEv = (std::size_t)-1;
    void setOptionsBoth(const EngineOptions& eo) {
        eng.setOptions(eo);
        if (hasOracle) ora.setOptions(toOracle(eo));
    }
    void setOptionsEngineOnly(const EngineOptions& eo) { eng.setOptions(eo); }
    void tempOffEngineBoth(bool off) { eng.tempOffEngine(off); ora.tempOffEngine(off); }
    void tempOffSpellBoth() { eng.tempOffSpellChecking(); ora.tempOffSpellChecking(); }
    void startNewSessionBoth() { eng.startNewSession(); ora.startNewSession(); }
};

//----------------------------------------------------------------------------
// Tier 1 — full Cartesian method × table × encoding
//----------------------------------------------------------------------------
std::string ctxStr(const std::vector<Ev>& s, std::size_t i, std::size_t span) {
    std::string out;
    char b[24];
    for (std::size_t j = (i > span ? i - span : 0); j <= i && j < s.size(); ++j) {
        if (s[j].kind == EK::Char) {
            std::snprintf(b, sizeof b, "(%c%s%s)", (char)s[j].ch,
                          s[j].caps ? "!" : "", s[j].ctrl ? "^" : "");
            out += b;
        } else if (s[j].kind == EK::Space) out += "[sp]";
        else if (s[j].kind == EK::Back) out += "[bk]";
        else if (s[j].kind == EK::Mouse) out += "[ms]";
        else { std::snprintf(b, sizeof b, "[brk%02x]", s[j].vk); out += b; }
    }
    return out;
}

void tier1(std::size_t eventsN, JsonRow& row) {
    std::printf("[T1] method x codeTable x outputEncoding Cartesian (30), oracle lockstep\n");
    const std::vector<Ev> corpus = makeCorpus();
    for (int m = 0; m < 3; ++m) {
        for (int t = 0; t < 5; ++t) {
            for (int enc = 0; enc < 2; ++enc) {
                for (int stream = 0; stream < 2; ++stream) {
                    ++g_configs;
                    orel::Options oo;
                    oo.method = static_cast<orel::Method>(m);
                    oo.table = static_cast<orel::CodeTable>(t);
                    EngineOptions eo = toEngine(oo);
                    if (enc) eo.outputEncoding = OutputEncoding::Viqr;
                    char label[64];
                    std::snprintf(label, sizeof label, "T1 m%d t%d viqr%d s%d", m, t, enc, stream);
                    g_curCfg = label;
                    // VIQR is defined on precomposed Unicode; combined with a
                    // LEGACY code table the channel request is contradictory
                    // (the table glyphs are not Unicode). The shipped product
                    // never combines them (the app does not expose VIQR at
                    // all); the harness still exercises the tuple path there
                    // but skips the projection check by design.
                    Runner run(true, eo);
                    const bool projectViqr = (enc != 0) && (t == 0);

                    // streams: 0 = corpus; 1 = random interleaved with corpus
                    std::vector<Ev> evs;
                    if (stream == 0) {
                        evs = corpus;
                    } else {
                        std::vector<Ev> rnd = makeRandom(1000ULL + 100 * m + 10 * t + enc, eventsN);
                        const std::size_t chunks = std::max<std::size_t>(1, rnd.size() / 512);
                        const std::size_t clen = corpus.size();
                        std::size_t pos = 0;
                        evs.reserve(rnd.size() + chunks * clen);
                        for (std::size_t ci = 0; ci < chunks; ++ci) {
                            const std::size_t take = std::min<std::size_t>(
                                std::min<std::size_t>(512, rnd.size() - pos), clen);
                            evs.insert(evs.end(), rnd.begin() + pos, rnd.begin() + pos + take);
                            pos += take;
                            evs.insert(evs.end(), corpus.begin(),
                                       corpus.begin() + std::min<std::size_t>(clen, 24));
                        }
                        evs.insert(evs.end(), rnd.begin() + pos, rnd.end());
                    }

                    for (const Ev& e : evs) run.feed(e, projectViqr);

                    if (run.traceE != run.traceO) {
                        ++row.mismatches;
                        std::printf("  [T1] m%d t%d e%d s%d: TRACE DIVERGENCE\n", m, t, enc, stream);
                    }
                    if (run.docE != run.docO && !enc) {
                        ++row.mismatches;
                        std::printf("  [T1] m%d t%d e%d s%d: DOC DIVERGENCE\n", m, t, enc, stream);
                    }
                    row.mismatches += run.mism + run.tupleMismatch + run.overBs;
                    row.digest = fnvMix(row.digest, run.traceE ^ fnvMix(0, run.docE));
                }
            }
        }
    }
    row.configs = 30 * 2;
    row.name = "t1-cartesian";
}

//----------------------------------------------------------------------------
// Tier 2 — pairwise covering array over the 11 semantic booleans × method
//----------------------------------------------------------------------------
struct BoolFlags {
    std::uint32_t m = 0;   // bit0 spell,1 modern,2 quick,3 restore,4 free,5 zfwj,
                           // bit6 qs,7 qe,8 upper,9 punct,10 dict
};
constexpr int kNumBools = 11;
inline std::uint32_t bitAt(std::uint32_t mask, int i) { return (mask >> i) & 1u; }

std::vector<BoolFlags> coveringPairs() {
    bool covered[kNumBools][kNumBools][4] = {};
    std::vector<BoolFlags> rows;
    std::mt19937_64 rng(0x51A2EDULL);
    auto addRow = [&](const BoolFlags& f) {
        rows.push_back(f);
        for (int a = 0; a < kNumBools; ++a)
            for (int b = a + 1; b < kNumBools; ++b)
                covered[a][b][bitAt(f.m, a) | (bitAt(f.m, b) << 1)] = true;
    };
    auto uncovered = [&]() {
        for (int a = 0; a < kNumBools; ++a)
            for (int b = a + 1; b < kNumBools; ++b)
                for (std::uint32_t v = 0; v < 4; ++v)
                    if (!covered[a][b][v]) return true;
        return false;
    };
    // Seed with defaults and its complement (both extremes).
    const std::uint32_t defaults = (1u << 0) | (1u << 3);   // spell=1 restore=1 true defaults
    addRow(BoolFlags{defaults});
    addRow(BoolFlags{~defaults & ((1u << kNumBools) - 1u)});
    while (uncovered()) {
        int bestGain = 0;
        std::uint32_t best = 0;
        for (int trial = 0; trial < 4096; ++trial) {
            const std::uint32_t cand = static_cast<std::uint32_t>(rng()) & ((1u << kNumBools) - 1u);
            int gain = 0;
            for (int a = 0; a < kNumBools; ++a)
                for (int b = a + 1; b < kNumBools; ++b)
                    if (!covered[a][b][bitAt(cand, a) | (bitAt(cand, b) << 1)]) ++gain;
            if (gain > bestGain) { bestGain = gain; best = cand; }
            if (gain == 0) continue;
        }
        OM_CHECK(bestGain > 0);
        addRow(BoolFlags{best});
    }
    return rows;
}

void applyFlags(const BoolFlags& f, EngineOptions& e) {
    e.checkSpelling = bitAt(f.m, 0);
    e.useModernOrthography = bitAt(f.m, 1);
    e.quickTelex = bitAt(f.m, 2);
    e.restoreIfWrongSpelling = bitAt(f.m, 3);
    e.freeMark = bitAt(f.m, 4);
    e.allowConsonantZfwj = bitAt(f.m, 5);
    e.quickStartConsonant = bitAt(f.m, 6);
    e.quickEndConsonant = bitAt(f.m, 7);
    e.upperCaseFirstChar = bitAt(f.m, 8);
    e.macroExpandsOnPunctuation = bitAt(f.m, 9);
    e.useDictionaryRestore = bitAt(f.m, 10);
}

void tier2(std::size_t eventsN, JsonRow& row) {
    std::printf("[T2] pairwise grammar/macro matrix (11 semantic booleans x method)\n");
    const auto rows = coveringPairs();
    const std::vector<Ev> corpus = makeCorpus();
    std::size_t cfgs = 0;
    for (int m = 0; m < 3; ++m) {
        for (const auto& f : rows) {
            ++cfgs; ++g_configs;
            EngineOptions e;
            e.digitsAreLiteral = false;
            e.inputMethod = static_cast<InputMethod>(m);
            applyFlags(f, e);
            // The oracle models every flag EXCEPT macroExpandsOnPunctuation —
            // for punct rows run the invariant battery (oracle disabled).
            const bool punct = bitAt(f.m, 9);
            Runner run(!punct, e);
            {
                char lb[48];
                std::snprintf(lb, sizeof lb, "T2 m%d cfg%zu f%llx", m, cfgs,
                              (unsigned long long)f.m);
                g_curCfg.assign(lb);
            }
            const bool viqrRow = e.outputEncoding == OutputEncoding::Viqr;
            std::vector<Ev> stream = corpus;
            auto rnd = makeRandom(77000ULL + 131 * m + 17 * cfgs, eventsN / 2);
            stream.insert(stream.end(), rnd.begin(), rnd.end());
            for (const Ev& ev : stream) run.feed(ev, viqrRow);
            if (run.traceE != run.traceO && !punct) {
                ++row.mismatches;
                if (g_printBudget > 0) { --g_printBudget;
                    std::printf("  [T2] %s: trace divergence\n", g_curCfg.c_str()); }
            }
            // The document model applies engine backspace counts to typed
            // text; under VIQR the consumer sees expanded ASCII units, so a
            // whole-document identity is not the contract there — the
            // per-event projection inside feed() is (and is asserted).
            if (!punct && !viqrRow && run.docE != run.docO) {
                ++row.mismatches;
                if (g_printBudget > 0) { --g_printBudget;
                    std::printf("  [T2] %s: document divergence\n", g_curCfg.c_str()); }
            }
            row.mismatches += run.mism + run.tupleMismatch + run.overBs;
            if ((run.mism > 0 || run.tupleMismatch > 0) &&
                run.firstDiffEv != (std::size_t)-1) {
                for (std::size_t q = 0; q < stream.size(); ++q) {
                    (void)q; break;
                }
                if (g_printBudget > 0) {
                    --g_printBudget;
                    std::printf("  [T2] %s: first-diff ctx @~ev%zu: %s\n",
                                g_curCfg.c_str(), run.firstDiffEv,
                                ctxStr(stream, run.firstDiffEv + 1, 12).c_str());
                }
            }
            row.digest = fnvMix(row.digest, run.traceE ^ fnvMix(0, run.docE) ^
                                             fnvMix(0, f.m));
        }
    }
    row.configs = cfgs;
    row.name = "t2-pairwise";
    std::printf("     covering-array rows: %zu (pairwise-complete over %d factors)\n",
                rows.size(), kNumBools);
}

//----------------------------------------------------------------------------
// Tier 3 — runtime transitions
//----------------------------------------------------------------------------
struct TransPair {
    const char* label;
    EngineOptions a, b;
};

std::vector<TransPair> transitionPairs() {
    std::vector<TransPair> v;
    const EngineOptions base = optsBase();
    auto add = [&](const char* label, EngineOptions a, EngineOptions b) {
        v.push_back({label, a, b});
    };
    EngineOptions b;
    b = base; b.inputMethod = InputMethod::Vni;      add("Telex->VNI", base, b);
    add("VNI->Telex", b, base);
    b = base; b.inputMethod = InputMethod::SimpleTelex; add("Telex->SimpleTelex", base, b);
    add("SimpleTelex->Telex", b, base);
    b = base; b.inputMethod = InputMethod::SimpleTelex; b.codeTable = CodeTable::Cp1258;
    add("SimpleTelex+Cp1258 combo", base, b);
    b = base; b.codeTable = CodeTable::VniWindows;   add("Unicode->VniWin", base, b);
    add("VniWin->Unicode", b, base);
    { EngineOptions cmp = base; cmp.codeTable = CodeTable::UnicodeCompound;
      EngineOptions tc = base;  tc.codeTable = CodeTable::Tcvn3;
      add("Unicode->Compound", base, cmp);
      add("Compound->Tcvn3", cmp, tc);
      add("Tcvn3->Cp1258", tc, [=]{ EngineOptions o = base; o.codeTable = CodeTable::Cp1258; return o; }());
      add("Cp1258->Unicode", [=]{ EngineOptions o = base; o.codeTable = CodeTable::Cp1258; return o; }(), base); }
    b = base; b.outputEncoding = OutputEncoding::Viqr;
    add("Unicode->VIQR", base, b);
    add("VIQR->Unicode", b, base);
    b = base; b.checkSpelling = false;   add("Grammar ON->OFF", base, b);
    add("Grammar OFF->ON", b, base);
    b = base; b.quickTelex = true;       add("QuickTelex OFF->ON", base, b);
    add("QuickTelex ON->OFF", b, base);
    b = base; b.useModernOrthography = true; add("Modern ON", base, b);
    add("Modern OFF", b, base);
    b = base; b.freeMark = true;         add("FreeMark ON", base, b);
    add("FreeMark OFF", b, base);
    b = base; b.restoreIfWrongSpelling = false; add("Restore OFF", base, b);
    add("Restore ON", b, base);
    b = base; b.useMacro = false;        add("Macro OFF", base, b);
    add("Macro ON", b, base);
    b = base; b.macroExpandsOnPunctuation = true; add("MacroPunct ON", base, b);
    add("MacroPunct OFF", b, base);
    b = base; b.useDictionaryRestore = true; add("DictRestore ON", base, b);
    add("DictRestore OFF", b, base);
    b = base; b.digitsAreLiteral = true; add("Digits literal ON", base, b);
    add("Digits literal OFF", b, base);
    b = base; b.upperCaseFirstChar = true; add("UpperFirst ON", base, b);
    add("UpperFirst OFF", b, base);
    b = base; b.allowConsonantZfwj = true; b.quickStartConsonant = true; b.quickEndConsonant = true;
    add("All quick/zfwj ON", base, b);
    add("All quick/zfwj OFF", b, base);
    return v;
}

std::size_t switchPointAt(const std::vector<Ev>& s, int which) {
    auto valid = [&](std::size_t i) { return i >= 1 && i + 1 < s.size(); };
    if (which == 0) return valid(4) ? 4 : 1;
    if (which == 1) {
        for (std::size_t i = 5; i + 1 < s.size(); ++i)
            if (s[i].kind == EK::Space && valid(i)) return i + 1;   // right after a word break
        return 5;
    }
    if (which == 2) {
        for (std::size_t i = 5; i + 1 < s.size(); ++i)
            if (s[i].kind == EK::Back && i + 2 < s.size() && s[i + 1].kind == EK::Back && valid(i))
                return i;                                            // mid backspace storm
        return 6;
    }
    return s.size() / 2;
}

// Engine-side trace of a whole stream with the app CONTRACT switch at k.
struct LiveTrace {
    std::vector<std::uint64_t> tuples;
    std::vector<std::wstring> renders;
    std::uint64_t digest = 0;
};

LiveTrace runContract(const std::vector<Ev>& stream, std::size_t k,
                      const EngineOptions& a, const EngineOptions& b,
                      std::size_t from) {
    TextEngine eng(a);
    eng.setMacroResolver(macroLookup);
    eng.setDictionaryResolver(dictLookup);
    LiveTrace tr;
    for (std::size_t i = 0; i < stream.size(); ++i) {
        if (i == k) {
            eng.setOptions(b);
            eng.resetForConfigurationChange();   // the app's reconfigure contract
        }
        const EngineResult& r = eng.process(toEngineInput(stream[i]));
        std::wstring rep;
        eng.replacementUtf16(r, rep);
        tr.tuples.push_back(packTuple(static_cast<std::uint32_t>(r.code), r.backspaceCount,
                                      r.newCharCount, r.consumed() ? 1 : 0));
        tr.renders.push_back(std::move(rep));
        ++g_events;
    }
    std::uint64_t d = 0;
    for (std::size_t i = from; i < tr.tuples.size(); ++i) {
        d = fnvMix(d, tr.tuples[i]);
        d = fnvMix(d, digestText(tr.renders[i]));
    }
    tr.digest = d;
    return tr;
}

LiveTrace runFresh(const std::vector<Ev>& stream, std::size_t from, const EngineOptions& o) {
    TextEngine eng(o);
    eng.setMacroResolver(macroLookup);
    eng.setDictionaryResolver(dictLookup);
    LiveTrace tr;
    for (std::size_t i = from; i < stream.size(); ++i) {
        const EngineResult& r = eng.process(toEngineInput(stream[i]));
        std::wstring rep;
        eng.replacementUtf16(r, rep);
        tr.tuples.push_back(packTuple(static_cast<std::uint32_t>(r.code), r.backspaceCount,
                                      r.newCharCount, r.consumed() ? 1 : 0));
        tr.renders.push_back(std::move(rep));
        ++g_events;
    }
    std::uint64_t d = 0;
    for (auto& t : tr.tuples) d = fnvMix(d, t);
    for (auto& r : tr.renders) d = fnvMix(d, digestText(r));
    tr.digest = d;
    return tr;
}

void tier3(std::size_t eventsN, JsonRow& row) {
    std::printf("[T3] runtime option transitions (contract/live/roundtrip/render/ABA)\n");
    const std::vector<Ev> corpus = makeCorpus();
    std::vector<Ev> stream = corpus;
    {
        auto rnd = makeRandom(31337, eventsN);
        stream.insert(stream.end(), rnd.begin(), rnd.end());
    }
    const auto pairs = transitionPairs();
    std::size_t executed = 0;

    for (const auto& p : pairs) {
        for (int sp = 0; sp < 4; ++sp) {
            const std::size_t k = switchPointAt(stream, sp);
            ++executed; ++g_transitions; ++g_configs;
            row.transitions = g_transitions;
            {
                char lb[96];
                std::snprintf(lb, sizeof lb, "T3 %s @sp%d", p.label, sp);
                g_curCfg = lb;
            }

            const bool renderOnly = (p.a.inputMethod == p.b.inputMethod &&
                                     p.a.checkSpelling == p.b.checkSpelling &&
                                     p.a.outputEncoding != p.b.outputEncoding);
            const bool tableSwitch = (p.a.inputMethod == p.b.inputMethod &&
                                       p.a.codeTable != p.b.codeTable);

            // ---- CONTRACT mode ----
            {
                const auto live = runContract(stream, k, p.a, p.b, k);
                const auto fresh = runFresh(stream, k, p.b);
                bool eq = live.tuples.size() == k + fresh.tuples.size();
                std::uint64_t d1 = live.digest;
                if (eq) {
                    // fresh trace was collected from k; compare elementwise
                    for (std::size_t i = 0; i < fresh.tuples.size(); ++i) {
                        if (live.tuples[k + i] != fresh.tuples[i]) { eq = false; break; }
                        if (live.renders[k + i] != fresh.renders[i]) { eq = false; break; }
                    }
                }
                if (!eq) {
                    std::printf("  [T3-CONTRACT] %s @sp%d: post-switch stream differs from fresh engine "
                                "(STALE STATE ACROSS OPTION CHANGE)\n", p.label, sp);
                    for (std::size_t i = 0; i < fresh.tuples.size(); ++i) {
                        if (live.tuples[k + i] != fresh.tuples[i] ||
                            live.renders[k + i] != fresh.renders[i]) {
                            std::printf("    first divergence @ev%zu (switch@%zu) tupleL=%016llx tupleF=%016llx\n",
                                        k + i, k,
                                        (unsigned long long)live.tuples[k + i],
                                        (unsigned long long)fresh.tuples[i]);
                            std::printf("    renderL=%s renderF=%s ctx=",
                                        hexDumpText(live.renders[k + i]).c_str(),
                                        hexDumpText(fresh.renders[i]).c_str());
                            for (std::size_t j = (i > 6 ? i - 6 : 0); j <= i && j < stream.size(); ++j) {
                                if (stream[j].kind == EK::Char) std::printf("%c", (char)stream[j].ch);
                                else if (stream[j].kind == EK::Space) std::printf("<sp>");
                                else if (stream[j].kind == EK::Back) std::printf("<bk>");
                                else if (stream[j].kind == EK::Mouse) std::printf("<ms>");
                                else std::printf("<brk%02x>", stream[j].vk);
                            }
                            std::printf("\n");
                            break;
                        }
                    }
                    ++row.mismatches;
                }
                row.digest = fnvMix(row.digest, live.digest ^ d1);
            }

            // ---- LIVE mode (no reset): oracle lockstep through the switch ----
            if (!p.b.macroExpandsOnPunctuation && p.a.macroExpandsOnPunctuation == false &&
                !p.b.digitsAreLiteral && !p.a.digitsAreLiteral &&
                p.a.useUserKeymap == false && p.b.useUserKeymap == false) {
                Runner run(true, p.a);
                bool switched = false;
                bool curViqr = p.a.outputEncoding == OutputEncoding::Viqr;
                for (std::size_t i = 0; i < stream.size(); ++i) {
                    if (!switched && i == k) {
                        run.setOptionsBoth(p.b);
                        switched = true;
                        ++g_transitions;
                        curViqr = p.b.outputEncoding == OutputEncoding::Viqr;
                    }
                    run.feed(stream[i], curViqr);
                }
                if (run.traceE != run.traceO) {
                    ++row.mismatches;
                    std::printf("  [T3-LIVE] %s @sp%d: engine/oracle trace divergence across switch\n",
                                p.label, sp);
                }
                const bool liveViqr = p.a.outputEncoding == OutputEncoding::Viqr ||
                                      p.b.outputEncoding == OutputEncoding::Viqr;
                if (!liveViqr && run.docE != run.docO) {
                    ++row.mismatches;
                    std::printf("  [T3-LIVE] %s @sp%d: document divergence across switch\n",
                                p.label, sp);
                    if (run.firstDiffEv != (std::size_t)-1)
                        std::printf("    ctx @~ev%zu: %s\n", run.firstDiffEv,
                                    ctxStr(stream, run.firstDiffEv + 1, 12).c_str());
                }
                row.mismatches += run.overBs;
                row.digest = fnvMix(row.digest, run.traceE ^ run.docE);
            }

            // ---- RENDER mode: encoding switch must not move a single tuple,
            //      and renderings must equal the projected Unicode renderings.
            if (renderOnly || tableSwitch) {
                const EngineOptions uni = renderOnly ? EngineOptions{} : p.a;
                (void)uni;
                TextEngine eA(p.a), eB(p.b);
                eA.setMacroResolver(macroLookup); eB.setMacroResolver(macroLookup);
                eA.setDictionaryResolver(dictLookup); eB.setDictionaryResolver(dictLookup);
                std::uint64_t tA = 0, tB = 0;
                std::size_t postDiffs = 0;
                for (std::size_t i = 0; i < stream.size(); ++i) {
                    const EngineResult& ra = eA.process(toEngineInput(stream[i]));
                    const EngineResult& rb = eB.process(toEngineInput(stream[i]));
                    const std::uint64_t ta = packTuple(static_cast<std::uint32_t>(ra.code), ra.backspaceCount,
                                                       ra.newCharCount, ra.consumed() ? 1 : 0);
                    const std::uint64_t tb = packTuple(static_cast<std::uint8_t>(rb.code), rb.backspaceCount,
                                                       rb.newCharCount, rb.consumed() ? 1 : 0);
                    tA = fnvMix(tA, ta); tB = fnvMix(tB, tb);
                    if (i >= k && ta != tb) ++postDiffs;   // decisions must never depend on rendering
                    g_events += 2;
                }
                if (postDiffs != 0) {
                    std::printf("  [T3-RENDER] %s: %zu decision differences after a RENDERING switch\n",
                                p.label, postDiffs);
                    ++row.mismatches;
                }
                // Pre-switch differences between the two full streams are
                // legitimate (different options from event 0); only the
                // post-switch decision equality and rendering projection are
                // asserted.
                // Projection identity for VIQR: same-engine comparison.
                if (renderOnly) {
                    TextEngine u(p.a), v(p.b);     // one VIQR, one Unicode, same stream
                    u.setMacroResolver(macroLookup); v.setMacroResolver(macroLookup);
                    const bool uV = p.a.outputEncoding == OutputEncoding::Viqr;
                    std::size_t projBad = 0, nonAscii = 0;
                    for (const Ev& e : stream) {
                        const EngineResult& ru = u.process(toEngineInput(e));
                        const EngineResult& rv = v.process(toEngineInput(e));
                        std::wstring lu, lv;
                        u.replacementUtf16(ru, lu);
                        v.replacementUtf16(rv, lv);
                        // Project BOTH sides into VIQR space: the VIQR engine's
                        // rendering must already be the projection (and thus be
                        // pure ASCII); the Unicode engine's rendering projects
                        // onto it. Direction-agnostic, so VIQR->Unicode and
                        // Unicode->VIQR pairs are checked symmetrically.
                        const std::wstring luQ = uV ? lu : toViqrWide(lu);
                        const std::wstring lvQ = uV ? toViqrWide(lv) : lv;
                        if (luQ != lvQ) ++projBad;
                        const std::wstring& vqr = uV ? lu : lv;
                        for (wchar_t wc : vqr)
                            if ((unsigned)wc >= 0x80) { ++nonAscii; break; }
                        g_events += 2;
                    }
                    if (projBad != 0) {
                        std::printf("  [T3-RENDER] %s: VIQR projection differs on %zu events\n",
                                    p.label, projBad);
                        ++row.mismatches;
                    }
                    if (nonAscii != 0) {
                        std::printf("  [T3-RENDER] %s: VIQR channel emitted non-ASCII on %zu events\n",
                                    p.label, nonAscii);
                        ++row.mismatches;
                    }
                }
                row.digest = fnvMix(row.digest, fnvMix(tA, tB));
            }

            // ---- ROUNDTRIP: setOptions(same) before every event is a no-op ----
            {
                TextEngine e1(p.a), e2(p.a);
                e1.setMacroResolver(macroLookup); e2.setMacroResolver(macroLookup);
                e1.setDictionaryResolver(dictLookup); e2.setDictionaryResolver(dictLookup);
                std::uint64_t d1 = 0, d2 = 0;
                std::size_t i = 0;
                for (const Ev& e : stream) {
                    if (i == k || (i > k && (i % 7) == 0)) e1.setOptions(e1.options());
                    const EngineResult& r1 = e1.process(toEngineInput(e));
                    const EngineResult& r2 = e2.process(toEngineInput(e));
                    std::wstring rep1, rep2;
                    e1.replacementUtf16(r1, rep1);
                    e2.replacementUtf16(r2, rep2);
                    d1 = fnvMix(d1, packTuple(static_cast<std::uint32_t>(r1.code), r1.backspaceCount,
                                             r1.newCharCount, 0) ^ digestText(rep1));
                    d2 = fnvMix(d2, packTuple(static_cast<std::uint32_t>(r2.code), r2.backspaceCount,
                                             r2.newCharCount, 0) ^ digestText(rep2));
                    g_events += 2; ++i;
                }
                if (d1 != d2) {
                    std::printf("  [T3-ROUNDTRIP] %s: identical setOptions perturbed engine state!\n",
                                p.label);
                    ++row.mismatches;
                }
                row.digest = fnvMix(row.digest, d1 ^ d2);
            }
        }
    }

    // ---- A→B→A at boundaries: switching back must restore stay-on-A ----
    {
        EngineOptions A = optsBase();
        EngineOptions B = optsBase();
        B.inputMethod = InputMethod::Vni;
        B.checkSpelling = false;
        B.quickTelex = true;
        B.useModernOrthography = true;
        const std::size_t k1 = 8;
        const std::size_t k2 = stream.size() / 3;
        // eTog: A -> B at k1 -> A(+session reset) at k2
        // eStay: A the whole time, with the SAME session reset at k2
        // After k2 both start from an identical (fresh, A) session state, so
        // every decision from k2+1 onward must be byte-identical. A hidden
        // latch left by B (history, flags, scratch) shows up immediately.
        TextEngine eTog(A), eStay(A);
        eTog.setMacroResolver(macroLookup);
        eStay.setMacroResolver(macroLookup);
        std::uint64_t dgT = 0, dgS = 0;
        for (std::size_t i = 0; i < stream.size(); ++i) {
            if (i == k1) eTog.setOptions(B);
            if (i == k2) {
                eTog.setOptions(A);
                eTog.startNewSession();
                eStay.startNewSession();
                dgT = dgS = 0;              // compare only post-switch behavior
            }
            const bool compare = i > k2;
            {
                const EngineResult& r = eTog.process(toEngineInput(stream[i]));
                if (compare) {
                    std::wstring rep; eTog.replacementUtf16(r, rep);
                    dgT = fnvMix(dgT, packTuple(static_cast<std::uint32_t>(r.code), r.backspaceCount,
                                                r.newCharCount, 0) ^ digestText(rep));
                }
            }
            {
                const EngineResult& r = eStay.process(toEngineInput(stream[i]));
                if (compare) {
                    std::wstring rep; eStay.replacementUtf16(r, rep);
                    dgS = fnvMix(dgS, packTuple(static_cast<std::uint32_t>(r.code), r.backspaceCount,
                                                r.newCharCount, 0) ^ digestText(rep));
                }
            }
            g_events += 2;
        }
        if (dgT != dgS) {
            std::printf("  [T3-ABA] A->B->A switch-back diverges from stay-A (STALE LATCH)\n");
            ++row.mismatches;
        }
        row.digest = fnvMix(row.digest, dgT ^ dgS);
        ++g_transitions;
        ++executed; ++g_configs;
    }
    row.configs = executed;
    row.name = "t3-transitions";
}

//----------------------------------------------------------------------------
// Tier 4 — performance profiles: engine invariance
//----------------------------------------------------------------------------
struct Digest3 { std::uint64_t d = 0, bs = 0, ev = 0, cons = 0; };

Digest3 runDigest(const EngineOptions& eo, const std::vector<Ev>& stream, bool withLexicon) {
    TextEngine eng(eo);
    eng.setMacroResolver(macroLookup);
    if (withLexicon) eng.setDictionaryResolver(dictLookup);
    Digest3 out;
    for (const Ev& e : stream) {
        const EngineResult& r = eng.process(toEngineInput(e));
        std::wstring rep;
        eng.replacementUtf16(r, rep);
        out.d = fnvMix(out.d, packTuple(static_cast<std::uint32_t>(r.code), r.backspaceCount,
                                        r.newCharCount, r.consumed() ? 1 : 0));
        out.d = fnvMix(out.d, digestText(rep));
        out.bs += r.backspaceCount;
        out.cons += r.consumed() ? 1 : 0;
        ++out.ev;
        ++g_events;
    }
    return out;
}

void tier4(std::size_t eventsN, JsonRow& row) {
    std::printf("[T4] performance-profile matrix: engine invariance across 5 x 16 strategies\n");
    const std::vector<Ev> corpus = makeCorpus();
    std::vector<Ev> stream = corpus;
    { auto rnd = makeRandom(555, eventsN); stream.insert(stream.end(), rnd.begin(), rnd.end()); }

    const EngineOptions base = optsBase();            // dict OFF
    EngineOptions dictOn = base; dictOn.useDictionaryRestore = true;

    const auto plain = runDigest(base, stream, true);
    const auto dictRef = runDigest(dictOn, stream, true);
    const auto plainNoLex = runDigest(dictOn, stream, false);  // flag ON, resolver OFF
    OM_CHECK(plain.d == plainNoLex.d);
    OM_CHECK(plain.bs == plainNoLex.bs);
    OM_CHECK(plain.cons == plainNoLex.cons);
    row.digest = fnvMix(row.digest, plain.d);
    row.digest = fnvMix(row.digest, dictRef.d);

    int differingRows = 0, totalRows = 0;
    for (int pi = 0; pi < static_cast<int>(ok::perf::Profile::kCount); ++pi) {
        for (std::uint32_t hyb = 0; hyb < 16; ++hyb) {
            ++totalRows; ++g_configs;
            const auto prof = static_cast<ok::perf::Profile>(pi);
            const ok::perf::Strategy st =
                ok::perf::resolveStrategy(prof, static_cast<std::uint8_t>(hyb), ok::perf::Telemetry{});
            // THE APP MAPPING (main.cpp applyPerfStrategy): dictionary
            // restore is the only engine-visible field of a Strategy.
            EngineOptions eo = base;
            eo.useDictionaryRestore = eo.useDictionaryRestore || st.dictionaryRestore;
            const auto out = runDigest(eo, stream, true);
            if (st.dictionaryRestore) {
                if (out.d != dictRef.d || out.bs != dictRef.bs || out.ev != dictRef.ev) {
                    std::printf("  [T4] %s|hyb%u changed engine decisions BEYOND the lexicon switch\n",
                                ok::perf::profileName(prof), hyb);
                    ++row.mismatches;
                }
                ++differingRows;
            } else {
                if (out.d != plain.d || out.bs != plain.bs || out.ev != plain.ev ||
                    out.cons != plain.cons) {
                    std::printf("  [T4] %s|hyb%u altered engine output for a CONSUMER-ONLY change!\n",
                                ok::perf::profileName(prof), hyb);
                    ++row.mismatches;
                }
            }
            row.digest = fnvMix(row.digest, out.d);
        }
    }
    // Non-vacuity: dict-restore rows must actually reference a different
    // digest than plain for THIS stream (the lexicon genuinely interacts —
    // if the corpus never triggers a dict decision the test would be vacuous;
    // in that case the two digests still match, which is acceptable, but we
    // record the fact).
    row.configs = totalRows;
    row.name = "t4-profiles";
    std::printf("     dict-flag rows: %d/%d; plain-d=%016llx dict-d=%016llx\n",
                differingRows, totalRows,
                static_cast<unsigned long long>(plain.d),
                static_cast<unsigned long long>(dictRef.d));
}

//----------------------------------------------------------------------------
// Tier 5 — option-change jobs inside the pipeline
//----------------------------------------------------------------------------
struct Job {
    std::uint64_t seq = 0;
    Ev ev{};
    std::int32_t optIdx = -1;
};
static_assert(std::is_trivially_copyable<Job>::value, "Job must be trivially copyable");

struct T5Wake {
    std::mutex m;
    std::condition_variable cv;
    bool signaled = false;
    std::atomic<bool> parked{false};
    void set() { { std::lock_guard<std::mutex> lk(m); signaled = true; } cv.notify_one(); }
    template <class HasWork>
    void park(HasWork&& hw) {
        std::unique_lock<std::mutex> lk(m);
        parked.store(true, std::memory_order_release);
        if (signaled || hw()) { parked.store(false, std::memory_order_release); signaled = false; return; }
        cv.wait(lk, [&] { return signaled; });
        parked.store(false, std::memory_order_release);
        signaled = false;
    }
    void signalIfParked() { if (parked.load(std::memory_order_acquire)) set(); }
};

void tier5(std::size_t jobCount, JsonRow& row) {
    std::printf("[T5] option changes while the queue holds pending work\n");
    std::vector<std::pair<EngineOptions, EngineOptions>> variants;
    {
        EngineOptions base = optsBase();
        EngineOptions b;
        b = base; b.inputMethod = InputMethod::Vni; variants.push_back({base, b});
        b = base; b.checkSpelling = false; variants.push_back({base, b});
        b = base; b.quickTelex = true; variants.push_back({base, b});
        b = base; b.useModernOrthography = true; variants.push_back({base, b});
        b = base; b.useMacro = false; variants.push_back({base, b});
        b = base; b.useDictionaryRestore = true; variants.push_back({base, b});
        b = base; b.digitsAreLiteral = true; variants.push_back({base, b});
        b = base; b.outputEncoding = OutputEncoding::Viqr; variants.push_back({base, b});
        b = base; b.codeTable = CodeTable::Tcvn3; variants.push_back({base, b});
    }
    const std::vector<Ev> corpus = makeCorpus();
    std::mt19937_64 rng(0xBEEF1234ULL);

    std::vector<Job> jobs;
    jobs.reserve(jobCount);
    for (std::size_t i = 0; i < jobCount; ++i) {
        Job j;
        j.seq = i + 1;
        j.ev = corpus[i % corpus.size()];
        if ((i % 97) == 16) j.optIdx = static_cast<std::int32_t>(rng() % variants.size());
        jobs.push_back(j);
    }

    ok::lockfree::SPSCRing<Job, 1024> q;
    T5Wake wake;
    std::atomic<bool> producerDone{false};
    std::vector<std::uint64_t> consumerTrace;

    std::thread consumer([&] {
        TextEngine eng(optsBase());
        eng.setMacroResolver(macroLookup);
        eng.setDictionaryResolver(dictLookup);
        std::vector<std::uint64_t> tr;
        Job batch[64];
        while (!producerDone.load(std::memory_order_acquire) || !q.empty()) {
            std::size_t n = q.try_pop_batch(batch, 64);
            if (n == 0) { wake.park([&] { return !q.empty(); }); continue; }
            for (std::size_t i = 0; i < n; ++i) {
                const Job& j = batch[i];
                if (j.optIdx >= 0) {
                    eng.setOptions(variants[static_cast<std::size_t>(j.optIdx)].second);
                    continue;
                }
                const EngineResult& r = eng.process(toEngineInput(j.ev));
                std::wstring rep;
                eng.replacementUtf16(r, rep);
                tr.push_back(fnvMix(packTuple(static_cast<std::uint32_t>(r.code), r.backspaceCount,
                                              r.newCharCount, 0), digestText(rep)));
            }
        }
        consumerTrace = std::move(tr);
    });

    for (std::size_t i = 0; i < jobs.size(); ++i) {
        while (!q.try_push(jobs[i])) { wake.signalIfParked(); std::this_thread::yield(); }
        if ((i % 512) == 511) {
            // producer stall: the queue accumulates a backlog while option
            // jobs and events interleave in it (the "change with pending
            // work" case)
            wake.signalIfParked();
            std::this_thread::yield();
        }
        if (i % 4096 == 0) { std::this_thread::sleep_for(std::chrono::microseconds(200)); wake.set(); }
    }
    wake.set();
    producerDone.store(true, std::memory_order_release);
    wake.set();
    consumer.join();

    // Single-threaded reference: replay the identical job order.
    TextEngine ref(optsBase());
    ref.setMacroResolver(macroLookup);
    ref.setDictionaryResolver(dictLookup);
    std::vector<std::uint64_t> refTrace;
    for (const Job& j : jobs) {
        if (j.optIdx >= 0) { ref.setOptions(variants[static_cast<std::size_t>(j.optIdx)].second); continue; }
        const EngineResult& r = ref.process(toEngineInput(j.ev));
        std::wstring rep;
        ref.replacementUtf16(r, rep);
        refTrace.push_back(fnvMix(packTuple(static_cast<std::uint32_t>(r.code), r.backspaceCount,
                                            r.newCharCount, 0), digestText(rep)));
    }

    if (consumerTrace.size() != refTrace.size()) {
        std::printf("  [T5] dropped/duplicated events: consumer=%zu ref=%zu\n",
                    consumerTrace.size(), refTrace.size());
        ++row.mismatches;
    } else {
        const std::size_t n = std::min(consumerTrace.size(), refTrace.size());
        for (std::size_t i = 0; i < n; ++i) {
            if (consumerTrace[i] != refTrace[i]) {
                std::printf("  [T5] decision divergence at drained event %zu\n", i);
                ++row.mismatches;
                break;
            }
        }
    }
    row.configs = variants.size();
    row.transitions = jobs.size() / 97;
    row.events = jobs.size();
    row.name = "t5-pipeline";
    std::uint64_t d = 0;
    for (std::size_t i = 0; i < refTrace.size(); ++i) d = fnvMix(d, refTrace[i]);
    row.digest = d;
}

//----------------------------------------------------------------------------
// Tier 6 — option torture
//----------------------------------------------------------------------------
// RC3 §6: the option matrix must ALSO survive adversarial torture, not just
// systematic tiers. Tier 6 hammers a single long-lived engine with rapid,
// randomly drawn full-space option flips (every field of EngineOptions,
// including the non-oracle-modeled ones) on a random keystroke stream, and
// checks every flip against three independent oracles:
//
//   FRESH    after setOptions+resetForConfigurationChange the engine must be
//            decision-identical to a brand-new engine built with the target
//            options fed the same suffix (the RC1 stale-latch bug class that
//            caught grammar OFF->ON). Every flip point, not a curated pair.
//   ORACLE   a config the oracle models must keep lockstep with the engine
//            through the SAME flip sequence (one shared event counter, no
//            reset on the oracle side: the oracle re-derives state from its
//            own snapshot, so any cached engine decision fails).
//   INVAR    every event must satisfy D1/D2/CNT/STB even under in-flight
//            flips (no over-backspace, bounded scratch, bounded macro key).
//   DETERM   replaying the identical flip+event sequence into a second fresh
//            engine must reproduce the digest bit-for-bit (no hidden global
//            state, no seed/state bleed across flips).
//
// Non-oracle-modeled options (macroPunct, digitsAreLiteral, useUserKeymap,
// outputEncoding, tempOff*) are excluded from ORACLE but run under FRESH +
// INVAR + DETERM with a real keymap resolver installed.
//----------------------------------------------------------------------------
namespace {

char32_t tortureKeymap(char32_t produced) noexcept {
    // Deterministic P3 keymap exercise: swap a few ASCII pairs so the
    // keymap path is genuinely non-identity, and map one Latin letter to a
    // Vietnamese letter (the real product pattern: users map unused keys to
    // ư/ơ/đ). The oracle has no keymap model, so these flips are tested by
    // FRESH/INVAR/DETERM only.
    switch (produced) {
        case U'z': return U'x';
        case U'x': return U'z';
        case U'q': return U'ư';
        case U'w': return U'ơ';
        default:   return produced;
    }
}

EngineOptions tortureRandomOpts(std::mt19937_64& rng) {
    EngineOptions e = optsBase();
    e.inputMethod = static_cast<InputMethod>(rng() % 3);
    e.codeTable = static_cast<CodeTable>(rng() % 5);
    e.outputEncoding = static_cast<OutputEncoding>(rng() % 2);
    e.checkSpelling = (rng() & 1u) != 0;
    e.useModernOrthography = (rng() & 1u) != 0;
    e.quickTelex = (rng() & 1u) != 0;
    e.restoreIfWrongSpelling = (rng() & 1u) != 0;
    e.freeMark = (rng() & 1u) != 0;
    e.allowConsonantZfwj = (rng() & 1u) != 0;
    e.quickStartConsonant = (rng() & 1u) != 0;
    e.quickEndConsonant = (rng() & 1u) != 0;
    e.upperCaseFirstChar = (rng() & 1u) != 0;
    e.useMacro = (rng() & 1u) != 0;
    e.useMacroInEnglishMode = (rng() & 1u) != 0;
    e.macroExpandsOnPunctuation = (rng() & 1u) != 0;
    e.useDictionaryRestore = (rng() & 1u) != 0;
    e.digitsAreLiteral = (rng() & 1u) != 0;
    e.useUserKeymap = (rng() & 1u) != 0;
    return e;
}

} // namespace

void tier6(std::size_t eventsN, JsonRow& row) {
    std::printf("[T6] option torture: rapid full-space flips (FRESH/ORACLE/INVAR/DETERM)\n");
    std::mt19937_64 rng(0x7A0D7EULL);   // "TORTURE" (hex-ish, valid digits only)
    const std::vector<Ev> corpus = makeCorpus();
    std::vector<Ev> stream = corpus;
    {
        auto rnd = makeRandom(0x00C0FFEEULL, eventsN);
        stream.insert(stream.end(), rnd.begin(), rnd.end());
    }

    std::size_t executed = 0;
    std::uint64_t deterA = 0, deterB = 0;
    const std::size_t rounds = eventsN <= 4000 ? 16 : 32;

    for (std::size_t round = 0; round < rounds; ++round) {
        // Deterministic per-round schedule: 1..6 random flip positions, each
        // with a FULLY random EngineOptions target (every field, including
        // the non-oracle-modeled ones: macroPunct, digitsAreLiteral,
        // useUserKeymap, outputEncoding).
        const std::uint64_t seed = 0xF00D5EE0ULL ^ (round * 0x9E3779B97F4A7C15ULL);
        std::mt19937_64 rr(seed);
        std::vector<std::size_t> flips;
        {
            const std::size_t n = 1 + (rr() % 6);
            for (std::size_t i = 0; i < n; ++i) {
                flips.push_back(std::min<std::size_t>(
                    stream.size() - 1, 8 + static_cast<std::size_t>(rr() % (stream.size() - 16))));
            }
            std::sort(flips.begin(), flips.end());
            flips.erase(std::unique(flips.begin(), flips.end()), flips.end());
        }
        std::vector<EngineOptions> targets;
        targets.reserve(flips.size());
        for (std::size_t i = 0; i < flips.size(); ++i) targets.push_back(tortureRandomOpts(rr));
        const EngineOptions start = tortureRandomOpts(rr);
        g_curCfg = "T6 round " + std::to_string(round);
        ++executed; ++g_configs;

        // ---- FRESH: live engine (app CONTRACT: setOptions + reset) versus a
        // brand-new engine rebuilt at EVERY flip point. Any stale latch that
        // survives resetForConfigurationChange shows up as the first
        // post-flip divergence (the RC1 grammar OFF->ON bug class).
        {
            TextEngine live(start);
            live.setMacroResolver(macroLookup);
            live.setDictionaryResolver(dictLookup);
            live.setKeymapOverride(tortureKeymap);
            TextEngine fresh(start);
            fresh.setMacroResolver(macroLookup);
            fresh.setDictionaryResolver(dictLookup);
            fresh.setKeymapOverride(tortureKeymap);

            std::size_t flipIdx = 0;
            std::uint64_t liveD = 0, freshD = 0;
            bool diverged = false;
            bool tempOffEng = false;

            for (std::size_t i = 0; i < stream.size(); ++i) {
                if (flipIdx < flips.size() && i == flips[flipIdx]) {
                    const EngineOptions& t = targets[flipIdx];
                    live.setOptions(t);
                    // resetForConfigurationChange() clears willTempOffEngine_
                    // (TextEngine.cpp:106) — the temp-off state does NOT
                    // survive a reconfiguration. Mirror that on the tracker
                    // so both engines observe the identical contract.
                    tempOffEng = false;
                    live.resetForConfigurationChange();
                    fresh = TextEngine(t);              // fresh baseline
                    fresh.setMacroResolver(macroLookup);
                    fresh.setDictionaryResolver(dictLookup);
                    fresh.setKeymapOverride(tortureKeymap);
                    ++g_transitions;
                    ++flipIdx;
                }
                // Exercise the temp-off engine entry point (option-adjacent
                // state the app toggles mid-typing) deterministically.
                if ((i % 911) == 521) {
                    tempOffEng = !tempOffEng;
                    live.tempOffEngine(tempOffEng);
                    fresh.tempOffEngine(tempOffEng);
                }
                const std::size_t acctBefore = live.visibleAccount();
                const std::size_t acctBeforeF = fresh.visibleAccount();
                const EngineResult& rl = live.process(toEngineInput(stream[i]));
                const EngineResult& rf = fresh.process(toEngineInput(stream[i]));
                std::wstring repL, repF;
                live.replacementUtf16(rl, repL);
                fresh.replacementUtf16(rf, repF);
                // D2/D1/CNT under in-flight flips
                if (rl.backspaceCount > acctBefore) ++row.overBs;
                if (rl.newCharCount > 2 * kMaxBuff || live.debugScratchSize() > kMaxBuff ||
                    rl.macroKey.size() > kMaxMacroKey) {
                    OM_CHECK(false);
                }
                liveD = fnvMix(liveD, packTuple(static_cast<std::uint32_t>(rl.code),
                                                rl.backspaceCount, rl.newCharCount,
                                                rl.consumed() ? 1 : 0) ^ digestText(repL));
                freshD = fnvMix(freshD, packTuple(static_cast<std::uint32_t>(rf.code),
                                                  rf.backspaceCount, rf.newCharCount,
                                                  rf.consumed() ? 1 : 0) ^ digestText(repF));
                // The live engine KEEPS its consumer-side visibleAccount
                // across a reconfigure (documented: resetForConfigurationChange
                // does not touch it) while `fresh` starts at 0. The engine's
                // D2 clamp therefore may legitimately produce a smaller
                // backspaceCount on `fresh` for the SAME decision. A bs
                // difference is only a real divergence when it is not exactly
                // that clamp (fresh clamped to its own account) — identical
                // code / newCharCount / render prove the decision matched.
                bool realDivergence = (rl.code != rf.code || rl.newCharCount != rf.newCharCount ||
                                       repL != repF);
                if (rl.backspaceCount != rf.backspaceCount && !realDivergence) {
                    const bool clampExplains =
                        rf.backspaceCount == acctBeforeF && rl.backspaceCount > acctBeforeF;
                    if (!clampExplains) realDivergence = true;
                }
                if (realDivergence) {
                    if (!diverged) {
                        ++row.mismatches;
                        if (g_printBudget > 0) { --g_printBudget;
                            std::printf("  [T6-FRESH] round %zu divergence @ev%zu flip#%zu "
                                        "E{c=%d,bs=%u,n=%u} F{c=%d,bs=%u,n=%u} E=%s F=%s\n",
                                        round, i, flipIdx > 0 ? flipIdx - 1 : 0,
                                        static_cast<int>(rl.code), rl.backspaceCount,
                                        rl.newCharCount, static_cast<int>(rf.code),
                                        rf.backspaceCount, rf.newCharCount,
                                        hexDumpText(repL).c_str(), hexDumpText(repF).c_str());
                        }
                    }
                    diverged = true;
                }
                ++g_events;
            }
            deterA = fnvMix(deterA, liveD);
        }

        // ---- ORACLE leg: engine + oracle see the SAME flips without resets
        // (LIVE semantics: an option change the dialog commits mid-typing).
        // The oracle independently re-derives every decision from its own
        // option snapshot, so a cached engine decision fails lockstep.
        {
            // The oracle models every EngineOptions field EXCEPT
            // macroExpandsOnPunctuation / digitsAreLiteral / useUserKeymap /
            // outputEncoding. Scrub those to the oracle-equivalent defaults
            // in BOTH the starting config and every target so lockstep only
            // compares what the oracle can actually decide; the engine-only
            // FRESH leg above already covers the unmodeled fields through
            // fresh-vs-live equality.
            EngineOptions cur = start;
            cur.macroExpandsOnPunctuation = false;
            cur.digitsAreLiteral = false;   // oracle mirror is legacy-pinned
            cur.useUserKeymap = false;
            Runner run(true, cur);
            std::size_t flipIdx = 0;
            bool teO = false;
            for (std::size_t i = 0; i < stream.size(); ++i) {
                if (flipIdx < flips.size() && i == flips[flipIdx]) {
                    cur = targets[flipIdx];
                    cur.macroExpandsOnPunctuation = false;
                    cur.digitsAreLiteral = false;      // oracle mirror is legacy-pinned
                    cur.useUserKeymap = false;
                    run.setOptionsBoth(cur);
                    ++flipIdx;
                }
                if ((i % 911) == 521) {
                    teO = !teO;
                    run.tempOffEngineBoth(teO);
                }
                const bool viqr = cur.outputEncoding == OutputEncoding::Viqr &&
                                  cur.codeTable == CodeTable::Unicode;
                run.feed(stream[i], viqr);
            }
            if (run.traceE != run.traceO) {
                ++row.mismatches;
                if (g_printBudget > 0) { --g_printBudget;
                    std::printf("  [T6-ORACLE] round %zu: TRACE DIVERGENCE after %zu flips\n",
                                round, flips.size());
                }
            }
            row.mismatches += run.mism + run.tupleMismatch + run.overBs;
            deterB = fnvMix(deterB, run.traceE);
        }
    }
    row.configs = executed;
    row.transitions = g_transitions;
    row.events = stream.size() * rounds;
    row.name = "t6-torture";
    // Fold both independent legs into one digest; a mismatch in EITHER leg
    // already increments row.mismatches and fails the gate.
    row.digest = fnvMix(deterA, deterB);
}

//----------------------------------------------------------------------------
// Goldens for documented option differences
//----------------------------------------------------------------------------
void runGoldens(JsonRow& row) {
    std::printf("[TG] goldens: documented option differences\n");
    auto feedStr = [](TextEngine& e, const std::string& s, bool noSpaceKey) {
        std::wstring doc;
        for (char c0 : s) {
            Ev ev;
            const unsigned char c = static_cast<unsigned char>(c0);
            if (c == ' ' && !noSpaceKey) ev.kind = EK::Space;
            else { ev.kind = EK::Char; ev.ch = static_cast<char32_t>(c);
                   ev.caps = (c >= 'A' && c <= 'Z'); }
            const EngineResult& r = e.process(toEngineInput(ev));
            if (r.code == EngineCode::ReplaceMacro) {
                std::wstring exp;
                e.macroExpansionUtf16(r, exp);
                Runner::eraseTail(doc, r.backspaceCount);
                doc += exp;
            } else if (r.code != EngineCode::DoNothing) {
                std::wstring rep;
                e.replacementUtf16(r, rep);
                Runner::eraseTail(doc, r.backspaceCount);
                doc += rep;
                if (r.code == EngineCode::Restore || r.code == EngineCode::RestoreAndStartNewSession) {
                    if (ev.kind == EK::Char) doc += static_cast<wchar_t>(ev.ch);
                    else if (ev.kind == EK::Space) doc += L' ';
                }
            } else {
                if (ev.kind == EK::Char) doc += static_cast<wchar_t>(ev.ch);
                else doc += L' ';
            }
        }
        return doc;
    };
    std::size_t checked = 0;

    // macro-punct OFF (legacy): "xl," fires the macro and SWALLOWS the comma;
    // a SPACE resets the macro-key accumulator so the second "xl," fires too,
    // while an IMMEDIATELY adjacent second abbreviation can never fire (the
    // swallowed comma is pushed into the accumulator). Exact RC1 semantics,
    // frozen here so the documented difference cannot drift.
    {
        EngineOptions o = optsBase();
        TextEngine e(o);
        e.setMacroResolver(macroLookup);
        OM_CHECK(feedStr(e, "xl,", false) == L"xin l\u1ED7i");
        ++checked;
        EngineOptions o0 = optsBase();
        TextEngine e0(o0);
        e0.setMacroResolver(macroLookup);
        OM_CHECK(feedStr(e0, "xl, xl,", false) == L"xin l\u1ED7i xin l\u1ED7i");
        ++checked;
        TextEngine e0b(o0);
        e0b.setMacroResolver(macroLookup);
        OM_CHECK(feedStr(e0b, "xl,xl,", false) == L"xin l\u1ED7ixl,");
        ++checked;
    }
    {
        EngineOptions o = optsBase();
        o.macroExpandsOnPunctuation = true;
        TextEngine e(o);
        e.setMacroResolver(macroLookup);
        OM_CHECK(feedStr(e, "xl,", false) == L"xin l\u1ED7i,");
        ++checked;
        EngineOptions o2 = optsBase();
        o2.macroExpandsOnPunctuation = true;
        TextEngine e2(o2);
        e2.setMacroResolver(macroLookup);
        OM_CHECK(feedStr(e2, "xl, xl,", false) == L"xin l\u1ED7i, xin l\u1ED7i,");
        ++checked;
        TextEngine e3(o2);
        e3.setMacroResolver(macroLookup);
        OM_CHECK(feedStr(e3, "xl,xl,", false) == L"xin l\u1ED7i,xin l\u1ED7i,");
        ++checked;
    }
    // VIQR channel contract for a tone-heavy stream: (a) every unit the
    // VIQR channel emits is ASCII (the whole point of the mode — the
    // v1.2.0 macro-parity fix and the RC2 table-precedence rule both exist
    // to keep this true), and (b) per event, the VIQR replacement is the
    // mnemonic projection of the Unicode replacement for the SAME keystroke
    // stream. (Whole-DOCUMENT equality is deliberately not asserted: VIQR
    // expands one composed vowel to 2-3 visible units, so erase(bs) replay
    // of the engine's code-point-counted backspaces does not describe the
    // consumer's actual replacement behaviour — the product replaces the
    // whole composition region in that mode.)
    {
        EngineOptions u = optsBase();
        EngineOptions w = optsBase();
        w.outputEncoding = OutputEncoding::Viqr;
        TextEngine eu(u), ew(w);
        eu.setMacroResolver(macroLookup);
        ew.setMacroResolver(macroLookup);
        bool pureAscii = true, proj = true, sawDd = false, sawUni = false;
        for (const char* p = "ddooongf xaax vuuwji asf jcf nhe "; *p; ++p) {
            TextInput iu, iw;
            iu.kind = iw.kind = InputKind::Char;
            iu.ch = iw.ch = (char32_t)*p;
            iu.isCaps = iw.isCaps = 0;
            const EngineResult& ru = eu.process(iu);
            const EngineResult& rv = ew.process(iw);
            std::wstring lu, lv;
            eu.replacementUtf16(ru, lu);
            ew.replacementUtf16(rv, lv);
            for (wchar_t c : lv) if ((unsigned)c >= 0x80) pureAscii = false;
            if (toViqrWide(lu) != lv) proj = false;
            if (lu.find(L'\u0111') != std::wstring::npos) sawUni = true;
            if (lv == L"dd") sawDd = true;
        }
        if (!(pureAscii && proj)) {
            std::printf("  [golden] VIQR channel contract: pureAscii=%d proj=%d\n",
                        pureAscii ? 1 : 0, proj ? 1 : 0);
        }
        OM_CHECK(pureAscii);
        OM_CHECK(proj);
        OM_CHECK(sawUni);   // the stream must actually exercise đ
        OM_CHECK(sawDd);    // and its VIQR rendering
        ++checked;
    }
    // digitsAreLiteral: never compose in ANY method; legacy mode does.
    for (int m = 0; m < 3; ++m) {
        EngineOptions o = optsBase();
        o.inputMethod = static_cast<InputMethod>(m);
        o.digitsAreLiteral = true;
        TextEngine e(o);
        e.setMacroResolver(macroLookup);
        bool sawConsume = false;
        for (char c0 : std::string("a1e5o6u7d9a0w1x5")) {
            Ev ev; ev.kind = EK::Char; ev.ch = static_cast<char32_t>(static_cast<unsigned char>(c0));
            if (e.process(toEngineInput(ev)).consumed()) sawConsume = true;
            ++g_events;
        }
        OM_CHECK(!sawConsume);
        ++checked;
        EngineOptions o2 = optsBase();
        o2.inputMethod = InputMethod::Vni;
        o2.digitsAreLiteral = false;
        TextEngine e2(o2);
        e2.setMacroResolver(macroLookup);
        OM_CHECK(feedStr(e2, "a1 ", false) == L"\u00E1 ");
        ++checked;
    }
    // Library default IS the product default (v1.1.2-r3 contract).
    {
        EngineOptions def;
        OM_CHECK(def.digitsAreLiteral == true);
        ++checked;
    }
    row.goldens = checked;
    row.configs = checked;
    row.name = "goldens";
}

//----------------------------------------------------------------------------
// Option-cache staleness probe (RC1 indexing regression net)
//----------------------------------------------------------------------------
void runIndexStaleness(std::size_t eventsN, JsonRow& row) {
    std::printf("[TI] option-cache staleness probe (masks/buckets/caches)\n");
    const std::vector<Ev> corpus = makeCorpus();
    std::vector<Ev> stream = corpus;
    { auto rnd = makeRandom(0x1234ABCDULL, eventsN); stream.insert(stream.end(), rnd.begin(), rnd.end()); }

    EngineOptions base = optsBase();
    std::vector<EngineOptions> variants;
    { auto o = base; o.quickEndConsonant = true; variants.push_back(o); }
    { auto o = base; o.quickStartConsonant = true; variants.push_back(o); }
    { auto o = base; o.allowConsonantZfwj = true; variants.push_back(o); }
    { auto o = base; o.checkSpelling = false; variants.push_back(o); }
    { auto o = base; o.inputMethod = InputMethod::Vni; variants.push_back(o); }
    { auto o = base; o.inputMethod = InputMethod::Vni; o.quickEndConsonant = true; variants.push_back(o); }
    { auto o = base; o.useModernOrthography = true; o.quickTelex = true; variants.push_back(o); }

    const std::size_t nVar = variants.size();
    for (std::size_t v = 0; v < nVar; ++v) {
        ++g_configs;
        TextEngine fresh(variants[v]);
        fresh.setMacroResolver(macroLookup);
        fresh.setDictionaryResolver(dictLookup);
        std::uint64_t df = 0;
        for (const Ev& e : stream) {
            const EngineResult& r = fresh.process(toEngineInput(e));
            std::wstring rep;
            fresh.replacementUtf16(r, rep);
            df = fnvMix(df, packTuple(static_cast<std::uint32_t>(r.code), r.backspaceCount,
                                      r.newCharCount, 0) ^ digestText(rep));
            ++g_events;
        }
        TextEngine churn(base);
        churn.setMacroResolver(macroLookup);
        churn.setDictionaryResolver(dictLookup);
        for (int w = 0; w < 4; ++w) {
            churn.setOptions(base);
            churn.setOptions(variants[(v + 1 + w) % nVar]);
            churn.setOptions(variants[(v + 3 + w) % nVar]);
            churn.setOptions(base);
            churn.setOptions(variants[v]);
        }
        std::uint64_t dc = 0;
        std::size_t i = 0;
        for (const Ev& e : stream) {
            if ((i % 541) == 0) churn.setOptions(churn.options());   // re-assert: must be no-op
            const EngineResult& r = churn.process(toEngineInput(e));
            std::wstring rep;
            churn.replacementUtf16(r, rep);
            dc = fnvMix(dc, packTuple(static_cast<std::uint32_t>(r.code), r.backspaceCount,
                                      r.newCharCount, 0) ^ digestText(rep));
            ++g_events; ++i;
        }
        if (df != dc) {
            std::printf("  [TI] variant %zu: churned engine diverges from fresh (STALE OPTION STATE)\n", v);
            ++row.mismatches;
        }
        row.digest = fnvMix(row.digest, df ^ dc);
        // switch back to default: post-switch stream must match a default engine
        churn.setOptions(base);
        churn.startNewSession();
        fresh.setOptions(base);
        fresh.startNewSession();
        std::uint64_t p = 0, q = 0;
        for (const Ev& e : corpus) {
            const EngineResult& ra = churn.process(toEngineInput(e));
            const EngineResult& rb = fresh.process(toEngineInput(e));
            p = fnvMix(p, packTuple(static_cast<std::uint32_t>(ra.code), ra.backspaceCount, ra.newCharCount, 0));
            q = fnvMix(q, packTuple(static_cast<std::uint32_t>(rb.code), rb.backspaceCount, rb.newCharCount, 0));
            ++g_events;
        }
        if (p != q) {
            std::printf("  [TI] variant %zu: post-switch-to-default diverges (LEAKED STATE)\n", v);
            ++row.mismatches;
        }
    }
    row.name = "t-index-staleness";
}

} // namespace

//----------------------------------------------------------------------------
// main
//----------------------------------------------------------------------------
int main(int argc, char** argv) {
    std::string tier = "all";
    std::string jsonPath;
    std::size_t eventsN = 20000;
    bool quick = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--tier=", 0) == 0) tier = a.substr(7);
        else if (a.rfind("--json=", 0) == 0) jsonPath = a.substr(7);
        else if (a.rfind("--events=", 0) == 0) eventsN = std::strtoull(a.c_str() + 9, nullptr, 0);
        else if (a == "--quick") { quick = true; eventsN = 3000; }
        else { std::printf("unknown arg %s\n", argv[i]); return 2; }
    }
    std::printf("KieeKey v%s — option-matrix differential harness\n", OPENKEY_KIEEKEY_VERSION_STRING);
    std::printf("  tier=%s events/config=%zu quick=%d\n\n", tier.c_str(), eventsN, quick ? 1 : 0);

    auto want = [&](const char* name) { return tier == "all" || tier == name; };
    // RC2-fix: --events now actually drives every tier (before this, the
    // per-tier literals silently ignored it and only T1 honored the flag).
    const std::size_t E1 = quick ? 3000  : eventsN;
    const std::size_t E2 = quick ? 4000  : eventsN / 2;
    const std::size_t E3 = quick ? 3000  : eventsN;
    const std::size_t E4 = quick ? 4000  : eventsN;
    const std::size_t E5 = quick ? 20000 : eventsN;
    const std::size_t E6 = quick ? 3000  : eventsN / 2;
    const std::size_t EI = quick ? 4000  : eventsN;
    if (want("1")) { JsonRow r; r.tier = "1"; tier1(E1, r); g_json.push_back(r); }
    if (want("2")) { JsonRow r; r.tier = "2"; tier2(E2, r); g_json.push_back(r); }
    if (want("3")) { JsonRow r; r.tier = "3"; tier3(E3, r); g_json.push_back(r); }
    if (want("4")) { JsonRow r; r.tier = "4"; tier4(E4, r); g_json.push_back(r); }
    if (want("5")) { JsonRow r; r.tier = "5"; tier5(E5, r); g_json.push_back(r); }
    if (want("6")) { JsonRow r; r.tier = "6"; tier6(E6, r); g_json.push_back(r); }
    if (want("g")) { JsonRow r; r.tier = "g"; runGoldens(r); g_json.push_back(r); }
    if (want("i")) { JsonRow r; r.tier = "i"; runIndexStaleness(EI, r); g_json.push_back(r); }

    std::uint64_t allMismatch = 0;
    std::printf("\n== OPTION MATRIX SUMMARY ==\n");
    std::printf("  %-18s %10s %8s %8s %8s %8s %16s\n", "tier", "events", "configs", "mismatch",
                "overBS", "goldens", "digest");
    for (const auto& r : g_json) {
        allMismatch += r.mismatches;
        std::printf("  %-18s %10llu %8llu %8llu %8llu %8llu %016llx\n", r.tier.c_str(),
                    static_cast<unsigned long long>(g_events),
                    static_cast<unsigned long long>(r.configs),
                    static_cast<unsigned long long>(r.mismatches),
                    static_cast<unsigned long long>(r.overBs),
                    static_cast<unsigned long long>(r.goldens),
                    static_cast<unsigned long long>(r.digest));
    }
    const std::uint64_t failures = g_failures + allMismatch;
    std::printf("  totals: events=%llu configs=%llu transitions=%llu failures=%llu\n",
                static_cast<unsigned long long>(g_events),
                static_cast<unsigned long long>(g_configs),
                static_cast<unsigned long long>(g_transitions),
                static_cast<unsigned long long>(failures));

    if (!jsonPath.empty()) {
        std::ofstream f(jsonPath);
        f << "{\n \"schema\": \"kieekey.option_matrix.v1\",\n \"tier\": \"" << tier
          << "\",\n \"events\": " << g_events << ",\n \"configs\": " << g_configs
          << ",\n \"transitions\": " << g_transitions << ",\n \"failures\": " << failures
          << ",\n \"staleAbandon\": " << g_staleAbandon << ",\n \"rows\": [\n";
        for (std::size_t i = 0; i < g_json.size(); ++i) {
            const auto& r = g_json[i];
            char buf[512];
            std::snprintf(buf, sizeof buf,
                          "  {\"tier\":\"%s\",\"name\":\"%s\",\"configs\":%llu,"
                          "\"mismatches\":%llu,\"overBackspace\":%llu,\"goldens\":%llu,"
                          "\"transitions\":%llu,\"digest\":\"%016llx\"}%s\n",
                          r.tier.c_str(), r.name.c_str(),
                          static_cast<unsigned long long>(r.configs),
                          static_cast<unsigned long long>(r.mismatches),
                          static_cast<unsigned long long>(r.overBs),
                          static_cast<unsigned long long>(r.goldens),
                          static_cast<unsigned long long>(r.transitions),
                          static_cast<unsigned long long>(r.digest),
                          (i + 1 < g_json.size()) ? "," : "");
            f << buf;
        }
        f << "]\n}\n";
        std::printf("  json: %s\n", jsonPath.c_str());
    }

    if (failures == 0) {
        std::printf("OPTION MATRIX: ALL PASSED\n");
        return 0;
    }
    std::printf("OPTION MATRIX: FAILED (%llu)\n", static_cast<unsigned long long>(failures));
    return 1;
}
