import type { Edge as FlowEdge, Node as FlowNode } from '@xyflow/svelte';
import type { Block, Edge, Port, Site } from './schema';

export const NODE_WIDTH = 230;
const HEADER_HEIGHT = 74;
const PORT_ROW_HEIGHT = 18;

export type PortSlot = { id: string; label: string };

export type BlockNodeData = {
  block: Block;
  inputs: PortSlot[];
  hasOutput: boolean;
};

export type BlockNode = FlowNode<BlockNodeData, 'block'>;

export type EdgePoint = { x: number; y: number };

export type RoutedEdgeData = { bends: EdgePoint[] };

export type RoutedEdge = FlowEdge<RoutedEdgeData, 'routed'>;

export type Projection = { nodes: BlockNode[]; edges: RoutedEdge[] };

export function portLabel(port: Port): string {
  return port.index === null ? port.name : `${port.name}[${port.index}]`;
}

function edgeKey(edge: Edge): string {
  return `${edge.from}->${edge.to}.${edge.port.name}[${edge.port.index ?? ''}]`;
}

export function edgeIds(edges: Edge[]): string[] {
  const taken = new Map<string, number>();
  return edges.map((edge) => {
    const key = edgeKey(edge);
    const ordinal = taken.get(key) ?? 0;
    taken.set(key, ordinal + 1);
    return `${key}#${ordinal}`;
  });
}

export function nodeHeight(inputCount: number): number {
  return HEADER_HEIGHT + Math.max(inputCount, 1) * PORT_ROW_HEIGHT;
}

export function typeSignature(block: Block): string {
  if (block.template_args.length === 0) return block.type_name;
  return `${block.type_name}<${block.template_args.map((a) => a.text).join(', ')}>`;
}

function inputSlots(site: Site, blockVar: string): PortSlot[] {
  const seen = new Map<string, PortSlot>();
  for (const edge of site.edges) {
    if (edge.to !== blockVar) continue;
    const id = portLabel(edge.port);
    if (!seen.has(id)) seen.set(id, { id, label: id });
  }
  return [...seen.values()].sort((a, b) => a.id.localeCompare(b.id, 'en', { numeric: true }));
}

function hasOutgoing(site: Site, blockVar: string): boolean {
  return site.edges.some((edge) => edge.from === blockVar);
}

function toNode(site: Site, block: Block): BlockNode {
  const inputs = inputSlots(site, block.var);
  return {
    id: block.var,
    type: 'block',
    position: { x: 0, y: 0 },
    width: NODE_WIDTH,
    height: nodeHeight(inputs.length),
    data: { block, inputs, hasOutput: hasOutgoing(site, block.var) },
    draggable: true,
    connectable: false,
    deletable: false
  };
}

function toEdge(edge: Edge, id: string): RoutedEdge {
  return {
    id,
    type: 'routed',
    source: edge.from,
    target: edge.to,
    sourceHandle: 'out',
    targetHandle: portLabel(edge.port),
    data: { bends: [] },
    animated: false,
    selectable: true,
    deletable: false,
    class: edge.editable ? 'cler-edge' : 'cler-edge cler-edge-readonly'
  };
}

export function projectSite(site: Site): Projection {
  const declared = new Set(site.blocks.map((block) => block.var));
  const wired = site.edges.filter((edge) => declared.has(edge.from) && declared.has(edge.to));
  const ids = edgeIds(wired);
  return {
    nodes: site.blocks.map((block) => toNode(site, block)),
    edges: wired.map((edge, index) => toEdge(edge, ids[index] ?? edgeKey(edge)))
  };
}

const NEW_NODE_GAP = 60;

function neighboursOf(id: string, edges: RoutedEdge[], placed: Map<string, EdgePoint>): EdgePoint[] {
  const spots: EdgePoint[] = [];
  for (const edge of edges) {
    const other = edge.source === id ? edge.target : edge.target === id ? edge.source : null;
    const at = other === null ? undefined : placed.get(other);
    if (at) spots.push(at);
  }
  return spots;
}

function spotFor(
  id: string,
  edges: RoutedEdge[],
  placed: Map<string, EdgePoint>,
  ordinal: number
): EdgePoint {
  const near = neighboursOf(id, edges, placed);
  if (near.length > 0) {
    const x = near.reduce((sum, spot) => sum + spot.x, 0) / near.length;
    const y = near.reduce((sum, spot) => sum + spot.y, 0) / near.length;
    return { x: x + NODE_WIDTH + NEW_NODE_GAP, y: y + NEW_NODE_GAP * (ordinal + 1) };
  }
  const all = [...placed.values()];
  if (all.length === 0) return { x: NEW_NODE_GAP, y: NEW_NODE_GAP * (ordinal + 1) };
  return {
    x: Math.min(...all.map((spot) => spot.x)),
    y: Math.max(...all.map((spot) => spot.y)) + nodeHeight(1) + NEW_NODE_GAP * (ordinal + 1)
  };
}

export function mergeProjection(previous: Projection, next: Projection): Projection {
  const kept = new Map(previous.nodes.map((node) => [node.id, node]));
  const bends = new Map(previous.edges.map((edge) => [edge.id, edge.data?.bends ?? []]));
  const placed = new Map<string, EdgePoint>();
  for (const node of next.nodes) {
    const before = kept.get(node.id);
    if (before) placed.set(node.id, before.position);
  }
  let added = 0;
  return {
    nodes: next.nodes.map((node) => {
      const before = kept.get(node.id);
      if (before) return { ...node, position: before.position, selected: before.selected };
      const spot = spotFor(node.id, next.edges, placed, added++);
      placed.set(node.id, spot);
      return { ...node, position: spot };
    }),
    edges: next.edges.map((edge) => ({ ...edge, data: { bends: bends.get(edge.id) ?? [] } }))
  };
}

export type BlockPorts = { inputs: string[]; outputs: string[] };

export function blockPorts(site: Site, blockVar: string): BlockPorts {
  const inputs = inputSlots(site, blockVar).map((slot) => slot.label);
  const outputs = [
    ...new Set(
      site.edges.filter((edge) => edge.from === blockVar).map((edge) => `${edge.to}.${portLabel(edge.port)}`)
    )
  ];
  return { inputs, outputs };
}

export type ReadOnlyNote = { element: string; reason: string };

export function readOnlyNotes(site: Site): ReadOnlyNote[] {
  const notes: ReadOnlyNote[] = [];
  const push = (element: string, reason: string | null) => {
    if (reason) notes.push({ element, reason });
  };
  push(`site ${site.function}()`, site.read_only_reason);
  for (const block of site.blocks) push(`block ${block.var}`, block.read_only_reason);
  for (const edge of site.edges) push(`edge ${edge.from} → ${edge.to}`, edge.read_only_reason);
  for (const runner of site.runners) push(`runner ${runner.block}`, runner.read_only_reason);
  if (site.config) push('config', site.config.read_only_reason);
  return notes;
}
