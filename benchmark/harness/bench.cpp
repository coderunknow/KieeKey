//============================================================================
// Vietnamese IME cross-engine benchmark
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File: benchmark/harness/bench.cpp
//----------------------------------------------------------------------------
// Modes
//   selftest     driver wiring / independence / determinism checks
//   correctness  per-engine text vs intended + invariant checks + digests
//   latency      interleaved per-key timing (adapter | engine | full)
//   mem          footprint, RSS growth, allocations per key
//   robust       hostile streams in isolated child processes
//
// Output: JSON Lines on --out (stdout if omitted). Every statistic quoted in
// REPORT.md is recomputed from these lines by scripts/summarize.py.
//============================================================================
#include "engines.hpp"
#include "stats.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

//============================================================================
// Allocation tracking (only in the -DBENCH_ALLOC_TRACK build, so the latency
// build never pays for it).
//============================================================================
#ifdef BENCH_ALLOC_TRACK
namespace {
std::atomic<uint64_t> g_allocs{0};
std::atomic<uint64_t> g_allocBytes{0};
}  // namespace
void* operator new(std::size_t n) {
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    g_allocBytes.fetch_add(n, std::memory_order_relaxed);
    void* p = std::malloc(n ? n : 1);
    if (!p) { throw std::bad_alloc(); }
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    g_allocBytes.fetch_add(n, std::memory_order_relaxed);
    return std::malloc(n ? n : 1);
}
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
#endif

namespace {

//============================================================================
// CLI
//============================================================================
struct Args {
    std::string mode = "correctness";
    std::string out;
    // Multi-invocation artifacts (one file per campaign step, e.g. the six
    // diffab seeds or the per-engine memory runs) must APPEND. Opening with "w"
    // each time silently kept only the last invocation's rows — which turned the
    // diffab gate into a check of 18 rows instead of 108 and made the memory
    // gate read exactly one engine.
    bool append = false;
    std::string engineFilter;               // for robust-child / targeted runs
    std::string probeFile;
    bool probeVni = false;
    bool probeTrace = false;
    std::string corpus = "tests/data/viet74k.txt";
    uint64_t rounds = 5;
    uint64_t keys = 200000;
    uint64_t words = 20000;
    uint64_t seed = 20260907;
    uint64_t soakKeys = 2000000;
    bool matched = false;                   // feature-matched instead of as-shipped
    bool allConfigs = true;                 // run both configs
    // ---- v1.3.0 RC1 additions (see benchmark/harness/rc1.hpp) ----
    std::string configFilter;               // "" | as-shipped | matched-minimal
    std::string drivers;                    // alias or comma list; empty -> default
    std::string session;                    // "s0" … written into every row
    uint64_t rotate = 1;                    // engine-order rotation per round
    uint64_t orderOffset = 0;               // A/B order offset (fixed-lead test)
    uint64_t seedIdx = 0;                   // corpus seed selection
    uint64_t hz = 1000;                     // SIGPROF sampling request
    uint64_t profileRounds = 0;             // 0 -> derive from --rounds
    std::string libDir = "benchmark/.build";
    bool candidate = false;                 // also load the attribution pair
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        auto val = [&s](const char* pfx) -> std::string {
            const std::size_t n = std::strlen(pfx);
            return s.compare(0, n, pfx) == 0 ? s.substr(n) : std::string();
        };
        std::string v;
        if (!(v = val("--mode=")).empty()) { a.mode = v; }
        else if (!(v = val("--out=")).empty()) { a.out = v; }
        else if (s == "--append") { a.append = true; }
        else if (!(v = val("--engine=")).empty()) { a.engineFilter = v; }
        else if (!(v = val("--engines=")).empty()) { a.drivers = v; }
        else if (!(v = val("--session=")).empty()) { a.session = v; }
        else if (!(v = val("--config=")).empty()) { a.configFilter = v; }
        else if (!(v = val("--rotate=")).empty()) { a.rotate = std::strtoull(v.c_str(), nullptr, 10); }
        else if (!(v = val("--order-offset=")).empty()) { a.orderOffset = std::strtoull(v.c_str(), nullptr, 10); }
        else if (!(v = val("--seed-idx=")).empty()) { a.seedIdx = std::strtoull(v.c_str(), nullptr, 10); }
        else if (!(v = val("--hz=")).empty()) { a.hz = std::strtoull(v.c_str(), nullptr, 10); }
        else if (!(v = val("--profile-rounds=")).empty()) { a.profileRounds = std::strtoull(v.c_str(), nullptr, 10); }
        else if (!(v = val("--build-dir=")).empty()) { a.libDir = v; }
        else if (s == "--candidate") { a.candidate = true; }
        else if (!(v = val("--corpus=")).empty()) { a.corpus = v; }
        else if (!(v = val("--probe=")).empty()) { a.probeFile = v; }
        else if (!(v = val("--rounds=")).empty()) { a.rounds = std::stoull(v); }
        else if (!(v = val("--keys=")).empty()) { a.keys = std::stoull(v); }
        else if (!(v = val("--words=")).empty()) { a.words = std::stoull(v); }
        else if (!(v = val("--seed=")).empty()) { a.seed = std::stoull(v); }
        else if (!(v = val("--soak=")).empty()) { a.soakKeys = std::stoull(v); }
        else if (s == "--vni") { a.probeVni = true; }
        else if (s == "--probe-trace") { a.probeTrace = true; }
        else if (s == "--matched") { a.matched = true; a.allConfigs = false; }
        else if (s == "--as-shipped") { a.matched = false; a.allConfigs = false; }
        else if (s == "--help") {
            std::printf("usage: bench --mode=selftest|correctness|latency|mem|robust"
                        " [--out=f.jsonl] [--rounds=N] [--keys=N] [--words=N]"
                        " [--corpus=path] [--engine=name] [--matched|--as-shipped]\n");
            std::exit(0);
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            std::exit(2);
        }
    }
    return a;
}

