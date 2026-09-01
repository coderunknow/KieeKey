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
// File: src/app/resource.h
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.0 — resource.h
// Resource identifiers for the Win32 tray application (src/app/main.cpp).
//----------------------------------------------------------------------------
#pragma once

// ---- resources ----
#define IDI_APPICON               101
#define IDI_APPICON_OFF           102
#define IDR_MANIFEST              1

// ---- tray context menu ----
#define IDM_TOGGLE                400
#define IDM_METHOD_TELEX          401
#define IDM_METHOD_VNI            402
#define IDM_METHOD_SIMPLETELEX    403
#define IDM_SETTINGS              404
#define IDM_EXIT                  405
// v1.1.0 — quick actions that used to require opening the settings dialog
#define IDM_RUN_AT_STARTUP        406   // toggle "start with Windows"
#define IDM_ABOUT                 407   // about / version box

// ---- settings dialog controls ----
#define IDC_TAB                   500
// tab 0 — Bàn phím
#define IDC_RADIO_TELEX           501
#define IDC_RADIO_VNI             502
#define IDC_RADIO_SIMPLETELEX     503
#define IDC_COMBO_CODETABLE       504
#define IDC_CHK_MACRO             505
#define IDC_CHK_SPELL             506
#define IDC_CHK_RESTORE           507
#define IDC_CHK_UPPER             508
#define IDC_CHK_MODERN            509
#define IDC_CHK_QUICK             510
// tab 1 — Ứng dụng
#define IDC_CHK_EXCLUDE_IDE       511
#define IDC_CHK_EXCLUDE_GAME      512
#define IDC_CHK_EXCLUDE_SHELL     513
// tab 2 — Chẩn đoán
#define IDC_STAT_LATVAL           514
#define IDC_STAT_PUSHV            515
#define IDC_STAT_DROPV            516
#define IDC_BTN_APPLY             517

// static labels (must have IDs so showTab() can hide/show them per tab)
#define IDC_STAT_METHOD           518   // tab 0
#define IDC_STAT_CODETABLE        519
#define IDC_STAT_APPS_TITLE       520   // tab 1
#define IDC_STAT_APPS_NOTE        521
#define IDC_STAT_LATLAB           522   // tab 2
#define IDC_STAT_PUSHLAB          523
#define IDC_STAT_DROPLAB          524
#define IDC_STAT_DESC             525

// tab 0 — output mode (WPM: inline SendInput is the zero-latency path)
#define IDC_STAT_OUT_LAB          526
#define IDC_RADIO_OUT_AUTO        527
#define IDC_RADIO_OUT_TSF         528
#define IDC_RADIO_OUT_SEND        529
#define IDC_STAT_OUT_NOTE         530

// tab 2 — extra telemetry
#define IDC_STAT_AVGLAB           531
#define IDC_STAT_AVGVAL           532
#define IDC_STAT_WPMLAB           533
#define IDC_STAT_WPMVAL           534

// ---- v1.1.0 — general tab (Bàn phím) additions ----
#define IDC_CHK_RUN_AT_STARTUP    535   // start KieeKey with Windows
#define IDC_CHK_NOTIFY_TOGGLE     536   // balloon when the IME is toggled
#define IDC_BTN_DEFAULTS          537   // restore the shipped defaults
#define IDC_STAT_VERSION          538   // "KieeKey v1.1.0" footer

// ---- v1.1.0 — diagnostics tab additions ----
#define IDC_STAT_SATLAB           539   // queue saturation runs
#define IDC_STAT_SATVAL           540
#define IDC_STAT_SATPPLAB         541   // longest saturation window
#define IDC_STAT_SATPPVAL         542

// ---- app messages ----
#define WM_APP_TRAY               (WM_APP + 1)   // tray icon notification
#define WM_APP_TOGGLE             (WM_APP + 2)   // engine on/off changed (hook thread)
#define WM_APP_UPDATE_TIP         (WM_APP + 3)   // refresh tray tooltip/balloon
#define WM_APP_RESTORE            (WM_APP + 4)   // 2nd-instance wake: restore tray icon
#define WM_APP_FGPROBE            (WM_APP + 5)   // probe foreground responsiveness (UI thread)
#define WM_APP_TOGGLE_NOTIFY      (WM_APP + 6)   // v1.1.0: show the on/off balloon
