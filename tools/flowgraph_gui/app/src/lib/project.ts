import type { Edge as FlowEdge, Node as FlowNode } from '@xyflow/svelte';
import {
  authorityOf,
  countOf,
  missingRequiredFields,
  specOfBlock,
  type BlockSpec,
  type RequiredBlockField
} from './palette';
import type { Block, Edge, Port, Site, Span } from './schema';

export const NODE_WIDTH = 230;
const HEADER_HEIGHT = 74;
const PORT_ROW_HEIGHT = 18;

export type PortSlot = { id: string; label: string; port: string; index: number | null; grow: boolean };

export type BlockNodeData = {
  block: Block;
  inputs: PortSlot[];
  hasOutput: boolean;
  hasRunner: boolean;
  missingRequiredFields: RequiredBlockField[];
};

export type BlockNode = FlowNode<BlockNodeData, 'block'>;

export type EdgePoint = { x: number; y: number };

export type RoutedEdgeData = {
  bends: EdgePoint[];
  title?: string;
  conflict?: boolean;
  reconnectable?: boolean;
};

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

export function parsePortId(id: string): { port: string; index: number | null } {
  const found = /^(.*)\[(\d+)\]$/.exec(id);
  if (!found || found[1] === undefined || found[2] === undefined) return { port: id, index: null };
  return { port: found[1], index: Number(found[2]) };
}

function slotOf(port: string, index: number | null, grow: boolean): PortSlot {
  const id = index === null ? port : `${port}[${index}]`;
  return { id, label: id, port, index, grow };
}

function declaredSlots(block: Block, spec: BlockSpec): PortSlot[] {
  const slots: PortSlot[] = [];
  const authority = authorityOf(spec.input_count);
  for (const port of spec.ports) {
    if (port.direction !== 'input') continue;
    if (!port.variable) {
      slots.push(slotOf(port.name, null, false));
      continue;
    }
    const declared = countOf(block, spec.input_count) ?? 0;
    for (let index = 0; index < declared; index++) slots.push(slotOf(port.name, index, false));
    if (authority.kind !== 'fixed' && block.editable) {
      slots.push(slotOf(port.name, declared, true));
    }
  }
  return slots;
}

function inputSlots(site: Site, block: Block, spec: BlockSpec | undefined): PortSlot[] {
  const seen = new Map<string, PortSlot>();
  for (const edge of site.edges) {
    if (edge.to !== block.var) continue;
    const found = portLabel(edge.port);
    if (!seen.has(found)) seen.set(found, slotOf(edge.port.name, edge.port.index, false));
  }
  if (spec) {
    for (const slot of declaredSlots(block, spec)) {
      if (!seen.has(slot.id)) seen.set(slot.id, slot);
    }
  }
  return [...seen.values()].sort((a, b) => a.id.localeCompare(b.id, 'en', { numeric: true }));
}

function hasOutgoing(site: Site, blockVar: string): boolean {
  return site.edges.some((edge) => edge.from === blockVar);
}

export function hasRunner(site: Site, blockVar: string): boolean {
  return site.runners.some((runner) => runner.block === blockVar);
}

function offersOutput(site: Site, block: Block, spec: BlockSpec | undefined): boolean {
  if (hasOutgoing(site, block.var)) return true;
  if (!spec || !block.editable) return false;
  return spec.ports.some((port) => port.direction === 'output');
}

export const TYPE_TOKENS = ['--type-1', '--type-2', '--type-3', '--type-4', '--type-5', '--type-6'];
export const NEUTRAL_TOKEN = '--muted';

export function edgeSampleType(edge: Edge): string | null {
  return edge.sample_type ?? edge.source_type ?? null;
}

function wiredEntries(site: Site): { edge: Edge; index: number }[] {
  const declared = new Set(site.blocks.map((block) => block.var));
  return site.edges
    .map((edge, index) => ({ edge, index }))
    .filter(({ edge }) => declared.has(edge.from) && declared.has(edge.to));
}

function wiredEdges(site: Site): Edge[] {
  return wiredEntries(site).map(({ edge }) => edge);
}

export function edgeIndexById(site: Site, id: string): number | null {
  const entries = wiredEntries(site);
  const ids = edgeIds(entries.map(({ edge }) => edge));
  const at = ids.indexOf(id);
  return at === -1 ? null : (entries[at]?.index ?? null);
}

