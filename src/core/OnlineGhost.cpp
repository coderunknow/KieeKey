//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// File: src/core/OnlineGhost.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "OnlineGhost.hpp"

#include <algorithm>

namespace ok::online {

bool LocalGhostProvider::saveReplay(const ReplaySession& session) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Remove if already present with same id
    m_replays.erase(
        std::remove_if(m_replays.begin(), m_replays.end(),
                       [&](const ReplaySession& r) { return r.sessionId == session.sessionId; }),
        m_replays.end());
    m_replays.push_back(session);
    return true;
}

std::optional<ReplaySession> LocalGhostProvider::loadReplay(std::string_view sessionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& r : m_replays) {
        if (r.sessionId == sessionId) {
            return r;
        }
    }
    return std::nullopt;
}

std::optional<ReplaySession> LocalGhostProvider::getBestReplay(std::string_view modeName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::optional<ReplaySession> best;
    for (const auto& r : m_replays) {
        if (r.modeName == modeName) {
            if (!best || r.finalScore > best->finalScore) {
                best = r;
            }
        }
    }
    return best;
}

std::vector<ReplaySession> LocalGhostProvider::listReplays(std::string_view modeName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ReplaySession> out;
    for (const auto& r : m_replays) {
        if (modeName.empty() || r.modeName == modeName) {
            out.push_back(r);
        }
    }
    return out;
}

void LocalGhostProvider::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_replays.clear();
}

bool OfflineLeaderboardProvider::submitScore(const ScorePayload& payload) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_scores.push_back(payload);
    return true;
}

std::vector<ScorePayload> OfflineLeaderboardProvider::getLeaderboard(std::string_view modeName, size_t topN) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ScorePayload> matched;
    for (const auto& s : m_scores) {
        if (modeName.empty() || s.gameMode == modeName) {
            matched.push_back(s);
        }
    }

    std::sort(matched.begin(), matched.end(), [](const ScorePayload& a, const ScorePayload& b) {
        return a.score > b.score;
    });

    if (matched.size() > topN) {
        matched.resize(topN);
    }
    return matched;
}

void OfflineLeaderboardProvider::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_scores.clear();
}

} // namespace ok::online
