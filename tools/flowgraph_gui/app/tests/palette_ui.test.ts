import { describe, expect, it } from 'vitest';
import { boot, CASE, openLibrary, shot, useBrowser } from './ui';

useBrowser();

describe('palette folders', () => {
  it(
    'expands a folder and places a block with single clicks',
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
      await page.waitForSelector('.svelte-flow__node[data-id="source_c_w"]');
      await expect
        .poll(() => page.textContent('.inspector .title'))
        .toBe('source_c_w');
      await page.close();
    },
    CASE
  );
});
