const ROWS = 1024;
const MAXN = 4096;

function colormap() {
  const stops = [[0, 0, 0], [0, 0, 120], [0, 120, 200], [0, 220, 160], [230, 230, 40], [255, 255, 255]];
  const lut = new Uint8ClampedArray(256 * 4);
  for (let i = 0; i < 256; i++) {
    const p = (i / 255) * (stops.length - 1);
    const k = Math.min(stops.length - 2, Math.floor(p));
    const f = p - k;
    for (let c = 0; c < 3; c++) lut[i * 4 + c] = stops[k][c] + (stops[k + 1][c] - stops[k][c]) * f;
    lut[i * 4 + 3] = 255;
  }
  return lut;
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
    this.lut = colormap();
    this.rowImage = null;
    this.dirty = true;
    this.bindEvents(this.spec);
    this.bindEvents(this.wf);
    new ResizeObserver(() => { this.resize(); }).observe(this.wf.parentElement);
    this.resize();
  }

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

  clear() { this.filled = 0; this.head = 0; this.dirty = true; this.repaint(); }

  push(frame) {
    if (frame.n !== this.n || frame.center !== this.center || frame.rate !== this.rate) {
      this.n = frame.n; this.center = frame.center; this.rate = frame.rate;
      this.filled = 0; this.head = 0; this.dirty = true;
    }
    this.dbMin = frame.dbMin; this.dbStep = frame.dbStep;
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
    ctx.fillStyle = '#000'; ctx.fillRect(0, 0, W, H);
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
    ctx.fillStyle = '#0b0e14'; ctx.fillRect(0, 0, W, H);
    if (!this.n || !this.filled) return;
    const font = `${11 * dpr}px system-ui, sans-serif`;
    ctx.font = font;
    const dbRange = 255 * this.dbStep;
    ctx.strokeStyle = '#1e2633'; ctx.fillStyle = '#8a94a6'; ctx.lineWidth = 1;
    for (let db = Math.ceil(this.dbMin / 20) * 20; db <= this.dbMin + dbRange; db += 20) {
      const y = H - ((db - this.dbMin) / dbRange) * H;
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke();
      ctx.fillText(`${db} dB`, 4 * dpr, y - 2 * dpr);
    }
    const f0 = this.freqAt(0, W), f1 = this.freqAt(W, W);
    const ticks = 8;
    for (let i = 0; i <= ticks; i++) {
      const f = f0 + (f1 - f0) * i / ticks;
      const x = (i / ticks) * W;
      ctx.beginPath(); ctx.moveTo(x, H - 14 * dpr); ctx.lineTo(x, H); ctx.stroke();
      const label = (f / 1e6).toFixed(3);
      const tw = ctx.measureText(label).width;
      ctx.fillText(label, Math.min(Math.max(x - tw / 2, 2), W - tw - 2), H - 3 * dpr);
    }
    if (this.passbandHz > 0) {
      const xa = this.pxOf(this.center + this.offsetHz - this.passbandHz / 2, W);
      const xb = this.pxOf(this.center + this.offsetHz + this.passbandHz / 2, W);
      ctx.fillStyle = 'rgba(255,200,60,0.12)'; ctx.fillRect(xa, 0, xb - xa, H);
    }
    const xo = this.pxOf(this.center + this.offsetHz, W);
    ctx.strokeStyle = '#ffc83c'; ctx.beginPath(); ctx.moveTo(xo, 0); ctx.lineTo(xo, H); ctx.stroke();
    ctx.strokeStyle = '#4fd1ff'; ctx.lineWidth = 1.2 * dpr; ctx.beginPath();
    const base = this.head * MAXN;
    for (let x = 0; x < W; x++) {
      const b = Math.min(this.n - 1, Math.floor(this.binAt(x, W)));
      const y = H - (this.rows[base + b] / 255) * H;
      if (x === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.stroke();
  }

  bindEvents(canvas) {
    let drag = null;
    canvas.addEventListener('mousedown', (e) => { drag = { x: e.offsetX, moved: false }; });
    canvas.addEventListener('mousemove', (e) => { if (drag && Math.abs(e.offsetX - drag.x) > 3) drag.moved = true; });
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
