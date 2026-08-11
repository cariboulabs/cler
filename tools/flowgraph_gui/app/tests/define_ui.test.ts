import { describe, expect, it } from 'vitest';
import {
  boot,
  CASE,
  commands,
  highlighted,
  menuIds,
  openMenu,
  sent,
  shot,
  useBrowser,
  viewer
} from './ui';
import type { Page } from 'playwright';

useBrowser();

const NOTE_BROWSER = 'example mode — read-only viewer, editing needs the desktop shell';

async function fillScale(page: Page): Promise<void> {
  await page.fill('[data-define-field="name"]', 'MyScaleBlock');
  await page.fill('[data-define-field="value_type"]', 'float');
  await page.fill('[data-define-field="input.0"]', 'in');
  await page.click('[data-testid="define-add-param"]');
  await page.fill('[data-define-field="param.0.name"]', 'scale');
  await page.fill('[data-define-field="param.0.type"]', 'float');
  await page.fill('[data-define-field="param.0.default"]', '1.0f');
}

const SCALE_COMMAND = {
  command: 'define_block',
  site: 0,
  name: 'MyScaleBlock',
  value_type: 'float',
  inputs: [{ name: 'in' }],
  outputs: 1,
  params: [{ name: 'scale', cpp_type: 'float', default: '1.0f' }],
  may_block: false
};

/* ================================================================ entry points */

describe('the new-block wizard has two ways in', () => {
  it(
    'opens from the palette button and closes on Escape without sending anything',
    async () => {
      const page = await boot();
      await page.click('[data-testid="palette-new"]');
      await page.waitForSelector('[data-testid="define-block"]');
      await shot(page, 'define-wizard');

      expect(await page.textContent('[data-testid="define-preview-ctor"]')).toBe(
        'NewBlock(const char* name)'
      );
      expect(await page.getAttribute('[data-define-field="value_type"]', 'list')).toBe(
        'cler-value-types'
      );
      const offered = await page
        .locator('#cler-value-types option')
        .evaluateAll((options) => options.map((option) => option.getAttribute('value')));
      expect(offered).toContain('float');
      expect(offered).toContain('std::complex<float>');
      await page.press('[data-define-field="name"]', 'Escape');
      await expect.poll(() => page.locator('[data-testid="define-block"]').count()).toBe(0);
      expect(await commands(page)).toEqual([]);
      await page.close();
    },
    CASE
  );

  it(
    'opens from the canvas context menu',
    async () => {
      const page = await boot();
      await openMenu(page, '.svelte-flow__pane', { x: 340, y: 640 });
      expect(await menuIds(page)).toContain('menu-new-block');
      await page.click('[data-testid="menu-new-block"]');
      await page.waitForSelector('[data-testid="define-block"]');
      await page.close();
    },
    CASE
  );

  it(
    'is disabled in example mode and says why',
    async () => {
      const page = await viewer('?example=hello_world');
      await page.waitForSelector('[data-testid="palette"] .entry');
      const button = page.locator('[data-testid="palette-new"]');
      expect(await button.isDisabled()).toBe(true);
      expect(await button.getAttribute('title')).toBe(NOTE_BROWSER);

      await openMenu(page, '.svelte-flow__pane', { x: 340, y: 640 });
      const entry = page.locator('[data-testid="menu-new-block"]');
      expect(await entry.isDisabled()).toBe(true);
      expect(await entry.getAttribute('title')).toBe(NOTE_BROWSER);
      await page.close();
    },
    CASE
  );
});

/* ================================================================ validation */

