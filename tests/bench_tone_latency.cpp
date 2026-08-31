//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.0 - refactored and completed logic
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
// File: tests/bench_tone_latency.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.0 — tests/bench_tone_latency.cpp
// TONE-POPULATION LATENCY BENCHMARK (Phase 2 of the latency audit).
//
// WHY: the frozen v3.3.1 E2E workload (tests/e2e_bench.cpp) is 99.9%
// pass-through (108 edits / 100k keys — measured). Pass-through keys cost the
// pipeline nothing; the user-perceivable latency lives in the EDIT path —
// the tone marks (sắc/huyền/hỏi/ngã/nặng), d→đ, and the vowel
// transformations (circumflex/breve/horn). This bench makes EDITS the
// population and measures every pipeline stage of every key.
//
// POPULATIONS (all empirically validated against the REAL engine — the bench
// aborts if the engine's composition drifts from the frozen expectations):
//   tone-append     "as "      → á        1 tone key = 1 edit
//   dbar            "dd "      → đ        d→đ edit
//   reposition      "asa "     → ấ        tone moved onto the circumflex
//   restore-reissue "asas "    → âs       Restore + key re-issue path
//   double-tone     "oasas "   → oâs      double tone (S5 family)
//   mid-word        "ddawjng " → đặng     đ + ă + nặng inside one word
//   horn-compound   "uows "    → ướ       horn ươ composition + sắc
//   heavy-word      "dduowcj " → được     đ + ươ + nặng (the heaviest)
//   mixed           real dense-mark passage @ 80/150/250 WPM, interleaved
//
// OUTPUT PATHS (the shipped app's per-app policy):
//   inline          hook-side emitInline — the REAL InlineEmitter::sendEdit
//                   (ONE batched SendInput per edit; shim-recorded on POSIX,
//                   real SendInput on Windows). No consumer, no ring —
//                   exactly like the shipped inline mode.
//   inline-deferred the v3.4 S2 opt-in: the edit rides OutputItem::
//                   InlineEdit through the ring; the consumer emits.
//   tsf             TSF-simulated (real TSF needs the focused app; the
//                   sandbox cannot reach it — VERIFICATION_PROTOCOL.md
//                   covers real Word/Chrome runs). The consumer applies the
//                   REAL batching (kMaxEditBatch=32) and spends a
//                   configurable commit cost simulating the synchronous
//                   edit session (--commit=0|100us|1ms / --commit-ns=N).
//
// BARRIER MODES (S1 before/after):
//   spin            v3.3.1-faithful: bounded 200,000 × _mm_pause (~2 ms)
//                   busy-spin inside the "hook callback" whenever
//                   pendingEdits > 0, plus the always-SetEvent wake protocol.
//   hybrid          v3.4: the production EditDrainBarrier (~2 µs spin, then
//                   an event-based wait ≤ 1 ms) + the parked-flag wake
//                   protocol (S4): SetEvent only when the consumer parked.
//
// MEASURED STAGES (ns, from t0 = hook capture):
//   edits:  t1 decision · t2 pushed / emit issued · t3 dequeued ·
//           t4 flush start · t5 commit returned / SendInput issued.
//           E2E = t5 - t0 (inline path: E2E = t2 - t0 — there is no consumer).
//   pass:   t2 = delivery delay of the key the app sees, INCLUDING the
//           ordering-barrier stall (reported separately as barrier) —
//           the S1 measurement.
//
// The pipeline reuses the REAL production code: TextEngine, InlineEmitter,
// OutputRing (Vyukov SPSC 1024), the wake ring (4096), the adaptive consumer
// spin window (4–200 µs) and the v3.4 EditDrainBarrier. Only the TSF edit
// session itself is simulated (a bounded spin of the configured cost).
//----------------------------------------------------------------------------
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32) || defined(OK_WRAP_NO_WIN32)
#include <pthread.h>
#include <sched.h>
#endif

#include "Profiler.hpp"
#include "win32_wrapper.hpp"

using namespace ok::text;

