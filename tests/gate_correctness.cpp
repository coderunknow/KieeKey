//============================================================================
// KieeKey v1.2.0 — tests/gate_correctness.cpp
// Differential correctness gate for the benchmark pipeline.
//
// Derived from tests/mega_correctness.cpp (same repo, GPL-3.0-or-later,
// original by coderunknow; copyright headers of the parent file apply).
// The Pair machinery below is copied VERBATIM from mega_correctness.cpp so
// the gate shares byte-identical engine<->oracle consumer semantics with the
// massive 3-way differential harness.
//
// The gate is the "separate correctness from latency" guard: latency numbers
// from bench_perf / e2e_bench / bench_tone_latency are only trustworthy when
// this binary exits 0. It runs deterministic workloads only — no timing, no
// OS nondeterminism:
//
//   S1 exact-composed  : authored lowercase Vietnamese corpus (precomposed),
//                        Telex+VNI. Typed through keygen keystrokes into a
//                        real TextEngine; engine text and oracle text must
//                        both equal the composed source + trailing space.
//   S2 viet74k         : every tests/data/viet74k.txt line the keygen can
//                        map, Telex+VNI, typed as one long deterministic
//                        session; engine and oracle must match on every
//                        event (checksum) with exact compare on any signal.
//   S3 macros          : 8 deterministic macro shapes × 8 abbreviations;
//                        engine==oracle per event; every ReplaceMacro must
//                        carry a non-empty expansion (D3 payload applied).
//   S4 tone populations: the frozen bench_tone_latency expectations (á, đ,
//                        ấ, asa, oasa, đặng, ướ, được) reproduced engine-only
//                        under the tone bench's options+lexicon (main.cpp
//                        consumer contract).
//
// Usage: gate [--max-words=N]   (N caps S2; default 0 = all 73,901 lines)
// Exit:  0 PASS, 1 FAIL (any mismatch/stale/expectation drift/macro gap).
//============================================================================
// SPDX-License-Identifier: GPL-3.0-or-later
#include "keygen.hpp"
#include "TextEngine.hpp"
#include "vi_oracle.hpp"
#ifndef NO_205
#include "engine205.hpp"
#endif

#include <csignal>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace ok::text;

