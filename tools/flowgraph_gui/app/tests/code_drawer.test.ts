import { chromium, type Browser, type Page } from 'playwright';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { createServer, type ViteDevServer } from 'vite';
import { fixtures, fixtureSources } from '../src/fixtures';
import { blockSpans, targetAt } from '../src/lib/project';
import { lineOfOffset, type FileModel, type Span } from '../src/lib/schema';
import { openInspector } from './ui';

const BOOT_TIMEOUT = 120_000;
const CASE = 90_000;
const STEP_TIMEOUT = 45_000;
const VIEWPORT = { width: 1440, height: 900 };
const FAKE_PATH = '/tmp/fake/hello_world.cpp';
const DEFAULT_HEIGHT = 260;

type Setup = { path: string; model: FileModel; source: string };

type Call = { name: string; args: Record<string, unknown> };

type Fake = {
  calls: Call[];
  source: () => string;
  prepend: (text: string) => void;
};

type FakeWindow = { __fake: Fake };

type Piece = { at: number; length: number };

let server: ViteDevServer;
let browser: Browser;
let origin: string;

function installFake(setup: Setup) {
  const state = {
    path: setup.path,
    revision: 1,
    model: setup.model,
    source: setup.source,
    canUndo: false,
    canRedo: false,
    dirty: false,
    externalChange: false,
    cache: {}
  };
  const calls: { name: string; args: Record<string, unknown> }[] = [];
  const callbacks = new Map<number, (message: unknown) => void>();
  let nextCallback = 1;

  function snapshot(): unknown {
    return JSON.parse(JSON.stringify(state));
  }

  function shiftOffsets(value: unknown, from: number, delta: number) {
    if (Array.isArray(value)) {
      for (const item of value) shiftOffsets(item, from, delta);
      return;
    }
    if (typeof value !== 'object' || value === null) return;
    const record = value as Record<string, unknown>;
    for (const key of ['start', 'end', 'call_offset']) {
      const at = record[key];
      if (typeof at === 'number' && at >= from) record[key] = at + delta;
    }
    for (const nested of Object.values(record)) shiftOffsets(nested, from, delta);
  }

  function splice(span: { start: number; end: number }, text: string) {
    const before = state.source.slice(0, span.start);
    const after = state.source.slice(span.end);
    const delta = text.length - (span.end - span.start);
    state.source = `${before}${text}${after}`;
    shiftOffsets(state.model, span.end, delta);
  }

  function apply(commands: Record<string, unknown>[]) {
    for (const command of commands) {
      const site = state.model.sites[command.site as number];
      if (!site) throw new Error('no site');
      const block = site.blocks.find((candidate) => candidate.var === command.block);
      if (!block) throw new Error('no block');
      const text = String(command.new_text ?? '');
      if (command.command === 'set_param') {
        const arg = block.ctor_args[command.ctor_arg_index as number];
        if (!arg) throw new Error('no ctor arg');
        const span = { ...arg.span };
        arg.text = text;
        splice(span, text);
      } else if (command.command === 'set_template_arg') {
        const arg = block.template_args[command.template_arg_index as number];
        if (!arg) throw new Error('no template arg');
        const span = { ...arg.span };
        arg.text = text;
        splice(span, text);
      } else {
        block.display_name = text;
      }
    }
    state.revision += 1;
    state.canUndo = true;
    state.dirty = true;
  }

  async function invoke(command: string, args: Record<string, unknown>): Promise<unknown> {
    if (command === 'plugin:event|listen') return args.handler;
    if (command === 'plugin:dialog|open') return state.path;
    if (command === 'save_cache') {
      state.cache = args.ui as Record<string, unknown>;
      return null;
    }
    calls.push({ name: command, args });
    if (command === 'apply_commands') apply(args.commands as Record<string, unknown>[]);
    if (command === 'edit_source') {
      const next = String(args.source);
      // The real backend gates on tree-sitter; balanced braces are enough here to
      // drive both the accepted and the "graph paused" path.
      const unparsed = next.split('{').length !== next.split('}').length;
      if (!unparsed) {
        state.source = next;
        state.revision += 1;
        state.dirty = true;
      }
      const at = next.lastIndexOf('{');
      return {
        state: snapshot(),
        unparsed,
        fault: unparsed ? { span: { start: at, end: at + 1 }, hint: 'expected `}`' } : null
      };
    }
    if (command === 'save_document') state.dirty = false;
    if (command === 'reload_document') {
      state.dirty = false;
      state.externalChange = false;
    }
    if (command === 'open_in_editor') return null;
    if (command === 'close_document') return null;
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
    calls,
    source: () => state.source,
    prepend: (text: string) => {
      state.source = text + state.source;
      shiftOffsets(state.model, 0, text.length);
      state.externalChange = true;
      for (const callback of callbacks.values()) {
        callback({
          event: 'document-changed-externally',
          id: 0,
          payload: { path: state.path }
        });
      }
    }
  };
}