namespace {

constexpr std::uint32_t kAbsent = ok::prof::kAbsent;

//---------------------------------------------------------------------------
// Populations
//---------------------------------------------------------------------------
struct PopDef {
    const char* name;
    const char* keys;               // the repeating unit (ends with a space)
    const unsigned* expect;         // expected composition of `keys` WITHOUT
    std::size_t expectLen;          // the trailing space (UTF-16 units)
    const char* desc;
};

// Expectations INCLUDE the trailing space of the repeating unit (the space
// is part of the composition: word-break or Restore+reissue). These are the
// production semantics measured with the bench lexicon — the mega
// differential freezes the engine, and this bench freezes the workload.
namespace expect {
constexpr unsigned kAs[]      = {0x00E1, 0x0020};                              // á+
constexpr unsigned kDd[]      = {0x0111, 0x0020};                              // đ+
constexpr unsigned kAsa[]     = {0x1EA5, 0x0020};                              // ấ+
constexpr unsigned kAsas[]    = {0x0061, 0x0073, 0x0061, 0x0020};              // asa+ (Restore chain: key4 Restore+reissue, space-Restore+reissue)
constexpr unsigned kOasas[]   = {0x006F, 0x0061, 0x0073, 0x0061, 0x0020};      // oasa+ (same chain on "oa")
constexpr unsigned kDdawjng[] = {0x0111, 0x1EB7, 0x006E, 0x0067, 0x0020};      // đặng+ (trailing ng re-typed as edits — 2.0.5 semantics)
constexpr unsigned kUows[]    = {0x01B0, 0x1EDB, 0x0020};                      // ướ+
constexpr unsigned kDduowcj[] = {0x0111, 0x01B0, 0x1EE3, 0x0063, 0x0020};      // được+
}

constexpr PopDef kPops[] = {
    {"tone-append",     "as ",      expect::kAs,      2, "as -> a-acute (1 tone key = 1 edit)"},
    {"dbar",            "dd ",      expect::kDd,      2, "dd -> d-stroke (d-bar edit)"},
    {"reposition",      "asa ",     expect::kAsa,     2, "asa -> a-circumflex-acute (sac reposition)"},
    {"restore-reissue", "asas ",    expect::kAsas,    4, "asas -> Restore chain: 2 edits + Restore-reissue + space-Restore-reissue"},
    {"double-tone",     "oasas ",   expect::kOasas,   5, "oasas -> double-tone family + Restore chain"},
    {"mid-word",        "ddawjng ", expect::kDdawjng, 5, "ddawjng -> dang-nang (d-bar + breve + nang; trailing ng re-typed)"},
    {"horn-compound",   "uows ",    expect::kUows,    3, "uows -> uo-horn + acute (horn compound + mark)"},
    {"heavy-word",      "dduowcj ", expect::kDduowcj, 5, "dduowcj -> duoc (d-bar + horn + nang: the heaviest)"},
};

// Real dense-mark passage (every marked word's composition validated
// word-by-word against the engine; plain words carry no mark keys).
constexpr const char* kMixedPassage =
    "hoofm nay troif ddepj qua, chungs tooi di ddaoj quanh hoof Taay. "
    "rooif ngooif ca phee va nooi vieetj nam co nhieeu dau thanh: "
    "sac, huyen, hoi, nga, nang. nguowif du an moi. ";

//---------------------------------------------------------------------------
// Per-key stage arrays (producer writes t1/t2/barrier, consumer writes
// t3/t4/t5 — split so no record is touched by two threads).
//---------------------------------------------------------------------------
struct ProdStages {
    std::uint32_t t1 = kAbsent;   // decision done
    std::uint32_t t2 = kAbsent;   // pushed / emit issued / delivery
    std::uint32_t barrierNs = 0;  // ordering-barrier time inside t2 (pass keys)
    bool isEdit = false;
    std::uint32_t bs = 0;
    std::uint32_t textLen = 0;
};
struct ConsStages {
    std::uint32_t t3 = kAbsent;   // dequeued
    std::uint32_t t4 = kAbsent;   // flush start
    std::uint32_t t5 = kAbsent;   // commit returned / SendInput issued
};

enum class Path { Inline, InlineDeferred, Tsf };
enum class Barrier { Spin, Hybrid };

constexpr std::size_t kMaxEditBatch = 32;   // same as the app

//---------------------------------------------------------------------------
// The pipeline. Mirrors the shipped ModernKeyHook + main.cpp structure with
// the production components.
//---------------------------------------------------------------------------
struct Pipeline {
    ok::wrap::OutputRing outRing;
    ok::wrap::InlineEmitter emitter{nullptr};
    ok::lockfree::SPSCRing<std::uint64_t, 4096> wakeRing;   // edit-key serials
    ok::wrap::EditDrainBarrier barrier;                     // v3.4 (shim flavor)

