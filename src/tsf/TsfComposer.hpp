//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.1.0 - refactored and completed logic
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
// File: src/tsf/TsfComposer.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.0 — TsfComposer.hpp
// Text Services Framework (TSF) composer — the "buffer-state output" backend
// that eliminates synthetic backspace injections and the clipboard+Shift+Insert
// round-trip (the #1 cause of ghost/duplicate letters and cursor flicker).
//
// What it does:
//   * Attaches to the active thread's TSF manager (ITfThreadMgrEx) and tracks
//     the focused ITfDocumentMgr / ITfContext.
//   * commit(backspaceCount, replacementUtf16) requests a READWRITE edit
//     session (ITfEditSession) on the focused context: inside the session it
//     deletes `backspaceCount` characters before the cursor and inserts the
//     replacement — all INSIDE the app's text store. Chrome, Edge, MS Office
//     and Excel never see VK_BACK or clipboard traffic.
//
// This is the "external TSF client" pattern (works for per-user input tools).
// A full in-proc TSF text service is the production-grade alternative.
//
// Thread affinity: all TSF objects are COM apartment-bound — attach() and
// commit() must be called from the SAME (consumer) thread.
// Requires: Windows SDK / mingw-w64 with msctf.h; link msctf (msctf.lib).
//----------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include <msctf.h>

namespace ok::tsf {

// One engine edit: delete `backspace` UTF-16 units before the caret, then
// insert `text`. A keystroke burst (e.g. "tồi" = tooi+f) produces a small
// run of these; committing them in ONE edit session removes the per-edit
// RequestEditSession round-trip that made accent typing feel delayed.
struct EditDelta {
    std::size_t backspace = 0;
    std::wstring text;
};

//---------------------------------------------------------------------------
// TsfComposer — attach once per session, commit per keystroke batch.
//---------------------------------------------------------------------------
class TsfComposer final {
public:
    TsfComposer() noexcept = default;
    ~TsfComposer() { detach(); }

    TsfComposer(const TsfComposer&)            = delete;
    TsfComposer& operator=(const TsfComposer&) = delete;

    // Initializes COM (CoInitializeEx) + TSF thread manager on this thread.
    // Call from the consumer thread. Returns false if TSF is unavailable.
    [[nodiscard]] bool attach() noexcept;

    // Releases TSF references. Idempotent. Must be called on the attaching
    // thread (COM apartment affinity).
    void detach() noexcept;

    // Track the foreground thread's focus (call on foreground-changed events).
    void onForegroundChanged() noexcept;

    // Replace the last `backspaceCount` characters before the cursor with
    // `replacement` (UTF-16). Returns true when committed via TSF.
    // When false, callers fall back to SendInput(KEYEVENTF_UNICODE).
    [[nodiscard]] bool commit(std::size_t backspaceCount,
                              const std::wstring& replacement) noexcept;

    // v3.4 (S3): zero-allocation single-delta fast path. The isolated tone
    // key (the dominant edit population: 1 tone key = 1 edit) bypasses the
    // std::vector<EditDelta> copy that commitBatch makes per commit — the
    // session object carries the single delta by value (short strings stay
    // in SSO, so the whole path is heap-free). Same TSF semantics as
    // commitBatch with one element: ONE synchronous READWRITE edit session.
    [[nodiscard]] bool commitOne(std::size_t backspaceCount,
                                 const std::wstring& replacement) noexcept;

    // Apply a batch of edits in ONE synchronous TSF edit session, in order.
    // Each delta is relative to the document state left by the previous one
    // (the engine produces sequential deltas on its own buffer). Batching
    // removes the per-keystroke RequestEditSession round-trip — the dominant
    // term in accent-typing latency. Returns true when the whole batch was
    // committed via TSF; on failure callers fall back per delta.
    [[nodiscard]] bool commitBatch(const std::vector<EditDelta>& deltas) noexcept;

    // Read the trailing run of ASCII letters immediately before the caret
    // (the word the user is currently inside). Used to re-sync the engine to
    // the visible text after the caret moved / text was edited outside the
    // keystroke stream. Returns false when there is nothing to read.
    [[nodiscard]] bool textBeforeCaret(std::wstring& out) noexcept;

    [[nodiscard]] bool attached() const noexcept { return attached_; }

    // v1.1.0 — slow-commit telemetry (consumer-side TSF hang watchdog).
    // A synchronous TF_ES_SYNC session that took far longer than the normal
    // few-µs commit means the foreground app's STA is starving; left alone
    // it wedges the consumer ("KieeKey suddenly stops"). The app checks
    // lastCommitSlow() after each consumer event and downgrades the current
    // foreground to inline SendInput (which can never block).
    [[nodiscard]] std::uint64_t slowCommitCount() const noexcept { return slowCommits_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool lastCommitSlow() const noexcept { return lastCommitSlow_.load(std::memory_order_relaxed); }

private:
    bool ensureContext() noexcept;

    bool attached_ = false;
    // v1.1.0-audit fix: whether THIS composer's CoInitializeEx call actually
    // took a COM init reference on the consumer thread (S_OK and S_FALSE
    // both do; RPC_E_CHANGED_MODE does NOT). Only an owned reference may be
    // balanced with CoUninitialize — the previous code called it on failure
    // paths even after RPC_E_CHANGED_MODE (unbalancing the real owner's
    // reference) and never in detach() (leaking one reference per
    // attach/detach cycle on a live thread).
    bool ownsComInit_ = false;
    ITfThreadMgrEx* threadMgr_ = nullptr;   // raw COM pointers — TSF-owned
    ITfDocumentMgr* docMgr_    = nullptr;
    ITfContext*     context_   = nullptr;
    // v3.4 (S3): ITfInsertAtSelection cached PER CONTEXT (invalidated on
    // foreground change). Removes a QueryInterface + Release pair from every
    // delta (the insert path runs for every pure insert AND as the SetText
    // fallback). ~100–300 ns saved per edit; interface lifetime is bound to
    // context_, which we re-resolve on focus change anyway.
    ITfInsertAtSelection* inserter_ = nullptr;
    DWORD clientId_            = 0;        // TfClientId (0 = TF_CLIENTID_NULL)

    // v1.1.0 — slow-commit watchdog state (see the public accessors above).
    std::atomic<std::uint64_t> slowCommits_{0};
    std::atomic<bool>          lastCommitSlow_{false};
};

} // namespace ok::tsf
