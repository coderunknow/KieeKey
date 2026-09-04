//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.1 RC2 - refactored and completed logic
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
// File: tests/stress_rc2.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// stress_rc2 — v1.2.1 RC2 real-world hardening battery.
//
// Scenarios (each an independent check; exit 0 only if all pass):
//   1. burst           — 200k keys, no pauses, prose+tone-heavy; A/B against
//                        a second engine fed the same stream one key at a
//                        time with periodic option round-trips (determinism).
//   2. sustained       — 2M keys, RSS/alloc growth must be flat after warm-up.
//   3. correction-heavy— backspace storms + Restore + quick-Telex expansion
//                        with QuickTelexDetector observing on the hot path;
//                        detector must never raise on legit words and must
//                        raise (bounded) on repeated undo-after-expansion.
//   4. mixed           — random mode switching (Telex<->VNI, spelling on/off,
//                        quickTelex on/off, modern orthography, code table)
//                        mid-word; invariants: buffer bounded, replacement
//                        len <= 2*kMaxBuff, backspaceCount <= buffered chars,
//                        no crash, DoNothing on control keys.
//   5. unicode/emoji   — astral code points, combining marks, CJK, RTL,
//                        ZWJ sequences: pass-through, never consumed, never
//                        corrupt the following Vietnamese word.
//   6. long-session    — 30 min simulated wall-clock at 8 keys/s in fast
//                        forward through PerfProfile Adaptive re-resolution
//                        + NotificationCenter rate limits: total presented
//                        notifications must respect per-hour caps; no
//                        pending-slot leak; suppression persists.
//   7. concurrent      — hook-thread style producer feeding the detector
//                        while a UI thread polls takePending/resolve at 1 ms:
//                        no torn state (TSAN-clean design; here: invariants).
//----------------------------------------------------------------------------
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "Notifications.hpp"
#include "PerfProfile.hpp"
#include "TextEngine.hpp"

#if defined(__linux__)
#include <fstream>
static long rssKb() {
    std::ifstream f("/proc/self/status");
    std::string l;
    while (std::getline(f, l)) {
        if (l.rfind("VmRSS:", 0) == 0) { return std::atol(l.c_str() + 6); }
    }
    return -1;
}
#else
static long rssKb() { return -1; }
#endif

static std::atomic<std::uint64_t> g_allocs{0};
#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
void* operator new(std::size_t n) {
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) { return p; }
    std::abort();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif

using namespace ok::text;
using ok::notify::NotificationCenter;
using ok::notify::QuickTelexDetector;
namespace notify = ok::notify;

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static TextInput mk(char c) {
    TextInput in;
    if (c == ' ') { in.kind = InputKind::Space; }
    else if (c == '\b') { in.kind = InputKind::Backspace; }
    else if (c == '\n') { in.kind = InputKind::WordBreak; in.vkCode = 0x0D; }
    else { in.kind = InputKind::Char; in.ch = static_cast<char32_t>(static_cast<unsigned char>(c)); }
    return in;
}

static const char* kProse =
    "xin chaof cacs banj tooi laf nguowif vieejt nam hoom nay trowif ddepj quas chungs ta ddi chowi nhes "
    "ddaay laf moojt ddoanj vawn banr dduwowcj gox bawngf telex ddeer kieerm tra hieeju nawng "
    "thuwowngf thif ngwowif duungf gox raats nhanh vaf hay sai chinhs tar rooif suwar lai ";
static const char* kStorm = "chugns\b\b\b\bungs concho\b\b\b\bong moongs\b\b\bing ddd\b\bd owow\b\bw ";

struct Invariants {
    std::uint64_t keys = 0, consumed = 0, maxBs = 0, maxRep = 0;
    void observe(const EngineResult& r, const std::wstring& rep) {
        ++keys;
        if (r.consumed()) { ++consumed; }
        maxBs = std::max<std::uint64_t>(maxBs, r.backspaceCount);
        maxRep = std::max<std::uint64_t>(maxRep, rep.size());
        CHECK(r.backspaceCount <= kMaxBuff, "backspaceCount %u > kMaxBuff", (unsigned)r.backspaceCount);
        CHECK(rep.size() <= 2 * kMaxBuff + 1, "replacement %zu too long", rep.size());
    }
};

