//============================================================================
// Vietnamese IME cross-engine benchmark — harness
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File: benchmark/harness/engines.hpp
//----------------------------------------------------------------------------
// One faithful consumer model per engine, so every engine is driven the way
// its own shipping front-end drives it. Each keystroke is measured in three
// separable stages, which is what makes the comparison honest:
//
//   prepare(ev) : ADAPTER work only — map the benchmark event onto whatever
//                 the engine's API wants (key code, shift flag, buffer size).
//                 No engine call. Measured by itself and subtracted, so no
//                 engine can be hurt or helped by adapter shape.
//   invoke()    : the engine entry point for one keystroke, nothing else.
//                 No output-buffer zeroing, no string building, no display
//                 update. This is "engine decision cost".
//   apply(ev)   : the work the real app does with the result — materialise
//                 replacement text and edit the visible string. The visible
//                 string is maintained by ONE shared helper (applyEdit) for
//                 every engine, so bookkeeping cannot differ by construction.
//
//   full = prepare + invoke + apply (what a user actually pays per key)
//============================================================================
#pragma once

#include "corpus.hpp"
#include "viet.hpp"

#include "TextEngine.hpp"

// ---- OpenKey reference engines: dlopen'd shared objects ------------------
#include <dlfcn.h>
#include <cstdlib>

// ---- UniKey engine -------------------------------------------------------
#include "ukengine.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bench {

//============================================================================
// Configuration
//============================================================================
enum class Method : uint8_t { Telex, Vni };

struct Cfg {
    Method method = Method::Telex;
    // asShipped: every engine keeps its own shipping defaults, untouched.
    // Otherwise the flags below configure the shared feature subset
    // identically on all engines (feature-matched run).
    bool asShipped = true;
    bool macroOn = false;
    bool restoreOn = false;      // auto-restore of non-Vietnamese words
    bool spellCheckOn = false;   // spelling validation of the composed word
    bool freeMark = false;       // tone key may be typed anywhere in the word
    bool modernOrthography = false;
};

//============================================================================
// Shared visible-text bookkeeping (identical code path for all engines)
//============================================================================
inline void applyEdit(std::wstring& vis, std::size_t backs, const std::wstring& text) {
    const std::size_t b = backs > vis.size() ? vis.size() : backs;
    vis.erase(vis.size() - b, b);
    vis += text;
}

//============================================================================
// Driver interface
//============================================================================
class IDriver {
public:
    virtual ~IDriver() = default;
    virtual const char* name() const = 0;
    virtual void configure(const Cfg& c) = 0;              // also resets state
    virtual void prepare(const corpus::Event& e) = 0;     // adapter work
    virtual void invoke() = 0;                             // engine call
    virtual void apply(const corpus::Event& e) = 0;       // consumer work
    virtual void installMacro(const std::string& key, const std::wstring& text) = 0;
    virtual std::string textUtf8() const { return viet::utf16To8(vis_); }

    // Call-machinery overhead of this adapter, in ns per call, or -1 when the
    // adapter has no extra indirection to disclose. Measured (never estimated)
    // so that the reader can subtract it from the timed engine call.
    virtual long long noopCall() const { return -1; }

    void feed(const corpus::Event& e) { prepare(e); invoke(); apply(e); }

    std::wstring vis_;
};

//============================================================================
// KieeKey — shipped TextEngine with the consumer contract documented in
// src/core/TextEngine.hpp and implemented by the shipping TSF composer:
//   !consumed()         -> pass the key through (backspace: app deletes)
//   ReplaceMacro        -> delete backspaceCount, insert expansion (D3)
//   Restore / RestoreNew -> delete, insert replacement, re-issue the key
//   otherwise           -> delete backspaceCount, insert replacement
//============================================================================
class KieeKeyDriver : public IDriver {
public:
    using MacroMap = std::map<std::vector<std::uint32_t>, std::vector<std::uint32_t>>;

    explicit KieeKeyDriver(const char* nm) : name_(nm) {}
    const char* name() const override { return name_; }

