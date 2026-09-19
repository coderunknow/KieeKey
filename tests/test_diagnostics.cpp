//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/test_diagnostics.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// Diagnostics suite (v1.3.0-beta3).
//
// Every regression this file locks down came from a user-visible symptom:
//
//   1. "Sự kiện phím đã xử lý tăng dù chưa gõ phím" — the beta2 counter was
//      fed by mouse clicks and foreground changes. The counter set now keeps
//      keyboard, mouse and foreground events in SEPARATE ids and
//      keyboardEvents() sums only the two keyboard ids. Asserted here.
//   2. A latency number nobody can act on — the histograms must report bucket
//      edges (never fake precision), stay monotonic under percentile queries
//      and survive a reset.
//   3. A log that grows forever / a disk that fills up — rotation is asserted
//      by writing past the cap and reading both files back.
//   4. Diagnostics that cost latency when nobody is looking — Level::Off must
//      record NOTHING (no histogram samples, no trace records).
//   5. A trace ring that silently loses the newest event — overwrite-oldest
//      ordering and the drain contract (oldest first, removes what it copied)
//      are asserted, because the file-log drainer depends on it.
//----------------------------------------------------------------------------
#include "Diagnostics.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace ok::diag;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string tempPath(const char* name) {
    const char* dir = std::getenv("TMPDIR");
    if (dir == nullptr || *dir == '\0') { dir = "/tmp"; }
    std::string path = dir;
    path += "/kieekey-diag-test-";
    path += std::to_string(static_cast<long long>(::getpid()));
    path += "-";
    path += name;
    return path;
}

//---------------------------------------------------------------------------
void testHistogramBucketsAndPercentiles() {
    Histogram h;
    assert(h.total() == 0);
    assert(h.percentileUs(0.5) == -1);        // empty: no invented number
    assert(h.maxUs() == 0);

    // Bucket 0 is "<1 µs", bucket i>=1 is [2^(i-1), 2^i).
    h.recordUs(0);
    h.recordUs(-5);                            // negative => bucket 0, never OOB
    assert(h.bucket(0) == 2);
    h.recordUs(1);
    assert(h.bucket(1) == 1);                  // [1,2)
    h.recordUs(2);
    h.recordUs(3);
    assert(h.bucket(2) == 2);                  // [2,4)
    h.recordUs(4);
    assert(h.bucket(3) == 1);                  // [4,8)
    h.recordUs(20000);                         // [16384, 32768) => bucket 15
    assert(h.bucket(Histogram::kBuckets - 1) == 1);
    h.recordUs(10'000'000);                    // saturates, no overflow slot
    assert(h.bucket(Histogram::kBuckets - 1) == 2);
    assert(h.total() == 8);                    // 0,-5 | 1 | 2,3 | 4 | 20000 | 1e7
    assert(h.bucket(0) + h.bucket(1) + h.bucket(2) + h.bucket(3) +
               h.bucket(Histogram::kBuckets - 1) == h.total());
    assert(h.maxUs() == 10'000'000);

    // Percentiles are bucket UPPER edges and monotonic in p.
    const std::int64_t p50 = h.percentileUs(0.50);
    const std::int64_t p95 = h.percentileUs(0.95);
    const std::int64_t p99 = h.percentileUs(0.99);
    assert(p50 >= 0 && p50 <= p95 && p95 <= p99);
    // p100 must reach the saturating bucket's low edge (never 0, never <max).
    assert(h.percentileUs(1.0) == Histogram::bucketLowUs(Histogram::kBuckets - 1));
    // Out-of-range p is clamped, not extrapolated.
    assert(h.percentileUs(-1.0) == h.percentileUs(0.0));
    assert(h.percentileUs(2.0) == h.percentileUs(1.0));

    // The mean is a documented bucket-midpoint approximation: it must stay
    // inside [min recorded, max recorded].
    const double mean = h.meanUs();
    assert(mean >= 0.0 && mean <= static_cast<double>(h.maxUs()));

    // Bucket edges are consistent with each other.
    for (std::size_t i = 1; i + 1 < Histogram::kBuckets; ++i) {
        assert(Histogram::bucketLowUs(i) < Histogram::bucketHighUs(i));
        assert(Histogram::bucketHighUs(i) == Histogram::bucketLowUs(i + 1));
    }

    const std::string text = h.toText();
    assert(text.find("<1us=2") != std::string::npos);
    assert(text.find("[2-4)us=2") != std::string::npos);
    assert(text.find(">=16384us=2") != std::string::npos);

    h.reset();
    assert(h.total() == 0 && h.maxUs() == 0 && h.toText().empty());
    std::cout << "  [PASS] histogram buckets/percentiles/reset\n";
}

