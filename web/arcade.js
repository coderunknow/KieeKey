//============================================================================
// KieeKey — web/arcade.js
// HTML5 canvas front-end for the Arcade Hub.
//
// The client owns NO game logic: every frame comes from the C++ engine
// (`ArcadeManager` -> `Frame` -> `RenderList` -> JSON) and every key is sent
// back into `ArcadeManager::handleKey`. Drawing here is a dumb interpreter of
// the command list, which is what keeps the Win32 GDI front-end and the web
// front-end pixel-consistent.
//============================================================================
'use strict';

const canvas = document.getElementById('screen');
const ctx = canvas.getContext('2d');

const ui = {
  title: document.getElementById('title'),
  score: document.getElementById('score'),
  best: document.getElementById('best'),
  level: document.getElementById('level'),
  combo: document.getElementById('combo'),
  lives: document.getElementById('lives'),
  wpm: document.getElementById('wpm'),
  acc: document.getElementById('acc'),
  banner: document.getElementById('banner'),
  toast: document.getElementById('toast'),
  connection: document.getElementById('connection'),
  fps: document.getElementById('fps'),
  catalog: document.getElementById('catalog'),
  subtitle: document.getElementById('subtitle'),
  failMode: document.getElementById('failMode'),
  bpm: document.getElementById('bpm'),
  bpmOut: document.getElementById('bpmOut'),
  pacer: document.getElementById('pacer'),
  pacerOut: document.getElementById('pacerOut'),
};

const KIND = { RECT: 0, CIRCLE: 1, LINE: 2, POLY: 3, TEXT: 4 };

let lastFrame = null;
let activeSlug = '';
let framesDrawn = 0;
let fpsWindowStart = performance.now();
let eventSource = null;
let pollTimer = null;
let pollErrors = 0;

//---------------------------------------------------------------------------
// Colors: the wire format is "#RRGGBBAA".
//---------------------------------------------------------------------------
function cssColor(hex) {
  if (typeof hex !== 'string' || hex.length !== 9 || hex[0] !== '#') {
    return 'rgba(0,0,0,0)';
  }
  const r = parseInt(hex.substr(1, 2), 16);
  const g = parseInt(hex.substr(3, 2), 16);
  const b = parseInt(hex.substr(5, 2), 16);
  const a = parseInt(hex.substr(7, 2), 16) / 255;
  return `rgba(${r},${g},${b},${a.toFixed(3)})`;
}

function isVisible(hex) {
  return typeof hex === 'string' && hex.length === 9 && hex.substr(7, 2) !== '00';
}

//---------------------------------------------------------------------------
// Renderer
//---------------------------------------------------------------------------
function roundedRect(x, y, w, h, r) {
  const radius = Math.max(0, Math.min(r, Math.min(w, h) / 2));
  ctx.beginPath();
  if (radius <= 0.01) {
    ctx.rect(x, y, w, h);
    return;
  }
  ctx.moveTo(x + radius, y);
  ctx.lineTo(x + w - radius, y);
  ctx.quadraticCurveTo(x + w, y, x + w, y + radius);
  ctx.lineTo(x + w, y + h - radius);
  ctx.quadraticCurveTo(x + w, y + h, x + w - radius, y + h);
  ctx.lineTo(x + radius, y + h);
  ctx.quadraticCurveTo(x, y + h, x, y + h - radius);
  ctx.lineTo(x, y + radius);
  ctx.quadraticCurveTo(x, y, x + radius, y);
  ctx.closePath();
}