//============================================================================
// Method variants
//============================================================================
struct MethodVariant {
    const char* name;
    bench::Method method;
    viet::TonePos tonePos;
};
const MethodVariant kMethods[] = {
    {"telex-end", bench::Method::Telex, viet::TonePos::EndOfWord},
    {"telex-mid", bench::Method::Telex, viet::TonePos::AfterVowel},
    {"vni", bench::Method::Vni, viet::TonePos::EndOfWord},
};

struct ConfigVariant { const char* name; bench::Cfg cfg; };
std::vector<ConfigVariant> configs(const Args& a) {
    std::vector<ConfigVariant> v;
    const bool wantShip = a.configFilter.empty() || a.configFilter == "as-shipped";
    const bool wantMatched = a.configFilter.empty() || a.configFilter == "matched-minimal";
    if ((a.allConfigs || !a.matched) && wantShip) {
        bench::Cfg c; c.asShipped = true;
        v.push_back({"as-shipped", c});
    }
    if ((a.allConfigs || a.matched) && wantMatched) {
        bench::Cfg c; c.asShipped = false;   // shared subset: all extras OFF
        c.macroOn = false; c.restoreOn = false; c.spellCheckOn = false;
        c.freeMark = false; c.modernOrthography = false;
        v.push_back({"matched-minimal", c});
    }
    return v;
}

// The engine list. --engine=<name> narrows every mode to that one driver, so a
// per-engine sanitizer pass cannot be contaminated by another engine's report.
std::vector<bench::Which> driverSet(const std::string& filter = std::string()) {
    std::vector<bench::Which> all = {bench::Which::KieeKey, bench::Which::KieeKeyCtl,
                                     bench::Which::OpenKey205, bench::Which::OpenKeyMaster,
                                     bench::Which::UniKey};
    if (filter.empty()) { return all; }
    // Each token is an alias (contest / rc1 / attrib / all) or one engine name,
    // so "contest,attrib" is a legal union — the release campaign needs the
    // rival pair and the attribution pair measured in the SAME rounds, or the
    // gain figure would carry campaign-to-campaign drift. Duplicates are
    // dropped: the same driver twice would double-count a round and quietly skew
    // every paired statistic built on it.
    std::vector<bench::Which> v;
    auto add = [&v](bench::Which w) {
        for (auto x : v) { if (x == w) { return; } }
        v.push_back(w);
    };
    std::size_t pos = 0;
    while (pos <= filter.size()) {
        const std::size_t comma = filter.find(',', pos);
        const std::string tok = filter.substr(pos, comma == std::string::npos ? std::string::npos
                                                                             : comma - pos);
        if (!tok.empty()) {
            if (tok == "contest") { for (auto w : bench::kContest) { add(w); } }
            else if (tok == "rc1") { for (auto w : bench::kRc1) { add(w); } }
            else if (tok == "attrib") { for (auto w : bench::kAttrib) { add(w); } }
            else if (tok == "all") { for (auto w : all) { add(w); } }
            else if (bench::whichKnown(tok)) { add(bench::whichFrom(tok)); }
            else {
                std::fprintf(stderr,
                             "[bench] unknown engine '%s' in --engine/--engines (known: contest, "
                             "rc1, attrib, all, kieekey, kieekey-aa, openkey-2.0.5, openkey-master, "
                             "unikey-4.x, kieekey-base, kieekey-cand)\n", tok.c_str());
                std::exit(2);
            }
        }
        if (comma == std::string::npos) { break; }
        pos = comma + 1;
    }
    return v;
}

// The engine list a mode should time: --engines= when given, else the legacy
// --drivers= (aliases welcome, e.g. "contest,attrib") when given, else the
// --engine= narrow, else the default contest set. Every mode routes through
// this so a one-off run cannot quietly measure a different engine set than a
// campaign did — that mistake once burned four minutes and printed "NO DATA".
std::vector<bench::Which> driverSetFor(const Args& a) {
    if (!a.drivers.empty()) { return driverSet(a.drivers); }
    return driverSet(a.engineFilter);
}

std::FILE* g_out = nullptr;

void lineStart(stats::LineJson&) {}

//============================================================================
// Streams helpers
//============================================================================
std::string stripTrailingSpaces(const std::string& s) {
    std::size_t e = s.size();
    while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\n' || s[e - 1] == '\r' || s[e - 1] == '\t')) { --e; }
    return s.substr(0, e);
}

// Expected visible text for an ASCII-only edit stream (append chars, pop on
// backspace). Used by the lossless-edit invariant.
std::string expectedAsciiEdit(const std::vector<corpus::Event>& ev) {
    std::u32string v;
    for (const auto& e : ev) {
        if (e.kind == corpus::Kind::Backspace) { if (!v.empty()) { v.pop_back(); } }
        else { v += corpus::displayChar(e); }
    }
    return viet::utf32To8(v);
}