// 1. burst + determinism ---------------------------------------------------
static void scenarioBurst() {
    std::printf("[1] burst 200k keys, determinism A vs B (B round-trips options every 997 keys)\n");
    EngineOptions o; o.quickTelex = true;
    TextEngine a(o), b(o);
    std::wstring ra, rb;
    Invariants inv;
    std::string s;
    while (s.size() < 200000) { s += kProse; s += kStorm; }
    std::uint64_t mism = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const TextInput in = mk(s[i]);
        const EngineResult& x = a.process(in);
        a.replacementUtf16(x, ra);
        if (i % 997 == 0) { b.setOptions(b.options()); }   // no-op round trip must not perturb
        const EngineResult& y = b.process(in);
        b.replacementUtf16(y, rb);
        inv.observe(x, ra);
        if (x.code != y.code || x.backspaceCount != y.backspaceCount || ra != rb) { ++mism; }
    }
    CHECK(mism == 0, "A/B mismatches: %llu", (unsigned long long)mism);
    std::printf("    keys=%llu consumed=%llu maxBs=%llu maxRep=%llu mismatches=%llu\n",
                (unsigned long long)inv.keys, (unsigned long long)inv.consumed,
                (unsigned long long)inv.maxBs, (unsigned long long)inv.maxRep, (unsigned long long)mism);
}

// 2. sustained + leak -------------------------------------------------------
static void scenarioSustained(std::uint64_t keys) {
    std::printf("[2] sustained %llu keys, allocation + RSS flatness\n", (unsigned long long)keys);
    EngineOptions o; o.useDictionaryRestore = true;
    TextEngine e(o);
    e.setDictionaryResolver([](const std::vector<std::uint32_t>& w) { return w.size() % 3 == 0; });
    e.setMacroResolver([](const std::vector<std::uint32_t>& w, std::vector<std::uint32_t>& out) {
        if (w.size() == 4 && w[0] == 'v' && w[1] == 'n' && w[2] == 'x' && w[3] == 'x') { out = {0x56, 0x69, 0x1EC7, 0x74}; return true; }
        return false;
    });
    std::wstring rep;
    std::string s;
    while (s.size() < 8192) { s += kProse; s += kStorm; s += "vnxx "; }
    std::uint64_t k = 0;
    // warm-up 10%
    const std::uint64_t warm = keys / 10;
    std::uint64_t allocAfterWarm = 0; long rssAfterWarm = 0;
    while (k < keys) {
        for (std::size_t i = 0; i < s.size() && k < keys; ++i, ++k) {
            const EngineResult& r = e.process(mk(s[i]));
            e.replacementUtf16(r, rep);
            if (r.code == EngineCode::ReplaceMacro) { e.macroExpansionUtf16(r, rep); }
            if (k == warm) { allocAfterWarm = g_allocs.load(); rssAfterWarm = rssKb(); }
        }
    }
    const std::uint64_t allocDelta = g_allocs.load() - allocAfterWarm;
    const long rssDelta = rssKb() - rssAfterWarm;
    std::printf("    allocs after warm-up: %llu over %llu keys (%.4f/key); RSS delta %ld kB\n",
                (unsigned long long)allocDelta, (unsigned long long)(keys - warm),
                static_cast<double>(allocDelta) / static_cast<double>(keys - warm), rssDelta);
    // Dictionary resolver + macro expansion legitimately allocate (vector/wstring
    // per word break); the bound is "per word", never "growing".
    CHECK(static_cast<double>(allocDelta) / static_cast<double>(keys - warm) < 0.5, "allocation rate too high");
    CHECK(rssDelta < 2048, "RSS grew %ld kB during sustained run", rssDelta);
}

