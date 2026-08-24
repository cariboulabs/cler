const ROWS = 1024;
const MAXN = 4096;

// inferno: perceptually uniform (~10 L* per stop), readable under all three
// colour-blindness types, and it keeps a true black floor — an operator reads
// black as "nothing there". It owns no blue, which is why the trace can be blue
// and never be mistaken for a bin.
const INFERNO = [[0, 0, 4], [27, 12, 65], [74, 12, 107], [120, 28, 109], [165, 44, 96],
                 [207, 68, 70], [237, 105, 37], [251, 155, 6], [247, 209, 61], [252, 255, 164]];

// A shade table maps a raw bin (dbMin + v*dbStep) onto the ramp between floor
// and floor+range, so the noise floor sits at inferno's black end instead of
// its hot middle. Baked into the same 256-entry table the row painter already
// reads, which keeps the paint loop a single lookup.
function shadeTable(dbMin, dbStep, floorDb, rangeDb) {
  const stops = INFERNO;
  const lut = new Uint8ClampedArray(256 * 4);
  for (let v = 0; v < 256; v++) {
    const db = dbMin + v * dbStep;
    const t = Math.max(0, Math.min(1, (db - floorDb) / rangeDb));
    const p = t * (stops.length - 1);
    const k = Math.min(stops.length - 2, Math.floor(p));
    const f = p - k;
    for (let c = 0; c < 3; c++) lut[v * 4 + c] = stops[k][c] + (stops[k + 1][c] - stops[k][c]) * f;
    lut[v * 4 + 3] = 255;
  }
  return lut;
}

// The floor is a low percentile of the frame, smoothed: a carrier occupies few
// bins so it cannot drag the floor up, and the smoothing stops the image
// breathing while still catching a gain change in a second or so. prev = null
// snaps, which is what a source switch wants.
export function trackFloor(prev, bins, n, dbMin, dbStep, alpha = 0.12, pct = 0.1, marginDb = 10) {
  if (!n) return prev;
  const hist = new Uint32Array(256);
  for (let i = 0; i < n; i++) hist[bins[i]]++;
  const target = Math.max(1, Math.ceil(n * pct));
  let seen = 0, v = 0;
  for (; v < 256; v++) { seen += hist[v]; if (seen >= target) break; }
  // margin keeps the noise just off the bottom of the plot, where the frequency
  // labels live, and inside inferno's black end
  const sample = dbMin + Math.min(v, 255) * dbStep - marginDb;
  return Number.isFinite(prev) ? prev + (sample - prev) * alpha : sample;
}

export class Waterfall {
  constructor(specCanvas, wfCanvas, handlers) {
    this.spec = specCanvas;
    this.wf = wfCanvas;
    this.h = handlers || {};
    this.rows = new Uint8Array(ROWS * MAXN);
    this.head = 0;
    this.filled = 0;
    this.n = 0;
    this.center = 0; this.rate = 0; this.dbMin = -120; this.dbStep = 0.5;
    this.view = { x0: 0, x1: 1 };
    this.offsetHz = 0; this.passbandHz = 0;
    this.auto = true; this.floorDb = null; this.rangeDb = 70; this.shadedFloor = -100;
    this.lut = shadeTable(this.dbMin, this.dbStep, this.shadedFloor, this.rangeDb);
    this.rowImage = null;
    this.dirty = true;
    this.readTokens();
    this.bindEvents(this.spec);
    this.bindEvents(this.wf);
    new ResizeObserver(() => { this.resize(); }).observe(this.wf.parentElement);
    this.resize();
  }

  // Components read token names, never raw colours — the canvas has to fetch them
  // once because 2d fillStyle takes a string.
  readTokens() {
    const s = getComputedStyle(document.documentElement);
    const tok = (n, fallback) => (s.getPropertyValue(n) || '').trim() || fallback;
    this.col = {
      ground: tok('--bg-0', '#0e1116'),
      grid: tok('--border', '#2a323d'),
      label: tok('--muted', '#8b98a9'),
      trace: tok('--trace', '#4aa3ff'),
      marker: tok('--warn', '#e0b341'),
      passband: 'rgba(224,179,65,0.14)'
    };
  }

  resetView() {
    this.setView(0, 1);
    if (this.h.onView) this.h.onView(this.view);
  }

  zoomed() { return this.view.x0 > 0 || this.view.x1 < 1; }

  resize() {
    const dpr = window.devicePixelRatio || 1;
    for (const c of [this.spec, this.wf]) {
      const w = Math.max(1, Math.floor(c.clientWidth * dpr)), h = Math.max(1, Math.floor(c.clientHeight * dpr));
      if (c.width !== w || c.height !== h) { c.width = w; c.height = h; }
    }
    this.rowImage = new ImageData(this.wf.width, 1);
    this.dirty = true;
    this.repaint();
  }

  clear() { this.filled = 0; this.head = 0; this.dirty = true; if (this.auto) this.floorDb = null; this.repaint(); }