// Builds an ASCII edit stream: safe consonants (no Telex/VNI composition key),
// spaces and backspaces. Deterministic from the seed.
std::string makeEditStream(uint64_t seed, std::size_t nKeys, std::size_t nWords) {
    static const std::string alpha = "clkvmpnhtg";
    std::string out;
    uint64_t x = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    auto rnd = [&x]() { x = x * 6364136223846793005ULL + 1442695040888963407ULL; return x; };
    for (std::size_t w = 0; w < nWords && out.size() < nKeys; ++w) {
        const std::size_t len = 1 + ((rnd() >> 41) % 9);
        for (std::size_t k = 0; k < len; ++k) { out += alpha[(rnd() >> 35) % alpha.size()]; }
        const unsigned r = static_cast<unsigned>((rnd() >> 29) % 100);
        if (r < 18) { out += '#'; }
        else if (r < 24) { out += "##"; }
        else { out += ' '; }
    }
    return out;
}

//============================================================================
// SELFTEST
//============================================================================
void modeSelftest(const Args& a) {
    const std::vector<corpus::Event> probe = corpus::parse("xin chao ban doan ket ban lam quen");
    std::vector<std::unique_ptr<bench::IDriver>> drv;
    for (auto w : driverSetFor(a)) { drv.push_back(bench::makeDriver(w)); }
    bench::Cfg c; c.asShipped = true;
    for (auto& d : drv) { d->configure(c); for (auto& e : probe) { d->feed(e); } }
    std::printf("[selftest] drivers=%zu\n", drv.size());
    for (std::size_t i = 0; i < drv.size(); ++i) {
        const std::string t = drv[i]->textUtf8();
        std::printf("[selftest] %-16s -> %s\n", drv[i]->name(), t.c_str());
    }
    const bool aa = drv[0]->textUtf8() == drv[1]->textUtf8();
    std::printf("[selftest] A/A (kieekey vs kieekey-aa) identical: %s\n", aa ? "YES" : "NO");
    const bool sameAB = drv[2]->textUtf8() == drv[3]->textUtf8();
    std::printf("[selftest] openkey-2.0.5 vs openkey-master identical on probe: %s "
                "(expected YES on plain text; corpus reports real divergences)\n",
                sameAB ? "YES" : "NO");
    std::printf("[selftest] sizeof(TextEngine)=%zu sizeof(UkEngine)=%zu sizeof(UkSharedMem)=%zu\n",
                sizeof(ok::text::TextEngine), sizeof(UkEngine), sizeof(UkSharedMem));
    // Encoder self-check: every target letter the encoder claims to know must
    // come back out of a de-accent round trip (guards the table, not an engine).
    {
        viet::Encoder enc{{viet::EncodeCfg::Method::Telex, viet::TonePos::EndOfWord}};
        std::string stream;
        const bool ok = enc.encode(u8"ngữ Việt Nam", stream);
        // Expected value is produced by an INDEPENDENT implementation
        // (scripts/check_encoder.py, which also re-derives the letter table
        // from Python's unicodedata) — see benchmark/README.md "Encoder".
        const std::string want = "nguwx ^vieetj ^nam";
        std::printf("[selftest] encoder ok=%d stream=\"%s\" (expect \"%s\") %s\n",
                    ok ? 1 : 0, stream.c_str(), want.c_str(),
                    (ok && stream == want) ? "MATCH" : "MISMATCH");
    }
    std::printf("[selftest] clock pair overhead = %llu ns\n",
                static_cast<unsigned long long>(stats::calibrateClockPair(20000)));
    (void)a;
}

//============================================================================
// CORRECTNESS
//============================================================================
struct Counters {
    uint64_t total = 0, exact = 0, restored = 0, caseOnly = 0, noTone = 0, toneWrong = 0,
             tonePos = 0, hatWrong = 0, lettersDiffer = 0;
    uint64_t keys = 0;
    uint64_t digest = 1469598103934665603ULL;
    void add(viet::Verdict v) {
        ++total;
        switch (v) {
            case viet::Verdict::Exact: ++exact; break;
            case viet::Verdict::CaseOnly: ++caseOnly; break;
            case viet::Verdict::RestoredRaw: ++restored; break;
            case viet::Verdict::NoTone: ++noTone; break;
            case viet::Verdict::ToneWrong: ++toneWrong; break;
            case viet::Verdict::TonePosition: ++tonePos; break;
            case viet::Verdict::HatWrong: ++hatWrong; break;
            default: ++lettersDiffer; break;
        }
    }
};

void emitCounters(const char* mode, const std::string& config, const std::string& method,
                  const std::string& cat, const std::string& engine, const Counters& ct) {
    stats::LineJson j(g_out);
    j.addStr("mode", mode); j.addStr("config", config); j.addStr("method", method);
    j.addStr("cat", cat); j.addStr("engine", engine);
    j.addInt("total", ct.total); j.addInt("exact", ct.exact); j.addInt("case_only", ct.caseOnly);
    j.addInt("restored_raw", ct.restored); j.addInt("tone_missing", ct.noTone);
    j.addInt("tone_wrong", ct.toneWrong); j.addInt("tone_position", ct.tonePos);
    j.addInt("hat_wrong", ct.hatWrong); j.addInt("letters_differ", ct.lettersDiffer);
    j.addInt("keys", ct.keys); j.addInt("digest", ct.digest);
    j.end();
    std::fflush(g_out);
}

// Runs one stream on every driver from a clean state and returns each engine's
// visible text. Engines are never interleaved inside a case, so an example
// line can never read a stale state.
void runStreamOnAll(std::vector<std::unique_ptr<bench::IDriver>>& drv, const bench::Cfg& cfg,
                    const std::vector<corpus::Event>& ev, std::vector<std::string>& texts) {
    texts.assign(drv.size(), std::string());
    for (std::size_t k = 0; k < drv.size(); ++k) {
        drv[k]->configure(cfg);
        for (const auto& e : ev) { drv[k]->feed(e); }
        texts[k] = stripTrailingSpaces(drv[k]->textUtf8());
    }
}

