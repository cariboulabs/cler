import { chromium, type Browser, type Page } from 'playwright';
import { afterAll, beforeAll, expect } from 'vitest';
import { createServer, type ViteDevServer } from 'vite';
import { fixtures, fixtureSources } from '../src/fixtures';
import type { BlockSpec } from '../src/lib/palette';
import type { Command, FileModel } from '../src/lib/schema';
import shipped from './palette.json';

const BOOT_TIMEOUT = 180_000;
export const CASE = 90_000;
const VIEWPORT = { width: 1440, height: 900 };
export const FAKE_PATH = '/tmp/fake/hello_world.cpp';
const SHOTS = (globalThis as { process?: { env: Record<string, string | undefined> } }).process
  ?.env.CLER_SHOTS;
export const specs = shipped.blocks as unknown as BlockSpec[];

export type FakeTarget = {
  available: boolean;
  reason: string | null;
  name: string;
  buildDir: string | null;
  binary: string | null;
  artifact:
    | { state: 'unavailable'; reason: string }
    | { state: 'needs_build'; reason: string }
    | { state: 'ready'; artifactPath: string };
};

export type FakeAssistant = { available: boolean; model: string; reason: string | null };

export type Ask = { path: string; question: string; history: { role: string; text: string }[] };

export type Peek = { commands: unknown[]; baseRevision: number };

export const SAMPLE_DIFF = [
  '@@ -12,3 +12,3 @@',
  '     const size_t SPS = 1000;',
  '-    SourceCWBlock<float> source1("CWSource", 1.0f, 1.0f, SPS);',
  '+    SourceCWBlock<float> source1("Chirp", 1.0f, 1.0f, SPS);',
  '     SourceCWBlock<float> source2("CWSource2", 1.0f, 20.0f, SPS);',
  ''
].join('\n');

type Setup = {
  path: string;
  model: FileModel;
  source: string;
  specs: BlockSpec[];
  refusal: unknown;
  openError: string | null;
  target: FakeTarget | null;
  assistant: FakeAssistant;
  askError: string | null;
  previewError: string | null;
  diff: string;
};

type Fake = {
  log: Command[][];
  calls: string[];
  runs: unknown[];
  editors: { path: string; line: number }[];
  asks: Ask[];
  peeks: Peek[];
  bases: number[];
  source: () => string;
  refuse: (value: unknown) => void;
  hold: () => void;
  release: () => void;
  emit: (event: string, payload: unknown) => void;
};

