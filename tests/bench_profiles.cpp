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
// File: tests/bench_profiles.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// bench_profiles — v1.2.1 RC2 Performance Preference Profiles, measured.
//
// Drives the SAME shim pipeline shape as tests/e2e_bench.cpp (producer =
// engine + decision on the hook-thread role; consumer = SPSC ring + adaptive
// spin + parked wake) but derives every tunable from ok::perf::Strategy so
// the reported numbers are the profiles' REAL behavioural effects:
//   * consumer spin floor/cap        → wake cost after word gaps
//   * ordering-barrier budget/spin   → pass-through stall while edits pend
//   * edit batch cap                 → commits per burst (LeastFlicker)
//   * output preference              → inline (no consumer hop) vs ring hop
//   * dictionaryRestore              → extra lexicon lookups at word breaks
//   * layoutRecheckEveryKey          → simulated per-key layout read
// Reports per profile: producer decision ns (p50/p99), end-to-end per-key
// latency µs (p50/p99/p99.9) for keys that took the consumer hop, barrier
// waits + timeouts, commits, wake count, CPU time consumed by the consumer
// (spin cost), and a "flicker proxy" = number of visible backspace+retype
// operations that hit the app (inline edits = visible, TSF commits = not).
//
// Usage: bench_profiles [--keys=N] [--runs=R] [--rate=HZ] [--json=path] [--profile=name]
//----------------------------------------------------------------------------
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "CpuPause.hpp"
#include "PerfProfile.hpp"
#include "TextEngine.hpp"
#include "win32_wrapper.hpp"

