// Drives the real websdr binary over its own websocket on the simulator source:
// audio actually decodes, a recording round-trips through /recordings and plays
// back, and a source switch bumps gen without leaking stale frames.
// Needs nothing but node >= 22 (global WebSocket/fetch):
//   WEBSDR_BIN=build/desktop_examples/websdr/websdr node desktop_examples/websdr/client_tests/integration.mjs
import { spawn } from 'node:child_process';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { setTimeout as sleep } from 'node:timers/promises';

const bin = process.env.WEBSDR_BIN;
if (!bin) { console.error('WEBSDR_BIN not set'); process.exit(2); }
if (typeof WebSocket === 'undefined') { console.log('SKIP: node has no global WebSocket (needs >= 22)'); process.exit(77); }

const port = 18100 + Math.floor(Math.random() * 800);
const dir = await mkdtemp(join(tmpdir(), 'websdr-itest-'));
// two 2 s recordings at 2.4 MS/s are ~19 MB each, so the second one forces a prune
const proc = spawn(bin, ['--source', 'sim', '--port', String(port), '--record-dir', dir,
                         '--record-max-bytes', '25000000'], { stdio: ['ignore', 'pipe', 'pipe'] });
let log = '';
proc.stdout.on('data', (d) => (log += d));
proc.stderr.on('data', (d) => (log += d));
const cleanup = () => { proc.kill(); };
process.on('exit', cleanup);

const fail = async (msg) => {
  console.error('FAIL:', msg, '\n--- websdr log ---\n' + log);
  proc.kill();
  await rm(dir, { recursive: true, force: true });
  process.exit(1);
};
const ok = (msg) => console.log('ok', msg);

for (let i = 0; ; ++i) {
  try { await fetch(`http://127.0.0.1:${port}/health`); break; } catch { await sleep(100); }
  if (i === 100) await fail('server never came up');
}

const ws = new WebSocket(`ws://127.0.0.1:${port}/`);
ws.binaryType = 'arraybuffer';
const state = { st: null, stats: null, gen: 0, audio: [], frames: [], errors: [] };
ws.onmessage = (e) => {
  if (typeof e.data === 'string') {
    const m = JSON.parse(e.data);
    if (m.t === 'hello') { state.st = m.state; state.gen = m.state?.gen ?? 0; }
    else if (m.t === 'state') { state.st = m; state.gen = m.gen; }
    else if (m.t === 'stats') state.stats = m;
    else if (m.t === 'error') state.errors.push(m);
    return;
  }
  const b = new Uint8Array(e.data);
  const gen = new DataView(e.data).getUint32(2, true);
  state.frames.push({ type: b[0], gen, at: Date.now() });
  if (b[0] === 2) state.audio.push(new DataView(e.data, 11));
};
await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
ws.send(JSON.stringify({ t: 'hello', proto: 1 }));

const waitFor = async (what, pred, ms = 8000) => {
  for (let t = 0; t < ms; t += 50) { if (pred()) return; await sleep(50); }
  await fail(`timed out waiting for ${what}`);
};
const send = (o) => ws.send(JSON.stringify(o));

// hello can land before the startup select finishes, so wait for the source
await waitFor('the simulator to come up', () => state.st?.source === 'sim');
ok(`hello, source sim, gen ${state.gen}`);

// (c) audio decodes to real PCM. WBFM of the simulator's unmodulated carrier is
// silence by physics, so listen in USB and tune 1 kHz below the tone, which turns
// it into a 1 kHz beat note.
const beatHz = 1000;
send({ t: 'set', mode: 'USB' });
await waitFor('USB', () => state.st.mode === 'USB');
send({ t: 'set', freq: state.st.freq - beatHz });
await sleep(300);
state.audio.length = 0;
await sleep(1500);
if (state.audio.length < 10) await fail(`only ${state.audio.length} audio frames arrived`);
let sum = 0, n = 0, crossings = 0, prev = 0;
for (const dv of state.audio) {
  for (let i = 0; i + 1 < dv.byteLength; i += 2) {
    const v = dv.getInt16(i, true);
    sum += v * v; ++n;
    if ((v < 0) !== (prev < 0)) ++crossings;
    prev = v;
  }
}
const rms = Math.sqrt(sum / n);
const tone = (crossings / 2) * (48000 / n);
if (rms < 1000) await fail(`audio is silent (rms ${rms.toFixed(0)} over ${n} samples)`);
if (Math.abs(tone - beatHz) > 300) await fail(`audio is not the expected ${beatHz} Hz beat (${tone.toFixed(0)} Hz)`);
ok(`audio decodes: ${state.audio.length} frames, rms ${rms.toFixed(0)}, ${tone.toFixed(0)} Hz beat`);