export type FakeWindow = { __fake: Fake };

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
    dirty: false,
    externalChange: false,
    cache: {} as Record<string, unknown>
  };
  type HistoricalState = {
    model: FileModel;
    source: string;
    cache: Record<string, unknown>;
    dirty: boolean;
  };
  enum HistoryKind {
    Source,
    Position
  }
  type HistoryAction = {
    kind: HistoryKind;
    before: HistoricalState;
    after: HistoricalState;
  };
  const palette = setup.specs.map((spec) => structuredClone(spec));
  const undone: HistoryAction[] = [];
  const redone: HistoryAction[] = [];
  const log: unknown[][] = [];
  const calls: string[] = [];
  const runs: unknown[] = [];
  const editors: { path: string; line: number }[] = [];
  const pristine = structuredClone({ model: setup.model, source: setup.source });
  const asks: unknown[] = [];
  const peeks: unknown[] = [];
  const bases: number[] = [];
  const callbacks = new Map<number, (message: unknown) => void>();
  const listeners: { event: string; handler: number }[] = [];
  let target = structuredClone(setup.target);
  let nextJob = 1;
  const jobs = new Map<string, { jobId: number; inputKey: object }>();
  let refusal: unknown = setup.refusal;
  let nextCallback = 1;
  let gate: { waited: Promise<void>; open: () => void } | null = null;

  type Loose = Record<string, unknown>;

  function snapshot(): unknown {
    return JSON.parse(JSON.stringify(state));
  }

  function historicalState(): HistoricalState {
    return structuredClone({
      model: state.model,
      source: state.source,
      cache: state.cache,
      dirty: state.dirty
    });
  }

  function restore(saved: HistoricalState): void {
    state.model = saved.model;
    state.source = saved.source;
    state.cache = saved.cache;
    state.dirty = saved.dirty;
  }

  function record(kind: HistoryKind, before: HistoricalState): void {
    undone.push({ kind, before, after: historicalState() });
    redone.length = 0;
    state.canUndo = true;
    state.canRedo = false;
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

  function define(command: Loose) {
    const name = String(command.name);
    const value = String(command.value_type);
    const inputs = (command.inputs ?? []) as Loose[];
    const outputs = Number(command.outputs ?? 0);
    const params = (command.params ?? []) as Loose[];
    const port = (label: string, direction: string) => ({
      name: label,
      direction,
      element_type: value,
      variable: false
    });
    palette.unshift({
      name,
      origin: state.path,
      synonyms: [],
      template_params: [],
      ctor_params: [
        { name: 'name', param_type: 'const char*', default: null },
        ...params.map((entry) => ({
          name: String(entry.name),
          param_type: String(entry.cpp_type),
          default: (entry.default ?? null) as string | null
        }))
      ],
      may_block: command.may_block === true,
      conditional_members: false,
      ports: [
        ...inputs.map((entry) => port(String(entry.name), 'input')),
        ...Array.from({ length: outputs }, (_, index) => port(`out${index}`, 'output'))
      ],
      input_count: { fixed: inputs.length },
      output_count: { fixed: outputs }
    } as unknown as BlockSpec);
    const at = state.source.indexOf('int main()');
    const text = `struct ${name} : public cler::BlockBase {\n    // TODO: implement\n};\n\n`;
    splice({ start: at, end: at }, text);
  }

  function apply(commands: Loose[]) {
    const before = historicalState();
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
      } else if (command.command === 'define_block') {
        define(command);
      } else if (command.command === 'delete_block') {
        const name = String(command.block);
        site(at).blocks = blocks(at).filter((entry) => entry.var !== name);
        site(at).runners = runners(at).filter((runner) => runner.block !== name);
        site(at).edges = edges(at).filter((edge) => edge.from !== name && edge.to !== name);
        restate(at);
      }
    }
    state.revision += 1;
    state.dirty = true;
    record(HistoryKind.Source, before);
  }

  function step(direction: 'undo' | 'redo') {
    const from = direction === 'undo' ? undone : redone;
    const to = direction === 'undo' ? redone : undone;
    const action = from.pop();
    if (action) {
      restore(direction === 'undo' ? action.before : action.after);
      to.push(action);
      if (action.kind === HistoryKind.Source) state.revision += 1;
    }
    state.canUndo = undone.length > 0;
    state.canRedo = redone.length > 0;
  }

  async function invoke(command: string, args: Record<string, unknown>): Promise<unknown> {
    if (command === 'plugin:event|listen') {
      listeners.push({ event: String(args.event), handler: args.handler as number });
      return args.handler;
    }
    if (command === 'plugin:dialog|open') return state.path;
    if (command === 'plugin:dialog|save') return state.path.replace(/\.cpp$/, '_copy.cpp');
    if (command === 'save_cache') {
      state.cache = args.ui as Record<string, unknown>;
      return null;
    }
    calls.push(command);
    if (command === 'app_settings') return { clerRoot: null, blockLibraries: [] };
    if (command === 'set_app_settings') return (args as Loose).next;
    if (command === 'run_target') runs.push(args.args);
    if (command === 'assistant_status') return setup.assistant;
    if (command === 'assistant_stop') return null;
    if (command === 'assistant_ask') {
      asks.push({
        path: args.path,
        question: args.question,
        history: args.history
      });
      if (setup.askError !== null) throw setup.askError;
      return null;
    }
    if (command === 'preview_commands') {
      peeks.push({ commands: args.commands, baseRevision: args.baseRevision });
      if (setup.previewError !== null) throw setup.previewError;
      return { diff: setup.diff, summary: { splices: 1 } };
    }
    if (command === 'open_document' && setup.openError !== null) throw setup.openError;
    if (command === 'palette') return JSON.parse(JSON.stringify(palette));
    if (command === 'find_target') {
      if (target === null) throw 'that file is not inside a cler repository';
      return target;
    }
    if (['check_document', 'build_target', 'run_target', 'stop_target'].includes(command)) {
      if (command === 'stop_target') return null;
      const kind = command.split('_')[0];
      const started = {
        jobId: nextJob++,
        inputKey: { inputs: { draft: String(state.revision) }, recipeSha256: 'fake' }
      };
      jobs.set(kind, started);
      if (command === 'build_target' && target?.available) {
        target.artifact = { state: 'building', jobId: started.jobId };
        for (const entry of listeners) {
          if (entry.event !== 'artifact-status-changed') continue;
          callbacks.get(entry.handler)?.({
            event: entry.event,
            id: entry.handler,
            payload: { path: state.path }
          });
        }
      }
      return started;
    }
    if (command === 'apply_commands') {
      const commands = args.commands as Loose[];
      bases.push(args.baseRevision as number);
      if (gate) await gate.waited;
      if (refusal !== null && refusal !== undefined) {
        const thrown = refusal;
        refusal = null;
        log.push(commands);
        throw thrown;
      }
      apply(commands);
      if (target?.available) {
        target.artifact = {
          state: 'needs_build',
          reason: 'build the current draft before running'
        };
      }
      return snapshot();
    }
    if (command === 'move_nodes') {
      const before = historicalState();
      const views = (state.cache.views ??= {}) as Loose;
      const view = (views[String(args.view)] ??= {}) as Loose;
      const positions = (view.positions ??= {}) as Loose;
      for (const movement of args.moves as { node: string; to: { x: number; y: number } }[]) {
        positions[movement.node] = movement.to;
      }
      record(HistoryKind.Position, before);
      return snapshot();
    }
    if (command === 'undo') step('undo');
    if (command === 'redo') step('redo');
    if (command === 'save_document') state.dirty = false;
    if (command === 'save_document_as') {
      state.path = String((args as Loose).newPath);
      state.dirty = false;
      state.revision += 1;
    }
    if (command === 'new_document') {
      state.model = structuredClone(pristine.model);
      state.source = pristine.source;
      state.path = String((args as Loose).path);
      state.dirty = false;
      state.canUndo = false;
      state.canRedo = false;
      state.revision += 1;
    }
    if (command === 'reload_document') {
      state.model = structuredClone(pristine.model);
      state.source = pristine.source;
      state.dirty = false;
      state.canUndo = false;
      state.canRedo = false;
      state.revision += 1;
    }
    if (command === 'close_document') return null;
    if (command === 'open_in_editor') {
      editors.push({ path: String((args as Loose).path), line: Number((args as Loose).line) });
      return null;
    }
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
    runs,
    editors,
    asks,
    peeks,
    bases,
    source: () => state.source,
    refuse: (value: unknown) => {
      refusal = value;
    },
    hold: () => {
      let open = () => undefined as void;
      const waited = new Promise<void>((resolve) => (open = resolve));
      gate = { waited, open };
    },
    release: () => {
      gate?.open();
      gate = null;
    },
    emit: (event: string, payload: unknown) => {
      const kind = event.split('-')[0] ?? '';
      const started = jobs.get(kind);
      const enriched = {
        ...(payload as Record<string, unknown>),
        jobId: (payload as Record<string, unknown>).jobId ?? started?.jobId ?? 0,
        inputKey:
          (payload as Record<string, unknown>).inputKey ?? started?.inputKey ?? {
            inputs: {},
            recipeSha256: 'fake'
          }
      };
      if (event === 'build-finished' && enriched.code === 0 && target?.available) {
        target.artifact = {
          state: 'ready',
          artifactPath: target.binary ?? '/tmp/fake/build/hello_world'
        };
      }
      for (const entry of listeners) {
        if (entry.event !== event) continue;
        callbacks.get(entry.handler)?.({ event, id: entry.handler, payload: enriched });
      }
    }
  };
}

