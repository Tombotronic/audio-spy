#pragma once

// Single-page UI: file list + playback/download/delete, threshold slider
// with live level readout, pause/resume, a bypass-threshold switch (keep
// everything while on), and a settings panel (IP address, storage,
// appearance, stealth toggle, delete all recordings).
// Served as one static page; all interactivity is plain fetch() calls
// against the JSON/POST routes in web_server.cpp. Basic Auth is handled
// by the browser's native prompt (the server challenges every route).
const char WEB_INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>Audio Signal Monitor</title>
<!-- Add to Home Screen: runs full screen with its own icon and name. -->
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="default">
<meta name="apple-mobile-web-app-title" content="Audio Monitor">
<meta name="theme-color" content="#f6f6f7" media="(prefers-color-scheme: light)">
<meta name="theme-color" content="#111111" media="(prefers-color-scheme: dark)">
<meta name="format-detection" content="telephone=no">
<link rel="manifest" href="/manifest.json">
<link rel="icon" href="/icon.png">
<link rel="apple-touch-icon" href="/icon.png">
<style>
  /* Theme tokens: dark by default; light via the system setting unless the
     user forced dark, or when forced light (Settings -> Appearance). */
  :root {
    color-scheme: dark;
    --bg: #111; --fg: #eee; --muted: #888; --scale: #777;
    --surface: #1b1b1b; --border: #333; --bar: #333; --track: #444;
    --btn: #333; --btn-hover: #444; --btn-border: #555;
    --level: #2e6b31; --level-above: #4caf50; --accent: #ffd400;
    --ok: #4caf50; --rec: #ff4040;
    --danger: #a33; --danger-text: #f88; --confirm-bg: #2a1414;
    --overlay: rgba(0, 0, 0, 0.6);
    --wave: #666; --wave-played: #4caf50; --wave-text: #555;
  }
  @media (prefers-color-scheme: light) {
    :root:not([data-theme="dark"]) {
      color-scheme: light;
      --bg: #f6f6f7; --fg: #1d1d1f; --muted: #6e6e73; --scale: #8e8e93;
      --surface: #fff; --border: #d9d9de; --bar: #e3e3e8; --track: #c7c7cc;
      --btn: #fff; --btn-hover: #ececf0; --btn-border: #c7c7cc;
      --level: #a5d6a7; --level-above: #2e7d32; --accent: #d49b00;
      --ok: #2e7d32; --rec: #d32f2f;
      --danger: #c62828; --danger-text: #c62828; --confirm-bg: #fdecec;
      --overlay: rgba(0, 0, 0, 0.3);
      --wave: #b0b0b8; --wave-played: #2e7d32; --wave-text: #9a9aa0;
    }
  }
  :root[data-theme="light"] {
    color-scheme: light;
    --bg: #f6f6f7; --fg: #1d1d1f; --muted: #6e6e73; --scale: #8e8e93;
    --surface: #fff; --border: #d9d9de; --bar: #e3e3e8; --track: #c7c7cc;
    --btn: #fff; --btn-hover: #ececf0; --btn-border: #c7c7cc;
    --level: #a5d6a7; --level-above: #2e7d32; --accent: #d49b00;
    --ok: #2e7d32; --rec: #d32f2f;
    --danger: #c62828; --danger-text: #c62828; --confirm-bg: #fdecec;
    --overlay: rgba(0, 0, 0, 0.3);
    --wave: #b0b0b8; --wave-played: #2e7d32; --wave-text: #9a9aa0;
  }

  html { -webkit-text-size-adjust: 100%; background: var(--bg); }
  /* Full screen from the home screen: keep clear of the notch and home bar. */
  body { font-family: -apple-system, sans-serif; max-width: 640px; margin: 0 auto; background: var(--bg); color: var(--fg);
         padding: max(16px, env(safe-area-inset-top)) max(16px, env(safe-area-inset-right))
                  max(16px, env(safe-area-inset-bottom)) max(16px, env(safe-area-inset-left));
         -webkit-tap-highlight-color: transparent; }
  /* No double-tap zoom (and its tap delay) on controls. */
  button, input, canvas, a { touch-action: manipulation; }
  h1 { font-size: 1.3em; flex: 1; margin: 0; }
  .row { display: flex; align-items: center; gap: 8px; margin: 10px 0; }
  .bar { flex: 1; height: 14px; background: var(--bar); border-radius: 5px; overflow: hidden; position: relative; }
  .bar > #level { height: 100%; background: var(--level); width: 0%; transition: width 0.15s linear; }
  .bar > #level.above { background: var(--level-above); }
  .bar > #thrMark { position: absolute; top: 0; bottom: 0; width: 2px; background: var(--accent); }
  button { background: var(--btn); color: var(--fg); border: 1px solid var(--btn-border); border-radius: 6px; padding: 6px 12px; cursor: pointer; }
  button:hover { background: var(--btn-hover); }
  button.danger { border-color: var(--danger); color: var(--danger-text); }
  button.danger.solid { background: var(--danger); color: #fff; border-color: var(--danger); }
  button:disabled { opacity: 0.4; cursor: default; }
  #settingsBtn { font-size: 1.2em; padding: 4px 10px; }
  #bypassBtn.active { background: var(--accent); border-color: var(--accent); color: #000; }
  #state.st-listening { color: var(--ok); }
  #state.st-rec { color: var(--rec); }
  #state.st-paused { color: var(--accent); }
  #thresholdVal { color: var(--accent); margin-left: 4px; }
  #battery { justify-self: end; font-variant-numeric: tabular-nums; }
  /* State left, threshold centred on the page (equal side columns). */
  .status-row { display: grid; grid-template-columns: 1fr auto 1fr; align-items: center; gap: 8px; margin: 10px 0; }
  /* Level bar, threshold slider and dB scale share one horizontal geometry
     (inset by the slider thumb's radius) so positions line up exactly. */
  .meter { margin: 10px 0; }
  .meter .bar, .meter .scale { margin: 0 8px; }
  .meter .scale { position: relative; height: 14px; font-size: 0.75em; color: var(--scale); }
  .meter .scale span { position: absolute; transform: translateX(-50%); }
  /* The native track spans the full width (the thumb needs the 8px inset on
     each side), so it's transparent; .track draws it with the bar's width. */
  .slider { position: relative; margin-top: 4px; }
  .slider .track { position: absolute; left: 8px; right: 8px; top: 8px; height: 4px; background: var(--track); border-radius: 2px; }
  #threshold { position: relative; -webkit-appearance: none; appearance: none; width: 100%; height: 20px; margin: 0; background: transparent; display: block; }
  #threshold::-webkit-slider-runnable-track { height: 4px; background: transparent; }
  #threshold::-webkit-slider-thumb { -webkit-appearance: none; width: 16px; height: 16px; margin-top: -6px; border-radius: 50%; background: var(--accent); border: none; }
  #threshold::-moz-range-track { height: 4px; background: transparent; }
  #threshold::-moz-range-thumb { width: 16px; height: 16px; border-radius: 50%; background: var(--accent); border: none; }
  .file { padding: 8px 0; border-bottom: 1px solid var(--border); font-size: 0.9em; }
  .file .head { display: flex; align-items: baseline; gap: 6px; }
  .file .name { flex: 1; word-break: break-all; }
  .file .player { display: flex; align-items: center; gap: 8px; margin-top: 6px; }
  .file .play { width: 36px; padding: 6px 0; }
  .file canvas { flex: 1; min-width: 0; height: 40px; background: var(--surface); border: 1px solid var(--border); border-radius: 4px; cursor: pointer; }
  .file .time { font-variant-numeric: tabular-nums; min-width: 84px; text-align: right; }
  .muted { color: var(--muted); font-size: 0.85em; }
  [hidden] { display: none !important; }
  .overlay { position: fixed; inset: 0; background: var(--overlay); display: flex; justify-content: center; align-items: flex-start; z-index: 10; overflow-y: auto; overscroll-behavior: contain;
             padding: max(48px, env(safe-area-inset-top)) max(16px, env(safe-area-inset-right)) max(16px, env(safe-area-inset-bottom)) max(16px, env(safe-area-inset-left)); }
  /* Touch screens: finger-sized controls (Apple's 44 pt guideline). */
  @media (pointer: coarse) {
    button { min-height: 44px; padding: 8px 14px; font-size: 1em; }
    .file .play { width: 44px; }
    .file canvas { height: 44px; }
    #threshold { height: 32px; }
    .slider .track { top: 14px; }
    .seg button { padding: 8px 14px; }
  }
  .panel { background: var(--surface); border: 1px solid var(--border); border-radius: 10px; padding: 16px; width: 100%; max-width: 480px; box-sizing: border-box; }
  .panel h2 { flex: 1; margin: 0; font-size: 1.15em; }
  .setting { display: flex; align-items: center; gap: 12px; padding: 12px 0; border-top: 1px solid var(--border); }
  .setting > div { flex: 1; }
  .setting input[type=checkbox] { width: 20px; height: 20px; }
  .setting > .seg { flex: none; }
  /* Storage: used space as a bar (recordings / everything else), free space
     is the empty remainder. Tiny amounts still get a visible sliver. */
  .storage-head { display: flex; align-items: baseline; justify-content: space-between; }
  #spaceFree { font-size: 1.15em; font-variant-numeric: tabular-nums; }
  .storage-bar { display: flex; height: 10px; margin: 8px 0 6px; background: var(--bar); border-radius: 5px; overflow: hidden; }
  .storage-bar > div { height: 100%; transition: width 0.3s ease; }
  #spaceRec { background: var(--ok); }
  #spaceOther { background: var(--scale); }
  .storage-legend { display: flex; flex-wrap: wrap; align-items: center; gap: 4px 12px; font-variant-numeric: tabular-nums; }
  .storage-legend > span { display: inline-flex; align-items: center; gap: 5px; }
  .dot { display: inline-block; width: 8px; height: 8px; border-radius: 50%; }
  .dot.rec { background: var(--ok); }
  .dot.other { background: var(--scale); }
  .seg { display: inline-grid; grid-template-columns: repeat(3, 1fr); border: 1px solid var(--btn-border); border-radius: 6px; overflow: hidden; }
  .seg button { border: none; border-radius: 0; padding: 6px 14px; }
  .seg button + button { border-left: 1px solid var(--btn-border); }
  .seg button.active { background: var(--accent); color: #000; }
  .confirm { background: var(--confirm-bg); border: 1px solid var(--danger); border-radius: 8px; padding: 12px; }
  .confirm p { margin: 0 0 8px; }
  .confirm .row { justify-content: flex-end; margin: 0; }
</style>
<script>
  // Apply a saved theme before first paint, so there's no dark flash.
  try {
    const t = localStorage.getItem('theme');
    if (t === 'light' || t === 'dark') document.documentElement.dataset.theme = t;
  } catch (e) {}
</script>
</head>
<body>
  <div class="row">
    <h1>Audio Signal Monitor</h1>
    <button id="settingsBtn" aria-label="Settings" title="Settings">⚙</button>
  </div>

  <div class="status-row">
    <strong id="state">-</strong>
    <span>Threshold <strong id="thresholdVal"></strong></span>
    <span id="battery" class="muted" title="Battery"></span>
  </div>

  <div class="meter">
    <div class="bar"><div id="level"></div><div id="thrMark"></div></div>
    <div class="slider">
      <div class="track"></div>
      <input type="range" id="threshold" min="-60" max="0" step="1" aria-label="Threshold">
    </div>
    <div class="scale" id="scale"></div>
  </div>

  <div class="row">
    <button id="pauseBtn">Pause</button>
    <button id="bypassBtn" title="While on, everything is recorded regardless of the threshold. Off again after a device reboot.">Bypass Threshold</button>
  </div>

  <h2>Recordings</h2>
  <div id="files"></div>

  <div id="settings" class="overlay" hidden>
    <div class="panel" role="dialog" aria-modal="true" aria-labelledby="settingsTitle">
      <div class="row" style="margin-top: 0">
        <h2 id="settingsTitle">Settings</h2>
        <button id="settingsClose" aria-label="Close">✕</button>
      </div>
      <div class="setting">
        <div><strong>IP address</strong></div>
        <span id="ip" class="muted"></span>
      </div>
      <div class="setting">
        <div><strong>WiFi</strong><br><span class="muted" id="ssid"></span></div>
        <button class="danger" id="forgetWifiBtn">Forget…</button>
      </div>
      <div id="forgetWifiConfirm" class="confirm" hidden>
        <p>Forget this network and restart? Pick a new one on the device's keyboard. Until then it records offline and this page can't be reached.</p>
        <div class="row">
          <button id="forgetWifiCancel">Cancel</button>
          <button class="danger solid" id="forgetWifiYes">Yes, forget WiFi</button>
        </div>
      </div>
      <p id="forgetWifiResult" class="muted" hidden></p>
      <div class="setting storage">
        <div>
          <div class="storage-head"><strong>Storage</strong><span><strong id="spaceFree"></strong> <span class="muted">free</span></span></div>
          <div class="storage-bar" role="img" id="spaceBar"><div id="spaceRec"></div><div id="spaceOther"></div></div>
          <div class="storage-legend muted">
            <span><i class="dot rec"></i><span id="spaceRecText"></span></span>
            <span><i class="dot other"></i><span id="spaceOtherText"></span></span>
            <span style="flex: 1"></span>
            <span id="spaceTotal"></span>
          </div>
        </div>
      </div>
      <div class="setting">
        <div><strong>Appearance</strong><br><span class="muted">Saved in this browser.</span></div>
        <div class="seg" id="themeSeg" role="group" aria-label="Appearance">
          <button data-theme="system">System</button><button data-theme="light">Light</button><button data-theme="dark">Dark</button>
        </div>
      </div>
      <label class="setting">
        <div><strong>Stealth mode</strong><br><span class="muted">Turns the device screen off. Back on after a reboot.</span></div>
        <input type="checkbox" id="stealth">
      </label>
      <div class="setting">
        <div><strong>Delete all recordings</strong><br><span class="muted" id="deleteAllInfo"></span></div>
        <button class="danger" id="deleteAllBtn">Delete all…</button>
      </div>
      <div id="deleteAllConfirm" class="confirm" hidden>
        <p>Do you really want to delete <strong id="deleteAllCount"></strong>? This can't be undone.</p>
        <div class="row">
          <button id="deleteAllCancel">Cancel</button>
          <button class="danger solid" id="deleteAllYes">Yes, delete all</button>
        </div>
      </div>
      <p id="deleteAllResult" class="muted" hidden></p>
    </div>
  </div>

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
const METER_MIN_DB = -60, METER_MAX_DB = 0;
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
  const stateEl = document.getElementById('state');
  stateEl.textContent = s.paused ? '❚❚ PAUSED' : (s.isRecording ? '● REC' : '◉ LISTENING');
  stateEl.className = s.paused ? 'st-paused' : (s.isRecording ? 'st-rec' : 'st-listening');
  const level = document.getElementById('level');
  level.style.width = meterPct(s.levelRms) + '%';
  level.classList.toggle('above', s.levelRms >= threshold);
  document.getElementById('thrMark').style.left = meterPct(threshold) + '%';
  document.getElementById('ip').textContent = s.ip || '';
  document.getElementById('ssid').textContent = s.ssid || '';
  showStorage(s.freeBytes, s.totalBytes);
  document.getElementById('battery').textContent = s.battery >= 0 ? '🔋 ' + s.battery + '%' : '';
  if (showServerValue) {
    slider.value = Math.round(rmsToDb(s.threshold));
    document.getElementById('thresholdVal').textContent = slider.value + ' dB';
  }
  document.getElementById('stealth').checked = s.stealthMode;
  document.getElementById('pauseBtn').textContent = s.paused ? 'Resume' : 'Pause';
  showBypass(s.bypassThreshold);
  // Nothing is recorded while paused, so bypass has nothing to act on.
  document.getElementById('bypassBtn').disabled = s.paused;
}

// --- Recordings: waveform + playback -------------------------------------
// The ESP32 WebServer is single-connection and ignores Range headers, which
// Safari's <audio> requires. So:
// - The waveform is a coarse indication from /peaks (~100 values the device
//   samples from the file on the SD card), not the whole WAV over WiFi.
// - The audio itself is fetched only when a row is played, then played
//   through Web Audio (no <audio> element, which Safari mishandles for blob
//   WAVs), peak-normalised because the mic records quietly. Downloads stay
//   raw. Play state is keyed by name, so list refreshes don't stop playback.
// Both fetches go through one queue, since the server handles one at a time.
const WAV_HEADER_BYTES = 44;
const BYTES_PER_SEC = 16000 * 2;  // 16 kHz, 16-bit mono
const PEAK_COUNT = 100;
const PLAYHEAD_REDRAW_MS = 100;

const TARGET_PEAK = 0.9;           // normalise playback to ~-1 dBFS...
const MAX_GAIN = 64;               // ...but never boost more than +36 dB

let actx = null;
const play = { name: null, source: null, startCtx: 0, offset: 0, playing: false, timer: 0, loading: null };
const peaksCache = {};             // key(name,size) -> Float32Array (0..1)
const audioCache = {};             // key(name,size) -> { samples, sampleRate, duration, gain, buffer }
const rows = {};                   // name -> { file, canvas, playBtn, timeEl }
let fetchQueue = Promise.resolve();
let listSignature = '';
let lastFiles = [];

function key(f) { return f.name + '|' + f.size; }

// "2026-09-22_20-37-06.wav" -> "22.09.2026 20:37:06" (a "_1" suffix, added
// when a name was taken, is kept). Files recorded before NTP sync (e.g.
// "unsynced-000003.wav") just lose the extension.
function displayName(name) {
  const m = name.match(/^(\d{4})-(\d{2})-(\d{2})_(\d{2})-(\d{2})-(\d{2})(_\d+)?\.wav$/);
  if (!m) return name.replace(/\.wav$/, '');
  return m[3] + '.' + m[2] + '.' + m[1] + ' ' + m[4] + ':' + m[5] + ':' + m[6] + (m[7] ? ' (' + m[7].slice(1) + ')' : '');
}
// Filenames come from the SD card, which anyone can write to.
function escapeHtml(s) {
  return s.replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
}
function fmtTime(s) {
  s = isFinite(s) ? Math.max(0, Math.floor(s)) : 0;
  return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
}
function cssVar(name) { return getComputedStyle(document.documentElement).getPropertyValue(name).trim(); }
function sizeDuration(f) { return Math.max(0, f.size - WAV_HEADER_BYTES) / BYTES_PER_SEC; }

function queued(task) {
  const p = fetchQueue.catch(() => {}).then(task);
  fetchQueue = p;
  return p;
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
      // Use everything after the data header, not its size field: the size
      // lags behind while a run is being written, and a run cut off by a
      // reboot can hold more audio than its last header update says. The
      // device's writer never appends other chunks after "data".
      const bytes = buf.byteLength - off - 8;
      return { samples: new Int16Array(buf, off + 8, Math.floor(bytes / 2)), sampleRate };
    }
    off += 8 + size + (size & 1);
  }
  return { samples: new Int16Array(0), sampleRate };
}