void emitExamples(const char* cfgName, const char* methodName, const char* cat,
                  const std::vector<std::unique_ptr<bench::IDriver>>& drv,
                  const std::vector<std::string>& texts,
                  const std::string& keyStr, const std::string& intended,
                  const std::string& id, int& budget) {
    if (budget <= 0) { return; }
    bool mismatch = false;
    for (std::size_t k = 0; k < drv.size(); ++k) {
        if (!intended.empty() && texts[k] != intended) { mismatch = true; }
    }
    if (!mismatch) { return; }
    --budget;
    stats::LineJson j(g_out);
    j.addStr("mode", "example"); j.addStr("config", cfgName); j.addStr("method", methodName);
    j.addStr("cat", cat); j.addStr("input_keys", keyStr); j.addStr("intended", intended);
    if (!id.empty()) { j.addStr("stream_id", id); }
    for (std::size_t k = 0; k < drv.size(); ++k) {
        j.addStr(std::string("out_") + drv[k]->name(), texts[k]);
    }
    j.end();
    std::fflush(g_out);
}

void modeCorrectness(const Args& a) {
    for (const auto& cv : configs(a)) {
        for (const auto& mv : kMethods) {
            viet::EncodeCfg ec;
            ec.method = (mv.method == bench::Method::Telex) ? viet::EncodeCfg::Method::Telex
                                                             : viet::EncodeCfg::Method::Vni;
            ec.tonePos = mv.tonePos;
            viet::Encoder enc(ec);

            // The engine configuration must follow the encoding: a VNI stream
            // only composes when the engine's input method is VNI (and, for
            // KieeKey, when digits are allowed to compose).
            bench::Cfg cfg = cv.cfg;
            cfg.method = mv.method;

            std::vector<std::unique_ptr<bench::IDriver>> drv;
            for (auto w : driverSetFor(a)) { drv.push_back(bench::makeDriver(w)); }

            // ---------------- category: words (isolated, space-committed) ----
            {
                corpus::WordCorpus words;
                words.load(a.corpus, static_cast<std::size_t>(a.words), false, enc);
                std::vector<Counters> ct(drv.size());
                std::vector<std::string> texts;
                int budget = 60;
                for (std::size_t i = 0; i < words.words.size(); ++i) {
                    const std::vector<corpus::Event> ev = corpus::parse(words.keys[i] + " ");
                    runStreamOnAll(drv, cfg, ev, texts);
                    for (std::size_t k = 0; k < drv.size(); ++k) {
                        Counters& c = ct[k];
                        c.add(viet::classify(words.words[i], texts[k], words.keys[i]));
                        c.keys += ev.size();
                        c.digest = stats::fnv1a(texts[k]) ^ (c.digest * 1099511628211ULL);
                    }
                    emitExamples(cv.name, mv.name, "words", drv, texts, words.keys[i],
                                 words.words[i], "", budget);
                }
                for (std::size_t k = 0; k < drv.size(); ++k) {
                    emitCounters("correctness", cv.name, mv.name, "words", drv[k]->name(), ct[k]);
                }
                stats::LineJson j(g_out);
                j.addStr("mode", "corpus"); j.addStr("config", cv.name);
                j.addStr("method", mv.name);
                j.addInt("corpus_lines_seen", words.totalLines);
                j.addInt("corpus_used", words.words.size());
                j.addInt("skipped_multitoken", words.skippedSpace);
                j.addInt("skipped_too_long", words.skippedLong);
                j.addInt("skipped_unencodable", words.skippedEncode);
                j.end();
            }

            // ---------------- category: passages (full sentences) ------------
            {
                std::vector<Counters> ct(drv.size());
                std::vector<std::string> texts;
                int budget = 30;
                for (const auto& p : corpus::passages()) {
                    std::string stream;
                    if (!enc.encode(p.intended, stream)) { continue; }
                    const std::vector<corpus::Event> ev = corpus::parse(stream + " ");
                    runStreamOnAll(drv, cfg, ev, texts);
                    for (std::size_t k = 0; k < drv.size(); ++k) {
                        Counters& c = ct[k];
                        c.add(viet::classify(p.intended, texts[k], stream));
                        c.keys += ev.size();
                        c.digest = stats::fnv1a(texts[k]) ^ (c.digest * 1099511628211ULL);
                    }
                    emitExamples(cv.name, mv.name, "passages", drv, texts, stream, p.intended,
                                 p.name, budget);
                }
                for (std::size_t k = 0; k < drv.size(); ++k) {
                    emitCounters("correctness", cv.name, mv.name, "passages", drv[k]->name(), ct[k]);
                }
            }
            std::fflush(g_out);
        }

        // ------------- behaviour categories (stream language, no encoding) ---
        {
            std::vector<std::unique_ptr<bench::IDriver>> drv;
            for (auto w : driverSetFor(a)) { drv.push_back(bench::makeDriver(w)); }

            // stress: behaviour probes; per-stream text + agreement
            {
                std::vector<Counters> ct(drv.size());
                std::vector<std::string> texts;
                for (const auto& s : corpus::stressStreams()) {
                    const std::vector<corpus::Event> ev = corpus::parse(s.stream);
                    runStreamOnAll(drv, cv.cfg, ev, texts);
                    for (std::size_t k = 0; k < drv.size(); ++k) {
                        ct[k].total += 1;
                        ct[k].keys += ev.size();
                        if (!texts[k].empty()) { ct[k].exact += 1; }
                        ct[k].digest = stats::fnv1a(texts[k]) ^ (ct[k].digest * 1099511628211ULL);
                        stats::LineJson j(g_out);
                        j.addStr("mode", "text"); j.addStr("config", cv.name);
                        j.addStr("cat", "stress"); j.addStr("stream_id", s.name);
                        j.addStr("engine", drv[k]->name()); j.addStr("text", texts[k]);
                        j.addInt("digest", stats::fnv1a(texts[k]));
                        j.end();
                    }
                }
                for (std::size_t k = 0; k < drv.size(); ++k) {
                    emitCounters("stress-digest", cv.name, "stream", "stress", drv[k]->name(), ct[k]);
                }
            }

            // lossless-edit invariant: ASCII-only streams survive exactly
            {
                std::vector<Counters> ct(drv.size());
                std::vector<uint64_t> lost(drv.size(), 0), mangled(drv.size(), 0);
                std::vector<std::string> texts;
                for (uint64_t s = 0; s < 40; ++s) {
                    const std::string stream = makeEditStream(a.seed + s * 7919, 6000, 900);
                    const std::vector<corpus::Event> ev = corpus::parse(stream);
                    const std::string expect = stripTrailingSpaces(expectedAsciiEdit(ev));
                    runStreamOnAll(drv, cv.cfg, ev, texts);
                    for (std::size_t k = 0; k < drv.size(); ++k) {
                        Counters& c = ct[k];
                        c.total += 1;
                        c.keys += ev.size();
                        if (texts[k] == expect) { c.exact += 1; }
                        else {
                            const std::int64_t dl = static_cast<std::int64_t>(expect.size()) -
                                                     static_cast<std::int64_t>(texts[k].size());
                            if (dl > 0) { lost[k] += static_cast<uint64_t>(dl); }
                            ++mangled[k];
                        }
                        c.digest = stats::fnv1a(texts[k]) ^ (c.digest * 1099511628211ULL);
                    }
                }
                for (std::size_t k = 0; k < drv.size(); ++k) {
                    stats::LineJson j(g_out);
                    j.addStr("mode", "invariant"); j.addStr("config", cv.name);
                    j.addStr("cat", "lossless-edit"); j.addStr("engine", drv[k]->name());
                    j.addInt("streams", ct[k].total); j.addInt("clean", ct[k].exact);
                    j.addInt("divergent_streams", mangled[k]); j.addInt("chars_lost", lost[k]);
                    j.addInt("keys", ct[k].keys); j.addInt("digest", ct[k].digest);
                    j.end();
                }
            }

            // fuzz: survival + agreement
            {
                const std::string stream = viet::makeFuzz(a.seed, 200000);
                const std::vector<corpus::Event> ev = corpus::parse(stream);
                std::vector<std::string> texts;
                runStreamOnAll(drv, cv.cfg, ev, texts);
                for (std::size_t k = 0; k < drv.size(); ++k) {
                    stats::LineJson j(g_out);
                    j.addStr("mode", "fuzz"); j.addStr("config", cv.name);
                    j.addStr("engine", drv[k]->name()); j.addInt("keys", ev.size());
                    j.addInt("out_len_bytes", texts[k].size());
                    j.addInt("digest", stats::fnv1a(texts[k]));
                    j.end();
                }
            }

            // macro: one abbreviation per engine, typed then space-committed
            {
                for (std::size_t k = 0; k < drv.size(); ++k) {
                    auto& d = drv[k];
                    bench::Cfg c = cv.cfg;
                    c.asShipped = false;
                    c.macroOn = true;
                    d->configure(c);
                    d->installMacro("tn", std::wstring{0x74, 0x00EA, 0x6E});   // "ten" with circumflex
                    const std::vector<corpus::Event> ev = corpus::parse("tn ");
                    for (const auto& e : ev) { d->feed(e); }
                    const std::string t = stripTrailingSpaces(d->textUtf8());
                    stats::LineJson j(g_out);
                    j.addStr("mode", "macro"); j.addStr("config", cv.name);
                    j.addStr("engine", d->name()); j.addStr("text", t);
                    j.addB("expanded", t == std::string("t\xC3\xAA""n"));
                    j.end();
                }
            }
            std::fflush(g_out);
        }
    }
}
//============================================================================
// LATENCY
//============================================================================
struct Streams {
    std::string name;
    const std::vector<corpus::Event>* ev;
};