/* ---------------------------------------------------------------- harness */

export function modelOf(name: string): FileModel {
  const base = structuredClone(fixtures[name]);
  if (!base) throw new Error(`missing fixture ${name}`);
  base.file = FAKE_PATH;
  return base;
}

export function sourceOf(name: string): string {
  const found = fixtureSources[name];
  if (found === undefined) throw new Error(`missing source ${name}`);
  return found;
}

export function withoutEdge(model: FileModel, from: string, to: string): FileModel {
  const site = model.sites[0];
  if (!site) throw new Error('no site');
  site.edges = site.edges.filter((edge) => !(edge.from === from && edge.to === to));
  return model;
}

export type BootOptions = {
  fixture?: string;
  specs?: BlockSpec[];
  model?: FileModel;
  refusal?: unknown;
  openError?: string;
  empty?: boolean;
  target?: FakeTarget | null;
  assistant?: FakeAssistant;
  askError?: string;
  previewError?: string;
  diff?: string;
};

export const ASSISTANT_READY: FakeAssistant = {
  available: true,
  model: 'claude-opus-5',
  reason: null
};

export const NO_KEY: FakeAssistant = {
  available: false,
  model: 'claude-opus-5',
  reason:
    'no Anthropic API key — export ANTHROPIC_API_KEY before starting the editor, or write the key into /home/pilot/.config/dev.cler.flowgraph-gui/anthropic-key and chmod 600 it'
};

