//============================================================================
// Vietnamese IME cross-engine benchmark — v1.3.0 RC1 measurement modes
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File: benchmark/harness/rc1.hpp
//----------------------------------------------------------------------------
// Included by bench.cpp right before main(), so it reuses the harness
// infrastructure (Args, configs(), driverSetFor(), the corpus/encoder helpers)
// instead of duplicating it: every mode here measures the SAME streams the
// correctness and latency modes measure.
//
// Modes (docs/bench/rc1-130/PROTOCOL.md is the contract; this file is only the
// measurement side — tier bands, noise envelopes and the verdict live in
// benchmark/scripts/rc1_stats.py so the arithmetic is auditable in one place):
//
//   tput          paired, interleaved per-round measurement of the engine-decision
//                 stage (prepare+invoke) and of the full producer→consumer stage
//                 (feed) for every (config, method, stream) cell. Round r times
//                 every engine in a rotated order; a paired statistic on the
//                 round is therefore immune to slow drift in host state, which is
//                 the only way to resolve a 1–3 % difference on a shared machine.
//   diffab        event-by-event differential between two engines (used for
//                 frozen v1.2.2 vs current tree: identical transcripts or the
//                 candidate is rejected, whatever the timing says).
//   profile       SIGPROF sampler over the invoke stage. Ranks functions; every
//                 accept/reject decision is made on `tput`, never here.
//   timer         the clock's own overhead distribution (what a clock pair costs
//                 when nothing is measured) — published, never subtracted by
//                 stealth.
//   cold          fresh-process wall time per engine (exec + dlopen + static init
//                 + first round), measured by spawning this binary once per
//                 engine and timing the child.
// (a "walks" mode lived here: the exhaustive equivalence oracle for candidate
//  C-A's automata. The candidate was proven equivalent, then measured slower than
//  the frozen engine in every cell, and reverted — so the oracle lost its subject
//  and left with it. Its numbers are recorded in
//  docs/bench/rc1-130/OPTIMIZATION_LEDGER.md §2.)
//   attrib-guard sanity check for the attribution pair: the two shim columns must
//                 reproduce the in-process column's transcript EXACTLY and their
//                 per-key cost must be within a plausible band of it. This is the
//                 guard against a shim that quietly measures nothing (see the
//                 comment on kk_shim.cpp).
//============================================================================
#pragma once

#include <algorithm>

#include <cerrno>
#include <csignal>
#include <ucontext.h>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

