import { describe, expect, it } from 'vitest';
import { REQUIRED_ARGUMENT_PLACEHOLDER } from '../src/lib/palette';
import { lineOfOffset, type Span } from '../src/lib/schema';
import {
  anchor,
  boot,
  CASE,
  centre,
  commands,
  dragBlock,
  dragWire,
  FAKE_PATH,
  handle,
  highlighted,
  menuIds,
  modelOf,
  openLibrary,
  openMenu,
  paneOrigin,
  sent,
  shot,
  sourceOf,
  specs,
  useBrowser,
  type FakeWindow,
  viewer,
  wire,
  withoutEdge
} from './ui';

useBrowser();


/* ================================================================ palette */

describe('the palette lists what the crate found', () => {
  it(
    'renders the discovered directory tree with ports and may_block chips, and searches it',
    async () => {
      const page = await boot();
      await openLibrary(page);
      expect(await page.locator('[data-testid="rail-tabs"] button').count()).toBe(3);
      expect(await page.getAttribute('[data-testid="rail-tab-library"]', 'aria-selected')).toBe(
        'true'
      );
      const entries = page.locator('[data-testid="palette"] .entry');
      expect(await entries.count()).toBe(0);
      expect(
        await page.getAttribute('[data-library-path="math"] > .folder-row', 'aria-expanded')
      ).toBe('false');

      await page.click('[data-library-path="math"] > .folder-row');
      expect(
        await page.locator('[data-library-path="math"] [data-block="GainBlock"]').count()
      ).toBe(1);
      expect(await page.textContent('[data-testid="palette"] [data-block="AddBlock"]')).toContain(
        'n in · 1 out'
      );

      await page.click('[data-library-path="sources"] > .folder-row');
      expect(await page.locator('[data-testid="palette"] .chip').count()).toBeGreaterThan(0);

      await page.click('[data-library-path="math"] > .folder-row');
      expect(await page.locator('[data-block="GainBlock"]').count()).toBe(0);

      await page.fill('[data-testid="palette-search"]', 'fanout');
      await expect.poll(() => entries.count()).toBe(1);
      expect(await entries.first().getAttribute('data-block')).toBe('FanoutBlock');

      expect(await page.getAttribute('[data-block="FanoutBlock"] .row', 'title')).toContain(
        'const size_t num_outputs'
      );

      await page.fill('[data-testid="palette-search"]', 'zzz');
      await expect.poll(() => entries.count()).toBe(0);
      await shot(page, 'palette-open');
      await page.close();
    },
    CASE
  );

  it(
    'lists the blocks defined in the open translation unit',
    async () => {
      const page = await boot({ fixture: 'mass_spring_damper' });
      await openLibrary(page);
      await page.fill('[data-testid="palette-search"]', 'plant');
      await expect.poll(() => page.locator('[data-block="PlantBlock"]').count()).toBe(1);
      await page.close();
    },
    CASE
  );

  it(
    'falls back to the example blocks and refuses to drag in the browser',
    async () => {
      const page = await viewer('?example=hello_world');
      await openLibrary(page);
      await page.click('[data-library-path="this file"] > .folder-row');
      expect(await page.locator('[data-testid="palette"] .entry').count()).toBe(4);
      expect(await page.getAttribute('[data-testid="demo-chip"]', 'title')).toContain(
        'example mode'
      );
      expect(await page.getAttribute('[data-block="AddBlock"]', 'draggable')).toBe('false');
      await page.close();
    },
    CASE
  );
});

/* ================================================================ add block */

