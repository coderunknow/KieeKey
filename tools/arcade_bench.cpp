//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tools/arcade_bench.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — tools/arcade_bench.cpp
// Standardized benchmark for the v1.3.0 arcade stack.
//
// Measures, per game and for the whole front-end pipeline:
//   * game update cost            (ArcadeManager::update)
//   * frame build + display list  (buildRenderList)
//   * wire payload + JSON cost    (renderListToJson)
//   * HTTP request cost           (ArcadeServer::handleRequest, no sockets)
//   * input dispatch cost         (ArcadeManager::handleKey)
//   * steady-state allocations    (operator new counter)
//   * process RSS                 (/proc/self/statm when available)
//   * a determinism digest over the JSON frames of a scripted run
//
// Output is machine-readable `key<TAB>value` plus markdown tables, so the
// same run can be pasted into docs/bench/arcade-130/ARCADE_BENCH_REPORT.md.
//
// Usage: arcade_bench [--iters=N] [--frames=N] [--warmup=N] [--json]
//----------------------------------------------------------------------------
#include "Arcade.hpp"
#include "ArcadeFrame.hpp"
#include "ArcadeRender.hpp"
#include "ArcadeServer.hpp"
#include "ChaosEngine.hpp"
#include "Progression.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <new>
#include <string>
#include <vector>

//---------------------------------------------------------------------------
// Allocation accounting: global operator new/delete counters. The arcade
// stack must not allocate per frame in steady state (the frame model reserves
// its buffers once), which is what these counters prove.
//---------------------------------------------------------------------------
namespace {
std::atomic<std::uint64_t> g_allocations{0};
std::atomic<std::uint64_t> g_frees{0};
std::atomic<bool> g_countAllocations{false};
} // namespace

void* operator new(std::size_t size) {
    if (g_countAllocations.load(std::memory_order_relaxed)) {
        g_allocations.fetch_add(1, std::memory_order_relaxed);
    }
    void* pointer = std::malloc(size == 0 ? 1 : size);
    if (pointer == nullptr) {
        throw std::bad_alloc();
    }
    return pointer;
}

