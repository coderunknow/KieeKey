//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.1.3 - refactored and completed logic
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
// File: src/tsf/TsfComposer.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.3 — TsfComposer.cpp
// See TsfComposer.hpp. External TSF client (ITfThreadMgrEx + edit session).
//
// commit() flow (all inside the focused app's text store — no VK_BACK):
//   attach() → CoInitializeEx + ITfThreadMgrEx::ActivateEx(clientId)
//   onForegroundChanged() → ITfThreadMgr::GetFocus → ITfDocumentMgr::GetTop
//   commit(n, text) → ITfContext::RequestEditSession(TF_ES_READWRITE):
//       DoEditSession(ec):
//         GetSelection(ec, TF_DEFAULT_SELECTION, …)
//         Collapse(ec, TF_ANCHOR_START); ShiftStart(ec, -n)
//         ITfInsertAtSelection::InsertTextAtSelection(ec, TF_IAS_NOQUERY, text)
//         (fallback: ITfRange::SetText(ec, 0, text))
//----------------------------------------------------------------------------
// Provide the TSF GUID/CLSID definitions ourselves. mingw-w64's msctf.h
// declares every IID_*/CLSID_* as an extern symbol but ships no msctf import
// library to define them (MSVC links msctf.lib instead). Defining INITGUID
// before the first <msctf.h> include turns every DEFINE_GUID in the header
// into an actual COMDAT definition (DECLSPEC_SELECTANY → harmless across TUs).
// CLSID_TF_ThreadMgr has no DEFINE_GUID in the header, so we add it manually
// (value from the ReactOS msctf.idl mirror: {529A9E6B-6587-4F23-AB9E-9C7D683E3C50}).
#define INITGUID
#include "TsfComposer.hpp"

#ifdef __MINGW32__
DEFINE_GUID(CLSID_TF_ThreadMgr, 0x529a9e6b, 0x6587, 0x4f23,
            0xab, 0x9e, 0x9c, 0x7d, 0x68, 0x3e, 0x3c, 0x50);