function fixtureModel(name: string): FileModel {
  const base = structuredClone(fixtures[name]);
  if (!base) throw new Error(`missing fixture ${name}`);
  base.file = FAKE_PATH;
  return base;
}

function fixtureSource(name: string): string {
  const source = fixtureSources[name];
  if (source === undefined) throw new Error(`missing source ${name}`);
  return source;
}

function siteOf(name: string, index: number) {
  const site = fixtures[name]?.sites[index];
  if (!site) throw new Error(`fixture ${name} has no site ${index}`);
  return site;
}

async function boot(name = 'hello_world'): Promise<Page> {
  const page = await browser.newPage({
    viewport: VIEWPORT,
    permissions: ['clipboard-read', 'clipboard-write']
  });
  page.setDefaultTimeout(STEP_TIMEOUT);
  await page.addInitScript(openInspector);
  await page.addInitScript(installFake, {
    path: FAKE_PATH,
    model: fixtureModel(name),
    source: fixtureSource(name)
  });
  await page.goto(origin, { waitUntil: 'load' });
  await page.waitForSelector(`[data-testid="doc-path"][title="${FAKE_PATH}"]`);
  await page.waitForSelector('.svelte-flow__node');
  return page;
}

async function openDrawer(page: Page): Promise<void> {
  await page.click('[data-testid="drawer-toggle"]');
  await page.waitForSelector('[data-testid="drawer-body"] .cm-line');
  await page.waitForTimeout(250);
}

const calls = (page: Page) => page.evaluate(() => (window as unknown as FakeWindow).__fake.calls);

function drawerHeight(page: Page): Promise<number> {
  return page.evaluate(() => {
    const drawer = document.querySelector('[data-testid="code-drawer"]');
    return drawer instanceof HTMLElement ? drawer.getBoundingClientRect().height : -1;
  });
}

function scrollTop(page: Page): Promise<number> {
  return page.evaluate(() => {
    const scroller = document.querySelector('[data-testid="drawer-body"] .cm-scroller');
    return scroller instanceof HTMLElement ? scroller.scrollTop : -1;
  });
}

function highlighted(page: Page): Promise<Piece[]> {
  return page.evaluate(() =>
    Array.from(document.querySelectorAll('[data-testid="drawer-body"] .hit')).map((piece) => ({
      at: Number(piece.getAttribute('data-at')),
      length: piece.textContent?.length ?? 0
    }))
  );
}

function merged(source: string, pieces: Piece[]): Span[] {
  const sorted = [...pieces].sort((a, b) => a.at - b.at);
  const ranges: Span[] = [];
  for (const piece of sorted) {
    const last = ranges[ranges.length - 1];
    const gap = last ? source.slice(last.end, piece.at) : null;
    if (last && (gap === '' || gap === '\n')) last.end = piece.at + piece.length;
    else ranges.push({ start: piece.at, end: piece.at + piece.length });
  }
  return ranges;
}

function offsetOf(source: string, needle: string): number {
  const at = source.indexOf(needle);
  if (at < 0) throw new Error(`"${needle}" is not in the source`);
  return at;
}