void operator delete(void* pointer) noexcept {
    if (pointer != nullptr && g_countAllocations.load(std::memory_order_relaxed)) {
        g_frees.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }

namespace {

using Clock = std::chrono::steady_clock;
using ok::arcade::ArcadeManager;
using ok::arcade::GameType;
using ok::arcade::RenderList;

struct Percentiles {
    double meanUs = 0.0;
    double p50Us = 0.0;
    double p95Us = 0.0;
    double p99Us = 0.0;
    double maxUs = 0.0;
};

Percentiles summarize(std::vector<double>& samples) {
    Percentiles out;
    if (samples.empty()) {
        return out;
    }
    double sum = 0.0;
    for (double sample : samples) {
        sum += sample;
    }
    out.meanUs = sum / static_cast<double>(samples.size());
    std::sort(samples.begin(), samples.end());
    const auto at = [&](double fraction) {
        const std::size_t index = static_cast<std::size_t>(
            fraction * static_cast<double>(samples.size() - 1) + 0.5);
        return samples[std::min(index, samples.size() - 1)];
    };
    out.p50Us = at(0.50);
    out.p95Us = at(0.95);
    out.p99Us = at(0.99);
    out.maxUs = samples.back();
    return out;
}

double micros(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::micro>(end - begin).count();
}

std::uint64_t fnv1a(const std::string& data) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char byte : data) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t currentRssBytes() {
#if defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    long pages = 0;
    long resident = 0;
    if (statm >> pages >> resident) {
        return static_cast<std::uint64_t>(resident) * 4096ull;
    }
#endif
    return 0;
}

std::string slugOf(GameType type) {
    return std::string(ok::arcade::gameSlug(static_cast<int>(type)));
}

//---------------------------------------------------------------------------
// One measured scenario.
//---------------------------------------------------------------------------
void benchGame(GameType type, int frames, int iters) {
    ArcadeManager& manager = ArcadeManager::instance();
    manager.stopGame();
    manager.launchGame(type, 0x1234u + static_cast<std::uint32_t>(type));

    RenderList list;
    ok::arcade::buildRenderList(manager.getFrame(), list);   // reserve once

    std::vector<double> updateUs;
    std::vector<double> buildUs;
    std::vector<double> jsonUs;
    updateUs.reserve(static_cast<std::size_t>(iters));
    buildUs.reserve(static_cast<std::size_t>(iters));
    jsonUs.reserve(static_cast<std::size_t>(iters));

    // A scripted, deterministic input stream so every game sees real work:
    // typing games get their own passage, the others get WASD/space.
    const std::u32string typed = U"KieeKey arcade benchmark passage 2026";
    std::size_t typedIndex = 0;
    const int keys[] = {'W', 'A', 'S', 'D', ' ', 'R', 'P'};
    int keyIndex = 0;

    std::uint64_t digest = 1469598103934665603ull;
    std::size_t lastCommands = 0;
    std::string lastJson;

    // Warm-up (also lets the games reach a steady state).
    for (int i = 0; i < 60; ++i) {
        manager.update(1.0 / 60.0);
        ok::arcade::buildRenderList(manager.getFrame(), list);
        (void)ok::arcade::renderListToJson(list);
    }

    for (int i = 0; i < iters; ++i) {
        if (manager.getCurrentGameType() == GameType::None &&
            manager.pendingRunResultCount() == 0) {
            manager.launchGame(type, 0x1234u + static_cast<std::uint32_t>(type));
        }

        // Input first (one key per frame, the harness's worst case).
        if (!typed.empty() && typedIndex < typed.size() * 4) {
            manager.handleKey(0, typed[typedIndex % typed.size()], true);
            ++typedIndex;
        } else {
            manager.handleKey(keys[keyIndex % 7], 0, true);
            ++keyIndex;
        }

        auto t0 = Clock::now();
        manager.update(1.0 / 60.0);
        auto t1 = Clock::now();
        ok::arcade::buildRenderList(manager.getFrame(), list);
        auto t2 = Clock::now();
        std::string json = ok::arcade::renderListToJson(list);
        auto t3 = Clock::now();

        updateUs.push_back(micros(t0, t1));
        buildUs.push_back(micros(t1, t2));
        jsonUs.push_back(micros(t2, t3));

        lastCommands = list.commands.size();
        lastJson = std::move(json);
        if ((i % 7) == 0) {
            digest ^= fnv1a(lastJson);
            digest *= 1099511628211ull;
        }
        if (i % 30 == 0) {
            manager.update(1.0 / 60.0);   // keep timers honest
        }
    }

    const Percentiles update = summarize(updateUs);
    const Percentiles build = summarize(buildUs);
    const Percentiles json = summarize(jsonUs);
    const ok::arcade::GameStats stats = manager.getFrame().stats;

    std::printf("game\t%s\n", slugOf(type).c_str());
    std::printf("title\t%s\n", std::string(ok::arcade::gameInfo(static_cast<int>(type))->nameEn)
                                   .c_str());
    std::printf("frames\t%d\n", frames);
    std::printf("iterations\t%d\n", iters);
    std::printf("update_mean_us\t%.3f\n", update.meanUs);
    std::printf("update_p50_us\t%.3f\n", update.p50Us);
    std::printf("update_p95_us\t%.3f\n", update.p95Us);
    std::printf("update_p99_us\t%.3f\n", update.p99Us);
    std::printf("update_max_us\t%.3f\n", update.maxUs);
    std::printf("build_mean_us\t%.3f\n", build.meanUs);
    std::printf("build_p99_us\t%.3f\n", build.p99Us);
    std::printf("json_mean_us\t%.3f\n", json.meanUs);
    std::printf("json_p99_us\t%.3f\n", json.p99Us);
    std::printf("frame_total_mean_us\t%.3f\n", update.meanUs + build.meanUs + json.meanUs);
    std::printf("frame_budget_pct_of_16667us\t%.3f\n",
                (update.meanUs + build.meanUs + json.meanUs) / 16666.7 * 100.0);
    std::printf("commands_last\t%zu\n", lastCommands);
    std::printf("json_bytes_last\t%zu\n", lastJson.size());
    std::printf("score\t%lld\n", static_cast<long long>(stats.score));
    std::printf("digest\t%016llx\n", static_cast<unsigned long long>(digest));
    std::printf("\n");

    manager.stopGame();
}

//---------------------------------------------------------------------------
// HTTP bridge cost (no sockets: handleRequest directly).
//---------------------------------------------------------------------------
void benchServer(int iters) {
    ok::arcade::ArcadeServerConfig config;
    config.port = 0;   // never started, only used for its manager/routes
    config.webRoot = "web";
    ok::arcade::ArcadeServer server(config);
    server.startGame("typing-race");

    std::vector<double> stateUs;
    std::vector<double> inputUs;
    std::vector<double> staticUs;
    stateUs.reserve(static_cast<std::size_t>(iters));
    inputUs.reserve(static_cast<std::size_t>(iters));
    staticUs.reserve(static_cast<std::size_t>(iters));

    for (int i = 0; i < 50; ++i) {   // warm-up
        (void)server.handleRequest("GET", "/api/state", "");
    }

    std::size_t stateBytes = 0;
    for (int i = 0; i < iters; ++i) {
        auto t0 = Clock::now();
        auto response = server.handleRequest("GET", "/api/state", "");
        auto t1 = Clock::now();
        stateUs.push_back(micros(t0, t1));
        stateBytes = response.body.size();

        auto t2 = Clock::now();
        (void)server.handleRequest("POST", "/api/input", "{\"ch\":\"K\",\"down\":true}");
        auto t3 = Clock::now();
        inputUs.push_back(micros(t2, t3));

        auto t4 = Clock::now();
        (void)server.handleRequest("GET", "/arcade.js", "");
        auto t5 = Clock::now();
        staticUs.push_back(micros(t4, t5));

        server.tick(1.0 / 60.0);
    }

    const Percentiles state = summarize(stateUs);
    const Percentiles input = summarize(inputUs);
    const Percentiles staticFile = summarize(staticUs);

    std::printf("game\thttp-bridge\n");
    std::printf("state_mean_us\t%.3f\n", state.meanUs);
    std::printf("state_p99_us\t%.3f\n", state.p99Us);
    std::printf("state_bytes\t%zu\n", stateBytes);
    std::printf("input_mean_us\t%.3f\n", input.meanUs);
    std::printf("input_p99_us\t%.3f\n", input.p99Us);
    std::printf("static_mean_us\t%.3f\n", staticFile.meanUs);
    std::printf("static_p99_us\t%.3f\n", staticFile.p99Us);
    std::printf("max_state_rate_hz\t%.0f\n", 1'000'000.0 / (state.meanUs > 0.0 ? state.meanUs : 1.0));
    std::printf("\n");
}

//---------------------------------------------------------------------------
// Steady-state allocation audit.
//
// Split into the three pipelines that actually exist:
//   A. game logic + display list      -> what the GDI hub does per frame
//   B. A + JSON serialization         -> what the web bridge does per frame
//   C. input dispatch (handleKey)
// Every buffer is warmed up first, so what remains is per-frame churn.
//---------------------------------------------------------------------------
void benchAllocations(int frames, GameType type) {
    ArcadeManager& manager = ArcadeManager::instance();
    RenderList list;
    manager.stopGame();
    manager.launchGame(type, 99u);

    for (int i = 0; i < 120; ++i) {   // warm-up: grow every buffer to its peak
        manager.update(1.0 / 60.0);
        manager.handleKey('A', U'a', true);
        ok::arcade::buildRenderList(manager.getFrame(), list);
        (void)ok::arcade::renderListToJson(list);
    }

    const auto measure = [&](int iterations, const auto& body) {
        const std::uint64_t before = g_allocations.load(std::memory_order_relaxed);
        g_countAllocations.store(true, std::memory_order_relaxed);
        for (int i = 0; i < iterations; ++i) {
            body();
        }
        g_countAllocations.store(false, std::memory_order_relaxed);
        return g_allocations.load(std::memory_order_relaxed) - before;
    };

    const std::uint64_t logic = measure(frames, [&] {
        manager.update(1.0 / 60.0);
        ok::arcade::buildRenderList(manager.getFrame(), list);
    });
    const std::uint64_t withJson = measure(frames, [&] {
        manager.update(1.0 / 60.0);
        ok::arcade::buildRenderList(manager.getFrame(), list);
        (void)ok::arcade::renderListToJson(list);
    });
    const std::uint64_t input = measure(frames, [&] {
        manager.handleKey('A', U'a', true);
    });

    std::printf("game\tsteady-state-allocations\n");
    std::printf("game_under_test\t%s\n", slugOf(type).c_str());
    std::printf("frames\t%d\n", frames);
    std::printf("allocations_logic_total\t%llu\n", static_cast<unsigned long long>(logic));
    std::printf("allocations_logic_per_frame\t%.4f\n",
                static_cast<double>(logic) / static_cast<double>(frames));
    std::printf("allocations_json_total\t%llu\n", static_cast<unsigned long long>(withJson));
    std::printf("allocations_json_per_frame\t%.4f\n",
                static_cast<double>(withJson) / static_cast<double>(frames));
    std::printf("allocations_input_per_key\t%.4f\n",
                static_cast<double>(input) / static_cast<double>(frames));
    std::printf("render_commands_capacity\t%zu\n", list.commands.capacity());
    std::printf("\n");
    manager.stopGame();
}

void printEnvironment() {
    std::printf("game\tenvironment\n");
#if defined(__linux__)
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.rfind("model name", 0) == 0) {
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::printf("cpu\t%s\n", line.substr(colon + 2).c_str());
            }
            break;
        }
    }
