// clock-free throughput driver: feeds the e2e passage/backspace storm through the engine N times
#include "TextEngine.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
using namespace ok::text;
static const char* kPassage =
    "Hom nay troi dep qua, chung toi di dao quanh ho Tay "
    "roi ngoi uong ca phe va noi chuyen ve du an moi. "
    "Tieng Viet co nhieu dau thanh: sac, huyen, hoi, nga, nang. "
    "Nguoi dung go nhanh thi do tre phai nho hon hai phan tram micro giay. "
    "He thong xu ly phim bang hang doi lock-free khong khoi tao bo nho dong. "
    "xin chaof cacs banj tooi laf nguowif vieejt nam hoom nay trowif ddepj quas "
    "chungs ta ddi chowi nhes camr own raast nhieeuf henj gawpj laij sau nhas "
    "the quick brown fox jumps over the lazy dog and keeps running through fields ";
static const char* kStorm = "chugns\bx\b\bungs concho\b\b\b\bong moongs\b\b\bing ";
int main(int argc, char** argv) {
    long keys = argc > 1 ? atol(argv[1]) : 20000000;
    int mode = argc > 2 ? atoi(argv[2]) : 0; // 0 = decision only, 1 = +encode
    std::string stream;
    long storm = 0;
    while ((long)stream.size() < keys) { if ((storm++ % 7) == 3) stream += kStorm; else stream += kPassage; }
    stream.resize(keys);
    EngineOptions o; o.digitsAreLiteral = true;
    TextEngine eng(o);
    std::wstring scratch;
    unsigned long long sink = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (char c : stream) {
        TextInput in;
        if (c == ' ') in.kind = InputKind::Space;
        else if (c == '\b') in.kind = InputKind::Backspace;
        else { in.kind = InputKind::Char; in.ch = (char32_t)c; in.isCaps = (c >= 'A' && c <= 'Z'); }
        const EngineResult& r = eng.process(in);
        sink += (unsigned)r.code + r.backspaceCount;
        if (mode) { eng.replacementUtf16(r, scratch); sink += scratch.size(); }
    }
    auto t1 = std::chrono::steady_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    std::printf("keys=%ld mode=%d total=%.1f ms  %.2f ns/key  sink=%llu\n", keys, mode, ns/1e6, ns/keys, sink);
}
