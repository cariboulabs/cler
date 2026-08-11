import { chromium, type Browser, type Page } from 'playwright';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { createServer, type ViteDevServer } from 'vite';
import { fixtures } from '../src/fixtures';
import { describeApplyError, queued, setInvoker } from '../src/lib/backend';
import { edgeIds, mergeProjection, projectSite } from '../src/lib/project';
import { siteViewIds, type Command, type FileModel, type Site } from '../src/lib/schema';
import { openInspector } from './ui';

const BOOT_TIMEOUT = 120_000;
const CASE = 60_000;
const VIEWPORT = { width: 1440, height: 900 };
const FAKE_PATH = '/tmp/fake/hello_world.cpp';
const OTHER_PATH = '/tmp/fake/other.cpp';

type FailMode = 'string' | 'json' | 'object' | 'null' | 'empty' | 'error' | 'undefined';

type Plan = { delay?: number; failMode?: FailMode; failValue?: unknown };

type CallRecord = { name: string; seq: number; start: number; end: number };

type Setup = { path: string; model: FileModel; shiftOffsets: boolean };

type Fake = {
  log: Command[];
  calls: CallRecord[];
  plan: (name: string, steps: Plan[]) => void;
  emitExternal: (path?: string) => void;
  setModel: (model: FileModel) => void;
  setPath: (path: string) => void;
  model: () => FileModel;
};

type FakeWindow = {
  __fake: Fake;
  __spy?: { calls: string[] };
  __TAURI_INTERNALS__: unknown;
  __TAURI_EVENT_PLUGIN_INTERNALS__: unknown;
};

let server: ViteDevServer;
let browser: Browser;
let origin: string;

/* ---------------------------------------------------------------- fake backend */

function installFake(setup: Setup) {
  type Step = { delay?: number; failMode?: string; failValue?: unknown };

  const state = {
    path: setup.path,
    revision: 1,
    model: setup.model,
    canUndo: false,
    canRedo: false,
    externalChange: false
  };
  const undone: string[] = [];
  const redone: string[] = [];
  const log: unknown[] = [];
  const calls: { name: string; seq: number; start: number; end: number }[] = [];
  const plans = new Map<string, Step[]>();
  const callbacks = new Map<number, (message: unknown) => void>();
  let nextCallback = 1;
  let seq = 0;

  function snapshot(): unknown {
    return JSON.parse(JSON.stringify(state));
  }

  function failure(step: Step): unknown {
    switch (step.failMode) {
      case 'string':
        return String(step.failValue);
      case 'json':
        return JSON.stringify(step.failValue);
      case 'object':
        return step.failValue;
      case 'null':
        return null;
      case 'empty':
        return '';
      case 'error':
        return new Error(String(step.failValue));
      case 'undefined':
        return undefined;
      default:
        return new Error('unplanned');
    }
  }

  function textOf(command: Record<string, unknown>): string {
    return String(command.new_text ?? command.new_value ?? '');
  }

  function shift(delta: number, from: number) {
    if (delta === 0) return;
    for (const site of state.model.sites) {
      if (site.call_offset >= from) site.call_offset += delta;
    }
  }

  function apply(commands: Record<string, unknown>[]) {
    undone.push(JSON.stringify(state.model));
    redone.length = 0;
    for (const command of commands) {
      log.push(command);
      const site = state.model.sites[command.site as number];
      if (!site) throw new Error(`no site ${String(command.site)}`);
      if (command.command === 'set_config') {
        const found = site.config?.assignments.find((entry) => entry.path === command.path);
        if (!found) throw new Error('no assignment');
        found.value = textOf(command);
        continue;
      }
      const block = site.blocks.find((candidate) => candidate.var === command.block);
      if (!block) throw new Error(`no block ${String(command.block)}`);
      let before = '';
      if (command.command === 'set_param') {
        const arg = block.ctor_args[command.ctor_arg_index as number];
        if (!arg) throw new Error('no ctor arg');
        before = arg.text;
        arg.text = textOf(command);
      } else if (command.command === 'set_template_arg') {
        const arg = block.template_args[command.template_arg_index as number];
        if (!arg) throw new Error('no template arg');
        before = arg.text;
        arg.text = textOf(command);
      } else {
        before = block.display_name ?? '';
        block.display_name = textOf(command);
      }
      if (setup.shiftOffsets) shift(textOf(command).length - before.length, block.span.start);
    }
    state.revision += 1;
    state.canUndo = undone.length > 0;
    state.canRedo = redone.length > 0;
  }

  function step(from: string[], to: string[]) {
    const previous = from.pop();
    if (previous) {
      to.push(JSON.stringify(state.model));
      state.model = JSON.parse(previous) as FileModel;
      state.revision += 1;
    }
    state.canUndo = undone.length > 0;
    state.canRedo = redone.length > 0;
  }

  async function invoke(command: string, args: Record<string, unknown>): Promise<unknown> {
    if (command === 'plugin:event|listen') return args.handler;
    if (command === 'plugin:dialog|open') return state.path;

    const mine = ++seq;
    const start = performance.now();
    const queue = plans.get(command);
    const planned = queue && queue.length > 0 ? queue.shift() : undefined;

    let result: unknown = null;
    let thrown: { value: unknown } | null = null;

    if (command === 'apply_commands') {
      const commands = args.commands as Record<string, unknown>[];
      if (planned?.failMode) {
        for (const entry of commands) log.push(entry);
        thrown = { value: failure(planned) };
      } else {
        apply(commands);
        result = snapshot();
      }
    } else if (planned?.failMode) {
      thrown = { value: failure(planned) };
    } else {
      if (command === 'undo') step(undone, redone);
      if (command === 'redo') step(redone, undone);
      if (command === 'reload_document') {
        state.externalChange = false;
        state.canUndo = false;
        state.canRedo = false;
        undone.length = 0;
        redone.length = 0;
      }
      result = command === 'close_document' ? null : snapshot();
    }

    if (planned?.delay) await new Promise((done) => setTimeout(done, planned.delay));
    calls.push({ name: command, seq: mine, start, end: performance.now() });
    if (thrown) throw thrown.value;
    return result;
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
    plan: (name: string, steps: Step[]) => plans.set(name, steps),
    emitExternal: (path?: string) => {
      for (const callback of callbacks.values()) {
        callback({
          event: 'document-changed-externally',
          id: 0,
          payload: { path: path ?? state.path }
        });
      }
    },
    setModel: (model: FileModel) => {
      state.model = model;
    },
    setPath: (path: string) => {
      state.path = path;
    },
    model: () => state.model
  };
}

/* ---------------------------------------------------------------- page helpers */

function helloModel(): FileModel {
  const base = structuredClone(fixtures.hello_world);
  if (!base) throw new Error('missing fixture');
  base.file = FAKE_PATH;
  const site = base.sites[0];
  if (!site) throw new Error('no site');
  site.config = {
    editable: true,
    read_only_reason: null,
    var: 'config',
    source: 'direct',
    run_call_span: { start: 0, end: 1 },
    assignments: [
      {
        editable: true,
        read_only_reason: null,
        path: 'scheduler',
        value: 'cler::SchedulerType::ThreadPerBlock',
        span: { start: 0, end: 1 },
        value_span: { start: 0, end: 1 }
      },
      {
        editable: false,
        read_only_reason: 'optional_emplace_declaration',
        path: 'adaptive_sleep',
        value: 'true',
        span: { start: 2, end: 3 },
        value_span: { start: 2, end: 3 }
      }
    ]
  };
  return base;
}

function uhdModel(): FileModel {
  const base = structuredClone(fixtures.uhd_device);
  if (!base) throw new Error('missing fixture');
  base.file = FAKE_PATH;
  return base;
}

async function boot(options: { model?: FileModel; shiftOffsets?: boolean } = {}): Promise<Page> {
  const page = await browser.newPage({ viewport: VIEWPORT });
  await page.addInitScript(openInspector);
  await page.addInitScript(installFake, {
    path: FAKE_PATH,
    model: options.model ?? helloModel(),
    shiftOffsets: options.shiftOffsets ?? false
  });
  await page.goto(origin, { waitUntil: 'load' });
  await page.click('button.primary');
  await page.waitForSelector(`.path[title="${FAKE_PATH}"]`);
  await page.waitForSelector('.svelte-flow__node');
  await page.waitForTimeout(300);
  return page;
}

