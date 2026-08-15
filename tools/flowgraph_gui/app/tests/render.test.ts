import { chromium, type Browser, type Page } from 'playwright';
import { afterAll, afterEach, beforeAll, beforeEach, describe, expect, it } from 'vitest';
import { createServer, type ViteDevServer } from 'vite';
import { fixtures } from '../src/fixtures';
import { projectSite } from '../src/lib/project';
import type { Command, FileModel, Site } from '../src/lib/schema';
import { openInspector, shot } from './ui';

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

async function measureEdges(
  fixture: string,
  endpoints: Record<string, string[]>
): Promise<RenderedEdge[]> {
  await page.goto(`${origin}/?fixture=${fixture}`, { waitUntil: 'load' });
  await page.waitForSelector('.svelte-flow__node');
  await page.waitForSelector('path.svelte-flow__edge-path');
  await page.waitForTimeout(500);

  return page.evaluate((ends: Record<string, string[]>) => {
    const pane = document.querySelector('.svelte-flow__pane');
    if (!pane) throw new Error('flow pane never rendered');
    const paneBox = pane.getBoundingClientRect();
    const nodeBoxes = Array.from(document.querySelectorAll('.svelte-flow__node')).map((node) => ({
      id: node.getAttribute('data-id') ?? '',
      box: node.getBoundingClientRect()
    }));
    // ponytail: a wire may tuck under the two blocks it joins — a back edge in a cycle
    // leaves its source sideways and crosses it. Only a third block swallowing it is a bug.
    const hidden = (x: number, y: number, own: string[]) =>
      x < paneBox.left ||
      x > paneBox.right ||
      y < paneBox.top ||
      y > paneBox.bottom ||
      nodeBoxes.some(
        ({ id, box }) =>
          !own.includes(id) && x >= box.left && x <= box.right && y >= box.top && y <= box.bottom
      );

    const samples = 100;
    return Array.from(document.querySelectorAll('.svelte-flow__edge')).map((group) => {
      const id = group.getAttribute('data-id') ?? '';
      const own = ends[id] ?? [];
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
        if (!hidden(x, y, own)) clear++;
      }
      return { id, length, visible: clear / (samples + 1) };
    });
  }, endpoints);
}

function viewportTransform(target: Page): Promise<string> {
  return target.evaluate(() => {
    const viewport = document.querySelector('.svelte-flow__viewport');
    if (!(viewport instanceof HTMLElement)) throw new Error('no flow viewport');
    return viewport.style.transform;
  });
}

async function settledViewport(target: Page): Promise<string> {
  let last = '';
  await expect
    .poll(
      async () => {
        const now = await viewportTransform(target);
        const stable = now !== '' && now === last;
        last = now;
        return stable;
      },
      { interval: 250, timeout: 6000 }
    )
    .toBe(true);
  return last;
}

function scaleOf(transform: string): number {
  const found = /scale\(([\d.]+)\)/.exec(transform)?.[1];
  if (!found) throw new Error(`no scale in "${transform}"`);
  return Number(found);
}

