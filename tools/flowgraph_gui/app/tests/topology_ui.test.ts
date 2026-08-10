import { chromium, type Browser, type Page } from 'playwright';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { createServer, type ViteDevServer } from 'vite';
import { fixtures, fixtureSources } from '../src/fixtures';
import type { BlockSpec } from '../src/lib/palette';
import { lineOfOffset, type Command, type FileModel, type Span } from '../src/lib/schema';
import shipped from './palette.json';

const BOOT_TIMEOUT = 180_000;
const CASE = 90_000;
const VIEWPORT = { width: 1440, height: 900 };
const FAKE_PATH = '/tmp/fake/hello_world.cpp';
const SHOTS = (globalThis as { process?: { env: Record<string, string | undefined> } }).process
  ?.env.CLER_SHOTS;
const specs = shipped.blocks as unknown as BlockSpec[];

type Setup = {
  path: string;
  model: FileModel;
  source: string;
  specs: BlockSpec[];
  refusal: unknown;
};

type Fake = {
  log: Command[][];
  calls: string[];
  source: () => string;
  refuse: (value: unknown) => void;
};

type FakeWindow = { __fake: Fake };

let server: ViteDevServer;
let browser: Browser;
let origin: string;

/* ---------------------------------------------------------------- fake backend */

function installFake(setup: Setup) {
  const state = {
    path: setup.path,
    revision: 1,
    model: setup.model,
    source: setup.source,
    canUndo: false,
    canRedo: false,
    externalChange: false
  };
  const undone: string[] = [];
  const redone: string[] = [];
  const log: unknown[][] = [];
  const calls: string[] = [];
  const callbacks = new Map<number, (message: unknown) => void>();
  let refusal: unknown = setup.refusal;
  let nextCallback = 1;

  type Loose = Record<string, unknown>;

  function snapshot(): unknown {
    return JSON.parse(JSON.stringify(state));
  }

  function site(index: number): Loose {
    const found = (state.model.sites as unknown as Loose[])[index];
    if (!found) throw new Error(`no site ${index}`);
    return found;
  }

  function blocks(index: number): Loose[] {
    return site(index).blocks as Loose[];
  }

  function edges(index: number): Loose[] {
    return site(index).edges as Loose[];
  }

  function runners(index: number): Loose[] {
    return site(index).runners as Loose[];
  }

  function block(index: number, name: string): Loose {
    const found = blocks(index).find((candidate) => candidate.var === name);
    if (!found) throw new Error(`no block ${name}`);
    return found;
  }

  function shiftOffsets(value: unknown, from: number, delta: number) {
    if (Array.isArray(value)) {
      for (const item of value) shiftOffsets(item, from, delta);
      return;
    }
    if (typeof value !== 'object' || value === null) return;
    const record = value as Loose;
    for (const key of ['start', 'end', 'call_offset']) {
      const at = record[key];
      if (typeof at === 'number' && at >= from) record[key] = at + delta;
    }
    for (const nested of Object.values(record)) shiftOffsets(nested, from, delta);
  }

  function splice(span: { start: number; end: number }, text: string) {
    const delta = text.length - (span.end - span.start);
    state.source = state.source.slice(0, span.start) + text + state.source.slice(span.end);
    shiftOffsets(state.model, span.end, delta);
  }

  function rewriteArg(holder: Loose, list: string, at: number, text: string) {
    const arg = (holder[list] as Loose[])[at];
    if (!arg) throw new Error(`no ${list}[${at}]`);
    const span = { ...(arg.span as { start: number; end: number }) };
    arg.text = text;
    splice(span, text);
  }

  function wired(index: number, name: string): boolean {
    return (
      runners(index).some((runner) => runner.block === name) ||
      edges(index).some((edge) => edge.to === name || edge.from === name)
    );
  }

  function restate(index: number) {
    for (const entry of blocks(index)) entry.in_graph = wired(index, String(entry.var));
  }

  function apply(commands: Loose[]) {
    undone.push(JSON.stringify({ model: state.model, source: state.source }));
    redone.length = 0;
    log.push(commands);
    for (const command of commands) {
      const at = command.site as number;
      if (command.command === 'set_param') {
        rewriteArg(
          block(at, String(command.block)),
          'ctor_args',
          command.ctor_arg_index as number,
          String(command.new_text)
        );
      } else if (command.command === 'set_template_arg') {
        rewriteArg(
          block(at, String(command.block)),
          'template_args',
          command.template_arg_index as number,
          String(command.new_text)
        );
      } else if (command.command === 'set_display_name') {
        block(at, String(command.block)).display_name = String(command.new_text);
      } else if (command.command === 'add_block') {
        blocks(at).push({
          var: String(command.var_name),
          type_text: String(command.type),
          type_name: String(command.type),
          alias: null,
          template_args: (command.template_args as string[]).map((text) => ({
            text,
            resolved: null,
            span: { start: 0, end: 0 }
          })),
          ctor_args: (command.ctor_args as string[]).map((text) => ({
            text,
            span: { start: 0, end: 0 }
          })),
          display_name: (command.ctor_args as string[])[0]?.replace(/"/g, '') ?? null,
          in_graph: false,
          span: { start: 0, end: 0 },
          editable: true,
          read_only_reason: null
        });
      } else if (command.command === 'connect') {
        const index = command.port_index as number | null;
        edges(at).push({
          from: String(command.from),
          to: String(command.to),
          port: {
            name: String(command.port),
            index,
            kind: index === null ? 'field' : 'indexed_field'
          },
          runner_index: runners(at).length,
          arg_index: 1,
          text: `&${String(command.to)}.${String(command.port)}`,
          span: { start: 0, end: 0 },
          editable: true,
          read_only_reason: null,
          sample_type: null,
          source_type: null,
          type_conflict: false
        });
        if (!runners(at).some((runner) => runner.block === command.from)) {
          runners(at).push({
            index: runners(at).length,
            block: String(command.from),
            block_expr: `&${String(command.from)}`,
            may_block: false,
            form: 'inline',
            span: { start: 0, end: 0 },
            editable: true,
            read_only_reason: null
          });
        }
        restate(at);
      } else if (command.command === 'disconnect') {
        edges(at).splice(command.edge as number, 1);
        restate(at);
      } else if (command.command === 'remove_from_graph') {
        const name = String(command.block);
        site(at).runners = runners(at).filter((runner) => runner.block !== name);
        site(at).edges = edges(at).filter((edge) => edge.from !== name);
        restate(at);
      } else if (command.command === 'delete_block') {
        const name = String(command.block);
        site(at).blocks = blocks(at).filter((entry) => entry.var !== name);
        site(at).runners = runners(at).filter((runner) => runner.block !== name);
        site(at).edges = edges(at).filter((edge) => edge.from !== name && edge.to !== name);
        restate(at);
      }
    }
    state.revision += 1;
    state.canUndo = undone.length > 0;
    state.canRedo = redone.length > 0;
  }

  function step(from: string[], to: string[]) {
    const previous = from.pop();
    if (previous) {
      to.push(JSON.stringify({ model: state.model, source: state.source }));
      const restored = JSON.parse(previous) as { model: FileModel; source: string };
      state.model = restored.model;
      state.source = restored.source;
      state.revision += 1;
    }
    state.canUndo = undone.length > 0;
    state.canRedo = redone.length > 0;
  }

  async function invoke(command: string, args: Record<string, unknown>): Promise<unknown> {
    if (command === 'plugin:event|listen') return args.handler;
    if (command === 'plugin:dialog|open') return state.path;
    calls.push(command);
    if (command === 'palette') return JSON.parse(JSON.stringify(setup.specs));
    if (command === 'apply_commands') {
      const commands = args.commands as Loose[];
      if (refusal !== null && refusal !== undefined) {
        const thrown = refusal;
        refusal = null;
        log.push(commands);
        throw thrown;
      }
      apply(commands);
      return snapshot();
    }
    if (command === 'undo') step(undone, redone);
    if (command === 'redo') step(redone, undone);
    if (command === 'close_document') return null;
    if (command === 'open_in_editor') return null;
    return snapshot();
  }

  const scope = window as unknown as Record<string, unknown>;
  scope.__TAURI_EVENT_PLUGIN_INTERNALS__ = { unregisterListener: () => undefined };
  scope.__TAURI_INTERNALS__ = {
    invoke,
    transformCallback: (callback: (message: unknown) => void) => {
      const id = nextCallback++;
      callbacks.set(id, callback);
      return id;
    },
    unregisterCallback: (id: number) => {
      callbacks.delete(id);
    }
  };
  scope.__fake = {
    log,
    calls,
    source: () => state.source,
    refuse: (value: unknown) => {
      refusal = value;
    }
  };
}

/* ---------------------------------------------------------------- harness */

function modelOf(name: string): FileModel {
  const base = structuredClone(fixtures[name]);
  if (!base) throw new Error(`missing fixture ${name}`);
  base.file = FAKE_PATH;
  return base;
}

function sourceOf(name: string): string {
  const found = fixtureSources[name];
  if (found === undefined) throw new Error(`missing source ${name}`);
  return found;
}

function withoutEdge(model: FileModel, from: string, to: string): FileModel {
  const site = model.sites[0];
  if (!site) throw new Error('no site');
  site.edges = site.edges.filter((edge) => !(edge.from === from && edge.to === to));
  return model;
}

type BootOptions = { fixture?: string; model?: FileModel; refusal?: unknown };

async function boot(options: BootOptions = {}): Promise<Page> {
  const name = options.fixture ?? 'hello_world';
  const page = await browser.newPage({ viewport: VIEWPORT });
  await page.addInitScript(installFake, {
    path: FAKE_PATH,
    model: options.model ?? modelOf(name),
    source: sourceOf(name),
    specs,
    refusal: options.refusal ?? null
  });
  await page.goto(origin, { waitUntil: 'load' });
  await page.click('button.primary');
  await page.waitForSelector(`.path[title="${FAKE_PATH}"]`);
  await page.waitForSelector('.svelte-flow__node');
  await page.waitForSelector('[data-testid="palette"] .entry');
  return page;
}

const sent = (page: Page) => page.evaluate(() => (window as unknown as FakeWindow).__fake.log);

const commands = async (page: Page) => (await sent(page)).flat();

async function highlighted(page: Page): Promise<string> {
  const pieces = await page.locator('[data-testid="code-drawer"] .hit').allTextContents();
  return pieces.join('');
}

async function shot(page: Page, name: string) {
  if (!SHOTS) return;
  await page.screenshot({ path: `${SHOTS}/${name}.png` });
}

function handle(node: string, id: string): string {
  return `.svelte-flow__node[data-id="${node}"] .svelte-flow__handle[data-handleid="${id}"]`;
}

function anchor(edge: string, end: 'source' | 'target'): string {
  return `[data-anchor^="${end}:${edge}"]`;
}

function wire(from: string, to: string): string {
  return `.svelte-flow__edge[data-id^="${from}->${to}"] .svelte-flow__edge-interaction`;
}

async function paneOrigin(page: Page): Promise<{ x: number; y: number }> {
  const box = await page.locator('.svelte-flow__pane').boundingBox();
  if (!box) throw new Error('no pane box');
  return { x: box.x, y: box.y };
}

async function centre(page: Page, selector: string): Promise<{ x: number; y: number }> {
  const box = await page.locator(selector).boundingBox();
  if (!box) throw new Error(`${selector} has no box`);
  return { x: box.x + box.width / 2, y: box.y + box.height / 2 };
}

async function dragWire(page: Page, from: string, to: string) {
  const start = await centre(page, from);
  const end = await centre(page, to);
  await page.mouse.move(start.x, start.y);
  await page.mouse.down();
  await page.mouse.move(start.x + 25, start.y, { steps: 4 });
  await page.mouse.move(end.x, end.y, { steps: 12 });
  await page.mouse.up();
}

async function dragBlock(page: Page, name: string, at: { x: number; y: number }) {
  await page
    .locator(`[data-block="${name}"]`)
    .dragTo(page.locator('.svelte-flow__pane'), { targetPosition: at });
}

async function openMenu(page: Page, selector: string, position?: { x: number; y: number }) {
  await page.locator(selector).click({ button: 'right', position });
  await page.waitForSelector('[data-testid="context-menu"]');
}

function menuIds(page: Page): Promise<(string | null)[]> {
  return page
    .locator('[data-testid="context-menu"] button')
    .evaluateAll((buttons) => buttons.map((button) => button.getAttribute('data-testid')));
}

beforeAll(async () => {
  server = await createServer({ logLevel: 'silent', server: { port: 0, strictPort: false } });
  await server.listen();
  const url = server.resolvedUrls?.local[0];
  if (!url) throw new Error('vite dev server reported no url');
  origin = url.replace(/\/$/, '');
  browser = await chromium.launch({ channel: 'chrome' });
}, BOOT_TIMEOUT);

afterAll(async () => {
  await browser?.close();
  await server?.close();
});

/* ================================================================ palette */

describe('the palette lists what the crate found', () => {
  it(
    'renders every spec with its category, ports and may_block chip, and searches them',
    async () => {
      const page = await boot();
      const entries = page.locator('[data-testid="palette"] .entry');
      expect(await entries.count()).toBe(specs.length);
      expect(await page.textContent('[data-testid="palette"] [data-block="GainBlock"]')).toContain(
        'math'
      );
      expect(await page.textContent('[data-testid="palette"] [data-block="AddBlock"]')).toContain(
        'n in · 1 out'
      );
      expect(await page.locator('[data-testid="palette"] .chip').count()).toBeGreaterThan(0);

      await page.fill('[data-testid="palette-search"]', 'fanout');
      await expect.poll(() => entries.count()).toBe(1);
      expect(await entries.first().getAttribute('data-block')).toBe('FanoutBlock');

      await page.click('[data-block="FanoutBlock"] .row');
      expect(await page.textContent('[data-testid="palette-signature"]')).toContain(
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
      await page.fill('[data-testid="palette-search"]', 'plant');
      await expect.poll(() => page.locator('[data-block="PlantBlock"]').count()).toBe(1);
      await page.close();
    },
    CASE
  );

  it(
    'falls back to the example blocks and refuses to drag in the browser',
    async () => {
      const page = await browser.newPage({ viewport: VIEWPORT });
      await page.goto(`${origin}/?example=hello_world`, { waitUntil: 'load' });
      await page.waitForSelector('[data-testid="palette"] .entry');
      expect(await page.locator('[data-testid="palette"] .entry').count()).toBe(4);
      expect(await page.textContent('[data-testid="palette-notice"]')).toContain('example mode');
      expect(await page.getAttribute('[data-block="AddBlock"]', 'draggable')).toBe('false');
      await page.close();
    },
    CASE
  );
});

/* ================================================================ add block */

describe('adding a block declares it unwired', () => {
  it(
    'drops from the palette, pre-fills the form and sends exactly one add_block',
    async () => {
      const page = await boot();
      await page.fill('[data-testid="palette-search"]', 'gain');
      await dragBlock(page, 'GainBlock', { x: 620, y: 620 });
      await page.waitForSelector('[data-testid="add-block"]');
      await shot(page, 'palette-drag-popover');

      expect(await page.inputValue('[data-add-field="var_name"]')).toBe('gain');
      expect(await page.inputValue('[data-add-field="ctor.0"]')).toBe('"gain"');
      expect(await page.inputValue('[data-add-field="ctor.2"]')).toBe('0');
      await page.fill('[data-add-field="template.0"]', 'float');
      await page.fill('[data-add-field="ctor.1"]', '2.0f');
      await page.click('[data-testid="add-confirm"]');

      await expect.poll(() => commands(page)).toEqual([
        {
          command: 'add_block',
          site: 0,
          type: 'GainBlock',
          template_args: ['float'],
          ctor_args: ['"gain"', '2.0f', '0'],
          var_name: 'gain'
        }
      ]);
      expect((await sent(page)).length).toBe(1);

      const node = page.locator('.svelte-flow__node[data-id="gain"]');
      await node.waitFor();
      expect(await node.locator('.block.unwired').count()).toBe(1);
      expect(await page.textContent('.inspector .title')).toBe('gain');
      expect(await page.locator('[data-testid="add-block"]').count()).toBe(0);
      await page.close();
    },
    CASE
  );

  it(
    'keeps the dropped node where it was dropped',
    async () => {
      const page = await boot();
      await page.fill('[data-testid="palette-search"]', 'gain');
      await dragBlock(page, 'GainBlock', { x: 700, y: 660 });
      await page.waitForSelector('[data-testid="add-block"]');
      await page.fill('[data-add-field="template.0"]', 'float');
      await page.fill('[data-add-field="ctor.1"]', '2.0f');
      await page.click('[data-testid="add-confirm"]');

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
      await page.fill('[data-testid="palette-search"]', 'gain');
      await dragBlock(page, 'GainBlock', { x: 620, y: 620 });
      await page.waitForSelector('[data-testid="add-block"]');
      await page.fill('[data-add-field="template.0"]', 'float');
      await page.fill('[data-add-field="ctor.1"]', '2.0f');
      await page.click('[data-testid="add-confirm"]');

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
      await page.click('[data-testid="drawer"]');
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

      await openMenu(page, '.svelte-flow__pane', { x: 40, y: 400 });
      expect(await page.getAttribute('[data-testid="context-menu"]', 'data-menu')).toBe('pane');
      expect(await menuIds(page)).toEqual([
        'menu-add-here',
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

      await page.fill('[data-testid="palette-search"]', 'gain');
      await dragBlock(page, 'GainBlock', { x: 620, y: 640 });
      await page.waitForSelector('[data-testid="add-block"]');
      await page.fill('[data-add-field="template.0"]', 'float');
      await page.fill('[data-add-field="ctor.1"]', '2.0f');
      await page.click('[data-testid="add-confirm"]');
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
