#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only
// MIN-1's demo video, recorded. The emulator page with the demo's driver (web/demo/build.sh) runs in headless
// Chromium a frame at a time, as its storyboard says (web/demo/storyboard.js); each frame's picture goes to
// ffmpeg and its sound to a WAV, and the two become build/demo/min1-demo.mp4 (the sound brought to -16 LUFS,
// nothing else done to it). Nothing here plays in real time, so nothing drops or drifts.
//
//   web/demo/build.sh && node web/demo/record.mjs [--fast] [--every S] [--at S,S,..] [--to S] [--out FILE]
//   --fast       no video: the sound, a log of what plays and stills (--every / --at seconds) in build/demo/check
//   --to S       stop at S seconds
//   --clean      without the captions and chapters (for stills)
// Chromium: Playwright's headless shell (npx playwright install chromium-headless-shell), or $CHROME.
import { spawn, spawnSync } from "child_process";
import { once } from "events";
import fs from "fs";
import http from "http";
import os from "os";
import path from "path";

const ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname), "../..");
const SITE = path.join(ROOT, "build/demo/site"), OUT = path.join(ROOT, "build/demo"), CHECK = path.join(OUT, "check");
const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); return i < 0 ? d : args[i + 1]; };
const FAST = args.includes("--fast"), TO = +opt("--to", 1e9), EVERY = +opt("--every", 0);
const AT = (opt("--at", "") || "").split(",").filter(Boolean).map(Number);
const FINAL = path.resolve(opt("--out", path.join(OUT, "min1-demo.mp4")));
const W = 1920, H = 1080;

function chrome() {
  if (process.env.CHROME) return process.env.CHROME;
  const base = path.join(os.homedir(), "Library/Caches/ms-playwright");
  const dirs = fs.existsSync(base) ? fs.readdirSync(base).filter((d) => d.startsWith("chromium_headless_shell-")).sort() : [];
  for (const d of dirs.reverse()) {
    for (const sub of fs.readdirSync(path.join(base, d))) {
      const p = path.join(base, d, sub, "chrome-headless-shell");
      if (fs.existsSync(p)) return p;
    }
  }
  throw new Error("no Chromium: set CHROME, or npx playwright install chromium-headless-shell");
}

// ---------------------------------------------------------------- the page, served
const TYPES = { ".html": "text/html; charset=utf-8", ".js": "text/javascript", ".css": "text/css", ".wasm": "application/wasm" };
const server = http.createServer((req, res) => {
  const rel = decodeURIComponent(new URL(req.url, "http://x").pathname).replace(/^\/+/, "") || "index.html";
  const p = path.join(SITE, rel);
  if (!p.startsWith(SITE + path.sep) || !fs.existsSync(p) || fs.statSync(p).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { "content-type": TYPES[path.extname(p)] || "application/octet-stream", "cache-control": "no-store" });
  fs.createReadStream(p).pipe(res);
});
server.listen(0, "127.0.0.1");
await once(server, "listening");
const URL_ = `http://127.0.0.1:${server.address().port}/index.html`;

// ---------------------------------------------------------------- Chromium, over the DevTools protocol
const profile = fs.mkdtempSync(path.join(os.tmpdir(), "min1-demo-"));
const browser = spawn(chrome(), ["--remote-debugging-port=0", `--user-data-dir=${profile}`, "--hide-scrollbars", "--force-device-scale-factor=1",
                                 `--window-size=${W},${H}`, "--force-color-profile=srgb", "--font-render-hinting=none", "--mute-audio", "about:blank"],
                      { stdio: ["ignore", "ignore", "pipe"] });
