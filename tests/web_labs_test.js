//============================================================================
// KieeKey — tests/web_labs_test.js
// Headless (Node) test for the browser side of the two "gõ thật" labs.
//
// There is no browser in the build sandbox, so this file provides the smallest
// DOM/fetch double that web/labs.js actually touches, loads the real client
// file, and checks the behaviour the user asked for:
//
//   1. Flexing Mode — the characters produced by the C++ engine land in the
//      editable "cửa sổ gõ thật" control, and the prepared passage is what gets
//      loaded into the engine (the browser never invents text).
//   2. Chaos Mode — the lab posts the live knob values and shows exactly the
//      bytes the engine would emit (injected) next to the display transform.
//   3. Keys typed into a lab text field are never forwarded to the game engine.
//
// Run:  node tests/web_labs_test.js
//============================================================================
'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.resolve(__dirname, '..', 'web');

let failures = 0;
let checks = 0;

function assert(condition, message) {
  ++checks;
  if (!condition) {
    ++failures;
    console.error('  [FAIL] ' + message);
  }
}

function section(title) {
  console.log('  [' + (failures === 0 ? 'PASS' : 'FAIL') + '] ' + title);
}

//---------------------------------------------------------------------------
// Minimal DOM double: element registry with the handful of members labs.js and
// arcade.js use (classList, value, textContent, addEventListener, closest).
//---------------------------------------------------------------------------
function makeElement(id) {
  const listeners = {};
  const classes = new Set();
  const element = {
    id,
    value: '',
    textContent: '',
    className: '',
    scrollTop: 0,
    scrollHeight: 0,
    checked: false,
    children: [],
    handlers: listeners,
    classList: {
      add: (name) => classes.add(name),
      remove: (name) => classes.delete(name),
      contains: (name) => classes.has(name),
      toggle: (name, force) => {
        if (force === undefined) {
          if (classes.has(name)) { classes.delete(name); } else { classes.add(name); }
        } else if (force) { classes.add(name); } else { classes.delete(name); }
      },
    },
    addEventListener: (type, handler) => { (listeners[type] = listeners[type] || []).push(handler); },
    dispatch: (type, event) => {
      for (const handler of (listeners[type] || [])) { handler(event || {}); }
    },
    closest: (selector) => (selector === '.lab-input' && element.labInput ? element : null),
    labInput: false,
    append: () => {},
    appendChild: () => {},
    querySelectorAll: () => [],
    focus: () => { sandbox.document.activeElement = element; },
  };
  return element;
}

const ids = [
  'toast', 'labFlexing', 'labChaos', 'flexSource', 'flexTarget', 'flexGran', 'flexN',
  'flexPill', 'flexWpm', 'flexGen', 'flexKeys', 'flexEff', 'flexCursor', 'flexPreload',
  'flexClear', 'flexStart', 'openFlexingLab', 'openChaosLab', 'closeLabs',
  'chaosMaster', 'chaosCase', 'chaosCaseIntensity', 'chaosCaseOut', 'chaosCaseGran',
  'chaosGlyph', 'chaosGlyphMode', 'chaosGlyphIntensity', 'chaosGlyphOut', 'chaosSource',
  'chaosInjected', 'chaosDisplay', 'chaosPill', 'chaosHonesty', 'chaosApply', 'chaosClear',
];

const elements = {};
for (const id of ids) { elements[id] = makeElement(id); }
elements.flexSource.labInput = true;
elements.chaosSource.labInput = true;
elements.flexGran.value = '0';
elements.flexN.value = '3';
elements.chaosCaseIntensity.value = '50';
elements.chaosGlyphIntensity.value = '100';
elements.chaosGlyphMode.value = '0';
elements.chaosCaseGran.value = '0';
// The panels start hidden, exactly like in index.html.
elements.labFlexing.classList.add('hidden');
elements.labChaos.classList.add('hidden');

const requests = [];
const responses = {
  'api/preload': { ok: true, total: 42, granularity: 0 },
  'api/chaos': { ok: true, active: false, enabled: false },
  'api/chaos/preview': { ok: true, preview: { injected: '', display: '' }, renderOnlyMode: false },
};