const sent = (page: Page) => page.evaluate(() => (window as unknown as FakeWindow).__fake.log);
const calls = (page: Page) =>
  page.evaluate(() => (window as unknown as FakeWindow).__fake.calls) as Promise<CallRecord[]>;
const names = async (page: Page) => (await calls(page)).map((call) => call.name);
const applyCount = async (page: Page) =>
  (await names(page)).filter((name) => name === 'apply_commands').length;

async function plan(page: Page, name: string, steps: Plan[]) {
  await page.evaluate(
    ([command, entries]) =>
      (window as unknown as FakeWindow).__fake.plan(command as string, entries as Plan[]),
    [name, steps] as const
  );
}

async function select(page: Page, blockVar: string) {
  await page.click(`.svelte-flow__node[data-id="${blockVar}"]`);
  await page.waitForSelector('.inspector input');
}

async function positions(page: Page): Promise<Record<string, string>> {
  return page.evaluate(() => {
    const out: Record<string, string> = {};
    for (const node of document.querySelectorAll('.svelte-flow__node')) {
      out[node.getAttribute('data-id') ?? ''] = (node as HTMLElement).style.transform;
    }
    return out;
  });
}

beforeAll(async () => {
  server = await createServer({ logLevel: 'silent', server: { port: 0, strictPort: false } });
  await server.listen();
  const url = server.resolvedUrls?.local[0];
  if (!url) throw new Error('no dev server url');
  origin = url.replace(/\/$/, '');
  browser = await chromium.launch({ channel: 'chrome' });
}, BOOT_TIMEOUT);

afterAll(async () => {
  await browser?.close();
  await server?.close();
});

/* ================================================ 0. findings provable in node */

function siteOf(name: string, index: number): Site {
  const site = fixtures[name]?.sites[index];
  if (!site) throw new Error(`no site ${index} in ${name}`);
  return structuredClone(site);
}

describe('finding 1: site identity for the view ignores byte offsets', () => {
  it('numbers sites by function name and file order, not call_offset', () => {
    const model = structuredClone(fixtures.uhd_device);
    if (!model) throw new Error('missing fixture');
    const before = siteViewIds(model.sites);
    for (const site of model.sites) site.call_offset += 4096;
    expect(siteViewIds(model.sites)).toEqual(before);
    expect(new Set(before).size).toBe(model.sites.length);
  });

  it('keeps sites that share a function name apart', () => {
    const site = siteOf('hello_world', 0);
    expect(siteViewIds([site, site, site])).toEqual(['main#0', 'main#1', 'main#2']);
  });
});

describe('finding 13: edge identity is content based', () => {
  it('survives a runner reorder and still separates true parallels', () => {
    const site = siteOf('plots', 0);
    const before = edgeIds(site.edges);
    for (const edge of site.edges) edge.runner_index += 10;
    expect(edgeIds(site.edges)).toEqual(before);
    expect(new Set(before).size).toBe(site.edges.length);
    expect(before.every((id) => !/^e\d/.test(id))).toBe(true);
  });

  it('disambiguates two identical from/to/port edges', () => {
    const site = siteOf('hello_world', 0);
    const first = site.edges[0];
    if (!first) throw new Error('no edge');
    const twin = structuredClone(first);
    twin.runner_index = 99;
    const ids = edgeIds([first, twin]);
    expect(new Set(ids).size).toBe(2);
    expect(ids[0]?.endsWith('#0')).toBe(true);
    expect(ids[1]?.endsWith('#1')).toBe(true);
  });
});

describe('finding 13: a newcomer node lands near its neighbours, never at the origin', () => {
  it('places a wired newcomer beside the nodes it connects to', () => {
    const site = siteOf('hello_world', 0);
    const previous = projectSite(site);
    const laid = {
      nodes: previous.nodes.map((node, index) => ({
        ...node,
        position: { x: index * 300, y: index * 120 }
      })),
      edges: previous.edges
    };

    const grown = siteOf('hello_world', 0);
    const clone = structuredClone(grown.blocks[0]);
    if (!clone) throw new Error('no block');
    clone.var = 'newcomer';
    grown.blocks.push(clone);
    const wire = structuredClone(grown.edges[0]);
    if (!wire) throw new Error('no edge');
    wire.from = 'newcomer';
    wire.to = 'plot';
    wire.runner_index = 42;
    grown.edges.push(wire);

    const merged = mergeProjection(laid, projectSite(grown));
    const spot = merged.nodes.find((node) => node.id === 'newcomer')?.position;
    const anchor = laid.nodes.find((node) => node.id === 'plot')?.position;
    expect(spot).toBeDefined();
    expect(spot).not.toEqual({ x: 0, y: 0 });
    expect(spot?.x).toBeGreaterThan(anchor?.x ?? 0);
  });

  it('places an unwired newcomer clear of the laid-out block', () => {
    const site = siteOf('hello_world', 0);
    const previous = projectSite(site);
    const laid = {
      nodes: previous.nodes.map((node, index) => ({
        ...node,
        position: { x: index * 300, y: index * 120 }
      })),
      edges: previous.edges
    };

    const grown = siteOf('hello_world', 0);
    const clone = structuredClone(grown.blocks[0]);
    if (!clone) throw new Error('no block');
    clone.var = 'orphan';
    grown.blocks.push(clone);

    const merged = mergeProjection(laid, projectSite(grown));
    const spot = merged.nodes.find((node) => node.id === 'orphan')?.position;
    const lowest = Math.max(...laid.nodes.map((node) => node.position.y));
    expect(spot).not.toEqual({ x: 0, y: 0 });
    expect(spot?.y ?? 0).toBeGreaterThan(lowest);
  });
});

describe('finding 12: describeApplyError never renders a js value name', () => {
  const fallback = 'the edit was refused but no reason was given';

  it('falls back when the rejection carries no message at all', () => {
    for (const rejection of [null, undefined, '', '   ', new Error('')]) {
      expect(describeApplyError(rejection)).toBe(fallback);
    }
  });

  it('never prints [object Object] for a thrown object', () => {
    const message = describeApplyError({ code: 7, detail: 'runner uses a named variable' });
    expect(message).toBe('the backend refused: runner uses a named variable');
    expect(message).not.toContain('[object');
  });

  it('clips an opaque object to a bounded json tail', () => {
    const message = describeApplyError({ code: 7, payload: 'x'.repeat(400) });
    expect(message.startsWith('the backend refused: {')).toBe(true);
    expect(message.length).toBeLessThan(240);
    expect(message.endsWith('…')).toBe(true);
  });

  it('reads the detail field of an unmapped error variant', () => {
    expect(
      describeApplyError(
        JSON.stringify({ error: 'unsupported_shape', detail: 'runner uses a named variable' })
      )
    ).toBe('unsupported shape: runner uses a named variable');
  });

  it('wraps a rejection with no prose in a sentence', () => {
    expect(describeApplyError('[1,2,3]')).toBe('the backend refused: [1,2,3]');
  });
});

describe('finding 8: one mutating call per path is in flight at a time', () => {
  it('serialises overlapping tasks on the same path and lets other paths run free', async () => {
    setInvoker(async () => null);
    const order: string[] = [];
    const slow = (name: string, ms: number) => async () => {
      order.push(`${name} start`);
      await new Promise((done) => setTimeout(done, ms));
      order.push(`${name} end`);
    };

    await Promise.all([
      queued('/a.cpp', slow('a1', 40)),
      queued('/a.cpp', slow('a2', 0)),
      queued('/b.cpp', slow('b1', 0))
    ]);

    expect(order.indexOf('a2 start')).toBeGreaterThan(order.indexOf('a1 end'));
    expect(order.indexOf('b1 start')).toBeLessThan(order.indexOf('a1 end'));
  });

  it('keeps draining after a task rejects', async () => {
    const seen: string[] = [];
    const boom = queued('/c.cpp', () => Promise.reject(new Error('nope')));
    await expect(boom).rejects.toThrow('nope');
    await queued('/c.cpp', async () => {
      seen.push('after');
    });
    expect(seen).toEqual(['after']);
  });
});