describe('adding a block declares it unwired', () => {
  it(
    'drops from the palette immediately and stays invalid until required fields are filled',
    async () => {
      const page = await boot();
      await openLibrary(page);
      await page.fill('[data-testid="palette-search"]', 'gain');
      await dragBlock(page, 'GainBlock', { x: 620, y: 620 });

      await expect.poll(() => commands(page)).toEqual([
        {
          command: 'add_block',
          site: 0,
          type: 'GainBlock',
          template_args: [REQUIRED_ARGUMENT_PLACEHOLDER],
          ctor_args: ['"gain"', REQUIRED_ARGUMENT_PLACEHOLDER, '0'],
          var_name: 'gain'
        }
      ]);

      const node = page.locator('.svelte-flow__node[data-id="gain"]');
      await node.waitFor();
      expect(await node.locator('.block.unwired').count()).toBe(1);
      expect(await node.locator('.block').getAttribute('data-invalid')).toBe('true');
      expect(await page.textContent('.inspector .title')).toBe('gain');
      expect(await page.inputValue('input[data-field="gain.template.0"]')).toBe('');
      expect(await page.inputValue('input[data-field="gain.ctor.1"]')).toBe('');
      expect(await page.locator('[data-testid="add-block"]').count()).toBe(0);
      await shot(page, 'palette-drag-required');

      await page.fill('input[data-field="gain.template.0"]', 'float');
      await page.press('input[data-field="gain.template.0"]', 'Enter');
      await page.fill('input[data-field="gain.ctor.1"]', '2.0f');
      await page.press('input[data-field="gain.ctor.1"]', 'Enter');
      await expect.poll(() => node.locator('.block').getAttribute('data-invalid')).toBe(null);
      await page.close();
    },
    CASE
  );

  it(
    'keeps the dropped node where it was dropped',
    async () => {
      const page = await boot();
      await openLibrary(page);
      await page.fill('[data-testid="palette-search"]', 'gain');
      await dragBlock(page, 'GainBlock', { x: 700, y: 660 });

      const node = page.locator('.svelte-flow__node[data-id="gain"]');
      await node.waitFor();
      const pane = await paneOrigin(page);
      const dropped = await node.boundingBox();
      if (!dropped) throw new Error('no node box');
      expect(Math.abs(dropped.x - (pane.x + 700))).toBeLessThan(60);
      expect(Math.abs(dropped.y - (pane.y + 660))).toBeLessThan(60);

      await page.click('.svelte-flow__node[data-id="adder"]');
      await page.fill('input[data-field="adder.display_name"]', 'Summer');
      await page.press('input[data-field="adder.display_name"]', 'Enter');
      await expect.poll(() => page.textContent('.svelte-flow__node[data-id="adder"] .name')).toBe(
        'Summer'
      );
      const settled = await node.boundingBox();
      expect(settled?.x).toBeCloseTo(dropped.x, 0);
      expect(settled?.y).toBeCloseTo(dropped.y, 0);
      await page.close();
    },
    CASE
  );

  it(
    'surfaces a crate refusal on the field that caused it and keeps the form open',
    async () => {
      const page = await boot({
        refusal: JSON.stringify({ error: 'duplicate_variable', var_name: 'gain' })
      });
      await openLibrary(page);
      await page.fill('[data-testid="palette-search"]', 'gain');
      await dragBlock(page, 'GainBlock', { x: 620, y: 620 });
      await page.waitForSelector('[data-testid="add-block"]');

      await expect.poll(() => page.textContent('[data-add-error="var_name"]')).toBe(
        'gain is already declared in this function'
      );
      expect(await page.locator('[data-testid="add-block"]').count()).toBe(1);
      expect(await page.locator('.svelte-flow__node[data-id="gain"]').count()).toBe(0);
      await page.close();
    },
    CASE
  );

  it(
    'adds from the pane menu at the click point',
    async () => {
      const page = await boot();
      await openMenu(page, '.svelte-flow__pane', { x: 300, y: 700 });
      await page.click('[data-testid="menu-add-here"]');
      await page.waitForSelector('[data-testid="add-search"]');
      await page.fill('[data-testid="add-search"]', 'gain');
      await page.click('[data-add-pick="GainBlock"]');
      await page.fill('[data-add-field="template.0"]', 'float');
      await page.fill('[data-add-field="ctor.1"]', '2.0f');
      await page.click('[data-testid="add-confirm"]');

      const node = page.locator('.svelte-flow__node[data-id="gain"]');
      await node.waitFor();
      const pane = await paneOrigin(page);
      const box = await node.boundingBox();
      if (!box) throw new Error('no node box');
      expect(Math.abs(box.x - (pane.x + 300))).toBeLessThan(60);
      expect(Math.abs(box.y - (pane.y + 700))).toBeLessThan(60);
      await page.close();
    },
    CASE
  );

  it(
    'cancels on Escape without sending anything',
    async () => {
      const page = await boot();
      await openMenu(page, '.svelte-flow__pane', { x: 300, y: 700 });
      await page.click('[data-testid="menu-add-here"]');
      await page.waitForSelector('[data-testid="add-search"]');
      await page.press('[data-testid="add-search"]', 'Escape');
      await expect.poll(() => page.locator('[data-testid="add-block"]').count()).toBe(0);
      expect(await commands(page)).toEqual([]);
      await page.close();
    },
    CASE
  );
});

