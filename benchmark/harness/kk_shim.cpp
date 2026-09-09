//============================================================================
// Vietnamese IME cross-engine benchmark
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File: benchmark/harness/kk_shim.cpp
//----------------------------------------------------------------------------
// A thin extern "C" shim around KieeKey's own engine, used for the v1.3.0 RC1
// change-attribution pair:
//
//   libkkbase.so = this file + the FROZEN v1.2.2 engine sources
//                  (benchmark/reference/kieekey-1.2.2)
//   libkkcand.so = this file + the CURRENT engine sources (src/core)
//
// Both are built from the SAME shim source with the SAME flags, so the only
// difference between the two columns is the engine translation unit behind
// them — never harness code, never a compiler setting. They are loaded with
// RTLD_LOCAL and -fvisibility=hidden, so each object keeps its own copy of
// every engine symbol and neither binds to the statically linked subject
// column. Nothing here edits or special-cases the engine.
//
// prepare / invoke are the same two timed stages the in-process drivers expose,
// and — this is the whole point of the pair — they are the SAME stages: the
// shim materialises the engine's own replacement text in kk_result, exactly as
// KieeKeyDriver does in process(), so a ns/key difference between the columns
// is the engine change and nothing else. (A first version of this shim dropped
// the kk_prepare call on the timed path; both columns then measured the
// engine's "no input" early-out and the pair reported a tidy, meaningless
// 1.5 %. The selftest's transcript check — see rc1.hpp — is what makes that
// class of mistake fail loudly instead of quietly.)
//============================================================================
#include "TextEngine.hpp"

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace {

struct Handle {
    ok::text::TextEngine eng{};
    ok::text::TextInput in{};
    const ok::text::EngineResult* r = nullptr;
    std::wstring out;
    bool consumed = false;
    unsigned code = 0;
    unsigned backs = 0;
    using MacroMap = std::map<std::vector<std::uint32_t>, std::vector<std::uint32_t>>;
    MacroMap macros;

    // Same key normalization as the shipping app's macro store, and the same
    // contract KieeKeyDriver uses (the engine accumulates key CODES).
    static std::uint32_t normalizeKeyChar(std::uint32_t v) {
        std::uint32_t c = v & 0xFFFFu;
        if (c >= U'A' && c <= U'Z') { c += 32; }
        return c;
    }

    void installResolver() {
        eng.setMacroResolver([this](const std::vector<std::uint32_t>& k,
                                    std::vector<std::uint32_t>& o) {
            std::vector<std::uint32_t> norm;
            norm.reserve(k.size());
            for (const std::uint32_t v : k) { norm.push_back(normalizeKeyChar(v)); }
            const auto it = macros.find(norm);
            if (it == macros.end()) { return false; }
            o = it->second;
            return true;
        });
    }
};

void decodeUtf8(const char* p, std::wstring& w) {
    w.clear();
    const auto* q = reinterpret_cast<const unsigned char*>(p);
    while (*q) {
        char32_t cp;
        if (*q < 0x80) { cp = *q++; }
        else if ((*q >> 5) == 0x6) { cp = *q++ & 0x1F; cp = (cp << 6) | (*q++ & 0x3F); }
        else if ((*q >> 4) == 0xE) {
            cp = *q++ & 0x0F; cp = (cp << 6) | (*q++ & 0x3F); cp = (cp << 6) | (*q++ & 0x3F);
        } else { cp = *q++; }
        w += static_cast<wchar_t>(cp);
    }
}

}  // namespace

// The attribution objects are built with -fvisibility=hidden so that neither of
// them can bind to the *statically linked* subject column in the benchmark
// binary; only these entry points cross the boundary.
#define KK_EXPORT __attribute__((visibility("default")))

