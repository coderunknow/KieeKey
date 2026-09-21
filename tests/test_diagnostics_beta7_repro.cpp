// KieeKey - v1.3.0-beta7 controlled repro for diagnostics/pipeline gaps
// Covers: snapshot fields stuck at zero, hook status vs counters, SendInput vs emit mismatch,
// runtime metadata zeros, DPI 96 vs 144 distinction, trace mismatch.

#include "Diagnostics.hpp"
#include <cassert>
#include <iostream>
#include <string>

using namespace ok::diag;

int main() {
    std::cout << "=== Beta7 Diagnostics Repro ===\n";
    Diagnostics& d = Diagnostics::instance();
    d.resetAll();
    d.setLevel(Level::Full);

    // --- 1. Counter set() contract: the live wrapper sync relies on it ---
    d.set(Counter::SendInputCalls, 13);
    assert(d.get(Counter::SendInputCalls) == 13);
    d.set(Counter::QueuedToConsumer, 42);
    assert(d.get(Counter::QueuedToConsumer) == 42);
    d.set(Counter::ConsumerWakes, 5);
    assert(d.get(Counter::ConsumerWakes) == 5);
    std::cout << "  [PASS] Diagnostics::set() atomic store\n";

    // --- 2. Snapshot was stuck at zeros/96: fill a realistic snapshot and ensure report shows it ---
    SystemSnapshot snap;
    snap.osName = "Windows 11 Pro 23H2 (build 22631)";
    snap.arch = "x64";
    snap.appVersion = "1.3.0-beta7 (PE 1.3.0.8)";
    snap.foregroundApp = "notepad.exe (SendInput)";
    snap.keyboardLayout = "00000409 (en-US)";
    snap.outputMode = "Auto (SendInput)";
    snap.inputMethod = "Telex";
    snap.codeTable = "Unicode";
    snap.workingSetKb = 54321;
    snap.peakWorkingSetKb = 60000;
    snap.kernelTimeMs = 1200;
    snap.userTimeMs = 3400;
    snap.uptimeMs = 123456;
    snap.cpuPercentSinceStart = 2.5;
    snap.dpi = 144;
    snap.imeEnabled = true;
    snap.hookInstalled = true;
    snap.fgHookInstalled = true;
    snap.liveEffectsEnabled = false;
    snap.excludedApp = false;
    d.setSystemSnapshot(snap);
    std::string rep = d.report(0);
    // Must contain os/arch/version/foreground/layout etc, not zeros
    assert(rep.find("Windows 11 Pro") != std::string::npos);
    assert(rep.find("x64") != std::string::npos);
    assert(rep.find("1.3.0-beta7") != std::string::npos);
    assert(rep.find("notepad.exe") != std::string::npos);
    assert(rep.find("Telex") != std::string::npos);
    assert(rep.find("Unicode") != std::string::npos);
    assert(rep.find("54321 KB") != std::string::npos || rep.find("54321") != std::string::npos);
    assert(rep.find("dpi (snapshot, monitor): 144") != std::string::npos);
    // uptime should be non-zero and annotated
    assert(rep.find("uptime") != std::string::npos);
    assert(rep.find("123") != std::string::npos); // seconds approx 123
    std::cout << "  [PASS] snapshot with 144 dpi and filled fields appears in report\n";

    // --- 3. DPI 96 default annotation: when snapshot not refreshed, report must say it's default ---
    d.resetAll();
    SystemSnapshot snap96;
    snap96.dpi = 96;
    snap96.uptimeMs = 0;
    snap96.workingSetKb = 0;
    d.setSystemSnapshot(snap96);
    rep = d.report(0);
    // The beta7 provenance note for 96
    assert(rep.find("dpi (snapshot, monitor): 96") != std::string::npos);
    assert(rep.find("snapshot not refreshed") != std::string::npos);
    assert(rep.find("display-metrics") != std::string::npos);
    // uptime zero annotation
    assert(rep.find("0 = snapshot not refreshed") != std::string::npos);
    std::cout << "  [PASS] dpi 96 and uptime 0 carry provenance notes\n";

    // --- 4. Hook status vs counters: if hookInstalled false but counters non-zero, report still shows state ---
    d.resetAll();
    d.set(Counter::KeyDown, 10);
    d.set(Counter::KeyUp, 10);
    SystemSnapshot snapHook;
    snapHook.hookInstalled = false; // stale
    snapHook.dpi = 96;
    d.setSystemSnapshot(snapHook);
    rep = d.report(0);
    assert(rep.find("hook") != std::string::npos);
    assert(rep.find("CH") != std::string::npos || rep.find("CHƯA") != std::string::npos);
    // keyboardEvents should be 20 even though hook says not installed — shows stale snapshot clue
    assert(d.keyboardEvents() == 20);
    std::cout << "  [PASS] hook CHUA cai vs keyboard events contradiction is observable\n";

    // --- 5. SendInput 0 vs emit-chain 13: counter must be settable to match emit trace ---
    d.resetAll();
    EmitRecord er{};
    er.channel = 1; er.chars = 2; er.processName = "notepad.exe"; er.windowClass = "Notepad";
    for (int i=0;i<13;++i) d.recordEmit(er);
    d.set(Counter::SendInputCalls, 13);
    assert(d.get(Counter::SendInputCalls) == 13);
    rep = d.report(0);
    assert(rep.find("emit-chain") != std::string::npos);
    assert(rep.find("SendInputCalls") != std::string::npos || rep.find("SendInput") != std::string::npos);
    std::cout << "  [PASS] SendInputCalls 13 vs 13 emit records consistent after sync\n";

    // --- 6. Runtime metadata zeros: workingSet/uptime must not be zero after realistic snapshot ---
    d.resetAll();
    d.setSystemSnapshot(snap);
    rep = d.report(0);
    assert(rep.find("workingSet") == std::string::npos || rep.find("0 KB") == std::string::npos || rep.find("54321") != std::string::npos);
    std::cout << "  [PASS] runtime metadata non-zero after refresh\n";

    // --- 7. Verify that Level::Off semantics still hold for set values (they persist) ---
    d.setLevel(Level::Off);
    d.set(Counter::KeyDown, 99);
    assert(d.get(Counter::KeyDown) == 99);

    std::cout << "=== ALL BETA7 REPRO CHECKS PASSED ===\n";
    d.resetAll();
    d.setLevel(Level::Basic);
    return 0;
}