// The editor draws a window well past the viewport and re-renders as it scrolls,
// so a click at a source offset means: scroll until that line is really on screen,
// then re-find it and aim at its character box.
async function clickOffset(page: Page, source: string, offset: number): Promise<void> {
  const head = source.slice(0, offset);
  const line = head.split('\n').length;
  const column = offset - (head.lastIndexOf('\n') + 1);
  const total = source.split('\n').length;

  const settled = await page.evaluate(
    async ([line, total]) => {
      const scroller = document.querySelector('[data-testid="drawer-body"] .cm-scroller');
      if (!(scroller instanceof HTMLElement)) return false;
      const gutterFor = (wanted: number) =>
        Array.from(
          document.querySelectorAll('[data-testid="drawer-body"] .cm-gutterElement')
        ).find(
          (element) =>
            element.textContent?.trim() === String(wanted) &&
            element.getBoundingClientRect().height > 0
        );
      for (let tries = 0; tries < 25; tries++) {
        const box = scroller.getBoundingClientRect();
        const found = gutterFor(line)?.getBoundingClientRect();
        if (found && found.top >= box.top && found.bottom <= box.bottom) return true;
        const height = scroller.scrollHeight / total;
        scroller.scrollTop = found
          ? scroller.scrollTop + (found.top - box.top) - box.height / 3
          : Math.max(0, (line - 1) * height - box.height / 3);
        await new Promise((wake) => setTimeout(wake, 60));
      }
      return false;
    },
    [line, total] as const
  );
  if (!settled) throw new Error(`line ${line} never came into view`);
  await page.waitForTimeout(120);

  const point = await page.evaluate(
    ([line, column]) => {
      const content = document.querySelector('[data-testid="drawer-body"] .cm-content');
      const numbered = Array.from(
        document.querySelectorAll('[data-testid="drawer-body"] .cm-gutterElement')
      ).find(
        (element) =>
          element.textContent?.trim() === String(line) &&
          element.getBoundingClientRect().height > 0
      );
      if (!numbered || !(content instanceof HTMLElement)) return null;
      const middle = numbered.getBoundingClientRect().top + 1;
      const row = Array.from(content.children).find((element) => {
        const box = element.getBoundingClientRect();
        return box.top <= middle && box.bottom > middle;
      });
      if (!(row instanceof HTMLElement)) return null;

      const walker = document.createTreeWalker(row, NodeFilter.SHOW_TEXT);
      let seen = 0;
      for (let node = walker.nextNode(); node; node = walker.nextNode()) {
        const text = node as Text;
        if (seen + text.length <= column) {
          seen += text.length;
          continue;
        }
        const range = document.createRange();
        range.setStart(text, column - seen);
        range.setEnd(text, column - seen + 1);
        const box = range.getBoundingClientRect();
        return { x: box.left + box.width / 2, y: box.top + box.height / 2 };
      }
      const box = row.getBoundingClientRect();
      return { x: box.right - 1, y: box.top + box.height / 2 };
    },
    [line, column] as const
  );
  if (!point) throw new Error(`line ${line} never rendered`);
  // Click through the content locator so Playwright does its own hit test rather
  // than firing at raw viewport coordinates.
  const content = page.locator('[data-testid="drawer-body"] .cm-content');
  const box = await content.boundingBox();
  if (!box) throw new Error('the editor has no box');
  await content.click({ position: { x: point.x - box.x, y: point.y - box.y } });
  await page.waitForTimeout(200);
}

async function pickNode(page: Page, id: string): Promise<void> {
  await page.locator(`.svelte-flow__node[data-id="${id}"]`).dispatchEvent('click');
  await page.waitForTimeout(250);
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

describe('the bundled fixture sources stay in step with the parsed models', () => {
  for (const name of Object.keys(fixtures)) {
    it(name, async () => {
      const model = fixtures[name];
      const source = fixtureSource(name);
      const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(source));
      const hex = Array.from(new Uint8Array(digest))
        .map((byte) => byte.toString(16).padStart(2, '0'))
        .join('');
      expect(hex, `regenerate ${name}.json from ${model?.file}`).toBe(model?.sha256);
    });
  }
});

