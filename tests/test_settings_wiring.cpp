// test_settings_wiring.cpp — v1.3.0-beta5 (bug B9) effect-layer pins.
//
// The tester reported "many options in the Bàn phím tab don't work". The
// three-layer audit (scripts/audit_settings_wiring.py) pins the WIRING layer
// (every interactive control is created, read, reflected, live-applied and
// persisted); THIS test pins the EFFECT layer: for every tab-0 setting, an
// option flip must observably change what the text engine emits.
//
// Every sequence/expectation pair below was produced by running the frozen
// engine itself (probe-first ground rule) — they are regression pins, not
// guesses. Most tab-0 options are CONDITIONAL by design (digits only matter
// in VNI, quickTelex only on doubled consonants, upperCaseFirst only after
// ". ", modern orthography only on oa/oe/uy words), which is exactly why a
// casual "toggle and type normally" test run made them look dead; the pairs
// here are the minimal sequences that expose each effect.
//
// Build (mirrors tests/test_vn_composer):
//   g++ -std=c++2b -O2 -pthread -Isrc/core tests/test_settings_wiring.cpp \
//       src/core/TextEngine.cpp -o /tmp/test_settings_wiring

#include "TextEngine.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace ok::text;

namespace {

int g_fail = 0;
int g_pass = 0;

void check(bool cond, const std::string& what) {
    if (cond) { ++g_pass; }
    else { ++g_fail; std::cout << "  FAIL: " << what << "\n"; }
}

// The exact consumer semantics main.cpp/the benchmark harness use: apply
// backspaces, append the replacement, and re-append the raw char on Restore.
struct Session {
    TextEngine eng;
    std::wstring text;
    explicit Session(const EngineOptions& o) : eng(o) {}

    void consume(const EngineResult& r, char32_t c) {
        if (r.consumed()) {
            const std::size_t b = r.backspaceCount > text.size() ? text.size() : r.backspaceCount;
            text.erase(text.size() - b, b);
            text += eng.replacementUtf16(r);
            if (r.code == EngineCode::Restore ||
                r.code == EngineCode::RestoreAndStartNewSession) {
                text += static_cast<wchar_t>(c);
            }
        } else {
            text += static_cast<wchar_t>(c);
        }
    }
    void feed(char32_t c) {
        TextInput in;
        in.kind = InputKind::Char;
        in.ch = c;
        consume(eng.process(in), c);
    }
    void space() {
        TextInput in;
        in.kind = InputKind::Space;
        in.ch = U' ';
        consume(eng.process(in), U' ');
    }
    // '|' in the script means a real Space key event (InputKind::Space).
    void feedScript(const std::string& s) {
        for (char ch : s) {
            if (ch == '|') { space(); }
            else { feed(static_cast<char32_t>(static_cast<unsigned char>(ch))); }
        }
    }
};

std::wstring run(const EngineOptions& o, const std::string& script) {
    Session s(o);
    s.feedScript(script);
    return s.text;
}

// Render a wstring with non-ASCII code points escaped, for failure messages.
std::string show(const std::wstring& w) {
    std::string out;
    for (wchar_t c : w) {
        if (c < 128) { out += static_cast<char>(c); }
        else { out += '['; out += std::to_string(static_cast<unsigned>(c)); out += ']'; }
    }
    return out;
}

EngineOptions base() { return EngineOptions{}; }

struct Row {
    std::string name;
    EngineOptions opts;
    std::string script;
    std::wstring expect;
};

} // namespace

