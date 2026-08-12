import { describe, expect, it } from 'vitest';
import { boot, CASE, openLibrary, shot, specs, useBrowser } from './ui';

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
