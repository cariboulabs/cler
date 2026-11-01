export const MAX_ROWS = 200;
export const STALE_MS = 120000;
export const COST_WARN_PCT = 25;

// Columns per stream; RDS is a single live card, the rest are newest-first tables
// keyed so repeat sightings of one station update in place. adsb is listed for
// the decoder the server still reports as unavailable — the table is ready for it.
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

// a decoder only makes sense on its own band; anything else is noise
export function outOfBand(expects, freq) {
  if (!expects || !expects.length || !Number.isFinite(freq)) return false;
  return !expects.some((b) => freq >= b.min && freq <= b.max);
}

export function bandLabel(expects) {
  if (!expects || !expects.length) return '';
  return expects.map((b) => (b.max - b.min < 1e5
    ? `${((b.min + b.max) / 2e6).toFixed(3)} MHz`
    : `${(b.min / 1e6).toFixed(1)}–${(b.max / 1e6).toFixed(1)} MHz`)).join(' or ');
}

export function costPct(hint) {
  const m = /(\d+(?:\.\d+)?)\s*%/.exec(hint || '');
  return m ? Number(m[1]) : 0;
}

// running is what the server says is on; availability and price come from hello
export function menuRows(list, running) {
  const on = new Set(running || []);
  return (list || []).map((d) => ({
    id: d.id,
    checked: on.has(d.id),
    available: d.available !== false,
    reason: d.reason || '',
    cost: d.cost_hint || '',
    band: bandLabel(d.expects)
  }));
}

export function costTotal(list, running) {
  const by = new Map((list || []).map((d) => [d.id, d.cost_hint]));
  const pct = (running || []).reduce((a, id) => a + costPct(by.get(id)), 0);
  const n = (running || []).length;
  if (!n) return { pct: 0, text: 'nothing running', warn: false };
  const text = `${n} decoder${n > 1 ? 's' : ''} running · ~${Math.round(pct)}% of a core`;
  return {
    pct,
    text,
    warn: pct > COST_WARN_PCT,
    note: pct > COST_WARN_PCT ? `decoders are using ~${Math.round(pct)}% of a core; on an RPi 4 that is most of one` : ''
  };
}

// A chip exists only for a decoder that is actually running; an out-of-band one
// warns from the chip, so it is visible while you are looking at another table.
export function chipRows(list, running, active, freq) {
  const meta = new Map((list || []).map((d) => [d.id, d]));
  return (running || []).map((id) => {
    const d = meta.get(id) || {};
    const off = outOfBand(d.expects, freq);
    return {
      id,
      active: id === active,
      outOfBand: off,
      title: off ? `tuned outside ${bandLabel(d.expects)} — ${id} will not decode here` : (d.cost_hint || '')
    };
  });
}

export function rdsLines(d) {
  const out = [];
  out.push(`PS   ${d.ps || '—'}${d.synced ? '' : '   (not synced)'}`);
  if (d.rt) out.push(`RT   ${d.rt}`);
  out.push(`PI   0x${(d.pi || 0).toString(16).toUpperCase().padStart(4, '0')}   PTY ${d.pty ?? 0}${d.tp ? '   TP' : ''}${d.ta ? '   TA' : ''}`);
  out.push(`blocks  ${d.groups_ok ?? 0} groups · ${(d.corrected_pct ?? 0).toFixed(1)}% corrected · ${(d.bad_pct ?? 0).toFixed(1)}% bad`);
  return out;
}

// Chips pick which running decoder's table is on screen; nothing running means the
// caller hides the whole section.
export class DecoderPanel {
  constructor(tabsEl, bodyEl, onSelect) {
    this.tabs = tabsEl;
    this.body = bodyEl;
    this.onSelect = onSelect;
    this.rows = new Map();
    this.rds = null;
    this.active = '';
    this.list = [];
    this.running = [];
    this.freq = NaN;
  }

  setDecoders(list) { this.list = list || []; this.renderChips(); }

  setRunning(running) {
    this.running = running || [];
    if (!this.running.includes(this.active)) this.active = this.running[0] || '';
    this.renderChips();
    this.render();
  }

  setActive(id) {
    if (id === this.active || !this.running.includes(id)) return;
    this.active = id;
    this.renderChips();
    this.render();
  }

  setTuning(freq) {
    if (freq === this.freq) return;
    this.freq = freq;
    this.renderChips();
    this.render();
  }

  renderChips() {
    this.tabs.textContent = '';
    for (const c of chipRows(this.list, this.running, this.active, this.freq)) {
      const b = document.createElement('button');
      b.textContent = c.outOfBand ? `! ${c.id}` : c.id;
      b.dataset.decoder = c.id;
      b.dataset.testid = `chip-${c.id}`;
      b.className = `chip${c.active ? ' on' : ''}${c.outOfBand ? ' warn' : ''}`;
      if (c.title) b.title = c.title;
      b.onclick = () => this.onSelect(c.id);
      this.tabs.appendChild(b);
    }
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
    if (!this.active) return;
    const expects = (this.list.find((d) => d.id === this.active) || {}).expects;
    if (outOfBand(expects, this.freq)) {
      const chip = document.createElement('div');
      chip.className = 'chip-warn';
      chip.dataset.testid = 'decoder-band-warning';
      chip.textContent = `tuned outside ${bandLabel(expects)} — ${this.active} will not decode here`;
      el.appendChild(chip);
    }
    const line = (text) => { const d = document.createElement('div'); d.textContent = text; el.appendChild(d); };
    if (this.active === 'rds') {
      if (!this.rds) { line('waiting for RDS…'); return; }
      for (const l of rdsLines(this.rds)) line(l);
      return;
    }
    const rows = this.rows.get(this.active) || [];
    if (!isTable(this.active)) return;
    if (!rows.length) { line('nothing decoded yet'); return; }
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
