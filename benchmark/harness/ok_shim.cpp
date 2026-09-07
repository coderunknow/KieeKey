//============================================================================
// Vietnamese IME cross-engine benchmark
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File: benchmark/harness/ok_shim.cpp
//----------------------------------------------------------------------------
// A thin extern "C" shim around the repository's OpenKey wrapper
// (tests/engine205.cpp + tests/engine205.hpp). It is compiled TWICE — once
// against the vendored OpenKey 2.0.5 engine and once against the latest
// OpenKey master engine — and each copy becomes a shared object that the
// benchmark loads with dlopen(RTLD_LOCAL).
//
// Why a .so: both OpenKey generations define the same global state
// (HookState, TypingWord, vLanguage, vKeyHandleEvent …). Loading them as two
// RTLD_LOCAL objects keeps their state strictly separate, and because the
// SAME shim source drives both, the two OpenKey columns differ only by the
// engine they were linked against — never by harness code.
//
// The shim performs no measurement and no text policy of its own: prepare /
// invoke / apply are the same three stages the in-process engines expose, and
// the visible-text edit is done by the caller. Nothing inside the upstream
// engine sources is edited.
//============================================================================
#include "engine205.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// The latest-master build is compiled with -Dok205=okmaster (the wrapper TU is
// compiled the same way), so this single source drives either engine build.

namespace {

struct Handle {
    ok205::Options opt;
    ok205::Delta d;
    // prepare() outputs
    int sel = 0;              // 0 char, 1 symbol, 2 space, 3 backspace, 4 break
    char32_t ch = 0;
    char sym = 0;
    bool caps = false;

    std::string textUtf8;
    std::wstring* visSink = nullptr;   // set by the harness: where to apply deltas
};

// kind: 0 char, 1 space, 2 backspace, 3 word break (matches the harness).
[[maybe_unused]] void encodeUtf8(const std::wstring& w, std::string& out) {
    out.clear();
    for (wchar_t wc : w) {
        const char32_t cp = static_cast<char32_t>(wc);
        if (cp < 0x80) { out += static_cast<char>(cp); }
        else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
}

std::size_t clampBacks(std::size_t b, const std::wstring& v) { return b > v.size() ? v.size() : b; }

}  // namespace

extern "C" {

// -- lifecycle --------------------------------------------------------------
void* ok_open(void) { return new Handle(); }
void ok_close(void* h) { delete static_cast<Handle*>(h); }

// method: 0 Telex, 1 VNI.  as_shipped: keep the wrapper's shipping defaults.
void ok_configure(void* hv, int method, int asShipped, int checkSpelling, int restore,
                  int freeMark, int modern, int macroOn) {
    auto* h = static_cast<Handle*>(hv);
    ok205::Options o;
    o.method = (method == 0) ? ok205::Method::Telex : ok205::Method::Vni;
    if (!asShipped) {
        o.checkSpelling = checkSpelling != 0;
        o.restoreIfWrongSpelling = restore != 0;
        o.freeMark = freeMark != 0;
        o.modernOrthography = modern != 0;
        o.useMacro = macroOn != 0;
    }
    h->opt = o;
    ok205::init(o);
}

void ok_install_macro(void* hv, const char* key, const char* textUtf8) {
    auto* h = static_cast<Handle*>(hv);
    std::wstring w;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(textUtf8);
    while (*p) {
        char32_t cp;
        if (*p < 0x80) { cp = *p++; }
        else if ((*p >> 5) == 0x6) { cp = *p++ & 0x1F; cp = (cp << 6) | (*p++ & 0x3F); }
        else if ((*p >> 4) == 0xE) {
            cp = *p++ & 0x0F; cp = (cp << 6) | (*p++ & 0x3F); cp = (cp << 6) | (*p++ & 0x3F);
        } else { cp = *p++; }
        w += static_cast<wchar_t>(cp);
    }
    (void)h;
    ok205::installMacro(key, w);
}

// -- the three measured stages ---------------------------------------------
void ok_prepare(void* hv, unsigned kind, unsigned ch, unsigned disp, int caps, int letterOrDigit) {
    auto* h = static_cast<Handle*>(hv);
    h->caps = caps != 0;
    h->sel = 0;
    if (kind == 1) { h->sel = 2; return; }
    if (kind == 2) { h->sel = 3; return; }
    if (kind == 3) { h->sel = 4; return; }
    if (letterOrDigit) {
        h->sel = 0;
        const char32_t c = static_cast<char32_t>(ch);
        h->ch = (c >= U'A' && c <= U'Z') ? static_cast<char32_t>(c + 32) : c;  // lowercase keycode
        return;
    }
    if (caps) { h->sel = 1; h->sym = static_cast<char>(disp); return; }   // shifted symbol
    h->sel = 0;
    h->ch = static_cast<char32_t>(ch);
}

void ok_invoke(void* hv) {
    auto* h = static_cast<Handle*>(hv);
    switch (h->sel) {
        case 0: ok205::processChar(h->ch, h->caps, false, h->d); break;
        case 1: ok205::processSymbol(h->sym, h->d); break;
        case 2: ok205::processSpace(false, h->d); break;
        case 3: ok205::processBackspace(h->d); break;
        default: ok205::processWordBreak(13, h->d); break;
    }
}

// Hands the delta back to the caller: code, backspace count, and a pointer to
// the engine's replacement text (valid until the next ok_invoke). The visible
// string is edited by the harness with the SAME helper every engine uses.
void ok_result(void* hv, unsigned* code, unsigned* backs, const wchar_t** text,
               unsigned long long* textLen) {
    auto* h = static_cast<Handle*>(hv);
    *code = static_cast<unsigned>(h->d.code);
    *backs = h->d.backspace;
    *text = h->d.text.empty() ? nullptr : h->d.text.data();
    *textLen = static_cast<unsigned long long>(h->d.text.size());
}

// Cost reference for the indirect-call machinery itself (dlsym'd pointer, no
// engine work): subtracted from the OpenKey columns and reported.
int ok_noop(void) { return 1; }

}  // extern "C"
