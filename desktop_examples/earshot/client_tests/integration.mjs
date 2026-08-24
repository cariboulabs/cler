// Drives the real earshot binary over its own websocket on the simulator source:
// audio actually decodes, a recording round-trips through /recordings and plays
// back, the archive is pruned to its cap, and a source switch bumps gen without
// leaking stale frames.
// Needs nothing but node >= 22 (global WebSocket/fetch):
//   EARSHOT_BIN=build/desktop_examples/earshot/earshot node desktop_examples/earshot/client_tests/integration.mjs
import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync, utimesSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { setTimeout as sleep } from 'node:timers/promises';

const bin = process.env.EARSHOT_BIN;
if (!bin) { console.error('EARSHOT_BIN not set'); process.exit(2); }
if (typeof WebSocket === 'undefined') { console.log('SKIP: node has no global WebSocket (needs >= 22)'); process.exit(77); }

const CAP = 1_000_000;
const dir = mkdtempSync(join(tmpdir(), 'earshot-itest-'));
let proc = null;
process.on('exit', () => { proc?.kill(); rmSync(dir, { recursive: true, force: true }); });
// a ctest timeout arrives as a signal, which does not run the exit handler on its own
for (const sig of ['SIGINT', 'SIGTERM']) process.on(sig, () => process.exit(1));

// ports are guessed, so treat a server that never answers as a collision and move on
let port = 0;
for (let attempt = 0; attempt < 4 && !proc; ++attempt) {
  port = 18100 + Math.floor(Math.random() * 800);
  const p = spawn(bin, ['--source', 'sim', '--port', String(port), '--record-dir', dir,
                        '--record-max-bytes', String(CAP)], { stdio: ['ignore', 'pipe', 'pipe'] });
  let log = '';
  p.stdout.on('data', (d) => (log += d));
  p.stderr.on('data', (d) => (log += d));
  for (let i = 0; i < 60; ++i) {
    if (p.exitCode !== null) break;
    try { await fetch(`http://127.0.0.1:${port}/health`); proc = p; break; } catch { await sleep(100); }
  }
  if (!proc) { p.kill(); console.log(`port ${port} did not come up, retrying`); }
  else proc.log = () => log;
}
if (!proc) { console.error('server never came up'); process.exit(1); }

const fail = (msg) => {
  console.error('FAIL:', msg, '\n--- earshot log ---\n' + proc.log());
  process.exit(1);
};
const ok = (msg) => console.log('ok', msg);

const ws = new WebSocket(`ws://127.0.0.1:${port}/`);
ws.binaryType = 'arraybuffer';
const state = { st: null, stats: null, gen: 0, audio: [], frames: [], errors: [], runt: [] };
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
  if (b.byteLength < 11) { state.runt.push(b.byteLength); return; }
  const gen = new DataView(e.data).getUint32(2, true);
  state.frames.push({ type: b[0], gen, at: Date.now() });
  if (b[0] === 2) state.audio.push(new DataView(e.data, 11));
};
await new Promise((res, rej) => { ws.onopen = res; ws.onerror = () => rej(new Error('websocket failed')); })
  .catch((e) => fail(e.message));
ws.send(JSON.stringify({ t: 'hello', proto: 1 }));

const waitFor = async (what, pred, ms = 8000) => {
  for (let t = 0; t < ms; t += 50) { if (pred()) return; await sleep(50); }
  fail(`timed out waiting for ${what}`);
};
const send = (o) => ws.send(JSON.stringify(o));
const recordings = async () => (await fetch(`http://127.0.0.1:${port}/recordings`)).json();

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
if (state.audio.length < 10) fail(`only ${state.audio.length} audio frames arrived`);
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
if (rms < 1000) fail(`audio is silent (rms ${rms.toFixed(0)} over ${n} samples)`);
if (Math.abs(tone - beatHz) > 300) fail(`audio is not the expected ${beatHz} Hz beat (${tone.toFixed(0)} Hz)`);
ok(`audio decodes: ${state.audio.length} frames, rms ${rms.toFixed(0)}, ${tone.toFixed(0)} Hz beat`);

// (b) a recording round-trips
const record = async (name, ms) => {
  send({ t: 'record', on: true, name });
  await waitFor(`${name} recording`, () => state.st.recording === true);
  await sleep(ms);
  send({ t: 'record', on: false });
  await waitFor(`${name} stopped`, () => state.st.recording === false);
};
await record('keepme', 2000);
const list = await recordings();
if (list.length !== 1) fail(`expected 1 recording, got ${JSON.stringify(list)}`);
const rec = list[0];
if (!(rec.bytes > 1e5) || !(rec.seconds > 0.5)) fail(`recording looks truncated: ${JSON.stringify(rec)}`);
ok(`record → ${rec.name}, ${(rec.bytes / 1e6).toFixed(1)} MB, ${rec.seconds.toFixed(1)} s`);

