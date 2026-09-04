//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.0 - refactored and completed logic
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
// File: src/core/kieekey_core.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v3.3.1 — kieekey_core.hpp
// Unified public facade of the KieeKey typing core ("ok::text").
//
// This header is the SINGLE entry point for host layers (Win32 wrapper,
// TSF composer, tests, benchmarks) that embed the engine. It exists so the
// host never includes engine internals directly, which keeps the task
// boundary enforced by v3.3.1 architecture review:
//
//   * ALL typing logic — composition, spelling, restoration, lexicon
//     arbitration, tone-style conversion — lives in TextEngine (core).
//   * The Win32 wrapper layer (win32_wrapper.hpp) must stay a pure
//     transport: queueing, batching, priorities, hook lifecycle. No
//     auto-correction heuristics there, ever.
//
// Everything here is header-only glue; the engine implementation remains
// in TextEngine.cpp (one translation unit — fast to build, easy to audit).
//
// v3.3.1 core changes vs v3.1 (see DIVERGENCES.md §6 residuals):
//   1. Lexicon-gated restoration now also resolves the two remaining
//      UniKey-win families:
//        - "huơ" vs "hươ": when the composed form is NOT a lexicon word,
//          the partial-composition candidate (u stays u, only the o carries
//          the W-hook) is offered — emitted when the lexicon approves it.
//        - "mono" vs "môn": a mid-word vowel toggle (insertAOE crossing a
//          final consonant) is flagged; at the word break, when BOTH the
//          composed form and the raw keystrokes are lexicon words, the raw
//          keystrokes win (lexicon-gated mid-word strictness).
//      With useDictionaryRestore OFF the engine stays byte-identical to
//      v3.0/2.0.5 (matched-config parity, G5) — empirically re-verified on
//      the full 66,552-word corpus (0 diffs vs the frozen v3.1 matched run).
//   2. TextEngine::switchToneStyle() — seamless "hoá" <-> "hóa" conversion
//      of the PENDING word directly inside the state buffer (mask move +
//      normal WillProcess re-emission; net-zero length change, D2-safe).
//
// Version history: 3.0 (rewrite) · 3.1 (P0–P4 hardening + lexicon gate) ·
//                  3.3 (SPSC pipeline + batched SendInput) ·
//                  3.3.1 (lexicon-gated loanword resolution + tone-style
//                         switching + hook self-healing + static CRT builds)
//----------------------------------------------------------------------------
#pragma once

// v1.1.0: the KieeKey release version (previously the pre-release lineage
// numbers 3.x leaked into this public constant, contradicting the 1.0.x
// release version used everywhere else).
#define OPENKEY_KIEEKEY_VERSION_MAJOR 1
#define OPENKEY_KIEEKEY_VERSION_MINOR 2
#define OPENKEY_KIEEKEY_VERSION_PATCH 0
#define OPENKEY_KIEEKEY_VERSION_STRING "1.2.0"

#include "TextEngine.hpp"       // engine + options + result contract
#include "VietnameseTables.hpp" // encoding masks (public contract of results)

namespace ok::kieekey {

// The embedding contract in one type alias: host layers hold a TextEngine
// and speak exclusively in TextInput / EngineResult (see TextEngine.hpp).
using Engine = ok::text::TextEngine;
using EngineInput = ok::text::TextInput;
using EngineResult = ok::text::EngineResult;
using EngineOptions = ok::text::EngineOptions;

// Consumer contract constants (v3.1 D-contracts, carried into v3.3.1):
//   * Apply EngineResult as: delete backspaceCount chars at the caret,
//     then insert replacementUtf16(result). backspaceCount is engine-clamped
//     (D2) — a consumer may clamp again defensively but must never expand.
//   * Restore on a Char re-issues the typed char; Restore on a Space
//     re-issues the space (D4). The engine accounting already includes both.
//   * ReplaceMacro: apply delete + macroExpansionUtf16, consume the break
//     key (D3). Oversized expansions ride the inline chunked emitter.
//   * The engine's own backspace policy is authoritative; do not translate
//     engine decisions into wrapper heuristics (keep the layers separated).

} // namespace ok::kieekey