function drawFrame(frame) {
  const scale = canvas.width / frame.w;
  ctx.setTransform(scale, 0, 0, scale, 0, 0);
  ctx.clearRect(0, 0, frame.w, frame.h);

  if (frame.bg === frame.bg2) {
    ctx.fillStyle = cssColor(frame.bg);
    ctx.fillRect(0, 0, frame.w, frame.h);
  } else {
    const gradient = ctx.createLinearGradient(0, 0, 0, frame.h);
    gradient.addColorStop(0, cssColor(frame.bg2));
    gradient.addColorStop(1, cssColor(frame.bg));
    ctx.fillStyle = gradient;
    ctx.fillRect(0, 0, frame.w, frame.h);
  }

  ctx.textBaseline = 'middle';
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';

  for (const cmd of frame.cmds) {
    switch (cmd[0]) {
      case KIND.RECT: {
        const [, x, y, w, h, radius, fill, stroke, strokeWidth] = cmd;
        roundedRect(x, y, w, h, radius);
        if (isVisible(fill)) { ctx.fillStyle = cssColor(fill); ctx.fill(); }
        if (isVisible(stroke) && strokeWidth > 0) {
          ctx.strokeStyle = cssColor(stroke);
          ctx.lineWidth = strokeWidth;
          ctx.stroke();
        }
        break;
      }
      case KIND.CIRCLE: {
        const [, cx, cy, r, fill, stroke, strokeWidth] = cmd;
        ctx.beginPath();
        ctx.arc(cx, cy, Math.max(0, r), 0, Math.PI * 2);
        if (isVisible(fill)) { ctx.fillStyle = cssColor(fill); ctx.fill(); }
        if (isVisible(stroke) && strokeWidth > 0) {
          ctx.strokeStyle = cssColor(stroke);
          ctx.lineWidth = strokeWidth;
          ctx.stroke();
        }
        break;
      }
      case KIND.LINE: {
        const [, x1, y1, x2, y2, color, width] = cmd;
        ctx.beginPath();
        ctx.moveTo(x1, y1);
        ctx.lineTo(x2, y2);
        ctx.strokeStyle = cssColor(color);
        ctx.lineWidth = Math.max(0.5, width);
        ctx.stroke();
        break;
      }
      case KIND.POLY: {
        const count = cmd[1];
        const pts = cmd.slice(2, 2 + count * 2);
        const fill = cmd[2 + count * 2];
        const stroke = cmd[3 + count * 2];
        const strokeWidth = cmd[4 + count * 2];
        if (count < 2) { break; }
        ctx.beginPath();
        ctx.moveTo(pts[0], pts[1]);
        for (let i = 1; i < count; ++i) { ctx.lineTo(pts[i * 2], pts[i * 2 + 1]); }
        ctx.closePath();
        if (isVisible(fill)) { ctx.fillStyle = cssColor(fill); ctx.fill(); }
        if (isVisible(stroke) && strokeWidth > 0) {
          ctx.strokeStyle = cssColor(stroke);
          ctx.lineWidth = strokeWidth;
          ctx.stroke();
        }
        break;
      }
      case KIND.TEXT: {
        const [, x, y, size, color, align, bold, mono, text] = cmd;
        if (!text) { break; }
        const weight = bold ? '700' : '400';
        const family = mono
          ? '"Cascadia Mono","Consolas","DejaVu Sans Mono",monospace'
          : '"Segoe UI",Inter,system-ui,"Noto Sans",sans-serif';
        ctx.font = `${weight} ${size}px ${family}`;
        ctx.textAlign = align === 1 ? 'center' : (align === 2 ? 'right' : 'left');
        ctx.fillStyle = cssColor(color);
        ctx.fillText(text, x, y);
        break;
      }
      default:
        break;
    }
  }
}

//---------------------------------------------------------------------------
// HUD
//---------------------------------------------------------------------------
function updateHud(state) {
  const s = state.frame.stats;
  ui.title.textContent = state.frame.title || 'Arcade Hub';
  ui.score.textContent = s.score.toLocaleString('vi-VN');
  ui.best.textContent = s.hasBest ? s.best.toLocaleString('vi-VN') : '—';
  ui.level.textContent = s.level;
  ui.combo.textContent = s.combo + (s.maxCombo ? ` (max ${s.maxCombo})` : '');
  ui.lives.textContent = s.lives > 0 ? s.lives : '—';
  ui.wpm.textContent = s.wpm ? s.wpm.toFixed(1) : '0';
  ui.acc.textContent = s.acc.toFixed(1) + '%';

  const bannerText = state.frame.banner || '';
  if (bannerText) {
    ui.banner.textContent = bannerText;
    ui.banner.classList.remove('hidden');
  } else {
    ui.banner.classList.add('hidden');
  }

  ui.subtitle.textContent = state.frame.status || state.frame.hint ||
    'Cùng một engine C++ với bản Win32 — vẽ bằng HTML5 Canvas';

  if (state.slug !== activeSlug) {
    activeSlug = state.slug;
    for (const btn of ui.catalog.querySelectorAll('.game')) {
      btn.classList.toggle('active', btn.dataset.slug === activeSlug);
    }
  }
}

