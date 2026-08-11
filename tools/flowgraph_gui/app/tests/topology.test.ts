import { describe, expect, it } from 'vitest';
import { fixtures } from '../src/fixtures';
import shipped from './palette.json';
import { blockFields } from '../src/lib/inspector';
import {
  addBlockCommand,
  addForm,
  addRefusal,
  braceListLength,
  categoryOf,
  connectPlan,
  countOf,
  ctorSignature,
  blockIsValid,
  initialBlockArguments,
  isRequiredArgumentPlaceholder,
  missingRequiredFields,
  REQUIRED_ARGUMENT_PLACEHOLDER,
  portsSummary,
  reconnectPlan,
  renameInForm,
  searchSpecs,
  specFor,
  specOfBlock,
  specsFromSites,
  suggestVarName,
  type BlockSpec
} from '../src/lib/palette';
import {
  edgeAtId,
  edgeIndexById,
  mergeProjection,
  parsePortId,
  problemsOf,
  projectSite
} from '../src/lib/project';
import type { Block, Command, Site, Unresolved } from '../src/lib/schema';

const specs = shipped.blocks as unknown as BlockSpec[];

function siteOf(name: string, index = 0): Site {
  const site = structuredClone(fixtures[name])?.sites[index];
  if (!site) throw new Error(`fixture ${name} has no site ${index}`);
  return site;
}

function blockOf(site: Site, blockVar: string): Block {
  const found = site.blocks.find((candidate) => candidate.var === blockVar);
  if (!found) throw new Error(`no block ${blockVar}`);
  return found;
}

function spec(name: string): BlockSpec {
  const found = specFor(specs, name);
  if (!found) throw new Error(`no spec ${name}`);
  return found;
}

function dropEdge(site: Site, from: string, to: string, index: number | null): Site {
  site.edges = site.edges.filter(
    (edge) => !(edge.from === from && edge.to === to && edge.port.index === index)
  );
  return site;
}

function commandsOf(plan: ReturnType<typeof connectPlan>): Command[] {
  if ('refusal' in plan) throw new Error(`refused: ${plan.refusal}`);
  return plan.commands;
}

function refusalOf(plan: ReturnType<typeof connectPlan>): string {
  if (!('refusal' in plan)) throw new Error(`expected a refusal, got ${JSON.stringify(plan)}`);
  return plan.refusal;
}

