import { describe, expect, it } from 'vitest';
import { fixtures } from '../src/fixtures';
import { layout } from '../src/lib/layout';
import {
  blockPorts,
  edgeIds,
  edgeTitle,
  projectSite,
  readOnlyNotes,
  typeColors,
  TYPE_TOKENS,
  type Projection
} from '../src/lib/project';
import type { Block, Edge, ParseResult, Site } from '../src/lib/schema';

function fixture(name: string): ParseResult {
  const found = fixtures[name];
  if (!found) throw new Error(`missing fixture ${name}`);
  return found;
}

function firstSite(name: string): Site {
  const site = fixture(name).sites[0];
  if (!site) throw new Error(`fixture ${name} has no sites`);
  return site;
}

async function laidOut(site: Site): Promise<Projection> {
  return layout(projectSite(site));
}

function positionsAreDistinct(projection: Projection): boolean {
  const keys = projection.nodes.map((node) => `${node.position.x},${node.position.y}`);
  return new Set(keys).size === keys.length;
}

describe('every fixture projects and lays out', () => {
  for (const name of Object.keys(fixtures)) {
    it(name, async () => {
      for (const site of fixture(name).sites) {
        const projection = await laidOut(site);
        expect(projection.nodes).toHaveLength(site.blocks.length);
        expect(new Set(projection.nodes.map((node) => node.id)).size).toBe(site.blocks.length);
        expect(new Set(projection.edges.map((edge) => edge.id)).size).toBe(
          projection.edges.length
        );
        expect(positionsAreDistinct(projection)).toBe(true);
        for (const edge of projection.edges) {
          const target = projection.nodes.find((node) => node.id === edge.target);
          expect(target).toBeDefined();
          expect(target?.data.inputs.map((slot) => slot.id)).toContain(edge.targetHandle);
          const source = projection.nodes.find((node) => node.id === edge.source);
          expect(source?.data.hasOutput).toBe(true);
        }
      }
    });
  }
});

describe('hello_world', () => {
  it('has 5 nodes and 4 edges', async () => {
    const projection = await laidOut(firstSite('hello_world'));
    expect(projection.nodes).toHaveLength(5);
    expect(projection.edges).toHaveLength(4);
  });

  it('lays out left to right along the chain', async () => {
    const projection = await laidOut(firstSite('hello_world'));
    const x = (id: string) => projection.nodes.find((node) => node.id === id)?.position.x ?? -1;
    expect(x('source1')).toBeLessThan(x('adder'));
    expect(x('adder')).toBeLessThan(x('throttle'));
    expect(x('throttle')).toBeLessThan(x('plot'));
  });
});

describe('plots', () => {
  it('has 12 nodes and 14 edges', async () => {
    const projection = await laidOut(firstSite('plots'));
    expect(projection.nodes).toHaveLength(12);
    expect(projection.edges).toHaveLength(14);
  });

  it('renders the two cw_timeseries_plot edges distinctly', async () => {
    const projection = await laidOut(firstSite('plots'));
    const parallel = projection.edges.filter(
      (edge) => edge.source === 'cw_complex2realimag' && edge.target === 'cw_timeseries_plot'
    );
    expect(parallel).toHaveLength(2);
    expect(new Set(parallel.map((edge) => edge.id)).size).toBe(2);
    expect(new Set(parallel.map((edge) => edge.targetHandle)).size).toBe(2);
    expect(parallel.map((edge) => edge.targetHandle).sort()).toEqual(['in[0]', 'in[1]']);
    expect(parallel.every((edge) => edge.label === undefined)).toBe(true);
  });

  it('gives cw_timeseries_plot two input handles', async () => {
    const projection = await laidOut(firstSite('plots'));
    const plot = projection.nodes.find((node) => node.id === 'cw_timeseries_plot');
    expect(plot?.data.inputs.map((slot) => slot.id)).toEqual(['in[0]', 'in[1]']);
  });
});

describe('mass_spring_damper', () => {
  it('keeps the fanout -> controller feedback edge', async () => {
    const site = firstSite('mass_spring_damper');
    const projection = await laidOut(site);
    const feedback = projection.edges.find(
      (edge) => edge.source === 'fanout' && edge.target === 'controller'
    );
    expect(feedback).toBeDefined();
    expect(feedback?.targetHandle).toBe('measured_position_in');
    expect(projection.nodes).toHaveLength(5);
    expect(projection.edges).toHaveLength(5);
  });

  it('lays the cycle out without collapsing nodes', async () => {
    const projection = await laidOut(firstSite('mass_spring_damper'));
    expect(positionsAreDistinct(projection)).toBe(true);
  });
});

