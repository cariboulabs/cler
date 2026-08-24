import { test } from 'node:test';
import assert from 'node:assert/strict';
import { keyAction, tuneStepHz } from '../client/keys.js';

test('typing in a field never drives the radio', () => {
  for (const tag of ['INPUT', 'SELECT', 'TEXTAREA']) {
    assert.equal(keyAction({ key: 'ArrowLeft' }, tag), null);
    assert.equal(keyAction({ key: 'r' }, tag), null);
    assert.equal(keyAction({ key: ' ' }, tag), null);
  }
});

test('Escape closes even while typing', () => {
  assert.deepEqual(keyAction({ key: 'Escape' }, 'INPUT'), { action: 'close' });
});

test('arrows tune, shift multiplies by ten', () => {
  assert.deepEqual(keyAction({ key: 'ArrowLeft' }, 'BODY'), { action: 'tune', steps: -1 });
  assert.deepEqual(keyAction({ key: 'ArrowRight' }, 'BODY'), { action: 'tune', steps: 1 });
  assert.deepEqual(keyAction({ key: 'ArrowRight', shiftKey: true }, 'BODY'), { action: 'tune', steps: 10 });
});

test('shortcuts, and modifiers are left to the browser', () => {
  assert.deepEqual(keyAction({ key: ' ' }, 'BODY'), { action: 'listen' });
  assert.deepEqual(keyAction({ key: 'r' }, 'BODY'), { action: 'record' });
  assert.deepEqual(keyAction({ key: '/' }, 'BODY'), { action: 'focusFreq' });
  assert.equal(keyAction({ key: 'r', ctrlKey: true }, 'BODY'), null);
  assert.equal(keyAction({ key: 'q' }, 'BODY'), null);
});

test('the tune step follows the zoom', () => {
  assert.equal(tuneStepHz(2.4e6, 1), 24000);
  assert.equal(tuneStepHz(2.4e6, 0.1), 2400);
  assert.equal(tuneStepHz(0, 1), 1);
});