describe('the palette reads the crate specs', () => {
  it('derives a category from the origin path and names the open file', () => {
    expect(categoryOf(spec('GainBlock'), '/tmp/x.cpp')).toBe('math');
    expect(categoryOf(spec('SourceCWBlock'), '/tmp/x.cpp')).toBe('sources');
    expect(categoryOf(spec('PlotTimeSeriesBlock'), '/tmp/x.cpp')).toBe('plots');
    expect(categoryOf(spec('FanoutBlock'), '/tmp/x.cpp')).toBe('utils');
    const local = spec('PlantBlock');
    expect(categoryOf(local, local.origin)).toBe('this file');
  });

  it('searches by name, category, synonym and sample type', () => {
    const byName = searchSpecs(specs, 'fanout', '');
    expect(byName.map((entry) => entry.name)).toEqual(['FanoutBlock']);
    expect(searchSpecs(specs, 'plots', '').every((entry) => entry.origin.includes('/plots/'))).toBe(
      true
    );
    expect(searchSpecs(specs, 'zzzz', '')).toHaveLength(0);
    expect(searchSpecs(specs, '', '')).toHaveLength(specs.length);
  });

  it('sorts by category then name', () => {
    const listed = searchSpecs(specs, '', '/tmp/x.cpp').map((entry) => [
      categoryOf(entry, '/tmp/x.cpp'),
      entry.name
    ]);
    const sorted = [...listed].sort((left, right) =>
      left[0] === right[0]
        ? String(left[1]).localeCompare(String(right[1]), 'en')
        : String(left[0]).localeCompare(String(right[0]), 'en')
    );
    expect(listed).toEqual(sorted);
  });

  it('summarises ports and previews the constructor', () => {
    expect(portsSummary(spec('GainBlock'))).toBe('1 in · 1 out');
    expect(portsSummary(spec('AddBlock'))).toBe('n in · 1 out');
    expect(portsSummary(spec('FanoutBlock'))).toBe('1 in · n out');
    expect(portsSummary(spec('SourceCWBlock'))).toBe('0 in · 1 out');
    expect(ctorSignature(spec('FanoutBlock'))).toBe(
      'FanoutBlock<T>(const char* name, const size_t num_outputs, const size_t buffer_size = 0)'
    );
    expect(ctorSignature(spec('PlotTimeSeriesBlock'))).toContain('std::vector<std::string>');
  });

  it('marks the blocks that may block', () => {
    expect(specs.some((entry) => entry.may_block)).toBe(true);
    expect(spec('GainBlock').may_block).toBe(false);
  });

  it('degrades to the site blocks when there is no backend palette', () => {
    const derived = specsFromSites(fixtures.hello_world?.sites ?? [], '/tmp/hello_world.cpp');
    expect(derived.map((entry) => entry.name).sort()).toEqual([
      'AddBlock',
      'PlotTimeSeriesBlock',
      'SourceCWBlock',
      'ThrottleBlock'
    ]);
    const add = derived.find((entry) => entry.name === 'AddBlock');
    expect(add?.origin).toBe('/tmp/hello_world.cpp');
    expect(add?.input_count).toBe('unknown');
    expect(add?.ports.some((port) => port.direction === 'output')).toBe(true);
  });
});

describe('port-count authorities read the instance', () => {
  const hello = siteOf('hello_world');
  const msd = siteOf('mass_spring_damper');

  it('reads a template argument', () => {
    expect(countOf(blockOf(hello, 'adder'), spec('AddBlock').input_count)).toBe(2);
  });

  it('reads a constructor argument', () => {
    expect(countOf(blockOf(msd, 'fanout'), spec('FanoutBlock').output_count)).toBe(2);
  });

  it('reads a label-vector length', () => {
    expect(countOf(blockOf(hello, 'plot'), spec('PlotTimeSeriesBlock').input_count)).toBe(1);
    expect(braceListLength('{"Real", "Imaginary"}')).toBe(2);
    expect(braceListLength('{}')).toBe(0);
    expect(braceListLength('{f(a, b), g}')).toBe(2);
    expect(braceListLength('SPS')).toBe(null);
  });

  it('reads a fixed count and refuses to guess an unknown one', () => {
    expect(countOf(blockOf(hello, 'source1'), spec('SourceCWBlock').output_count)).toBe(1);
    expect(countOf(blockOf(hello, 'source1'), 'unknown')).toBe(null);
  });

  it('matches a block to its spec through its type name', () => {
    expect(specOfBlock(specs, blockOf(hello, 'adder'))?.name).toBe('AddBlock');
    expect(specOfBlock(specs, blockOf(msd, 'plant'))?.name).toBe('PlantBlock');
  });
});

