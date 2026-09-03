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

// v1.1.0 — tab 2: Gõ tắt (macro editor) + tab 3: extra diagnostics rows
#define IDC_EDIT_MACRO            540   // tab 2: multiline macro definition editor
#define IDC_STAT_MACRO_HINT       541   // tab 2: format hint
#define IDC_STAT_BARRIERLAB       542   // tab 3: ordering-barrier timeouts
#define IDC_STAT_BARRIERV         543
#define IDC_STAT_REINSTLAB        544   // tab 3: hook self-healing reinstalls
#define IDC_STAT_REINSTV          545
#define IDC_STAT_TSFLAB           546   // tab 3: slow TSF commits (watchdog)
#define IDC_STAT_TSFV             547
#define IDC_STAT_APPLAB           548   // tab 3: current foreground app + state
#define IDC_STAT_APPV             549

// ---- app messages ----
#define WM_APP_TRAY               (WM_APP + 1)   // tray icon notification
#define WM_APP_TOGGLE             (WM_APP + 2)   // engine on/off changed (hook thread)
#define WM_APP_UPDATE_TIP         (WM_APP + 3)   // refresh tray tooltip/balloon
#define WM_APP_RESTORE            (WM_APP + 4)   // 2nd-instance wake: restore tray icon
#define WM_APP_FGPROBE            (WM_APP + 5)   // probe foreground responsiveness (UI thread)
