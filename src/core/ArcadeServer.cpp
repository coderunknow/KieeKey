//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: src/core/ArcadeServer.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "ArcadeServer.hpp"

#include "AiRival.hpp"
#include "ChaosEngine.hpp"
#include "Progression.hpp"
#include "TypingAnalytics.hpp"
#include "Progression.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace ok::arcade {

namespace {

constexpr std::size_t kMaxRequestBytes = 64 * 1024;
constexpr int kListenBacklog = 16;
constexpr double kTickSeconds = 1.0 / 60.0;

void closeSocket(SocketHandle handle) {
#if defined(_WIN32)
    ::closesocket(handle);
#else
    ::close(handle);
#endif
}

// Escapes a UTF-8 byte string for embedding in a JSON string literal.
std::string jsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char raw : text) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04X", c);
                    out += buf;
                } else {
                    out += raw;   // UTF-8 continuation bytes pass through
                }
                break;
        }
    }
    return out;
}

int lastSocketError() {
#if defined(_WIN32)
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

bool socketLibraryInit() {
#if defined(_WIN32)
    static const bool initialized = [] {
        WSADATA data{};
        return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return initialized;
#else
    return true;
#endif
}

//---- tiny JSON reading helpers (the request bodies are ours, not arbitrary) --
bool jsonFindRaw(const std::string& json, const std::string& key, std::string& out) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return false;
    }
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' ||
                                 json[pos] == '\r')) {
        ++pos;
    }
    if (pos >= json.size()) {
        return false;
    }
    if (json[pos] == '"') {
        ++pos;
        std::string value;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                ++pos;
                switch (json[pos]) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case 'r': value += '\r'; break;
                    default: value += json[pos]; break;
                }
            } else {
                value += json[pos];
            }
            ++pos;
        }
        out = value;
        return true;
    }
    const std::size_t end = json.find_first_of(",}\n\r", pos);
    out = json.substr(pos, (end == std::string::npos) ? std::string::npos : end - pos);
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
    }
    return !out.empty();
}

bool jsonFindString(const std::string& json, const std::string& key, std::string& out) {
    return jsonFindRaw(json, key, out);
}

bool jsonFindInt(const std::string& json, const std::string& key, long long& out) {
    std::string raw;
    if (!jsonFindRaw(json, key, raw)) {
        return false;
    }
    try {
        out = std::stoll(raw);
    } catch (...) {
        return false;
    }
    return true;
}

bool jsonFindBool(const std::string& json, const std::string& key, bool& out) {
    std::string raw;
    if (!jsonFindRaw(json, key, raw)) {
        return false;
    }
    out = (raw == "true" || raw == "1");
    return true;
}

std::string jsonError(const std::string& message) {
    std::string out = "{\"ok\":false,\"error\":\"";
    for (char c : message) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    out += "\"}";
    return out;
}

std::string jsonOk() { return "{\"ok\":true}"; }

std::string contentTypeForExtension(const std::string& path) {
    auto endsWith = [&path](const char* suffix) {
        const std::size_t n = std::strlen(suffix);
        return path.size() >= n && path.compare(path.size() - n, n, suffix) == 0;
    };
    if (endsWith(".html") || endsWith(".htm")) return "text/html; charset=utf-8";
    if (endsWith(".js") || endsWith(".mjs")) return "text/javascript; charset=utf-8";
    if (endsWith(".css")) return "text/css; charset=utf-8";
    if (endsWith(".json")) return "application/json; charset=utf-8";
    if (endsWith(".svg")) return "image/svg+xml";
    if (endsWith(".png")) return "image/png";
    if (endsWith(".ico")) return "image/x-icon";
    if (endsWith(".woff2")) return "font/woff2";
    return "text/plain; charset=utf-8";
}

} // namespace

int progressionGameIdFor(GameType type) noexcept {
    return ArcadeManager::progressionGameIdFor(type);
}

//===========================================================================
// Construction
//===========================================================================
ArcadeServer::ArcadeServer(ArcadeServerConfig config) : m_config(std::move(config)) {
    if (m_config.port <= 0) {
        m_config.port = 8765;
    }
    if (m_config.targetFps <= 0) {
        m_config.targetFps = 60;
    }
}

ArcadeServer::~ArcadeServer() {
    stop();
}

//===========================================================================
// Simulation
//===========================================================================
void ArcadeServer::tick(double dt) {
    if (!(dt > 0.0)) {
        return;
    }
    const double clamped = std::min(dt, 0.25);   // long stalls must not teleport
    std::lock_guard<std::mutex> lock(m_gameMutex);
    m_manager.update(clamped);
    m_accumulatedSeconds += clamped;
    drainRunResults();
}