// windows.h on MinGW does not pull in objbase.h, so IID_IUnknown stays
// undefined; it is the canonical all-zero IID.
DEFINE_GUID(IID_IUnknown, 0x00000000, 0x0000, 0x0000,
            0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
#endif

namespace ok::tsf {

namespace {

//---------------------------------------------------------------------------
// v1.1.0 — slow-commit watchdog support (see TsfComposer.hpp accessors).
// A synchronous TF_ES_SYNC session normally completes in single-digit µs;
// anything at/above 100 ms means the foreground app's STA is starving the
// consumer (the pre-hang symptom of the "KieeKey suddenly stopped" family).
// The consumer surfaces it via lastCommitSlow() and the app degrades that
// foreground to inline SendInput — which can never block.
//---------------------------------------------------------------------------
std::int64_t s_qpcFreq() noexcept {
    LARGE_INTEGER f{};
    ::QueryPerformanceFrequency(&f);
    return f.QuadPart;
}
inline constexpr std::int64_t kSlowCommitThresholdUs = 100'000;   // 100 ms

//---------------------------------------------------------------------------
// v3.4 (S3): the per-delta apply logic is shared by the batched and the
// single-delta session classes. The cached ITfInsertAtSelection comes from
// the TsfComposer (one QueryInterface per context, not per delta).
//---------------------------------------------------------------------------
HRESULT applyDelta(ITfContext* ctx, ITfInsertAtSelection* inserter,
                   TfEditCookie ec, const EditDelta& d) noexcept {
    if (d.backspace == 0) {
        // FAST PATH (the common case): pure insert — no selection query,
        // no collapse/shift. InsertTextAtSelection resolves the caret
        // itself, so this is a single COM call per delta.
        if (inserter != nullptr) {
            ITfRange* ignored = nullptr;
            const HRESULT hr = inserter->InsertTextAtSelection(
                ec, TF_IAS_NOQUERY, d.text.data(),
                static_cast<LONG>(d.text.size()), &ignored);
            if (ignored) { ignored->Release(); }
            return hr;
        }
        // No cached inserter (context rejected the QI) — QI once here so the
        // behavior stays identical to pre-v3.4 for such contexts.
        ITfInsertAtSelection* local = nullptr;
        HRESULT hr = ctx->QueryInterface(IID_ITfInsertAtSelection,
                                         reinterpret_cast<void**>(&local));
        if (SUCCEEDED(hr) && local) {
            ITfRange* ignored = nullptr;
            hr = local->InsertTextAtSelection(ec, TF_IAS_NOQUERY, d.text.data(),
                                              static_cast<LONG>(d.text.size()),
                                              &ignored);
            if (ignored) { ignored->Release(); }
            local->Release();
        } else {
            hr = E_FAIL;
        }
        return hr;
    }

    // Replace path: query the selection, collapse to its start, extend
    // back by N chars, then set the replacement text on that range.
    TF_SELECTION sel{};
    ULONG fetched = 0;
    HRESULT hr = ctx->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched);
    if (FAILED(hr) || fetched == 0 || sel.range == nullptr) {
        // v1.1.0-audit fix: a selection-less context is a FAILURE for this
        // delta. Returning S_OK (the old behavior when GetSelection succeeded
        // but fetched == 0) made commitOne/commitBatch report success while
        // nothing was applied — the engine advanced its buffer and the
        // keystroke silently vanished (no SendInput fallback either).
        return FAILED(hr) ? hr : E_FAIL;
    }

    hr = sel.range->Collapse(ec, TF_ANCHOR_START);
    if (SUCCEEDED(hr)) {
        LONG shifted = 0;
        hr = sel.range->ShiftStart(ec, -static_cast<LONG>(d.backspace),
                                   &shifted, nullptr);
    }
    if (SUCCEEDED(hr)) {
        // SetText on the (collapsed+shifted) range replaces the last
        // `backspace` chars with the replacement — a single COM call for
        // the whole delete+insert, and the caret lands after the text,
        // so the next delta in the batch is positioned correctly.
        hr = sel.range->SetText(ec, 0, d.text.data(),
                                static_cast<LONG>(d.text.size()));
        if (FAILED(hr) && inserter != nullptr) {
            // Some apps reject SetText on a collapsed range; fall back to
            // ITfInsertAtSelection (insert at the shifted selection).
            ITfRange* ignored = nullptr;
            hr = inserter->InsertTextAtSelection(ec, TF_IAS_NOQUERY, d.text.data(),
                                                 static_cast<LONG>(d.text.size()),
                                                 &ignored);
            if (ignored) { ignored->Release(); }
        }
    }
    sel.range->Release();
    return hr;
}

// The synchronous edit session that performs the replacement inside the
// focused context's text store. TSF owns its lifetime (releases it after
// DoEditSession completes), so it is created with refcount 1.
// v3.4: carries a VARIANT storage — either a batch (vector, copied as
// before) or a single delta by value (zero-alloc commitOne path).
class CommitEditSession final : public ITfEditSession {
public:
    CommitEditSession(ITfContext* ctx, ITfInsertAtSelection* inserter,
                      std::vector<EditDelta> deltas, HRESULT* resultOut) noexcept
        : ctx_(ctx), inserter_(inserter), deltas_(std::move(deltas)),
          resultOut_(resultOut) {}

    CommitEditSession(ITfContext* ctx, ITfInsertAtSelection* inserter,
                      EditDelta single, HRESULT* resultOut) noexcept
        : ctx_(ctx), inserter_(inserter), single_(std::move(single)),
          isSingle_(true), resultOut_(resultOut) {}

    // ---- IUnknown ----
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) noexcept override {
        if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
            *ppv = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() noexcept override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() noexcept override {
        const ULONG r = --refs_;
        if (r == 0) { delete this; }
        return r;
    }

    // ---- ITfEditSession ----
    STDMETHODIMP DoEditSession(TfEditCookie ec) noexcept override {
        HRESULT hr = S_OK;
        if (isSingle_) {
            hr = applyDelta(ctx_, inserter_, ec, single_);
            applied_ = SUCCEEDED(hr) ? 1u : 0u;
        } else {
            for (const EditDelta& d : deltas_) {
                hr = applyDelta(ctx_, inserter_, ec, d);
                if (FAILED(hr)) { break; }
                ++applied_;
            }
        }
        if (resultOut_) { *resultOut_ = hr; }
        return hr;
    }

    // v1.1.3 — how many deltas were COMMITTED before the session stopped
    // (or deltas_.size()/1 on success). TSF sessions are NOT transactional:
    // deltas applied before a failing one persist in the document, so the
    // app-side SendInput fallback must re-emit only the UNAPPLIED suffix —
    // re-emitting the whole batch duplicated word-initial pure-insert deltas
    // ("t" + "to" came out as "tto") whenever a mid-batch delta failed.
    [[nodiscard]] std::uint32_t appliedCount() const noexcept { return applied_; }

private:
    ITfContext* ctx_;
    ITfInsertAtSelection* inserter_;
    std::vector<EditDelta> deltas_;   // copied — the session may outlive the caller
    EditDelta single_{};
    bool isSingle_ = false;
    std::uint32_t applied_ = 0;
    HRESULT* resultOut_;
    ULONG refs_ = 1;
};

// Read-only edit session: captures the trailing run of ASCII letters before
// the caret (the word the user is inside). Used by textBeforeCaret().
// v1.1.0: the session OWNS its result string. The previous version pointed
// directly at the caller's stack std::wstring while requesting the session
// WITHOUT TF_ES_SYNC — a deferred DoEditSession would write into memory that
// belonged to a returned frame (stack-use-after-return) and, even when
// benign, delivered an empty read. The caller copies the result out after a
// synchronous request completes; an async fallback degrades gracefully.
class ReadBeforeCaretSession final : public ITfEditSession {
public:
    ReadBeforeCaretSession(ITfContext* ctx, HRESULT* resultOut) noexcept
        : ctx_(ctx), resultOut_(resultOut) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) noexcept override {
        if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
            *ppv = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() noexcept override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() noexcept override {
        const ULONG r = --refs_;
        if (r == 0) { delete this; }
        return r;
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec) noexcept override {
        HRESULT hr = E_FAIL;
        TF_SELECTION sel{};
        ULONG fetched = 0;
        hr = ctx_->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched);
        if (FAILED(hr) || fetched == 0 || sel.range == nullptr) {
            if (resultOut_) { *resultOut_ = hr; }
            return hr;
        }

        // Caret -> extend the start backward up to kMaxRead chars.
        hr = sel.range->Collapse(ec, TF_ANCHOR_START);
        if (SUCCEEDED(hr)) {
            LONG shifted = 0;
            hr = sel.range->ShiftStart(ec, -kMaxRead, &shifted, nullptr);
        }
        if (SUCCEEDED(hr)) {
            // MinGW's ITfRange::GetText fills a caller buffer (no BSTR form).
            // `got` — the outer `fetched` above is still in scope here, so
            // reusing that name shadows it (C4457 under MSVC /W4 /WX).
            wchar_t buf[128] = {};
            ULONG got = 0;
            hr = sel.range->GetText(ec, TF_TF_MOVESTART, buf,
                                    static_cast<ULONG>(std::size(buf)), &got);
            if (SUCCEEDED(hr)) {
                std::size_t i = got;
                while (i > 0) {
                    const wchar_t c = buf[i - 1];
                    if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')) { --i; }
                    else { break; }
                }
                // v1.1.3 — TRUNCATED-READBACK GUARD (output-corruption fix).
                // The raw replay can only mirror ASCII Telex/VNI keystrokes.
                // When a letter that is NOT plain ASCII (composed Vietnamese
                // such as the à of "chào", or any digit/punctuation word
                // character) sits directly before the ASCII run, the visible
                // word continues beyond what this read can represent:
                // replaying just the ASCII tail ("o" for "chào|") seeded the
                // engine with a TRUNCATED word, and the next tone key then
                // computed its backspace count against the wrong length —
                // deleting the wrong characters at the caret. Strictly worse
                // than not resyncing (an empty buffer degrades safely).
                // Fail the session: the caller keeps the empty-buffer model
                // (startNewSession — the safe degradation).
                if (i > 0) {
                    const wchar_t prev = buf[i - 1];
                    if (prev > 0x7F || (prev >= L'0' && prev <= L'9')) {
                        if (resultOut_) { *resultOut_ = S_FALSE; }
                        return S_FALSE;
                    }
                }
                own_.assign(buf + i, buf + got);
                hr = S_OK;
            }
        }
        sel.range->Release();
        if (resultOut_) { *resultOut_ = hr; }
        return hr;
    }

    // The captured word (session-owned storage — safe to copy out after a
    // synchronous RequestEditSession returns).
    [[nodiscard]] const std::wstring& result() const noexcept { return own_; }

private:
    static constexpr LONG kMaxRead = 64;   // enough for the longest word
    ITfContext* ctx_;
    std::wstring own_;
    HRESULT* resultOut_;
    ULONG refs_ = 1;
};
} // namespace

