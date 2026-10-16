// SPDX-License-Identifier: GPL-3.0-only
// The browser build in Node: boot it and expect it to play by itself and draw the screen, the keys to
// take the chord (EASY keys, the default: a white key is a chord of the key, the lights the pads in the
// chord; NOTES keys: several keys their notes, one key the triad of the cycle's key on it, the lights
// the chord's notes; let go: the cycle again) and each of the twenty pieces (PRESETS) to sound; a song
// recorded and saved to survive a reload (every flash object handed back, as the page does).
import fs from "fs";
import v8 from "v8";
// Node 26 can hang at exit while V8 compiles this script in the background (concurrent Sparkplug): not here
v8.setFlagsFromString("--no-concurrent-sparkplug");
const wasm = fs.readFileSync(process.argv[2]);
let mem, ex;
const wasi = new Proxy({ clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
  fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; } }, { get: (t, k) => k in t ? t[k] : () => 0 });
async function boot(store = {}) {        // a fresh FM-1, given the saved objects (the page's localStorage) before boot
  const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
  ex = instance.exports; mem = ex.memory;
  if (ex._initialize) ex._initialize();
  for (const [o, b] of Object.entries(store)) {
    new Uint8Array(mem.buffer, ex.web_store(+o), b.length).set(b);
    ex.web_store_set_len(+o, b.length);
  }
  ex.web_master(2800); ex.web_boot();
}
await boot();

let fails = 0;
function check(ok, what) {
  console.log((ok ? "ok   " : "FAIL ") + what);
  if (!ok) fails++;
}
function run(ms) {                      // ms of the device, in 128-frame quanta as the worklet asks: the output's peak
  let peak = 0;
  for (let n = Math.round(ms * 44.1); n > 0; n -= 128) {
    ex.web_render(128);
    for (const p of [ex.web_out_l(), ex.web_out_r()])
      for (const v of new Float32Array(mem.buffer, p, 128)) peak = Number.isFinite(v) ? Math.max(peak, Math.abs(v)) : NaN;
  }
  return peak;
}
const sounds = (peak) => peak > 0.05 && peak < 1;
const KEY0 = 53, NKEYS = 27;            // the keys are F3 .. G5
const keys = (...notes) => notes.reduce((m, n) => m | 1 << (n - KEY0), 0);
const lit = (...pcs) => { let m = 0; for (let k = 0; k < NKEYS; k++) if (pcs.includes((KEY0 + k) % 12)) m |= 1 << k; return m >>> 0; };
const names = (m) => [...new Set([...Array(NKEYS).keys()].filter((k) => m >> k & 1).map((k) => "C C# D Eb E F F# G Ab A Bb B".split(" ")[(KEY0 + k) % 12]))].join(" ") || "none";

let peak = run(3000);
check(sounds(peak), `plays by itself from boot (peak ${peak.toFixed(3)})`);
check(ex.web_blits() > 0, `draws the screen (${ex.web_blits()} blits)`);

ex.web_keys(keys(53));                  // EASY: the leftmost white key, the tonic: A minor; its pads light
run(300);
check(ex.web_lit_keys() === (keys(53) | keys(65) | keys(77)) >>> 0, `easy: F3 is the tonic, A minor (lit: the pads F3 F4 F5? ${(ex.web_lit_keys() >>> 0).toString(2)})`);
ex.web_keys(0);
run(6000);
ex.web_buttons(1 << 5);                 // GLO: Setup; KNOB 3: Keys to NOTES
run(60);
ex.web_buttons(0);
run(60);
ex.web_enc(5, 1);
run(60);

ex.web_keys(keys(62));                  // NOTES: D alone, in the first piece's A minor: D minor
run(300);
check(ex.web_lit_keys() === lit(2, 5, 9), `one key, D: its triad in the key, D minor (lit: ${names(ex.web_lit_keys())})`);
ex.web_keys(0);
run(6000);
check(ex.web_lit_keys() !== lit(2, 5, 9), `let go: the cycle again (lit: ${names(ex.web_lit_keys())})`);

ex.web_keys(keys(63, 67, 70));          // E flat major, outside the cycle
run(300);
check(ex.web_lit_keys() === lit(3, 7, 10), `three keys, Eb G Bb: that chord (lit: ${names(ex.web_lit_keys())})`);
peak = run(1500);
check(sounds(peak), `and the parts play it (peak ${peak.toFixed(3)})`);
ex.web_keys(0);
run(6000);
check(ex.web_lit_keys() !== lit(3, 7, 10), `let go: the cycle again (lit: ${names(ex.web_lit_keys())})`);

for (let i = 1; i <= 20; i++) {         // PRESETS, a detent at a time: pieces 2 .. 20, then 1 again
  ex.web_enc(2, 1);
  peak = run(3500);
  check(sounds(peak), `piece ${i % 20 + 1} sounds (peak ${peak.toFixed(3)})`);
}

// SONGS: REC records a take from the top, REC again keeps it (SONGS), SAVE writes it to flash. The page
// keeps every object (all 34) and hands them back to a fresh FM-1: there SEL + PLAY (songs) and PLAY play it
const SEL = 1 << 1, SAVE = 1 << 7, PLAY = 1 << 10, REC = 1 << 11;
const press = (mask) => { ex.web_buttons(mask); run(80); ex.web_buttons(0); run(80); };
press(REC);
ex.web_keys(keys(53));
run(1500);
ex.web_keys(0);
run(1500);
press(REC);
run(500);
press(SAVE);
run(300);
const store = {};
for (let o = 0; o < ex.web_nobj(); o++) {
  const n = ex.web_store_len(o);
  if (n >= 0) store[o] = new Uint8Array(mem.buffer, ex.web_store(o), n).slice();
}
const songBytes = Object.entries(store).reduce((s, [o, b]) => s + (+o >= 2 ? b.length : 0), 0);
check(ex.web_nobj() === 34 && songBytes > 0, `a song recorded and saved (${ex.web_nobj()} objects, ${songBytes} bytes of song)`);
async function songPlays(saved) {
  await boot(saved);
  run(500);
  ex.web_buttons(SEL); run(80); ex.web_buttons(SEL | PLAY); run(80); ex.web_buttons(SEL); run(80); ex.web_buttons(0); run(80);
  press(PLAY);
  run(500);
  return ex.web_song_state();
}
check(await songPlays(store) === 2, "after a reload the song plays (SEL + PLAY: songs; PLAY)");
check(await songPlays({}) === 0, "while a fresh FM-1 has none to play");
console.log(fails ? `${fails} failed` : "all passed", "blits", ex.web_blits());
process.exit(fails ? 1 : 0);
