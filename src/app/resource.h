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
// File: src/app/resource.h
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — resource.h
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
#define IDM_ABOUT                 406   // v1.1.2: open the Information tab

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

// v1.1.1 — the in-app ON/OFF switch (replaces the removed Ctrl+Shift hotkey)
#define IDC_BTN_TOGGLE            550   // settings dialog: always-visible toggle button

// v1.1.2 — digits-are-numbers option + modernized dialog chrome + info tab
#define IDC_CHK_DIGITS            551   // tab 0: "Số 0–9 luôn là chữ số"
#define IDC_STAT_HEAD_ICON        552   // header: app icon (static w/ icon)
#define IDC_STAT_HEAD_TITLE       553   // header: app title
#define IDC_STAT_HEAD_STATUS      554   // header: engine status + method line
#define IDC_GRP_METHOD            555   // tab 0: group box "Phương thức gõ"
#define IDC_GRP_OPTIONS           556   // tab 0: group box "Tùy chọn gõ"
#define IDC_GRP_OUTPUT            557   // tab 0: group box "Chế độ xuất"
#define IDC_STAT_INFO_NAME        558   // tab 4: big app name + version
#define IDC_STAT_INFO_STATUS      559   // tab 4: live diagnostics + conflict verdict
#define IDC_STAT_INFO_ABOUT       560   // tab 4: about paragraph
#define IDC_STAT_INFO_FEAT        561   // tab 4: feature list
#define IDC_STAT_INFO_GUIDE       562   // tab 4: quick-start guide
#define IDC_STAT_INFO_LICENSE     563   // tab 4: origin & license
#define IDC_LNK_REPO              564   // tab 4: source repository (SysLink)
#define IDC_STAT_METHOD_HINT      565   // tab 0: input-method explainer line
#define IDC_STAT_PERF_LAB         566   // tab 0: "Hồ sơ hiệu năng:"
#define IDC_COMBO_PERF            567   // tab 0: profile combo
#define IDC_CHK_PERF_LOWCPU       568   // tab 0: hybrid — tiết kiệm CPU
#define IDC_CHK_PERF_DICT         569   // tab 0: hybrid — kiểm tra từ điển
#define IDC_STAT_PERF_NOTE        570   // tab 0: profile explainer
#define IDC_CHK_NOTIFY            571   // tab 0: thông báo thông minh (mute)

// v1.3.0 — Arcade, Chaos Lab, AI & Progression controls
#define IDC_GRP_ARCADE            580
#define IDC_BTN_PLAY_SNAKE        581
#define IDC_BTN_PLAY_TETRIS       582
#define IDC_BTN_PLAY_FISHING      583
#define IDC_BTN_PLAY_TYPINGRACE   584
#define IDC_BTN_PLAY_WASDRACE     585
#define IDC_BTN_PLAY_RHYTHM       586
#define IDC_BTN_PLAY_NOMISTAKE    587
#define IDC_BTN_PLAY_FLEXING      588
#define IDC_STAT_ARCADE_STATUS    589
#define IDC_CMB_FAILMODE          596   // Rhythm / No-Mistake: Hardcore vs HP
#define IDC_EDT_RHYTHM_BPM        597
#define IDC_BTN_OPEN_CHAOS_LAB    598   // dedicated Chaos/Flexing test window
#define IDC_BTN_APPLY_ARCADE_CFG  599   // apply fail-mode / BPM to ArcadeManager

#define IDC_STAT_FAILMODE         616   // tab 5: labels must hide with the controls
#define IDC_STAT_RHYTHM_BPM       617
#define IDC_STAT_PASSAGE_LANG     618   // tab 5: VN/EN typing-passage language label
#define IDC_CMB_PASSAGE_LANG      619   // tab 5: Tiếng Việt (Telex/VNI) vs English (ASCII)

// tab 6: Chaos & Lab
#define IDC_GRP_CHAOS             590
#define IDC_CHK_CHAOS_MASTER      591
#define IDC_CHK_CHAOS_CASE        592
#define IDC_CHK_GLYPH_TRANSFORM   593
#define IDC_COMBO_GLYPH_MODE      594
#define IDC_STAT_CHAOS_WARN       595

// tab 7: AI Rival & Coaching
#define IDC_GRP_AI                600
#define IDC_CHK_AI_OPTIN          601
#define IDC_BTN_AI_RESET          602
#define IDC_STAT_AI_STATS         603
#define IDC_STAT_COACH_ADVICE     604

