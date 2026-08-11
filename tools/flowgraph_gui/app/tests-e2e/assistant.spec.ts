import { expect, test } from './harness';

test('j) the assistant asks for a key before it can cost anything', async ({
  page,
  work,
  openFile,
  calls,
  shot
}) => {
  const file = work.copy('hello_world.cpp');

  await page.goto('/');
  await openFile(file);

  await test.step('Ctrl+J opens the assistant on the inspector rail', async () => {
    await page.keyboard.press('Control+j');
    await expect(page.getByTestId('assistant-panel')).toBeVisible();
    await expect(page.getByTestId('rail-tab-assistant')).toHaveAttribute('aria-selected', 'true');
    await expect(page.getByTestId('assistant-model')).toContainText('claude-opus-5');
  });

  await test.step('the real backend reports no key and says what to do', async () => {
    await expect(page.getByTestId('assistant-setup')).toBeVisible();
    const reason = page.getByTestId('assistant-reason');
    await expect(reason).toContainText('ANTHROPIC_API_KEY');
    await expect(reason).toContainText('anthropic-key');
    await expect(reason).toContainText('chmod 600');
    await expect(page.getByTestId('assistant-input')).toHaveCount(0);
    await expect(page.getByTestId('assistant-chip')).toHaveCount(0);
    await shot('assistant-setup');
  });

  await test.step('nothing was asked of the Anthropic API', async () => {
    expect(await calls('assistant_ask')).toEqual([]);
    expect((await calls('assistant_status')).length).toBeGreaterThan(0);
  });

  await test.step('the rail goes back to the inspector', async () => {
    await page.getByTestId('rail-tab-inspector').click();
    await expect(page.locator('.inspector')).toBeVisible();
    await expect(page.getByTestId('assistant-panel')).toHaveCount(0);
  });
});
