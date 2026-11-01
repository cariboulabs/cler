import { describe, expect, it } from 'vitest';
import { fixtures } from '../src/fixtures';
import { addForm, braceListLength } from '../src/lib/palette';
import { boot, CASE, commands, dragBlock, modelOf, openLibrary, openMenu, shot, specs, useBrowser, type FakeWindow } from './ui';

useBrowser();

describe('palette folders', () => {
  it(
    'expands a folder and describes a block on click',
    async () => {
      const page = await boot();
      await openLibrary(page);
      const folders = page.locator('.folder > button');
      const count = await folders.count();
      console.log('folders:', count);
      expect(count).toBeGreaterThan(0);
      await folders.first().click();
      await page.waitForTimeout(200);
      await shot(page, 'palette-folder-open');
      const rows = page.locator('button.row');
      expect(await rows.count()).toBeGreaterThan(0);

      await page.fill('[data-testid="palette-search"]', 'SourceCW');
      await page.click('[data-block="SourceCWBlock"] button.row');
      const detail = page.locator('[data-detail="SourceCWBlock"]');
      await detail.waitFor();
      expect(await detail.textContent()).toContain('SourceCWBlock');
      expect(await detail.textContent()).toContain('drag this row onto the canvas');
      expect(await page.locator('.svelte-flow__node[data-id="source_c_w"]').count()).toBe(0);

      await page.click('[data-block="SourceCWBlock"] button.row');
      await expect.poll(() => detail.count()).toBe(0);
      await page.close();
    },
    CASE
  );
});

describe('duplicate specs never brick the palette', () => {
  it(
    'stays interactive when the backend reports the same block twice',
    async () => {
      const doubled = [...specs, ...specs.map((spec) => structuredClone(spec))];
      const page = await boot({ specs: doubled });
      await openLibrary(page);
      await page.fill('[data-testid="palette-search"]', 'SourceCW');
      const rows = page.locator('[data-block="SourceCWBlock"] button.row');
      await expect.poll(() => rows.count()).toBe(1);
      await rows.first().click();
      await page.waitForSelector('[data-detail="SourceCWBlock"]');
      await page.close();
    },
    CASE
  );
});

describe('ports sized by a brace list', () => {
  it(
    'seeds a placeable label list and reports a block whose list is not one',
    async () => {
      const source = fixtures.hello_world;
      if (!source) throw new Error('no hello_world fixture');
      const model = structuredClone(source);
      const plot = model.sites[0]?.blocks.find((block) => block.var === 'plot');
      if (!plot) throw new Error('no plot block in hello_world');
      const labels = plot.ctor_args[1];
      if (!labels) throw new Error('plot has no label argument');
      labels.text = '"in"';

      const page = await boot({ model });
      await expect
        .poll(() => page.getAttribute('[data-testid="problems"]', 'data-count'))
        .not.toBe('0');
      await page.click('[data-testid="problems"]');
      expect(await page.textContent('[data-testid="problems-list"]')).toContain(
        'must be a brace list'
      );
      await page.close();
    },
    CASE
  );

  it('seeds the label list when the palette form is built', () => {
    const spec = specs.find((candidate) => candidate.name === 'PlotTimeSeriesBlock');
    if (!spec) throw new Error('no PlotTimeSeriesBlock spec');
    const form = addForm(spec, 'plot');
    expect(form.ctorArgs[1]?.value).toBe('{"in"}');
    expect(braceListLength(form.ctorArgs[1]?.value ?? '')).toBe(1);
  });
});

describe('a wired block without a runner', () => {
  it(
    'offers Add to graph in its menu and gains a runner',
    async () => {
      const base = fixtures.hello_world;
      if (!base) throw new Error('no hello_world fixture');
      const model = structuredClone(base);
      const site = model.sites[0];
      if (!site) throw new Error('no site');
      site.runners = site.runners.filter((runner) => runner.block !== 'plot');

      const page = await boot({ model });
      await openMenu(page, '.svelte-flow__node[data-id="plot"]');
      expect(await page.locator('[data-testid="menu-remove"]').count()).toBe(0);
      const add = page.locator('[data-testid="menu-add-to-graph"]');
      await add.waitFor();
      await add.click();

      await expect.poll(() => commands(page)).toEqual([
        { command: 'add_to_graph', site: 0, block: 'plot' }
      ]);
      await page.close();
    },
    CASE
  );
});

describe('the block menu reaches the block type', () => {
  it(
    'opens the header the block type came from',
    async () => {
      const page = await boot();
      await openMenu(page, '.svelte-flow__node[data-id="adder"]');
      const entry = page.locator('[data-testid="menu-open-block-source"]');
      await entry.waitFor();
      expect(await entry.getAttribute('title')).toContain('add.hpp');
      await entry.click();
      const opened = await page.evaluate(
        () => (window as unknown as FakeWindow).__fake.editors
      );
      expect(opened).toEqual([{ path: expect.stringContaining('add.hpp'), line: 1 }]);
      await page.close();
    },
    CASE
  );
});

describe('placing a plot', () => {
  it(
    'renders it without the user writing a loop',
    async () => {
      const model = modelOf('hello_world');
      if (model.sites[0]) model.sites[0].gui = null;
      const page = await boot({ model });
      await openLibrary(page);
      await page.fill('[data-testid="palette-search"]', 'PlotCSpectrum');
      await dragBlock(page, 'PlotCSpectrumBlock', { x: 640, y: 620 });

      await expect.poll(() => commands(page)).toEqual([
        expect.objectContaining({ command: 'add_block', type: 'PlotCSpectrumBlock' }),
        { command: 'materialize_gui', site: 0 }
      ]);
      await page.close();
    },
    CASE
  );
});