describe('uhd_device', () => {
  it('yields four sites with the expected block counts', async () => {
    const result = fixture('uhd_device');
    expect(result.sites.map((site) => site.function)).toEqual([
      'mode_rx',
      'mode_tx_chirp',
      'mode_tx_cw',
      'mode_zero_span'
    ]);

    const counts: Array<[number, number]> = [];
    for (const site of result.sites) {
      const projection = await laidOut(site);
      counts.push([projection.nodes.length, projection.edges.length]);
    }
    expect(counts).toEqual([
      [4, 3],
      [4, 3],
      [4, 3],
      [3, 2]
    ]);
  });

  it('keeps per-site ids separate despite reused variable names', () => {
    const result = fixture('uhd_device');
    const ids = result.sites.map((site) => `${site.function}@${site.call_offset}`);
    expect(new Set(ids).size).toBe(4);
  });
});

describe('adsb_receiver', () => {
  it('surfaces read-only elements', () => {
    const site = firstSite('adsb_receiver');
    const notes = readOnlyNotes(site);
    const reasons = notes.map((note) => note.reason);
    expect(reasons).toContain('site_has_read_only_elements');
    expect(reasons).toContain('optional_emplace_declaration');
    expect(reasons).toContain('unsupported_block_expression');
    expect(notes.some((note) => note.element === 'block source')).toBe(true);
  });

  it('marks the read-only edge and the unwired block', async () => {
    const site = firstSite('adsb_receiver');
    const projection = await laidOut(site);
    const readOnlyEdges = projection.edges.filter((edge) =>
      String(edge.class).includes('cler-edge-readonly')
    );
    expect(readOnlyEdges).toHaveLength(1);
    expect(readOnlyEdges[0]?.source).toBe('source');

    const unwired = projection.nodes.filter((node) => !node.data.block.in_graph);
    expect(unwired.map((node) => node.id)).toEqual(['null_sink']);
  });
});

function syntheticBlock(name: string): Block {
  return {
    var: name,
    type_text: 'AnyBlock',
    type_name: 'AnyBlock',
    alias: null,
    template_args: [],
    ctor_args: [],
    display_name: null,
    in_graph: true,
    span: { start: 0, end: 0 },
    editable: true,
    read_only_reason: null
  };
}

type EdgeSpec = {
  from: string;
  to: string;
  port: string;
  sample?: string | null;
  source?: string | null;
  conflict?: boolean;
  editable?: boolean;
};

function syntheticEdge(spec: EdgeSpec): Edge {
  return {
    from: spec.from,
    to: spec.to,
    port: { name: spec.port, index: null, kind: 'field' },
    runner_index: 0,
    arg_index: 1,
    text: '',
    span: { start: 0, end: 0 },
    editable: spec.editable ?? true,
    read_only_reason: null,
    sample_type: spec.sample ?? null,
    source_type: spec.source ?? spec.sample ?? null,
    type_conflict: spec.conflict ?? false
  };
}

function syntheticSite(specs: EdgeSpec[]): Site {
  const edges = specs.map(syntheticEdge);
  const names = [...new Set(edges.flatMap((edge) => [edge.from, edge.to]))];
  return {
    function: 'main',
    call_offset: 0,
    span: { start: 0, end: 0 },
    flowgraph_var: 'flowgraph',
    blocks: names.map(syntheticBlock),
    edges,
    runners: [],
    config: null,
    unresolved: [],
    editable: true,
    read_only_reason: null
  };
}

function chain(types: (string | null)[]): Site {
  return syntheticSite(
    types.map((sample, index) => ({
      from: `src${index}`,
      to: 'sink',
      port: `in${index}`,
      sample
    }))
  );
}

function styleOf(projection: Projection, id: string): string | undefined {
  const edge = projection.edges.find((candidate) => candidate.id === id);
  return edge?.style === undefined ? undefined : String(edge.style);
}

