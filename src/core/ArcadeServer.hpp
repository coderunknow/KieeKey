//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: src/core/ArcadeServer.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — ArcadeServer.hpp
// Local bridge that lets the HTML5 canvas client play the *same* C++ games:
//
//   browser (web/)  ──HTTP/JSON──►  ArcadeServer  ──►  ArcadeManager
//                                                        └─► Frame ─► RenderList
//
// The HTTP layer is deliberately tiny (no dependencies, no keep-alive, one
// request per connection) and the request handling is a plain function
// (`handleRequest`) so the whole API can be exercised head-less, without a
// socket, by tests/test_arcade_server.cpp.
//
// Security posture: the server binds to 127.0.0.1 by default, serves only files
// from the configured web root, rejects path traversal, and never executes
// anything from the request body. It exists so the Arcade Hub can also be
// played/tested on a machine that cannot build the Win32 front-end.
//----------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Arcade.hpp"
#include "ArcadeRender.hpp"

namespace ok::arcade {

struct ArcadeServerConfig {
    std::string host = "127.0.0.1";   // 0.0.0.0 to expose on the LAN
    int port = 8765;
    std::string webRoot = "web";      // where index.html / arcade.js live
    int targetFps = 60;               // simulation rate of the tick thread
};

//----------------------------------------------------------------------------
// A single HTTP response. `handleRequest()` fills this in; the socket layer
// only has to put it on the wire.
//----------------------------------------------------------------------------
struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
    std::string extraHeaders;   // appended verbatim ("X-Foo: bar\r\n")
};

class ArcadeServer {
public:
    static constexpr const char* kDefaultPort = "8765";

    explicit ArcadeServer(ArcadeServerConfig config = {});
    ~ArcadeServer();

    ArcadeServer(const ArcadeServer&) = delete;
    ArcadeServer& operator=(const ArcadeServer&) = delete;

    //---- lifecycle ---------------------------------------------------------
    bool start();                       // spawns the socket + tick threads
    void stop();
    [[nodiscard]] bool isRunning() const noexcept {
        return m_running.load(std::memory_order_acquire);
    }
    [[nodiscard]] int boundPort() const noexcept { return m_boundPort; }
    [[nodiscard]] const std::string& lastError() const noexcept { return m_lastError; }

    //---- transport-independent API (also the test entry point) -------------
    // method: "GET" / "POST"; path: "/api/state"; body: raw request body.
    [[nodiscard]] HttpResponse handleRequest(const std::string& method, const std::string& path,
                                             const std::string& body);

    //---- simulation --------------------------------------------------------
    // Advances the active game. Called by the tick thread, and directly by the
    // tests so a whole run can be simulated without wall-clock waiting.
    void tick(double dt);
    // Starts a game by slug ("snake", "tetris", ...). Returns false for an
    // unknown slug.
    bool startGame(const std::string& slug);
    void stopGame();
    void togglePause();
    void restartGame();
    // Forwards one key event (Win32 virtual-key code + produced character).
    InputResult sendInput(int vk, char32_t ch, bool down);
    // Key events still waiting to be consumed (diagnostics).
    [[nodiscard]] std::size_t pendingResults() const;

    [[nodiscard]] ArcadeManager& manager() noexcept { return m_manager; }
    [[nodiscard]] const ArcadeManager& manager() const noexcept { return m_manager; }

private:
    void acceptLoop();
    void tickLoop();
    // Serves one request. Returns true when the socket was handed over to the
    // event-stream channel (`/api/stream`), in which case the caller must not
    // close it.
    bool handleConnection(int socketHandle);
    // Pushes one SSE frame to every live stream subscriber.
    void serviceStreams();
    [[nodiscard]] bool writeAll(int socketHandle, const std::string& data);
    [[nodiscard]] std::string buildStateJson();
    [[nodiscard]] bool readStaticFile(const std::string& relativePath, std::string& out,
                                      std::string& contentTypeForFile) const;
    // Drains finished runs into the progression engine (single place where the
    // arcade -> progression mapping lives).
    void drainRunResults();

    ArcadeServerConfig m_config;
    ArcadeManager m_manager;
    mutable std::mutex m_gameMutex;      // guards manager update/frame/input
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::thread m_acceptThread;
    std::thread m_tickThread;
    int m_listenSocket = -1;
    int m_boundPort = 0;
    // Socket value + last successful write time, so a dead subscriber is
    // dropped instead of leaking a descriptor.
    mutable std::mutex m_streamMutex;
    std::vector<int> m_streamSockets;
    std::string m_lastError;
    std::string m_lastSlug;
    std::uint32_t m_runCounter = 0;
    double m_accumulatedSeconds = 0.0;
};

// Maps an arcade game type (1..8) to the progression recorder ids. Exposed for
// tests: the two enumerations must stay in lock-step with gameCatalog().
// Game id (1..8) used by the progression recorder for a game type. Implemented
// by ArcadeManager::progressionGameIdFor — kept as a forwarding declaration so
// existing callers keep working.
[[nodiscard]] int progressionGameIdFor(GameType type) noexcept;

} // namespace ok::arcade