export const NO_TARGET: FakeTarget = {
  available: false,
  reason: 'only files under desktop_examples/ have a cmake target',
  name: 'hello_world',
  buildDir: null,
  binary: null,
  artifact: {
    state: 'unavailable',
    reason: 'only files under desktop_examples/ have a cmake target'
  }
};

export const BUILDABLE: FakeTarget = {
  available: true,
  reason: null,
  name: 'hello_world',
  buildDir: '/tmp/fake/build',
  binary: '/tmp/fake/build/desktop_examples/hello_world',
  artifact: { state: 'needs_build', reason: 'build the current draft before running' }
};

export function openInspector(): void {
  if (localStorage.getItem('cler.panel.right') === null) {
    localStorage.setItem('cler.panel.right', 'open');
  }
}

export async function boot(options: BootOptions = {}): Promise<Page> {
  const name = options.fixture ?? 'hello_world';
  const page = await browser.newPage({ viewport: VIEWPORT });
  await page.addInitScript(installFake, {
    path: FAKE_PATH,
    model: options.model ?? modelOf(name),
    source: sourceOf(name),
    specs: options.specs ?? specs,
    refusal: options.refusal ?? null,
    openError: options.openError ?? null,
    target: options.target === undefined ? NO_TARGET : options.target,
    assistant: options.assistant ?? ASSISTANT_READY,
    askError: options.askError ?? null,
    previewError: options.previewError ?? null,
    diff: options.diff ?? SAMPLE_DIFF
  });
  await page.addInitScript(openInspector);
  await page.goto(origin, { waitUntil: 'load' });
  if (options.empty === true) {
    await page.waitForSelector('[data-testid="empty-state"]');
    return page;
  }
  await page.waitForSelector(`.path[title="${FAKE_PATH}"]`);
  await page.waitForSelector('.svelte-flow__node');
  return page;
}

export const sent = (page: Page) => page.evaluate(() => (window as unknown as FakeWindow).__fake.log);

export const commands = async (page: Page) => (await sent(page)).flat();

export const calls = (page: Page) =>
  page.evaluate(() => (window as unknown as FakeWindow).__fake.calls);

export const asks = (page: Page) =>
  page.evaluate(() => (window as unknown as FakeWindow).__fake.asks);

export const peeks = (page: Page) =>
  page.evaluate(() => (window as unknown as FakeWindow).__fake.peeks);

export const bases = (page: Page) =>
  page.evaluate(() => (window as unknown as FakeWindow).__fake.bases);

export const hold = (page: Page) =>
  page.evaluate(() => (window as unknown as FakeWindow).__fake.hold());

export const release = (page: Page) =>
  page.evaluate(() => (window as unknown as FakeWindow).__fake.release());

export const emit = (page: Page, event: string, payload: unknown) =>
  page.evaluate(
    ([name, sent]) =>
      (window as unknown as FakeWindow).__fake.emit(name as string, sent),
    [event, payload] as [string, unknown]
  );

export async function highlighted(page: Page): Promise<string> {
  const pieces = await page.locator('[data-testid="code-drawer"] .hit').allTextContents();
  return pieces.join('');
}

export async function shot(page: Page, name: string) {
  if (!SHOTS) return;
  await page.screenshot({ path: `${SHOTS}/${name}.png` });
}

export function handle(node: string, id: string): string {
  return `.svelte-flow__node[data-id="${node}"] .svelte-flow__handle[data-handleid="${id}"]`;
}