// Timers are hand-driven so the replay used by "gõ thử vào cửa sổ" can be
// checked step by step instead of racing the event loop.
const intervals = [];
function runIntervals(times) {
  for (let i = 0; i < times; ++i) {
    for (const entry of intervals) {
      if (!entry.cleared) { entry.fn(); }
    }
  }
}

const sandbox = {
  console,
  setTimeout: () => 0,
  clearTimeout: () => {},
  setInterval: (fn) => intervals.push({ fn, cleared: false }),
  clearInterval: (id) => { if (id != null) { intervals[id - 1].cleared = true; } },
  performance: { now: () => 0 },
  document: {
    activeElement: null,
    getElementById: (id) => elements[id] || null,
    createElement: (tag) => makeElement(tag),
    addEventListener: () => {},
  },
  window: { addEventListener: () => {} },
  fetch: async (url, options) => {
    const body = options && options.body ? JSON.parse(options.body) : null;
    requests.push({ url, body });
    const payload = responses[url] || { ok: true };
    return { ok: true, status: 200, json: async () => payload };
  },
};
sandbox.window.KieeKeyLabs = undefined;
sandbox.globalThis = sandbox;

vm.createContext(sandbox);
const source = fs.readFileSync(path.join(ROOT, 'labs.js'), 'utf8');
vm.runInContext(source, sandbox, { filename: 'web/labs.js' });

async function flush() {
  // Let the microtask queue drain so the awaited fetch() chains complete
  // (pushChaos -> post -> previewChaos -> post is several awaits deep).
  for (let i = 0; i < 64; ++i) { await Promise.resolve(); }
}

//---------------------------------------------------------------------------
async function testFlexingStage() {
  elements.flexTarget.value = '';

  // Opening the stage seeds the prepared passage and loads it into the engine.
  elements.openFlexingLab.dispatch('click');
  await flush();
  const seeded = requests.filter((r) => r.url === 'api/preload').pop();
  assert(!!seeded, 'opening the Flexing stage loads the prepared text');
  assert(seeded && seeded.body && seeded.body.text.length > 0,
         'the seeded passage is not empty');

  // Editing it and pressing "Nạp vào engine" sends exactly the edited text.
  requests.length = 0;
  elements.flexSource.value = 'Xin chao!';
  elements.flexGran.value = '1';
  elements.flexPreload.dispatch('click');
  await flush();
  const preload = requests.filter((r) => r.url === 'api/preload').pop();
  assert(preload && preload.body && preload.body.text === 'Xin chao!',
         'the passage sent to the engine is the one in the text box');
  assert(preload && preload.body.granularity === 1, 'granularity comes from the combo');
  assert(!elements.labFlexing.classList.contains('hidden'), 'the stage becomes visible');

  // The streamed engine output is what appears in the target control.
  sandbox.window.KieeKeyLabs.onState({ slug: 'flexing', flex: {
    emitted: 'Xin ', wpm: 120, cursor: 4, total: 42, generated: 4, keys: 1, efficiency: 4,
  } });
  assert(elements.flexTarget.value === 'Xin ', 'engine characters land in the target box');
  assert(elements.flexGen.textContent === 4, 'generated counter follows the engine');
  assert(elements.flexEff.textContent === 'x4.00', 'efficiency multiplier is shown');
  assert(elements.flexCursor.textContent === '4 / 42', 'cursor position is shown');

  sandbox.window.KieeKeyLabs.onState({ slug: 'flexing', flex: {
    emitted: 'chao', wpm: 120, cursor: 8, total: 42, generated: 8, keys: 2, efficiency: 4,
  } });
  assert(elements.flexTarget.value === 'Xin chao', 'further output is appended, never replaced');

  elements.flexSource.focus();
  sandbox.window.KieeKeyLabs.onState({ slug: 'flexing', flex: { emitted: '', cursor: 8, total: 42 } });
  assert(sandbox.document.activeElement === elements.flexSource,
         'stream frames do not steal focus from passage editing');
  sandbox.window.KieeKeyLabs.closeLabs();
  sandbox.window.KieeKeyLabs.onState({ slug: 'flexing', flex: { emitted: '', cursor: 8, total: 42 } });
  assert(elements.labFlexing.classList.contains('hidden'), 'stream does not reopen a dismissed lab');

  // A non-Flexing frame must not touch the control.
  sandbox.window.KieeKeyLabs.onState({ slug: 'snake', flex: { emitted: 'zzz', cursor: 0, total: 0 } });
  assert(elements.flexTarget.value === 'Xin chao', 'other games leave the flexing box alone');

  // Clearing the box is a UI-only action.
  elements.flexClear.dispatch('click');
  assert(elements.flexTarget.value === '', 'the clear button empties the target box');
  section('Flexing stage renders the engine\'s own text');
}