export function edgeAtId(site: Site, id: string): Edge | null {
  const index = edgeIndexById(site, id);
  return index === null ? null : (site.edges[index] ?? null);
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

function edgeClass(edge: Edge, selected: string | null): string {
  const names = ['cler-edge'];
  if (!edge.editable) names.push('cler-edge-readonly');
  if (edge.type_conflict) names.push('cler-edge-conflict');
  if (selected !== null) {
    names.push(
      edge.from === selected || edge.to === selected ? 'cler-edge-lift' : 'cler-edge-dim'
    );
  }
  return names.join(' ');
}

function edgeStyle(edge: Edge, colors: Map<string, string>): string | undefined {
  const type = edgeSampleType(edge);
  if (type === null) return undefined;
  return `--edge-color: var(${colors.get(type) ?? NEUTRAL_TOKEN})`;
}

function toNode(site: Site, block: Block, specs: BlockSpec[], connectable: boolean): BlockNode {
  const spec = specOfBlock(specs, block);
  const inputs = inputSlots(site, block, spec);
  return {
    id: block.var,
    type: 'block',
    position: { x: 0, y: 0 },
    width: NODE_WIDTH,
    height: nodeHeight(inputs.length),
    data: {
      block,
      inputs,
      hasOutput: offersOutput(site, block, spec),
      hasRunner: hasRunner(site, block.var),
      missingRequiredFields: spec ? missingRequiredFields(block, spec) : []
    },
    draggable: true,
    connectable: connectable && block.editable,
    deletable: false
  };
}

function toEdge(
  edge: Edge,
  id: string,
  colors: Map<string, string>,
  connectable: boolean,
  selected: string | null
): RoutedEdge {
  return {
    id,
    type: 'routed',
    source: edge.from,
    target: edge.to,
    sourceHandle: 'out',
    targetHandle: portLabel(edge.port),
    data: {
      bends: [],
      title: edgeTitle(edge),
      conflict: edge.type_conflict,
      reconnectable: connectable && edge.editable
    },
    animated: false,
    selectable: true,
    deletable: false,
    style: edgeStyle(edge, colors),
    class: edgeClass(edge, selected)
  };
}

export function projectSite(
  site: Site,
  specs: BlockSpec[] = [],
  connectable = false,
  selected: string | null = null
): Projection {
  const wired = wiredEdges(site);
  const ids = edgeIds(wired);
  const colors = typeColors(site);
  return {
    nodes: site.blocks.map((block) => toNode(site, block, specs, connectable)),
    edges: wired.map((edge, index) =>
      toEdge(edge, ids[index] ?? edgeKey(edge), colors, connectable, selected)
    )
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

export function mergeProjection(
  previous: Projection,
  next: Projection,
  pinned: Map<string, EdgePoint> = new Map()
): Projection {
  const kept = new Map(previous.nodes.map((node) => [node.id, node]));
  const bends = new Map(previous.edges.map((edge) => [edge.id, edge.data?.bends ?? []]));
  const chosen = new Map(previous.edges.map((edge) => [edge.id, edge.selected === true]));
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
      const dropped = pinned.get(node.id);
      if (dropped) {
        placed.set(node.id, dropped);
        return { ...node, position: dropped };
      }
      const spot = spotFor(node.id, next.edges, placed, added++);
      placed.set(node.id, spot);
      return { ...node, position: spot };
    }),
    edges: next.edges.map((edge) => ({
      ...edge,
      selected: chosen.get(edge.id) ?? false,
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

export type Problem = {
  id: string;
  kind: 'conflict' | 'unresolved' | 'no runner' | 'compile';
  severity: 'error' | 'warning';
  title: string;
  detail: string;
  span: Span;
  edge: string | null;
  block: string | null;
};

export const NO_RUNNER_BADGE = 'no runner';

export const NO_RUNNER_HINT =
  'declared, receives wires, but never runs — reconnect or remove its consumers';

export function neverRuns(site: Site, block: Block): boolean {
  return block.in_graph && !hasRunner(site, block.var);
}

export function problemsOf(site: Site | undefined): Problem[] {
  if (!site) return [];
  const problems: Problem[] = [];
  const entries = wiredEntries(site);
  const ids = edgeIds(entries.map(({ edge }) => edge));
  for (const [at, { edge }] of entries.entries()) {
    if (!edge.type_conflict) continue;
    const id = ids[at] ?? `${edge.from}->${edge.to}`;
    problems.push({
      id: `conflict:${id}`,
      kind: 'conflict',
      severity: 'error',
      title: `${edge.from} → ${edge.to}`,
      detail: `${edge.source_type ?? '?'} out into ${edge.sample_type ?? '?'} in`,
      span: edge.span,
      edge: id,
      block: edge.to
    });
  }
  for (const [at, item] of site.unresolved.entries()) {
    problems.push({
      id: `unresolved:${at}`,
      kind: 'unresolved',
      severity: 'error',
      title: item.text,
      detail: item.reason.replace(/_/g, ' '),
      span: item.span,
      edge: null,
      block: null
    });
  }
  for (const block of site.blocks) {
    if (!neverRuns(site, block)) continue;
    problems.push({
      id: `no-runner:${block.var}`,
      kind: 'no runner',
      severity: 'warning',
      title: block.var,
      detail: `${block.var} has no runner and will never execute`,
      span: block.span,
      edge: null,
      block: block.var
    });
  }
  return problems;
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
