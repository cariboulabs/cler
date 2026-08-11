import ELK, { type ElkExtendedEdge, type ElkNode } from 'elkjs/lib/elk.bundled.js';
import type { EdgePoint, Projection } from './project';

const elk = new ELK();

const LAYOUT_OPTIONS: Record<string, string> = {
  'elk.algorithm': 'layered',
  'elk.direction': 'RIGHT',
  'elk.layered.spacing.nodeNodeBetweenLayers': '90',
  'elk.spacing.nodeNode': '40',
  'elk.edgeRouting': 'POLYLINE',
  'elk.layered.cycleBreaking.strategy': 'GREEDY',
  'elk.layered.mergeEdges': 'false'
};

function bendPoints(edge: ElkExtendedEdge): EdgePoint[] {
  return (edge.sections ?? []).flatMap((section) => section.bendPoints ?? []);
}

export async function layout(projection: Projection): Promise<Projection> {
  if (projection.nodes.length === 0) return projection;

  const graph: ElkNode = {
    id: 'root',
    layoutOptions: LAYOUT_OPTIONS,
    children: projection.nodes.map((node) => ({
      id: node.id,
      width: node.width ?? 1,
      height: node.height ?? 1
    })),
    edges: projection.edges.map((edge) => ({
      id: edge.id,
      sources: [edge.source],
      targets: [edge.target]
    }))
  };

  const laid = await elk.layout(graph);
  const positions = new Map<string, EdgePoint>();
  for (const child of laid.children ?? []) {
    positions.set(child.id, { x: child.x ?? 0, y: child.y ?? 0 });
  }
  const bends = new Map<string, EdgePoint[]>();
  for (const edge of laid.edges ?? []) {
    bends.set(edge.id, bendPoints(edge));
  }

  return {
    nodes: projection.nodes.map((node) => ({
      ...node,
      position: positions.get(node.id) ?? node.position
    })),
    edges: projection.edges.map((edge) => ({
      ...edge,
      data: { ...edge.data, bends: bends.get(edge.id) ?? [] }
    }))
  };
}
