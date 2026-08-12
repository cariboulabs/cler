import { describe, expect, it } from 'vitest';
import {
  boot,
  calls,
  CASE,
  centre,
  handle,
  modelOf,
  openLibrary,
  settledZoom,
  shot,
  styleOf,
  token,
  useBrowser,
  viewer,
  widthOf,
  zoomOf
} from './ui';
import type { FileModel } from '../src/lib/schema';

useBrowser();

const RAIL = 44;
const SIDEBAR = 280;
const INSPECTOR = 320;

function siteless(): FileModel {
  const model = modelOf('hello_world');
  model.sites = [];
  return model;
}

/* ============================================================ the empty state */

describe('the empty-state card carries the honest reason', () => {
  it(
    'shows the bundled graph immediately without a first-run banner',
    async () => {
      const page = await viewer('', 'first-run');
      await page.waitForSelector('.svelte-flow__node');
      expect(await widthOf(page, '.inspector')).toBeCloseTo(RAIL, 0);
      expect(await page.locator('[data-testid="empty-state"]').count()).toBe(0);
      expect(await page.locator('[data-testid="demo-chip"]').count()).toBe(1);
      expect(await page.locator('[data-testid="palette-notice"]').count()).toBe(0);
      expect(await page.locator('[data-testid="viewer-note"]').count()).toBe(0);
      expect(await page.locator('aside.sidebar button.primary').count()).toBe(0);
      await shot(page, 'first-run-empty-state');

      await page.click('[data-testid="file-menu"]');
      await page.waitForSelector('[data-testid="file-menu-list"]');
      await shot(page, 'file-menu-open');
      await page.click('[data-testid="file-open-example"]');
      await page.click('[data-example="plots"]');
      await page.waitForSelector('.svelte-flow__node[data-id="cw_throttle"]');
      await page.close();
    },
    CASE
  );

  it(
    'opens the bundled example as an editable desktop document on startup',
    async () => {
      const page = await boot();
      expect((await calls(page)).filter((name) => name === 'open_document')).toHaveLength(1);
      expect(await page.locator('[data-testid="demo-chip"]').count()).toBe(0);
      await page.click('.svelte-flow__node[data-id="source1"]');
      await page.waitForSelector('.inspector input');
      expect(await page.locator('.inspector input').first().isDisabled()).toBe(false);
      await page.close();
    },
    CASE
  );

  it(
    'says a file has no flowgraph site in the card, not in a red chip in the corner',
    async () => {
      const page = await boot({ model: siteless(), empty: true });
      expect(await page.textContent('[data-testid="empty-reason"]')).toBe(
        'no flowgraph site found in this file'
      );
      expect(await page.locator('[data-testid="status"]').count()).toBe(0);
      expect(await page.locator('.sidebar h2', { hasText: 'Read-only' }).count()).toBe(0);
      expect(await page.locator('.sidebar h2', { hasText: 'Graph' }).count()).toBe(0);
      await page.close();
    },
    CASE
  );

  it(
    'explains a refused open in the card',
    async () => {
      const page = await boot({
        openError: JSON.stringify({ error: 'file_has_errors' }),
        empty: true
      });
      expect(await page.textContent('[data-testid="empty-reason"]')).toContain('parse errors');
      expect(await page.locator('[data-testid="empty-open"]').count()).toBe(1);
      await page.close();
    },
    CASE
  );
});

/* ================================================================ hide chrome */

