<script lang="ts">
  import {
    Background,
    MiniMap,
    SvelteFlow,
    type EdgeTypes,
    type NodeTypes,
    type Viewport
  } from '@xyflow/svelte';
  import '@xyflow/svelte/dist/style.css';
  import { tick, untrack } from 'svelte';
  import Actions, {
    type Alert,
    type EdgeInfo,
    type FitPadding,
    type Tasks
  } from './lib/Actions.svelte';
  import AddBlock from './lib/AddBlock.svelte';
  import AiAgent from './lib/AiAgent.svelte';
  import BlockNode from './lib/BlockNode.svelte';
  import FieldList from './lib/FieldList.svelte';
  import Inspector from './lib/Inspector.svelte';
  import Palette from './lib/Palette.svelte';
  import RailTabs, { type RailTab } from './lib/RailTabs.svelte';
  import RoutedEdge from './lib/RoutedEdge.svelte';
  import TypeLegend from './lib/TypeLegend.svelte';
  import { diffLines, historyOf, type Message, type Proposal, type Usage } from './lib/agent';
  import {
    applyCommands,
    aiAgentAsk,
    aiAgentModels,
  aiAgentStatus,
    aiAgentOauthStart,
    aiAgentOauthLogout,
    aiAgentStop,
    onAiAgentAuthChanged,
    onAiAgentDelta,
    onAiAgentDone,
    onAiAgentProposal,
    previewCommands,
    type AiAgentProposal,
    type AiAgentStatus,
  type ListedModel,
    buildTarget,
    checkDocument,
    closeDocument,
    describeApplyError,
    editSource,
    type ParseFault,
    errorRecord,
    findTarget,
    inTauri,
    loadFixture,
    loadPalette,
    moveNodes,
    onArtifactStatusChange,
    onExternalChange,
    onTaskEnd,
    onTaskLine,
    openDocument,
    openInEditor,
    pickFile,
    pickSavePath,
    queued,
    redoDocument,
    reloadDocument,
    runTarget,
    newDocument,
    appSettings,
    setAppSettings,
    resolvedClerRoot,
    pickFolder,
    type AppSettings,
    saveDocument,
    saveDocumentAs,
    spansOf,
    stopTarget,
    undoDocument,
    type NodeMove,
    type TargetInfo,
    type TaskStarted,
    type TaskKind
  } from './lib/backend';
  import {
    compileProblems,
    parseDiagnostics,
    placeDiagnostics,
    type Placed
  } from './lib/diagnostics';
  import * as layoutCache from './lib/layout/cache';
  import { cacheOf, type UiCache } from './lib/layout/cache';
  import { layout } from './lib/layout';
  import {
    addBlockCommand,
    addRefusal,
    connectPlan,
    DRAG_TYPE,
    missingRequiredFields,
    reconnectPlan,
    specFor,
    specOfBlock,
    specsFromSites,
    type AddForm,
    type BlockSpec,
    type ConnectPlan,
    type FieldRefusal,
    type Wire
  } from './lib/palette';
  import {
    anchorSpans,
    blockSpans,
    edgeAtId,
    edgeIndexById,
    mergeProjection,
    parsePortId,
    problemsOf,
    projectSite,
    readOnlyNotes,
    targetAt,
    typeColors,
    type BlockNode as BlockNodeType,
    type EdgePoint,
    type Problem,
    type RoutedEdge as RoutedEdgeType
  } from './lib/project';
  import {
    lineOfOffset,
    lineTextAt,
    siteLabel,
    siteViewIds,
    type Command,
    type DocumentState,
    type Site,
    type Span
  } from './lib/schema';
  import type { CodeMark } from './lib/editor';
  import BuildProgress, { STRIP_H } from './web/BuildProgress.svelte';
  import CodeDrawer from './lib/CodeDrawer.svelte';
  import type { Tab } from './lib/CodeDrawer.svelte';
  import { fixtureNames } from './fixtures';
  import { configFields, type Outcome } from './lib/inspector';

  const nodeTypes: NodeTypes = { block: BlockNode as unknown as NodeTypes[string] };
  const edgeTypes: EdgeTypes = { routed: RoutedEdge as unknown as EdgeTypes[string] };

  const requestedFixture = (() => {
    const search = new URLSearchParams(window.location.search);
    return search.get('example') ?? search.get('fixture');
  })();

  function initialFixture(): string {
    const requested = requestedFixture;
    return requested && fixtureNames.includes(requested) ? requested : 'hello_world';
  }

  const desktop = inTauri();
  // ponytail: the wasm shell installs __TAURI_INTERNALS__, so inTauri() is true in the browser
  // too — this build flag is what separates /try from the desktop app. Progress panel is
  // browser-only; make it generic if the Tauri jobs ever grow phases worth showing.
  const inBrowser = !!import.meta.env.VITE_CLER_WASM;
  const viewerNote = 'example mode — read-only viewer — use Open file… to edit the real file';
  const DISK_DRIFT = 'changed on disk';
  const DISK_NOTE = 'this file changed on disk — reload before building or running';
  const NO_SITE = 'no flowgraph site found in this file';
  const DROP_HINT = 'dropped — release on an input port to connect';
  const NO_SHELL = 'the AI agent runs on your machine — open the desktop shell to use it';
  const UNSAVEABLE = 'the code has a syntax error — fix it before saving';
  const UNPARSED_LOCK =
    'the code has a syntax error — fix it in the drawer, or discard the edit, before the graph moves again';
  const TEXT_DELAY = 300;
  const MOVED_ON = 'the graph moved on since that proposal — re-check it before applying';
  const RAIL_WIDTH = 44;
  const SIDEBAR_WIDTH = 280;
  const INSPECTOR_WIDTH = 320;
  const AGENT_WIDTH = 320;
  const MINIMAP_MIN = 12;
  const BAR_HEIGHT = 40;
  const INSET = 12;
  const FIT_GAP = 4;
  const LEFT_PANEL = 'cler.panel.left';
  const RIGHT_PANEL = 'cler.panel.right';
  const DRAWER_PANEL = 'cler.panel.drawer';
  const DRAWER_HEIGHT = 'cler.panel.drawer.height';
  const DEFAULT_DRAWER_HEIGHT = 260;
  const OUTPUT_LINES = 2000;
  const MIN_DRAWER_HEIGHT = 90;

  function storedOpen(key: string, fallback: boolean): boolean {
    const stored = localStorage.getItem(key);
    return stored === null ? fallback : stored !== 'closed';
  }

  function storeOpen(key: string, open: boolean): void {
    localStorage.setItem(key, open ? 'open' : 'closed');
  }

  function storedHeight(): number {
    const stored = Number(localStorage.getItem(DRAWER_HEIGHT));
    return Number.isFinite(stored) && stored >= MIN_DRAWER_HEIGHT ? stored : DEFAULT_DRAWER_HEIGHT;
  }

  function targetKey(path: string, sha256: string | undefined, refresh: number): string {
    return `${path}:${sha256 ?? ''}:${refresh}`;
  }

  const startFixture = initialFixture();

  let fixtureName = $state(startFixture);
  let doc = $state.raw<DocumentState>(loadFixture(startFixture));
  let opened = $state<string | null>(null);
  let siteIndex = $state(0);
  let selected = $state<string | null>(null);
  let changedOnDisk = $state(false);
  let nodes = $state.raw<BlockNodeType[]>([]);
  let edges = $state.raw<RoutedEdgeType[]>([]);
  let status = $state('');
  let viewKey = $state('');
  let leftOpen = $state(storedOpen(LEFT_PANEL, true));
  let rightOpen = $state(storedOpen(RIGHT_PANEL, false));
  let drawerOpen = $state(storedOpen(DRAWER_PANEL, false));
  let drawerHeight = $state(storedHeight());
  let stashed = $state.raw<[boolean, boolean, boolean] | null>(null);
  let failure = $state<string | null>(null);
  let drawerMounted = $state(false);
  let actions = $state<Actions | null>(null);
  let inspector = $state<Inspector | null>(null);
  let configList = $state<FieldList | null>(null);
  let adder = $state<AddBlock | null>(null);
  let specs = $state.raw<BlockSpec[]>([]);
  let selectedEdge = $state<string | null>(null);
  let focus = $state<Span | null>(null);
  let refusal = $state<{ block: string; spans: Span[] } | null>(null);
  let alert = $state.raw<Alert | null>(null);
  let tab = $state<Tab>('code');
  let target = $state.raw<TargetInfo | null>(null);
  let targetError = $state<string | null>(null);
  let diagLines = $state.raw<string[]>([]);
  let output = $state.raw<string[]>([]);
  let busy = $state<TaskKind | null>(null);
  let running = $state(false);
  let taskFail = $state<string | null>(null);
  let justFinished = $state<'check' | 'build' | null>(null);
  let justFinishedTimer: ReturnType<typeof setTimeout> | undefined;
  const progressOn = $derived(
    inBrowser && (busy !== null || running || taskFail !== null || justFinished !== null)
  );
  let targetRefresh = $state(0);
  let rightTab = $state<RailTab>('inspector');
  let leftTab = $state<RailTab>(desktop ? 'ai-agent' : 'settings');
  let runArgs = $state('');
  let libSettings = $state<AppSettings>({ clerRoot: null, blockLibraries: [], aiAgentModel: null, aiAgentProvider: null, aiAgentBaseUrl: null });
  let resolvedRoot = $state<string | null>(null);

  $effect(() => {
    const path = doc.path;
    if (!desktop || !path) {
      resolvedRoot = null;
      return;
    }
    void resolvedClerRoot(path).then(
      (root) => (resolvedRoot = root),
      () => (resolvedRoot = null)
    );
  });
  let pathsMenuOpen = $state(false);
  let agentModels = $state<ListedModel[]>([]);
  let keyStatus = $state.raw<AiAgentStatus | null>(null);
  let chat = $state.raw<Message[]>([]);
  let pendingReply = $state<number | null>(null);
  let turns = 0;
  let generation = 0;
  let opening = 0;
  let alerted = 0;
  let pinned = new Map<string, EdgePoint>();
  let flowCache = $state.raw<UiCache>(cacheOf({}));
  const activeJobs = new Map<TaskKind, number>();
  const latestJobs = new Map<TaskKind, number>();

  let unparsed = $state(false);
  let fault = $state.raw<ParseFault | null>(null);
  let pendingText: string | null = null;
  let textTimer: ReturnType<typeof setTimeout> | null = null;

  const editable = $derived(desktop && opened !== null);
  const site = $derived<Site | undefined>(doc.model.sites[siteIndex]);
  const viewIds = $derived(siteViewIds(doc.model.sites));
  const viewId = $derived(viewIds[siteIndex] ?? '');
  const notes = $derived(site ? readOnlyNotes(site) : []);
  const legend = $derived<[string, string][]>(site ? [...typeColors(site)] : []);
  const needsReload = $derived(changedOnDisk || doc.externalChange);
  const hits = $derived<Span[]>(
    focus ? [focus] : site && selected ? blockSpans(site, selected) : []
  );
  const marks = $derived<CodeMark[]>(
    notes.map((note) => ({ span: note.span, reason: note.reason }))
  );
  const anchors = $derived<Span[]>(anchorSpans(doc.model.sites));
  const siteAnchor = $derived<Span | null>(anchors[siteIndex] ?? null);
  const emptyState = $derived.by(() => {
    if (failure !== null) return { title: 'that file did not open', reason: failure };
    return { title: 'nothing to show yet', reason: NO_SITE };
  });
  const empty = $derived(failure !== null || doc.model.sites.length === 0);
  const leftWidth = $derived(
    !leftOpen ? RAIL_WIDTH : leftTab === 'ai-agent' ? AGENT_WIDTH : SIDEBAR_WIDTH
  );
  const fitPadding = $derived<FitPadding>({
    top: `${BAR_HEIGHT + INSET + FIT_GAP}px`,
    right: `${(rightOpen ? INSPECTOR_WIDTH : RAIL_WIDTH) + INSET + FIT_GAP}px`,
    bottom: `${INSET + FIT_GAP}px`,
    left: `${leftWidth + INSET + FIT_GAP}px`
  });
  const shownSpecs = $derived(editable ? specs : specsFromSites(doc.model.sites, doc.path));
  const problems = $derived<Problem[]>(problemsOf(site, specs));
  const declared = $derived(site ? site.blocks.map((block) => block.var) : []);
  const selectedBlock = $derived(site?.blocks.find((block) => block.var === selected) ?? null);
  const selectedSpec = $derived.by(() => {
    const block = site?.blocks.find((candidate) => candidate.var === selected);
    return block ? specOfBlock(specs, block) : undefined;
  });
  const incompleteBlocks = $derived.by(() =>
    doc.model.sites.flatMap((candidate) =>
      candidate.blocks.flatMap((block) => {
        const spec = specOfBlock(specs, block);
        return spec && missingRequiredFields(block, spec).length > 0 ? [block.var] : [];
      })
    )
  );
  const incompleteNote = $derived(
    incompleteBlocks.length === 0
      ? null
      : `${incompleteBlocks.length} block${incompleteBlocks.length === 1 ? '' : 's'} missing required fields`
  );
  const diagnostics = $derived<Placed[]>(
    placeDiagnostics(parseDiagnostics(diagLines), doc.path, doc.source, doc.model.sites)
  );
  const compiled = $derived<Problem[]>(compileProblems(diagnostics));
  const blocked = $derived<string | null>(
    !editable ? viewerNote : needsReload ? DISK_NOTE : null
  );
  const tasks = $derived<Tasks>({
    check: {
      enabled: blocked === null && targetError === null && busy === null,
      hint: blocked ?? targetError ?? 'syntax-check the temporary draft with g++ (F7)'
    },
    build: {
      enabled:
        blocked === null &&
        busy === null &&
        target !== null &&
        targetError === null &&
        target.artifact?.state !== 'building',
      hint:
        blocked ??
        targetError ??
        (target === null
          ? 'finding the build target'
          : target.artifact?.state === 'building'
            ? `a build is already running for this document (job ${target.artifact.jobId})`
            : target.available !== true
              ? 'build the temporary draft (configures the build directory first) (Ctrl+B)'
              : 'build the temporary draft (Ctrl+B)')
    },
    run: {
      enabled:
        running ||
        (blocked === null &&
          incompleteNote === null &&
          busy === null &&
          target?.artifact?.state === 'ready' &&
          target?.available === true),
      hint:
        running
          ? 'stop the running example (Ctrl+R)'
          : blocked ??
            incompleteNote ??
            target?.reason ??
            targetError ??
            (target === null
              ? 'checking whether the current draft is built'
              : target.artifact?.state === 'building'
                ? `a build is already running for this document (job ${target.artifact.jobId})`
                : target.artifact?.state === 'needs_build'
                  ? target.artifact.reason
                  : 'run the built example (Ctrl+R)')
    }
  });

  $effect(() => {
    storeOpen(LEFT_PANEL, leftOpen);
    untrack(storePanels);
  });
  $effect(() => {
    storeOpen(RIGHT_PANEL, rightOpen);
    untrack(storePanels);
  });
  $effect(() => {
    storeOpen(DRAWER_PANEL, drawerOpen);
    untrack(storePanels);
  });
  $effect(() => {
    localStorage.setItem(DRAWER_HEIGHT, String(drawerHeight));
    untrack(storePanels);
  });
  $effect(() => {
    void rightTab;
    void leftTab;
    untrack(storePanels);
  });

  $effect(() => {
    if (!editable || !viewId) return;
    const activeView = viewId;
    untrack(() => {
      flowCache = { ...flowCache, activeView };
      persistCache();
    });
  });

  $effect(() => {
    if (!desktop) return;
    const name = fixtureName;
    void openExample(name);
  });

  $effect(() => {
    if (drawerOpen) drawerMounted = true;
  });

  $effect(() => {
    if (!editable) return;
    const pending = onExternalChange((path) => {
      if (path === doc.path) changedOnDisk = true;
    });
    return () => {
      void pending.then((unlisten) => unlisten());
    };
  });

  $effect(() => {
    const path = doc.path;
    const requestKey = targetKey(path, doc.model.sha256, targetRefresh);
    if (!editable) {
      target = null;
      targetError = null;
      return;
    }
    target = null;
    targetError = null;
    let stale = false;
    void findTarget(path).then(
      (found) => {
        if (stale || requestKey !== targetKey(doc.path, doc.model.sha256, targetRefresh)) return;
        target = found;
        targetError = null;
      },
      (error: unknown) => {
        if (stale || requestKey !== targetKey(doc.path, doc.model.sha256, targetRefresh)) return;
        target = null;
        targetError = describeApplyError(error);
      }
    );
    return () => {
      stale = true;
    };
  });

  $effect(() => {
    if (!editable) return;
    const streams = onTaskLine((kind, payload) => {
      if (payload.path !== doc.path) return;
      if (!acceptTask(kind, payload.jobId)) return;
      output = [...output, payload.line].slice(-OUTPUT_LINES);
      if (kind !== 'run') diagLines = [...diagLines, payload.line];
    });
    const ends = onTaskEnd((kind, payload) => {
      if (payload.path !== doc.path) return;
      if (!acceptTask(kind, payload.jobId)) return;
      latestJobs.set(kind, payload.jobId);
      activeJobs.delete(kind);
      if (kind === 'run') running = false;
      else {
        busy = null;
        if (kind === 'build') targetRefresh += 1;
      }
      output = [...output, `— ${kind} finished (exit ${payload.code ?? 'signal'})`];
      if (kind !== 'run' && payload.code === 0) flashFinished(kind);
      if (kind !== 'run' && payload.code !== 0) {
        taskFail =
          diagnostics.find((entry) => entry.severity === 'error')?.message ??
          `${kind} failed — the raw log is in Output`;
      }
    });
    const artifactChanges = onArtifactStatusChange((path) => {
      if (path === doc.path) targetRefresh += 1;
    });
    return () => {
      void Promise.all([streams, ends, artifactChanges]).then((pending) => {
        for (const unlisten of pending.flat()) unlisten();
      });
    };
  });

  $effect(() => {
    void refreshAiAgent();
  });

  $effect(() => {
    void loadAppSettings();
  });

  $effect(() => {
    if (!desktop) return;
    const deltas = onAiAgentDelta((payload) => {
      if (payload.path !== doc.path || pendingReply === null) return;
      const target = pendingReply;
      chat = chat.map((message) =>
        message.id === target ? { ...message, text: message.text + payload.text } : message
      );
    });
    const ends = onAiAgentDone((payload) => {
      if (payload.path !== doc.path) return;
      closeReply(payload.usage, payload.error);
    });
    const plans = onAiAgentProposal((payload) => void attachProposal(payload));
    const auth = onAiAgentAuthChanged((next) => {
      keyStatus = next;
    });
    return () => {
      void Promise.all([deltas, ends, plans, auth]).then((pending) => {
        for (const unlisten of pending) unlisten();
      });
    };
  });

  $effect(() => {
    const current = site;
    const key = current ? `${doc.path}#${viewId}` : '';
    if (!current) {
      nodes = [];
      edges = [];
      viewKey = '';
      return;
    }
    const fresh = projectSite(current, specs, editable, selected);
    if (untrack(() => viewKey) === key) {
      const merged = mergeProjection(untrack(() => ({ nodes, edges })), fresh, pinned);
      nodes = merged.nodes;
      edges = merged.edges;
      untrack(() => rememberPositions(key, merged.nodes, true));
      return;
    }
    let stale = false;
    pinned = new Map();
    layout(fresh).then(async (laid) => {
      if (stale) return;
      nodes = restorePositions(key, laid.nodes);
      edges = laid.edges;
      rememberPositions(key, nodes, true);
      viewKey = key;
      await tick();
      if (stale) return;
      void actions?.showView(layoutCache.viewportOf(key));
    });
    return () => {
      stale = true;
    };
  });

  function cacheEnv(): layoutCache.CacheEnv {
    return { editable, path: doc.path, flowCache, setFlowCache: (next) => (flowCache = next) };
  }

  function rememberPositions(
    key: string,
    current: { id: string; position: EdgePoint }[],
    complete = false
  ): void {
    layoutCache.rememberPositions(cacheEnv(), key, current, complete);
  }

  function movedNodes(key: string, current: { id: string; position: EdgePoint }[]): NodeMove[] {
    return layoutCache.movedNodes(cacheEnv(), key, current);
  }

  function applyNodeMoves(key: string, moves: NodeMove[], direction: 'from' | 'to'): void {
    const positions = layoutCache.applyNodeMoves(cacheEnv(), key, moves, direction);
    if (key === viewKey) {
      nodes = nodes.map((node) => {
        const position = positions.get(node.id);
        return position ? { ...node, position: { ...position } } : node;
      });
    }
  }

  async function finishNodeDrag(
    key: string,
    current: { id: string; position: EdgePoint }[]
  ): Promise<void> {
    const moves = movedNodes(key, current);
    rememberPositions(key, current);
    if (!editable || moves.length === 0) return;
    const outcome = await run((path) => moveNodes(path, cacheViewKey(key), moves));
    if (!outcome.ok) {
      applyNodeMoves(key, moves, 'from');
      storeLayout(key);
    }
  }

  function restorePositions(key: string, current: BlockNodeType[]): BlockNodeType[] {
    return layoutCache.restorePositions(cacheEnv(), key, current);
  }

  function rememberViewport(key: string, viewport: Viewport): void {
    layoutCache.rememberViewport(cacheEnv(), key, viewport);
  }

  function loadLayout(key: string): void {
    layoutCache.loadLayout(cacheEnv(), key);
  }

  function storeLayout(key: string): void {
    layoutCache.storeLayout(cacheEnv(), key);
  }

  function cacheViewKey(key: string): string {
    return layoutCache.cacheViewKey(doc.path, key);
  }

  function storePanels(): void {
    if (!editable) return;
    const next: layoutCache.UiCache = {
      ...flowCache,
      panels: {
        ...flowCache.panels,
        left: leftOpen,
        right: rightOpen,
        drawer: drawerOpen,
        drawerHeight,
        rightTab,
        leftTab,
        runArgs
      }
    };
    flowCache = next;
    layoutCache.persistCache(cacheEnv(), next);
  }

  function persistCache(): void {
    layoutCache.persistCache(cacheEnv());
  }

  function cancelCacheWrite(): void {
    layoutCache.cancelCacheWrite();
  }

  function syncPositionCache(value: unknown): void {
    flowCache = layoutCache.syncPositionCache(cacheEnv(), value);
    if (viewKey) nodes = restorePositions(viewKey, nodes);
  }

  function clampContext() {
    const count = doc.model.sites.length;
    if (count === 0) {
      siteIndex = 0;
      selected = null;
      return;
    }
    if (siteIndex >= count || siteIndex < 0) siteIndex = count - 1;
    if (count === 1) siteIndex = 0;
    const current = doc.model.sites[siteIndex];
    if (selected && !current?.blocks.some((block) => block.var === selected)) selected = null;
    if (selectedEdge && current && edgeIndexById(current, selectedEdge) === null) {
      selectedEdge = null;
    }
  }

  function adopt(next: DocumentState) {
    if (next.path === doc.path && next.revision < doc.revision) return;
    doc = next;
    clampContext();
  }

  function install(next: DocumentState, fresh: boolean) {
    // A burst still inside the debounce window was never sent anywhere, so leaving
    // the document drops it; flushing here would await the queue this may run in.
    if (textTimer !== null) clearTimeout(textTimer);
    textTimer = null;
    pendingText = null;
    unparsed = false;
    fault = null;
    if (fresh && pendingReply !== null) void aiAgentStop(doc.path).catch(() => undefined);
    doc = next;
    generation += 1;
    changedOnDisk = false;
    refusal = null;
    focus = null;
    void refreshPalette(next.path);
    if (fresh) {
      flowCache = cacheOf(next.cache);
      layoutCache.resetViewCache();
      const panels = flowCache.panels;
      if (typeof panels.left === 'boolean') leftOpen = panels.left;
      if (typeof panels.right === 'boolean') rightOpen = panels.right;
      if (typeof panels.drawer === 'boolean') drawerOpen = panels.drawer;
      if (
        typeof panels.drawerHeight === 'number' &&
        Number.isFinite(panels.drawerHeight) &&
        panels.drawerHeight >= MIN_DRAWER_HEIGHT
      ) {
        drawerHeight = panels.drawerHeight;
      }
      rightTab = panels.rightTab === 'library' ? 'library' : 'inspector';
      leftTab =
        panels.leftTab === 'settings' || panels.leftTab === 'ai-agent'
          ? panels.leftTab
          : editable
            ? 'ai-agent'
            : 'settings';
      runArgs = typeof panels.runArgs === 'string' ? panels.runArgs : '';
      const cachedSite = siteViewIds(next.model.sites).indexOf(flowCache.activeView ?? '');
      siteIndex = cachedSite >= 0 ? cachedSite : 0;
      selected = null;
      selectedEdge = null;
      chat = [];
      pendingReply = null;
    }
    clampContext();
    inspector?.discardDrafts();
    configList?.discardDrafts();
    diagLines = [];
    output = [];
    busy = null;
    running = false;
    taskFail = null;
    for (const [kind, jobId] of activeJobs) latestJobs.set(kind, jobId);
    activeJobs.clear();
    targetRefresh += 1;
    status = '';
  }

  function reset(next: DocumentState) {
    install(next, true);
  }

  function announce(message: string) {
    status = message;
    alerted += 1;
    alert = { text: message, at: alerted, tone: 'error' };
  }

  function hint(message: string) {
    alerted += 1;
    alert = { text: message, at: alerted, tone: 'note' };
  }

  // Asked for when the panel is first opened, not at startup: it is a network call for
  // a rail tab most sessions never reach.
  async function refreshModels() {
    if (!desktop) {
      agentModels = [];
      return;
    }
    try {
      agentModels = await aiAgentModels();
    } catch {
      agentModels = [];
    }
  }

  async function refreshAiAgent() {
    if (!desktop) {
      keyStatus = { available: false, provider: 'anthropic', model: '—', reason: NO_SHELL, method: null };
      return;
    }
    try {
      keyStatus = await aiAgentStatus();
    } catch (error) {
      keyStatus = {
        available: false,
        provider: 'anthropic',
        model: '—',
        reason: describeApplyError(error),
        method: null
      };
    }
  }

  async function setAiAgentModel(model: string) {
    try {
      libSettings = await setAppSettings({ ...libSettings, aiAgentModel: model });
      await refreshAiAgent();
    } catch (error) {
      announce(describeApplyError(error));
    }
  }

  async function setAiAgentProvider(provider: string) {
    if (provider === libSettings.aiAgentProvider) return;
    stopAiAgent();
    try {
      libSettings = await setAppSettings({
        ...libSettings,
        aiAgentProvider: provider,
        aiAgentModel: null
      });
      await refreshAiAgent();
      await refreshModels();
    } catch (error) {
      announce(describeApplyError(error));
    }
  }

  async function startOauth(): Promise<string | null> {
    if (!desktop) return NO_SHELL;
    try {
      await aiAgentOauthStart();
      return null;
    } catch (error) {
      return describeApplyError(error);
    }
  }

  async function logoutOauth() {
    if (!desktop) return;
    try {
      keyStatus = await aiAgentOauthLogout();
    } catch (error) {
      keyStatus = {
        available: false,
        provider: 'anthropic',
        model: '—',
        reason: describeApplyError(error),
        method: null
      };
    }
    pendingReply = null;
  }

  function retryAsk(id: number) {
    const at = chat.findIndex((message) => message.id === id);
    const asked = at > 0 ? chat[at - 1] : undefined;
    if (!asked || asked.role !== 'user') return;
    chat = chat.slice(0, at - 1);
    void askAiAgent(asked.text);
  }

  function closeReply(usage: Usage | null, error: string | null) {
    pendingReply = null;
    const last = chat.at(-1);
    if (!last || last.role !== 'assistant') return;
    chat = [...chat.slice(0, -1), { ...last, usage: usage ?? last.usage, error: error ?? last.error }];
  }

  async function askAiAgent(question: string) {
    if (!editable) {
      announce(viewerNote);
      return;
    }
    if (pendingReply !== null) return;
    const history = historyOf(chat);
    const asked: Message = {
      id: ++turns,
      role: 'user',
      text: question,
      usage: null,
      error: null,
      proposal: null
    };
    const reply: Message = {
      id: ++turns,
      role: 'assistant',
      text: '',
      usage: null,
      error: null,
      proposal: null
    };
    chat = [...chat, asked, reply];
    pendingReply = reply.id;
    try {
      await aiAgentAsk(doc.path, question, history, selected);
    } catch (error) {
      closeReply(null, describeApplyError(error));
    }
  }

  async function vetted(plan: Proposal, planned: number): Promise<Proposal> {
    try {
      const checked = await previewCommands(doc.path, plan.commands, planned);
      return {
        ...plan,
        baseRevision: planned,
        diff: diffLines(checked.diff),
        splices: checked.summary.splices,
        refusal: null,
        state: 'ready'
      };
    } catch (error) {
      const record = errorRecord(error);
      if (record?.error === 'references_outside_graph' && typeof record.block === 'string') {
        refusal = { block: record.block, spans: spansOf(record) };
      }
      return {
        ...plan,
        baseRevision: planned,
        diff: [],
        splices: 0,
        refusal: describeApplyError(error),
        state: 'refused'
      };
    }
  }

  async function attachProposal(payload: AiAgentProposal) {
    if (!editable || payload.path !== doc.path) return;
    const planned = doc.revision;
    const plan = await vetted(
      {
        rationale: payload.rationale,
        commands: payload.commands,
        baseRevision: planned,
        dropped: payload.dropped,
        diff: [],
        splices: 0,
        refusal: null,
        state: 'ready',
        appliedAt: null
      },
      planned
    );
    const last = chat.at(-1);
    if (last && last.role === 'assistant' && last.proposal === null) {
      chat = [...chat.slice(0, -1), { ...last, proposal: plan }];
      return;
    }
    chat = [
      ...chat,
      { id: ++turns, role: 'assistant', text: '', usage: null, error: null, proposal: plan }
    ];
  }

  function planOf(id: number): Proposal | null {
    return chat.find((message) => message.id === id)?.proposal ?? null;
  }

  function setProposal(id: number, next: Proposal) {
    chat = chat.map((message) => (message.id === id ? { ...message, proposal: next } : message));
  }

  async function acceptProposal(id: number) {
    const plan = planOf(id);
    if (!plan || plan.state !== 'ready') return;
    if (plan.baseRevision !== doc.revision) {
      announce(MOVED_ON);
      return;
    }
    const outcome = await submitAll(plan.commands);
    const settled = planOf(id);
    if (!settled) return;
    if (!outcome.ok) {
      setProposal(id, { ...settled, refusal: outcome.message, state: 'refused' });
      return;
    }
    setProposal(id, { ...settled, state: 'accepted', appliedAt: doc.revision });
  }

  function rejectProposal(id: number) {
    const plan = planOf(id);
    if (!plan || plan.state === 'accepted') return;
    setProposal(id, { ...plan, state: 'rejected' });
  }

  async function replanProposal(id: number) {
    const plan = planOf(id);
    if (!plan || plan.state !== 'ready') return;
    setProposal(id, await vetted(plan, doc.revision));
  }

  function stopAiAgent() {
    pendingReply = null;
    void aiAgentStop(doc.path).catch(() => undefined);
  }

  function pickRailTab(next: RailTab) {
    rightTab = next;
    rightOpen = true;
  }

  function pickLeftTab(next: RailTab) {
    leftTab = next;
    leftOpen = true;
    if (next === 'ai-agent' && agentModels.length === 0) void refreshModels();
  }

  function toggleAiAgent() {
    if (leftOpen && leftTab === 'ai-agent') {
      leftOpen = false;
      return;
    }
    leftTab = 'ai-agent';
    leftOpen = true;
    if (agentModels.length === 0) void refreshModels();
  }

  function toggleChrome() {
    const previous = stashed;
    if (previous) {
      leftOpen = previous[0];
      rightOpen = previous[1];
      drawerOpen = previous[2];
      stashed = null;
      return;
    }
    if (!leftOpen && !rightOpen && !drawerOpen) {
      leftOpen = true;
      return;
    }
    stashed = [leftOpen, rightOpen, drawerOpen];
    leftOpen = false;
    rightOpen = false;
    drawerOpen = false;
  }

  async function attempt(
    action: (path: string) => Promise<DocumentState>,
    take: (next: DocumentState) => void
  ): Promise<Outcome> {
    if (!editable) return refuse(viewerNote);
    const era = generation;
    try {
      const next = await action(doc.path);
      status = '';
      take(next);
      return { ok: true };
    } catch (error) {
      const message = describeApplyError(error);
      if (message.includes(DISK_DRIFT) && era === generation) changedOnDisk = true;
      announce(message);
      return { ok: false, message, record: errorRecord(error) };
    }
  }

  function run(action: (path: string) => Promise<DocumentState>): Promise<Outcome> {
    // The buffer is ahead of the model and cannot be reconciled while it does not
    // parse; a gesture applied now would be spliced away by the next commit.
    if (unparsed) return Promise.resolve(refuse(UNPARSED_LOCK));
    const path = doc.path;
    return queued(path, () => attempt(action, adopt));
  }

  async function runHistory(action: (path: string) => Promise<DocumentState>): Promise<Outcome> {
    cancelCacheWrite();
    await flushText();
    if (unparsed) return refuse(UNPARSED_LOCK);
    const path = doc.path;
    return queued(path, () =>
      attempt(action, (next) => {
        syncPositionCache(next.cache);
        adopt(next);
      })
    );
  }

  function discardEdit() {
    if (textTimer !== null) clearTimeout(textTimer);
    textTimer = null;
    pendingText = null;
    unparsed = false;
    fault = null;
    status = 'edit discarded, the code is back to the last version that parsed';
  }

  function noteEdit(next: string) {
    if (textTimer !== null) clearTimeout(textTimer);
    pendingText = next;
    textTimer = setTimeout(() => void flushText(), TEXT_DELAY);
  }

  // Text lands before any gesture that reads or rewrites the model, so the two
  // never race for the same revision.
  function flushText(): Promise<Outcome> {
    if (textTimer !== null) {
      clearTimeout(textTimer);
      textTimer = null;
    }
    const text = pendingText;
    if (text === null) return Promise.resolve({ ok: true });
    pendingText = null;
    return queued(doc.path, () =>
      // The base is read here, inside the queue: an earlier flush may still have
      // been in flight when this one was armed.
      attempt(async (path) => {
        const outcome = await editSource(path, text, doc.revision);
        unparsed = outcome.unparsed;
        fault = outcome.fault;
        return outcome.state;
      }, adopt)
    );
  }

  async function resync(message: string) {
    try {
      install(await openDocument(doc.path), false);
    } catch {
      return;
    }
    status = message;
  }

  async function reload() {
    const outcome = await attempt(reloadDocument, (next) => install(next, false));
    if (!outcome.ok) changedOnDisk = true;
  }

  async function discardDraft() {
    if (!doc.dirty) return;
    if (!confirm('Discard the draft and restore the saved file?')) return;
    await reload();
    hint('draft discarded, saved file restored');
  }

  async function newFile() {
    if (!desktop) {
      announce('new file needs the desktop shell');
      return;
    }
    const picked = await pickSavePath('flowgraph.cpp');
    if (!picked) return;
    try {
      const next = await newDocument(picked);
      opened = picked;
      install(next, true);
      hint(`created ${picked.split('/').pop() ?? picked}`);
    } catch (error) {
      announce(describeApplyError(error));
    }
  }

  async function loadAppSettings() {
    if (!desktop) return;
    try {
      libSettings = await appSettings();
    } catch {
      libSettings = { clerRoot: null, blockLibraries: [], aiAgentModel: null, aiAgentProvider: null, aiAgentBaseUrl: null };
    }
  }

  async function updateLibraries(next: AppSettings) {
    try {
      libSettings = await setAppSettings(next);
      void refreshPalette(doc.path);
      hint('libraries updated');
    } catch (error) {
      announce(describeApplyError(error));
    }
  }

  async function pickClerRoot() {
    const dir = await pickFolder();
    if (dir) await updateLibraries({ ...libSettings, clerRoot: dir });
  }

  async function addLibrary() {
    const dir = await pickFolder();
    if (dir && !libSettings.blockLibraries.includes(dir)) {
      await updateLibraries({
        ...libSettings,
        blockLibraries: [...libSettings.blockLibraries, dir]
      });
    }
  }


  function openExampleByName(name: string) {
    if (desktop && name === fixtureName) return;
    fixtureName = name;
    if (!desktop) openFixture();
  }

  async function saveAs() {
    if (!desktop) {
      announce('save as needs the desktop shell');
      return;
    }
    const picked = await pickSavePath(doc.path);
    if (!picked) return;
    const outcome = await attempt((path) => saveDocumentAs(path, picked), (next) => {
      opened = picked;
      install(next, true);
    });
    if (outcome.ok) {
      hint(`saved as ${picked.split('/').pop() ?? picked}`);
    }
  }

  async function save() {
    if (document.activeElement instanceof HTMLElement) document.activeElement.blur();
    if (!(await flushText()).ok) return;
    if (unparsed) {
      announce(UNSAVEABLE);
      return;
    }
    const outcome = await run(saveDocument);
    if (outcome.ok) hint('saved to source');
  }

  function discardOnReload() {
    inspector?.discardDrafts();
    configList?.discardDrafts();
  }

  async function submitAll(commands: Command[]): Promise<Outcome> {
    if (commands.length === 0) return { ok: true };
    await flushText();
    const planned = doc.revision;
    const outcome = await run((path) => applyCommands(path, commands, planned));
    if (!outcome.ok && outcome.record?.error === 'revision_mismatch') {
      await resync(outcome.message);
    }
    return outcome;
  }

  function submit(command: Command): Promise<Outcome> {
    return submitAll([command]);
  }

  function refuse(message: string): Outcome {
    announce(message);
    return { ok: false, message, record: null };
  }

  function submitPlan(plan: ConnectPlan): Promise<Outcome> {
    if ('refusal' in plan) return Promise.resolve(refuse(plan.refusal));
    return submitAll(plan.commands);
  }

  async function refreshPalette(path: string) {
    if (!editable) {
      specs = [];
      return;
    }
    try {
      specs = await loadPalette(path);
    } catch {
      specs = [];
    }
  }

  async function openFile() {
    if (!desktop) {
      announce('file dialog needs the desktop shell — pick an example below');
      return;
    }
    status = '';
    try {
      const path = await pickFile();
      if (!path) return;
      const request = ++opening;
      const previous = opened;
      const next = await openDocument(path);
      if (request !== opening) return;
      opened = path;
      failure = null;
      if (previous && previous !== path) void closeDocument(previous).catch(() => undefined);
      reset(next);
    } catch (error) {
      failure = describeApplyError(error);
      announce(failure);
    }
  }

  async function openExample(name: string) {
    const request = ++opening;
    try {
      const next = await openDocument(loadFixture(name).path);
      if (request !== opening) return;
      const previous = opened;
      opened = next.path;
      failure = null;
      if (previous && previous !== next.path) void closeDocument(previous).catch(() => undefined);
      reset(next);
    } catch (error) {
      if (request !== opening) return;
      failure = describeApplyError(error);
      announce(failure);
    }
  }

  function openFixture() {
    if (desktop) return;
    const previous = opened;
    opened = null;
    failure = null;
    if (previous) void closeDocument(previous).catch(() => undefined);
    reset(loadFixture(fixtureName));
  }

  function selectNode(id: string) {
    selected = id;
    selectedEdge = null;
    focus = null;
    rightOpen = true;
  }

  function selectEdge(id: string) {
    selectedEdge = id;
    focus = null;
    edges = edges.map((edge) => ({ ...edge, selected: edge.id === id }));
  }

  function clearSelection() {
    selected = null;
    selectedEdge = null;
    focus = null;
  }

  function edgeInfo(id: string): EdgeInfo | null {
    const edge = site ? edgeAtId(site, id) : null;
    if (!edge) return null;
    const type = edge.type_conflict
      ? `type conflict: ${edge.source_type ?? '?'} → ${edge.sample_type ?? '?'}`
      : (edge.sample_type ?? edge.source_type ?? 'type unresolved');
    return {
      title: `${edge.from} → ${edge.to}.${edge.port.name}${edge.port.index === null ? '' : `[${edge.port.index}]`}`,
      detail: type,
      editable: edge.editable,
      reason: edge.read_only_reason ? `this wire is read-only: ${edge.read_only_reason.replace(/_/g, ' ')}` : null
    };
  }

  function wire(connection: {
    source: string;
    target: string;
    targetHandle?: string | null;
  }): void {
    if (!site) return;
    const slot = parsePortId(connection.targetHandle ?? 'in');
    const wanted: Wire = {
      from: connection.source,
      to: connection.target,
      port: slot.port,
      portIndex: slot.index
    };
    void submitPlan(connectPlan(siteIndex, site, specs, wanted));
  }

  function rewire(
    previous: { id: string },
    next: { source: string; target: string; targetHandle?: string | null }
  ): void {
    if (!site) return;
    const index = edgeIndexById(site, previous.id);
    if (index === null) return;
    const slot = parsePortId(next.targetHandle ?? 'in');
    void submitPlan(
      reconnectPlan(siteIndex, site, specs, index, {
        from: next.source,
        to: next.target,
        port: slot.port,
        portIndex: slot.index
      })
    );
  }

  function disconnect(id: string): void {
    if (!site) return;
    const edge = edgeAtId(site, id);
    const index = edgeIndexById(site, id);
    if (!edge || index === null) return;
    if (!edge.editable) {
      refuse(edgeInfo(id)?.reason ?? 'this wire is read-only');
      return;
    }
    selectedEdge = null;
    void submit({ command: 'disconnect', site: siteIndex, edge: index });
  }

  function removeFromGraph(block: string): void {
    void submit({ command: 'remove_from_graph', site: siteIndex, block });
  }

  async function deleteBlock(block: string): Promise<void> {
    const outcome = await submit({ command: 'delete_block', site: siteIndex, block });
    if (outcome.ok) return;
    if (outcome.record?.error !== 'references_outside_graph') return;
    refusal = { block, spans: spansOf(outcome.record) };
  }

  function jumpTo(span: Span) {
    focus = span;
    drawerOpen = true;
    tab = 'code';
  }

  function acceptTask(kind: TaskKind, jobId: number): boolean {
    if (kind === 'run' ? !running : busy !== kind) return false;
    const active = activeJobs.get(kind);
    if (active !== undefined) return active === jobId;
    if (jobId <= (latestJobs.get(kind) ?? 0)) return false;
    activeJobs.set(kind, jobId);
    latestJobs.set(kind, jobId);
    return true;
  }

  function flashFinished(kind: 'check' | 'build') {
    clearTimeout(justFinishedTimer);
    justFinished = kind;
    justFinishedTimer = setTimeout(() => (justFinished = null), 1400);
  }

  async function task(kind: TaskKind, action: (path: string) => Promise<TaskStarted>) {
    await flushText();
    if (unparsed) {
      announce(UNPARSED_LOCK);
      return;
    }
    output = [];
    taskFail = null;
    clearTimeout(justFinishedTimer);
    justFinished = null;
    if (kind !== 'run') diagLines = [];
    if (kind === 'run') running = true;
    else busy = kind;
    tab = kind === 'check' ? 'diagnostics' : 'output';
    drawerOpen = true;
    try {
      const started = await action(doc.path);
      if (kind === 'run' ? running : busy === kind) {
        activeJobs.set(kind, started.jobId);
        latestJobs.set(kind, started.jobId);
      }
    } catch (error) {
      activeJobs.delete(kind);
      if (kind === 'run') running = false;
      else busy = null;
      taskFail = describeApplyError(error);
      announce(taskFail);
    }
  }

  async function toggleRun() {
    if (!running) {
      const args = runArgs.trim().split(/\s+/).filter(Boolean);
      await task('run', (path) => runTarget(path, args));
      return;
    }
    try {
      await stopTarget(doc.path);
    } catch (error) {
      announce(describeApplyError(error));
    }
  }

  function pickDiagnostic(entry: Placed) {
    if (entry.site !== null) siteIndex = entry.site;
    if (entry.block) selectNode(entry.block);
    if (entry.span) jumpTo(entry.span);
  }

  function pickProblem(problem: Problem) {
    if (problem.edge) {
      if (problem.block) selected = problem.block;
      selectEdge(problem.edge);
      return;
    }
    if (problem.block) selectNode(problem.block);
    jumpTo(problem.span);
  }

  async function addBlock(
    spec: BlockSpec,
    form: AddForm,
    at: EdgePoint
  ): Promise<FieldRefusal | null> {
    const varName = form.varName.trim();
    pinned.set(varName, at);
    const commands: Command[] = [addBlockCommand(siteIndex, spec, form)];
    if (spec.is_gui && doc.model.sites[siteIndex]?.gui === null) {
      commands.push({ command: 'materialize_gui', site: siteIndex });
    }
    const outcome = await submitAll(commands);
    if (outcome.ok) {
      rightTab = 'inspector';
      selectNode(varName);
      return null;
    }
    pinned.delete(varName);
    return addRefusal(outcome.record, form) ?? { field: null, message: outcome.message };
  }

  function droppedSpec(event: DragEvent): BlockSpec | null {
    const name = event.dataTransfer?.getData(DRAG_TYPE);
    return name ? (specFor(shownSpecs, name) ?? null) : null;
  }

  function onDrop(event: DragEvent) {
    const spec = droppedSpec(event);
    if (!spec) return;
    event.preventDefault();
    if (!editable) {
      announce(viewerNote);
      return;
    }
    adder?.placeAt(event.clientX, event.clientY, spec);
  }

  function onDragOver(event: DragEvent) {
    if (!event.dataTransfer?.types.includes(DRAG_TYPE)) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = 'copy';
  }

  function addHere(clientX: number, clientY: number) {
    adder?.openAt(clientX, clientY);
  }

  function declarationOf(blockVar: string): Span | null {
    return site?.blocks.find((block) => block.var === blockVar)?.span ?? null;
  }

  function viewSource(blockVar: string) {
    selectNode(blockVar);
    drawerOpen = true;
  }

  async function copyDeclaration(blockVar: string) {
    const span = declarationOf(blockVar);
    if (!span) return;
    try {
      await navigator.clipboard.writeText(doc.source.slice(span.start, span.end));
      status = '';
    } catch {
      announce('the clipboard is not available here');
    }
  }

  function blockSourcePath(blockVar: string): string | null {
    const block = site?.blocks.find((candidate) => candidate.var === blockVar);
    const spec = block ? specOfBlock(specs, block) : undefined;
    const origin = spec?.origin;
    return origin && origin !== doc.path ? origin : null;
  }

  async function openBlockSource(blockVar: string) {
    if (!desktop) {
      announce('opening an editor needs the desktop shell');
      return;
    }
    const origin = blockSourcePath(blockVar);
    if (!origin) return;
    try {
      await openInEditor(origin, 1);
    } catch (error) {
      announce(describeApplyError(error));
    }
  }

  async function openEditor(blockVar: string) {
    if (!desktop) {
      announce('opening an editor needs the desktop shell');
      return;
    }
    if (!editable) {
      announce(viewerNote);
      return;
    }
    const span = declarationOf(blockVar);
    if (!span) return;
    try {
      await openInEditor(doc.path, lineOfOffset(doc.source, span.start));
      status = '';
    } catch (error) {
      announce(describeApplyError(error));
    }
  }

  function readable(text: string): string {
    return text.replace(/_/g, ' ');
  }

  function pickInCode(offset: number) {
    const target = targetAt(doc.model.sites, offset);
    if (!target) return;
    siteIndex = target.siteIndex;
    selectNode(target.block);
  }