describe('required block fields', () => {
  it('creates parser-safe placeholders and still treats them as missing', () => {
    const sourceSpec = spec('SourceCWBlock');
    const block = structuredClone(blockOf(siteOf('hello_world'), 'source1'));
    const initial = initialBlockArguments(sourceSpec);
    block.template_args = initial.templateArgs.map((text) => ({
      text,
      resolved: null,
      span: { start: 0, end: 0 }
    }));
    block.ctor_args = initial.ctorArgs.map((text) => ({ text, span: { start: 0, end: 0 } }));

    expect(initial).toEqual({
      templateArgs: [REQUIRED_ARGUMENT_PLACEHOLDER],
      ctorArgs: Array(4).fill(REQUIRED_ARGUMENT_PLACEHOLDER)
    });
    expect(initialBlockArguments(sourceSpec, 'source3').ctorArgs[0]).toBe('"source3"');
    expect(isRequiredArgumentPlaceholder(` ${REQUIRED_ARGUMENT_PLACEHOLDER} `)).toBe(true);
    expect(missingRequiredFields(block, sourceSpec).map((field) => field.name)).toEqual([
      'T',
      'name',
      'amplitude',
      'frequency_hz',
      'sps'
    ]);
  });

  it('reports missing required template and constructor arguments by name', () => {
    const block = structuredClone(blockOf(siteOf('hello_world'), 'source1'));
    block.template_args[0]!.text = '  ';
    block.ctor_args = block.ctor_args.slice(0, 2);

    expect(missingRequiredFields(block, spec('SourceCWBlock'))).toEqual([
      { kind: 'template', index: 0, name: 'T' },
      { kind: 'constructor', index: 2, name: 'frequency_hz' },
      { kind: 'constructor', index: 3, name: 'sps' }
    ]);
    expect(blockIsValid(block, spec('SourceCWBlock'))).toBe(false);
  });

  it('accepts complete blocks and does not require defaulted fields or packs', () => {
    const source = blockOf(siteOf('hello_world'), 'source1');
    expect(missingRequiredFields(source, spec('SourceCWBlock'))).toEqual([]);

    const gain = structuredClone(blockOf(siteOf('hello_world'), 'source1'));
    gain.type_name = 'GainBlock';
    gain.template_args = [{ ...gain.template_args[0]!, text: 'float' }];
    gain.ctor_args = gain.ctor_args.slice(0, 2);
    expect(missingRequiredFields(gain, spec('GainBlock'))).toEqual([]);
    expect(blockIsValid(gain, spec('GainBlock'))).toBe(true);
    expect(blockIsValid(gain, undefined)).toBe(true);
  });

  it('projects missing fields into node data for the renderer and run gate', () => {
    const site = siteOf('hello_world');
    blockOf(site, 'source1').ctor_args = blockOf(site, 'source1').ctor_args.slice(0, 3);
    const node = projectSite(site, specs).nodes.find((candidate) => candidate.id === 'source1');
    expect(node?.data.missingRequiredFields).toEqual([
      { kind: 'constructor', index: 3, name: 'sps' }
    ]);
  });
});

