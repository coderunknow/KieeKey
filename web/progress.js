//============================================================================
// KieeKey — web/progress.js
// "Tiến trình & AI Rival" panel of the web player.
//
// Two features the user asked for, made visible without guessing:
//
//   * Progression — level, XP bar, streak, achievements and the minigame
//     records, read from the same ProgressionEngine the desktop app persists
//     (`GET /api/progression`).
//   * AI Rival — opt-in switch, how many samples the engine learned from, the
//     pace it derived, and the finish time it would get on the passage the
//     player is racing right now (`GET|POST /api/rival`).
//
// The panel only displays numbers the engine produced; it never computes a
// level, an XP curve or a rival pace itself.
//============================================================================
'use strict';

(function () {
  const $ = (id) => document.getElementById(id);

  const ui = {
    level: $('progLevel'),
    xp: $('progXp'),
    bar: $('progBar'),
    percent: $('progPercent'),
    bestWpm: $('progBestWpm'),
    keys: $('progKeys'),
    streak: $('progStreak'),
    achievements: $('progAchievements'),
    race: $('progRace'),
    rivalPill: $('rivalPill'),
    rivalOptIn: $('rivalOptIn'),
    rivalSamples: $('rivalSamples'),
    rivalWpm: $('rivalWpm'),
    rivalFinish: $('rivalFinish'),
    rivalError: $('rivalError'),
    rivalTrain: $('rivalTrain'),
    rivalReset: $('rivalReset'),
    refresh: $('progRefresh'),
  };

  let lastLevel = 0;
  let timer = null;

  async function getJson(path) {
    const response = await fetch(path, { cache: 'no-store' });
    return response.json();
  }

  async function post(path, body) {
    const response = await fetch(path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body || {}),
    });
    return response.json();
  }

  function toast(text) {
    const node = document.getElementById('toast');
    if (!node) { return; }
    node.textContent = text;
    node.classList.remove('hidden');
    clearTimeout(toast.timer);
    toast.timer = setTimeout(() => node.classList.add('hidden'), 1800);
  }

  //--------------------------------------------------------------------------
  async function refreshProgression() {
    if (!ui.level) { return null; }
    const data = await getJson('api/progression');
    if (!data || !data.ok) { return null; }

    ui.level.textContent = data.level;
    ui.xp.textContent = data.xp.toLocaleString('vi-VN');
    if (ui.bar) { ui.bar.style.width = Math.max(0, Math.min(100, data.levelPercent)) + '%'; }
    if (ui.percent) { ui.percent.textContent = data.levelPercent + '%'; }
    if (ui.bestWpm) { ui.bestWpm.textContent = (data.bestWpm || 0).toFixed(1); }
    if (ui.keys) { ui.keys.textContent = data.totalKeystrokes.toLocaleString('vi-VN'); }
    if (ui.streak) { ui.streak.textContent = data.streakDays; }
    if (ui.achievements) { ui.achievements.textContent = data.achievements; }
    if (ui.race) { ui.race.textContent = (data.typingRaceBestWpm || 0).toFixed(1); }

    // A level-up is the one progression event worth interrupting the player for.
    if (lastLevel !== 0 && data.level > lastLevel) {
      toast('🎉 Lên cấp ' + data.level + ' — gõ nhiều thật đấy!');
    }
    lastLevel = data.level;
    return data;
  }

  async function refreshRival() {
    if (!ui.rivalPill) { return null; }
    const data = await getJson('api/rival');
    if (!data || !data.ok) { return null; }

    if (ui.rivalOptIn) { ui.rivalOptIn.checked = data.optIn === true; }
    if (ui.rivalPill) {
      ui.rivalPill.textContent = data.optIn ? 'ĐANG HỌC' : 'TẮT (opt-in)';
      ui.rivalPill.className = 'pill ' + (data.optIn ? 'pill-ok' : '');
    }
    if (ui.rivalSamples) { ui.rivalSamples.textContent = data.samples; }
    if (ui.rivalWpm) { ui.rivalWpm.textContent = (data.profileWpm || 0).toFixed(1) + ' WPM'; }
    if (ui.rivalError) { ui.rivalError.textContent = ((data.errorRate || 0) * 100).toFixed(1) + '%'; }
    if (ui.rivalFinish) {
      ui.rivalFinish.textContent = (data.rivalFinishSec > 0)
        ? data.rivalFinishSec.toFixed(1) + ' s (đoạn đang chạy: ' + data.passageChars + ' ký tự)'
        : '— chưa có đoạn đua nào đang mở';
    }
    return data;
  }

  async function refresh() {
    await refreshProgression();
    await refreshRival();
  }

  //--------------------------------------------------------------------------
  // Wiring
  //--------------------------------------------------------------------------
  window.KieeKeyProgress = {
    refresh,
    onState: (state) => {
      // Re-read when a run ends: that is when XP and records move.
      if (state && state.gameOver) { void refresh(); }
    },
  };

  if (ui.refresh) { ui.refresh.addEventListener('click', () => void refresh()); }
  if (ui.rivalOptIn) {
    ui.rivalOptIn.addEventListener('change', async () => {
      await post('api/rival', { optIn: ui.rivalOptIn.checked });
      await refresh();
    });
  }
  if (ui.rivalTrain) {
    ui.rivalTrain.addEventListener('click', async () => {
      await post('api/rival', { train: true });
      await refresh();
    });
  }
  if (ui.rivalReset) {
    ui.rivalReset.addEventListener('click', async () => {
      await post('api/rival', { reset: true });
      await refresh();
    });
  }

  // Poll slowly (the numbers are not per-frame), and only while visible.
  const startPolling = () => {
    if (timer !== null) { return; }
    timer = setInterval(() => {
      if (document.hidden) { return; }
      void refresh();
    }, 2000);
  };
  startPolling();
  void refresh();
})();