//===========================================================================
// Lifecycle
//===========================================================================
bool TsfComposer::attach() noexcept {
    if (attached_) { return true; }

    const HRESULT coHr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // v1.1.3: RPC_E_CHANGED_MODE (thread already MTA) previously CONTINUED
    // attaching. TSF's focus/edit machinery is apartment-threaded and
    // message-window based; created from an MTA thread it proxies into a
    // host STA nobody pumps and edit sessions degrade unpredictably. Attach
    // fails cleanly instead — the caller falls back to inline SendInput.
    if (coHr == RPC_E_CHANGED_MODE) { return false; }
    if (FAILED(coHr) && coHr != S_FALSE) {
        return false;
    }
    // v1.1.0-audit fix: S_OK and S_FALSE both take a COM init reference we
    // must balance later; RPC_E_CHANGED_MODE failed and took NOTHING (the
    // thread is already MTA — unbalance would tear down the real owner).
    ownsComInit_ = SUCCEEDED(coHr);

    HRESULT hr = ::CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_ITfThreadMgrEx,
                                    reinterpret_cast<void**>(&threadMgr_));
    if (FAILED(hr) || threadMgr_ == nullptr) {
        if (ownsComInit_) { ::CoUninitialize(); }
        ownsComInit_ = false;
        threadMgr_ = nullptr;
        return false;
    }

    // Activate this tool on the foreground thread's TSF input stack and get
    // our TfClientId for edit-session requests.
    if (FAILED(threadMgr_->ActivateEx(&clientId_, TF_TMAE_UIELEMENTENABLEDONLY))) {
        threadMgr_->Release();
        threadMgr_ = nullptr;
        if (ownsComInit_) { ::CoUninitialize(); }
        ownsComInit_ = false;
        return false;
    }

    attached_ = true;
    onForegroundChanged();
    return true;
}