#endif
    std::printf("compiler\t%s\n",
#if defined(__clang__)
                "clang " __clang_version__
#elif defined(__GNUC__)
                "g++ " __VERSION__
#elif defined(_MSC_VER)
                "msvc " 
#endif
    );
    std::printf("build_date\t%s %s\n", __DATE__, __TIME__);
    std::printf("rss_bytes\t%llu\n", static_cast<unsigned long long>(currentRssBytes()));
    std::printf("\n");
}

int parseIntArg(int argc, char** argv, const char* name, int fallback) {
    const std::string prefix = std::string("--") + name + "=";
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument.rfind(prefix, 0) == 0) {
            return std::atoi(argument.substr(prefix.size()).c_str());
        }
    }
    return fallback;
}

const ok::arcade::GameType kGames[] = {
    GameType::Snake,     GameType::Tetris,  GameType::Fishing, GameType::TypingRace,
    GameType::WasdRace,  GameType::Rhythm,  GameType::NoMistake, GameType::Flexing,
};

} // namespace

int main(int argc, char** argv) {
    const int iters = parseIntArg(argc, argv, "iters", 4000);
    const int warmupFrames = parseIntArg(argc, argv, "warmup", 120);
    const int serverIters = parseIntArg(argc, argv, "server-iters", 2000);

    printEnvironment();
    std::printf("game\tparameters\n");
    std::printf("iterations\t%d\n", iters);
    std::printf("warmup_frames\t%d\n", warmupFrames);
    std::printf("\n");

    for (ok::arcade::GameType type : kGames) {
        benchGame(type, warmupFrames, iters);
    }
    benchServer(serverIters);
    benchAllocations(600, GameType::Tetris);   // the most variable frame

    std::printf("game\tallocations_total\n");
    std::printf("new\t%llu\n", static_cast<unsigned long long>(g_allocations.load()));
    std::printf("delete\t%llu\n", static_cast<unsigned long long>(g_frees.load()));
    return 0;
}