function loadPeaks(f) {
  const k = key(f);
  if (peaksCache[k]) return Promise.resolve(peaksCache[k]);
  return queued(async () => {
    if (peaksCache[k]) return peaksCache[k];
    const raw = await (await api('/peaks?n=' + PEAK_COUNT + '&name=' + encodeURIComponent(f.name))).json();
    const max = Math.max(1, ...raw);
    // normalise per file so quiet recordings are still readable
    peaksCache[k] = Float32Array.from(raw, (v) => v / max);
    return peaksCache[k];
  });
}

function loadAudio(f) {
  const k = key(f);
  if (audioCache[k]) return Promise.resolve(audioCache[k]);
  return queued(async () => {
    if (audioCache[k]) return audioCache[k];
    const buf = await (await api('/stream?name=' + encodeURIComponent(f.name))).arrayBuffer();
    const wav = parseWav(buf);
    let peak = 0;
    for (let i = 0; i < wav.samples.length; i++) {
      const v = Math.abs(wav.samples[i]);
      if (v > peak) peak = v;
    }
    audioCache[k] = {
      samples: wav.samples,
      sampleRate: wav.sampleRate,
      duration: wav.samples.length / wav.sampleRate,
      gain: peak > 0 ? Math.min(MAX_GAIN, TARGET_PEAK * 32768 / peak) : 1,
      buffer: null,                // AudioBuffer, built on first play
    };
    return audioCache[k];
  });
}

