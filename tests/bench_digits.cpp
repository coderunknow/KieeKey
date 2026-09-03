// bench_digits.cpp — v1.1.2-r2 digit-path performance proof.
//
// The shipped fix (digitsAreLiteral=true) makes digits pass through the
// engine without composing. This benchmark measures the per-key cost of the
// digit path in every mode/configuration so the "no performance regression"
// claim is measured, not asserted:
//   * VNI + digits literal (SHIPPED default) — digit-heavy streams
//   * VNI + legacy composition               — digit-heavy streams
//   * Telex                                  — digit-heavy streams
//   * mixed real-world-ish streams (words + numbers + spaces) per mode
// Each key's engine decision is fully applied (backspaces simulated).
#include "TextEngine.hpp"
#include <chrono>
#include <cstdio>
#include <string>

using namespace ok::text;

namespace {

double benchStream(const EngineOptions& opts, const std::string& stream, int iters) {
    TextEngine engine(opts);
    volatile std::size_t sink = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int it = 0; it < iters; ++it) {
        for (const char c : stream) {
            if (c == ' ') {
                TextInput in;
                in.kind = InputKind::Space;
                sink += engine.process(in).newCharCount;
            } else {
                TextInput in;
                in.kind  = InputKind::Char;
                in.ch    = static_cast<char32_t>(static_cast<unsigned char>(c));
                const EngineResult& r = engine.process(in);
                if (r.backspaceCount != 0) { sink += r.backspaceCount; }
            }
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double ns =
        static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    return ns / (static_cast<double>(iters) * static_cast<double>(stream.size()));
}

void report(const char* label, const EngineOptions& opts, const std::string& stream) {
    const double ns = benchStream(opts, stream, 200);
    std::printf("  %-46s %8.0f ns/key\n", label, ns);
}

} // namespace

int main() {
    const std::string digits      = "2015 0901234567 100 250000 42 ";
    const std::string mixed       = "nam 2016 nhan5 d9 boqua0901234567 binh2 thang1 ";
    const std::string wordsOnly   = "xin chao ban toi di hoc ve nha an trua ";

    std::printf("KieeKey digit-path benchmark (lower is better)\n\n");

    EngineOptions vniLiteral; vniLiteral.inputMethod = InputMethod::Vni; vniLiteral.digitsAreLiteral = true;
    EngineOptions vniLegacy;  vniLegacy.inputMethod  = InputMethod::Vni; vniLegacy.digitsAreLiteral  = false;
    EngineOptions telex;      telex.inputMethod      = InputMethod::Telex;

    report("VNI literal (SHIPPED) - digit-heavy", vniLiteral, digits);
    report("VNI legacy compose   - digit-heavy", vniLegacy, digits);
    report("Telex                - digit-heavy", telex, digits);
    report("VNI literal (SHIPPED) - mixed", vniLiteral, mixed);
    report("VNI legacy compose   - mixed", vniLegacy, mixed);
    report("Telex                - mixed", telex, mixed);
    report("VNI literal (SHIPPED) - words only", vniLiteral, wordsOnly);
    report("Telex                - words only", telex, wordsOnly);

    std::printf("\nReference: 200 WPM typing = ~58,000,000 ns/key of human time.\n");
    return 0;
}
