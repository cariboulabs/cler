import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildModel, applyState, coerce, fmtValue, roReason } from '../client/panel.js';

const controls = [
  { id: 'freq', label: 'frequency', type: 'range', min: 1e6, max: 6e9, step: 1e3, unit: 'Hz' },
  { id: 'mode', type: 'enum', options: ['WBFM', 'AM'] },
  { id: 'amp', type: 'bool' },
  { id: 'rate', type: 'range', min: 2.4e6, max: 2.4e6, ro: true }
];

test('builds the model from controls and state', () => {
  const m = buildModel(controls, { freq: 100e6, mode: 'AM', amp: true });
  assert.equal(m.length, 4);
  assert.equal(m[0].value, 100e6); assert.equal(m[0].label, 'frequency'); assert.equal(m[0].unit, 'Hz');
  assert.equal(m[1].label, 'mode'); assert.equal(m[1].value, 'AM');
  assert.equal(m[2].value, true);
  assert.equal(m[3].ro, true); assert.equal(m[3].value, undefined);
  applyState(m, { freq: 2e6, rate: 2.4e6 });
  assert.equal(m[0].value, 2e6); assert.equal(m[3].value, 2.4e6);
});

test('carries the per-option disable reasons the server sent', () => {
  const [mode] = buildModel([{ id: 'mode', type: 'enum', options: ['WBFM', 'AM'],
                              options_disabled: ['needs 200 kHz; this source is 48 kHz wide', ''] }], {});
  assert.equal(mode.options_disabled.length, 2);
  assert.match(mode.options_disabled[0], /needs 200 kHz/);
  assert.equal(mode.options_disabled[1], '');
  // a server that never sends them must not break the client
  assert.deepEqual(buildModel([{ id: 'mode', type: 'enum', options: ['AM'] }], {})[0].options_disabled, []);
});

test('a read-only control shows a value and why it cannot be changed', () => {
  const m = buildModel(controls, { rate: 2400000, amp: true });
  assert.equal(fmtValue(m[3]), '2,400,000');
  assert.equal(fmtValue(m[2]), 'on');
  assert.equal(fmtValue({ value: undefined }), '—');
  assert.match(roReason('rate'), /reconnect/);
  assert.match(roReason('anything'), /read-only/);
});

test('coerces input by control type and clamps ranges', () => {
  const m = buildModel(controls, {});
  assert.equal(coerce(m[0], '7e9'), 6e9);
  assert.equal(coerce(m[0], '0'), 1e6);
  assert.equal(coerce(m[0], 'abc'), null);
  assert.equal(coerce(m[1], 'AM'), 'AM');
  assert.equal(coerce(m[2], 0), false);
});