describe('the code drawer opens, persists and resizes', () => {
  it(
    'toggles from the canvas edge and remembers its state and height across a reload',
    async () => {
      const page = await boot();
      expect(await page.locator('[data-testid="code-drawer"]').count()).toBe(0);

      await openDrawer(page);
      expect(await drawerHeight(page)).toBeCloseTo(DEFAULT_HEIGHT, 0);
      expect(await page.locator('[data-testid="tab-code"]').count()).toBe(1);

      await page.reload({ waitUntil: 'load' });
      await page.waitForSelector('[data-testid="drawer-body"] .cm-line');
      await page.waitForTimeout(250);
      expect(await drawerHeight(page)).toBeCloseTo(DEFAULT_HEIGHT, 0);

      await page.click('[data-testid="drawer-close"]');
      await expect.poll(() => drawerHeight(page), { timeout: 2000 }).toBeLessThanOrEqual(1);
      await page.reload({ waitUntil: 'load' });
      await page.waitForSelector('.svelte-flow__node');
      await page.waitForTimeout(400);
      expect(await page.locator('[data-testid="code-drawer"]').count()).toBe(0);
      await page.close();
    },
    CASE
  );

  it(
    'resizes by dragging its top edge and keeps the new height',
    async () => {
      const page = await boot();
      await openDrawer(page);
      const grip = await page.locator('[data-testid="drawer-grip"]').boundingBox();
      if (!grip) throw new Error('no grip');

      await page.mouse.move(grip.x + grip.width / 2, grip.y + grip.height / 2);
      await page.mouse.down();
      await page.mouse.move(grip.x + grip.width / 2, grip.y - 120, { steps: 10 });
      await page.mouse.up();
      await page.waitForTimeout(250);
      expect(await drawerHeight(page)).toBeCloseTo(DEFAULT_HEIGHT + 120, -1);

      await page.reload({ waitUntil: 'load' });
      await page.waitForSelector('[data-testid="drawer-body"] .cm-line');
      await page.waitForTimeout(300);
      expect(await drawerHeight(page)).toBeCloseTo(DEFAULT_HEIGHT + 120, -1);
      await page.close();
    },
    CASE
  );

  it(
    'reports the read-only count of the open site',
    async () => {
      const page = await boot('adsb_receiver');
      await openDrawer(page);
      const reported = await page.textContent('[data-testid="drawer-readonly"]');
      const counted = reported?.trim().split(' ')[0] ?? '';
      await page.click('[data-testid="tab-diagnostics"]');
      const heading = await page.textContent('[data-testid="readonly-heading"]');
      expect(heading).toContain(`(${counted})`);
      expect(await page.locator('[data-diagnostic-note], [data-readonly-note]').count()).toBe(
        Number(counted)
      );
      await page.click('[data-testid="tab-code"]');
      expect(await page.locator('[data-testid="drawer-body"] .ro').count()).toBeGreaterThan(0);
      await page.close();
    },
    CASE
  );
});