/* ================================================================ 1. view state */

describe('A. view state survives an ordinary edit', () => {
  it(
    'A1 keeps node positions when a param edit shifts the flowgraph call offset',
    async () => {
      const page = await boot({ shiftOffsets: true });
      const before = await positions(page);
      const beforeOffset = await page.evaluate(
        () => (window as unknown as FakeWindow).__fake.model().sites[0]?.call_offset
      );

      await page.evaluate(() => {
        document.querySelector('.svelte-flow')?.setAttribute('data-mount-probe', 'first');
      });

      await select(page, 'source1');
      const input = page.locator('input[data-field="source1.ctor.1"]');
      await input.fill('1234567.0f');
      await input.press('Enter');
      await page.waitForTimeout(900);

      const after = await positions(page);
      const afterOffset = await page.evaluate(
        () => (window as unknown as FakeWindow).__fake.model().sites[0]?.call_offset
      );
      const survived = await page.evaluate(
        () => document.querySelector('.svelte-flow')?.getAttribute('data-mount-probe') ?? 'gone'
      );

      expect(afterOffset).not.toBe(beforeOffset);
      expect(survived).toBe('first');
      expect(after).toEqual(before);
      await page.close();
    },
    CASE
  );

  it(
    'A2 keeps a dragged node where the user put it after an edit that shifts offsets',
    async () => {
      const page = await boot({ shiftOffsets: true });
      const before = (await positions(page)).adder;
      const node = page.locator('.svelte-flow__node[data-id="adder"]');
      const box = await node.boundingBox();
      if (!box) throw new Error('no node box');
      await page.mouse.move(box.x + box.width / 2, box.y + 6);
      await page.mouse.down();
      await page.mouse.move(box.x + box.width / 2 + 40, box.y + 226, { steps: 12 });
      await page.mouse.up();
      await page.waitForTimeout(300);
      const dragged = (await positions(page)).adder;
      expect(dragged, 'the drag itself must move the node').not.toBe(before);

      await select(page, 'source1');
      const input = page.locator('input[data-field="source1.ctor.1"]');
      await input.fill('7.125f');
      await input.press('Enter');
      await page.waitForTimeout(900);

      expect((await positions(page)).adder).toBe(dragged);
      await page.close();
    },
    CASE
  );

  it(
    'A3 gives a block that appears after a reload a real position',
    async () => {
      const page = await boot();

      await page.evaluate(() => {
        const fake = (window as unknown as FakeWindow).__fake;
        const model = JSON.parse(JSON.stringify(fake.model())) as FileModel;
        const site = model.sites[0];
        if (!site) throw new Error('no site');
        const clone = JSON.parse(JSON.stringify(site.blocks[0])) as (typeof site.blocks)[number];
        clone.var = 'newcomer';
        clone.display_name = 'Newcomer';
        site.blocks.push(clone);
        fake.setModel(model);
        fake.emitExternal();
      });

      await page.waitForSelector('[data-testid="reload-banner"]');
      await page.click('[data-testid="reload-banner"] button');
      await page.waitForSelector('.svelte-flow__node[data-id="newcomer"]');
      await page.waitForTimeout(500);

      const after = await positions(page);
      expect(after.newcomer).toBeDefined();
      expect(after.newcomer).not.toBe('translate(0px, 0px)');
      await page.close();
    },
    CASE
  );

  it(
    'A4 keeps the viewport (zoom and pan) across an edit',
    async () => {
      const page = await boot({ shiftOffsets: true });
      const viewport = () =>
        page.evaluate(
          () =>
            (document.querySelector('.svelte-flow__viewport') as HTMLElement | null)?.style
              .transform ?? 'none'
        );
      await select(page, 'source1');
      await page.click('[data-testid="zoom-in"]');
      await page.click('[data-testid="zoom-in"]');
      await page.waitForTimeout(500);
      const zoomed = await viewport();

      const input = page.locator('input[data-field="source1.ctor.1"]');
      await input.fill('1234.5f');
      await input.press('Enter');
      await page.waitForTimeout(1000);
      expect(await viewport()).toBe(zoomed);
      await page.close();
    },
    CASE
  );

  it(
    'A5 keeps the canvas selection highlight across an edit',
    async () => {
      const page = await boot({ shiftOffsets: true });
      await select(page, 'source1');
      expect(await page.locator('.svelte-flow__node.selected').count()).toBe(1);
      const input = page.locator('input[data-field="source1.ctor.1"]');
      await input.fill('4242.0f');
      await input.press('Enter');
      await page.waitForTimeout(1000);
      expect(await page.locator('.svelte-flow__node.selected').count()).toBe(1);
      expect(await page.textContent('.inspector .title')).toBeTruthy();
      await page.close();
    },
    CASE
  );
});

/* ================================================= 2. draft / error leakage */

describe('B. no state survives the block it belongs to', () => {
  it(
    'B1 does not show one site error on another site field of the same name',
    async () => {
      const page = await boot({ model: uhdModel() });
      await plan(page, 'apply_commands', [
        { failMode: 'json', failValue: { error: 'invalid_expression', element: 'ctor', text: 'X' } }
      ]);

      await select(page, 'spectrum');
      const field = page.locator('input[data-field="spectrum.ctor.0"]');
      await expect.poll(() => field.inputValue()).toBe('"USRP Spectrum"');
      await field.fill('"broken');
      await field.press('Enter');
      await expect.poll(() => page.locator('[data-error="spectrum.ctor.0"]').count()).toBe(1);

      await page.selectOption('select', { index: 1 });
      await page.waitForTimeout(600);
      expect(await page.locator('[data-error="spectrum.ctor.0"]').count()).toBe(0);
      await page.close();
    },
    CASE
  );

  it(
    'B1b keyboard-only site switch does not carry the error across',
    async () => {
      const page = await boot({ model: uhdModel() });
      await plan(page, 'apply_commands', [
        { failMode: 'json', failValue: { error: 'invalid_expression', element: 'ctor', text: 'X' } }
      ]);
      await select(page, 'spectrum');
      const field = page.locator('input[data-field="spectrum.ctor.0"]');
      await field.fill('"broken');
      await field.press('Enter');
      await expect.poll(() => page.locator('[data-error="spectrum.ctor.0"]').count()).toBe(1);

      await page.click('.sidebar select >> nth=0');
      await page.keyboard.press('ArrowDown');
      await page.keyboard.press('Enter');
      await page.waitForTimeout(700);

      const value = await page.locator('input[data-field="spectrum.ctor.0"]').inputValue();
      expect(value, 'the site really changed').toBe('"TX Spectrum"');
      expect(
        await page.locator('[data-error="spectrum.ctor.0"]').count(),
        'site 0 refusal must not be shown against site 1'
      ).toBe(0);
      await page.close();
    },
    CASE
  );

  it(
    'B2 does not carry an error from one file to the next file opened',
    async () => {
      const page = await boot();
      await plan(page, 'apply_commands', [
        { failMode: 'json', failValue: { error: 'invalid_expression', element: 'ctor', text: 'X' } }
      ]);
      await select(page, 'source1');
      const field = page.locator('input[data-field="source1.ctor.1"]');
      await field.fill('nope');
      await field.press('Enter');
      await expect.poll(() => page.locator('[data-error="source1.ctor.1"]').count()).toBe(1);

      await page.evaluate(
        (next) => (window as unknown as FakeWindow).__fake.setPath(next),
        OTHER_PATH
      );
      await page.click('button.primary');
      await page.waitForSelector(`.path[title="${OTHER_PATH}"]`);
      await select(page, 'source1');
      expect(await page.locator('[data-error="source1.ctor.1"]').count()).toBe(0);
      await page.close();
    },
    CASE
  );

  it(
    'B3 does not commit a half-typed value into a site the user switched away from',
    async () => {
      const page = await boot({ model: uhdModel() });
      await select(page, 'spectrum');
      await page.locator('input[data-field="spectrum.ctor.0"]').fill('"half typed');
      await page.selectOption('select', { index: 1 });
      await page.waitForTimeout(600);

      const log = await sent(page);
      const shown = await page.locator('input[data-field="spectrum.ctor.0"]').inputValue();
      const model = await page.evaluate(
        () =>
          (window as unknown as FakeWindow).__fake
            .model()
            .sites[1]?.blocks.find((block) => block.var === 'spectrum')?.ctor_args[0]?.text
      );
      expect(log).toHaveLength(0);
      expect(shown, 'site 1 must show site 1 text, not the draft typed on site 0').toBe(model);
      await page.close();
    },
    CASE
  );

  it(
    'B3b does not let a draft typed on one site commit against another site',
    async () => {
      const page = await boot({ model: uhdModel() });
      await select(page, 'spectrum');
      await page.locator('input[data-field="spectrum.ctor.0"]').fill('"LEAKED');
      await page.selectOption('select', { index: 1 });
      await page.waitForTimeout(500);
      await page.locator('input[data-field="spectrum.ctor.0"]').press('Enter');
      await page.waitForTimeout(600);
      expect(await sent(page)).toEqual([]);
      await page.close();
    },
    CASE
  );

  it(
    'B3c does not keep a draft alive across a block selection change',
    async () => {
      const page = await boot({ model: uhdModel() });
      await select(page, 'spectrum');
      await page.locator('input[data-field="spectrum.ctor.0"]').fill('"STILL HERE');
      await page.selectOption('select', { index: 1 });
      await page.waitForTimeout(400);
      await page.selectOption('select', { index: 0 });
      await page.waitForTimeout(400);
      expect(await page.locator('input[data-field="spectrum.ctor.0"]').inputValue()).toBe(
        '"USRP Spectrum"'
      );
      await page.close();
    },
    CASE
  );

  it(
    'B4 keeps a config read-only reason visible whatever block is selected',
    async () => {
      const page = await boot();
      const hint = () =>
        page.evaluate(() => {
          const input = document.querySelector('input[data-field="config.adaptive_sleep"]');
          return input?.closest('label')?.querySelector('.hint')?.textContent ?? null;
        });
      const withNoBlock = await hint();
      expect(withNoBlock).toContain('optional emplace declaration');

      await page.evaluate(() => {
        const fake = (window as unknown as FakeWindow).__fake;
        const model = JSON.parse(JSON.stringify(fake.model())) as FileModel;
        const plot = model.sites[0]?.blocks.find((block) => block.var === 'plot');
        if (!plot) throw new Error('no plot');
        plot.editable = false;
        plot.read_only_reason = 'optional_emplace_declaration';
        fake.setModel(model);
        fake.emitExternal();
      });
      await page.waitForSelector('[data-testid="reload-banner"]');
      await page.click('[data-testid="reload-banner"] button');
      await page.waitForTimeout(400);
      await select(page, 'plot');
      expect(await hint()).toBe(withNoBlock);
      await page.close();
    },
    CASE
  );
});

