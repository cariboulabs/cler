import { chromium, type Browser, type Page } from 'playwright';
import { afterAll, afterEach, beforeAll, beforeEach, describe, expect, it } from 'vitest';
import { createServer, type ViteDevServer } from 'vite';
import { fixtures } from '../src/fixtures';
import { projectSite } from '../src/lib/project';
import type { Command, FileModel, Site } from '../src/lib/schema';

const MIN_VISIBLE_FRACTION = 0.95;
const BOOT_TIMEOUT = 120_000;
const CASE_TIMEOUT = 60_000;
const VIEWPORT = { width: 1440, height: 900 };
const FAKE_PATH = '/tmp/fake/hello_world.cpp';

type RenderedEdge = { id: string; length: number; visible: number };

type FakeSetup = { path: string; model: FileModel };

type FakeControls = {
  log: Command[];
  calls: string[];
  fail: (message: string) => void;
  emitExternal: () => void;
};

type FakeWindow = {
  __fake: FakeControls;
  __TAURI_INTERNALS__: {
    invoke: (command: string, args: Record<string, unknown>) => Promise<unknown>;
    transformCallback: (callback: (message: unknown) => void) => number;
    unregisterCallback: (id: number) => void;
  };
  __TAURI_EVENT_PLUGIN_INTERNALS__: { unregisterListener: (event: string, id: number) => void };
};

let server: ViteDevServer;
let browser: Browser;
let page: Page;
let origin: string;

function firstSite(name: string): Site {
  const site = fixtures[name]?.sites[0];
  if (!site) throw new Error(`fixture ${name} has no sites`);
  return site;
}

async function measureEdges(fixture: string): Promise<RenderedEdge[]> {
  await page.goto(`${origin}/?fixture=${fixture}`, { waitUntil: 'load' });
  await page.waitForSelector('.svelte-flow__node');
  await page.waitForSelector('path.svelte-flow__edge-path');
  await page.waitForTimeout(500);

  return page.evaluate(() => {
    const pane = document.querySelector('.svelte-flow__pane');
    if (!pane) throw new Error('flow pane never rendered');
    const paneBox = pane.getBoundingClientRect();
    const nodeBoxes = Array.from(document.querySelectorAll('.svelte-flow__node')).map((node) =>
      node.getBoundingClientRect()
    );
    const hidden = (x: number, y: number) =>
      x < paneBox.left ||
      x > paneBox.right ||
      y < paneBox.top ||
      y > paneBox.bottom ||
      nodeBoxes.some((box) => x >= box.left && x <= box.right && y >= box.top && y <= box.bottom);

    const samples = 100;
    return Array.from(document.querySelectorAll('.svelte-flow__edge')).map((group) => {
      const path = group.querySelector('path.svelte-flow__edge-path');
      if (!(path instanceof SVGPathElement)) throw new Error('edge rendered without a path');
      const matrix = path.getScreenCTM();
      if (!matrix) throw new Error('edge path has no screen geometry');
      const length = path.getTotalLength();
      let clear = 0;
      for (let i = 0; i <= samples; i++) {
        const point = path.getPointAtLength((length * i) / samples);
        const x = matrix.a * point.x + matrix.c * point.y + matrix.e;
        const y = matrix.b * point.x + matrix.d * point.y + matrix.f;
        if (!hidden(x, y)) clear++;
      }
      return {
        id: group.getAttribute('data-id') ?? '',
        length,
        visible: clear / (samples + 1)
      };
    });
  });
}

