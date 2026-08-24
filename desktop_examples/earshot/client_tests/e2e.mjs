// Headless end-to-end check of the earshot client against the real binary on the
// simulator source. Exits 77 (ctest SKIP) when playwright is not installed:
//   EARSHOT_BIN=build/desktop_examples/earshot/earshot \
//   NODE_PATH=tools/flowgraph_gui/app/node_modules node desktop_examples/earshot/client_tests/e2e.mjs
import { spawn } from 'node:child_process';
import { createRequire } from 'node:module';
import { setTimeout as sleep } from 'node:timers/promises';

const require = createRequire(import.meta.url);
let chromium;
try { ({ chromium } = require('playwright')); } catch { console.log('SKIP: playwright not installed'); process.exit(77); }

const bin = process.env.EARSHOT_BIN;
if (!bin) { console.error('EARSHOT_BIN not set'); process.exit(2); }
const port = 18000 + Math.floor(Math.random() * 1000);
const proc = spawn(bin, ['--source', 'none', '--port', String(port)], { stdio: ['ignore', 'pipe', 'pipe'] });
process.on('exit', () => proc.kill());
let log = '';
proc.stdout.on('data', (d) => (log += d));
proc.stderr.on('data', (d) => (log += d));

const fail = (msg) => { console.error('FAIL:', msg, '\n--- earshot log ---\n' + log); proc.kill(); process.exit(1); };
const ok = (msg) => console.log('ok', msg);

for (let i = 0; i < 50; ++i) {
  try { await fetch(`http://127.0.0.1:${port}/health`); break; } catch { await sleep(100); }
  if (i === 49) fail('server never came up');
}

const browser = await chromium.launch();
const page = await browser.newPage();
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
await page.goto(`http://127.0.0.1:${port}/`);

const state = async () => JSON.parse(await page.evaluate(() => JSON.stringify(window.__earshot?.st ?? {})));

await page.waitForFunction(() => document.getElementById('conn')?.textContent === 'online', null, { timeout: 10_000 }).catch(() => fail('never online'));
ok('page loads, hello arrived');

await page.waitForFunction(() => [...document.querySelectorAll('#devlist .dev .lbl')].some((e) => e.textContent === 'Simulator'), null, { timeout: 5_000 }).catch(() => fail('Simulator not listed'));
ok('devices list shows Simulator');

await page.evaluate(() => { const row = [...document.querySelectorAll('#devlist .dev')].find((r) => r.querySelector('.lbl').textContent === 'Simulator'); row.querySelector('button').click(); });
await page.waitForFunction(() => document.getElementById('source')?.textContent.startsWith('sim'), null, { timeout: 5_000 }).catch(() => fail('source did not become sim'));
ok('Connect → state.source sim');

await sleep(2000);
const painted = await page.evaluate(() => {
  const c = document.getElementById('wf');
  const ctx = c.getContext('2d');
  const d = ctx.getImageData(0, 0, c.width, Math.min(c.height, 64)).data;
  let lit = 0;
  for (let i = 0; i < d.length; i += 4) if (d[i] + d[i + 1] + d[i + 2] > 30) ++lit;
  return lit;
});
if (painted < 100) fail(`waterfall not painted (${painted} lit pixels)`);
ok(`waterfall painted (${painted} lit pixels)`);

const before = (await page.$eval('#freq', (e) => e.value));
const box = await page.locator('#wf').boundingBox();
await page.mouse.dblclick(box.x + box.width * 0.7, box.y + box.height * 0.5);
await sleep(500);
const after = (await page.$eval('#freq', (e) => e.value));
if (before === after) fail(`dblclick did not change freq (${before})`);
ok(`dblclick → freq ${before} → ${after}`);

await page.locator('#modes button', { hasText: 'AM' }).click();
await page.waitForFunction(() => [...document.querySelectorAll('#modes button')].find((b) => b.classList.contains('on'))?.textContent === 'AM', null, { timeout: 5_000 }).catch(() => fail('mode did not become AM'));
ok('mode button → state mode AM');

if (errors.length) fail('page errors: ' + errors.join('; '));
await browser.close();
proc.kill();
console.log('e2e passed');
