#pragma once

// Single-page UI: file list + playback/download/delete, threshold slider
// with live level readout, stealth toggle, pause/resume, force-keep.
// Served as one static page; all interactivity is plain fetch() calls
// against the JSON/POST routes in web_server.cpp. Basic Auth is handled
// by the browser's native prompt (the server challenges every route).
const char WEB_INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Audio Signal Monitor</title>
<style>
  body { font-family: -apple-system, sans-serif; max-width: 640px; margin: 0 auto; padding: 16px; background: #111; color: #eee; }
  h1 { font-size: 1.3em; }
  .row { display: flex; align-items: center; gap: 8px; margin: 10px 0; }
  .bar { flex: 1; height: 14px; background: #333; border-radius: 5px; overflow: hidden; position: relative; }
  .bar > #level { height: 100%; background: #2e6b31; width: 0%; transition: width 0.15s linear; }
  .bar > #level.above { background: #4caf50; }
  .bar > #thrMark { position: absolute; top: 0; bottom: 0; width: 2px; background: #ffd400; }
  button { background: #333; color: #eee; border: 1px solid #555; border-radius: 6px; padding: 6px 12px; cursor: pointer; }
  button:hover { background: #444; }
  button.danger { border-color: #a33; color: #f88; }
  /* Level bar, threshold slider and dB scale share one horizontal geometry
     (inset by the slider thumb's radius) so positions line up exactly. */
  .meter { margin: 10px 0; }
  .meter .bar, .meter .scale { margin: 0 8px; }
  .meter .scale { position: relative; height: 14px; font-size: 0.75em; color: #777; }
  .meter .scale span { position: absolute; transform: translateX(-50%); }
  #threshold { -webkit-appearance: none; appearance: none; width: 100%; height: 20px; margin: 4px 0 0; background: transparent; }
  #threshold::-webkit-slider-runnable-track { height: 4px; background: #444; border-radius: 2px; }
  #threshold::-webkit-slider-thumb { -webkit-appearance: none; width: 16px; height: 16px; margin-top: -6px; border-radius: 50%; background: #ffd400; border: none; }
  #threshold::-moz-range-track { height: 4px; background: #444; border-radius: 2px; }
  #threshold::-moz-range-thumb { width: 16px; height: 16px; border-radius: 50%; background: #ffd400; border: none; }
  .file { padding: 8px 0; border-bottom: 1px solid #333; font-size: 0.9em; }
  .file .head { display: flex; align-items: baseline; gap: 6px; }
  .file .name { flex: 1; word-break: break-all; }
  .file .player { display: flex; align-items: center; gap: 8px; margin-top: 6px; }
  .file .play { width: 36px; padding: 6px 0; }
  .file canvas { flex: 1; min-width: 0; height: 40px; background: #1b1b1b; border-radius: 4px; cursor: pointer; }
  .file .time { font-variant-numeric: tabular-nums; min-width: 84px; text-align: right; }
  .muted { color: #888; font-size: 0.85em; }
</style>
</head>
<body>
  <h1>Audio Signal Monitor</h1>

  <div class="row">
    <strong id="state">-</strong>
    <span style="flex: 1"></span>
    <span id="ip" class="muted"></span>
  </div>

  <div class="meter">
    <div class="bar"><div id="level"></div><div id="thrMark"></div></div>
    <input type="range" id="threshold" min="-70" max="-10" step="1" aria-label="Threshold">
    <div class="scale" id="scale"></div>
  </div>
  <div class="row">
    <span>Threshold</span>
    <strong id="thresholdVal" style="color: #ffd400"></strong>
    <span style="flex: 1"></span>
    <span id="space" class="muted"></span>
  </div>

  <div class="row">
    <button id="pauseBtn">Pause</button>
    <button id="forceKeepBtn">Force-keep chunk</button>
    <label class="row"><input type="checkbox" id="stealth"> Stealth mode</label>
  </div>

  <h2>Recordings</h2>
  <div id="files"></div>

<script>
async function api(path, opts) {
  const res = await fetch(path, opts);
  if (!res.ok) throw new Error(path + ' -> ' + res.status);
  return res;
}

// Threshold is stored as linear RMS (0..1) on the device; the UI works in dBFS.
function rmsToDb(rms) { return rms > 0 ? 20 * Math.log10(rms) : -100; }
function dbToRms(db) { return Math.pow(10, db / 20); }

// Same scale as the device's status screen; the threshold slider's
// min/max attributes must match these.
const METER_MIN_DB = -70, METER_MAX_DB = -10;
function meterPct(rms) {
  const t = (rmsToDb(rms) - METER_MIN_DB) / (METER_MAX_DB - METER_MIN_DB);
  return Math.max(0, Math.min(1, t)) * 100;
}

// The slider must not snap back to a stale server value. Safari doesn't
// focus range inputs, so dragging is tracked explicitly. After a change,
// the new value stays "pending" and only /status responses requested after
// the POST completed are trusted. The ESP32 serves one request at a time,
// so right after page load the POST can queue behind waveform downloads
// while older polls still return the old value.
let sliderDragging = false;
let pendingDb = null;        // value the user set, not yet confirmed
let pendingPostedAt = 0;     // when its POST completed (0 = still in flight)
let statusInFlight = false;

async function refreshStatus() {
  if (statusInFlight) return;  // don't pile polls up behind a busy server
  statusInFlight = true;
  const requestedAt = Date.now();
  let s;
  try {
    s = await (await api('/status')).json();
  } finally {
    statusInFlight = false;
  }
  const slider = document.getElementById('threshold');
  if (pendingDb !== null && pendingPostedAt && requestedAt > pendingPostedAt) pendingDb = null;
  const showServerValue = !sliderDragging && pendingDb === null;
  const threshold = showServerValue ? s.threshold : dbToRms(Number(slider.value));
  document.getElementById('state').textContent =
    s.paused ? 'PAUSED' : (s.isRecording ? '● REC' : 'listening');
  const level = document.getElementById('level');
  level.style.width = meterPct(s.levelRms) + '%';
  level.classList.toggle('above', s.levelRms >= threshold);
  document.getElementById('thrMark').style.left = meterPct(threshold) + '%';
  document.getElementById('ip').textContent = s.ip || '';
  const freeMB = (s.freeBytes / 1048576).toFixed(0);
  const totalMB = (s.totalBytes / 1048576).toFixed(0);
  document.getElementById('space').textContent = freeMB + ' MB free of ' + totalMB + ' MB';
  if (showServerValue) {
    slider.value = Math.round(rmsToDb(s.threshold));
    document.getElementById('thresholdVal').textContent = slider.value + ' dB';
  }
  document.getElementById('stealth').checked = s.stealthMode;
  document.getElementById('pauseBtn').textContent = s.paused ? 'Resume' : 'Pause';
}

// --- Recordings: waveform + playback -------------------------------------
// The ESP32 WebServer is single-connection and ignores Range headers, which
// Safari's <audio> requires. So instead of one <audio> per row, each WAV is
// fetched once (sequentially, when its row scrolls into view), parsed here
// for the waveform, and played through Web Audio (no <audio> element, which
// Safari mishandles for blob WAVs). Playback is peak-normalised because the
// mic records very quietly; downloads stay raw. Play state is keyed by name,
// so list refreshes don't stop playback.
const WAV_HEADER_BYTES = 44;
const BYTES_PER_SEC = 16000 * 2;  // 16 kHz, 16-bit mono
const PEAK_COUNT = 300;

const TARGET_PEAK = 0.9;           // normalise playback to ~-1 dBFS...
const MAX_GAIN = 64;               // ...but never boost more than +36 dB

let actx = null;
const play = { name: null, source: null, startCtx: 0, offset: 0, playing: false, raf: 0 };
const cache = {};                  // key(name,size) -> { peaks, samples, sampleRate, duration, gain, buffer }
const rows = {};                   // name -> { file, canvas, playBtn, timeEl }
let loadChain = Promise.resolve();
let listSignature = '';

function key(f) { return f.name + '|' + f.size; }
function fmtTime(s) {
  s = isFinite(s) ? Math.max(0, Math.floor(s)) : 0;
  return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
}

function parseWav(buf) {
  const view = new DataView(buf);
  let sampleRate = 16000;
  let off = 12;
  while (off + 8 <= buf.byteLength) {
    const id = String.fromCharCode(view.getUint8(off), view.getUint8(off + 1), view.getUint8(off + 2), view.getUint8(off + 3));
    const size = view.getUint32(off + 4, true);
    if (id === 'fmt ') sampleRate = view.getUint32(off + 12, true);
    if (id === 'data') {
      const remaining = buf.byteLength - off - 8;
      // size is a placeholder (0) while a run is still being written
      const bytes = size > 0 && size <= remaining ? size : remaining;
      return { samples: new Int16Array(buf, off + 8, Math.floor(bytes / 2)), sampleRate };
    }
    off += 8 + size + (size & 1);
  }
  return { samples: new Int16Array(0), sampleRate };
}

function computePeaks(samples) {
  const peaks = new Float32Array(PEAK_COUNT);
  const per = Math.max(1, Math.floor(samples.length / PEAK_COUNT));
  let max = 0;
  for (let i = 0; i < PEAK_COUNT; i++) {
    let p = 0;
    const end = Math.min(samples.length, (i + 1) * per);
    for (let j = i * per; j < end; j++) {
      const v = Math.abs(samples[j]);
      if (v > p) p = v;
    }
    peaks[i] = p;
    if (p > max) max = p;
  }
  // normalise per file so quiet recordings are still readable
  if (max > 0) for (let i = 0; i < PEAK_COUNT; i++) peaks[i] /= max;
  return peaks;
}

function loadRecording(f) {
  const k = key(f);
  if (cache[k]) return Promise.resolve(cache[k]);
  loadChain = loadChain.catch(() => {}).then(async () => {
    if (cache[k]) return cache[k];
    const buf = await (await api('/stream?name=' + encodeURIComponent(f.name))).arrayBuffer();
    const wav = parseWav(buf);
    let peak = 0;
    for (let i = 0; i < wav.samples.length; i++) {
      const v = Math.abs(wav.samples[i]);
      if (v > peak) peak = v;
    }
    cache[k] = {
      peaks: computePeaks(wav.samples),
      samples: wav.samples,
      sampleRate: wav.sampleRate,
      duration: wav.samples.length / wav.sampleRate,
      gain: peak > 0 ? Math.min(MAX_GAIN, TARGET_PEAK * 32768 / peak) : 1,
      buffer: null,                // AudioBuffer, built on first play
    };
    return cache[k];
  });
  return loadChain;
}

function drawWave(name) {
  const r = rows[name];
  if (!r) return;
  const c = r.canvas;
  const dpr = window.devicePixelRatio || 1;
  const w = c.clientWidth * dpr, h = c.clientHeight * dpr;
  if (c.width !== w || c.height !== h) { c.width = w; c.height = h; }
  const ctx = c.getContext('2d');
  ctx.clearRect(0, 0, w, h);
  const entry = cache[key(r.file)];
  if (!entry) {
    ctx.fillStyle = '#555';
    ctx.font = (11 * dpr) + 'px -apple-system, sans-serif';
    ctx.fillText(r.file.size <= WAV_HEADER_BYTES ? 'recording…' : 'loading…', 8 * dpr, h / 2 + 4 * dpr);
    return;
  }
  const progress = name === play.name && entry.duration ? currentPos() / entry.duration : 0;
  const barW = w / PEAK_COUNT;
  for (let i = 0; i < PEAK_COUNT; i++) {
    const bh = Math.max(1 * dpr, entry.peaks[i] * (h - 4 * dpr));
    ctx.fillStyle = (i + 0.5) / PEAK_COUNT <= progress ? '#4caf50' : '#666';
    ctx.fillRect(i * barW, (h - bh) / 2, Math.max(1, barW - 1 * dpr), bh);
  }
}

function updateRowUi(name) {
  const r = rows[name];
  if (!r) return;
  const entry = cache[key(r.file)];
  const total = entry ? entry.duration : Math.max(0, r.file.size - WAV_HEADER_BYTES) / BYTES_PER_SEC;
  r.playBtn.textContent = name === play.name && play.playing ? '❚❚' : '▶';
  r.timeEl.textContent = (name === play.name ? fmtTime(currentPos()) + ' / ' : '') + fmtTime(total);
  drawWave(name);
}

// Must run synchronously inside the click handler: Safari only lets an
// AudioContext start (or resume) from a user gesture.
function unlockAudio() {
  if (!actx) actx = new (window.AudioContext || window.webkitAudioContext)();
  if (actx.state !== 'running') actx.resume();
}

function currentPos() {
  if (!play.name) return 0;
  return play.playing ? play.offset + actx.currentTime - play.startCtx : play.offset;
}

function stopSource() {
  if (!play.source) return;
  play.source.onended = null;
  try { play.source.stop(); } catch (e) {}
  play.source = null;
}

function tick() {
  cancelAnimationFrame(play.raf);
  if (!play.playing) return;
  updateRowUi(play.name);
  play.raf = requestAnimationFrame(tick);
}

function pause() {
  play.offset = currentPos();
  play.playing = false;
  stopSource();
  updateRowUi(play.name);
}

async function playAt(f, fraction) {
  const entry = await loadRecording(f);
  if (!entry.buffer) {
    entry.buffer = actx.createBuffer(1, Math.max(1, entry.samples.length), entry.sampleRate);
    const ch = entry.buffer.getChannelData(0);
    for (let i = 0; i < entry.samples.length; i++) ch[i] = entry.samples[i] / 32768;
  }
  if (play.name !== f.name) {
    const prev = play.name;
    stopSource();
    play.playing = false;
    play.name = f.name;
    play.offset = 0;
    if (prev) updateRowUi(prev);
  }
  let offset = fraction !== null ? fraction * entry.duration : play.offset;
  if (offset >= entry.duration) offset = 0;

  stopSource();
  const src = actx.createBufferSource();
  src.buffer = entry.buffer;
  const gain = actx.createGain();
  gain.gain.value = entry.gain;
  src.connect(gain).connect(actx.destination);
  src.onended = () => {
    play.source = null;
    play.playing = false;
    play.offset = 0;
    updateRowUi(f.name);
  };
  src.start(0, offset);
  Object.assign(play, { source: src, startCtx: actx.currentTime, offset, playing: true });
  tick();
}

const observer = new IntersectionObserver((entries) => {
  for (const e of entries) {
    if (!e.isIntersecting) continue;
    const r = rows[e.target.dataset.name];
    if (!r || r.file.size <= WAV_HEADER_BYTES) continue;
    observer.unobserve(e.target);
    loadRecording(r.file).then(() => updateRowUi(r.file.name)).catch(() => {});
  }
});

async function refreshFiles() {
  const files = await (await api('/files')).json();
  files.sort((a, b) => b.name.localeCompare(a.name));
  const sig = files.map(key).join(',');
  if (sig === listSignature) return;   // unchanged: keep DOM (and canvases) as is
  listSignature = sig;

  const el = document.getElementById('files');
  observer.disconnect();
  el.innerHTML = '';
  for (const n in rows) delete rows[n];

  for (const f of files) {
    const row = document.createElement('div');
    row.className = 'file';
    row.dataset.name = f.name;
    const sizeMB = (f.size / 1048576).toFixed(2);
    row.innerHTML =
      '<div class="head"><span class="name">' + f.name + ' <span class="muted">(' + sizeMB + ' MB)</span></span>' +
      '<a href="/download?name=' + encodeURIComponent(f.name) + '"><button>Download</button></a>' +
      '<button class="danger">Delete</button></div>' +
      '<div class="player"><button class="play">▶</button><canvas></canvas><span class="time muted"></span></div>';
    const r = rows[f.name] = {
      file: f,
      canvas: row.querySelector('canvas'),
      playBtn: row.querySelector('.play'),
      timeEl: row.querySelector('.time'),
    };
    r.playBtn.addEventListener('click', () => {
      unlockAudio();
      if (play.name === f.name && play.playing) pause();
      else playAt(f, null).catch(() => {});
    });
    r.canvas.addEventListener('click', (e) => {
      unlockAudio();
      const rect = r.canvas.getBoundingClientRect();
      playAt(f, (e.clientX - rect.left) / rect.width).catch(() => {});
    });
    row.querySelector('.danger').addEventListener('click', async () => {
      if (!confirm('Delete ' + f.name + '?')) return;
      if (play.name === f.name) { stopSource(); play.playing = false; play.name = null; }
      await api('/delete?name=' + encodeURIComponent(f.name), { method: 'POST' });
      refreshFiles();
    });
    el.appendChild(row);
    updateRowUi(f.name);
    observer.observe(row);
  }
}

window.addEventListener('resize', () => { for (const n in rows) drawWave(n); });

const thresholdSlider = document.getElementById('threshold');
thresholdSlider.addEventListener('pointerdown', () => { sliderDragging = true; });
window.addEventListener('pointerup', () => { sliderDragging = false; });
document.getElementById('threshold').addEventListener('input', (e) => {
  pendingDb = Number(e.target.value);
  pendingPostedAt = 0;
  document.getElementById('thresholdVal').textContent = e.target.value + ' dB';
});
document.getElementById('threshold').addEventListener('change', async (e) => {
  const db = Number(e.target.value);
  pendingDb = db;
  pendingPostedAt = 0;
  try {
    await api('/threshold', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'value=' + dbToRms(db).toPrecision(4) });
  } finally {
    // a newer change may have started meanwhile; only settle our own value
    if (pendingDb === db) pendingPostedAt = Date.now();
  }
});
document.getElementById('stealth').addEventListener('change', async (e) => {
  await api('/stealth', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'on=' + (e.target.checked ? '1' : '0') });
});
document.getElementById('pauseBtn').addEventListener('click', async () => {
  const paused = document.getElementById('pauseBtn').textContent === 'Resume';
  await api(paused ? '/resume' : '/pause', { method: 'POST' });
  refreshStatus();
});
document.getElementById('forceKeepBtn').addEventListener('click', async () => {
  await api('/forcekeep', { method: 'POST' });
});

for (let db = -60; db <= -20; db += 10) {
  const tick = document.createElement('span');
  tick.textContent = db;
  tick.style.left = ((db - METER_MIN_DB) / (METER_MAX_DB - METER_MIN_DB) * 100) + '%';
  document.getElementById('scale').appendChild(tick);
}

refreshStatus();
refreshFiles();
setInterval(refreshStatus, 500);
setInterval(refreshFiles, 10000);
</script>
</body>
</html>
)rawliteral";