//---------------------------------------------------------------------------
void testHistogramPercentileIsHonest() {
    // A single 900 µs outlier must not be reported as the median of a burst of
    // 100 fast keys (this is the "peak latency dominates forever" symptom).
    Histogram h;
    for (int i = 0; i < 100; ++i) { h.recordUs(30); }
    h.recordUs(900);
    assert(h.percentileUs(0.5) == 32);         // [16,32) upper edge? no: 30 in [16,32)
    assert(h.percentileUs(0.99) == 32);
    assert(h.percentileUs(1.0) >= 512);        // the outlier lives in [512,1024)
    assert(h.maxUs() == 900);
    std::cout << "  [PASS] percentiles ignore a single outlier\n";
}

//---------------------------------------------------------------------------
void testKeyboardCounterIsKeyboardOnly() {
    Diagnostics& d = Diagnostics::instance();
    d.resetAll();
    assert(d.keyboardEvents() == 0);

    // The beta2 bug: mouse + foreground events inflated "keyboard events".
    d.add(Counter::MouseButton, 7);
    d.add(Counter::MouseWheel, 3);
    d.add(Counter::ForegroundChanged, 5);
    assert(d.keyboardEvents() == 0);           // REGRESSION GUARD

    d.add(Counter::KeyDown, 11);
    d.add(Counter::KeyUp, 11);
    assert(d.keyboardEvents() == 22);
    assert(d.get(Counter::KeyDown) == 11);
    // Ids are distinct and names are non-empty & unique (a duplicated label is
    // how two counters become indistinguishable in a report).
    std::vector<std::string> seen;
    for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(Counter::kCount); ++i) {
        const Counter id = static_cast<Counter>(i);
        const char* name = counterName(id);
        const char* vi = counterNameVi(id);
        assert(name != nullptr && name[0] != '\0');
        assert(vi != nullptr && vi[0] != '\0');
        assert(std::string(name) != "?");
        assert(std::string(vi) != "?");
        assert(std::find(seen.begin(), seen.end(), std::string(name)) == seen.end());
        seen.emplace_back(name);
    }
    assert(seen.size() == static_cast<std::size_t>(Counter::kCount));
    std::cout << "  [PASS] keyboard counter counts keyboard events only ("
              << seen.size() << " distinct counters)\n";
}

//---------------------------------------------------------------------------
void testLevelGating() {
    Diagnostics& d = Diagnostics::instance();
    d.resetAll();

    d.setLevel(Level::Off);
    assert(!d.atLeast(Level::Basic) && !d.atLeast(Level::Full));
    d.record(Stage::TotalEdit, 1234);
    EventRecord rec{};
    rec.vk = 0x41;
    d.trace(rec);
    assert(d.histogram(Stage::TotalEdit).total() == 0);   // nothing recorded
    assert(d.events().size() == 0);

    d.setLevel(Level::Basic);
    assert(d.atLeast(Level::Basic) && !d.atLeast(Level::Full));
    d.record(Stage::TotalEdit, 1234);
    assert(d.histogram(Stage::TotalEdit).total() == 1);
    d.trace(rec);
    assert(d.events().size() == 0);                        // trace is Full-only

    d.setLevel(Level::Full);
    assert(d.atLeast(Level::Full));
    d.trace(rec);
    assert(d.events().size() == 1);

    // levelFromInt clamps garbage from the registry/UI.
    assert(levelFromInt(-3) == Level::Off);
    assert(levelFromInt(0) == Level::Off);
    assert(levelFromInt(1) == Level::Basic);
    assert(levelFromInt(2) == Level::Full);
    assert(levelFromInt(99) == Level::Full);
    assert(std::string(levelName(Level::Full)) == "Full");

    d.setLevel(Level::Basic);
    d.resetAll();
    std::cout << "  [PASS] level gating: Off records nothing, Full adds the trace\n";
}