function installFakeBackend(setup: FakeSetup) {
  function need<T>(value: T | undefined | null, what: string): T {
    if (value === undefined || value === null) throw new Error(`fake backend: no ${what}`);
    return value;
  }

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
  const log: Command[] = [];
  const calls: string[] = [];
  const failures: string[] = [];
  const callbacks = new Map<number, (message: unknown) => void>();
  let nextCallback = 1;

  function snapshot(): unknown {
    return JSON.parse(JSON.stringify(state));
  }

  function site(index: number) {
    return need(state.model.sites[index], `site ${index}`);
  }

  function block(index: number, name: string) {
    return need(
      site(index).blocks.find((candidate) => candidate.var === name),
      `block ${name}`
    );
  }

  function apply(commands: Command[]) {
    undone.push(JSON.stringify(state.model));
    redone.length = 0;
    for (const command of commands) {
      log.push(command);
      if (command.command === 'set_param') {
        need(block(command.site, command.block).ctor_args[command.ctor_arg_index], 'ctor arg').text =
          command.new_text;
      } else if (command.command === 'set_template_arg') {
        need(
          block(command.site, command.block).template_args[command.template_arg_index],
          'template arg'
        ).text = command.new_text;
      } else if (command.command === 'set_display_name') {
        block(command.site, command.block).display_name = command.new_text;
      } else {
        const config = need(site(command.site).config, 'config');
        need(
          config.assignments.find((entry) => entry.path === command.path),
          `assignment ${command.path}`
        ).value = command.new_value;
      }
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
    if (command === 'plugin:event|listen') {
      return args.handler;
    }
    if (command === 'plugin:dialog|open') return setup.path;
    calls.push(command);
    if (command === 'apply_commands') {
      const commands = args.commands as Command[];
      const failure = failures.shift();
      if (failure) {
        for (const entry of commands) log.push(entry);
        throw failure;
      }
      apply(commands);
      return snapshot();
    }
    if (command === 'undo') step(undone, redone);
    if (command === 'redo') step(redone, undone);
    if (command === 'reload_document') {
      state.externalChange = false;
      state.canUndo = false;
      state.canRedo = false;
      undone.length = 0;
      redone.length = 0;
    }
    if (command === 'close_document') return null;
    return snapshot();
  }

  const scope = window as unknown as FakeWindow;
  scope.__TAURI_EVENT_PLUGIN_INTERNALS__ = { unregisterListener: () => undefined };
  scope.__TAURI_INTERNALS__ = {
    invoke,
    transformCallback: (callback) => {
      const id = nextCallback++;
      callbacks.set(id, callback);
      return id;
    },
    unregisterCallback: (id) => {
      callbacks.delete(id);
    }
  };
  scope.__fake = {
    log,
    calls,
    fail: (message) => failures.push(message),
    emitExternal: () => {
      for (const callback of callbacks.values()) {
        callback({ event: 'document-changed-externally', id: 0, payload: { path: state.path } });
      }
    }
  };
}

function editableModel(): FileModel {
  const base = structuredClone(fixtures.hello_world);
  if (!base) throw new Error('hello_world fixture missing');
  base.file = FAKE_PATH;
  const site = base.sites[0];
  if (!site) throw new Error('hello_world fixture has no site');
  const plot = site.blocks.find((block) => block.var === 'plot');
  if (!plot) throw new Error('hello_world fixture has no plot block');
  plot.editable = false;
  plot.read_only_reason = 'optional_emplace_declaration';
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
        read_only_reason: 'config_from_factory',
        path: 'adaptive_sleep',
        value: 'true',
        span: { start: 2, end: 3 },
        value_span: { start: 2, end: 3 }
      }
    ]
  };
  return base;
}

beforeAll(async () => {
  server = await createServer({ logLevel: 'silent', server: { port: 0, strictPort: false } });
  await server.listen();
  const url = server.resolvedUrls?.local[0];
  if (!url) throw new Error('vite dev server reported no url');
  origin = url.replace(/\/$/, '');
  browser = await chromium.launch({ channel: 'chrome' });
  page = await browser.newPage({ viewport: VIEWPORT });
}, BOOT_TIMEOUT);

afterAll(async () => {
  await browser?.close();
  await server?.close();
});

describe('every fixture renders its edges on screen', () => {
  for (const name of Object.keys(fixtures)) {
    it(
      name,
      async () => {
        const projected = projectSite(firstSite(name)).edges;
        const rendered = await measureEdges(name);

        expect(rendered.map((edge) => edge.id).sort()).toEqual(
          projected.map((edge) => edge.id).sort()
        );

        for (const edge of rendered) {
          expect(edge.length).toBeGreaterThan(0);
          expect(edge.visible).toBeGreaterThanOrEqual(MIN_VISIBLE_FRACTION);
        }
      },
      CASE_TIMEOUT
    );
  }
});