namespace {

//============================================================================
// Encoding helpers
//============================================================================
std::string toUtf8(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) {
        char32_t cp = static_cast<char32_t>(c);
        if (cp < 0x80) s += static_cast<char>(cp);
        else if (cp < 0x800) {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
 else {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return s;
}

// Escape control chars in op-stream samples so the report stays readable.
std::string escapeOps(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\n': r += "\\n"; break;
            case '\b': r += "\\b"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default:    r += c;       break;
        }
    }
    return r;
}

std::string utf16Units(const std::wstring& w) {
    std::ostringstream o;
    for (wchar_t c : w) {
        o << "U+" << std::hex << static_cast<unsigned>(c) << " ";
    }
    return o.str();
}

std::string codepoints(const std::wstring& w) {
    std::ostringstream o;
    o << "CP[";
    for (std::size_t i = 0; i < w.size(); ++i) {
        char32_t cp = static_cast<char32_t>(w[i]);
        o << "U+" << std::hex << static_cast<unsigned>(cp) << " ";
    }
    o << "]";
    return o.str();
}

std::string firstDivergence(const std::wstring& a, const std::wstring& b) {
    std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    std::ostringstream o;
    o << "pos=" << i << " engine=";
    if (i < a.size()) { o << toUtf8(std::wstring(1, a[i])) << " " << utf16Units(std::wstring(1, a[i])); }
    else o << "<end>";
    o << " oracle=";
    if (i < b.size()) { o << toUtf8(std::wstring(1, b[i])) << " " << utf16Units(std::wstring(1, b[i])); }
    else o << "<end>";
    return o.str();
}

//============================================================================
// Options bridge engine <-> oracle
//============================================================================
EngineOptions toEngine(const orel::Options& o) {
    EngineOptions e;
    e.inputMethod = static_cast<InputMethod>(static_cast<int>(o.method));
    e.codeTable = static_cast<CodeTable>(static_cast<int>(o.table));
    // v1.1.2-r3: the oracle models the LEGACY 2.0.5 engine (digits compose
    // in VNI) — pin the flag explicitly. The library default flipped to
    // digits-literal in r3; without this pin every VNI differential case
    // containing digits would diverge for the wrong reason.
    e.digitsAreLiteral = false;
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
    return e;
}

#ifndef NO_205
ok205::Options to205(const orel::Options& o) {
    ok205::Options e;
    e.method = static_cast<ok205::Method>(static_cast<int>(o.method));
    e.table = static_cast<int>(o.table) <= 2 ? static_cast<int>(o.table) : 0;
    e.checkSpelling = o.checkSpelling;
    e.modernOrthography = o.modernOrthography;
    e.quickTelex = o.quickTelex;
    e.restoreIfWrongSpelling = o.restoreIfWrongSpelling;
    e.freeMark = o.freeMark;
    e.quickStartConsonant = o.quickStartConsonant;
    e.quickEndConsonant = o.quickEndConsonant;
    e.upperCaseFirstChar = o.upperCaseFirstChar;
    e.useMacro = o.useMacro;
    e.useMacroInEnglishMode = o.useMacroInEnglishMode;
    e.allowConsonantZFWJ = o.allowConsonantZfwj;
    return e;
}
#endif

//============================================================================
// Shared macro table (deterministic). Key = typed raw characters.
//============================================================================
struct MacroDef { std::string key; std::wstring expansion; };
std::vector<MacroDef> gMacros = {
    {"ok",     L"\u0111\u01B0\u1EE3c"},        // ok -> được
    {"vcl",    L"v\u00E3i"},                    // vcl -> vãi
    {"bt",     L"b\u00ECnh th\u01B0\u1EDDng"},  // bt -> bình thường
    {"xl",     L"xin l\u1ED7i"},                // xl -> xin lỗi
    {"abc",    L"a b c"},
    {"uong",   L"u\u1ED1ng"},                   // uong -> uống
    {"sd",     L"smile d\u00E0i d\u00F2ng v\u0103n b\u1EA3n"},
    {"telex",  L"b\u1ED9 g\u00F5 ti\u1EBFng vi\u1EC7t"},
};

// Side-channel for ReplaceMacro: the engine (and oracle) clear their macro-key
// accumulator before returning ReplaceMacro and expose no expansion data in the
// result, so the consumer cannot re-resolve the macro afterwards. The resolver
// (harness-owned, called by findMacro at match time) therefore remembers the
// last successfully matched key here; the Pair runner reads it when applying a
// ReplaceMacro event.
std::vector<std::uint32_t> gLastMacroKey;

std::string longMacroKey() {
    std::string s;
    for (int i = 0; i < 300; ++i) s += "ab";
    return s;
}

bool macroLookup(const std::vector<std::uint32_t>& key, std::vector<std::uint32_t>& data) {
    // Mirror of legacy 2.0.5 macro matching: the engine (and oracle) accumulate
    // each key as `toUpperAscii(raw) | (caps ? kCapsMask : 0)`, i.e. the letter
    // is stored uppercase with a separate caps bit. Legacy stores the raw
    // keycode with CAPS_MASK when shifted, and findMacro() matches exactly
    // (auto-caps is off in the harness), so 'ok' fires but 'OK'/'Ok' do not.
    // Reconstruct the typed (case-adjusted) string: caps-bit set → uppercase,
    // else lowercase — then compare against the macro table.
    std::string raw;
    for (std::uint32_t v : key) {
        char32_t ch = static_cast<char32_t>(v & orel::kCharMask);
        if (ch >= 32 && ch < 127) {
            char c = static_cast<char>(ch);
            if (v & orel::kCapsMask) {
                if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            } else {
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            }
            raw += c;
        } else return false;
    }
    for (const auto& m : gMacros) {
        if (raw == m.key) {
            data.clear();
            for (wchar_t c : m.expansion) data.push_back(static_cast<std::uint32_t>(c));
            gLastMacroKey = key;
            return true;
        }
    }
    return false;
}

std::wstring expansionFor(const std::vector<std::uint32_t>& key) {
    std::vector<std::uint32_t> data;
    if (macroLookup(key, data)) {
        std::wstring out;
        for (std::uint32_t v : data) out += static_cast<wchar_t>(v);
        return out;
    }
    return L"";
}

// O(1) incremental text checksum (length + sum + sum-of-squares + xor).
// Used for the per-event equality gate; any check difference triggers a full
// exact UTF-16 comparison to confirm and capture the first divergence.
// Collisions across four independent invariants are negligible; the confirm
// step guarantees zero false positives in the report.
struct TextCheck {
    std::uint64_t len = 0, sum = 0, sum2 = 0, xr = 0;
    void push(wchar_t c) {
        const std::uint64_t u = static_cast<std::uint64_t>(static_cast<char32_t>(c));
        ++len; sum += u; sum2 += u * u; xr ^= u;
    }
    void pop(wchar_t c) {
        const std::uint64_t u = static_cast<std::uint64_t>(static_cast<char32_t>(c));
        --len; sum -= u; sum2 -= u * u; xr ^= u;
    }
    bool equals(const TextCheck& o) const {
        return len == o.len && sum == o.sum && sum2 == o.sum2 && xr == o.xr;
    }
};

//============================================================================
// Pair: feeds identical events to engine + oracle, maintains both texts.
//============================================================================
struct Pair {
    TextEngine  eng;
    orel::Oracle ora;
    std::wstring et, ot;
    TextCheck ec, oc;
    std::uint64_t events = 0;
    std::uint64_t overBackspace = 0;     // engine requested more bs than text length
    std::uint64_t replaceMacro = 0;      // ReplaceMacro events (known consumer gap)
    std::uint64_t replaceMacroGap = 0;   // ReplaceMacro events whose expansion was missing

    explicit Pair(const orel::Options& o) : eng(toEngine(o)), ora(o) {
        eng.setMacroResolver(macroLookup);
        ora.setMacroResolver(macroLookup);
    }

    static char32_t producedChar(char32_t raw, bool caps) {
        if (caps && raw >= U'a' && raw <= U'z') return static_cast<char32_t>(raw - 32);
        return raw;
    }

    void eEraseTail(std::size_t n) {
        n = std::min<std::size_t>(n, et.size());
        for (std::size_t i = 0; i < n; ++i) { ec.pop(et.back()); et.pop_back(); }
    }
    void eAppend(const std::wstring& s) { for (wchar_t c : s) { et += c; ec.push(c); } }
    void oEraseTail(std::size_t n) {
        n = std::min<std::size_t>(n, ot.size());
        for (std::size_t i = 0; i < n; ++i) { oc.pop(ot.back()); ot.pop_back(); }
    }
    void oAppend(const std::wstring& s) { for (wchar_t c : s) { ot += c; oc.push(c); } }

    bool checksEqual() const { return ec.equals(oc); }

    // D3: the expansion now rides IN the result (EngineResult::macroExpansion /
    // oracle Result::macroExpansion) — the consumer applies it directly; no
    // side-channel needed. An empty expansion on a ReplaceMacro result is a
    // consumer GAP (the shipped pre-v3.1 consumer ignored the payload
    // entirely — 462,627 gap events in the frozen baseline).
    static std::wstring expansionOf(const std::vector<std::uint32_t>& v) {
        std::wstring s;
        s.reserve(v.size());
        for (std::uint32_t c : v) { s += static_cast<wchar_t>(c); }
        return s;
    }

