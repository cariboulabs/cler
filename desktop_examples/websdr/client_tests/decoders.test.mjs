import { test } from 'node:test';
import assert from 'node:assert/strict';
import { addRow, cell, columns, isStale, isTable, rdsLines, MAX_ROWS, STALE_MS } from '../client/decoders.js';

test('knows which streams are tables', () => {
  assert.equal(isTable('ais'), true);
  assert.equal(isTable('aprs'), true);
  assert.equal(isTable('rds'), false);
  assert.deepEqual(columns('ais')[0], 'mmsi');
  assert.deepEqual(columns('rds'), []);
});

test('rows are newest first, one per key, and capped', () => {
  const rows = [];
  addRow(rows, 'ais', { mmsi: 1, lat: 1 }, 1000);
  addRow(rows, 'ais', { mmsi: 2, lat: 2 }, 2000);
  assert.deepEqual(rows.map((r) => r.key), ['2', '1']);

  addRow(rows, 'ais', { mmsi: 1, lat: 9 }, 3000);
  assert.equal(rows.length, 2, 'a repeat sighting updates in place');
  assert.deepEqual(rows.map((r) => r.key), ['1', '2']);
  assert.equal(rows[0].data.lat, 9);
  assert.equal(rows[0].seen, 3000);

  for (let i = 0; i < MAX_ROWS + 50; i++) addRow(rows, 'ais', { mmsi: 1000 + i }, 4000 + i);
  assert.equal(rows.length, MAX_ROWS);
  assert.equal(rows[0].key, String(1000 + MAX_ROWS + 49));
});

test('unknown streams are ignored', () => {
  const rows = [];
  addRow(rows, 'nope', { a: 1 }, 1000);
  assert.equal(rows.length, 0);
});

test('rows go stale after two minutes', () => {
  const row = { key: '1', seen: 1000, data: {} };
  assert.equal(isStale(row, 1000 + STALE_MS - 1), false);
  assert.equal(isStale(row, 1000 + STALE_MS + 1), true);
});

test('cells format positions and skip missing fields', () => {
  const row = { key: '1', seen: 0, data: { mmsi: 244660000, lat: 52.3752345, sog: 12.345, name: 'SHIP' } };
  assert.equal(cell(row, 'lat'), '52.3752');
  assert.equal(cell(row, 'lon'), '');
  assert.equal(cell(row, 'sog'), '12.35');
  assert.equal(cell(row, 'name'), 'SHIP');
  assert.equal(cell(row, 'mmsi'), '244660000');
});

test('rds renders a card, radiotext only when present', () => {
  const lines = rdsLines({ synced: true, ps: 'K-BARAMA', pi: 0x4416, pty: 11, groups_ok: 40, corrected_pct: 5.6, bad_pct: 25 });
  assert.match(lines[0], /K-BARAMA/);
  assert.equal(lines.length, 3, 'no RT line when radiotext is empty');
  assert.match(lines[1], /0x4416/);
  assert.match(lines[2], /40 groups/);

  const withRt = rdsLines({ ps: 'X', rt: 'now playing', pi: 1 });
  assert.equal(withRt.length, 4);
  assert.match(withRt[0], /not synced/);
  assert.match(withRt[1], /now playing/);
});