//---------------------------------------------------------------------------
async function testChaosLab() {
  responses['api/chaos'] = {
    ok: true, active: true, enabled: true, randomCase: true, glyph: true, glyphMode: 2,
  };
  responses['api/chaos/preview'] = {
    ok: true,
    renderOnlyMode: false,
    preview: { injected: 'NGUYEN VAN A', display: 'ɐ uɐʌ uǝʎnƃu' },
  };

  elements.chaosMaster.checked = true;
  elements.chaosCase.checked = true;
  elements.chaosGlyph.checked = true;
  elements.chaosGlyphMode.value = '2';
  elements.chaosCaseIntensity.value = '100';
  elements.chaosSource.value = 'nguyen van a';

  requests.length = 0;
  // The knobs push the configuration as they change, like in index.html...
  elements.chaosMaster.dispatch('change');
  elements.chaosCase.dispatch('change');
  elements.chaosGlyph.dispatch('change');
  elements.chaosGlyphMode.dispatch('change');
  await flush();
  // ...and the button runs the "type it for real" replay.
  elements.chaosApply.dispatch('click');
  await flush();

  const post = requests.find((r) => r.url === 'api/chaos');
  assert(!!post, 'the lab applies its configuration through the API');
  assert(post && post.body.enabled === true, 'master switch is forwarded');
  assert(post && post.body.glyphMode === 2, 'glyph mode is forwarded as a number');
  assert(post && post.body.caseIntensityPercent === 100, 'case intensity is forwarded');

  const preview = requests.find((r) => r.url === 'api/chaos/preview');
  assert(!!preview, 'the preview is produced by the engine, not by the browser');
  assert(preview && preview.body.text === 'nguyen van a', 'the preview uses the sampled text');
  runIntervals(64);
  assert(elements.chaosInjected.value === 'NGUYEN VAN A',
         'the replay types exactly the bytes the engine would emit');
  assert(elements.chaosDisplay.value === 'ɐ uɐʌ uǝʎnƃu',
         'the display box shows the render-only glyph transform');
  assert(elements.chaosPill.textContent === 'ĐANG BẬT', 'the pill reports the live state');

  // 90° rotation must be advertised as render-only.
  responses['api/chaos/preview'] = {
    ok: true, renderOnlyMode: true, preview: { injected: 'abc', display: 'abc' },
  };
  elements.chaosGlyphMode.value = '1';
  elements.chaosGlyphMode.dispatch('change');
  await flush();
  assert(elements.chaosHonesty.textContent.indexOf('KHÔNG gõ') !== -1,
         'render-only rotations are announced honestly: ' + elements.chaosHonesty.textContent);

  section('Chaos lab previews the real engine output');
}

//---------------------------------------------------------------------------
function testNativeInputGuard() {
  const api = sandbox.window.KieeKeyLabs;
  assert(typeof api.isNativeInput === 'function', 'the labs expose the input guard');
  assert(api.isNativeInput(elements.flexSource) === true,
         'editing the prepared passage stays native text editing');
  assert(api.isNativeInput(elements.chaosSource) === true,
         'editing the chaos sample stays native text editing');
  assert(api.isNativeInput(elements.flexTarget) === false,
         'the flexing target box is NOT a native input (the engine owns its text)');
  assert(api.isNativeInput(null) === false, 'null targets are handled');
  section('Lab text fields keep the keyboard out of the game engine');
}

//---------------------------------------------------------------------------
(async () => {
  console.log('=== Running Web Labs (headless) Suite ===');
  await testFlexingStage();
  await testChaosLab();
  testNativeInputGuard();
  if (failures === 0) {
    console.log('=== ALL WEB LAB TESTS PASSED (' + checks + ' checks) ===');
    process.exit(0);
  }
  console.error('=== WEB LAB TESTS FAILED (' + failures + '/' + checks + ') ===');
  process.exit(1);
})();
