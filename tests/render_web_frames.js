//============================================================================
// KieeKey — tests/render_web_frames.js
// Rasterize every game frame captured from the real C++ engine
// (tests/data/web_frames.json) through the REAL web renderer (web/arcade.js)
// into PNG files, using a real Canvas2D implementation (@napi-rs/canvas, the
// same API surface the browser exposes — Skia under the hood).
//
// WHY: the sandbox has no browser (the Playwright/Chromium download is blocked
// and no system browser exists), so the pixel evidence for the HTML5 player is
// produced here instead: the drawing code under test is the shipped
// web/arcade.js, the frames come from `arcade_serve`, and the rasterizer is a
// standards-shaped Canvas2D. What this proves: colours, geometry, text, scaling
// and per-game composition are real pixels. What it does NOT prove: browser CSS
// layout / fonts / the surrounding page chrome — that still needs a real
// browser, and the README says so.
//
//   node tests/render_web_frames.js [out-dir]
//
// Requires the optional dependency:
//   npm install --no-save @napi-rs/canvas        (or set NAPI_CANVAS_PATH)
// Exits 0 with a clear notice when the module is absent, so the native test
// suite can call it unconditionally.
//============================================================================
'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.resolve(__dirname, '..');
const OUT = path.resolve(process.argv[2] || path.join(ROOT, 'docs/bench/arcade-130/frames'));
const fixtures = JSON.parse(fs.readFileSync(path.join(ROOT, 'tests/data/web_frames.json'), 'utf8'));

//---------------------------------------------------------------------------
// Optional dependency lookup (repo-local, env override, or a scratch install)
//---------------------------------------------------------------------------
function loadCanvas() {
  const candidates = [
    process.env.NAPI_CANVAS_PATH,
    path.join(ROOT, 'node_modules/@napi-rs/canvas'),
    '/tmp/pw/node_modules/@napi-rs/canvas',
    '@napi-rs/canvas',
  ].filter(Boolean);
  for (const candidate of candidates) {
    try { return require(candidate); } catch (err) { /* try the next one */ }
  }
  return null;
}

const canvasLib = loadCanvas();
if (!canvasLib) {
  console.log('[render_web_frames] SKIPPED — @napi-rs/canvas is not installed.');
  console.log('                     npm install --no-save @napi-rs/canvas');
  process.exit(0);
}

const { createCanvas } = canvasLib;

//---------------------------------------------------------------------------
// Canvas sized like the page's <canvas> (arcade.css: 1280x720, then scaled by
// devicePixelRatio; 1x keeps the PNGs small and the geometry identical).
//---------------------------------------------------------------------------
const WIDTH = 1280;
const HEIGHT = 720;
// One canvas per game so the contact sheet can composite the very same bitmaps
// (drawImage of a Canvas works everywhere; re-decoding our own PNGs does not).
function newCanvas() {
  const c = createCanvas(WIDTH, HEIGHT);
  return { canvas: c, ctx: c.getContext('2d') };
}
const first = newCanvas();
let activeCtx = first.ctx;

// arcade.js grabs the canvas + context once, at load time. To render several
// games into their own bitmaps we hand it a forwarding context: every call and
// property goes to whichever real context is active for the current game.
const ctx = new Proxy({}, {
  get(_target, prop) {
    const value = activeCtx[prop];
    return (typeof value === 'function') ? value.bind(activeCtx) : value;
  },
  set(_target, prop, value) {
    activeCtx[prop] = value;
    return true;
  },
});

//---------------------------------------------------------------------------
// Minimal DOM double — identical in shape to tests/web_render_test.js, except
// that the canvas context is the real rasterizer instead of a recorder.
//---------------------------------------------------------------------------
function makeElement(id) {
  const listeners = {};
  const classes = new Set();
  return {
    id, value: '', textContent: '', className: '', width: WIDTH, height: HEIGHT, tabIndex: 0,
    dataset: {}, classList: {
      add: (n) => classes.add(n), remove: (n) => classes.delete(n),
      contains: (n) => classes.has(n),
      toggle: (n, f) => { if (f) { classes.add(n); } else { classes.delete(n); } },
    },
    addEventListener: (t, h) => { (listeners[t] = listeners[t] || []).push(h); },
    append: () => {}, appendChild: () => {}, focus: () => {},
    querySelectorAll: () => [],
    getBoundingClientRect: () => ({ left: 0, top: 0, width: WIDTH, height: HEIGHT }),
    closest: () => null,
    handlers: listeners,
  };
}

const ids = ['screen', 'title', 'score', 'best', 'level', 'combo', 'lives', 'wpm', 'acc',
             'banner', 'toast', 'connection', 'fps', 'catalog', 'subtitle', 'failMode',
             'bpm', 'bpmOut', 'pacer', 'pacerOut', 'steering', 'restart', 'pause', 'stop'];