/* ================================================================ connect */

describe('wiring is one gesture and one transaction', () => {
  it(
    'sends connect alone when the declared arity already covers the slot',
    async () => {
      const page = await boot({ model: withoutEdge(modelOf('hello_world'), 'source2', 'adder') });
      await dragWire(page, handle('source2', 'out'), handle('adder', 'in[1]'));

      await expect.poll(() => commands(page)).toEqual([
        { command: 'connect', site: 0, from: 'source2', to: 'adder', port: 'in', port_index: 1 }
      ]);
      expect((await sent(page)).length).toBe(1);
      await page.close();
    },
    CASE
  );

  it(
    'co-patches the template arg and the wire in one transaction, in order',
    async () => {
      const page = await boot({ model: withoutEdge(modelOf('hello_world'), 'source2', 'adder') });
      await dragWire(page, handle('source2', 'out'), handle('adder', 'in[2]'));

      await expect.poll(() => sent(page)).toEqual([
        [
          {
            command: 'set_template_arg',
            site: 0,
            block: 'adder',
            template_arg_index: 1,
            new_text: '3'
          },
          { command: 'connect', site: 0, from: 'source2', to: 'adder', port: 'in', port_index: 2 }
        ]
      ]);
      await expect
        .poll(() => page.textContent('.svelte-flow__node[data-id="adder"] .type'))
        .toBe('AddBlock<float, 3>');
      await page.close();
    },
    CASE
  );

  it(
    'bumps a FanoutBlock(2) ctor argument to 3 in the same gesture',
    async () => {
      const model = withoutEdge(modelOf('mass_spring_damper'), 'throttle', 'plant');
      const page = await boot({ fixture: 'mass_spring_damper', model });
      await page.keyboard.press('Control+`');
      await page.waitForSelector('[data-testid="drawer-body"]');
      await page.click('.svelte-flow__node[data-id="fanout"]');
      await expect
        .poll(() => highlighted(page))
        .toContain('FanoutBlock<float> fanout("Fanout", 2);');
      await shot(page, 'arity-copatch-before');

      await dragWire(page, handle('fanout', 'out'), handle('plant', 'force_in'));

      await expect.poll(() => sent(page)).toEqual([
        [
          { command: 'set_param', site: 0, block: 'fanout', ctor_arg_index: 1, new_text: '3' },
          {
            command: 'connect',
            site: 0,
            from: 'fanout',
            to: 'plant',
            port: 'force_in',
            port_index: null
          }
        ]
      ]);
      await expect
        .poll(() => page.evaluate(() => (window as unknown as FakeWindow).__fake.source()))
        .toContain('FanoutBlock<float> fanout("Fanout", 3);');
      await expect
        .poll(() => highlighted(page))
        .toContain('FanoutBlock<float> fanout("Fanout", 3);');
      await shot(page, 'arity-copatch-after');
      await page.close();
    },
    CASE
  );

  it(
    'refuses to grow a label-sized port and says to add a label first',
    async () => {
      const page = await boot({ model: withoutEdge(modelOf('hello_world'), 'source2', 'adder') });
      await dragWire(page, handle('source2', 'out'), handle('plot', 'in[1]'));

      await expect.poll(() => page.textContent('[data-testid="status"]')).toContain(
        'add a label there first'
      );
      expect(await page.textContent('[data-testid="status"]')).toContain('signal_labels');
      expect(await commands(page)).toEqual([]);
      await page.close();
    },
    CASE
  );

  it(
    'moves a wire with one reconnect transaction',
    async () => {
      const page = await boot({ model: withoutEdge(modelOf('hello_world'), 'source2', 'adder') });
      await dragWire(page, anchor('source1->adder.in[0]#0', 'target'), handle('adder', 'in[1]'));

      await expect.poll(() => sent(page)).toEqual([
        [
          { command: 'disconnect', site: 0, edge: 0 },
          { command: 'connect', site: 0, from: 'source1', to: 'adder', port: 'in', port_index: 1 }
        ]
      ]);
      await page.close();
    },
    CASE
  );

  it(
    'leaves a refused reconnect where it was',
    async () => {
      const page = await boot();
      await dragWire(page, anchor('source1->adder.in[0]#0', 'target'), handle('plot', 'in[1]'));

      await expect.poll(() => page.textContent('[data-testid="status"]')).toContain('add a label');
      expect(await commands(page)).toEqual([]);
      expect(
        await page.locator('.svelte-flow__edge[data-id^="source1->adder.in[0]"]').count()
      ).toBe(1);
      await page.close();
    },
    CASE
  );
});