    void configure(const Cfg& c) override {
        ok::text::EngineOptions o;
        o.inputMethod = (c.method == Method::Telex) ? ok::text::InputMethod::Telex
                                                   : ok::text::InputMethod::Vni;
        // Shipped Windows app default: digits are literal in Telex; a VNI
        // user turns that back off so the tone digits compose.
        o.digitsAreLiteral = (c.method == Method::Telex);
        if (!c.asShipped) {
            o.checkSpelling = c.spellCheckOn;
            o.restoreIfWrongSpelling = c.restoreOn;
            o.freeMark = c.freeMark;
            o.useModernOrthography = c.modernOrthography;
            o.useMacro = c.macroOn;
        }
        eng_ = ok::text::TextEngine(o);
        if (!macros_.empty()) { installResolver(); }
        vis_.clear();
    }

    void installMacro(const std::string& key, const std::wstring& text) override {
        std::vector<std::uint32_t> kp;
        for (unsigned char ch : key) { kp.push_back(normalizeKeyChar(ch)); }
        macros_[kp] = std::vector<std::uint32_t>(text.begin(), text.end());
        installResolver();
    }

    void prepare(const corpus::Event& e) override {
        in_ = ok::text::TextInput{};
        switch (e.kind) {
            case corpus::Kind::Space: in_.kind = ok::text::InputKind::Space; break;
            case corpus::Kind::Backspace: in_.kind = ok::text::InputKind::Backspace; break;
            case corpus::Kind::WordBreak:
                in_.kind = ok::text::InputKind::WordBreak;
                in_.vkCode = 13;
                break;
            default:
                in_.kind = ok::text::InputKind::Char;
                in_.ch = e.ch;
                in_.isCaps = e.caps;
                break;
        }
    }

    void invoke() override { r_ = &eng_.process(in_); }

    void apply(const corpus::Event& e) override {
        const ok::text::EngineResult& r = *r_;
        const wchar_t ch = static_cast<wchar_t>(corpus::displayChar(e));
        if (!r.consumed()) {
            if (e.kind == corpus::Kind::Backspace) {
                if (!vis_.empty()) { vis_.pop_back(); }
            } else {
                vis_ += ch;
            }
            return;
        }
        if (r.code == ok::text::EngineCode::ReplaceMacro) {
            eng_.macroExpansionUtf16(r, scratch_);
            applyEdit(vis_, r.backspaceCount, scratch_);
            return;
        }
        eng_.replacementUtf16(r, scratch_);
        applyEdit(vis_, r.backspaceCount, scratch_);
        if (r.code == ok::text::EngineCode::Restore ||
            r.code == ok::text::EngineCode::RestoreAndStartNewSession) {
            vis_ += ch;                 // char / space restore re-issue
        }
    }

private:
    // Same key normalization as the shipping app's macro store
    // (src/app/main.cpp: low 16 bits of each accumulated code, A-Z folded to
    // a-z) — the engine accumulates key CODES, not characters.
    static std::uint32_t normalizeKeyChar(std::uint32_t v) {
        std::uint32_t c = v & 0xFFFFu;
        if (c >= U'A' && c <= U'Z') { c += 32; }
        return c;
    }

    void installResolver() {
        eng_.setMacroResolver([this](const std::vector<std::uint32_t>& k,
                                    std::vector<std::uint32_t>& out) {
            std::vector<std::uint32_t> norm;
            norm.reserve(k.size());
            for (const std::uint32_t v : k) { norm.push_back(normalizeKeyChar(v)); }
            const auto it = macros_.find(norm);
            if (it == macros_.end()) { return false; }
            out = it->second;
            return true;
        });
    }

    const char* name_;
    ok::text::TextEngine eng_{};
    ok::text::TextInput in_{};
    const ok::text::EngineResult* r_ = nullptr;
    std::wstring scratch_;
    MacroMap macros_;
};