    Path path = Path::Inline;
    Barrier barrierMode = Barrier::Hybrid;
    std::uint64_t commitNs = 0;
    std::uint64_t spinCapUs = 200;

    TextEngine& eng;
    std::atomic<std::uint32_t> pendingEdits{0};
    std::atomic<bool> evt_{false};
    std::mutex wakeMtx;
    std::condition_variable wakeCv;
    std::atomic<bool> running{true};
    std::atomic<bool> parked{false};

    std::wstring repScratch;
    std::vector<std::uint64_t> startNs;      // per-key t0 (producer)
    std::vector<std::uint64_t> dequeNs;      // per-key t3 abs (consumer)
    std::vector<ProdStages>* prod = nullptr;
    std::vector<ConsStages>* cons = nullptr;

    struct BatchItem { std::uint32_t backspace; std::wstring text; std::uint64_t serial; };
    std::vector<BatchItem> batch;

    explicit Pipeline(TextEngine& e) : eng(e) {}

    static void pauseCpu() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
#else
        std::this_thread::yield();
#endif
    }
    static std::uint64_t nowNs() noexcept {
        return static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    }
    // v3.3.1-faithful bounded spin (kMaxPause = 200,000 × _mm_pause ≈ 2 ms)
    static void spinBarrierV331(const std::atomic<std::uint32_t>& pending) noexcept {
        constexpr int kMaxPause = 200'000;
        for (int i = 0; i < kMaxPause &&
                        pending.load(std::memory_order_relaxed) != 0; ++i) {
            pauseCpu();
        }
    }
    void simulateTsfCommit() noexcept {
        if (commitNs == 0) { return; }
        const std::uint64_t end = nowNs() + commitNs;
        while (nowNs() < end) { pauseCpu(); }
    }

    //-----------------------------------------------------------------------
    // PRODUCER — the hook-callback role.
    //-----------------------------------------------------------------------
    void produce(char c, std::uint64_t serial) {
        const std::uint64_t t0 = nowNs();
        startNs[serial] = t0;
        TextInput in;
        if (c == ' ') in.kind = InputKind::Space;
        else { in.kind = InputKind::Char; in.ch = static_cast<char32_t>(c); }

        const EngineResult& r = eng.process(in);
        eng.replacementUtf16(r, repScratch);
        bool suppress = false;
        std::size_t bs = 0;
        if (r.code == EngineCode::ReplaceMacro) {
            suppress = true;
            bs = r.backspaceCount;
            eng.macroExpansionUtf16(r, repScratch);
        } else if (r.consumed() && !(r.backspaceCount == 0 && repScratch.empty())) {
            suppress = true;
            bs = r.backspaceCount;
        }
        ProdStages& ps = (*prod)[serial];
        ps.isEdit = suppress;
        ps.bs = static_cast<std::uint32_t>(bs);
        ps.textLen = static_cast<std::uint32_t>(repScratch.size());
        ps.t1 = static_cast<std::uint32_t>(nowNs() - t0);   // t1: decision done

        if (!suppress) {
            // ---- ordering barrier (the S1 path; TSF/deferred paths only —
            // the shipped inline mode never increments pendingEdits) ----
            if (path != Path::Inline) {
                const std::uint64_t b0 = nowNs();
                if (barrierMode == Barrier::Spin) {
                    spinBarrierV331(pendingEdits);
                } else {
                    static_cast<void>(barrier.waitDrained(pendingEdits));
                }
                ps.barrierNs = static_cast<std::uint32_t>(nowNs() - b0);
            }
            ps.t2 = static_cast<std::uint32_t>(nowNs() - t0);
            return;
        }

        if (path == Path::Inline) {
            // hook-side emit (shipped default): ONE batched SendInput, no hop
            emitter.sendEdit(bs, repScratch);
            ps.t2 = static_cast<std::uint32_t>(nowNs() - t0);
            return;
        }

        // ring path (TSF / deferred-inline)
        ok::wrap::OutputItem it;
        it.kind = (path == Path::Tsf) ? ok::wrap::OutputItem::Kind::Edit
                                      : ok::wrap::OutputItem::Kind::InlineEdit;
        it.backspace = static_cast<std::uint32_t>(bs);
        const std::size_t n = std::min<std::size_t>(repScratch.size(),
                                                    std::size(it.text));
        it.textLen = static_cast<std::uint32_t>(n);
        for (std::size_t i = 0; i < n; ++i) { it.text[i] = repScratch[i]; }
        while (!outRing.try_push(it)) {}        // bench: never drop
        pendingEdits.fetch_add(1, std::memory_order_relaxed);
        while (!wakeRing.try_push(serial)) {}   // bench: never drop
        ps.t2 = static_cast<std::uint32_t>(nowNs() - t0);

        // wake protocol: spin mode always signals (v3.3.1); hybrid signals
        // only when the consumer is parked (v3.4 S4)
        if (barrierMode == Barrier::Spin || parked.load(std::memory_order_acquire)) {
            evt_.store(true, std::memory_order_release);
            wakeCv.notify_one();
        }
    }