describe('the chrome gets out of the way', () => {
  it(
    'Ctrl+backslash folds both rails and the drawer away, and gives them back',
    async () => {
      const page = await boot();
      await page.keyboard.press('Control+`');
      await page.waitForSelector('[data-testid="drawer-body"] .row');
      await expect.poll(() => widthOf(page, '.sidebar')).toBeCloseTo(SIDEBAR, 0);
      await expect.poll(() => widthOf(page, '.inspector')).toBeCloseTo(INSPECTOR, 0);

      await page.keyboard.press('Control+\\');
      await expect.poll(() => widthOf(page, '.sidebar'), { timeout: 3000 }).toBeCloseTo(RAIL, 0);
      await expect.poll(() => widthOf(page, '.inspector'), { timeout: 3000 }).toBeCloseTo(RAIL, 0);
      await expect
        .poll(() => widthOf(page, '[data-testid="code-drawer"]'), { timeout: 3000 })
        .toBeGreaterThan(0);
      await expect
        .poll(
          () =>
            page.evaluate(() => {
              const drawer = document.querySelector('[data-testid="code-drawer"]');
              return drawer instanceof HTMLElement ? drawer.getBoundingClientRect().height : -1;
            }),
          { timeout: 3000 }
        )
        .toBeLessThanOrEqual(1);
      await shot(page, 'chrome-hidden');

      await page.keyboard.press('Control+\\');
      await expect.poll(() => widthOf(page, '.sidebar'), { timeout: 3000 }).toBeCloseTo(SIDEBAR, 0);
      await expect
        .poll(() => widthOf(page, '.inspector'), { timeout: 3000 })
        .toBeCloseTo(INSPECTOR, 0);
      await expect
        .poll(
          () =>
            page.evaluate(() => {
              const drawer = document.querySelector('[data-testid="code-drawer"]');
              return drawer instanceof HTMLElement ? drawer.getBoundingClientRect().height : -1;
            }),
          { timeout: 3000 }
        )
        .toBeGreaterThan(1);
      await page.close();
    },
    CASE
  );

  it(
    'keeps the viewport fixed while the floating rails open and close',
    async () => {
      const page = await boot();
      await page.click('[data-testid="zoom-in"]');
      await page.waitForTimeout(300);
      const viewport = () =>
        page.evaluate(
          () =>
            (document.querySelector('.svelte-flow__viewport') as HTMLElement | null)?.style
              .transform ?? 'none'
        );
      const before = await viewport();

      await page.click('[data-testid="toggle-left"]');
      await page.click('[data-testid="toggle-right"]');
      await page.waitForTimeout(500);

      expect(await viewport()).toBe(before);
      await page.close();
    },
    CASE
  );

  it(
    'fits the whole window, so a five-block graph is legible at 1440x900',
    async () => {
      const page = await viewer('?fixture=hello_world', 'first-run');
      await page.waitForSelector('.svelte-flow__node');
      expect(await widthOf(page, '.inspector')).toBeCloseTo(RAIL, 0);
      const fitted = await settledZoom(page);
      expect(fitted).toBeGreaterThanOrEqual(0.9);
      expect(await page.locator('.svelte-flow__minimap').count()).toBe(0);
      expect(await page.locator('.svelte-flow__controls').count()).toBe(0);
      expect(await page.locator('.svelte-flow__attribution').count()).toBe(0);
      await shot(page, 'hello-world-full-bleed');
      await page.close();
    },
    CASE
  );

  it(
    'keeps the minimap for a graph that needs one',
    async () => {
      const page = await viewer('?fixture=plots');
      await page.waitForSelector('.svelte-flow__node');
      await expect.poll(() => page.locator('.svelte-flow__minimap').count()).toBe(1);
      await page.close();
    },
    CASE
  );
});

/* ========================================================== red means selected */

describe('read-only is a property, not an error', () => {
  it(
    'paints read-only blocks and wires in --faint and keeps --danger off them',
    async () => {
      const page = await viewer('?fixture=adsb_receiver');
      await page.waitForSelector('.svelte-flow__node');
      await page.waitForSelector('.svelte-flow__edge.cler-edge-readonly');
      const faint = await token(page, '--faint');
      const danger = await token(page, '--danger');

      const node = '.svelte-flow__node[data-id="source"] .block';
      expect(await styleOf(page, node, 'border-top-color')).toBe(faint);
      expect(await styleOf(page, node, 'background-color')).not.toBe(danger);

      const wire = '.svelte-flow__edge.cler-edge-readonly .svelte-flow__edge-path';
      expect(await styleOf(page, wire, 'stroke')).toBe(faint);

      const badge = '.svelte-flow__node[data-id="source"] .badge';
      expect(await styleOf(page, badge, 'border-top-color')).toBe(faint);
      expect(await styleOf(page, badge, 'background-color')).toBe('rgba(0, 0, 0, 0)');

      await page.click('.svelte-flow__node[data-id="source"]');
      await page.waitForSelector('[data-testid="block-reason"]');
      expect(await styleOf(page, '[data-testid="block-reason"]', 'border-top-color')).toBe(faint);
      await shot(page, 'read-only-repaint');
      await page.close();
    },
    CASE
  );

  it(
    'keeps --danger for a type conflict',
    async () => {
      const page = await viewer('?fixture=type_conflict');
      await page.waitForSelector('.svelte-flow__edge.cler-edge-conflict');
      const conflict = '.svelte-flow__edge.cler-edge-conflict .svelte-flow__edge-path';
      expect(await styleOf(page, conflict, 'stroke')).toBe(await token(page, '--danger'));
      await page.close();
    },
    CASE
  );
});

/* =============================================================== the hover layer */

