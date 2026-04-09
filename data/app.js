/* ═══════════════════════════════════════════════════════════════
   ESP32 Sensor Dashboard — app.js
   ► POST /treshold  (unchanged from test code)
   ► SSE  /events    (unchanged from test code)
   ► Pure vanilla JS — no external libraries
   ═══════════════════════════════════════════════════════════════ */

// ── Config ──────────────────────────────────────────────────────
const MAX_POINTS   = 60;   // chart history length
const THRESH_WARN  = {     // default warning thresholds (overridden by user)
  current:     10,
  vibration:   5,
  temp:        85
};

// ── State ────────────────────────────────────────────────────────
const series = {
  current:     [],
  vibration:   [],
  temp:        []
};

let userThresh = { ...THRESH_WARN };

// ── Clock ────────────────────────────────────────────────────────
function updateClock() {
  document.getElementById('clock').textContent =
    new Date().toLocaleTimeString('en-GB');
}
setInterval(updateClock, 1000);
updateClock();

// ── POST (unchanged logic from test code) ───────────────────────
async function sendThresh() {
  const res = await fetch('/treshold', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      currentTresh:   parseFloat(document.getElementById('cur').value),
      vibrationTresh: parseFloat(document.getElementById('vib').value),
      tempTresh:      parseFloat(document.getElementById('temp').value)
    })
  });
  const data = await res.json();
  document.getElementById('post-result').textContent = JSON.stringify(data);

  // Cache thresholds for local alerting
  const cv = parseFloat(document.getElementById('cur').value);
  const vv = parseFloat(document.getElementById('vib').value);
  const tv = parseFloat(document.getElementById('temp').value);
  if (!isNaN(cv)) userThresh.current   = cv;
  if (!isNaN(vv)) userThresh.vibration = vv;
  if (!isNaN(tv)) userThresh.temp      = tv;
}

// ── SSE (unchanged logic from test code) ────────────────────────
const evtSource = new EventSource('/events');

evtSource.addEventListener('sensor', (e) => {
  const d = JSON.parse(e.data);
  document.getElementById('cur-val').textContent  = d.current;
  document.getElementById('vib-val').textContent  = d.vibration;
  document.getElementById('temp-val').textContent = d.temp;

  // Extended dashboard updates
  onSensorData(d);
});

evtSource.onopen = () => {
  document.getElementById('conn-dot').className   = 'status-dot connected';
  document.getElementById('conn-label').textContent = 'CONNECTED';
};
evtSource.onerror = () => {
  document.getElementById('conn-dot').className   = 'status-dot error';
  document.getElementById('conn-label').textContent = 'DISCONNECTED';
  addLog('SYSTEM', 'SSE connection lost');
};

// ── On new sensor data ───────────────────────────────────────────
function onSensorData(d) {
  const cur = parseFloat(d.current);
  const vib = parseFloat(d.vibration);
  const tmp = parseFloat(d.temp);

  // Push to chart series
  pushSeries('current',   cur);
  pushSeries('vibration', vib);
  pushSeries('temp',      tmp);

  // Update bar indicators (0–100% of threshold × 2)
  setBar('bar-cur',  cur, userThresh.current   * 2);
  setBar('bar-vib',  vib, userThresh.vibration * 2);
  setBar('bar-temp', tmp, userThresh.temp      * 1.2);

  // Update latest labels in chart headers
  document.getElementById('chart-cur-latest').textContent  = isNaN(cur) ? '--' : cur.toFixed(2) + ' A';
  document.getElementById('chart-vib-latest').textContent  = isNaN(vib) ? '--' : vib.toFixed(2) + ' V';
  document.getElementById('chart-temp-latest').textContent = isNaN(tmp) ? '--' : tmp.toFixed(1) + ' °C';

  // Threshold alerts
  checkThreshold('CURRENT',     cur, userThresh.current,   'card-cur');
  checkThreshold('VIBRATION',   vib, userThresh.vibration, 'card-vib');
  checkThreshold('TEMPERATURE', tmp, userThresh.temp,      'card-temp');

  // Redraw charts
  drawChart('chart-cur',  series.current,   '#00e5ff');
  drawChart('chart-vib',  series.vibration, '#ffe600');
  drawChart('chart-temp', series.temp,      '#ff5c5c');
}

function pushSeries(key, val) {
  series[key].push(isNaN(val) ? null : val);
  if (series[key].length > MAX_POINTS) series[key].shift();
}

function setBar(id, val, max) {
  const pct = Math.min(100, Math.max(0, (val / max) * 100)) || 0;
  document.getElementById(id).style.width = pct + '%';
}

function checkThreshold(label, val, thresh, cardId) {
  if (!isNaN(val) && val > thresh) {
    addLog(label, `Value ${val.toFixed(2)} exceeded threshold ${thresh}`);
    const card = document.getElementById(cardId);
    card.classList.remove('alert');
    void card.offsetWidth; // reflow to restart animation
    card.classList.add('alert');
    card.addEventListener('animationend', () => card.classList.remove('alert'), { once: true });
  }
}

