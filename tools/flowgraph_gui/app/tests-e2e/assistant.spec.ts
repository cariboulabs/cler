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

  await test.step('the real backend reports no key and offers sign-in', async () => {
    await expect(page.getByTestId('assistant-setup')).toBeVisible();
    await expect(page.getByTestId('assistant-signin')).toBeVisible();
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

test('k) an accepted proposal is checked, drafted, then saved to the file', async ({
  page,
  work,
  openFile,
  emit,
  calls,
  shot
}) => {
  const file = work.copy('hello_world.cpp');
  const original = work.bytes(file);

  await page.goto('/');
  const opened = await openFile(file);
  await page.keyboard.press('Control+j');
  await expect(page.getByTestId('assistant-panel')).toBeVisible();

  await test.step('the proposal is dry-run through the real validator before it is shown', async () => {
    await emit('assistant-proposal', {
      path: opened,
      rationale: 'rename source1 so the plot legend reads right',
      commands: [{ command: 'set_display_name', site: 0, block: 'source1', new_text: 'Chirp' }],
      dropped: 0
    });

    await expect(page.getByTestId('assistant-proposal')).toBeVisible();
    await expect(page.getByTestId('proposal-command')).toHaveText('rename source1 to "Chirp"');
    const diff = page.getByTestId('proposal-diff');
    await expect(diff.locator('.row.del')).toContainText('"CWSource"');
    await expect(diff.locator('.row.add')).toContainText('"Chirp"');
    expect((await calls('preview_commands')).length).toBe(1);
    expect(work.bytes(opened)).toBe(original);
    await shot('assistant-proposal');
  });

  await test.step('accept drafts the bytes and the card records the revision', async () => {
    await page.getByTestId('proposal-accept').click();
    await expect(page.getByTestId('proposal-applied')).toContainText('revision 1');

    expect(work.bytes(opened)).toBe(original);
    await expect(page.getByTestId('draft-chip')).toBeVisible();
    await page.getByTestId('save').click();
    await expect
      .poll(() => work.bytes(opened), { timeout: 20_000 })
      .toContain('SourceCWBlock<float> source1("Chirp"');
    expect(work.bytes(opened)).not.toContain('"CWSource"');
    expect(await page.getByTestId('proposal-accept').count()).toBe(0);
    await shot('assistant-proposal-applied');
  });

  await test.step('the whole proposal is one undo step', async () => {
    await page.getByTestId('undo').click();
    expect(work.bytes(opened)).not.toBe(original);
    await page.getByTestId('save').click();
    await expect.poll(() => work.bytes(opened), { timeout: 20_000 }).toBe(original);
    await expect(page.getByTestId('undo')).toBeDisabled();
    await expect(page.getByTestId('assistant-proposal')).toBeVisible();
  });
});