// 3. correction-heavy + QuickTelex detector --------------------------------
static void scenarioCorrectionHeavy() {
    std::printf("[3] correction-heavy: backspace storms, restore, quick-Telex undo detection\n");
    EngineOptions o; o.quickTelex = true; o.restoreIfWrongSpelling = true;
    TextEngine e(o);
    std::wstring rep;
    QuickTelexDetector det;
    NotificationCenter nc;
    std::uint64_t now = 1'000'000;
    // (a) legit Vietnamese with quickTelex ON — expansions the user keeps.
    //     'cc'→'ch', 'gg'→'gi', 'kk'→'kh', 'nn'→'ng', 'qq'→'qu', 'pp'→'ph', 'tt'→'th'
    std::string legit;
    for (int i = 0; i < 400; ++i) { legit += "ccungs ttoi kkoong ppair nnuwowif "; }
    std::uint64_t falseRaises = 0;
    for (char c : legit) {
        const EngineResult& r = e.process(mk(c));
        e.replacementUtf16(r, rep);
        now += 120;
        if (c == ' ') { det.observeWordCommitted(now); if (det.maybeRaise(nc, now)) { ++falseRaises; } }
        else if (r.consumed() && r.backspaceCount == 1 && rep.size() == 2 && c != 's' && c != 'f') { det.observeExpansion(static_cast<char32_t>(c), now); }
        else if (c == '\b') { det.observeBackspace(now); }
        else { det.observeRawChar(now); }
    }
    CHECK(falseRaises == 0, "detector raised %llu times on legit quick-Telex prose", (unsigned long long)falseRaises);
    // (b) unwanted: user types English "app", "ppt", "http" — expansion then
    //     immediate backspace + retype the doubled letter. Must raise (once).
    std::uint64_t raises = 0;
    for (int i = 0; i < 12; ++i) {
        const char* w = (i % 3 == 0) ? "app" : (i % 3 == 1) ? "ppt" : "http";
        for (const char* p = w; *p; ++p) {
            const EngineResult& r = e.process(mk(*p));
            e.replacementUtf16(r, rep);
            now += 150;
            if (r.consumed() && r.backspaceCount == 1 && rep.size() == 2) {
                det.observeExpansion(static_cast<char32_t>(*p), now);
                // user notices, backspaces the expansion and retypes raw
                (void)e.process(mk('\b')); det.observeBackspace(now += 300);
                (void)e.process(mk('\b')); det.observeBackspace(now += 100);
                det.observeRawChar(now += 200); det.observeRawChar(now += 100);
            } else { det.observeRawChar(now); }
        }
        (void)e.process(mk(' ')); det.observeWordCommitted(now += 200);
        if (det.maybeRaise(nc, now)) { ++raises; }
    }
    const notify::Notification n = nc.takePending(now);
    CHECK(n.id == notify::Id::QuickTelexUnwanted, "expected QuickTelexUnwanted pending, got id=%d", (int)n.id);
    CHECK(raises >= 1, "detector never raised on 12 unwanted expansions");
    // Second raise within the cooldown must be blocked (no spam).
    std::uint64_t spam = 0;
    for (int i = 0; i < 50; ++i) {
        det.observeExpansion(U'p', now += 100); det.observeBackspace(now += 100); det.observeBackspace(now += 50);
        det.observeRawChar(now += 50); det.observeWordCommitted(now += 100);
        if (det.maybeRaise(nc, now) && nc.takePending(now).id != notify::Id::None) { ++spam; }
    }
    CHECK(spam == 0, "notification re-presented %llu times inside cooldown", (unsigned long long)spam);
    std::printf("    legit false raises=%llu, unwanted raises=%llu, in-cooldown re-presents=%llu\n",
                (unsigned long long)falseRaises, (unsigned long long)raises, (unsigned long long)spam);
}

