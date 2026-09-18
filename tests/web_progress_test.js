//============================================================================
// KieeKey — tests/web_progress_test.js
// Headless (Node) test for the browser's progression + AI rival panel.
//
// Same idea as tests/web_labs_test.js: no browser in the sandbox, so a small
// DOM/fetch double runs the real web/progress.js and checks the behaviour the
// user asked for — "level up from typing a lot" and "the AI learns you, then
// races you", both visible and testable in the web player.
//
// Run:  node tests/web_progress_test.js
//============================================================================
'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

let failures = 0;
let checks = 0;

function assert(condition, message) {
  ++checks;
  if (!condition) { ++failures; console.error('  [FAIL] ' + message); }
}
function section(title) {
  console.log('  [' + (failures === 0 ? 'PASS' : 'FAIL') + '] ' + title);
}

function makeElement(id) {
  const listeners = {};
  const classes = new Set();
  return {
    id, value: '', textContent: '', className: '', checked: false, style: {},
    handlers: listeners,
    classList: {
      add: (n) => classes.add(n), remove: (n) => classes.delete(n),
      contains: (n) => classes.has(n),
      toggle: (n, f) => { if (f) { classes.add(n); } else { classes.delete(n); } },
    },
    addEventListener: (type, handler) => { (listeners[type] = listeners[type] || []).push(handler); },
    dispatch: (type, event) => { for (const h of (listeners[type] || [])) { h(event || {}); } },
    closest: () => null,
    append: () => {}, appendChild: () => {},
  };
}

const ids = ['toast', 'progLevel', 'progXp', 'progBar', 'progPercent', 'progBestWpm', 'progKeys',
             'progStreak', 'progAchievements', 'progRace', 'progRefresh', 'rivalPill',
             'rivalOptIn', 'rivalSamples', 'rivalWpm', 'rivalFinish', 'rivalError',
             'rivalTrain', 'rivalReset'];
const elements = {};
for (const id of ids) { elements[id] = makeElement(id); }

const requests = [];
const intervals = [];
let progression = {
  ok: true, level: 4, xp: 2640, xpIntoLevel: 140, xpForLevel: 300, levelPercent: 46,
  bestWpm: 72.4, bestAccuracy: 97.5, totalKeystrokes: 18422, totalWords: 3100,
  typingTimeSeconds: 2400, streakDays: 3, achievements: 5, typingRaceBestWpm: 68.2,
  rhythmHighScore: 1200, noMistakeMaxCombo: 42,
};
let rival = {
  ok: true, optIn: false, samples: 0, pendingObservations: 0, meanIkiMs: 150,
  errorRate: 0.025, toneDelayMs: 175, profileWpm: 80, ghostSamples: 0,
  passageChars: 0, rivalFinishSec: 0, rivalWpm: 0,
};

const sandbox = {
  console,
  setTimeout: () => 0,
  clearTimeout: () => {},
  setInterval: (fn) => intervals.push({ fn, cleared: false }),
  clearInterval: (id) => { if (id != null) { intervals[id - 1].cleared = true; } },
  performance: { now: () => 0 },
  document: {
    hidden: false,
    getElementById: (id) => elements[id] || null,
    createElement: (tag) => makeElement(tag),
    addEventListener: () => {},
  },
  window: { addEventListener: () => {} },
  fetch: async (url, options) => {
    const body = options && options.body ? JSON.parse(options.body) : null;
    requests.push({ url, body });
    if (url === 'api/progression') {
      return { ok: true, status: 200, json: async () => progression };
    }
    if (url === 'api/rival') {
      if (body && typeof body.optIn === 'boolean') { rival = Object.assign({}, rival, { optIn: body.optIn }); }
      if (body && body.reset) { rival = Object.assign({}, rival, { samples: 0, profileWpm: 80 }); }
      if (body && body.train) { rival = Object.assign({}, rival, { samples: 1234, profileWpm: 71.5 }); }
      return { ok: true, status: 200, json: async () => rival };
    }
    return { ok: true, status: 200, json: async () => ({ ok: true }) };
  },
};
sandbox.globalThis = sandbox;

vm.createContext(sandbox);
vm.runInContext(fs.readFileSync(path.resolve(__dirname, '..', 'web', 'progress.js'), 'utf8'),
                sandbox, { filename: 'web/progress.js' });

async function flush() {
  for (let i = 0; i < 64; ++i) { await Promise.resolve(); }
}

