import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { beforeEach, describe, expect, it } from 'vitest';
import { fixtures } from '../src/fixtures';
import {
  applyCommands,
  describeApplyError,
  redoDocument,
  reloadDocument,
  saveCache,
  saveDocument,
  setInvoker,
  undoDocument
} from '../src/lib/backend';
import {
  blockFields,
  blurAction,
  configFields,
  keyAction,
  type Field,
  type FieldAction
} from '../src/lib/inspector';
import { layout } from '../src/lib/layout';
import { mergeProjection, projectSite } from '../src/lib/project';
import type {
  Block,
  Command,
  DocumentState,
  FileModel,
  Site,
  SiteConfig
} from '../src/lib/schema';

type Call = { command: string; args: Record<string, unknown> };

function model(name: string): FileModel {
  const found = fixtures[name];
  if (!found) throw new Error(`missing fixture ${name}`);
  return structuredClone(found);
}

function firstSite(name: string): Site {
  const site = model(name).sites[0];
  if (!site) throw new Error(`fixture ${name} has no sites`);
  return site;
}

function blockOf(site: Site, blockVar: string): Block {
  const found = site.blocks.find((candidate) => candidate.var === blockVar);
  if (!found) throw new Error(`no block ${blockVar}`);
  return found;
}

function fieldOf(fields: Field[], id: string): Field {
  const found = fields.find((field) => field.id === id);
  if (!found) throw new Error(`no field ${id}`);
  return found;
}

function record(reply: (call: Call) => unknown): Call[] {
  const calls: Call[] = [];
  setInvoker(async (command, args) => {
    calls.push({ command, args });
    return reply({ command, args });
  });
  return calls;
}

function documentState(next: FileModel): DocumentState {
  return {
    path: next.file,
    revision: 7,
    model: next,
    source: '',
    canUndo: true,
    canRedo: false,
    dirty: false,
    externalChange: false
  };
}

function field(site: Site, blockVar: string, id: string): Field {
  return fieldOf(blockFields(0, blockOf(site, blockVar)), id);
}

function editor(target: Field) {
  const commands: Command[] = [];
  let draft: string | undefined;

  function act(action: FieldAction) {
    if (action.kind === 'none') return;
    draft = undefined;
    if (action.kind === 'commit') commands.push(target.toCommand(action.text));
  }

  return {
    commands,
    value: () => draft ?? target.value,
    type(chars: string) {
      for (const character of chars) draft = (draft ?? target.value) + character;
    },
    replace(text: string) {
      draft = text;
    },
    press(key: string) {
      act(keyAction(key, draft, target.value));
    },
    blur() {
      act(blurAction(draft, target.value));
    }
  };
}

const APP = readFileSync(fileURLToPath(new URL('../src/App.svelte', import.meta.url)), 'utf8');

const MUTATORS = [
  'applyCommands',
  'moveNodes',
  'undoDocument',
  'redoDocument',
  'saveDocument',
  'saveDocumentAs',
  'reloadDocument'
];

function bodyOf(source: string, header: string): string {
  const start = source.indexOf(header);
  if (start < 0) throw new Error(`App.svelte no longer declares ${header}`);
  const rest = source.slice(start + header.length);
  const end = rest.indexOf('\n  }');
  if (end < 0) throw new Error(`cannot find the end of ${header}`);
  return rest.slice(0, end);
}

function mutatorLines(source: string): string[] {
  const body = source.slice(source.indexOf("} from './lib/backend';"));
  return body.split('\n').filter((line) => MUTATORS.some((name) => line.includes(name)));
}

