//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tools/arcade_serve.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey Arcade Hub — web bridge entry point.
//
//   arcade_serve --port 8765 --host 0.0.0.0 --web web
//
// Serves the HTML5 canvas client (web/) and drives the very same C++ engine
// that the Win32 front-end (src/app/ArcadeWindow.cpp) paints with GDI. Handy
// for testing the Arcade Hub on a machine without a Windows tool-chain, and
// for playing it from a phone on the same network (--host 0.0.0.0).
//----------------------------------------------------------------------------
#include "ArcadeServer.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_stop{false};

void onSignal(int) { g_stop.store(true, std::memory_order_relaxed); }

void printUsage() {
    std::printf(
        "KieeKey Arcade Hub — web bridge\n"
        "usage: arcade_serve [--port N] [--host ADDR] [--web DIR] [--fps N]\n"
        "  --port   TCP port (default 8765, 0 = pick a free one)\n"
        "  --host   bind address (default 0.0.0.0)\n"
        "  --web    directory that contains index.html (default web)\n"
        "  --fps    simulation rate (default 60)\n");
}

} // namespace

int main(int argc, char** argv) {
    ok::arcade::ArcadeServerConfig config;
    config.host = "0.0.0.0";
    config.port = 8765;
    config.webRoot = "web";
    config.targetFps = 60;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::printf("missing value for %s\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--port") {
            config.port = std::atoi(next("--port"));
        } else if (arg == "--host") {
            config.host = next("--host");
        } else if (arg == "--web") {
            config.webRoot = next("--web");
        } else if (arg == "--fps") {
            config.targetFps = std::atoi(next("--fps"));
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            std::printf("unknown option: %s\n", arg.c_str());
            printUsage();
            return 2;
        }
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    ok::arcade::ArcadeServer server(config);
    if (!server.start()) {
        std::printf("failed to start: %s\n", server.lastError().c_str());
        return 1;
    }

    std::printf("KieeKey Arcade Hub listening on http://%s:%d/\n", config.host.c_str(),
                server.boundPort());
    std::printf("  client   : http://localhost:%d/\n", server.boundPort());
    std::printf("  state    : http://localhost:%d/api/state\n", server.boundPort());
    std::printf("  stream   : http://localhost:%d/api/stream (server-sent events)\n",
                server.boundPort());
    std::printf("  catalog  : http://localhost:%d/api/catalog\n", server.boundPort());
    std::printf("Ctrl+C to stop.\n");
    std::fflush(stdout);

    while (!g_stop.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server.stop();
    std::printf("stopped.\n");
    return 0;
}
