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
  .bar { flex: 1; height: 10px; background: #333; border-radius: 5px; overflow: hidden; }
  .bar > div { height: 100%; background: #4caf50; width: 0%; }
  button { background: #333; color: #eee; border: 1px solid #555; border-radius: 6px; padding: 6px 12px; cursor: pointer; }
  button:hover { background: #444; }
  button.danger { border-color: #a33; color: #f88; }
  input[type=range] { flex: 1; }
  .file { display: flex; align-items: center; gap: 6px; padding: 6px 0; border-bottom: 1px solid #333; font-size: 0.9em; }
  .file .name { flex: 1; word-break: break-all; }
  audio { height: 28px; }
  .muted { color: #888; font-size: 0.85em; }
</style>
</head>
<body>
  <h1>Audio Signal Monitor</h1>

  <div class="row">
    <strong id="state">-</strong>
    <div class="bar"><div id="level"></div></div>
    <span id="ip" class="muted"></span>
  </div>
  <div class="row muted"><span id="space"></span></div>

  <div class="row">
    <label for="threshold">Threshold</label>
    <input type="range" id="threshold" min="0" max="0.05" step="0.0005">
    <span id="thresholdVal" class="muted"></span>
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

async function refreshStatus() {
  const s = await (await api('/status')).json();
  document.getElementById('state').textContent =
    s.paused ? 'PAUSED' : (s.isRecording ? '● REC' : 'listening');
  document.getElementById('level').style.width = Math.min(100, s.liveRms / 0.05 * 100) + '%';
  document.getElementById('ip').textContent = s.ip || '';
  const freeMB = (s.freeBytes / 1048576).toFixed(0);
  const totalMB = (s.totalBytes / 1048576).toFixed(0);
  document.getElementById('space').textContent = freeMB + ' MB free of ' + totalMB + ' MB';
  document.getElementById('threshold').value = s.threshold;
  document.getElementById('thresholdVal').textContent = s.threshold.toFixed(4);
  document.getElementById('stealth').checked = s.stealthMode;
  document.getElementById('pauseBtn').textContent = s.paused ? 'Resume' : 'Pause';
}

async function refreshFiles() {
  const files = await (await api('/files')).json();
  const el = document.getElementById('files');
  el.innerHTML = '';
  files.sort((a, b) => b.name.localeCompare(a.name));
  for (const f of files) {
    const row = document.createElement('div');
    row.className = 'file';
    const sizeMB = (f.size / 1048576).toFixed(2);
    row.innerHTML =
      '<span class="name">' + f.name + ' <span class="muted">(' + sizeMB + ' MB)</span></span>' +
      '<audio controls src="/stream?name=' + encodeURIComponent(f.name) + '"></audio>' +
      '<a href="/download?name=' + encodeURIComponent(f.name) + '"><button>Download</button></a>' +
      '<button class="danger" data-name="' + f.name + '">Delete</button>';
    row.querySelector('.danger').addEventListener('click', async (e) => {
      if (!confirm('Delete ' + f.name + '?')) return;
      await api('/delete?name=' + encodeURIComponent(f.name), { method: 'POST' });
      refreshFiles();
    });
    el.appendChild(row);
  }
}

document.getElementById('threshold').addEventListener('change', async (e) => {
  await api('/threshold', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'value=' + e.target.value });
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

refreshStatus();
refreshFiles();
setInterval(refreshStatus, 1500);
setInterval(refreshFiles, 10000);
</script>
</body>
</html>
)rawliteral";
