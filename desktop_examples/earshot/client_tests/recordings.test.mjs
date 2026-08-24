import { test } from 'node:test';
import assert from 'node:assert/strict';
import { recordingRows, fmtAge, dirSummary, scpLine, prunedNote } from '../client/recordings.js';

const rec = { name: 'wfm_20260824T140236_99600000', dir: '0', bytes: 24068888, rate: 2400000, freq: 99600000, seconds: 2.5, age_s: 42 };

test('a row says what the capture is and what you may do with it', () => {
  const [r] = recordingRows([rec], 'ctl');
  assert.equal(r.name, rec.name);
  assert.match(r.meta, /24\.1 MB · 0:02 · 99\.600 MHz @ 2\.40 MS\/s · 42s ago/);
  assert.equal(r.canDelete, true);
  assert.equal(r.deleteReason, '');
});

test('a viewer is refused with the reason, not silently', () => {
  const [r] = recordingRows([rec], 'view');
  assert.equal(r.canDelete, false);
  assert.match(r.deleteReason, /only the controller/);
  assert.equal(r.canPlay, false);
});

test('age reads in the unit a human would use', () => {
  assert.equal(fmtAge(5), '5s ago');
  assert.equal(fmtAge(600), '10m ago');
  assert.equal(fmtAge(7200), '2h ago');
  assert.equal(fmtAge(200000), '2d ago');
});

test('the directory line carries room left and the cap', () => {
  const d = { path: '/data/rec', free_bytes: 214e9, total_bytes: 500e9, writable: true };
  assert.equal(dirSummary(d, 20e9), '214.0 GB free of 500.0 GB · cap 20.0 GB');
  assert.match(dirSummary({ ...d, writable: false }, 0), /not writable/);
  assert.equal(dirSummary(null, 0), '');
});

test('the scp line is ready to paste', () => {
  assert.equal(scpLine('box', '/data/rec/', 'cap1'), 'scp box:/data/rec/cap1.sigmf-* .');
});

test('a pruned capture is explained, and silence when nothing was pruned', () => {
  assert.match(prunedNote(21e6), /21\.0 MB pruned this run/);
  assert.equal(prunedNote(0), '');
});
