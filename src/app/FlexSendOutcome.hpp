//============================================================================
// KieeKey - A modified version based on OpenKey
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
// File: src/app/FlexSendOutcome.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//---------------------------------------------------------------------------
// v1.3.0-beta8 (bug FT-02) — why "Gõ chữ Flexing ra app" did nothing.
//
// beta7's typeIntoFocusApp() returned a bare `std::size_t` and every failure
// path `return 0;`'d — identically to "queued, nothing emitted yet". The user
// pressed the button, nothing happened, and the Lab said nothing at all: the
// three real causes (no external target remembered / Windows refused the
// activation / the emitter rejected a chunk) were indistinguishable from
// success-with-nothing-produced.
//
// The outcome is now an enum with a Vietnamese reason per case. The reasons
// live HERE (portable, no windows.h) so tests/test_flex_send_outcome.cpp can
// pin the contract on Linux: every outcome has a non-empty, distinct reason,
// and only the failure outcomes make the Lab shout.
//---------------------------------------------------------------------------
#ifndef KIEEKEY_APP_FLEX_SEND_OUTCOME_HPP
#define KIEEKEY_APP_FLEX_SEND_OUTCOME_HPP

#include <cstddef>
#include <string>

namespace ok::flexsend {

enum class Outcome {
    Empty,             //  0 characters produced yet — nothing to type
    NoTarget,          //  1 no external app was remembered when the Lab opened
    NoEmitter,         //  2 the host emit callback is not installed
    ActivationDenied,  //  3 SetForegroundWindow refused (Windows focus rules)
    EmitFailed,        //  4 the emitter rejected the text / a chunk
    TargetLost,        //  5 the target lost the foreground mid-injection → stopped
    Queued,            //  6 per-chunk injection queued (nothing emitted yet)
    Sent,              //  7 emitted synchronously
};

// The four cases in which the user must be told something went wrong. Queued
// and Sent are not failures; Empty is user-action guidance, not a failure, and
// is reported with a "go type something first" hint rather than a warning.
[[nodiscard]] constexpr bool isFailure(Outcome o) noexcept {
    switch (o) {
        case Outcome::NoTarget:
        case Outcome::NoEmitter:
        case Outcome::ActivationDenied:
        case Outcome::EmitFailed:
        case Outcome::TargetLost:
            return true;
        case Outcome::Empty:
        case Outcome::Queued:
        case Outcome::Sent:
            return false;
    }
    return false;
}

// Only the focus refusal deserves an interrupting dialog: it is the case the
// user cannot diagnose from the Lab alone (Windows denies activation whenever
// the requesting process is not already in the foreground).
[[nodiscard]] constexpr bool needsAlert(Outcome o) noexcept {
    return o == Outcome::ActivationDenied;
}

// The one-line Vietnamese reason shown in the Lab status row. The row is
// authored 720 design units wide at 96 DPI (≈120 characters of Segoe UI 13),
// and it scales WITH the font, so the limit is the same at every DPI — a longer
// string would clip in exactly the way beta7's layout bugs did.
[[nodiscard]] constexpr const wchar_t* reasonVi(Outcome o) noexcept {
    switch (o) {
        case Outcome::Empty:
            return L"Chưa có chữ nào — gõ vài phím vào ô dưới cùng trước đã.";
        case Outcome::NoTarget:
            return L"Không thấy app đích — hãy mở app đó, bấm vào nó rồi quay lại Lab.";
        case Outcome::NoEmitter:
            return L"Chưa nối được bộ phát phím — hãy mở Lab từ KieeKey đang chạy.";
        case Outcome::ActivationDenied:
            return L"Windows từ chối đưa app đích lên trước — hãy bấm vào app đó rồi thử lại.";
        case Outcome::EmitFailed:
            return L"Bộ phát phím báo lỗi giữa đường — một phần chữ có thể chưa ra tới app.";
        case Outcome::TargetLost:
            return L"App đích mất focus giữa lúc gõ — đã DỪNG để không gõ lẫn cửa sổ khác.";
        case Outcome::Queued:
            return L"Đang gõ từng nhịp ra app đích…";
        case Outcome::Sent:
            return L"Đã gõ xong ra app đích.";
    }
    return L"";
}

// The longer explanation used by the one-time dialog (a MessageBox has room for
// the actual advice the status row cannot carry).
[[nodiscard]] constexpr const wchar_t* detailVi(Outcome o) noexcept {
    switch (o) {
        case Outcome::ActivationDenied:
            return L"Windows chỉ cho phép cửa sổ đang ở trước được đưa app khác lên trước.\n\n"
                   L"Cách sửa: bấm trực tiếp vào app đích (Word, Notepad…) để nó đang ở trước, "
                   L"rồi quay lại Lab và bấm lại \"Gõ chữ Flexing ra app\". Nếu app đích chạy "
                   L"với quyền quản trị, hãy chạy KieeKey ở cùng mức quyền đó.";
        case Outcome::NoTarget:
            return L"Lab chỉ nhớ app đang ở trước tại thời điểm Lab được MỞ. Bấm vào app bạn "
                   L"muốn gõ vào, rồi mở lại Lab (nút \"Phòng Chaos\" ở tab Arcade).";
        case Outcome::NoEmitter:
            return L"Bộ phát phím do KieeKey truyền vào Lab chưa được cài đặt. Hãy mở Lab từ "
                   L"biểu tượng KieeKey ở khay hệ thống, không chạy file thử nghiệm riêng.";
        case Outcome::EmitFailed:
            return L"SendInput đã bị hệ thống từ chối giữa đường — thường do app đích chạy ở "
                   L"mức quyền cao hơn, hoặc một ứng dụng khác đang giữ bàn phím.";
        case Outcome::TargetLost:
            return L"Bạn (hoặc một cửa sổ khác) đã đổi focus trong lúc Lab đang gõ từng nhịp. "
                   L"Lab dừng ngay để chữ không rơi vào cửa sổ sai — hãy bấm lại vào app đích.";
        case Outcome::Empty:
        case Outcome::Queued:
        case Outcome::Sent:
            return L"";
    }
    return L"";
}

// The Lab's status row keeps a prefix so a screenshot/reading user can tell a
// warning from a confirmation at a glance.
[[nodiscard]] inline std::wstring statusLineVi(Outcome o) {
    std::wstring line;
    line += isFailure(o) ? L"⚠ " : L"• ";
    line += reasonVi(o);
    return line;
}

} // namespace ok::flexsend

#endif // KIEEKEY_APP_FLEX_SEND_OUTCOME_HPP