// ── Error Log ────────────────────────────────────────────────────
function addLog(sensor, msg) {
  const list = document.getElementById('log-list');
  const empty = list.querySelector('.log-empty');
  if (empty) empty.remove();

  const now = new Date().toLocaleTimeString('en-GB');
  const entry = document.createElement('div');
  entry.className = 'log-entry';
  entry.innerHTML =
    `<span class="log-time">${now}</span>` +
    `<span class="log-sensor">${sensor}</span>` +
    `<span class="log-msg">${msg}</span>`;

  list.prepend(entry);

  // Cap log at 100 entries
  while (list.children.length > 100) list.removeChild(list.lastChild);
}

function clearLog() {
  const list = document.getElementById('log-list');
  list.innerHTML = '<div class="log-empty">No errors recorded.</div>';
}

// ── Canvas Charts (pure vanilla) ─────────────────────────────────
function drawChart(canvasId, data, color) {
  const canvas = document.getElementById(canvasId);
  if (!canvas) return;
  const dpr = window.devicePixelRatio || 1;
  const W   = canvas.clientWidth  || canvas.parentElement.clientWidth;
  const H   = canvas.height;

  // Resize canvas backing store
  if (canvas.width !== W * dpr || canvas._lastH !== H * dpr) {
    canvas.width  = W * dpr;
    canvas.height = H * dpr;
    canvas._lastH = H * dpr;
  }

  const ctx = canvas.getContext('2d');
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.scale(dpr, dpr);

  const pts    = data.filter(v => v !== null);
  const padX   = 4;
  const padY   = 10;
  const drawW  = W - padX * 2;
  const drawH  = H - padY * 2;

  if (pts.length < 2) {
    // Draw placeholder baseline
    ctx.strokeStyle = 'rgba(255,255,255,.08)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(padX, H / 2);
    ctx.lineTo(W - padX, H / 2);
    ctx.stroke();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    return;
  }

  const min = Math.min(...pts);
  const max = Math.max(...pts);
  const range = max - min || 1;

  const xStep = drawW / (data.length - 1);

  function xPos(i)   { return padX + i * xStep; }
  function yPos(val) { return padY + drawH - ((val - min) / range) * drawH; }

  // ── Grid lines
  ctx.strokeStyle = 'rgba(255,255,255,.04)';
  ctx.lineWidth = 1;
  for (let i = 1; i <= 3; i++) {
    const y = padY + (drawH / 4) * i;
    ctx.beginPath();
    ctx.moveTo(padX, y);
    ctx.lineTo(W - padX, y);
    ctx.stroke();
  }

  // ── Gradient fill
  const grad = ctx.createLinearGradient(0, padY, 0, padY + drawH);
  grad.addColorStop(0,   hexAlpha(color, .35));
  grad.addColorStop(1,   hexAlpha(color, .00));

  ctx.beginPath();
  data.forEach((v, i) => {
    if (v === null) return;
    i === 0 || data[i - 1] === null
      ? ctx.moveTo(xPos(i), yPos(v))
      : ctx.lineTo(xPos(i), yPos(v));
  });
  // close path for fill
  const last = data.length - 1;
  ctx.lineTo(xPos(last), padY + drawH);
  ctx.lineTo(xPos(0), padY + drawH);
  ctx.closePath();
  ctx.fillStyle = grad;
  ctx.fill();

  // ── Line
  ctx.beginPath();
  ctx.lineWidth = 1.5;
  ctx.strokeStyle = color;
  ctx.lineJoin = 'round';
  data.forEach((v, i) => {
    if (v === null) return;
    i === 0 || data[i - 1] === null
      ? ctx.moveTo(xPos(i), yPos(v))
      : ctx.lineTo(xPos(i), yPos(v));
  });
  ctx.stroke();

  // ── Latest dot
  const lastVal = data[data.length - 1];
  if (lastVal !== null) {
    ctx.beginPath();
    ctx.arc(xPos(data.length - 1), yPos(lastVal), 3, 0, Math.PI * 2);
    ctx.fillStyle = color;
    ctx.fill();
    ctx.strokeStyle = '#0d0f12';
    ctx.lineWidth = 1.5;
    ctx.stroke();
  }

  ctx.setTransform(1, 0, 0, 1, 0, 0);
}

function hexAlpha(hex, alpha) {
  const r = parseInt(hex.slice(1, 3), 16);
  const g = parseInt(hex.slice(3, 5), 16);
  const b = parseInt(hex.slice(5, 7), 16);
  return `rgba(${r},${g},${b},${alpha})`;
}

// ── Init charts with blank state ─────────────────────────────────
window.addEventListener('load', () => {
  drawChart('chart-cur',  [], '#00e5ff');
  drawChart('chart-vib',  [], '#ffe600');
  drawChart('chart-temp', [], '#ff5c5c');
});

window.addEventListener('resize', () => {
  drawChart('chart-cur',  series.current,   '#00e5ff');
  drawChart('chart-vib',  series.vibration, '#ffe600');
  drawChart('chart-temp', series.temp,      '#ff5c5c');
});
