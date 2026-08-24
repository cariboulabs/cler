// Headless end-to-end check of the earshot client against the real binary on the
// simulator source. Exits 77 (ctest SKIP) when playwright is not installed:
//   EARSHOT_BIN=build/desktop_examples/earshot/earshot \
//   NODE_PATH=tools/flowgraph_gui/app/node_modules node desktop_examples/earshot/client_tests/e2e.mjs
import { spawn } from 'node:child_process';
import { createRequire } from 'node:module';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { setTimeout as sleep } from 'node:timers/promises';

const require = createRequire(import.meta.url);
let chromium;
try { ({ chromium } = require('playwright')); } catch { console.log('SKIP: playwright not installed'); process.exit(77); }

const bin = process.env.EARSHOT_BIN;
if (!bin) { console.error('EARSHOT_BIN not set'); process.exit(2); }
const recDir = mkdtempSync(join(tmpdir(), 'earshot-e2e-'));
const port = 18000 + Math.floor(Math.random() * 1000);
const proc = spawn(bin, ['--source', 'none', '--port', String(port), '--record-dir', recDir],
                   { stdio: ['ignore', 'pipe', 'pipe'] });
const cleanup = () => { proc.kill(); try { rmSync(recDir, { recursive: true, force: true }); } catch {} };
process.on('exit', cleanup);
let log = '';
proc.stdout.on('data', (d) => (log += d));
proc.stderr.on('data', (d) => (log += d));

const fail = (msg) => { console.error('FAIL:', msg, '\n--- earshot log ---\n' + log); cleanup(); process.exit(1); };
const ok = (msg) => console.log('ok', msg);

for (let i = 0; i < 50; ++i) {
  try { await fetch(`http://127.0.0.1:${port}/health`); break; } catch { await sleep(100); }
  if (i === 49) fail('server never came up');
}

const browser = await chromium.launch();
const context = await browser.newContext();
// keep every socket the page opens so a protocol-mismatch frame can be injected
// into the real handler without a test-only hook in the client
await context.addInitScript(() => {
  window.__sockets = [];
  const Real = window.WebSocket;
  window.WebSocket = function (...a) { const s = new Real(...a); window.__sockets.push(s); return s; };
  window.WebSocket.prototype = Real.prototype;
  Object.assign(window.WebSocket, Real);
});
const page = await context.newPage();
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
await page.goto(`http://127.0.0.1:${port}/`);

await page.waitForFunction(() => document.getElementById('conn')?.textContent === 'live', null, { timeout: 10_000 }).catch(() => fail('never live'));
ok('page loads, hello arrived');

await page.waitForSelector('[data-testid=dlg-sources][open]', { timeout: 5_000 }).catch(() => fail('sources dialog did not open with no source'));
await page.waitForFunction(() => [...document.querySelectorAll('#devlist .row .lbl')].some((e) => e.textContent === 'Simulator'), null, { timeout: 5_000 }).catch(() => fail('Simulator not listed'));
ok('sources dialog lists Simulator');

await page.evaluate(() => { const row = [...document.querySelectorAll('#devlist .row')].find((r) => r.querySelector('.lbl').textContent === 'Simulator'); row.querySelector('button').click(); });
await page.waitForFunction(() => document.getElementById('source')?.textContent.startsWith('sim'), null, { timeout: 5_000 }).catch(() => fail('source did not become sim'));
ok('Connect → state.source sim');

await sleep(2000);
const painted = await page.evaluate(() => {
  const c = document.getElementById('wf');
  const d = c.getContext('2d').getImageData(0, 0, c.width, Math.min(c.height, 64)).data;
  let lit = 0;
  for (let i = 0; i < d.length; i += 4) if (d[i] + d[i + 1] + d[i + 2] > 30) ++lit;
  return lit;
});
if (painted < 100) fail(`waterfall not painted (${painted} lit pixels)`);
ok(`waterfall painted (${painted} lit pixels)`);

const before = await page.$eval('#freq', (e) => e.value);
const box = await page.locator('#wf').boundingBox();
await page.mouse.dblclick(box.x + box.width * 0.7, box.y + box.height * 0.5);
await sleep(500);
const after = await page.$eval('#freq', (e) => e.value);
if (before === after) fail(`dblclick did not change freq (${before})`);
ok(`dblclick → freq ${before} → ${after}`);

await page.locator('[data-testid=mode-AM]').click();
await page.waitForFunction(() => document.querySelector('[data-testid=mode-AM]')?.classList.contains('on'), null, { timeout: 5_000 }).catch(() => fail('mode did not become AM'));
ok('mode button → state mode AM');

// two decoders at once, from the ⋯ menu, with the running cost shown
await page.locator('[data-testid=tab-decoded]').click();
await page.locator('[data-testid=decoder-menu]').click();
await page.locator('[data-testid=decoder-rds]').check();
await sleep(400);
await page.locator('[data-testid=decoder-aprs]').check();
await page.waitForFunction(() => (window.__earshot?.st?.decoders || []).length === 2, null, { timeout: 5_000 })
  .catch(() => fail('two decoders did not run at once'));
const chips = await page.locator('#dectabs .chip').count();
if (chips !== 2) fail(`expected a chip per running decoder, got ${chips}`);
const cost = await page.locator('[data-testid=decoder-cost]').textContent();
if (!/decoders running/.test(cost)) fail(`cost line reads "${cost}"`);
ok(`two decoders running, ${chips} chips, cost "${cost.trim()}"`);

