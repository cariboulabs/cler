import type { Edge as FlowEdge, Node as FlowNode } from '@xyflow/svelte';
import type { Block, Edge, Port, Site, Span } from './schema';

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

export type RoutedEdgeData = { bends: EdgePoint[]; title?: string; conflict?: boolean };

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

export const TYPE_TOKENS = ['--type-1', '--type-2', '--type-3', '--type-4', '--type-5', '--type-6'];
export const NEUTRAL_TOKEN = '--muted';

export function edgeSampleType(edge: Edge): string | null {
  return edge.sample_type ?? edge.source_type ?? null;
}

function wiredEdges(site: Site): Edge[] {
  const declared = new Set(site.blocks.map((block) => block.var));
  return site.edges.filter((edge) => declared.has(edge.from) && declared.has(edge.to));
}

export function typeColors(site: Site): Map<string, string> {
  const colors = new Map<string, string>();
  for (const edge of wiredEdges(site)) {
    const type = edgeSampleType(edge);
    if (type === null || colors.has(type)) continue;
    colors.set(type, TYPE_TOKENS[colors.size] ?? NEUTRAL_TOKEN);
  }
  return colors;
}

export function edgeTitle(edge: Edge): string | undefined {
  if (edge.type_conflict && edge.source_type && edge.sample_type) {
    return `type conflict: ${edge.source_type} out → ${edge.sample_type} in`;
  }
  const type = edgeSampleType(edge);
  return type === null ? undefined : `sample type: ${type}`;
}

function edgeClass(edge: Edge): string {
  const names = ['cler-edge'];
  if (!edge.editable) names.push('cler-edge-readonly');
  if (edge.type_conflict) names.push('cler-edge-conflict');
  return names.join(' ');
}

function edgeStyle(edge: Edge, colors: Map<string, string>): string | undefined {
  const type = edgeSampleType(edge);
  if (type === null) return undefined;
  return `--edge-color: var(${colors.get(type) ?? NEUTRAL_TOKEN})`;
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

function toEdge(edge: Edge, id: string, colors: Map<string, string>): RoutedEdge {
  return {
    id,
    type: 'routed',
    source: edge.from,
    target: edge.to,
    sourceHandle: 'out',
    targetHandle: portLabel(edge.port),
    data: { bends: [], title: edgeTitle(edge), conflict: edge.type_conflict },
    animated: false,
    selectable: true,
    deletable: false,
    style: edgeStyle(edge, colors),
    class: edgeClass(edge)
  };
}

export function projectSite(site: Site): Projection {
  const wired = wiredEdges(site);
  const ids = edgeIds(wired);
  const colors = typeColors(site);
  return {
    nodes: site.blocks.map((block) => toNode(site, block)),
    edges: wired.map((edge, index) => toEdge(edge, ids[index] ?? edgeKey(edge), colors))
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
    edges: next.edges.map((edge) => ({
      ...edge,
      data: { ...edge.data, bends: bends.get(edge.id) ?? [] }
    }))
  };
}

export type PortLine = { label: string; type: string | null };

export type BlockPorts = { inputs: PortLine[]; outputs: PortLine[] };

function firstSeen(pairs: [string, string | null][]): PortLine[] {
  const found = new Map<string, string | null>();
  for (const [label, type] of pairs) if (!found.has(label)) found.set(label, type);
  return [...found].map(([label, type]) => ({ label, type }));
}

export function blockPorts(site: Site, blockVar: string): BlockPorts {
  const inputs = firstSeen(
    site.edges
      .filter((edge) => edge.to === blockVar)
      .map((edge) => [portLabel(edge.port), edgeSampleType(edge)])
  ).sort((a, b) => a.label.localeCompare(b.label, 'en', { numeric: true }));
  const outputs = firstSeen(
    site.edges
      .filter((edge) => edge.from === blockVar)
      .map((edge) => [`${edge.to}.${portLabel(edge.port)}`, edgeSampleType(edge)])
  );
  return { inputs, outputs };
}

export type ReadOnlyNote = { element: string; reason: string; span: Span };

export function readOnlyNotes(site: Site): ReadOnlyNote[] {
  const notes: ReadOnlyNote[] = [];
  const push = (element: string, reason: string | null, span: Span) => {
    if (reason) notes.push({ element, reason, span });
  };
  push(`site ${site.function}()`, site.read_only_reason, site.span);
  for (const block of site.blocks) push(`block ${block.var}`, block.read_only_reason, block.span);
  for (const edge of site.edges)
    push(`edge ${edge.from} → ${edge.to}`, edge.read_only_reason, edge.span);
  for (const runner of site.runners)
    push(`runner ${runner.block}`, runner.read_only_reason, runner.span);
  if (site.config) push('config', site.config.read_only_reason, site.config.run_call_span);
  return notes;
}

export function blockSpans(site: Site, blockVar: string): Span[] {
  const block = site.blocks.find((candidate) => candidate.var === blockVar);
  const runners = site.runners.filter((runner) => runner.block === blockVar);
  return [...(block ? [block.span] : []), ...runners.map((runner) => runner.span)];
}

export function anchorSpans(sites: Site[]): Span[] {
  return sites.flatMap((site) => [
    ...site.blocks.map((block) => block.span),
    ...site.runners.map((runner) => runner.span)
  ]);
}

export type CodeTarget = { siteIndex: number; block: string };

export function targetAt(sites: Site[], offset: number): CodeTarget | null {
  const inside = (span: Span) => offset >= span.start && offset < span.end;
  for (const [siteIndex, site] of sites.entries()) {
    const block = site.blocks.find((candidate) => inside(candidate.span));
    if (block) return { siteIndex, block: block.var };
    const runner = site.runners.find((candidate) => inside(candidate.span));
    if (runner) return { siteIndex, block: runner.block };
  }
  return null;
}