//============================================================================
// OpenKey (both generations) — each build is a shared object loaded with
// dlopen(RTLD_LOCAL|RTLD_NOW) and driven through the SAME shim source
// (harness/ok_shim.cpp), so the 2.0.5 column and the latest-master column
// cannot differ by harness code, and their global engine state cannot be
// merged by the linker. The shim's indirect-call cost is measured (ok_noop)
// and published so the reader can subtract it.
//============================================================================
inline std::string okLibPath(const char* which) {
    const std::string envName = std::string(which) == "master" ? "BENCH_LIB_OKMASTER"
                                                                : "BENCH_LIB_OK205";
    if (const char* e = std::getenv(envName.c_str()); e && *e) { return e; }
    // A sanitizer build must load the sanitizer build of the engine, otherwise
    // the OpenKey columns would silently be un-instrumented inside bench_san.
    std::string sfx;
#if defined(__SANITIZE_ADDRESS__)
    sfx = "_san";
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
    sfx = "_san";
#  endif
#endif
    return "benchmark/.build/libok" + std::string(which) + sfx + ".so";
}

struct OkShimApi {
    void* lib = nullptr;
    void* h = nullptr;
    void* (*open)() = nullptr;
    void (*close)(void*) = nullptr;
    void (*configure)(void*, int, int, int, int, int, int, int) = nullptr;
    void (*prepare)(void*, unsigned, unsigned, unsigned, int, int) = nullptr;
    void (*invoke)(void*) = nullptr;
    void (*result)(void*, unsigned*, unsigned*, const wchar_t**, unsigned long long*) = nullptr;
    void (*installMacro)(void*, const char*, const char*) = nullptr;
    int (*noop)() = nullptr;

    bool load(const std::string& path, std::string& err) {
        lib = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!lib) { err = ::dlerror(); return false; }
        auto sym = [this](const char* n) { return reinterpret_cast<void*>(::dlsym(lib, n)); };
        *reinterpret_cast<void**>(&open) = sym("ok_open");
        *reinterpret_cast<void**>(&close) = sym("ok_close");
        *reinterpret_cast<void**>(&configure) = sym("ok_configure");
        *reinterpret_cast<void**>(&prepare) = sym("ok_prepare");
        *reinterpret_cast<void**>(&invoke) = sym("ok_invoke");
        *reinterpret_cast<void**>(&result) = sym("ok_result");
        *reinterpret_cast<void**>(&installMacro) = sym("ok_install_macro");
        *reinterpret_cast<void**>(&noop) = sym("ok_noop");
        if (!open || !configure || !prepare || !invoke || !result || !noop) {
            err = "missing ok_* symbol in " + path;
            return false;
        }
        h = open();
        return h != nullptr;
    }
};

class OpenKeyShimDriver final : public IDriver {
public:
    OpenKeyShimDriver(const char* nm, std::string libPath)
        : name_(nm), libPath_(std::move(libPath)) {
        std::string err;
        loaded_ = api_.load(libPath_, err);
        if (!loaded_) {
            std::fprintf(stderr, "[harness] dlopen failed for %s: %s\n",
                         libPath_.c_str(), err.c_str());
        }
    }

    ~OpenKeyShimDriver() override {
        if (api_.lib && api_.h && api_.close) { api_.close(api_.h); }
    }

    const char* name() const override { return name_; }
    bool loaded() const { return loaded_; }
    // Cost of the call machinery itself (used by the latency mode to publish
    // the shim overhead next to the OpenKey numbers).
    int noop() const { return api_.noop ? api_.noop() : 0; }
    // One dlsym-resolved indirect call into the shared object, then back — the
    // part of OpenKey's timed region that is harness plumbing, not engine work.
    long long noopCall() const override {
        if (!loaded_ || !api_.noop) { return -1; }
        volatile int sink = 0;
        for (int i = 0; i < 1000; ++i) { sink += api_.noop(); }
        (void)sink;
        return 0;
    }
    int (*noopFn() const)() { return api_.noop; }