describe('editability belongs to the document, not the environment', () => {
  it('opens the selected example as a real document in the desktop shell', () => {
    expect(APP).toContain('const editable = $derived(desktop && opened !== null);');
    expect(APP).not.toMatch(/const editable = inTauri\(\)/);
    expect(APP).toContain('void openExample(name);');
    expect(bodyOf(APP, 'async function openExample(name: string)')).toContain(
      'await openDocument(loadFixture(name).path)'
    );
  });

  it('routes every mutating backend call through the guarded attempt', () => {
    const lines = mutatorLines(APP);
    expect(lines.length).toBeGreaterThanOrEqual(MUTATORS.length);
    for (const line of lines) {
      expect(line, `unguarded backend call: ${line.trim()}`).toMatch(/\b(run|runHistory|attempt)\(/);
    }
  });

  it('refuses inside attempt, the one choke point undo, redo, reload and apply share', () => {
    expect(bodyOf(APP, 'async function attempt(')).toMatch(
      /\): Promise<Outcome> \{\n\s+if \(!editable\) return refuse\(viewerNote\);/
    );
  });

  it('keeps the file dialog and the editor launcher on the raw environment flag', () => {
    expect(bodyOf(APP, 'async function openFile()')).toContain('if (!desktop)');
    expect(bodyOf(APP, 'async function openEditor(blockVar: string)')).toContain('if (!desktop)');
  });
});

describe('transient field state machine', () => {
  it('dispatches exactly one command for four keystrokes plus a blur', () => {
    const target = field(firstSite('hello_world'), 'source1', 'source1.ctor.1');
    const input = editor(target);

    input.replace('');
    input.type('9.5f');
    expect(input.commands).toHaveLength(0);
    expect(input.value()).toBe('9.5f');

    input.blur();
    expect(input.commands).toEqual([
      { command: 'set_param', site: 0, block: 'source1', ctor_arg_index: 1, new_text: '9.5f' }
    ]);
  });

  it('reverts on Escape without dispatching', () => {
    const target = field(firstSite('hello_world'), 'source1', 'source1.ctor.1');
    const input = editor(target);

    input.type('junk');
    input.press('Escape');

    expect(input.commands).toHaveLength(0);
    expect(input.value()).toBe(target.value);
  });

  it('commits once on Enter and stays quiet on the blur that follows', () => {
    const target = field(firstSite('hello_world'), 'source1', 'source1.ctor.2');
    const input = editor(target);

    input.replace('2.0f');
    input.press('Enter');
    input.blur();

    expect(input.commands).toHaveLength(1);
    expect(input.commands[0]).toMatchObject({ ctor_arg_index: 2, new_text: '2.0f' });
  });

  it('sends nothing when the text is untouched or typed back to the model value', () => {
    const target = field(firstSite('hello_world'), 'source1', 'source1.ctor.1');
    const untouched = editor(target);
    untouched.blur();

    const restored = editor(target);
    restored.replace(target.value);
    restored.blur();

    expect(untouched.commands).toHaveLength(0);
    expect(restored.commands).toHaveLength(0);
  });

  it('keeps typing local to the field until it commits', () => {
    const site = firstSite('hello_world');
    const target = field(site, 'source1', 'source1.ctor.1');
    const input = editor(target);
    input.replace('3.3f');

    expect(blockOf(site, 'source1').ctor_args[1]?.text).toBe(target.value);
    expect(input.value()).toBe('3.3f');
  });
});

describe('field commands', () => {
  const site = firstSite('hello_world');

  it('builds set_display_name', () => {
    const target = field(site, 'adder', 'adder.display_name');
    expect(target.value).toBe('Adder');
    expect(target.toCommand('Summer')).toEqual({
      command: 'set_display_name',
      site: 0,
      block: 'adder',
      new_text: 'Summer'
    });
  });

  it('builds set_template_arg', () => {
    const target = field(site, 'adder', 'adder.template.1');
    expect(target.value).toBe('2');
    expect(target.toCommand('3')).toEqual({
      command: 'set_template_arg',
      site: 0,
      block: 'adder',
      template_arg_index: 1,
      new_text: '3'
    });
  });

  it('builds set_config from a site config', () => {
    const config: SiteConfig = {
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

    const rows = configFields(2, config);
    expect(rows.map((row) => row.id)).toEqual(['config.scheduler', 'config.adaptive_sleep']);
    expect(rows[0]?.toCommand('cler::SchedulerType::FixedThreadPool')).toEqual({
      command: 'set_config',
      site: 2,
      path: 'scheduler',
      new_value: 'cler::SchedulerType::FixedThreadPool'
    });
    expect(rows[1]?.editable).toBe(false);
    expect(rows[1]?.hint).toBe('config_from_factory');
  });
});

describe('read-only elements', () => {
  it('disables every field of a read-only block and keeps its reason', () => {
    const fields = blockFields(0, blockOf(firstSite('adsb_receiver'), 'source'));
    expect(fields.length).toBeGreaterThan(1);
    for (const target of fields) {
      expect(target.editable).toBe(false);
      expect(target.hint).toBe('optional_emplace_declaration');
    }
  });

  it('leaves editable blocks editable', () => {
    const fields = blockFields(0, blockOf(firstSite('adsb_receiver'), 'iq2mag'));
    expect(fields.every((target) => target.editable)).toBe(true);
  });

  it('refuses the display name when the block has no name argument', () => {
    const site = firstSite('hello_world');
    const block = blockOf(site, 'adder');
    block.display_name = null;
    const target = fieldOf(blockFields(0, block), 'adder.display_name');
    expect(target.editable).toBe(false);
    expect(target.hint).toBe('this block has no display-name argument');
  });
});

describe('apply error surfacing', () => {
  it('reads not_editable', () => {
    const message = describeApplyError(
      JSON.stringify({
        error: 'not_editable',
        element: 'block source',
        reason: 'optional_emplace_declaration'
      })
    );
    expect(message).toBe('block source is read-only: optional emplace declaration');
  });

  it('reads invalid_expression', () => {
    const message = describeApplyError(
      JSON.stringify({ error: 'invalid_expression', element: 'new_text', text: '1.0f;;' })
    );
    expect(message).toBe('"1.0f;;" is not a valid expression');
  });

  it('reads references_outside_graph as a sentence', () => {
    const message = describeApplyError(
      JSON.stringify({
        error: 'references_outside_graph',
        block: 'plot',
        spans: [
          { start: 10, end: 20 },
          { start: 30, end: 40 },
          { start: 50, end: 60 }
        ]
      })
    );
    expect(message).toBe(
      'plot is still used in 3 places outside the flowgraph — remove those references first'
    );
    expect(message).not.toContain('{');
  });

  it('falls back to the raw text for plain-string rejections', () => {
    const raw = 'hello_world.cpp changed on disk since the last write; reload before editing';
    expect(describeApplyError(raw)).toBe(raw);
    expect(describeApplyError(new Error(raw))).toBe(raw);
  });

  it('describes unmapped variants without leaking json', () => {
    const message = describeApplyError(
      JSON.stringify({ error: 'duplicate_variable', var_name: 'source1' })
    );
    expect(message).toBe('duplicate variable: source1');
  });
});

describe('ipc payloads', () => {
  beforeEach(() => {
    setInvoker(async () => null);
  });

  it('sends only the command objects, never a version or base revision', async () => {
    const next = model('hello_world');
    const calls = record(() => documentState(next));
    const command: Command = {
      command: 'set_param',
      site: 0,
      block: 'source1',
      ctor_arg_index: 1,
      new_text: '4.0f'
    };

    const state = await applyCommands('/tmp/hello_world.cpp', [command], 3);

    expect(calls).toHaveLength(1);
    expect(calls[0]?.command).toBe('apply_commands');
    expect(Object.keys(calls[0]?.args ?? {}).sort()).toEqual(['baseRevision', 'commands', 'path']);
    expect(calls[0]?.args.commands).toEqual([command]);
    expect(calls[0]?.args.baseRevision).toBe(3);
    expect(JSON.stringify(calls[0]?.args)).not.toContain('base_revision');
    expect(JSON.stringify(calls[0]?.args)).not.toContain('version');
    expect(state.revision).toBe(7);
  });

  it('uses the contract command names for history, save, cache and reload', async () => {
    const next = model('hello_world');
    const calls = record(() => documentState(next));

    await undoDocument('/tmp/a.cpp');
    await redoDocument('/tmp/a.cpp');
    await saveDocument('/tmp/a.cpp');
    await saveCache('/tmp/a.cpp', { version: 1 });
    await reloadDocument('/tmp/a.cpp');

    expect(calls.map((call) => call.command)).toEqual([
      'undo',
      'redo',
      'save_document',
      'save_cache',
      'reload_document'
    ]);
    expect(calls[0]?.args).toEqual({ path: '/tmp/a.cpp' });
    expect(calls[1]?.args).toEqual({ path: '/tmp/a.cpp' });
    expect(calls[2]?.args).toEqual({ path: '/tmp/a.cpp' });
    expect(calls[3]?.args).toEqual({ path: '/tmp/a.cpp', ui: { version: 1 } });
    expect(calls[4]?.args).toEqual({ path: '/tmp/a.cpp' });
  });
});

describe('re-render from the returned document state', () => {
  it('shows the mutated model on the canvas and keeps the laid-out positions', async () => {
    const before = model('hello_world');
    const beforeSite = before.sites[0];
    if (!beforeSite) throw new Error('fixture has no sites');
    const laid = await layout(projectSite(beforeSite));

    const after = structuredClone(before);
    const target = after.sites[0]?.blocks.find((block) => block.var === 'source1');
    const arg = target?.ctor_args[1];
    if (!target || !arg) throw new Error('fixture shape changed');
    arg.text = '99.0f';
    target.display_name = 'Renamed Source';

    record(() => documentState(after));
    const state = await applyCommands(
      before.file,
      [{ command: 'set_param', site: 0, block: 'source1', ctor_arg_index: 1, new_text: '99.0f' }],
      0
    );

    const nextSite = state.model.sites[0];
    if (!nextSite) throw new Error('returned state has no sites');
    const merged = mergeProjection(laid, projectSite(nextSite));

    const node = merged.nodes.find((candidate) => candidate.id === 'source1');
    const original = laid.nodes.find((candidate) => candidate.id === 'source1');
    expect(node?.position).toEqual(original?.position);
    expect(node?.data.block.ctor_args[1]?.text).toBe('99.0f');
    expect(node?.data.block.display_name).toBe('Renamed Source');

    expect(merged.edges.map((edge) => edge.id)).toEqual(laid.edges.map((edge) => edge.id));
    for (const edge of merged.edges) {
      const kept = laid.edges.find((candidate) => candidate.id === edge.id);
      expect(edge.data?.bends).toEqual(kept?.data?.bends);
    }

    const refreshed = field(nextSite, 'source1', 'source1.ctor.1');
    expect(refreshed.value).toBe('99.0f');
  });

  it('drops positions for blocks that no longer exist', () => {
    const site = firstSite('hello_world');
    const previous = projectSite(site);
    const moved = {
      nodes: previous.nodes.map((node) => ({ ...node, position: { x: 11, y: 22 } })),
      edges: previous.edges
    };
    const trimmed = {
      nodes: previous.nodes.filter((node) => node.id !== 'plot'),
      edges: previous.edges.filter((edge) => edge.target !== 'plot')
    };

    const merged = mergeProjection(moved, trimmed);
    expect(merged.nodes.map((node) => node.id)).not.toContain('plot');
    for (const node of merged.nodes) expect(node.position).toEqual({ x: 11, y: 22 });
  });
});
