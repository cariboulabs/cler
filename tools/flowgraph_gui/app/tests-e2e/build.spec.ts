import { expect, test } from './harness';

test('h) F7 checks the temporary draft and blames the right block without saving', async ({
  page,
  work,
  openFile,
  node,
  shot
}) => {
  const file = work.copy('hello_world.cpp');
  const field = page.locator('input[data-field="source1.ctor.1"]');
  const rows = page.locator('[data-diagnostic]');

  await page.goto('/');
  await openFile(file);

  await test.step('break the amplitude and check', async () => {
    await node('source1').click();
    await field.fill('1.0ff');
    await field.press('Enter');
    expect(work.bytes(file)).not.toContain('1.0ff');
    await field.blur();
    // the blur may still be applying the edit on a slow runner; F7 on a busy
    // document is a no-op
    await expect(page.getByTestId('check')).toBeEnabled();

    // the action silently ignores a press that lands during a transient
    // disabled flicker, so press until the check visibly produces diagnostics
    await expect
      .poll(
        async () => {
          await page.keyboard.press('F7');
          return page
            .getByTestId('problems')
            .getAttribute('data-count')
            .catch(() => null);
        },
        { timeout: 90_000, intervals: [15_000] }
      )
      .toBe('1');
    await page.getByTestId('tab-diagnostics').click();
    await expect(rows.first()).toBeVisible({ timeout: 90_000 });
    await expect(rows.first()).toContainText('numeric literal');
    await expect(page.locator('[data-diagnostic-block]').first()).toHaveText('source1');
    await expect(page.getByTestId('problems')).toHaveAttribute('data-count', '1');
    expect(work.bytes(file)).not.toContain('1.0ff');
    await shot('check-broken');
  });

  await test.step('the row jumps to the offending line', async () => {
    await rows.first().click();
    await expect(page.getByTestId('tab-code')).toHaveAttribute('aria-pressed', 'true');
    await expect
      .poll(() => page.locator('[data-testid="code-drawer"] .hit').allTextContents())
      .toEqual(expect.arrayContaining([expect.stringContaining('1.0ff')]));
  });

  await test.step('a copy outside desktop_examples builds as a draft target', async () => {
    await expect(page.getByTestId('build')).toBeEnabled();
    await expect(page.getByTestId('run')).toBeDisabled();
  });
});
