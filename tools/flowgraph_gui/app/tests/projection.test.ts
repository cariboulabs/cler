import { describe, expect, it } from 'vitest';
import { fixtures } from '../src/fixtures';
import { layout } from '../src/lib/layout';
import { edgeId, projectSite, readOnlyNotes, type Projection } from '../src/lib/project';
import type { ParseResult, Site } from '../src/lib/schema';

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

describe('spike', () => {
  it('projects the largest example', async () => {
    const site = firstSite('spike');
    const projection = await laidOut(site);
    expect(projection.nodes.length).toBe(site.blocks.length);
    expect(projection.edges.map((edge) => edge.id)).toEqual(site.edges.map(edgeId));
  });
});