describe('the connect decision table', () => {
  it('sends connect alone when the wire fits the declared arity', () => {
    const site = dropEdge(siteOf('hello_world'), 'source2', 'adder', 1);
    const plan = connectPlan(0, site, specs, {
      from: 'source2',
      to: 'adder',
      port: 'in',
      portIndex: 1
    });
    expect(commandsOf(plan)).toEqual([
      { command: 'connect', site: 0, from: 'source2', to: 'adder', port: 'in', port_index: 1 }
    ]);
  });

  it('co-patches a template-arg authority and keeps the two commands in order', () => {
    const site = dropEdge(siteOf('hello_world'), 'source2', 'adder', 1);
    const plan = connectPlan(0, site, specs, {
      from: 'source2',
      to: 'adder',
      port: 'in',
      portIndex: 2
    });
    expect(commandsOf(plan)).toEqual([
      {
        command: 'set_template_arg',
        site: 0,
        block: 'adder',
        template_arg_index: 1,
        new_text: '3'
      },
      { command: 'connect', site: 0, from: 'source2', to: 'adder', port: 'in', port_index: 2 }
    ]);
  });

  it('co-patches a ctor-arg authority — the fanout third output bumps the count', () => {
    const site = dropEdge(siteOf('mass_spring_damper'), 'throttle', 'plant', null);
    const plan = connectPlan(0, site, specs, {
      from: 'fanout',
      to: 'plant',
      port: 'force_in',
      portIndex: null
    });
    expect(commandsOf(plan)).toEqual([
      { command: 'set_param', site: 0, block: 'fanout', ctor_arg_index: 1, new_text: '3' },
      {
        command: 'connect',
        site: 0,
        from: 'fanout',
        to: 'plant',
        port: 'force_in',
        port_index: null
      }
    ]);
  });

  it('refuses to grow a ctor_arg_len authority and says to add a label first', () => {
    const site = siteOf('hello_world');
    const refusal = refusalOf(
      connectPlan(0, site, specs, { from: 'source2', to: 'plot', port: 'in', portIndex: 1 })
    );
    expect(refusal).toContain('signal_labels');
    expect(refusal).toContain('add a label');
    expect(refusal).toContain('1');
  });

  it('allows wiring up to the current label count', () => {
    const site = dropEdge(siteOf('hello_world'), 'throttle', 'plot', 0);
    expect(
      commandsOf(
        connectPlan(0, site, specs, { from: 'throttle', to: 'plot', port: 'in', portIndex: 0 })
      )
    ).toHaveLength(1);
  });

  it('refuses a second consumer on a fixed single output', () => {
    const site = siteOf('hello_world');
    const refusal = refusalOf(
      connectPlan(0, site, specs, { from: 'throttle', to: 'adder', port: 'in', portIndex: 5 })
    );
    expect(refusal).toBe('ThrottleBlock has 1 output — it cannot take another');
  });

  it('refuses a self edge, an occupied slot and a read-only endpoint', () => {
    const site = siteOf('hello_world');
    expect(
      refusalOf(connectPlan(0, site, specs, { from: 'adder', to: 'adder', port: 'in', portIndex: 3 }))
    ).toBe('a block cannot wire into itself');

    expect(
      refusalOf(
        connectPlan(0, site, specs, { from: 'throttle', to: 'adder', port: 'in', portIndex: 1 })
      )
    ).toContain('already takes source2');

    const locked = siteOf('hello_world');
    blockOf(locked, 'adder').editable = false;
    blockOf(locked, 'adder').read_only_reason = 'optional_emplace_declaration';
    expect(
      refusalOf(
        connectPlan(0, locked, specs, { from: 'source1', to: 'adder', port: 'in', portIndex: 2 })
      )
    ).toBe('adder is read-only: optional emplace declaration');
  });

  it('reports an existing wire as already there', () => {
    const site = siteOf('hello_world');
    expect(
      refusalOf(
        connectPlan(0, site, specs, { from: 'source2', to: 'adder', port: 'in', portIndex: 1 })
      )
    ).toBe('that wire already exists');
  });

  it('never patches arity when no spec resolves', () => {
    const site = dropEdge(siteOf('hello_world'), 'source2', 'adder', 1);
    expect(
      commandsOf(
        connectPlan(0, site, [], { from: 'source2', to: 'adder', port: 'in', portIndex: 9 })
      )
    ).toEqual([
      { command: 'connect', site: 0, from: 'source2', to: 'adder', port: 'in', port_index: 9 }
    ]);
  });
});

