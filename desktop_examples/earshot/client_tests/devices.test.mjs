import { test } from 'node:test';
import assert from 'node:assert/strict';
import { applyError, deviceRows } from '../client/devices.js';

const sources = [
  { id: 'cariboulite:s1g', kind: 'cariboulite', label: 'CaribouLite S1G' },
  { id: 'sim', kind: 'sim', label: 'Simulator' },
  { id: 'uhd', kind: 'uhd', label: 'USRP', available: false }
];

test('a source error lands on its row, everything else toasts', () => {
  const a = applyError({}, { code: 'source', id: 'cariboulite:s1g', msg: 'no access to /dev/gpiomem' });
  assert.equal(a.toast, false);
  assert.equal(a.errors['cariboulite:s1g'], 'no access to /dev/gpiomem');

  const b = applyError({}, { code: 'record', msg: 'disk almost full' });
  assert.equal(b.toast, true);
  assert.deepEqual(b.errors, {});

  // a source error with no id has no row to land on
  const c = applyError({}, { code: 'source', msg: 'unknown source' });
  assert.equal(c.toast, true);
});

test('the failed row keeps a button and shows why', () => {
  const errors = { 'cariboulite:s1g': 'no access to /dev/gpiomem — add the user running this to the gpio, spi and i2c groups' };
  const rows = deviceRows(sources, errors, 'sim', 'ctl');

  assert.equal(rows[0].why, errors['cariboulite:s1g']);
  assert.equal(rows[0].connectable, true, 'a failed device must stay retryable');
  assert.equal(rows[0].connected, false);

  assert.equal(rows[1].connected, true, 'the running source is the connected one');
  assert.equal(rows[1].connectable, false);
  assert.equal(rows[1].why, '');

  assert.equal(rows[2].connectable, false, 'available:false stays unconnectable');
});

test('a viewer cannot connect anything', () => {
  for (const r of deviceRows(sources, {}, 'sim', 'view')) assert.equal(r.connectable, false);
});
