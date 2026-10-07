// ui.js — PWA entry point: live dashboard, cell register and history views.
//
// Consumes `Store` (state.js) for all synced data and drives the views via the
// REST endpoints. No framework, no build step — plain ES modules.

import { Store } from './state.js';
import { startSync } from './sync.js';

// --- constants ---------------------------------------------------------------

const MODES = [
  ['charge', 'Charge'],
  ['discharge', 'Discharge'],
  ['storage', 'Storage'],
  ['cycle', 'Cycle'],
  ['refresh', 'Refresh'],
  ['break_in', 'Break-in'],
];

const CHEMISTRIES = ['Li-Ion', 'Li-Ion HV', 'LiFePO4', 'NiMH', 'NiCd', 'Eneloop', 'NiZn', 'RAM', 'LTO', 'Na-Ion'];

const CHEMISTRY_CODES = {
  'li-ion': 0, 'li-ion hv': 1, 'lifepo4': 2, 'nimh': 3, 'nicd': 4,
  'eneloop': 5, 'nizn': 6, 'ram': 7, 'lto': 8, 'na-ion': 9,
};

// --- helpers -----------------------------------------------------------------

function esc(s) {
  return String(s == null ? '' : s).replace(/[&<>"']/g, (c) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
  }[c]));
}

function genId(prefix) {
  return `${prefix}-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 7)}`;
}

function formatTime(ts) {
  if (!ts) return '\u2014';
  return new Date(ts * 1000).toLocaleString();
}

function chemistryToCode(chem) {
  const key = String(chem || '').toLowerCase();
  return CHEMISTRY_CODES[key] != null ? CHEMISTRY_CODES[key] : 0;
}

function typeLabel(type) {
  if (!type) return '(ohne Typ)';
  return `${type.manufacturer || ''} ${type.model || ''}`.trim() || type.id;
}

async function api(path, method = 'GET', body) {
  const opts = { method };
  if (body !== undefined) {
    opts.headers = { 'Content-Type': 'application/json' };
    opts.body = JSON.stringify(body);
  }
  const resp = await fetch(path, opts);
  return resp.json().catch(() => ({}));
}

function sparkline(points) {
  const vs = points.map((p) => p[1]);   // voltage_mv
  if (vs.length < 2) return '';
  const max = Math.max(...vs);
  const min = Math.min(...vs);
  const range = max - min || 1;
  const w = 110;
  const h = 30;
  const coords = vs
    .map((v, i) => `${(i / (vs.length - 1) * w).toFixed(1)},${(h - ((v - min) / range) * h).toFixed(1)}`)
    .join(' ');
  return `<svg width="${w}" height="${h}" viewBox="0 0 ${w} ${h}" preserveAspectRatio="none">` +
    `<polyline points="${coords}" fill="none" stroke="#1a73e8" stroke-width="1.5"/></svg>`;
}

// --- dashboard ---------------------------------------------------------------

async function renderDashboard() {
  const [status, live] = await Promise.all([api('/api/status'), api('/api/live')]);
  const slotsEl = document.getElementById('slots');
  slotsEl.innerHTML = '';

  const ble = document.getElementById('ble-state');
  ble.textContent = status.connected ? 'Ladeger\u00e4t verbunden' : 'Ladeger\u00e4t nicht verbunden';

  for (let i = 0; i < 4; i++) {
    const s = (status.slots || []).find((x) => x.slot === i + 1);
    const lp = (live.slots || []).find((x) => x.slot === i + 1);
    slotsEl.appendChild(renderSlot(i + 1, s, lp ? lp.points || [] : []));
  }
}

function renderSlot(num, s, points) {
  const el = document.createElement('div');
  const occupied = s && s.valid && s.status !== 'Standby';

  if (!occupied) {
    el.className = 'slot free';
    el.innerHTML = `<h3>Slot ${num}</h3><span class="muted">frei</span><button>Belegen</button>`;
    el.addEventListener('click', () => openSlotModal(num));
    return el;
  }

  el.className = 'slot';
  el.innerHTML = `
    <h3>Slot ${num}</h3>
    <div class="status ${esc(s.status)}">${esc(s.status)}</div>
    <div class="row"><span class="k">Modus</span><span>${esc(s.mode)}</span></div>
    <div class="row"><span class="k">Spannung</span><span>${(s.voltageV || 0).toFixed(3)} V</span></div>
    <div class="row"><span class="k">Strom</span><span>${(s.currentA || 0).toFixed(3)} A</span></div>
    <div class="row"><span class="k">Kapazit\u00e4t</span><span>${s.capacityMah ?? 0} mAh</span></div>
    <div class="row"><span class="k">Temp</span><span>${(s.temperatureC || 0).toFixed(1)} \u00b0C</span></div>
    <div class="row"><span class="k">IR</span><span>${s.internalResistanceMOhm ?? 0} m\u03a9</span></div>
    <div class="row"><span class="k">Zeit</span><span>${fmtElapsed(s.elapsedSeconds)}</span></div>
    ${sparkline(points)}`;
  return el;
}