    void configure(const Cfg& c) override {
        if (!loaded_) { return; }
        api_.configure(api_.h, c.method == Method::Telex ? 0 : 1, c.asShipped ? 1 : 0,
                       c.spellCheckOn ? 1 : 0, c.restoreOn ? 1 : 0, c.freeMark ? 1 : 0,
                       c.modernOrthography ? 1 : 0, c.macroOn ? 1 : 0);
        vis_.clear();
    }

    void installMacro(const std::string& key, const std::wstring& text) override {
        if (!loaded_ || !api_.installMacro) { return; }
        const std::string t = viet::utf16To8(text);
        api_.installMacro(api_.h, key.c_str(), t.c_str());
    }

    void prepare(const corpus::Event& e) override {
        if (!loaded_) { return; }
        const bool letter = corpus::isLetterKey(e);
        const bool digit = (e.ch >= U'0' && e.ch <= U'9');
        api_.prepare(api_.h, static_cast<unsigned>(e.kind), static_cast<unsigned>(e.ch),
                     static_cast<unsigned>(corpus::displayChar(e)), e.caps ? 1 : 0,
                     (letter || digit) ? 1 : 0);
    }

    void invoke() override { if (loaded_) { api_.invoke(api_.h); } }

    void apply(const corpus::Event& e) override {
        if (!loaded_) { return; }
        unsigned code = 0, backs = 0;
        const wchar_t* txt = nullptr;
        unsigned long long n = 0;
        api_.result(api_.h, &code, &backs, &txt, &n);
        if (code == 0) {
            if (backs > 0) { applyEdit(vis_, backs, std::wstring()); }
            else if (e.kind != corpus::Kind::Backspace) {
                vis_ += static_cast<wchar_t>(corpus::displayChar(e));
            }
            return;
        }
        applyEdit(vis_, backs, txt ? std::wstring(txt, txt + n) : std::wstring());
    }

    std::string textUtf8() const override { return viet::utf16To8(vis_); }

private:
    const char* name_;
    std::string libPath_;
    OkShimApi api_{};
    bool loaded_ = false;
};

//============================================================================
// UniKey — vendored UKEngine, driven as its own front-ends drive it (XIM /
// GTK / UnikeyNT): the engine buffers pending keystrokes and reports a
// changed span only on a conversion; a word break commits the preedit.
// Output charset XUTF8, i.e. the engine itself converts to the app's text
// encoding inside the measured call (its real design; the other engines'
// equivalent conversion happens in their apply() stage or in replaceUtf16).
//============================================================================
class UniKeyDriver : public IDriver {
public:
    explicit UniKeyDriver(const char* nm) : name_(nm) {}
    const char* name() const override { return name_; }

    // Debug aid for harness-model verification (enabled by --probe-trace).
    static bool& traceFlag() { static bool v = false; return v; }

    void configure(const Cfg& c) override {
        if (!initedFlag()) { SetupUnikeyEngine(); initedFlag() = true; }
        std::memset(&shm_, 0, sizeof(shm_));
        shm_.initialized = 1;
        shm_.vietKey = 1;
        shm_.iconShown = 0;
        shm_.usrKeyMapLoaded = 0;
        // The engine writes the replacement span in this charset; `backs`
        // always counts CHARACTERS (writeOutput/markChange work on buffer
        // entries), so a UTF-8 charset is decodable without a width divide.
        shm_.charsetId = CONV_CHARSET_XUTF8;
        // Shipping UniKey defaults (UnikeySetup): free marking ON, spell check
        // ON and non-strict, auto-restore of non-Vietnamese words ON, macro ON
        // with an empty user table, modern style OFF.
        shm_.options.freeMarking = 1;
        shm_.options.modernStyle = 0;
        shm_.options.macroEnabled = 1;
        shm_.options.useUnicodeClipboard = 1;
        shm_.options.alwaysMacro = 0;
        shm_.options.strictSpellCheck = 0;
        shm_.options.useIME = 0;
        shm_.options.spellCheckEnabled = 1;
        shm_.options.autoNonVnRestore = 1;
        if (!c.asShipped) {
            shm_.options.freeMarking = c.freeMark ? 1 : 0;
            shm_.options.modernStyle = c.modernOrthography ? 1 : 0;
            shm_.options.macroEnabled = c.macroOn ? 1 : 0;
            shm_.options.spellCheckEnabled = c.spellCheckOn ? 1 : 0;
            shm_.options.autoNonVnRestore = c.restoreOn ? 1 : 0;
        }
        shm_.input.init();
        shm_.macStore.init();
        shm_.input.setIM(c.method == Method::Telex ? UkTelex : UkVni);
        engine_ = UkEngine();
        engine_.setCtrlInfo(&shm_);
        vis_.clear();
        wordStart_ = 0;
    }