InputResult ArcadeServer::sendInput(int vk, char32_t ch, bool down) {
    std::lock_guard<std::mutex> lock(m_gameMutex);
    const InputResult result = m_manager.handleKey(vk, ch, down);
    drainRunResults();
    return result;
}

std::size_t ArcadeServer::pendingResults() const {
    std::lock_guard<std::mutex> lock(m_gameMutex);
    return m_manager.pendingRunResultCount();
}

void ArcadeServer::drainRunResults() {
    // Shared with the desktop hub (ArcadeManager::drainRunResultsToProgression):
    // one implementation, so the two front-ends can never credit differently.
    m_runCounter += static_cast<std::uint32_t>(m_manager.drainRunResultsToProgression());
}

bool ArcadeServer::startGame(const std::string& slug) {
    const int typeId = gameTypeFromSlug(slug);
    if (typeId <= 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_gameMutex);
    const bool launched = m_manager.launchGame(static_cast<GameType>(typeId),
                                               static_cast<std::uint32_t>(m_runCounter + 1u));
    if (launched) {
        m_lastSlug = slug;
        m_accumulatedSeconds = 0.0;
        ++m_runCounter;
    }
    return launched;
}

void ArcadeServer::stopGame() {
    std::lock_guard<std::mutex> lock(m_gameMutex);
    m_manager.stopGame();
    drainRunResults();
}

void ArcadeServer::togglePause() {
    std::lock_guard<std::mutex> lock(m_gameMutex);
    const InputEvent ev = [] {
        InputEvent e{};
        e.vk = vk::kPause;
        e.down = true;
        return e;
    }();
    (void)m_manager.handleKey(ev);
}

void ArcadeServer::restartGame() {
    std::lock_guard<std::mutex> lock(m_gameMutex);
    m_manager.restartGame();
}

//===========================================================================
// State / API
//===========================================================================
std::string ArcadeServer::buildStateJson(bool consumeFlexEmit) {
    std::lock_guard<std::mutex> lock(m_gameMutex);
    const Frame& frame = m_manager.getFrame();
    RenderList list;
    buildRenderList(frame, list);

    std::string out = "{\"ok\":true,\"active\":";
    out += m_manager.hasActiveGame() ? "true" : "false";
    out += ",\"slug\":\"";
    const GameType current = m_manager.getCurrentGameType();
    if (current != GameType::None) {
        out += std::string(gameSlug(static_cast<int>(current)));
    }
    out += "\",\"gameId\":";
    out += std::to_string(static_cast<int>(current));
    out += ",\"paused\":";
    out += (m_manager.getCurrentGame() != nullptr && m_manager.getCurrentGame()->isPaused())
               ? "true"
               : "false";
    out += ",\"gameOver\":";
    out += (m_manager.getCurrentGame() != nullptr && m_manager.getCurrentGame()->isGameOver())
               ? "true"
               : "false";
    out += ",\"elapsed\":";
    out += std::to_string(static_cast<long long>(m_accumulatedSeconds * 1000.0));
    out += ",\"frame\":";
    out += renderListToJson(list);

    // Flexing Mode is the one game whose whole point is the text it produces:
    // hand the freshly generated characters to the client so it can type them
    // into a real editable control ("gõ thật" mode).
    if (current == GameType::Flexing) {
        auto* flex = dynamic_cast<FlexingGame*>(m_manager.getCurrentGame());
        if (flex != nullptr) {
            std::string emitted;
            if (consumeFlexEmit) {
                const std::u32string raw = flex->popEmittedOutput();
                if (!raw.empty()) {
                    utf8FromUtf32(raw, emitted);
                }
            }
            out += ",\"flex\":{\"emitted\":\"";
            out += jsonEscape(emitted);
            out += "\",\"wpm\":";
            out += std::to_string(static_cast<long long>(flex->getDisplayedWpm() + 0.5));
            out += ",\"cursor\":";
            out += std::to_string(static_cast<long long>(flex->getCursor()));
            out += ",\"total\":";
            out += std::to_string(static_cast<long long>(flex->getPreloadedText().size()));
            out += ",\"generated\":";
            out += std::to_string(static_cast<long long>(flex->getGeneratedChars()));
            out += ",\"keys\":";
            out += std::to_string(static_cast<long long>(flex->getActualKeypresses()));
            out += ",\"efficiency\":";
            out += std::to_string(flex->getEfficiencyMultiplier());
            out += '}';
        }
    }
    out += '}';
    return out;
}