void runOneLatency(bench::IDriver& d, const bench::Cfg& cfg, const std::vector<corpus::Event>& ev,
                   const char* streamName, const char* cfgName, const char* methodName,
                   uint64_t round, uint64_t clockOverhead) {
    const std::size_t n = ev.size();
    double prep_ns_ = 0.0, eng_ns_ = 0.0, full_ns_ = 0.0;
    // warmup (same code path, discarded) — first-touch of code and data.
    d.configure(cfg);
    for (const auto& e : ev) { d.feed(e); }

    // (1) adapter only
    d.configure(cfg);
    {
        const uint64_t t0 = stats::nowNs();
        for (const auto& e : ev) { d.prepare(e); }
        const uint64_t t1 = stats::nowNs();
        prep_ns_ = static_cast<double>(t1 - t0) / static_cast<double>(n);
    }
    // (2) adapter + engine call
    d.configure(cfg);
    {
        const uint64_t t0 = stats::nowNs();
        for (const auto& e : ev) { d.prepare(e); d.invoke(); }
        const uint64_t t1 = stats::nowNs();
        eng_ns_ = static_cast<double>(t1 - t0) / static_cast<double>(n);
    }
    // (3) full path (adapter + engine + consumer apply)
    d.configure(cfg);
    stats::Agg ag;
    {
        const uint64_t t0 = stats::nowNs();
        for (const auto& e : ev) {
            const uint64_t a = stats::nowNs();
            d.prepare(e);
            d.invoke();
            d.apply(e);
            ag.add(stats::nowNs() - a);
        }
        const uint64_t t1 = stats::nowNs();
        full_ns_ = static_cast<double>(t1 - t0) / static_cast<double>(n);
    }
    ag.sortSamples();

    stats::LineJson j(g_out);
    j.addStr("mode", "latency"); j.addStr("config", cfgName); j.addStr("method", methodName);
    j.addStr("stream", streamName); j.addStr("engine", d.name()); j.addInt("round", round);
    j.addInt("keys", n);
    j.addF("prep_ns_per_key", prep_ns_);
    j.addF("engine_ns_per_key", eng_ns_);
    j.addF("engine_core_net_ns", eng_ns_ - prep_ns_);
    j.addF("full_ns_per_key", full_ns_);
    j.addF("p50_ns", static_cast<double>(ag.pct(0.50)), 0);
    j.addF("p90_ns", static_cast<double>(ag.pct(0.90)), 0);
    j.addF("p99_ns", static_cast<double>(ag.pct(0.99)), 0);
    j.addF("p999_ns", static_cast<double>(ag.pct(0.999)), 0);
    j.addF("mean_sample_ns", ag.mean());
    j.addInt("max_sample_ns", ag.mx);
    j.addF("clock_pair_overhead_ns", static_cast<double>(clockOverhead), 0);
    j.addInt("out_digest", stats::fnv1a(d.textUtf8()));
    j.end();
    std::fflush(g_out);
}