/* ============================================== 3. one command per commit */

describe('C. exactly one command per intentional commit', () => {
  it(
    'C1 Enter then blur sends one',
    async () => {
      const page = await boot();
      await select(page, 'source1');
      const input = page.locator('input[data-field="source1.ctor.1"]');
      await input.fill('2.0f');
      await input.press('Enter');
      await input.blur();
      await page.waitForTimeout(400);
      expect(await applyCount(page)).toBe(1);
      await page.close();
    },
    CASE
  );

  it(
    'C2 two fast Enters send one',
    async () => {
      const page = await boot();
      await select(page, 'source1');
      const input = page.locator('input[data-field="source1.ctor.1"]');
      await input.fill('3.0f');
      await input.press('Enter');
      await input.press('Enter');
      await page.waitForTimeout(400);
      expect(await applyCount(page)).toBe(1);
      await page.close();
    },
    CASE
  );

  it(
    'C3 keeps focus in the field after Enter, and through a slow commit',
    async () => {
      const page = await boot();
      await plan(page, 'apply_commands', [{ delay: 500 }]);
      await select(page, 'source1');
      const input = page.locator('input[data-field="source1.ctor.1"]');
      await input.fill('4.0f');
      await input.press('Enter');
      await page.waitForTimeout(200);
      const midflight = await page.evaluate(
        () => document.activeElement?.getAttribute('data-field') ?? document.activeElement?.tagName
      );
      await page.waitForTimeout(800);
      const settled = await page.evaluate(
        () => document.activeElement?.getAttribute('data-field') ?? document.activeElement?.tagName
      );
      expect(midflight).toBe('source1.ctor.1');
      expect(settled).toBe('source1.ctor.1');
      await page.close();
    },
    CASE
  );

  it(
    'C4 does not commit the intermediate text when an IME candidate is confirmed',
    async () => {
      const page = await boot();
      await select(page, 'source1');
      await page.evaluate(() => {
        const input = document.querySelector(
          'input[data-field="source1.ctor.1"]'
        ) as HTMLInputElement;
        input.focus();
        input.dispatchEvent(new CompositionEvent('compositionstart', { bubbles: true }));
        input.value = 'にほ';
        input.dispatchEvent(new InputEvent('input', { bubbles: true, isComposing: true }));
        input.dispatchEvent(
          new KeyboardEvent('keydown', { key: 'Enter', bubbles: true, isComposing: true })
        );
      });
      await page.waitForTimeout(400);
      expect(await applyCount(page)).toBe(0);
      await page.close();
    },
    CASE
  );

  it(
    'C5 does not drop keystrokes typed into the next field while a commit is in flight',
    async () => {
      const page = await boot();
      await plan(page, 'apply_commands', [{ delay: 700 }]);
      await select(page, 'source1');
      const first = page.locator('input[data-field="source1.ctor.1"]');
      const second = page.locator('input[data-field="source1.ctor.2"]');
      await first.fill('5.0f');
      await first.press('Tab');
      await page.keyboard.type('9');
      await page.waitForTimeout(1200);
      expect(await second.inputValue()).toContain('9');
      await page.close();
    },
    CASE
  );

  it(
    'C6 commits the pending draft and then performs the Undo clicked over it',
    async () => {
      const page = await boot();
      await plan(page, 'apply_commands', [{ delay: 0 }, { delay: 600 }]);
      await select(page, 'source1');
      const input = page.locator('input[data-field="source1.ctor.1"]');
      await input.fill('6.0f');
      await input.press('Enter');
      await expect.poll(() => page.locator('[data-testid="undo"]').isDisabled()).toBe(false);

      await input.fill('7.0f');
      await page.click('[data-testid="undo"]');
      await page.waitForTimeout(1800);

      const observed = (await names(page)).filter(
        (name) => !['open_document', 'palette', 'find_target', 'assistant_status'].includes(name)
      );
      expect(observed, 'the draft commits first, the undo follows, neither is dropped').toEqual([
        'apply_commands',
        'apply_commands',
        'undo'
      ]);
      await page.close();
    },
    CASE
  );
});

/* ======================================================== 4. async ordering */