function rightClickPrevented(target: Page, selector: string): Promise<boolean> {
  return target.evaluate((where) => {
    const element = document.querySelector(where);
    if (!element) throw new Error(`nothing matches ${where}`);
    const event = new MouseEvent('contextmenu', {
      bubbles: true,
      cancelable: true,
      clientX: 500,
      clientY: 400
    });
    element.dispatchEvent(event);
    return event.defaultPrevented;
  }, selector);
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
    dirty: false,
    externalChange: false,
    cache: { panels: { leftTab: 'settings' } }
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
      } else if (command.command === 'set_config') {
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
    state.dirty = true;
  }

  function step(from: string[], to: string[]) {
    const previous = from.pop();
    if (previous) {
      to.push(JSON.stringify(state.model));
      state.model = JSON.parse(previous) as FileModel;
      state.revision += 1;
      state.dirty = true;
    }
    state.canUndo = undone.length > 0;
    state.canRedo = redone.length > 0;
  }

  async function invoke(command: string, args: Record<string, unknown>): Promise<unknown> {
    if (command === 'plugin:event|listen') {
      return args.handler;
    }
    if (command === 'plugin:dialog|open') return setup.path;
    if (command === 'save_cache') {
      state.cache = args.ui as Record<string, unknown>;
      return null;
    }
    if (command === 'resolved_cler_root') return '/tmp/fake';
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
    if (command === 'save_document') state.dirty = false;
    if (command === 'reload_document') {
      state.dirty = false;
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
        const rendered = await measureEdges(
          name,
          Object.fromEntries(projected.map((edge) => [edge.id, [edge.source, edge.target]]))
        );

        expect(rendered.map((edge) => edge.id).sort()).toEqual(
          projected.map((edge) => edge.id).sort()
        );

        for (const edge of rendered) {
          expect(edge.length).toBeGreaterThan(0);
          expect(edge.visible, edge.id).toBeGreaterThanOrEqual(MIN_VISIBLE_FRACTION);
        }
      },
      CASE_TIMEOUT
    );
  }
});