    void installMacro(const std::string& key, const std::wstring& text) override {
        const std::string t = viet::utf16To8(text);
        shm_.macStore.addItem(key.c_str(), t.c_str(), CONV_CHARSET_XUTF8);
    }

    void prepare(const corpus::Event& e) override {
        ev_ = e;
        vk_ = vkFor(e);
        backs_ = 0;
        outSize_ = static_cast<int>(sizeof(outBuf_));
        outType_ = UkCharOutput;
    }

    void invoke() override {
        if (ev_.kind == corpus::Kind::Backspace) {
            ret_ = engine_.processBackspace(backs_, outBuf_, outSize_, outType_);
        } else {
            ret_ = engine_.process(vk_, backs_, outBuf_, outSize_, outType_);
        }
    }

    // Contract of UkEngine::process / processBackspace: on a non-zero return
    // the front-end deletes `backs` characters of the text it last inserted
    // for the current word and writes the returned span in its place; on zero
    // the key is typed through untouched. The engine's own word buffer begins
    // after the last reset key (space / Enter / any control code), so the tail
    // of the app text from wordStart_ is exactly the span it may rewrite; the
    // erase is clamped to that span so a mis-count can never reach an earlier
    // word.
    void apply(const corpus::Event& e) override {
        if (traceFlag()) {
            std::fprintf(stderr, "[uk] key=%u(%c) ret=%d backs=%d outSize=%d text=[%s]\n",
                         vk_, (e.ch >= 32 && e.ch < 127) ? (char)e.ch : '?', ret_, backs_, outSize_,
                         viet::utf16To8(vis_).c_str());
        }
        if (ret_ != 0) {
            const std::size_t avail = vis_.size() - wordStart_;
            const std::size_t b = std::min<std::size_t>(
                backs_ > 0 ? static_cast<std::size_t>(backs_) : 0, avail);
            vis_.erase(vis_.size() - b, b);
            vis_ += viet::utf8BytesToW(outBuf_,
                                       outSize_ > 0 ? static_cast<std::size_t>(outSize_) : 0);
            return;
        }
        if (e.kind == corpus::Kind::Backspace) {
            // processBackspace already accounts for the physical deletion; the
            // app deletes one character itself only when backs == 0.
            if (!vis_.empty()) { vis_.pop_back(); }
            if (wordStart_ > vis_.size()) { wordStart_ = vis_.size(); }
            return;
        }
        vis_ += static_cast<wchar_t>(corpus::displayChar(e));
        if (isResetKey(e)) { wordStart_ = vis_.size(); }
    }

    std::string textUtf8() const override { return viet::utf16To8(vis_); }

private:
    // Codes the engine classifies as ukcReset (0..32): they end the current
    // word and clear its buffer rather than joining it.
    static bool isResetKey(const corpus::Event& e) {
        return e.kind == corpus::Kind::Space || e.kind == corpus::Kind::WordBreak;
    }