describe('D. a newer model is never replaced by an older one', () => {
  it(
    'D1 does not resurrect a stale model when a slow reply lands after a fast one',
    async () => {
      const page = await boot();
      await plan(page, 'apply_commands', [{ delay: 900 }, { delay: 0 }]);
      await select(page, 'source1');

      await page.evaluate(() => {
        const a = document.querySelector('input[data-field="source1.ctor.1"]') as HTMLInputElement;
        const b = document.querySelector('input[data-field="source1.ctor.2"]') as HTMLInputElement;
        a.value = 'AAA';
        a.dispatchEvent(new Event('input', { bubbles: true }));
        b.value = 'BBB';
        b.dispatchEvent(new Event('input', { bubbles: true }));
        a.dispatchEvent(new FocusEvent('blur', { bubbles: false }));
        b.dispatchEvent(new FocusEvent('blur', { bubbles: false }));
      });

      await page.waitForTimeout(2000);
      const first = await page.locator('input[data-field="source1.ctor.1"]').inputValue();
      const second = await page.locator('input[data-field="source1.ctor.2"]').inputValue();
      const backend = await page.evaluate(() => {
        const site = (window as unknown as FakeWindow).__fake.model().sites[0];
        const block = site?.blocks.find((candidate) => candidate.var === 'source1');
        return [block?.ctor_args[1]?.text, block?.ctor_args[2]?.text];
      });
      expect([first, second]).toEqual(backend);
      await page.close();
    },
    CASE
  );

  it(
    'D2 gates a second write while one is in flight',
    async () => {
      const page = await boot();
      await plan(page, 'apply_commands', [{ delay: 900 }, { delay: 0 }]);
      await select(page, 'source1');
      const overlap = await page.evaluate(async () => {
        const a = document.querySelector('input[data-field="source1.ctor.1"]') as HTMLInputElement;
        const b = document.querySelector('input[data-field="source1.ctor.2"]') as HTMLInputElement;
        a.value = 'AAA';
        a.dispatchEvent(new Event('input', { bubbles: true }));
        b.value = 'BBB';
        b.dispatchEvent(new Event('input', { bubbles: true }));
        a.dispatchEvent(new FocusEvent('blur'));
        b.dispatchEvent(new FocusEvent('blur'));
        await new Promise((done) => setTimeout(done, 1600));
        const seen = (window as unknown as FakeWindow).__fake.calls.filter(
          (call) => call.name === 'apply_commands'
        );
        if (seen.length < 2) return false;
        const [one, two] = seen;
        return !!one && !!two && two.start < one.end;
      });
      expect(overlap).toBe(false);
      await page.close();
    },
    CASE
  );

  it(
    'D3 refuses to install a document older than the one on screen',
    async () => {
      const page = await boot();
      await select(page, 'source1');
      const input = page.locator('input[data-field="source1.ctor.1"]');
      await input.fill('77.0f');
      await input.press('Enter');
      await expect.poll(() => input.inputValue()).toBe('77.0f');
      const revision = await page.textContent('.sidebar dl dd:nth-of-type(2)');

      await page.evaluate(() => {
        const fake = (window as unknown as FakeWindow).__fake;
        const stale = JSON.parse(JSON.stringify(fake.model())) as FileModel;
        const arg = stale.sites[0]?.blocks.find((block) => block.var === 'source1')?.ctor_args[1];
        if (arg) arg.text = 'STALE';
        const scope = window as unknown as { __TAURI_INTERNALS__: { invoke: unknown } };
        const real = scope.__TAURI_INTERNALS__.invoke as (
          name: string,
          args: Record<string, unknown>
        ) => Promise<unknown>;
        scope.__TAURI_INTERNALS__.invoke = async (
          name: string,
          args: Record<string, unknown>
        ): Promise<unknown> => {
          if (name !== 'undo') return real(name, args);
          return { path: '/tmp/fake/hello_world.cpp', revision: 1, model: stale, canUndo: false, canRedo: true, externalChange: false };
        };
      });

      await page.click('[data-testid="undo"]');
      await page.waitForTimeout(600);
      expect(await input.inputValue()).toBe('77.0f');
      expect(await page.textContent('.sidebar dl dd:nth-of-type(2)')).toBe(revision);
      await page.close();
    },
    CASE
  );
});

/* ============================================================ 5. error paths */

type ErrorCase = { name: string; plan: Plan; expect?: string };

const errorCases: ErrorCase[] = [
  {
    name: 'not_editable',
    plan: {
      failMode: 'json',
      failValue: {
        error: 'not_editable',
        element: 'block source1',
        reason: 'optional_emplace_declaration'
      }
    },
    expect: 'block source1 is read-only: optional emplace declaration'
  },
  {
    name: 'invalid_expression',
    plan: {
      failMode: 'json',
      failValue: { error: 'invalid_expression', element: 'x', text: '1.0f;;' }
    },
    expect: '"1.0f;;" is not a valid expression'
  },
  {
    name: 'references_outside_graph',
    plan: {
      failMode: 'json',
      failValue: { error: 'references_outside_graph', block: 'plot', spans: [{ start: 1, end: 2 }] }
    },
    expect: 'plot is still used in 1 place outside the flowgraph — remove those references first'
  },
  {
    name: 'revision_mismatch',
    plan: {
      failMode: 'json',
      failValue: { error: 'revision_mismatch', base_revision: 1, current_revision: 4 }
    },
    expect: 'the graph changed under this gesture — try again'
  },
  {
    name: 'unsupported_shape',
    plan: {
      failMode: 'json',
      failValue: { error: 'unsupported_shape', detail: 'runner uses a named variable' }
    },
    expect: 'unsupported shape: runner uses a named variable'
  },
  {
    name: 'disk drift plain string',
    plan: {
      failMode: 'string',
      failValue:
        '/tmp/fake/hello_world.cpp changed on disk since the last write; reload before editing'
    }
  },
  {
    name: 'rust panic text',
    plan: { failMode: 'string', failValue: 'called `Option::unwrap()` on a `None` value' }
  },
  { name: 'empty string', plan: { failMode: 'empty' } },
  { name: 'null', plan: { failMode: 'null' } },
  { name: 'undefined', plan: { failMode: 'undefined' } },
  { name: 'thrown plain object', plan: { failMode: 'object', failValue: { code: 7, detail: 'x' } } },
  { name: 'json array', plan: { failMode: 'string', failValue: '[1,2,3]' } }
];

describe('E. every refusal reverts visibly with a readable reason', () => {
  for (const entry of errorCases) {
    it(
      `E-${entry.name}`,
      async () => {
        const page = await boot();
        await plan(page, 'apply_commands', [entry.plan]);
        await select(page, 'source1');
        const input = page.locator('input[data-field="source1.ctor.1"]');
        const before = await input.inputValue();
        await input.fill('9.75f');
        await input.press('Enter');
        await page.waitForTimeout(600);

        const fieldError = await page.evaluate(
          () => document.querySelector('[data-error="source1.ctor.1"]')?.textContent ?? null
        );
        const value = await input.inputValue();

        expect(value).toBe(before);
        expect(fieldError, 'a refusal must name a reason next to the field').toBeTruthy();
        expect(fieldError ?? '').not.toContain('{');
        expect(fieldError ?? '').not.toContain('[object');
        expect(
          ['null', 'undefined', 'NaN'],
          'a refusal must not print a js value name'
        ).not.toContain(fieldError);
        expect(fieldError ?? '', 'a refusal must read as a sentence').toMatch(/[a-z]{3}\s|—/);
        if (entry.expect) expect(fieldError).toBe(entry.expect);
        await page.close();
      },
      CASE
    );
  }

  it(
    'E-clears the error once the next edit succeeds',
    async () => {
      const page = await boot();
      await plan(page, 'apply_commands', [
        { failMode: 'json', failValue: { error: 'invalid_expression', element: 'x', text: 'q' } }
      ]);
      await select(page, 'source1');
      const bad = page.locator('input[data-field="source1.ctor.1"]');
      await bad.fill('q');
      await bad.press('Enter');
      await expect.poll(() => page.locator('[data-error="source1.ctor.1"]').count()).toBe(1);

      const other = page.locator('input[data-field="source1.ctor.2"]');
      await other.fill('11.0f');
      await other.press('Enter');
      await page.waitForTimeout(600);
      expect(await page.locator('[data-error="source1.ctor.1"]').count()).toBe(0);
      await page.close();
    },
    CASE
  );

  it(
    'E-does not visibly revert the field while a successful commit is still in flight',
    async () => {
      const page = await boot();
      await plan(page, 'apply_commands', [{ delay: 900 }]);
      await select(page, 'source1');
      const input = page.locator('input[data-field="source1.ctor.1"]');
      await input.fill('55.0f');
      await input.press('Enter');
      await page.waitForTimeout(300);
      const midflight = await input.inputValue();
      await page.waitForTimeout(1000);
      const settled = await input.inputValue();
      expect(midflight, 'the draft must stay on screen until the commit resolves').toBe('55.0f');
      expect(settled).toBe('55.0f');
      await page.close();
    },
    CASE
  );
});