    // Returns true if the engine's underlying history buffer would overflow
    // (stale-history defect) — the caller must abandon the case then.
    bool feedChar(char32_t raw, bool caps, bool ctrl) {
        ++events;
        {
            TextInput in; in.kind = InputKind::Char; in.ch = raw; in.isCaps = caps; in.otherCtrl = ctrl;
            const EngineResult& r = eng.process(in);
            if (r.code == EngineCode::ReplaceMacro) {
                ++replaceMacro;
                const std::wstring exp = expansionOf(r.macroExpansion);
                if (exp.empty()) { ++replaceMacroGap; }
                eEraseTail(r.backspaceCount);
                eAppend(exp);
            } else if (r.consumed()) {
                const std::wstring rep = eng.replacementUtf16(r);
                if (static_cast<std::size_t>(r.backspaceCount) > et.size()) ++overBackspace;
                eEraseTail(r.backspaceCount);
                eAppend(rep);
                if (r.code == EngineCode::Restore || r.code == EngineCode::RestoreAndStartNewSession) {
                    // Legacy-hook consumer semantics: after a Restore the hook
                    // re-sends the typed key to the app (OpenKey.mm / win32
                    // OpenKey.cpp SendKeyCode(_keycode|CAPS_MASK)). This is what
                    // turns 'cass' into 'cas' (mark toggled off + 's' typed).
                    eAppend(std::wstring(1, static_cast<wchar_t>(producedChar(raw, caps))));
                }
            } else {
                eAppend(std::wstring(1, static_cast<wchar_t>(producedChar(raw, caps))));
            }
        }
        {
            orel::Event ev; ev.kind = orel::Kind::Char; ev.ch = raw; ev.caps = caps; ev.ctrl = ctrl;
            const orel::Result& r = ora.process(ev);
            if (r.code == orel::Code::ReplaceMacro) {
                const std::wstring exp = expansionOf(r.macroExpansion);
                oEraseTail(r.backspaceCount);
                oAppend(exp);
            } else if (r.consumed()) {
                if (static_cast<std::size_t>(r.backspaceCount) > ot.size()) ++overBackspace;
                oEraseTail(r.backspaceCount);
                oAppend(r.replacement);
                if (r.code == orel::Code::Restore || r.code == orel::Code::RestoreAndStartNewSession) {
                    // same re-send semantics as the engine block above
                    oAppend(std::wstring(1, static_cast<wchar_t>(producedChar(raw, caps))));
                }
            } else {
                oAppend(std::wstring(1, static_cast<wchar_t>(producedChar(raw, caps))));
            }
        }
        return ora.staleHistorySize() >= 32 || ora.overflowDetected();
    }

    bool space() {
        ++events;
        {
            TextInput in; in.kind = InputKind::Space;
            const EngineResult& r = eng.process(in);
            if (r.code == EngineCode::ReplaceMacro) {
                ++replaceMacro;
                const std::wstring exp = expansionOf(r.macroExpansion);
                if (exp.empty()) { ++replaceMacroGap; }
                eEraseTail(r.backspaceCount);
                eAppend(exp);
            } else if (r.consumed()) {
                // Real-hook consumer semantics (win32 OpenKey.cpp / macOS
                // OpenKey.mm): a non-DoNothing result CONSUMES the space key —
                // backspaceCount chars are deleted and replacementUtf16()
                // inserted (the reverted plain word).
                // D4 fix (v3.1 consumer contract): the space is RE-ISSUED
                // after the restore — exactly like the typed-character
                // re-issue after a Char-Restore (hotfix §3). Before this
                // fix, a wrong-spelling word that triggered Restore on the
                // space ate the space: 'arbit hối đoái' rendered
                // 'arbithối đoái' (3-way benchmark finding, defect D4).
                if (static_cast<std::size_t>(r.backspaceCount) > et.size()) ++overBackspace;
                eEraseTail(r.backspaceCount);
                eAppend(eng.replacementUtf16(r));
                if (r.code == EngineCode::Restore || r.code == EngineCode::RestoreAndStartNewSession) {
                    eAppend(L" ");
                }
            } else {
                eAppend(L" ");
            }
        }
        {
            orel::Event ev; ev.kind = orel::Kind::Space;
            const orel::Result& r = ora.process(ev);
            if (r.code == orel::Code::ReplaceMacro) {
                const std::wstring exp = expansionOf(r.macroExpansion);
                oEraseTail(r.backspaceCount);
                oAppend(exp);
            } else if (r.consumed()) {
                if (static_cast<std::size_t>(r.backspaceCount) > ot.size()) ++overBackspace;
                oEraseTail(r.backspaceCount);
                oAppend(r.replacement);
                if (r.code == orel::Code::Restore || r.code == orel::Code::RestoreAndStartNewSession) {
                    oAppend(L" ");   // D4 fix — mirror of the engine block above
                }
            } else {
                oAppend(L" ");
            }
        }
        return ora.staleHistorySize() >= 32 || ora.overflowDetected();
    }

    bool backspace() {
        ++events;
        {
            TextInput in; in.kind = InputKind::Backspace;
            static_cast<void>(eng.process(in));
            if (!et.empty()) { ec.pop(et.back()); et.pop_back(); }
        }
        {
            orel::Event ev; ev.kind = orel::Kind::Backspace;
            static_cast<void>(ora.process(ev));
            if (!ot.empty()) { oc.pop(ot.back()); ot.pop_back(); }
        }
        return ora.staleHistorySize() >= 32 || ora.overflowDetected();
    }

    bool wordBreak(std::uint16_t vk) {
        ++events;
        {
            TextInput in; in.kind = InputKind::WordBreak; in.vkCode = vk;
            const EngineResult& r = eng.process(in);
            if (r.code == EngineCode::ReplaceMacro) {
                ++replaceMacro;
                const std::wstring exp = expansionOf(r.macroExpansion);
                if (exp.empty()) { ++replaceMacroGap; }
                eEraseTail(r.backspaceCount);
                eAppend(exp);
            } else if (vk == 0x0D) {
                eAppend(L"\n");
            }
        }
        {
            orel::Event ev; ev.kind = orel::Kind::WordBreak; ev.vk = vk;
            const orel::Result& r = ora.process(ev);
            if (r.code == orel::Code::ReplaceMacro) {
                const std::wstring exp = expansionOf(r.macroExpansion);
                oEraseTail(r.backspaceCount);
                oAppend(exp);
            } else if (vk == 0x0D) {
                oAppend(L"\n");
            }
        }
        return ora.staleHistorySize() >= 32 || ora.overflowDetected();
    }

