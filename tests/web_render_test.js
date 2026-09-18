//============================================================================
// KieeKey — tests/web_render_test.js
// Headless test for the browser renderer (web/arcade.js).
//
// There is no browser in the build sandbox, so the CanvasRenderingContext2D is
// replaced by a recording double and the real drawFrame() runs against frames
// captured from the real C++ engine (tests/data/web_frames.json, dumped over
// the HTTP bridge). That proves the client can draw every game, with the right
// colours, text and scaling — not that it looks pretty (no pixels are
// produced), and this file says so instead of pretending otherwise.
//
// Run:  node tests/web_render_test.js
//============================================================================
'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.resolve(__dirname, '..');
const fixtures = JSON.parse(fs.readFileSync(path.join(ROOT, 'tests/data/web_frames.json'), 'utf8'));

let failures = 0;
let checks = 0;

function assert(condition, message) {
  ++checks;
  if (!condition) { ++failures; console.error('  [FAIL] ' + message); }
}
function section(title) {
  console.log('  [' + (failures === 0 ? 'PASS' : 'FAIL') + '] ' + title);
}

//---------------------------------------------------------------------------
// Recording canvas
//---------------------------------------------------------------------------
const calls = [];
function makeContext() {
  const ctx = {
    fillStyle: '',
    strokeStyle: '',
    lineWidth: 0,
    font: '',
    textAlign: '',
    textBaseline: '',
    lineCap: '',
    lineJoin: '',
    transform: null,
    record(kind, arg) { calls.push({ kind, arg, fillStyle: ctx.fillStyle, font: ctx.font }); },
  };
  const methods = [
    'clearRect', 'fillRect', 'beginPath', 'rect', 'moveTo', 'lineTo', 'closePath',
    'quadraticCurveTo', 'arc', 'fill', 'stroke', 'fillText', 'setTransform',
    'createLinearGradient', 'addColorStop',
  ];
  for (const name of methods) {
    ctx[name] = (...args) => {
      if (name === 'setTransform') { ctx.transform = args; }
      ctx.record(name, args);
    };
  }
  ctx.createLinearGradient = (...args) => {
    ctx.record('createLinearGradient', args);
    return { addColorStop: (...stops) => ctx.record('addColorStop', stops) };
  };
  return ctx;
}

//---------------------------------------------------------------------------
// Minimal DOM double (only the members arcade.js touches at load time)
//---------------------------------------------------------------------------
function makeElement(id) {
  const listeners = {};
  const classes = new Set();
  const el = {
    id, value: '', textContent: '', className: '', width: 1280, height: 720, tabIndex: 0,
    dataset: {}, classList: {
      add: (n) => classes.add(n), remove: (n) => classes.delete(n),
      contains: (n) => classes.has(n),
      toggle: (n, f) => { if (f) { classes.add(n); } else { classes.delete(n); } },
    },
    addEventListener: (t, h) => { (listeners[t] = listeners[t] || []).push(h); },
    append: () => {}, appendChild: () => {}, focus: () => {},
    querySelectorAll: () => [], getBoundingClientRect: () => ({ left: 0, top: 0, width: 1280, height: 720 }),
    closest: () => null,
    handlers: listeners,
  };
  return el;
}

const ids = ['screen', 'title', 'score', 'best', 'level', 'combo', 'lives', 'wpm', 'acc',
             'banner', 'toast', 'connection', 'fps', 'catalog', 'subtitle', 'failMode',
             'bpm', 'bpmOut', 'pacer', 'pacerOut', 'restart', 'pause', 'stop'];
const elements = {};
for (const id of ids) { elements[id] = makeElement(id); }
elements.screen.getContext = () => theContext;

const theContext = makeContext();

let streamOpened = false;
const keyHandlers = {};
const requests = [];
const sandbox = {
  console,
  performance: { now: () => 0 },
  setTimeout: () => 0,
  clearTimeout: () => {},
  setInterval: () => 0,
  clearInterval: () => {},
  document: {
    activeElement: null,
    getElementById: (id) => elements[id] || null,
    createElement: (tag) => makeElement(tag),
    querySelectorAll: () => [],
    addEventListener: () => {},
  },
  window: {
    addEventListener: (type, handler) => { keyHandlers[type] = handler; },
    KieeKeyLabs: undefined,
  },
  fetch: async (url) => { requests.push(url); return { ok: true, status: 200, json: async () => ({ ok: true, games: [] }) }; },
  EventSource: function EventSource() { streamOpened = true; },
};
sandbox.globalThis = sandbox;

