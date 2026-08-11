import type { Viewport } from '@xyflow/svelte';
import { queued, saveCache, type NodeMove } from '../backend';
import type { BlockNode as BlockNodeType, EdgePoint } from '../project';

export type CachedView = {
  positions?: Record<string, EdgePoint>;
  viewport?: Viewport | null;
  [key: string]: unknown;
};

export type RailTabName = 'inspector' | 'library' | 'assistant';

export type UiCache = {
  version: number;
  activeView: string | null;
  views: Record<string, CachedView>;
  panels: {
    left?: boolean;
    right?: boolean;
    drawer?: boolean;
    drawerHeight?: number;
    rightTab?: RailTabName;
    [key: string]: unknown;
  };
  [key: string]: unknown;
};

export type CacheEnv = {
  editable: boolean;
  path: string;
  flowCache: UiCache;
  setFlowCache: (next: UiCache) => void;
};

const LAYOUT_PREFIX = 'cler.layout.';

const positionsByView = new Map<string, Map<string, EdgePoint>>();
const viewportsByView = new Map<string, Viewport>();
const loadedViews = new Set<string>();
let cacheTimer: ReturnType<typeof setTimeout> | undefined;

export function cacheOf(value: unknown): UiCache {
  const stored = value && typeof value === 'object' ? structuredClone(value) : {};
  const record = stored as Record<string, unknown>;
  const views = record.views && typeof record.views === 'object' ? record.views : {};
  const panels = record.panels && typeof record.panels === 'object' ? record.panels : {};
  return {
    ...record,
    version: typeof record.version === 'number' ? record.version : 1,
    activeView: typeof record.activeView === 'string' ? record.activeView : null,
    views: views as Record<string, CachedView>,
    panels: panels as UiCache['panels']
  };
}

export function cacheViewKey(path: string, key: string): string {
  return key.startsWith(`${path}#`) ? key.slice(path.length + 1) : key;
}

export function persistCache(env: CacheEnv, cache: UiCache = env.flowCache): void {
  if (!env.editable) return;
  if (cacheTimer) clearTimeout(cacheTimer);
  const path = env.path;
  const cached = structuredClone(cache);
  cacheTimer = setTimeout(() => {
    cacheTimer = undefined;
    void queued(path, () => saveCache(path, cached)).catch(() => undefined);
  }, 120);
}

export function cancelCacheWrite(): void {
  if (cacheTimer) clearTimeout(cacheTimer);
  cacheTimer = undefined;
}

export function loadLayout(env: CacheEnv, key: string): void {
  if (loadedViews.has(key)) return;
  loadedViews.add(key);
  try {
    const stored = (env.editable
      ? env.flowCache.views[cacheViewKey(env.path, key)]
      : JSON.parse(localStorage.getItem(LAYOUT_PREFIX + key) ?? '{}')) as {
      positions?: Record<string, EdgePoint>;
      viewport?: Viewport;
    } | undefined;
    const positions = new Map<string, EdgePoint>();
    for (const [id, point] of Object.entries(stored?.positions ?? {})) {
      if (Number.isFinite(point.x) && Number.isFinite(point.y)) positions.set(id, point);
    }
    if (positions.size > 0) positionsByView.set(key, positions);
    const viewport = stored?.viewport;
    if (
      viewport &&
      Number.isFinite(viewport.x) &&
      Number.isFinite(viewport.y) &&
      Number.isFinite(viewport.zoom)
    ) {
      viewportsByView.set(key, viewport);
    }
  } catch {
    localStorage.removeItem(LAYOUT_PREFIX + key);
  }
}

export function storeLayout(env: CacheEnv, key: string): void {
  const positions = Object.fromEntries(positionsByView.get(key) ?? []);
  const viewport = viewportsByView.get(key) ?? null;
  if (!env.editable) {
    localStorage.setItem(LAYOUT_PREFIX + key, JSON.stringify({ positions, viewport }));
    return;
  }
  const id = cacheViewKey(env.path, key);
  const next: UiCache = {
    ...env.flowCache,
    views: { ...env.flowCache.views, [id]: { ...env.flowCache.views[id], positions, viewport } }
  };
  env.setFlowCache(next);
  persistCache(env, next);
}

export function rememberPositions(
  env: CacheEnv,
  key: string,
  current: { id: string; position: EdgePoint }[],
  complete = false
): void {
  if (!key) return;
  loadLayout(env, key);
  const positions = complete
    ? new Map<string, EdgePoint>()
    : (positionsByView.get(key) ?? new Map<string, EdgePoint>());
  for (const node of current) {
    positions.set(node.id, { x: node.position.x, y: node.position.y });
  }
  positionsByView.set(key, positions);
  storeLayout(env, key);
}

export function movedNodes(
  env: CacheEnv,
  key: string,
  current: { id: string; position: EdgePoint }[]
): NodeMove[] {
  loadLayout(env, key);
  const stored = positionsByView.get(key);
  if (!stored) return [];
  return current.flatMap((node) => {
    const before = stored.get(node.id);
    const after = { x: node.position.x, y: node.position.y };
    if (!before || (before.x === after.x && before.y === after.y)) return [];
    return [{ node: node.id, from: { ...before }, to: after }];
  });
}

export function applyNodeMoves(
  env: CacheEnv,
  key: string,
  moves: NodeMove[],
  direction: 'from' | 'to'
): Map<string, EdgePoint> {
  loadLayout(env, key);
  const positions = new Map(positionsByView.get(key) ?? []);
  for (const movement of moves) positions.set(movement.node, { ...movement[direction] });
  positionsByView.set(key, positions);
  return positions;
}

export function restorePositions(
  env: CacheEnv,
  key: string,
  current: BlockNodeType[]
): BlockNodeType[] {
  loadLayout(env, key);
  const positions = positionsByView.get(key);
  if (!positions) return current;
  return current.map((node) => {
    const position = positions.get(node.id);
    return position ? { ...node, position: { x: position.x, y: position.y } } : node;
  });
}

export function rememberViewport(env: CacheEnv, key: string, viewport: Viewport): void {
  if (!key) return;
  loadLayout(env, key);
  viewportsByView.set(key, { x: viewport.x, y: viewport.y, zoom: viewport.zoom });
  storeLayout(env, key);
}

export function syncPositionCache(env: CacheEnv, value: unknown): UiCache {
  const stored = cacheOf(value);
  const views = { ...env.flowCache.views };
  for (const [id, cached] of Object.entries(stored.views)) {
    if (!cached.positions) continue;
    const positions = new Map<string, EdgePoint>();
    for (const [node, point] of Object.entries(cached.positions)) {
      if (Number.isFinite(point.x) && Number.isFinite(point.y)) positions.set(node, point);
    }
    const key = `${env.path}#${id}`;
    positionsByView.set(key, positions);
    loadedViews.add(key);
    views[id] = { ...views[id], positions: Object.fromEntries(positions) };
  }
  return { ...env.flowCache, views };
}

export function viewportOf(key: string): Viewport | null {
  return viewportsByView.get(key) ?? null;
}

export function resetViewCache(): void {
  positionsByView.clear();
  viewportsByView.clear();
  loadedViews.clear();
}