describe('the wizard validates before the crate has to', () => {
  it(
    'live-validates the name against the suffix and against the whole palette',
    async () => {
      const page = await boot();
      await page.click('[data-testid="palette-new"]');
      await page.waitForSelector('[data-testid="define-block"]');
      expect(await page.locator('[data-testid="define-confirm"]').isDisabled()).toBe(true);

      await page.fill('[data-define-field="name"]', 'MyScale');
      await expect.poll(() => page.textContent('[data-define-error="name"]')).toBe(
        'a block type name must end in "Block"'
      );

      await page.fill('[data-define-field="name"]', 'SinkFileBlock');
      await expect.poll(() => page.textContent('[data-define-error="name"]')).toBe(
        'SinkFileBlock already exists in sinks'
      );
      expect(await page.locator('[data-testid="define-confirm"]').isDisabled()).toBe(true);

      await page.fill('[data-define-field="name"]', 'MyScaleBlock');
      await expect.poll(() => page.locator('[data-define-error="name"]').count()).toBe(0);
      expect(await page.locator('[data-testid="define-confirm"]').isDisabled()).toBe(false);
      expect(await commands(page)).toEqual([]);
      await page.close();
    },
    CASE
  );

  it(
    'blocks the 0-in 0-out shape and the reserved member names client-side',
    async () => {
      const page = await boot();
      await page.click('[data-testid="palette-new"]');
      await page.waitForSelector('[data-testid="define-block"]');
      await page.fill('[data-define-field="name"]', 'MyScaleBlock');

      await page.click('[data-define-drop-input="0"]');
      await page.click('[data-testid="define-outputs-down"]');
      await expect.poll(() => page.textContent('[data-testid="define-outputs"]')).toBe('0');
      await expect.poll(() => page.textContent('[data-define-error="outputs"]')).toBe(
        'MyScaleBlock declares no inputs and no outputs'
      );
      expect(await page.getAttribute('[data-testid="define-confirm"]', 'title')).toBe(
        'MyScaleBlock declares no inputs and no outputs'
      );

      await page.click('[data-testid="define-outputs-up"]');
      await page.click('[data-testid="define-add-input"]');
      await page.fill('[data-define-field="input.0"]', 'out0');
      await expect.poll(() => page.textContent('[data-define-error="input.0"]')).toBe(
        '"out0" is a generated output parameter'
      );
      await page.fill('[data-define-field="input.0"]', '_in');
      await expect.poll(() => page.textContent('[data-define-error="input.0"]')).toBe(
        '"_in" becomes a reserved identifier as a member'
      );

      expect(await commands(page)).toEqual([]);
      expect(await page.locator('[data-testid="define-block"]').count()).toBe(1);
      await page.close();
    },
    CASE
  );
});

/* ================================================================ submit */

describe('creating a block is one define_block', () => {
  it(
    'sends exactly one command with the shape the wizard shows',
    async () => {
      const page = await boot();
      await page.click('[data-testid="palette-new"]');
      await page.waitForSelector('[data-testid="define-block"]');
      await fillScale(page);

      await expect.poll(() => page.textContent('[data-testid="define-preview-ctor"]')).toBe(
        'MyScaleBlock(const char* name, float scale = 1.0f)'
      );
      expect(await page.textContent('[data-testid="define-preview-ports"]')).toBe(
        'in → out0 · float'
      );

      await page.click('[data-testid="define-confirm"]');
      await expect.poll(() => commands(page)).toEqual([SCALE_COMMAND]);
      expect((await sent(page)).length).toBe(1);
      await expect.poll(() => page.locator('[data-testid="define-block"]').count()).toBe(0);
      await page.close();
    },
    CASE
  );

  it(
    'sends may_block and a bare parameter when they are asked for',
    async () => {
      const page = await boot();
      await page.click('[data-testid="palette-new"]');
      await page.waitForSelector('[data-testid="define-block"]');
      await page.fill('[data-define-field="name"]', 'LoggerSinkBlock');
      await page.fill('[data-define-field="input.0"]', 'left');
      await page.click('[data-testid="define-add-input"]');
      await page.fill('[data-define-field="input.1"]', 'right');
      await page.click('[data-testid="define-outputs-down"]');
      await page.click('[data-define-field="may_block"]');
      await page.click('[data-testid="define-add-param"]');
      await page.fill('[data-define-field="param.0.name"]', 'depth');
      await page.fill('[data-define-field="param.0.type"]', 'size_t');
      await page.click('[data-testid="define-confirm"]');

      await expect.poll(() => commands(page)).toEqual([
        {
          command: 'define_block',
          site: 0,
          name: 'LoggerSinkBlock',
          value_type: 'float',
          inputs: [{ name: 'left' }, { name: 'right' }],
          outputs: 0,
          params: [{ name: 'depth', cpp_type: 'size_t', default: null }],
          may_block: true
        }
      ]);
      await page.close();
    },
    CASE
  );
});

/* ================================================================ refusal */