HttpResponse ArcadeServer::handleRequest(const std::string& method, const std::string& path,
                                        const std::string& body) {
    HttpResponse response;

    // Strip a query string: the client never needs one, but a stray "?v=2"
    // must not turn into a 404.
    const std::size_t queryPos = path.find('?');
    const std::string route = (queryPos == std::string::npos) ? path : path.substr(0, queryPos);

    const bool isGet = (method == "GET" || method == "HEAD");
    const bool isPost = (method == "POST");
    if (isGet && (route == "/api/state" || route == "/api/status")) {
        // /api/status is the polling fallback: it reports the Flexing buffer
        // without consuming it, so a client that polls and streams at the same
        // time cannot steal characters from itself.
        response.body = buildStateJson(route == "/api/state");
        return response;
    }
    if (isGet && route == "/api/catalog") {
        response.body = gameCatalogToJson();
        return response;
    }
    if (isGet && route == "/api/ping") {
        response.body = "{\"ok\":true,\"pong\":true}";
        return response;
    }
    if (isPost && route == "/api/start") {
        std::string slug;
        if (!jsonFindString(body, "slug", slug)) {
            long long id = 0;
            if (jsonFindInt(body, "id", id) && id > 0 && id <= 8) {
                slug = std::string(gameSlug(static_cast<int>(id)));
            }
        }
        if (slug.empty() || !startGame(slug)) {
            response.status = 400;
            response.body = jsonError("unknown game slug");
            return response;
        }
        response.body = std::string("{\"ok\":true,\"started\":\"") + slug + "\"}";
        return response;
    }
    if (isPost && route == "/api/stop") {
        stopGame();
        response.body = jsonOk();
        return response;
    }
    if (isPost && route == "/api/pause") {
        togglePause();
        response.body = jsonOk();
        return response;
    }
    if (isPost && route == "/api/restart") {
        restartGame();
        response.body = jsonOk();
        return response;
    }
    if (isPost && route == "/api/input") {
        long long vk = 0;
        bool down = true;
        std::string chText;
        (void)jsonFindInt(body, "vk", vk);
        (void)jsonFindBool(body, "down", down);
        std::string code;
        if (jsonFindString(body, "ch", code)) {
            chText = code;
        }
        const std::u32string chars = utf32FromUtf8(chText);
        const char32_t ch = chars.empty() ? 0 : chars.front();
        const InputResult result = sendInput(static_cast<int>(vk), ch, down);
        response.body = std::string("{\"ok\":true,\"result\":") +
                        std::to_string(static_cast<int>(result)) + "}";
        return response;
    }
    if (isPost && route == "/api/text") {
        // Free-form line used by the typing games (the client sends the typed
        // character rather than a key code, so non-US layouts work too).
        std::string text;
        if (!jsonFindString(body, "text", text)) {
            response.status = 400;
            response.body = jsonError("missing text");
            return response;
        }
        const std::u32string chars = utf32FromUtf8(text);
        InputResult last = InputResult::NotConsumed;
        for (char32_t ch : chars) {
            last = sendInput(0, ch, true);
        }
        response.body = std::string("{\"ok\":true,\"result\":") +
                        std::to_string(static_cast<int>(last)) + "}";
        return response;
    }
    if (isPost && route == "/api/preload") {
        // Flexing Mode pipeline: the client supplies the text KieeKey will type
        // on the user's behalf, plus how much of it each keypress produces.
        std::string text;
        long long granularity = -1;
        long long nChars = 0;
        const bool haveText = jsonFindString(body, "text", text);
        (void)jsonFindInt(body, "granularity", granularity);
        (void)jsonFindInt(body, "nChars", nChars);
        if (!haveText && granularity < 0) {
            response.status = 400;
            response.body = jsonError("missing text or granularity");
            return response;
        }
        std::lock_guard<std::mutex> lock(m_gameMutex);
        auto* flex = dynamic_cast<FlexingGame*>(m_manager.getCurrentGame());
        if (flex == nullptr) {
            response.status = 409;
            response.body = jsonError("flexing mode is not the active game");
            return response;
        }
        if (haveText) {
            flex->setPreloadedText(utf32FromUtf8(text));
            flex->reset();
        }
        if (granularity >= 0 && granularity <= 3) {
            flex->setGranularity(static_cast<FlexGranularity>(granularity),
                                 static_cast<std::uint32_t>(nChars <= 0 ? 3 : nChars));
        }
        response.body = std::string("{\"ok\":true,\"total\":") +
                        std::to_string(static_cast<long long>(flex->getPreloadedText().size())) +
                        ",\"granularity\":" +
                        std::to_string(static_cast<int>(flex->getGranularity())) + "}";
        return response;
    }
    if ((isGet || isPost) && route == "/api/progression") {
        // "Type a lot to level up" is part of the feature set, so the web HUD
        // reads the very same ProgressionEngine the desktop app persists.
        auto& progress = ok::progression::ProgressionEngine::instance();
        if (isPost) {
            bool flag = false;
            if (jsonFindBool(body, "flush", flag) && flag) {
                progress.flushStats();
            }
        }
        const ok::progression::ProgressionStats stats = progress.getStats();
        const std::uint32_t level = ok::progression::ProgressionEngine::calculateLevel(stats.totalXp);
        const std::uint64_t levelFloor =
            ok::progression::ProgressionEngine::xpRequiredForLevel(level);
        const std::uint64_t levelCeil =
            ok::progression::ProgressionEngine::xpRequiredForLevel(level + 1);
        const std::uint64_t span = (levelCeil > levelFloor) ? (levelCeil - levelFloor) : 1;
        const std::uint64_t into = (stats.totalXp > levelFloor) ? (stats.totalXp - levelFloor) : 0;
        const int intoPercent = static_cast<int>(
            std::min<std::uint64_t>(100, (into * 100) / (span == 0 ? 1 : span)));
        response.body =
            std::string("{\"ok\":true,\"level\":") + std::to_string(level) +
            ",\"xp\":" + std::to_string(stats.totalXp) +
            ",\"xpIntoLevel\":" + std::to_string(into) +
            ",\"xpForLevel\":" + std::to_string(span) +
            ",\"levelPercent\":" + std::to_string(intoPercent) +
            ",\"bestWpm\":" + std::to_string(stats.bestWpm) +
            ",\"bestAccuracy\":" + std::to_string(stats.bestAccuracy) +
            ",\"totalKeystrokes\":" + std::to_string(stats.totalKeystrokes) +
            ",\"totalWords\":" + std::to_string(stats.totalWords) +
            ",\"typingTimeSeconds\":" + std::to_string(stats.typingTimeSeconds) +
            ",\"streakDays\":" + std::to_string(stats.currentStreakDays) +
            ",\"achievements\":" + std::to_string(progress.getUnlockedAchievements().size()) +
            ",\"typingRaceBestWpm\":" + std::to_string(stats.typingRaceBestWpm) +
            ",\"rhythmHighScore\":" + std::to_string(stats.rhythmHighScore) +
            ",\"noMistakeMaxCombo\":" + std::to_string(stats.noMistakeMaxCombo) + "}";
        return response;
    }
    if ((isGet || isPost) && route == "/api/rival") {
        // The AI rival is opt-in; this route is the browser's only way to see
        // (and train) the learned profile, and it never invents numbers.
        auto& rival = ok::ai::AiRivalEngine::instance();
        if (isPost) {
            bool flag = false;
            if (jsonFindBool(body, "optIn", flag)) {
                rival.setOptIn(flag);
            }
            if (jsonFindBool(body, "reset", flag) && flag) {
                rival.resetProfile();
            }
            if (jsonFindBool(body, "train", flag) && flag) {
                rival.trainBatch();
            }
        }
        const ok::ai::AiProfile profile = rival.getProfile();
        const double meanIki = profile.meanIkiMs > 0.0 ? profile.meanIkiMs : 150.0;
        // 5 chars per word, standard WPM definition, from the learned IKI.
        const double profileWpm = (meanIki > 0.0) ? (60000.0 / (meanIki * 5.0)) : 0.0;

        // Race preview against the passage the typing race currently uses, so
        // "it learns you and then races to beat you" is visible in numbers.
        std::size_t passageChars = 0;
        double rivalFinishSec = 0.0;
        double rivalWpm = 0.0;
        {
            std::lock_guard<std::mutex> lock(m_gameMutex);
            auto* race = dynamic_cast<TypingRaceGame*>(m_manager.getCurrentGame());
            if (race != nullptr) {
                const std::u32string passage(race->getPassage());
                passageChars = passage.size();
                ok::ai::AiRaceConfig raceConfig;
                raceConfig.aggression = 1.05;   // 5 % faster than the learned pace
                ok::ai::AiRacer racer = rival.makeRacer(passage, raceConfig);
                for (int step = 0; step < 60000 && !racer.isFinished(); ++step) {
                    racer.update(kTickSeconds);
                }
                rivalFinishSec = racer.getFinishTimeSec();
                rivalWpm = racer.getWpm();
            }
        }
        response.body =
            std::string("{\"ok\":true,\"optIn\":") + (rival.isOptIn() ? "true" : "false") +
            ",\"samples\":" + std::to_string(profile.sampleCount) +
            ",\"pendingObservations\":" + std::to_string(rival.pendingObservationCount()) +
            ",\"meanIkiMs\":" + std::to_string(meanIki) +
            ",\"errorRate\":" + std::to_string(profile.errorRate) +
            ",\"toneDelayMs\":" + std::to_string(profile.toneDelayMs) +
            ",\"profileWpm\":" + std::to_string(profileWpm) +
            ",\"ghostSamples\":" + std::to_string(rival.getYesterdayGhost().size()) +
            ",\"passageChars\":" + std::to_string(passageChars) +
            ",\"rivalFinishSec\":" + std::to_string(rivalFinishSec) +
            ",\"rivalWpm\":" + std::to_string(rivalWpm) + "}";
        return response;
    }
    if ((isGet || isPost) && (route == "/api/chaos" || route == "/api/chaos/preview")) {
        // Chaos Mode lab. The transformation itself always runs in the shared
        // C++ engine (ChaosEngine), never in JavaScript: the browser only shows
        // what the engine produced, so the lab cannot drift from the real
        // typing path.
        auto& chaos = ok::chaos::ChaosEngine::instance();
        if (isPost && route == "/api/chaos") {
            ok::chaos::ChaosConfig config = chaos.getConfig();
            long long value = 0;
            bool flag = false;
            if (jsonFindBool(body, "enabled", flag) || jsonFindBool(body, "masterEnabled", flag)) {
                config.masterEnabled = flag;
            }
            if (jsonFindBool(body, "randomCase", flag) || jsonFindBool(body, "randomCaseEnabled", flag)) {
                config.randomCaseEnabled = flag;
            }
            if (jsonFindInt(body, "caseIntensityPercent", value)) {
                const long long clamped = std::clamp(value, 0LL, 100LL);
                config.randomCaseIntensity = static_cast<float>(clamped) / 100.0f;
            }
            if (jsonFindInt(body, "caseGranularity", value) && value >= 0 && value <= 1) {
                config.caseGranularity = (value == 0) ? ok::chaos::CaseGranularity::ByChar
                                                      : ok::chaos::CaseGranularity::ByWord;
            }
            if (jsonFindBool(body, "glyph", flag) || jsonFindBool(body, "glyphTransformEnabled", flag)) {
                config.glyphTransformEnabled = flag;
            }
            if (jsonFindInt(body, "glyphMode", value) && value >= 0 && value <= 6) {
                config.glyphMode = static_cast<ok::chaos::GlyphTransformMode>(value);
            }
            if (jsonFindInt(body, "glyphIntensityPercent", value)) {
                const long long clamped = std::clamp(value, 0LL, 100LL);
                config.glyphIntensity = static_cast<float>(clamped) / 100.0f;
            }
            if (jsonFindBool(body, "rotateRenderOnly", flag)) {
                config.rotateRenderOnly = flag;
            }
            chaos.setConfig(config);
        }

        const ok::chaos::ChaosConfig now = chaos.getConfig();
        std::string preview;
        bool renderOnly = false;
        if (isPost && route == "/api/chaos/preview") {
            std::string text;
            (void)jsonFindString(body, "text", text);
            const std::u32string source =
                utf32FromUtf8(text.empty() ? std::string("KieeKey Chaos Mode - go thu that!") : text);
            const std::u32string injected = chaos.processCase(source, 0xC4A0);
            const std::u32string display = chaos.getVisualDisplayString(source, 0xC4A0);
            renderOnly = ok::chaos::ChaosEngine::isRenderOnlyRotation(now.glyphMode);
            std::string injectedUtf8;
            std::string displayUtf8;
            utf8FromUtf32(injected, injectedUtf8);
            utf8FromUtf32(display, displayUtf8);
            preview = "{\"injected\":\"" + jsonEscape(injectedUtf8) +
                      "\",\"display\":\"" + jsonEscape(displayUtf8) + "\"}";
        }
        response.body = std::string("{\"ok\":true,\"active\":") +
                        (chaos.isChaosActive() ? "true" : "false") +
                        ",\"enabled\":" + (now.masterEnabled ? "true" : "false") +
                        ",\"randomCase\":" + (now.randomCaseEnabled ? "true" : "false") +
                        ",\"caseIntensityPercent\":" +
                        std::to_string(static_cast<int>(now.randomCaseIntensity * 100.0f + 0.5f)) +
                        ",\"caseGranularity\":" +
                        std::to_string(static_cast<int>(now.caseGranularity)) +
                        ",\"glyph\":" + (now.glyphTransformEnabled ? "true" : "false") +
                        ",\"glyphMode\":" + std::to_string(static_cast<int>(now.glyphMode)) +
                        ",\"glyphIntensityPercent\":" +
                        std::to_string(static_cast<int>(now.glyphIntensity * 100.0f + 0.5f)) +
                        ",\"rotateRenderOnly\":" + (now.rotateRenderOnly ? "true" : "false") +
                        ",\"renderOnlyMode\":" + (renderOnly ? "true" : "false");
        if (!preview.empty()) {
            response.body += ",\"preview\":" + preview;
        }
        response.body += '}';
        return response;
    }
    if (isPost && route == "/api/config") {
        long long value = 0;
        ArcadeConfig config = m_manager.getConfig();
        bool changed = false;
        if (jsonFindInt(body, "rhythmFailMode", value)) {
            config.rhythmFailMode = (value == 0) ? FailMode::Hardcore : FailMode::HealthBar;
            changed = true;
        }
        if (jsonFindInt(body, "noMistakeFailMode", value)) {
            config.noMistakeFailMode = (value == 0) ? FailMode::Hardcore : FailMode::HealthBar;
            changed = true;
        }
        if (jsonFindInt(body, "rhythmBpm", value) && value >= 40 && value <= 400) {
            config.rhythmBpm = static_cast<double>(value);
            changed = true;
        }
        if (jsonFindInt(body, "typingRacePacerWpm", value) && value >= 0 && value <= 300) {
            config.typingRacePacerWpm = static_cast<double>(value);
            changed = true;
        }
        if (changed) {
            std::lock_guard<std::mutex> lock(m_gameMutex);
            m_manager.setConfig(config);
        }
        const ArcadeConfig now = m_manager.getConfig();
        response.body = std::string("{\"ok\":true,\"config\":{\"rhythmFailMode\":") +
                        (now.rhythmFailMode == FailMode::Hardcore ? "0" : "1") +
                        ",\"noMistakeFailMode\":" +
                        (now.noMistakeFailMode == FailMode::Hardcore ? "0" : "1") +
                        ",\"rhythmBpm\":" + std::to_string(static_cast<int>(now.rhythmBpm)) +
                        ",\"typingRacePacerWpm\":" +
                        std::to_string(static_cast<int>(now.typingRacePacerWpm)) + "}}";
        return response;
    }

    if (isGet && route == "/healthz") {
        response.contentType = "text/plain; charset=utf-8";
        response.body = "ok";
        return response;
    }

    // Anything under /api/ that reached this point is either unknown or was
    // called with the wrong verb; never let it fall through to the file server.
    if (route.rfind("/api/", 0) == 0) {
        // 405 when the verb is wrong (the route exists, the method does not),
        // 404 when the route itself does not exist.
        const bool knownRoute =
            route == "/api/state" || route == "/api/status" || route == "/api/catalog" ||
            route == "/api/ping" || route == "/api/start" || route == "/api/stop" ||
            route == "/api/pause" || route == "/api/restart" || route == "/api/input" ||
            route == "/api/text" || route == "/api/config" || route == "/api/preload" ||
            route == "/api/chaos" || route == "/api/chaos/preview" ||
            route == "/api/progression" || route == "/api/rival";
        response.status = knownRoute ? 405 : 404;
        response.body = jsonError(knownRoute ? "method not allowed for this route"
                                             : "unknown api route");
        return response;
    }

    //---- static files ------------------------------------------------------
    if (isGet) {
        std::string relative = route;
        if (relative == "/" || relative.empty()) {
            relative = "/index.html";
        }
        std::string fileBody;
        std::string type;
        if (!readStaticFile(relative, fileBody, type)) {
            response.status = 404;
            response.contentType = "text/plain; charset=utf-8";
            response.body = "404 not found: " + relative;
            return response;
        }
        response.contentType = type;
        response.body = method == "HEAD" ? std::string() : std::move(fileBody);
        return response;
    }

    response.status = 405;
    response.body = jsonError("method not allowed");
    return response;
}

