// Smoke test for the wasm earshot demo: serves run/ like GitHub Pages (no COOP/COEP),
// expects the coi service worker reload, then a live connection, a hello from the
// wasm server and a double-click retune through the whole graph.
// usage: node earshot_smoke.mjs   (from tools/wasm-demos, after ./build.sh)
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { chromium } from '../cler-fg/app/node_modules/playwright/index.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const run = path.resolve(here, '../../docs/demos/run');
const port = 3311;

const server = spawn('python3', ['-m', 'http.server', String(port), '--bind', '127.0.0.1'], { cwd: run, stdio: 'ignore' });
const done = (code) => { server.kill(); process.exit(code); };

try {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(e.message));
  page.on('console', (m) => m.type() === 'error' && errors.push(m.text()));

  await page.goto(`http://127.0.0.1:${port}/earshot/index.html`);
  await page.waitForFunction(() => window.crossOriginIsolated, null, { timeout: 20_000 });
  console.log('cross-origin isolated');
  errors.length = 0;   // the pre-isolation first load fails on SharedArrayBuffer by design

  await page.waitForFunction(() => document.getElementById('conn')?.textContent === 'live', null, { timeout: 30_000 });
  console.log('connected: live');

  await page.waitForFunction(() => {
    const s = document.getElementById('s-ver');
    return s && s.textContent.includes('wasm');
  }, null, { timeout: 10_000 });
  console.log('hello: ' + await page.evaluate(() => document.getElementById('s-ver').textContent));

  const freqText = () => page.evaluate(() => document.getElementById('freq').value);
  const before = await freqText();
  await page.locator('#wf').dblclick({ position: { x: 200, y: 20 } });
  await page.waitForFunction((b) => document.getElementById('freq').value !== b, before, { timeout: 10_000 });
  console.log(`retune: ${before} -> ${await freqText()}`);

  await page.waitForTimeout(3000);
  const shot = path.join(here, 'out/smoke/earshot.png');
  await page.screenshot({ path: shot });
  console.log('screenshot: ' + shot);

  const state = await page.evaluate(() => ({
    conn: document.getElementById('conn')?.textContent,
    banner: document.getElementById('banner')?.textContent,
    bannerHidden: document.getElementById('banner')?.classList.contains('hidden'),
  }));
  console.log(JSON.stringify(state));
  if (errors.length) { console.log('console errors:', errors.slice(0, 10)); }
  await browser.close();
  done(state.conn === 'live' && state.bannerHidden && errors.length === 0 ? 0 : 1);
} catch (e) {
  console.error(e);
  done(1);
}