const body = await (await fetch(`http://127.0.0.1:${port}/recordings/${rec.name}.sigmf-data`)).arrayBuffer();
if (body.byteLength !== rec.bytes) fail(`download is ${body.byteLength} bytes, listing says ${rec.bytes}`);
ok(`download byte-exact (${body.byteLength})`);

// pruning: plant an aged capture that puts the archive over the cap. Size is
// fixed rather than recorded, so a slow box cannot change the outcome.
const aged = join(dir, 'aged_capture');
writeFileSync(`${aged}.sigmf-data`, Buffer.alloc(4 * CAP));
writeFileSync(`${aged}.sigmf-meta`, JSON.stringify({
  global: { 'core:datatype': 'ci16_le', 'core:sample_rate': 1e6 },
  captures: [{ 'core:sample_start': 0 }], annotations: [],
}));
const hourAgo = new Date(Date.now() - 3600e3);
for (const f of [`${aged}.sigmf-data`, `${aged}.sigmf-meta`]) utimesSync(f, hourAgo, hourAgo);

// ...and one that started an hour ago but is still being appended to, the way a
// second earshot writing into the same directory would look. Only its data file
// is recent, which is exactly what the freshness check has to notice.
const growing = join(dir, 'growing_capture');
writeFileSync(`${growing}.sigmf-data`, Buffer.alloc(4 * CAP));
writeFileSync(`${growing}.sigmf-meta`, JSON.stringify({
  global: { 'core:datatype': 'ci16_le', 'core:sample_rate': 1e6 },
  captures: [{ 'core:sample_start': 0 }], annotations: [],
}));
utimesSync(`${growing}.sigmf-meta`, hourAgo, hourAgo);   // written once, at the start
const prunedBefore = state.stats?.pruned_bytes ?? 0;

await record('second', 1200);   // prune runs when a recording starts and every second
await waitFor('the prune to be reported', () => (state.stats?.pruned_bytes ?? 0) > prunedBefore, 6000);
const after = await recordings();
if (after.some((r) => r.name === 'aged_capture')) fail('the aged capture was not pruned');
if (!after.some((r) => r.name === rec.name)) fail(`the fresh recording ${rec.name} was pruned`);
if (!after.some((r) => r.name === 'growing_capture')) fail('a capture another writer is still appending to was pruned');
ok(`prune: aged capture gone, ${rec.name} and growing_capture kept, pruned_bytes ${state.stats.pruned_bytes}`);

// (a) play the recording back: gen bumps, is_file, and pos advances
const genBeforePlay = state.gen;
send({ t: 'play', name: rec.name });
await waitFor('playback', () => state.st.is_file === true && state.st.source.startsWith('sigmf:'));
if (!(state.gen > genBeforePlay)) fail(`gen did not bump on play (${genBeforePlay} → ${state.gen})`);
await waitFor('a position', () => state.stats?.pos !== undefined);
const pos0 = state.stats.pos;
await sleep(1500);
const pos1 = state.stats.pos;
if (!(pos1 > pos0)) fail(`position did not advance (${pos0} → ${pos1})`);
ok(`play → is_file, gen ${genBeforePlay} → ${state.gen}, pos ${pos0.toFixed(2)} → ${pos1.toFixed(2)}`);

// switch back to sim; no frame may carry a gen newer than the state we hold, and
// once the switch settles every frame must be current
const genBeforeSim = state.gen;
send({ t: 'source', id: 'sim' });
await waitFor('sim again', () => state.st.is_file === false && state.st.source === 'sim');
if (!(state.gen > genBeforeSim)) fail(`gen did not bump on switch (${genBeforeSim} → ${state.gen})`);
const settledAt = Date.now();
state.frames.length = 0;
await sleep(1000);
const ahead = state.frames.filter((f) => f.gen > state.gen);
const stale = state.frames.filter((f) => f.gen < state.gen && f.at > settledAt + 300);
if (ahead.length) fail(`${ahead.length} frames carried a gen newer than the state`);
if (stale.length) fail(`${stale.length} stale frames arrived more than 300 ms after the switch`);
if (state.frames.length < 10) fail(`stream did not resume after the switch (${state.frames.length} frames)`);
ok(`switch back to sim: gen ${genBeforeSim} → ${state.gen}, ${state.frames.length} frames, none stale`);

if (state.errors.some((e) => e.code === 'source' || e.code === 'play')) {
  fail('unexpected errors: ' + JSON.stringify(state.errors));
}
if (state.runt.length) fail(`binary frames shorter than a header: ${state.runt.join(',')}`);
ws.close();
proc.kill();   // the exit handler is the failure-path net; the child holds the loop open
console.log('integration passed');