    //-----------------------------------------------------------------------
    // CONSUMER — the consumerThreadMain + onConsumerEvent role.
    //-----------------------------------------------------------------------
    void flushBatch() {
        if (batch.empty()) { return; }
        const std::uint64_t tFlush = nowNs();
        if (path == Path::Tsf) {
            simulateTsfCommit();     // the simulated synchronous edit session
        } else {
            for (const BatchItem& b : batch) {   // deferred inline
                emitter.sendEdit(b.backspace, b.text);
            }
        }
        const std::uint64_t tDone = nowNs();
        for (const BatchItem& b : batch) {
            ConsStages& cs = (*cons)[b.serial];
            cs.t3 = static_cast<std::uint32_t>(dequeNs[b.serial] - startNs[b.serial]);
            cs.t4 = static_cast<std::uint32_t>(tFlush - startNs[b.serial]);
            cs.t5 = static_cast<std::uint32_t>(tDone - startNs[b.serial]);
        }
        const std::uint32_t n = static_cast<std::uint32_t>(batch.size());
        const std::uint32_t prev =
            pendingEdits.fetch_sub(n, std::memory_order_relaxed);
        if (prev == n && barrierMode == Barrier::Hybrid) {
            barrier.notifyDrained();     // v3.4: wake waiting pass-throughs
        }
        batch.clear();
    }

    void consumeLoop() {
        std::uint64_t serials[wakeRing.capacity];
        std::uint64_t lastArrivalUs = nowNs() / 1000;
        std::uint64_t gapWindow[32]{};
        std::size_t   gapIdx = 0;
        const auto pushGap = [&](std::uint64_t gap) {
            gapWindow[gapIdx & 31u] = gap;
            ++gapIdx;
        };
        const auto recentMaxGap = [&]() noexcept -> std::uint64_t {
            std::uint64_t m = 0;
            for (const std::uint64_t g : gapWindow) { if (g > m) { m = g; } }
            return m;
        };

        while (running.load(std::memory_order_acquire) || !wakeRing.empty()) {
            const std::size_t cnt = wakeRing.try_pop_batch(serials, wakeRing.capacity);
            if (cnt != 0) {
                const std::uint64_t nowUs = nowNs() / 1000;
                const std::uint64_t gap = nowUs - lastArrivalUs;
                lastArrivalUs = nowUs;
                if (gap > 0 && gap < 1'000'000) { pushGap(gap); }
                for (std::size_t i = 0; i < cnt; ++i) {
                    ok::wrap::OutputItem it;
                    if (outRing.try_pop(it)) {
                        dequeNs[serials[i]] = nowNs();
                        batch.push_back(BatchItem{it.backspace,
                                                  std::wstring(it.text, it.text + it.textLen),
                                                  serials[i]});
                        if (batch.size() >= kMaxEditBatch) { flushBatch(); }
                    }
                }
                continue;    // burst: never wait while data flows
            }
            flushBatch();    // drain end — mirrors the app's final flush
            // adaptive spin (same window math as the shipped consumer)
            const std::uint64_t mg = recentMaxGap();
            const std::uint64_t spinBudgetUs =
                std::clamp<std::uint64_t>(mg + mg / 4, 4, spinCapUs);
            const std::uint64_t spinStart = nowNs() / 1000;
            while (wakeRing.empty() && nowNs() / 1000 - spinStart < spinBudgetUs) {
                for (int s = 0; s < 64; ++s) { pauseCpu(); }
            }
            if (wakeRing.empty() && running.load(std::memory_order_acquire)) {
                // parked protocol (v3.4 S4): publish, re-check, then wait
                parked.store(true, std::memory_order_release);
                if (!wakeRing.empty() || !running.load(std::memory_order_acquire)) {
                    parked.store(false, std::memory_order_release);
                    continue;
                }
                std::unique_lock<std::mutex> lk(wakeMtx);
                wakeCv.wait(lk, [&] {
                    return evt_.load(std::memory_order_acquire) ||
                           !wakeRing.empty() ||
                           !running.load(std::memory_order_acquire);
                });
                parked.store(false, std::memory_order_release);
                evt_.store(false, std::memory_order_release);   // auto-reset
            }
        }
        flushBatch();
    }
};