// (b) record twice; the second recording must prune the first without ever
// deleting the file being written
const recordOnce = async (name) => {
  send({ t: 'record', on: true, name });
  await waitFor(`${name} recording`, () => state.st.recording === true);
  await sleep(2000);
  send({ t: 'record', on: false });
  await waitFor(`${name} stopped`, () => state.st.recording === false);
};
await recordOnce('first');
const afterFirst = await (await fetch(`http://127.0.0.1:${port}/recordings`)).json();
if (afterFirst.length !== 1) await fail(`expected 1 recording, got ${JSON.stringify(afterFirst)}`);
await recordOnce('second');

const list = await (await fetch(`http://127.0.0.1:${port}/recordings`)).json();
if (list.length !== 1) await fail(`prune should have left 1 recording, got ${JSON.stringify(list)}`);
const rec = list[0];
if (!rec.name.startsWith('second_')) await fail(`prune kept the wrong recording: ${rec.name}`);
if (!(rec.bytes > 1e6) || !(rec.seconds > 1)) await fail(`recording looks truncated: ${JSON.stringify(rec)}`);
if (!state.stats || !(state.stats.pruned_bytes > 0)) await fail(`prune not reported in stats: ${JSON.stringify(state.stats)}`);
ok(`record x2 → prune kept the newest intact (${rec.name}, ${(rec.bytes / 1e6).toFixed(1)} MB, ${rec.seconds.toFixed(1)} s), pruned_bytes ${state.stats.pruned_bytes}`);

// downloads still work on the survivor
const head = await fetch(`http://127.0.0.1:${port}/recordings/${rec.name}.sigmf-data`);
const body = await head.arrayBuffer();
if (body.byteLength !== rec.bytes) await fail(`download is ${body.byteLength} bytes, listing says ${rec.bytes}`);
ok(`download byte-exact (${body.byteLength})`);

// (a) play it back: gen bumps, is_file, and pos advances
const genBeforePlay = state.gen;
send({ t: 'play', name: rec.name });
await waitFor('playback', () => state.st.is_file === true && state.st.source.startsWith('sigmf:'));
if (!(state.gen > genBeforePlay)) await fail(`gen did not bump on play (${genBeforePlay} → ${state.gen})`);
await waitFor('a position', () => state.stats?.pos !== undefined);
const pos0 = state.stats.pos;
await sleep(1500);
const pos1 = state.stats.pos;
if (!(pos1 > pos0)) await fail(`position did not advance (${pos0} → ${pos1})`);
ok(`play → is_file, gen ${genBeforePlay} → ${state.gen}, pos ${pos0.toFixed(2)} → ${pos1.toFixed(2)}`);

// switch back to sim; no frame may carry a gen newer than the state we hold, and
// once the switch settles every frame must be current
const genBeforeSim = state.gen;
send({ t: 'source', id: 'sim' });
await waitFor('sim again', () => state.st.is_file === false && state.st.source === 'sim');
if (!(state.gen > genBeforeSim)) await fail(`gen did not bump on switch (${genBeforeSim} → ${state.gen})`);
const settledAt = Date.now();
state.frames.length = 0;
await sleep(1000);
const ahead = state.frames.filter((f) => f.gen > state.gen);
const stale = state.frames.filter((f) => f.gen < state.gen && f.at > settledAt + 300);
if (ahead.length) await fail(`${ahead.length} frames carried a gen newer than the state`);
if (stale.length) await fail(`${stale.length} stale frames arrived more than 300 ms after the switch`);
if (state.frames.length < 10) await fail(`stream did not resume after the switch (${state.frames.length} frames)`);
ok(`switch back to sim: gen ${genBeforeSim} → ${state.gen}, ${state.frames.length} frames, none stale`);

if (state.errors.some((e) => e.code === 'source' || e.code === 'play')) {
  await fail('unexpected errors: ' + JSON.stringify(state.errors));
}
ws.close();
proc.kill();
await rm(dir, { recursive: true, force: true });
console.log('integration passed');