// Probe mode: feed hand-written streams (one per line, optional tab-separated
// intended text) and print every engine's result. Used to verify the consumer
// models and to publish worked examples in the report.
void modeProbe(const Args& a, const std::string& file) {
    std::ifstream f(file);
    if (!f) { std::fprintf(stderr, "cannot read %s\n", file.c_str()); return; }
    std::vector<std::unique_ptr<bench::IDriver>> drv;
    for (auto w : driverSetFor(a)) { drv.push_back(bench::makeDriver(w)); }
    bench::Cfg cfg; cfg.asShipped = true;
    if (a.probeVni) { cfg.method = bench::Method::Vni; }
    std::string line;
    std::size_t idx = 0;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') { line.pop_back(); }
        if (line.empty() || line[0] == '#') { continue; }
        std::string stream = line, intended;
        const auto tab = line.find('\t');
        if (tab != std::string::npos) { stream = line.substr(0, tab); intended = line.substr(tab + 1); }
        const std::vector<corpus::Event> ev = corpus::parse(stream);
        bench::UniKeyDriver::traceFlag() = (a.probeTrace && idx == 0);
        std::vector<std::string> texts;
        runStreamOnAll(drv, cfg, ev, texts);
        bench::UniKeyDriver::traceFlag() = false;
        stats::LineJson j(g_out);
        j.addStr("mode", "probe"); j.addInt("id", idx++); j.addStr("input_keys", stream);
        j.addStr("intended", intended);
        for (std::size_t k = 0; k < drv.size(); ++k) {
            j.addStr(std::string("out_") + drv[k]->name(), texts[k]);
            j.addB(std::string("ok_") + drv[k]->name(), intended.empty() ? true : texts[k] == intended);
        }
        j.end();
    }
}

// Measures the adapter's own call machinery (the OpenKey shim's dlsym-resolved
// indirect call, and the equivalent empty entry points of the other adapters)
// so the report can publish — rather than hide — the asymmetry inside the
// timed region. No engine is modified; this only times plumbing.
void openkeyNoopLoop(bench::IDriver& d, int n) {
    auto* ok = dynamic_cast<bench::OpenKeyShimDriver*>(&d);
    if (!ok || !ok->noopFn()) { return; }
    int (*fn)() = ok->noopFn();
    volatile int sink = 0;
    for (int i = 0; i < n; ++i) { sink += fn(); }
    (void)sink;
}

void modeOverhead(const Args& a) {
    const uint64_t clockOverhead = stats::calibrateClockPair(200000);
    std::vector<std::unique_ptr<bench::IDriver>> drv;
    for (auto w : driverSetFor(a)) { drv.push_back(bench::makeDriver(w)); }
    bench::Cfg cfg; cfg.asShipped = true;
    constexpr int kIters = 200000;
    for (auto& d : drv) {
        d->configure(cfg);
        const long long support = d->noopCall();
        stats::LineJson j(g_out);
        j.addStr("mode", "overhead"); j.addStr("engine", d->name());
        j.addInt("iterations", kIters);
        j.addF("clock_pair_overhead_ns", static_cast<double>(clockOverhead), 0);
        if (support < 0) {
            j.addStr("note", "adapter adds no call indirection (statically linked entry point)");
            j.addF("ns_per_noop_call", 0.0, 2);
        } else {
            uint64_t best = ~0ull;
            for (int rep = 0; rep < 5; ++rep) {
                const uint64_t t0 = stats::nowNs();
                openkeyNoopLoop(*d, kIters);
                const uint64_t t1 = stats::nowNs();
                best = std::min<uint64_t>(best, t1 - t0);
            }
            j.addF("ns_per_noop_call", static_cast<double>(best) / kIters, 3);
            j.addStr("note", "one indirect call through the dlopen shim + back "
                             "(subtract from this engine's invoke-stage cost)");
        }
        j.end();
    }
}