// an unavailable decoder explains itself in text, not only in a tooltip
const adsbDisabled = await page.locator('[data-testid=decoder-adsb]').isDisabled();
const adsbReason = await page.locator('#decmenu-items .reason').first().textContent();
if (!adsbDisabled || !/unavailable/.test(adsbReason)) fail(`adsb not explained: disabled=${adsbDisabled} reason="${adsbReason}"`);
ok('adsb is disabled and says why in text');
await page.locator('[data-testid=decoder-menu]').click();

// record → the dialog lists it → delete round trip
await page.locator('[data-testid=tab-receiver]').click();
const preview = await page.locator('[data-testid=rec-preview]').textContent();
if (!/\.sigmf-data$/.test(preview.trim())) fail(`no name preview before recording: "${preview}"`);
await page.locator('[data-testid=record]').click();
await page.waitForFunction(() => window.__earshot?.st?.recording === true, null, { timeout: 5_000 }).catch(() => fail('recording did not start'));
await sleep(1500);
await page.locator('[data-testid=record]').click();
await page.waitForFunction(() => window.__earshot?.st?.recording === false, null, { timeout: 5_000 }).catch(() => fail('recording did not stop'));
ok(`record round trip, preview was "${preview.trim()}"`);

await page.locator('[data-testid=menu]').click();
await page.locator('[data-testid=menu-recordings]').click();
await page.waitForSelector('[data-testid=dlg-recordings][open]', { timeout: 5_000 }).catch(() => fail('recordings dialog did not open'));
await page.waitForFunction(() => document.querySelectorAll('#reclist .row').length === 1, null, { timeout: 5_000 })
  .catch(() => fail('the recording was not listed'));
const name = await page.$eval('#reclist .row .mono', (e) => e.textContent);
ok(`recordings dialog lists ${name}`);

// destructive, so it confirms first
const del = page.locator(`[data-testid="delete-${name}"]`);
await del.click();
if ((await del.textContent()) !== 'Confirm') fail('delete did not ask for confirmation');
await del.click();
await page.waitForFunction(() => document.querySelectorAll('#reclist .row').length === 0, null, { timeout: 5_000 })
  .catch(() => fail('the recording was not deleted'));
ok('delete confirms once, then removes the capture');
await page.locator('[data-testid=recordings-close]').click();

// narrow: the dialog must be on screen and the panel reachable
await page.setViewportSize({ width: 480, height: 900 });
await sleep(400);
await page.locator('[data-testid=menu]').click();
await page.locator('[data-testid=menu-sources]').click();
await sleep(300);
const dlg = await page.locator('[data-testid=dlg-sources]').boundingBox();
if (dlg.x < 0 || dlg.x + dlg.width > 480) fail(`dialog off screen at 480px: x=${dlg.x} w=${dlg.width}`);
await page.locator('[data-testid=sources-close]').click();
const panel = await page.locator('aside').boundingBox();
if (!panel || panel.width < 300) fail(`panel not reachable at 480px: ${JSON.stringify(panel)}`);
ok(`narrow viewport: dialog at x=${Math.round(dlg.x)} w=${Math.round(dlg.width)}, panel ${Math.round(panel.width)}px wide`);
await page.setViewportSize({ width: 1280, height: 720 });

// a viewer is refused with a reason, never by a silent no-op
const viewer = await context.newPage();
await viewer.goto(`http://127.0.0.1:${port}/`);
await viewer.waitForFunction(() => document.getElementById('role')?.textContent === 'viewer', null, { timeout: 10_000 })
  .catch(() => fail('second tab did not become a viewer'));
for (const id of ['freq', 'record']) {
  const el = viewer.locator(`[data-testid=${id}]`);
  if (!(await el.isDisabled())) fail(`${id} is not disabled for a viewer`);
  const title = await el.getAttribute('title');
  if (!title || !/controller/.test(title)) fail(`${id} disabled without a reason (title="${title}")`);
}
await viewer.locator('[data-testid=tab-decoded]').click();
await viewer.locator('[data-testid=decoder-menu]').click();
const vAis = viewer.locator('[data-testid=decoder-ais]');
if (!(await vAis.isDisabled())) fail('a viewer can start a decoder');
if (!/controller/.test((await vAis.getAttribute('title')) || '')) fail('viewer decoder checkbox has no reason');
// listening is this browser's business, not the controller's
if (await viewer.locator('[data-testid=listen]').isDisabled()) fail('a viewer cannot listen');
if (await viewer.locator('[data-testid=volume]').isDisabled()) fail('a viewer cannot set volume');
ok('viewer: tune/record/decoders disabled with reasons, audio still theirs');
await viewer.close();

// a fatal error outlives a toast
await page.evaluate(() => {
  const b = new Uint8Array(10); b[0] = 1; b[1] = 2;      // spectrum frame, protocol 2
  window.__sockets.at(-1).dispatchEvent(new MessageEvent('message', { data: b.buffer }));
});
await page.waitForSelector('[data-testid=banner]:not(.hidden)', { timeout: 5_000 }).catch(() => fail('no banner for a protocol mismatch'));
await sleep(6000);   // longer than a toast lives
if (await page.locator('[data-testid=banner]').isHidden()) fail('the fatal banner vanished like a toast');
ok('fatal banner persists past a toast lifetime');

if (errors.length) fail('page errors: ' + errors.join('; '));
await browser.close();
cleanup();
console.log('e2e passed');