    // The vendored engine's classifier (UkKeyMap / UkcMap / WordBreakSyms) is
    // indexed by CHARACTER code, not by Windows virtual-key codes: its Telex
    // and VNI tables bind ASCII codes ('.', ',', '[', '5'), and the word-break
    // set is an ASCII set. Codes above 255 fall into the generic
    // non-Vietnamese path, which is how a front-end passes unmapped input.
    static unsigned vkFor(const corpus::Event& e) {
        if (e.kind == corpus::Kind::WordBreak) { return 13u; }   // Enter / Tab
        if (e.kind == corpus::Kind::Space) { return 32u; }
        const char32_t c = e.ch;
        if (c >= U'a' && c <= U'z') {
            return e.caps ? static_cast<unsigned>(c - 32u) : static_cast<unsigned>(c);
        }
        return static_cast<unsigned>(c);
    }

    const char* name_;
    UkSharedMem shm_{};
    UkEngine engine_;
    unsigned char outBuf_[1024]{};
    corpus::Event ev_{};
    unsigned vk_ = 0;
    int backs_ = 0;
    int outSize_ = 0;
    UkOutputType outType_ = UkCharOutput;
    int ret_ = 0;
    std::size_t wordStart_ = 0;
    static bool& initedFlag() { static bool b = false; return b; }
};

//============================================================================
// Driver set
//============================================================================
enum class Which : uint8_t {
    KieeKey = 0,        // this repository, current tree (subject)
    KieeKeyCtl = 1,     // A/A control: same engine, second instance
    OpenKey205 = 2,     // latest OpenKey release
    OpenKeyMaster = 3,  // latest OpenKey code (upstream master)
    UniKey = 4,         // UniKey UKEngine (4.x line)
    // v1.3.0 RC1 attribution pair: the FROZEN v1.2.2 engine and the CURRENT
    // tree, both built from benchmark/harness/kk_shim.cpp with identical flags
    // and loaded side by side. A difference between these two columns is
    // attributable to the engine change and to nothing else — campaign-to-
    // campaign drift (host state, another day's binary) cannot enter, because
    // they are measured in the same round of the same process.
    KieeKeyBase = 5,
    KieeKeyCand = 6
};
inline constexpr Which kAll[] = {Which::KieeKey, Which::KieeKeyCtl, Which::OpenKey205,
                                 Which::OpenKeyMaster, Which::UniKey};
inline constexpr Which kContest[] = {Which::KieeKey, Which::OpenKey205,
                                     Which::OpenKeyMaster, Which::UniKey};
inline constexpr Which kRc1[] = {Which::KieeKey, Which::KieeKeyCtl, Which::OpenKey205,
                                 Which::OpenKeyMaster, Which::UniKey};
inline constexpr Which kAttrib[] = {Which::KieeKeyBase, Which::KieeKeyCand};

inline const char* whichName(Which w) {
    switch (w) {
        case Which::KieeKey: return "kieekey";
        case Which::KieeKeyCtl: return "kieekey-aa";
        case Which::OpenKey205: return "openkey-2.0.5";
        case Which::OpenKeyMaster: return "openkey-master";
        case Which::UniKey: return "unikey-4.x";
        case Which::KieeKeyBase: return "kieekey-base";
        case Which::KieeKeyCand: return "kieekey-cand";
    }
    return "?";
}

inline Which whichFrom(const std::string& s) {
    if (s == "kieekey") { return Which::KieeKey; }
    if (s == "kieekey-aa" || s == "ctl" || s == "aa") { return Which::KieeKeyCtl; }
    if (s == "openkey-2.0.5") { return Which::OpenKey205; }
    if (s == "openkey-master") { return Which::OpenKeyMaster; }
    if (s == "unikey-4.x") { return Which::UniKey; }
    if (s == "kieekey-base") { return Which::KieeKeyBase; }
    if (s == "kieekey-cand") { return Which::KieeKeyCand; }
    return Which::UniKey;
}

// whichFrom deliberately cannot signal failure (it is called from parsing that
// predates this), so the caller checks first. The reason: an unrecognised name
// used to fall through to UniKey, so a typo like --engines=contest,ctl quietly
// measured UniKey twice and dropped the A/A control — the one column whose whole
// job is to say how much of a difference is noise. A campaign must not be able to
// lose its control column silently.
inline bool whichKnown(const std::string& s) {
    return s == "kieekey" || s == "kieekey-aa" || s == "ctl" || s == "aa"
        || s == "openkey-2.0.5" || s == "openkey-master" || s == "unikey-4.x"
        || s == "kieekey-base" || s == "kieekey-cand";
}