// 4. mixed mode switching ---------------------------------------------------
static void scenarioMixed(std::uint64_t keys, std::uint32_t seed) {
    std::printf("[4] mixed: random mode switching mid-word, %llu keys, seed %u\n", (unsigned long long)keys, seed);
    std::mt19937 rng(seed);
    EngineOptions o;
    TextEngine e(o);
    std::wstring rep;
    Invariants inv;
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzaeiouwsfrxjdd  \b\b\n0123456789.,ADW";
    std::uint64_t switches = 0, ctrlConsumed = 0, ctrlRestoreGuarded = 0;
    std::string trail;   // last 40 keys for diagnosis
    for (std::uint64_t k = 0; k < keys; ++k) {
        if ((rng() & 1023) == 0) {
            ++switches;
            switch (rng() % 7) {
                case 0: o.inputMethod = (o.inputMethod == InputMethod::Telex) ? InputMethod::Vni : InputMethod::Telex; break;
                case 1: o.checkSpelling = !o.checkSpelling; break;
                case 2: o.quickTelex = !o.quickTelex; break;
                case 3: o.useModernOrthography = !o.useModernOrthography; break;
                case 4: o.codeTable = static_cast<CodeTable>((static_cast<int>(o.codeTable) + 1) % 4); break;
                case 5: o.freeMark = !o.freeMark; break;
                default: o.restoreIfWrongSpelling = !o.restoreIfWrongSpelling; break;
            }
            e.setOptions(o);
        }
        char c = alphabet[rng() % (sizeof(alphabet) - 1)];
        TextInput in = mk(c);
        if ((rng() & 63) == 0) { in.otherCtrl = true; }   // Ctrl+key mid-word
        if ((rng() & 127) == 0) { in.kind = InputKind::MouseDown; }
        const EngineResult& r = e.process(in);
        e.replacementUtf16(r, rep);
        inv.observe(r, rep);
        trail += (in.kind == InputKind::MouseDown) ? 'M' : (in.otherCtrl ? '^' : ' ');
        trail += (c == '\b') ? '<' : (c == '\n') ? '|' : c;
        if (trail.size() > 80) trail.erase(0, trail.size() - 80);
        // BUG #3 (RC2): the engine may answer a Ctrl/Alt chord with a Restore
        // (word finalised as non-Vietnamese). The hook must NOT consume the
        // chord — main.cpp's shortcutRestoreEvent guard turns it into a
        // pass-through + startNewSession. Mirror that contract here: any
        // OTHER consumed decision on a chord is a defect.
        if (in.otherCtrl && (r.code == EngineCode::Restore || r.code == EngineCode::RestoreAndStartNewSession)) {
            ++ctrlRestoreGuarded;
            e.startNewSession();
        } else if (in.otherCtrl && in.kind == InputKind::Char && r.consumed() && r.code != EngineCode::BreakWord) {
            ++ctrlConsumed;
            std::printf("    ctrl+'%c' consumed: code=%d bs=%u rep=%zu (telex=%d spell=%d qt=%d restore=%d free=%d ct=%d) trail=[%s]\n", c, (int)r.code, r.backspaceCount, rep.size(),
                        o.inputMethod == InputMethod::Telex, o.checkSpelling, o.quickTelex, o.restoreIfWrongSpelling, o.freeMark, (int)o.codeTable, trail.c_str());
        }
    }
    CHECK(ctrlConsumed == 0, "Ctrl+key was consumed %llu times", (unsigned long long)ctrlConsumed);
    std::printf("    switches=%llu consumed=%llu maxBs=%llu maxRep=%llu ctrlConsumed=%llu ctrlRestoreGuarded=%llu\n",
                (unsigned long long)switches, (unsigned long long)inv.consumed,
                (unsigned long long)inv.maxBs, (unsigned long long)inv.maxRep, (unsigned long long)ctrlConsumed,
                (unsigned long long)ctrlRestoreGuarded);
}