    // Real-app model for shifted symbols: the engine treats the physical
    // shift+key as a WORD BREAK (clears the pending composition) and the
    // application inserts the symbol character itself. Mirrors what the
    // legacy hook delivers for '(' / ')' / '!' / ... (shift+digit etc.).
    bool symbolBreak(wchar_t c) {
        ++events;
        {
            TextInput in; in.kind = InputKind::WordBreak; in.vkCode = 0x09;   // Tab = break, no text
            const EngineResult& r = eng.process(in);
            if (r.code == EngineCode::ReplaceMacro) {
                ++replaceMacro;
                const std::wstring exp = expansionOf(r.macroExpansion);
                if (exp.empty()) { ++replaceMacroGap; }
                eEraseTail(r.backspaceCount);
                eAppend(exp);
            }
            eAppend(std::wstring(1, c));
        }
        {
            orel::Event ev; ev.kind = orel::Kind::WordBreak; ev.vk = 0x09;
            const orel::Result& r = ora.process(ev);
            if (r.code == orel::Code::ReplaceMacro) {
                const std::wstring exp = expansionOf(r.macroExpansion);
                oEraseTail(r.backspaceCount);
                oAppend(exp);
            }
            oAppend(std::wstring(1, c));
        }
        return ora.staleHistorySize() >= 32 || ora.overflowDetected();
    }

    bool mouseDown() {
        ++events;
        {
            TextInput in; in.kind = InputKind::MouseDown;
            static_cast<void>(eng.process(in));
        }
        {
            orel::Event ev; ev.kind = orel::Kind::MouseDown;
            static_cast<void>(ora.process(ev));
        }
        return ora.staleHistorySize() >= 32 || ora.overflowDetected();
    }

    void setMode(const orel::Options& o) {
        ++events;
        eng.setOptions(toEngine(o));
        ora.setOptions(o);
    }

    bool textsEqual() const { return et == ot; }
};

//============================================================================
// Suite statistics + failure capture
//============================================================================
struct Stats {
    std::string name;
    std::uint64_t cases = 0;
    std::uint64_t events = 0;
    std::uint64_t textMismatches = 0;
    std::uint64_t staleDefects = 0;      // engine history-overflow pre-empts
    std::uint64_t overBackspaceEvents = 0;
    std::uint64_t replaceMacroEvents = 0;
    std::uint64_t replaceMacroGapEvents = 0;  // ReplaceMacro without in-result expansion (D3 gap)
    std::uint64_t undecodable = 0;       // suite 2: targets the scheme cannot type (unmappable+collision); suite 8: typed chars
    std::uint64_t unmappable = 0;        // suite 2: targets containing chars outside the Telex alphabet
    std::uint64_t collision = 0;         // suite 2: targets whose generated keys re-type to a different valid string
    std::set<std::string> signatures;
    std::vector<std::string> reproFiles;
};

struct CaseRecord {
    std::string suite;
    std::string meta;                    // seed / word / mode info
    std::vector<std::string> ops;        // op descriptions (for the repro file)
    std::wstring engineText, oracleText;
    std::string divergence;              // firstDivergence() string
};

// sigsetjmp safety net for unexpected engine crashes (should never fire;
// the oracle mirror pre-empts the known overflow before the engine faults).
sigjmp_buf g_jmp;
volatile sig_atomic_t g_crashed = 0;
void onCrash(int) { g_crashed = 1; siglongjmp(g_jmp, 1); }

void installCrashHandler() {
    std::signal(SIGSEGV, onCrash);
    std::signal(SIGABRT, onCrash);
    std::signal(SIGBUS, onCrash);
    std::signal(SIGFPE, onCrash);
}

std::string opSignature(const std::vector<std::string>& ops) {
    std::uint64_t h = 1469598103934665603ull;
    for (const auto& s : ops) {
        for (char c : s) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ull; }
        h ^= 0xFFu; h *= 1099511628211ull;
    }
    std::ostringstream o;
    o << std::hex << h;
    return o.str();
}

std::string opSignature(const std::string& ops) {
    std::uint64_t h = 1469598103934665603ull;
    for (char c : ops) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ull; }
    std::ostringstream o;
    o << std::hex << h;
    return o.str();
}

//============================================================================
// Word corpus (real Vietnamese words) + Telex decoding of precomposed text
//============================================================================
std::vector<std::wstring> loadWords(const char* path) {
    std::vector<std::wstring> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::wstring w;
        bool ok = true;
        for (std::size_t i = 0; i < line.size();) {
            unsigned char c = static_cast<unsigned char>(line[i]);
            char32_t cp = 0;
            int n = 0;
            if (c < 0x80) { cp = c; n = 1; }
            else if ((c >> 5) == 0x6) { cp = c & 0x1F; n = 2; }
            else if ((c >> 4) == 0xE) { cp = c & 0x0F; n = 3; }
            else { ok = false; break; }
            for (int j = 1; j < n; ++j) {
                if (i + j >= line.size()) { ok = false; break; }
                cp = (cp << 6) | (static_cast<unsigned char>(line[i + j]) & 0x3F);
            }
            if (!ok) break;
            if (cp > 0xFFFF) { ok = false; break; }
            w += static_cast<wchar_t>(cp);
            i += n;
        }
        if (ok) out.push_back(w);
    }
    return out;
}