describe('typed edges', () => {
  it('assigns tokens in first-appearance order, not alphabetical order', () => {
    const site = chain(['uint16_t', 'float', 'uint16_t', 'std::complex<float>']);
    expect([...typeColors(site)]).toEqual([
      ['uint16_t', '--type-1'],
      ['float', '--type-2'],
      ['std::complex<float>', '--type-3']
    ]);
  });

  it('gives the same site the same colours every time it is projected', () => {
    const site = chain(['float', 'std::complex<float>', 'float']);
    const first = projectSite(site).edges.map((edge) => String(edge.style));
    const reloaded = projectSite(JSON.parse(JSON.stringify(site)) as Site).edges.map((edge) =>
      String(edge.style)
    );
    expect(reloaded).toEqual(first);
    expect(first).toEqual([
      '--edge-color: var(--type-1)',
      '--edge-color: var(--type-2)',
      '--edge-color: var(--type-1)'
    ]);
  });

  it('falls back to the neutral stroke past the sixth distinct type', () => {
    const types = ['t0', 't1', 't2', 't3', 't4', 't5', 't6', 't7'];
    const colors = typeColors(chain(types));
    expect(types.slice(0, 6).map((type) => colors.get(type))).toEqual(TYPE_TOKENS);
    expect(colors.get('t6')).toBe('--muted');
    expect(colors.get('t7')).toBe('--muted');
  });

  it('leaves an unresolved edge without a colour at all', () => {
    const projection = projectSite(chain(['float', null]));
    expect(styleOf(projection, 'src0->sink.in0[]#0')).toBe('--edge-color: var(--type-1)');
    expect(styleOf(projection, 'src1->sink.in1[]#0')).toBeUndefined();
  });

  it('uses the source element type when only the source end resolves', () => {
    const site = syntheticSite([
      { from: 'a', to: 'b', port: 'in', sample: null, source: 'float' }
    ]);
    expect([...typeColors(site)]).toEqual([['float', '--type-1']]);
  });

  it('marks a conflicting edge so the danger styling outranks its type colour', () => {
    const site = syntheticSite([
      { from: 'a', to: 'b', port: 'in', sample: 'float' },
      {
        from: 'b',
        to: 'c',
        port: 'in',
        sample: 'std::complex<float>',
        source: 'float',
        conflict: true
      }
    ]);
    const projection = projectSite(site);
    const conflicting = projection.edges[1];
    expect(String(conflicting?.class)).toContain('cler-edge-conflict');
    expect(conflicting?.data?.conflict).toBe(true);
    expect(String(projection.edges[0]?.class)).not.toContain('cler-edge-conflict');
  });

  it('keeps read-only and conflicting edges on separate classes', () => {
    const site = syntheticSite([
      { from: 'a', to: 'b', port: 'in', sample: 'float', editable: false },
      { from: 'b', to: 'c', port: 'in', sample: 'float', source: 'int', conflict: true }
    ]);
    const classes = projectSite(site).edges.map((edge) => String(edge.class));
    expect(classes[0]).toBe('cler-edge cler-edge-readonly');
    expect(classes[1]).toBe('cler-edge cler-edge-conflict');
  });

  it('writes the type into the edge tooltip', () => {
    const [plain, conflicting, unknown] = syntheticSite([
      { from: 'a', to: 'b', port: 'in', sample: 'std::complex<float>' },
      { from: 'b', to: 'c', port: 'in', sample: 'uint16_t', source: 'float', conflict: true },
      { from: 'c', to: 'd', port: 'in', sample: null }
    ]).edges;
    expect(plain && edgeTitle(plain)).toBe('sample type: std::complex<float>');
    expect(conflicting && edgeTitle(conflicting)).toBe('type conflict: float out → uint16_t in');
    expect(unknown && edgeTitle(unknown)).toBeUndefined();
  });

  it('hands the inspector a type per wired port', () => {
    const site = firstSite('hello_world');
    expect(blockPorts(site, 'adder')).toEqual({
      inputs: [
        { label: 'in[0]', type: 'float' },
        { label: 'in[1]', type: 'float' }
      ],
      outputs: [{ label: 'throttle.in', type: 'float' }]
    });
  });

  it('reads real types off the regenerated fixtures', () => {
    expect([...typeColors(firstSite('hello_world'))]).toEqual([['float', '--type-1']]);
    expect([...typeColors(firstSite('plots'))]).toEqual([
      ['std::complex<float>', '--type-1'],
      ['float', '--type-2']
    ]);

    const conflict = firstSite('type_conflict');
    expect([...typeColors(conflict)]).toEqual([
      ['float', '--type-1'],
      ['std::complex<float>', '--type-2']
    ]);
    expect(conflict.edges.filter((edge) => edge.type_conflict)).toHaveLength(1);
  });
});

describe('spike', () => {
  it('projects the largest example', async () => {
    const site = firstSite('spike');
    const projection = await laidOut(site);
    expect(projection.nodes.length).toBe(site.blocks.length);
    expect(projection.edges.map((edge) => edge.id)).toEqual(edgeIds(site.edges));
  });
});