const wsUrl = await new Promise((res, rej) => {
  let err = "";
  browser.stderr.on("data", (d) => {
    err += d;
    const m = /DevTools listening on (ws:\/\/\S+)/.exec(err);
    if (m) res(m[1]);
  });
  browser.on("exit", (c) => rej(new Error(`Chromium exited (${c}): ${err}`)));
});
const ws = new WebSocket(wsUrl);
await once(ws, "open");
let nextId = 0;
const waiting = new Map();
ws.addEventListener("message", (e) => {
  const m = JSON.parse(e.data);
  if (m.id && waiting.has(m.id)) {
    const w = waiting.get(m.id);
    waiting.delete(m.id);
    if (m.error) w.rej(new Error(`${w.method}: ${m.error.message}`)); else w.res(m.result);
  }
});
function cdp(method, params = {}, sessionId) {
  return new Promise((res, rej) => {
    const id = ++nextId;
    waiting.set(id, { res, rej, method });
    ws.send(JSON.stringify({ id, method, params, sessionId }));
  });
}
const { targetId } = await cdp("Target.createTarget", { url: "about:blank" });
const { sessionId: sid } = await cdp("Target.attachToTarget", { targetId, flatten: true });
const page = (m, p) => cdp(m, p, sid);
async function js(expression, awaitPromise = false) {
  const r = await page("Runtime.evaluate", { expression, awaitPromise, returnByValue: true });
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text);
  return r.result.value;
}
await page("Page.enable");
await page("Runtime.enable");
await page("Emulation.setDeviceMetricsOverride", { width: W, height: H, deviceScaleFactor: 1, mobile: false });
await page("Emulation.setEmulatedMedia", { features: [{ name: "prefers-color-scheme", value: "dark" }, { name: "prefers-reduced-motion", value: "reduce" }] });
await page("Page.navigate", { url: URL_ });
for (let i = 0; !(await js("document.readyState === 'complete' && typeof DEMO === 'object'").catch(() => false)); i++) {
  if (i > 200) throw new Error("the page did not load");
  await new Promise((r) => setTimeout(r, 50));
}
const info = await js("DEMO.init()", true);
if (args.includes("--clean")) await js("document.body.classList.add('clean')");
console.log(`page: ${info.events} events; fonts: ${info.fonts}`);