function fmtElapsed(sec) {
  if (sec == null) return '\u2014';
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;
  return `${h}h ${m}m ${s}s`;
}

// --- register ----------------------------------------------------------------

function renderRegister() {
  renderTypes();
  renderCells();
}

function renderTypes() {
  const body = document.getElementById('types-body');
  body.innerHTML = '';
  for (const t of Store.typesList) {
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td>${esc(t.manufacturer)}</td>
      <td>${esc(t.model)}</td>
      <td>${esc(t.chemistry)}</td>
      <td>${t.nominal_capacity_mah ?? 0} mAh</td>
      <td>${(t.nominal_voltage_mv ?? 0) / 1000} V</td>
      <td class="actions">
        <button data-action="edit" data-id="${esc(t.id)}">\u270e</button>
        <button data-action="delete" data-id="${esc(t.id)}" class="danger">\u2715</button>
      </td>`;
    body.appendChild(tr);
  }
}

function renderCells() {
  const body = document.getElementById('cells-body');
  body.innerHTML = '';
  for (const c of Store.cellsList) {
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td>${c.number ?? ''}</td>
      <td>${esc(c.label)}</td>
      <td>${esc(typeLabel(Store.types.get(c.cell_type_id)))}</td>
      <td><span class="badge ${esc(c.status)}">${esc(c.status)}</span></td>
      <td class="actions">
        <button data-action="edit" data-id="${esc(c.id)}">\u270e</button>
        <button data-action="delete" data-id="${esc(c.id)}" class="danger">\u2715</button>
      </td>`;
    body.appendChild(tr);
  }
}

// --- history -----------------------------------------------------------------