//---------------------------------------------------------------------------
// Transport: SSE with a polling fallback (some proxies buffer chunked bodies)
//---------------------------------------------------------------------------
function setConnection(cls, text) {
  ui.connection.className = 'pill ' + cls;
  ui.connection.textContent = text;
}

function onState(state) {
  lastFrame = state.frame;
  updateHud(state);
  if (window.KieeKeyLabs) { window.KieeKeyLabs.onState(state); }
  drawFrame(state.frame);
  framesDrawn++;
  const now = performance.now();
  if (now - fpsWindowStart >= 500) {
    const fps = (framesDrawn * 1000) / (now - fpsWindowStart);
    ui.fps.textContent = fps.toFixed(0) + ' fps';
    framesDrawn = 0;
    fpsWindowStart = now;
  }
}

function startPolling() {
  if (pollTimer !== null) { return; }
  setConnection('pill-warn', 'đang dùng polling');
  pollTimer = setInterval(async () => {
    try {
      const response = await fetch('api/state', { cache: 'no-store' });
      if (!response.ok) { throw new Error('http ' + response.status); }
      onState(await response.json());
      pollErrors = 0;
    } catch (err) {
      pollErrors++;
      if (pollErrors > 5) { setConnection('pill-bad', 'mất kết nối'); }
    }
  }, 33);
}

function startStream() {
  if (typeof EventSource === 'undefined') { startPolling(); return; }
  eventSource = new EventSource('api/stream');
  let received = 0;
  eventSource.onopen = () => { setConnection('pill-ok', 'đã kết nối'); };
  eventSource.onmessage = (event) => {
    received++;
    if (received === 1) { setConnection('pill-ok', 'đã kết nối'); }
    try {
      onState(JSON.parse(event.data));
    } catch (err) {
      /* ignore a truncated frame; the next one is 16 ms away */
    }
  };
  eventSource.onerror = () => {
    // EventSource retries by itself; if we never received a frame, the proxy
    // is probably buffering the stream, so fall back to polling.
    if (received === 0) {
      eventSource.close();
      eventSource = null;
      startPolling();
    } else {
      setConnection('pill-warn', 'đang kết nối lại…');
    }
  };
}

//---------------------------------------------------------------------------
// Input: browser key -> Win32 virtual key + produced character
//---------------------------------------------------------------------------
const VK = {
  Backspace: 0x08, Tab: 0x09, Enter: 0x0D, Escape: 0x1B, Space: 0x20,
  ArrowLeft: 0x25, ArrowUp: 0x26, ArrowRight: 0x27, ArrowDown: 0x28,
  Pause: 0x13, F1: 0x70, F2: 0x71,
};

function virtualKeyFor(event) {
  if (Object.prototype.hasOwnProperty.call(VK, event.key)) { return VK[event.key]; }
  if (event.key.length === 1) {
    const upper = event.key.toUpperCase();
    return upper.charCodeAt(0);
  }
  return 0;
}

const GAME_KEYS = new Set([
  'ArrowLeft', 'ArrowUp', 'ArrowRight', 'ArrowDown', ' ', 'Space',
  'a', 's', 'd', 'w', 'A', 'S', 'D', 'W',
  'p', 'P', 'r', 'R', 'F1', 'F2', 'Escape', 'Backspace', 'Enter', ';', 'j', 'k', 'l',
]);

async function sendInput(vk, ch, down) {
  try {
    await fetch('api/input', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ vk, ch, down }),
    });
  } catch (err) {
    /* a dropped key event must never break the UI */
  }
}

function isNativeLabInput(event) {
  return !!(window.KieeKeyLabs && window.KieeKeyLabs.isNativeInput(event.target));
}

window.addEventListener('keydown', (event) => {
  if (event.ctrlKey || event.metaKey || event.altKey) { return; }
  // Editing the prepared passage / chaos sample is normal text editing: the
  // keystroke belongs to the textarea, not to the game engine.
  if (isNativeLabInput(event)) { return; }
  const vk = virtualKeyFor(event);
  if (vk === 0) { return; }
  if (event.key === 'Tab') { return; }
  if (activeSlug === 'flexing' && document.activeElement === document.getElementById('flexTarget')) {
    // Flexing Mode: the visible text comes from the engine, never from the OS
    // repeat of the physical key.
    event.preventDefault();
    sendInput(vk, event.key.length === 1 ? event.key : '', true);
    return;
  }
  if (GAME_KEYS.has(event.key) || event.key.length === 1) {
    // Stop the browser from scrolling / opening its own find bar while playing.
    event.preventDefault();
  }
  sendInput(vk, event.key.length === 1 ? event.key : '', true);
});