bool ArcadeServer::readStaticFile(const std::string& relativePath, std::string& out,
                                  std::string& contentTypeForFile) const {
    // Path traversal guard: only plain relative names inside the web root.
    if (relativePath.find("..") != std::string::npos) {
        return false;
    }
    std::string relative = relativePath;
    while (!relative.empty() && relative.front() == '/') {
        relative.erase(relative.begin());
    }
    if (relative.empty()) {
        return false;
    }
    const std::string full =
        m_config.webRoot.empty() ? relative : (m_config.webRoot + "/" + relative);
    std::ifstream ifs(full, std::ios::binary);
    if (!ifs.is_open()) {
        return false;
    }
    out.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    contentTypeForFile = contentTypeForExtension(relative);
    return true;
}

//===========================================================================
// Threads
//===========================================================================
bool ArcadeServer::start() {
    if (m_running.load(std::memory_order_acquire)) {
        return true;
    }
    if (!socketLibraryInit()) {
        m_lastError = "socket library initialisation failed";
        return false;
    }

    SocketHandle listenSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == kInvalidSocket) {
        m_lastError = "socket() failed: " + std::to_string(lastSocketError());
        return false;
    }

    int reuse = 1;
    ::setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
                 sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(m_config.port));
    if (::inet_pton(AF_INET, m_config.host.c_str(), &address.sin_addr) != 1) {
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    if (::bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        m_lastError = "bind() failed on " + m_config.host + ":" +
                      std::to_string(m_config.port) + ": " + std::to_string(lastSocketError());
        closeSocket(listenSocket);
        return false;
    }

    // Port 0 = "pick a free port" (used by the tests).
    sockaddr_in bound{};
#if defined(_WIN32)
    int boundLength = sizeof(bound);
#else
    socklen_t boundLength = sizeof(bound);
#endif
    if (::getsockname(listenSocket, reinterpret_cast<sockaddr*>(&bound), &boundLength) == 0) {
        m_boundPort = ntohs(bound.sin_port);
    } else {
        m_boundPort = m_config.port;
    }

    if (::listen(listenSocket, kListenBacklog) != 0) {
        m_lastError = "listen() failed: " + std::to_string(lastSocketError());
        closeSocket(listenSocket);
        return false;
    }

    m_listenSocket = static_cast<int>(listenSocket);
    m_stopRequested.store(false, std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    m_acceptThread = std::thread([this] { acceptLoop(); });
    m_tickThread = std::thread([this] { tickLoop(); });
    return true;
}

void ArcadeServer::stop() {
    if (!m_running.load(std::memory_order_acquire)) {
        return;
    }
    m_stopRequested.store(true, std::memory_order_release);
    m_running.store(false, std::memory_order_release);
    if (m_listenSocket != -1) {
        closeSocket(static_cast<SocketHandle>(m_listenSocket));
        m_listenSocket = -1;
    }
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        for (int socketHandle : m_streamSockets) {
            closeSocket(static_cast<SocketHandle>(socketHandle));
        }
        m_streamSockets.clear();
    }
    if (m_acceptThread.joinable()) {
        m_acceptThread.join();
    }
    if (m_tickThread.joinable()) {
        m_tickThread.join();
    }
}

