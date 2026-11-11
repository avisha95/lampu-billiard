const NUM_TABLES = 20;
const DEFAULT_RATE = 30000;
const POLL_MS = 1000;
const API_STORAGE_KEY = 'billiardApiBase';

function normalizeApiBase(value) {
  let url = String(value || '').trim();
  if (!url) return '';
  if (!/^https?:\/\//i.test(url)) url = `http://${url}`;
  return url.replace(/\/+$/, '');
}

const queryApi = new URLSearchParams(window.location.search).get('api');
let apiBase = normalizeApiBase(queryApi || localStorage.getItem(API_STORAGE_KEY) || '');
if (queryApi) localStorage.setItem(API_STORAGE_KEY, apiBase);

const state = {
  demo: false,
  mode: null,
  megaOnline: null,
  deviceId: null,
  deviceOnline: null,
  rate: DEFAULT_RATE,
  online: false,
  filter: 'all',
  relays: Array.from({ length: NUM_TABLES }, () => ({ on: false, since: null, pending: false, fault: false })),
};

let commandSeq = 0;

function makeCommandId() {
  commandSeq += 1;
  return `CMD-${Date.now()}-${commandSeq}`;
}

const els = {
  grid: document.getElementById('grid'),
  log: document.getElementById('log'),
  connBadge: document.getElementById('connBadge'),
  availBadge: document.getElementById('availBadge'),
  idBadge: document.getElementById('idBadge'),
  modeBadge: document.getElementById('modeBadge'),
  megaBadge: document.getElementById('megaBadge'),
  modeNotice: document.getElementById('modeNotice'),
  statActive: document.getElementById('statActive'),
  statFree: document.getElementById('statFree'),
  statTime: document.getElementById('statTime'),
  statCost: document.getElementById('statCost'),
  statRate: document.getElementById('statRate'),
  rateInput: document.getElementById('rateInput'),
  allOn: document.getElementById('allOn'),
  allOff: document.getElementById('allOff'),
  demoBtn: document.getElementById('demoBtn'),
  countAll: document.getElementById('countAll'),
  countOn: document.getElementById('countOn'),
  countOff: document.getElementById('countOff'),
  lastUpdate: document.getElementById('lastUpdate'),
  apiForm: document.getElementById('apiForm'),
  apiInput: document.getElementById('apiBaseInput'),
};

const currency = new Intl.NumberFormat('id-ID', {
  style: 'currency',
  currency: 'IDR',
  maximumFractionDigits: 0,
});

function log(message, type) {
  const line = document.createElement('div');
  line.className = 'logline' + (type ? ' ' + type : '');
  line.textContent = `${new Date().toLocaleTimeString('id-ID')}  ${message}`;
  els.log.appendChild(line);
  while (els.log.childElementCount > 200) {
    els.log.removeChild(els.log.firstChild);
  }
  els.log.scrollTop = els.log.scrollHeight;
}

function setBadge(el, text, cls) {
  el.textContent = text;
  el.className = 'badge' + (cls ? ' ' + cls : '');
}

function formatDuration(ms) {
  const total = Math.max(0, Math.floor(ms / 1000));
  const h = String(Math.floor(total / 3600)).padStart(2, '0');
  const m = String(Math.floor((total % 3600) / 60)).padStart(2, '0');
  const s = String(total % 60).padStart(2, '0');
  return `${h}:${m}:${s}`;
}

function formatClock(ts) {
  return new Date(ts).toLocaleTimeString('id-ID', {
    hour: '2-digit',
    minute: '2-digit',
  });
}

function calcTotals() {
  const now = Date.now();
  let active = 0;
  let totalMs = 0;
  let cost = 0;
  for (const relay of state.relays) {
    if (relay.on && relay.since) {
      const ms = now - relay.since;
      active += 1;
      totalMs += ms;
      cost += (state.rate * ms) / 3600000;
    }
  }
  return { active, totalMs, cost };
}

function buildCards() {
  els.grid.innerHTML = '';
  for (let ch = 1; ch <= NUM_TABLES; ch += 1) {
    const card = document.createElement('article');
    card.className = 'card';
    card.id = `card-${ch}`;
    card.dataset.ch = String(ch);
    card.innerHTML = `
      <div class="card-head">
        <div class="table-id">
          <span class="table-label">MEJA</span>
          <span class="table-no">${String(ch).padStart(2, '0')}</span>
        </div>
        <span class="state-chip">MATI</span>
      </div>
      <div class="lamp-row">
        <span class="bulb"></span>
        <span class="lamp-text">Lampu mati</span>
      </div>
      <dl class="metrics">
        <div class="metric metric-duration">
          <dt>Durasi menyala</dt>
          <dd class="timer">00:00:00</dd>
        </div>
        <div class="metric">
          <dt>Mulai</dt>
          <dd class="started">—</dd>
        </div>
        <div class="metric">
          <dt>Tarif / jam</dt>
          <dd class="rate">${currency.format(state.rate)}</dd>
        </div>
        <div class="metric metric-cost">
          <dt>Estimasi biaya</dt>
          <dd class="cost">${currency.format(0)}</dd>
        </div>
      </dl>
      <div class="card-actions">
        <button class="btn on" type="button" data-ch="${ch}" data-action="ON">Nyalakan</button>
        <button class="btn off" type="button" data-ch="${ch}" data-action="OFF">Matikan</button>
      </div>
    `;
    els.grid.appendChild(card);
  }

  els.grid.querySelectorAll('button[data-action]').forEach((btn) => {
    btn.addEventListener('click', () => {
      sendCommand({
        channel: Number(btn.dataset.ch),
        action: btn.dataset.action,
      });
    });
  });
}

function controlsAllowed() {
  if (state.demo) return true;
  return state.online && state.deviceOnline !== false &&
         state.mode === 'AUTO' && state.megaOnline === true;
}

function updateCard(ch) {
  const card = document.getElementById(`card-${ch}`);
  if (!card) return;
  const relay = state.relays[ch - 1];
  const chip = card.querySelector('.state-chip');
  card.classList.toggle('on', relay.on);
  chip.classList.toggle('pending', relay.pending);
  chip.classList.toggle('fault', relay.fault);
  chip.textContent = relay.fault ? 'GAGAL' : relay.pending ? 'MENUNGGU' : relay.on ? 'NYALA' : 'MATI';
  card.querySelector('.lamp-text').textContent = relay.on ? 'Lampu menyala' : 'Lampu mati';

  const timerEl = card.querySelector('.timer');
  const startedEl = card.querySelector('.started');
  const rateEl = card.querySelector('.rate');
  const costEl = card.querySelector('.cost');
  const btnOn = card.querySelector('.btn.on');
  const btnOff = card.querySelector('.btn.off');

  const allow = controlsAllowed();
  rateEl.textContent = currency.format(state.rate);
  btnOn.disabled = !allow || relay.on || relay.pending;
  btnOff.disabled = !allow || !relay.on || relay.pending;

  if (relay.on && relay.since) {
    const ms = Date.now() - relay.since;
    timerEl.textContent = formatDuration(ms);
    startedEl.textContent = formatClock(relay.since);
    costEl.textContent = currency.format((state.rate * ms) / 3600000);
  } else {
    timerEl.textContent = '00:00:00';
    startedEl.textContent = '—';
    costEl.textContent = currency.format(0);
  }
}

function applyFilter() {
  for (let ch = 1; ch <= NUM_TABLES; ch += 1) {
    const card = document.getElementById(`card-${ch}`);
    if (!card) continue;
    const on = state.relays[ch - 1].on;
    const show =
      state.filter === 'all' ||
      (state.filter === 'on' && on) ||
      (state.filter === 'off' && !on);
    card.classList.toggle('hidden', !show);
  }
}

function updateAll() {
  for (let ch = 1; ch <= NUM_TABLES; ch += 1) {
    updateCard(ch);
  }
  const { active, totalMs, cost } = calcTotals();
  els.statActive.textContent = `${active} / ${NUM_TABLES}`;
  els.statFree.textContent = `${NUM_TABLES - active} meja tersedia`;
  els.statTime.textContent = formatDuration(totalMs);
  els.statCost.textContent = currency.format(cost);
  els.statRate.textContent = `Tarif ${currency.format(state.rate)} / jam`;
  els.countAll.textContent = String(NUM_TABLES);
  els.countOn.textContent = String(active);
  els.countOff.textContent = String(NUM_TABLES - active);

  const allow = controlsAllowed();
  if (els.allOn) els.allOn.disabled = !allow || active === NUM_TABLES;
  if (els.allOff) els.allOff.disabled = !allow || active === 0;

  applyFilter();
}

function updateModeNotice() {
  let text = '';

  if (!state.demo) {
    if (state.mode === 'MANUAL') {
      text = 'Mode MANUAL aktif — lampu dikontrol dari tombol panel fisik. Perintah dari web ditolak.';
    } else if (!state.mode || state.mode === 'UNKNOWN') {
      text = 'Status mode belum diketahui — perintah web dinonaktifkan sampai ESP32/Mega terhubung.';
    } else if (state.megaOnline !== true) {
      text = 'Mega offline — status mode tidak dapat dipastikan. Perintah web dinonaktifkan.';
    }
  }

  if (!text) {
    els.modeNotice.classList.add('hidden');
    return;
  }
  els.modeNotice.textContent = text;
  els.modeNotice.classList.remove('hidden');
}

function applyStatus(data) {
  if (typeof data.device_id === 'string' && data.device_id) {
    state.deviceId = data.device_id;
    if (els.idBadge) setBadge(els.idBadge, `ID: ${data.device_id}`, '');
  }
  if (typeof data.online === 'boolean') {
    state.deviceOnline = data.online;
    setBadge(els.availBadge, `Device: ${data.online ? 'online' : 'offline'}`, data.online ? 'ok' : 'bad');
  }
  if (typeof data.mode === 'string') {
    state.mode = data.mode;
    const auto = data.mode === 'AUTO';
    const cls = auto ? 'ok' : data.mode === 'MANUAL' ? 'warn' : 'bad';
    setBadge(els.modeBadge, `Mode: ${data.mode}`, cls);
  }
  if (typeof data.mega_online === 'boolean') {
    state.megaOnline = data.mega_online;
    setBadge(els.megaBadge, `Mega: ${data.mega_online ? 'online' : 'offline'}`, data.mega_online ? 'ok' : 'bad');
  }
  if (Array.isArray(data.relays)) {
    for (const item of data.relays) {
      const ch = Number(item.channel);
      if (!Number.isInteger(ch) || ch < 1 || ch > NUM_TABLES) continue;
      const on = String(item.state).toUpperCase() === 'ON';
      const relay = state.relays[ch - 1];
      if (on !== relay.on) {
        relay.on = on;
        relay.since = on ? Date.now() : null;
      }
      relay.pending = Boolean(item.pending);
      relay.fault = Boolean(item.fault);
    }
  }
  updateModeNotice();
  refreshDeviceLabel();
  updateAll();
}

let pollInFlight = false;
let pollFailures = 0;

async function poll() {
  if (state.demo || pollInFlight) return;

  if (!apiBase) {
    state.online = false;
    state.mode = null;
    state.megaOnline = null;
    state.deviceId = null;
    state.deviceOnline = null;
    setBadge(els.connBadge, 'API: belum diatur', 'warn');
    setBadge(els.availBadge, 'Device: -', '');
    if (els.idBadge) setBadge(els.idBadge, 'ID: -', '');
    setBadge(els.megaBadge, 'Mega: -', '');
    setBadge(els.modeBadge, 'Mode: -', '');
    updateModeNotice();
    updateAll();
    return;
  }

  pollInFlight = true;
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 3000);

  try {
    const res = await fetch(`${apiBase}/api/status`, { signal: controller.signal, cache: 'no-store' });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();

    pollFailures = 0;
    if (!state.online) log('Terhubung ke ESP32', 'ok');
    state.online = true;
    setBadge(els.connBadge, 'Server: terhubung', 'ok');
    setBadge(els.availBadge, 'Device: online', 'ok');
    els.lastUpdate.textContent = `Update ${new Date().toLocaleTimeString('id-ID')}`;
    applyStatus(data);
  } catch {
    pollFailures += 1;
    if (pollFailures >= 2) {
      if (state.online) log('Koneksi ke ESP32 terputus', 'bad');
      state.online = false;
      setBadge(els.connBadge, 'Server: offline', 'bad');
      setBadge(els.availBadge, 'Device: offline', 'bad');
      setBadge(els.megaBadge, 'Mega: -', '');
      updateModeNotice();
      updateAll();
    } else {
      setBadge(els.connBadge, 'Server: lambat', 'warn');
    }
  } finally {
    clearTimeout(timer);
    pollInFlight = false;
  }
}