/* ================================================================ unwiring */

describe('unwiring, removing and deleting', () => {
  it(
    'disconnects from the edge menu and refuses a read-only wire with its reason',
    async () => {
      const model = modelOf('hello_world');
      const locked = model.sites[0]?.edges[3];
      if (!locked) throw new Error('no fourth edge');
      locked.editable = false;
      locked.read_only_reason = 'method_call_port';

      const page = await boot({ model });
      await openMenu(page, '.svelte-flow__edge[data-id^="source1->adder"]');
      expect(await menuIds(page)).toEqual(['menu-disconnect']);
      await page.click('[data-testid="menu-disconnect"]');
      await expect.poll(() => commands(page)).toEqual([
        { command: 'disconnect', site: 0, edge: 0 }
      ]);

      await openMenu(page, '.svelte-flow__edge[data-id^="throttle->plot"]');
      expect(await page.locator('[data-testid="menu-disconnect"]').isDisabled()).toBe(true);
      expect(await page.getAttribute('[data-testid="menu-disconnect"]', 'title')).toContain(
        'method call port'
      );
      await page.close();
    },
    CASE
  );

  it(
    'removes a block from the graph and renders it hatched',
    async () => {
      const page = await boot();
      await openMenu(page, '.svelte-flow__node[data-id="source1"]');
      await page.click('[data-testid="menu-remove"]');

      await expect.poll(() => commands(page)).toEqual([
        { command: 'remove_from_graph', site: 0, block: 'source1' }
      ]);
      await expect
        .poll(() => page.locator('.svelte-flow__node[data-id="source1"] .block.unwired').count())
        .toBe(1);
      await page.close();
    },
    CASE
  );

  it(
    'shows the delete refusal as a dialog listing every outside reference',
    async () => {
      const source = sourceOf('hello_world');
      const spans: Span[] = [];
      for (const needle of ['plot.set_initial_window', 'plot.render()']) {
        const at = source.indexOf(needle);
        if (at === -1) throw new Error(`no ${needle}`);
        spans.push({ start: at, end: at + 4 });
      }
      const page = await boot({
        refusal: JSON.stringify({ error: 'references_outside_graph', block: 'plot', spans })
      });

      await openMenu(page, '.svelte-flow__node[data-id="plot"]');
      await page.click('[data-testid="menu-delete-block"]');
      const dialog = page.locator('[data-testid="delete-refusal"]');
      await dialog.waitFor();
      expect(await dialog.textContent()).toContain('plot cannot be deleted');
      expect(await dialog.textContent()).toContain('2 places');
      for (const span of spans) {
        expect(await dialog.textContent()).toContain(`line ${lineOfOffset(source, span.start)}`);
      }
      await shot(page, 'delete-refusal');

      await dialog.locator(`[data-reference="${spans[1]?.start}"]`).click();
      await page.waitForSelector('[data-testid="code-drawer"]');
      await expect
        .poll(() => page.locator('[data-testid="code-drawer"] .hit').count())
        .toBeGreaterThan(0);

      await page.click('[data-testid="refusal-close"]');
      await expect.poll(() => dialog.count()).toBe(0);
      expect(await page.locator('.svelte-flow__node[data-id="plot"]').count()).toBe(1);
      await page.close();
    },
    CASE
  );

  it(
    'deletes a block nothing else mentions',
    async () => {
      const page = await boot();
      await openMenu(page, '.svelte-flow__node[data-id="source2"]');
      await page.click('[data-testid="menu-delete-block"]');
      await expect.poll(() => commands(page)).toEqual([
        { command: 'delete_block', site: 0, block: 'source2' }
      ]);
      await expect.poll(() => page.locator('.svelte-flow__node[data-id="source2"]').count()).toBe(0);
      expect(await page.locator('[data-testid="delete-refusal"]').count()).toBe(0);
      await page.close();
    },
    CASE
  );
});