//---------------------------------------------------------------------------
// Percentile helper (returns µs)
//---------------------------------------------------------------------------
double pct(std::vector<std::uint64_t>& v, double p) {
    if (v.empty()) { return 0.0; }
    std::sort(v.begin(), v.end());
    const auto idx = static_cast<std::size_t>(p * static_cast<double>(v.size() - 1));
    return static_cast<double>(v[idx]) / 1000.0;
}

//---------------------------------------------------------------------------
// Composition validation (no threads): keystrokes → UTF-16 code units
//---------------------------------------------------------------------------
std::wstring repScratchG;
std::vector<unsigned> compose(TextEngine& eng, const std::string& keys) {
    std::vector<unsigned> out;
    eng.startNewSession();
    for (char c : keys) {
        TextInput in;
        if (c == ' ') in.kind = InputKind::Space;
        else { in.kind = InputKind::Char; in.ch = static_cast<char32_t>(c); }
        const EngineResult& r = eng.process(in);
        eng.replacementUtf16(r, repScratchG);
        // reissue contract (main.cpp): Restore on a Char/Space key re-sends
        // the typed key after the revert — the raw char rides the edit text.
        const bool reissue = (r.code == EngineCode::Restore ||
                              r.code == EngineCode::RestoreAndStartNewSession);
        const bool suppress = (r.code == EngineCode::ReplaceMacro) ||
                              (r.consumed() && !(r.backspaceCount == 0 && repScratchG.empty()));
        if (suppress) {
            out.resize(out.size() - std::min<std::size_t>(r.backspaceCount, out.size()));
            for (wchar_t w : repScratchG) { out.push_back(static_cast<unsigned>(static_cast<unsigned short>(w))); }
            if (reissue) { out.push_back(c == ' ' ? 0x20u : static_cast<unsigned>(static_cast<unsigned char>(c))); }
        } else {
            out.push_back(c == ' ' ? 0x20u : static_cast<unsigned>(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

//---------------------------------------------------------------------------
// One (population × path × barrier) run + report
//---------------------------------------------------------------------------
void runPopulation(TextEngine& eng, const PopDef& p, Path path, const char* pathName,
                   Barrier barrierMode, const char* barrierName,
                   std::uint64_t commitNs, long iters, double pacedMs) {
    const std::string unit = p.keys;
    std::string stream;
    stream.reserve(unit.size() * static_cast<std::size_t>(iters) + 64);
    for (long i = 0; i < iters; ++i) { stream += unit; }
    if (pacedMs > 0.0) {
        stream.assign(kMixedPassage);
        for (long i = 1; i < iters; ++i) { stream += kMixedPassage; }
    }
    const std::size_t nKeys = stream.size();

    Pipeline pipe(eng);
    pipe.path = path;
    pipe.barrierMode = barrierMode;
    pipe.commitNs = commitNs;
    pipe.prod = new std::vector<ProdStages>(nKeys);
    pipe.cons = new std::vector<ConsStages>(nKeys);
    pipe.startNs.assign(nKeys, 0);
    pipe.dequeNs.assign(nKeys, 0);

    // priority policy (same as the shipped pipeline / e2e bench)
#if defined(_WIN32) && !defined(OK_WRAP_NO_WIN32)
    ::SetPriorityClass(::GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#else
    sched_param sp{};
    sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
    static_cast<void>(pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp));
#endif

    // Consumer thread — core-pinned from inside the thread (same scheme as
    // e2e_bench: MinGW-posix native_handle is a pthread_t, not a HANDLE).
    std::thread consumer([&pipe] {
#if defined(_WIN32) && !defined(OK_WRAP_NO_WIN32)
        DWORD procs = 1;
        { SYSTEM_INFO si{}; ::GetSystemInfo(&si); procs = si.dwNumberOfProcessors; }
        ::SetThreadAffinityMask(::GetCurrentThread(), 1u << (1u % procs));
#endif
        pipe.consumeLoop();
    });
#if !defined(_WIN32) || defined(OK_WRAP_NO_WIN32)
    // dedicate cores when available (2-thread pipeline, like e2e_bench)
    {
        cpu_set_t pset, cset;
        CPU_ZERO(&pset); CPU_ZERO(&cset);
        CPU_SET(0, &pset);
        CPU_SET(1 % std::max<unsigned>(std::thread::hardware_concurrency(), 2), &cset);
        pthread_setaffinity_np(pthread_self(), sizeof(pset), &pset);
        pthread_setaffinity_np(consumer.native_handle(), sizeof(cset), &cset);
    }
#else
    // producer (this thread) pinned to core 0
    ::SetThreadAffinityMask(::GetCurrentThread(), 1u);
#endif

    const auto tBegin = std::chrono::steady_clock::now();
    const auto nextKey = tBegin;
    for (std::size_t i = 0; i < nKeys; ++i) {
        pipe.produce(stream[i], static_cast<std::uint64_t>(i));
        if (pacedMs > 0.0) {
            std::this_thread::sleep_until(
                nextKey + std::chrono::duration<double, std::milli>(pacedMs * (i + 1)));
        }
    }
    pipe.running.store(false, std::memory_order_release);
    pipe.evt_.store(true, std::memory_order_release);
    { std::lock_guard<std::mutex> lk(pipe.wakeMtx); pipe.wakeCv.notify_one(); }
    consumer.join();

    // ---- aggregate ----
    std::vector<std::uint64_t> eE2e, eT1, eT2, eT3, eT4, eT5;
    std::vector<std::uint64_t> pT2, pBar;
    eE2e.reserve(nKeys); eT1.reserve(nKeys); eT2.reserve(nKeys);
    eT3.reserve(nKeys); eT4.reserve(nKeys); eT5.reserve(nKeys);
    pT2.reserve(nKeys); pBar.reserve(nKeys);
    for (std::size_t i = 0; i < nKeys; ++i) {
        const ProdStages& ps = (*pipe.prod)[i];
        if (ps.isEdit) {
            const ConsStages& cs = (*pipe.cons)[i];
            eT1.push_back(ps.t1);
            eT2.push_back(ps.t2);
            if (cs.t5 != kAbsent) {
                eT3.push_back(cs.t3); eT4.push_back(cs.t4); eT5.push_back(cs.t5);
                eE2e.push_back(cs.t5);
            } else {
                eE2e.push_back(ps.t2);   // inline path: no consumer stages
            }
        } else {
            pT2.push_back(ps.t2);
            pBar.push_back(ps.barrierNs);
        }
    }

    std::printf("[pop] %-15s path=%-15s barrier=%-6s commit=%lluns n=%zu\n",
                p.name, pathName, barrierName,
                static_cast<unsigned long long>(commitNs), nKeys);
    std::printf("      desc: %s\n", p.desc);
    std::printf("      EDIT  n=%-5zu E2E(t5) p50=%7.3f p99=%8.3f p99.9=%9.3f max=%9.3f us\n",
                eE2e.size(), pct(eE2e, 0.50), pct(eE2e, 0.99), pct(eE2e, 0.999),
                eE2e.empty() ? 0.0 : *std::max_element(eE2e.begin(), eE2e.end()) / 1000.0);
    std::printf("            t1(decision) p50=%6.3f  t2(push/emit) p50=%7.3f  "
                "t3(deq) p50=%7.3f  t4(flush) p50=%7.3f  t5(commit) p50=%7.3f us\n",
                pct(eT1, 0.50), pct(eT2, 0.50), pct(eT3, 0.50), pct(eT4, 0.50),
                pct(eT5, 0.50));
    std::printf("      PASS  n=%-5zu delivery(t2) p50=%7.3f p99=%8.3f max=%9.3f us | "
                "barrier p50=%7.3f max=%9.3f us\n",
                pT2.size(), pct(pT2, 0.50), pct(pT2, 0.99),
                pT2.empty() ? 0.0 : *std::max_element(pT2.begin(), pT2.end()) / 1000.0,
                pct(pBar, 0.50),
                pBar.empty() ? 0.0 : *std::max_element(pBar.begin(), pBar.end()) / 1000.0);
    std::printf("      edits: %zu sendInputCalls: %llu\n",
                eE2e.size(),
                static_cast<unsigned long long>(pipe.emitter.sendInputCalls()));

    delete pipe.prod;
    delete pipe.cons;
}

} // namespace

int main(int argc, char** argv) {
    std::string popSel = "all", pathSel = "all", barrierSel = "all", commitSel = "0";
    long commitNsOpt = -1;
    long iters = 2000;
    std::string wpmSel = "80,150,250";
    long mixedReps = 3;
    for (int i = 1; i < argc; ++i) {
        const auto val = [&](const char* a, const char* pfx) -> const char* {
            const std::size_t n = std::strlen(pfx);
            return std::strncmp(a, pfx, n) == 0 ? a + n : nullptr;
        };
        if (const char* v = val(argv[i], "--pop=")) { popSel = v; }
        else if (const char* v = val(argv[i], "--path=")) { pathSel = v; }
        else if (const char* v = val(argv[i], "--barrier=")) { barrierSel = v; }
        else if (const char* v = val(argv[i], "--commit-ns=")) { commitNsOpt = std::atol(v); }
        else if (const char* v = val(argv[i], "--commit=")) { commitSel = v; }
        else if (const char* v = val(argv[i], "--iters=")) { iters = std::atol(v); }
        else if (const char* v = val(argv[i], "--mixed-wpm=")) { wpmSel = v; }
        else if (const char* v = val(argv[i], "--mixed-reps=")) { mixedReps = std::atol(v); }
        else { std::printf("[tone] unknown arg %s\n", argv[i]); return 2; }
    }
    if (commitNsOpt >= 0) { commitSel = std::to_string(commitNsOpt); }
    std::uint64_t commitNs = 0;
    if (commitSel == "0") { commitNs = 0; }
    else if (commitSel == "100us") { commitNs = 100'000; }
    else if (commitSel == "1ms") { commitNs = 1'000'000; }
    else { commitNs = std::strtoull(commitSel.c_str(), nullptr, 10); }

    std::printf("[tone] KieeKey v1.0 — tone-population latency benchmark\n");

    EngineOptions o;
    o.restoreIfWrongSpelling = true;
    o.useDictionaryRestore = true;
    TextEngine eng(o);
    // The lexicon keeps the restore policy live exactly as the shipped app
    // (loanwords like "mono" stay raw; the composed words here resolve).
    // The resolver receives the COMPOSED word's code points (engine contract,
    // see TextEngine::checkRestoreIfNotInDictionary — word-break lexicon
    // veto). Entries are the composed UTF-8 forms the populations and the
    // passage produce; plain-ASCII words need no entry (sameAsRaw never
    // vetoes).
    static const char* lex[] = {
        // population targets
        "\xC3\xA1",                       // Ã¡ (as)
        "\xC4\x91",                       // đ (dd)
        "\xE1\xBA\xA5",                  // ấ (asa)
        "\xC3\xA2",                       // â (asas post-restore buffer)
        "\xC3\xB3\x61",                  // óa (oasas mid)
        "\x6F\xE1\xBA\xA5",             //  oấ (oasas mid)
        "\x6F\xC3\xA2",                  //  oâ (oasas post-restore buffer)
        "\xC4\x91\xE1\xBA\xB7ng",  // đặng (ddawjng)
        "\xC6\xB0\xE1\xBB\x9B",        // ướ (uows)
        "\xC4\x91\xC6\xB0\xE1\xBB\xA3\x63",       // được (dduowcj)
        // mixed-passage composed words
        "h\xC3\xB4m",                     // hôm
        "tr\xC3\xB2i",                    // tròi
        "\xC4\x91\xE1\xBA\xB9p",       // đẹp
        "ch\xC3\xBAng",                   // chúng
        "t\xC3\xB4i",                     // tôi
        "\xC4\x91\xE1\xBA\xA1o",       // đạo
        "h\xE1\xBB\x93",                 // hồ
        "T\xC3\xA2y",                     // Tây
        "r\xE1\xBB\x93i",                // rồi
        "ng\xE1\xBB\x93i",               // ngồi
        "ph\xC3\xAA",                     // phê
        "n\xC3\xB4i",                     // nôi
        "vi\xE1\xBB\x87t",               // việt
        "nhi\xE1\xBB\x81u",              // nhiều
        "ng\xC6\xB0\xE1\xBB\x9Di",     // người
    };
    eng.setDictionaryResolver([](const std::vector<std::uint32_t>& w) {
        thread_local std::string s;
        s.clear();
        for (std::uint32_t c : w) {
            if (c < 0x80) { s += static_cast<char>(c); }
            else if (c < 0x800) {
                s += static_cast<char>(0xC0 | (c >> 6));
                s += static_cast<char>(0x80 | (c & 0x3F));
            } else {
                s += static_cast<char>(0xE0 | (c >> 12));
                s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                s += static_cast<char>(0x80 | (c & 0x3F));
            }
        }
        for (const char* x : lex) { if (s == x) { return true; } }
        return false;
    });

    // ---- composition validation (abort on drift) --------------------------
    int fail = 0;
    for (const PopDef& p : kPops) {
        const std::vector<unsigned> got = compose(eng, p.keys);
        const bool ok = got.size() == p.expectLen &&
                        std::equal(got.begin(), got.begin() + static_cast<long>(p.expectLen), p.expect);
        if (!ok) {
            ++fail;
            std::printf("[tone] COMPOSITION MISMATCH pop=%s keys=\"%s\" got:", p.name, p.keys);
            for (unsigned u : got) { std::printf(" %04X", u); }
            std::printf("  want:");
            for (std::size_t i = 0; i < p.expectLen; ++i) { std::printf(" %04X", p.expect[i]); }
            std::printf("\n");
        }
    }
    if (fail != 0) { std::printf("[tone] FAIL: %d composition drift(s)\n", fail); return 1; }
    std::printf("[tone] composition validation: %zu populations OK\n",
                sizeof(kPops) / sizeof(kPops[0]));

    // ---- mixed-passage population (paced) ----------------------------------
    struct WpmDef { long wpm; double msPerKey; };

    const struct { Path p; const char* name; } paths[] = {
        {Path::Inline, "inline"},
        {Path::InlineDeferred, "inline-deferred"},
        {Path::Tsf, "tsf"},
    };
    const struct { Barrier b; const char* name; } barriers[] = {
        {Barrier::Spin, "spin"},
        {Barrier::Hybrid, "hybrid"},
    };

    // structured populations
    for (const PopDef& p : kPops) {
        if (popSel != "all" && popSel != p.name) { continue; }
        for (const auto& ps : paths) {
            if (pathSel != "all" && pathSel != ps.name) { continue; }
            for (const auto& bs : barriers) {
                if (barrierSel != "all" && barrierSel != bs.name) { continue; }
                runPopulation(eng, p, ps.p, ps.name, bs.b, bs.name, commitNs,
                              iters, 0.0);
            }
        }
    }

    // mixed passage: parse the WPM list
    PopDef mixed{"mixed", kMixedPassage, nullptr, 0,
                 "dense-mark passage, human-paced (interleaved pass+edit)"};
    std::size_t start = 0;
    while (start <= wpmSel.size()) {
        const std::size_t comma = wpmSel.find(',', start);
        const std::string tok = wpmSel.substr(
            start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!tok.empty()) {
            const long wpm = std::atol(tok.c_str());
            const double ms = (wpm > 0) ? 60000.0 / (static_cast<double>(wpm) * 5.0) : 80.0;
            if (popSel == "all" || popSel == "mixed") {
                for (const auto& ps : paths) {
                    if (pathSel != "all" && pathSel != ps.name) { continue; }
                    for (const auto& bs : barriers) {
                        if (barrierSel != "all" && barrierSel != bs.name) { continue; }
                        char label[32];
                        std::snprintf(label, sizeof(label), "%s@%ldwpm", mixed.name, wpm);
                        PopDef mp{"mixed", kMixedPassage, nullptr, 0, label};
                        runPopulation(eng, mp, ps.p, ps.name, bs.b, bs.name,
                                      commitNs, mixedReps, ms);
                    }
                }
            }
        }
        if (comma == std::string::npos) { break; }
        start = comma + 1;
    }

    return fail == 0 ? 0 : 1;
}