//---------------------------------------------------------------------------
void testEventTraceOrderingAndDrain() {
    EventTrace trace(4);
    assert(trace.capacity() == 4 && trace.size() == 0);

    for (int i = 1; i <= 4; ++i) {
        EventRecord r{};
        r.vk = static_cast<std::uint32_t>(i);
        assert(!trace.push(r));                 // not wrapped yet
    }
    assert(trace.size() == 4 && trace.wrapped() == 0);

    // Snapshot is oldest-first and capped.
    EventRecord out[8]{};
    assert(trace.snapshot(out, 2) == 2);
    assert(out[0].vk == 3 && out[1].vk == 4);   // the two MOST RECENT
    assert(trace.snapshot(out, 8) == 4);
    assert(out[0].vk == 1 && out[3].vk == 4);
    // Sequence numbers are assigned by the ring, monotonic.
    assert(out[0].seq == 1 && out[3].seq == 4);

    // Overwrite-oldest on wrap.
    EventRecord five{};
    five.vk = 5;
    assert(trace.push(five));                   // wrapped
    assert(trace.wrapped() == 1);
    assert(trace.size() == 4);
    assert(trace.snapshot(out, 8) == 4);
    assert(out[0].vk == 2 && out[3].vk == 5);

    // Drain removes exactly what it copied (the file-log contract).
    assert(trace.drain(out, 2) == 2);
    assert(out[0].vk == 2 && out[1].vk == 3);
    assert(trace.size() == 2);
    assert(trace.snapshot(out, 8) == 2);
    assert(out[0].vk == 4 && out[1].vk == 5);
    assert(trace.drain(out, 8) == 2);
    assert(trace.size() == 0);
    assert(trace.drain(out, 8) == 0);
    assert(trace.snapshot(nullptr, 4) == 0);
    assert(trace.drain(out, 0) == 0);

    trace.clear();
    assert(trace.size() == 0);
    std::cout << "  [PASS] trace ring: ordering, wrap, drain\n";
}

//---------------------------------------------------------------------------
void testFileLogWritesAndRotates() {
    const std::string path = tempPath("log.txt");
    std::remove(path.c_str());
    std::remove((path + ".1").c_str());

    FileLog log;
    assert(!log.isOpen());
    assert(!log.write("orphan line"));         // closed log never writes
    // A 64-byte cap with ~30-byte lines must rotate within a few writes.
    assert(log.open(path, 64));
    assert(log.isOpen());
    assert(log.path() == path);

    for (int i = 0; i < 5; ++i) {
        assert(log.writef("line %d — tiếng Việt ăâđêôơư", i));
    }
    assert(log.bytesWritten() > 0);
    // The live file always holds the MOST RECENT line and stays inside the cap
    // (+ one line: the write that tripped the rotation lands in the new file).
    const std::string live = readFile(path);
    const std::string backup = readFile(path + ".1");
    assert(live.find("line 4") != std::string::npos);
    assert(live.size() <= 64 + 64);
    assert(live.find("tiếng Việt") != std::string::npos);      // UTF-8 survives
    // 5 lines of ~36 bytes against a 64-byte cap must have rotated.
    assert(log.rotations() >= 1);
    assert(!backup.empty());
    // One backup generation is kept (documented): it holds the PREVIOUS line,
    // never the newest one.
    assert(backup.find("line 3") != std::string::npos);
    assert(backup.find("line 4") == std::string::npos);
    assert(log.bytesWritten() == live.size());

    log.close();
    assert(!log.isOpen());
    // An empty path means "disabled": it closes the log and reports false, and
    // Diagnostics::setLogFile("") maps that to success (disabling is not an
    // error) — asserted in testFlushTraceToFileLog.
    assert(!log.open(""));
    assert(!log.isOpen());
    // An unwritable path degrades to "closed", never to a crash or a retry per
    // keystroke.
    assert(!log.open("/proc/definitely-not-writable/kieekey.log"));
    assert(!log.isOpen());

    std::remove(path.c_str());
    std::remove((path + ".1").c_str());
    std::cout << "  [PASS] file log: UTF-8 lines, rotation, failure handling\n";
}

//---------------------------------------------------------------------------
void testFlushTraceToFileLog() {
    Diagnostics& d = Diagnostics::instance();
    d.resetAll();
    const std::string path = tempPath("trace.log");
    std::remove(path.c_str());

    // Off / Basic must not touch the file, even when it is open.
    assert(d.setLogFile(path));
    d.setLevel(Level::Basic);
    EventRecord rec{};
    rec.vk = 0x5A;
    rec.source = 0;
    rec.action = 0;
    rec.decision = 1;
    rec.stage = static_cast<std::uint16_t>(Stage::TotalEdit);
    rec.stageUs = 42;
    d.trace(rec);                              // ignored below Full
    assert(d.flushTraceToFileLog() == 0);
    assert(readFile(path).empty());

    d.setLevel(Level::Full);
    for (int i = 0; i < 3; ++i) {
        rec.vk = static_cast<std::uint32_t>(0x41 + i);
        d.trace(rec);
    }
    assert(d.events().size() == 3);
    assert(d.flushTraceToFileLog() == 3);
    assert(d.events().size() == 0);            // drained
    const std::string content = readFile(path);
    assert(content.find("vk=0x41") != std::string::npos);
    assert(content.find("vk=0x43") != std::string::npos);
    assert(content.find("total (edit key)") != std::string::npos);
    assert(content.find("42us") != std::string::npos);
    assert(d.get(Counter::LogLinesWritten) == 3);

    d.setLogFile("");                          // closes
    d.setLevel(Level::Basic);
    d.resetAll();
    std::remove(path.c_str());
    std::cout << "  [PASS] trace -> file log drain (Full only)\n";
}