  // floor/range in dB; auto tracks the noise, manual pins it. null floor = snap
  // to the next frame rather than sliding there.
  setRange({ auto, floorDb, rangeDb }) {
    if (auto !== undefined) this.auto = !!auto;
    if (Number.isFinite(rangeDb)) this.rangeDb = Math.max(10, Math.min(160, rangeDb));
    if (Number.isFinite(floorDb)) this.floorDb = floorDb;
    if (this.auto && floorDb === null) this.floorDb = null;
    this.reshade();
  }

  // Rows hold raw bins, so a new table re-shades the whole history for free —
  // the existing dirty flag makes push() repaint instead of scrolling one line.
  reshade() {
    this.shadedFloor = this.floorDb ?? -100;
    this.lut = shadeTable(this.dbMin, this.dbStep, this.shadedFloor, this.rangeDb);
    this.dirty = true; this.repaint(); this.drawSpectrum();
  }

  push(frame) {
    if (frame.n !== this.n || frame.center !== this.center || frame.rate !== this.rate) {
      this.n = frame.n; this.center = frame.center; this.rate = frame.rate;
      this.filled = 0; this.head = 0; this.dirty = true;
      if (this.auto) this.floorDb = null;
    }
    this.dbMin = frame.dbMin; this.dbStep = frame.dbStep;
    if (this.auto) {
      this.floorDb = trackFloor(this.floorDb, frame.bins, frame.n, frame.dbMin, frame.dbStep);
      if (Math.abs(this.floorDb - this.shadedFloor) > 0.5) {
        this.shadedFloor = this.floorDb;
        this.lut = shadeTable(this.dbMin, this.dbStep, this.shadedFloor, this.rangeDb);
        this.dirty = true;
      }
    }
    this.head = (this.head + ROWS - 1) % ROWS;
    this.rows.set(frame.bins, this.head * MAXN);
    if (this.filled < ROWS) this.filled++;
    if (this.dirty) this.repaint();
    else this.scrollOne();
    this.drawSpectrum();
  }

  setMarker(offsetHz, passbandHz) { this.offsetHz = offsetHz; this.passbandHz = passbandHz; this.drawSpectrum(); }
  setView(x0, x1) { this.view = { x0: Math.max(0, x0), x1: Math.min(1, x1) }; this.dirty = true; this.repaint(); this.drawSpectrum(); }

  binAt(px, width) { const f = this.view.x0 + (px / width) * (this.view.x1 - this.view.x0); return f * this.n; }
  freqAt(px, width) { const f = this.view.x0 + (px / width) * (this.view.x1 - this.view.x0); return this.center + (f - 0.5) * this.rate; }
  pxOf(freq, width) { const f = (freq - this.center) / this.rate + 0.5; return ((f - this.view.x0) / (this.view.x1 - this.view.x0)) * width; }

  paintRow(rowIdx, out, width) {
    const base = rowIdx * MAXN;
    const span = this.view.x1 - this.view.x0;
    for (let x = 0; x < width; x++) {
      const b0 = Math.floor((this.view.x0 + (x / width) * span) * this.n);
      const b1 = Math.max(b0 + 1, Math.floor((this.view.x0 + ((x + 1) / width) * span) * this.n));
      let v = 0;
      for (let b = b0; b < b1 && b < this.n; b++) v = Math.max(v, this.rows[base + b]);
      const li = v * 4;
      out[x * 4] = this.lut[li]; out[x * 4 + 1] = this.lut[li + 1]; out[x * 4 + 2] = this.lut[li + 2]; out[x * 4 + 3] = 255;
    }
  }

  scrollOne() {
    const ctx = this.wf.getContext('2d');
    const W = this.wf.width, H = this.wf.height;
    ctx.drawImage(this.wf, 0, 0, W, H - 1, 0, 1, W, H - 1);
    this.paintRow(this.head, this.rowImage.data, W);
    ctx.putImageData(this.rowImage, 0, 0);
  }

  repaint() {
    const ctx = this.wf.getContext('2d');
    const W = this.wf.width, H = this.wf.height;
    ctx.fillStyle = '#000004'; ctx.fillRect(0, 0, W, H);
    if (!this.n) { this.dirty = false; return; }
    const rowsToDraw = Math.min(H, this.filled);
    const img = ctx.createImageData(W, Math.max(1, rowsToDraw));
    for (let y = 0; y < rowsToDraw; y++) {
      const rowIdx = (this.head + y) % ROWS;
      this.paintRow(rowIdx, img.data.subarray(y * W * 4, (y + 1) * W * 4), W);
    }
    if (rowsToDraw) ctx.putImageData(img, 0, 0);
    this.dirty = false;
  }