extern "C" {

KK_EXPORT void* kk_open(void) { return new Handle(); }
KK_EXPORT void kk_close(void* hv) { delete static_cast<Handle*>(hv); }

// Which engine build this object is — lets the report prove it loaded the tree
// it claims to have loaded (checked against the baseline manifest).
KK_EXPORT const char* kk_build_id(void) {
#ifdef KK_BUILD_ID
#define KK_STR2(x) #x
#define KK_STR(x) KK_STR2(x)
    return KK_STR(KK_BUILD_ID);
#else
    return "unspecified";
#endif
}

// method: 0 Telex, 1 VNI. asShipped: keep the engine's own shipping defaults.
KK_EXPORT void kk_configure(void* hv, int method, int asShipped, int checkSpelling, int restore,
                  int freeMark, int modern, int macroOn, int digitsLiteral) {
    auto* h = static_cast<Handle*>(hv);
    ok::text::EngineOptions o;
    o.inputMethod = (method == 0) ? ok::text::InputMethod::Telex : ok::text::InputMethod::Vni;
    o.digitsAreLiteral = digitsLiteral != 0;
    if (!asShipped) {
        o.checkSpelling = checkSpelling != 0;
        o.restoreIfWrongSpelling = restore != 0;
        o.freeMark = freeMark != 0;
        o.useModernOrthography = modern != 0;
        o.useMacro = macroOn != 0;
    }
    h->eng = ok::text::TextEngine(o);
    if (!h->macros.empty()) { h->installResolver(); }
    h->consumed = false;
    h->code = 0;
    h->backs = 0;
    h->out.clear();
    h->in = ok::text::TextInput{};
    h->r = nullptr;
}

KK_EXPORT void kk_install_macro(void* hv, const char* key, const char* textUtf8) {
    auto* h = static_cast<Handle*>(hv);
    std::vector<std::uint32_t> kp;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(key); *p; ++p) {
        kp.push_back(Handle::normalizeKeyChar(*p));
    }
    std::wstring w;
    decodeUtf8(textUtf8, w);
    h->macros[kp] = std::vector<std::uint32_t>(w.begin(), w.end());
    h->installResolver();
}

// kind: 0 char, 1 space, 2 backspace, 3 word break (the harness' numbering).
// MUST be called before every kk_invoke that is meant to measure a real key:
// it is what materialises the engine's input for that keystroke.
KK_EXPORT void kk_prepare(void* hv, unsigned kind, unsigned ch, int caps) {
    auto* h = static_cast<Handle*>(hv);
    h->in = ok::text::TextInput{};
    switch (kind) {
        case 1: h->in.kind = ok::text::InputKind::Space; break;
        case 2: h->in.kind = ok::text::InputKind::Backspace; break;
        case 3:
            h->in.kind = ok::text::InputKind::WordBreak;
            h->in.vkCode = 13;
            break;
        default:
            h->in.kind = ok::text::InputKind::Char;
            h->in.ch = static_cast<char32_t>(ch);
            h->in.isCaps = caps != 0;
            break;
    }
}

KK_EXPORT void kk_invoke(void* hv) {
    auto* h = static_cast<Handle*>(hv);
    h->r = &h->eng.process(h->in);
}

// Hands the engine's decision back: consumed flag, code, backspace count and a
// pointer to the materialised UTF-16 replacement (valid until the next
// kk_invoke). The visible string is edited by the caller, with the shared
// applyEdit helper that every engine's column uses.
KK_EXPORT void kk_result(void* hv, int* consumed, unsigned* code, unsigned* backs,
               const wchar_t** text, unsigned long long* textLen) {
    auto* h = static_cast<Handle*>(hv);
    const ok::text::EngineResult* r = h->r;
    h->consumed = r != nullptr && r->consumed();
    h->code = r ? static_cast<unsigned>(r->code) : 0u;
    h->backs = r ? r->backspaceCount : 0u;
    h->out.clear();
    if (r != nullptr && h->consumed) {
        if (r->code == ok::text::EngineCode::ReplaceMacro) {
            h->eng.macroExpansionUtf16(*r, h->out);
        } else {
            h->eng.replacementUtf16(*r, h->out);
        }
    }
    *consumed = h->consumed ? 1 : 0;
    *code = h->code;
    *backs = h->backs;
    *text = h->out.empty() ? nullptr : h->out.data();
    *textLen = static_cast<unsigned long long>(h->out.size());
}

// Cost reference for the indirect-call machinery itself (one dlsym'd pointer
// call, no engine work) — published, never silently subtracted.
KK_EXPORT int kk_noop(void) { return 1; }

}  // extern "C"