void modeLatency(const Args& a) {
    const uint64_t clockOverhead = stats::calibrateClockPair(200000);
    std::printf("[latency] clock-pair overhead = %llu ns\n",
                static_cast<unsigned long long>(clockOverhead));
    for (const auto& cv : configs(a)) {
        for (const auto& mv : kMethods) {
            viet::EncodeCfg ec;
            ec.method = (mv.method == bench::Method::Telex) ? viet::EncodeCfg::Method::Telex
                                                             : viet::EncodeCfg::Method::Vni;
            ec.tonePos = mv.tonePos;   // all three encodings are timed, so no
                                        // engine can be favoured by a stream choice
            viet::Encoder enc(ec);
            corpus::WordCorpus wc;
            wc.load(a.corpus, static_cast<std::size_t>(a.words) * 4, false, enc);
            corpus::LatencySet ls = corpus::buildLatencySet(wc, static_cast<std::size_t>(a.keys));
            bench::Cfg lcfg = cv.cfg;
            lcfg.method = mv.method;

            std::vector<std::unique_ptr<bench::IDriver>> drv;
            for (auto w : driverSetFor(a)) { drv.push_back(bench::makeDriver(w)); }

            const std::pair<const char*, std::vector<corpus::Event>*> streams[] = {
                {"prose", &ls.words}, {"edit-storm", &ls.edit}, {"pathological", &ls.pathological}
            };
            // Interleaved rounds with a rotating engine order: round r starts
            // at driver (r % count), so no engine always runs first (first run
            // of a round has a colder cache).
            for (uint64_t r = 0; r < a.rounds; ++r) {
                const std::size_t cnt = drv.size();
                for (std::size_t i = 0; i < cnt; ++i) {
                    const std::size_t idx = (i + static_cast<std::size_t>(r)) % cnt;
                    for (const auto& st : streams) {
                        runOneLatency(*drv[idx], lcfg, *st.second, st.first, cv.name, mv.name,
                                      r, clockOverhead);
                    }
                }
            }
        }
    }
}

//============================================================================
// MEMORY
//============================================================================
void modeMem(const Args& a) {
    for (const auto& cv : configs(a)) {
        std::vector<std::unique_ptr<bench::IDriver>> drv;
        // RSS is only trustworthy one-engine-per-process (shared pages and
        // first-touch order otherwise attribute another engine's memory to it):
        // the campaign runner therefore repeats this mode with --engine=<name>.
        for (auto w : (a.engineFilter.empty() ? driverSet()
                                              : std::vector<bench::Which>{bench::whichFrom(a.engineFilter)})) {
            drv.push_back(bench::makeDriver(w));
        }
        for (auto& d : drv) {
            const uint64_t rss0 = stats::rssBytes();
#ifdef BENCH_ALLOC_TRACK
            const uint64_t a0 = g_allocs.load(), b0 = g_allocBytes.load();
#endif
            d->configure(cv.cfg);
            const uint64_t rss1 = stats::rssBytes();
            std::vector<corpus::Event> ev = corpus::parse(
                "xin chao Viet Nam toi la Nam hom nay troi dep qua chung ta di choi nhe "
                "cong viec hom nay nhieu qua toi phai lam them gio ngay mai chung ta hop");
            std::vector<corpus::Event> big;
            big.reserve(static_cast<std::size_t>(a.soakKeys));
            while (big.size() < a.soakKeys) {
                for (const auto& e : ev) { big.push_back(e); }
            }
            big.resize(static_cast<std::size_t>(a.soakKeys));
#ifdef BENCH_ALLOC_TRACK
            const uint64_t a1 = g_allocs.load(), b1 = g_allocBytes.load();
#endif
            for (const auto& e : big) { d->feed(e); }
#ifdef BENCH_ALLOC_TRACK
            const uint64_t a2 = g_allocs.load(), b2 = g_allocBytes.load();
#endif
            const uint64_t rss2 = stats::rssBytes();
            stats::LineJson j(g_out);
            j.addStr("mode", "mem"); j.addStr("config", cv.name); j.addStr("engine", d->name());
            j.addInt("rss_after_init", rss1); j.addInt("rss_after_soak", rss2);
            j.addInt("soak_keys", big.size());
#ifdef BENCH_ALLOC_TRACK
            j.addInt("alloc_init", a1 - a0); j.addInt("bytes_init", b1 - b0);
            j.addInt("alloc_soak", a2 - a1); j.addInt("bytes_soak", b2 - b1);
            j.addF("allocs_per_key", static_cast<double>(a2 - a1) / static_cast<double>(big.size()), 6);
#else
            j.addInt("alloc_init", 0); j.addInt("alloc_soak", 0);
#endif
            j.end();
            std::fflush(g_out);
        }
    }
}

//============================================================================
// ROBUSTNESS — hostile streams, one child process per (engine, stream) so an
// engine's own fault cannot hide another engine's result.
//============================================================================
std::vector<std::pair<std::string, std::string>> hostileStreams() {
    std::vector<std::pair<std::string, std::string>> h;
    h.emplace_back("long-word-5k", std::string(5000, 'a'));
    std::string hats;
    for (int i = 0; i < 800; ++i) { hats += "aaeeooaaeeoo"; }
    h.emplace_back("hat-thrash-9k", hats);
    h.emplace_back("backspace-flood", std::string(4000, '#'));
    std::string tones;
    for (int i = 0; i < 700; ++i) { tones += "ssrrjjffxxasfrxj"; }
    h.emplace_back("tone-storm", tones);
    std::string mixed;
    for (int i = 0; i < 400; ++i) { mixed += "ddoo#saa^T^Hee~?~!##"; }
    h.emplace_back("mixed-with-shift", mixed);
    h.emplace_back("all-symbols", std::string(2000, ','));
    std::string ent;
    for (int i = 0; i < 1000; ++i) { ent += "ab%cd#ef%"; }
    h.emplace_back("break-hammer", ent);
    std::string digits;
    for (int i = 0; i < 500; ++i) { digits += "a1e2o3u4y5d6w7f8s9j0"; }
    h.emplace_back("digit-hammer-vni-keys", digits);
    h.emplace_back("fuzz-200k", viet::makeFuzz(4242, 200000));
    h.emplace_back("fuzz-1m", viet::makeFuzz(99991, 1000000));
    return h;
}