/* =========================================================== 6. reload banner */

describe('F. the reload banner tells the truth', () => {
  it(
    'F1 ignores an event for a different path',
    async () => {
      const page = await boot();
      await page.evaluate(() =>
        (window as unknown as FakeWindow).__fake.emitExternal('/tmp/fake/somewhere-else.cpp')
      );
      await page.waitForTimeout(400);
      expect(await page.locator('[data-testid="reload-banner"]').count()).toBe(0);
      await page.close();
    },
    CASE
  );

  it(
    'F2 keeps the banner up when the reload itself fails',
    async () => {
      const page = await boot();
      await plan(page, 'reload_document', [
        { failMode: 'string', failValue: 'cannot read /tmp/fake/hello_world.cpp: no such file' }
      ]);
      await page.evaluate(() => (window as unknown as FakeWindow).__fake.emitExternal());
      await page.waitForSelector('[data-testid="reload-banner"]');
      await page.click('[data-testid="reload-banner"] button');
      await page.waitForTimeout(500);
      expect(await page.locator('[data-testid="reload-banner"]').count()).toBe(1);
      expect(await page.textContent('[data-testid="status"]')).toContain('no such file');
      await page.close();
    },
    CASE
  );

  it(
    'F3 keeps the banner up and says so when the reload fails with an empty message',
    async () => {
      const page = await boot();
      await plan(page, 'reload_document', [{ failMode: 'empty' }]);
      await page.evaluate(() => (window as unknown as FakeWindow).__fake.emitExternal());
      await page.waitForSelector('[data-testid="reload-banner"]');
      await page.click('[data-testid="reload-banner"] button');
      await page.waitForTimeout(500);
      expect(await page.locator('[data-testid="reload-banner"]').count()).toBe(1);
      expect(await page.textContent('[data-testid="status"]')).toBe(
        'the edit was refused but no reason was given'
      );
      await page.close();
    },
    CASE
  );

  it(
    'F4 keeps the banner up when an edit is refused because the file drifted',
    async () => {
      const page = await boot();
      await plan(page, 'apply_commands', [
        {
          failMode: 'string',
          failValue:
            '/tmp/fake/hello_world.cpp changed on disk since the last write; reload before editing'
        }
      ]);
      await select(page, 'source1');
      const input = page.locator('input[data-field="source1.ctor.1"]');
      await input.fill('8.0f');
      await input.press('Enter');
      await page.waitForTimeout(600);
      expect(await page.locator('[data-testid="reload-banner"]').count()).toBe(1);
      await page.close();
    },
    CASE
  );

  it(
    'F5 leaves a usable view when the reloaded file lost the selected site',
    async () => {
      const page = await boot({ model: uhdModel() });
      await page.selectOption('select', { index: 3 });
      await page.waitForTimeout(600);
      await select(page, 'power_plot');

      await page.evaluate(() => {
        const fake = (window as unknown as FakeWindow).__fake;
        const model = JSON.parse(JSON.stringify(fake.model())) as FileModel;
        model.sites = model.sites.slice(0, 1);
        fake.setModel(model);
        fake.emitExternal();
      });
      await page.waitForSelector('[data-testid="reload-banner"]');
      await page.click('[data-testid="reload-banner"] button');
      await page.waitForTimeout(900);

      expect(await page.locator('.svelte-flow__node').count()).toBeGreaterThan(0);
      expect(
        await page.locator('.inspector [data-testid="block-reason"]').count(),
        'the vanished selection must be dropped'
      ).toBe(0);
      expect(await page.locator('.svelte-flow__node[data-id="usrp_source"]').count()).toBe(1);
      await page.close();
    },
    CASE
  );

  it(
    'F6 does not flap the banner when a reload races an in-flight commit',
    async () => {
      const page = await boot();
      await plan(page, 'apply_commands', [
        {
          delay: 700,
          failMode: 'string',
          failValue:
            '/tmp/fake/hello_world.cpp changed on disk since the last write; reload before editing'
        }
      ]);
      await select(page, 'source1');
      await page.evaluate(() => (window as unknown as FakeWindow).__fake.emitExternal());
      await page.waitForSelector('[data-testid="reload-banner"]');

      await page.evaluate(() => {
        const input = document.querySelector(
          'input[data-field="source1.ctor.1"]'
        ) as HTMLInputElement;
        input.value = 'RACE';
        input.dispatchEvent(new Event('input', { bubbles: true }));
        input.dispatchEvent(new FocusEvent('blur'));
      });
      await page.click('[data-testid="reload-banner"] button');
      await page.waitForTimeout(1800);
      expect(await page.locator('[data-testid="reload-banner"]').count()).toBe(0);
      await page.close();
    },
    CASE
  );

  it(
    'F6b does not re-raise the banner after a reload that succeeded',
    async () => {
      const page = await boot();
      await plan(page, 'apply_commands', [
        {
          delay: 900,
          failMode: 'string',
          failValue:
            '/tmp/fake/hello_world.cpp changed on disk since the last write; reload before editing'
        }
      ]);
      await select(page, 'source1');
      await page.evaluate(() => (window as unknown as FakeWindow).__fake.emitExternal());
      await page.waitForSelector('[data-testid="reload-banner"]');

      await page.evaluate(() => {
        const input = document.querySelector(
          'input[data-field="source1.ctor.1"]'
        ) as HTMLInputElement;
        input.value = 'RACE';
        input.dispatchEvent(new Event('input', { bubbles: true }));
        input.dispatchEvent(new FocusEvent('blur'));
      });
      await page.click('[data-testid="reload-banner"] button', { force: true });
      await page.waitForTimeout(400);
      const rightAfterReload = await page.locator('[data-testid="reload-banner"]').count();
      await page.waitForTimeout(1400);
      const later = await page.locator('[data-testid="reload-banner"]').count();
      expect([rightAfterReload, later]).toEqual([0, 0]);
      await page.close();
    },
    CASE
  );

  it(
    'F7 clears an in-progress draft when the document is reloaded under it',
    async () => {
      const page = await boot();
      await plan(page, 'apply_commands', [
        {
          failMode: 'string',
          failValue:
            '/tmp/fake/hello_world.cpp changed on disk since the last write; reload before editing'
        }
      ]);
      await select(page, 'source1');
      await page.locator('input[data-field="source1.ctor.1"]').fill('DRAFT');
      await page.evaluate(() => {
        const fake = (window as unknown as FakeWindow).__fake;
        const model = JSON.parse(JSON.stringify(fake.model())) as FileModel;
        const arg = model.sites[0]?.blocks.find((candidate) => candidate.var === 'source1')
          ?.ctor_args[1];
        if (arg) arg.text = '999.0f';
        fake.setModel(model);
        fake.emitExternal();
      });
      await page.waitForSelector('[data-testid="reload-banner"]');
      await page.click('[data-testid="reload-banner"] button', { force: true });
      await page.waitForTimeout(800);

      expect(await page.locator('input[data-field="source1.ctor.1"]').inputValue()).toBe('999.0f');
      expect(await sent(page), 'taking disk must not write the draft back').toEqual([]);
      expect(
        await page.evaluate(
          () => document.querySelector('[data-error="source1.ctor.1"]')?.textContent ?? null
        ),
        'a completed reload must not leave a refusal on screen'
      ).toBeNull();
      await page.close();
    },
    CASE
  );
});

/* ============================ 6b. how a draft can outlive its context at all */