//---------------------------------------------------------------------------
void testInjectedDelay() {
    Diagnostics& d = Diagnostics::instance();
    d.setLevel(Level::Full);
    d.setInjectedDelayUs(0, 0);
    assert(d.injectedHookDelayUs() == 0);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) { d.applyHookDelay(); }
    const auto idleUs = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - t0).count();
    assert(idleUs < 5000);                     // a disabled delay is free

    d.setInjectedDelayUs(300, 0);
    assert(d.injectedHookDelayUs() == 300);
    const auto t1 = std::chrono::steady_clock::now();
    d.applyHookDelay();
    const auto waitedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                              std::chrono::steady_clock::now() - t1).count();
    assert(waitedUs >= 300);                   // really waited
    assert(waitedUs < 20000);                  // …and not absurdly long

    // The clamp keeps a mistyped registry value from freezing the hook thread
    // (100 ms is the LowLevelHooksTimeout neighbourhood).
    d.setInjectedDelayUs(10'000'000, 10'000'000);
    assert(d.injectedHookDelayUs() == 100000);
    assert(d.injectedConsumerDelayUs() == 100000);

    // Below Full the delay must NOT apply: diagnostics cannot slow down a user
    // who turned them off.
    d.setLevel(Level::Off);
    const auto t2 = std::chrono::steady_clock::now();
    d.applyHookDelay();
    assert(std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - t2).count() < 5000);

    d.setInjectedDelayUs(0, 0);
    d.setLevel(Level::Basic);
    std::cout << "  [PASS] injected delay: free when off, real when Full, clamped\n";
}