// Map one precomposed Vietnamese character to its raw Telex key sequence.
// Handles every base vowel, every tone, and đ. Returns false if undecodable.
bool charToTelex(wchar_t c, std::string& keys) {
    switch (c) {
        case L'a': keys += 'a'; return true;
        case L'\u00E0': keys += "af"; return true;      // à
        case L'\u00E1': keys += "as"; return true;      // á
        case L'\u1EA3': keys += "ar"; return true;      // ả
        case L'\u00E3': keys += "ax"; return true;      // ã
        case L'\u1EA1': keys += "aj"; return true;      // ạ
        case L'\u0103': keys += "aw"; return true;      // ă
        case L'\u1EB1': keys += "awf"; return true;     // ằ
        case L'\u1EAF': keys += "aws"; return true;     // ắ
        case L'\u1EB3': keys += "awr"; return true;     // ẳ
        case L'\u1EB5': keys += "awx"; return true;     // ẵ
        case L'\u1EB7': keys += "awj"; return true;     // ặ
        case L'\u00E2': keys += "aa"; return true;      // â
        case L'\u1EA7': keys += "aaf"; return true;     // ầ
        case L'\u1EA5': keys += "aas"; return true;     // ấ
        case L'\u1EA9': keys += "aar"; return true;     // ẩ
        case L'\u1EAB': keys += "aax"; return true;     // ẫ
        case L'\u1EAD': keys += "aaj"; return true;     // ậ
        case L'e': keys += 'e'; return true;
        case L'\u00E8': keys += "ef"; return true;      // è
        case L'\u00E9': keys += "es"; return true;      // é
        case L'\u1EBB': keys += "er"; return true;      // ẻ
        case L'\u1EBD': keys += "ex"; return true;      // ẽ
        case L'\u1EB9': keys += "ej"; return true;      // ẹ
        case L'\u00EA': keys += "ee"; return true;      // ê
        case L'\u1EC1': keys += "eef"; return true;     // ề
        case L'\u1EBF': keys += "ees"; return true;     // ế
        case L'\u1EC3': keys += "eer"; return true;     // ể
        case L'\u1EC5': keys += "eex"; return true;     // ễ
        case L'\u1EC7': keys += "eej"; return true;     // ệ
        case L'i': keys += 'i'; return true;
        case L'\u00EC': keys += "if"; return true;      // ì
        case L'\u00ED': keys += "is"; return true;      // í
        case L'\u1EC9': keys += "ir"; return true;      // ỉ
        case L'\u0129': keys += "ix"; return true;      // ĩ
        case L'\u1ECB': keys += "ij"; return true;      // ị
        case L'o': keys += 'o'; return true;
        case L'\u00F2': keys += "of"; return true;      // ò
        case L'\u00F3': keys += "os"; return true;      // ó
        case L'\u1ECF': keys += "or"; return true;      // ỏ
        case L'\u00F5': keys += "ox"; return true;      // õ
        case L'\u1ECD': keys += "oj"; return true;      // ọ
        case L'\u00F4': keys += "oo"; return true;      // ô
        case L'\u1ED3': keys += "oof"; return true;     // ồ
        case L'\u1ED1': keys += "oos"; return true;     // ố
        case L'\u1ED5': keys += "oor"; return true;     // ổ
        case L'\u1ED7': keys += "oox"; return true;     // ỗ
        case L'\u1ED9': keys += "ooj"; return true;     // ộ
        case L'\u01A1': keys += "ow"; return true;      // ơ
        case L'\u1EDD': keys += "owf"; return true;     // ờ
        case L'\u1EDB': keys += "ows"; return true;     // ớ
        case L'\u1EDF': keys += "owr"; return true;     // ở
        case L'\u1EE1': keys += "owx"; return true;     // ỡ
        case L'\u1EE3': keys += "owj"; return true;     // ợ
        case L'u': keys += 'u'; return true;
        case L'\u00F9': keys += "uf"; return true;      // ù
        case L'\u00FA': keys += "us"; return true;      // ú
        case L'\u1EE7': keys += "ur"; return true;      // ủ
        case L'\u0169': keys += "ux"; return true;      // ũ
        case L'\u1EE5': keys += "uj"; return true;      // ụ
        case L'\u01B0': keys += "uw"; return true;      // ư
        case L'\u1EEB': keys += "uwf"; return true;     // ừ
        case L'\u1EE9': keys += "uws"; return true;     // ứ
        case L'\u1EED': keys += "uwr"; return true;     // ử
        case L'\u1EEF': keys += "uwx"; return true;     // ữ
        case L'\u1EF1': keys += "uwj"; return true;     // ự
        case L'y': keys += 'y'; return true;
        case L'\u1EF3': keys += "yf"; return true;      // ỳ
        case L'\u00FD': keys += "ys"; return true;      // ý
        case L'\u1EF7': keys += "yr"; return true;      // ỷ
        case L'\u1EF9': keys += "yx"; return true;      // ỹ
        case L'\u1EF5': keys += "yj"; return true;      // ỵ
        case L'\u0111': keys += "dd"; return true;      // đ
        default: {
            if ((c >= L'a' && c <= L'z')) { keys += static_cast<char>(c); return true; }
            if (c >= L'0' && c <= L'9') { keys += static_cast<char>(c); return true; }
            if (c == L'-' || c == L'\'') { keys += static_cast<char>(c); return true; }
            return false;
        }
    }
}

// Convert a precomposed word (possibly containing spaces/hyphens) to raw
// Telex keys. Returns false if any character is undecodable.
bool wordToTelexKeys(const std::wstring& word, std::string& keys) {
    std::string k;
    for (wchar_t c : word) {
        if (c == L' ') { k += ' '; continue; }
        if (!charToTelex(c, k)) return false;
    }
    keys = k;
    return !k.empty();
}

} // namespace