// 5. unicode / emoji --------------------------------------------------------
static void scenarioUnicode() {
    std::printf("[5] unicode/emoji pass-through and word isolation\n");
    EngineOptions o;
    TextEngine e(o);
    std::wstring rep;
    static const char32_t exotic[] = { 0x1F600, 0x1F469, 0x200D, 0x1F4BB, 0x0301, 0x4E2D, 0x6587, 0x05D0, 0x0627, 0xFE0F, 0x1F1FB, 0x1F1F3, 0x00E9, 0x1EC7 };
    std::uint64_t consumedExotic = 0;
    for (int rep_i = 0; rep_i < 200; ++rep_i) {
        for (char32_t cp : exotic) {
            TextInput in; in.kind = InputKind::Char; in.ch = cp;
            const EngineResult& r = e.process(in);
            e.replacementUtf16(r, rep);
            if (r.consumed() && r.code != EngineCode::BreakWord) { ++consumedExotic; }
        }
        // Documented behaviour (== RC1 == legacy 2.0.5): a foreign code point
        // GLUED to the following letters makes that word non-Vietnamese, so
        // "😀vieejt" is left raw (no partial composition, no data loss).
        std::wstring last; int consumedGlued = 0;
        for (char c : std::string("vieejt")) { const EngineResult& r = e.process(mk(c)); e.replacementUtf16(r, rep); if (r.consumed()) { ++consumedGlued; last = rep; } }
        CHECK(consumedGlued == 0, "glued word after exotic char was partially composed (rep='%ls')", last.c_str());
        (void)e.process(mk(' '));
        // After a separator the next word must compose normally: "vieejt " -> "việt".
        last.clear();
        for (char c : std::string("vieejt")) { const EngineResult& r = e.process(mk(c)); e.replacementUtf16(r, rep); if (r.consumed()) last = rep; }
        CHECK(last.find(L'\x1EC7') != std::wstring::npos, "word after exotic run + space lost tone (rep='%ls')", last.c_str());
        (void)e.process(mk(' '));
    }
    CHECK(consumedExotic == 0, "exotic code points consumed %llu times", (unsigned long long)consumedExotic);
    std::printf("    exotic consumed=%llu\n", (unsigned long long)consumedExotic);
}

// 6. long-session: notification caps + adaptive ----------------------------
static void scenarioLongSession() {
    std::printf("[6] long session: 3 h simulated, notification caps, adaptive re-resolution\n");
    NotificationCenter nc;
    std::uint64_t now = 10'000;
    std::uint64_t presented[static_cast<std::size_t>(notify::Id::kCount)] = {};
    ok::perf::Telemetry tel;
    std::uint64_t strategyChanges = 0;
    ok::perf::Strategy prev = ok::perf::resolveStrategy(ok::perf::Profile::Adaptive, ok::perf::kHybridNone, tel);
    for (std::uint64_t sec = 0; sec < 3 * 3600; ++sec) {
        now += 1000;
        // producer side: raise aggressively every second (worst case)
        nc.raise(notify::Id::QuickTelexUnwanted, 90, 5, now);
        nc.raise(notify::Id::HookReinstalled, 100, 1, now);
        nc.raise(notify::Id::BarrierTimeouts, 100, 3, now);
        nc.raise(notify::Id::TsfSlowDowngrade, 100, 1, now);
        // UI tick
        const notify::Notification n = nc.takePending(now);
        if (n.id != notify::Id::None) { ++presented[static_cast<std::size_t>(n.id)]; }
        // adaptive telemetry: alternate typing bursts and idle
        tel.recentKeysPerSec = (sec % 120 < 60) ? 8 : 0;
        tel.idleSeconds = (sec % 120 < 60) ? 0 : static_cast<std::uint32_t>(sec % 120 - 60);
        tel.barrierTimeouts += (sec % 600 == 0) ? 1 : 0;
        ok::perf::Strategy cur = ok::perf::resolveStrategy(ok::perf::Profile::Adaptive, ok::perf::kHybridNone, tel);
        if (cur.consumerSpinCapUs != prev.consumerSpinCapUs || cur.output != prev.output || cur.editBatchMax != prev.editBatchMax) { ++strategyChanges; prev = cur; }
        CHECK(cur.consumerSpinCapUs <= 200 && cur.editBatchMax >= 1 && cur.editBatchMax <= 64 && cur.barrierBudgetUs >= 1000 && cur.barrierBudgetUs <= 4000,
              "adaptive strategy out of bounds at sec %llu", (unsigned long long)sec);
    }
    const std::uint64_t q = presented[static_cast<std::size_t>(notify::Id::QuickTelexUnwanted)];
    const std::uint64_t h = presented[static_cast<std::size_t>(notify::Id::HookReinstalled)];
    const std::uint64_t b = presented[static_cast<std::size_t>(notify::Id::BarrierTimeouts)];
    const std::uint64_t t = presented[static_cast<std::size_t>(notify::Id::TsfSlowDowngrade)];
    std::printf("    presented over 3h: quickTelex=%llu hookReinstalled=%llu barrier=%llu tsfSlow=%llu; adaptive changes=%llu\n",
                (unsigned long long)q, (unsigned long long)h, (unsigned long long)b, (unsigned long long)t, (unsigned long long)strategyChanges);
    CHECK(q <= 2 * 3, "QuickTelex over cap (%llu)", (unsigned long long)q);
    CHECK(h <= 3 * 3, "HookReinstalled over cap (%llu)", (unsigned long long)h);
    CHECK(b <= 1, "BarrierTimeouts must be once per session (%llu)", (unsigned long long)b);
    CHECK(t <= 1 * 3, "TsfSlowDowngrade over cap (%llu)", (unsigned long long)t);
    CHECK(q + h + b + t <= 3 * 60, "global gap violated (%llu total)", (unsigned long long)(q + h + b + t));
    // suppression persists through a new center with the same store
    std::vector<std::pair<int, bool>> disk;
    notify::Store store;
    store.isSuppressed = [&](notify::Id id) { for (auto& kv : disk) if (kv.first == (int)id) return kv.second; return false; };
    store.setSuppressed = [&](notify::Id id, bool v) { disk.push_back({(int)id, v}); };
    NotificationCenter nc2(store);
    nc2.resolve(notify::Id::QuickTelexUnwanted, notify::Action::DontShowAgain);
    NotificationCenter nc3(store);
    nc3.loadSuppressions();
    CHECK(!nc3.raise(notify::Id::QuickTelexUnwanted, 100, 9, now + 1'000'000), "suppression did not persist");
}