//============================================================================
// KieeKey attribution pair (v1.3.0 RC1)
//============================================================================
inline std::string kkLibPath(const char* which) {
    const std::string envName = std::string("BENCH_LIB_KK") + which;
    if (const char* e = std::getenv(envName.c_str()); e && *e) { return e; }
    std::string sfx;
#if defined(__SANITIZE_ADDRESS__)
    sfx = "_san";
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
    sfx = "_san";
#  endif
#endif
    return std::string("benchmark/.build/libkk") + which + sfx + ".so";
}

struct KkShimApi {
    void* lib = nullptr;
    void* h = nullptr;
    void* (*open)() = nullptr;
    void (*close)(void*) = nullptr;
    void (*configure)(void*, int, int, int, int, int, int, int, int) = nullptr;
    void (*prepare)(void*, unsigned, unsigned, int) = nullptr;
    void (*invoke)(void*) = nullptr;
    void (*result)(void*, int*, unsigned*, unsigned*, const wchar_t**, unsigned long long*) = nullptr;
    void (*installMacro)(void*, const char*, const char*) = nullptr;
    int (*noop)() = nullptr;
    const char* (*buildId)() = nullptr;

    bool load(const std::string& path, std::string& err) {
        lib = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!lib) { err = ::dlerror(); return false; }
        auto sym = [this](const char* n) { return reinterpret_cast<void*>(::dlsym(lib, n)); };
        *reinterpret_cast<void**>(&open) = sym("kk_open");
        *reinterpret_cast<void**>(&close) = sym("kk_close");
        *reinterpret_cast<void**>(&configure) = sym("kk_configure");
        *reinterpret_cast<void**>(&prepare) = sym("kk_prepare");
        *reinterpret_cast<void**>(&invoke) = sym("kk_invoke");
        *reinterpret_cast<void**>(&result) = sym("kk_result");
        *reinterpret_cast<void**>(&installMacro) = sym("kk_install_macro");
        *reinterpret_cast<void**>(&noop) = sym("kk_noop");
        *reinterpret_cast<void**>(&buildId) = sym("kk_build_id");
        if (!open || !configure || !prepare || !invoke || !result || !noop) {
            err = "missing kk_* symbol in " + path;
            return false;
        }
        h = open();
        return h != nullptr;
    }
};

class KieeKeyShimDriver final : public IDriver {
public:
    KieeKeyShimDriver(const char* nm, std::string libPath)
        : name_(nm), libPath_(std::move(libPath)) {
        std::string err;
        loaded_ = api_.load(libPath_, err);
        if (!loaded_) {
            std::fprintf(stderr, "[harness] dlopen failed for %s: %s\n",
                         libPath_.c_str(), err.c_str());
        }
    }
    ~KieeKeyShimDriver() override {
        if (api_.lib && api_.h && api_.close) { api_.close(api_.h); }
    }
    const char* name() const override { return name_; }
    bool loaded() const { return loaded_; }
    // Which engine tree this object was built from — written into every artifact
    // row, so the report cannot claim the wrong build (and so a stale .so is
    // visible in the data rather than in a footnote).
    std::string buildId() const { return (loaded_ && api_.buildId) ? api_.buildId() : "?"; }
    long long noopCall() const override {
        if (!loaded_ || !api_.noop) { return -1; }
        volatile int sink = 0;
        for (int i = 0; i < 1000; ++i) { sink += api_.noop(); }
        (void)sink;
        return 0;
    }