function totalDuration(f) {
  const audio = audioCache[key(f)];
  return audio ? audio.duration : sizeDuration(f);
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
  const peaks = peaksCache[key(r.file)];
  if (!peaks) {
    ctx.fillStyle = cssVar('--wave-text');
    ctx.font = (11 * dpr) + 'px -apple-system, sans-serif';
    ctx.fillText(r.file.size <= WAV_HEADER_BYTES ? 'recording…' : 'loading…', 8 * dpr, h / 2 + 4 * dpr);
    return;
  }
  const total = totalDuration(r.file);
  const progress = name === play.name && total ? currentPos() / total : 0;
  const barW = w / peaks.length;
  const played = cssVar('--wave-played'), unplayed = cssVar('--wave');
  for (let i = 0; i < peaks.length; i++) {
    const bh = Math.max(1 * dpr, peaks[i] * (h - 4 * dpr));
    ctx.fillStyle = (i + 0.5) / peaks.length <= progress ? played : unplayed;
    ctx.fillRect(i * barW, (h - bh) / 2, Math.max(1, barW - 2 * dpr), bh);
  }
}

function updateRowUi(name) {
  const r = rows[name];
  if (!r) return;
  setText(r.playBtn, play.loading === name ? '…' : (name === play.name && play.playing ? '❚❚' : '▶'));
  setText(r.timeEl, (name === play.name ? fmtTime(currentPos()) + ' / ' : '') + fmtTime(totalDuration(r.file)));
  drawWave(name);
}