describe('the canvas answers the cursor', () => {
  it(
    'lifts a hovered block and grows its handles',
    async () => {
      const page = await viewer('?fixture=hello_world');
      await page.waitForSelector('.svelte-flow__node');
      const block = '.svelte-flow__node[data-id="adder"] .block';
      const port = `${handle('adder', 'in[0]')}`;
      expect(await styleOf(page, block, 'box-shadow')).toBe('none');
      expect(await styleOf(page, port, 'scale')).toBe('none');

      await page.hover('.svelte-flow__node[data-id="adder"]');
      await expect
        .poll(() => styleOf(page, block, 'border-top-color'))
        .toBe(await token(page, '--border-hi'));
      expect(await styleOf(page, block, 'box-shadow')).not.toBe('none');
      await expect.poll(() => styleOf(page, port, 'scale')).toBe('1.6');
      await shot(page, 'node-hover');
      await page.close();
    },
    CASE
  );

  it(
    'lifts the edges of a selected block and dims the rest',
    async () => {
      const page = await viewer('?fixture=hello_world');
      await page.waitForSelector('.svelte-flow__node');
      await page.click('.svelte-flow__node[data-id="adder"]');
      await page.waitForSelector('.svelte-flow__edge.cler-edge-dim');

      const lifted = await page
        .locator('.svelte-flow__edge.cler-edge-lift')
        .evaluateAll((edges) => edges.map((edge) => edge.getAttribute('data-id') ?? ''));
      expect(lifted.every((id) => id.startsWith('adder->') || id.includes('->adder.'))).toBe(true);
      expect(lifted.length).toBeGreaterThan(0);
      expect(await styleOf(page, '.svelte-flow__edge.cler-edge-dim', 'opacity')).toBe('0.4');
      await page.close();
    },
    CASE
  );

  it(
    'says so when a wire is dropped on empty canvas',
    async () => {
      const page = await boot();
      const start = await centre(page, handle('source1', 'out'));
      const port = await centre(page, handle('adder', 'in[1]'));
      await page.mouse.move(start.x, start.y);
      await page.mouse.down();
      await page.mouse.move(port.x, port.y, { steps: 10 });
      await shot(page, 'wire-drag-over-port');
      expect(await styleOf(page, `${handle('adder', 'in[1]')}`, 'box-shadow')).not.toBe('none');
      await page.mouse.move(port.x, port.y + 260, { steps: 8 });
      await shot(page, 'wire-drag-empty');
      await page.mouse.up();

      const toast = page.locator('[data-testid="note-toast"]');
      await toast.waitFor();
      expect(await toast.textContent()).toContain('release on an input port');
      await page.close();
    },
    CASE
  );
});

/* ================================================================ small repaints */

describe('the cheap repaints', () => {
  it(
    'hides the problems chip when the graph is clean',
    async () => {
      const page = await viewer('?fixture=hello_world');
      await page.waitForSelector('.svelte-flow__node');
      expect(await page.locator('[data-testid="problems"]').count()).toBe(0);
      await page.close();
    },
    CASE
  );

  it(
    'answers Ctrl+S in a viewer with the viewer note, not a save',
    async () => {
      const page = await viewer('?fixture=hello_world');
      await page.waitForSelector('.svelte-flow__node');
      await page.locator('.svelte-flow__pane').click({ position: { x: 500, y: 700 } });
      await page.keyboard.press('Control+s');
      const toast = page.locator('[data-testid="note-toast"]');
      await toast.waitFor();
      expect(await toast.textContent()).toContain('read-only viewer');
      await page.close();
    },
    CASE
  );

  it(
    'shows the full document path in the top bar with a matching tooltip',
    async () => {
      const page = await boot();
      const path = page.locator('[data-testid="doc-path"]');
      expect(await path.inputValue()).toBe('/tmp/fake/hello_world.cpp');
      expect(await path.getAttribute('title')).toBe('/tmp/fake/hello_world.cpp');
      await page.close();
    },
    CASE
  );

  it(
    'opens the drawer at the flowgraph, not at line one',
    async () => {
      const page = await boot({ fixture: 'adsb_receiver' });
      await page.keyboard.press('Control+`');
      await page.waitForSelector('[data-testid="drawer-body"] .row');
      await expect
        .poll(
          () =>
            page.evaluate(() => {
              const body = document.querySelector('[data-testid="drawer-body"]');
              return body instanceof HTMLElement ? body.scrollTop : -1;
            }),
          { timeout: 3000 }
        )
        .toBeGreaterThan(0);
      expect(await page.textContent('[data-testid="drawer-close"]')).toBe('▾');
      await shot(page, 'drawer-glass');
      await page.close();
    },
    CASE
  );

  it(
    'groups fixture-only blocks under the open translation unit',
    async () => {
      const page = await viewer('?example=hello_world');
      await openLibrary(page);
      expect(await page.locator('[data-library-path="this file"]').count()).toBe(1);
      await page.click('[data-library-path="this file"] > .folder-row');
      expect(await page.locator('[data-testid="palette"] .row').first().textContent()).not.toContain(
        '?'
      );
      await page.close();
    },
    CASE
  );
});