async function sendCommand({ channel, channels, action }) {
  if (state.demo) {
    simulateCommand({ channel, channels, action });
    return;
  }

  if (!apiBase) {
    log('API ESP32 belum diatur — isi IP ESP32 dulu', 'warn');
    return;
  }

  if (!controlsAllowed()) {
    log('Kontrol dinonaktifkan: mode bukan AUTO atau Mega/ESP32 offline', 'warn');
    return;
  }

  const payload = { command_id: makeCommandId(), action };
  if (channels) payload.channels = channels;
  else if (channel) payload.channel = channel;

  const target = channel ? ` meja ${channel}` : channels ? ` ${channels.length} meja` : '';

  try {
    const res = await fetch(`${apiBase}/api/relay`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    const data = await res.json().catch(() => ({}));

    if (data.ok) {
      log(`OK: ${action}${target}`, 'ok');
    } else {
      log(`Ditolak: ${action}${target} (${data.reason || 'error'})`, 'bad');
    }
  } catch {
    log('Gagal mengirim perintah ke ESP32', 'bad');
  }

  poll();
}

function simulateCommand({ channel, channels, action }) {
  const on = action === 'ON';

  if (action === 'ALL_OFF') {
    state.relays.forEach((relay) => {
      relay.on = false;
      relay.since = null;
      relay.pending = false;
      relay.fault = false;
    });
  } else if (action === 'ON' || action === 'OFF') {
    const list = channels && channels.length ? channels : channel ? [channel] : [];
    if (!list.length) {
      log('Demo: channel tidak ada', 'bad');
      return;
    }
    for (const ch of list) {
      if (!Number.isInteger(ch) || ch < 1 || ch > NUM_TABLES) continue;
      const relay = state.relays[ch - 1];
      relay.pending = false;
      relay.fault = false;
      if (relay.on !== on) {
        relay.on = on;
        relay.since = on ? Date.now() : null;
      }
    }
  } else {
    log(`Demo: aksi "${action}" tidak dikenal`, 'bad');
    return;
  }

  log(`Demo: ${action}${channel ? ' meja ' + channel : ''}`, 'out');
  updateAll();
}

function setDemo(on) {
  state.demo = on;
  els.demoBtn.textContent = `Mode Demo: ${on ? 'ON' : 'OFF'}`;
  els.demoBtn.classList.toggle('active', on);

  if (on) {
    state.online = true;
    state.mode = 'AUTO';
    state.megaOnline = true;
    state.deviceId = 'DEMO';
    state.deviceOnline = true;
    setBadge(els.connBadge, 'DEMO: simulasi lokal', 'warn');
    setBadge(els.availBadge, 'Device: online', 'ok');
    if (els.idBadge) setBadge(els.idBadge, 'ID: DEMO', '');
    setBadge(els.modeBadge, 'Mode: AUTO', 'ok');
    setBadge(els.megaBadge, 'Mega: online', 'ok');
    refreshDeviceLabel();
    updateModeNotice();
    updateAll();
    log('Mode demo aktif — kontrol disimulasikan lokal', 'warn');
  } else {
    state.online = false;
    state.mode = null;
    state.megaOnline = null;
    state.deviceId = null;
    state.deviceOnline = null;
    setBadge(els.connBadge, 'Server: menghubungkan…', 'pending');
    setBadge(els.availBadge, 'Device: offline', 'bad');
    if (els.idBadge) setBadge(els.idBadge, 'ID: -', '');
    setBadge(els.modeBadge, 'Mode: -', '');
    setBadge(els.megaBadge, 'Mega: -', '');
    refreshDeviceLabel();
    updateModeNotice();
    updateAll();
    log('Mode demo nonaktif — polling ESP32', 'warn');
    poll();
  }
}

document.querySelectorAll('.filter').forEach((btn) => {
  btn.addEventListener('click', () => {
    state.filter = btn.dataset.filter;
    document.querySelectorAll('.filter').forEach((other) => {
      other.classList.toggle('active', other === btn);
    });
    applyFilter();
  });
});

const deviceUrl = document.getElementById('deviceUrl');
function refreshDeviceLabel() {
  if (!deviceUrl) return;
  if (state.demo) {
    deviceUrl.textContent = 'DEMO · simulasi lokal';
    return;
  }
  const idPart = state.deviceId ? ` · ${state.deviceId}` : '';
  deviceUrl.textContent = apiBase ? `API ${apiBase}${idPart}` : 'API ESP32 belum diatur';
}
refreshDeviceLabel();

if (els.apiInput) els.apiInput.value = apiBase;
if (els.apiForm) {
  els.apiForm.addEventListener('submit', (event) => {
    event.preventDefault();
    const next = normalizeApiBase(els.apiInput ? els.apiInput.value : '');
    apiBase = next;
    if (els.apiInput) els.apiInput.value = next;
    if (next) localStorage.setItem(API_STORAGE_KEY, next);
    else localStorage.removeItem(API_STORAGE_KEY);
    state.online = false;
    state.mode = null;
    state.megaOnline = null;
    state.deviceId = null;
    state.deviceOnline = null;
    if (els.idBadge) setBadge(els.idBadge, 'ID: -', '');
    setBadge(els.connBadge, next ? 'Server: menghubungkan…' : 'API: belum diatur', next ? 'pending' : 'warn');
    setBadge(els.modeBadge, 'Mode: -', '');
    setBadge(els.megaBadge, 'Mega: -', '');
    refreshDeviceLabel();
    updateModeNotice();
    updateAll();
    log(next ? `API ESP32 diset ke ${next}` : 'API ESP32 dikosongkan', 'out');
    poll();
  });
}

els.rateInput.value = String(state.rate);
els.rateInput.addEventListener('input', () => {
  const value = Number(els.rateInput.value);
  state.rate = Number.isFinite(value) && value >= 0 ? value : 0;
  updateAll();
});

if (els.allOn) {
  els.allOn.addEventListener('click', () => {
    sendCommand({
      channels: Array.from({ length: NUM_TABLES }, (_, i) => i + 1),
      action: 'ON',
    });
  });
}
els.allOff.addEventListener('click', () => sendCommand({ action: 'ALL_OFF' }));
els.demoBtn.addEventListener('click', () => setDemo(!state.demo));

buildCards();
updateAll();
poll();
setInterval(poll, POLL_MS);
setInterval(updateAll, 1000);