describe('B5. a draft that outlives the field it was typed in', () => {
  it(
    'B5 commits or discards a draft when the user clicks another block',
    async () => {
      const page = await boot({ model: uhdModel() });
      await select(page, 'spectrum');
      const field = page.locator('input[data-field="spectrum.ctor.0"]');
      await field.fill('"LEAKED');

      await page.click('.svelte-flow__node[data-id="fanout"]');
      await page.waitForTimeout(500);
      const afterLeaving = await sent(page);

      await page.click('.svelte-flow__node[data-id="spectrum"]');
      await page.waitForTimeout(400);
      const backOnSpectrum = await field.inputValue();

      expect(
        afterLeaving.length === 1 || backOnSpectrum === '"USRP Spectrum"',
        'the draft must either commit or disappear, not linger uncommitted'
      ).toBe(true);
      await page.close();
    },
    CASE
  );

  it(
    'B6 never commits a draft typed on one site into a different site',
    async () => {
      const page = await boot({ model: uhdModel() });

      await select(page, 'spectrum');
      await page.locator('input[data-field="spectrum.ctor.0"]').fill('"LEAKED');

      await page.click('.svelte-flow__node[data-id="fanout"]');
      await page.waitForTimeout(300);
      expect(await sent(page)).toEqual([
        { command: 'set_param', site: 0, block: 'spectrum', ctor_arg_index: 0, new_text: '"LEAKED' }
      ]);

      await page.selectOption('select', { index: 1 });
      await page.waitForTimeout(600);
      await page.click('.svelte-flow__node[data-id="spectrum"]');
      await page.waitForTimeout(400);
      const shown = await page.locator('input[data-field="spectrum.ctor.0"]').inputValue();

      await page.locator('input[data-field="spectrum.ctor.0"]').press('Tab');
      await page.waitForTimeout(600);
      const log = await sent(page);
      const site1 = await page.evaluate(
        () =>
          (window as unknown as FakeWindow).__fake
            .model()
            .sites[1]?.blocks.find((block) => block.var === 'spectrum')?.ctor_args[0]?.text
      );

      expect(shown, 'site 1 must not display site 0 text').toBe('"TX Spectrum"');
      expect(site1, 'site 1 must keep its own value').toBe('"TX Spectrum"');
      expect(
        log,
        'no extra command may be built from a draft typed on another site'
      ).toHaveLength(1);
      await page.close();
    },
    CASE
  );
});

/* ========================================================= 7. fixture inertness */

describe('G. fixture mode never reaches for a backend', () => {
  it(
    'G1 makes no invoke and raises no error while every control is exercised',
    async () => {
      const page = await browser.newPage({ viewport: VIEWPORT });
      const problems: string[] = [];
      page.on('pageerror', (error) => problems.push(`pageerror: ${error.message}`));
      page.on('console', (message) => {
        if (message.type() === 'error') problems.push(`console: ${message.text()}`);
      });
      await page.goto(`${origin}/?fixture=uhd_device`, { waitUntil: 'load' });
      await page.waitForSelector('.svelte-flow__node');

      await page.evaluate(() => {
        const spy = { calls: [] as string[] };
        (window as unknown as FakeWindow).__spy = spy;
        (window as unknown as FakeWindow).__TAURI_INTERNALS__ = {
          invoke: (command: string) => {
            spy.calls.push(command);
            return Promise.resolve(null);
          },
          transformCallback: () => 1,
          unregisterCallback: () => undefined
        };
        (window as unknown as FakeWindow).__TAURI_EVENT_PLUGIN_INTERNALS__ = {
          unregisterListener: () => undefined
        };
      });

      await page.click('.svelte-flow__node[data-id="fanout"]');
      await page.waitForSelector('.inspector input');
      await page.click('button.primary');
      await page.locator('[data-testid="undo"]').click({ force: true });
      await page.locator('[data-testid="redo"]').click({ force: true });
      await page.selectOption('.sidebar select >> nth=0', { index: 1 });
      await page.waitForTimeout(300);

      await page.evaluate(() => {
        const input = document.querySelector('.inspector input') as HTMLInputElement | null;
        if (!input) return;
        input.disabled = false;
        input.value = 'tampered';
        input.dispatchEvent(new Event('input', { bubbles: true }));
        input.dispatchEvent(new FocusEvent('blur'));
      });
      await page.evaluate(() => {
        window.dispatchEvent(new CustomEvent('document-changed-externally'));
      });
      await page.waitForTimeout(600);

      const spied = await page.evaluate(
        () => (window as unknown as FakeWindow).__spy?.calls ?? ['spy missing']
      );
      expect(spied).toEqual([]);
      expect(problems).toEqual([]);
      expect(await page.locator('[data-testid="reload-banner"]').count()).toBe(0);
      await page.close();
    },
    CASE
  );

  const DESKTOP_NOTE = 'example mode — read-only viewer — use Open file… to edit the real file';

  async function bootExample(): Promise<Page> {
    const page = await browser.newPage({ viewport: VIEWPORT });
    await page.addInitScript(openInspector);
    await page.addInitScript(installFake, {
      path: FAKE_PATH,
      model: helloModel(),
      shiftOffsets: false
    });
    await page.goto(origin, { waitUntil: 'load' });
    await page.waitForSelector('.svelte-flow__node');
    await page.selectOption('[data-testid="example-select"]', 'adsb_receiver');
    await page.waitForSelector('.path[title="desktop_examples/adsb_receiver.cpp"]');
    await page.waitForSelector('.svelte-flow__node[data-id="iq2mag"]');
    return page;
  }

  it(
    'G2 keeps an example read-only inside the desktop shell and says how to edit the real file',
    async () => {
      const page = await bootExample();
      const problems: string[] = [];
      page.on('pageerror', (error) => problems.push(`pageerror: ${error.message}`));

      await select(page, 'iq2mag');
      const field = page.locator('.inspector input').first();
      expect(await field.isDisabled()).toBe(true);
      expect(await page.getAttribute('[data-testid="demo-chip"]', 'title')).toBe(DESKTOP_NOTE);
      expect(await page.textContent('[data-testid="demo-chip"]')).toBe('demo');
      expect(await page.textContent('[data-testid="examples-head"]')).toBe('Examples (viewer)');

      await page.evaluate(() => {
        const input = document.querySelector('.inspector input') as HTMLInputElement | null;
        if (!input) throw new Error('no field');
        input.disabled = false;
        input.value = 'tampered';
        input.dispatchEvent(new Event('input', { bubbles: true }));
        input.dispatchEvent(new FocusEvent('blur'));
      });
      await page.locator('[data-testid="undo"]').click({ force: true });
      await page.locator('[data-testid="redo"]').click({ force: true });
      await page.waitForTimeout(400);

      expect((await names(page)).filter((name) => name !== 'assistant_status')).toEqual([]);
      expect(await sent(page)).toEqual([]);
      expect(await page.textContent('[data-testid="status"]')).toBe(DESKTOP_NOTE);
      expect(problems).toEqual([]);
      await page.close();
    },
    CASE
  );

  it(
    'G3 hands editing back the moment a real file is opened',
    async () => {
      const page = await bootExample();
      await page.click('button.primary');
      await page.waitForSelector(`.path[title="${FAKE_PATH}"]`);
      await page.waitForSelector('.svelte-flow__node[data-id="source1"]');

      expect(await page.locator('[data-testid="demo-chip"]').count()).toBe(0);
      expect(await page.locator('[data-testid="examples-head"]').count()).toBe(0);

      await select(page, 'source1');
      const field = page.locator('input[data-field="source1.ctor.1"]');
      expect(await field.isDisabled()).toBe(false);
      await field.fill('9.5f');
      await field.blur();

      await expect.poll(() => sent(page)).toEqual([
        { command: 'set_param', site: 0, block: 'source1', ctor_arg_index: 1, new_text: '9.5f' }
      ]);
      expect(await names(page)).toContain('open_document');
      await page.close();
    },
    CASE
  );
});

/* ============================================================== 8. legibility */

type Reading = {
  what: string;
  selector: string;
  text: string;
  fg: string;
  bg: string;
  ratio: number;
  size: string;
};

