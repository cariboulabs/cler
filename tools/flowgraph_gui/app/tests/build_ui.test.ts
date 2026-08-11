import { describe, expect, it } from 'vitest';
import type { Page } from 'playwright';
import { fixtures, fixtureSources } from '../src/fixtures';
import { lineOfOffset } from '../src/lib/schema';
import {
  boot,
  BUILDABLE,
  calls,
  CASE,
  emit,
  FAKE_PATH,
  highlighted,
  openLibrary,
  shot,
  useBrowser,
  viewer
} from './ui';

useBrowser();

function declarationLine(fixture: string, blockVar: string): number {
  const block = fixtures[fixture]?.sites[0]?.blocks.find((candidate) => candidate.var === blockVar);
  const source = fixtureSources[fixture];
  if (!block || source === undefined) throw new Error(`no ${blockVar} in ${fixture}`);
  return lineOfOffset(source, block.span.start);
}

async function ran(page: Page, command: string): Promise<number> {
  return (await calls(page)).filter((name) => name === command).length;
}

async function streamCheck(page: Page, lines: string[], code: number) {
  for (const line of lines) await emit(page, 'check-output', { path: FAKE_PATH, line });
  await emit(page, 'check-finished', { path: FAKE_PATH, code });
}

const BAD_LITERAL = 'unable to find numeric literal operator ‘operator""ff’';

/* ============================================================ the panel */

describe('the drawer carries code, diagnostics and output', () => {
  it(
    'shows three tabs and switches between them',
    async () => {
      const page = await boot();
      await page.keyboard.press('Control+`');
      await page.waitForSelector('[data-testid="drawer-tabs"]');

      expect(await page.locator('[data-testid="drawer-tabs"] button').count()).toBe(3);
      expect(await page.getByTestId('tab-code').getAttribute('aria-pressed')).toBe('true');

      await page.click('[data-testid="tab-diagnostics"]');
      expect(await page.textContent('[data-testid="diagnostics-empty"]')).toContain('press F7');
      expect(await page.locator('[data-testid="drawer-body"]').isVisible()).toBe(false);

      await page.click('[data-testid="tab-output"]');
      expect(await page.textContent('[data-testid="output-body"]')).toContain('no output yet');

      await page.click('[data-testid="tab-code"]');
      expect(await page.locator('[data-testid="drawer-body"]').isVisible()).toBe(true);
      await shot(page, 'drawer-tabs');
      await page.close();
    },
    CASE
  );

  it(
    'F7 checks the file, streams the output and names the owning block',
    async () => {
      const page = await boot();
      const line = declarationLine('hello_world', 'source1');

      await page.keyboard.press('F7');
      await expect.poll(() => ran(page, 'check_document')).toBe(1);
      await page.waitForSelector('[data-testid="drawer-busy"]');

      await streamCheck(
        page,
        [
          `${FAKE_PATH}: In function ‘int main()’:`,
          `${FAKE_PATH}:${line}:46: error: ${BAD_LITERAL}`,
          `${FAKE_PATH}:${line}:46: note: use ‘-fext-numeric-literals’ to enable more built-in suffixes`
        ],
        1
      );

      const row = page.locator('[data-diagnostic]');
      await row.first().waitFor();
      expect(await row.count()).toBe(1);
      expect(await row.first().textContent()).toContain(BAD_LITERAL);
      expect(await page.textContent('[data-diagnostic-block]')).toBe('source1');
      expect(await page.locator('[data-testid="drawer-busy"]').count()).toBe(0);
      await shot(page, 'diagnostics');

      await page.click('[data-testid="tab-output"]');
      const stream = await page.textContent('[data-testid="output-body"]');
      expect(stream).toContain('In function');
      expect(stream).toContain('check finished (exit 1)');

      await page.click('[data-testid="tab-diagnostics"]');
      await row.first().click();
      expect(await page.getByTestId('tab-code').getAttribute('aria-pressed')).toBe('true');
      expect(await highlighted(page)).toContain('SourceCWBlock<float> source1');
      await page.waitForSelector('input[data-field="source1.ctor.1"]');
      await page.close();
    },
    CASE
  );

  it(
    'keeps a diagnostic from another file listed but unattached',
    async () => {
      const page = await boot();
      await page.keyboard.press('F7');
      await streamCheck(
        page,
        ['/usr/include/c++/11/vector:120:7: error: template argument 1 is invalid'],
        1
      );

      const row = page.locator('[data-diagnostic]');
      await row.first().waitFor();
      expect(await page.locator('[data-diagnostic-block]').count()).toBe(0);
      expect(await row.first().textContent()).toContain('vector:120');
      expect(await page.getAttribute('[data-testid="problems"]', 'data-count')).toBe('0');
      await page.close();
    },
    CASE
  );
});

/* ============================================================ the chip */