describe('editing against a fake backend', () => {
  let editor: Page;

  const sent = () => editor.evaluate(() => (window as unknown as FakeWindow).__fake.log);
  const calls = () => editor.evaluate(() => (window as unknown as FakeWindow).__fake.calls);

  async function select(blockVar: string) {
    await editor.click(`.svelte-flow__node[data-id="${blockVar}"]`);
    await editor.waitForSelector('.inspector input');
  }

  beforeEach(async () => {
    editor = await browser.newPage({ viewport: VIEWPORT });
    await editor.addInitScript(installFakeBackend, { path: FAKE_PATH, model: editableModel() });
    await editor.goto(origin, { waitUntil: 'load' });
    await editor.click('button.primary');
    await editor.waitForSelector(`.path[title="${FAKE_PATH}"]`);
    await editor.waitForSelector('.svelte-flow__node');
  }, CASE_TIMEOUT);

  afterEach(async () => {
    await editor.close();
  });

  it(
    'commits one set_param on blur, whatever the keystroke count',
    async () => {
      await select('source1');
      const input = editor.locator('input[data-field="source1.ctor.1"]');
      await expect.poll(() => input.inputValue()).toBe('1.0f');

      await input.fill('');
      await input.pressSequentially('9.5f');
      expect(await sent()).toHaveLength(0);

      await input.blur();
      await expect.poll(sent).toEqual([
        { command: 'set_param', site: 0, block: 'source1', ctor_arg_index: 1, new_text: '9.5f' }
      ]);
      await expect.poll(() => input.inputValue()).toBe('9.5f');
      await expect
        .poll(() => editor.textContent('.svelte-flow__node[data-id="source1"] .type'))
        .toContain('SourceCWBlock');
    },
    CASE_TIMEOUT
  );

  it(
    'reverts on Escape and sends nothing',
    async () => {
      await select('source1');
      const input = editor.locator('input[data-field="source1.ctor.2"]');
      const before = await input.inputValue();

      await input.fill('nonsense');
      await input.press('Escape');

      expect(await sent()).toHaveLength(0);
      await expect.poll(() => input.inputValue()).toBe(before);
    },
    CASE_TIMEOUT
  );

  it(
    'renders the returned model on the canvas after a display-name edit',
    async () => {
      await select('adder');
      const input = editor.locator('input[data-field="adder.display_name"]');
      await input.fill('Summing Junction');
      await input.press('Enter');

      await expect.poll(sent).toEqual([
        { command: 'set_display_name', site: 0, block: 'adder', new_text: 'Summing Junction' }
      ]);
      await expect
        .poll(() => editor.textContent('.svelte-flow__node[data-id="adder"] .name'))
        .toBe('Summing Junction');
      await expect.poll(() => editor.textContent('.inspector .title')).toBe('Summing Junction');
    },
    CASE_TIMEOUT
  );

  it(
    'edits a config assignment and disables the read-only one',
    async () => {
      const editable = editor.locator('input[data-field="config.scheduler"]');
      await editable.fill('cler::SchedulerType::FixedThreadPool');
      await editable.blur();

      await expect.poll(sent).toEqual([
        {
          command: 'set_config',
          site: 0,
          path: 'scheduler',
          new_value: 'cler::SchedulerType::FixedThreadPool'
        }
      ]);

      const locked = editor.locator('input[data-field="config.adaptive_sleep"]');
      expect(await locked.isDisabled()).toBe(true);
      await expect.poll(() => editor.textContent('.inspector')).toContain('config from factory');
    },
    CASE_TIMEOUT
  );

  it(
    'disables a read-only block and shows its reason',
    async () => {
      await select('plot');
      await expect
        .poll(() => editor.textContent('[data-testid="block-reason"]'))
        .toContain('optional emplace declaration');

      for (const field of ['plot.display_name', 'plot.ctor.0', 'plot.ctor.2']) {
        expect(await editor.locator(`input[data-field="${field}"]`).isDisabled()).toBe(true);
      }
      expect(await sent()).toHaveLength(0);
    },
    CASE_TIMEOUT
  );

  it(
    'surfaces a refused edit and reverts the field',
    async () => {
      await editor.evaluate(() =>
        (window as unknown as FakeWindow).__fake.fail(
          JSON.stringify({
            error: 'references_outside_graph',
            block: 'plot',
            spans: [
              { start: 1, end: 2 },
              { start: 3, end: 4 }
            ]
          })
        )
      );

      await select('source1');
      const input = editor.locator('input[data-field="source1.ctor.1"]');
      const before = await input.inputValue();
      await input.fill('7.0f');
      await input.blur();

      await expect
        .poll(() => editor.textContent('[data-error="source1.ctor.1"]'))
        .toBe('plot is still used in 2 places outside the flowgraph — remove those references first');
      await expect.poll(() => input.inputValue()).toBe(before);
      expect(await editor.textContent('[data-testid="status"]')).toContain('outside the flowgraph');
    },
    CASE_TIMEOUT
  );

  it(
    'wires undo and redo to the backend history flags',
    async () => {
      const undo = editor.locator('.history button', { hasText: 'Undo' });
      const redo = editor.locator('.history button', { hasText: 'Redo' });
      expect(await undo.isDisabled()).toBe(true);
      expect(await redo.isDisabled()).toBe(true);

      await select('source1');
      const input = editor.locator('input[data-field="source1.ctor.1"]');
      const before = await input.inputValue();
      await input.fill('42.0f');
      await input.blur();
      await expect.poll(() => input.inputValue()).toBe('42.0f');
      await expect.poll(() => undo.isDisabled()).toBe(false);

      await undo.click();
      await expect.poll(() => input.inputValue()).toBe(before);
      await expect.poll(() => redo.isDisabled()).toBe(false);

      await redo.click();
      await expect.poll(() => input.inputValue()).toBe('42.0f');
      expect(await calls()).toEqual(['open_document', 'apply_commands', 'undo', 'redo']);
    },
    CASE_TIMEOUT
  );

  it(
    'raises the reload banner on the external-change event and clears it on reload',
    async () => {
      expect(await editor.locator('[data-testid="reload-banner"]').count()).toBe(0);

      await editor.evaluate(() => (window as unknown as FakeWindow).__fake.emitExternal());
      const banner = editor.locator('[data-testid="reload-banner"]');
      await banner.waitFor();
      expect(await banner.textContent()).toContain('discards the undo history');

      await banner.locator('button').click();
      await expect.poll(() => banner.count()).toBe(0);
      expect(await calls()).toContain('reload_document');
    },
    CASE_TIMEOUT
  );
});