describe('editing against a fake backend', () => {
  let editor: Page;

  const sent = () => editor.evaluate(() => (window as unknown as FakeWindow).__fake.log);
  const rawCalls = () => editor.evaluate(() => (window as unknown as FakeWindow).__fake.calls);
  const quiet = ['palette', 'find_target', 'ai_agent_status', 'app_settings'];
  const calls = async () => (await rawCalls()).filter((name) => !quiet.includes(name));

  async function select(blockVar: string) {
    await editor.click(`.svelte-flow__node[data-id="${blockVar}"]`);
    await editor.waitForSelector('.inspector input');
  }

  beforeEach(async () => {
    editor = await browser.newPage({ viewport: VIEWPORT });
    await editor.addInitScript(installFakeBackend, { path: FAKE_PATH, model: editableModel() });
    await editor.addInitScript(openInspector);
    await editor.goto(origin, { waitUntil: 'load' });
    await editor.waitForSelector(`[data-testid="doc-path"][title="${FAKE_PATH}"]`);
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
      const editable = editor.locator('select[data-field="config.scheduler"]');
      await editable.selectOption('cler::SchedulerType::FixedThreadPool');

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
      await expect.poll(() => editor.textContent('.sidebar')).toContain('config from factory');
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
      const undo = editor.locator('[data-testid="undo"]');
      const redo = editor.locator('[data-testid="redo"]');
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

describe('top bar, context menu and shortcuts', () => {
  let editor: Page;

  const PANE_SPOT = { x: 660, y: 760 };

  const rawCalls = () => editor.evaluate(() => (window as unknown as FakeWindow).__fake.calls);
  const quiet = ['palette', 'find_target', 'ai_agent_status', 'app_settings'];
  const calls = async () => (await rawCalls()).filter((name) => !quiet.includes(name));
  const menu = () => editor.locator('[data-testid="context-menu"]');
  const param = () => editor.locator('input[data-field="source1.ctor.1"]');

  async function editParam(text: string) {
    await editor.click('.svelte-flow__node[data-id="source1"]');
    await editor.waitForSelector('.inspector input');
    await param().fill(text);
    await param().blur();
    await expect.poll(() => editor.locator('[data-testid="undo"]').isDisabled()).toBe(false);
  }

  async function openMenu() {
    await editor.locator('.svelte-flow__pane').click({ button: 'right', position: PANE_SPOT });
    await menu().waitFor();
  }

  beforeEach(async () => {
    editor = await browser.newPage({ viewport: VIEWPORT });
    await editor.addInitScript(installFakeBackend, { path: FAKE_PATH, model: editableModel() });
    await editor.addInitScript(openInspector);
    await editor.goto(origin, { waitUntil: 'load' });
    await editor.waitForSelector('.svelte-flow__node');
  }, CASE_TIMEOUT);

  afterEach(async () => {
    await editor.close();
  });

  it(
    'keeps document and canvas actions in the top bar, with history enabled only when it exists',
    async () => {
      const ids = ['undo', 'redo', 'zoom-out', 'zoom-in', 'fit'];
      const titles = await Promise.all(
        ids.map((id) => editor.getAttribute(`[data-testid="${id}"]`, 'title'))
      );
      expect(titles).toEqual([
        null,
        null,
        'Zoom out (Ctrl+-)',
        'Zoom in (Ctrl+=)',
        'Fit view (Ctrl+0)'
      ]);
      expect(await editor.locator('[data-testid="file-menu"]').count()).toBe(1);
      expect(await editor.locator('[data-testid="open"]').count()).toBe(0);
      expect(await editor.locator('[data-testid="save"]').count()).toBe(0);
      expect(await editor.textContent('[data-testid="undo-tooltip"]')).toBe('nothing to undo');
      expect(await editor.textContent('[data-testid="redo-tooltip"]')).toBe('nothing to redo');
      for (const id of ['drawer', 'ai-agent', 'chrome']) {
        expect(await editor.locator(`[data-testid="${id}"]`).count()).toBe(0);
      }

      expect(await editor.locator('.sidebar button', { hasText: 'Undo' }).count()).toBe(0);
      expect(await editor.locator('[data-testid="undo"]').isDisabled()).toBe(true);
      expect(await editor.locator('[data-testid="redo"]').isDisabled()).toBe(true);
      for (const id of ['zoom-out', 'zoom-in', 'fit']) {
        expect(await editor.locator(`[data-testid="${id}"]`).isDisabled()).toBe(false);
      }

      await editParam('12.0f');
      await editor.click('[data-testid="undo"]');
      await expect.poll(() => param().inputValue()).toBe('1.0f');
      await expect.poll(() => editor.locator('[data-testid="redo"]').isDisabled()).toBe(false);
    },
    CASE_TIMEOUT
  );

  it(
    'opens a context menu on the pane and undoes from it',
    async () => {
      await openMenu();
      expect(await editor.locator('[data-testid="menu-undo"]').isDisabled()).toBe(true);
      expect(await editor.locator('[data-testid="menu-redo"]').isDisabled()).toBe(true);
      expect(await editor.locator('[data-testid="menu-fit"]').isDisabled()).toBe(false);
      expect(await editor.textContent('[data-testid="menu-undo"]')).toContain('Ctrl+Z');
      await editor.keyboard.press('Escape');
      await expect.poll(() => menu().count()).toBe(0);

      await editParam('33.0f');
      await openMenu();
      expect(await editor.locator('[data-testid="menu-undo"]').isDisabled()).toBe(false);
      await editor.click('[data-testid="menu-undo"]');

      await expect.poll(() => menu().count()).toBe(0);
      await expect.poll(() => param().inputValue()).toBe('1.0f');
      expect(await calls()).toEqual(['open_document', 'apply_commands', 'undo']);
    },
    CASE_TIMEOUT
  );

  it(
    'spends one backend call per history shortcut, and none while a field has focus',
    async () => {
      await editParam('42.0f');

      await editor.keyboard.press('Control+z');
      await expect.poll(() => param().inputValue()).toBe('1.0f');
      await editor.keyboard.press('Control+Shift+z');
      await expect.poll(() => param().inputValue()).toBe('42.0f');
      await editor.keyboard.press('Control+z');
      await expect.poll(() => param().inputValue()).toBe('1.0f');
      await editor.keyboard.press('Control+y');
      await expect.poll(() => param().inputValue()).toBe('42.0f');

      const history = await calls();
      expect(history).toEqual([
        'open_document',
        'apply_commands',
        'undo',
        'redo',
        'undo',
        'redo'
      ]);

      await param().click();
      await editor.keyboard.press('Control+z');
      await editor.keyboard.press('Control+y');
      await editor.keyboard.press('Control+Shift+z');
      await editor.waitForTimeout(300);
      expect(await calls()).toEqual(history);
    },
    CASE_TIMEOUT
  );

  it(
    'sends Ctrl+S through the document save command',
    async () => {
      const before = await calls();
      await editor.keyboard.press('Control+s');

      const toast = editor.locator('[data-testid="note-toast"]');
      await toast.waitFor();
      expect(await toast.textContent()).toContain('saved to source');
      expect(await calls()).toEqual([...before, 'save_document']);

      await expect.poll(() => toast.count(), { timeout: 5000 }).toBe(0);
    },
    CASE_TIMEOUT
  );

  it(
    'zooms and refits from the keyboard',
    async () => {
      const fitted = await settledViewport(editor);
      const base = scaleOf(fitted);

      const settled = (zoom: number) =>
        expect
          .poll(async () => scaleOf(await viewportTransform(editor)), { timeout: 2000 })
          .toBeCloseTo(base * zoom, 3);

      await editor.keyboard.press('Control+Equal');
      await settled(1.2);

      await editor.keyboard.press('Control+Minus');
      await settled(1);

      await editor.keyboard.press('Control+Equal');
      await settled(1.2);
      await editor.keyboard.press('Control+Equal');
      await settled(1.44);
      expect(await viewportTransform(editor)).not.toBe(fitted);

      await editor.keyboard.press('Control+0');
      await expect.poll(() => viewportTransform(editor), { timeout: 2000 }).toBe(fitted);
    },
    CASE_TIMEOUT
  );

  it(
    'suppresses the browser menu on the pane but leaves it alone in a field',
    async () => {
      expect(await rightClickPrevented(editor, '.svelte-flow__pane')).toBe(true);
      await expect.poll(() => menu().count()).toBe(1);

      expect(await rightClickPrevented(editor, 'input[data-field="config.num_workers"]')).toBe(false);
      await expect.poll(() => menu().count()).toBe(0);
    },
    CASE_TIMEOUT
  );
});

describe('retractable panels', () => {
  let editor: Page;

  const RAIL_WIDTH = 44;
  const SIDEBAR_WIDTH = 280;
  const INSPECTOR_WIDTH = 320;

  async function width(selector: string): Promise<number> {
    const box = await editor.locator(selector).boundingBox();
    if (!box) throw new Error(`${selector} has no box`);
    return box.width;
  }

  function settled(selector: string, expected: number) {
    return expect.poll(() => width(selector), { timeout: 2000 }).toBeCloseTo(expected, 0);
  }

  beforeEach(async () => {
    editor = await browser.newPage({ viewport: VIEWPORT });
    await editor.addInitScript(installFakeBackend, { path: FAKE_PATH, model: editableModel() });
    await editor.addInitScript(openInspector);
    await editor.goto(origin, { waitUntil: 'load' });
    await editor.waitForSelector('.svelte-flow__node');
  }, CASE_TIMEOUT);

  afterEach(async () => {
    await editor.close();
  });

  it(
    'collapses each panel to a rail and remembers it across a reload',
    async () => {
      await settled('.sidebar', SIDEBAR_WIDTH);
      await settled('.inspector', INSPECTOR_WIDTH);
      expect(await width('main')).toBeCloseTo(VIEWPORT.width, 0);

      await editor.click('[data-testid="toggle-left"]');
      await editor.click('[data-testid="toggle-right"]');
      await settled('.sidebar', RAIL_WIDTH);
      await settled('.inspector', RAIL_WIDTH);
      expect(await width('main')).toBeCloseTo(VIEWPORT.width, 0);

      expect(await editor.locator('.sidebar .path').first().isVisible()).toBe(false);
      expect(await editor.locator('.inspector input').first().isVisible()).toBe(false);
      expect(await editor.locator('[data-testid="top-bar"] img').isVisible()).toBe(true);
      expect(await editor.locator('[data-testid="undo"]').isVisible()).toBe(true);

      await editor.reload({ waitUntil: 'load' });
      await editor.waitForSelector('.svelte-flow__node');
      await settled('.sidebar', RAIL_WIDTH);
      await settled('.inspector', RAIL_WIDTH);

      await editor.click('[data-testid="toggle-left"]');
      await editor.click('[data-testid="toggle-right"]');
      await editor.reload({ waitUntil: 'load' });
      await editor.waitForSelector('.svelte-flow__node');
      await settled('.sidebar', SIDEBAR_WIDTH);
      await settled('.inspector', INSPECTOR_WIDTH);
    },
    CASE_TIMEOUT
  );

  it(
    'toggles on [ and ] unless the keystroke lands in a field',
    async () => {
      await editor.locator('.svelte-flow__pane').click({ position: { x: 660, y: 760 } });
      await editor.keyboard.press('[');
      await settled('.sidebar', RAIL_WIDTH);
      await editor.keyboard.press(']');
      await settled('.inspector', RAIL_WIDTH);

      await editor.keyboard.press('[');
      await editor.keyboard.press(']');
      await settled('.sidebar', SIDEBAR_WIDTH);
      await settled('.inspector', INSPECTOR_WIDTH);

      const field = editor.locator('input[data-field="config.num_workers"]');
      await field.click();
      await field.pressSequentially('[]');
      await settled('.sidebar', SIDEBAR_WIDTH);
      await settled('.inspector', INSPECTOR_WIDTH);
      expect(await field.inputValue()).toContain('[]');
      await field.press('Escape');
      const commands = await editor.evaluate(() => (window as unknown as FakeWindow).__fake.log);
      expect(commands).toHaveLength(0);
    },
    CASE_TIMEOUT
  );

  it(
    'expands a collapsed inspector when a block is clicked, and stays open on deselect',
    async () => {
      await editor.click('[data-testid="toggle-right"]');
      await settled('.inspector', RAIL_WIDTH);

      await editor.click('.svelte-flow__node[data-id="adder"]');
      await settled('.inspector', INSPECTOR_WIDTH);
      await expect.poll(() => editor.textContent('.inspector .title')).toBe('Adder');

      await editor.locator('.svelte-flow__pane').click({ position: { x: 660, y: 760 } });
      await expect.poll(() => editor.locator('.inspector .title').count()).toBe(0);
      await settled('.inspector', INSPECTOR_WIDTH);
    },
    CASE_TIMEOUT
  );

  it(
    'keeps the collapsed rail hinting that a selection is waiting',
    async () => {
      await editor.click('.svelte-flow__node[data-id="adder"]');
      await editor.click('[data-testid="toggle-right"]');
      await settled('.inspector', RAIL_WIDTH);

      const rail = editor.locator('[data-testid="toggle-right"]');
      expect(await rail.locator('svg').count()).toBe(1);
      expect(await rail.getAttribute('title')).toContain('Adder selected');
      expect(await rail.getAttribute('aria-expanded')).toBe('false');
      expect(await editor.getAttribute('[data-testid="toggle-left"]', 'title')).toContain('[');
    },
    CASE_TIMEOUT
  );

  it(
    'does not move the graph when floating rails are toggled',
    async () => {
      const fitted = await settledViewport(editor);
      await editor.click('[data-testid="zoom-in"]');
      await editor.mouse.move(700, 400);
      await editor.mouse.down();
      await editor.mouse.move(760, 460, { steps: 8 });
      await editor.mouse.up();
      await editor.waitForTimeout(400);
      const moved = await settledViewport(editor);
      expect(moved).not.toBe(fitted);

      await editor.click('[data-testid="toggle-left"]');
      await settled('.sidebar', RAIL_WIDTH);
      await editor.click('[data-testid="toggle-right"]');
      await settled('.inspector', RAIL_WIDTH);
      await editor.waitForTimeout(500);
      expect(await viewportTransform(editor)).toBe(moved);
    },
    CASE_TIMEOUT
  );
});

describe('fixture mode stays a read-only viewer', () => {
  let viewer: Page;

  beforeEach(async () => {
    viewer = await browser.newPage({ viewport: VIEWPORT });
    await viewer.addInitScript(openInspector);
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
      expect(await viewer.getAttribute('[data-testid="demo-chip"]', 'title')).toContain(
        'read-only viewer'
      );

      expect(await viewer.locator('[data-testid="undo"]').isDisabled()).toBe(true);
      expect(await viewer.locator('[data-testid="reload-banner"]').count()).toBe(0);
    },
    CASE_TIMEOUT
  );

  it(
    'retracts its panels without gaining any edit power',
    async () => {
      const boxWidth = async (selector: string) =>
        (await viewer.locator(selector).boundingBox())?.width ?? -1;

      await viewer.click('[data-testid="toggle-left"]');
      await viewer.click('[data-testid="toggle-right"]');
      await expect.poll(() => boxWidth('.sidebar'), { timeout: 2000 }).toBeCloseTo(44, 0);
      await expect.poll(() => boxWidth('.inspector'), { timeout: 2000 }).toBeCloseTo(44, 0);
      await expect.poll(() => boxWidth('main'), { timeout: 2000 }).toBeCloseTo(1440, 0);

      await viewer.click('.svelte-flow__node[data-id="iq2mag"]');
      await expect.poll(() => boxWidth('.inspector'), { timeout: 2000 }).toBeCloseTo(320, 0);
      expect(await viewer.locator('.inspector input').first().isDisabled()).toBe(true);
      expect(await viewer.getAttribute('[data-testid="demo-chip"]', 'title')).toContain(
        'read-only viewer'
      );
    },
    CASE_TIMEOUT
  );

  it(
    'keeps the view actions live while history and the file dialog stay out of reach',
    async () => {
      expect(await viewer.locator('[data-testid="redo"]').isDisabled()).toBe(true);
      expect(await viewer.locator('[data-testid="fit"]').isDisabled()).toBe(false);

      await viewer
        .locator('.svelte-flow__pane')
        .click({ button: 'right', position: { x: 660, y: 760 } });
      const menu = viewer.locator('[data-testid="context-menu"]');
      await menu.waitFor();
      expect(await viewer.locator('[data-testid="menu-undo"]').isDisabled()).toBe(true);
      expect(await viewer.locator('[data-testid="menu-fit"]').isDisabled()).toBe(false);

      const fitted = await settledViewport(viewer);
      await viewer.click('[data-testid="menu-fit"]');
      await expect.poll(() => menu.count()).toBe(0);

      await viewer.keyboard.press('Control+Equal');
      await expect
        .poll(async () => scaleOf(await viewportTransform(viewer)), { timeout: 2000 })
        .toBeGreaterThan(scaleOf(fitted));

      await viewer.keyboard.press('Control+o');
      await viewer.waitForSelector('[data-testid="status"]');
      expect(await viewer.textContent('[data-testid="status"]')).toContain('desktop shell');
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

describe('typed edges on the canvas', () => {
  let typed: Page;

  function rgbOf(hex: string): string {
    const value = hex.replace('#', '');
    const channel = (at: number) => parseInt(value.slice(at, at + 2), 16);
    return `rgb(${channel(0)}, ${channel(2)}, ${channel(4)})`;
  }

  async function token(name: string): Promise<string> {
    const hex = await typed.evaluate(
      (which) => getComputedStyle(document.documentElement).getPropertyValue(which).trim(),
      name
    );
    return rgbOf(hex);
  }

  async function show(fixture: string): Promise<void> {
    await typed.goto(`${origin}/?fixture=${fixture}`, { waitUntil: 'load' });
    await typed.waitForSelector('path.svelte-flow__edge-path');
    await typed.waitForTimeout(400);
  }

  function strokes(): Promise<Record<string, string>> {
    return typed.evaluate(() =>
      Object.fromEntries(
        Array.from(document.querySelectorAll('.svelte-flow__edge')).map((group) => {
          const path = group.querySelector('path.svelte-flow__edge-path');
          const id = group.getAttribute('data-id') ?? '';
          return [id, path ? getComputedStyle(path).stroke : ''];
        })
      )
    );
  }

  function titles(): Promise<Record<string, string>> {
    return typed.evaluate(() =>
      Object.fromEntries(
        Array.from(document.querySelectorAll('.svelte-flow__edge')).map((group) => [
          group.getAttribute('data-id') ?? '',
          group.querySelector('title')?.textContent?.trim() ?? ''
        ])
      )
    );
  }

  beforeEach(async () => {
    typed = await browser.newPage({ viewport: VIEWPORT });
  }, CASE_TIMEOUT);

  afterEach(async () => {
    await typed.close();
  });

  it(
    'paints one token per type and leaves unresolved edges neutral',
    async () => {
      await show('spike');
      const painted = await strokes();
      expect(painted['source->fanout.in[]#0']).toBe(await token('--type-1'));
      expect(painted['fanout->power.in[]#0']).toBe(await token('--muted'));
      expect(painted['power->trigger.in[]#0']).toBe(await token('--type-2'));
    },
    CASE_TIMEOUT
  );

  it(
    'hides the legend when a site resolves a single type',
    async () => {
      await show('hello_world');
      expect(await typed.locator('[data-testid="type-legend"]').count()).toBe(0);
    },
    CASE_TIMEOUT
  );

  it(
    'lists every resolved type in the legend and collapses on demand',
    async () => {
      await show('type_conflict');
      const legend = typed.locator('[data-testid="type-legend"]');
      await legend.waitFor();
      expect(await legend.locator('li .name').allTextContents()).toEqual([
        'float',
        'std::complex<float>'
      ]);
      const swatch = await legend.locator('li .swatch').first().evaluate(
        (node) => getComputedStyle(node).backgroundColor
      );
      expect(swatch).toBe(await token('--type-1'));
      await shot(typed, 'type-legend');

      await typed.click('[data-testid="type-legend-toggle"]');
      await expect.poll(() => legend.locator('li').count()).toBe(0);
    },
    CASE_TIMEOUT
  );

  it(
    'lets a type conflict outrank the type colour and badges the target end',
    async () => {
      await show('type_conflict');
      const painted = await strokes();
      const danger = await token('--danger');
      expect(painted['throttle->throughput.in[]#0']).toBe(danger);
      expect(painted['ramp->throttle.in[]#0']).toBe(await token('--type-1'));
      expect(painted['throughput->sink.in[]#0']).toBe(await token('--type-2'));

      const badges = typed.locator('.cler-type-badge');
      expect(await badges.count()).toBe(1);
      expect((await badges.first().textContent())?.trim()).toBe('type');
    },
    CASE_TIMEOUT
  );

  it(
    'names the type in the edge tooltip and both types on a conflict',
    async () => {
      await show('type_conflict');
      const tooltips = await titles();
      expect(tooltips['ramp->throttle.in[]#0']).toBe('sample type: float');
      expect(tooltips['throttle->throughput.in[]#0']).toBe(
        'type conflict: float out → std::complex<float> in'
      );
    },
    CASE_TIMEOUT
  );

  it(
    'shows the resolved type beside every port in the inspector',
    async () => {
      await show('type_conflict');
      await typed.click('.svelte-flow__node[data-id="throttle"]');
      await typed.waitForSelector('.inspector .ports');
      expect(await typed.locator('.inspector .ports li').allTextContents()).toEqual([
        'inin float',
        'outthroughput.in std::complex<float>'
      ]);
    },
    CASE_TIMEOUT
  );
});