namespace rc1 {

inline bool isRc1Mode(const std::string& m) {
    return m == "tput" || m == "diffab" || m == "profile" || m == "timer" || m == "cold"
           || m == "attrib-guard";
}

//---------------------------------------------------------------- streams --
struct Streams {
    struct One {
        std::string name;
        std::vector<corpus::Event> ev;
    };
    std::vector<One> v;
};

// The three measurement streams, built once per (config, method) pair and then
// replayed identically by every engine in every round. `keys` is the event
// count of the longest stream; each row carries its own count, so a stream
// shorter than the request can never inflate a per-key number.
// `seedIdx` rotates the window of the corpus that becomes the timed stream, so
// several seeds in one campaign are not measuring the same 200 k keys over and
// over — a report that says "sessions x rounds paired samples" only means
// something if the underlying events actually vary.
inline Streams buildStreams(const Args& a, const viet::Encoder& enc, std::size_t nKeys,
                            uint64_t seedIdx = 0) {
    corpus::WordCorpus wc;
    wc.load(a.corpus, static_cast<std::size_t>(a.words) * 4, false, enc);
    corpus::LatencySet ls = corpus::buildLatencySet(wc, nKeys);
    Streams s;
    s.v.push_back({"prose", ls.words});
    s.v.push_back({"edit-storm", ls.edit});
    s.v.push_back({"pathological", ls.pathological});
    for (auto& o : s.v) { if (o.ev.empty()) { o.ev = ls.words; } }
    if (seedIdx) {
        for (auto& o : s.v) {
            const std::size_t rot = static_cast<std::size_t>(
                (seedIdx * (o.ev.size() / 8)) % (o.ev.size() ? o.ev.size() : 1));
            std::rotate(o.ev.begin(), o.ev.begin() + rot, o.ev.end());
        }
    }
    return s;
}

inline viet::Encoder encoderFor(const MethodVariant& mv, uint64_t /*seedIdx*/) {
    viet::EncodeCfg ec;
    ec.method = (mv.method == bench::Method::Telex) ? viet::EncodeCfg::Method::Telex
                                                    : viet::EncodeCfg::Method::Vni;
    ec.tonePos = mv.tonePos;
    return viet::Encoder(ec);
}

inline std::vector<bench::IDriver*> rawPtrs(std::vector<std::unique_ptr<bench::IDriver>>& d) {
    std::vector<bench::IDriver*> v;
    for (auto& u : d) { v.push_back(u.get()); }
    return v;
}

inline std::string buildOf(bench::IDriver* d) {
    if (auto* k = dynamic_cast<bench::KieeKeyShimDriver*>(d)) { return k->buildId(); }
    return "in-process";
}

//------------------------------------------------------------------- tput --
// One row per (session, round, config, method, stream, engine).
inline int modeTput(const Args& a, std::FILE* out) {
    auto which = driverSetFor(a);
    std::vector<std::unique_ptr<bench::IDriver>> drv;
    for (auto w : which) { drv.push_back(bench::makeDriver(w)); }
    std::vector<bench::IDriver*> d = rawPtrs(drv);
    for (auto* x : d) {
        if (x->name() == std::string("?")) { std::fprintf(stderr, "[tput] driver failed to load\n"); return 1; }
    }
    std::printf("[tput] session=%s engines=%zu\n", a.session.c_str(), d.size());
    stats::LineJson m(out);
    m.addStr("mode", "meta"); m.addStr("action", "tput");
    m.addStr("session", a.session); m.addStr("corpus", a.corpus);
    m.addInt("rounds", a.rounds); m.addInt("keys_requested", a.keys);
    m.addInt("rotate", a.rotate); m.addInt("order_offset", a.orderOffset);
    m.addInt("seed_idx", a.seedIdx);
    m.addStr("clock", "CLOCK_MONOTONIC_RAW");
    m.end();
    for (std::size_t k = 0; k < d.size(); ++k) {
        stats::LineJson b(out);
        b.addStr("mode", "build"); b.addStr("engine", d[k]->name());
        b.addStr("build", buildOf(d[k])); b.end();
    }

    const uint64_t warm = 1;   // round 0 is discarded by the statistics side
    for (const auto& cv : configs(a)) {
        for (const auto& mv : kMethods) {
            viet::Encoder enc = encoderFor(mv, a.seedIdx);
            Streams st = buildStreams(a, enc, static_cast<std::size_t>(a.keys), a.seedIdx);
            bench::Cfg lcfg = cv.cfg;
            lcfg.method = mv.method;
            for (auto* x : d) { x->configure(lcfg); }
            for (uint64_t r = 0; r < a.rounds + warm; ++r) {
                std::vector<size_t> ord(d.size());
                for (size_t i = 0; i < ord.size(); ++i) {
                    // rotate=0 keeps one fixed order (the fixed-lead test);
                    // rotate=1 walks the engines by one place per round, so each
                    // engine is first exactly often and the paired differences
                    // cancel any positional bias.
                    ord[i] = a.rotate ? (i + r * a.rotate + a.orderOffset) % d.size() : i;
                }
                for (auto& sl : st.v) {
                    for (size_t oi = 0; oi < ord.size(); ++oi) {
                        bench::IDriver* x = d[ord[oi]];
                        const std::vector<corpus::Event>& ev = sl.ev;
                        x->configure(lcfg);
                        const uint64_t t0 = stats::nowNs();
                        for (const auto& e : ev) { x->prepare(e); x->invoke(); }
                        const uint64_t t1 = stats::nowNs();
                        x->configure(lcfg);
                        const uint64_t t2 = stats::nowNs();
                        for (const auto& e : ev) { x->feed(e); }
                        const uint64_t t3 = stats::nowNs();
                        stats::LineJson j(out);
                        j.addStr("mode", "tput"); j.addStr("session", a.session);
                        j.addInt("round", r); j.addInt("order", oi);
                        j.addStr("config", cv.name); j.addStr("method", mv.name);
                        j.addStr("stream", sl.name); j.addStr("engine", x->name());
                        j.addStr("build", buildOf(x));
                        j.addInt("keys", ev.size());
                        j.addInt("warm", r < warm ? 1 : 0);
                        j.addF("invoke_ns", static_cast<double>(t1 - t0), 1);
                        j.addF("full_ns", static_cast<double>(t3 - t2), 1);
                        j.addF("engine_ns_per_key", static_cast<double>(t1 - t0) / ev.size(), 4);
                        j.addF("full_ns_per_key", static_cast<double>(t3 - t2) / ev.size(), 4);
                        j.end();
                    }
                }
            }
        }
    }
    return 0;
}

//----------------------------------------------------------------- diffab --
// Event-by-event transcript comparison. Nothing about this mode is a
// performance question: a candidate that changes any key's decision, however
// slightly, is rejected here before its speed is of any interest.
inline uint64_t visDigest(bench::IDriver* x) { return stats::fnv1a(x->textUtf8()); }

inline int modeDiffab(const Args& a, std::FILE* out) {
    std::vector<bench::Which> which = driverSetFor(a);
    if (which.size() != 2) {
        std::fprintf(stderr, "[diffab] pass exactly two engines (--engines=), subject first\n");
        return 1;
    }
    std::vector<std::unique_ptr<bench::IDriver>> drv;
    for (auto w : which) { drv.push_back(bench::makeDriver(w)); }
    std::vector<bench::IDriver*> d = rawPtrs(drv);
    std::printf("[diffab] %s vs %s, session=%s, seed=%llu\n", d[0]->name(), d[1]->name(),
                a.session.c_str(), static_cast<unsigned long long>(a.seedIdx));
    unsigned long long totalEvents = 0, totalMismatch = 0;
    for (const auto& cv : configs(a)) {
        for (const auto& mv : kMethods) {
            viet::Encoder enc = encoderFor(mv, a.seedIdx);
            Streams st = buildStreams(a, enc, static_cast<std::size_t>(a.keys), a.seedIdx);
            bench::Cfg lcfg = cv.cfg;
            lcfg.method = mv.method;
            for (auto& sl : st.v) {
                d[0]->configure(lcfg);
                d[1]->configure(lcfg);
                unsigned long long mism = 0, firstAt = 0;
                std::string s0, s1;
                for (std::size_t i = 0; i < sl.ev.size(); ++i) {
                    d[0]->feed(sl.ev[i]);
                    d[1]->feed(sl.ev[i]);
                    if (visDigest(d[0]) != visDigest(d[1])) {
                        if (mism == 0) {
                            firstAt = i;
                            s0 = d[0]->textUtf8();
                            s1 = d[1]->textUtf8();
                        }
                        ++mism;
                    }
                }
                const bool tailEq = d[0]->textUtf8() == d[1]->textUtf8();
                totalEvents += sl.ev.size();
                totalMismatch += mism;
                stats::LineJson j(out);
                j.addStr("mode", "diffab"); j.addStr("session", a.session);
                j.addInt("seed_idx", a.seedIdx);
                j.addStr("config", cv.name); j.addStr("method", mv.name);
                j.addStr("stream", sl.name);
                j.addStr("subject", d[0]->name()); j.addStr("rival", d[1]->name());
                j.addStr("subject_build", buildOf(d[0]));
                j.addStr("rival_build", buildOf(d[1]));
                j.addInt("events", sl.ev.size());
                j.addInt("per_key_mismatches", mism);
                j.addInt("first_mismatch_key", firstAt);
                j.addB("final_text_equal", tailEq);
                j.addStr("subject_final", s0); j.addStr("rival_final", s1);
                j.end();
            }
        }
    }
    std::printf("[diffab] events=%llu per-key mismatches=%llu\n",
                totalEvents, totalMismatch);
    return totalMismatch == 0 ? 0 : 1;
}

// A/B transcript guard for the attribution pair. Two failure modes are caught:
// the shim measuring a different engine tree than it claims (transcript
// mismatch), and the shim measuring almost nothing at all (per-key cost far
// below the in-process column, which is what a missing kk_prepare looked like).
inline int modeAttribGuard(const Args& a, std::FILE* out) {
    std::vector<std::unique_ptr<bench::IDriver>> drv;
    drv.push_back(bench::makeDriver(bench::Which::KieeKey));
    drv.push_back(bench::makeDriver(bench::Which::KieeKeyBase));
    drv.push_back(bench::makeDriver(bench::Which::KieeKeyCand));
    std::vector<bench::IDriver*> d = rawPtrs(drv);
    for (auto* x : d) {
        if (auto* k = dynamic_cast<bench::KieeKeyShimDriver*>(x)) {
            if (!k->loaded()) { std::fprintf(stderr, "[attrib-guard] %s not loaded\n", x->name()); return 1; }
        }
    }
    bench::Cfg cfg; cfg.asShipped = true; cfg.method = bench::Method::Telex;
    viet::Encoder enc = encoderFor(kMethods[0], 0);
    Streams st = buildStreams(a, enc, 4000);
    const std::vector<corpus::Event>& ev = st.v[0].ev;
    bool same = true;
    for (auto* x : d) { x->configure(cfg); }
    for (std::size_t i = 0; i < ev.size(); ++i) {
        for (auto* x : d) { x->feed(ev[i]); }
        const uint64_t h0 = visDigest(d[0]), h1 = visDigest(d[1]), h2 = visDigest(d[2]);
        if (h0 != h1 || h0 != h2) { same = false; break; }
    }
    // timing sanity: same engine, same stage shape -> within a factor of ~3 once
    // the decode step is included on both sides. A shim far below the in-process
    // column means it is not driving the engine the way the column is.
    const uint64_t reps = 20;
    double ns[3] = {0, 0, 0};
    for (std::size_t k = 0; k < 3; ++k) {
        double best = 1e300;
        for (uint64_t r = 0; r < reps; ++r) {
            d[k]->configure(cfg);
            const uint64_t t0 = stats::nowNs();
            for (const auto& e : ev) { d[k]->feed(e); }
            const uint64_t t1 = stats::nowNs();
            if (double(t1 - t0) / ev.size() < best) { best = double(t1 - t0) / ev.size(); }
        }
        ns[k] = best;
    }
    const double ratio = ns[0] > 0 ? ns[1] / ns[0] : 0.0;
    const bool bandOk = ratio > 0.25 && ratio < 4.0;
    stats::LineJson j(out);
    j.addStr("mode", "attrib-guard");
    j.addB("transcripts_equal", same);
    j.addStr("kieekey_build", buildOf(d[0]));
    j.addStr("base_build", buildOf(d[1]));
    j.addStr("cand_build", buildOf(d[2]));
    j.addF("ns_per_key_inprocess", ns[0], 3);
    j.addF("ns_per_key_base", ns[1], 3);
    j.addF("ns_per_key_cand", ns[2], 3);
    j.addF("base_over_inprocess", ratio, 3);
    j.addB("band_ok", bandOk);
    j.end();
    std::printf("[attrib-guard] transcripts %s · base/in-process = %.3f× (%s)\n",
                same ? "IDENTICAL" : "DIVERGED", ratio, bandOk ? "in band" : "OUT OF BAND");
    return (same && bandOk) ? 0 : 1;
}

//----------------------------------------------------------------- profile --
namespace {
struct Sampler {
    static constexpr std::size_t kSlots = 1 << 15;
    std::vector<uint64_t> key{};
    std::vector<uint32_t> cnt{};
    uint64_t total = 0;
    static Sampler* self;