describe('fixture mode stays a read-only viewer', () => {
  let viewer: Page;

  beforeEach(async () => {
    viewer = await browser.newPage({ viewport: VIEWPORT });
    await viewer.goto(`${origin}/?fixture=adsb_receiver`, { waitUntil: 'load' });
    await viewer.waitForSelector('.svelte-flow__node');
  }, CASE_TIMEOUT);

  afterEach(async () => {
    await viewer.close();
  });

  it(
    'disables every field and says why',
    async () => {
      await viewer.click('.svelte-flow__node[data-id="iq2mag"]');
      await viewer.waitForSelector('.inspector input');

      const inputs = viewer.locator('.inspector input');
      const count = await inputs.count();
      expect(count).toBeGreaterThan(1);
      for (let index = 0; index < count; index++) {
        expect(await inputs.nth(index).isDisabled()).toBe(true);
      }
      expect(await viewer.textContent('[data-testid="viewer-note"]')).toContain('read-only viewer');

      expect(await viewer.locator('.history button').first().isDisabled()).toBe(true);
      expect(await viewer.locator('[data-testid="reload-banner"]').count()).toBe(0);
    },
    CASE_TIMEOUT
  );

  it(
    'shows the read-only reason for a block the parser refused',
    async () => {
      await viewer.click('.svelte-flow__node[data-id="source"]');
      await viewer.waitForSelector('[data-testid="block-reason"]');
      expect(await viewer.textContent('[data-testid="block-reason"]')).toContain(
        'optional emplace declaration'
      );
      expect(await viewer.textContent('.inspector')).toContain('optional emplace declaration');
    },
    CASE_TIMEOUT
  );
});
