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
// File: src/tsf/TsfComposer.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.0 — TsfComposer.cpp
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
        return hr;
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
        } else {
            for (const EditDelta& d : deltas_) {
                hr = applyDelta(ctx_, inserter_, ec, d);
                if (FAILED(hr)) { break; }
            }
        }
        if (resultOut_) { *resultOut_ = hr; }
        return hr;
    }

private:
    ITfContext* ctx_;
    ITfInsertAtSelection* inserter_;
    std::vector<EditDelta> deltas_;   // copied — the session may outlive the caller
    EditDelta single_{};
    bool isSingle_ = false;
    HRESULT* resultOut_;
    ULONG refs_ = 1;
};

// Read-only edit session: captures the trailing run of ASCII letters before
// the caret (the word the user is inside). Used by textBeforeCaret().
class ReadBeforeCaretSession final : public ITfEditSession {
public:
    ReadBeforeCaretSession(ITfContext* ctx, std::wstring* out,
                           HRESULT* resultOut) noexcept
        : ctx_(ctx), out_(out), resultOut_(resultOut) {}

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
                out_->assign(buf + i, buf + got);
                hr = S_OK;
            }
        }
        sel.range->Release();
        if (resultOut_) { *resultOut_ = hr; }
        return hr;
    }

private:
    static constexpr LONG kMaxRead = 64;   // enough for the longest word
    ITfContext* ctx_;
    std::wstring* out_;
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
    if (FAILED(coHr) && coHr != RPC_E_CHANGED_MODE && coHr != S_FALSE) {
        return false;
    }

    HRESULT hr = ::CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_ITfThreadMgrEx,
                                    reinterpret_cast<void**>(&threadMgr_));
    if (FAILED(hr) || threadMgr_ == nullptr) {
        ::CoUninitialize();
        threadMgr_ = nullptr;
        return false;
    }

    // Activate this tool on the foreground thread's TSF input stack and get
    // our TfClientId for edit-session requests.
    if (FAILED(threadMgr_->ActivateEx(&clientId_, TF_TMAE_UIELEMENTENABLEDONLY))) {
        threadMgr_->Release();
        threadMgr_ = nullptr;
        ::CoUninitialize();
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
    // NOTE: CoUninitialize must run on the thread that called CoInitializeEx
    // (the consumer thread); detach() is thread-affine by design.
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
    if (FAILED(hr) || FAILED(sessionHr)) {
        return false;
    }
    return true;
}

bool TsfComposer::commitBatch(const std::vector<EditDelta>& deltas) noexcept {
    if (!attached_ || threadMgr_ == nullptr || !ensureContext()) { return false; }
    if (deltas.empty()) { return true; }
    if (deltas.size() == 1) { return commitOne(deltas[0].backspace, deltas[0].text); }

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
    if (FAILED(hr) || FAILED(sessionHr)) {
        return false;
    }
    return true;
}

bool TsfComposer::textBeforeCaret(std::wstring& out) noexcept {
    out.clear();
    if (!attached_ || !ensureContext()) { return false; }

    HRESULT sessionHr = E_FAIL;
    auto* session = new ReadBeforeCaretSession(context_, &out, &sessionHr);
    // No TF_ES_READWRITE flag -> the edit session has read-only access.
    HRESULT hr = context_->RequestEditSession(clientId_, session,
                                              TF_ES_ASYNCDONTCARE, &sessionHr);
    if (FAILED(hr) || FAILED(sessionHr)) { return false; }
    return !out.empty();
}

} // namespace ok::tsf