// Rewriting text replaces the node even when it's unchanged; on the play
// button, a replacement between mouse down and up (the playhead redraws
// every 100 ms) makes Safari drop the click.
function setText(el, text) {
  if (el.textContent !== text) el.textContent = text;
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
  clearTimeout(play.timer);
  if (!play.playing) return;
  updateRowUi(play.name);
  play.timer = setTimeout(tick, PLAYHEAD_REDRAW_MS);
}

function pause() {
  play.offset = currentPos();
  play.playing = false;
  stopSource();
  updateRowUi(play.name);
}

async function playAt(f, fraction) {
  let entry = audioCache[key(f)];
  if (!entry) {
    play.loading = f.name;
    updateRowUi(f.name);
    try {
      entry = await loadAudio(f);
    } finally {
      play.loading = null;
      updateRowUi(f.name);
    }
  }
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
    loadPeaks(r.file).then(() => updateRowUi(r.file.name)).catch(() => {});
  }
});

async function refreshFiles() {
  const files = await (await api('/files')).json();
  files.sort((a, b) => b.name.localeCompare(a.name));
  lastFiles = files;
  updateDeleteAllInfo();
  const sig = files.map((f) => key(f) + (f.recording ? '*' : '')).join(',');
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
      '<div class="head"><span class="name">' + escapeHtml(displayName(f.name)) + ' <span class="muted">(' + sizeMB + ' MB)</span></span>' +
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
    if (f.recording) {
      row.querySelector('.danger').disabled = true;
      row.querySelector('.danger').title = 'Still recording';
    }
    row.querySelector('.danger').addEventListener('click', async () => {
      if (!confirm('Delete ' + displayName(f.name) + '?')) return;
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
// --- Settings panel ------------------------------------------------------
function fmtBytes(b) {
  if (b >= 1e9) return (b / 1073741824).toFixed(1) + ' GB';
  if (b >= 1e6) return (b / 1048576).toFixed(1) + ' MB';
  return Math.round(b / 1024) + ' KB';
}

// Recordings come from the last /files listing; "other" is the rest of the
// used space (filesystem, temp chunks, anything else on the card).
function showStorage(free, total) {
  if (!total) return;
  const used = Math.max(0, total - free);
  const rec = Math.min(used, lastFiles.reduce((sum, f) => sum + f.size, 0));
  const other = used - rec;
  const pct = (b) => b > 0 ? 'max(3px, ' + (b / total * 100) + '%)' : '0';
  document.getElementById('spaceFree').textContent = fmtBytes(free);
  document.getElementById('spaceRec').style.width = pct(rec);
  document.getElementById('spaceOther').style.width = pct(other);
  document.getElementById('spaceRecText').textContent = 'Recordings ' + fmtBytes(rec);
  document.getElementById('spaceOtherText').textContent = 'Other ' + fmtBytes(other);
  document.getElementById('spaceTotal').textContent = fmtBytes(total) + ' total';
  document.getElementById('spaceBar').setAttribute('aria-label',
    fmtBytes(free) + ' free of ' + fmtBytes(total) + ', recordings ' + fmtBytes(rec));
}

const settingsEl = document.getElementById('settings');

function deletableFiles() { return lastFiles.filter((f) => !f.recording); }
function plural(n) { return n + ' recording' + (n === 1 ? '' : 's'); }

function updateDeleteAllInfo() {
  const files = deletableFiles();
  const mb = files.reduce((sum, f) => sum + f.size, 0) / 1048576;
  const inProgress = lastFiles.length - files.length;
  document.getElementById('deleteAllInfo').textContent =
    plural(files.length) + ', ' + mb.toFixed(1) + ' MB' +
    (inProgress ? ' (the one being recorded right now is kept)' : '');
  document.getElementById('deleteAllBtn').disabled = files.length === 0;
}

function showDeleteAllConfirm(show) {
  document.getElementById('deleteAllConfirm').hidden = !show;
  document.getElementById('deleteAllBtn').hidden = show;
  if (show) {
    document.getElementById('deleteAllCount').textContent = 'all ' + plural(deletableFiles().length);
    document.getElementById('deleteAllResult').hidden = true;
  }
}

function showForgetWifiConfirm(show) {
  document.getElementById('forgetWifiConfirm').hidden = !show;
  document.getElementById('forgetWifiBtn').hidden = show;
}

function openSettings() {
  showForgetWifiConfirm(false);
  document.getElementById('forgetWifiResult').hidden = true;
  showDeleteAllConfirm(false);
  document.getElementById('deleteAllResult').hidden = true;
  updateDeleteAllInfo();
  settingsEl.hidden = false;
}
function closeSettings() { settingsEl.hidden = true; }

// Appearance: "system" follows prefers-color-scheme; light/dark force it.
function currentTheme() {
  try { return localStorage.getItem('theme') || 'system'; } catch (e) { return 'system'; }
}
function applyTheme(theme) {
  if (theme === 'light' || theme === 'dark') document.documentElement.dataset.theme = theme;
  else delete document.documentElement.dataset.theme;
  try {
    if (theme === 'system') localStorage.removeItem('theme');
    else localStorage.setItem('theme', theme);
  } catch (e) {}
  for (const b of document.querySelectorAll('#themeSeg button')) {
    b.classList.toggle('active', b.dataset.theme === theme);
    b.setAttribute('aria-pressed', b.dataset.theme === theme);
  }
  for (const n in rows) drawWave(n);  // canvas colours don't follow CSS by themselves
}
for (const b of document.querySelectorAll('#themeSeg button')) {
  b.addEventListener('click', () => applyTheme(b.dataset.theme));
}
window.matchMedia('(prefers-color-scheme: light)').addEventListener('change', () => {
  for (const n in rows) drawWave(n);
});
applyTheme(currentTheme());

document.getElementById('settingsBtn').addEventListener('click', openSettings);
document.getElementById('settingsClose').addEventListener('click', closeSettings);
settingsEl.addEventListener('click', (e) => { if (e.target === settingsEl) closeSettings(); });
document.addEventListener('keydown', (e) => { if (e.key === 'Escape' && !settingsEl.hidden) closeSettings(); });

document.getElementById('deleteAllBtn').addEventListener('click', () => showDeleteAllConfirm(true));
document.getElementById('deleteAllCancel').addEventListener('click', () => showDeleteAllConfirm(false));
document.getElementById('deleteAllYes').addEventListener('click', async () => {
  const yes = document.getElementById('deleteAllYes');
  yes.disabled = true;
  const result = document.getElementById('deleteAllResult');
  try {
    if (play.name) { stopSource(); play.playing = false; play.name = null; }
    const r = await (await api('/deleteall', { method: 'POST' })).json();
    result.textContent = 'Deleted ' + plural(r.deleted) + '.' +
      (r.skipped ? ' The recording in progress was kept.' : '');
  } catch (e) {
    result.textContent = 'Deleting failed: ' + e.message;
  } finally {
    yes.disabled = false;
    showDeleteAllConfirm(false);
    result.hidden = false;
    listSignature = '';
    refreshFiles();
  }
});

document.getElementById('forgetWifiBtn').addEventListener('click', () => showForgetWifiConfirm(true));
document.getElementById('forgetWifiCancel').addEventListener('click', () => showForgetWifiConfirm(false));
document.getElementById('forgetWifiYes').addEventListener('click', async () => {
  const yes = document.getElementById('forgetWifiYes');
  yes.disabled = true;
  const result = document.getElementById('forgetWifiResult');
  try {
    await api('/forgetwifi', { method: 'POST' });
    result.textContent = 'WiFi forgotten. The device is restarting; set up the new network on its keyboard.';
  } catch (e) {
    result.textContent = 'Forgetting WiFi failed: ' + e.message;
  } finally {
    yes.disabled = false;
    showForgetWifiConfirm(false);
    result.hidden = false;
  }
});

document.getElementById('pauseBtn').addEventListener('click', async () => {
  const paused = document.getElementById('pauseBtn').textContent === 'Resume';
  await api(paused ? '/resume' : '/pause', { method: 'POST' });
  refreshStatus();
});
function showBypass(on) {
  const btn = document.getElementById('bypassBtn');
  btn.classList.toggle('active', on);
  btn.setAttribute('aria-pressed', on);
  btn.textContent = on ? 'Bypass ON' : 'Bypass Threshold';
}
document.getElementById('bypassBtn').addEventListener('click', async () => {
  const on = !document.getElementById('bypassBtn').classList.contains('active');
  showBypass(on);
  await api('/bypass', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'on=' + (on ? '1' : '0') });
  refreshStatus();
});

for (let db = -50; db <= -10; db += 10) {
  const tick = document.createElement('span');
  tick.textContent = db;
  tick.style.left = ((db - METER_MIN_DB) / (METER_MAX_DB - METER_MIN_DB) * 100) + '%';
  document.getElementById('scale').appendChild(tick);
}

refreshStatus();
refreshFiles();
setInterval(refreshStatus, 500);
setInterval(refreshFiles, 10000);
// iOS suspends a home-screen app in the background; catch up at once on return.
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible') { refreshStatus(); refreshFiles(); }
});
</script>
</body>
</html>
)rawliteral";