function renderHistory() {
  const body = document.getElementById('history-body');
  body.innerHTML = '';
  for (const h of Store.history.slice().reverse()) {
    const cell = Store.cells.get(h.cell_id);
    const label = cell ? (cell.label || `#${cell.number}`) : h.cell_id;
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td>${formatTime(h.timestamp_s)}</td>
      <td>${esc(label)}</td>
      <td>${esc(h.action)}</td>
      <td>${h.capacity_mah ?? 0} mAh</td>
      <td>${h.energy_mwh ?? 0} mWh</td>
      <td>${h.ir_mohm ?? 0} m\u03a9</td>`;
    body.appendChild(tr);
  }
}

// --- slot modal --------------------------------------------------------------

let activeSlot = 0;

function openSlotModal(slot) {
  activeSlot = slot;
  document.getElementById('slot-modal-num').textContent = slot;

  const cellSelect = document.getElementById('slot-modal-cell');
  cellSelect.innerHTML = '';
  const activeCells = Store.cellsList.filter((c) => c.status === 'active');
  for (const c of activeCells) {
    const opt = document.createElement('option');
    opt.value = c.id;
    opt.textContent = c.label ? `${c.label} (#${c.number})` : `#${c.number}`;
    cellSelect.appendChild(opt);
  }

  const modeSelect = document.getElementById('slot-modal-mode');
  modeSelect.innerHTML = '';
  for (const [value, label] of MODES) {
    const opt = document.createElement('option');
    opt.value = value;
    opt.textContent = label;
    modeSelect.appendChild(opt);
  }

  document.getElementById('slot-modal-start').disabled = activeCells.length === 0;
  document.getElementById('slot-modal').hidden = false;
}

function closeSlotModal() {
  document.getElementById('slot-modal').hidden = true;
  activeSlot = 0;
}

async function startCharge() {
  const cellId = document.getElementById('slot-modal-cell').value;
  const mode = document.getElementById('slot-modal-mode').value;
  if (!cellId || !activeSlot) return;

  const cell = Store.cells.get(cellId);
  const type = cell ? Store.types.get(cell.cell_type_id) : null;
  const chemistry = type ? chemistryToCode(type.chemistry) : 0;

  closeSlotModal();
  await api('/api/charge', 'POST', { cell_id: cellId, slot: activeSlot, mode, chemistry });
}

// --- entity modal (cell / type CRUD) -----------------------------------------

let currentEntity = null;     // 'type' | 'cell'
let currentEntityId = null;   // existing id, or null for "add"

function formField(name, label, value = '', type = 'text') {
  const labelEl = document.createElement('label');
  labelEl.appendChild(document.createTextNode(label));
  const input = document.createElement('input');
  input.name = name;
  input.type = type;
  input.value = value;
  labelEl.appendChild(input);
  return labelEl;
}

function formSelect(name, label, options) {
  const labelEl = document.createElement('label');
  labelEl.appendChild(document.createTextNode(label));
  const select = document.createElement('select');
  select.name = name;
  for (const [value, text] of options) {
    const opt = document.createElement('option');
    opt.value = value;
    opt.textContent = text;
    select.appendChild(opt);
  }
  labelEl.appendChild(select);
  return labelEl;
}

function hiddenField(name, value) {
  const input = document.createElement('input');
  input.type = 'hidden';
  input.name = name;
  input.value = value;
  return input;
}

function openEntityModal(entity, existing = null) {
  currentEntity = entity;
  currentEntityId = existing ? existing.id : null;

  const title = document.getElementById('entity-modal-title');
  const form = document.getElementById('entity-form');
  form.innerHTML = '';

  if (entity === 'type') {
    title.textContent = existing ? 'Zelltyp bearbeiten' : 'Zelltyp anlegen';
    form.appendChild(hiddenField('id', existing ? existing.id : genId('type')));
    form.appendChild(formField('manufacturer', 'Hersteller', existing?.manufacturer || ''));
    form.appendChild(formField('model', 'Modell', existing?.model || ''));
    form.appendChild(formSelect('chemistry', 'Chemie', CHEMISTRIES.map((c) => [c, c])));
    form.appendChild(formField('nominal_capacity_mah', 'Nennkapazit\u00e4t (mAh)', existing?.nominal_capacity_mah ?? 3000, 'number'));
    form.appendChild(formField('nominal_voltage_mv', 'Nennspannung (mV)', existing?.nominal_voltage_mv ?? 3700, 'number'));
    form.querySelector('[name="chemistry"]').value = existing?.chemistry || 'Li-Ion';
  } else {
    title.textContent = existing ? 'Zelle bearbeiten' : 'Zelle anlegen';
    form.appendChild(hiddenField('id', existing ? existing.id : genId('cell')));
    const types = Store.typesList.map((t) => [t.id, typeLabel(t)]);
    form.appendChild(formSelect('cell_type_id', 'Zelltyp', types));
    form.appendChild(formField('number', 'Nummer', existing?.number ?? '', 'number'));
    form.appendChild(formField('label', 'Label', existing?.label || ''));
    form.appendChild(formSelect('status', 'Status', [['active', 'Aktiv'], ['retired', 'Ausgemustert']]));
    if (existing) {
      form.querySelector('[name="cell_type_id"]').value = existing.cell_type_id || '';
      form.querySelector('[name="status"]').value = existing.status || 'active';
    }
  }

  document.getElementById('entity-modal').hidden = false;
}

function closeEntityModal() {
  document.getElementById('entity-modal').hidden = true;
  currentEntity = null;
  currentEntityId = null;
}

async function saveEntity() {
  const entity = currentEntity;
  const entityId = currentEntityId;
  const form = document.getElementById('entity-form');
  const data = Object.fromEntries(new FormData(form).entries());
  for (const k of ['nominal_capacity_mah', 'nominal_voltage_mv', 'number']) {
    if (data[k] !== undefined) data[k] = parseInt(data[k], 10) || 0;
  }

  const collection = entity === 'type' ? '/api/v1/cell_types' : '/api/v1/cells';
  const url = entityId ? `${collection}/${encodeURIComponent(data.id)}` : collection;
  const method = entityId ? 'PUT' : 'POST';

  closeEntityModal();
  await api(url, method, data);
}

async function deleteEntity(entity, id) {
  if (!confirm('Wirklich l\u00f6schen?')) return;
  const collection = entity === 'type' ? '/api/v1/cell_types' : '/api/v1/cells';
  await api(`${collection}/${encodeURIComponent(id)}`, 'DELETE');
}

// --- nav ---------------------------------------------------------------------

function switchView(name) {
  for (const view of document.querySelectorAll('.view')) {
    view.hidden = view.id !== `view-${name}`;
  }
  for (const btn of document.querySelectorAll('nav button')) {
    btn.classList.toggle('active', btn.dataset.view === name);
  }
  if (name === 'register') renderRegister();
  if (name === 'history') renderHistory();
}

// --- init --------------------------------------------------------------------

function init() {
  document.querySelectorAll('nav button').forEach((b) =>
    b.addEventListener('click', () => switchView(b.dataset.view)));

  // dashboard polling (live data is transient, not in the delta log)
  renderDashboard();
  setInterval(renderDashboard, 2000);

  // delta sync -> re-render register + history
  startSync(2000, () => { renderRegister(); renderHistory(); });

  // slot modal
  document.getElementById('slot-modal-cancel').addEventListener('click', closeSlotModal);
  document.getElementById('slot-modal-start').addEventListener('click', startCharge);

  // entity modal
  document.getElementById('entity-modal-cancel').addEventListener('click', closeEntityModal);
  document.getElementById('entity-modal-save').addEventListener('click', saveEntity);
  document.getElementById('btn-add-type').addEventListener('click', () => openEntityModal('type'));
  document.getElementById('btn-add-cell').addEventListener('click', () => openEntityModal('cell'));

  // table actions (event delegation)
  document.getElementById('types-body').addEventListener('click', onRowAction('type'));
  document.getElementById('cells-body').addEventListener('click', onRowAction('cell'));

  // service worker
  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./sw.js').catch((e) => console.warn('[sw]', e));
  }
}

function onRowAction(entity) {
  return (e) => {
    const btn = e.target.closest('button[data-action]');
    if (!btn) return;
    const id = btn.dataset.id;
    if (btn.dataset.action === 'edit') {
      openEntityModal(entity, Store[entity === 'type' ? 'types' : 'cells'].get(id));
    } else if (btn.dataset.action === 'delete') {
      deleteEntity(entity, id);
    }
  };
}

init();