describe('reconnect moves a wire in one transaction', () => {
  it('disconnects the old wire and connects the new one, in that order', () => {
    const site = dropEdge(siteOf('hello_world'), 'source2', 'adder', 1);
    const moving = site.edges.findIndex((edge) => edge.from === 'source1');
    const plan = reconnectPlan(0, site, specs, moving, {
      from: 'source1',
      to: 'adder',
      port: 'in',
      portIndex: 1
    });
    expect(commandsOf(plan)).toEqual([
      { command: 'disconnect', site: 0, edge: moving },
      { command: 'connect', site: 0, from: 'source1', to: 'adder', port: 'in', port_index: 1 }
    ]);
  });

  it('does nothing when the wire lands back where it started', () => {
    const site = siteOf('hello_world');
    const moving = site.edges.findIndex((edge) => edge.from === 'source1');
    expect(
      commandsOf(
        reconnectPlan(0, site, specs, moving, {
          from: 'source1',
          to: 'adder',
          port: 'in',
          portIndex: 0
        })
      )
    ).toEqual([]);
  });

  it('does not count the wire it is moving when it checks output arity', () => {
    const site = dropEdge(siteOf('mass_spring_damper'), 'throttle', 'plant', null);
    const moving = site.edges.findIndex((edge) => edge.from === 'fanout' && edge.to === 'plot');
    const plan = reconnectPlan(0, site, specs, moving, {
      from: 'fanout',
      to: 'plant',
      port: 'force_in',
      portIndex: null
    });
    expect(commandsOf(plan).map((command) => command.command)).toEqual(['disconnect', 'connect']);
  });

  it('refuses to move a read-only wire', () => {
    const site = siteOf('hello_world');
    const moving = site.edges.findIndex((edge) => edge.from === 'source1');
    const edge = site.edges[moving];
    if (!edge) throw new Error('no edge');
    edge.editable = false;
    edge.read_only_reason = 'method_call_port';
    expect(
      refusalOf(
        reconnectPlan(0, site, specs, moving, {
          from: 'source1',
          to: 'adder',
          port: 'in',
          portIndex: 1
        })
      )
    ).toBe('that wire is read-only: method call port');
  });
});

describe('the add-block form', () => {
  it('suggests a free variable name from the type', () => {
    expect(suggestVarName('GainBlock', [])).toBe('gain');
    expect(suggestVarName('SourceCWBlock', [])).toBe('source_c_w');
    expect(suggestVarName('GainBlock', ['gain'])).toBe('gain2');
    expect(suggestVarName('GainBlock', ['gain', 'gain2'])).toBe('gain3');
  });

  it('pre-fills the spec defaults and quotes the display name', () => {
    const form = addForm(spec('FanoutBlock'), 'fanout2');
    expect(form.templateArgs.map((field) => [field.label, field.value])).toEqual([['T', '']]);
    expect(form.ctorArgs.map((field) => [field.label, field.value])).toEqual([
      ['name', '"fanout2"'],
      ['num_outputs', ''],
      ['buffer_size', '0']
    ]);
    expect(form.ctorArgs[1]?.hint).toBe('const size_t');
  });

  it('follows the variable name into the display name until it is edited', () => {
    const form = addForm(spec('GainBlock'), 'gain');
    expect(renameInForm(form, 'volume').ctorArgs[0]?.value).toBe('"volume"');
    const custom = { ...form, ctorArgs: [{ ...form.ctorArgs[0]!, value: '"Loud"' }, ...form.ctorArgs.slice(1)] };
    expect(renameInForm(custom, 'volume').ctorArgs[0]?.value).toBe('"Loud"');
  });

  it('builds one add_block command and drops the fields left blank', () => {
    const form = addForm(spec('FanoutBlock'), 'fanout2');
    form.templateArgs[0]!.value = 'float';
    form.ctorArgs[1]!.value = '2';
    form.ctorArgs[2]!.value = '';
    expect(addBlockCommand(3, spec('FanoutBlock'), form)).toEqual({
      command: 'add_block',
      site: 3,
      type: 'FanoutBlock',
      template_args: ['float'],
      ctor_args: ['"fanout2"', '2'],
      var_name: 'fanout2'
    });
  });

  it('routes every crate refusal to the field that caused it', () => {
    const form = addForm(spec('GainBlock'), 'gain');
    form.templateArgs[0]!.value = 'float float';
    expect(addRefusal({ error: 'duplicate_variable', var_name: 'gain' }, form)).toEqual({
      field: 'var_name',
      message: 'gain is already declared in this function'
    });
    expect(addRefusal({ error: 'reserved_identifier', text: 'gain' }, form)?.field).toBe('var_name');
    expect(addRefusal({ error: 'invalid_identifier', text: 'gain' }, form)).toEqual({
      field: 'var_name',
      message: '"gain" is not a valid C++ identifier'
    });
    expect(
      addRefusal({ error: 'invalid_expression', element: 'template_args', text: 'float float' }, form)
    ).toEqual({
      field: 'template.0',
      message: '"float float" is not a valid template argument'
    });
    expect(addRefusal({ error: 'empty_constructor_arguments', var_name: 'gain' }, form)?.field).toBe(
      null
    );
    expect(addRefusal({ error: 'revision_mismatch' }, form)).toBe(null);
    expect(addRefusal(null, form)).toBe(null);
  });
});