void runHostileChild(const Args& a, const char* engine, const std::string& streamName,
                     const std::string& stream) {
    const pid_t pid = ::fork();
    if (pid == 0) {
        // Child: single engine, single stream. Any crash / sanitizer abort is
        // reported by the exit status; ASan text goes to the log file.
        std::signal(SIGSEGV, SIG_DFL);
        std::signal(SIGABRT, SIG_DFL);
        try {
            auto d = bench::makeDriver(bench::whichFrom(engine));
            bench::Cfg c; c.asShipped = true;
            d->configure(c);
            const std::vector<corpus::Event> ev = corpus::parse(stream);
            for (const auto& e : ev) { d->feed(e); }
            const std::string t = d->textUtf8();
            std::fprintf(stdout, "[child] %s %s out_len=%zu digest=%llu\n", engine, streamName.c_str(),
                         t.size(), static_cast<unsigned long long>(stats::fnv1a(t)));
            std::fflush(stdout);
            _exit(0);
        } catch (...) {
            std::fprintf(stdout, "[child] %s %s CXX_EXCEPTION\n", engine, streamName.c_str());
            std::fflush(stdout);
            _exit(3);
        }
    }
    int status = 0;
    const pid_t got = ::waitpid(pid, &status, 0);
    (void)got; (void)a;
    stats::LineJson j(g_out);
    j.addStr("mode", "robust"); j.addStr("engine", engine); j.addStr("stream", streamName);
    j.addInt("keys", stream.size());
    if (WIFSIGNALED(status)) {
        j.addStr("outcome", "signal");
        j.addInt("signal", static_cast<uint64_t>(WTERMSIG(status)));
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        j.addStr("outcome", "ok");
    } else {
        j.addStr("outcome", "exit");
        j.addInt("exit_code", static_cast<uint64_t>(WEXITSTATUS(status)));
    }
    j.end();
    std::fflush(g_out);
}

void modeRobust(const Args& a) {
    const std::vector<std::pair<std::string, std::string>> h = hostileStreams();
    if (!a.engineFilter.empty()) {
        // Child-selection mode driven from the shell (so each engine can have
        // its own sanitizer log): run every hostile stream on this engine.
        for (const auto& [nm, st] : h) { runHostileChild(a, a.engineFilter.c_str(), nm, st); }
        return;
    }
    for (const char* eng : {"kieekey", "openkey-2.0.5", "openkey-master", "unikey-4.x"}) {
        for (const auto& [nm, st] : h) { runHostileChild(a, eng, nm, st); }
    }
}

}  // namespace

#include "rc1.hpp"   // needs Args/configs()/driverSetFor() above

int main(int argc, char** argv) {
    const Args a = parseArgs(argc, argv);
    g_out = a.out.empty() ? stdout : std::fopen(a.out.c_str(), a.append ? "a" : "w");
    if (!g_out) { std::fprintf(stderr, "cannot open %s\n", a.out.c_str()); return 2; }

    stats::LineJson meta(g_out);
    meta.addStr("mode", "meta"); meta.addStr("bench", "viet-ime-4way");
    meta.addStr("action", a.mode); meta.addStr("corpus", a.corpus);
    meta.addInt("rounds", a.rounds); meta.addInt("keys", a.keys);
    meta.addInt("words", a.words); meta.addInt("seed", a.seed);
    meta.addInt("t0_unix", static_cast<uint64_t>(std::time(nullptr)));
    meta.end();

    if (rc1::isRc1Mode(a.mode)) { return rc1::run(a) ? 1 : 0; }
    if (a.mode == "selftest") { modeSelftest(a); }
    else if (a.mode == "correctness") { modeCorrectness(a); }
    else if (a.mode == "latency") { modeLatency(a); }
    else if (a.mode == "mem") { modeMem(a); }
    else if (a.mode == "robust") { modeRobust(a); }
    else if (a.mode == "probe") { modeProbe(a, a.probeFile); }
    else if (a.mode == "overhead") { modeOverhead(a); }
    else if (a.mode == "encoder-dump") {
        for (const auto& mv : kMethods) {
            viet::EncodeCfg ec;
            ec.method = (mv.method == bench::Method::Telex) ? viet::EncodeCfg::Method::Telex
                                                             : viet::EncodeCfg::Method::Vni;
            ec.tonePos = mv.tonePos;
            viet::Encoder enc(ec);
            corpus::WordCorpus w;
            w.load(a.corpus, static_cast<std::size_t>(a.words), true, enc);
            for (std::size_t i = 0; i < w.words.size(); ++i) {
                std::fprintf(g_out, "%s\t%s\t%s\n", mv.name, w.words[i].c_str(), w.keys[i].c_str());
            }
        }
    }
    else { std::fprintf(stderr, "bad mode\n"); return 2; }
    return 0;
}