function measureTargets(targets: [string, string][]) {
  function parse(value: string): number[] {
    const nums = value.match(/[\d.]+/g)?.map(Number) ?? [];
    if (value.startsWith('color(')) {
      const [r = 0, g = 0, b = 0, a = 1] = nums;
      return [r * 255, g * 255, b * 255, a];
    }
    if (value === 'transparent' || value === 'rgba(0, 0, 0, 0)') return [0, 0, 0, 0];
    const [r = 0, g = 0, b = 0, a = 1] = nums;
    return [r, g, b, a];
  }
  function over(top: number[], under: number[]): number[] {
    const a = top[3] ?? 1;
    return [
      (top[0] ?? 0) * a + (under[0] ?? 0) * (1 - a),
      (top[1] ?? 0) * a + (under[1] ?? 0) * (1 - a),
      (top[2] ?? 0) * a + (under[2] ?? 0) * (1 - a),
      1
    ];
  }
  function lum(color: number[]): number {
    const [r = 0, g = 0, b = 0] = color.slice(0, 3).map((channel) => {
      const c = channel / 255;
      return c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
    });
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
  }
  function ratio(a: number[], b: number[]): number {
    const values = [lum(a), lum(b)].sort((x, y) => y - x);
    return ((values[0] ?? 0) + 0.05) / ((values[1] ?? 0) + 0.05);
  }
  function hex(color: number[]): string {
    return (
      '#' +
      color
        .slice(0, 3)
        .map((channel) => Math.round(channel).toString(16).padStart(2, '0'))
        .join('')
    );
  }
  function measure(what: string, selector: string) {
    const element = document.querySelector(selector) as HTMLElement | null;
    if (!element) return null;
    const chain: HTMLElement[] = [];
    for (let node: HTMLElement | null = element; node; node = node.parentElement) chain.push(node);
    chain.reverse();
    let under: number[] = [0, 0, 0, 1];
    let opacity = 1;
    for (const node of chain) {
      const style = getComputedStyle(node);
      if (node !== element) under = over(parse(style.backgroundColor), under);
      opacity *= Number(style.opacity);
    }
    const style = getComputedStyle(element);
    const layerBg = over(parse(style.backgroundColor), under);
    const layerText = over(parse(style.color), layerBg);
    const bg = over([layerBg[0] ?? 0, layerBg[1] ?? 0, layerBg[2] ?? 0, opacity], under);
    const fg = over([layerText[0] ?? 0, layerText[1] ?? 0, layerText[2] ?? 0, opacity], under);
    return {
      what,
      selector,
      text: (element.textContent || (element as HTMLInputElement).value || '').trim().slice(0, 44),
      fg: hex(fg),
      bg: hex(bg),
      ratio: Math.round(ratio(fg, bg) * 100) / 100,
      size: style.fontSize
    };
  }
  return targets
    .map(([what, selector]) => measure(what, selector))
    .filter((row): row is NonNullable<typeof row> => row !== null);
}

function report(label: string, rows: Reading[]): Reading[] {
  // eslint-disable-next-line no-console
  console.log(`\n${label} (WCAG AA body text needs 4.5:1, large/bold 3:1)`);
  for (const row of rows) {
    // eslint-disable-next-line no-console
    console.log(
      `  ${row.ratio.toFixed(2).padStart(6)}:1  ${row.size.padStart(7)}  ${row.fg} on ${row.bg}  ${row.what}  ${JSON.stringify(row.text)}`
    );
  }
  return rows.filter((row) => row.ratio < 4.5);
}

const MIN_TEXT_PX = 11;
const DECORATIVE = /badge|^node /;

function tooSmall(rows: Reading[]): string[] {
  return rows
    .filter((row) => !DECORATIVE.test(row.what))
    .filter((row) => Number.parseFloat(row.size) < MIN_TEXT_PX)
    .map((row) => `${row.what} ${row.size}`);
}

describe('H. the inspector is readable', () => {
  it(
    'H1 measures contrast on every text role the editor shows',
    async () => {
      const page = await boot();
      await plan(page, 'apply_commands', [
        { failMode: 'json', failValue: { error: 'invalid_expression', element: 'x', text: 'bogus' } }
      ]);
      await select(page, 'source1');
      const input = page.locator('input[data-field="source1.ctor.1"]');
      await input.fill('bogus');
      await input.press('Enter');
      await expect.poll(() => page.locator('[data-error="source1.ctor.1"]').count()).toBe(1);

      const rows = (await page.evaluate(measureTargets, [
        ['field error message', '.inspector .err'],
        ['sidebar status message', '[data-testid="status"]'],
        ['inline field hint', '.inspector .hint'],
        ['disabled field text (read-only value)', 'input[data-field="config.adaptive_sleep"]'],
        ['enabled field text', 'input[data-field="source1.ctor.1"]'],
        ['field label', '.inspector .label'],
        ['section heading', '.inspector h2'],
        ['block type signature', '.inspector .type'],
        ['port name', '.inspector .port'],
        ['port direction', '.inspector .dir'],
        ['read-only note reason (sidebar)', '.sidebar .notes .reason'],
        ['file path', '.sidebar .path'],
        ['sidebar dt', '.sidebar dt'],
        ['tagline', '[data-testid="top-bar"] .tagline'],
        ['attribution', '.sidebar .attribution'],
        ['node type line', '.svelte-flow__node .type'],
        ['node var line', '.svelte-flow__node .var'],
        ['node port label', '.svelte-flow__node .port-label']
      ] as [string, string][])) as Reading[];

      const failing = report('H1 inspector contrast, editable block with an error', rows);
      expect(failing.map((row) => row.what)).toEqual([]);
      expect(tooSmall(rows)).toEqual([]);
      await page.close();
    },
    CASE
  );

  it(
    'H2 measures contrast on the read-only surfaces',
    async () => {
      const page = await boot();
      await page.evaluate(() => {
        const fake = (window as unknown as FakeWindow).__fake;
        const model = JSON.parse(JSON.stringify(fake.model())) as FileModel;
        const plot = model.sites[0]?.blocks.find((block) => block.var === 'plot');
        if (!plot) throw new Error('no plot');
        plot.editable = false;
        plot.read_only_reason = 'optional_emplace_declaration';
        fake.setModel(model);
        fake.emitExternal();
      });
      await page.waitForSelector('[data-testid="reload-banner"]');
      const bannerRows = (await page.evaluate(measureTargets, [
        ['reload banner text', '[data-testid="reload-banner"] span'],
        ['reload banner button', '[data-testid="reload-banner"] button']
      ] as [string, string][])) as Reading[];
      await page.click('[data-testid="reload-banner"] button', { force: true });
      await page.waitForTimeout(500);
      await select(page, 'plot');
      await page.waitForSelector('[data-testid="block-reason"]');

      const rows = (await page.evaluate(measureTargets, [
        ['read-only reason banner', '[data-testid="block-reason"]'],
        ['read-only field label', '.inspector .field.ro .label'],
        ['read-only field text', 'input[data-field="plot.ctor.0"]'],
        ['read-only node badge', '.svelte-flow__node[data-id="plot"] .badge'],
        ['read-only note reason (sidebar)', '.sidebar .notes .reason'],
        ['disabled Undo button', '[data-testid="undo"]']
      ] as [string, string][])) as Reading[];

      const all = [...bannerRows, ...rows];
      const failing = report('H2 read-only surfaces', all);
      expect(failing.map((row) => row.what)).toEqual([]);
      expect(tooSmall(all)).toEqual([]);
      await page.close();
    },
    CASE
  );

  it(
    'H3 measures the fixture-mode viewer, where every field is disabled',
    async () => {
      const page = await browser.newPage({ viewport: VIEWPORT });
      await page.addInitScript(openInspector);
      await page.goto(`${origin}/?fixture=adsb_receiver`, { waitUntil: 'load' });
      await page.waitForSelector('.svelte-flow__node');
      await page.click('.svelte-flow__node[data-id="source"]');
      await page.waitForSelector('.inspector input');

      const rows = (await page.evaluate(measureTargets, [
        ['demo chip', '[data-testid="demo-chip"]'],
        ['read-only reason banner', '[data-testid="block-reason"]'],
        ['disabled field text', '.inspector input'],
        ['field label', '.inspector .label'],
        ['unwired node badge', '.svelte-flow__node .unwired-badge'],
        ['context shortcut key', '[data-testid="top-bar"] .tagline']
      ] as [string, string][])) as Reading[];

      const failing = report('H3 fixture viewer', rows);
      expect(failing.map((row) => row.what)).toEqual([]);
      expect(tooSmall(rows)).toEqual([]);
      await page.close();
    },
    CASE
  );
});
