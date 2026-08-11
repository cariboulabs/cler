import { describe, expect, it } from 'vitest';
import { REQUIRED_ARGUMENT_PLACEHOLDER } from '../src/lib/palette';
import { boot, BUILDABLE, CASE, modelOf, useBrowser } from './ui';

useBrowser();

describe('invalid block nodes', () => {
  it(
    'renders required placeholders with the danger tint and missing-field hint',
    async () => {
      const model = modelOf('hello_world');
      const source = model.sites[0]?.blocks.find((block) => block.var === 'source1');
      if (!source) throw new Error('no source1 block');
      source.template_args[0]!.text = REQUIRED_ARGUMENT_PLACEHOLDER;
      source.ctor_args[2]!.text = REQUIRED_ARGUMENT_PLACEHOLDER;

      const page = await boot({ model });
      const block = page.locator('.svelte-flow__node[data-id="source1"] .block');
      await block.waitFor();

      expect(await block.getAttribute('data-invalid')).toBe('true');
      expect(await block.getAttribute('title')).toContain('T, frequency_hz');
      expect(await block.evaluate((element) => getComputedStyle(element).backgroundColor)).toBe(
        'rgb(55, 24, 28)'
      );
      await page.close();
    },
    CASE
  );

  it(
    'blocks Run until every required placeholder is replaced',
    async () => {
      const model = modelOf('hello_world');
      const source = model.sites[0]?.blocks.find((block) => block.var === 'source1');
      if (!source) throw new Error('no source1 block');
      source.template_args[0]!.text = REQUIRED_ARGUMENT_PLACEHOLDER;
      source.ctor_args[2]!.text = REQUIRED_ARGUMENT_PLACEHOLDER;

      const page = await boot({ model, target: BUILDABLE });
      const run = page.locator('[data-testid="run"]');
      expect(await run.isDisabled()).toBe(true);
      expect(await run.getAttribute('title')).toContain('1 block missing required fields');

      await page.click('.svelte-flow__node[data-id="source1"]');
      await page.fill('input[data-field="source1.template.0"]', 'float');
      await page.press('input[data-field="source1.template.0"]', 'Enter');
      await page.fill('input[data-field="source1.ctor.2"]', '20.0f');
      await page.press('input[data-field="source1.ctor.2"]', 'Enter');

      await expect.poll(() => run.isDisabled()).toBe(false);
      await page.close();
    },
    CASE
  );
});