describe('selection and the code stay in sync both ways', () => {
  it(
    'highlights the declaration and the runner of the selected block and scrolls to it',
    async () => {
      const page = await boot('uhd_device');
      await openDrawer(page);
      expect(await highlighted(page)).toEqual([]);

      await pickNode(page, 'spectrogram');

      const source = fixtureSource('uhd_device');
      const expected = blockSpans(siteOf('uhd_device', 0), 'spectrogram');
      expect(expected).toHaveLength(2);
      expect(merged(source, await highlighted(page))).toEqual(expected);
      expect(await scrollTop(page)).toBeGreaterThan(0);

      await pickNode(page, 'usrp_source');
      expect(merged(source, await highlighted(page))).toEqual(
        blockSpans(siteOf('uhd_device', 0), 'usrp_source')
      );
      await page.close();
    },
    CASE
  );

  it(
    'selects the block whose declaration or runner was clicked',
    async () => {
      const page = await boot();
      await openDrawer(page);
      const source = fixtureSource('hello_world');
      const site = siteOf('hello_world', 0);

      const declaration = site.blocks.find((block) => block.var === 'throttle');
      if (!declaration) throw new Error('no throttle');
      await clickOffset(page, source, declaration.span.start);
      await expect.poll(() => page.textContent('.inspector dd')).toBe('throttle');

      const runner = site.runners.find((entry) => entry.block === 'plot');
      if (!runner) throw new Error('no plot runner');
      await clickOffset(page, source, runner.span.start);
      await expect.poll(() => page.textContent('.inspector dd')).toBe('plot');

      const outside = offsetOf(source, '#include');
      await clickOffset(page, source, outside);
      expect(await page.textContent('.inspector dd')).toBe('plot');
      await page.close();
    },
    CASE
  );

  it(
    'follows a click into another site and switches the canvas to it',
    async () => {
      const page = await boot('uhd_device');
      await openDrawer(page);
      const source = fixtureSource('uhd_device');
      const chirp = siteOf('uhd_device', 1).blocks.find((block) => block.var === 'chirp');
      if (!chirp) throw new Error('no chirp');
      expect(targetAt(fixtures.uhd_device?.sites ?? [], chirp.span.start)).toEqual({
        siteIndex: 1,
        block: 'chirp'
      });

      await clickOffset(page, source, chirp.span.start);
      await page.waitForTimeout(400);
      await expect.poll(() => page.textContent('.inspector dd')).toBe('chirp');
      await expect
        .poll(() => page.locator('.svelte-flow__node[data-id="usrp_sink"]').count())
        .toBe(1);
      expect(await page.textContent('.sidebar select option:checked')).toContain('mode_tx_chirp');
      await page.close();
    },
    CASE
  );

  it(
    'keeps the scroll position across an edit and follows a span that moved',
    async () => {
      const page = await boot('uhd_device');
      await openDrawer(page);
      await pickNode(page, 'spectrogram');

      await page.evaluate(() => {
        const scroller = document.querySelector('[data-testid="drawer-body"] .cm-scroller');
        if (scroller instanceof HTMLElement) scroller.scrollTop = 0;
      });
      await page.waitForTimeout(150);

      const field = page.locator('input[data-field="spectrogram.ctor.0"]');
      await field.fill('"Waterfall Of A Very Different Length"');
      await field.blur();
      // The editor renders only what is in view, so the moved text is read from the
      // document rather than from the visible lines.
      await expect
        .poll(() => page.evaluate(() => (window as unknown as FakeWindow).__fake.source()))
        .toContain('Waterfall Of A Very Different Length');
      await page.waitForTimeout(250);
      expect(await scrollTop(page)).toBe(0);

      await page.evaluate(() =>
        (window as unknown as FakeWindow).__fake.prepend('// pushed down\n'.repeat(40))
      );
      await page.click('[data-testid="reload-banner"] button');
      await page.waitForTimeout(500);
      expect(await scrollTop(page)).toBeGreaterThan(0);
      expect(merged(await page.evaluate(() => (window as unknown as FakeWindow).__fake.source()),
        await highlighted(page)
      )).toHaveLength(2);
      await page.close();
    },
    CASE
  );
});

