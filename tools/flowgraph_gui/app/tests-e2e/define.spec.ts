import type { Page } from 'playwright/test';
import { expect, test } from './harness';

async function stillViewport(page: Page): Promise<void> {
  let last: string | null = null;
  await expect
    .poll(
      async () => {
        const now = await page.locator('.svelte-flow__viewport').getAttribute('style');
        const stable = now !== null && now === last;
        last = now;
        return stable;
      },
      { intervals: [200], timeout: 15_000 }
    )
    .toBe(true);
}

test('h) a wizard-defined block goes from nothing to wired without a text editor', async ({
  page,
  work,
  openFile,
  calls,
  forget,
  dragWire,
  shot
}) => {
  const file = work.copy('hello_world.cpp');

  await page.goto('/');
  await openFile(file);
  await forget();

  await test.step('the wizard writes the struct to disk', async () => {
    await page.getByTestId('palette-new').click();
    await expect(page.getByTestId('define-block')).toBeVisible();

    await page.locator('[data-define-field="name"]').fill('MyScaleBlock');
    await page.locator('[data-define-field="value_type"]').fill('float');
    await page.locator('[data-define-field="input.0"]').fill('in');
    await page.getByTestId('define-add-param').click();
    await page.locator('[data-define-field="param.0.name"]').fill('scale');
    await page.locator('[data-define-field="param.0.type"]').fill('float');

    await expect(page.getByTestId('define-preview-ctor')).toHaveText(
      'MyScaleBlock(const char* name, float scale)'
    );
    await shot('wizard');
    await page.getByTestId('define-confirm').click();
    await expect(page.getByTestId('define-block')).toHaveCount(0);

    await expect
      .poll(() => work.bytes(file), { timeout: 20_000 })
      .toContain('struct MyScaleBlock : public cler::BlockBase {');

    const text = work.bytes(file);
    expect(text).toContain('cler::Channel<float> in;');
    expect(text).toContain('MyScaleBlock(const char* name, float scale)');
    expect(text).toContain('cler::ChannelBase<float>* out0');
    expect(text).toContain('float _scale;');
    expect(text.indexOf('struct MyScaleBlock')).toBeLessThan(text.indexOf('int main()'));

    const applied = await calls('apply_commands');
    expect(applied).toHaveLength(1);
    expect(applied[0]?.args.commands).toHaveLength(1);
  });

  await test.step('the palette gains the entry the crate now discovers', async () => {
    const entry = page.locator('[data-testid="palette"] [data-block="MyScaleBlock"]');
    await expect(entry).toBeVisible({ timeout: 20_000 });
    await expect(entry).toContainText('this file');
    await expect(entry).toContainText('1 in · 1 out');
    await shot('palette');
  });

  await test.step('dragging it onto the canvas declares it', async () => {
    await page
      .locator('[data-block="MyScaleBlock"]')
      .dragTo(page.locator('.svelte-flow__pane'), { targetPosition: { x: 620, y: 700 } });
    await expect(page.getByTestId('add-block')).toBeVisible();
    expect(await page.locator('[data-add-field="var_name"]').inputValue()).toBe('my_scale');
    await page.locator('[data-add-field="ctor.1"]').fill('2.0f');
    await page.getByTestId('add-confirm').click();

    await expect
      .poll(() => work.bytes(file), { timeout: 20_000 })
      .toContain('MyScaleBlock my_scale("my_scale", 2.0f);');
    await expect(page.locator('.svelte-flow__node[data-id="my_scale"]')).toBeVisible();
    await shot('placed');
  });

  await test.step('wiring it grows the adder and writes the runner line', async () => {
    await stillViewport(page);
    await dragWire('my_scale', 'adder', 'in[2]');

    await expect
      .poll(() => work.bytes(file), { timeout: 20_000 })
      .toMatch(/BlockRunner\(&my_scale,\s*&adder\.in\[2\]\)/);

    const text = work.bytes(file);
    expect(text).toContain('AddBlock<float, 3> adder("Adder")');
    await expect(page.locator('.svelte-flow__edge')).toHaveCount(5);
    await expect(page.getByTestId('problems')).toHaveAttribute('data-count', '0');
    await shot('wired');
  });
});
