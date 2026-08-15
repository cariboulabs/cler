// End-to-end check of the browser edition: serves docs/ with no COOP/COEP headers (as GitHub
// Pages does), then edits, checks, builds and runs hello_world entirely in the browser.
// usage: node ../web-build/smoke.mjs   (from tools/flowgraph_gui/app, after `npm run build:web`)
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { chromium } from '../app/node_modules/playwright/index.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const docs = path.resolve(here, '../../../docs');
const shots = path.join(here, 'out/smoke');
const port = 3310;

const server = spawn('python3', ['-m', 'http.server', String(port), '--bind', '127.0.0.1'], {
  cwd: docs,
  stdio: 'ignore'
});
const done = (code) => {
  server.kill();
  process.exit(code);
};
process.on('uncaughtException', (error) => {
  console.error(error);
  done(1);
});

const browser = await chromium.launch({
  headless: true,
  args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader']
});
const context = await browser.newContext();
// /try must be self-contained: the toolchain lives in public/emception/, so any hit on the
// upstream base is a missing vendored file, not a fallback worth exercising.
await context.route('https://jprendes.github.io/**', (route) => route.abort());
const page = await context.newPage();
page.on('pageerror', (error) => console.log('[pageerror]', error.message));
page.on('console', (message) => message.type() === 'error' && console.log('[console]', message.text()));
if (process.env.SMOKE_TRACE) page.on('response', (r) => console.log(r.status(), r.url()));

const clock = (label, work) =>
  work().then((value) => {
    console.log(`${label}: ${((Date.now() - clock.t0) / 1000).toFixed(1)}s`);
    return value;
  }, (error) => { console.log(`${label}: FAILED`); throw error; });
const timed = async (label, work) => { clock.t0 = Date.now(); return clock(label, work); };
const output = () => page.getByTestId('output-body').innerText();

await page.goto(`http://127.0.0.1:${port}/try/`);
await page.waitForFunction(() => window.crossOriginIsolated, null, { timeout: 30_000 });
await page.locator('.svelte-flow__node').first().waitFor({ timeout: 30_000 });
console.log('editor loaded, cross-origin isolated');

// Untouched, hello_world still runs from its prebuilt run/hello_world.html.
await page.waitForFunction(() => !document.querySelector('[data-testid="run"]')?.disabled, null, { timeout: 15_000 });
console.log('pristine example: Run enabled from the prebuilt run/hello_world.html');

// Edit the document so it is no longer the pristine bundle: Run must now go through a real build.
const field = page.locator('input[data-field="source1.ctor.1"]');
await page.locator('.svelte-flow__node', { hasText: 'source1' }).first().click();
await field.fill('2.5');
await field.press('Enter');
await field.blur();

// Offline cold: the toolchain cannot arrive, so Check must say so and stay retryable.
await context.setOffline(true);
await timed('check (offline, must fail fast)', async () => {
  await page.getByTestId('check').click();
  await page.getByTestId('tab-output').click();
  await page.waitForFunction(
    () => document.querySelector('[data-testid="output-body"]')?.textContent?.includes('check finished'),
    null,
    { timeout: 120_000 }
  );
});
const offline = await output();
if (!/cannot reach the C\+\+ toolchain|did not start|worker failed/.test(offline)) {
  throw new Error(`offline check gave no clear reason:\n${offline}`);
}
if (offline.includes('check finished (exit 0)')) throw new Error('offline check reported success');
await context.setOffline(false);

// The progress panel must name the phase actually running: cold path starts with the download.
const phaseSeen = new Set();
const watchPhases = setInterval(async () => {
  const phase = await page
    .evaluate(() => document.querySelector('[data-testid="progress-phase"]')?.getAttribute('data-phase') ?? null)
    .catch(() => null);
  if (phase) phaseSeen.add(phase);
}, 200);