vm.createContext(sandbox);
vm.runInContext(fs.readFileSync(path.join(ROOT, 'web/arcade.js'), 'utf8'), sandbox,
                { filename: 'web/arcade.js' });

const api = {
  drawFrame: sandbox.drawFrame,
  onState: sandbox.onState,
  cssColor: sandbox.cssColor,
  isVisible: sandbox.isVisible,
  virtualKeyFor: sandbox.virtualKeyFor,
};

//---------------------------------------------------------------------------
function testColorHelpers() {
  assert(api.cssColor('#FF8A3DFF') === 'rgba(255,138,61,1.000)', 'cssColor parses #RRGGBBAA');
  assert(api.cssColor('#00000080') === 'rgba(0,0,0,0.502)', 'cssColor keeps 50% alpha');
  assert(api.cssColor('nope') === 'rgba(0,0,0,0)', 'garbage colours are transparent');
  assert(api.isVisible('#FFFFFFFF') === true, 'opaque colours are visible');
  assert(api.isVisible('#FFFFFF00') === false, 'fully transparent colours are skipped');
  section('Colour plumbing');
}

//---------------------------------------------------------------------------
function testExplicitPassageCells() {
  calls.length = 0;
  api.drawFrame({ w: 1000, h: 620, bg: '#000000FF', bg2: '#000000FF', cmds: [
    [4, 40, 100, 30, '#FFFFFFFF', 0, false, true, 'á😀b', 18.6],
    [4, 95.8, 100, 30, '#22C55EFF', 0, false, true, 'c', 18.6],
  ] });
  const glyphs = calls.filter(c => c.kind === 'fillText');
  assert(glyphs.length === 4, 'one cell per Unicode code point, including supplementary text');
  for (let i = 0; i < glyphs.length; i++) {
    assert(Math.abs(glyphs[i].arg[1] - (40 + i * 18.6)) < 0.01, 'adjacent runs share explicit cell grid');
    assert(glyphs[i].arg[3] === 18.6, 'font fallback cannot exceed cell width');
  }
}

function testKeyMapping() {
  assert(api.virtualKeyFor({ key: 'ArrowLeft' }) === 0x25, 'arrow keys map to VK codes');
  assert(api.virtualKeyFor({ key: ' ' }) === 0x20, 'space maps to VK_SPACE');
  assert(api.virtualKeyFor({ key: 'a' }) === 65, 'letters map to their uppercase VK');
  assert(api.virtualKeyFor({ key: 'A' }) === 65, 'shifted letters map to the same VK');
  assert(api.virtualKeyFor({ key: 'F2' }) === 0x71, 'F2 maps to VK_F2');
  assert(api.virtualKeyFor({ key: 'Shift' }) === 0, 'modifiers are not forwarded');
  requests.length = 0;
  let prevented = false;
  const event = { key: 'a', target: elements.bpm, preventDefault: () => { prevented = true; } };
  keyHandlers.keydown(event);
  keyHandlers.keyup(event);
  assert(!prevented && !requests.includes('api/input'), 'settings controls never send game keys');
  event.target = elements.screen;
  event.isComposing = true;
  keyHandlers.keydown(event);
  assert(!requests.includes('api/input'), 'IME composition is not forwarded as a physical game key');
  event.isComposing = false;
  keyHandlers.keydown(event);
  assert(prevented && requests.includes('api/input'), 'focused canvas receives game keys');
  section('Keyboard mapping (browser -> VK)');
}

//---------------------------------------------------------------------------
function drawOne(slug) {
  calls.length = 0;
  const frame = fixtures.games[slug].frame;
  api.drawFrame(frame);
  return { frame, drawCalls: calls.slice() };
}