const elements = {};
for (const id of ids) { elements[id] = makeElement(id); }
elements.screen.getContext = () => ctx;

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
  window: { addEventListener: () => {}, KieeKeyLabs: undefined },
  fetch: async () => ({ ok: true, status: 200, json: async () => ({ ok: true, games: [] }) }),
  EventSource: function EventSource() {},
};
sandbox.globalThis = sandbox;
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync(path.join(ROOT, 'web/arcade.js'), 'utf8'), sandbox,
                { filename: 'web/arcade.js' });

const drawFrame = sandbox.drawFrame;
const onState = sandbox.onState;
if (typeof drawFrame !== 'function') {
  console.error('[render_web_frames] FAIL — web/arcade.js did not expose drawFrame()');
  process.exit(1);
}

//---------------------------------------------------------------------------
// Render
//---------------------------------------------------------------------------
fs.mkdirSync(OUT, { recursive: true });

const written = [];
for (const slug of Object.keys(fixtures.games)) {
  const fixture = fixtures.games[slug];
  const frame = fixture.frame;
  const surface = newCanvas();
  activeCtx = surface.ctx;
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, WIDTH, HEIGHT);
  // The HUD/banner/toast come from onState(), exactly like the live page.
  onState({ slug, frame });
  drawFrame(frame);
  const file = path.join(OUT, slug + '.png');
  fs.writeFileSync(file, surface.canvas.toBuffer('image/png'));
  written.push({ slug, file, bytes: fs.statSync(file).size, cmds: frame.cmds.length,
                 title: (frame.title || ''), canvas: surface.canvas });
  console.log('  [ok] ' + slug.padEnd(12) + frame.cmds.length.toString().padStart(4) +
              ' draw commands -> ' + path.relative(ROOT, file) +
              ' (' + Math.round(fs.statSync(file).size / 1024) + ' KB)');
}

//---------------------------------------------------------------------------
// Contact sheet: all games in one image, so a human can look at the whole
// feature set at once (this is the file the report/README link to).
//---------------------------------------------------------------------------
const COLS = 2;
const ROWS = Math.ceil(written.length / COLS);
const THUMB_W = 640;
const THUMB_H = 360;
const sheet = createCanvas(THUMB_W * COLS, THUMB_H * ROWS + 40);
const sheetCtx = sheet.getContext('2d');
sheetCtx.fillStyle = '#0B0F17';
sheetCtx.fillRect(0, 0, sheet.width, sheet.height);
sheetCtx.fillStyle = '#F2F5FA';
sheetCtx.font = 'bold 22px DejaVu Sans, sans-serif';
sheetCtx.fillText('KieeKey Arcade Hub — frames captured from the C++ engine, drawn by web/arcade.js',
                  16, 28);

written.forEach((entry, index) => {
  const col = index % COLS;
  const row = Math.floor(index / COLS);
  const x = col * THUMB_W;
  const y = row * THUMB_H + 40;
  sheetCtx.drawImage(entry.canvas, x + 4, y + 4, THUMB_W - 8, THUMB_H - 8);
  sheetCtx.fillStyle = 'rgba(0,0,0,0.65)';
  sheetCtx.fillRect(x + 4, y + THUMB_H - 34, THUMB_W - 8, 26);
  sheetCtx.fillStyle = '#FFFFFF';
  sheetCtx.font = 'bold 16px DejaVu Sans, sans-serif';
  sheetCtx.fillText(entry.slug + ' — ' + entry.title, x + 12, y + THUMB_H - 16);
});

const sheetFile = path.join(OUT, 'contact-sheet.png');
fs.writeFileSync(sheetFile, sheet.toBuffer('image/png'));
console.log('  [ok] contact sheet -> ' + path.relative(ROOT, sheetFile) +
            ' (' + Math.round(fs.statSync(sheetFile).size / 1024) + ' KB)');

// A machine-readable record next to the images, so the report can be regenerated.
fs.writeFileSync(path.join(OUT, 'manifest.json'), JSON.stringify({
  renderer: 'web/arcade.js',
  rasterizer: 'Canvas2D via @napi-rs/canvas',
  source: 'tests/data/web_frames.json (arcade_serve, real engine)',
  note: fixtures.note,
  canvas: { width: WIDTH, height: HEIGHT },
  games: written.map(({ slug, file, bytes, cmds, title }) => ({
    slug, file: path.relative(ROOT, file), bytes, cmds, title })),
}, null, 2) + '\n');

console.log('=== rendered ' + written.length + ' game frames + a contact sheet ===');
