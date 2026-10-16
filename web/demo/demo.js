"use strict";
// MIN-1's demo video, the page's half (web/demo/record.mjs is the other). The emulator page as it is, but
// the FM-1 runs on the page's own thread a video frame at a time instead of in the audio worklet, so each
// frame of picture has exactly its 1/30 s of sound, however fast or slow this computer is. The storyboard
// (storyboard.js) presses the controls through the page's own input paths (the lights, the held keys and
// the knobs show as they do for a player); beside the FM-1 go its screen at twice the size, the captions,
// a ring where a finger is, and the chapters.
const DEMO = (() => {
  const SR = 44100, FPS = 30, CHUNK = 128;          // the worklet's quantum
  const ev = STORY.events;
  let ex = null, mem = null, done = 0, blits = -1, now = 0;
  let last = { t: 0, beats: 0 }, seen = {};          // the music's position after the last chunk, what it crossed
  const touches = [];                                 // rings: { el, at, up }
  const st = { cap: null, headAt: -9, detailAt: -9, chip: null, chipAt: -9, chipEnd: -9, card: null, cardAt: -9, cardEnd: -9, ended: false };
  const chStart = [];                                 // when each chapter began
  let zoom = null, ui = {};
  let probe = null;                                   // a probe: what EDIT would roll at each of the next chunks

  // ------------------------------------------------------------- the FM-1
  function cstr(p) {
    const b = new Uint8Array(mem.buffer, p, 64);
    let s = "";
    for (let i = 0; i < b.length && b[i]; i++) s += String.fromCharCode(b[i]);
    return s;
  }
  function input(m) {                                 // what the page sends the worklet, to the FM-1 here
    if (m.type === "buttons") ex.web_buttons(m.mask >>> 0);
    else if (m.type === "keys") ex.web_keys(m.mask >>> 0);
    else if (m.type === "enc") ex.web_enc(m.role, m.n | 0);
    else if (m.type === "master") ex.web_master(m.value | 0);
  }

  // ------------------------------------------------------------- the controls
  function control(name) {                            // SEL, W1..W16 (white keys), B1..B11 (black), knobs by name
    if (/^W\d+$/.test(name)) { const k = WHITE_K[+name.slice(1) - 1]; return { key: k, el: keys[k].hit }; }
    if (/^B\d+$/.test(name)) { const k = BLACK_K[+name.slice(1) - 1]; return { key: k, el: keys[k].hit }; }
    if (knobs[name]) return { knob: knobs[name], el: knobs[name].hit };
    if (!buttons[name]) throw new Error("no control " + name);
    return { btn: buttons[name].bit, el: buttons[name].hit };
  }
  const held = new Map();                             // control -> its ring
  function down(name) {
    const c = control(name), id = "demo:" + name;
    if (c.key !== undefined) holdDown(keyHeldBy, keyDownAt, id, c.key, syncKeys);
    else holdDown(btnHeldBy, btnDownAt, id, c.btn, syncButtons);
    const r = ring(c.el);
    held.set(name, r);
  }
  function up(name) {
    const c = control(name), id = "demo:" + name;
    if (c.key !== undefined) { keyHeldBy.delete(id); keyDownAt.delete(id); syncKeys(); }
    else { btnHeldBy.delete(id); btnDownAt.delete(id); syncButtons(); }
    const r = held.get(name);
    if (r) { r.up = now; held.delete(name); }
  }
  function twist(name, n) {                           // a detent of a knob, as the page turns one
    const c = control(name);
    turn(c.knob, n);
    const r = touches.find((t) => t.el === c.el && t.up === null) || ring(c.el);
    r.until = now + 0.45;
  }
  function ring(el) {
    const b = el.getBoundingClientRect(), d = document.createElement("div");
    const size = Math.max(40, Math.min(70, Math.min(b.width, b.height) * 0.92));
    d.className = "ring";
    Object.assign(d.style, { left: `${b.left + b.width / 2 - size / 2}px`, top: `${b.top + b.height / 2 - size / 2}px`, width: `${size}px`, height: `${size}px` });
    ui.rings.appendChild(d);
    const r = { el, d, at: now, up: null, until: 0 };
    touches.push(r);
    return r;
  }

  // ------------------------------------------------------------- the storyboard
  function fire(e) {
    e.fired = true;
    e.firedAt = now;
    if (e.up) up(e.up);
    if (e.down) down(e.down);
    if (e.turn) twist(e.turn, e.n || 1);
    if (e.cap) { st.cap = e.cap; if (chStart[e.cap.ch - 1] === undefined) chStart[e.cap.ch - 1] = now; drawCap(); }
    if (e.chip) { st.chip = e.chip; st.chipAt = now; st.chipEnd = now + (e.dur || 2.4); drawChip(); }
    if (e.card) { st.card = e.card; st.cardAt = now; st.cardEnd = now + (e.dur || 1e9); drawCard(); }
    if (e.end) st.ended = true;
    if (e.probe) probe = { left: e.probe, r: ex.demo_idea_r(), out: [] };
  }
  function rolls(e) {                                 // an EDIT waiting for the cycle it should roll (want: a pattern)
    const r = cstr(ex.demo_roll(ex.web_now() + 1, ex.demo_idea_r()));
    return new RegExp(e.want).test(r) || now - e.armedAt >= (e.maxWait || 0.6);
  }
  function step() {                                   // the events due before the next chunk
    for (const e of ev)                               // those waiting for the music: the boundaries it crossed
      if (e.armed && !e.fired && e.sync && seen[e.sync] && --e.left <= 0) fire(e);
    for (const e of ev)                               // those waiting for a roll
      if (e.armed && !e.fired && e.want && rolls(e)) fire(e);
    for (let again = true; again;) {                  // the others (again: one may start another now)
      again = false;
      for (const e of ev) {
        if (e.armed || e.fired) continue;
        let t0 = e.t;
        if (e.ref !== undefined) {
          const r = ev[e.ref];
          if (!r.fired) continue;
          t0 = r.firedAt + (e.dt || 0);
        }
        if (now + 1e-9 < t0) continue;
        e.armed = true;
        e.armedAt = now;
        e.left = e.count || 1;
        if (e.want ? rolls(e) : !e.sync) { fire(e); again = true; }
      }
    }
    seen = {};
  }
  function observe() {                                // where the music is: did it cross a chord, its middle, a beat, a bar
    const t = ex.demo_chord_t(), beats = ex.demo_beats();
    seen = { chord: t < last.t, mid: last.t < 0.5 && t >= 0.5, beat: beats !== last.beats, bar: beats !== last.beats && beats % 4 === 0 };
    last = { t, beats };
  }

  // ------------------------------------------------------------- the stage
  const ease = (x) => (x <= 0 ? 0 : x >= 1 ? 1 : x * x * (3 - 2 * x));
  function el(tag, cls, parent, html) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (html !== undefined) e.innerHTML = html;
    parent.appendChild(e);
    return e;
  }
  function stage() {
    const s = el("div", "", document.body);
    s.id = "stage";
    const brand = el("div", "brand", s);
    el("b", "", brand, "MIN-1");
    el("span", "", brand, "minimalist music for the M-VAVE FM-1");
    el("span", "zh", brand, lat("M-VAVE FM-1 极简主义音乐固件"));
    const where = el("div", "where", s);
    el("div", "", where, "the browser emulator: the FM-1's own code");
    el("div", "zh", where, lat("浏览器模拟器 · 与 FM-1 运行同一份代码"));
    const z = el("div", "zoom", s);
    zoom = el("canvas", "", z);
    zoom.width = zoom.height = 480;
    zoom = zoom.getContext("2d");
    zoom.imageSmoothingEnabled = false;
    ui.cap = el("section", "cap", s);
    ui.head = el("div", "head", ui.cap);
    ui.detail = el("div", "detail", ui.cap);
    ui.chip = el("div", "chip", s);
    ui.chapters = el("nav", "chapters", s);
    STORY.chapters.forEach((c, i) => {
      const d = el("div", "", ui.chapters);
      el("span", "n", d, String(i + 1));
      el("span", "en", d, c.en);
      el("span", "zh", d, lat(c.zh));
      el("i", "", d);
    });
    ui.rings = el("div", "rings", s);
    ui.card = el("div", "card", s);
  }
  // Latin in Chinese lines in the Latin face (PingFang's hyphen is a dash): the runs outside tags
  const lat = (h) => h.replace(/(<[^>]*>)|([A-Za-z0-9♭#][A-Za-z0-9♭#\-+.:'’ ]*[A-Za-z0-9♭#]|[A-Za-z0-9♭#])/g,
                               (m, tag, run) => tag || `<span class="lat">${run}</span>`);
  function drawCap() {                                // the heading fades in when it changes, the detail when it does
    const c = st.cap, ch = STORY.chapters[c.ch - 1];
    const head = (ch ? `<div class="ch">${c.ch} / ${STORY.chapters.length} · ${ch.en.toUpperCase()} <span>${lat(ch.zh)}</span></div>` : "") +
                 (c.en ? `<div class="en">${c.en}</div>` : "") + (c.zh ? `<div class="zh">${lat(c.zh)}</div>` : "");
    const detail = (c.d ? `<div class="d">${c.d}</div>` : "") + (c.dz ? `<div class="dz zh">${lat(c.dz)}</div>` : "");
    if (head !== ui.headHtml) { ui.head.innerHTML = ui.headHtml = head; st.headAt = now; }
    if (detail !== ui.detailHtml) { ui.detail.innerHTML = ui.detailHtml = detail; st.detailAt = now; }
    [...ui.chapters.children].forEach((d, i) => { d.classList.toggle("on", i === c.ch - 1); d.classList.toggle("past", i < c.ch - 1); });
  }
  function drawChip() {
    ui.chip.innerHTML = st.chip.split(" + ").map((k) => `<kbd>${k}</kbd>`).join("<em>+</em>");
  }
  function drawCard() {
    const c = st.card;
    ui.card.className = "card " + (c.kind || "");
    ui.card.innerHTML = `<div class="in">${c.html.replace(/<div class="(tz|sz) zh">([^]*?)<\/div>/g, (m, k, t) => `<div class="${k} zh">${lat(t)}</div>`)}</div>`;
  }
  function draw() {
    // the caption fades in when it changes; the chip comes and goes; the cards fade
    for (const [e, t] of [[ui.head, st.headAt], [ui.detail, st.detailAt]]) {
      e.style.opacity = ease((now - t) / 0.5);
      e.style.transform = `translateY(${(1 - ease((now - t) / 0.5)) * 10}px)`;
    }
    const chipA = st.chip ? Math.min(ease((now - st.chipAt) / 0.2), 1 - ease((now - st.chipEnd) / 0.4)) : 0;
    ui.chip.style.opacity = chipA;
    const cardA = st.card ? Math.min(ease((now - st.cardAt) / (st.card.fadeIn ?? 0.8)), 1 - ease((now - st.cardEnd) / (st.card.fadeOut ?? 0.8))) : 0;
    ui.card.style.opacity = cardA;
    ui.card.style.display = cardA > 0.001 ? "" : "none";
    // the rings: in with a little press, out with a fade
    for (let i = touches.length - 1; i >= 0; i--) {
      const r = touches[i], gone = r.up !== null ? now - r.up : r.until && now > r.until ? now - r.until : -1;
      const a = gone >= 0 ? 1 - ease(gone / 0.3) : ease((now - r.at) / 0.06);
      const sc = gone >= 0 ? 1 + 0.25 * ease(gone / 0.3) : 1.35 - 0.35 * ease((now - r.at) / 0.14);
      if (gone >= 0.3) { r.d.remove(); touches.splice(i, 1); continue; }
      r.d.style.opacity = a;
      r.d.style.transform = `scale(${sc})`;
    }
    // where we are: the chapters before full, this one filling over its (rough) length
    [...ui.chapters.children].forEach((d, k) => {
      const t0 = chStart[k], f = t0 === undefined ? 0 : chStart[k + 1] !== undefined ? 1 : Math.min(1, (now - t0) / STORY.chapters[k].dur);
      d.querySelector("i").style.width = `${(f * 100).toFixed(2)}%`;
    });
  }

  // ------------------------------------------------------------- one frame
  function b64(f32) {
    const u8 = new Uint8Array(f32.buffer);
    let s = "";
    for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000));
    return btoa(s);
  }
  function frame(f) {
    const end = Math.round((f + 1) * SR / FPS), out = new Float32Array((end - done) * 2);
    let o = 0, peak = 0;
    while (done < end) {
      now = done / SR;
      if (probe && probe.left-- > 0) probe.out.push(`${now.toFixed(4)} ${ex.web_now() + 1}: ${cstr(ex.demo_roll(ex.web_now() + 1, probe.r))}`);
      step();
      const n = Math.min(CHUNK, end - done);
      ex.web_render(n);
      const l = new Float32Array(mem.buffer, ex.web_out_l(), n), r = new Float32Array(mem.buffer, ex.web_out_r(), n);
      for (let i = 0; i < n; i++) {
        out[o++] = l[i];
        out[o++] = r[i];
        peak = Math.max(peak, Math.abs(l[i]), Math.abs(r[i]));
      }
      done += n;
      observe();
    }
    now = done / SR;
    const b = ex.web_blits();
    if (b !== blits) {
      blits = b;
      drawScreen(new Uint16Array(mem.buffer, ex.web_fb(), 240 * 240));
    }
    drawLights(ex.web_lit_buttons() >>> 0, ex.web_lit_keys() >>> 0, ex.web_song_state() === 1);
    zoom.drawImage(document.getElementById("screen"), 0, 0, 480, 480);
    draw();
    const parts = ex.demo_parts();
    return {
      pcm: b64(out), peak, end: st.ended, probe: probe && probe.left <= 0 && probe.out.length ? probe.out.splice(0) : undefined,
      s: { title: cstr(ex.web_title()), chord: cstr(ex.demo_chord()), parts: "ULBDC".split("").filter((_, i) => parts >> i & 1).join(""),
           msg: [cstr(ex.demo_msg()), cstr(ex.demo_msg2())].join(" ").trim(), view: ex.demo_view(), playing: ex.demo_playing(),
           song: ex.web_song_state(), yours: ex.demo_yours(), cycle: ex.demo_cycle(), section: ex.demo_section(), voices: ex.demo_voices() },
    };
  }

  async function init() {
    document.documentElement.dataset.theme = "dark";
    const wasm = await fetch("min.wasm").then((r) => { if (!r.ok) throw new Error(`min.wasm: ${r.status}`); return r.arrayBuffer(); });
    const wasi = {                                    // the clock stands still while the FM-1 renders, as in the worklet
      clock_time_get: (id, prec, out) => { new DataView(mem.buffer).setBigUint64(out, BigInt(Math.round(done / SR * 1e9)), true); return 0; },
      fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; },
      proc_exit: () => {},
    };
    const stub = new Proxy(wasi, { get: (t, k) => (k in t ? t[k] : () => 0) });
    const env = new Proxy({}, { get: () => () => 0 });
    const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: stub, env });
    ex = instance.exports;
    mem = ex.memory;
    if (ex._initialize) ex._initialize();
    ex.web_master(master);                            // a fresh FM-1: nothing saved, the page's MASTER
    ex.web_boot();
    node = { port: { postMessage: input }, context: { state: "running", resume: () => Promise.resolve() } };
    document.getElementById("start").hidden = true;
    stage();
    syncButtons();
    syncKeys();
    await document.fonts.ready;
    return { fps: FPS, rate: SR, events: ev.length,
             fonts: ["600 20px 'Barlow Semi Condensed'", "400 20px Barlow"].map((f) => `${f}: ${document.fonts.check(f)}`).join(", ") };
  }

  return { init, frame };
})();
