import { writeFileSync } from 'node:fs';
import { expect, saveFile, test } from './harness';

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
  'preview_commands',
  'undo',
  'redo',
  'save_cache',
  'reload_document',
  'parse_file',
  'palette',
  'check_document',
  'find_target',
  'build_target',
  'run_target',
  'stop_target',
  'ai_agent_status',
  'ai_agent_stop'
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

test('a) picking an example inside the desktop shell opens its editable source', async ({
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

  await page.getByTestId('file-menu').click();
  await page.getByTestId('file-open-example').click();
  await page.locator('[data-example="plots"]').click();
  await expect(node('cw_fanout')).toBeVisible();
  await node('cw_throttle').click();
  await shot('example-picked');

  await expect(page.getByTestId('demo-chip')).toHaveCount(0);
  const fields = page.locator('input[data-field]');
  await expect(fields.first()).toBeVisible();
  const count = await fields.count();
  for (let index = 0; index < count; index += 1) {
    await expect(fields.nth(index)).toBeEnabled();
  }

  await node('cw_throttle').click({ button: 'right' });
  await expect(page.getByTestId('menu-remove')).toBeEnabled();
  await expect(page.getByTestId('menu-delete-block')).toBeEnabled();
  await page.keyboard.press('Escape');

  expect(await calls('open_document')).toHaveLength(1);
  expect(await calls('apply_commands')).toEqual([]);
});

test('b+c) edits stay drafted until Save and undo can be saved too', async ({
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

    expect(work.bytes(file)).toBe(original);
    await expect(page.getByTestId('draft-chip')).toBeVisible();
    await expect(page.getByTestId('check')).toBeEnabled();
    await expect(page.getByTestId('check')).toHaveAttribute('title', /temporary draft/);

    if (!(await page.getByTestId('drawer-body').isVisible())) {
      await page.getByTestId('drawer-toggle').click();
    }
    await expect(page.getByTestId('drawer-body')).toContainText('"Throttle", 4242');
    await saveFile(page);
    await expect
      .poll(() => work.bytes(file), { timeout: 20_000 })
      .toContain('ThrottleBlock<float> throttle("Throttle", 4242)');
    await expect(page.getByTestId('draft-chip')).toHaveCount(0);
    await shot('edited');
  });

  await test.step('undo creates a draft and Save restores the bytes byte for byte', async () => {
    await page.getByTestId('undo').click();
    expect(work.bytes(file)).not.toBe(original);
    await expect(page.getByTestId('undo')).toBeDisabled();
    await expect(page.getByTestId('redo')).toBeEnabled();
    await expect(page.getByTestId('drawer-body')).toContainText('"Throttle", SPS');
    await expect(page.getByTestId('draft-chip')).toBeVisible();
    await saveFile(page);
    await expect.poll(() => work.bytes(file), { timeout: 20_000 }).toBe(original);
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

  await expect(page.getByTestId('draft-chip')).toBeVisible();
  await saveFile(page);
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

test('d2) moving a block survives disconnecting an edge', async ({
  page,
  work,
  openFile,
  node
}) => {
  const file = work.copy('hello_world.cpp');

  await page.goto('/');
  await openFile(file);

  const adder = node('adder');
  const transform = () => adder.evaluate((element) => element.style.transform);
  const before = await transform();
  const box = await adder.boundingBox();
  if (!box) throw new Error('adder has no bounds');
  await page.mouse.move(box.x + box.width / 2, box.y + 6);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width / 2 + 180, box.y + 174, { steps: 12 });
  await page.mouse.up();
  await expect.poll(transform).not.toBe(before);

  const moved = await transform();
  const viewport = page.locator('.svelte-flow__viewport');
  const viewportTransform = () => viewport.evaluate((element) => element.style.transform);
  const movedViewport = await viewportTransform();
  await page.evaluate(() => {
    document.querySelector('.svelte-flow')?.setAttribute('data-mount-probe', 'stable');
  });

  const edge = page.locator(
    '.svelte-flow__edge[data-id^="source1->adder"] .svelte-flow__edge-interaction'
  );
  await edge.click();
  await page.keyboard.press('Delete');
  await expect(edge).toHaveCount(0);
  await saveFile(page);
  await expect.poll(() => work.bytes(file), { timeout: 20_000 }).not.toContain('&adder.in[0]');

  expect(await transform()).toBe(moved);
  expect(await viewportTransform()).toBe(movedViewport);
  expect(
    await page.evaluate(
      () => document.querySelector('.svelte-flow')?.getAttribute('data-mount-probe')
    )
  ).toBe('stable');
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
  await saveFile(page);
  await expect.poll(() => work.bytes(file), { timeout: 20_000 }).toContain('"Throttle", 777');
  await expect(page.getByTestId('undo')).toBeEnabled();

  const behind = work.bytes(file).replace('Hello World Plot Example', 'Edited Behind The App');
  writeFileSync(file, behind);
  await expect(page.getByTestId('reload-banner')).toBeVisible({ timeout: 30_000 });
  await shot('banner');

  await page.locator('input[data-field="throttle.ctor.1"]').fill('888');
  await page.locator('input[data-field="throttle.ctor.1"]').press('Enter');
  await expect(page.getByTestId('alert-toast')).toContainText('changed on disk');
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
  await saveFile(page);
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
  await expect(dialog.locator('li')).toHaveCount(1);
  await expect(dialog.locator('li').nth(0)).toContainText('set_initial_window');
  await shot('delete-refusal');

  expect(work.bytes(file)).toBe(original);
});

test('g) a new block reaches disk only after its required arguments are saved', async ({
  page,
  work,
  openFile,
  shot
}) => {
  const file = work.copy('hello_world.cpp');
  const original = work.bytes(file);

  await page.goto('/');
  await openFile(file);

  if (!(await page.getByTestId('rail-tab-library').isVisible())) {
    await page.getByTestId('toggle-right').click();
  }
  await page.getByTestId('rail-tab-library').click();
  await page.getByTestId('palette-search').fill('SourceCW');
  await page.dragAndDrop('[data-block="SourceCWBlock"]', '.svelte-flow__pane', {
    targetPosition: { x: 760, y: 620 }
  });
  const node = page.locator('.svelte-flow__node[data-id="source_c_w"]');
  await expect(node).toBeVisible();
  await expect(node.locator('.block')).toHaveAttribute('data-invalid', 'true');
  await expect(page.getByTestId('run')).toBeDisabled();
  await shot('required-args');

  for (const [field, value, missing] of [
    ['source_c_w.template.0', 'float', 'T'],
    ['source_c_w.ctor.1', '1.0f', 'amplitude'],
    ['source_c_w.ctor.2', '2.0f', 'frequency_hz'],
    ['source_c_w.ctor.3', '1000', 'sps']
  ]) {
    const input = page.locator(`input[data-field="${field}"]`);
    await input.fill(value);
    await input.press('Enter');
    await expect(node.locator('.block')).not.toHaveAttribute('title', new RegExp(`\\b${missing}\\b`));
    expect(work.bytes(file)).toBe(original);
  }

  await expect(node.locator('.block')).not.toHaveAttribute('data-invalid', 'true');
  await saveFile(page);
  await expect
    .poll(() => work.bytes(file), { timeout: 20_000 })
    .toContain('SourceCWBlock<float> source_c_w("source_c_w", 1.0f, 2.0f, 1000);');
});
