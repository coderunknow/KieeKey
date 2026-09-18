//============================================================================
// KieeKey — web/labs.js
// The two "gõ thật" (type-for-real) test surfaces of the web player:
//
//   * Flexing Mode stage — you press any key, and the text that appears is the
//     pre-prepared passage produced by the C++ engine, typed into a real
//     editable control (`/api/preload` + `flex.emitted` from `/api/stream`).
//   * Chaos Mode lab — the same ChaosEngine the Win32 hook uses, showing the
//     exact characters that would be injected into the focused application
//     ("engine gõ thật") next to the display-only glyph rotation.
//
// No game or transformation logic lives here: this file only moves text
// between the engine and the DOM, so the browser cannot drift from the real
// typing path.
//============================================================================
'use strict';

(function () {
  const $ = (id) => document.getElementById(id);

  const flex = {
    panel: $('labFlexing'),
    source: $('flexSource'),
    target: $('flexTarget'),
    gran: $('flexGran'),
    n: $('flexN'),
    pill: $('flexPill'),
    wpm: $('flexWpm'),
    gen: $('flexGen'),
    keys: $('flexKeys'),
    eff: $('flexEff'),
    cursor: $('flexCursor'),
  };

  const chaos = {
    panel: $('labChaos'),
    master: $('chaosMaster'),
    caseOn: $('chaosCase'),
    caseIntensity: $('chaosCaseIntensity'),
    caseOut: $('chaosCaseOut'),
    caseGran: $('chaosCaseGran'),
    glyphOn: $('chaosGlyph'),
    glyphMode: $('chaosGlyphMode'),
    glyphIntensity: $('chaosGlyphIntensity'),
    glyphOut: $('chaosGlyphOut'),
    source: $('chaosSource'),
    injected: $('chaosInjected'),
    display: $('chaosDisplay'),
    pill: $('chaosPill'),
    honesty: $('chaosHonesty'),
  };

  const DEFAULT_FLEX_TEXT =
    'Xin chao! Day la van ban duoc chuan bi truoc, va no tu dong hien ra ' +
    'khi ban go phim bat ky.\nKieeKey go thay ban, con ban chi viec go vui.';

  let flexTextLoaded = false;
  let lastEmitted = '';
  let lastSlug = '';

  //--------------------------------------------------------------------------
  // Transport
  //--------------------------------------------------------------------------
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
  // Panel visibility
  //--------------------------------------------------------------------------
  function show(el, visible) {
    if (el) { el.classList.toggle('hidden', !visible); }
  }

  function openFlexingLab(loadDefaults = true) {
    show(flex.panel, true);
    if (!flexTextLoaded) {
      flex.source.value = DEFAULT_FLEX_TEXT;
      flexTextLoaded = true;
      if (loadDefaults) { void preload(); }
    }
    if (flex.target) { flex.target.focus(); }
  }

  function openChaosLab() {
    show(chaos.panel, true);
    void pushChaos();
  }

  function closeLabs() {
    show(flex.panel, false);
    show(chaos.panel, false);
  }

  //--------------------------------------------------------------------------
  // Flexing Mode
  //--------------------------------------------------------------------------
  async function preload() {
    if (!flex.source) { return; }
    const body = {
      text: flex.source.value,
      granularity: parseInt(flex.gran.value, 10) || 0,
      nChars: parseInt(flex.n.value, 10) || 3,
    };
    const result = await post('api/preload', body);
    if (result && result.ok) {
      flex.pill.textContent = result.total + ' ký tự đã nạp';
      flex.pill.className = 'pill pill-ok';
      if (flex.target) { flex.target.value = ''; }
    } else {
      flex.pill.textContent = 'cần mở game Flexing trước';
      flex.pill.className = 'pill pill-warn';
      toast('Hãy chọn game 🗿 Flexing Mode ở cột phải trước');
    }
    return result;
  }

  // Called from arcade.js for every streamed state frame.
  function applyFlexState(state) {
    const info = state && state.flex;
    if (!info || !flex.panel || flex.panel.classList.contains('hidden')) { return; }
    // Only the Flexing run may drive this control, whoever sent the frame.
    if (state.slug && state.slug !== 'flexing') { return; }

    if (info.emitted && info.emitted !== '') {
      lastEmitted += info.emitted;
      if (flex.target) {
        // "Gõ thật": the characters really land in the editable control, and
        // the caret follows them like in any other text field.
        flex.target.value += info.emitted;
        flex.target.scrollTop = flex.target.scrollHeight;
      }
    }
    if (flex.wpm) { flex.wpm.textContent = info.wpm; }
    if (flex.gen) { flex.gen.textContent = info.generated; }
    if (flex.keys) { flex.keys.textContent = info.keys; }
    if (flex.eff) { flex.eff.textContent = 'x' + (info.efficiency || 1).toFixed(2); }
    if (flex.cursor) { flex.cursor.textContent = info.cursor + ' / ' + info.total; }
    flex.pill.textContent = info.cursor + ' / ' + info.total + ' ký tự';
    flex.pill.className = info.cursor >= info.total ? 'pill pill-ok' : 'pill';
  }

  //--------------------------------------------------------------------------
  // Chaos Mode
  //--------------------------------------------------------------------------
  function chaosBody() {
    return {
      enabled: chaos.master.checked,
      randomCase: chaos.caseOn.checked,
      caseIntensityPercent: parseInt(chaos.caseIntensity.value, 10),
      caseGranularity: parseInt(chaos.caseGran.value, 10),
      glyph: chaos.glyphOn.checked,
      glyphMode: parseInt(chaos.glyphMode.value, 10),
      glyphIntensityPercent: parseInt(chaos.glyphIntensity.value, 10),
    };
  }

  async function pushChaos() {
    if (!chaos.master) { return; }
    const result = await post('api/chaos', chaosBody());
    if (chaos.caseOut) { chaos.caseOut.textContent = chaos.caseIntensity.value; }
    if (chaos.glyphOut) { chaos.glyphOut.textContent = chaos.glyphIntensity.value; }
    if (chaos.pill) {
      chaos.pill.textContent = result && result.active ? 'ĐANG BẬT' : 'TẮT';
      chaos.pill.className = 'pill ' + (result && result.active ? 'pill-ok' : 'pill');
    }
    await previewChaos();
  }

  async function previewChaos() {
    if (!chaos.source) { return; }
    const result = await post('api/chaos/preview', { text: chaos.source.value });
    if (!result || !result.preview) { return; }
    chaos.injected.value = result.preview.injected;
    chaos.display.value = result.preview.display;
    if (chaos.honesty) {
      chaos.honesty.textContent = result.renderOnlyMode
        ? 'Đang chọn xoay 90°/270°: engine KHÔNG gõ ký tự lạ vào tài liệu, nó chỉ xoay phần vẽ — chữ gõ thật giữ nguyên.'
        : 'Chữ ở ô “Engine gõ thật” chính là chuỗi mà hook sẽ đưa vào ứng dụng đang mở.';
    }
    return result;
  }

  // "Gõ thử vào cửa sổ": replay the engine's own output into an editable
  // control, one character at a time, exactly like real typing.
  let replayTimer = null;
  async function typeForReal() {
    const result = await previewChaos();
    if (!result || !result.preview) { return; }
    const text = result.preview.injected;
    if (chaos.injected) { chaos.injected.value = ''; }
    clearInterval(replayTimer);
    let index = 0;
    replayTimer = setInterval(() => {
      if (index >= text.length) {
        clearInterval(replayTimer);
        return;
      }
      chaos.injected.value += text[index];
      chaos.injected.scrollTop = chaos.injected.scrollHeight;
      index++;
    }, 40);
  }

  //--------------------------------------------------------------------------
  // Wiring
  //--------------------------------------------------------------------------
  window.KieeKeyLabs = {
    isNativeInput: (target) =>
      !!(target && target.closest && target.closest('.lab-input')),
    onState: (state) => {
      const slug = state && state.slug;
      // Open once on a game transition, BEFORE consuming the first frame.
      // Streaming frames must never steal focus from an editor or reopen a
      // dismissed panel. Do not overwrite an already-running server passage.
      if (slug === 'flexing' && lastSlug !== 'flexing') { openFlexingLab(false); }
      lastSlug = slug || '';
      applyFlexState(state);
    },
    openFlexingLab,
    openChaosLab,
    closeLabs,
  };

  const openFlex = $('openFlexingLab');
  const openChaos = $('openChaosLab');
  const closeBtn = $('closeLabs');
  if (openFlex) { openFlex.addEventListener('click', () => openFlexingLab()); }
  if (openChaos) { openChaos.addEventListener('click', openChaosLab); }
  if (closeBtn) { closeBtn.addEventListener('click', closeLabs); }

  const preloadBtn = $('flexPreload');
  if (preloadBtn) { preloadBtn.addEventListener('click', () => void preload()); }
  const clearBtn = $('flexClear');
  if (clearBtn) { clearBtn.addEventListener('click', () => { if (flex.target) { flex.target.value = ''; } }); }
  if (flex.gran) { flex.gran.addEventListener('change', () => void preload()); }
  if (flex.n) { flex.n.addEventListener('change', () => void preload()); }
  const startBtn = $('flexStart');
  if (startBtn) { startBtn.addEventListener('click', () => void post('api/start', { slug: 'flexing' })); }

  const chaosApply = $('chaosApply');
  if (chaosApply) { chaosApply.addEventListener('click', () => void typeForReal()); }
  const chaosClear = $('chaosClear');
  if (chaosClear) {
    chaosClear.addEventListener('click', () => {
      chaos.injected.value = '';
      chaos.display.value = '';
    });
  }
  for (const el of [chaos.master, chaos.caseOn, chaos.caseGran, chaos.glyphOn, chaos.glyphMode]) {
    if (el) { el.addEventListener('change', () => void pushChaos()); }
  }
  for (const el of [chaos.caseIntensity, chaos.glyphIntensity]) {
    if (el) {
      el.addEventListener('input', () => {
        if (chaos.caseOut) { chaos.caseOut.textContent = chaos.caseIntensity.value; }
        if (chaos.glyphOut) { chaos.glyphOut.textContent = chaos.glyphIntensity.value; }
      });
      el.addEventListener('change', () => void pushChaos());
    }
  }
  if (chaos.source) { chaos.source.addEventListener('change', () => void previewChaos()); }

  void pushChaos();
})();