// tab 8: Progression
#define IDC_GRP_PROG              610
#define IDC_STAT_LEVEL_VAL        611
#define IDC_STAT_XP_VAL           612
#define IDC_STAT_KEYS_VAL         613
#define IDC_STAT_ACHIEVEMENTS     614
#define IDC_BTN_PROG_RESET        615

// ---- tray context menu items ----
#define IDM_ARCADE_HUB            410
#define IDM_ARCADE_SNAKE          411
#define IDM_ARCADE_TETRIS         412
#define IDM_ARCADE_FISHING        413
#define IDM_ARCADE_TYPINGRACE     414
#define IDM_ARCADE_WASDRACE       415
#define IDM_ARCADE_RHYTHM         416
#define IDM_ARCADE_NOMISTAKE      417
#define IDM_ARCADE_FLEXING        418
#define IDM_CHAOS_LAB             419
#define IDM_AI_RIVAL              420
#define IDM_PROGRESSION           421
#define IDM_LIVE_EFFECTS          422

// ---- app messages ----
#define WM_APP_TRAY               (WM_APP + 1)   // tray icon notification
// (WM_APP + 2 retired in v1.1.1 — was WM_APP_TOGGLE, the hotkey toggle
//  notification; on/off now happens only on the UI thread and saves directly)
#define WM_APP_UPDATE_TIP         (WM_APP + 3)   // refresh tray tooltip/balloon
#define WM_APP_RESTORE            (WM_APP + 4)   // 2nd-instance wake: restore tray icon
#define WM_APP_FGPROBE            (WM_APP + 5)   // probe foreground responsiveness (UI thread)
#define WM_APP_NOTIFY_POLL        (WM_APP + 6)   // v1.2.1 RC2: notification center poll / adaptive tick

// Explicit live output channel (independent of Lab configuration).
#define IDC_GRP_LIVE              620
#define IDC_CHK_LIVE              621
#define IDC_CHK_LIVE_CASE         622
#define IDC_CMB_LIVE_GLYPH        623
#define IDC_STAT_LIVE_GLYPH       624
#define IDC_CMB_LIVE_INTENSITY    625
#define IDC_STAT_LIVE_INTENSITY   626
#define IDC_STAT_LIVE_HINT        627

// ---- v1.3.0-beta4: diagnostics tab (tab 3 "Chẩn đoán") self-check UI ----
#define IDC_GRP_DIAG             630
#define IDC_RAD_DIAG_OFF         631
#define IDC_RAD_DIAG_BASIC       632
#define IDC_RAD_DIAG_FULL        633
#define IDC_BTN_DIAG_RUN         634
#define IDC_BTN_DIAG_REPORT      635
#define IDC_STAT_DIAG_RESULT     636
#define IDC_BTN_DIAG_COPY        637   /* v1.3.0-beta6 (V4): copy report to clipboard */
/* ---- v1.3.0-beta8 (bug DS-01): the report is now readable INSIDE the app.
   The tab could export and copy its report but never display it: the only
   in-app feedback was one clipped status line. */
#define IDC_BTN_DIAG_VIEW        638   /* load report(40) + the gate line into the pane */
#define IDC_BTN_DIAG_OPEN        639   /* ShellExecuteW on the last exported file */
#define IDC_GRP_DIAG_REPORT      649   /* the pane's group box */
#define IDC_EDIT_DIAG_REPORT     650   /* read-only, scrollable report pane */

// ---- v1.3.0-beta5 (bug B3): per-source telemetry rows (tab 3) ----
// "Sự kiện bàn phím đã xử lý" was fed by pushed() — the ring counter that
// keyboard AND mouse AND foreground events all increment. These rows give
// each source its own honest number (HookCounters.hpp).
#define IDC_STAT_MOUSELAB        640
#define IDC_STAT_MOUSEV          641
#define IDC_STAT_FGLAB           642
#define IDC_STAT_FGV             643
#define IDC_STAT_RINGLAB         644
#define IDC_STAT_RINGV           645
// ---- v1.3.0-beta5 (bug B2): live-effects gate visibility ----
// The tester could not tell WHICH condition vetoed the effects. Tab 6 now
// carries a status line driven by ok::effects::liveGateBlocker() — the same
// pure decision the hook evaluates.
#define IDC_STAT_LIVE_GATE       646
// ---- v1.3.0-beta5 (bug B7): WASD-race steering-key choice (tab 5) ----
#define IDC_STAT_STEERING        647
#define IDC_CMB_STEERING         648