// 7. concurrent producer/UI -------------------------------------------------
static void scenarioConcurrent() {
    std::printf("[7] concurrent: hot-path producer vs UI poller (2 s)\n");
    NotificationCenter nc;
    QuickTelexDetector det;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> produced{0}, presented{0}, resolved{0};
    std::thread producer([&] {
        std::uint64_t now = 1;
        while (!stop.load(std::memory_order_relaxed)) {
            det.observeExpansion(U'p', now); det.observeBackspace(now + 10); det.observeBackspace(now + 20);
            det.observeRawChar(now + 30); det.observeWordCommitted(now + 40);
            det.maybeRaise(nc, now + 40);
            nc.raise(notify::Id::HookReinstalled, 100, 1, now);
            now += 50'000;
            produced.fetch_add(1, std::memory_order_relaxed);
        }
    });
    std::thread ui([&] {
        std::uint64_t now = 1;
        while (!stop.load(std::memory_order_relaxed)) {
            const notify::Notification n = nc.takePending(now);
            if (n.id != notify::Id::None) {
                presented.fetch_add(1, std::memory_order_relaxed);
                CHECK(n.id < notify::Id::kCount && n.confidence <= 100, "torn notification");
                nc.resolve(n.id, (presented.load() % 3 == 0) ? notify::Action::Keep : notify::Action::TurnOff);
                resolved.fetch_add(1, std::memory_order_relaxed);
            }
            now += 50'000;
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });
    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop.store(true);
    producer.join(); ui.join();
    std::printf("    produced=%llu presented=%llu resolved=%llu\n",
                (unsigned long long)produced.load(), (unsigned long long)presented.load(), (unsigned long long)resolved.load());
    CHECK(presented.load() == resolved.load(), "presented != resolved");
}

int main(int argc, char** argv) {
    const std::uint64_t sustainedKeys = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 2'000'000ull;
    const std::uint32_t seed = argc > 2 ? static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10)) : 20260904u;
    std::printf("stress_rc2 — KieeKey v1.2.1 RC2 hardening battery\n");
    const auto t0 = std::chrono::steady_clock::now();
    scenarioBurst();
    scenarioSustained(sustainedKeys);
    scenarioCorrectionHeavy();
    scenarioMixed(500'000, seed);
    scenarioUnicode();
    scenarioLongSession();
    scenarioConcurrent();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%s (%.1f s, %d failures)\n", g_fail ? "STRESS_RC2 FAILED" : "STRESS_RC2 ALL PASSED", secs, g_fail);
    return g_fail ? 1 : 0;
}