// ---------------------------------------------------------------- the frames
fs.mkdirSync(OUT, { recursive: true });
if (FAST) { fs.rmSync(CHECK, { recursive: true, force: true }); fs.mkdirSync(CHECK, { recursive: true }); }
const wavPath = path.join(OUT, "sound.wav"), videoPath = path.join(OUT, "picture.mp4");
const wav = fs.openSync(wavPath, "w");
fs.writeSync(wav, Buffer.alloc(44));
let pcmBytes = 0;
let ff = null;
if (!FAST) {
  ff = spawn("ffmpeg", ["-y", "-loglevel", "error", "-f", "image2pipe", "-framerate", String(info.fps), "-c:v", "mjpeg", "-i", "-",
                        "-c:v", "libx264", "-preset", "slow", "-crf", "16", "-tune", "animation", "-pix_fmt", "yuv420p", "-movflags", "+faststart", videoPath],
             { stdio: ["pipe", "inherit", "inherit"] });
}
const shotsAt = new Set(AT.map((s) => Math.round(s * info.fps)));
const t0 = Date.now();
let f = 0, endAt = -1, lastLog = "", peak = 0;
const fmt = (s) => `${Math.floor(s / 60)}:${(s % 60).toFixed(1).padStart(4, "0")}`;
for (; f / info.fps < TO; f++) {
  const r = await js(`DEMO.frame(${f})`);
  const pcm = Buffer.from(r.pcm, "base64");
  fs.writeSync(wav, pcm);
  pcmBytes += pcm.length;
  peak = Math.max(peak, r.peak);
  if (r.probe) console.log("probe:\n" + r.probe.join("\n"));
  const t = (f + 1) / info.fps, s = r.s;
  const line = `${s.title.padEnd(9)} ${s.playing ? "▶" : "■"}${s.song === 1 ? " REC" : s.song === 2 ? " SONG" : ""} chord ${s.chord.padEnd(7)} parts ${s.parts.padEnd(5)} ${s.yours ? "yours " : ""}${s.msg}`;
  if (FAST && line !== lastLog) console.log(`${fmt(t).padStart(7)}  ${line}`);
  lastLog = line;
  if (!FAST) {
    const shot = await page("Page.captureScreenshot", { format: "jpeg", quality: 93, optimizeForSpeed: true });
    if (!ff.stdin.write(Buffer.from(shot.data, "base64"))) await once(ff.stdin, "drain");
    if (f % (info.fps * 10) === 0) console.log(`${fmt(t)}  ${((Date.now() - t0) / 1000).toFixed(0)} s  ${line}`);
  } else if (shotsAt.has(f) || (EVERY > 0 && f % Math.round(EVERY * info.fps) === 0)) {
    const shot = await page("Page.captureScreenshot", { format: "png" });
    fs.writeFileSync(path.join(CHECK, `t${t.toFixed(1).padStart(6, "0")}.png`), Buffer.from(shot.data, "base64"));
    const screen = await js("document.getElementById('screen').toDataURL('image/png')");   // the FM-1's own 240 x 240
    fs.writeFileSync(path.join(CHECK, `t${t.toFixed(1).padStart(6, "0")}-screen.png`), Buffer.from(screen.split(",")[1], "base64"));
  }
  if (r.end && endAt < 0) { endAt = f; break; }
}
const frames = f + 1;
// the WAV's header: 32-bit float stereo, 44.1 kHz
const h = Buffer.alloc(44);
h.write("RIFF", 0); h.writeUInt32LE(36 + pcmBytes, 4); h.write("WAVE", 8); h.write("fmt ", 12); h.writeUInt32LE(16, 16);
h.writeUInt16LE(3, 20); h.writeUInt16LE(2, 22); h.writeUInt32LE(info.rate, 24); h.writeUInt32LE(info.rate * 8, 28);
h.writeUInt16LE(8, 32); h.writeUInt16LE(32, 34); h.write("data", 36); h.writeUInt32LE(pcmBytes, 40);
fs.writeSync(wav, h, 0, 44, 0);
fs.closeSync(wav);
const secs = frames / info.fps;
console.log(`${frames} frames, ${fmt(secs)}, peak ${peak.toFixed(3)} (${(20 * Math.log10(peak)).toFixed(1)} dBFS), in ${((Date.now() - t0) / 1000).toFixed(0)} s`);
ws.close();
browser.kill();
server.close();
fs.rmSync(profile, { recursive: true, force: true });

if (ff) {
  ff.stdin.end();
  await once(ff, "exit");
  // the sound to -16 LUFS (measured, then one gain for the whole, so its dynamics stay), a fade at the very end
  const m = spawnSync("ffmpeg", ["-hide_banner", "-i", wavPath, "-af", "loudnorm=I=-16:TP=-1.5:LRA=20:print_format=json", "-f", "null", "-"], { encoding: "utf8" });
  const j = JSON.parse(m.stderr.match(/\{[^{}]*\}/g).pop());
  const norm = `loudnorm=I=-16:TP=-1.5:LRA=20:measured_I=${j.input_i}:measured_TP=${j.input_tp}:measured_LRA=${j.input_lra}:measured_thresh=${j.input_thresh}:offset=${j.target_offset}:linear=true`;
  const r = spawnSync("ffmpeg", ["-y", "-loglevel", "error", "-i", videoPath, "-i", wavPath, "-filter:a", `${norm},afade=t=out:st=${(secs - 3.5).toFixed(2)}:d=3.5`,
                                 "-ar", "44100", "-c:v", "copy", "-c:a", "aac", "-b:a", "256k", "-shortest", "-movflags", "+faststart", FINAL], { stdio: "inherit" });
  if (r.status !== 0) process.exit(1);
  console.log(`video: ${FINAL} (loudness ${j.input_i} LUFS -> -16)`);
}
