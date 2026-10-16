"use strict";
// MIN-1's demo video: what happens when (web/demo/demo.js plays it, web/demo/record.mjs records it).
// An event happens at a time in seconds, or a while after another event happened ([event, s]). With sync
// it then waits for the music: "chord" the start of a chord, "mid" its middle, "beat", "bar"; count says
// which one (the first by default). Controls: the buttons and knobs by name, W1..W16 the white keys from
// the left, B1..B11 the black ones. A fresh FM-1: it boots into Morning, as it does out of the box.
const STORY = (() => {
  const events = [];
  const at = (when, e) => {
    if (Array.isArray(when)) { e.ref = when[0]; e.dt = when[1] || 0; } else e.t = when;
    events.push(e);
    return events.length - 1;
  };
  const tap = (when, ctl, extra = {}, hold = 0.14) => {
    const d = at(when, { down: ctl, ...extra });
    at([d, hold], { up: ctl });
    return d;
  };
  const chip = (when, text, dur = 2.2) => at(when, { chip: text, dur });
  const sel = (when, ctl, label = ctl, extra = {}) => {   // SEL held, then a control: the SEL map shows meanwhile
    const s = at(when, { down: "SEL", ...extra });
    chip([s, 0], `SEL + ${label}`, 2.4);
    const d = tap([s, 0.55], ctl);
    at([d, 0.8], { up: "SEL" });
    return d;
  };
  const twist = (when, knob, n, extra = {}) => {          // n detents, two frames apart
    let e = at(when, { turn: knob, n: Math.sign(n), ...extra });
    for (let i = 1; i < Math.abs(n); i++) e = at([e, 0.066], { turn: knob, n: Math.sign(n) });
    return e;
  };
  const piece = (when, n) => {                            // stop on a bar, PRESETS n on, play: the piece from its top
    const s = tap(when, "PLAY", { sync: "bar" });
    chip([s, 0.45], "PRESETS", 2.0);
    const t = twist([s, 0.45], "PRESETS", n);
    return tap([t, 0.55], "PLAY");
  };
  const cap = (when, ch, en, zh, d = "", dz = "") => at(when, { cap: { ch, en, zh, d, dz } });
  const card = (when, kind, html, dur, fadeIn = 0.8, fadeOut = 0.8) => at(when, { card: { kind, html, fadeIn, fadeOut }, dur });
  const hold = (when, ctls, bars, extra = {}) => {        // keys down on a chord, up in the middle of the last of `bars`
    const first = at(when, { down: ctls[0], sync: "chord", ...extra });
    for (const c of ctls.slice(1)) at([first, 0], { down: c });
    let up;
    for (const c of ctls) up = at([first, 0], { up: c, sync: "mid", count: bars });
    return [first, up];
  };
  const change = (prev, ctlsOld, ctlsNew, bars) => {     // legato: the old keys up and the new down on the chord
    const d = at([prev, 0], { down: ctlsNew[0], sync: "chord", count: bars });
    for (const c of ctlsOld) at([prev, 0], { up: c, sync: "chord", count: bars });
    for (const c of ctlsNew.slice(1)) at([d, 0], { down: c });
    return d;
  };

  // ---------------------------------------------------------------- 1 listen
  card(0, "title", `<h1>MIN-1</h1><div class="t">minimalist music for the M-VAVE FM-1</div>
    <div class="tz zh">给 M-VAVE FM-1 的极简主义音乐固件</div>
    <div class="models"><span>ARPEGGIO</span><span>PHASE</span><span>PATTERNS</span><span>BELLS</span></div>`, 4.4, 0.01, 1.1);
  cap(0, 1, "Switch it on and it plays.", "一开机，它就自己演奏。",
      "<b>Morning</b> · ARPEGGIO: figures of the chord&rsquo;s notes, two against three.", "琶音模型：和弦音组成的音型，二对三。");
  const four = ["Twenty pieces in four models.", "四种模型，二十首曲子。"];
  let p = piece(20, 8);
  cap([p, 0], 1, ...four, "<b>Drift</b> · PHASE: two pianos play one figure; the lower slowly drifts ahead.",
      "相位模型：两架钢琴弹同一个音型，下面那架慢慢领先。");
  p = piece([p, 13.5], 4);
  cap([p, 0], 1, ...four, "<b>Bazaar</b> · PATTERNS: a chain of short patterns, each player at its own pace.",
      "模式模型：一串短音型，每个声部按自己的步调往前走。");
  p = piece([p, 11.5], 6);
  cap([p, 0], 1, ...four, "<b>Lantern</b> · BELLS: a melody walks by step; a bell answers every note.",
      "钟声模型：旋律级进，每个音都有钟声回应。");
  p = piece([p, 11.5], 7);
  cap([p, 0], 1, "Each piece is a place to start.", "每一首都是一个起点。",
      "<b>Evening</b> · ARPEGGIO, in E♭ major. Now make it yours.", "琶音模型，降E大调。现在，把它变成你的。");

  // ---------------------------------------------------------------- 2 play the harmony
  const c2 = cap([p, 5], 2, "Every white key is a chord that fits.", "每个白键都是一个合调的和弦。",
                 "No theory needed: hold a key and the harmony follows it.", "不需要乐理：按住一个键，和声就跟着它走。");
  let k = at([c2, 1.2], { down: "W4", sync: "chord" });
  k = change(k, ["W4"], ["W5"], 2);
  k = change(k, ["W5"], ["B5"], 2);
  cap([k, 0], 2, "Black keys add a seventh.", "黑键加上七音。", "C minor becomes C minor 7.", "Cm 变成 Cm7。");
  k = change(k, ["B5"], ["W4", "W6"], 2);
  cap([k, 0], 2, "Two keys, a richer chord.", "两个键，更丰富的和弦。", "A♭ and C minor together: A♭ major 7.", "A♭ 加 Cm，就是 A♭maj7。");
  at([k, 0], { up: "W4", sync: "mid", count: 2 });
  const u2 = at([k, 0], { up: "W6", sync: "mid", count: 2 });
  cap([u2, 0.9], 2, "Let go, and the cycle comes back.", "松开，原来的和弦循环就回来了。",
      "The keys light up where the cycle is, so you can play along.", "键灯显示循环走到了哪里，跟着弹就行。");

  // ---------------------------------------------------------------- 3 roll ideas
  const ideas = ["EDIT rolls a new idea.", "EDIT：随机出一个新点子。"];
  const c3 = cap([u2, 4.5], 3, ...ideas, "On the Form page: a new chord cycle.", "在 Form 页：一个新的和弦循环。");
  chip([c3, 1.0], "HOME", 1.4);
  tap([c3, 1.0], "HOME");
  // EDIT rolls from the millisecond it is let go: let go when it rolls this one (within 0.6 s, or anyway)
  let e = at([c3, 1.8], { down: "EDIT" });
  at([e, 0.12], { up: "EDIT", want: "^Eb Gm Cm Ab Bb$" });
  chip([e, 0], "EDIT");
  const pu = tap([e, 11], "ENV");
  chip([pu, 0], "ENV", 1.4);
  cap([pu, 0], 3, ...ideas, "On the Upper page: a new figure for the upper part.", "在 Upper 页：上声部的新音型。");
  e = tap([pu, 1.4], "EDIT");
  chip([e, 0], "EDIT");
  e = tap([e, 5.5], "EDIT");
  chip([e, 0], "EDIT");
  const back = sel([e, 5.5], "EDIT");
  cap([back, 0], 3, "Don&rsquo;t like it? SEL + EDIT goes back.", "不喜欢？SEL + EDIT 退回上一个。",
      "Ideas are kept with the piece, like any other setting.", "点子随曲子保存，和其他设置一样。");

  // ---------------------------------------------------------------- 4 your own cycle
  const c4 = cap([back, 5.5], 4, "Teach it your own cycle.", "教它你自己的和弦循环。",
                 "SEL + REC, play the chords one at a time, then REC.", "SEL + REC，一个一个弹出和弦，再按 REC。");
  const learn = sel([c4, 1.2], "REC");
  let q = at([learn, 1.5], { down: "W1", sync: "chord" });
  for (const w of ["W6", "W4", "W5"]) {                  // a chord a bar, let go in its middle: one at a time
    at([q, 0], { up: events[q].down, sync: "mid" });
    q = at([q, 0], { down: w, sync: "chord" });
  }
  at([q, 0], { up: "W5", sync: "mid" });
  const learned = tap([q, 0], "REC", { sync: "mid" });
  chip([learned, 0], "REC");
  cap([learned, 0.3], 4, "Your chords, its figures.", "你的和弦，它的音型。",
      "The cycle is now <b>Mine</b>, kept with the piece.", "循环变成了 Mine，随曲子一起保存。");

  // ---------------------------------------------------------------- 5 arrange by hand
  const c5 = cap([learned, 9.5], 5, "Bring parts in and out by hand.", "亲手让声部进入、退出。",
                 "Hold SEL: a part&rsquo;s button brings it in or takes it out, at the next chord.",
                 "按住 SEL：声部按钮让它在下一个和弦进入或退出。");
  let a = sel([c5, 1.2], "FX");
  a = sel([a, 5], "ARP");
  const c5b = cap([a, 5], 5, "Put processes on the figures.", "给音型加上过程。",
                  "SEL + white keys: grow, shrink, shift, flip, still, slower, faster, a new section.",
                  "SEL + 白键：增长、缩短、移位、翻转、静止、<br>放慢、加快、新段落。");
  a = sel([c5b, 1.2], "W1", "KEY 1");
  a = sel([a, 5], "W3", "KEY 3");
  const c5c = cap([a, 5], 5, "Bring parts in and out by hand.", "亲手让声部进入、退出。",
                  "SEL + ARP again: the lower part comes back.", "再按 SEL + ARP：下声部回来了。");
  a = sel([c5c, 0.6], "ARP");

  // ---------------------------------------------------------------- 6 record a song
  const c6 = cap([a, 6], 6, "REC records a song from the top.", "REC：从头开始录一首歌。",
                 "Everything you play and change goes into it.", "你弹的、改的，全都录进去。");
  const rec = tap([c6, 1.2], "REC", { sync: "bar" });
  chip([rec, 0], "REC");
  let [k6] = hold([rec, 7.5], ["W2"], 1);              // Fm, B♭7, E♭maj7: a cadence of your own
  k6 = change(k6, ["W2"], ["B4"], 1);
  k6 = change(k6, ["B4"], ["W1", "W3"], 1);
  at([k6, 0], { up: "W1", sync: "mid", count: 2 });
  const u6 = at([k6, 0], { up: "W3", sync: "mid", count: 2 });
  sel([u6, 2], "W8", "KEY 8");
  const kept = tap([rec, 23], "REC", { sync: "bar" });
  chip([kept, 0], "REC");
  cap([kept, 0], 6, "REC again: it&rsquo;s kept, as Song 1.", "再按 REC：存为 Song 1。");
  const play = tap([kept, 2.5], "PLAY");
  chip([play, 0], "PLAY");
  cap([play, 0.2], 6, "PLAY plays it back, exactly.", "PLAY：一模一样地回放。",
      "Eight songs, kept on the FM-1.", "可以存八首歌，保存在 FM-1 里。");
  const fin = card([play, 9.5], "end", `<h1>MIN-1</h1><div class="t">minimalist music for everyone</div>
    <div class="tz zh">人人都能做的极简主义音乐</div>
    <div class="s">Recorded from the browser emulator: the FM-1&rsquo;s own code at 44.1 kHz, as it played.<br>
    Built on Felucca (Leo Kuroshita) and Charles Vestal&rsquo;s X0X and AMB-1 · GPL-3.0</div>
    <div class="sz zh">录自浏览器模拟器：与 FM-1 相同的代码，44.1 kHz，原样录制。</div>`, 1e9, 1.2, 0.8);
  at([fin, 5.5], { end: true });

  return {
    events,
    chapters: [
      { en: "Listen", zh: "聆听", dur: 70 },
      { en: "Play chords", zh: "弹和弦", dur: 28 },
      { en: "Roll ideas", zh: "随机灵感", dur: 28 },
      { en: "Your own cycle", zh: "自己的循环", dur: 22 },
      { en: "Arrange by hand", zh: "亲手编排", dur: 33 },
      { en: "Record a song", zh: "录一首歌", dur: 45 },
    ],
  };
})();