describe('the problems chip merges the compiler with the model', () => {
  it(
    'counts both and lists them under their own headings',
    async () => {
      const page = await boot({ fixture: 'type_conflict' });
      const chip = page.locator('[data-testid="problems"]');
      expect(await chip.getAttribute('data-count')).toBe('1');

      await page.keyboard.press('F7');
      await streamCheck(
        page,
        [
          `${FAKE_PATH}:1:1: error: ${BAD_LITERAL}`,
          `${FAKE_PATH}:2:1: warning: unused variable ‘gui’`
        ],
        1
      );

      await expect.poll(() => chip.getAttribute('data-count')).toBe('3');
      await chip.click();
      await page.waitForSelector('[data-testid="problems-list"]');
      expect(await page.locator('[data-testid="section-compiler"]').count()).toBe(1);
      expect(await page.locator('[data-testid="section-graph"]').count()).toBe(1);
      expect(await page.locator('[data-testid="problems-list"] button').count()).toBe(3);
      await shot(page, 'merged-problems');
      await page.close();
    },
    CASE
  );
});

/* ============================================================ build and run */

describe('build and run follow what find_target reports', () => {
  it(
    'stays disabled with the reason as its tooltip when there is no target',
    async () => {
      const page = await boot();
      expect(await page.locator('[data-testid="check"]').isDisabled()).toBe(false);
      expect(await page.locator('[data-testid="build"]').isDisabled()).toBe(true);
      expect(await page.locator('[data-testid="run"]').isDisabled()).toBe(true);
      expect(await page.textContent('[data-testid="build-tooltip"]')).toContain(
        'desktop_examples'
      );

      await page.keyboard.press('Control+b');
      await page.keyboard.press('Control+r');
      await page.waitForTimeout(200);
      expect(await ran(page, 'build_target')).toBe(0);
      expect(await ran(page, 'run_target')).toBe(0);
      await page.close();
    },
    CASE
  );

  it(
    'builds with Ctrl+B and toggles the child with Ctrl+R',
    async () => {
      const page = await boot({ target: BUILDABLE });
      await expect.poll(() => page.locator('[data-testid="build"]').isDisabled()).toBe(false);

      await page.keyboard.press('Control+b');
      await expect.poll(() => ran(page, 'build_target')).toBe(1);
      await emit(page, 'build-output', { path: FAKE_PATH, line: '[100%] Built target hello_world' });
      await emit(page, 'build-finished', { path: FAKE_PATH, code: 0 });
      await page.waitForSelector('[data-testid="output-body"]');
      expect(await page.textContent('[data-testid="output-body"]')).toContain('Built target');

      await page.keyboard.press('Control+r');
      await expect.poll(() => ran(page, 'run_target')).toBe(1);
      await page.waitForSelector('[data-testid="run"].live');
      expect(await page.getAttribute('[data-testid="run"]', 'aria-label')).toBe('Stop');
      await shot(page, 'running');

      await emit(page, 'run-output', { path: FAKE_PATH, line: 'hello from the flowgraph' });
      await expect
        .poll(() => page.textContent('[data-testid="output-body"]'))
        .toContain('hello from the flowgraph');

      await page.keyboard.press('Control+r');
      await expect.poll(() => ran(page, 'stop_target')).toBe(1);
      await emit(page, 'run-finished', { path: FAKE_PATH, code: null });
      await expect.poll(() => page.locator('[data-testid="run"].live').count()).toBe(0);
      expect(await page.textContent('[data-testid="output-body"]')).toContain(
        'run finished (exit signal)'
      );
      await page.close();
    },
    CASE
  );

  it(
    'ignores the shortcuts while a field has focus',
    async () => {
      const page = await boot({ target: BUILDABLE });
      await openLibrary(page);
      await page.click('[data-testid="palette-search"]');

      await page.keyboard.press('F7');
      await page.keyboard.press('Control+b');
      await page.keyboard.press('Control+r');
      await page.waitForTimeout(300);

      expect(await ran(page, 'check_document')).toBe(0);
      expect(await ran(page, 'build_target')).toBe(0);
      expect(await ran(page, 'run_target')).toBe(0);
      await page.close();
    },
    CASE
  );

  it(
    'offers all three in the canvas context menu',
    async () => {
      const page = await boot({ target: BUILDABLE });
      await page.locator('.svelte-flow__pane').click({ button: 'right', position: { x: 40, y: 40 } });
      await page.waitForSelector('[data-testid="context-menu"]');
      expect(await page.locator('[data-testid="menu-check"]').isDisabled()).toBe(false);
      expect(await page.locator('[data-testid="menu-build"]').isDisabled()).toBe(false);
      await page.click('[data-testid="menu-run"]');
      await expect.poll(() => ran(page, 'run_target')).toBe(1);
      await page.close();
    },
    CASE
  );

  it(
    'refuses all three in example mode with the viewer note',
    async () => {
      const page = await viewer('?example=hello_world');
      await page.waitForSelector('.svelte-flow__node');

      for (const id of ['check', 'build', 'run']) {
        expect(await page.locator(`[data-testid="${id}"]`).isDisabled()).toBe(true);
        expect(await page.textContent(`[data-testid="${id}-tooltip"]`)).toContain('example mode');
      }

      await page.keyboard.press('F7');
      await page.waitForTimeout(200);
      expect(await page.locator('[data-testid="drawer-tabs"]').count()).toBe(0);
      await page.close();
    },
    CASE
  );
});