window.addEventListener('keyup', (event) => {
  if (isNativeLabInput(event)) { return; }
  const vk = virtualKeyFor(event);
  if (vk === 0) { return; }
  sendInput(vk, event.key.length === 1 ? event.key : '', false);
});

// Touch / click steering for the WASD games.
const TOUCH_MAP = { left: [0x25], right: [0x27], up: [0x26], down: [0x28] };
canvas.addEventListener('pointerdown', (event) => {
  const rect = canvas.getBoundingClientRect();
  const nx = (event.clientX - rect.left) / rect.width;
  const ny = (event.clientY - rect.top) / rect.height;
  const dy = ny - 0.5;
  const dx = nx - 0.5;
  let key = 'left';
  if (Math.abs(dy) > Math.abs(dx)) { key = dy < 0 ? 'up' : 'down'; } else { key = dx < 0 ? 'left' : 'right'; }
  for (const vk of TOUCH_MAP[key]) { sendInput(vk, '', true); }
});
canvas.addEventListener('pointerup', () => {
  for (const key of Object.keys(TOUCH_MAP)) {
    for (const vk of TOUCH_MAP[key]) { sendInput(vk, '', false); }
  }
});
canvas.addEventListener('contextmenu', (event) => event.preventDefault());
canvas.focus();

//---------------------------------------------------------------------------
// Catalog + config
//---------------------------------------------------------------------------
function showToast(text) {
  ui.toast.textContent = text;
  ui.toast.classList.remove('hidden');
  clearTimeout(showToast.timer);
  showToast.timer = setTimeout(() => ui.toast.classList.add('hidden'), 1600);
}

async function post(path, body) {
  const response = await fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body || {}),
  });
  return response.json();
}

async function loadCatalog() {
  try {
    const response = await fetch('api/catalog', { cache: 'no-store' });
    const data = await response.json();
    ui.catalog.innerHTML = '';
    for (const game of data.games) {
      const button = document.createElement('button');
      button.className = 'game';
      button.dataset.slug = game.slug;
      button.title = game.descVi + '\n' + game.controlsVi;
      const emoji = document.createElement('span');
      emoji.className = 'emoji';
      emoji.textContent = game.emoji || '🎮';
      const meta = document.createElement('span');
      meta.className = 'meta';
      const name = document.createElement('b');
      name.textContent = game.nameVi || game.nameEn || game.slug;
      const controls = document.createElement('small');
      controls.textContent = game.controlsVi || '';
      meta.append(name, controls);
      button.append(emoji, meta);
      button.addEventListener('click', async () => {
        const result = await post('api/start', { slug: game.slug });
        if (!result.ok) { showToast('Không mở được: ' + game.slug); }
        canvas.focus();
      });
      ui.catalog.append(button);
    }
  } catch (err) {
    showToast('Không tải được danh sách game');
  }
}

async function pushConfig() {
  ui.bpmOut.textContent = ui.bpm.value;
  ui.pacerOut.textContent = ui.pacer.value;
  await post('api/config', {
    rhythmFailMode: parseInt(ui.failMode.value, 10),
    noMistakeFailMode: parseInt(ui.failMode.value, 10),
    rhythmBpm: parseInt(ui.bpm.value, 10),
    typingRacePacerWpm: parseInt(ui.pacer.value, 10),
  });
}

for (const el of [ui.failMode, ui.bpm, ui.pacer]) {
  el.addEventListener('change', pushConfig);
  el.addEventListener('input', pushConfig);
}

document.getElementById('restart').addEventListener('click', () => post('api/restart'));
document.getElementById('pause').addEventListener('click', () => post('api/pause'));
document.getElementById('stop').addEventListener('click', () => post('api/stop'));

//---------------------------------------------------------------------------
// Boot
//---------------------------------------------------------------------------
loadCatalog();
pushConfig();
startStream();
// Keep a low-rate poll alive even while streaming: it refreshes the HUD if the
// stream stalls without firing onerror (some transparent proxies do that).
setInterval(async () => {
  if (eventSource && eventSource.readyState === 1) { return; }
  try {
    const response = await fetch('api/state', { cache: 'no-store' });
    if (response.ok) { onState(await response.json()); }
  } catch (err) { /* ignored */ }
}, 250);