describe('the node context menu reaches the code', () => {
  it(
    'offers view, copy and open, and copies the declaration text',
    async () => {
      const page = await boot();
      await page.locator('.svelte-flow__node[data-id="adder"]').click({ button: 'right' });
      await page.waitForSelector('[data-testid="context-menu"]');
      expect(await page.locator('[data-testid="context-menu"] button').allTextContents()).toEqual([
        'View declarationCtrl+`',
        'Open block source…',
        'Copy declaration',
        'Open in editor',
        'Remove from graph',
        'Delete block…Del'
      ]);
      expect(await page.locator('[data-testid="menu-open-editor"]').isDisabled()).toBe(false);

      await page.click('[data-testid="menu-copy-declaration"]');
      const declaration = siteOf('hello_world', 0).blocks.find((block) => block.var === 'adder');
      if (!declaration) throw new Error('no adder');
      const source = fixtureSource('hello_world');
      await expect
        .poll(() => page.evaluate(() => navigator.clipboard.readText()))
        .toBe(source.slice(declaration.span.start, declaration.span.end));
      await page.close();
    },
    CASE
  );

  it(
    'opens the drawer on view source and scrolls to the declaration',
    async () => {
      const page = await boot('uhd_device');
      await page.locator('.svelte-flow__node[data-id="spectrogram"]').click({ button: 'right' });
      await page.click('[data-testid="menu-view-source"]');
      await page.waitForSelector('[data-testid="drawer-body"] .cm-line');
      await page.waitForTimeout(400);

      expect(await scrollTop(page)).toBeGreaterThan(0);
      expect(await page.textContent('.inspector dd')).toBe('spectrogram');
      expect(merged(fixtureSource('uhd_device'), await highlighted(page))).toEqual(
        blockSpans(siteOf('uhd_device', 0), 'spectrogram')
      );
      await page.close();
    },
    CASE
  );

  it(
    'asks the shell to open the file at the declaration line',
    async () => {
      const page = await boot('uhd_device');
      await page.locator('.svelte-flow__node[data-id="spectrum"]').click({ button: 'right' });
      await page.click('[data-testid="menu-open-editor"]');
      await page.waitForTimeout(300);

      const declaration = siteOf('uhd_device', 0).blocks.find((block) => block.var === 'spectrum');
      if (!declaration) throw new Error('no spectrum');
      const line = lineOfOffset(fixtureSource('uhd_device'), declaration.span.start);
      const launched = (await calls(page)).filter((call) => call.name === 'open_in_editor');
      expect(launched).toEqual([{ name: 'open_in_editor', args: { path: FAKE_PATH, line } }]);
      await page.close();
    },
    CASE
  );

  it(
    'keeps the pane menu unchanged',
    async () => {
      const page = await boot();
      await page
        .locator('.svelte-flow__pane')
        .click({ button: 'right', position: { x: 660, y: 760 } });
      await page.waitForSelector('[data-testid="context-menu"]');
      expect(
        await page.locator('[data-testid="context-menu"] button').evaluateAll((buttons) =>
          buttons.map((button) => button.getAttribute('data-testid'))
        )
      ).toEqual([
        'menu-add-here',
        'menu-check',
        'menu-build',
        'menu-run',
        'menu-save',
        'menu-save-as',
        'menu-undo',
        'menu-redo',
        'menu-fit'
      ]);
      await page.close();
    },
    CASE
  );
});

describe('the drawer in fixture mode', () => {
  it(
    'shows the bundled source, copies from it, and refuses the editor',
    async () => {
      const page = await browser.newPage({
        viewport: VIEWPORT,
        permissions: ['clipboard-read', 'clipboard-write']
      });
      page.setDefaultTimeout(STEP_TIMEOUT);
      await page.addInitScript(openInspector);
      await page.goto(`${origin}/?fixture=hello_world`, { waitUntil: 'load' });
      await page.waitForSelector('.svelte-flow__node');

      await page.locator('.svelte-flow__node[data-id="adder"]').click({ button: 'right' });
      await page.waitForSelector('[data-testid="context-menu"]');
      expect(await page.locator('[data-testid="menu-view-source"]').isDisabled()).toBe(false);
      expect(await page.locator('[data-testid="menu-copy-declaration"]').isDisabled()).toBe(false);
      expect(await page.locator('[data-testid="menu-open-editor"]').isDisabled()).toBe(true);
      expect(await page.getAttribute('[data-testid="menu-open-editor"]', 'title')).toContain(
        'desktop shell'
      );

      await page.click('[data-testid="menu-copy-declaration"]');
      const source = fixtureSource('hello_world');
      const declaration = siteOf('hello_world', 0).blocks.find((block) => block.var === 'adder');
      if (!declaration) throw new Error('no adder');
      await expect
        .poll(() => page.evaluate(() => navigator.clipboard.readText()))
        .toBe(source.slice(declaration.span.start, declaration.span.end));

      await openDrawer(page);
      expect(await page.textContent('[data-testid="drawer-body"]')).toContain('cler::BlockRunner');
      expect(
        await page.getAttribute('[data-testid="drawer-body"] .cm-content', 'contenteditable')
      ).toBe('false');

      await page.locator('.svelte-flow__node[data-id="adder"]').dispatchEvent('contextmenu');
      await page.waitForSelector('[data-testid="context-menu"]');
      await page.click('[data-testid="menu-view-source"]');
      await expect.poll(() => page.textContent('.inspector dd')).toBe('adder');
      expect(merged(source, await highlighted(page))).toEqual(
        blockSpans(siteOf('hello_world', 0), 'adder')
      );
      await page.close();
    },
    CASE
  );
});