export function anchor(edge: string, end: 'source' | 'target'): string {
  return `[data-anchor^="${end}:${edge}"]`;
}

export function wire(from: string, to: string): string {
  return `.svelte-flow__edge[data-id^="${from}->${to}"] .svelte-flow__edge-interaction`;
}

export async function paneOrigin(page: Page): Promise<{ x: number; y: number }> {
  const box = await page.locator('.svelte-flow__pane').boundingBox();
  if (!box) throw new Error('no pane box');
  return { x: box.x, y: box.y };
}

export async function centre(page: Page, selector: string): Promise<{ x: number; y: number }> {
  const box = await page.locator(selector).boundingBox();
  if (!box) throw new Error(`${selector} has no box`);
  return { x: box.x + box.width / 2, y: box.y + box.height / 2 };
}

export async function dragWire(page: Page, from: string, to: string) {
  const start = await centre(page, from);
  const end = await centre(page, to);
  await page.mouse.move(start.x, start.y);
  await page.mouse.down();
  await page.mouse.move(start.x + 25, start.y, { steps: 4 });
  await page.mouse.move(end.x, end.y, { steps: 12 });
  await page.mouse.up();
}

export async function dragBlock(page: Page, name: string, at: { x: number; y: number }) {
  await page
    .locator(`[data-block="${name}"]`)
    .dragTo(page.locator('.svelte-flow__pane'), { targetPosition: at });
}

export async function openLibrary(page: Page) {
  const tab = page.locator('[data-testid="rail-tab-library"]');
  if (!(await tab.isVisible())) {
    await page
      .locator(
        '[data-testid="toggle-right"], [data-testid="toggle-library"], [data-testid="toggle-assistant"]'
      )
      .click();
  }
  await tab.click();
  await page.waitForSelector('[data-testid="palette"] .folder');
}

export async function openMenu(page: Page, selector: string, position?: { x: number; y: number }) {
  await page.locator(selector).click({ button: 'right', position });
  await page.waitForSelector('[data-testid="context-menu"]');
}

export function menuIds(page: Page): Promise<(string | null)[]> {
  return page
    .locator('[data-testid="context-menu"] button')
    .evaluateAll((buttons) => buttons.map((button) => button.getAttribute('data-testid')));
}

export function useBrowser(): void {
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
}

export async function viewer(query: string, panels: 'stored' | 'first-run' = 'stored'): Promise<Page> {
  const page = await browser.newPage({ viewport: VIEWPORT });
  if (panels === 'stored') await page.addInitScript(openInspector);
  await page.goto(`${origin}/${query}`, { waitUntil: 'load' });
  return page;
}

export async function token(page: Page, name: string): Promise<string> {
  const hex = await page.evaluate(
    (which) => getComputedStyle(document.documentElement).getPropertyValue(which).trim(),
    name
  );
  const value = hex.replace('#', '');
  const channel = (at: number) => parseInt(value.slice(at, at + 2), 16);
  return `rgb(${channel(0)}, ${channel(2)}, ${channel(4)})`;
}

export function styleOf(page: Page, selector: string, property: string): Promise<string> {
  return page.evaluate(
    ([where, which]) => {
      const element = document.querySelector(where ?? '');
      if (!element) throw new Error(`nothing matches ${where}`);
      return getComputedStyle(element).getPropertyValue(which ?? '');
    },
    [selector, property]
  );
}

export function zoomOf(page: Page): Promise<number> {
  return page.evaluate(() => {
    const viewport = document.querySelector('.svelte-flow__viewport');
    if (!(viewport instanceof HTMLElement)) throw new Error('no flow viewport');
    return Number(/scale\(([\d.]+)\)/.exec(viewport.style.transform)?.[1] ?? 0);
  });
}

export async function settledZoom(page: Page): Promise<number> {
  let last = -1;
  await expect
    .poll(
      async () => {
        const now = await zoomOf(page);
        const stable = now > 0 && Math.abs(now - last) < 1e-6;
        last = now;
        return stable;
      },
      { interval: 250, timeout: 8000 }
    )
    .toBe(true);
  return last;
}

export function widthOf(page: Page, selector: string): Promise<number> {
  return page
    .locator(selector)
    .boundingBox()
    .then((box) => box?.width ?? -1);
}
