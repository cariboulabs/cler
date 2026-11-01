import { test } from 'node:test';
import assert from 'node:assert/strict';
import { trackFloor } from '../client/waterfall.js';

const DB_MIN = -120, DB_STEP = 0.5;
const bin = (db) => Math.max(0, Math.min(255, Math.round((db - DB_MIN) / DB_STEP)));

// a frame of noise at `floor` dB, plus optional carrier bins at `peak` dB
function frame(n, floorDb, { carriers = 0, peakDb = 0 } = {}) {
  const bins = new Uint8Array(n);
  for (let i = 0; i < n; i++) bins[i] = bin(floorDb + (i % 7) - 3);   // ±3 dB of ripple
  for (let i = 0; i < carriers; i++) bins[(n >> 1) + i] = bin(peakDb);
  return bins;
}

const settle = (start, bins, n, rounds) => {
  let f = start;
  for (let i = 0; i < rounds; i++) f = trackFloor(f, bins, n, DB_MIN, DB_STEP);
  return f;
};

test('a steady floor is tracked and then stays put', () => {
  const bins = frame(1024, -70);
  const first = trackFloor(null, bins, 1024, DB_MIN, DB_STEP);
  assert.ok(Math.abs(first - -83) <= 1.5, `snapped to ${first}`);
  const later = settle(first, bins, 1024, 60);
  assert.ok(Math.abs(later - first) < 0.6, `drifted from ${first} to ${later}`);
});

test('a strong carrier does not drag the floor up', () => {
  const n = 1024;
  const quiet = settle(null, frame(n, -70), n, 40);
  // 5% of the span at full scale — far more carrier than a real signal
  const loud = settle(quiet, frame(n, -70, { carriers: n * 0.05, peakDb: -5 }), n, 40);
  assert.ok(Math.abs(loud - quiet) < 1, `floor moved ${quiet} -> ${loud}`);
});

test('a 20 dB gain change is caught within a couple of seconds', () => {
  const n = 1024;
  const before = settle(null, frame(n, -70), n, 40);
  const fps = 20;
  const after = settle(before, frame(n, -50), n, 2 * fps);
  assert.ok(after > before + 17, `only reached ${after} from ${before}`);
  assert.ok(after < before + 23, `overshot to ${after}`);
});

test('a null floor snaps rather than sliding', () => {
  const n = 512;
  const snapped = trackFloor(null, frame(n, -40), n, DB_MIN, DB_STEP);
  assert.ok(snapped > -55 && snapped < -48, `snapped to ${snapped}`);
});