describe('the canvas projects spec-declared ports', () => {
  it('shows every declared input slot plus one growth slot', () => {
    const site = dropEdge(siteOf('hello_world'), 'source2', 'adder', 1);
    const node = projectSite(site, specs, true).nodes.find((entry) => entry.id === 'adder');
    expect(node?.data.inputs.map((slot) => slot.id)).toEqual(['in[0]', 'in[1]', 'in[2]']);
    expect(node?.data.inputs.map((slot) => slot.grow)).toEqual([false, false, true]);
  });

  it('offers a growth slot on a label-sized port so the refusal can be reached', () => {
    const node = projectSite(siteOf('hello_world'), specs, true).nodes.find(
      (entry) => entry.id === 'plot'
    );
    expect(node?.data.inputs.map((slot) => slot.id)).toEqual(['in[0]', 'in[1]']);
  });

  it('never offers a growth slot on a fixed port', () => {
    const node = projectSite(siteOf('mass_spring_damper'), specs, true).nodes.find(
      (entry) => entry.id === 'plant'
    );
    expect(node?.data.inputs.map((slot) => slot.id)).toEqual(['force_in']);
  });

  it('offers a source handle to a block that has outputs but no wire yet', () => {
    const site = dropEdge(siteOf('hello_world'), 'source2', 'adder', 1);
    const projected = projectSite(site, specs, true);
    expect(projected.nodes.find((entry) => entry.id === 'source2')?.data.hasOutput).toBe(true);
    expect(projected.nodes.find((entry) => entry.id === 'plot')?.data.hasOutput).toBe(false);
  });

  it('renders exactly as before when no spec is known', () => {
    const site = siteOf('hello_world');
    expect(projectSite(site)).toEqual(projectSite(site, [], false));
    const node = projectSite(site).nodes.find((entry) => entry.id === 'adder');
    expect(node?.data.inputs.map((slot) => slot.id)).toEqual(['in[0]', 'in[1]']);
    expect(node?.connectable).toBe(false);
  });

  it('parses a handle id back into a port and index', () => {
    expect(parsePortId('in[2]')).toEqual({ port: 'in', index: 2 });
    expect(parsePortId('force_in')).toEqual({ port: 'force_in', index: null });
  });
});

describe('edges address the model by identity', () => {
  it('maps a canvas edge id to its index in the site', () => {
    const site = siteOf('hello_world');
    const projected = projectSite(site);
    for (const [at, edge] of projected.edges.entries()) {
      const index = edgeIndexById(site, edge.id);
      expect(index).not.toBe(null);
      expect(site.edges[index ?? -1]?.from).toBe(projected.edges[at]?.source);
    }
    expect(edgeIndexById(site, 'nothing->there.in[0]#0')).toBe(null);
    expect(edgeAtId(site, projected.edges[0]?.id ?? '')?.from).toBe('source1');
  });

  it('skips edges whose endpoints are not declared when it numbers them', () => {
    const site = siteOf('hello_world');
    site.edges.unshift({
      ...structuredClone(site.edges[0]!),
      from: 'ghost',
      to: 'adder',
      port: { name: 'in', index: 7, kind: 'indexed_field' }
    });
    const projected = projectSite(site);
    const first = projected.edges[0];
    if (!first) throw new Error('no edges');
    expect(edgeIndexById(site, first.id)).toBe(1);
  });
});