    void configure(const Cfg& c) override {
        if (!loaded_) { return; }
        // digits are literal in Telex, not in VNI — the same rule the in-process
        // KieeKeyDriver applies, so the two KieeKey columns agree by
        // construction about what they were told to do.
        const int digitsLiteral = (c.method == Method::Telex) ? 1 : 0;
        api_.configure(api_.h, c.method == Method::Telex ? 0 : 1, c.asShipped ? 1 : 0,
                       c.spellCheckOn ? 1 : 0, c.restoreOn ? 1 : 0, c.freeMark ? 1 : 0,
                       c.modernOrthography ? 1 : 0, c.macroOn ? 1 : 0, digitsLiteral);
        vis_.clear();
    }

    void installMacro(const std::string& key, const std::wstring& text) override {
        if (!loaded_ || !api_.installMacro) { return; }
        const std::string t = viet::utf16To8(text);
        api_.installMacro(api_.h, key.c_str(), t.c_str());
    }

    // THE STAGE THAT WAS MISSING ONCE: prepare must reach the shim, or the
    // engine is handed its default-constructed input on every key and both
    // attribution columns measure an early-out instead of the engine. The
    // rc1 selftest compares this column's transcript against the in-process
    // KieeKey column and fails the campaign if they ever differ.
    void prepare(const corpus::Event& e) override {
        if (!loaded_) { return; }
        kind_ = static_cast<unsigned>(e.kind);
        ch_ = static_cast<unsigned>(e.ch);
        caps_ = e.caps ? 1 : 0;
        api_.prepare(api_.h, kind_, ch_, caps_);
    }

    void invoke() override { if (loaded_) { api_.invoke(api_.h); } }

    // The consumer contract, applied identically to the in-process KieeKey
    // column above (same three cases, same re-issue of a restored key). The
    // attribution pair is only meaningful if the two columns differ in the
    // engine translation unit and in NOTHING else — rc1.hpp's attrib-guard
    // mode compares this transcript against the in-process one key by key and
    // fails the campaign when they diverge.
    void apply(const corpus::Event& e) override {
        if (!loaded_) { return; }
        int consumed = 0;
        unsigned code = 0, backs = 0;
        const wchar_t* txt = nullptr;
        unsigned long long n = 0;
        api_.result(api_.h, &consumed, &code, &backs, &txt, &n);
        const std::wstring t(txt, txt + n);
        const wchar_t ch = static_cast<wchar_t>(corpus::displayChar(e));
        if (!consumed) {
            if (e.kind == corpus::Kind::Backspace) {
                if (!vis_.empty()) { vis_.pop_back(); }
            } else {
                vis_ += ch;
            }
            return;
        }
        applyEdit(vis_, backs, t);
        if (code == static_cast<unsigned>(ok::text::EngineCode::Restore) ||
            code == static_cast<unsigned>(ok::text::EngineCode::RestoreAndStartNewSession)) {
            vis_ += ch;                 // char / space restore re-issue
        }
    }

private:
    const char* name_ = "kieekey-shim";
    std::string libPath_;
    bool loaded_ = false;
    KkShimApi api_{};
    unsigned kind_ = 0, ch_ = 0, caps_ = 0;
};

inline std::unique_ptr<IDriver> makeDriver(Which w) {
    switch (w) {
        case Which::KieeKey: return std::unique_ptr<IDriver>(new KieeKeyDriver("kieekey"));
        case Which::KieeKeyCtl: return std::unique_ptr<IDriver>(new KieeKeyDriver("kieekey-aa"));
        case Which::OpenKey205:
            return std::unique_ptr<IDriver>(new OpenKeyShimDriver("openkey-2.0.5", okLibPath("205")));
        case Which::OpenKeyMaster:
            return std::unique_ptr<IDriver>(new OpenKeyShimDriver("openkey-master", okLibPath("master")));
        case Which::UniKey: return std::unique_ptr<IDriver>(new UniKeyDriver("unikey-4.x"));
        case Which::KieeKeyBase:
            return std::unique_ptr<IDriver>(new KieeKeyShimDriver("kieekey-base", kkLibPath("base")));
        case Which::KieeKeyCand:
            return std::unique_ptr<IDriver>(new KieeKeyShimDriver("kieekey-cand", kkLibPath("cand")));
    }
    return nullptr;
}

}  // namespace bench

