import { writeFileSync } from 'node:fs';
import { expect, revision, test } from './harness';

const FANOUT_CASE = `#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_cw.hpp"
#include "desktop_blocks/utils/throttle.hpp"
#include "desktop_blocks/utils/fanout.hpp"

int main() {
    const size_t SPS = 1000;
    SourceCWBlock<float> source("CWSource", 1.0f, 1.0f, SPS);
    FanoutBlock<float> fanout("Fanout", 2);
    ThrottleBlock<float> sink_a("SinkA", SPS);
    ThrottleBlock<float> sink_b("SinkB", SPS);
    ThrottleBlock<float> sink_c("SinkC", SPS);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &fanout.in),
        cler::BlockRunner(&fanout, &sink_a.in, &sink_b.in),
        cler::BlockRunner(&sink_a),
        cler::BlockRunner(&sink_b),
        cler::BlockRunner(&sink_c)
    );

    flowgraph.run();
    return 0;
}
`;

const SURFACE = [
  'open_document',
  'close_document',
  'apply_commands',
  'undo',
  'redo',
  'reload_document',
  'parse_file',
  'palette'
];

type Bridge = { invoke: (cmd: string, args: Record<string, unknown>) => Promise<unknown> };

test('the backend still answers the whole command surface', async ({ page }) => {
  await page.goto('/');

  const drifted = await page.evaluate(async (commands) => {
    const loud: string[] = [];
    for (const cmd of commands) {
      try {
        await (window as unknown as { __TAURI_INTERNALS__: Bridge }).__TAURI_INTERNALS__.invoke(cmd, {
          path: '/nonexistent/e2e-surface.cpp',
          commands: [],
          baseRevision: 0
        });
      } catch (error) {
        if (error instanceof Error) loud.push(`${cmd}: ${error.message}`);
      }
    }
    return loud;
  }, SURFACE);
  expect(drifted).toEqual([]);

  const unknown = await page.evaluate(async () => {
    try {
      await (
        window as unknown as { __TAURI_INTERNALS__: Bridge }
      ).__TAURI_INTERNALS__.invoke('not_a_command', { path: '/nonexistent/e2e-surface.cpp' });
      return 'no rejection';
    } catch (error) {
      return error instanceof Error ? error.message : `plain rejection: ${String(error)}`;
    }
  });
  expect(unknown).toContain('unknown command: not_a_command');
});

test('a) picking an example inside the desktop shell drops to a read-only viewer', async ({
  page,
  calls,
  forget,
  node,
  shot
}) => {
  await page.goto('/');
  await expect(page.locator('.svelte-flow__node').first()).toBeVisible();
  await shot('loaded');
  await forget();

  await page.getByTestId('example-select').selectOption('plots');
  await expect(node('cw_fanout')).toBeVisible();
  await node('cw_throttle').click();
  await shot('example-picked');

  await expect(page.getByTestId('viewer-note')).toBeVisible();
  const fields = page.locator('input[data-field]');
  await expect(fields.first()).toBeVisible();
  const count = await fields.count();
  for (let index = 0; index < count; index += 1) {
    await expect(fields.nth(index)).toBeDisabled();
  }

  await node('cw_throttle').click({ button: 'right' });
  await expect(page.getByTestId('menu-remove')).toBeDisabled();
  await expect(page.getByTestId('menu-delete-block')).toBeDisabled();
  await page.keyboard.press('Escape');

  expect(await calls()).toEqual([]);
});

test('b+c) editing a real file writes the bytes and undo restores them', async ({
  page,
  work,
  openFile,
  node,
  shot
}) => {
  const file = work.copy('hello_world.cpp');
  const original = work.bytes(file);

  await page.goto('/');
  await openFile(file);
  await shot('opened');

  await test.step('edit the throttle constructor argument', async () => {
    await node('throttle').click();
    const field = page.locator('input[data-field="throttle.ctor.1"]');
    await expect(field).toBeEnabled();
    await field.fill('4242');
    await field.press('Enter');

    await expect
      .poll(() => work.bytes(file), { timeout: 20_000 })
      .toContain('ThrottleBlock<float> throttle("Throttle", 4242)');
    await expect.poll(() => revision(page)).toBe(1);

    await page.getByTestId('drawer').click();
    await expect(page.getByTestId('drawer-revision')).toHaveText('rev 1');
    await expect(page.getByTestId('drawer-body')).toContainText('"Throttle", 4242');
    await shot('edited');
  });

  await test.step('undo restores the bytes byte for byte', async () => {
    await page.getByTestId('undo').click();
    await expect.poll(() => work.bytes(file), { timeout: 20_000 }).toBe(original);
    await expect(page.getByTestId('undo')).toBeDisabled();
    await expect(page.getByTestId('redo')).toBeEnabled();
    await expect(page.getByTestId('drawer-body')).toContainText('"Throttle", SPS');
    await shot('undone');
  });
});