//---------------------------------------------------------------------------
async function testProgressionPanel() {
  await sandbox.window.KieeKeyProgress.refresh();
  await flush();

  assert(elements.progLevel.textContent === 4, 'the level comes from the engine');
  assert(elements.progXp.textContent.length > 0, 'the XP total is shown');
  assert(elements.progBar.style.width === '46%', 'the XP bar follows levelPercent');
  assert(elements.progPercent.textContent === '46%', 'the percentage is shown');
  assert(elements.progBestWpm.textContent === '72.4', 'the best WPM is shown');
  assert(elements.progStreak.textContent === 3, 'the day streak is shown');
  assert(elements.progAchievements.textContent === 5, 'the achievement count is shown');
  section('Progression panel renders engine numbers');
}

//---------------------------------------------------------------------------
async function testLevelUpToast() {
  // The first read primes the level; a later read that is higher must celebrate.
  progression = Object.assign({}, progression, { level: 5, xp: 3000, levelPercent: 0 });
  sandbox.window.KieeKeyProgress.onState({ gameOver: true });
  await flush();
  assert(elements.toast.textContent.indexOf('Lên cấp 5') !== -1,
         'a level-up raises a toast (the "type a lot" reward)');
  assert(!elements.toast.classList.contains('hidden'), 'the toast is visible');

  // Re-reading the same level must not spam toasts.
  elements.toast.textContent = '';
  progression = Object.assign({}, progression, { levelPercent: 10 });
  await sandbox.window.KieeKeyProgress.refresh();
  await flush();
  assert(elements.toast.textContent === '', 'no toast when the level did not change');
  section('Level-up feedback');
}

//---------------------------------------------------------------------------
async function testRivalPanel() {
  assert(elements.rivalOptIn.checked === false, 'the rival starts opted out');
  assert(elements.rivalPill.textContent.indexOf('TẮT') !== -1, 'the pill says it is off');

  elements.rivalOptIn.checked = true;
  elements.rivalOptIn.dispatch('change');
  await flush();

  const optInPost = requests.filter((r) => r.url === 'api/rival' && r.body && r.body.optIn === true);
  assert(optInPost.length === 1, 'the opt-in switch is sent to the engine');
  await sandbox.window.KieeKeyProgress.refresh();
  await flush();
  assert(elements.rivalPill.textContent.indexOf('ĐANG HỌC') !== -1,
         'the pill reports the live learning state');

  elements.rivalTrain.dispatch('click');
  await flush();
  assert(requests.some((r) => r.url === 'api/rival' && r.body && r.body.train === true),
         'training is requested from the engine, not simulated in the browser');
  assert(elements.rivalSamples.textContent === 1234, 'the learned sample count is shown');
  assert(elements.rivalWpm.textContent === '71.5 WPM', 'the learned pace is shown');

  // The rival's finish time on the passage being raced right now.
  rival = Object.assign({}, rival, { rivalFinishSec: 41.2, passageChars: 210, rivalWpm: 75.4 });
  await sandbox.window.KieeKeyProgress.refresh();
  await flush();
  assert(elements.rivalFinish.textContent.indexOf('41.2 s') !== -1,
         'the rival finish time for the live passage is shown');
  assert(elements.rivalFinish.textContent.indexOf('210') !== -1,
         '...together with the passage length it was computed for');

  elements.rivalReset.dispatch('click');
  await flush();
  assert(requests.some((r) => r.url === 'api/rival' && r.body && r.body.reset === true),
         'resetting the profile is an explicit request to the engine');
  section('AI rival panel (opt-in, learned pace, live race preview)');
}

//---------------------------------------------------------------------------
async function testPollingIsVisibleAware() {
  assert(intervals.length >= 1, 'the panel polls periodically');
  const before = requests.length;
  sandbox.document.hidden = true;
  intervals[intervals.length - 1].fn();
  await flush();
  assert(requests.length === before, 'a hidden tab does not poll the engine');
  sandbox.document.hidden = false;
  intervals[intervals.length - 1].fn();
  await flush();
  assert(requests.length > before, 'a visible tab refreshes again');
  section('Polling stays cheap (visibility-aware)');
}

//---------------------------------------------------------------------------
(async () => {
  console.log('=== Running Web Progression/AI panel (headless) Suite ===');
  await testProgressionPanel();
  await testLevelUpToast();
  await testRivalPanel();
  await testPollingIsVisibleAware();
  if (failures === 0) {
    console.log('=== ALL WEB PROGRESS TESTS PASSED (' + checks + ' checks) ===');
    process.exit(0);
  }
  console.error('=== WEB PROGRESS TESTS FAILED (' + failures + '/' + checks + ') ===');
  process.exit(1);
})();
