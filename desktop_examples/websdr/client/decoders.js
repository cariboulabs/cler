export const MAX_ROWS = 200;
export const STALE_MS = 120000;

// Columns per stream; RDS is a single live card, the rest are newest-first tables
// keyed so repeat sightings of one station update in place.
const TABLES = {
  ais: { key: (d) => String(d.mmsi), cols: ['mmsi', 'name', 'lat', 'lon', 'sog', 'cog', 'type'] },
  aprs: { key: (d) => `${d.source}/${d.type}`, cols: ['source', 'type', 'lat', 'lon', 'speed', 'comment'] },
  adsb: { key: (d) => String(d.icao), cols: ['icao', 'callsign', 'lat', 'lon', 'altitude', 'speed'] }
};

export function isTable(stream) { return !!TABLES[stream]; }
export function columns(stream) { return TABLES[stream] ? TABLES[stream].cols : []; }

// newest first, one row per key, capped
export function addRow(rows, stream, data, now) {
  const t = TABLES[stream];
  if (!t) return rows;
  const key = t.key(data);
  const i = rows.findIndex((r) => r.key === key);
  if (i >= 0) rows.splice(i, 1);
  rows.unshift({ key, seen: now, data });
  if (rows.length > MAX_ROWS) rows.length = MAX_ROWS;
  return rows;
}

export function isStale(row, now) { return now - row.seen > STALE_MS; }

export function cell(row, col) {
  const v = row.data[col];
  if (v === undefined || v === null) return '';
  if (col === 'lat' || col === 'lon') return Number(v).toFixed(4);
  return typeof v === 'number' ? String(Math.round(v * 100) / 100) : String(v);
}

export function rdsLines(d) {
  const out = [];
  out.push(`PS   ${d.ps || '—'}${d.synced ? '' : '   (not synced)'}`);
  if (d.rt) out.push(`RT   ${d.rt}`);
  out.push(`PI   0x${(d.pi || 0).toString(16).toUpperCase().padStart(4, '0')}   PTY ${d.pty ?? 0}${d.tp ? '   TP' : ''}${d.ta ? '   TA' : ''}`);
  out.push(`blocks  ${d.groups_ok ?? 0} groups · ${(d.corrected_pct ?? 0).toFixed(1)}% corrected · ${(d.bad_pct ?? 0).toFixed(1)}% bad`);
  return out;
}

// One tab per stream the server announced; tables render into a shared <div>.
export class DecoderPanel {
  constructor(tabsEl, bodyEl, onSelect) {
    this.tabs = tabsEl;
    this.body = bodyEl;
    this.onSelect = onSelect;
    this.rows = new Map();
    this.rds = null;
    this.active = 'none';
  }

  setDecoders(list, current) {
    this.tabs.textContent = '';
    for (const d of list || []) {
      const b = document.createElement('button');
      b.textContent = d.id;
      b.dataset.decoder = d.id;
      b.disabled = d.available === false;
      if (d.available === false) b.dataset.ro = '1';   // the role sweep re-enables anything without it
      if (d.reason) b.title = d.reason;
      b.classList.toggle('on', d.id === current);
      b.onclick = () => this.onSelect(d.id);
      this.tabs.appendChild(b);
    }
    this.setActive(current);
  }

  setActive(id) {
    if (id === this.active) return;
    this.active = id;
    for (const b of this.tabs.children) b.classList.toggle('on', b.dataset.decoder === id);
    this.render();
  }

  push(stream, data, now = Date.now()) {
    if (stream === 'rds') {
      this.rds = data;
    } else {
      if (!this.rows.has(stream)) this.rows.set(stream, []);
      addRow(this.rows.get(stream), stream, data, now);
    }
    if (stream === this.active) this.render(now);
  }

  render(now = Date.now()) {
    const el = this.body;
    el.textContent = '';
    if (this.active === 'rds') {
      if (!this.rds) { el.textContent = 'waiting for RDS…'; return; }
      for (const line of rdsLines(this.rds)) {
        const d = document.createElement('div'); d.textContent = line; el.appendChild(d);
      }
      return;
    }
    const rows = this.rows.get(this.active) || [];
    if (!isTable(this.active)) { el.textContent = this.active === 'none' ? 'no decoder running' : ''; return; }
    if (!rows.length) { el.textContent = 'nothing decoded yet'; return; }
    const table = document.createElement('table');
    const head = document.createElement('tr');
    for (const c of columns(this.active)) {
      const th = document.createElement('th'); th.textContent = c; head.appendChild(th);
    }
    table.appendChild(head);
    for (const r of rows) {
      const tr = document.createElement('tr');
      if (isStale(r, now)) tr.className = 'stale';
      for (const c of columns(this.active)) {
        const td = document.createElement('td'); td.textContent = cell(r, c); tr.appendChild(td);
      }
      table.appendChild(tr);
    }
    el.appendChild(table);
  }
}
