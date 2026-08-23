import { test } from 'node:test';
import assert from 'node:assert/strict';
import { decodeFrame, encodeSet, encodeHello, formatHz, SeqTracker, T_SPECTRUM, T_AUDIO, SPECTRUM_HEAD_BYTES, AUDIO_HEAD_BYTES } from '../client/proto.js';

function header(type, gen, seq, extra) {
  const buf = new ArrayBuffer(10 + extra);
  const dv = new DataView(buf);
  dv.setUint8(0, type); dv.setUint8(1, 1); dv.setUint32(2, gen, true); dv.setUint32(6, seq, true);
  return { buf, dv };
}

test('decodes a spectrum frame little-endian', () => {
  const n = 16;
  const { buf, dv } = header(T_SPECTRUM, 7, 42, 26 + n);
  dv.setFloat64(10, 100e6, true); dv.setFloat64(18, 2.4e6, true); dv.setUint16(26, n, true);
  dv.setFloat32(28, -120, true); dv.setFloat32(32, 0.5, true);
  for (let i = 0; i < n; i++) dv.setUint8(SPECTRUM_HEAD_BYTES + i, i * 10);
  const f = decodeFrame(buf);
  assert.equal(f.type, T_SPECTRUM);
  assert.equal(f.gen, 7); assert.equal(f.seq, 42);
  assert.equal(f.center, 100e6); assert.equal(f.rate, 2.4e6); assert.equal(f.n, n);
  assert.equal(f.dbMin, -120); assert.equal(f.dbStep, 0.5);
  assert.equal(f.bins[3], 30);
  assert.equal(decodeFrame(buf.slice(0, buf.byteLength - 1)), null);
});

test('decodes an audio frame and rejects other versions', () => {
  const n = 4;
  const { buf, dv } = header(T_AUDIO, 1, 9, 1 + 2 * n);
  dv.setUint8(10, 0);
  [-32768, -1, 0, 32767].forEach((v, i) => dv.setInt16(AUDIO_HEAD_BYTES + 2 * i, v, true));
  const f = decodeFrame(buf);
  assert.equal(f.type, T_AUDIO); assert.equal(f.codec, 0); assert.equal(f.seq, 9);
  assert.deepEqual(Array.from(f.pcm), [-32768, -1, 0, 32767]);
  dv.setUint8(1, 2);
  assert.equal(decodeFrame(buf).type, 0);
});

test('encodes control messages', () => {
  assert.equal(encodeSet({ freq: 1e6 }), '{"t":"set","freq":1000000}');
  assert.equal(JSON.parse(encodeHello('x')).token, 'x');
  assert.equal(JSON.parse(encodeHello()).proto, 1);
});

test('formats frequencies and tracks sequence gaps', () => {
  assert.equal(formatHz(100.5e6), '100.5000 MHz');
  assert.equal(formatHz(12e3), '12.00 kHz');
  const s = new SeqTracker();
  assert.equal(s.push(1), false); assert.equal(s.push(2), false); assert.equal(s.push(4), true);
  assert.equal(s.gaps, 1);
  s.last = 0xFFFFFFFF; assert.equal(s.push(0), false);
});