void ArcadeServer::tickLoop() {
    using clock = std::chrono::steady_clock;
    const double frameSeconds = 1.0 / static_cast<double>(m_config.targetFps);
    auto previous = clock::now();
    while (!m_stopRequested.load(std::memory_order_acquire)) {
        const auto now = clock::now();
        double elapsed = std::chrono::duration<double>(now - previous).count();
        previous = now;
        if (elapsed < 0.0) {
            elapsed = 0.0;
        }
        if (elapsed > 0.25) {
            elapsed = 0.25;
        }
        if (elapsed > 0.0) {
            tick(elapsed);
        }
        const auto wakeAt = clock::now() + std::chrono::duration_cast<clock::duration>(
                                                std::chrono::duration<double>(frameSeconds));
        std::this_thread::sleep_until(wakeAt);
    }
}

void ArcadeServer::acceptLoop() {
    while (!m_stopRequested.load(std::memory_order_acquire)) {
        fd_set readSet;
        FD_ZERO(&readSet);
        const int listenSocket = m_listenSocket;
        if (listenSocket == -1) {
            break;
        }
        FD_SET(static_cast<SocketHandle>(listenSocket), &readSet);
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 200 * 1000;
        const int ready = ::select(listenSocket + 1, &readSet, nullptr, nullptr, &timeout);
        // Subscribers are served on every pass, including the idle ones: the
        // stream must keep flowing even when nobody is connecting.
        serviceStreams();
        if (ready <= 0) {
            continue;   // timeout or interrupted: re-check the stop flag
        }
        sockaddr_in client{};
#if defined(_WIN32)
        int clientLength = sizeof(client);
#else
        socklen_t clientLength = sizeof(client);
#endif
        const SocketHandle clientSocket =
            ::accept(static_cast<SocketHandle>(listenSocket), reinterpret_cast<sockaddr*>(&client),
                     &clientLength);
        if (clientSocket == kInvalidSocket) {
            continue;
        }
        // A client that connects and then says nothing must not stall the loop.
        timeval receiveTimeout{};
        receiveTimeout.tv_sec = 2;
        receiveTimeout.tv_usec = 0;
        ::setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&receiveTimeout), sizeof(receiveTimeout));
        const bool streaming = handleConnection(static_cast<int>(clientSocket));
        if (streaming) {
            std::lock_guard<std::mutex> lock(m_streamMutex);
            m_streamSockets.push_back(static_cast<int>(clientSocket));
        }
    }
}