/* ================================================================ delete key */

describe('the Delete key follows the selection', () => {
  it(
    'disconnects a selected edge, removes a selected node, and stays quiet while typing',
    async () => {
      const page = await boot();
      await page.click(wire('source1', 'adder'));
      await page.keyboard.press('Delete');
      await expect.poll(() => commands(page)).toEqual([
        { command: 'disconnect', site: 0, edge: 0 }
      ]);

      await page.click('.svelte-flow__node[data-id="source2"]');
      await page.keyboard.press('Delete');
      await expect.poll(() => commands(page)).toEqual([
        { command: 'disconnect', site: 0, edge: 0 },
        { command: 'remove_from_graph', site: 0, block: 'source2' }
      ]);

      await page.click('.svelte-flow__node[data-id="adder"]');
      await page.click('input[data-field="adder.display_name"]');
      await page.keyboard.press('Delete');
      await page.keyboard.press('Backspace');
      await page.waitForTimeout(200);
      expect(await commands(page)).toHaveLength(2);
      await page.close();
    },
    CASE
  );

  it(
    'never deletes the declaration from the keyboard',
    async () => {
      const page = await boot();
      await page.click('.svelte-flow__node[data-id="source2"]');
      await page.keyboard.press('Backspace');
      await expect.poll(() => commands(page)).toEqual([
        { command: 'remove_from_graph', site: 0, block: 'source2' }
      ]);
      expect((await commands(page)).some((entry) => entry.command === 'delete_block')).toBe(false);
      await page.close();
    },
    CASE
  );
});

/* ================================================================ menus */