    static void install(const Args& a) {
        self = new Sampler();
        self->key.assign(kSlots, 0);
        self->cnt.assign(kSlots, 0);
        struct sigaction sa{};
        // SA_SIGINFO + the interrupted RIP from the ucontext. The obvious
        // shortcut — a plain handler reading __builtin_return_address(0) — was
        // what this file did before, and it produced ONE pc for 1 248 samples:
        // for a non-real-time handler glibc returns through a fixed trampoline,
        // so the "return address" is the restorer stub, not the profiled
        // instruction. A profile that resolves to a single address looks exactly
        // like "no hot spot", which is worse than no profile at all.
        // REG_RIP is part of the x86-64 Linux ABI (ucontext.h), not a glibc
        // private; on any other layout the guard below keeps the count honest.
#if defined(__x86_64__) && defined(REG_RIP)
        sa.sa_sigaction = [](int, siginfo_t*, void* uc) {
            const auto* g = &static_cast<ucontext_t*>(uc)->uc_mcontext.gregs;
            self->hit(static_cast<uint64_t>((*g)[REG_RIP]));
        };
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
#else
        sa.sa_handler = [](int) { self->hit(0); };   // unresolved, but counted
#endif
        sigaction(SIGPROF, &sa, nullptr);
        struct itimerval tv{};
        const uint64_t per = 1000000ull / (a.hz ? a.hz : 1000ull);
        tv.it_interval.tv_usec = static_cast<long>(per);
        tv.it_value.tv_usec = static_cast<long>(per);
        setitimer(ITIMER_PROF, &tv, nullptr);
    }
    static void stop() {
        struct itimerval tv{};
        setitimer(ITIMER_PROF, &tv, nullptr);
    }
    void hit(uint64_t pc) {
        std::size_t h = static_cast<std::size_t>((pc >> 4) % kSlots);
        for (std::size_t p = 0; p < 64; ++p, h = (h + 1) % kSlots) {
            if (key[h] == 0) { key[h] = pc; cnt[h] = 1; ++total; return; }
            if (key[h] == pc) { ++cnt[h]; ++total; return; }
        }
        ++total;   // table full: count it, drop the pc (never perturb the sample rate)
    }
};
Sampler* Sampler::self = nullptr;
}  // namespace

inline int modeProfile(const Args& a, std::FILE* out) {
    auto which = driverSetFor(a);
    if (which.size() != 1) { std::fprintf(stderr, "[profile] pass exactly one engine\n"); return 1; }
    std::vector<std::unique_ptr<bench::IDriver>> drv;
    drv.push_back(bench::makeDriver(which[0]));
    bench::IDriver* x = drv[0].get();
    const auto cfgs = configs(a);
    bench::Cfg cfg = cfgs.empty() ? bench::Cfg() : cfgs[0].cfg;
    cfg.method = bench::Method::Telex;
    const char* cfgName = cfgs.empty() ? "as-shipped" : cfgs[0].name;
    viet::Encoder enc = encoderFor(kMethods[0], 0);
    Streams st = buildStreams(a, enc, static_cast<std::size_t>(a.keys), a.seedIdx);
    std::vector<corpus::Event> ev;
    for (auto& o : st.v) { for (auto& e : o.ev) { ev.push_back(e); } }
    const uint64_t rounds = a.profileRounds ? a.profileRounds : std::max<uint64_t>(1, a.rounds * 40);
    x->configure(cfg);
    for (uint64_t r = 0; r < 2; ++r) { for (const auto& e : ev) { x->prepare(e); x->invoke(); } }
    x->configure(cfg);
    const uint64_t t0 = stats::nowNs();
    Sampler::install(a);
    uint64_t keys = 0;
    for (uint64_t r = 0; r < rounds; ++r) {
        for (const auto& e : ev) { x->prepare(e); x->invoke(); ++keys; }
    }
    Sampler::stop();
    const uint64_t t1 = stats::nowNs();
    stats::LineJson j(out);
    j.addStr("mode", "profile-meta");
    j.addStr("engine", x->name()); j.addStr("build", buildOf(x));
    j.addStr("config", cfgName);
    j.addStr("stage", "invoke");
    j.addInt("keys", keys); j.addInt("window_ns", t1 - t0);
    j.addInt("samples", Sampler::self->total);
    j.addInt("hz", a.hz);
    j.end();
    for (std::size_t i = 0; i < Sampler::self->key.size(); ++i) {
        if (!Sampler::self->cnt[i]) { continue; }
        stats::LineJson r(out);
        r.addStr("mode", "sample");
        r.addInt("pc", Sampler::self->key[i]);
        r.addInt("n", Sampler::self->cnt[i]);
        r.end();
    }
    std::printf("[profile] %s: %llu samples over %.2f s, %llu keys -> %.2f ns/key\n",
                x->name(), (unsigned long long)Sampler::self->total, (t1 - t0) / 1e9,
                (unsigned long long)keys, double(t1 - t0) / double(keys ? keys : 1));
    return 0;
}

//------------------------------------------------------------------- timer --
inline int modeTimer(const Args& a, std::FILE* out) {
    const uint64_t reps = 200000;
    stats::Agg pairAg, emptyAg;
    volatile uint64_t sink = 0;
    for (uint64_t i = 0; i < reps; ++i) {
        const uint64_t t0 = stats::nowNs();
        const uint64_t t1 = stats::nowNs();
        sink += (t1 - t0);
        pairAg.add(t1 - t0);
    }
    for (uint64_t i = 0; i < reps; ++i) {
        const uint64_t t0 = stats::nowNs();
        sink += t0;
        emptyAg.add(0);
        (void)sink;
    }
    timespec res{};
    clock_getres(CLOCK_MONOTONIC_RAW, &res);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%lld ns", static_cast<long long>(res.tv_nsec));
    stats::LineJson j(out);
    j.addStr("mode", "timer"); j.addInt("reps", reps);
    j.addStr("clock", "CLOCK_MONOTONIC_RAW"); j.addStr("resolution", buf);
    j.addF("pair_min_ns", static_cast<double>(pairAg.mn), 0);
    j.addF("pair_p50_ns", pairAg.pct(0.50), 0);
    j.addF("pair_p95_ns", pairAg.pct(0.95), 0);
    j.addF("pair_p99_ns", pairAg.pct(0.99), 0);
    j.addF("pair_max_ns", static_cast<double>(pairAg.mx), 0);
    j.addF("pair_mean_ns", pairAg.mean(), 2);
    j.end();
    // pct() returns an integer count of nanoseconds: it must be cast before it
    // meets %f, or the varargs read prints 0 (the JSON artifact was always right;
    // a display-only defect still misleads the person watching a campaign run).
    std::printf("[timer] clock pair: res=%s min=%llu med=%.0f p99=%.0f ns\n", buf,
                (unsigned long long)pairAg.mn, static_cast<double>(pairAg.pct(0.50)),
                static_cast<double>(pairAg.pct(0.99)));
    return 0;
}

// Cold start = one fresh process per engine, timing exec → the end of its first
// measured round. Nothing is warm: no dlopen cache reuse (each child loads its
// own object), no branch-predictor history, no page-cache surprises for the
// engine's own text — the last of those is real and stays in the number, which
// is the point of measuring it separately from the steady-state cells.
inline int modeCold(const Args& a, std::FILE* out) {
    char self[4096];
    ssize_t n = ::readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) { std::fprintf(stderr, "[cold] cannot find /proc/self/exe\n"); return 1; }
    self[n] = 0;
    auto which = driverSetFor(a);
    const int reps = 12;
    for (auto w : which) {
        const std::string name = bench::whichName(w);
        const std::string tmp = "/home/user/.cache/kbench/cold_" + name + ".jsonl";
        stats::Agg wall;
        double engMin = 0;
        for (int r = 0; r < reps; ++r) {
            const uint64_t t0 = stats::nowNs();
            const std::string cmd = std::string(self) + " --mode=tput --engines=" + name +
                " --rounds=1 --config=as-shipped --keys=" + std::to_string(a.keys) +
                " --words=" + std::to_string(a.words) + " --out=" + tmp + " >/dev/null 2>&1";
            if (std::system(cmd.c_str()) != 0) { std::fprintf(stderr, "[cold] child failed\n"); return 1; }
            const uint64_t t1 = stats::nowNs();
            wall.add(t1 - t0);
            // read the child's own first-round number back
            std::FILE* f = std::fopen(tmp.c_str(), "r");
            if (!f) { continue; }
            char line[4096];
            while (std::fgets(line, sizeof line, f)) {
                const char* p = std::strstr(line, "\"engine_ns_per_key\":");
                if (p && std::strstr(line, "\"mode\":\"tput\"")) {
                    const double v = std::atof(p + std::strlen("\"engine_ns_per_key\":"));
                    if (engMin == 0 || v < engMin) { engMin = v; }
                }
            }
            std::fclose(f);
        }
        stats::LineJson j(out);
        j.addStr("mode", "cold"); j.addStr("engine", name); j.addInt("reps", reps);
        j.addF("wall_min_ns", static_cast<double>(wall.mn), 0);
        j.addF("wall_p50_ns", wall.pct(0.50), 0);
        j.addF("wall_p95_ns", wall.pct(0.95), 0);
        j.addF("wall_max_ns", static_cast<double>(wall.mx), 0);
        j.addF("first_round_engine_ns_per_key", engMin, 3);
        j.end();
        std::printf("[cold] %-14s wall p50=%.2f ms  min=%.2f ms  first-round=%.2f ns/key\n",
                    name.c_str(), wall.pct(0.50) / 1e6, wall.mn / 1e6, engMin);
    }
    return 0;
}


inline int run(const Args& a) {
    std::FILE* out = g_out ? g_out : stdout;
    if (a.mode == "tput") { return modeTput(a, out); }
    if (a.mode == "diffab") { return modeDiffab(a, out); }
    if (a.mode == "profile") { return modeProfile(a, out); }
    if (a.mode == "timer") { return modeTimer(a, out); }
    if (a.mode == "cold") { return modeCold(a, out); }
    if (a.mode == "attrib-guard") { return modeAttribGuard(a, out); }
    return 1;
}

}  // namespace rc1