function testEveryGameDraws() {
  let drawnAll = true;
  for (const slug of Object.keys(fixtures.games)) {
    const fixture = fixtures.games[slug];
    const { frame, drawCalls } = drawOne(slug);
    assert(frame.cmds.length === fixture.cmds, slug + ': fixture matches its command count');
    assert(drawCalls.find((c) => c.kind === 'setTransform') !== undefined,
           slug + ': the canvas transform is set (scaling to the CSS size)');
    const transform = drawCalls.find((c) => c.kind === 'setTransform');
    assert(transform && Math.abs(transform.arg[0] - 1280 / frame.w) < 1e-9,
           slug + ': scale = canvas width / frame width');
    assert(drawCalls.find((c) => c.kind === 'clearRect') !== undefined,
           slug + ': the frame is cleared before drawing');
    // Every command kind maps onto draw calls: rects/circles/lines/polys/text.
    const shapeCalls = drawCalls.filter((c) => ['fillRect', 'arc', 'fill', 'stroke', 'fillText']
      .includes(c.kind)).length;
    assert(shapeCalls > 0, slug + ': something is actually painted');
    // Wire text is index 8; index 9 is the new optional cell advance.
    // Compare the entire ordered draw stream, not membership (or undefined
    // fields, which used to let this assertion pass without checking text).
    const texts = frame.cmds.filter(c => c[0] === 4).flatMap(c =>
      c[9] > 0 ? Array.from(c[8]) : [c[8]]);
    const drawnTexts = drawCalls.filter(c => c.kind === 'fillText').map(c => c.arg[0]);
    if (JSON.stringify(texts) !== JSON.stringify(drawnTexts)) {
      drawnAll = false;
      console.error('    text stream mismatch in ' + slug);
    }
  }
  assert(drawnAll, 'every TEXT command of every game reaches fillText');
  section('All 8 games render from real engine frames');
}

//---------------------------------------------------------------------------
function testGradientAndBackground() {
  // A frame with two background colours must use a vertical gradient.
  const gradientFrame = Object.values(fixtures.games).map((g) => g.frame)
    .find((f) => f.bg !== f.bg2);
  assert(!!gradientFrame, 'at least one game uses a two-colour background');
  calls.length = 0;
  api.drawFrame(gradientFrame);
  const gradient = calls.find((c) => c.kind === 'createLinearGradient');
  assert(!!gradient, 'background gradient is created when bg != bg2');
  assert(calls.filter((c) => c.kind === 'addColorStop').length === 2,
         'the gradient has a stop for each background colour');

  // A single-colour frame must not create one.
  const flatFrame = Object.values(fixtures.games).map((g) => g.frame).find((f) => f.bg === f.bg2);
  if (flatFrame) {
    calls.length = 0;
    api.drawFrame(flatFrame);
    assert(!calls.find((c) => c.kind === 'createLinearGradient'),
           'no gradient is created for a flat background');
  }
  section('Background / gradient path');
}

//---------------------------------------------------------------------------
function testHudAndBanner() {
  const state = {
    slug: 'snake',
    frame: Object.assign({}, fixtures.games.snake.frame, {
      banner: 'Game Over', status: 'Nhấn F2 để chơi lại',
      stats: fixtures.games.snake.frame.stats,
    }),
  };
  api.onState(state);
  assert(elements.banner.textContent === 'Game Over', 'the banner text reaches the DOM');
  assert(!elements.banner.classList.contains('hidden'), 'the banner is shown while it has text');
  assert(elements.title.textContent === state.frame.title, 'the title is shown in the HUD');
  assert(elements.subtitle.textContent === 'Nhấn F2 để chơi lại', 'the status line is shown');

  delete state.frame.banner;
  state.frame = Object.assign({}, state.frame, { banner: '' });
  api.onState(state);
  assert(elements.banner.classList.contains('hidden'), 'an empty banner is hidden again');
  section('HUD / banner updates');
}

//---------------------------------------------------------------------------
function testLabsHook() {
  // arcade.js must hand frames to the labs when they are present...
  const seen = [];
  sandbox.window.KieeKeyLabs = { onState: (state) => seen.push(state.slug) };
  api.onState({ slug: 'flexing', frame: fixtures.games.flexing.frame });
  assert(seen.length === 1 && seen[0] === 'flexing', 'the labs receive streamed frames');
  // ...and survive their absence (the module is loaded after the player).
  sandbox.window.KieeKeyLabs = undefined;
  api.onState({ slug: 'snake', frame: fixtures.games.snake.frame });
  assert(true, 'frames without the labs loaded do not throw');
  section('Labs integration point');
}

//---------------------------------------------------------------------------
console.log('=== Running Web Render (headless canvas) Suite ===');
console.log('    fixtures captured from the C++ engine: ' + fixtures.captured_at);
testColorHelpers();
testKeyMapping();
testExplicitPassageCells();
testEveryGameDraws();
testGradientAndBackground();
testHudAndBanner();
testLabsHook();
if (failures === 0) {
  console.log('=== ALL WEB RENDER TESTS PASSED (' + checks + ' checks, ' +
              calls.length + ' canvas calls in the last frame) ===');
  process.exit(0);
}
console.error('=== WEB RENDER TESTS FAILED (' + failures + '/' + checks + ') ===');
process.exit(1);