</script>

<svelte:window onclick={() => (pathsMenuOpen = false)} />

<div
  class="shell"
  style="--rail-left: {leftWidth}px; --rail-right: {rightOpen
    ? INSPECTOR_WIDTH
    : RAIL_WIDTH}px"
>
  {#if leftTab === 'ai-agent'}
    <AiAgent
      open={leftOpen}
      status={keyStatus}
      messages={chat}
      pending={pendingReply}
      enabled={editable}
      note={viewerNote}
      revision={doc.revision}
      {selected}
      models={agentModels}
      ontoggle={() => (leftOpen = !leftOpen)}
      ontab={pickLeftTab}
      onask={(question) => void askAiAgent(question)}
      onstop={stopAiAgent}
      onsignin={startOauth}
      onlogout={() => void logoutOauth()}
      onmodel={(model) => void setAiAgentModel(model)}
      onprovider={(provider) => void setAiAgentProvider(provider)}
      onretry={retryAsk}
      onaccept={(id) => void acceptProposal(id)}
      onreject={rejectProposal}
      onreplan={(id) => void replanProposal(id)}
    />
  {:else}
  <aside class="sidebar" class:collapsed={!leftOpen}>
    <div class="head">
      <RailTabs tab="settings" side="left" ontab={pickLeftTab} />
      <button
        class="toggle"
        data-testid="toggle-left"
        aria-expanded={leftOpen}
        aria-label={leftOpen ? 'Collapse sidebar' : 'Expand sidebar'}
        title={leftOpen ? 'Collapse sidebar  [' : 'Expand sidebar  ['}
        onclick={() => (leftOpen = !leftOpen)}>{leftOpen ? '‹' : '›'}</button
      >
    </div>

    <div class="body">
      {#if doc.model.sites.length > 1}
        <section>
          <h2>Site</h2>
          <select data-testid="site-select" bind:value={siteIndex}>
            {#each doc.model.sites as candidate, i (viewIds[i] ?? i)}
              <option value={i}>{siteLabel(candidate)}</option>
            {/each}
          </select>
        </section>
      {/if}

      <section>
        <h2>Run arguments</h2>
        <input
          class="runargs"
          data-testid="run-args"
          type="text"
          placeholder="command-line args"
          title="command-line arguments passed to the binary on Run"
          value={runArgs}
          onchange={(event) => {
            runArgs = event.currentTarget.value;
            storePanels();
          }}
        />
      </section>

      {#if site?.config}
        <section>
          <h2>
            Flowgraph config
            {#if site.config.source !== 'absent'}
              <span class="config-source">{site.config.source}</span>
            {/if}
          </h2>
          {#if configFields(siteIndex, site.config).length === 0}
            <p class="muted">no direct assignments</p>
          {:else}
            <FieldList
              bind:this={configList}
              scope={`${doc.path}::${siteIndex}::`}
              fields={configFields(siteIndex, site.config)}
              ownerReason={site.config.read_only_reason}
              enabled={editable}
              {submit}
            />
          {/if}
        </section>
      {/if}


      {#if site}
        <section>
          <TypeLegend entries={legend} />
        </section>
      {/if}

      {#if desktop}
        <section data-testid="libraries">
          <h2>
            Library block paths
            <span class="section-menu-slot">
              <button
                class="section-menu"
                data-testid="block-paths-menu"
                aria-expanded={pathsMenuOpen}
                title="manage block search paths"
                onclick={(event) => {
                  event.stopPropagation();
                  pathsMenuOpen = !pathsMenuOpen;
                }}>⋯</button
              >
              {#if pathsMenuOpen}
                <div class="panel-menu" data-testid="block-paths-menu-list">
                  <button
                    data-testid="pick-cler-root"
                    onclick={() => {
                      pathsMenuOpen = false;
                      void pickClerRoot();
                    }}>Set cler root…</button
                  >
                  {#if libSettings.clerRoot}
                    <button
                      data-testid="clear-cler-root"
                      onclick={() => {
                        pathsMenuOpen = false;
                        void updateLibraries({ ...libSettings, clerRoot: null });
                      }}>Use automatic cler root</button
                    >
                  {/if}
                  <button
                    data-testid="add-library"
                    onclick={() => {
                      pathsMenuOpen = false;
                      void addLibrary();
                    }}>Add block library…</button
                  >
                </div>
              {/if}
            </span>
          </h2>
          <dl>
            <dt title="git checkout version for compiling and blocks">cler root</dt>
            <dd class="path root-path">
              {libSettings.clerRoot ?? (resolvedRoot ? `auto — ${resolvedRoot}` : 'auto')}
            </dd>
          </dl>
          {#each libSettings.blockLibraries as library (library)}
            <div class="lib-row">
              <span class="path" title={library}>{library}</span>
              <button
                class="lib-remove"
                data-library-remove={library}
                title="remove this block library"
                onclick={() =>
                  void updateLibraries({
                    ...libSettings,
                    blockLibraries: libSettings.blockLibraries.filter(
                      (entry) => entry !== library
                    )
                  })}>✕</button
              >
            </div>
          {/each}
        </section>
      {/if}

      {#if status}
        <p class="status" data-testid="status">{status}</p>
      {/if}
    </div>
  </aside>
  {/if}

  <main>
    {#if needsReload}
      <div class="banner" data-testid="reload-banner">
        <span
          >This file changed on disk. Reloading replaces the open document and
          <strong>discards the undo history</strong>.</span
        >
        <button onpointerdown={discardOnReload} onclick={reload}>Reload</button>
      </div>
    {/if}

    <div class="canvas" data-canvas tabindex="-1">
      <SvelteFlow
          bind:nodes
          bind:edges
          {nodeTypes}
          {edgeTypes}
          colorMode="dark"
          fitViewOptions={{ padding: fitPadding }}
          nodesConnectable={editable}
          elementsSelectable={true}
          deleteKey={null}
          minZoom={0.1}
          proOptions={{ hideAttribution: true }}
          ondrop={onDrop}
          ondragover={onDragOver}
          onnodedragstop={({ nodes: moved }) => void finishNodeDrag(viewKey, moved)}
          onmoveend={(_event, viewport) => rememberViewport(viewKey, viewport)}
          onnodeclick={({ node }) => selectNode(node.id)}
          onedgeclick={({ edge }) => selectEdge(edge.id)}
          onpaneclick={clearSelection}
          onconnectend={(_event, state) => {
            if (!state.toHandle) hint(DROP_HINT);
          }}
          onbeforeconnect={(connection) => {
            wire(connection);
            return null;
          }}
          onbeforereconnect={(next, previous) => {
            rewire(previous, next);
            return null;
          }}
        >
          <Background bgColor="var(--bg-0)" patternColor="var(--border)" gap={18} size={2} />
          <Actions
            bind:this={actions}
            path={opened ?? doc.path}
            examples={fixtureNames}
            onnew={() => void newFile()}
            onexample={openExampleByName}
            canUndo={doc.canUndo}
            canRedo={doc.canRedo}
            canSave={editable && !needsReload}
            canOpenEditor={editable}
            canEdit={editable}
            dirty={doc.dirty}
            demo={!editable}
            editNote={viewerNote}
            saveNote={!editable
              ? viewerNote
              : needsReload
                ? 'reload the external change before saving'
                : doc.dirty
                  ? 'write the draft to the source file (Ctrl+S)'
                  : 'save the current document (Ctrl+S)'}
            {alert}
            {leftOpen}
            {rightOpen}
            {fitPadding}
            selectedNode={selected}
            {selectedEdge}
            {problems}
            {compiled}
            {tasks}
            {running}
            onaddtograph={(block) => void submit({ command: 'add_to_graph', site: siteIndex, block })}
            runsAt={(block) =>
              site?.runners.some((runner) => runner.block === block) ?? false}
            ondiscarddraft={() => void discardDraft()}
            onsaveas={() => void saveAs()}
            edgeAt={edgeInfo}
            oncheck={() => void task('check', checkDocument)}
            onbuild={() => void task('build', buildTarget)}
            onrun={() => void toggleRun()}
            onsave={() => void save()}
            onundo={() => void runHistory(undoDocument)}
            onredo={() => void runHistory(redoDocument)}
            onopen={() => void openFile()}
            ontoggleleft={() => (leftOpen = !leftOpen)}
            ontoggleright={() => (rightOpen = !rightOpen)}
            ontoggledrawer={() => (drawerOpen = !drawerOpen)}
            ontoggleaiagent={toggleAiAgent}
            ontogglechrome={toggleChrome}
            onviewsource={viewSource}
            oncopydeclaration={(block) => void copyDeclaration(block)}
            onopeneditor={(block) => void openEditor(block)}
            onremove={removeFromGraph}
            onopenblocksource={(block) => void openBlockSource(block)}
            blockSourceAt={blockSourcePath}
            ondeleteblock={(block) => void deleteBlock(block)}
            ondisconnect={disconnect}
            onaddhere={addHere}
            onproblem={pickProblem}
          />
          <AddBlock
            bind:this={adder}
            specs={shownSpecs}
            documentPath={doc.path}
            taken={declared}
            onadd={addBlock}
          />
          {#if nodes.length >= MINIMAP_MIN}
            <MiniMap bgColor="var(--bg-1)" maskColor="var(--scrim)" nodeColor="var(--border-hi)" />
          {/if}
      </SvelteFlow>

      {#if empty}
        <div class="empty" data-testid="empty-state">
          <h2>{emptyState.title}</h2>
          <p data-testid="empty-reason">{emptyState.reason}</p>
          <div class="choices">
            <button class="primary" data-testid="empty-open" onclick={openFile}
              >Open a .cpp file (Ctrl+O)</button
            >
            <label class="browse">
              Browse examples
              <select
                data-testid="empty-examples"
                bind:value={fixtureName}
                onchange={openFixture}
              >
                {#each fixtureNames as name (name)}
                  <option value={name}>{name}</option>
                {/each}
              </select>
            </label>
          </div>
        </div>
      {/if}

      {#if refusal}
        <div class="dialog" role="dialog" aria-modal="true" data-testid="delete-refusal">
          <h2>{refusal.block} cannot be deleted</h2>
          <p>
            {`Its declaration is still referenced in ${refusal.spans.length} ${refusal.spans.length === 1 ? 'place' : 'places'} outside the flowgraph.`}
            Remove those references first — the editor will not rewrite code it does not own.
          </p>
          <ul>
            {#each refusal.spans as span (span.start)}
              <li>
                <button data-reference={span.start} onclick={() => jumpTo(span)}>
                  <span class="line">line {lineOfOffset(doc.source, span.start)}</span>
                  <code>{lineTextAt(doc.source, span.start)}</code>
                </button>
              </li>
            {/each}
          </ul>
          <footer>
            <button data-testid="refusal-close" onclick={() => (refusal = null)}>Close</button>
          </footer>
        </div>
      {/if}

      {#if !drawerOpen && !progressOn}
        <button
          class="drawer-toggle"
          data-testid="drawer-toggle"
          aria-label="Expand code drawer"
          title="Expand code drawer  Ctrl+`"
          onclick={() => (drawerOpen = true)}>⌃ Code</button
        >
      {/if}

      {#if progressOn}
        <BuildProgress
          kind={busy ?? (running ? 'run' : null)}
          done={justFinished}
          error={taskFail}
          docked={drawerOpen}
          bottom={drawerOpen ? drawerHeight - STRIP_H + 12 : 12}
          onstop={() => void toggleRun()}
          ondiagnostics={() => {
            drawerOpen = true;
            tab = 'diagnostics';
          }}
          ondismiss={() => (taskFail = null)}
          onopen={() => (drawerOpen = true)}
        />
      {/if}

      {#if drawerMounted}
        <CodeDrawer
          open={drawerOpen}
          source={doc.source}
          path={doc.path}
          revision={doc.revision}
          readOnly={notes.length}
          writable={editable}
          {unparsed}
          {fault}
          {notes}
          ondiscard={discardEdit}
          {hits}
          {marks}
          {anchors}
          {siteAnchor}
          height={drawerHeight}
          inset={progressOn && drawerOpen ? STRIP_H : 0}
          {tab}
          {diagnostics}
          {output}
          {busy}
          onpick={pickInCode}
          onedit={noteEdit}
          ontoggle={() => (drawerOpen = !drawerOpen)}
          onheight={(next) => (drawerHeight = next)}
          ontab={(next: Tab) => (tab = next)}
          ondiagnostic={pickDiagnostic}
        />
      {/if}
    </div>
  </main>

  {#if rightTab === 'library'}
    <Palette
      specs={shownSpecs}
      documentPath={doc.path}
      enabled={editable}
      open={rightOpen}
      ontoggle={() => (rightOpen = !rightOpen)}
      ontab={pickRailTab}
    />
  {:else}
    <Inspector
      bind:this={inspector}
      path={doc.path}
      {site}
      {siteIndex}
      {selected}
      spec={selectedSpec}
      enabled={editable}
      {submit}
      open={rightOpen}
      ontoggle={() => (rightOpen = !rightOpen)}
      ontab={pickRailTab}
    />
  {/if}
</div>

<style>
  .shell {
    position: relative;
    height: 100%;
  }
  .sidebar {
    position: absolute;
    z-index: 8;
    top: calc(var(--bar-h) + var(--sp-3));
    left: var(--sp-3);
    bottom: var(--sp-3);
    width: var(--rail-left);
    background: var(--glass);
    backdrop-filter: blur(12px);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    box-shadow: var(--shadow);
    overflow: hidden;
    display: flex;
    flex-direction: column;
    transition: width 150ms ease;
  }
  .head {
    display: flex;
    align-items: center;
    gap: var(--sp-2);
    padding: var(--sp-3);
  }
  .collapsed .head {
    padding: var(--sp-2);
  }
  .collapsed :global(.tabs) {
    display: none;
  }
  .toggle {
    margin-left: auto;
    flex: none;
    width: 26px;
    padding: var(--sp-0) 0;
    font-size: 14px;
    line-height: 1.1;
    color: var(--muted);
  }
  .collapsed .toggle {
    margin-left: 0;
  }
  .body {
    flex: 1;
    min-height: 0;
    width: 280px;
    padding: 0 var(--sp-3) var(--sp-3);
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: var(--sp-3);
    transition:
      opacity 120ms ease,
      visibility 150ms;
  }
  .collapsed .body {
    opacity: 0;
    visibility: hidden;
  }
  h2 {
    display: flex;
    align-items: center;
    gap: var(--sp-2);
    margin: 0 0 var(--sp-2);
    font-size: 11px;
    letter-spacing: 0.09em;
    text-transform: uppercase;
    color: var(--muted);
    font-weight: 600;
  }
  .path {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
    word-break: break-all;
  }
  .runargs {
    width: 100%;
    padding: var(--sp-1) var(--sp-2);
    background: var(--bg-2);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    font-family: var(--mono);
    font-size: 11px;
    color: var(--fg);
  }
  .config-source {
    text-transform: none;
    letter-spacing: 0;
    font-family: var(--mono);
    font-weight: 400;
  }
  .root-path {
    word-break: normal;
    overflow-wrap: anywhere;
  }
  .section-menu-slot {
    position: relative;
    margin-left: auto;
    display: inline-flex;
  }
  .section-menu {
    padding: 0 var(--sp-1);
    background: transparent;
    border-color: transparent;
    font-size: 12px;
    line-height: 1;
    color: var(--muted);
  }
  .section-menu:hover {
    background: var(--bg-2);
  }
  .panel-menu {
    position: absolute;
    top: calc(100% + var(--sp-0));
    right: 0;
    z-index: 30;
    display: flex;
    flex-direction: column;
    min-width: 190px;
    padding: var(--sp-0);
    background: var(--bg-1);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    box-shadow: var(--shadow);
  }
  .panel-menu button {
    justify-content: flex-start;
    text-align: left;
    width: 100%;
    padding: var(--sp-0) var(--sp-2);
    background: transparent;
    border: none;
    font-size: 12px;
    text-transform: none;
    letter-spacing: normal;
    color: var(--fg);
  }
  .panel-menu button:hover {
    background: var(--bg-2);
  }
  .lib-row {
    display: flex;
    align-items: center;
    gap: var(--sp-2);
    min-width: 0;
  }
  .lib-row .path {
    flex: 1;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .lib-remove {
    flex: none;
    padding: 0 var(--sp-1);
    font-size: 11px;
    color: var(--muted);
  }
  dl {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 0 var(--sp-3);
    margin: var(--sp-2) 0 0;
    align-items: baseline;
  }
  dt {
    color: var(--muted);
  }
  dd {
    margin: 0;
    font-family: var(--mono);
    font-size: 11px;
  }
  .notes {
    margin: 0;
    padding: 0;
    list-style: none;
    display: flex;
    flex-direction: column;
    gap: var(--sp-1);
  }
  .notes li {
    display: flex;
    flex-direction: column;
    border-left: 2px solid var(--faint);
    padding-left: var(--sp-2);
  }
  .el {
    font-size: 11px;
  }
  .reason {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
  }
  .muted {
    margin: 0;
    color: var(--muted);
    font-size: 11px;
  }
  .status {
    margin: 0;
    padding: var(--sp-1) var(--sp-2);
    border: 1px solid var(--danger-border);
    background: var(--danger-bg);
    border-radius: var(--radius-sm);
    font-size: 11px;
    color: var(--danger-fg);
  }
  main {
    position: absolute;
    inset: 0;
    display: flex;
    flex-direction: column;
  }
  .canvas {
    position: relative;
    flex: 1;
    min-height: 0;
  }
  .drawer-toggle {
    position: absolute;
    z-index: 8;
    left: 50%;
    bottom: var(--sp-3);
    transform: translateX(-50%);
    padding: var(--sp-0) var(--sp-3);
    background: var(--glass);
    backdrop-filter: blur(12px);
    border-color: var(--border);
    color: var(--muted);
    font-size: 11px;
  }
  .banner {
    display: flex;
    align-items: center;
    gap: var(--sp-3);
    padding: var(--sp-2) var(--sp-3);
    border-bottom: 1px solid var(--warn-border);
    background: var(--warn-bg);
    color: var(--fg);
    font-size: 12px;
  }
  .banner button {
    margin-left: auto;
    flex: none;
  }
  .empty {
    position: absolute;
    z-index: 6;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    width: min(420px, calc(100% - 2 * var(--sp-4)));
    padding: var(--sp-4);
    display: flex;
    flex-direction: column;
    gap: var(--sp-2);
    background: var(--glass);
    backdrop-filter: blur(12px);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    box-shadow: var(--shadow);
  }
  .empty h2 {
    margin: 0;
    font-size: 14px;
    font-weight: 600;
    color: var(--fg);
    text-transform: none;
    letter-spacing: 0;
  }
  .empty p {
    margin: 0;
    font-size: 12px;
    color: var(--muted);
  }
  .choices {
    display: flex;
    flex-direction: column;
    gap: var(--sp-2);
    margin-top: var(--sp-2);
  }
  .browse {
    display: flex;
    align-items: center;
    gap: var(--sp-2);
    white-space: nowrap;
    font-size: 11px;
    color: var(--muted);
  }
  .dialog {
    position: absolute;
    top: 60px;
    left: 50%;
    transform: translateX(-50%);
    z-index: 25;
    width: min(560px, calc(100% - 2 * var(--sp-4)));
    max-height: 60%;
    overflow-y: auto;
    padding: var(--sp-3);
    background: var(--glass);
    backdrop-filter: blur(12px);
    border: 1px solid var(--danger-border);
    border-radius: var(--radius);
    box-shadow: var(--shadow);
  }
  .dialog h2 {
    margin: 0 0 var(--sp-2);
    font-size: 14px;
    font-weight: 600;
    color: var(--danger-fg);
    text-transform: none;
    letter-spacing: 0;
  }
  .dialog p {
    margin: 0 0 var(--sp-2);
    font-size: 12px;
    color: var(--fg);
  }
  .dialog ul {
    margin: 0;
    padding: 0;
    list-style: none;
    display: flex;
    flex-direction: column;
    gap: 0;
  }
  .dialog ul button {
    display: flex;
    gap: var(--sp-3);
    width: 100%;
    padding: var(--sp-0) var(--sp-2);
    background: transparent;
    border-color: transparent;
    text-align: left;
  }
  .dialog ul button:hover {
    background: var(--bg-2);
    border-color: transparent;
  }
  .dialog .line {
    flex: none;
    width: 62px;
    font-family: var(--mono);
    font-size: 11px;
    color: var(--muted);
  }
  .dialog code {
    font-family: var(--mono);
    font-size: 11px;
    color: var(--fg);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .dialog footer {
    display: flex;
    justify-content: flex-end;
    margin-top: var(--sp-3);
  }
  @media (prefers-reduced-motion: reduce) {
    .sidebar,
    .body {
      transition: none;
    }
  }

</style>