describe('problems surface what the model already says', () => {
  it('counts type conflicts and points at the edge', () => {
    const site = siteOf('type_conflict');
    const problems = problemsOf(site);
    expect(problems).toHaveLength(1);
    expect(problems[0]?.kind).toBe('conflict');
    expect(problems[0]?.title).toBe('throttle → throughput');
    expect(problems[0]?.detail).toBe('float out into std::complex<float> in');
    expect(problems[0]?.block).toBe('throughput');
    const ids = projectSite(site).edges.map((edge) => edge.id);
    expect(ids).toContain(problems[0]?.edge);
  });

  it('lists unresolved runner arguments with their span', () => {
    const site = siteOf('hello_world');
    const unresolved: Unresolved = {
      text: '&*maybe.in',
      span: { start: 10, end: 20 },
      runner_index: 2,
      arg_index: 1,
      reason: 'unresolved_runner_argument'
    };
    site.unresolved = [unresolved];
    const problems = problemsOf(site);
    expect(problems).toHaveLength(1);
    expect(problems[0]?.kind).toBe('unresolved');
    expect(problems[0]?.detail).toBe('unresolved runner argument');
    expect(problems[0]?.span).toEqual(unresolved.span);
    expect(problems[0]?.edge).toBe(null);
  });

  it('reports nothing for a clean site', () => {
    expect(problemsOf(siteOf('hello_world'))).toEqual([]);
    expect(problemsOf(undefined)).toEqual([]);
  });
});

describe('a dropped block keeps the position it was dropped at', () => {
  it('pins the newcomer instead of guessing a spot beside its neighbours', () => {
    const site = siteOf('hello_world');
    const before = projectSite(site);
    const added = structuredClone(site);
    added.blocks.push({
      ...structuredClone(blockOf(site, 'adder')),
      var: 'gain',
      type_name: 'GainBlock',
      in_graph: false
    });

    const pinned = new Map([['gain', { x: 411, y: 222 }]]);
    const merged = mergeProjection(before, projectSite(added), pinned);
    expect(merged.nodes.find((node) => node.id === 'gain')?.position).toEqual({ x: 411, y: 222 });

    const guessed = mergeProjection(before, projectSite(added));
    expect(guessed.nodes.find((node) => node.id === 'gain')?.position).not.toEqual({
      x: 411,
      y: 222
    });
  });
});

describe('the inspector names the parameters when a spec resolves', () => {
  it('labels ctor and template arguments and keeps the index visible', () => {
    const block = blockOf(siteOf('hello_world'), 'source1');
    const fields = blockFields(0, block, spec('SourceCWBlock'));
    expect(fields.map((field) => [field.label, field.slot])).toEqual([
      ['display name', ''],
      ['T', 'template 0'],
      ['name', 'ctor 0'],
      ['amplitude', 'ctor 1'],
      ['frequency_hz', 'ctor 2'],
      ['sps', 'ctor 3']
    ]);
    expect(fields[3]?.hint).toBe('float');
    expect(fields[3]?.toCommand('9.0f')).toEqual({
      command: 'set_param',
      site: 0,
      block: 'source1',
      ctor_arg_index: 1,
      new_text: '9.0f'
    });
  });

  it('falls back to indices when no spec matches', () => {
    const block = blockOf(siteOf('hello_world'), 'source1');
    expect(blockFields(0, block).map((field) => field.label)).toEqual([
      'display name',
      'template arg 0',
      'ctor arg 0',
      'ctor arg 1',
      'ctor arg 2',
      'ctor arg 3'
    ]);
  });

  it('never guesses an alignment when the arities disagree', () => {
    const block = blockOf(siteOf('hello_world'), 'plot');
    block.ctor_args = block.ctor_args.slice(0, 2);
    const fields = blockFields(0, block, spec('PlotTimeSeriesBlock'));
    expect(fields.map((field) => field.label)).toEqual([
      'display name',
      'ctor arg 0',
      'ctor arg 1'
    ]);
  });
});