void TsfComposer::detach() noexcept {
    if (inserter_) { inserter_->Release(); inserter_ = nullptr; }
    if (context_)  { context_->Release();  context_  = nullptr; }
    if (docMgr_)   { docMgr_->Release();   docMgr_   = nullptr; }
    if (threadMgr_) {
        threadMgr_->Deactivate();
        threadMgr_->Release();
        threadMgr_ = nullptr;
    }
    // v1.1.0-audit fix: balance the COM init reference this instance took in
    // attach() (S_OK/S_FALSE only — see ownsComInit_). The previous detach()
    // never called CoUninitialize despite the comment acknowledging the
    // requirement, leaking one reference per attach/detach cycle. Thread-
    // affine by design: detach() runs on the consumer thread that attached.
    if (ownsComInit_) {
        ::CoUninitialize();
        ownsComInit_ = false;
    }
    attached_ = false;
}

void TsfComposer::onForegroundChanged() noexcept {
    if (!attached_ || threadMgr_ == nullptr) { return; }

    // Release previous references before re-resolving the focused document.
    // v3.4 (S3): the cached ITfInsertAtSelection is bound to the context —
    // drop it with the context so a stale interface is never reused.
    if (inserter_) { inserter_->Release(); inserter_ = nullptr; }
    if (context_) { context_->Release(); context_ = nullptr; }
    if (docMgr_)  { docMgr_->Release();  docMgr_  = nullptr; }

    ITfThreadMgr* baseMgr = nullptr;
    if (FAILED(threadMgr_->QueryInterface(IID_ITfThreadMgr,
                                          reinterpret_cast<void**>(&baseMgr)))) {
        return;
    }
    // GetFocus returns the document manager with keyboard focus on this
    // thread; may be nullptr when the foreground window is not TSF-aware.
    baseMgr->GetFocus(&docMgr_);
    baseMgr->Release();

    if (docMgr_) {
        docMgr_->GetTop(&context_);   // add-ref'd; used by commit()
        // Cache the insert-at-selection interface for this context (v3.4):
        // one QueryInterface per focus change instead of per edit delta.
        if (context_ != nullptr) {
            static_cast<void>(context_->QueryInterface(
                IID_ITfInsertAtSelection, reinterpret_cast<void**>(&inserter_)));
        }
    }
}