test('d) wiring a third fanout output is one transaction', async ({
  page,
  work,
  openFile,
  calls,
  forget,
  dragWire,
  shot
}) => {
  const file = work.write('fanout_case.cpp', FANOUT_CASE);

  await page.goto('/');
  await openFile(file);
  await expect(page.locator('.svelte-flow__edge')).toHaveCount(3);
  await shot('before-wire');
  await forget();

  await dragWire('fanout', 'sink_c', 'in');

  await expect.poll(() => work.bytes(file), { timeout: 20_000 }).toContain('&sink_c.in');
  await shot('after-wire');

  const applied = await calls('apply_commands');
  expect(applied).toHaveLength(1);
  expect(applied[0]?.args.commands).toHaveLength(2);

  const text = work.bytes(file);
  expect(text).toContain('FanoutBlock<float> fanout("Fanout", 3)');
  expect(text).toMatch(/BlockRunner\(&fanout,[^)]*&sink_c\.in\)/);
  await expect(page.locator('.svelte-flow__edge')).toHaveCount(4);
});

test('e) an external edit blocks editing until reload', async ({
  page,
  work,
  openFile,
  node,
  shot
}) => {
  const file = work.copy('hello_world.cpp');

  await page.goto('/');
  await openFile(file);
  await node('throttle').click();
  const field = page.locator('input[data-field="throttle.ctor.1"]');
  await field.fill('777');
  await field.press('Enter');
  await expect.poll(() => work.bytes(file), { timeout: 20_000 }).toContain('"Throttle", 777');
  await expect(page.getByTestId('undo')).toBeEnabled();

  const behind = work.bytes(file).replace('Hello World Plot Example', 'Edited Behind The App');
  writeFileSync(file, behind);
  await expect(page.getByTestId('reload-banner')).toBeVisible({ timeout: 30_000 });
  await shot('banner');

  await page.locator('input[data-field="throttle.ctor.1"]').fill('888');
  await page.locator('input[data-field="throttle.ctor.1"]').press('Enter');
  await expect(page.getByTestId('status')).toContainText('changed on disk');
  expect(work.bytes(file)).toBe(behind);
  await shot('refused');

  await page.getByTestId('reload-banner').getByRole('button', { name: 'Reload' }).click();
  await expect(page.getByTestId('reload-banner')).toHaveCount(0);
  await expect(page.getByTestId('undo')).toBeDisabled();
  await expect(page.getByTestId('redo')).toBeDisabled();
  await shot('reloaded');

  await node('throttle').click();
  const fresh = page.locator('input[data-field="throttle.ctor.1"]');
  await fresh.fill('999');
  await fresh.press('Enter');
  await expect.poll(() => work.bytes(file), { timeout: 20_000 }).toContain('"Throttle", 999');
  expect(work.bytes(file)).toContain('Edited Behind The App');
});

test('f) deleting a block referenced outside the graph is refused', async ({
  page,
  work,
  openFile,
  node,
  shot
}) => {
  const file = work.copy('hello_world.cpp');
  const original = work.bytes(file);

  await page.goto('/');
  await openFile(file);

  await node('plot').click({ button: 'right' });
  await expect(page.getByTestId('context-menu')).toBeVisible();
  await page.getByTestId('menu-delete-block').click();

  const dialog = page.getByTestId('delete-refusal');
  await expect(dialog).toBeVisible();
  await expect(dialog.locator('li')).toHaveCount(2);
  await expect(dialog.locator('li').nth(0)).toContainText('set_initial_window');
  await expect(dialog.locator('li').nth(1)).toContainText('plot.render()');
  await shot('delete-refusal');

  expect(work.bytes(file)).toBe(original);
});