describe('the drawer shortcut respects a field with focus', () => {
  it(
    'toggles on Ctrl+backtick but not while typing',
    async () => {
      const page = await boot();
      await page.locator('.svelte-flow__pane').click({ position: { x: 660, y: 760 } });
      await page.keyboard.press('Control+`');
      await page.waitForSelector('[data-testid="drawer-body"] .cm-line');
      await page.waitForTimeout(250);
      expect(await drawerHeight(page)).toBeCloseTo(DEFAULT_HEIGHT, 0);

      await page.keyboard.press('Control+`');
      await expect.poll(() => drawerHeight(page), { timeout: 2000 }).toBeLessThanOrEqual(1);

      await page.click('.svelte-flow__node[data-id="source1"]');
      const field = page.locator('input[data-field="source1.ctor.1"]');
      await field.click();
      await page.keyboard.press('Control+`');
      await page.waitForTimeout(400);
      expect(await drawerHeight(page)).toBeLessThanOrEqual(1);
      expect(await field.inputValue()).toBe('1.0f');
      await page.close();
    },
    CASE
  );
});

describe('the code drawer edits the file', () => {
  it(
    'commits what was typed, once the typing settles',
    async () => {
      const page = await boot();
      await openDrawer(page);
      const source = fixtureSource('hello_world');
      await clickOffset(page, source, offsetOf(source, 'int main()'));
      await page.keyboard.type('// typed by hand');

      await expect
        .poll(() => page.evaluate(() => (window as unknown as FakeWindow).__fake.source()))
        .toContain('// typed by hand');
      const commits = (await calls(page)).filter((call) => call.name === 'edit_source');
      expect(commits, 'one commit for the burst, not one per keystroke').toHaveLength(1);
      await page.close();
    },
    CASE
  );

  it(
    'says the graph is paused while the text does not parse, and refuses to save',
    async () => {
      const page = await boot();
      await openDrawer(page);
      const source = fixtureSource('hello_world');
      await clickOffset(page, source, offsetOf(source, 'int main()'));
      await page.keyboard.type('{');

      await page.waitForSelector('[data-testid="drawer-unparsed"]');
      const chip = await page.textContent('[data-testid="drawer-unparsed"]');
      expect(chip, 'the chip names where and what').toMatch(/line \d+/);
      expect(chip).toContain('expected `}`');
      expect(
        await page.locator('[data-testid="drawer-body"] .bad').count(),
        'the offending line is marked in the code'
      ).toBeGreaterThan(0);
      expect(
        await page.textContent('[data-testid="drawer-body"] .cm-content'),
        'the refused text stays in the buffer'
      ).toContain('{int main()');
      expect(await page.getAttribute('[data-testid="drawer-unparsed"]', 'title')).toContain(
        'does not parse'
      );

      await page.click('[data-testid="file-menu"]');
      await page.click('[data-testid="file-save"]');
      await expect.poll(() => page.textContent('[data-testid="status"]')).toContain('syntax error');
      expect((await calls(page)).filter((call) => call.name === 'save_document')).toHaveLength(0);

      // The graph must not move while the buffer it came from does not parse.
      await page.locator('.svelte-flow__node[data-id="adder"]').dispatchEvent('click');
      await page.fill('input[data-field="adder.display_name"]', 'Renamed');
      await page.keyboard.press('Enter');
      await expect.poll(() => page.textContent('[data-testid="status"]')).toContain('syntax error');
      expect((await calls(page)).filter((call) => call.name === 'apply_commands')).toHaveLength(0);

      await page.click('[data-testid="drawer-discard"]');
      await expect.poll(() => page.locator('[data-testid="drawer-unparsed"]').count()).toBe(0);
      expect(
        await page.textContent('[data-testid="drawer-body"] .cm-content'),
        'discarding puts the last parseable version back'
      ).not.toContain('{int main()');
      await page.close();
    },
    CASE
  );
});