//===========================================================================
// commit()
//===========================================================================
bool TsfComposer::ensureContext() noexcept {
    // v1.1.3 — stale-focus defense (cross-app text-injection fix). The
    // cached context is now verified against the CURRENT thread focus on
    // every commit. GetFocus on OUR OWN thread manager is an in-process
    // read (msctf-local state, no marshaling into the target app), so the
    // check is a few nanoseconds per batch. This closes three known holes
    // at once:
    //   1. The v3.5 FGPROBE gate: while a foreground switch is gated, the
    //      consumer skips onForegroundChanged(); a non-null context from
    //      the PREVIOUS app survived and the first commit after the probe
    //      re-armed TSF landed in the OLD app's document.
    //   2. Same-window focus changes (browser tab switch, page <-> omnibox,
    //      document <-> task pane) — never observable via foreground events.
    //   3. A popped document (tab closed) — previously detected only by the
    //      session failing later.
    // ITfThreadMgrEx derives from ITfThreadMgr, so GetFocus is callable
    // directly — no per-commit QueryInterface churn.
    if (context_ != nullptr && threadMgr_ != nullptr) {
        ITfDocumentMgr* focus = nullptr;
        if (SUCCEEDED(threadMgr_->GetFocus(&focus))) {
            if (focus != docMgr_) {
                if (focus) { focus->Release(); }
                onForegroundChanged();   // stale — re-resolve from real focus
            } else if (focus) {
                focus->Release();
            }
        }
    }
    if (context_ != nullptr) { return true; }
    onForegroundChanged();   // focus may have moved since the last event
    return context_ != nullptr;
}

bool TsfComposer::commit(std::size_t backspaceCount,
                         const std::wstring& replacement) noexcept {
    if (backspaceCount == 0 && replacement.empty()) { return true; }
    return commitOne(backspaceCount, replacement);
}

//---------------------------------------------------------------------------
// v3.4 (S3) — zero-allocation single-delta session. The isolated tone-key
// edit (1 tone key = 1 edit) is the dominant TSF population; this path
// skips the std::vector<EditDelta> copy commitBatch makes per commit.
// Replacement strings are ≤ 65 UTF-16 units (kRingTextCap); most are 1–3
// chars — inside every implementation's SSO, so the whole commit is
// heap-free. Same ONE synchronous READWRITE edit session as commitBatch.
//---------------------------------------------------------------------------
bool TsfComposer::commitOne(std::size_t backspaceCount,
                            const std::wstring& replacement) noexcept {
    if (!attached_ || threadMgr_ == nullptr || !ensureContext()) { return false; }

    // v1.1.0 — slow-commit watchdog: measure the synchronous session. The
    // normal commit is a few µs; a multi-ms (worst case: unbounded) session
    // means the foreground app's STA is starving. The consumer flags it and
    // the app downgrades this foreground to inline SendInput.
    LARGE_INTEGER t0{};
    ::QueryPerformanceCounter(&t0);
    lastCommitSlow_.store(false, std::memory_order_relaxed);

    HRESULT sessionHr = E_FAIL;
    auto* session = new CommitEditSession(context_, inserter_,
                                          EditDelta{backspaceCount, replacement},
                                          &sessionHr);
    // Same session request as commitBatch — TF_ES_READWRITE | TF_ES_SYNC.
    // (TF_ES_ASYNCDONTCARE was evaluated for S3: it returns BEFORE the edit
    // lands, which breaks the pendingEdits ordering barrier and lets a
    // pass-through key overtake the edit (ghosting). Re-evaluate only with
    // real-app A/B data from VERIFICATION_PROTOCOL.md; sync stays the
    // correctness anchor.)
    HRESULT hr = context_->RequestEditSession(clientId_, session,
                                              TF_ES_READWRITE | TF_ES_SYNC,
                                              &sessionHr);
    // v1.1.0 — COM reference-balance fix: the `new` above is the caller's
    // initial reference. TSF held an internal ref only FOR the duration of
    // the synchronous request, so without this Release one session object
    // leaked PER COMMITTED EDIT (measurable heap growth in browsers/Office,
    // the Auto-TSF set). Mirrors the Microsoft edit-session sample.
    session->Release();

    LARGE_INTEGER t1{};
    ::QueryPerformanceCounter(&t1);
    const std::int64_t us = (t1.QuadPart - t0.QuadPart) * 1000000LL
                            / s_qpcFreq();
    if (us > kSlowCommitThresholdUs) {
        slowCommits_.fetch_add(1, std::memory_order_relaxed);
        lastCommitSlow_.store(true, std::memory_order_relaxed);
    }

    if (FAILED(hr) || FAILED(sessionHr)) {
        return false;
    }
    return true;
}