describe('a crate refusal lands on the field that caused it', () => {
  it(
    'points at the parameter type and keeps the form open',
    async () => {
      const page = await boot({
        refusal: JSON.stringify({ error: 'invalid_type', element: 'cpp_type', text: 'flaot' })
      });
      await page.click('[data-testid="palette-new"]');
      await page.waitForSelector('[data-testid="define-block"]');
      await page.fill('[data-define-field="name"]', 'MyScaleBlock');
      await page.click('[data-testid="define-add-param"]');
      await page.fill('[data-define-field="param.0.name"]', 'scale');
      await page.fill('[data-define-field="param.0.type"]', 'flaot');
      await page.click('[data-testid="define-confirm"]');

      await expect.poll(() => page.textContent('[data-define-error="param.0.type"]')).toBe(
        '"flaot" is not a valid C++ type'
      );
      expect(await page.locator('[data-testid="define-block"]').count()).toBe(1);
      expect(await page.locator('[data-block="MyScaleBlock"]').count()).toBe(0);
      await page.close();
    },
    CASE
  );

  it(
    'points at the name when the translation unit already owns the type',
    async () => {
      const page = await boot({
        refusal: JSON.stringify({ error: 'duplicate_type', name: 'MyScaleBlock' })
      });
      await page.click('[data-testid="palette-new"]');
      await page.waitForSelector('[data-testid="define-block"]');
      await fillScale(page);
      await page.click('[data-testid="define-confirm"]');

      await expect.poll(() => page.textContent('[data-define-error="name"]')).toBe(
        'MyScaleBlock is already a type in this file'
      );
      await page.close();
    },
    CASE
  );

  it(
    'points at the input row the crate named',
    async () => {
      const page = await boot({
        refusal: JSON.stringify({ error: 'reserved_identifier', text: 'operator' })
      });
      await page.click('[data-testid="palette-new"]');
      await page.waitForSelector('[data-testid="define-block"]');
      await page.fill('[data-define-field="name"]', 'MyScaleBlock');
      await page.click('[data-testid="define-add-input"]');
      await page.fill('[data-define-field="input.1"]', 'operator');
      await page.click('[data-testid="define-confirm"]');

      await expect.poll(() => page.textContent('[data-define-error="input.1"]')).toBe(
        '"operator" is a C++ keyword'
      );
      await page.close();
    },
    CASE
  );
});

/* ================================================================ success */

describe('a created block reaches the palette, the drawer and the canvas', () => {
  it(
    'refreshes the palette and offers to place the new type',
    async () => {
      const page = await boot();
      const before = await page.locator('[data-testid="palette"] .entry').count();
      await page.click('[data-testid="palette-new"]');
      await page.waitForSelector('[data-testid="define-block"]');
      await fillScale(page);
      await page.click('[data-testid="define-confirm"]');

      await page.waitForSelector('[data-block="MyScaleBlock"]');
      expect(await page.locator('[data-testid="palette"] .entry').count()).toBe(before + 1);
      expect(await page.textContent('[data-block="MyScaleBlock"]')).toContain('this file');
      expect(await page.textContent('[data-block="MyScaleBlock"]')).toContain('1 in · 1 out');

      await page.waitForSelector('[data-testid="toast-action"]');
      expect(await page.textContent('[data-testid="note-toast"]')).toContain(
        'MyScaleBlock is in the palette'
      );
      await shot(page, 'define-toast');
      await page.click('[data-testid="toast-action"]');
      await page.waitForSelector('[data-testid="add-block"]');
      expect(await page.textContent('[data-testid="add-block"] .title')).toBe('MyScaleBlock');
      expect(await page.inputValue('[data-add-field="ctor.1"]')).toBe('1.0f');
      await page.close();
    },
    CASE
  );

  it(
    'scrolls the open code drawer to the struct it just wrote',
    async () => {
      const page = await boot();
      await page.click('[data-testid="drawer"]');
      await page.waitForSelector('[data-testid="drawer-body"]');
      await page.click('[data-testid="palette-new"]');
      await page.waitForSelector('[data-testid="define-block"]');
      await fillScale(page);
      await page.click('[data-testid="define-confirm"]');

      await expect.poll(() => highlighted(page)).toBe('struct MyScaleBlock');
      await page.close();
    },
    CASE
  );
});