  drawSpectrum() {
    const ctx = this.spec.getContext('2d');
    const W = this.spec.width, H = this.spec.height;
    const dpr = window.devicePixelRatio || 1;
    ctx.fillStyle = this.col.ground; ctx.fillRect(0, 0, W, H);
    if (!this.n || !this.filled) return;
    const font = `${11 * dpr}px system-ui, sans-serif`;
    ctx.font = font;
    // the same window the waterfall is shaded with, so the trace and the colours
    // are reading the same dB. PH keeps the plot out of the strip the frequency
    // labels own, so a noisy floor cannot scribble over them.
    const lo = this.displayFloor(), dbRange = this.rangeDb, PH = H - 16 * dpr;
    ctx.strokeStyle = this.col.grid; ctx.fillStyle = this.col.label; ctx.lineWidth = 1;
    for (let db = Math.ceil(lo / 20) * 20; db <= lo + dbRange; db += 20) {
      const y = PH - ((db - lo) / dbRange) * PH;
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke();
      if (y < H - 18 * dpr) ctx.fillText(`${db} dB`, 4 * dpr, y - 2 * dpr);   // the frequency ticks own the bottom strip
    }
    const f0 = this.freqAt(0, W), f1 = this.freqAt(W, W);
    const ticks = 8;
    let lastRight = -Infinity;   // clamping a label to the edge used to stack it on its neighbour
    for (let i = 0; i <= ticks; i++) {
      const f = f0 + (f1 - f0) * i / ticks;
      const x = (i / ticks) * W;
      ctx.beginPath(); ctx.moveTo(x, H - 14 * dpr); ctx.lineTo(x, H); ctx.stroke();
      const label = (f / 1e6).toFixed(3);
      const tw = ctx.measureText(label).width;
      const lx = Math.min(Math.max(x - tw / 2, 2), W - tw - 2);
      if (lx < lastRight + 6 * dpr) continue;
      ctx.fillText(label, lx, H - 3 * dpr);
      lastRight = lx + tw;
    }
    if (this.passbandHz > 0) {
      const xa = this.pxOf(this.center + this.offsetHz - this.passbandHz / 2, W);
      const xb = this.pxOf(this.center + this.offsetHz + this.passbandHz / 2, W);
      ctx.fillStyle = this.col.passband; ctx.fillRect(xa, 0, xb - xa, H);
    }
    const xo = this.pxOf(this.center + this.offsetHz, W);
    ctx.strokeStyle = this.col.marker; ctx.beginPath(); ctx.moveTo(xo, 0); ctx.lineTo(xo, H); ctx.stroke();
    ctx.strokeStyle = this.col.trace; ctx.lineWidth = 1.2 * dpr; ctx.beginPath();
    const base = this.head * MAXN;
    for (let x = 0; x < W; x++) {
      const b = Math.min(this.n - 1, Math.floor(this.binAt(x, W)));
      const db = this.dbMin + this.rows[base + b] * this.dbStep;
      const y = PH - Math.max(0, Math.min(1, (db - lo) / dbRange)) * PH;
      if (x === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.stroke();
  }

  displayFloor() { return this.floorDb ?? this.shadedFloor; }

  // dB under the pointer, from the newest row — the same numbers the trace is drawn from
  dbAt(px, width) {
    if (!this.n || !this.filled) return null;
    const b = Math.max(0, Math.min(this.n - 1, Math.floor(this.binAt(px, width))));
    return this.dbMin + this.rows[this.head * MAXN + b] * this.dbStep;
  }

  bindEvents(canvas) {
    let drag = null;
    canvas.addEventListener('mousedown', (e) => { drag = { x: e.offsetX, moved: false }; });
    canvas.addEventListener('mousemove', (e) => {
      if (drag && Math.abs(e.offsetX - drag.x) > 3) drag.moved = true;
      if (this.h.onCursor) this.h.onCursor(this.freqAt(e.offsetX, canvas.clientWidth), this.dbAt(e.offsetX, canvas.clientWidth));
    });
    canvas.addEventListener('mouseleave', () => { if (this.h.onCursor) this.h.onCursor(null, null); });
    window.addEventListener('mouseup', (e) => {
      if (!drag) return;
      if (drag.moved && canvas === this.wf && this.h.onPan) {
        const W = canvas.clientWidth;
        const dHz = -((e.clientX - canvas.getBoundingClientRect().left) - drag.x) / W * (this.view.x1 - this.view.x0) * this.rate;
        this.h.onPan(dHz);
      }
      drag = null;
    });
    canvas.addEventListener('dblclick', (e) => {
      if (this.h.onTune) this.h.onTune(this.freqAt(e.offsetX, canvas.clientWidth));
    });
    canvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      const W = canvas.clientWidth;
      const at = this.view.x0 + (e.offsetX / W) * (this.view.x1 - this.view.x0);
      const factor = e.deltaY > 0 ? 1.25 : 0.8;
      let width = Math.min(1, Math.max(1 / 64, (this.view.x1 - this.view.x0) * factor));
      let x0 = at - (at - this.view.x0) * (width / (this.view.x1 - this.view.x0));
      x0 = Math.max(0, Math.min(1 - width, x0));
      this.setView(x0, x0 + width);
      if (this.h.onView) this.h.onView(this.view);
    }, { passive: false });
  }
}
