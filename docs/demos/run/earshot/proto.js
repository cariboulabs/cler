export const PROTO_VER = 1;
export const T_SPECTRUM = 1;
export const T_AUDIO = 2;
export const HEADER_BYTES = 10;
export const SPECTRUM_HEAD_BYTES = 36;
export const AUDIO_HEAD_BYTES = 11;

export function decodeFrame(buf) {
  if (buf.byteLength < HEADER_BYTES) return null;
  const dv = new DataView(buf);
  const type = dv.getUint8(0);
  const ver = dv.getUint8(1);
  if (ver !== PROTO_VER) return { type: 0, ver };
  const gen = dv.getUint32(2, true);
  const seq = dv.getUint32(6, true);
  if (type === T_SPECTRUM) {
    if (buf.byteLength < SPECTRUM_HEAD_BYTES) return null;
    const n = dv.getUint16(26, true);
    if (buf.byteLength !== SPECTRUM_HEAD_BYTES + n) return null;
    return {
      type, gen, seq,
      center: dv.getFloat64(10, true),
      rate: dv.getFloat64(18, true),
      n,
      dbMin: dv.getFloat32(28, true),
      dbStep: dv.getFloat32(32, true),
      bins: new Uint8Array(buf, SPECTRUM_HEAD_BYTES, n)
    };
  }
  if (type === T_AUDIO) {
    if (buf.byteLength < AUDIO_HEAD_BYTES) return null;
    const codec = dv.getUint8(10);
    const n = (buf.byteLength - AUDIO_HEAD_BYTES) >> 1;
    const pcm = new Int16Array(n);
    for (let i = 0; i < n; i++) pcm[i] = dv.getInt16(AUDIO_HEAD_BYTES + 2 * i, true);
    return { type, gen, seq, codec, pcm };
  }
  return { type, gen, seq };
}

export function encodeSet(fields) {
  return JSON.stringify({ t: 'set', ...fields });
}

export function encodeSource(id, rate) {
  return JSON.stringify(rate > 0 ? { t: 'source', id, rate } : { t: 'source', id });
}

export function encodeRecord(on, dir, name) {
  const m = { t: 'record', on };
  if (dir) m.dir = dir;
  if (name) m.name = name;
  return JSON.stringify(m);
}

export function encodeDeleteRecording(name) {
  return JSON.stringify({ t: 'recording', action: 'delete', name });
}

export function encodeHello(token) {
  const m = { t: 'hello', proto: PROTO_VER, accept: { codecs: ['pcm16'] } };
  if (token) m.token = token;
  return JSON.stringify(m);
}

export function formatHz(hz) {
  const a = Math.abs(hz);
  if (a >= 1e9) return (hz / 1e9).toFixed(4) + ' GHz';
  if (a >= 1e6) return (hz / 1e6).toFixed(4) + ' MHz';
  if (a >= 1e3) return (hz / 1e3).toFixed(2) + ' kHz';
  return hz.toFixed(0) + ' Hz';
}

// bare number = MHz, which is what a radio operator types
export function parseFreq(text) {
  const m = String(text).trim().toLowerCase().match(/^([\d.]+)\s*(g|m|k|ghz|mhz|khz|hz)?$/);
  if (!m || !Number.isFinite(Number(m[1]))) return null;
  const mult = { g: 1e9, ghz: 1e9, m: 1e6, mhz: 1e6, k: 1e3, khz: 1e3, hz: 1 }[m[2] || 'mhz'];
  const hz = Number(m[1]) * mult;
  return hz > 0 ? hz : null;
}

export function fmtBytes(b) {
  b = Number(b) || 0;
  if (b >= 1e9) return (b / 1e9).toFixed(1) + ' GB';
  if (b >= 1e6) return (b / 1e6).toFixed(1) + ' MB';
  if (b >= 1e3) return (b / 1e3).toFixed(0) + ' kB';
  return `${b} B`;
}

export function fmtDuration(sec) {
  const t = Math.max(0, Math.floor(Number(sec) || 0));
  return `${Math.floor(t / 60)}:${String(t % 60).padStart(2, '0')}`;
}

export class SeqTracker {
  constructor() { this.last = null; this.gaps = 0; }
  push(seq) {
    const gap = this.last !== null && seq !== ((this.last + 1) >>> 0);
    if (gap) this.gaps++;
    this.last = seq;
    return gap;
  }
  reset() { this.last = null; }
}