bool TsfComposer::commitBatch(const std::vector<EditDelta>& deltas,
                              std::size_t* appliedOut) noexcept {
    if (appliedOut) { *appliedOut = 0; }
    if (!attached_ || threadMgr_ == nullptr || !ensureContext()) { return false; }
    if (deltas.empty()) { return true; }
    if (deltas.size() == 1) {
        const bool okR = commitOne(deltas[0].backspace, deltas[0].text);
        if (appliedOut) { *appliedOut = okR ? 1u : 0u; }
        return okR;
    }

    // v1.1.0 — slow-commit watchdog (same instrumentation as commitOne).
    LARGE_INTEGER t0{};
    ::QueryPerformanceCounter(&t0);
    lastCommitSlow_.store(false, std::memory_order_relaxed);

    HRESULT sessionHr = E_FAIL;
    auto* session = new CommitEditSession(context_, inserter_, deltas, &sessionHr);

    // Request a SYNCHRONOUS READWRITE edit session. TF_ES_SYNC guarantees
    // DoEditSession has fully run when RequestEditSession returns — required
    // for the producer's ordering barrier (pendingEdits must reflect reality)
    // and to keep the document in lockstep with the engine buffer. Without
    // it, a session could be queued asynchronously and applied after the
    // caller already resumed. TSF releases `session`.
    HRESULT hr = context_->RequestEditSession(clientId_, session,
                                              TF_ES_READWRITE | TF_ES_SYNC,
                                              &sessionHr);
    // v1.1.0 — release the caller's initial reference (see commitOne:
    // without this, one session object leaked per batched commit).
    session->Release();

    LARGE_INTEGER t1{};
    ::QueryPerformanceCounter(&t1);
    const std::int64_t us = (t1.QuadPart - t0.QuadPart) * 1000000LL
                            / s_qpcFreq();
    if (us > kSlowCommitThresholdUs) {
        slowCommits_.fetch_add(1, std::memory_order_relaxed);
        lastCommitSlow_.store(true, std::memory_order_relaxed);
    }

    if (appliedOut) { *appliedOut = session->appliedCount(); }
    if (FAILED(hr) || FAILED(sessionHr)) {
        return false;
    }
    return true;
}

bool TsfComposer::textBeforeCaret(std::wstring& out) noexcept {
    out.clear();
    if (!attached_ || !ensureContext()) { return false; }

    // v1.1.0 — the session OWNS the read buffer (see ReadBeforeCaretSession).
    // A synchronous READ-ONLY session runs inside RequestEditSession, so the
    // copy-out below is safe.
    // v1.1.0-audit fix: the ASYNCDONTCARE retry that used to follow a sync
    // refusal was REMOVED. It could never produce a usable result (this
    // function returned false unconditionally after it), while TSF was free
    // to queue the session and run DoEditSession AFTER the frame returned —
    // writing *resultOut_ into dead stack memory (stack-use-after-return)
    // and dereferencing ctx_ after detach()/onForegroundChanged() may have
    // released the context (use-after-free). "Unavailable this round" is the
    // same graceful degradation without the corruption.
    HRESULT sessionHr = E_FAIL;
    auto* session = new ReadBeforeCaretSession(context_, &sessionHr);
    // READ (not READWRITE) access: the session only reads the selection.
    // TF_ES_SYNC makes it run inside RequestEditSession, so the copy-out
    // below is safe.
    HRESULT hr = context_->RequestEditSession(clientId_, session,
                                              TF_ES_READ | TF_ES_SYNC,
                                              &sessionHr);
    if (FAILED(hr)) {
        session->Release();   // sync request refused — no external pointers remain
        return false;
    }
    const bool ok = SUCCEEDED(sessionHr) && !session->result().empty();
    if (ok) { out = session->result(); }
    session->Release();
    return ok;
}

} // namespace ok::tsf