//============================================================================
// Gate tail — suites S1..S4 + main (added on top of the verbatim slice).
//============================================================================
namespace gate {

struct GateStats {
    const char* name = "";
    std::uint64_t cases = 0, events = 0, mismatches = 0, stale = 0;
    std::uint64_t overBs = 0, replaceMacro = 0, macroGap = 0, skipped = 0;
    std::uint64_t expectDrift = 0, printed = 0;
    void note(const char* kind, const std::string& msg) {
        if (printed < 8) { std::printf("  %s %s: %s\n", kind, name, msg.c_str()); ++printed; }
    }
};

orel::Options baseOptions(bool vni) {
    orel::Options o;
    o.method = vni ? orel::Method::Vni : orel::Method::Telex;
    o.table = orel::CodeTable::Unicode;
    o.checkSpelling = true;
    o.modernOrthography = false;
    o.quickTelex = false;
    o.restoreIfWrongSpelling = true;
    o.freeMark = false;
    o.allowConsonantZfwj = false;
    o.quickStartConsonant = false;
    o.quickEndConsonant = false;
    o.upperCaseFirstChar = false;
    o.useMacro = true;
    o.useMacroInEnglishMode = false;
    o.useDictionaryRestore = false;
    return o;
}

//---------------------------------------------------------------------------
// S1 — exact composed round-trip (authored corpus, both methods).
// One long session per method; engine text and oracle text must equal the
// accumulated composed source at every word boundary.
//---------------------------------------------------------------------------
const wchar_t* kCorpus[] = {
    L"xin",
    L"ch\u00E0o",
    L"c\u00E1c",
    L"b\u1EA1n",
    L"t\u00F4i",
    L"l\u00E0",
    L"ng\u01B0\u1EDDi",
    L"h\u00F4m",
    L"nay",
    L"tr\u1EDDi",
    L"\u0111\u1EB9p",
    L"qu\u00E1",
    L"ch\u00FAng",
    L"\u0111i",
    L"d\u1EA1o",
    L"quanh",
    L"h\u1ED3",
    L"r\u1ED3i",
    L"ng\u1ED3i",
    L"u\u1ED1ng",
    L"c\u00E0",
    L"ph\u00EA",
    L"v\u00E0",
    L"n\u00F3i",
    L"chuy\u1EC7n",
    L"v\u1EC1",
    L"d\u1EF1",
    L"\u00E1n",
    L"m\u1EDBi",
    L"ti\u1EBFng",
    L"c\u00F3",
    L"nhi\u1EC1u",
    L"d\u1EA5u",
    L"thanh",
    L"s\u1EAFc",
    L"huy\u1EC1n",
    L"h\u1ECFi",
    L"ng\u00E3",
    L"n\u1EB7ng",
    L"ng\u01B0\u1EDDi",
    L"d\u00F9ng",
    L"g\u00F5",
    L"nhanh",
    L"th\u00EC",
    L"\u0111\u1ED9",
    L"tr\u1EC5",
    L"ph\u1EA3i",
    L"nh\u1ECF",
    L"h\u01A1n",
    L"hai",
    L"ph\u1EA7n",
    L"tr\u0103m",
    L"micro",
    L"gi\u00E2y",
    L"h\u1EC7",
    L"th\u1ED1ng",
    L"x\u1EED",
    L"l\u00FD",
    L"ph\u00EDm",
    L"b\u1EB1ng",
    L"h\u00E0ng",
    L"\u0111\u1EE3i",
    L"kh\u00F3a",
    L"t\u1EF1",
    L"do",
    L"kh\u00F4ng",
    L"kh\u1EDFi",
    L"t\u1EA1o",
    L"b\u1ED9",
    L"nh\u1EDB",
    L"\u0111\u1ED9ng",
    L"\u0111\u01B0\u1EE3c",
    L"\u0111ang",
    L"\u01B0\u1EDBc",
    L"mu\u1ED1n",
    L"th\u01B0\u01A1ng",
    L"nh\u1EDB",
    L"mong",
    L"ng\u00F3ng"
};

bool feedKeys(Pair& p, const std::string& keys) {
    const bool diag = std::getenv("GATE_DIAG") != nullptr;
    for (char k : keys) {
        bool stop;
        if (k == ' ') stop = p.space();
        else stop = p.feedChar(static_cast<char32_t>(static_cast<unsigned char>(k)), false, false);
        if (diag) std::printf("    ev '%c' stop=%d eq=%d et=[%s] ot=[%s]\n", k, (int)stop, (int)p.checksEqual(), toUtf8(p.et).c_str(), toUtf8(p.ot).c_str());
        if (stop) return false;
        if (!p.checksEqual()) return false;
    }
    return true;
}

struct WordOutcome {
    bool ok = true;
    bool stale = false;
    std::uint64_t events = 0, overBs = 0, replaceMacro = 0, macroGap = 0;
};

// One word in its own Pair (mega suite-2 semantics: fresh Pair per case so
// oracle stale-history accumulation cannot cross word boundaries).
WordOutcome runWordCase(const orel::Options& o, const std::wstring& w, int method,
                        const std::wstring* expectAfter = nullptr) {
    WordOutcome r;
    std::string keys;
    if (!keygen::wordToKeys(w, keys, method)) { return r; }
    Pair p(o);
    if (!feedKeys(p, keys)) {
        r.ok = false;
        r.stale = p.ora.staleHistorySize() >= 32 || p.ora.overflowDetected();
        r.events = p.events; r.overBs = p.overBackspace; r.replaceMacro = p.replaceMacro; r.macroGap = p.replaceMacroGap;
        return r;
    }
    if (p.space()) {
        r.ok = false; r.stale = true;
        r.events = p.events; r.overBs = p.overBackspace; r.replaceMacro = p.replaceMacro; r.macroGap = p.replaceMacroGap;
        return r;
    }
    if (!p.checksEqual()) { r.ok = false; }
    else if (expectAfter && (p.et != *expectAfter || p.ot != *expectAfter)) { r.ok = false; }
    r.events = p.events; r.overBs = p.overBackspace; r.replaceMacro = p.replaceMacro; r.macroGap = p.replaceMacroGap;
    return r;
}

void runS1(GateStats& g) {
    for (int m = 0; m < 2; ++m) {
        orel::Options o = baseOptions(m == 1);
        o.useMacro = false;
        for (const wchar_t* cw : kCorpus) {
            std::wstring w(cw);
            const std::wstring expect = w + L' ';
            WordOutcome r = runWordCase(o, w, m, &expect);
            ++g.cases;
            if (!r.ok && r.stale) ++g.stale;
            else if (!r.ok) { ++g.mismatches; g.note("MISMATCH", "word " + toUtf8(w)); }
            g.events += r.events; g.overBs += r.overBs; g.replaceMacro += r.replaceMacro; g.macroGap += r.macroGap;
        }
    }
}

//---------------------------------------------------------------------------
// S2 — viet74k differential (both methods, long deterministic session).
//---------------------------------------------------------------------------
void runS2(GateStats& g, const std::vector<std::wstring>& words, std::size_t maxWords) {
    const std::size_t n = maxWords == 0 ? words.size() : std::min(maxWords, words.size());
    for (int m = 0; m < 2; ++m) {
        orel::Options o = baseOptions(m == 1);
        for (std::size_t i = 0; i < n; ++i) {
            std::string keys;
            if (!keygen::wordToKeys(words[i], keys, m)) { ++g.skipped; continue; }
            ++g.cases;
            WordOutcome r = runWordCase(o, words[i], m);
            if (!r.ok && r.stale) ++g.stale;
            else if (!r.ok) { ++g.mismatches; g.note("MISMATCH", "line " + std::to_string(i) + " word=" + toUtf8(words[i])); }
            g.events += r.events; g.overBs += r.overBs; g.replaceMacro += r.replaceMacro; g.macroGap += r.macroGap;
        }
    }
}

//---------------------------------------------------------------------------
// S3 — macro differential (deterministic shapes from mega suite 10).
//---------------------------------------------------------------------------
void runS3(GateStats& g) {
    const char* keys8[] = {"ok", "vcl", "bt", "xl", "abc", "uong", "sd", "telex"};
    const std::uint64_t n = 4608;   // 8 macros x 9 patterns x 64 seeds
    orel::Options o = baseOptions(false);
    for (std::uint64_t k = 0; k < n; ++k) {
        std::mt19937_64 rng(0x13198A2E03707344ull ^ (k * 0x100000001B3ull));
        const char* m = keys8[(k / 9) % 8];
        const std::size_t mlen = std::strlen(m);
        Pair p(o);
        ++g.cases;
        bool dead = false;
        auto feed = [&](char c) { if (dead) return; if (p.feedChar(static_cast<char32_t>(static_cast<unsigned char>(c)), false, false)) { ++g.stale; dead = true; } else if (!p.checksEqual()) { ++g.mismatches; g.note("MISMATCH", "macro case " + std::to_string(k)); dead = true; } };
        auto sp = [&]() { if (dead) return; if (p.space()) { ++g.stale; dead = true; } else if (!p.checksEqual()) { ++g.mismatches; g.note("MISMATCH", "macro case(space) " + std::to_string(k)); dead = true; } };
        auto bs = [&]() { if (dead) return; if (p.backspace()) { ++g.stale; dead = true; } else if (!p.checksEqual()) { ++g.mismatches; g.note("MISMATCH", "macro case(bs) " + std::to_string(k)); dead = true; } };
        auto ent = [&]() { if (dead) return; if (p.wordBreak(0x0D)) { ++g.stale; dead = true; } else if (!p.checksEqual()) { ++g.mismatches; g.note("MISMATCH", "macro case(enter) " + std::to_string(k)); dead = true; } };
        switch (k % 9) {
            case 0: for (std::size_t i = 0; i < mlen && !dead; ++i) feed(m[i]); sp(); break;
            case 1: { const std::size_t half = mlen > 1 ? mlen / 2 : 1; for (std::size_t i = 0; i < half && !dead; ++i) feed(m[i]); ent(); break; }
            case 2: for (std::size_t i = 0; i < mlen && !dead; ++i) feed(m[i]); feed("xy"[k % 2]); sp(); break;
            case 3: for (int i = 0; i < 3 + static_cast<int>(k % 5) && !dead; ++i) feed("qwerty"[rng() % 6]); sp(); break;
            case 4: { std::string lm; for (int i = 0; i < 300; ++i) lm += "ab"; for (std::size_t i = 0; i < lm.size() && !dead; ++i) feed(lm[i]); sp(); break; }
            case 5: for (int rep = 0; rep < 2 && !dead; ++rep) { for (std::size_t i = 0; i < mlen && !dead; ++i) feed(m[i]); sp(); } break;
            case 6: for (std::size_t i = 0; i < mlen && !dead; ++i) { feed(m[i]); if (i % 2 == 1 && !dead) bs(); } sp(); break;
            case 7: for (std::size_t i = 0; i < mlen && !dead; ++i) feed(m[i]); feed("sfrxj"[k % 5]); sp(); break;
            case 8: for (std::size_t i = 0; i < mlen && !dead; ++i) { feed(m[i]); if (i == 1 && !dead) { orel::Options oo = o; oo.method = static_cast<orel::Method>(rng() % 3); p.setMode(oo); } } sp(); break;
        }
        g.events += p.events;
        g.overBs += p.overBackspace;
        g.replaceMacro += p.replaceMacro;
        g.macroGap += p.replaceMacroGap;
    }
}

//---------------------------------------------------------------------------
// S4 — tone-population frozen expectations (bench_tone_latency lexicon).
// Engine-only, main.cpp consumer contract (compose model), identical to the
// tone bench's startup validation.
//---------------------------------------------------------------------------
void runS4(GateStats& g) {
    struct Pop { const char* name; const char* keys; const unsigned* expect; std::size_t len; };
    constexpr unsigned eAs[]   = {0x00E1, 0x0020};
    constexpr unsigned eDd[]   = {0x0111, 0x0020};
    constexpr unsigned eAsa[]  = {0x1EA5, 0x0020};
    constexpr unsigned eAsas[] = {0x0061, 0x0073, 0x0061, 0x0020};
    constexpr unsigned eOasas[]= {0x006F, 0x0061, 0x0073, 0x0061, 0x0020};
    constexpr unsigned eDdawjng[] = {0x0111, 0x1EB7, 0x006E, 0x0067, 0x0020};
    constexpr unsigned eUows[] = {0x01B0, 0x1EDB, 0x0020};
    constexpr unsigned eDduowcj[] = {0x0111, 0x01B0, 0x1EE3, 0x0063, 0x0020};
    const Pop pops[] = {
        {"tone-append", "as ", eAs, 2}, {"dbar", "dd ", eDd, 2}, {"reposition", "asa ", eAsa, 2},
        {"restore-reissue", "asas ", eAsas, 4}, {"double-tone", "oasas ", eOasas, 5},
        {"mid-word", "ddawjng ", eDdawjng, 5}, {"horn-compound", "uows ", eUows, 3},
        {"heavy-word", "dduowcj ", eDduowcj, 5},
    };
    EngineOptions o;
    o.restoreIfWrongSpelling = true;
    o.useDictionaryRestore = true;
    TextEngine eng(o);
    static const wchar_t* lex[] = {
        L"\xE1", L"\x111", L"\x1EA5", L"\xE2", L"\xF3\x61", L"\x6F\x1EA5", L"\x6F\xE2",
        L"\x111\x1EB7ng", L"\x1B0\x1EDB", L"\x111\x1B0\x1EE3\x63",
        L"h\xF4m", L"tr\xF2i", L"\x111\x1EB9p", L"ch\xFAng", L"t\xF4i", L"\x111\x1EA1o",
        L"h\x1ED3", L"T\xE2y", L"r\x1ED3i", L"ng\x1ED3i", L"ph\xEA", L"n\xF4i",
        L"vi\x1EC7t", L"nhi\x1EC1u", L"ng\x1B0\x1EDDi",
    };
    eng.setDictionaryResolver([](const std::vector<std::uint32_t>& w) {
        static const std::vector<std::wstring> words = [] {
            std::vector<std::wstring> v;
            for (const wchar_t* x : lex) v.push_back(std::wstring(x));
            return v;
        }();
        std::wstring s;
        for (std::uint32_t c : w) s += static_cast<wchar_t>(c);
        for (const auto& x : words) if (s == x) return true;
        return false;
    });
    std::wstring scratch;
    for (const Pop& p : pops) {
        ++g.cases;
        eng.startNewSession();
        std::vector<unsigned> out;
        for (const char* cp = p.keys; *cp; ++cp) {
            const char c = *cp;
            TextInput in;
            if (c == ' ') in.kind = InputKind::Space;
            else { in.kind = InputKind::Char; in.ch = static_cast<char32_t>(c); }
            const EngineResult& r = eng.process(in);
            scratch.clear();
            eng.replacementUtf16(r, scratch);
            const bool reissue = (r.code == EngineCode::Restore || r.code == EngineCode::RestoreAndStartNewSession);
            const bool suppress = (r.code == EngineCode::ReplaceMacro) ||
                                  (r.consumed() && !(r.backspaceCount == 0 && scratch.empty()));
            if (suppress) {
                out.resize(out.size() - std::min<std::size_t>(r.backspaceCount, out.size()));
                for (wchar_t wc : scratch) out.push_back(static_cast<unsigned>(static_cast<unsigned short>(wc)));
                if (reissue) out.push_back(c == ' ' ? 0x20u : static_cast<unsigned>(static_cast<unsigned char>(c)));
            } else {
                out.push_back(c == ' ' ? 0x20u : static_cast<unsigned>(static_cast<unsigned char>(c)));
            }
        }
        if (out.size() == p.len && std::equal(out.begin(), out.end(), p.expect)) continue;
        ++g.expectDrift;
        std::string got;
        for (unsigned u : out) { char b[16]; std::snprintf(b, sizeof b, " %04X", u); got += b; }
        g.note("DRIFT", std::string("pop=") + p.name + " got:" + got);
    }
}

} // namespace gate