int main() {
    std::vector<Row> rows;

    // ---- input method radios (IDC_RADIO_TELEX / VNI / SIMPLETELEX) ----
    { Row r; r.name = "Telex composes 'vieejt' -> việt";
      r.opts = base(); r.opts.inputMethod = InputMethod::Telex;
      r.script = "vieejt"; r.expect = L"việt"; rows.push_back(r); }
    { Row r; r.name = "VNI composes 'vie65t' -> việt";
      r.opts = base(); r.opts.inputMethod = InputMethod::Vni; r.opts.digitsAreLiteral = false;
      r.script = "vie65t"; r.expect = L"việt"; rows.push_back(r); }
    { Row r; r.name = "Telex maps 'qw' -> qư";
      r.opts = base(); r.opts.inputMethod = InputMethod::Telex;
      r.script = "qw"; r.expect = L"q\u01B0"; rows.push_back(r); }
    { Row r; r.name = "SimpleTelex leaves 'qw' raw (method radio changes behavior)";
      r.opts = base(); r.opts.inputMethod = InputMethod::SimpleTelex;
      r.script = "qw"; r.expect = L"qw"; rows.push_back(r); }

    // ---- IDC_CHK_DIGITS (digitsAreLiteral) — VNI-only surface ----
    { Row r; r.name = "VNI + digits-as-tones: 'ho2' -> hò";
      r.opts = base(); r.opts.inputMethod = InputMethod::Vni; r.opts.digitsAreLiteral = false;
      r.script = "ho2"; r.expect = L"hò"; rows.push_back(r); }
    { Row r; r.name = "VNI + digits literal: 'ho2' stays raw";
      r.opts = base(); r.opts.inputMethod = InputMethod::Vni; r.opts.digitsAreLiteral = true;
      r.script = "ho2"; r.expect = L"ho2"; rows.push_back(r); }

    // ---- IDC_CHK_SPELL (checkSpelling) ----
    // "nguoif" violates the vowel rules: with the spelling check ON the
    // engine refuses the composition and restores the raw keystrokes; OFF it
    // composes the (wrong) word anyway.
    { Row r; r.name = "checkSpelling ON: invalid 'nguoif' restores raw";
      r.opts = base(); r.opts.checkSpelling = true;
      r.script = "nguoif|"; r.expect = L"nguoif "; rows.push_back(r); }
    { Row r; r.name = "checkSpelling OFF: invalid 'nguoif' composes anyway";
      r.opts = base(); r.opts.checkSpelling = false;
      r.script = "nguoif|"; r.expect = L"nguòi "; rows.push_back(r); }

    // ---- IDC_CHK_RESTORE (restoreIfWrongSpelling) ----
    { Row r; r.name = "restore ON: wrong spelling reverts to raw keys";
      r.opts = base(); r.opts.restoreIfWrongSpelling = true;
      r.script = "nguoif|"; r.expect = L"nguoif "; rows.push_back(r); }
    { Row r; r.name = "restore OFF: wrong spelling keeps the composed form";
      r.opts = base(); r.opts.restoreIfWrongSpelling = false;
      r.script = "nguoif|"; r.expect = L"nguòi "; rows.push_back(r); }

    // ---- IDC_CHK_MODERN (useModernOrthography) ----
    { Row r; r.name = "modern OFF: 'hoaf' -> hòa (legacy mark placement)";
      r.opts = base(); r.opts.useModernOrthography = false;
      r.script = "hoaf"; r.expect = L"hòa"; rows.push_back(r); }
    { Row r; r.name = "modern ON: 'hoaf' -> hoà";
      r.opts = base(); r.opts.useModernOrthography = true;
      r.script = "hoaf"; r.expect = L"hoà"; rows.push_back(r); }

    // ---- IDC_CHK_QUICK (quickTelex: doubled consonant shortcuts) ----
    const std::pair<const char*, const wchar_t*> kQuick[] = {
        {"cc", L"ch"}, {"gg", L"gi"}, {"kk", L"kh"}, {"nn", L"ng"},
        {"pp", L"ph"}, {"qq", L"qu"}, {"tt", L"th"},
    };
    for (const auto& [seq, want] : kQuick) {
        { Row r; r.name = std::string("quickTelex ON: '") + seq + "' expands";
          r.opts = base(); r.opts.quickTelex = true;
          r.script = std::string(seq) + "|"; r.expect = std::wstring(want) + L" ";
          rows.push_back(r); }
        { Row r; r.name = std::string("quickTelex OFF: '") + seq + "' stays raw";
          r.opts = base(); r.opts.quickTelex = false;
          r.script = std::string(seq) + "|"; r.expect = std::wstring(L"") +
              static_cast<wchar_t>(seq[0]) + static_cast<wchar_t>(seq[1]) + L" ";
          rows.push_back(r); }
    }

    // ---- IDC_CHK_UPPER (upperCaseFirstChar: sentence-start capital) ----
    { Row r; r.name = "upperFirst OFF: 'a. b' keeps lowercase";
      r.opts = base(); r.opts.upperCaseFirstChar = false;
      r.script = "a.|b"; r.expect = L"a. b"; rows.push_back(r); }
    { Row r; r.name = "upperFirst ON: 'a. b' -> 'a. B'";
      r.opts = base(); r.opts.upperCaseFirstChar = true;
      r.script = "a.|b"; r.expect = L"a. B"; rows.push_back(r); }

    std::cout << "== B9 effect layer: every tab-0 option flips engine output ==\n";
    for (const Row& r : rows) {
        const std::wstring got = run(r.opts, r.script);
        check(got == r.expect,
              r.name + "  (got \"" + show(got) + "\", want \"" +
              show(r.expect) + "\")");
    }

    // ---- IDC_COMBO_CODETABLE: a non-Unicode table must change the emitted
    // bytes (the exact legacy code points live in the frozen flat tables; the
    // pin is "the option is not a no-op" + Unicode stays canonical). ----
    std::cout << "== B9 effect layer: code table combo changes emitted text ==\n";
    {
        EngineOptions uni = base();
        EngineOptions cp1258 = base();
        cp1258.codeTable = CodeTable::Cp1258;
        const std::wstring u = run(uni, "vieejt");
        const std::wstring c = run(cp1258, "vieejt");
        check(u == L"việt", "Unicode table composes canonical 'việt'");
        check(!c.empty() && c != u, "Cp1258 table emits different code points");
        check(c.size() == u.size(), "Cp1258 output keeps the same character count");
    }

    if (g_fail == 0) {
        std::cout << "\nALL SETTINGS-WIRING TESTS PASSED (" << g_pass << " checks)\n";
        return 0;
    }
    std::cout << "\n" << g_fail << " SETTINGS-WIRING TEST(S) FAILED (" << g_pass
              << " passed)\n";
    return 1;
}