describe('each right-click target has its own menu', () => {
  it(
    'keeps block, edge and pane entries apart',
    async () => {
      const page = await boot();

      await openMenu(page, '.svelte-flow__node[data-id="adder"]');
      expect(await page.getAttribute('[data-testid="context-menu"]', 'data-menu')).toBe('block');
      expect(await menuIds(page)).toEqual([
        'menu-view-source',
        'menu-copy-declaration',
        'menu-open-editor',
        'menu-remove',
        'menu-delete-block'
      ]);
      await page.keyboard.press('Escape');

      await openMenu(page, '.svelte-flow__edge[data-id^="source1->adder"]');
      expect(await page.getAttribute('[data-testid="context-menu"]', 'data-menu')).toBe('edge');
      expect(await menuIds(page)).toEqual(['menu-disconnect']);
      expect(await page.textContent('[data-testid="menu-edge-info"]')).toContain(
        'source1 → adder.in[0]'
      );
      await page.keyboard.press('Escape');

      await openMenu(page, '.svelte-flow__pane', { x: 660, y: 760 });
      expect(await page.getAttribute('[data-testid="context-menu"]', 'data-menu')).toBe('pane');
      expect(await menuIds(page)).toEqual([
        'menu-add-here',
        'menu-check',
        'menu-build',
        'menu-run',
        'menu-save',
        'menu-undo',
        'menu-redo',
        'menu-fit'
      ]);
      expect(await page.locator('[data-testid="menu-edge-info"]').count()).toBe(0);
      await page.close();
    },
    CASE
  );
});

/* ================================================================ problems */

describe('the problems chip surfaces what the model says', () => {
  it(
    'counts the conflicts in this site and selects the element on click',
    async () => {
      const page = await boot({ fixture: 'type_conflict' });
      const chip = page.locator('[data-testid="problems"]');
      expect(await chip.getAttribute('data-count')).toBe('1');
      expect(await chip.textContent()).toContain('1 problem');

      await chip.click();
      const list = page.locator('[data-testid="problems-list"]');
      await list.waitFor();
      expect(await list.textContent()).toContain('throttle → throughput');
      expect(await list.textContent()).toContain('float out into std::complex<float> in');

      await list.locator('button').first().click();
      await expect
        .poll(() => page.locator('.svelte-flow__edge.selected').count())
        .toBeGreaterThan(0);
      await expect.poll(() => page.textContent('.inspector')).toContain('throughput');
      await page.close();
    },
    CASE
  );

  it(
    'shows zero for a clean graph',
    async () => {
      const page = await boot();
      expect(await page.getAttribute('[data-testid="problems"]', 'data-count')).toBe('0');
      expect(await page.textContent('[data-testid="problems"]')).toContain('0 problems');
      await page.close();
    },
    CASE
  );
});

/* ================================================================ undo */

describe('every topology gesture round-trips through undo', () => {
  it(
    'undoes an add, a connect, a disconnect and a remove',
    async () => {
      const page = await boot({ model: withoutEdge(modelOf('hello_world'), 'source2', 'adder') });
      const undo = page.locator('[data-testid="undo"]');
      const nodes = () => page.locator('.svelte-flow__node').count();
      const wires = () => page.locator('.svelte-flow__edge').count();
      const before = { nodes: await nodes(), wires: await wires() };

      await openLibrary(page);
      await page.fill('[data-testid="palette-search"]', 'gain');
      await dragBlock(page, 'GainBlock', { x: 620, y: 640 });
      await page.locator('.svelte-flow__node[data-id="gain"]').waitFor();
      await undo.click();
      await expect.poll(nodes).toBe(before.nodes);

      await dragWire(page, handle('source2', 'out'), handle('adder', 'in[1]'));
      await expect.poll(wires).toBe(before.wires + 1);
      await undo.click();
      await expect.poll(wires).toBe(before.wires);

      await page.click(wire('source1', 'adder'));
      await page.keyboard.press('Delete');
      await expect.poll(wires).toBe(before.wires - 1);
      await undo.click();
      await expect.poll(wires).toBe(before.wires);

      await openMenu(page, '.svelte-flow__node[data-id="source1"]');
      await page.click('[data-testid="menu-remove"]');
      await expect
        .poll(() => page.locator('.svelte-flow__node[data-id="source1"] .block.unwired').count())
        .toBe(1);
      await undo.click();
      await expect
        .poll(() => page.locator('.svelte-flow__node[data-id="source1"] .block.unwired').count())
        .toBe(0);

      expect(await page.locator('[data-testid="redo"]').isDisabled()).toBe(false);
      await page.close();
    },
    CASE
  );
});