int main(int argc, char** argv) {
    std::size_t maxWords = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--max-words" && i + 1 < argc) maxWords = static_cast<std::size_t>(std::atoll(argv[++i]));
        else { std::fprintf(stderr, "unknown arg %s\n", argv[i]); return 2; }
    }
    std::printf("[gate] KieeKey correctness gate (deterministic; no timing)\\n");
    gate::GateStats s1{"S1-exact-composed",}, s2{"S2-viet74k-diff",}, s3{"S3-macros",}, s4{"S4-tone-pops",};
    gate::runS1(s1);
    std::vector<std::wstring> words = loadWords("tests/data/viet74k.txt");
    std::printf("[gate] viet74k: %zu lines\\n", words.size());
    gate::runS2(s2, words, maxWords);
    gate::runS3(s3);
    gate::runS4(s4);
    const gate::GateStats* all[] = {&s1, &s2, &s3, &s4};
    std::uint64_t tCases = 0, tEvents = 0, tMism = 0, tStale = 0, tDrift = 0, tGap = 0, tSkipped = 0;
    std::printf("[gate] suite        cases      events  mismatch  stale  expectDrift macroGap skipped\\n");
    for (const auto* s : all) {
        std::printf("%-13s %8llu %10llu %8llu %6llu %11llu %8llu %7llu\\n",
                    s->name, (unsigned long long)s->cases, (unsigned long long)s->events,
                    (unsigned long long)s->mismatches, (unsigned long long)s->stale,
                    (unsigned long long)s->expectDrift, (unsigned long long)s->macroGap,
                    (unsigned long long)s->skipped);
        tCases += s->cases; tEvents += s->events; tMism += s->mismatches; tStale += s->stale;
        tDrift += s->expectDrift; tGap += s->macroGap; tSkipped += s->skipped;
    }
    std::printf("[gate] totals: cases=%llu events=%llu mismatches=%llu stale=%llu expectDrift=%llu macroGap=%llu skipped(unmappable)=%llu\\n",
                (unsigned long long)tCases, (unsigned long long)tEvents, (unsigned long long)tMism,
                (unsigned long long)tStale, (unsigned long long)tDrift, (unsigned long long)tGap,
                (unsigned long long)tSkipped);
    if (tMism == 0 && tStale == 0 && tDrift == 0 && tGap == 0) {
        std::printf("[gate] VERDICT: PASS\\n");
        return 0;
    }
    std::printf("[gate] VERDICT: FAIL\\n");
    return 1;
}