bool ArcadeServer::writeAll(int socketHandle, const std::string& data) {
    const SocketHandle socket = static_cast<SocketHandle>(socketHandle);
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int written =
            ::send(socket, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (written <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

void ArcadeServer::serviceStreams() {
    std::lock_guard<std::mutex> lock(m_streamMutex);
    if (m_streamSockets.empty()) {
        return;
    }
    std::string state;
    bool haveState = false;
    for (std::size_t i = 0; i < m_streamSockets.size();) {
        if (!haveState) {
            state = buildStateJson();
            haveState = true;
        }
        const std::string payload = "data: " + state + "\n\n";
        char frameHeader[32];
        std::snprintf(frameHeader, sizeof(frameHeader), "%zX\r\n", payload.size());
        const std::string chunk = std::string(frameHeader) + payload + "\r\n";
        if (!writeAll(m_streamSockets[i], chunk)) {
            closeSocket(static_cast<SocketHandle>(m_streamSockets[i]));
            m_streamSockets.erase(m_streamSockets.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

bool ArcadeServer::handleConnection(int socketHandle) {
    const SocketHandle client = static_cast<SocketHandle>(socketHandle);
    std::string request;
    request.reserve(2048);
    char buffer[4096];

    // Read headers (bounded), then the body according to Content-Length.
    std::size_t headerEnd = std::string::npos;
    std::size_t contentLength = 0;
    while (request.size() < kMaxRequestBytes) {
        const int received =
            ::recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (received <= 0) {
            break;
        }
        request.append(buffer, static_cast<std::size_t>(received));
        headerEnd = request.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            const std::string headers = request.substr(0, headerEnd);
            const std::size_t clPos = headers.find("Content-Length:");
            if (clPos != std::string::npos) {
                const std::size_t valueStart = clPos + std::strlen("Content-Length:");
                const std::size_t valueEnd = headers.find('\r', valueStart);
                contentLength = static_cast<std::size_t>(std::atoll(
                    headers.substr(valueStart, valueEnd - valueStart).c_str()));
            }
            const std::size_t bodyStart = headerEnd + 4;
            if (request.size() >= bodyStart + contentLength) {
                break;
            }
        }
    }

    HttpResponse response;
    if (headerEnd == std::string::npos) {
        response.status = 400;
        response.body = jsonError("malformed request");
    } else {
        const std::string requestLine = request.substr(0, request.find("\r\n"));
        std::istringstream lineStream(requestLine);
        std::string method;
        std::string path;
        std::string version;
        lineStream >> method >> path >> version;
        const std::size_t bodyStart = headerEnd + 4;
        std::string body = (request.size() > bodyStart) ? request.substr(bodyStart, contentLength)
                                                        : std::string();

        // Server-sent events: one long-lived connection streaming the frame at
        // the simulation rate. Falls back to /api/state polling on the client
        // when a proxy does not pass chunked responses through.
        const std::size_t queryPos = path.find('?');
        const std::string route = (queryPos == std::string::npos) ? path : path.substr(0, queryPos);
        if (route == "/api/stream" && (method == "GET" || method == "HEAD")) {
            const std::string header =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/event-stream; charset=utf-8\r\n"
                "Cache-Control: no-store\r\n"
                "X-Accel-Buffering: no\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Transfer-Encoding: chunked\r\n"
                "Connection: keep-alive\r\n\r\n";
            if (!writeAll(socketHandle, header)) {
                closeSocket(static_cast<SocketHandle>(socketHandle));
                return false;
            }
            return true;   // the accept loop now owns this socket
        }

        response = handleRequest(method, path, body);
    }

    std::string header = "HTTP/1.1 " + std::to_string(response.status) + " " +
                         (response.status == 200   ? "OK"
                          : response.status == 400 ? "Bad Request"
                          : response.status == 404 ? "Not Found"
                          : response.status == 405 ? "Method Not Allowed"
                                                   : "Error") +
                         "\r\nContent-Type: " + response.contentType +
                         "\r\nContent-Length: " + std::to_string(response.body.size()) +
                         "\r\nCache-Control: no-store"
                         "\r\nAccess-Control-Allow-Origin: *"
                         "\r\nAccess-Control-Allow-Headers: content-type"
                         "\r\nConnection: close\r\n" +
                         response.extraHeaders + "\r\n";
    std::string payload = header + response.body;

    (void)writeAll(socketHandle, payload);
    closeSocket(client);
    return false;
}

} // namespace ok::arcade