// Build (native):  g++ -std=c++20 -O2 -DOK_WRAP_NO_WIN32 -Isrc/core -Itests
//                      tests/bench_profiles.cpp src/core/win32_wrapper.cpp
//                      src/core/TextEngine.cpp -lpthread
// Build (Windows): same file WITHOUT OK_WRAP_NO_WIN32 (real QPC/SendInput
//                  shim types come from <windows.h> via win32_wrapper.hpp).
#if defined(_WIN32) && defined(OK_WRAP_NO_WIN32)
static double threadCpuMs() { return 0; }   // shim-on-Windows: no CPU clock without <windows.h>
#elif !defined(_WIN32)
#include <time.h>
static double threadCpuMs() {
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
#else
static double threadCpuMs() {
    FILETIME c, e, k, u;
    if (!::GetThreadTimes(::GetCurrentThread(), &c, &e, &k, &u)) return 0;
    ULARGE_INTEGER ku{}, uu{};
    ku.LowPart = k.dwLowDateTime; ku.HighPart = k.dwHighDateTime;
    uu.LowPart = u.dwLowDateTime; uu.HighPart = u.dwHighDateTime;
    return (ku.QuadPart + uu.QuadPart) / 10000.0;
}
#endif

using namespace ok::text;
using ok::perf::Profile;
using ok::perf::Strategy;
using ok::perf::OutputPreference;
using Clock = std::chrono::steady_clock;

static std::uint64_t nowNs() noexcept {
    return static_cast<std::uint64_t>(Clock::now().time_since_epoch().count());
}

// Same corpus shape as e2e_bench: prose + backspace storms + correction-heavy words.
static const char* kPassage =
    "Hom nay troi dep qua, chung toi di dao quanh ho Tay roi ngoi uong ca phe va noi chuyen ve du an moi. "
    "xin chaof cacs banj tooi laf nguowif vieejt nam hoom nay trowif ddepj quas chungs ta ddi chowi nhes "
    "Tieng Viet co nhieu dau thanh: sac, huyen, hoi, nga, nang. cass caas huow thuowng dduwowcj khoong bieets ";
static const char* kStorm = "chugns\bx\b\bungs concho\b\b\b\bong moongs\b\b\bing ";

static std::string buildStream(long keys) {
    std::string s;
    s.reserve(static_cast<std::size_t>(keys) + 256);
    long seg = 0;
    while (static_cast<long>(s.size()) < keys) {
        s += ((seg++ % 7) == 3) ? kStorm : kPassage;
    }
    s.resize(static_cast<std::size_t>(keys));
    return s;
}

struct EditDelta { std::size_t backspace = 0; std::wstring text; };   // mirror of ok::tsf::EditDelta

struct Result {
    std::string profile;
    Strategy st;
    std::vector<std::uint64_t> decisionNs;    // producer-side engine+decision
    std::vector<std::uint64_t> e2eNs;         // keys that went through the consumer
    std::vector<std::uint64_t> barrierNs;     // pass-through barrier waits
    std::uint64_t barrierTimeouts = 0, barrierWaits = 0;
    std::uint64_t commits = 0, editsRing = 0, editsInline = 0, wakes = 0, passthrough = 0;
    double consumerCpuMs = 0, wallMs = 0;
    std::uint64_t visibleRetypes = 0;   // flicker proxy
};

static double pct(std::vector<std::uint64_t>& v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return static_cast<double>(v[static_cast<std::size_t>(p * static_cast<double>(v.size() - 1))]);
}

struct Pipeline {
    using Ring = ok::lockfree::SPSCRing<std::uint64_t, 4096>;   // serial of ring edits
    ok::wrap::OutputRing outRing;
    Ring wake;
    std::atomic<bool> running{true}, parked{false}, evt{false};
    std::mutex m; std::condition_variable cv;
    const Strategy& st;
    Result& res;
    ok::wrap::EditDrainBarrier barrier;
    ok::wrap::PendingEditCounter pending{barrier};
    std::vector<std::uint64_t> t0;          // per serial
    std::vector<std::uint64_t> done;
    std::vector<EditDelta> batch;  // commit batch (simulated)
    std::uint64_t batchStartNs = 0;

    Pipeline(const Strategy& s, Result& r, std::size_t n) : st(s), res(r), t0(n), done(n) {
#ifndef BENCH_RC1_ENGINE   // RC1 barrier has fixed tuning (== Balanced); used for the RC1/default row
        barrier.setTuning((s.barrierBudgetUs + 999) / 1000, s.barrierSpinIters);
#endif
    }

    void consumerLoop() {
        std::uint64_t serials[Ring::capacity];
        std::uint64_t lastArrivalUs = nowNs() / 1000, ewma = 20;
        const std::uint64_t floorUs = st.consumerSpinFloorUs, capUs = std::max<std::uint32_t>(st.consumerSpinFloorUs, st.consumerSpinCapUs);
        auto flush = [&] {
            if (batch.empty()) return;
            // Simulated TSF commit: a fixed ~3 µs cost per session (the
            // RequestEditSession round trip on a responsive app) — identical
            // for every profile, so the profiles differ only in HOW MANY.
            const std::uint64_t t = nowNs();
            while (nowNs() - t < 3000) { ok::cpu::pause(); }
            ++res.commits;
            batch.clear();
        };
        while (running.load(std::memory_order_acquire) || !wake.empty()) {
            const std::size_t n = wake.try_pop_batch(serials, Ring::capacity);
            if (n) {
                const std::uint64_t now = nowNs() / 1000, gap = now - lastArrivalUs;
                lastArrivalUs = now;
                if (gap > 0 && gap < 1'000'000) {
                    ewma = gap < ewma ? ewma - (ewma - gap) / 4 : ewma + (gap - ewma) / 16;
                }
                for (std::size_t i = 0; i < n; ++i) {
                    ok::wrap::OutputItem it;
                    if (outRing.try_pop(it)) {
                        if (it.kind == ok::wrap::OutputItem::Kind::Edit) {
                            batch.push_back(EditDelta{it.backspace, std::wstring(it.text, it.text + it.textLen)});
                            if (batch.size() >= st.editBatchMax) { flush(); }
                        } else {   // deferred inline edit: flush TSF first, then "SendInput"
                            flush();
                            ++res.visibleRetypes;
                        }
                        pending.consume(1);
                    }
                    if (serials[i] < done.size()) done[serials[i]] = nowNs();
                }
                continue;
            }
            flush();   // burst ended: commit what we have
            const std::uint64_t nowIdle = nowNs() / 1000;
            if (nowIdle - lastArrivalUs >= 50'000) ewma = floorUs;
            const std::uint64_t budget = std::clamp<std::uint64_t>(ewma + ewma / 4, floorUs, capUs);
            const std::uint64_t spinStart = nowIdle;
            while (wake.empty() && (nowNs() / 1000) - spinStart < budget) {
                for (int s = 0; s < 64; ++s) ok::cpu::pause();
            }
            if (wake.empty() && running.load(std::memory_order_acquire)) {
                parked.store(true, std::memory_order_release);
                if (!wake.empty() || !running.load(std::memory_order_acquire)) { parked.store(false); continue; }
                std::unique_lock<std::mutex> lk(m);
                if (wake.empty() && running.load(std::memory_order_acquire)) {
                    ++res.wakes;
                    cv.wait(lk, [&] { return evt.load(std::memory_order_acquire) || !wake.empty() || !running.load(); });
                }
                parked.store(false, std::memory_order_release);
            }
            evt.store(false, std::memory_order_release);
        }
        flush();
        res.consumerCpuMs = threadCpuMs();
    }

    void push(const ok::wrap::OutputItem& it, std::uint64_t serial) {
        pending.publish(1);
        while (!outRing.try_push(it)) {}
        while (!wake.try_push(serial)) {}
        evt.store(true, std::memory_order_release);
        if (parked.load(std::memory_order_acquire)) cv.notify_one();
    }
};

static long g_rateHz = 0;   // 0 = unpaced burst; else keys/s (spin-paced, like e2e --paced)

static Result runProfile(Profile p, std::uint8_t hybrid, const std::string& stream, const char* label) {
    Result r;
    r.profile = label;
    r.st = ok::perf::resolveStrategy(p, hybrid, ok::perf::Telemetry{});
    const Strategy& st = r.st;

    EngineOptions o;
    o.restoreIfWrongSpelling = st.restoreIfWrongSpelling;
    o.useDictionaryRestore = st.dictionaryRestore;
    TextEngine eng(o);
    static const char* lex[] = {"hom","nay","troi","dep","qua","chung","toi","di","dao","quanh","ho","tay",
        "roi","ngoi","uong","ca","phe","va","noi","chuyen","ve","du","an","moi","tieng","viet","co","nhieu",
        "dau","thanh","sac","huyen","hoi","nga","nang","nguoi","dung","go","nhanh","xin","chào","các","bạn",
        "tôi","là","người","việt","nam","hôm","trời","đẹp","quá","chúng","ta","đi","chơi","nhé","cá","cả",
        "hươ","thương","được","không","biết","ung","ong","ing","concho","moong"};
    eng.setDictionaryResolver([](const std::vector<std::uint32_t>& w) {
        thread_local std::string s; s.clear();
        for (std::uint32_t c : w) {
            if (c < 0x80) s += static_cast<char>(c);
            else if (c < 0x800) { s += static_cast<char>(0xC0 | (c >> 6)); s += static_cast<char>(0x80 | (c & 0x3F)); }
            else { s += static_cast<char>(0xE0 | (c >> 12)); s += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); s += static_cast<char>(0x80 | (c & 0x3F)); }
        }
        for (const char* x : lex) if (s == x) return true;
        return false;
    });

    Pipeline pipe(st, r, stream.size());
    std::thread consumer([&] { pipe.consumerLoop(); });

    // Output routing per strategy: Auto → the e2e assumption (TSF-capable
    // app, i.e. ring hop) for 50 % of "apps" — we alternate per 1000 keys
    // to model app switching; Inline → all inline; Tsf → all ring.
    std::wstring rep;
    std::vector<std::uint64_t> decision; decision.reserve(stream.size());
    std::vector<std::uint64_t> barrierWaits; barrierWaits.reserve(stream.size() / 4);
    volatile std::uint32_t layoutSink = 0;
    const std::uint64_t wall0 = nowNs();
    const std::uint64_t periodNs = g_rateHz > 0 ? 1'000'000'000ull / static_cast<std::uint64_t>(g_rateHz) : 0;
    std::uint64_t nextDue = nowNs();
    for (std::size_t i = 0; i < stream.size(); ++i) {
        const char c = stream[i];
        if (periodNs) {
            // realistic cadence: words separated by a longer pause (5x) so the
            // consumer actually parks and wake cost is exercised per profile
            nextDue += (c == ' ') ? periodNs * 5 : periodNs;
            while (nowNs() < nextDue) { ok::cpu::pause(); }
        }
        const std::uint64_t t0 = nowNs();
        pipe.t0[i] = t0;
        if (st.layoutRecheckEveryKey) { layoutSink = layoutSink * 1664525u + 1013904223u; }   // ~2 reads
        TextInput in;
        if (c == ' ') in.kind = InputKind::Space;
        else if (c == '\b') in.kind = InputKind::Backspace;
        else { in.kind = InputKind::Char; in.ch = static_cast<char32_t>(c); }
        const EngineResult& res = eng.process(in);
        eng.replacementUtf16(res, rep);
        bool suppress = false; std::size_t bs = 0;
        if (res.code == EngineCode::ReplaceMacro) { suppress = true; bs = res.backspaceCount; eng.macroExpansionUtf16(res, rep); }
        else if (res.consumed() && !(res.backspaceCount == 0 && rep.empty())) { suppress = true; bs = res.backspaceCount; }
        decision.push_back(nowNs() - t0);
        if (!suppress) {
            ++r.passthrough;
            if (pipe.pending.pending() != 0) {
                const std::uint64_t b0 = nowNs();
                const bool ok = pipe.pending.waitDrained();
                barrierWaits.push_back(nowNs() - b0);
                ++r.barrierWaits;
                if (!ok) ++r.barrierTimeouts;
            }
            continue;
        }
        bool useTsf;
        switch (st.output) {
            case OutputPreference::Inline: useTsf = false; break;
            case OutputPreference::Tsf:    useTsf = true;  break;
            default:                       useTsf = ((i / 1000) % 2) == 0; break;
        }
        ok::wrap::OutputItem it;
        it.backspace = static_cast<std::uint32_t>(bs);
        const std::size_t n = std::min<std::size_t>(rep.size(), std::size(it.text));
        it.textLen = static_cast<std::uint32_t>(n);
        for (std::size_t k = 0; k < n; ++k) it.text[k] = rep[k];
        if (useTsf) {
            it.kind = ok::wrap::OutputItem::Kind::Edit;
            ++r.editsRing;
            pipe.push(it, i);
        } else if (st.deferInlineToConsumer) {
            it.kind = ok::wrap::OutputItem::Kind::InlineEdit;
            ++r.editsRing;
            pipe.push(it, i);
        } else {
            // In-callback SendInput: no hop; a visible backspace+retype.
            ++r.editsInline;
            ++r.visibleRetypes;
            pipe.done[i] = nowNs();
        }
    }
    pipe.running.store(false, std::memory_order_release);
    { std::lock_guard<std::mutex> lk(pipe.m); pipe.evt.store(true); }
    pipe.cv.notify_one();
    consumer.join();
    r.wallMs = (nowNs() - wall0) / 1e6;
    r.decisionNs = std::move(decision);
    r.barrierNs = std::move(barrierWaits);
    for (std::size_t i = 0; i < stream.size(); ++i) {
        if (pipe.done[i] && pipe.done[i] >= pipe.t0[i]) r.e2eNs.push_back(pipe.done[i] - pipe.t0[i]);
    }
    return r;
}

int main(int argc, char** argv) {
    long keys = 200000, runs = 3;
    std::string jsonPath, only;
    for (int i = 1; i < argc; ++i) {
        if (!std::strncmp(argv[i], "--keys=", 7)) keys = std::atol(argv[i] + 7);
        else if (!std::strncmp(argv[i], "--runs=", 7)) runs = std::atol(argv[i] + 7);
        else if (!std::strncmp(argv[i], "--json=", 7)) jsonPath = argv[i] + 7;
        else if (!std::strncmp(argv[i], "--profile=", 10)) only = argv[i] + 10;
        else if (!std::strncmp(argv[i], "--rate=", 7)) g_rateHz = std::atol(argv[i] + 7);
        else { std::fprintf(stderr, "unknown arg %s\n", argv[i]); return 2; }
    }
    const std::string stream = buildStream(keys);
    struct P { const char* name; Profile p; std::uint8_t h; } profiles[] = {
        {"Balanced",        Profile::Balanced,       ok::perf::kHybridNone},
        {"Fastest",         Profile::Fastest,        ok::perf::kHybridNone},
        {"LeastFlicker",    Profile::LeastFlicker,   ok::perf::kHybridNone},
        {"MaxCorrectness",  Profile::MaxCorrectness, ok::perf::kHybridNone},
        {"Adaptive",        Profile::Adaptive,       ok::perf::kHybridNone},
        {"Fastest+Dict",    Profile::Fastest,        ok::perf::kHybridExtraCorrect},
        {"LeastFlicker+LowCpu", Profile::LeastFlicker, ok::perf::kHybridLowCpu},
    };
    std::printf("[profiles] keys=%ld runs=%ld rate=%ld/s (0=unpaced burst; median run by e2e p50)\n", keys, runs, g_rateHz);
    std::printf("%-20s %8s %8s | %9s %9s %9s | %7s %7s %6s | %7s %8s %8s | %8s\n",
                "profile", "dec p50", "dec p99", "e2e p50", "e2e p99", "e2e p999",
                "barrier", "b p99", "b t/o", "commits", "wakes", "retypes", "cCPU ms");
    std::ofstream js;
    if (!jsonPath.empty()) { js.open(jsonPath); js << "{\n  \"keys\": " << keys << ", \"runs\": " << runs << ", \"rate_hz\": " << g_rateHz << ",\n  \"profiles\": [\n"; }
    bool first = true;
    for (const auto& pr : profiles) {
        if (!only.empty() && only != pr.name) continue;
        std::vector<Result> rs;
        for (long r = 0; r < runs; ++r) rs.push_back(runProfile(pr.p, pr.h, stream, pr.name));
        // median by e2e p50
        std::vector<std::pair<double, std::size_t>> order;
        for (std::size_t i = 0; i < rs.size(); ++i) order.push_back({pct(rs[i].e2eNs, 0.5), i});
        std::sort(order.begin(), order.end());
        Result& m = rs[order[order.size() / 2].second];
        const double d50 = pct(m.decisionNs, 0.5), d99 = pct(m.decisionNs, 0.99);
        const double e50 = pct(m.e2eNs, 0.5) / 1000, e99 = pct(m.e2eNs, 0.99) / 1000, e999 = pct(m.e2eNs, 0.999) / 1000;
        const double b50 = pct(m.barrierNs, 0.5) / 1000, b99 = pct(m.barrierNs, 0.99) / 1000;
        std::printf("%-20s %8.0f %8.0f | %9.2f %9.2f %9.2f | %7.2f %7.2f %6llu | %7llu %8llu %8llu | %8.1f\n",
                    pr.name, d50, d99, e50, e99, e999, b50, b99,
                    (unsigned long long)m.barrierTimeouts, (unsigned long long)m.commits,
                    (unsigned long long)m.wakes, (unsigned long long)m.visibleRetypes, m.consumerCpuMs);
        if (js) {
            js << (first ? "" : ",\n") << "    {\"profile\": \"" << pr.name << "\", "
               << "\"output\": " << static_cast<int>(m.st.output) << ", \"spinCapUs\": " << m.st.consumerSpinCapUs
               << ", \"barrierBudgetUs\": " << m.st.barrierBudgetUs << ", \"editBatchMax\": " << m.st.editBatchMax
               << ", \"dictionaryRestore\": " << (m.st.dictionaryRestore ? "true" : "false")
               << ", \"decision_p50_ns\": " << d50 << ", \"decision_p99_ns\": " << d99
               << ", \"e2e_p50_us\": " << e50 << ", \"e2e_p99_us\": " << e99 << ", \"e2e_p999_us\": " << e999
               << ", \"barrier_p50_us\": " << b50 << ", \"barrier_p99_us\": " << b99
               << ", \"barrier_waits\": " << m.barrierWaits << ", \"barrier_timeouts\": " << m.barrierTimeouts
               << ", \"commits\": " << m.commits << ", \"edits_ring\": " << m.editsRing << ", \"edits_inline\": " << m.editsInline
               << ", \"wakes\": " << m.wakes << ", \"visible_retypes\": " << m.visibleRetypes
               << ", \"consumer_cpu_ms\": " << m.consumerCpuMs << ", \"wall_ms\": " << m.wallMs << "}";
            first = false;
        }
    }
    if (js) { js << "\n  ]\n}\n"; }
    return 0;
}