//---------------------------------------------------------------------------
void testReportAndVerdict() {
    Diagnostics& d = Diagnostics::instance();
    d.resetAll();
    d.setLevel(Level::Full);

    SystemSnapshot sys;
    sys.osName = "Windows 11 Pro (build 22631)";
    sys.arch = "x64";
    sys.appVersion = "1.3.0-beta3 (PE 1.3.0.4)";
    sys.foregroundApp = "notepad.exe — đang gõ";
    sys.keyboardLayout = "00000409 (US)";
    sys.outputMode = "Auto (TSF)";
    sys.inputMethod = "Telex";
    sys.codeTable = "Unicode";
    sys.workingSetKb = 12345;
    sys.uptimeMs = 65000;
    sys.dpi = 144;
    sys.imeEnabled = true;
    sys.hookInstalled = true;
    sys.fgHookInstalled = true;
    sys.liveEffectsEnabled = false;
    d.setSystemSnapshot(sys);
    const SystemSnapshot back = d.systemSnapshot();
    assert(back.dpi == 144 && back.arch == "x64" && back.workingSetKb == 12345);

    d.add(Counter::KeyDown, 40);
    d.add(Counter::KeyUp, 40);
    d.add(Counter::MouseButton, 3);
    d.record(Stage::TotalEdit, 250);
    d.record(Stage::HookToDecision, 12);
    EventRecord rec{};
    rec.vk = 0x45;
    rec.stage = static_cast<std::uint16_t>(Stage::TotalEdit);
    rec.stageUs = 250;
    d.trace(rec);

    std::string report = d.report();
    // Header/system block.
    assert(report.find("KieeKey diagnostics report") != std::string::npos);
    assert(report.find("1.3.0-beta3") != std::string::npos);
    assert(report.find("Windows 11 Pro") != std::string::npos);
    assert(report.find("notepad.exe") != std::string::npos);
    // The keyboard total must be 80, and the mouse events must NOT be folded
    // into it (the beta2 mislabel, asserted on the report a user actually sends).
    assert(report.find("TỔNG sự kiện bàn phím: 80") != std::string::npos);
    assert(report.find("key.down): 40") != std::string::npos);
    assert(report.find("mouse.button): 3") != std::string::npos);
    // Latency section carries bucket edges, not fake precision.
    assert(report.find("total (edit key)") != std::string::npos);
    assert(report.find("[128-256)us=1") != std::string::npos);
    assert(report.find("p99=") != std::string::npos);
    // Traced events are listed in Full.
    assert(report.find("last traced events") != std::string::npos);
    assert(report.find("vk=0x45") != std::string::npos);
    // Verdict.
    assert(d.verdict().find("OK") != std::string::npos);

    // A producer fault must change the verdict (this is what makes the panel
    // "có ý nghĩa để fix hơn" instead of decorative).
    d.add(Counter::ProducerExceptions, 1);
    assert(d.verdict().find("LỖI") != std::string::npos);
    d.resetAll();
    d.add(Counter::QueueOverflowDropped, 2);
    assert(d.verdict().find("CẢNH BÁO") != std::string::npos);
    d.resetAll();
    d.record(Stage::TotalEdit, 50'000);        // 50 ms p99
    assert(d.verdict().find("CHẬM") != std::string::npos);

    // Off hides the trace section but still reports counters.
    d.resetAll();
    d.setLevel(Level::Off);
    d.add(Counter::KeyDown, 5);
    report = d.report();
    assert(report.find("last traced events") == std::string::npos);
    assert(report.find("key.down): 5") != std::string::npos);

    d.setLevel(Level::Basic);
    d.resetAll();
    std::cout << "  [PASS] report content + verdict escalation\n";
}

//---------------------------------------------------------------------------
void testConcurrentCounters() {
    // The hook thread, the consumer thread and the UI timer all touch this
    // singleton. Under TSan this is the check that the atomics are real.
    Diagnostics& d = Diagnostics::instance();
    d.resetAll();
    d.setLevel(Level::Full);
    constexpr int kThreads = 4;
    constexpr int kPerThread = 20000;
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&d, t] {
            for (int i = 0; i < kPerThread; ++i) {
                d.add(Counter::KeyDown, 1);
                d.record(Stage::HookToDecision, 5 + (i % 40));
                if (t % 2 == 0) {
                    EventRecord rec{};
                    rec.vk = static_cast<std::uint32_t>(i & 0xFF);
                    d.trace(rec);
                }
            }
        });
    }
    for (auto& worker : workers) { worker.join(); }
    assert(d.get(Counter::KeyDown) == static_cast<std::uint64_t>(kThreads) * kPerThread);
    assert(d.histogram(Stage::HookToDecision).total() ==
           static_cast<std::uint64_t>(kThreads) * kPerThread);
    assert(d.events().size() <= d.events().capacity());
    d.setLevel(Level::Basic);
    d.resetAll();
    std::cout << "  [PASS] 4 threads x 20k records: no lost counter update\n";
}

//---------------------------------------------------------------------------
void testFormatEventRecord() {
    EventRecord rec{};
    rec.seq = 17;
    rec.tickMs = 123456;
    rec.vk = 0x41;
    rec.chars = 2;
    rec.backspace = 1;
    rec.stage = static_cast<std::uint16_t>(Stage::HookToDecision);
    rec.stageUs = 33;
    rec.action = 0;
    rec.source = 0;
    rec.decision = 1;
    rec.flags = 0x04;
    const std::string line = formatEventRecord(rec);
    assert(line.find("#17") != std::string::npos);
    assert(line.find("t=123456ms") != std::string::npos);
    assert(line.find("kb down") != std::string::npos);
    assert(line.find("vk=0x41") != std::string::npos);
    assert(line.find("edit") != std::string::npos);
    assert(line.find("bs=1") != std::string::npos);
    assert(line.find("len=2") != std::string::npos);
    assert(line.find("hook->decision 33us") != std::string::npos);
    assert(line.find("flags=0x04") != std::string::npos);

    // Out-of-range enums must degrade, not read out of bounds.
    rec.source = 9;
    rec.action = 9;
    rec.decision = 9;
    rec.stage = 200;
    const std::string safe = formatEventRecord(rec);
    assert(safe.find("kb down") != std::string::npos);
    assert(safe.find("pass") != std::string::npos);
    assert(!safe.empty());
    std::cout << "  [PASS] trace line format (+ out-of-range enum safety)\n";
}

} // namespace

int main() {
    std::cout << "=== Running Diagnostics Suite ===\n";
    testHistogramBucketsAndPercentiles();
    testHistogramPercentileIsHonest();
    testKeyboardCounterIsKeyboardOnly();
    testLevelGating();
    testEventTraceOrderingAndDrain();
    testFileLogWritesAndRotates();
    testFlushTraceToFileLog();
    testInjectedDelay();
    testReportAndVerdict();
    testConcurrentCounters();
    testFormatEventRecord();
    std::cout << "=== ALL DIAGNOSTICS TESTS PASSED ===\n";
    return 0;
}