await timed('check (cold, includes the ~25 MB toolchain)', async () => {
  await page.getByTestId('check').click();
  await page.getByTestId('progress-phase').waitFor({ timeout: 15_000 });
  await page.waitForFunction(
    () => document.querySelector('[data-testid="progress-phase"]')?.getAttribute('data-phase') === 'toolchain',
    null,
    { timeout: 60_000 }
  );
  console.log(`progress: ${await page.getByTestId('progress-phase').innerText()}`);
  await page.screenshot({ path: path.join(shots, 'progress.png') });
  await page.getByTestId('drawer-close').click();
  await page.getByTestId('progress-open').waitFor({ timeout: 5000 });
  await page.waitForTimeout(400);
  await page.screenshot({ path: path.join(shots, 'progress-pill.png') });
  await page.getByTestId('progress-open').click();
  await page.getByTestId('drawer-close').waitFor({ timeout: 5000 });
  await page.getByTestId('tab-output').click();
  await page.waitForFunction(
    () => document.querySelector('[data-testid="output-body"]')?.textContent?.includes('check finished'),
    null,
    { timeout: 300_000 }
  );
});
clearInterval(watchPhases);
const checked = await output();
for (const wanted of ['toolchain', 'compile']) {
  if (!phaseSeen.has(wanted)) throw new Error(`progress panel never showed the ${wanted} phase (saw ${[...phaseSeen]})`);
}
console.log(`progress phases seen: ${[...phaseSeen].join(', ')}`);

if (!checked.includes('check finished (exit 0)')) throw new Error(`check failed:\n${checked}`);

await timed('build (compile + link)', async () => {
  await page.getByTestId('build').click();
  const fold = page
    .waitForFunction(
      () => document.querySelector('[data-testid="progress-phase"]')?.getAttribute('data-phase') === 'done',
      null,
      { timeout: 300_000 }
    )
    .then(() => page.screenshot({ path: path.join(shots, 'progress-done.png') }))
    .catch(() => console.log('missed the success flash'));
  await page.waitForFunction(
    () =>
      ['compile', 'link', 'optimize'].includes(
        document.querySelector('[data-testid="progress-phase"]')?.getAttribute('data-phase') ?? ''
      ),
    null,
    { timeout: 120_000 }
  );
  console.log(`progress: ${await page.getByTestId('progress-phase').innerText()}`);
  await page.screenshot({ path: path.join(shots, 'progress-build.png') });
  await page.getByTestId('tab-output').click();
  await page.waitForFunction(
    () => document.querySelector('[data-testid="output-body"]')?.textContent?.includes('build finished'),
    null,
    { timeout: 300_000 }
  );
  await fold;
});
const builtLog = await output();
if (!builtLog.includes('build finished (exit 0)')) throw new Error(`build failed:\n${builtLog.slice(-4000)}`);
await page.getByTestId('run').waitFor({ state: 'visible' });
await page.waitForFunction(() => !document.querySelector('[data-testid="run"]')?.disabled, null, { timeout: 30_000 });

// Already built: Build must recognise the cached artifact instead of linking again.
await timed('build (already cached)', async () => {
  await page.getByTestId('build').click();
  await page.waitForFunction(
    () => document.querySelector('[data-testid="output-body"]')?.textContent?.includes('already built'),
    null,
    { timeout: 30_000 }
  );
});

const popupPromise = context.waitForEvent('page');
await page.getByTestId('run').click();
const popup = await popupPromise;
popup.on('pageerror', (error) => console.log('[popup pageerror]', error.message));
await popup.waitForLoadState();
if (!popup.url().includes('/built/')) throw new Error(`run opened ${popup.url()}, expected built/`);
if (!(await popup.evaluate(() => window.crossOriginIsolated))) throw new Error('run window is not cross-origin isolated');
await popup.waitForFunction(() => !document.getElementById('status'), null, { timeout: 120_000 });
await popup.waitForTimeout(3000);
await popup.screenshot({ path: path.join(shots, 'run.png') });
console.log(`run: ${popup.url().split('/try/')[1]} rendering, screenshot in ${shots}/run.png`);
await popup.close();

// A syntax error must land on the edited line, in the document's own path.
await field.fill('2.5ff');
await field.press('Enter');
await field.blur();
await timed('check (warm, syntax error)', async () => {
  await page.getByTestId('check').click();
  await page.getByTestId('tab-diagnostics').click();
  await page.locator('[data-diagnostic]').first().waitFor({ timeout: 300_000 });
});
const diagnostic = await page.locator('[data-diagnostic]').first().innerText();
if (!/invalid suffix/.test(diagnostic)) throw new Error(`unexpected diagnostic: ${diagnostic}`);
const blamed = await page.locator('[data-diagnostic-block]').first().innerText();
if (blamed !== 'source1') throw new Error(`diagnostic blamed ${blamed}, expected source1`);
console.log(`diagnostics: "${diagnostic.split('\n')[0]}" on ${blamed}`);
await page.getByTestId('progress-diagnostics').waitFor({ timeout: 5000 });
await page.screenshot({ path: path.join(shots, 'progress-fail.png') });

await page.screenshot({ path: path.join(shots, 'editor.png') });
await browser.close();
console.log('smoke OK');
done(0);
